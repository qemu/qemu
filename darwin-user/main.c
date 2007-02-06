/*
 *  qemu user main
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2006 Pierre d'Herbemont
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/mman.h>

#include "qemu.h"

#define DEBUG_LOGFILE "/tmp/qemu.log"

#ifdef __APPLE__
#include <crt_externs.h>
# define environ  (*_NSGetEnviron())
#endif

#include <mach/mach_init.h>
#include <mach/vm_map.h>

const char *interp_prefix = "";

asm(".zerofill __STD_PROG_ZONE, __STD_PROG_ZONE, __std_prog_zone, 0x0dfff000");

/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
unsigned long stack_size = 512 * 1024;

void qerror(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

void gemu_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void cpu_outb(CPUState *env, int addr, int val)
{
    fprintf(stderr, "outb: port=0x%04x, data=%02x\n", addr, val);
}

void cpu_outw(CPUState *env, int addr, int val)
{
    fprintf(stderr, "outw: port=0x%04x, data=%04x\n", addr, val);
}

void cpu_outl(CPUState *env, int addr, int val)
{
    fprintf(stderr, "outl: port=0x%04x, data=%08x\n", addr, val);
}

int cpu_inb(CPUState *env, int addr)
{
    fprintf(stderr, "inb: port=0x%04x\n", addr);
    return 0;
}

int cpu_inw(CPUState *env, int addr)
{
    fprintf(stderr, "inw: port=0x%04x\n", addr);
    return 0;
}

int cpu_inl(CPUState *env, int addr)
{
    fprintf(stderr, "inl: port=0x%04x\n", addr);
    return 0;
}

int cpu_get_pic_interrupt(CPUState *env)
{
    return -1;
}
#ifdef TARGET_PPC

static inline uint64_t cpu_ppc_get_tb (CPUState *env)
{
    /* TO FIX */
    return 0;
}

uint32_t cpu_ppc_load_tbl (CPUState *env)
{
    return cpu_ppc_get_tb(env) & 0xFFFFFFFF;
}

uint32_t cpu_ppc_load_tbu (CPUState *env)
{
    return cpu_ppc_get_tb(env) >> 32;
}

static void cpu_ppc_store_tb (CPUState *env, uint64_t value)
{
    /* TO FIX */
}

void cpu_ppc_store_tbu (CPUState *env, uint32_t value)
{
    cpu_ppc_store_tb(env, ((uint64_t)value << 32) | cpu_ppc_load_tbl(env));
}

void cpu_ppc_store_tbl (CPUState *env, uint32_t value)
{
    cpu_ppc_store_tb(env, ((uint64_t)cpu_ppc_load_tbl(env) << 32) | value);
}

uint32_t cpu_ppc_load_decr (CPUState *env)
{
    /* TO FIX */
    return -1;
}

void cpu_ppc_store_decr (CPUState *env, uint32_t value)
{
    /* TO FIX */
}

