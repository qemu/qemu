/*
 *  i386 emulator main execution loop
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"
#ifdef TARGET_I386
#include "exec-i386.h"
#endif
#ifdef TARGET_ARM
#include "exec-arm.h"
#endif

#include "disas.h"

//#define DEBUG_EXEC
//#define DEBUG_SIGNAL

#if defined(TARGET_ARM)
/* XXX: unify with i386 target */
void cpu_loop_exit(void)
{
    longjmp(env->jmp_env, 1);
}
#endif

/* main execution loop */

int cpu_exec(CPUState *env1)
{
    int saved_T0, saved_T1, saved_T2;
    CPUState *saved_env;
#ifdef reg_EAX
    int saved_EAX;
#endif
#ifdef reg_ECX
    int saved_ECX;
#endif
#ifdef reg_EDX
    int saved_EDX;
#endif
#ifdef reg_EBX
    int saved_EBX;
#endif
#ifdef reg_ESP
    int saved_ESP;
#endif
#ifdef reg_EBP
    int saved_EBP;
#endif
#ifdef reg_ESI
    int saved_ESI;
#endif
#ifdef reg_EDI
    int saved_EDI;
#endif
#ifdef __sparc__
    int saved_i7, tmp_T0;
#endif
    int code_gen_size, ret, interrupt_request;
    void (*gen_func)(void);
    TranslationBlock *tb, **ptb;
    uint8_t *tc_ptr, *cs_base, *pc;
    unsigned int flags;

    /* first we save global registers */
    saved_T0 = T0;
    saved_T1 = T1;
    saved_T2 = T2;
    saved_env = env;
    env = env1;
#ifdef __sparc__
    /* we also save i7 because longjmp may not restore it */
    asm volatile ("mov %%i7, %0" : "=r" (saved_i7));
#endif

#if defined(TARGET_I386)
#ifdef reg_EAX
    saved_EAX = EAX;
    EAX = env->regs[R_EAX];
#endif
#ifdef reg_ECX
    saved_ECX = ECX;
    ECX = env->regs[R_ECX];
#endif
#ifdef reg_EDX
    saved_EDX = EDX;
    EDX = env->regs[R_EDX];
#endif
#ifdef reg_EBX
    saved_EBX = EBX;
    EBX = env->regs[R_EBX];
#endif
#ifdef reg_ESP
    saved_ESP = ESP;
    ESP = env->regs[R_ESP];
#endif
#ifdef reg_EBP
    saved_EBP = EBP;
    EBP = env->regs[R_EBP];
#endif
#ifdef reg_ESI
    saved_ESI = ESI;
    ESI = env->regs[R_ESI];
#endif
#ifdef reg_EDI
    saved_EDI = EDI;
    EDI = env->regs[R_EDI];
#endif
    
    /* put eflags in CPU temporary format */
    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#elif defined(TARGET_ARM)
    {
        unsigned int psr;
        psr = env->cpsr;
        env->CF = (psr >> 29) & 1;
        env->NZF = (psr & 0xc0000000) ^ 0x40000000;
        env->VF = (psr << 3) & 0x80000000;
        env->cpsr = psr & ~0xf0000000;
    }
#else
#error unsupported target CPU
#endif
    env->exception_index = -1;

    /* prepare setjmp context for exception handling */
    for(;;) {
        if (setjmp(env->jmp_env) == 0) {
            /* if an exception is pending, we execute it here */
            if (env->exception_index >= 0) {
                if (env->exception_index >= EXCP_INTERRUPT) {
                    /* exit request from the cpu execution loop */
                    ret = env->exception_index;
                    break;
                } else if (env->user_mode_only) {
                    /* if user mode only, we simulate a fake exception
                       which will be hanlded outside the cpu execution
                       loop */
#if defined(TARGET_I386)
                    do_interrupt_user(env->exception_index, 
                                      env->exception_is_int, 
                                      env->error_code, 
                                      env->exception_next_eip);
#endif
                    ret = env->exception_index;
                    break;
                } else {
#if defined(TARGET_I386)
                    /* simulate a real cpu exception. On i386, it can
                       trigger new exceptions, but we do not handle
                       double or triple faults yet. */
                    do_interrupt(env->exception_index, 
                                 env->exception_is_int, 
                                 env->error_code, 
                                 env->exception_next_eip, 0);
#endif
                }
                env->exception_index = -1;
            }
            T0 = 0; /* force lookup of first TB */
            for(;;) {
#ifdef __sparc__
                /* g1 can be modified by some libc? functions */ 
                tmp_T0 = T0;
#endif	    
                interrupt_request = env->interrupt_request;
                if (interrupt_request) {
#if defined(TARGET_I386)
                    /* if hardware interrupt pending, we execute it */
                    if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                        (env->eflags & IF_MASK)) {
                        int intno;
                        intno = cpu_x86_get_pic_interrupt(env);
                        if (loglevel) {
                            fprintf(logfile, "Servicing hardware INT=0x%02x\n", intno);
                        }
                        do_interrupt(intno, 0, 0, 0, 1);
                        env->interrupt_request &= ~CPU_INTERRUPT_HARD;
                        /* ensure that no TB jump will be modified as
                           the program flow was changed */
#ifdef __sparc__
                        tmp_T0 = 0;
#else
                        T0 = 0;
#endif
                    }
#endif
                    if (interrupt_request & CPU_INTERRUPT_EXIT) {
                        env->interrupt_request &= ~CPU_INTERRUPT_EXIT;
                        env->exception_index = EXCP_INTERRUPT;
                        cpu_loop_exit();
                    }
                }
#ifdef DEBUG_EXEC
                if (loglevel) {
#if defined(TARGET_I386)
                    /* restore flags in standard format */
                    env->regs[R_EAX] = EAX;
                    env->regs[R_EBX] = EBX;
                    env->regs[R_ECX] = ECX;
                    env->regs[R_EDX] = EDX;
                    env->regs[R_ESI] = ESI;
                    env->regs[R_EDI] = EDI;
                    env->regs[R_EBP] = EBP;
                    env->regs[R_ESP] = ESP;
                    env->eflags = env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);
                    cpu_x86_dump_state(env, logfile, X86_DUMP_CCOP);
                    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
#elif defined(TARGET_ARM)
                    env->cpsr = compute_cpsr();
                    cpu_arm_dump_state(env, logfile, 0);
                    env->cpsr &= ~0xf0000000;
#else
#error unsupported target CPU 
#endif
                }
#endif
                /* we compute the CPU state. We assume it will not
                   change during the whole generated block. */
#if defined(TARGET_I386)
                flags = (env->segs[R_CS].flags & DESC_B_MASK)
                    >> (DESC_B_SHIFT - GEN_FLAG_CODE32_SHIFT);
                flags |= (env->segs[R_SS].flags & DESC_B_MASK)
                    >> (DESC_B_SHIFT - GEN_FLAG_SS32_SHIFT);
                flags |= (((unsigned long)env->segs[R_DS].base | 
                           (unsigned long)env->segs[R_ES].base |
                           (unsigned long)env->segs[R_SS].base) != 0) << 
                    GEN_FLAG_ADDSEG_SHIFT;
                flags |= env->cpl << GEN_FLAG_CPL_SHIFT;
                flags |= env->soft_mmu << GEN_FLAG_SOFT_MMU_SHIFT;
                flags |= (env->eflags & VM_MASK) >> (17 - GEN_FLAG_VM_SHIFT);
                flags |= (env->eflags & (IOPL_MASK | TF_MASK));
                cs_base = env->segs[R_CS].base;
                pc = cs_base + env->eip;
#elif defined(TARGET_ARM)
                flags = 0;
                cs_base = 0;
                pc = (uint8_t *)env->regs[15];
#else
#error unsupported CPU
#endif
                tb = tb_find(&ptb, (unsigned long)pc, (unsigned long)cs_base, 
                             flags);
                if (!tb) {
                    spin_lock(&tb_lock);
                    /* if no translated code available, then translate it now */
                    tb = tb_alloc((unsigned long)pc);
                    if (!tb) {
                        /* flush must be done */
                        tb_flush();
                        /* cannot fail at this point */
                        tb = tb_alloc((unsigned long)pc);
                        /* don't forget to invalidate previous TB info */
                        ptb = &tb_hash[tb_hash_func((unsigned long)pc)];
                        T0 = 0;
                    }
                    tc_ptr = code_gen_ptr;
                    tb->tc_ptr = tc_ptr;
                    tb->cs_base = (unsigned long)cs_base;
                    tb->flags = flags;
                    ret = cpu_gen_code(env, tb, CODE_GEN_MAX_SIZE, &code_gen_size);
#if defined(TARGET_I386)
                    /* XXX: suppress that, this is incorrect */
                    /* if invalid instruction, signal it */
                    if (ret != 0) {
                        /* NOTE: the tb is allocated but not linked, so we
                           can leave it */
                        spin_unlock(&tb_lock);
                        raise_exception(EXCP06_ILLOP);
                    }
#endif
                    *ptb = tb;
                    tb->hash_next = NULL;
                    tb_link(tb);
                    code_gen_ptr = (void *)(((unsigned long)code_gen_ptr + code_gen_size + CODE_GEN_ALIGN - 1) & ~(CODE_GEN_ALIGN - 1));
                    spin_unlock(&tb_lock);
                }
#ifdef DEBUG_EXEC
                if (loglevel) {
                    fprintf(logfile, "Trace 0x%08lx [0x%08lx] %s\n",
                            (long)tb->tc_ptr, (long)tb->pc,
                            lookup_symbol((void *)tb->pc));
                }
#endif
#ifdef __sparc__
                T0 = tmp_T0;
#endif	    
                /* see if we can patch the calling TB. XXX: remove TF test */
                if (T0 != 0
#if defined(TARGET_I386)
                    && !(env->eflags & TF_MASK)
#endif
                    ) {
                    spin_lock(&tb_lock);
                    tb_add_jump((TranslationBlock *)(T0 & ~3), T0 & 3, tb);
                    spin_unlock(&tb_lock);
                }
                tc_ptr = tb->tc_ptr;
                env->current_tb = tb;
                /* execute the generated code */
                gen_func = (void *)tc_ptr;
#if defined(__sparc__)
                __asm__ __volatile__("call	%0\n\t"
                                     "mov	%%o7,%%i0"
                                     : /* no outputs */
                                     : "r" (gen_func) 
                                     : "i0", "i1", "i2", "i3", "i4", "i5");
#elif defined(__arm__)
                asm volatile ("mov pc, %0\n\t"
                              ".global exec_loop\n\t"
                              "exec_loop:\n\t"
                              : /* no outputs */
                              : "r" (gen_func)
                              : "r1", "r2", "r3", "r8", "r9", "r10", "r12", "r14");
#else
                gen_func();
#endif
                env->current_tb = NULL;
                /* reset soft MMU for next block (it can currently
                   only be set by a memory fault) */
#if defined(TARGET_I386) && !defined(CONFIG_SOFTMMU)
                if (env->soft_mmu) {
                    env->soft_mmu = 0;
                    /* do not allow linking to another block */
                    T0 = 0;
                }
#endif
            }
        } else {
        }
    } /* for(;;) */


