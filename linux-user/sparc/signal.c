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

/* A Sparc register window */
struct target_reg_window {
    abi_ulong locals[8];
    abi_ulong ins[8];
};

/* A Sparc stack frame. */
struct target_stackf {
    /*
     * Since qemu does not reference fp or callers_pc directly,
     * it's simpler to treat fp and callers_pc as elements of ins[],
     * and then bundle locals[] and ins[] into reg_window.
     */
    struct target_reg_window win;
    /*
     * Similarly, bundle structptr and xxargs into xargs[].
     * This portion of the struct is part of the function call abi,
     * and belongs to the callee for spilling argument registers.
     */
    abi_ulong xargs[8];
};

struct target_siginfo_fpu {
#ifdef TARGET_SPARC64
    uint64_t si_double_regs[32];
    uint64_t si_fsr;
    uint64_t si_gsr;
    uint64_t si_fprs;
#else
    /* It is more convenient for qemu to move doubles, not singles. */
    uint64_t si_double_regs[16];
    uint32_t si_fsr;
    uint32_t si_fpqdepth;
    struct {
        uint32_t insn_addr;
        uint32_t insn;
    } si_fpqueue [16];
#endif
};

#ifdef TARGET_ARCH_HAS_SETUP_FRAME
struct target_signal_frame {
    struct target_stackf ss;
    struct target_pt_regs regs;
    uint32_t si_mask;
    abi_ulong fpu_save;
    uint32_t insns[2] QEMU_ALIGNED(8);
    abi_ulong extramask[TARGET_NSIG_WORDS - 1];
    abi_ulong extra_size; /* Should be 0 */
    abi_ulong rwin_save;
};
#endif

struct target_rt_signal_frame {
    struct target_stackf ss;
    target_siginfo_t info;
    struct target_pt_regs regs;
#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
    abi_ulong fpu_save;
    target_stack_t stack;
    target_sigset_t mask;
#else
    target_sigset_t mask;
    abi_ulong fpu_save;
    uint32_t insns[2];
    target_stack_t stack;
    abi_ulong extra_size; /* Should be 0 */
#endif
    abi_ulong rwin_save;
};

static abi_ulong get_sigframe(struct target_sigaction *sa,
                              CPUSPARCState *env,
                              size_t framesize)
{
    abi_ulong sp = get_sp_from_cpustate(env);

    /*
     * If we are on the alternate signal stack and would overflow it, don't.
     * Return an always-bogus address instead so we will die with SIGSEGV.
     */
    if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize))) {
        return -1;
    }

    /* This is the X/Open sanctioned signal stack switching.  */
    sp = target_sigsp(sp, sa) - framesize;

    /*
     * Always align the stack frame.  This handles two cases.  First,
     * sigaltstack need not be mindful of platform specific stack
     * alignment.  Second, if we took this signal because the stack
     * is not aligned properly, we'd like to take the signal cleanly
     * and report that.
     */
    sp &= ~15UL;

    return sp;
}

static void save_pt_regs(struct target_pt_regs *regs, CPUSPARCState *env)
{
    int i;

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
    __put_user(sparc64_tstate(env), &regs->tstate);
    /* TODO: magic should contain PT_REG_MAGIC + %tt. */
    __put_user(0, &regs->magic);
#else
    __put_user(cpu_get_psr(env), &regs->psr);
#endif

    __put_user(env->pc, &regs->pc);
    __put_user(env->npc, &regs->npc);
    __put_user(env->y, &regs->y);

    for (i = 0; i < 8; i++) {
        __put_user(env->gregs[i], &regs->u_regs[i]);
    }
    for (i = 0; i < 8; i++) {
        __put_user(env->regwptr[WREG_O0 + i], &regs->u_regs[i + 8]);
    }
}

