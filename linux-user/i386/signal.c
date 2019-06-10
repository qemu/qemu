/*
 *  Emulation of Linux signals
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu.h"
#include "signal-common.h"
#include "linux-user/trace.h"

/* from the Linux kernel - /arch/x86/include/uapi/asm/sigcontext.h */

struct target_fpreg {
    uint16_t significand[4];
    uint16_t exponent;
};

struct target_fpxreg {
    uint16_t significand[4];
    uint16_t exponent;
    uint16_t padding[3];
};

struct target_xmmreg {
    uint32_t element[4];
};

struct target_fpstate_32 {
    /* Regular FPU environment */
    uint32_t cw;
    uint32_t sw;
    uint32_t tag;
    uint32_t ipoff;
    uint32_t cssel;
    uint32_t dataoff;
    uint32_t datasel;
    struct target_fpreg st[8];
    uint16_t  status;
    uint16_t  magic;          /* 0xffff = regular FPU data only */

    /* FXSR FPU environment */
    uint32_t _fxsr_env[6];   /* FXSR FPU env is ignored */
    uint32_t mxcsr;
    uint32_t reserved;
    struct target_fpxreg fxsr_st[8]; /* FXSR FPU reg data is ignored */
    struct target_xmmreg xmm[8];
    uint32_t padding[56];
};

struct target_fpstate_64 {
    /* FXSAVE format */
    uint16_t cw;
    uint16_t sw;
    uint16_t twd;
    uint16_t fop;
    uint64_t rip;
    uint64_t rdp;
    uint32_t mxcsr;
    uint32_t mxcsr_mask;
    uint32_t st_space[32];
    uint32_t xmm_space[64];
    uint32_t reserved[24];
};

#ifndef TARGET_X86_64
# define target_fpstate target_fpstate_32
#else
# define target_fpstate target_fpstate_64
#endif

struct target_sigcontext_32 {
    uint16_t gs, __gsh;
    uint16_t fs, __fsh;
    uint16_t es, __esh;
    uint16_t ds, __dsh;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t trapno;
    uint32_t err;
    uint32_t eip;
    uint16_t cs, __csh;
    uint32_t eflags;
    uint32_t esp_at_signal;
    uint16_t ss, __ssh;
    uint32_t fpstate; /* pointer */
    uint32_t oldmask;
    uint32_t cr2;
};

struct target_sigcontext_64 {
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rsp;
    uint64_t rip;

    uint64_t eflags;

    uint16_t cs;
    uint16_t gs;
    uint16_t fs;
    uint16_t ss;

    uint64_t err;
    uint64_t trapno;
    uint64_t oldmask;
    uint64_t cr2;

    uint64_t fpstate; /* pointer */
    uint64_t padding[8];
};

#ifndef TARGET_X86_64
# define target_sigcontext target_sigcontext_32
#else
# define target_sigcontext target_sigcontext_64
#endif

/* see Linux/include/uapi/asm-generic/ucontext.h */
struct target_ucontext {
    abi_ulong         tuc_flags;
    abi_ulong         tuc_link;
    target_stack_t    tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t   tuc_sigmask;  /* mask last for extensibility */
};

#ifndef TARGET_X86_64
struct sigframe {
    abi_ulong pretcode;
    int sig;
    struct target_sigcontext sc;
    struct target_fpstate fpstate;
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    char retcode[8];
};

struct rt_sigframe {
    abi_ulong pretcode;
    int sig;
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext uc;
    struct target_fpstate fpstate;
    char retcode[8];
};

#else

struct rt_sigframe {
    abi_ulong pretcode;
    struct target_ucontext uc;
    struct target_siginfo info;
    struct target_fpstate fpstate;
};

#endif

/*
 * Set up a signal frame.
 */

