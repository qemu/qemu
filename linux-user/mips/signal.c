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
#include "user-internals.h"
#include "signal-common.h"
#include "linux-user/trace.h"

# if defined(TARGET_ABI_MIPSO32)
struct target_sigcontext {
    uint32_t   sc_regmask;     /* Unused */
    uint32_t   sc_status;
    uint64_t   sc_pc;
    uint64_t   sc_regs[32];
    uint64_t   sc_fpregs[32];
    uint32_t   sc_ownedfp;     /* Unused */
    uint32_t   sc_fpc_csr;
    uint32_t   sc_fpc_eir;     /* Unused */
    uint32_t   sc_used_math;
    uint32_t   sc_dsp;         /* dsp status, was sc_ssflags */
    uint32_t   pad0;
    uint64_t   sc_mdhi;
    uint64_t   sc_mdlo;
    target_ulong   sc_hi1;         /* Was sc_cause */
    target_ulong   sc_lo1;         /* Was sc_badvaddr */
    target_ulong   sc_hi2;         /* Was sc_sigset[4] */
    target_ulong   sc_lo2;
    target_ulong   sc_hi3;
    target_ulong   sc_lo3;
};
# else /* N32 || N64 */
struct target_sigcontext {
    uint64_t sc_regs[32];
    uint64_t sc_fpregs[32];
    uint64_t sc_mdhi;
    uint64_t sc_hi1;
    uint64_t sc_hi2;
    uint64_t sc_hi3;
    uint64_t sc_mdlo;
    uint64_t sc_lo1;
    uint64_t sc_lo2;
    uint64_t sc_lo3;
    uint64_t sc_pc;
    uint32_t sc_fpc_csr;
    uint32_t sc_used_math;
    uint32_t sc_dsp;
    uint32_t sc_reserved;
};
# endif /* O32 */

struct sigframe {
    uint32_t sf_ass[4];                 /* argument save space for o32 */
    uint32_t sf_code[2];                        /* signal trampoline */
    struct target_sigcontext sf_sc;
    target_sigset_t sf_mask;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;
};

struct target_rt_sigframe {
    uint32_t rs_ass[4];               /* argument save space for o32 */
    uint32_t rs_code[2];              /* signal trampoline */
    struct target_siginfo rs_info;
    struct target_ucontext rs_uc;
};

/* Install trampoline to jump back from signal handler */
static void install_sigtramp(uint32_t *tramp, unsigned int syscall)
{
    /*
     * Set up the return code ...
     *
     *         li      v0, __NR__foo_sigreturn
     *         syscall
     */

    __put_user(0x24020000 + syscall, tramp + 0);
    __put_user(0x0000000c          , tramp + 1);
}

static inline void setup_sigcontext(CPUMIPSState *regs,
                                    struct target_sigcontext *sc)
{
    int i;

    __put_user(exception_resume_pc(regs), &sc->sc_pc);
    regs->hflags &= ~MIPS_HFLAG_BMASK;

    __put_user(0, &sc->sc_regs[0]);
    for (i = 1; i < 32; ++i) {
        __put_user(regs->active_tc.gpr[i], &sc->sc_regs[i]);
    }

    __put_user(regs->active_tc.HI[0], &sc->sc_mdhi);
    __put_user(regs->active_tc.LO[0], &sc->sc_mdlo);

    /* Rather than checking for dsp existence, always copy.  The storage
       would just be garbage otherwise.  */
    __put_user(regs->active_tc.HI[1], &sc->sc_hi1);
    __put_user(regs->active_tc.HI[2], &sc->sc_hi2);
    __put_user(regs->active_tc.HI[3], &sc->sc_hi3);
    __put_user(regs->active_tc.LO[1], &sc->sc_lo1);
    __put_user(regs->active_tc.LO[2], &sc->sc_lo2);
    __put_user(regs->active_tc.LO[3], &sc->sc_lo3);
    {
        uint32_t dsp = cpu_rddsp(0x3ff, regs);
        __put_user(dsp, &sc->sc_dsp);
    }

    __put_user(1, &sc->sc_used_math);

    for (i = 0; i < 32; ++i) {
        __put_user(regs->active_fpu.fpr[i].d, &sc->sc_fpregs[i]);
    }
}

static inline void
restore_sigcontext(CPUMIPSState *regs, struct target_sigcontext *sc)
{
    int i;

    __get_user(regs->CP0_EPC, &sc->sc_pc);

    __get_user(regs->active_tc.HI[0], &sc->sc_mdhi);
    __get_user(regs->active_tc.LO[0], &sc->sc_mdlo);

    for (i = 1; i < 32; ++i) {
        __get_user(regs->active_tc.gpr[i], &sc->sc_regs[i]);
    }

    __get_user(regs->active_tc.HI[1], &sc->sc_hi1);
    __get_user(regs->active_tc.HI[2], &sc->sc_hi2);
    __get_user(regs->active_tc.HI[3], &sc->sc_hi3);
    __get_user(regs->active_tc.LO[1], &sc->sc_lo1);
    __get_user(regs->active_tc.LO[2], &sc->sc_lo2);
    __get_user(regs->active_tc.LO[3], &sc->sc_lo3);
    {
        uint32_t dsp;
        __get_user(dsp, &sc->sc_dsp);
        cpu_wrdsp(dsp, 0x3ff, regs);
    }

    for (i = 0; i < 32; ++i) {
        __get_user(regs->active_fpu.fpr[i].d, &sc->sc_fpregs[i]);
    }
}

/*
 * Determine which stack to use..
 */
static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUMIPSState *regs, size_t frame_size)
{
    unsigned long sp;

    /*
     * FPU emulator may have its own trampoline active just
     * above the user stack, 16-bytes before the next lowest
     * 16 byte boundary.  Try to avoid trashing it.
     */
    sp = target_sigsp(get_sp_from_cpustate(regs) - 32, ka);

    return (sp - frame_size) & ~7;
}