static void restore_pt_regs(struct target_pt_regs *regs, CPUSPARCState *env)
{
    int i;

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
    /* User can only change condition codes and %asi in %tstate. */
    uint64_t tstate;
    __get_user(tstate, &regs->tstate);
    cpu_put_ccr(env, tstate >> 32);
    env->asi = extract64(tstate, 24, 8);
#else
    /*
     * User can only change condition codes and FPU enabling in %psr.
     * But don't bother with FPU enabling, since a real kernel would
     * just re-enable the FPU upon the next fpu trap.
     */
    uint32_t psr;
    __get_user(psr, &regs->psr);
    env->psr = (psr & PSR_ICC) | (env->psr & ~PSR_ICC);
#endif

    /* Note that pc and npc are handled in the caller. */

    __get_user(env->y, &regs->y);

    for (i = 0; i < 8; i++) {
        __get_user(env->gregs[i], &regs->u_regs[i]);
    }
    for (i = 0; i < 8; i++) {
        __get_user(env->regwptr[WREG_O0 + i], &regs->u_regs[i + 8]);
    }
}

static void save_reg_win(struct target_reg_window *win, CPUSPARCState *env)
{
    int i;

    for (i = 0; i < 8; i++) {
        __put_user(env->regwptr[i + WREG_L0], &win->locals[i]);
    }
    for (i = 0; i < 8; i++) {
        __put_user(env->regwptr[i + WREG_I0], &win->ins[i]);
    }
}

static void save_fpu(struct target_siginfo_fpu *fpu, CPUSPARCState *env)
{
    int i;

#ifdef TARGET_SPARC64
    for (i = 0; i < 32; ++i) {
        __put_user(env->fpr[i].ll, &fpu->si_double_regs[i]);
    }
    __put_user(env->fsr, &fpu->si_fsr);
    __put_user(env->gsr, &fpu->si_gsr);
    __put_user(env->fprs, &fpu->si_fprs);
#else
    for (i = 0; i < 16; ++i) {
        __put_user(env->fpr[i].ll, &fpu->si_double_regs[i]);
    }
    __put_user(env->fsr, &fpu->si_fsr);
    __put_user(0, &fpu->si_fpqdepth);
#endif
}

static void restore_fpu(struct target_siginfo_fpu *fpu, CPUSPARCState *env)
{
    int i;

#ifdef TARGET_SPARC64
    uint64_t fprs;
    __get_user(fprs, &fpu->si_fprs);

    /* In case the user mucks about with FPRS, restore as directed. */
    if (fprs & FPRS_DL) {
        for (i = 0; i < 16; ++i) {
            __get_user(env->fpr[i].ll, &fpu->si_double_regs[i]);
        }
    }
    if (fprs & FPRS_DU) {
        for (i = 16; i < 32; ++i) {
            __get_user(env->fpr[i].ll, &fpu->si_double_regs[i]);
        }
    }
    __get_user(env->fsr, &fpu->si_fsr);
    __get_user(env->gsr, &fpu->si_gsr);
    env->fprs |= fprs;
#else
    for (i = 0; i < 16; ++i) {
        __get_user(env->fpr[i].ll, &fpu->si_double_regs[i]);
    }
    __get_user(env->fsr, &fpu->si_fsr);
#endif
}