/* XXX: save x87 state */
static void setup_sigcontext(struct target_sigcontext *sc,
        struct target_fpstate *fpstate, CPUX86State *env, abi_ulong mask,
        abi_ulong fpstate_addr)
{
    CPUState *cs = env_cpu(env);
#ifndef TARGET_X86_64
    uint16_t magic;

    /* already locked in setup_frame() */
    __put_user(env->segs[R_GS].selector, (unsigned int *)&sc->gs);
    __put_user(env->segs[R_FS].selector, (unsigned int *)&sc->fs);
    __put_user(env->segs[R_ES].selector, (unsigned int *)&sc->es);
    __put_user(env->segs[R_DS].selector, (unsigned int *)&sc->ds);
    __put_user(env->regs[R_EDI], &sc->edi);
    __put_user(env->regs[R_ESI], &sc->esi);
    __put_user(env->regs[R_EBP], &sc->ebp);
    __put_user(env->regs[R_ESP], &sc->esp);
    __put_user(env->regs[R_EBX], &sc->ebx);
    __put_user(env->regs[R_EDX], &sc->edx);
    __put_user(env->regs[R_ECX], &sc->ecx);
    __put_user(env->regs[R_EAX], &sc->eax);
    __put_user(cs->exception_index, &sc->trapno);
    __put_user(env->error_code, &sc->err);
    __put_user(env->eip, &sc->eip);
    __put_user(env->segs[R_CS].selector, (unsigned int *)&sc->cs);
    __put_user(env->eflags, &sc->eflags);
    __put_user(env->regs[R_ESP], &sc->esp_at_signal);
    __put_user(env->segs[R_SS].selector, (unsigned int *)&sc->ss);

    cpu_x86_fsave(env, fpstate_addr, 1);
    fpstate->status = fpstate->sw;
    magic = 0xffff;
    __put_user(magic, &fpstate->magic);
    __put_user(fpstate_addr, &sc->fpstate);

    /* non-iBCS2 extensions.. */
    __put_user(mask, &sc->oldmask);
    __put_user(env->cr[2], &sc->cr2);
#else
    __put_user(env->regs[R_EDI], &sc->rdi);
    __put_user(env->regs[R_ESI], &sc->rsi);
    __put_user(env->regs[R_EBP], &sc->rbp);
    __put_user(env->regs[R_ESP], &sc->rsp);
    __put_user(env->regs[R_EBX], &sc->rbx);
    __put_user(env->regs[R_EDX], &sc->rdx);
    __put_user(env->regs[R_ECX], &sc->rcx);
    __put_user(env->regs[R_EAX], &sc->rax);

    __put_user(env->regs[8], &sc->r8);
    __put_user(env->regs[9], &sc->r9);
    __put_user(env->regs[10], &sc->r10);
    __put_user(env->regs[11], &sc->r11);
    __put_user(env->regs[12], &sc->r12);
    __put_user(env->regs[13], &sc->r13);
    __put_user(env->regs[14], &sc->r14);
    __put_user(env->regs[15], &sc->r15);

    __put_user(cs->exception_index, &sc->trapno);
    __put_user(env->error_code, &sc->err);
    __put_user(env->eip, &sc->rip);

    __put_user(env->eflags, &sc->eflags);
    __put_user(env->segs[R_CS].selector, &sc->cs);
    __put_user((uint16_t)0, &sc->gs);
    __put_user((uint16_t)0, &sc->fs);
    __put_user(env->segs[R_SS].selector, &sc->ss);

    __put_user(mask, &sc->oldmask);
    __put_user(env->cr[2], &sc->cr2);

    /* fpstate_addr must be 16 byte aligned for fxsave */
    assert(!(fpstate_addr & 0xf));

    cpu_x86_fxsave(env, fpstate_addr);
    __put_user(fpstate_addr, &sc->fpstate);
#endif
}

/*
 * Determine which stack to use..
 */

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUX86State *env, size_t frame_size)
{
    unsigned long esp;

    /* Default to using normal stack */
    esp = get_sp_from_cpustate(env);
#ifdef TARGET_X86_64
    esp -= 128; /* this is the redzone */
#endif

    /* This is the X/Open sanctioned signal stack switching.  */
    if (ka->sa_flags & TARGET_SA_ONSTACK) {
        esp = target_sigsp(esp, ka);
    } else {
#ifndef TARGET_X86_64
        /* This is the legacy signal stack switching. */
        if ((env->segs[R_SS].selector & 0xffff) != __USER_DS &&
                !(ka->sa_flags & TARGET_SA_RESTORER) &&
                ka->sa_restorer) {
            esp = (unsigned long) ka->sa_restorer;
        }
#endif
    }

#ifndef TARGET_X86_64
    return (esp - frame_size) & -8ul;
#else
    return ((esp - frame_size) & (~15ul)) - 8;
#endif
}