void cpu_loop(CPUPPCState *env)
{
    int trapnr;
    uint32_t ret;
    target_siginfo_t info;

    for(;;) {
        trapnr = cpu_ppc_exec(env);
        if (trapnr != EXCP_SYSCALL_USER && trapnr != EXCP_BRANCH &&
            trapnr != EXCP_TRACE) {
            if (loglevel > 0) {
                cpu_dump_state(env, logfile, fprintf, 0);
            }
        }
        switch(trapnr) {
        case EXCP_NONE:
            break;
        case EXCP_SYSCALL_USER:
            /* system call */
            if(((int)env->gpr[0]) <= SYS_MAXSYSCALL && ((int)env->gpr[0])>0)
                ret = do_unix_syscall(env, env->gpr[0]/*, env->gpr[3], env->gpr[4],
                                      env->gpr[5], env->gpr[6], env->gpr[7],
                                      env->gpr[8], env->gpr[9], env->gpr[10]*/);
            else if(((int)env->gpr[0])<0)
                ret = do_mach_syscall(env, env->gpr[0], env->gpr[3], env->gpr[4],
                                      env->gpr[5], env->gpr[6], env->gpr[7],
                                      env->gpr[8], env->gpr[9], env->gpr[10]);
            else
                ret = do_thread_syscall(env, env->gpr[0], env->gpr[3], env->gpr[4],
                                        env->gpr[5], env->gpr[6], env->gpr[7],
                                        env->gpr[8], env->gpr[9], env->gpr[10]);

            /* Unix syscall error signaling */
            if(((int)env->gpr[0]) <= SYS_MAXSYSCALL && ((int)env->gpr[0])>0)
            {
                if( (int)ret < 0 )
                    env->nip += 0;
                else
                    env->nip += 4;
            }

            /* Return value */
            env->gpr[3] = ret;
            break;
        case EXCP_RESET:
            /* Should not happen ! */
            fprintf(stderr, "RESET asked... Stop emulation\n");
            if (loglevel)
                fprintf(logfile, "RESET asked... Stop emulation\n");
                abort();
        case EXCP_MACHINE_CHECK:
            fprintf(stderr, "Machine check exeption...  Stop emulation\n");
            if (loglevel)
                fprintf(logfile, "RESET asked... Stop emulation\n");
                info.si_signo = SIGBUS;
            info.si_errno = 0;
            info.si_code = BUS_OBJERR;
            info.si_addr = (void*)(env->nip - 4);
            queue_signal(info.si_signo, &info);
        case EXCP_DSI:
#ifndef DAR
/* To deal with multiple qemu header version as host for the darwin-user code */
# define DAR SPR_DAR
#endif
            fprintf(stderr, "Invalid data memory access: 0x%08x\n", env->spr[DAR]);
            if (loglevel) {
                fprintf(logfile, "Invalid data memory access: 0x%08x\n",
                        env->spr[DAR]);
            }
            /* Handle this via the gdb */
            gdb_handlesig (env, SIGSEGV);

            info.si_addr = (void*)env->nip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_ISI:
            fprintf(stderr, "Invalid instruction fetch\n");
            if (loglevel)
                fprintf(logfile, "Invalid instruction fetch\n");
            /* Handle this via the gdb */
            gdb_handlesig (env, SIGSEGV);

            info.si_addr = (void*)(env->nip - 4);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_EXTERNAL:
            /* Should not happen ! */
            fprintf(stderr, "External interruption... Stop emulation\n");
            if (loglevel)
                fprintf(logfile, "External interruption... Stop emulation\n");
                abort();
        case EXCP_ALIGN:
            fprintf(stderr, "Invalid unaligned memory access\n");
            if (loglevel)
                fprintf(logfile, "Invalid unaligned memory access\n");
                info.si_signo = SIGBUS;
            info.si_errno = 0;
            info.si_code = BUS_ADRALN;
            info.si_addr = (void*)(env->nip - 4);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_PROGRAM:
            switch (env->error_code & ~0xF) {
                case EXCP_FP:
                    fprintf(stderr, "Program exception\n");
                    if (loglevel)
                        fprintf(logfile, "Program exception\n");
                        /* Set FX */
                        env->fpscr[7] |= 0x8;
                    /* Finally, update FEX */
                    if ((((env->fpscr[7] & 0x3) << 3) | (env->fpscr[6] >> 1)) &
                        ((env->fpscr[1] << 1) | (env->fpscr[0] >> 3)))
                        env->fpscr[7] |= 0x4;
                        info.si_signo = SIGFPE;
                    info.si_errno = 0;
                    switch (env->error_code & 0xF) {
                        case EXCP_FP_OX:
                            info.si_code = FPE_FLTOVF;
                            break;
                        case EXCP_FP_UX:
                            info.si_code = FPE_FLTUND;
                            break;
                        case EXCP_FP_ZX:
                        case EXCP_FP_VXZDZ:
                            info.si_code = FPE_FLTDIV;
                            break;
                        case EXCP_FP_XX:
                            info.si_code = FPE_FLTRES;
                            break;
                        case EXCP_FP_VXSOFT:
                            info.si_code = FPE_FLTINV;
                            break;
                        case EXCP_FP_VXNAN:
                        case EXCP_FP_VXISI:
                        case EXCP_FP_VXIDI:
                        case EXCP_FP_VXIMZ:
                        case EXCP_FP_VXVC:
                        case EXCP_FP_VXSQRT:
                        case EXCP_FP_VXCVI:
                            info.si_code = FPE_FLTSUB;
                            break;
                        default:
                            fprintf(stderr, "Unknown floating point exception "
                                    "(%02x)\n", env->error_code);
                            if (loglevel) {
                                fprintf(logfile, "Unknown floating point exception "
                                        "(%02x)\n", env->error_code & 0xF);
                            }
                    }
                        break;
                case EXCP_INVAL:
                    fprintf(stderr, "Invalid instruction\n");
                    if (loglevel)
                        fprintf(logfile, "Invalid instruction\n");
                        info.si_signo = SIGILL;
                    info.si_errno = 0;
                    switch (env->error_code & 0xF) {
                        case EXCP_INVAL_INVAL:
                            info.si_code = ILL_ILLOPC;
                            break;
                        case EXCP_INVAL_LSWX:
                            info.si_code = ILL_ILLOPN;
                            break;
                        case EXCP_INVAL_SPR:
                            info.si_code = ILL_PRVREG;
                            break;
                        case EXCP_INVAL_FP:
                            info.si_code = ILL_COPROC;
                            break;
                        default:
                            fprintf(stderr, "Unknown invalid operation (%02x)\n",
                                    env->error_code & 0xF);
                            if (loglevel) {
                                fprintf(logfile, "Unknown invalid operation (%02x)\n",
                                        env->error_code & 0xF);
                            }
                                info.si_code = ILL_ILLADR;
                            break;
                    }
                        /* Handle this via the gdb */
                        gdb_handlesig (env, SIGSEGV);
                    break;
                case EXCP_PRIV:
                    fprintf(stderr, "Privilege violation\n");
                    if (loglevel)
                        fprintf(logfile, "Privilege violation\n");
                        info.si_signo = SIGILL;
                    info.si_errno = 0;
                    switch (env->error_code & 0xF) {
                        case EXCP_PRIV_OPC:
                            info.si_code = ILL_PRVOPC;
                            break;
                        case EXCP_PRIV_REG:
                            info.si_code = ILL_PRVREG;
                            break;
                        default:
                            fprintf(stderr, "Unknown privilege violation (%02x)\n",
                                    env->error_code & 0xF);
                            info.si_code = ILL_PRVOPC;
                            break;
                    }
                        break;
                case EXCP_TRAP:
                    fprintf(stderr, "Tried to call a TRAP\n");
                    if (loglevel)
                        fprintf(logfile, "Tried to call a TRAP\n");
                        abort();
                default:
                    /* Should not happen ! */
                    fprintf(stderr, "Unknown program exception (%02x)\n",
                            env->error_code);
                    if (loglevel) {
                        fprintf(logfile, "Unknwon program exception (%02x)\n",
                                env->error_code);
                    }
                        abort();
            }
            info.si_addr = (void*)(env->nip - 4);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_NO_FP:
            fprintf(stderr, "No floating point allowed\n");
            if (loglevel)
                fprintf(logfile, "No floating point allowed\n");
                info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = ILL_COPROC;
            info.si_addr = (void*)(env->nip - 4);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_DECR:
            /* Should not happen ! */
            fprintf(stderr, "Decrementer exception\n");
            if (loglevel)
                fprintf(logfile, "Decrementer exception\n");
            abort();
        case EXCP_TRACE:
            /* Pass to gdb: we use this to trace execution */
            gdb_handlesig (env, SIGTRAP);
            break;
        case EXCP_FP_ASSIST:
            /* Should not happen ! */
            fprintf(stderr, "Floating point assist exception\n");
            if (loglevel)
                fprintf(logfile, "Floating point assist exception\n");
            abort();
        case EXCP_MTMSR:
            /* We reloaded the msr, just go on */
            if (msr_pr == 0) {
                fprintf(stderr, "Tried to go into supervisor mode !\n");
                if (loglevel)
                    fprintf(logfile, "Tried to go into supervisor mode !\n");
                abort();
            }
            break;
        case EXCP_BRANCH:
            /* We stopped because of a jump... */
            break;
        case EXCP_INTERRUPT:
            /* Don't know why this should ever happen... */
            fprintf(stderr, "EXCP_INTERRUPT\n");
            break;
        case EXCP_DEBUG:
            gdb_handlesig (env, SIGTRAP);
            break;
        default:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n",
                    trapnr);
            if (loglevel) {
                fprintf(logfile, "qemu: unhandled CPU exception 0x%02x - "
                        "0x%02x - aborting\n", trapnr, env->error_code);
            }
                abort();
        }
        process_pending_signals(env);
    }
}
#endif