#if defined(TARGET_I386)
    /* restore flags in standard format */
    env->eflags = env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);

    /* restore global registers */
#ifdef reg_EAX
    EAX = saved_EAX;
#endif
#ifdef reg_ECX
    ECX = saved_ECX;
#endif
#ifdef reg_EDX
    EDX = saved_EDX;
#endif
#ifdef reg_EBX
    EBX = saved_EBX;
#endif
#ifdef reg_ESP
    ESP = saved_ESP;
#endif
#ifdef reg_EBP
    EBP = saved_EBP;
#endif
#ifdef reg_ESI
    ESI = saved_ESI;
#endif
#ifdef reg_EDI
    EDI = saved_EDI;
#endif
#elif defined(TARGET_ARM)
    env->cpsr = compute_cpsr();
#else
#error unsupported target CPU
#endif
#ifdef __sparc__
    asm volatile ("mov %0, %%i7" : : "r" (saved_i7));
#endif
    T0 = saved_T0;
    T1 = saved_T1;
    T2 = saved_T2;
    env = saved_env;
    return ret;
}

#if defined(TARGET_I386)

void cpu_x86_load_seg(CPUX86State *s, int seg_reg, int selector)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;
    if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK)) {
        SegmentCache *sc;
        selector &= 0xffff;
        sc = &env->segs[seg_reg];
        sc->base = (void *)(selector << 4);
        sc->limit = 0xffff;
        sc->flags = 0;
        sc->selector = selector;
    } else {
        load_seg(seg_reg, selector, 0);
    }
    env = saved_env;
}

