/*
 *  qemu user main
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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

#include "qemu.h"

#define DEBUG_LOGFILE "/tmp/qemu.log"

#ifdef __APPLE__
#include <crt_externs.h>
# define environ  (*_NSGetEnviron())
#endif

static const char *interp_prefix = CONFIG_QEMU_PREFIX;
const char *qemu_uname_release = CONFIG_UNAME_RELEASE;

#if defined(__i386__) && !defined(CONFIG_STATIC)
/* Force usage of an ELF interpreter even if it is an ELF shared
   object ! */
const char interp[] __attribute__((section(".interp"))) = "/lib/ld-linux.so.2";
#endif

/* for recent libc, we add these dummy symbols which are not declared
   when generating a linked object (bug in ld ?) */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3)) && !defined(CONFIG_STATIC)
long __preinit_array_start[0];
long __preinit_array_end[0];
long __init_array_start[0];
long __init_array_end[0];
long __fini_array_start[0];
long __fini_array_end[0];
#endif

/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
unsigned long x86_stack_size = 512 * 1024;

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

/* timers for rdtsc */

#if 0

static uint64_t emu_time;

int64_t cpu_get_real_ticks(void)
{
    return emu_time++;
}

#endif

#ifdef TARGET_I386
/***********************************************************/
/* CPUX86 core interface */

void cpu_smm_update(CPUState *env)
{
}

uint64_t cpu_get_tsc(CPUX86State *env)
{
    return cpu_get_real_ticks();
}

static void write_dt(void *ptr, unsigned long addr, unsigned long limit, 
                     int flags)
{
    unsigned int e1, e2;
    uint32_t *p;
    e1 = (addr << 16) | (limit & 0xffff);
    e2 = ((addr >> 16) & 0xff) | (addr & 0xff000000) | (limit & 0x000f0000);
    e2 |= flags;
    p = ptr;
    p[0] = tswapl(e1);
    p[1] = tswapl(e2);
}

static void set_gate(void *ptr, unsigned int type, unsigned int dpl, 
                     unsigned long addr, unsigned int sel)
{
    unsigned int e1, e2;
    uint32_t *p;
    e1 = (addr & 0xffff) | (sel << 16);
    e2 = (addr & 0xffff0000) | 0x8000 | (dpl << 13) | (type << 8);
    p = ptr;
    p[0] = tswapl(e1);
    p[1] = tswapl(e2);
}

uint64_t gdt_table[6];
uint64_t idt_table[256];

/* only dpl matters as we do only user space emulation */
static void set_idt(int n, unsigned int dpl)
{
    set_gate(idt_table + n, 0, dpl, 0, 0);
}