#ifdef TARGET_ARCH_HAS_SETUP_FRAME
static void install_sigtramp(uint32_t *tramp, int syscall)
{
    __put_user(0x82102000u + syscall, &tramp[0]); /* mov syscall, %g1 */
    __put_user(0x91d02010u, &tramp[1]);           /* t 0x10 */
}

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUSPARCState *env)
{
    abi_ulong sf_addr;
    struct target_signal_frame *sf;
    size_t sf_size = sizeof(*sf) + sizeof(struct target_siginfo_fpu);
    int i;

    sf_addr = get_sigframe(ka, env, sf_size);
    trace_user_setup_frame(env, sf_addr);

    sf = lock_user(VERIFY_WRITE, sf_addr, sf_size, 0);
    if (!sf) {
        force_sigsegv(sig);
        return;
    }

    /* 2. Save the current process state */
    save_pt_regs(&sf->regs, env);
    __put_user(0, &sf->extra_size);

    save_fpu((struct target_siginfo_fpu *)(sf + 1), env);
    __put_user(sf_addr + sizeof(*sf), &sf->fpu_save);

    __put_user(0, &sf->rwin_save);  /* TODO: save_rwin_state */

    __put_user(set->sig[0], &sf->si_mask);
    for (i = 0; i < TARGET_NSIG_WORDS - 1; i++) {
        __put_user(set->sig[i + 1], &sf->extramask[i]);
    }

    save_reg_win(&sf->ss.win, env);

    /* 3. signal handler back-trampoline and parameters */
    env->regwptr[WREG_SP] = sf_addr;
    env->regwptr[WREG_O0] = sig;
    env->regwptr[WREG_O1] = sf_addr +
            offsetof(struct target_signal_frame, regs);
    env->regwptr[WREG_O2] = sf_addr +
            offsetof(struct target_signal_frame, regs);

    /* 4. signal handler */
    env->pc = ka->_sa_handler;
    env->npc = env->pc + 4;

    /* 5. return to kernel instructions */
    if (ka->ka_restorer) {
        env->regwptr[WREG_O7] = ka->ka_restorer;
    } else {
        /* Not used, but retain for ABI compatibility. */
        install_sigtramp(sf->insns, TARGET_NR_sigreturn);
        env->regwptr[WREG_O7] = default_sigreturn;
    }
    unlock_user(sf, sf_addr, sf_size);
}
#endif /* TARGET_ARCH_HAS_SETUP_FRAME */

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUSPARCState *env)
{
    abi_ulong sf_addr;
    struct target_rt_signal_frame *sf;
    size_t sf_size = sizeof(*sf) + sizeof(struct target_siginfo_fpu);

    sf_addr = get_sigframe(ka, env, sf_size);
    trace_user_setup_rt_frame(env, sf_addr);

    sf = lock_user(VERIFY_WRITE, sf_addr, sf_size, 0);
    if (!sf) {
        force_sigsegv(sig);
        return;
    }

    /* 2. Save the current process state */
    save_reg_win(&sf->ss.win, env);
    save_pt_regs(&sf->regs, env);

    save_fpu((struct target_siginfo_fpu *)(sf + 1), env);
    __put_user(sf_addr + sizeof(*sf), &sf->fpu_save);

    __put_user(0, &sf->rwin_save);  /* TODO: save_rwin_state */

    tswap_siginfo(&sf->info, info);
    tswap_sigset(&sf->mask, set);
    target_save_altstack(&sf->stack, env);

#ifdef TARGET_ABI32
    __put_user(0, &sf->extra_size);
#endif

    /* 3. signal handler back-trampoline and parameters */
    env->regwptr[WREG_SP] = sf_addr - TARGET_STACK_BIAS;
    env->regwptr[WREG_O0] = sig;
    env->regwptr[WREG_O1] =
        sf_addr + offsetof(struct target_rt_signal_frame, info);
#ifdef TARGET_ABI32
    env->regwptr[WREG_O2] =
        sf_addr + offsetof(struct target_rt_signal_frame, regs);
#else
    env->regwptr[WREG_O2] = env->regwptr[WREG_O1];
#endif

    /* 4. signal handler */
    env->pc = ka->_sa_handler;
    env->npc = env->pc + 4;

    /* 5. return to kernel instructions */
#ifdef TARGET_ABI32
    if (ka->ka_restorer) {
        env->regwptr[WREG_O7] = ka->ka_restorer;
    } else {
        /* Not used, but retain for ABI compatibility. */
        install_sigtramp(sf->insns, TARGET_NR_rt_sigreturn);
        env->regwptr[WREG_O7] = default_rt_sigreturn;
    }
#else
    env->regwptr[WREG_O7] = ka->ka_restorer;
#endif

    unlock_user(sf, sf_addr, sf_size);
}