void cpu_x86_fsave(CPUX86State *s, uint8_t *ptr, int data32)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;
    
    helper_fsave(ptr, data32);

    env = saved_env;
}

void cpu_x86_frstor(CPUX86State *s, uint8_t *ptr, int data32)
{
    CPUX86State *saved_env;

    saved_env = env;
    env = s;
    
    helper_frstor(ptr, data32);

    env = saved_env;
}

#endif /* TARGET_I386 */

#undef EAX
#undef ECX
#undef EDX
#undef EBX
#undef ESP
#undef EBP
#undef ESI
#undef EDI
#undef EIP
#include <signal.h>
#include <sys/ucontext.h>

#if defined(TARGET_I386)

/* 'pc' is the host PC at which the exception was raised. 'address' is
   the effective address of the memory exception. 'is_write' is 1 if a
   write caused the exception and otherwise 0'. 'old_set' is the
   signal set which should be restored */
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set)
{
    TranslationBlock *tb;
    int ret;

    if (cpu_single_env)
        env = cpu_single_env; /* XXX: find a correct solution for multithread */
#if defined(DEBUG_SIGNAL)
    printf("qemu: SIGSEGV pc=0x%08lx address=%08lx w=%d oldset=0x%08lx\n", 
           pc, address, is_write, *(unsigned long *)old_set);
#endif
    /* XXX: locking issue */
    if (is_write && page_unprotect(address)) {
        return 1;
    }
    /* see if it is an MMU fault */
    ret = cpu_x86_handle_mmu_fault(env, address, is_write);
    if (ret < 0)
        return 0; /* not an MMU fault */
    if (ret == 0)
        return 1; /* the MMU fault was handled without causing real CPU fault */
    /* now we have a real cpu fault */
    tb = tb_find_pc(pc);
    if (tb) {
        /* the PC is inside the translated code. It means that we have
           a virtual CPU fault */
        cpu_restore_state(tb, env, pc);
    }
    if (ret == 1) {
#if 0
        printf("PF exception: EIP=0x%08x CR2=0x%08x error=0x%x\n", 
               env->eip, env->cr[2], env->error_code);
#endif
        /* we restore the process signal mask as the sigreturn should
           do it (XXX: use sigsetjmp) */
        sigprocmask(SIG_SETMASK, old_set, NULL);
        raise_exception_err(EXCP0E_PAGE, env->error_code);
    } else {
        /* activate soft MMU for this block */
        env->soft_mmu = 1;
        sigprocmask(SIG_SETMASK, old_set, NULL);
        cpu_loop_exit();
    }
    /* never comes here */
    return 1;
}