#ifndef TARGET_X86_64
/* compare linux/arch/i386/kernel/signal.c:setup_frame() */
void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUX86State *env)
{
    abi_ulong frame_addr;
    struct sigframe *frame;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_frame(env, frame_addr);

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
        goto give_sigsegv;

    __put_user(sig, &frame->sig);

    setup_sigcontext(&frame->sc, &frame->fpstate, env, set->sig[0],
            frame_addr + offsetof(struct sigframe, fpstate));

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->extramask[i - 1]);
    }

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        __put_user(ka->sa_restorer, &frame->pretcode);
    } else {
        uint16_t val16;
        abi_ulong retcode_addr;
        retcode_addr = frame_addr + offsetof(struct sigframe, retcode);
        __put_user(retcode_addr, &frame->pretcode);
        /* This is popl %eax ; movl $,%eax ; int $0x80 */
        val16 = 0xb858;
        __put_user(val16, (uint16_t *)(frame->retcode+0));
        __put_user(TARGET_NR_sigreturn, (int *)(frame->retcode+2));
        val16 = 0x80cd;
        __put_user(val16, (uint16_t *)(frame->retcode+6));
    }

    /* Set up registers for signal handler */
    env->regs[R_ESP] = frame_addr;
    env->eip = ka->_sa_handler;

    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    env->eflags &= ~TF_MASK;

    unlock_user_struct(frame, frame_addr, 1);

    return;

give_sigsegv:
    force_sigsegv(sig);
}
#endif

/* compare linux/arch/x86/kernel/signal.c:setup_rt_frame() */
void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUX86State *env)
{
    abi_ulong frame_addr;
#ifndef TARGET_X86_64
    abi_ulong addr;
#endif
    struct rt_sigframe *frame;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
        goto give_sigsegv;

    /* These fields are only in rt_sigframe on 32 bit */
#ifndef TARGET_X86_64
    __put_user(sig, &frame->sig);
    addr = frame_addr + offsetof(struct rt_sigframe, info);
    __put_user(addr, &frame->pinfo);
    addr = frame_addr + offsetof(struct rt_sigframe, uc);
    __put_user(addr, &frame->puc);
#endif
    if (ka->sa_flags & TARGET_SA_SIGINFO) {
        tswap_siginfo(&frame->info, info);
    }

    /* Create the ucontext.  */
    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    setup_sigcontext(&frame->uc.tuc_mcontext, &frame->fpstate, env,
            set->sig[0], frame_addr + offsetof(struct rt_sigframe, fpstate));

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    /* Set up to return from userspace.  If provided, use a stub
       already in userspace.  */
#ifndef TARGET_X86_64
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        __put_user(ka->sa_restorer, &frame->pretcode);
    } else {
        uint16_t val16;
        addr = frame_addr + offsetof(struct rt_sigframe, retcode);
        __put_user(addr, &frame->pretcode);
        /* This is movl $,%eax ; int $0x80 */
        __put_user(0xb8, (char *)(frame->retcode+0));
        __put_user(TARGET_NR_rt_sigreturn, (int *)(frame->retcode+1));
        val16 = 0x80cd;
        __put_user(val16, (uint16_t *)(frame->retcode+5));
    }
#else
    /* XXX: Would be slightly better to return -EFAULT here if test fails
       assert(ka->sa_flags & TARGET_SA_RESTORER); */
    __put_user(ka->sa_restorer, &frame->pretcode);
#endif

    /* Set up registers for signal handler */
    env->regs[R_ESP] = frame_addr;
    env->eip = ka->_sa_handler;

#ifndef TARGET_X86_64
    env->regs[R_EAX] = sig;
    env->regs[R_EDX] = (unsigned long)&frame->info;
    env->regs[R_ECX] = (unsigned long)&frame->uc;
#else
    env->regs[R_EAX] = 0;
    env->regs[R_EDI] = sig;
    env->regs[R_ESI] = (unsigned long)&frame->info;
    env->regs[R_EDX] = (unsigned long)&frame->uc;
#endif

    cpu_x86_load_seg(env, R_DS, __USER_DS);
    cpu_x86_load_seg(env, R_ES, __USER_DS);
    cpu_x86_load_seg(env, R_CS, __USER_CS);
    cpu_x86_load_seg(env, R_SS, __USER_DS);
    env->eflags &= ~TF_MASK;

    unlock_user_struct(frame, frame_addr, 1);

    return;