static void mips_set_hflags_isa_mode_from_pc(CPUMIPSState *env)
{
    if (env->insn_flags & (ASE_MIPS16 | ASE_MICROMIPS)) {
        env->hflags &= ~MIPS_HFLAG_M16;
        env->hflags |= (env->active_tc.PC & 1) << MIPS_HFLAG_M16_SHIFT;
        env->active_tc.PC &= ~(target_ulong) 1;
    }
}

# if defined(TARGET_ABI_MIPSO32)
/* compare linux/arch/mips/kernel/signal.c:setup_frame() */
void setup_frame(int sig, struct target_sigaction * ka,
                 target_sigset_t *set, CPUMIPSState *regs)
{
    struct sigframe *frame;
    abi_ulong frame_addr;
    int i;

    frame_addr = get_sigframe(ka, regs, sizeof(*frame));
    trace_user_setup_frame(regs, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    setup_sigcontext(regs, &frame->sf_sc);

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->sf_mask.sig[i]);
    }

    /*
    * Arguments to signal handler:
    *
    *   a0 = signal number
    *   a1 = 0 (should be cause)
    *   a2 = pointer to struct sigcontext
    *
    * $25 and PC point to the signal handler, $29 points to the
    * struct sigframe.
    */
    regs->active_tc.gpr[ 4] = sig;
    regs->active_tc.gpr[ 5] = 0;
    regs->active_tc.gpr[ 6] = frame_addr + offsetof(struct sigframe, sf_sc);
    regs->active_tc.gpr[29] = frame_addr;
    regs->active_tc.gpr[31] = default_sigreturn;
    /* The original kernel code sets CP0_EPC to the handler
    * since it returns to userland using eret
    * we cannot do this here, and we must set PC directly */
    regs->active_tc.PC = regs->active_tc.gpr[25] = ka->_sa_handler;
    mips_set_hflags_isa_mode_from_pc(regs);
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    force_sigsegv(sig);
}

long do_sigreturn(CPUMIPSState *regs)
{
    struct sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;
    target_sigset_t target_set;
    int i;

    frame_addr = regs->active_tc.gpr[29];
    trace_user_do_sigreturn(regs, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &frame->sf_mask.sig[i]);
    }

    target_to_host_sigset_internal(&blocked, &target_set);
    set_sigmask(&blocked);

    restore_sigcontext(regs, &frame->sf_sc);

#if 0
    /*
     * Don't let your children do this ...
     */
    __asm__ __volatile__(
        "move\t$29, %0\n\t"
        "j\tsyscall_exit"
        :/* no outputs */
        :"r" (&regs));
    /* Unreached */
#endif

    regs->active_tc.PC = regs->CP0_EPC;
    mips_set_hflags_isa_mode_from_pc(regs);
    /* I am not sure this is right, but it seems to work
    * maybe a problem with nested signals ? */
    regs->CP0_EPC = 0;
    return -QEMU_ESIGRETURN;

badframe:
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}
# endif /* O32 */

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUMIPSState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    tswap_siginfo(&frame->rs_info, info);

    __put_user(0, &frame->rs_uc.tuc_flags);
    __put_user(0, &frame->rs_uc.tuc_link);
    target_save_altstack(&frame->rs_uc.tuc_stack, env);

    setup_sigcontext(env, &frame->rs_uc.tuc_mcontext);

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->rs_uc.tuc_sigmask.sig[i]);
    }

    /*
    * Arguments to signal handler:
    *
    *   a0 = signal number
    *   a1 = pointer to siginfo_t
    *   a2 = pointer to ucontext_t
    *
    * $25 and PC point to the signal handler, $29 points to the
    * struct sigframe.
    */
    env->active_tc.gpr[ 4] = sig;
    env->active_tc.gpr[ 5] = frame_addr
                             + offsetof(struct target_rt_sigframe, rs_info);
    env->active_tc.gpr[ 6] = frame_addr
                             + offsetof(struct target_rt_sigframe, rs_uc);
    env->active_tc.gpr[29] = frame_addr;
    env->active_tc.gpr[31] = default_rt_sigreturn;

    /*
     * The original kernel code sets CP0_EPC to the handler
     * since it returns to userland using eret
     * we cannot do this here, and we must set PC directly
     */
    env->active_tc.PC = env->active_tc.gpr[25] = ka->_sa_handler;
    mips_set_hflags_isa_mode_from_pc(env);
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(sig);
}

long do_rt_sigreturn(CPUMIPSState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    sigset_t blocked;

    frame_addr = env->active_tc.gpr[29];
    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    target_to_host_sigset(&blocked, &frame->rs_uc.tuc_sigmask);
    set_sigmask(&blocked);

    restore_sigcontext(env, &frame->rs_uc.tuc_mcontext);
    target_restore_altstack(&frame->rs_uc.tuc_stack, env);

    env->active_tc.PC = env->CP0_EPC;
    mips_set_hflags_isa_mode_from_pc(env);
    /* I am not sure this is right, but it seems to work
    * maybe a problem with nested signals ? */
    env->CP0_EPC = 0;
    return -QEMU_ESIGRETURN;

badframe:
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 2 * 8, 0);
    assert(tramp != NULL);

#ifdef TARGET_ARCH_HAS_SETUP_FRAME
    default_sigreturn = sigtramp_page;
    install_sigtramp(tramp, TARGET_NR_sigreturn);
#endif

    default_rt_sigreturn = sigtramp_page + 8;
    install_sigtramp(tramp + 2, TARGET_NR_rt_sigreturn);

    unlock_user(tramp, sigtramp_page, 2 * 8);
}
