/*
 *  qemu user main
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu-version.h"
#include <sys/syscall.h>
#include <sys/resource.h>

#include "qapi/error.h"
#include "qemu.h"
#include "qemu/path.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/help_option.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg.h"
#include "qemu/timer.h"
#include "qemu/envlist.h"
#include "elf.h"
#include "trace/control.h"
#include "target_elf.h"
#include "cpu_loop-common.h"

char *exec_path;

int singlestep;
static const char *filename;
static const char *argv0;
static int gdbstub_port;
static envlist_t *envlist;
static const char *cpu_model;
static const char *cpu_type;
unsigned long mmap_min_addr;
unsigned long guest_base;
int have_guest_base;

/*
 * When running 32-on-64 we should make sure we can fit all of the possible
 * guest address space into a contiguous chunk of virtual host memory.
 *
 * This way we will never overlap with our own libraries or binaries or stack
 * or anything else that QEMU maps.
 *
 * Many cpus reserve the high bit (or more than one for some 64-bit cpus)
 * of the address for the kernel.  Some cpus rely on this and user space
 * uses the high bit(s) for pointer tagging and the like.  For them, we
 * must preserve the expected address space.
 */
#ifndef MAX_RESERVED_VA
# if HOST_LONG_BITS > TARGET_VIRT_ADDR_SPACE_BITS
#  if TARGET_VIRT_ADDR_SPACE_BITS == 32 && \
      (TARGET_LONG_BITS == 32 || defined(TARGET_ABI32))
/* There are a number of places where we assign reserved_va to a variable
   of type abi_ulong and expect it to fit.  Avoid the last page.  */
#   define MAX_RESERVED_VA  (0xfffffffful & TARGET_PAGE_MASK)
#  else
#   define MAX_RESERVED_VA  (1ul << TARGET_VIRT_ADDR_SPACE_BITS)
#  endif
# else
#  define MAX_RESERVED_VA  0
# endif
#endif

/* That said, reserving *too* much vm space via mmap can run into problems
   with rlimits, oom due to page table creation, etc.  We will still try it,
   if directed by the command-line option, but not by default.  */
#if HOST_LONG_BITS == 64 && TARGET_VIRT_ADDR_SPACE_BITS <= 32
unsigned long reserved_va = MAX_RESERVED_VA;
#else
unsigned long reserved_va;
#endif

static void usage(int exitcode);

static const char *interp_prefix = CONFIG_QEMU_INTERP_PREFIX;
const char *qemu_uname_release;

/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
unsigned long guest_stack_size = 8 * 1024 * 1024UL;

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

#if defined(TARGET_I386)
int cpu_get_pic_interrupt(CPUX86State *env)
{
    return -1;
}
#endif

/***********************************************************/
/* Helper routines for implementing atomic operations.  */

/* Make sure everything is in a consistent state for calling fork().  */
void fork_start(void)
{
    start_exclusive();
    mmap_fork_start();
    qemu_mutex_lock(&tb_ctx.tb_lock);
    cpu_list_lock();
}

void fork_end(int child)
{
    mmap_fork_end(child);
    if (child) {
        CPUState *cpu, *next_cpu;
        /* Child processes created by fork() only have a single thread.
           Discard information about the parent threads.  */
        CPU_FOREACH_SAFE(cpu, next_cpu) {
            if (cpu != thread_cpu) {
                QTAILQ_REMOVE(&cpus, cpu, node);
            }
        }
        qemu_mutex_init(&tb_ctx.tb_lock);
        qemu_init_cpu_list();
        gdbserver_fork(thread_cpu);
        /* qemu_init_cpu_list() takes care of reinitializing the
         * exclusive state, so we don't need to end_exclusive() here.
         */
    } else {
        qemu_mutex_unlock(&tb_ctx.tb_lock);
        cpu_list_unlock();
        end_exclusive();
    }
}

#ifdef TARGET_CRIS
void cpu_loop(CPUCRISState *env)
{
    CPUState *cs = CPU(cris_env_get_cpu(env));
    int trapnr, ret;
    target_siginfo_t info;
    
    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case 0xaa:
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->pregs[PR_EDA];
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
	case EXCP_INTERRUPT:
	  /* just indicate that signals should be handled asap */
	  break;
        case EXCP_BREAK:
            ret = do_syscall(env, 
                             env->regs[9], 
                             env->regs[10], 
                             env->regs[11], 
                             env->regs[12], 
                             env->regs[13], 
                             env->pregs[7], 
                             env->pregs[11],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->pc -= 2;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[10] = ret;
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig(cs, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                  }
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, fprintf, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);
    }
}
#endif

#ifdef TARGET_MICROBLAZE
void cpu_loop(CPUMBState *env)
{
    CPUState *cs = CPU(mb_env_get_cpu(env));
    int trapnr, ret;
    target_siginfo_t info;
    
    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case 0xaa:
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = 0;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
	case EXCP_INTERRUPT:
	  /* just indicate that signals should be handled asap */
	  break;
        case EXCP_BREAK:
            /* Return address is 4 bytes after the call.  */
            env->regs[14] += 4;
            env->sregs[SR_PC] = env->regs[14];
            ret = do_syscall(env, 
                             env->regs[12], 
                             env->regs[5], 
                             env->regs[6], 
                             env->regs[7], 
                             env->regs[8], 
                             env->regs[9], 
                             env->regs[10],
                             0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                /* Wind back to before the syscall. */
                env->sregs[SR_PC] -= 4;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[3] = ret;
            }
            /* All syscall exits result in guest r14 being equal to the
             * PC we return to, because the kernel syscall exit "rtbd" does
             * this. (This is true even for sigreturn(); note that r14 is
             * not a userspace-usable register, as the kernel may clobber it
             * at any point.)
             */
            env->regs[14] = env->sregs[SR_PC];
            break;
        case EXCP_HW_EXCP:
            env->regs[17] = env->sregs[SR_PC] + 4;
            if (env->iflags & D_FLAG) {
                env->sregs[SR_ESR] |= 1 << 12;
                env->sregs[SR_PC] -= 4;
                /* FIXME: if branch was immed, replay the imm as well.  */
            }

            env->iflags &= ~(IMM_FLAG | D_FLAG);

            switch (env->sregs[SR_ESR] & 31) {
                case ESR_EC_DIVZERO:
                    info.si_signo = TARGET_SIGFPE;
                    info.si_errno = 0;
                    info.si_code = TARGET_FPE_FLTDIV;
                    info._sifields._sigfault._addr = 0;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                    break;
                case ESR_EC_FPU:
                    info.si_signo = TARGET_SIGFPE;
                    info.si_errno = 0;
                    if (env->sregs[SR_FSR] & FSR_IO) {
                        info.si_code = TARGET_FPE_FLTINV;
                    }
                    if (env->sregs[SR_FSR] & FSR_DZ) {
                        info.si_code = TARGET_FPE_FLTDIV;
                    }
                    info._sifields._sigfault._addr = 0;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                    break;
                default:
                    printf ("Unhandled hw-exception: 0x%x\n",
                            env->sregs[SR_ESR] & ESR_EC_MASK);
                    cpu_dump_state(cs, stderr, fprintf, 0);
                    exit(EXIT_FAILURE);
                    break;
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig(cs, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                  }
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, fprintf, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);
    }
}
#endif

#ifdef TARGET_M68K