give_sigsegv:
    force_sigsegv(sig);
}

static int
restore_sigcontext(CPUX86State *env, struct target_sigcontext *sc)
{
    unsigned int err = 0;
    abi_ulong fpstate_addr;
    unsigned int tmpflags;

#ifndef TARGET_X86_64
    cpu_x86_load_seg(env, R_GS, tswap16(sc->gs));
    cpu_x86_load_seg(env, R_FS, tswap16(sc->fs));
    cpu_x86_load_seg(env, R_ES, tswap16(sc->es));
    cpu_x86_load_seg(env, R_DS, tswap16(sc->ds));

    env->regs[R_EDI] = tswapl(sc->edi);
    env->regs[R_ESI] = tswapl(sc->esi);
    env->regs[R_EBP] = tswapl(sc->ebp);
    env->regs[R_ESP] = tswapl(sc->esp);
    env->regs[R_EBX] = tswapl(sc->ebx);
    env->regs[R_EDX] = tswapl(sc->edx);
    env->regs[R_ECX] = tswapl(sc->ecx);
    env->regs[R_EAX] = tswapl(sc->eax);

    env->eip = tswapl(sc->eip);
#else
    env->regs[8] = tswapl(sc->r8);
    env->regs[9] = tswapl(sc->r9);
    env->regs[10] = tswapl(sc->r10);
    env->regs[11] = tswapl(sc->r11);
    env->regs[12] = tswapl(sc->r12);
    env->regs[13] = tswapl(sc->r13);
    env->regs[14] = tswapl(sc->r14);
    env->regs[15] = tswapl(sc->r15);

    env->regs[R_EDI] = tswapl(sc->rdi);
    env->regs[R_ESI] = tswapl(sc->rsi);
    env->regs[R_EBP] = tswapl(sc->rbp);
    env->regs[R_EBX] = tswapl(sc->rbx);
    env->regs[R_EDX] = tswapl(sc->rdx);
    env->regs[R_EAX] = tswapl(sc->rax);
    env->regs[R_ECX] = tswapl(sc->rcx);
    env->regs[R_ESP] = tswapl(sc->rsp);

    env->eip = tswapl(sc->rip);
#endif

    cpu_x86_load_seg(env, R_CS, lduw_p(&sc->cs) | 3);
    cpu_x86_load_seg(env, R_SS, lduw_p(&sc->ss) | 3);

    tmpflags = tswapl(sc->eflags);
    env->eflags = (env->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);
    //          regs->orig_eax = -1;            /* disable syscall checks */

    fpstate_addr = tswapl(sc->fpstate);
    if (fpstate_addr != 0) {
        if (!access_ok(VERIFY_READ, fpstate_addr,
                       sizeof(struct target_fpstate)))
            goto badframe;
#ifndef TARGET_X86_64
        cpu_x86_frstor(env, fpstate_addr, 1);
#else
        cpu_x86_fxrstor(env, fpstate_addr);
#endif
    }

    return err;
badframe:
    return 1;
}

/* Note: there is no sigreturn on x86_64, there is only rt_sigreturn */
#ifndef TARGET_X86_64
long do_sigreturn(CPUX86State *env)
{
    struct sigframe *frame;
    abi_ulong frame_addr = env->regs[R_ESP] - 8;
    target_sigset_t target_set;
    sigset_t set;
    int i;

    trace_user_do_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;
    /* set blocked signals */
    __get_user(target_set.sig[0], &frame->sc.oldmask);
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &frame->extramask[i - 1]);
    }

    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set);

    /* restore registers */
    if (restore_sigcontext(env, &frame->sc))
        goto badframe;
    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
#endif

long do_rt_sigreturn(CPUX86State *env)
{
    abi_ulong frame_addr;
    struct rt_sigframe *frame;
    sigset_t set;

    frame_addr = env->regs[R_ESP] - sizeof(abi_ulong);
    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    if (restore_sigcontext(env, &frame->uc.tuc_mcontext)) {
        goto badframe;
    }

    if (do_sigaltstack(frame_addr + offsetof(struct rt_sigframe, uc.tuc_stack), 0,
                       get_sp_from_cpustate(env)) == -EFAULT) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