#elif defined(TARGET_ARM)
static inline int handle_cpu_signal(unsigned long pc, unsigned long address,
                                    int is_write, sigset_t *old_set)
{
    /* XXX: do more */
    return 0;
}
#else
#error unsupported target CPU
#endif

#if defined(__i386__)

int cpu_signal_handler(int host_signum, struct siginfo *info, 
                       void *puc)
{
    struct ucontext *uc = puc;
    unsigned long pc;
    
#ifndef REG_EIP
/* for glibc 2.1 */
#define REG_EIP    EIP
#define REG_ERR    ERR
#define REG_TRAPNO TRAPNO
#endif
    pc = uc->uc_mcontext.gregs[REG_EIP];
    return handle_cpu_signal(pc, (unsigned long)info->si_addr, 
                             uc->uc_mcontext.gregs[REG_TRAPNO] == 0xe ? 
                             (uc->uc_mcontext.gregs[REG_ERR] >> 1) & 1 : 0,
                             &uc->uc_sigmask);
}

#elif defined(__powerpc)

int cpu_signal_handler(int host_signum, struct siginfo *info, 
                       void *puc)
{
    struct ucontext *uc = puc;
    struct pt_regs *regs = uc->uc_mcontext.regs;
    unsigned long pc;
    int is_write;

    pc = regs->nip;
    is_write = 0;
#if 0
    /* ppc 4xx case */
    if (regs->dsisr & 0x00800000)
        is_write = 1;
#else
    if (regs->trap != 0x400 && (regs->dsisr & 0x02000000))
        is_write = 1;
#endif
    return handle_cpu_signal(pc, (unsigned long)info->si_addr, 
                             is_write, &uc->uc_sigmask);
}