#ifdef TARGET_I386

/***********************************************************/
/* CPUX86 core interface */

uint64_t cpu_get_tsc(CPUX86State *env)
{
    return cpu_get_real_ticks();
}

void
write_dt(void *ptr, unsigned long addr, unsigned long limit,
                     int flags)
{
    unsigned int e1, e2;
    e1 = (addr << 16) | (limit & 0xffff);
    e2 = ((addr >> 16) & 0xff) | (addr & 0xff000000) | (limit & 0x000f0000);
    e2 |= flags;
    stl((uint8_t *)ptr, e1);
    stl((uint8_t *)ptr + 4, e2);
}

static void set_gate(void *ptr, unsigned int type, unsigned int dpl,
                     unsigned long addr, unsigned int sel)
{
    unsigned int e1, e2;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    stl((uint8_t *)ptr, e1);
    stl((uint8_t *)ptr + 4, e2);
}

#define GDT_TABLE_SIZE 14
#define LDT_TABLE_SIZE 15
#define IDT_TABLE_SIZE 256
#define TSS_SIZE 104
uint64_t gdt_table[GDT_TABLE_SIZE];
uint64_t ldt_table[LDT_TABLE_SIZE];
uint64_t idt_table[IDT_TABLE_SIZE];
uint32_t tss[TSS_SIZE];