void cpu_loop(CPUM68KState *env)
{
    CPUState *cs = CPU(m68k_env_get_cpu(env));
    int trapnr;
    unsigned int n;
    target_siginfo_t info;
    TaskState *ts = cs->opaque;

    for(;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch(trapnr) {
        case EXCP_ILLEGAL:
            {
                if (ts->sim_syscalls) {
                    uint16_t nr;
                    get_user_u16(nr, env->pc + 2);
                    env->pc += 4;
                    do_m68k_simcall(env, nr);
                } else {
                    goto do_sigill;
                }
            }
            break;
        case EXCP_HALT_INSN:
            /* Semihosing syscall.  */
            env->pc += 4;
            do_m68k_semihosting(env, env->dregs[0]);
            break;
        case EXCP_LINEA:
        case EXCP_LINEF:
        case EXCP_UNSUPPORTED:
        do_sigill:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_CHK:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = TARGET_FPE_INTOVF;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_DIV0:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = TARGET_FPE_INTDIV;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_TRAP0:
            {
                abi_long ret;
                ts->sim_syscalls = 0;
                n = env->dregs[0];
                env->pc += 2;
                ret = do_syscall(env,
                                 n,
                                 env->dregs[1],
                                 env->dregs[2],
                                 env->dregs[3],
                                 env->dregs[4],
                                 env->dregs[5],
                                 env->aregs[0],
                                 0, 0);
                if (ret == -TARGET_ERESTARTSYS) {
                    env->pc -= 2;
                } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                    env->dregs[0] = ret;
                }
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ACCESS:
            {
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->mmu.ar;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig(cs, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                  }
            }
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            EXCP_DUMP(env, "qemu: unhandled CPU exception 0x%x - aborting\n", trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif /* TARGET_M68K */

#ifdef TARGET_ALPHA
void cpu_loop(CPUAlphaState *env)
{
    CPUState *cs = CPU(alpha_env_get_cpu(env));
    int trapnr;
    target_siginfo_t info;
    abi_long sysret;

    while (1) {
        bool arch_interrupt = true;

        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_RESET:
            fprintf(stderr, "Reset requested. Exit\n");
            exit(EXIT_FAILURE);
            break;
        case EXCP_MCHK:
            fprintf(stderr, "Machine check exception. Exit\n");
            exit(EXIT_FAILURE);
            break;
        case EXCP_SMP_INTERRUPT:
        case EXCP_CLK_INTERRUPT:
        case EXCP_DEV_INTERRUPT:
            fprintf(stderr, "External interrupt. Exit\n");
            exit(EXIT_FAILURE);
            break;
        case EXCP_MMFAULT:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            info.si_code = (page_get_flags(env->trap_arg0) & PAGE_VALID
                            ? TARGET_SEGV_ACCERR : TARGET_SEGV_MAPERR);
            info._sifields._sigfault._addr = env->trap_arg0;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_UNALIGN:
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_BUS_ADRALN;
            info._sifields._sigfault._addr = env->trap_arg0;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_OPCDEC:
        do_sigill:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPC;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_ARITH:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = TARGET_FPE_FLTINV;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_FEN:
            /* No-op.  Linux simply re-enables the FPU.  */
            break;
        case EXCP_CALL_PAL:
            switch (env->error_code) {
            case 0x80:
                /* BPT */
                info.si_signo = TARGET_SIGTRAP;
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                info._sifields._sigfault._addr = env->pc;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                break;
            case 0x81:
                /* BUGCHK */
                info.si_signo = TARGET_SIGTRAP;
                info.si_errno = 0;
                info.si_code = 0;
                info._sifields._sigfault._addr = env->pc;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                break;
            case 0x83:
                /* CALLSYS */
                trapnr = env->ir[IR_V0];
                sysret = do_syscall(env, trapnr,
                                    env->ir[IR_A0], env->ir[IR_A1],
                                    env->ir[IR_A2], env->ir[IR_A3],
                                    env->ir[IR_A4], env->ir[IR_A5],
                                    0, 0);
                if (sysret == -TARGET_ERESTARTSYS) {
                    env->pc -= 4;
                    break;
                }
                if (sysret == -TARGET_QEMU_ESIGRETURN) {
                    break;
                }
                /* Syscall writes 0 to V0 to bypass error check, similar
                   to how this is handled internal to Linux kernel.
                   (Ab)use trapnr temporarily as boolean indicating error.  */
                trapnr = (env->ir[IR_V0] != 0 && sysret < 0);
                env->ir[IR_V0] = (trapnr ? -sysret : sysret);
                env->ir[IR_A3] = trapnr;
                break;
            case 0x86:
                /* IMB */
                /* ??? We can probably elide the code using page_unprotect
                   that is checking for self-modifying code.  Instead we
                   could simply call tb_flush here.  Until we work out the
                   changes required to turn off the extra write protection,
                   this can be a no-op.  */
                break;
            case 0x9E:
                /* RDUNIQUE */
                /* Handled in the translator for usermode.  */
                abort();
            case 0x9F:
                /* WRUNIQUE */
                /* Handled in the translator for usermode.  */
                abort();
            case 0xAA:
                /* GENTRAP */
                info.si_signo = TARGET_SIGFPE;
                switch (env->ir[IR_A0]) {
                case TARGET_GEN_INTOVF:
                    info.si_code = TARGET_FPE_INTOVF;
                    break;
                case TARGET_GEN_INTDIV:
                    info.si_code = TARGET_FPE_INTDIV;
                    break;
                case TARGET_GEN_FLTOVF:
                    info.si_code = TARGET_FPE_FLTOVF;
                    break;
                case TARGET_GEN_FLTUND:
                    info.si_code = TARGET_FPE_FLTUND;
                    break;
                case TARGET_GEN_FLTINV:
                    info.si_code = TARGET_FPE_FLTINV;
                    break;
                case TARGET_GEN_FLTINE:
                    info.si_code = TARGET_FPE_FLTRES;
                    break;
                case TARGET_GEN_ROPRAND:
                    info.si_code = 0;
                    break;
                default:
                    info.si_signo = TARGET_SIGTRAP;
                    info.si_code = 0;
                    break;
                }
                info.si_errno = 0;
                info._sifields._sigfault._addr = env->pc;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                break;
            default:
                goto do_sigill;
            }
            break;
        case EXCP_DEBUG:
            info.si_signo = gdb_handlesig(cs, TARGET_SIGTRAP);
            if (info.si_signo) {
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            } else {
                arch_interrupt = false;
            }
            break;
        case EXCP_INTERRUPT:
            /* Just indicate that signals should be handled asap.  */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            arch_interrupt = false;
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, fprintf, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);

        /* Most of the traps imply a transition through PALcode, which
           implies an REI instruction has been executed.  Which means
           that RX and LOCK_ADDR should be cleared.  But there are a
           few exceptions for traps internal to QEMU.  */
        if (arch_interrupt) {
            env->flags &= ~ENV_FLAG_RX_FLAG;
            env->lock_addr = -1;
        }
    }
}
#endif /* TARGET_ALPHA */

#ifdef TARGET_S390X

/* s390x masks the fault address it reports in si_addr for SIGSEGV and SIGBUS */
#define S390X_FAIL_ADDR_MASK -4096LL

void cpu_loop(CPUS390XState *env)
{
    CPUState *cs = CPU(s390_env_get_cpu(env));
    int trapnr, n, sig;
    target_siginfo_t info;
    target_ulong addr;
    abi_long ret;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* Just indicate that signals should be handled asap.  */
            break;

        case EXCP_SVC:
            n = env->int_svc_code;
            if (!n) {
                /* syscalls > 255 */
                n = env->regs[1];
            }
            env->psw.addr += env->int_svc_ilen;
            ret = do_syscall(env, n, env->regs[2], env->regs[3],
                             env->regs[4], env->regs[5],
                             env->regs[6], env->regs[7], 0, 0);
            if (ret == -TARGET_ERESTARTSYS) {
                env->psw.addr -= env->int_svc_ilen;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[2] = ret;
            }
            break;

        case EXCP_DEBUG:
            sig = gdb_handlesig(cs, TARGET_SIGTRAP);
            if (sig) {
                n = TARGET_TRAP_BRKPT;
                goto do_signal_pc;
            }
            break;
        case EXCP_PGM:
            n = env->int_pgm_code;
            switch (n) {
            case PGM_OPERATION:
            case PGM_PRIVILEGED:
                sig = TARGET_SIGILL;
                n = TARGET_ILL_ILLOPC;
                goto do_signal_pc;
            case PGM_PROTECTION:
            case PGM_ADDRESSING:
                sig = TARGET_SIGSEGV;
                /* XXX: check env->error_code */
                n = TARGET_SEGV_MAPERR;
                addr = env->__excp_addr & S390X_FAIL_ADDR_MASK;
                goto do_signal;
            case PGM_EXECUTE:
            case PGM_SPECIFICATION:
            case PGM_SPECIAL_OP:
            case PGM_OPERAND:
            do_sigill_opn:
                sig = TARGET_SIGILL;
                n = TARGET_ILL_ILLOPN;
                goto do_signal_pc;

            case PGM_FIXPT_OVERFLOW:
                sig = TARGET_SIGFPE;
                n = TARGET_FPE_INTOVF;
                goto do_signal_pc;
            case PGM_FIXPT_DIVIDE:
                sig = TARGET_SIGFPE;
                n = TARGET_FPE_INTDIV;
                goto do_signal_pc;

            case PGM_DATA:
                n = (env->fpc >> 8) & 0xff;
                if (n == 0xff) {
                    /* compare-and-trap */
                    goto do_sigill_opn;
                } else {
                    /* An IEEE exception, simulated or otherwise.  */
                    if (n & 0x80) {
                        n = TARGET_FPE_FLTINV;
                    } else if (n & 0x40) {
                        n = TARGET_FPE_FLTDIV;
                    } else if (n & 0x20) {
                        n = TARGET_FPE_FLTOVF;
                    } else if (n & 0x10) {
                        n = TARGET_FPE_FLTUND;
                    } else if (n & 0x08) {
                        n = TARGET_FPE_FLTRES;
                    } else {
                        /* ??? Quantum exception; BFP, DFP error.  */
                        goto do_sigill_opn;
                    }
                    sig = TARGET_SIGFPE;
                    goto do_signal_pc;
                }

            default:
                fprintf(stderr, "Unhandled program exception: %#x\n", n);
                cpu_dump_state(cs, stderr, fprintf, 0);
                exit(EXIT_FAILURE);
            }
            break;

        do_signal_pc:
            addr = env->psw.addr;
        do_signal:
            info.si_signo = sig;
            info.si_errno = 0;
            info.si_code = n;
            info._sifields._sigfault._addr = addr;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;

        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            fprintf(stderr, "Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(cs, stderr, fprintf, 0);
            exit(EXIT_FAILURE);
        }
        process_pending_signals (env);
    }
}

#endif /* TARGET_S390X */

#ifdef TARGET_TILEGX

static void gen_sigill_reg(CPUTLGState *env)
{
    target_siginfo_t info;

    info.si_signo = TARGET_SIGILL;
    info.si_errno = 0;
    info.si_code = TARGET_ILL_PRVREG;
    info._sifields._sigfault._addr = env->pc;
    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
}

static void do_signal(CPUTLGState *env, int signo, int sigcode)
{
    target_siginfo_t info;

    info.si_signo = signo;
    info.si_errno = 0;
    info._sifields._sigfault._addr = env->pc;

    if (signo == TARGET_SIGSEGV) {
        /* The passed in sigcode is a dummy; check for a page mapping
           and pass either MAPERR or ACCERR.  */
        target_ulong addr = env->excaddr;
        info._sifields._sigfault._addr = addr;
        if (page_check_range(addr, 1, PAGE_VALID) < 0) {
            sigcode = TARGET_SEGV_MAPERR;
        } else {
            sigcode = TARGET_SEGV_ACCERR;
        }
    }
    info.si_code = sigcode;

    queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
}

static void gen_sigsegv_maperr(CPUTLGState *env, target_ulong addr)
{
    env->excaddr = addr;
    do_signal(env, TARGET_SIGSEGV, 0);
}

static void set_regval(CPUTLGState *env, uint8_t reg, uint64_t val)
{
    if (unlikely(reg >= TILEGX_R_COUNT)) {
        switch (reg) {
        case TILEGX_R_SN:
        case TILEGX_R_ZERO:
            return;
        case TILEGX_R_IDN0:
        case TILEGX_R_IDN1:
        case TILEGX_R_UDN0:
        case TILEGX_R_UDN1:
        case TILEGX_R_UDN2:
        case TILEGX_R_UDN3:
            gen_sigill_reg(env);
            return;
        default:
            g_assert_not_reached();
        }
    }
    env->regs[reg] = val;
}

/*
 * Compare the 8-byte contents of the CmpValue SPR with the 8-byte value in
 * memory at the address held in the first source register. If the values are
 * not equal, then no memory operation is performed. If the values are equal,
 * the 8-byte quantity from the second source register is written into memory
 * at the address held in the first source register. In either case, the result
 * of the instruction is the value read from memory. The compare and write to
 * memory are atomic and thus can be used for synchronization purposes. This
 * instruction only operates for addresses aligned to a 8-byte boundary.
 * Unaligned memory access causes an Unaligned Data Reference interrupt.
 *
 * Functional Description (64-bit)
 *       uint64_t memVal = memoryReadDoubleWord (rf[SrcA]);
 *       rf[Dest] = memVal;
 *       if (memVal == SPR[CmpValueSPR])
 *           memoryWriteDoubleWord (rf[SrcA], rf[SrcB]);
 *
 * Functional Description (32-bit)
 *       uint64_t memVal = signExtend32 (memoryReadWord (rf[SrcA]));
 *       rf[Dest] = memVal;
 *       if (memVal == signExtend32 (SPR[CmpValueSPR]))
 *           memoryWriteWord (rf[SrcA], rf[SrcB]);
 *
 *
 * This function also processes exch and exch4 which need not process SPR.
 */
static void do_exch(CPUTLGState *env, bool quad, bool cmp)
{
    target_ulong addr;
    target_long val, sprval;

    start_exclusive();

    addr = env->atomic_srca;
    if (quad ? get_user_s64(val, addr) : get_user_s32(val, addr)) {
        goto sigsegv_maperr;
    }

    if (cmp) {
        if (quad) {
            sprval = env->spregs[TILEGX_SPR_CMPEXCH];
        } else {
            sprval = sextract64(env->spregs[TILEGX_SPR_CMPEXCH], 0, 32);
        }
    }

    if (!cmp || val == sprval) {
        target_long valb = env->atomic_srcb;
        if (quad ? put_user_u64(valb, addr) : put_user_u32(valb, addr)) {
            goto sigsegv_maperr;
        }
    }

    set_regval(env, env->atomic_dstr, val);
    end_exclusive();
    return;

 sigsegv_maperr:
    end_exclusive();
    gen_sigsegv_maperr(env, addr);
}

static void do_fetch(CPUTLGState *env, int trapnr, bool quad)
{
    int8_t write = 1;
    target_ulong addr;
    target_long val, valb;

    start_exclusive();

    addr = env->atomic_srca;
    valb = env->atomic_srcb;
    if (quad ? get_user_s64(val, addr) : get_user_s32(val, addr)) {
        goto sigsegv_maperr;
    }

    switch (trapnr) {
    case TILEGX_EXCP_OPCODE_FETCHADD:
    case TILEGX_EXCP_OPCODE_FETCHADD4:
        valb += val;
        break;
    case TILEGX_EXCP_OPCODE_FETCHADDGEZ:
        valb += val;
        if (valb < 0) {
            write = 0;
        }
        break;
    case TILEGX_EXCP_OPCODE_FETCHADDGEZ4:
        valb += val;
        if ((int32_t)valb < 0) {
            write = 0;
        }
        break;
    case TILEGX_EXCP_OPCODE_FETCHAND:
    case TILEGX_EXCP_OPCODE_FETCHAND4:
        valb &= val;
        break;
    case TILEGX_EXCP_OPCODE_FETCHOR:
    case TILEGX_EXCP_OPCODE_FETCHOR4:
        valb |= val;
        break;
    default:
        g_assert_not_reached();
    }

    if (write) {
        if (quad ? put_user_u64(valb, addr) : put_user_u32(valb, addr)) {
            goto sigsegv_maperr;
        }
    }

    set_regval(env, env->atomic_dstr, val);
    end_exclusive();
    return;

 sigsegv_maperr:
    end_exclusive();
    gen_sigsegv_maperr(env, addr);
}

void cpu_loop(CPUTLGState *env)
{
    CPUState *cs = CPU(tilegx_env_get_cpu(env));
    int trapnr;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case TILEGX_EXCP_SYSCALL:
        {
            abi_ulong ret = do_syscall(env, env->regs[TILEGX_R_NR],
                                       env->regs[0], env->regs[1],
                                       env->regs[2], env->regs[3],
                                       env->regs[4], env->regs[5],
                                       env->regs[6], env->regs[7]);
            if (ret == -TARGET_ERESTARTSYS) {
                env->pc -= 8;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->regs[TILEGX_R_RE] = ret;
                env->regs[TILEGX_R_ERR] = TILEGX_IS_ERRNO(ret) ? -ret : 0;
            }
            break;
        }
        case TILEGX_EXCP_OPCODE_EXCH:
            do_exch(env, true, false);
            break;
        case TILEGX_EXCP_OPCODE_EXCH4:
            do_exch(env, false, false);
            break;
        case TILEGX_EXCP_OPCODE_CMPEXCH:
            do_exch(env, true, true);
            break;
        case TILEGX_EXCP_OPCODE_CMPEXCH4:
            do_exch(env, false, true);
            break;
        case TILEGX_EXCP_OPCODE_FETCHADD:
        case TILEGX_EXCP_OPCODE_FETCHADDGEZ:
        case TILEGX_EXCP_OPCODE_FETCHAND:
        case TILEGX_EXCP_OPCODE_FETCHOR:
            do_fetch(env, trapnr, true);
            break;
        case TILEGX_EXCP_OPCODE_FETCHADD4:
        case TILEGX_EXCP_OPCODE_FETCHADDGEZ4:
        case TILEGX_EXCP_OPCODE_FETCHAND4:
        case TILEGX_EXCP_OPCODE_FETCHOR4:
            do_fetch(env, trapnr, false);
            break;
        case TILEGX_EXCP_SIGNAL:
            do_signal(env, env->signo, env->sigcode);
            break;
        case TILEGX_EXCP_REG_IDN_ACCESS:
        case TILEGX_EXCP_REG_UDN_ACCESS:
            gen_sigill_reg(env);
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        default:
            fprintf(stderr, "trapnr is %d[0x%x].\n", trapnr, trapnr);
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}

#endif

#ifdef TARGET_RISCV

void cpu_loop(CPURISCVState *env)
{
    CPUState *cs = CPU(riscv_env_get_cpu(env));
    int trapnr, signum, sigcode;
    target_ulong sigaddr;
    target_ulong ret;

    for (;;) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        signum = 0;
        sigcode = 0;
        sigaddr = 0;

        switch (trapnr) {
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ATOMIC:
            cpu_exec_step_atomic(cs);
            break;
        case RISCV_EXCP_U_ECALL:
            env->pc += 4;
            if (env->gpr[xA7] == TARGET_NR_arch_specific_syscall + 15) {
                /* riscv_flush_icache_syscall is a no-op in QEMU as
                   self-modifying code is automatically detected */
                ret = 0;
            } else {
                ret = do_syscall(env,
                                 env->gpr[xA7],
                                 env->gpr[xA0],
                                 env->gpr[xA1],
                                 env->gpr[xA2],
                                 env->gpr[xA3],
                                 env->gpr[xA4],
                                 env->gpr[xA5],
                                 0, 0);
            }
            if (ret == -TARGET_ERESTARTSYS) {
                env->pc -= 4;
            } else if (ret != -TARGET_QEMU_ESIGRETURN) {
                env->gpr[xA0] = ret;
            }
            if (cs->singlestep_enabled) {
                goto gdbstep;
            }
            break;
        case RISCV_EXCP_ILLEGAL_INST:
            signum = TARGET_SIGILL;
            sigcode = TARGET_ILL_ILLOPC;
            break;
        case RISCV_EXCP_BREAKPOINT:
            signum = TARGET_SIGTRAP;
            sigcode = TARGET_TRAP_BRKPT;
            sigaddr = env->pc;
            break;
        case RISCV_EXCP_INST_PAGE_FAULT:
        case RISCV_EXCP_LOAD_PAGE_FAULT:
        case RISCV_EXCP_STORE_PAGE_FAULT:
            signum = TARGET_SIGSEGV;
            sigcode = TARGET_SEGV_MAPERR;
            break;
        case EXCP_DEBUG:
        gdbstep:
            signum = gdb_handlesig(cs, TARGET_SIGTRAP);
            sigcode = TARGET_TRAP_BRKPT;
            break;
        default:
            EXCP_DUMP(env, "\nqemu: unhandled CPU exception %#x - aborting\n",
                     trapnr);
            exit(EXIT_FAILURE);
        }

        if (signum) {
            target_siginfo_t info = {
                .si_signo = signum,
                .si_errno = 0,
                .si_code = sigcode,
                ._sifields._sigfault._addr = sigaddr
            };
            queue_signal(env, info.si_signo, QEMU_SI_KILL, &info);
        }

        process_pending_signals(env);
    }
}

#endif /* TARGET_RISCV */

#ifdef TARGET_HPPA

static abi_ulong hppa_lws(CPUHPPAState *env)
{
    uint32_t which = env->gr[20];
    abi_ulong addr = env->gr[26];
    abi_ulong old = env->gr[25];
    abi_ulong new = env->gr[24];
    abi_ulong size, ret;

    switch (which) {
    default:
        return -TARGET_ENOSYS;

    case 0: /* elf32 atomic 32bit cmpxchg */
        if ((addr & 3) || !access_ok(VERIFY_WRITE, addr, 4)) {
            return -TARGET_EFAULT;
        }
        old = tswap32(old);
        new = tswap32(new);
        ret = atomic_cmpxchg((uint32_t *)g2h(addr), old, new);
        ret = tswap32(ret);
        break;

    case 2: /* elf32 atomic "new" cmpxchg */
        size = env->gr[23];
        if (size >= 4) {
            return -TARGET_ENOSYS;
        }
        if (((addr | old | new) & ((1 << size) - 1))
            || !access_ok(VERIFY_WRITE, addr, 1 << size)
            || !access_ok(VERIFY_READ, old, 1 << size)
            || !access_ok(VERIFY_READ, new, 1 << size)) {
            return -TARGET_EFAULT;
        }
        /* Note that below we use host-endian loads so that the cmpxchg
           can be host-endian as well.  */
        switch (size) {
        case 0:
            old = *(uint8_t *)g2h(old);
            new = *(uint8_t *)g2h(new);
            ret = atomic_cmpxchg((uint8_t *)g2h(addr), old, new);
            ret = ret != old;
            break;
        case 1:
            old = *(uint16_t *)g2h(old);
            new = *(uint16_t *)g2h(new);
            ret = atomic_cmpxchg((uint16_t *)g2h(addr), old, new);
            ret = ret != old;
            break;
        case 2:
            old = *(uint32_t *)g2h(old);
            new = *(uint32_t *)g2h(new);
            ret = atomic_cmpxchg((uint32_t *)g2h(addr), old, new);
            ret = ret != old;
            break;
        case 3:
            {
                uint64_t o64, n64, r64;
                o64 = *(uint64_t *)g2h(old);
                n64 = *(uint64_t *)g2h(new);
#ifdef CONFIG_ATOMIC64
                r64 = atomic_cmpxchg__nocheck((uint64_t *)g2h(addr), o64, n64);
                ret = r64 != o64;
#else
                start_exclusive();
                r64 = *(uint64_t *)g2h(addr);
                ret = 1;
                if (r64 == o64) {
                    *(uint64_t *)g2h(addr) = n64;
                    ret = 0;
                }
                end_exclusive();
#endif
            }
            break;
        }
        break;
    }

    env->gr[28] = ret;
    return 0;
}

void cpu_loop(CPUHPPAState *env)
{
    CPUState *cs = CPU(hppa_env_get_cpu(env));
    target_siginfo_t info;
    abi_ulong ret;
    int trapnr;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        switch (trapnr) {
        case EXCP_SYSCALL:
            ret = do_syscall(env, env->gr[20],
                             env->gr[26], env->gr[25],
                             env->gr[24], env->gr[23],
                             env->gr[22], env->gr[21], 0, 0);
            switch (ret) {
            default:
                env->gr[28] = ret;
                /* We arrived here by faking the gateway page.  Return.  */
                env->iaoq_f = env->gr[31];
                env->iaoq_b = env->gr[31] + 4;
                break;
            case -TARGET_ERESTARTSYS:
            case -TARGET_QEMU_ESIGRETURN:
                break;
            }
            break;
        case EXCP_SYSCALL_LWS:
            env->gr[21] = hppa_lws(env);
            /* We arrived here by faking the gateway page.  Return.  */
            env->iaoq_f = env->gr[31];
            env->iaoq_b = env->gr[31] + 4;
            break;
        case EXCP_ITLB_MISS:
        case EXCP_DTLB_MISS:
        case EXCP_NA_ITLB_MISS:
        case EXCP_NA_DTLB_MISS:
        case EXCP_IMP:
        case EXCP_DMP:
        case EXCP_DMB:
        case EXCP_PAGE_REF:
        case EXCP_DMAR:
        case EXCP_DMPI:
            info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
            info.si_code = TARGET_SEGV_ACCERR;
            info._sifields._sigfault._addr = env->cr[CR_IOR];
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_UNALIGN:
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = 0;
            info._sifields._sigfault._addr = env->cr[CR_IOR];
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_ILL:
        case EXCP_PRIV_OPR:
        case EXCP_PRIV_REG:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->iaoq_f;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_OVERFLOW:
        case EXCP_COND:
        case EXCP_ASSIST:
            info.si_signo = TARGET_SIGFPE;
            info.si_errno = 0;
            info.si_code = 0;
            info._sifields._sigfault._addr = env->iaoq_f;
            queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
            break;
        case EXCP_DEBUG:
            trapnr = gdb_handlesig(cs, TARGET_SIGTRAP);
            if (trapnr) {
                info.si_signo = trapnr;
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                queue_signal(env, trapnr, QEMU_SI_FAULT, &info);
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        default:
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}

#endif /* TARGET_HPPA */

#ifdef TARGET_XTENSA

static void xtensa_rfw(CPUXtensaState *env)
{
    xtensa_restore_owb(env);
    env->pc = env->sregs[EPC1];
}

static void xtensa_rfwu(CPUXtensaState *env)
{
    env->sregs[WINDOW_START] |= (1 << env->sregs[WINDOW_BASE]);
    xtensa_rfw(env);
}

static void xtensa_rfwo(CPUXtensaState *env)
{
    env->sregs[WINDOW_START] &= ~(1 << env->sregs[WINDOW_BASE]);
    xtensa_rfw(env);
}

static void xtensa_overflow4(CPUXtensaState *env)
{
    put_user_ual(env->regs[0], env->regs[5] - 16);
    put_user_ual(env->regs[1], env->regs[5] - 12);
    put_user_ual(env->regs[2], env->regs[5] -  8);
    put_user_ual(env->regs[3], env->regs[5] -  4);
    xtensa_rfwo(env);
}

static void xtensa_underflow4(CPUXtensaState *env)
{
    get_user_ual(env->regs[0], env->regs[5] - 16);
    get_user_ual(env->regs[1], env->regs[5] - 12);
    get_user_ual(env->regs[2], env->regs[5] -  8);
    get_user_ual(env->regs[3], env->regs[5] -  4);
    xtensa_rfwu(env);
}

static void xtensa_overflow8(CPUXtensaState *env)
{
    put_user_ual(env->regs[0], env->regs[9] - 16);
    get_user_ual(env->regs[0], env->regs[1] - 12);
    put_user_ual(env->regs[1], env->regs[9] - 12);
    put_user_ual(env->regs[2], env->regs[9] -  8);
    put_user_ual(env->regs[3], env->regs[9] -  4);
    put_user_ual(env->regs[4], env->regs[0] - 32);
    put_user_ual(env->regs[5], env->regs[0] - 28);
    put_user_ual(env->regs[6], env->regs[0] - 24);
    put_user_ual(env->regs[7], env->regs[0] - 20);
    xtensa_rfwo(env);
}

static void xtensa_underflow8(CPUXtensaState *env)
{
    get_user_ual(env->regs[0], env->regs[9] - 16);
    get_user_ual(env->regs[1], env->regs[9] - 12);
    get_user_ual(env->regs[2], env->regs[9] -  8);
    get_user_ual(env->regs[7], env->regs[1] - 12);
    get_user_ual(env->regs[3], env->regs[9] -  4);
    get_user_ual(env->regs[4], env->regs[7] - 32);
    get_user_ual(env->regs[5], env->regs[7] - 28);
    get_user_ual(env->regs[6], env->regs[7] - 24);
    get_user_ual(env->regs[7], env->regs[7] - 20);
    xtensa_rfwu(env);
}

static void xtensa_overflow12(CPUXtensaState *env)
{
    put_user_ual(env->regs[0],  env->regs[13] - 16);
    get_user_ual(env->regs[0],  env->regs[1]  - 12);
    put_user_ual(env->regs[1],  env->regs[13] - 12);
    put_user_ual(env->regs[2],  env->regs[13] -  8);
    put_user_ual(env->regs[3],  env->regs[13] -  4);
    put_user_ual(env->regs[4],  env->regs[0]  - 48);
    put_user_ual(env->regs[5],  env->regs[0]  - 44);
    put_user_ual(env->regs[6],  env->regs[0]  - 40);
    put_user_ual(env->regs[7],  env->regs[0]  - 36);
    put_user_ual(env->regs[8],  env->regs[0]  - 32);
    put_user_ual(env->regs[9],  env->regs[0]  - 28);
    put_user_ual(env->regs[10], env->regs[0]  - 24);
    put_user_ual(env->regs[11], env->regs[0]  - 20);
    xtensa_rfwo(env);
}

static void xtensa_underflow12(CPUXtensaState *env)
{
    get_user_ual(env->regs[0],  env->regs[13] - 16);
    get_user_ual(env->regs[1],  env->regs[13] - 12);
    get_user_ual(env->regs[2],  env->regs[13] -  8);
    get_user_ual(env->regs[11], env->regs[1]  - 12);
    get_user_ual(env->regs[3],  env->regs[13] -  4);
    get_user_ual(env->regs[4],  env->regs[11] - 48);
    get_user_ual(env->regs[5],  env->regs[11] - 44);
    get_user_ual(env->regs[6],  env->regs[11] - 40);
    get_user_ual(env->regs[7],  env->regs[11] - 36);
    get_user_ual(env->regs[8],  env->regs[11] - 32);
    get_user_ual(env->regs[9],  env->regs[11] - 28);
    get_user_ual(env->regs[10], env->regs[11] - 24);
    get_user_ual(env->regs[11], env->regs[11] - 20);
    xtensa_rfwu(env);
}

void cpu_loop(CPUXtensaState *env)
{
    CPUState *cs = CPU(xtensa_env_get_cpu(env));
    target_siginfo_t info;
    abi_ulong ret;
    int trapnr;

    while (1) {
        cpu_exec_start(cs);
        trapnr = cpu_exec(cs);
        cpu_exec_end(cs);
        process_queued_cpu_work(cs);

        env->sregs[PS] &= ~PS_EXCM;
        switch (trapnr) {
        case EXCP_INTERRUPT:
            break;

        case EXC_WINDOW_OVERFLOW4:
            xtensa_overflow4(env);
            break;
        case EXC_WINDOW_UNDERFLOW4:
            xtensa_underflow4(env);
            break;
        case EXC_WINDOW_OVERFLOW8:
            xtensa_overflow8(env);
            break;
        case EXC_WINDOW_UNDERFLOW8:
            xtensa_underflow8(env);
            break;
        case EXC_WINDOW_OVERFLOW12:
            xtensa_overflow12(env);
            break;
        case EXC_WINDOW_UNDERFLOW12:
            xtensa_underflow12(env);
            break;

        case EXC_USER:
            switch (env->sregs[EXCCAUSE]) {
            case ILLEGAL_INSTRUCTION_CAUSE:
            case PRIVILEGED_CAUSE:
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                info.si_code =
                    env->sregs[EXCCAUSE] == ILLEGAL_INSTRUCTION_CAUSE ?
                    TARGET_ILL_ILLOPC : TARGET_ILL_PRVOPC;
                info._sifields._sigfault._addr = env->sregs[EPC1];
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                break;

            case SYSCALL_CAUSE:
                env->pc += 3;
                ret = do_syscall(env, env->regs[2],
                                 env->regs[6], env->regs[3],
                                 env->regs[4], env->regs[5],
                                 env->regs[8], env->regs[9], 0, 0);
                switch (ret) {
                default:
                    env->regs[2] = ret;
                    break;

                case -TARGET_ERESTARTSYS:
                    env->pc -= 3;
                    break;

                case -TARGET_QEMU_ESIGRETURN:
                    break;
                }
                break;

            case ALLOCA_CAUSE:
                env->sregs[PS] = deposit32(env->sregs[PS],
                                           PS_OWB_SHIFT,
                                           PS_OWB_LEN,
                                           env->sregs[WINDOW_BASE]);

                switch (env->regs[0] & 0xc0000000) {
                case 0x00000000:
                case 0x40000000:
                    xtensa_rotate_window(env, -1);
                    xtensa_underflow4(env);
                    break;

                case 0x80000000:
                    xtensa_rotate_window(env, -2);
                    xtensa_underflow8(env);
                    break;

                case 0xc0000000:
                    xtensa_rotate_window(env, -3);
                    xtensa_underflow12(env);
                    break;
                }
                break;

            case INTEGER_DIVIDE_BY_ZERO_CAUSE:
                info.si_signo = TARGET_SIGFPE;
                info.si_errno = 0;
                info.si_code = TARGET_FPE_INTDIV;
                info._sifields._sigfault._addr = env->sregs[EPC1];
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                break;

            case LOAD_PROHIBITED_CAUSE:
            case STORE_PROHIBITED_CAUSE:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_ACCERR;
                info._sifields._sigfault._addr = env->sregs[EXCVADDR];
                queue_signal(env, info.si_signo, QEMU_SI_FAULT, &info);
                break;

            default:
                fprintf(stderr, "exccause = %d\n", env->sregs[EXCCAUSE]);
                g_assert_not_reached();
            }
            break;
        case EXCP_DEBUG:
            trapnr = gdb_handlesig(cs, TARGET_SIGTRAP);
            if (trapnr) {
                info.si_signo = trapnr;
                info.si_errno = 0;
                info.si_code = TARGET_TRAP_BRKPT;
                queue_signal(env, trapnr, QEMU_SI_FAULT, &info);
            }
            break;
        case EXC_DEBUG:
        default:
            fprintf(stderr, "trapnr = %d\n", trapnr);
            g_assert_not_reached();
        }
        process_pending_signals(env);
    }
}

#endif /* TARGET_XTENSA */

__thread CPUState *thread_cpu;

bool qemu_cpu_is_self(CPUState *cpu)
{
    return thread_cpu == cpu;
}

void qemu_cpu_kick(CPUState *cpu)
{
    cpu_exit(cpu);
}

void task_settid(TaskState *ts)
{
    if (ts->ts_tid == 0) {
        ts->ts_tid = (pid_t)syscall(SYS_gettid);
    }
}

void stop_all_tasks(void)
{
    /*
     * We trust that when using NPTL, start_exclusive()
     * handles thread stopping correctly.
     */
    start_exclusive();
}

/* Assumes contents are already zeroed.  */
void init_task_state(TaskState *ts)
{
    ts->used = 1;
}

CPUArchState *cpu_copy(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    CPUState *new_cpu = cpu_create(cpu_type);
    CPUArchState *new_env = new_cpu->env_ptr;
    CPUBreakpoint *bp;
    CPUWatchpoint *wp;

    /* Reset non arch specific state */
    cpu_reset(new_cpu);

    memcpy(new_env, env, sizeof(CPUArchState));

    /* Clone all break/watchpoints.
       Note: Once we support ptrace with hw-debug register access, make sure
       BP_CPU break/watchpoints are handled correctly on clone. */
    QTAILQ_INIT(&new_cpu->breakpoints);
    QTAILQ_INIT(&new_cpu->watchpoints);
    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        cpu_breakpoint_insert(new_cpu, bp->pc, bp->flags, NULL);
    }
    QTAILQ_FOREACH(wp, &cpu->watchpoints, entry) {
        cpu_watchpoint_insert(new_cpu, wp->vaddr, wp->len, wp->flags, NULL);
    }

    return new_env;
}

static void handle_arg_help(const char *arg)
{
    usage(EXIT_SUCCESS);
}

static void handle_arg_log(const char *arg)
{
    int mask;

    mask = qemu_str_to_log_mask(arg);
    if (!mask) {
        qemu_print_log_usage(stdout);
        exit(EXIT_FAILURE);
    }
    qemu_log_needs_buffers();
    qemu_set_log(mask);
}

static void handle_arg_dfilter(const char *arg)
{
    qemu_set_dfilter_ranges(arg, NULL);
}

static void handle_arg_log_filename(const char *arg)
{
    qemu_set_log_filename(arg, &error_fatal);
}

static void handle_arg_set_env(const char *arg)
{
    char *r, *p, *token;
    r = p = strdup(arg);
    while ((token = strsep(&p, ",")) != NULL) {
        if (envlist_setenv(envlist, token) != 0) {
            usage(EXIT_FAILURE);
        }
    }
    free(r);
}

static void handle_arg_unset_env(const char *arg)
{
    char *r, *p, *token;
    r = p = strdup(arg);
    while ((token = strsep(&p, ",")) != NULL) {
        if (envlist_unsetenv(envlist, token) != 0) {
            usage(EXIT_FAILURE);
        }
    }
    free(r);
}

static void handle_arg_argv0(const char *arg)
{
    argv0 = strdup(arg);
}

static void handle_arg_stack_size(const char *arg)
{
    char *p;
    guest_stack_size = strtoul(arg, &p, 0);
    if (guest_stack_size == 0) {
        usage(EXIT_FAILURE);
    }

    if (*p == 'M') {
        guest_stack_size *= 1024 * 1024;
    } else if (*p == 'k' || *p == 'K') {
        guest_stack_size *= 1024;
    }
}

static void handle_arg_ld_prefix(const char *arg)
{
    interp_prefix = strdup(arg);
}

static void handle_arg_pagesize(const char *arg)
{
    qemu_host_page_size = atoi(arg);
    if (qemu_host_page_size == 0 ||
        (qemu_host_page_size & (qemu_host_page_size - 1)) != 0) {
        fprintf(stderr, "page size must be a power of two\n");
        exit(EXIT_FAILURE);
    }
}

static void handle_arg_randseed(const char *arg)
{
    unsigned long long seed;

    if (parse_uint_full(arg, &seed, 0) != 0 || seed > UINT_MAX) {
        fprintf(stderr, "Invalid seed number: %s\n", arg);
        exit(EXIT_FAILURE);
    }
    srand(seed);
}

static void handle_arg_gdb(const char *arg)
{
    gdbstub_port = atoi(arg);
}

static void handle_arg_uname(const char *arg)
{
    qemu_uname_release = strdup(arg);
}

static void handle_arg_cpu(const char *arg)
{
    cpu_model = strdup(arg);
    if (cpu_model == NULL || is_help_option(cpu_model)) {
        /* XXX: implement xxx_cpu_list for targets that still miss it */
#if defined(cpu_list)
        cpu_list(stdout, &fprintf);
#endif
        exit(EXIT_FAILURE);
    }
}

static void handle_arg_guest_base(const char *arg)
{
    guest_base = strtol(arg, NULL, 0);
    have_guest_base = 1;
}

static void handle_arg_reserved_va(const char *arg)
{
    char *p;
    int shift = 0;
    reserved_va = strtoul(arg, &p, 0);
    switch (*p) {
    case 'k':
    case 'K':
        shift = 10;
        break;
    case 'M':
        shift = 20;
        break;
    case 'G':
        shift = 30;
        break;
    }
    if (shift) {
        unsigned long unshifted = reserved_va;
        p++;
        reserved_va <<= shift;
        if (reserved_va >> shift != unshifted
            || (MAX_RESERVED_VA && reserved_va > MAX_RESERVED_VA)) {
            fprintf(stderr, "Reserved virtual address too big\n");
            exit(EXIT_FAILURE);
        }
    }
    if (*p) {
        fprintf(stderr, "Unrecognised -R size suffix '%s'\n", p);
        exit(EXIT_FAILURE);
    }
}

static void handle_arg_singlestep(const char *arg)
{
    singlestep = 1;
}

static void handle_arg_strace(const char *arg)
{
    do_strace = 1;
}

static void handle_arg_version(const char *arg)
{
    printf("qemu-" TARGET_NAME " version " QEMU_FULL_VERSION
           "\n" QEMU_COPYRIGHT "\n");
    exit(EXIT_SUCCESS);
}

static char *trace_file;
static void handle_arg_trace(const char *arg)
{
    g_free(trace_file);
    trace_file = trace_opt_parse(arg);
}

struct qemu_argument {
    const char *argv;
    const char *env;
    bool has_arg;
    void (*handle_opt)(const char *arg);
    const char *example;
    const char *help;
};

static const struct qemu_argument arg_table[] = {
    {"h",          "",                 false, handle_arg_help,
     "",           "print this help"},
    {"help",       "",                 false, handle_arg_help,
     "",           ""},
    {"g",          "QEMU_GDB",         true,  handle_arg_gdb,
     "port",       "wait gdb connection to 'port'"},
    {"L",          "QEMU_LD_PREFIX",   true,  handle_arg_ld_prefix,
     "path",       "set the elf interpreter prefix to 'path'"},
    {"s",          "QEMU_STACK_SIZE",  true,  handle_arg_stack_size,
     "size",       "set the stack size to 'size' bytes"},
    {"cpu",        "QEMU_CPU",         true,  handle_arg_cpu,
     "model",      "select CPU (-cpu help for list)"},
    {"E",          "QEMU_SET_ENV",     true,  handle_arg_set_env,
     "var=value",  "sets targets environment variable (see below)"},
    {"U",          "QEMU_UNSET_ENV",   true,  handle_arg_unset_env,
     "var",        "unsets targets environment variable (see below)"},
    {"0",          "QEMU_ARGV0",       true,  handle_arg_argv0,
     "argv0",      "forces target process argv[0] to be 'argv0'"},
    {"r",          "QEMU_UNAME",       true,  handle_arg_uname,
     "uname",      "set qemu uname release string to 'uname'"},
    {"B",          "QEMU_GUEST_BASE",  true,  handle_arg_guest_base,
     "address",    "set guest_base address to 'address'"},
    {"R",          "QEMU_RESERVED_VA", true,  handle_arg_reserved_va,
     "size",       "reserve 'size' bytes for guest virtual address space"},
    {"d",          "QEMU_LOG",         true,  handle_arg_log,
     "item[,...]", "enable logging of specified items "
     "(use '-d help' for a list of items)"},
    {"dfilter",    "QEMU_DFILTER",     true,  handle_arg_dfilter,
     "range[,...]","filter logging based on address range"},
    {"D",          "QEMU_LOG_FILENAME", true, handle_arg_log_filename,
     "logfile",     "write logs to 'logfile' (default stderr)"},
    {"p",          "QEMU_PAGESIZE",    true,  handle_arg_pagesize,
     "pagesize",   "set the host page size to 'pagesize'"},
    {"singlestep", "QEMU_SINGLESTEP",  false, handle_arg_singlestep,
     "",           "run in singlestep mode"},
    {"strace",     "QEMU_STRACE",      false, handle_arg_strace,
     "",           "log system calls"},
    {"seed",       "QEMU_RAND_SEED",   true,  handle_arg_randseed,
     "",           "Seed for pseudo-random number generator"},
    {"trace",      "QEMU_TRACE",       true,  handle_arg_trace,
     "",           "[[enable=]<pattern>][,events=<file>][,file=<file>]"},
    {"version",    "QEMU_VERSION",     false, handle_arg_version,
     "",           "display version information and exit"},
    {NULL, NULL, false, NULL, NULL, NULL}
};

static void usage(int exitcode)
{
    const struct qemu_argument *arginfo;
    int maxarglen;
    int maxenvlen;

    printf("usage: qemu-" TARGET_NAME " [options] program [arguments...]\n"
           "Linux CPU emulator (compiled for " TARGET_NAME " emulation)\n"
           "\n"
           "Options and associated environment variables:\n"
           "\n");

    /* Calculate column widths. We must always have at least enough space
     * for the column header.
     */
    maxarglen = strlen("Argument");
    maxenvlen = strlen("Env-variable");

    for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
        int arglen = strlen(arginfo->argv);
        if (arginfo->has_arg) {
            arglen += strlen(arginfo->example) + 1;
        }
        if (strlen(arginfo->env) > maxenvlen) {
            maxenvlen = strlen(arginfo->env);
        }
        if (arglen > maxarglen) {
            maxarglen = arglen;
        }
    }

    printf("%-*s %-*s Description\n", maxarglen+1, "Argument",
            maxenvlen, "Env-variable");

    for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
        if (arginfo->has_arg) {
            printf("-%s %-*s %-*s %s\n", arginfo->argv,
                   (int)(maxarglen - strlen(arginfo->argv) - 1),
                   arginfo->example, maxenvlen, arginfo->env, arginfo->help);
        } else {
            printf("-%-*s %-*s %s\n", maxarglen, arginfo->argv,
                    maxenvlen, arginfo->env,
                    arginfo->help);
        }
    }

    printf("\n"
           "Defaults:\n"
           "QEMU_LD_PREFIX  = %s\n"
           "QEMU_STACK_SIZE = %ld byte\n",
           interp_prefix,
           guest_stack_size);

    printf("\n"
           "You can use -E and -U options or the QEMU_SET_ENV and\n"
           "QEMU_UNSET_ENV environment variables to set and unset\n"
           "environment variables for the target process.\n"
           "It is possible to provide several variables by separating them\n"
           "by commas in getsubopt(3) style. Additionally it is possible to\n"
           "provide the -E and -U options multiple times.\n"
           "The following lines are equivalent:\n"
           "    -E var1=val2 -E var2=val2 -U LD_PRELOAD -U LD_DEBUG\n"
           "    -E var1=val2,var2=val2 -U LD_PRELOAD,LD_DEBUG\n"
           "    QEMU_SET_ENV=var1=val2,var2=val2 QEMU_UNSET_ENV=LD_PRELOAD,LD_DEBUG\n"
           "Note that if you provide several changes to a single variable\n"
           "the last change will stay in effect.\n"
           "\n"
           QEMU_HELP_BOTTOM "\n");

    exit(exitcode);
}