long do_sigreturn(CPUSPARCState *env)
{
#ifdef TARGET_ARCH_HAS_SETUP_FRAME
    abi_ulong sf_addr;
    struct target_signal_frame *sf = NULL;
    abi_ulong pc, npc, ptr;
    target_sigset_t set;
    sigset_t host_set;
    int i;

    sf_addr = env->regwptr[WREG_SP];
    trace_user_do_sigreturn(env, sf_addr);

    /* 1. Make sure we are not getting garbage from the user */
    if ((sf_addr & 15) || !lock_user_struct(VERIFY_READ, sf, sf_addr, 1)) {
        goto segv_and_exit;
    }

    /* Make sure stack pointer is aligned.  */
    __get_user(ptr, &sf->regs.u_regs[14]);
    if (ptr & 7) {
        goto segv_and_exit;
    }

    /* Make sure instruction pointers are aligned.  */
    __get_user(pc, &sf->regs.pc);
    __get_user(npc, &sf->regs.npc);
    if ((pc | npc) & 3) {
        goto segv_and_exit;
    }

    /* 2. Restore the state */
    restore_pt_regs(&sf->regs, env);
    env->pc = pc;
    env->npc = npc;

    __get_user(ptr, &sf->fpu_save);
    if (ptr) {
        struct target_siginfo_fpu *fpu;
        if ((ptr & 3) || !lock_user_struct(VERIFY_READ, fpu, ptr, 1)) {
            goto segv_and_exit;
        }
        restore_fpu(fpu, env);
        unlock_user_struct(fpu, ptr, 0);
    }

    __get_user(ptr, &sf->rwin_save);
    if (ptr) {
        goto segv_and_exit;  /* TODO: restore_rwin */
    }

    __get_user(set.sig[0], &sf->si_mask);
    for (i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(set.sig[i], &sf->extramask[i - 1]);
    }

    target_to_host_sigset_internal(&host_set, &set);
    set_sigmask(&host_set);

    unlock_user_struct(sf, sf_addr, 0);
    return -QEMU_ESIGRETURN;

 segv_and_exit:
    unlock_user_struct(sf, sf_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
#else
    return -TARGET_ENOSYS;
#endif
}

long do_rt_sigreturn(CPUSPARCState *env)
{
    abi_ulong sf_addr, tpc, tnpc, ptr;
    struct target_rt_signal_frame *sf = NULL;
    sigset_t set;

    sf_addr = get_sp_from_cpustate(env);
    trace_user_do_rt_sigreturn(env, sf_addr);

    /* 1. Make sure we are not getting garbage from the user */
    if ((sf_addr & 15) || !lock_user_struct(VERIFY_READ, sf, sf_addr, 1)) {
        goto segv_and_exit;
    }

    /* Validate SP alignment.  */
    __get_user(ptr, &sf->regs.u_regs[8 + WREG_SP]);
    if ((ptr + TARGET_STACK_BIAS) & 7) {
        goto segv_and_exit;
    }

    /* Validate PC and NPC alignment.  */
    __get_user(tpc, &sf->regs.pc);
    __get_user(tnpc, &sf->regs.npc);
    if ((tpc | tnpc) & 3) {
        goto segv_and_exit;
    }

    /* 2. Restore the state */
    restore_pt_regs(&sf->regs, env);

    __get_user(ptr, &sf->fpu_save);
    if (ptr) {
        struct target_siginfo_fpu *fpu;
        if ((ptr & 7) || !lock_user_struct(VERIFY_READ, fpu, ptr, 1)) {
            goto segv_and_exit;
        }
        restore_fpu(fpu, env);
        unlock_user_struct(fpu, ptr, 0);
    }

    __get_user(ptr, &sf->rwin_save);
    if (ptr) {
        goto segv_and_exit;  /* TODO: restore_rwin_state */
    }

    target_restore_altstack(&sf->stack, env);
    target_to_host_sigset(&set, &sf->mask);
    set_sigmask(&set);

    env->pc = tpc;
    env->npc = tnpc;

    unlock_user_struct(sf, sf_addr, 0);
    return -QEMU_ESIGRETURN;

 segv_and_exit:
    unlock_user_struct(sf, sf_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

#ifdef TARGET_ABI32
void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 2 * 8, 0);
    assert(tramp != NULL);

    default_sigreturn = sigtramp_page;
    install_sigtramp(tramp, TARGET_NR_sigreturn);

    default_rt_sigreturn = sigtramp_page + 8;
    install_sigtramp(tramp + 2, TARGET_NR_rt_sigreturn);

    unlock_user(tramp, sigtramp_page, 2 * 8);
}
#endif

#ifdef TARGET_SPARC64
#define SPARC_MC_TSTATE 0
#define SPARC_MC_PC 1
#define SPARC_MC_NPC 2
#define SPARC_MC_Y 3
#define SPARC_MC_G1 4
#define SPARC_MC_G2 5
#define SPARC_MC_G3 6
#define SPARC_MC_G4 7
#define SPARC_MC_G5 8
#define SPARC_MC_G6 9
#define SPARC_MC_G7 10
#define SPARC_MC_O0 11
#define SPARC_MC_O1 12
#define SPARC_MC_O2 13
#define SPARC_MC_O3 14
#define SPARC_MC_O4 15
#define SPARC_MC_O5 16
#define SPARC_MC_O6 17
#define SPARC_MC_O7 18
#define SPARC_MC_NGREG 19

typedef abi_ulong target_mc_greg_t;
typedef target_mc_greg_t target_mc_gregset_t[SPARC_MC_NGREG];

struct target_mc_fq {
    abi_ulong mcfq_addr;
    uint32_t mcfq_insn;
};

/*
 * Note the manual 16-alignment; the kernel gets this because it
 * includes a "long double qregs[16]" in the mcpu_fregs union,
 * which we can't do.
 */
struct target_mc_fpu {
    union {
        uint32_t sregs[32];
        uint64_t dregs[32];
        //uint128_t qregs[16];
    } mcfpu_fregs;
    abi_ulong mcfpu_fsr;
    abi_ulong mcfpu_fprs;
    abi_ulong mcfpu_gsr;
    abi_ulong mcfpu_fq;
    unsigned char mcfpu_qcnt;
    unsigned char mcfpu_qentsz;
    unsigned char mcfpu_enab;
} __attribute__((aligned(16)));
typedef struct target_mc_fpu target_mc_fpu_t;

typedef struct {
    target_mc_gregset_t mc_gregs;
    target_mc_greg_t mc_fp;
    target_mc_greg_t mc_i7;
    target_mc_fpu_t mc_fpregs;
} target_mcontext_t;

struct target_ucontext {
    abi_ulong tuc_link;
    abi_ulong tuc_flags;
    target_sigset_t tuc_sigmask;
    target_mcontext_t tuc_mcontext;
};

/* {set, get}context() needed for 64-bit SparcLinux userland. */
void sparc64_set_context(CPUSPARCState *env)
{
    abi_ulong ucp_addr;
    struct target_ucontext *ucp;
    target_mc_gregset_t *grp;
    target_mc_fpu_t *fpup;
    target_ulong pc, npc, tstate;
    unsigned int i;
    unsigned char fenab;

    ucp_addr = env->regwptr[WREG_O0];
    if (!lock_user_struct(VERIFY_READ, ucp, ucp_addr, 1)) {
        goto do_sigsegv;
    }
    grp  = &ucp->tuc_mcontext.mc_gregs;
    __get_user(pc, &((*grp)[SPARC_MC_PC]));
    __get_user(npc, &((*grp)[SPARC_MC_NPC]));
    if ((pc | npc) & 3) {
        goto do_sigsegv;
    }
    if (env->regwptr[WREG_O1]) {
        target_sigset_t target_set;
        sigset_t set;

        if (TARGET_NSIG_WORDS == 1) {
            __get_user(target_set.sig[0], &ucp->tuc_sigmask.sig[0]);
        } else {
            abi_ulong *src, *dst;
            src = ucp->tuc_sigmask.sig;
            dst = target_set.sig;
            for (i = 0; i < TARGET_NSIG_WORDS; i++, dst++, src++) {
                __get_user(*dst, src);
            }
        }
        target_to_host_sigset_internal(&set, &target_set);
        set_sigmask(&set);
    }
    env->pc = pc;
    env->npc = npc;
    __get_user(env->y, &((*grp)[SPARC_MC_Y]));
    __get_user(tstate, &((*grp)[SPARC_MC_TSTATE]));
    /* Honour TSTATE_ASI, TSTATE_ICC and TSTATE_XCC only */
    env->asi = (tstate >> 24) & 0xff;
    cpu_put_ccr(env, (tstate >> 32) & 0xff);
    __get_user(env->gregs[1], (&(*grp)[SPARC_MC_G1]));
    __get_user(env->gregs[2], (&(*grp)[SPARC_MC_G2]));
    __get_user(env->gregs[3], (&(*grp)[SPARC_MC_G3]));
    __get_user(env->gregs[4], (&(*grp)[SPARC_MC_G4]));
    __get_user(env->gregs[5], (&(*grp)[SPARC_MC_G5]));
    __get_user(env->gregs[6], (&(*grp)[SPARC_MC_G6]));
    /* Skip g7 as that's the thread register in userspace */

    /*
     * Note that unlike the kernel, we didn't need to mess with the
     * guest register window state to save it into a pt_regs to run
     * the kernel. So for us the guest's O regs are still in WREG_O*
     * (unlike the kernel which has put them in UREG_I* in a pt_regs)
     * and the fp and i7 are still in WREG_I6 and WREG_I7 and don't
     * need to be written back to userspace memory.
     */
    __get_user(env->regwptr[WREG_O0], (&(*grp)[SPARC_MC_O0]));
    __get_user(env->regwptr[WREG_O1], (&(*grp)[SPARC_MC_O1]));
    __get_user(env->regwptr[WREG_O2], (&(*grp)[SPARC_MC_O2]));
    __get_user(env->regwptr[WREG_O3], (&(*grp)[SPARC_MC_O3]));
    __get_user(env->regwptr[WREG_O4], (&(*grp)[SPARC_MC_O4]));
    __get_user(env->regwptr[WREG_O5], (&(*grp)[SPARC_MC_O5]));
    __get_user(env->regwptr[WREG_O6], (&(*grp)[SPARC_MC_O6]));
    __get_user(env->regwptr[WREG_O7], (&(*grp)[SPARC_MC_O7]));

    __get_user(env->regwptr[WREG_FP], &(ucp->tuc_mcontext.mc_fp));
    __get_user(env->regwptr[WREG_I7], &(ucp->tuc_mcontext.mc_i7));

    fpup = &ucp->tuc_mcontext.mc_fpregs;

    __get_user(fenab, &(fpup->mcfpu_enab));
    if (fenab) {
        abi_ulong fprs;

        /*
         * We use the FPRS from the guest only in deciding whether
         * to restore the upper, lower, or both banks of the FPU regs.
         * The kernel here writes the FPU register data into the
         * process's current_thread_info state and unconditionally
         * clears FPRS and TSTATE_PEF: this disables the FPU so that the
         * next FPU-disabled trap will copy the data out of
         * current_thread_info and into the real FPU registers.
         * QEMU doesn't need to handle lazy-FPU-state-restoring like that,
         * so we always load the data directly into the FPU registers
         * and leave FPRS and TSTATE_PEF alone (so the FPU stays enabled).
         * Note that because we (and the kernel) always write zeroes for
         * the fenab and fprs in sparc64_get_context() none of this code
         * will execute unless the guest manually constructed or changed
         * the context structure.
         */
        __get_user(fprs, &(fpup->mcfpu_fprs));
        if (fprs & FPRS_DL) {
            for (i = 0; i < 16; i++) {
                __get_user(env->fpr[i].ll, &(fpup->mcfpu_fregs.dregs[i]));
            }
        }
        if (fprs & FPRS_DU) {
            for (i = 16; i < 32; i++) {
                __get_user(env->fpr[i].ll, &(fpup->mcfpu_fregs.dregs[i]));
            }
        }
        __get_user(env->fsr, &(fpup->mcfpu_fsr));
        __get_user(env->gsr, &(fpup->mcfpu_gsr));
    }
    unlock_user_struct(ucp, ucp_addr, 0);
    return;
do_sigsegv:
    unlock_user_struct(ucp, ucp_addr, 0);
    force_sig(TARGET_SIGSEGV);
}

void sparc64_get_context(CPUSPARCState *env)
{
    abi_ulong ucp_addr;
    struct target_ucontext *ucp;
    target_mc_gregset_t *grp;
    target_mcontext_t *mcp;
    int err;
    unsigned int i;
    target_sigset_t target_set;
    sigset_t set;

    ucp_addr = env->regwptr[WREG_O0];
    if (!lock_user_struct(VERIFY_WRITE, ucp, ucp_addr, 0)) {
        goto do_sigsegv;
    }

    memset(ucp, 0, sizeof(*ucp));

    mcp = &ucp->tuc_mcontext;
    grp = &mcp->mc_gregs;

    /* Skip over the trap instruction, first. */
    env->pc = env->npc;
    env->npc += 4;

    /* If we're only reading the signal mask then do_sigprocmask()
     * is guaranteed not to fail, which is important because we don't
     * have any way to signal a failure or restart this operation since
     * this is not a normal syscall.
     */
    err = do_sigprocmask(0, NULL, &set);
    assert(err == 0);
    host_to_target_sigset_internal(&target_set, &set);
    if (TARGET_NSIG_WORDS == 1) {
        __put_user(target_set.sig[0],
                   (abi_ulong *)&ucp->tuc_sigmask);
    } else {
        abi_ulong *src, *dst;
        src = target_set.sig;
        dst = ucp->tuc_sigmask.sig;
        for (i = 0; i < TARGET_NSIG_WORDS; i++, dst++, src++) {
            __put_user(*src, dst);
        }
    }

    __put_user(sparc64_tstate(env), &((*grp)[SPARC_MC_TSTATE]));
    __put_user(env->pc, &((*grp)[SPARC_MC_PC]));
    __put_user(env->npc, &((*grp)[SPARC_MC_NPC]));
    __put_user(env->y, &((*grp)[SPARC_MC_Y]));
    __put_user(env->gregs[1], &((*grp)[SPARC_MC_G1]));
    __put_user(env->gregs[2], &((*grp)[SPARC_MC_G2]));
    __put_user(env->gregs[3], &((*grp)[SPARC_MC_G3]));
    __put_user(env->gregs[4], &((*grp)[SPARC_MC_G4]));
    __put_user(env->gregs[5], &((*grp)[SPARC_MC_G5]));
    __put_user(env->gregs[6], &((*grp)[SPARC_MC_G6]));
    __put_user(env->gregs[7], &((*grp)[SPARC_MC_G7]));

    /*
     * Note that unlike the kernel, we didn't need to mess with the
     * guest register window state to save it into a pt_regs to run
     * the kernel. So for us the guest's O regs are still in WREG_O*
     * (unlike the kernel which has put them in UREG_I* in a pt_regs)
     * and the fp and i7 are still in WREG_I6 and WREG_I7 and don't
     * need to be fished out of userspace memory.
     */
    __put_user(env->regwptr[WREG_O0], &((*grp)[SPARC_MC_O0]));
    __put_user(env->regwptr[WREG_O1], &((*grp)[SPARC_MC_O1]));
    __put_user(env->regwptr[WREG_O2], &((*grp)[SPARC_MC_O2]));
    __put_user(env->regwptr[WREG_O3], &((*grp)[SPARC_MC_O3]));
    __put_user(env->regwptr[WREG_O4], &((*grp)[SPARC_MC_O4]));
    __put_user(env->regwptr[WREG_O5], &((*grp)[SPARC_MC_O5]));
    __put_user(env->regwptr[WREG_O6], &((*grp)[SPARC_MC_O6]));
    __put_user(env->regwptr[WREG_O7], &((*grp)[SPARC_MC_O7]));

    __put_user(env->regwptr[WREG_FP], &(mcp->mc_fp));
    __put_user(env->regwptr[WREG_I7], &(mcp->mc_i7));

    /*
     * We don't write out the FPU state. This matches the kernel's
     * implementation (which has the code for doing this but
     * hidden behind an "if (fenab)" where fenab is always 0).
     */

    unlock_user_struct(ucp, ucp_addr, 1);
    return;
do_sigsegv:
    unlock_user_struct(ucp, ucp_addr, 1);
    force_sig(TARGET_SIGSEGV);
}
#endif /* TARGET_SPARC64 */