/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate(idt_table + n, 0, dpl, 0, 0);
}

/* ABI convention: after a syscall if there was an error the CF flag is set */
static inline void set_error(CPUX86State *env, int ret)
{
    if(ret<0)
        env->eflags = env->eflags | 0x1;
    else
        env->eflags &= ~0x1;
    env->regs[R_EAX] = ret;
}

void cpu_loop(CPUX86State *env)
{
    int trapnr;
    int ret;
    uint8_t *pc;
    target_siginfo_t info;

    for(;;) {
        trapnr = cpu_x86_exec(env);
        uint32_t *params = (uint32_t *)env->regs[R_ESP];
        switch(trapnr) {
        case 0x79: /* Our commpage hack back door exit is here */
            do_commpage(env,  env->eip,   *(params + 1), *(params + 2),
                                          *(params + 3), *(params + 4),
                                          *(params + 5), *(params + 6),
                                          *(params + 7), *(params + 8));
            break;
        case 0x81: /* mach syscall */
        {
            ret = do_mach_syscall(env,  env->regs[R_EAX],
                                          *(params + 1), *(params + 2),
                                          *(params + 3), *(params + 4),
                                          *(params + 5), *(params + 6),
                                          *(params + 7), *(params + 8));
            set_error(env, ret);
            break;
        }
        case 0x90: /* unix backdoor */
        {
            /* after sysenter, stack is in R_ECX, new eip in R_EDX (sysexit will flip them back)*/
            int saved_stack = env->regs[R_ESP];
            env->regs[R_ESP] = env->regs[R_ECX];

            ret = do_unix_syscall(env, env->regs[R_EAX]);

            env->regs[R_ECX] = env->regs[R_ESP];
            env->regs[R_ESP] = saved_stack;

            set_error(env, ret);
            break;
        }
        case 0x80: /* unix syscall */
        {
            ret = do_unix_syscall(env, env->regs[R_EAX]/*,
                                          *(params + 1), *(params + 2),
                                          *(params + 3), *(params + 4),
                                          *(params + 5), *(params + 6),
                                          *(params + 7), *(params + 8)*/);
            set_error(env, ret);
            break;
        }
        case 0x82: /* thread syscall */
        {
            ret = do_thread_syscall(env,  env->regs[R_EAX],
                                          *(params + 1), *(params + 2),
                                          *(params + 3), *(params + 4),
                                          *(params + 5), *(params + 6),
                                          *(params + 7), *(params + 8));
            set_error(env, ret);
            break;
        }
        case EXCP0B_NOSEG:
        case EXCP0C_STACK:
            info.si_signo = SIGBUS;
            info.si_errno = 0;
            info.si_code = BUS_NOOP;
            info.si_addr = 0;
            gdb_handlesig (env, SIGBUS);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP0D_GPF:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            info.si_code = SEGV_NOOP;
            info.si_addr = 0;
            gdb_handlesig (env, SIGSEGV);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP0E_PAGE:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            if (!(env->error_code & 1))
                info.si_code = SEGV_MAPERR;
            else
                info.si_code = SEGV_ACCERR;
            info.si_addr = (void*)env->cr[2];
            gdb_handlesig (env, SIGSEGV);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP00_DIVZ:
            /* division by zero */
            info.si_signo = SIGFPE;
            info.si_errno = 0;
            info.si_code = FPE_INTDIV;
            info.si_addr = (void*)env->eip;
            gdb_handlesig (env, SIGFPE);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP01_SSTP:
        case EXCP03_INT3:
            info.si_signo = SIGTRAP;
            info.si_errno = 0;
            info.si_code = TRAP_BRKPT;
            info.si_addr = (void*)env->eip;
            gdb_handlesig (env, SIGTRAP);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP04_INTO:
        case EXCP05_BOUND:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            info.si_code = SEGV_NOOP;
            info.si_addr = 0;
            gdb_handlesig (env, SIGSEGV);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP06_ILLOP:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = ILL_ILLOPN;
            info.si_addr = (void*)env->eip;
            gdb_handlesig (env, SIGILL);
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            pc = (void*)(env->segs[R_CS].base + env->eip);
            fprintf(stderr, "qemu: 0x%08lx: unhandled CPU exception 0x%x - aborting\n",
                    (long)pc, trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif

void usage(void)
{
    printf("qemu-" TARGET_ARCH " version " QEMU_VERSION ", Copyright (c) 2003-2004 Fabrice Bellard\n"
           "usage: qemu-" TARGET_ARCH " [-h] [-d opts] [-L path] [-s size] program [arguments...]\n"
           "Darwin CPU emulator (compiled for %s emulation)\n"
           "\n"
           "-h           print this help\n"
           "-L path      set the %s library path (default='%s')\n"
           "-s size      set the stack size in bytes (default=%ld)\n"
           "\n"
           "debug options:\n"
#ifdef USE_CODE_COPY
           "-no-code-copy   disable code copy acceleration\n"
#endif
           "-d options   activate log (logfile='%s')\n"
           "-g wait for gdb on port 1234\n"
           "-p pagesize  set the host page size to 'pagesize'\n",
           TARGET_ARCH,
           TARGET_ARCH,
           interp_prefix,
           stack_size,
           DEBUG_LOGFILE);
    _exit(1);
}

/* XXX: currently only used for async signals (see signal.c) */
CPUState *global_env;
/* used only if single thread */
CPUState *cpu_single_env = NULL;

/* used to free thread contexts */
TaskState *first_task_state;

int main(int argc, char **argv)
{
    const char *filename;
    struct target_pt_regs regs1, *regs = &regs1;
    TaskState ts1, *ts = &ts1;
    CPUState *env;
    int optind;
    short use_gdbstub = 0;
    const char *r;

    if (argc <= 1)
        usage();

    /* init debug */
    cpu_set_log_filename(DEBUG_LOGFILE);

    optind = 1;
    for(;;) {
        if (optind >= argc)
            break;
        r = argv[optind];
        if (r[0] != '-')
            break;
        optind++;
        r++;
        if (!strcmp(r, "-")) {
            break;
        } else if (!strcmp(r, "d")) {
            int mask;
            CPULogItem *item;

        if (optind >= argc)
        break;

        r = argv[optind++];
            mask = cpu_str_to_log_mask(r);
            if (!mask) {
                printf("Log items (comma separated):\n");
                for(item = cpu_log_items; item->mask != 0; item++) {
                    printf("%-10s %s\n", item->name, item->help);
                }
                exit(1);
            }
            cpu_set_log(mask);
        } else if (!strcmp(r, "s")) {
            r = argv[optind++];
            stack_size = strtol(r, (char **)&r, 0);
            if (stack_size <= 0)
                usage();
            if (*r == 'M')
                stack_size *= 1024 * 1024;
            else if (*r == 'k' || *r == 'K')
                stack_size *= 1024;
        } else if (!strcmp(r, "L")) {
            interp_prefix = argv[optind++];
        } else if (!strcmp(r, "p")) {
            qemu_host_page_size = atoi(argv[optind++]);
            if (qemu_host_page_size == 0 ||
                (qemu_host_page_size & (qemu_host_page_size - 1)) != 0) {
                fprintf(stderr, "page size must be a power of two\n");
                exit(1);
            }
        } else
        if (!strcmp(r, "g")) {
            use_gdbstub = 1;
        } else
#ifdef USE_CODE_COPY
        if (!strcmp(r, "no-code-copy")) {
            code_copy_enabled = 0;
        } else
#endif
        {
            usage();
        }
    }
    if (optind >= argc)
        usage();
    filename = argv[optind];

    /* Zero out regs */
    memset(regs, 0, sizeof(struct target_pt_regs));

    /* NOTE: we need to init the CPU at this stage to get
       qemu_host_page_size */
    env = cpu_init();

    printf("Starting %s with qemu\n----------------\n", filename);

    commpage_init();

    if (mach_exec(filename, argv+optind, environ, regs) != 0) {
    printf("Error loading %s\n", filename);
    _exit(1);
    }

    syscall_init();
    signal_init();
    global_env = env;

    /* build Task State */
    memset(ts, 0, sizeof(TaskState));
    env->opaque = ts;
    ts->used = 1;
    env->user_mode_only = 1;

#if defined(TARGET_I386)
    cpu_x86_set_cpl(env, 3);

    env->cr[0] = CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK;
    env->hflags |= HF_PE_MASK;

    if (env->cpuid_features & CPUID_SSE) {
        env->cr[4] |= CR4_OSFXSR_MASK;
        env->hflags |= HF_OSFXSR_MASK;
    }

    /* flags setup : we activate the IRQs by default as in user mode */
    env->eflags |= IF_MASK;

    /* darwin register setup */
    env->regs[R_EAX] = regs->eax;
    env->regs[R_EBX] = regs->ebx;
    env->regs[R_ECX] = regs->ecx;
    env->regs[R_EDX] = regs->edx;
    env->regs[R_ESI] = regs->esi;
    env->regs[R_EDI] = regs->edi;
    env->regs[R_EBP] = regs->ebp;
    env->regs[R_ESP] = regs->esp;
    env->eip = regs->eip;

    /* Darwin LDT setup */
    /* 2 - User code segment
       3 - User data segment
       4 - User cthread */
    bzero(ldt_table, LDT_TABLE_SIZE * sizeof(ldt_table[0]));
    env->ldt.base = (uint32_t) ldt_table;
    env->ldt.limit = sizeof(ldt_table) - 1;

    write_dt(ldt_table + 2, 0, 0xfffff,
             DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
             (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));
    write_dt(ldt_table + 3, 0, 0xfffff,
             DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
             (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));
    write_dt(ldt_table + 4, 0, 0xfffff,
             DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK |
             (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));

    /* Darwin GDT setup.
     * has changed a lot between old Darwin/x86 (pre-Mac Intel) and Mac OS X/x86,
       now everything is done via  int 0x81(mach) int 0x82 (thread) and sysenter/sysexit(unix) */
    bzero(gdt_table, sizeof(gdt_table));
    env->gdt.base = (uint32_t)gdt_table;
    env->gdt.limit = sizeof(gdt_table) - 1;

    /* Set up a back door to handle sysenter syscalls (unix) */
    char * syscallbackdoor = malloc(64);
    page_set_flags((int)syscallbackdoor, (int)syscallbackdoor + 64, PROT_EXEC | PROT_READ | PAGE_VALID);

    int i = 0;
    syscallbackdoor[i++] = 0xcd;
    syscallbackdoor[i++] = 0x90; /* int 0x90 */
    syscallbackdoor[i++] = 0x0F;
    syscallbackdoor[i++] = 0x35; /* sysexit */

    /* Darwin sysenter/sysexit setup */
    env->sysenter_cs = 0x1; //XXX
    env->sysenter_eip = (int)syscallbackdoor;
    env->sysenter_esp = (int)malloc(64);

    /* Darwin TSS setup
       This must match up with GDT[4] */
    env->tr.base = (uint32_t) tss;
    env->tr.limit = sizeof(tss) - 1;
    env->tr.flags = DESC_P_MASK | (0x9 << DESC_TYPE_SHIFT);
    stw(tss + 2, 0x10);  // ss0 = 0x10 = GDT[2] = Kernel Data Segment

    /* Darwin interrupt setup */
    bzero(idt_table, sizeof(idt_table));
    env->idt.base = (uint32_t) idt_table;
    env->idt.limit = sizeof(idt_table) - 1;
    set_idt(0, 0);
    set_idt(1, 0);
    set_idt(2, 0);
    set_idt(3, 3);
    set_idt(4, 3);
    set_idt(5, 3);
    set_idt(6, 0);
    set_idt(7, 0);
    set_idt(8, 0);
    set_idt(9, 0);
    set_idt(10, 0);
    set_idt(11, 0);
    set_idt(12, 0);
    set_idt(13, 0);
    set_idt(14, 0);
    set_idt(15, 0);
    set_idt(16, 0);
    set_idt(17, 0);
    set_idt(18, 0);
    set_idt(19, 0);
    /* Syscalls are done via
        int 0x80 (unix) (rarely used)
        int 0x81 (mach)
        int 0x82 (thread)
        int 0x83 (diag) (not handled here)
        sysenter/sysexit (unix) -> we redirect that to int 0x90 */
    set_idt(0x79, 3); /* Commpage hack, here is our backdoor interrupt */
    set_idt(0x80, 3); /* Unix Syscall */
    set_idt(0x81, 3); /* Mach Syscalls */
    set_idt(0x82, 3); /* thread Syscalls */

    set_idt(0x90, 3); /* qemu-darwin-user's Unix syscalls backdoor */


    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_FS, __USER_DS);
    cpu_x86_load_seg(env, R_GS, __USER_DS);

#elif defined(TARGET_PPC)
    {
        int i;
        env->nip = regs->nip;
        for(i = 0; i < 32; i++) {
            env->gpr[i] = regs->gpr[i];
        }
    }
#else
#error unsupported target CPU
#endif

    if (use_gdbstub) {
        printf("Waiting for gdb Connection on port 1234...\n");
        gdbserver_start (1234);
        gdb_handlesig(env, 0);
    }

    cpu_loop(env);
    /* never exits */
    return 0;
}