static int parse_args(int argc, char **argv)
{
    const char *r;
    int optind;
    const struct qemu_argument *arginfo;

    for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
        if (arginfo->env == NULL) {
            continue;
        }

        r = getenv(arginfo->env);
        if (r != NULL) {
            arginfo->handle_opt(r);
        }
    }

    optind = 1;
    for (;;) {
        if (optind >= argc) {
            break;
        }
        r = argv[optind];
        if (r[0] != '-') {
            break;
        }
        optind++;
        r++;
        if (!strcmp(r, "-")) {
            break;
        }
        /* Treat --foo the same as -foo.  */
        if (r[0] == '-') {
            r++;
        }

        for (arginfo = arg_table; arginfo->handle_opt != NULL; arginfo++) {
            if (!strcmp(r, arginfo->argv)) {
                if (arginfo->has_arg) {
                    if (optind >= argc) {
                        (void) fprintf(stderr,
                            "qemu: missing argument for option '%s'\n", r);
                        exit(EXIT_FAILURE);
                    }
                    arginfo->handle_opt(argv[optind]);
                    optind++;
                } else {
                    arginfo->handle_opt(NULL);
                }
                break;
            }
        }

        /* no option matched the current argv */
        if (arginfo->handle_opt == NULL) {
            (void) fprintf(stderr, "qemu: unknown option '%s'\n", r);
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        (void) fprintf(stderr, "qemu: no user program specified\n");
        exit(EXIT_FAILURE);
    }

    filename = argv[optind];
    exec_path = argv[optind];

    return optind;
}