void cpu_loop(CPUX86State *env)
{
    int trapnr;
    target_ulong pc;
    target_siginfo_t info;

    for(;;) {
        trapnr = cpu_x86_exec(env);
        switch(trapnr) {
        case 0x80:
            /* linux syscall */
            env->regs[R_EAX] = do_syscall(env, 
                                          env->regs[R_EAX], 
                                          env->regs[R_EBX],
                                          env->regs[R_ECX],
                                          env->regs[R_EDX],
                                          env->regs[R_ESI],
                                          env->regs[R_EDI],
                                          env->regs[R_EBP]);
            break;
        case EXCP0B_NOSEG:
        case EXCP0C_STACK:
            info.si_signo = SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_SI_KERNEL;
            info._sifields._sigfault._addr = 0;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP0D_GPF:
            if (env->eflags & VM_MASK) {
                handle_vm86_fault(env);
            } else {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP0E_PAGE:
            info.si_signo = SIGSEGV;
            info.si_errno = 0;
            if (!(env->error_code & 1))
                info.si_code = TARGET_SEGV_MAPERR;
            else
                info.si_code = TARGET_SEGV_ACCERR;
            info._sifields._sigfault._addr = env->cr[2];
            queue_signal(info.si_signo, &info);
            break;
        case EXCP00_DIVZ:
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else {
                /* division by zero */
                info.si_signo = SIGFPE;
                info.si_errno = 0;
                info.si_code = TARGET_FPE_INTDIV;
                info._sifields._sigfault._addr = env->eip;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP01_SSTP:
        case EXCP03_INT3:
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else {
                info.si_signo = SIGTRAP;
                info.si_errno = 0;
                if (trapnr == EXCP01_SSTP) {
                    info.si_code = TARGET_TRAP_BRKPT;
                    info._sifields._sigfault._addr = env->eip;
                } else {
                    info.si_code = TARGET_SI_KERNEL;
                    info._sifields._sigfault._addr = 0;
                }
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP04_INTO:
        case EXCP05_BOUND:
            if (env->eflags & VM_MASK) {
                handle_vm86_trap(env, trapnr);
            } else {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SI_KERNEL;
                info._sifields._sigfault._addr = 0;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP06_ILLOP:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->eip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            pc = env->segs[R_CS].base + env->eip;
            fprintf(stderr, "qemu: 0x%08lx: unhandled CPU exception 0x%x - aborting\n", 
                    (long)pc, trapnr);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif

#ifdef TARGET_ARM

/* XXX: find a better solution */
extern void tb_invalidate_page_range(target_ulong start, target_ulong end);

static void arm_cache_flush(target_ulong start, target_ulong last)
{
    target_ulong addr, last1;

    if (last < start)
        return;
    addr = start;
    for(;;) {
        last1 = ((addr + TARGET_PAGE_SIZE) & TARGET_PAGE_MASK) - 1;
        if (last1 > last)
            last1 = last;
        tb_invalidate_page_range(addr, last1 + 1);
        if (last1 == last)
            break;
        addr = last1 + 1;
    }
}

void cpu_loop(CPUARMState *env)
{
    int trapnr;
    unsigned int n, insn;
    target_siginfo_t info;
    uint32_t addr;
    
    for(;;) {
        trapnr = cpu_arm_exec(env);
        switch(trapnr) {
        case EXCP_UDEF:
            {
                TaskState *ts = env->opaque;
                uint32_t opcode;

                /* we handle the FPU emulation here, as Linux */
                /* we get the opcode */
                opcode = tget32(env->regs[15]);
                
                if (EmulateAll(opcode, &ts->fpa, env) == 0) {
                    info.si_signo = SIGILL;
                    info.si_errno = 0;
                    info.si_code = TARGET_ILL_ILLOPN;
                    info._sifields._sigfault._addr = env->regs[15];
                    queue_signal(info.si_signo, &info);
                } else {
                    /* increment PC */
                    env->regs[15] += 4;
                }
            }
            break;
        case EXCP_SWI:
        case EXCP_BKPT:
            {
                env->eabi = 1;
                /* system call */
                if (trapnr == EXCP_BKPT) {
                    if (env->thumb) {
                        insn = tget16(env->regs[15]);
                        n = insn & 0xff;
                        env->regs[15] += 2;
                    } else {
                        insn = tget32(env->regs[15]);
                        n = (insn & 0xf) | ((insn >> 4) & 0xff0);
                        env->regs[15] += 4;
                    }
                } else {
                    if (env->thumb) {
                        insn = tget16(env->regs[15] - 2);
                        n = insn & 0xff;
                    } else {
                        insn = tget32(env->regs[15] - 4);
                        n = insn & 0xffffff;
                    }
                }

                if (n == ARM_NR_cacheflush) {
                    arm_cache_flush(env->regs[0], env->regs[1]);
                } else if (n == ARM_NR_semihosting
                           || n == ARM_NR_thumb_semihosting) {
                    env->regs[0] = do_arm_semihosting (env);
                } else if (n == 0 || n >= ARM_SYSCALL_BASE
                           || (env->thumb && n == ARM_THUMB_SYSCALL)) {
                    /* linux syscall */
                    if (env->thumb || n == 0) {
                        n = env->regs[7];
                    } else {
                        n -= ARM_SYSCALL_BASE;
                        env->eabi = 0;
                    }
                    env->regs[0] = do_syscall(env, 
                                              n, 
                                              env->regs[0],
                                              env->regs[1],
                                              env->regs[2],
                                              env->regs[3],
                                              env->regs[4],
                                              env->regs[5]);
                } else {
                    goto error;
                }
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_PREFETCH_ABORT:
            addr = env->cp15.c6_data;
            goto do_segv;
        case EXCP_DATA_ABORT:
            addr = env->cp15.c6_insn;
            goto do_segv;
        do_segv:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = addr;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
        error:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n", 
                    trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            abort();
        }
        process_pending_signals(env);
    }
}

#endif

#ifdef TARGET_SPARC

//#define DEBUG_WIN

/* WARNING: dealing with register windows _is_ complicated. More info
   can be found at http://www.sics.se/~psm/sparcstack.html */
static inline int get_reg_index(CPUSPARCState *env, int cwp, int index)
{
    index = (index + cwp * 16) & (16 * NWINDOWS - 1);
    /* wrap handling : if cwp is on the last window, then we use the
       registers 'after' the end */
    if (index < 8 && env->cwp == (NWINDOWS - 1))
        index += (16 * NWINDOWS);
    return index;
}

/* save the register window 'cwp1' */
static inline void save_window_offset(CPUSPARCState *env, int cwp1)
{
    unsigned int i;
    target_ulong sp_ptr;
    
    sp_ptr = env->regbase[get_reg_index(env, cwp1, 6)];
#if defined(DEBUG_WIN)
    printf("win_overflow: sp_ptr=0x%x save_cwp=%d\n", 
           (int)sp_ptr, cwp1);
#endif
    for(i = 0; i < 16; i++) {
        tputl(sp_ptr, env->regbase[get_reg_index(env, cwp1, 8 + i)]);
        sp_ptr += sizeof(target_ulong);
    }
}

static void save_window(CPUSPARCState *env)
{
#ifndef TARGET_SPARC64
    unsigned int new_wim;
    new_wim = ((env->wim >> 1) | (env->wim << (NWINDOWS - 1))) &
        ((1LL << NWINDOWS) - 1);
    save_window_offset(env, (env->cwp - 2) & (NWINDOWS - 1));
    env->wim = new_wim;
#else
    save_window_offset(env, (env->cwp - 2) & (NWINDOWS - 1));
    env->cansave++;
    env->canrestore--;
#endif
}

static void restore_window(CPUSPARCState *env)
{
    unsigned int new_wim, i, cwp1;
    target_ulong sp_ptr;
    
    new_wim = ((env->wim << 1) | (env->wim >> (NWINDOWS - 1))) &
        ((1LL << NWINDOWS) - 1);
    
    /* restore the invalid window */
    cwp1 = (env->cwp + 1) & (NWINDOWS - 1);
    sp_ptr = env->regbase[get_reg_index(env, cwp1, 6)];
#if defined(DEBUG_WIN)
    printf("win_underflow: sp_ptr=0x%x load_cwp=%d\n", 
           (int)sp_ptr, cwp1);
#endif
    for(i = 0; i < 16; i++) {
        env->regbase[get_reg_index(env, cwp1, 8 + i)] = tgetl(sp_ptr);
        sp_ptr += sizeof(target_ulong);
    }
    env->wim = new_wim;
#ifdef TARGET_SPARC64
    env->canrestore++;
    if (env->cleanwin < NWINDOWS - 1)
	env->cleanwin++;
    env->cansave--;
#endif
}

static void flush_windows(CPUSPARCState *env)
{
    int offset, cwp1;

    offset = 1;
    for(;;) {
        /* if restore would invoke restore_window(), then we can stop */
        cwp1 = (env->cwp + offset) & (NWINDOWS - 1);
        if (env->wim & (1 << cwp1))
            break;
        save_window_offset(env, cwp1);
        offset++;
    }
    /* set wim so that restore will reload the registers */
    cwp1 = (env->cwp + 1) & (NWINDOWS - 1);
    env->wim = 1 << cwp1;
#if defined(DEBUG_WIN)
    printf("flush_windows: nb=%d\n", offset - 1);
#endif
}

void cpu_loop (CPUSPARCState *env)
{
    int trapnr, ret;
    target_siginfo_t info;
    
    while (1) {
        trapnr = cpu_sparc_exec (env);
        
        switch (trapnr) {
#ifndef TARGET_SPARC64
        case 0x88: 
        case 0x90:
#else
        case 0x16d:
#endif
            ret = do_syscall (env, env->gregs[1],
                              env->regwptr[0], env->regwptr[1], 
                              env->regwptr[2], env->regwptr[3], 
                              env->regwptr[4], env->regwptr[5]);
            if ((unsigned int)ret >= (unsigned int)(-515)) {
#ifdef TARGET_SPARC64
                env->xcc |= PSR_CARRY;
#else
                env->psr |= PSR_CARRY;
#endif
                ret = -ret;
            } else {
#ifdef TARGET_SPARC64
                env->xcc &= ~PSR_CARRY;
#else
                env->psr &= ~PSR_CARRY;
#endif
            }
            env->regwptr[0] = ret;
            /* next instruction */
            env->pc = env->npc;
            env->npc = env->npc + 4;
            break;
        case 0x83: /* flush windows */
            flush_windows(env);
            /* next instruction */
            env->pc = env->npc;
            env->npc = env->npc + 4;
            break;
#ifndef TARGET_SPARC64
        case TT_WIN_OVF: /* window overflow */
            save_window(env);
            break;
        case TT_WIN_UNF: /* window underflow */
            restore_window(env);
            break;
        case TT_TFAULT:
        case TT_DFAULT:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->mmuregs[4];
                queue_signal(info.si_signo, &info);
            }
            break;
#else
        case TT_SPILL: /* window overflow */
            save_window(env);
            break;
        case TT_FILL: /* window underflow */
            restore_window(env);
            break;
	    // XXX
#endif
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            exit (1);
        }
        process_pending_signals (env);
    }
}

#endif

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
    target_siginfo_t info;
    int trapnr;
    uint32_t ret;
    
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
            /* WARNING:
             * PPC ABI uses overflow flag in cr0 to signal an error
             * in syscalls.
             */
#if 0
            printf("syscall %d 0x%08x 0x%08x 0x%08x 0x%08x\n", env->gpr[0],
                   env->gpr[3], env->gpr[4], env->gpr[5], env->gpr[6]);
#endif
            env->crf[0] &= ~0x1;
            ret = do_syscall(env, env->gpr[0], env->gpr[3], env->gpr[4],
                             env->gpr[5], env->gpr[6], env->gpr[7],
                             env->gpr[8]);
            if (ret > (uint32_t)(-515)) {
                env->crf[0] |= 0x1;
                ret = -ret;
            }
            env->gpr[3] = ret;
#if 0
            printf("syscall returned 0x%08x (%d)\n", ret, ret);
#endif
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
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_BUS_OBJERR;
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
        case EXCP_DSI:
            fprintf(stderr, "Invalid data memory access: 0x%08x\n",
                    env->spr[SPR_DAR]);
            if (loglevel) {
                fprintf(logfile, "Invalid data memory access: 0x%08x\n",
                        env->spr[SPR_DAR]);
            }
            switch (env->error_code & 0xFF000000) {
            case 0x40000000:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            case 0x04000000:
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                info.si_code = TARGET_ILL_ILLADR;
                break;
            case 0x08000000:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_ACCERR;
                break;
            default:
                /* Let's send a regular segfault... */
                fprintf(stderr, "Invalid segfault errno (%02x)\n",
                        env->error_code);
                if (loglevel) {
                    fprintf(logfile, "Invalid segfault errno (%02x)\n",
                            env->error_code);
                }
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            }
            info._sifields._sigfault._addr = env->nip;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_ISI:
            fprintf(stderr, "Invalid instruction fetch\n");
            if (loglevel)
                fprintf(logfile, "Invalid instruction fetch\n");
            switch (env->error_code & 0xFF000000) {
            case 0x40000000:
                info.si_signo = TARGET_SIGSEGV;
            info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            case 0x10000000:
            case 0x08000000:
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_ACCERR;
                break;
            default:
                /* Let's send a regular segfault... */
                fprintf(stderr, "Invalid segfault errno (%02x)\n",
                        env->error_code);
                if (loglevel) {
                    fprintf(logfile, "Invalid segfault errno (%02x)\n",
                            env->error_code);
                }
                info.si_signo = TARGET_SIGSEGV;
                info.si_errno = 0;
                info.si_code = TARGET_SEGV_MAPERR;
                break;
            }
            info._sifields._sigfault._addr = env->nip - 4;
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
            info.si_signo = TARGET_SIGBUS;
            info.si_errno = 0;
            info.si_code = TARGET_BUS_ADRALN;
            info._sifields._sigfault._addr = env->nip - 4;
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
                info.si_signo = TARGET_SIGFPE;
                info.si_errno = 0;
                switch (env->error_code & 0xF) {
                case EXCP_FP_OX:
                    info.si_code = TARGET_FPE_FLTOVF;
                    break;
                case EXCP_FP_UX:
                    info.si_code = TARGET_FPE_FLTUND;
                    break;
                case EXCP_FP_ZX:
                case EXCP_FP_VXZDZ:
                    info.si_code = TARGET_FPE_FLTDIV;
                    break;
                case EXCP_FP_XX:
                    info.si_code = TARGET_FPE_FLTRES;
                    break;
                case EXCP_FP_VXSOFT:
                    info.si_code = TARGET_FPE_FLTINV;
                    break;
                case EXCP_FP_VXNAN:
                case EXCP_FP_VXISI:
                case EXCP_FP_VXIDI:
                case EXCP_FP_VXIMZ:
                case EXCP_FP_VXVC:
                case EXCP_FP_VXSQRT:
                case EXCP_FP_VXCVI:
                    info.si_code = TARGET_FPE_FLTSUB;
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
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                switch (env->error_code & 0xF) {
                case EXCP_INVAL_INVAL:
                    info.si_code = TARGET_ILL_ILLOPC;
                    break;
                case EXCP_INVAL_LSWX:
            info.si_code = TARGET_ILL_ILLOPN;
                    break;
                case EXCP_INVAL_SPR:
                    info.si_code = TARGET_ILL_PRVREG;
                    break;
                case EXCP_INVAL_FP:
                    info.si_code = TARGET_ILL_COPROC;
                    break;
                default:
                    fprintf(stderr, "Unknown invalid operation (%02x)\n",
                            env->error_code & 0xF);
                    if (loglevel) {
                        fprintf(logfile, "Unknown invalid operation (%02x)\n",
                                env->error_code & 0xF);
                    }
                    info.si_code = TARGET_ILL_ILLADR;
                    break;
                }
                break;
            case EXCP_PRIV:
                fprintf(stderr, "Privilege violation\n");
                if (loglevel)
                    fprintf(logfile, "Privilege violation\n");
                info.si_signo = TARGET_SIGILL;
                info.si_errno = 0;
                switch (env->error_code & 0xF) {
                case EXCP_PRIV_OPC:
                    info.si_code = TARGET_ILL_PRVOPC;
                    break;
                case EXCP_PRIV_REG:
                    info.si_code = TARGET_ILL_PRVREG;
                break;
                default:
                    fprintf(stderr, "Unknown privilege violation (%02x)\n",
                            env->error_code & 0xF);
                    info.si_code = TARGET_ILL_PRVOPC;
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
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_NO_FP:
            fprintf(stderr, "No floating point allowed\n");
            if (loglevel)
                fprintf(logfile, "No floating point allowed\n");
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_COPROC;
            info._sifields._sigfault._addr = env->nip - 4;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_DECR:
            /* Should not happen ! */
            fprintf(stderr, "Decrementer exception\n");
            if (loglevel)
                fprintf(logfile, "Decrementer exception\n");
            abort();
        case EXCP_TRACE:
            /* Do nothing: we use this to trace execution */
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
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
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

#ifdef TARGET_MIPS

#define MIPS_SYS(name, args) args,

static const uint8_t mips_syscall_args[] = {
	MIPS_SYS(sys_syscall	, 0)	/* 4000 */
	MIPS_SYS(sys_exit	, 1)
	MIPS_SYS(sys_fork	, 0)
	MIPS_SYS(sys_read	, 3)
	MIPS_SYS(sys_write	, 3)
	MIPS_SYS(sys_open	, 3)	/* 4005 */
	MIPS_SYS(sys_close	, 1)
	MIPS_SYS(sys_waitpid	, 3)
	MIPS_SYS(sys_creat	, 2)
	MIPS_SYS(sys_link	, 2)
	MIPS_SYS(sys_unlink	, 1)	/* 4010 */
	MIPS_SYS(sys_execve	, 0)
	MIPS_SYS(sys_chdir	, 1)
	MIPS_SYS(sys_time	, 1)
	MIPS_SYS(sys_mknod	, 3)
	MIPS_SYS(sys_chmod	, 2)	/* 4015 */
	MIPS_SYS(sys_lchown	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_stat */
	MIPS_SYS(sys_lseek	, 3)
	MIPS_SYS(sys_getpid	, 0)	/* 4020 */
	MIPS_SYS(sys_mount	, 5)
	MIPS_SYS(sys_oldumount	, 1)
	MIPS_SYS(sys_setuid	, 1)
	MIPS_SYS(sys_getuid	, 0)
	MIPS_SYS(sys_stime	, 1)	/* 4025 */
	MIPS_SYS(sys_ptrace	, 4)
	MIPS_SYS(sys_alarm	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_fstat */
	MIPS_SYS(sys_pause	, 0)
	MIPS_SYS(sys_utime	, 2)	/* 4030 */
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_access	, 2)
	MIPS_SYS(sys_nice	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4035 */
	MIPS_SYS(sys_sync	, 0)
	MIPS_SYS(sys_kill	, 2)
	MIPS_SYS(sys_rename	, 2)
	MIPS_SYS(sys_mkdir	, 2)
	MIPS_SYS(sys_rmdir	, 1)	/* 4040 */
	MIPS_SYS(sys_dup		, 1)
	MIPS_SYS(sys_pipe	, 0)
	MIPS_SYS(sys_times	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_brk		, 1)	/* 4045 */
	MIPS_SYS(sys_setgid	, 1)
	MIPS_SYS(sys_getgid	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was signal(2) */
	MIPS_SYS(sys_geteuid	, 0)
	MIPS_SYS(sys_getegid	, 0)	/* 4050 */
	MIPS_SYS(sys_acct	, 0)
	MIPS_SYS(sys_umount	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ioctl	, 3)
	MIPS_SYS(sys_fcntl	, 3)	/* 4055 */
	MIPS_SYS(sys_ni_syscall	, 2)
	MIPS_SYS(sys_setpgid	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_olduname	, 1)
	MIPS_SYS(sys_umask	, 1)	/* 4060 */
	MIPS_SYS(sys_chroot	, 1)
	MIPS_SYS(sys_ustat	, 2)
	MIPS_SYS(sys_dup2	, 2)
	MIPS_SYS(sys_getppid	, 0)
	MIPS_SYS(sys_getpgrp	, 0)	/* 4065 */
	MIPS_SYS(sys_setsid	, 0)
	MIPS_SYS(sys_sigaction	, 3)
	MIPS_SYS(sys_sgetmask	, 0)
	MIPS_SYS(sys_ssetmask	, 1)
	MIPS_SYS(sys_setreuid	, 2)	/* 4070 */
	MIPS_SYS(sys_setregid	, 2)
	MIPS_SYS(sys_sigsuspend	, 0)
	MIPS_SYS(sys_sigpending	, 1)
	MIPS_SYS(sys_sethostname	, 2)
	MIPS_SYS(sys_setrlimit	, 2)	/* 4075 */
	MIPS_SYS(sys_getrlimit	, 2)
	MIPS_SYS(sys_getrusage	, 2)
	MIPS_SYS(sys_gettimeofday, 2)
	MIPS_SYS(sys_settimeofday, 2)
	MIPS_SYS(sys_getgroups	, 2)	/* 4080 */
	MIPS_SYS(sys_setgroups	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)	/* old_select */
	MIPS_SYS(sys_symlink	, 2)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_lstat */
	MIPS_SYS(sys_readlink	, 3)	/* 4085 */
	MIPS_SYS(sys_uselib	, 1)
	MIPS_SYS(sys_swapon	, 2)
	MIPS_SYS(sys_reboot	, 3)
	MIPS_SYS(old_readdir	, 3)
	MIPS_SYS(old_mmap	, 6)	/* 4090 */
	MIPS_SYS(sys_munmap	, 2)
	MIPS_SYS(sys_truncate	, 2)
	MIPS_SYS(sys_ftruncate	, 2)
	MIPS_SYS(sys_fchmod	, 2)
	MIPS_SYS(sys_fchown	, 3)	/* 4095 */
	MIPS_SYS(sys_getpriority	, 2)
	MIPS_SYS(sys_setpriority	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_statfs	, 2)
	MIPS_SYS(sys_fstatfs	, 2)	/* 4100 */
	MIPS_SYS(sys_ni_syscall	, 0)	/* was ioperm(2) */
	MIPS_SYS(sys_socketcall	, 2)
	MIPS_SYS(sys_syslog	, 3)
	MIPS_SYS(sys_setitimer	, 3)
	MIPS_SYS(sys_getitimer	, 2)	/* 4105 */
	MIPS_SYS(sys_newstat	, 2)
	MIPS_SYS(sys_newlstat	, 2)
	MIPS_SYS(sys_newfstat	, 2)
	MIPS_SYS(sys_uname	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4110 was iopl(2) */
	MIPS_SYS(sys_vhangup	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_idle() */
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_vm86 */
	MIPS_SYS(sys_wait4	, 4)
	MIPS_SYS(sys_swapoff	, 1)	/* 4115 */
	MIPS_SYS(sys_sysinfo	, 1)
	MIPS_SYS(sys_ipc		, 6)
	MIPS_SYS(sys_fsync	, 1)
	MIPS_SYS(sys_sigreturn	, 0)
	MIPS_SYS(sys_clone	, 0)	/* 4120 */
	MIPS_SYS(sys_setdomainname, 2)
	MIPS_SYS(sys_newuname	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* sys_modify_ldt */
	MIPS_SYS(sys_adjtimex	, 1)
	MIPS_SYS(sys_mprotect	, 3)	/* 4125 */
	MIPS_SYS(sys_sigprocmask	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was create_module */
	MIPS_SYS(sys_init_module	, 5)
	MIPS_SYS(sys_delete_module, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4130	was get_kernel_syms */
	MIPS_SYS(sys_quotactl	, 0)
	MIPS_SYS(sys_getpgid	, 1)
	MIPS_SYS(sys_fchdir	, 1)
	MIPS_SYS(sys_bdflush	, 2)
	MIPS_SYS(sys_sysfs	, 3)	/* 4135 */
	MIPS_SYS(sys_personality	, 1)
	MIPS_SYS(sys_ni_syscall	, 0)	/* for afs_syscall */
	MIPS_SYS(sys_setfsuid	, 1)
	MIPS_SYS(sys_setfsgid	, 1)
	MIPS_SYS(sys_llseek	, 5)	/* 4140 */
	MIPS_SYS(sys_getdents	, 3)
	MIPS_SYS(sys_select	, 5)
	MIPS_SYS(sys_flock	, 2)
	MIPS_SYS(sys_msync	, 3)
	MIPS_SYS(sys_readv	, 3)	/* 4145 */
	MIPS_SYS(sys_writev	, 3)
	MIPS_SYS(sys_cacheflush	, 3)
	MIPS_SYS(sys_cachectl	, 3)
	MIPS_SYS(sys_sysmips	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4150 */
	MIPS_SYS(sys_getsid	, 1)
	MIPS_SYS(sys_fdatasync	, 0)
	MIPS_SYS(sys_sysctl	, 1)
	MIPS_SYS(sys_mlock	, 2)
	MIPS_SYS(sys_munlock	, 2)	/* 4155 */
	MIPS_SYS(sys_mlockall	, 1)
	MIPS_SYS(sys_munlockall	, 0)
	MIPS_SYS(sys_sched_setparam, 2)
	MIPS_SYS(sys_sched_getparam, 2)
	MIPS_SYS(sys_sched_setscheduler, 3)	/* 4160 */
	MIPS_SYS(sys_sched_getscheduler, 1)
	MIPS_SYS(sys_sched_yield	, 0)
	MIPS_SYS(sys_sched_get_priority_max, 1)
	MIPS_SYS(sys_sched_get_priority_min, 1)
	MIPS_SYS(sys_sched_rr_get_interval, 2)	/* 4165 */
	MIPS_SYS(sys_nanosleep,	2)
	MIPS_SYS(sys_mremap	, 4)
	MIPS_SYS(sys_accept	, 3)
	MIPS_SYS(sys_bind	, 3)
	MIPS_SYS(sys_connect	, 3)	/* 4170 */
	MIPS_SYS(sys_getpeername	, 3)
	MIPS_SYS(sys_getsockname	, 3)
	MIPS_SYS(sys_getsockopt	, 5)
	MIPS_SYS(sys_listen	, 2)
	MIPS_SYS(sys_recv	, 4)	/* 4175 */
	MIPS_SYS(sys_recvfrom	, 6)
	MIPS_SYS(sys_recvmsg	, 3)
	MIPS_SYS(sys_send	, 4)
	MIPS_SYS(sys_sendmsg	, 3)
	MIPS_SYS(sys_sendto	, 6)	/* 4180 */
	MIPS_SYS(sys_setsockopt	, 5)
	MIPS_SYS(sys_shutdown	, 2)
	MIPS_SYS(sys_socket	, 3)
	MIPS_SYS(sys_socketpair	, 4)
	MIPS_SYS(sys_setresuid	, 3)	/* 4185 */
	MIPS_SYS(sys_getresuid	, 3)
	MIPS_SYS(sys_ni_syscall	, 0)	/* was sys_query_module */
	MIPS_SYS(sys_poll	, 3)
	MIPS_SYS(sys_nfsservctl	, 3)
	MIPS_SYS(sys_setresgid	, 3)	/* 4190 */
	MIPS_SYS(sys_getresgid	, 3)
	MIPS_SYS(sys_prctl	, 5)
	MIPS_SYS(sys_rt_sigreturn, 0)
	MIPS_SYS(sys_rt_sigaction, 4)
	MIPS_SYS(sys_rt_sigprocmask, 4)	/* 4195 */
	MIPS_SYS(sys_rt_sigpending, 2)
	MIPS_SYS(sys_rt_sigtimedwait, 4)
	MIPS_SYS(sys_rt_sigqueueinfo, 3)
	MIPS_SYS(sys_rt_sigsuspend, 0)
	MIPS_SYS(sys_pread64	, 6)	/* 4200 */
	MIPS_SYS(sys_pwrite64	, 6)
	MIPS_SYS(sys_chown	, 3)
	MIPS_SYS(sys_getcwd	, 2)
	MIPS_SYS(sys_capget	, 2)
	MIPS_SYS(sys_capset	, 2)	/* 4205 */
	MIPS_SYS(sys_sigaltstack	, 0)
	MIPS_SYS(sys_sendfile	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_mmap2	, 6)	/* 4210 */
	MIPS_SYS(sys_truncate64	, 4)
	MIPS_SYS(sys_ftruncate64	, 4)
	MIPS_SYS(sys_stat64	, 2)
	MIPS_SYS(sys_lstat64	, 2)
	MIPS_SYS(sys_fstat64	, 2)	/* 4215 */
	MIPS_SYS(sys_pivot_root	, 2)
	MIPS_SYS(sys_mincore	, 3)
	MIPS_SYS(sys_madvise	, 3)
	MIPS_SYS(sys_getdents64	, 3)
	MIPS_SYS(sys_fcntl64	, 3)	/* 4220 */
	MIPS_SYS(sys_ni_syscall	, 0)
	MIPS_SYS(sys_gettid	, 0)
	MIPS_SYS(sys_readahead	, 5)
	MIPS_SYS(sys_setxattr	, 5)
	MIPS_SYS(sys_lsetxattr	, 5)	/* 4225 */
	MIPS_SYS(sys_fsetxattr	, 5)
	MIPS_SYS(sys_getxattr	, 4)
	MIPS_SYS(sys_lgetxattr	, 4)
	MIPS_SYS(sys_fgetxattr	, 4)
	MIPS_SYS(sys_listxattr	, 3)	/* 4230 */
	MIPS_SYS(sys_llistxattr	, 3)
	MIPS_SYS(sys_flistxattr	, 3)
	MIPS_SYS(sys_removexattr	, 2)
	MIPS_SYS(sys_lremovexattr, 2)
	MIPS_SYS(sys_fremovexattr, 2)	/* 4235 */
	MIPS_SYS(sys_tkill	, 2)
	MIPS_SYS(sys_sendfile64	, 5)
	MIPS_SYS(sys_futex	, 2)
	MIPS_SYS(sys_sched_setaffinity, 3)
	MIPS_SYS(sys_sched_getaffinity, 3)	/* 4240 */
	MIPS_SYS(sys_io_setup	, 2)
	MIPS_SYS(sys_io_destroy	, 1)
	MIPS_SYS(sys_io_getevents, 5)
	MIPS_SYS(sys_io_submit	, 3)
	MIPS_SYS(sys_io_cancel	, 3)	/* 4245 */
	MIPS_SYS(sys_exit_group	, 1)
	MIPS_SYS(sys_lookup_dcookie, 3)
	MIPS_SYS(sys_epoll_create, 1)
	MIPS_SYS(sys_epoll_ctl	, 4)
	MIPS_SYS(sys_epoll_wait	, 3)	/* 4250 */
	MIPS_SYS(sys_remap_file_pages, 5)
	MIPS_SYS(sys_set_tid_address, 1)
	MIPS_SYS(sys_restart_syscall, 0)
	MIPS_SYS(sys_fadvise64_64, 7)
	MIPS_SYS(sys_statfs64	, 3)	/* 4255 */
	MIPS_SYS(sys_fstatfs64	, 2)
	MIPS_SYS(sys_timer_create, 3)
	MIPS_SYS(sys_timer_settime, 4)
	MIPS_SYS(sys_timer_gettime, 2)
	MIPS_SYS(sys_timer_getoverrun, 1)	/* 4260 */
	MIPS_SYS(sys_timer_delete, 1)
	MIPS_SYS(sys_clock_settime, 2)
	MIPS_SYS(sys_clock_gettime, 2)
	MIPS_SYS(sys_clock_getres, 2)
	MIPS_SYS(sys_clock_nanosleep, 4)	/* 4265 */
	MIPS_SYS(sys_tgkill	, 3)
	MIPS_SYS(sys_utimes	, 2)
	MIPS_SYS(sys_mbind	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)	/* sys_get_mempolicy */
	MIPS_SYS(sys_ni_syscall	, 0)	/* 4270 sys_set_mempolicy */
	MIPS_SYS(sys_mq_open	, 4)
	MIPS_SYS(sys_mq_unlink	, 1)
	MIPS_SYS(sys_mq_timedsend, 5)
	MIPS_SYS(sys_mq_timedreceive, 5)
	MIPS_SYS(sys_mq_notify	, 2)	/* 4275 */
	MIPS_SYS(sys_mq_getsetattr, 3)
	MIPS_SYS(sys_ni_syscall	, 0)	/* sys_vserver */
	MIPS_SYS(sys_waitid	, 4)
	MIPS_SYS(sys_ni_syscall	, 0)	/* available, was setaltroot */
	MIPS_SYS(sys_add_key	, 5)
	MIPS_SYS(sys_request_key	, 4)
	MIPS_SYS(sys_keyctl	, 5)
};

#undef MIPS_SYS

void cpu_loop(CPUMIPSState *env)
{
    target_siginfo_t info;
    int trapnr, ret, nb_args;
    unsigned int syscall_num;
    target_ulong arg5, arg6, sp_reg;

    for(;;) {
        trapnr = cpu_mips_exec(env);
        switch(trapnr) {
        case EXCP_SYSCALL:
            {
                syscall_num = env->gpr[2] - 4000;
                env->PC += 4;
                if (syscall_num >= sizeof(mips_syscall_args)) {
                    ret = -ENOSYS;
                } else {
                    nb_args = mips_syscall_args[syscall_num];
                    if (nb_args >= 5) {
                        sp_reg = env->gpr[29];
                        /* these arguments are taken from the stack */
                        arg5 = tgetl(sp_reg + 16);
                        if (nb_args >= 6) {
                            arg6 = tgetl(sp_reg + 20);
                        } else {
                            arg6 = 0;
                        }
                    } else {
                        arg5 = 0;
                        arg6 = 0;
                    }
                    ret = do_syscall(env, 
                                     env->gpr[2], 
                                     env->gpr[4],
                                     env->gpr[5],
                                     env->gpr[6],
                                     env->gpr[7],
                                     arg5, 
                                     arg6);
                }
                if ((unsigned int)ret >= (unsigned int)(-1133)) {
                    env->gpr[7] = 1; /* error flag */
                    ret = -ret;
                    env->gpr[0] = ret;
                    env->gpr[2] = ret;
                } else {
                    env->gpr[7] = 0; /* error flag */
                    env->gpr[2] = ret;
                }
            }
            break;
        case EXCP_TLBL:
        case EXCP_TLBS:
        case EXCP_CpU:
        case EXCP_RI:
            info.si_signo = TARGET_SIGILL;
            info.si_errno = 0;
            info.si_code = 0;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            //        error:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n", 
                    trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif

#ifdef TARGET_SH4
void cpu_loop (CPUState *env)
{
    int trapnr, ret;
    target_siginfo_t info;
    
    while (1) {
        trapnr = cpu_sh4_exec (env);
        
        switch (trapnr) {
        case 0x160:
            ret = do_syscall(env, 
                             env->gregs[3], 
                             env->gregs[4], 
                             env->gregs[5], 
                             env->gregs[6], 
                             env->gregs[7], 
                             env->gregs[0], 
                             0);
            env->gregs[0] = ret;
            env->pc += 2;
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            printf ("Unhandled trap: 0x%x\n", trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            exit (1);
        }
        process_pending_signals (env);
    }
}
#endif

#ifdef TARGET_M68K

void cpu_loop(CPUM68KState *env)
{
    int trapnr;
    unsigned int n;
    target_siginfo_t info;
    TaskState *ts = env->opaque;
    
    for(;;) {
        trapnr = cpu_m68k_exec(env);
        switch(trapnr) {
        case EXCP_ILLEGAL:
            {
                if (ts->sim_syscalls) {
                    uint16_t nr;
                    nr = lduw(env->pc + 2);
                    env->pc += 4;
                    do_m68k_simcall(env, nr);
                } else {
                    goto do_sigill;
                }
            }
            break;
        case EXCP_HALTED:
            /* Semihosing syscall.  */
            env->pc += 2;
            do_m68k_semihosting(env, env->dregs[0]);
            break;
        case EXCP_LINEA:
        case EXCP_LINEF:
        case EXCP_UNSUPPORTED:
        do_sigill:
            info.si_signo = SIGILL;
            info.si_errno = 0;
            info.si_code = TARGET_ILL_ILLOPN;
            info._sifields._sigfault._addr = env->pc;
            queue_signal(info.si_signo, &info);
            break;
        case EXCP_TRAP0:
            {
                ts->sim_syscalls = 0;
                n = env->dregs[0];
                env->pc += 2;
                env->dregs[0] = do_syscall(env, 
                                          n, 
                                          env->dregs[1],
                                          env->dregs[2],
                                          env->dregs[3],
                                          env->dregs[4],
                                          env->dregs[5],
                                          env->dregs[6]);
            }
            break;
        case EXCP_INTERRUPT:
            /* just indicate that signals should be handled asap */
            break;
        case EXCP_ACCESS:
            {
                info.si_signo = SIGSEGV;
                info.si_errno = 0;
                /* XXX: check env->error_code */
                info.si_code = TARGET_SEGV_MAPERR;
                info._sifields._sigfault._addr = env->mmu.ar;
                queue_signal(info.si_signo, &info);
            }
            break;
        case EXCP_DEBUG:
            {
                int sig;

                sig = gdb_handlesig (env, TARGET_SIGTRAP);
                if (sig)
                  {
                    info.si_signo = sig;
                    info.si_errno = 0;
                    info.si_code = TARGET_TRAP_BRKPT;
                    queue_signal(info.si_signo, &info);
                  }
            }
            break;
        default:
            fprintf(stderr, "qemu: unhandled CPU exception 0x%x - aborting\n", 
                    trapnr);
            cpu_dump_state(env, stderr, fprintf, 0);
            abort();
        }
        process_pending_signals(env);
    }
}
#endif /* TARGET_M68K */

void usage(void)
{
    printf("qemu-" TARGET_ARCH " version " QEMU_VERSION ", Copyright (c) 2003-2007 Fabrice Bellard\n"
           "usage: qemu-" TARGET_ARCH " [-h] [-g] [-d opts] [-L path] [-s size] program [arguments...]\n"
           "Linux CPU emulator (compiled for %s emulation)\n"
           "\n"
           "-h           print this help\n"
           "-g port      wait gdb connection to port\n"
           "-L path      set the elf interpreter prefix (default=%s)\n"
           "-s size      set the stack size in bytes (default=%ld)\n"
           "\n"
           "debug options:\n"
#ifdef USE_CODE_COPY
           "-no-code-copy   disable code copy acceleration\n"
#endif
           "-d options   activate log (logfile=%s)\n"
           "-p pagesize  set the host page size to 'pagesize'\n",
           TARGET_ARCH,
           interp_prefix, 
           x86_stack_size,
           DEBUG_LOGFILE);
    _exit(1);
}

/* XXX: currently only used for async signals (see signal.c) */
CPUState *global_env;

/* used to free thread contexts */
TaskState *first_task_state;

int main(int argc, char **argv)
{
    const char *filename;
    struct target_pt_regs regs1, *regs = &regs1;
    struct image_info info1, *info = &info1;
    TaskState ts1, *ts = &ts1;
    CPUState *env;
    int optind;
    const char *r;
    int gdbstub_port = 0;
    
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
            x86_stack_size = strtol(r, (char **)&r, 0);
            if (x86_stack_size <= 0)
                usage();
            if (*r == 'M')
                x86_stack_size *= 1024 * 1024;
            else if (*r == 'k' || *r == 'K')
                x86_stack_size *= 1024;
        } else if (!strcmp(r, "L")) {
            interp_prefix = argv[optind++];
        } else if (!strcmp(r, "p")) {
            qemu_host_page_size = atoi(argv[optind++]);
            if (qemu_host_page_size == 0 ||
                (qemu_host_page_size & (qemu_host_page_size - 1)) != 0) {
                fprintf(stderr, "page size must be a power of two\n");
                exit(1);
            }
        } else if (!strcmp(r, "g")) {
            gdbstub_port = atoi(argv[optind++]);
	} else if (!strcmp(r, "r")) {
	    qemu_uname_release = argv[optind++];
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

    /* Zero out image_info */
    memset(info, 0, sizeof(struct image_info));

    /* Scan interp_prefix dir for replacement files. */
    init_paths(interp_prefix);

    /* NOTE: we need to init the CPU at this stage to get
       qemu_host_page_size */
    env = cpu_init();
    global_env = env;
    
    if (loader_exec(filename, argv+optind, environ, regs, info) != 0) {
	printf("Error loading %s\n", filename);
	_exit(1);
    }
    
    if (loglevel) {
        page_dump(logfile);
    
        fprintf(logfile, "start_brk   0x%08lx\n" , info->start_brk);
        fprintf(logfile, "end_code    0x%08lx\n" , info->end_code);
        fprintf(logfile, "start_code  0x%08lx\n" , info->start_code);
        fprintf(logfile, "start_data  0x%08lx\n" , info->start_data);
        fprintf(logfile, "end_data    0x%08lx\n" , info->end_data);
        fprintf(logfile, "start_stack 0x%08lx\n" , info->start_stack);
        fprintf(logfile, "brk         0x%08lx\n" , info->brk);
        fprintf(logfile, "entry       0x%08lx\n" , info->entry);
    }

    target_set_brk(info->brk);
    syscall_init();
    signal_init();

    /* build Task State */
    memset(ts, 0, sizeof(TaskState));
    env->opaque = ts;
    ts->used = 1;
    ts->info = info;
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
    
    /* linux register setup */
    env->regs[R_EAX] = regs->eax;
    env->regs[R_EBX] = regs->ebx;
    env->regs[R_ECX] = regs->ecx;
    env->regs[R_EDX] = regs->edx;
    env->regs[R_ESI] = regs->esi;
    env->regs[R_EDI] = regs->edi;
    env->regs[R_EBP] = regs->ebp;
    env->regs[R_ESP] = regs->esp;
    env->eip = regs->eip;

    /* linux interrupt setup */
    env->idt.base = h2g(idt_table);
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
    set_idt(0x80, 3);

    /* linux segment setup */
    env->gdt.base = h2g(gdt_table);
    env->gdt.limit = sizeof(gdt_table) - 1;
    write_dt(&gdt_table[__USER_CS >> 3], 0, 0xfffff,
             DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK | 
             (3 << DESC_DPL_SHIFT) | (0xa << DESC_TYPE_SHIFT));
    write_dt(&gdt_table[__USER_DS >> 3], 0, 0xfffff,
             DESC_G_MASK | DESC_B_MASK | DESC_P_MASK | DESC_S_MASK | 
             (3 << DESC_DPL_SHIFT) | (0x2 << DESC_TYPE_SHIFT));
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_FS, __USER_DS);
    cpu_x86_load_seg(env, R_GS, __USER_DS);

#elif defined(TARGET_ARM)
    {
        int i;
        cpu_arm_set_model(env, ARM_CPUID_ARM1026);
        cpsr_write(env, regs->uregs[16], 0xffffffff);
        for(i = 0; i < 16; i++) {
            env->regs[i] = regs->uregs[i];
        }
        ts->stack_base = info->start_stack;
        ts->heap_base = info->brk;
        /* This will be filled in on the first SYS_HEAPINFO call.  */
        ts->heap_limit = 0;
    }
#elif defined(TARGET_SPARC)
    {
        int i;
	env->pc = regs->pc;
	env->npc = regs->npc;
        env->y = regs->y;
        for(i = 0; i < 8; i++)
            env->gregs[i] = regs->u_regs[i];
        for(i = 0; i < 8; i++)
            env->regwptr[i] = regs->u_regs[i + 8];
    }
#elif defined(TARGET_PPC)
    {
        ppc_def_t *def;
        int i;

        /* Choose and initialise CPU */
        /* XXX: CPU model (or PVR) should be provided on command line */
        //        ppc_find_by_name("750gx", &def);
        //        ppc_find_by_name("750fx", &def);
        //        ppc_find_by_name("750p", &def);
        ppc_find_by_name("750", &def);
        //        ppc_find_by_name("G3", &def);
        //        ppc_find_by_name("604r", &def);
        //        ppc_find_by_name("604e", &def);
        //        ppc_find_by_name("604", &def);
        if (def == NULL) {
            cpu_abort(env,
                      "Unable to find PowerPC CPU definition\n");
        }
        cpu_ppc_register(env, def);

        for (i = 0; i < 32; i++) {
            if (i != 12 && i != 6 && i != 13)
                env->msr[i] = (regs->msr >> i) & 1;
        }
        env->nip = regs->nip;
        for(i = 0; i < 32; i++) {
            env->gpr[i] = regs->gpr[i];
        }
    }
#elif defined(TARGET_M68K)
    {
        m68k_def_t *def;
        def = m68k_find_by_name("cfv4e");
        if (def == NULL) {
            cpu_abort(cpu_single_env,
                      "Unable to find m68k CPU definition\n");
        }
        cpu_m68k_register(cpu_single_env, def);
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
#elif defined(TARGET_MIPS)
    {
        int i;

        for(i = 0; i < 32; i++) {
            env->gpr[i] = regs->regs[i];
        }
        env->PC = regs->cp0_epc;
#ifdef MIPS_USES_FPU
        env->CP0_Status |= (1 << CP0St_CU1);
#endif
    }
#elif defined(TARGET_SH4)
    {
        int i;

        for(i = 0; i < 16; i++) {
            env->gregs[i] = regs->regs[i];
        }
        env->pc = regs->pc;
    }
#else
#error unsupported target CPU
#endif

    if (gdbstub_port) {
        gdbserver_start (gdbstub_port);
        gdb_handlesig(env, 0);
    }
    cpu_loop(env);
    /* never exits */
    return 0;
}