#elif defined(__alpha__)

int cpu_signal_handler(int host_signum, struct siginfo *info, 
                           void *puc)
{
    struct ucontext *uc = puc;
    uint32_t *pc = uc->uc_mcontext.sc_pc;
    uint32_t insn = *pc;
    int is_write = 0;

    /* XXX: need kernel patch to get write flag faster */
    switch (insn >> 26) {
    case 0x0d: // stw
    case 0x0e: // stb
    case 0x0f: // stq_u
    case 0x24: // stf
    case 0x25: // stg
    case 0x26: // sts
    case 0x27: // stt
    case 0x2c: // stl
    case 0x2d: // stq
    case 0x2e: // stl_c
    case 0x2f: // stq_c
	is_write = 1;
    }

    return handle_cpu_signal(pc, (unsigned long)info->si_addr, 
                             is_write, &uc->uc_sigmask);
}
#elif defined(__sparc__)

int cpu_signal_handler(int host_signum, struct siginfo *info, 
                       void *puc)
{
    uint32_t *regs = (uint32_t *)(info + 1);
    void *sigmask = (regs + 20);
    unsigned long pc;
    int is_write;
    uint32_t insn;
    
    /* XXX: is there a standard glibc define ? */
    pc = regs[1];
    /* XXX: need kernel patch to get write flag faster */
    is_write = 0;
    insn = *(uint32_t *)pc;
    if ((insn >> 30) == 3) {
      switch((insn >> 19) & 0x3f) {
      case 0x05: // stb
      case 0x06: // sth
      case 0x04: // st
      case 0x07: // std
      case 0x24: // stf
      case 0x27: // stdf
      case 0x25: // stfsr
	is_write = 1;
	break;
      }
    }
    return handle_cpu_signal(pc, (unsigned long)info->si_addr, 
                             is_write, sigmask);
}

#elif defined(__arm__)

int cpu_signal_handler(int host_signum, struct siginfo *info, 
                       void *puc)
{
    struct ucontext *uc = puc;
    unsigned long pc;
    int is_write;
    
    pc = uc->uc_mcontext.gregs[R15];
    /* XXX: compute is_write */
    is_write = 0;
    return handle_cpu_signal(pc, (unsigned long)info->si_addr, 
                             is_write,
                             &uc->uc_sigmask);
}

#elif defined(__mc68000)

int cpu_signal_handler(int host_signum, struct siginfo *info, 
                       void *puc)
{
    struct ucontext *uc = puc;
    unsigned long pc;
    int is_write;
    
    pc = uc->uc_mcontext.gregs[16];
    /* XXX: compute is_write */
    is_write = 0;
    return handle_cpu_signal(pc, (unsigned long)info->si_addr, 
                             is_write,
                             &uc->uc_sigmask);
}

#else

#error host CPU specific signal handler needed

#endif