int main(int argc, char **argv, char **envp)
{
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    struct linux_binprm bprm;
    TaskState *ts;
    CPUArchState *env;
    CPUState *cpu;
    int optind;
    char **target_environ, **wrk;
    char **target_argv;
    int target_argc;
    int i;
    int ret;
    int execfd;

    module_call_init(MODULE_INIT_TRACE);
    qemu_init_cpu_list();
    module_call_init(MODULE_INIT_QOM);

    envlist = envlist_create();

    /* add current environment into the list */
    for (wrk = environ; *wrk != NULL; wrk++) {
        (void) envlist_setenv(envlist, *wrk);
    }

    /* Read the stack limit from the kernel.  If it's "unlimited",
       then we can do little else besides use the default.  */
    {
        struct rlimit lim;
        if (getrlimit(RLIMIT_STACK, &lim) == 0
            && lim.rlim_cur != RLIM_INFINITY
            && lim.rlim_cur == (target_long)lim.rlim_cur) {
            guest_stack_size = lim.rlim_cur;
        }
    }

    cpu_model = NULL;

    srand(time(NULL));

    qemu_add_opts(&qemu_trace_opts);

    optind = parse_args(argc, argv);

    if (!trace_init_backends()) {
        exit(1);
    }
    trace_init_file(trace_file);

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    memset(&bprm, 0, sizeof (bprm));

    /* Scan interp_prefix dir for replacement files. */
    init_paths(interp_prefix);

    init_qemu_uname_release();

    execfd = qemu_getauxval(AT_EXECFD);
    if (execfd == 0) {
        execfd = open(filename, O_RDONLY);
        if (execfd < 0) {
            printf("Error while loading %s: %s\n", filename, strerror(errno));
            _exit(EXIT_FAILURE);
        }
    }

    if (cpu_model == NULL) {
        cpu_model = cpu_get_model(get_elf_eflags(execfd));
    }
    cpu_type = parse_cpu_model(cpu_model);

    tcg_exec_init(0);
    /* NOTE: we need to init the CPU at this stage to get
       qemu_host_page_size */

    cpu = cpu_create(cpu_type);
    env = cpu->env_ptr;
    cpu_reset(cpu);

    thread_cpu = cpu;

    if (getenv("QEMU_STRACE")) {
        do_strace = 1;
    }

    if (getenv("QEMU_RAND_SEED")) {
        handle_arg_randseed(getenv("QEMU_RAND_SEED"));
    }

    target_environ = envlist_to_environ(envlist, NULL);
    envlist_free(envlist);

    /*
     * Now that page sizes are configured in cpu_init() we can do
     * proper page alignment for guest_base.
     */
    guest_base = HOST_PAGE_ALIGN(guest_base);

    if (reserved_va || have_guest_base) {
        guest_base = init_guest_space(guest_base, reserved_va, 0,
                                      have_guest_base);
        if (guest_base == (unsigned long)-1) {
            fprintf(stderr, "Unable to reserve 0x%lx bytes of virtual address "
                    "space for use as guest address space (check your virtual "
                    "memory ulimit setting or reserve less using -R option)\n",
                    reserved_va);
            exit(EXIT_FAILURE);
        }

        if (reserved_va) {
            mmap_next_start = reserved_va;
        }
    }

    /*
     * Read in mmap_min_addr kernel parameter.  This value is used
     * When loading the ELF image to determine whether guest_base
     * is needed.  It is also used in mmap_find_vma.
     */
    {
        FILE *fp;

        if ((fp = fopen("/proc/sys/vm/mmap_min_addr", "r")) != NULL) {
            unsigned long tmp;
            if (fscanf(fp, "%lu", &tmp) == 1) {
                mmap_min_addr = tmp;
                qemu_log_mask(CPU_LOG_PAGE, "host mmap_min_addr=0x%lx\n", mmap_min_addr);
            }
            fclose(fp);
        }
    }

    /*
     * Prepare copy of argv vector for target.
     */
    target_argc = argc - optind;
    target_argv = calloc(target_argc + 1, sizeof (char *));
    if (target_argv == NULL) {
	(void) fprintf(stderr, "Unable to allocate memory for target_argv\n");
	exit(EXIT_FAILURE);
    }

    /*
     * If argv0 is specified (using '-0' switch) we replace
     * argv[0] pointer with the given one.
     */
    i = 0;
    if (argv0 != NULL) {
        target_argv[i++] = strdup(argv0);
    }
    for (; i < target_argc; i++) {
        target_argv[i] = strdup(argv[optind + i]);
    }
    target_argv[target_argc] = NULL;

    ts = g_new0(TaskState, 1);
    init_task_state(ts);
    /* build Task State */
    ts->info = info;
    ts->bprm = &bprm;
    cpu->opaque = ts;
    task_settid(ts);

    ret = loader_exec(execfd, filename, target_argv, target_environ, regs,
        info, &bprm);
    if (ret != 0) {
        printf("Error while loading %s: %s\n", filename, strerror(-ret));
        _exit(EXIT_FAILURE);
    }

    for (wrk = target_environ; *wrk; wrk++) {
        g_free(*wrk);
    }

    g_free(target_environ);

    if (qemu_loglevel_mask(CPU_LOG_PAGE)) {
        qemu_log("guest_base  0x%lx\n", guest_base);
        log_page_dump();

        qemu_log("start_brk   0x" TARGET_ABI_FMT_lx "\n", info->start_brk);
        qemu_log("end_code    0x" TARGET_ABI_FMT_lx "\n", info->end_code);
        qemu_log("start_code  0x" TARGET_ABI_FMT_lx "\n", info->start_code);
        qemu_log("start_data  0x" TARGET_ABI_FMT_lx "\n", info->start_data);
        qemu_log("end_data    0x" TARGET_ABI_FMT_lx "\n", info->end_data);
        qemu_log("start_stack 0x" TARGET_ABI_FMT_lx "\n", info->start_stack);
        qemu_log("brk         0x" TARGET_ABI_FMT_lx "\n", info->brk);
        qemu_log("entry       0x" TARGET_ABI_FMT_lx "\n", info->entry);
        qemu_log("argv_start  0x" TARGET_ABI_FMT_lx "\n", info->arg_start);
        qemu_log("env_start   0x" TARGET_ABI_FMT_lx "\n",
                 info->arg_end + (abi_ulong)sizeof(abi_ulong));
        qemu_log("auxv_start  0x" TARGET_ABI_FMT_lx "\n", info->saved_auxv);
    }

    target_set_brk(info->brk);
    syscall_init();
    signal_init();

    /* Now that we've loaded the binary, GUEST_BASE is fixed.  Delay
       generating the prologue until now so that the prologue can take
       the real value of GUEST_BASE into account.  */
    tcg_prologue_init(tcg_ctx);
    tcg_region_init();

    target_cpu_copy_regs(env, regs);

#if defined(TARGET_M68K)
    {
        env->pc = regs->pc;
        env->dregs[0] = regs->d0;
        env->dregs[1] = regs->d1;
        env->dregs[2] = regs->d2;
        env->dregs[3] = regs->d3;
        env->dregs[4] = regs->d4;
        env->dregs[5] = regs->d5;
        env->dregs[6] = regs->d6;
        env->dregs[7] = regs->d7;
        env->aregs[0] = regs->a0;
        env->aregs[1] = regs->a1;
        env->aregs[2] = regs->a2;
        env->aregs[3] = regs->a3;
        env->aregs[4] = regs->a4;
        env->aregs[5] = regs->a5;
        env->aregs[6] = regs->a6;
        env->aregs[7] = regs->usp;
        env->sr = regs->sr;
        ts->sim_syscalls = 1;
    }
#elif defined(TARGET_MICROBLAZE)
    {
        env->regs[0] = regs->r0;
        env->regs[1] = regs->r1;
        env->regs[2] = regs->r2;
        env->regs[3] = regs->r3;
        env->regs[4] = regs->r4;
        env->regs[5] = regs->r5;
        env->regs[6] = regs->r6;
        env->regs[7] = regs->r7;
        env->regs[8] = regs->r8;
        env->regs[9] = regs->r9;
        env->regs[10] = regs->r10;
        env->regs[11] = regs->r11;
        env->regs[12] = regs->r12;
        env->regs[13] = regs->r13;
        env->regs[14] = regs->r14;
        env->regs[15] = regs->r15;	    
        env->regs[16] = regs->r16;	    
        env->regs[17] = regs->r17;	    
        env->regs[18] = regs->r18;	    
        env->regs[19] = regs->r19;	    
        env->regs[20] = regs->r20;	    
        env->regs[21] = regs->r21;	    
        env->regs[22] = regs->r22;	    
        env->regs[23] = regs->r23;	    
        env->regs[24] = regs->r24;	    
        env->regs[25] = regs->r25;	    
        env->regs[26] = regs->r26;	    
        env->regs[27] = regs->r27;	    
        env->regs[28] = regs->r28;	    
        env->regs[29] = regs->r29;	    
        env->regs[30] = regs->r30;	    
        env->regs[31] = regs->r31;	    
        env->sregs[SR_PC] = regs->pc;
    }
#elif defined(TARGET_RISCV)
    {
        env->pc = regs->sepc;
        env->gpr[xSP] = regs->sp;
    }
#elif defined(TARGET_ALPHA)
    {
        int i;

        for(i = 0; i < 28; i++) {
            env->ir[i] = ((abi_ulong *)regs)[i];
        }
        env->ir[IR_SP] = regs->usp;
        env->pc = regs->pc;
    }
#elif defined(TARGET_CRIS)
    {
	    env->regs[0] = regs->r0;
	    env->regs[1] = regs->r1;
	    env->regs[2] = regs->r2;
	    env->regs[3] = regs->r3;
	    env->regs[4] = regs->r4;
	    env->regs[5] = regs->r5;
	    env->regs[6] = regs->r6;
	    env->regs[7] = regs->r7;
	    env->regs[8] = regs->r8;
	    env->regs[9] = regs->r9;
	    env->regs[10] = regs->r10;
	    env->regs[11] = regs->r11;
	    env->regs[12] = regs->r12;
	    env->regs[13] = regs->r13;
	    env->regs[14] = info->start_stack;
	    env->regs[15] = regs->acr;	    
	    env->pc = regs->erp;
    }
#elif defined(TARGET_S390X)
    {
            int i;
            for (i = 0; i < 16; i++) {
                env->regs[i] = regs->gprs[i];
            }
            env->psw.mask = regs->psw.mask;
            env->psw.addr = regs->psw.addr;
    }
#elif defined(TARGET_TILEGX)
    {
        int i;
        for (i = 0; i < TILEGX_R_COUNT; i++) {
            env->regs[i] = regs->regs[i];
        }
        for (i = 0; i < TILEGX_SPR_COUNT; i++) {
            env->spregs[i] = 0;
        }
        env->pc = regs->pc;
    }
#elif defined(TARGET_HPPA)
    {
        int i;
        for (i = 1; i < 32; i++) {
            env->gr[i] = regs->gr[i];
        }
        env->iaoq_f = regs->iaoq[0];
        env->iaoq_b = regs->iaoq[1];
    }
#elif defined(TARGET_XTENSA)
    {
        int i;
        for (i = 0; i < 16; ++i) {
            env->regs[i] = regs->areg[i];
        }
        env->sregs[WINDOW_START] = regs->windowstart;
        env->pc = regs->pc;
    }
#endif

#if defined(TARGET_M68K)
    ts->stack_base = info->start_stack;
    ts->heap_base = info->brk;
    /* This will be filled in on the first SYS_HEAPINFO call.  */
    ts->heap_limit = 0;
#endif

    if (gdbstub_port) {
        if (gdbserver_start(gdbstub_port) < 0) {
            fprintf(stderr, "qemu: could not open gdbserver on port %d\n",
                    gdbstub_port);
            exit(EXIT_FAILURE);
        }
        gdb_handlesig(cpu, 0);
    }
    cpu_loop(env);
    /* never exits */
    return 0;
}
