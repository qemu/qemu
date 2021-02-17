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

/* Size of dummy stack frame allocated when calling signal handler.
   See arch/powerpc/include/asm/ptrace.h.  */
#if defined(TARGET_PPC64)
#define SIGNAL_FRAMESIZE 128
#else
#define SIGNAL_FRAMESIZE 64
#endif

/* See arch/powerpc/include/asm/ucontext.h.  Only used for 32-bit PPC;
   on 64-bit PPC, sigcontext and mcontext are one and the same.  */
struct target_mcontext {
    target_ulong mc_gregs[48];
    /* Includes fpscr.  */
    uint64_t mc_fregs[33];

#if defined(TARGET_PPC64)
    /* Pointer to the vector regs */
    target_ulong v_regs;
    /*
     * On ppc64, this mcontext structure is naturally *unaligned*,
     * or rather it is aligned on a 8 bytes boundary but not on
     * a 16 byte boundary.  This pad fixes it up.  This is why we
     * cannot use ppc_avr_t, which would force alignment.  This is
     * also why the vector regs are referenced in the ABI by the
     * v_regs pointer above so any amount of padding can be added here.
     */
    target_ulong pad;
    /* VSCR and VRSAVE are saved separately.  Also reserve space for VSX. */
    struct {
        uint64_t altivec[34 + 16][2];
    } mc_vregs;
#else
    target_ulong mc_pad[2];

    /* We need to handle Altivec and SPE at the same time, which no
       kernel needs to do.  Fortunately, the kernel defines this bit to
       be Altivec-register-large all the time, rather than trying to
       twiddle it based on the specific platform.  */
    union {
        /* SPE vector registers.  One extra for SPEFSCR.  */
        uint32_t spe[33];
        /*
         * Altivec vector registers.  One extra for VRSAVE.
         * On ppc32, we are already aligned to 16 bytes.  We could
         * use ppc_avr_t, but choose to share the same type as ppc64.
         */
        uint64_t altivec[33][2];
    } mc_vregs;
#endif
};

/* See arch/powerpc/include/asm/sigcontext.h.  */
struct target_sigcontext {
    target_ulong _unused[4];
    int32_t signal;
#if defined(TARGET_PPC64)
    int32_t pad0;
#endif
    target_ulong handler;
    target_ulong oldmask;
    target_ulong regs;      /* struct pt_regs __user * */
#if defined(TARGET_PPC64)
    struct target_mcontext mcontext;
#endif
};

/* Indices for target_mcontext.mc_gregs, below.
   See arch/powerpc/include/asm/ptrace.h for details.  */
enum {
    TARGET_PT_R0 = 0,
    TARGET_PT_R1 = 1,
    TARGET_PT_R2 = 2,
    TARGET_PT_R3 = 3,
    TARGET_PT_R4 = 4,
    TARGET_PT_R5 = 5,
    TARGET_PT_R6 = 6,
    TARGET_PT_R7 = 7,
    TARGET_PT_R8 = 8,
    TARGET_PT_R9 = 9,
    TARGET_PT_R10 = 10,
    TARGET_PT_R11 = 11,
    TARGET_PT_R12 = 12,
    TARGET_PT_R13 = 13,
    TARGET_PT_R14 = 14,
    TARGET_PT_R15 = 15,
    TARGET_PT_R16 = 16,
    TARGET_PT_R17 = 17,
    TARGET_PT_R18 = 18,
    TARGET_PT_R19 = 19,
    TARGET_PT_R20 = 20,
    TARGET_PT_R21 = 21,
    TARGET_PT_R22 = 22,
    TARGET_PT_R23 = 23,
    TARGET_PT_R24 = 24,
    TARGET_PT_R25 = 25,
    TARGET_PT_R26 = 26,
    TARGET_PT_R27 = 27,
    TARGET_PT_R28 = 28,
    TARGET_PT_R29 = 29,
    TARGET_PT_R30 = 30,
    TARGET_PT_R31 = 31,
    TARGET_PT_NIP = 32,
    TARGET_PT_MSR = 33,
    TARGET_PT_ORIG_R3 = 34,
    TARGET_PT_CTR = 35,
    TARGET_PT_LNK = 36,
    TARGET_PT_XER = 37,
    TARGET_PT_CCR = 38,
    /* Yes, there are two registers with #39.  One is 64-bit only.  */
    TARGET_PT_MQ = 39,
    TARGET_PT_SOFTE = 39,
    TARGET_PT_TRAP = 40,
    TARGET_PT_DAR = 41,
    TARGET_PT_DSISR = 42,
    TARGET_PT_RESULT = 43,
    TARGET_PT_REGS_COUNT = 44
};


struct target_ucontext {
    target_ulong tuc_flags;
    target_ulong tuc_link;    /* ucontext_t __user * */
    struct target_sigaltstack tuc_stack;
#if !defined(TARGET_PPC64)
    int32_t tuc_pad[7];
    target_ulong tuc_regs;    /* struct mcontext __user *
                                points to uc_mcontext field */
#endif
    target_sigset_t tuc_sigmask;
#if defined(TARGET_PPC64)
    target_sigset_t unused[15]; /* Allow for uc_sigmask growth */
    struct target_sigcontext tuc_sigcontext;
#else
    int32_t tuc_maskext[30];
    int32_t tuc_pad2[3];
    struct target_mcontext tuc_mcontext;
#endif
};

/* See arch/powerpc/kernel/signal_32.c.  */
struct target_sigframe {
    struct target_sigcontext sctx;
    struct target_mcontext mctx;
    int32_t abigap[56];
};

#if defined(TARGET_PPC64)

#define TARGET_TRAMP_SIZE 6

struct target_rt_sigframe {
    /* sys_rt_sigreturn requires the ucontext be the first field */
    struct target_ucontext uc;
    target_ulong  _unused[2];
    uint32_t trampoline[TARGET_TRAMP_SIZE];
    target_ulong pinfo; /* struct siginfo __user * */
    target_ulong puc; /* void __user * */
    struct target_siginfo info;
    /* 64 bit ABI allows for 288 bytes below sp before decrementing it. */
    char abigap[288];
} __attribute__((aligned(16)));

#else

struct target_rt_sigframe {
    struct target_siginfo info;
    struct target_ucontext uc;
    int32_t abigap[56];
};

#endif

#if defined(TARGET_PPC64)

struct target_func_ptr {
    target_ulong entry;
    target_ulong toc;
};

#endif

/* We use the mc_pad field for the signal return trampoline.  */
#define tramp mc_pad

/* See arch/powerpc/kernel/signal.c.  */
static target_ulong get_sigframe(struct target_sigaction *ka,
                                 CPUPPCState *env,
                                 int frame_size)
{
    target_ulong oldsp;

    oldsp = target_sigsp(get_sp_from_cpustate(env), ka);

    return (oldsp - frame_size) & ~0xFUL;
}

#if ((defined(TARGET_WORDS_BIGENDIAN) && defined(HOST_WORDS_BIGENDIAN)) || \
     (!defined(HOST_WORDS_BIGENDIAN) && !defined(TARGET_WORDS_BIGENDIAN)))
#define PPC_VEC_HI      0
#define PPC_VEC_LO      1
#else
#define PPC_VEC_HI      1
#define PPC_VEC_LO      0
#endif


static void save_user_regs(CPUPPCState *env, struct target_mcontext *frame)
{
    target_ulong msr = env->msr;
    int i;
    target_ulong ccr = 0;

    /* In general, the kernel attempts to be intelligent about what it
       needs to save for Altivec/FP/SPE registers.  We don't care that
       much, so we just go ahead and save everything.  */

    /* Save general registers.  */
    for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        __put_user(env->gpr[i], &frame->mc_gregs[i]);
    }
    __put_user(env->nip, &frame->mc_gregs[TARGET_PT_NIP]);
    __put_user(env->ctr, &frame->mc_gregs[TARGET_PT_CTR]);
    __put_user(env->lr, &frame->mc_gregs[TARGET_PT_LNK]);
    __put_user(env->xer, &frame->mc_gregs[TARGET_PT_XER]);

    for (i = 0; i < ARRAY_SIZE(env->crf); i++) {
        ccr |= env->crf[i] << (32 - ((i + 1) * 4));
    }
    __put_user(ccr, &frame->mc_gregs[TARGET_PT_CCR]);

    /* Save Altivec registers if necessary.  */
    if (env->insns_flags & PPC_ALTIVEC) {
        uint32_t *vrsave;
        for (i = 0; i < 32; i++) {
            ppc_avr_t *avr = cpu_avr_ptr(env, i);
            ppc_avr_t *vreg = (ppc_avr_t *)&frame->mc_vregs.altivec[i];

            __put_user(avr->u64[PPC_VEC_HI], &vreg->u64[0]);
            __put_user(avr->u64[PPC_VEC_LO], &vreg->u64[1]);
        }
        /* Set MSR_VR in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
        msr |= MSR_VR;
#if defined(TARGET_PPC64)
        vrsave = (uint32_t *)&frame->mc_vregs.altivec[33];
        /* 64-bit needs to put a pointer to the vectors in the frame */
        __put_user(h2g(frame->mc_vregs.altivec), &frame->v_regs);
#else
        vrsave = (uint32_t *)&frame->mc_vregs.altivec[32];
#endif
        __put_user((uint32_t)env->spr[SPR_VRSAVE], vrsave);
    }

#if defined(TARGET_PPC64)
    /* Save VSX second halves */
    if (env->insns_flags2 & PPC2_VSX) {
        uint64_t *vsregs = (uint64_t *)&frame->mc_vregs.altivec[34];
        for (i = 0; i < 32; i++) {
            uint64_t *vsrl = cpu_vsrl_ptr(env, i);
            __put_user(*vsrl, &vsregs[i]);
        }
    }
#endif

    /* Save floating point registers.  */
    if (env->insns_flags & PPC_FLOAT) {
        for (i = 0; i < 32; i++) {
            uint64_t *fpr = cpu_fpr_ptr(env, i);
            __put_user(*fpr, &frame->mc_fregs[i]);
        }
        __put_user((uint64_t) env->fpscr, &frame->mc_fregs[32]);
    }

#if !defined(TARGET_PPC64)
    /* Save SPE registers.  The kernel only saves the high half.  */
    if (env->insns_flags & PPC_SPE) {
        for (i = 0; i < ARRAY_SIZE(env->gprh); i++) {
            __put_user(env->gprh[i], &frame->mc_vregs.spe[i]);
        }
        /* Set MSR_SPE in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
        msr |= MSR_SPE;
        __put_user(env->spe_fscr, &frame->mc_vregs.spe[32]);
    }
#endif

    /* Store MSR.  */
    __put_user(msr, &frame->mc_gregs[TARGET_PT_MSR]);
}

static void encode_trampoline(int sigret, uint32_t *tramp)
{
    /* Set up the sigreturn trampoline: li r0,sigret; sc.  */
    if (sigret) {
        __put_user(0x38000000 | sigret, &tramp[0]);
        __put_user(0x44000002, &tramp[1]);
    }
}

static void restore_user_regs(CPUPPCState *env,
                              struct target_mcontext *frame, int sig)
{
    target_ulong save_r2 = 0;
    target_ulong msr;
    target_ulong ccr;

    int i;

    if (!sig) {
        save_r2 = env->gpr[2];
    }

    /* Restore general registers.  */
    for (i = 0; i < ARRAY_SIZE(env->gpr); i++) {
        __get_user(env->gpr[i], &frame->mc_gregs[i]);
    }
    __get_user(env->nip, &frame->mc_gregs[TARGET_PT_NIP]);
    __get_user(env->ctr, &frame->mc_gregs[TARGET_PT_CTR]);
    __get_user(env->lr, &frame->mc_gregs[TARGET_PT_LNK]);
    __get_user(env->xer, &frame->mc_gregs[TARGET_PT_XER]);
    __get_user(ccr, &frame->mc_gregs[TARGET_PT_CCR]);

    for (i = 0; i < ARRAY_SIZE(env->crf); i++) {
        env->crf[i] = (ccr >> (32 - ((i + 1) * 4))) & 0xf;
    }

    if (!sig) {
        env->gpr[2] = save_r2;
    }
    /* Restore MSR.  */
    __get_user(msr, &frame->mc_gregs[TARGET_PT_MSR]);

    /* If doing signal return, restore the previous little-endian mode.  */
    if (sig)
        env->msr = (env->msr & ~(1ull << MSR_LE)) | (msr & (1ull << MSR_LE));

    /* Restore Altivec registers if necessary.  */
    if (env->insns_flags & PPC_ALTIVEC) {
        ppc_avr_t *v_regs;
        uint32_t *vrsave;
#if defined(TARGET_PPC64)
        uint64_t v_addr;
        /* 64-bit needs to recover the pointer to the vectors from the frame */
        __get_user(v_addr, &frame->v_regs);
        v_regs = g2h(env_cpu(env), v_addr);
#else
        v_regs = (ppc_avr_t *)frame->mc_vregs.altivec;
#endif
        for (i = 0; i < 32; i++) {
            ppc_avr_t *avr = cpu_avr_ptr(env, i);
            ppc_avr_t *vreg = &v_regs[i];

            __get_user(avr->u64[PPC_VEC_HI], &vreg->u64[0]);
            __get_user(avr->u64[PPC_VEC_LO], &vreg->u64[1]);
        }
        /* Set MSR_VEC in the saved MSR value to indicate that
           frame->mc_vregs contains valid data.  */
#if defined(TARGET_PPC64)
        vrsave = (uint32_t *)&v_regs[33];
#else
        vrsave = (uint32_t *)&v_regs[32];
#endif
        __get_user(env->spr[SPR_VRSAVE], vrsave);
    }

#if defined(TARGET_PPC64)
    /* Restore VSX second halves */
    if (env->insns_flags2 & PPC2_VSX) {
        uint64_t *vsregs = (uint64_t *)&frame->mc_vregs.altivec[34];
        for (i = 0; i < 32; i++) {
            uint64_t *vsrl = cpu_vsrl_ptr(env, i);
            __get_user(*vsrl, &vsregs[i]);
        }
    }
#endif

    /* Restore floating point registers.  */
    if (env->insns_flags & PPC_FLOAT) {
        uint64_t fpscr;
        for (i = 0; i < 32; i++) {
            uint64_t *fpr = cpu_fpr_ptr(env, i);
            __get_user(*fpr, &frame->mc_fregs[i]);
        }
        __get_user(fpscr, &frame->mc_fregs[32]);
        env->fpscr = (uint32_t) fpscr;
    }

#if !defined(TARGET_PPC64)
    /* Save SPE registers.  The kernel only saves the high half.  */
    if (env->insns_flags & PPC_SPE) {
        for (i = 0; i < ARRAY_SIZE(env->gprh); i++) {
            __get_user(env->gprh[i], &frame->mc_vregs.spe[i]);
        }
        __get_user(env->spe_fscr, &frame->mc_vregs.spe[32]);
    }
#endif
}

#if !defined(TARGET_PPC64)
void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUPPCState *env)
{
    struct target_sigframe *frame;
    struct target_sigcontext *sc;
    target_ulong frame_addr, newsp;
    int err = 0;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 1))
        goto sigsegv;
    sc = &frame->sctx;

    __put_user(ka->_sa_handler, &sc->handler);
    __put_user(set->sig[0], &sc->oldmask);
    __put_user(set->sig[1], &sc->_unused[3]);
    __put_user(h2g(&frame->mctx), &sc->regs);
    __put_user(sig, &sc->signal);

    /* Save user regs.  */
    save_user_regs(env, &frame->mctx);

    /* Construct the trampoline code on the stack. */
    encode_trampoline(TARGET_NR_sigreturn, (uint32_t *)&frame->mctx.tramp);

    /* The kernel checks for the presence of a VDSO here.  We don't
       emulate a vdso, so use a sigreturn system call.  */
    env->lr = (target_ulong) h2g(frame->mctx.tramp);

    /* Turn off all fp exceptions.  */
    env->fpscr = 0;

    /* Create a stack frame for the caller of the handler.  */
    newsp = frame_addr - SIGNAL_FRAMESIZE;
    err |= put_user(env->gpr[1], newsp, target_ulong);

    if (err)
        goto sigsegv;

    /* Set up registers for signal handler.  */
    env->gpr[1] = newsp;
    env->gpr[3] = sig;
    env->gpr[4] = frame_addr + offsetof(struct target_sigframe, sctx);

    env->nip = (target_ulong) ka->_sa_handler;

    /* Signal handlers are entered in big-endian mode.  */
    env->msr &= ~(1ull << MSR_LE);

    unlock_user_struct(frame, frame_addr, 1);
    return;

sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(sig);
}
#endif /* !defined(TARGET_PPC64) */

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUPPCState *env)
{
    struct target_rt_sigframe *rt_sf;
    uint32_t *trampptr = 0;
    struct target_mcontext *mctx = 0;
    target_ulong rt_sf_addr, newsp = 0;
    int i, err = 0;
#if defined(TARGET_PPC64)
    struct target_sigcontext *sc = 0;
#if !defined(TARGET_ABI32)
    struct image_info *image = ((TaskState *)thread_cpu->opaque)->info;
#endif
#endif

    rt_sf_addr = get_sigframe(ka, env, sizeof(*rt_sf));
    if (!lock_user_struct(VERIFY_WRITE, rt_sf, rt_sf_addr, 1))
        goto sigsegv;

    tswap_siginfo(&rt_sf->info, info);

    __put_user(0, &rt_sf->uc.tuc_flags);
    __put_user(0, &rt_sf->uc.tuc_link);
    target_save_altstack(&rt_sf->uc.tuc_stack, env);
#if !defined(TARGET_PPC64)
    __put_user(h2g (&rt_sf->uc.tuc_mcontext),
               &rt_sf->uc.tuc_regs);
#endif
    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &rt_sf->uc.tuc_sigmask.sig[i]);
    }

#if defined(TARGET_PPC64)
    mctx = &rt_sf->uc.tuc_sigcontext.mcontext;
    trampptr = &rt_sf->trampoline[0];

    sc = &rt_sf->uc.tuc_sigcontext;
    __put_user(h2g(mctx), &sc->regs);
    __put_user(sig, &sc->signal);
#else
    mctx = &rt_sf->uc.tuc_mcontext;
    trampptr = (uint32_t *)&rt_sf->uc.tuc_mcontext.tramp;
#endif

    save_user_regs(env, mctx);
    encode_trampoline(TARGET_NR_rt_sigreturn, trampptr);

    /* The kernel checks for the presence of a VDSO here.  We don't
       emulate a vdso, so use a sigreturn system call.  */
    env->lr = (target_ulong) h2g(trampptr);

    /* Turn off all fp exceptions.  */
    env->fpscr = 0;

    /* Create a stack frame for the caller of the handler.  */
    newsp = rt_sf_addr - (SIGNAL_FRAMESIZE + 16);
    err |= put_user(env->gpr[1], newsp, target_ulong);

    if (err)
        goto sigsegv;

    /* Set up registers for signal handler.  */
    env->gpr[1] = newsp;
    env->gpr[3] = (target_ulong) sig;
    env->gpr[4] = (target_ulong) h2g(&rt_sf->info);
    env->gpr[5] = (target_ulong) h2g(&rt_sf->uc);
    env->gpr[6] = (target_ulong) h2g(rt_sf);

#if defined(TARGET_PPC64) && !defined(TARGET_ABI32)
    if (get_ppc64_abi(image) < 2) {
        /* ELFv1 PPC64 function pointers are pointers to OPD entries. */
        struct target_func_ptr *handler =
            (struct target_func_ptr *)g2h(env_cpu(env), ka->_sa_handler);
        env->nip = tswapl(handler->entry);
        env->gpr[2] = tswapl(handler->toc);
    } else {
        /* ELFv2 PPC64 function pointers are entry points. R12 must also be set. */
        env->gpr[12] = env->nip = ka->_sa_handler;
    }
#else
    env->nip = (target_ulong) ka->_sa_handler;
#endif

    /* Signal handlers are entered in big-endian mode.  */
    env->msr &= ~(1ull << MSR_LE);

    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    return;

sigsegv:
    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    force_sigsegv(sig);

}

#if !defined(TARGET_PPC64) || defined(TARGET_ABI32)
long do_sigreturn(CPUPPCState *env)
{
    struct target_sigcontext *sc = NULL;
    struct target_mcontext *sr = NULL;
    target_ulong sr_addr = 0, sc_addr;
    sigset_t blocked;
    target_sigset_t set;

    sc_addr = env->gpr[1] + SIGNAL_FRAMESIZE;
    if (!lock_user_struct(VERIFY_READ, sc, sc_addr, 1))
        goto sigsegv;

#if defined(TARGET_PPC64)
    set.sig[0] = sc->oldmask + ((uint64_t)(sc->_unused[3]) << 32);
#else
    __get_user(set.sig[0], &sc->oldmask);
    __get_user(set.sig[1], &sc->_unused[3]);
#endif
    target_to_host_sigset_internal(&blocked, &set);
    set_sigmask(&blocked);

    __get_user(sr_addr, &sc->regs);
    if (!lock_user_struct(VERIFY_READ, sr, sr_addr, 1))
        goto sigsegv;
    restore_user_regs(env, sr, 1);

    unlock_user_struct(sr, sr_addr, 1);
    unlock_user_struct(sc, sc_addr, 1);
    return -TARGET_QEMU_ESIGRETURN;

sigsegv:
    unlock_user_struct(sr, sr_addr, 1);
    unlock_user_struct(sc, sc_addr, 1);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
#endif /* !defined(TARGET_PPC64) */

/* See arch/powerpc/kernel/signal_32.c.  */
static int do_setcontext(struct target_ucontext *ucp, CPUPPCState *env, int sig)
{
    struct target_mcontext *mcp;
    target_ulong mcp_addr;
    sigset_t blocked;
    target_sigset_t set;

    if (copy_from_user(&set, h2g(ucp) + offsetof(struct target_ucontext, tuc_sigmask),
                       sizeof (set)))
        return 1;

#if defined(TARGET_PPC64)
    mcp_addr = h2g(ucp) +
        offsetof(struct target_ucontext, tuc_sigcontext.mcontext);
#else
    __get_user(mcp_addr, &ucp->tuc_regs);
#endif

    if (!lock_user_struct(VERIFY_READ, mcp, mcp_addr, 1))
        return 1;

    target_to_host_sigset_internal(&blocked, &set);
    set_sigmask(&blocked);
    restore_user_regs(env, mcp, sig);

    unlock_user_struct(mcp, mcp_addr, 1);
    return 0;
}

long do_rt_sigreturn(CPUPPCState *env)
{
    struct target_rt_sigframe *rt_sf = NULL;
    target_ulong rt_sf_addr;

    rt_sf_addr = env->gpr[1] + SIGNAL_FRAMESIZE + 16;
    if (!lock_user_struct(VERIFY_READ, rt_sf, rt_sf_addr, 1))
        goto sigsegv;

    if (do_setcontext(&rt_sf->uc, env, 1))
        goto sigsegv;

    do_sigaltstack(rt_sf_addr
                   + offsetof(struct target_rt_sigframe, uc.tuc_stack),
                   0, env->gpr[1]);

    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    return -TARGET_QEMU_ESIGRETURN;

sigsegv:
    unlock_user_struct(rt_sf, rt_sf_addr, 1);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

/* This syscall implements {get,set,swap}context for userland.  */
abi_long do_swapcontext(CPUArchState *env, abi_ulong uold_ctx,
                        abi_ulong unew_ctx, abi_long ctx_size)
{
    struct target_ucontext *uctx;
    struct target_mcontext *mctx;

    /* For ppc32, ctx_size is "reserved for future use".
     * For ppc64, we do not yet support the VSX extension.
     */
    if (ctx_size < sizeof(struct target_ucontext)) {
        return -TARGET_EINVAL;
    }

    if (uold_ctx) {
        TaskState *ts = (TaskState *)thread_cpu->opaque;

        if (!lock_user_struct(VERIFY_WRITE, uctx, uold_ctx, 1)) {
            return -TARGET_EFAULT;
        }

#ifdef TARGET_PPC64
        mctx = &uctx->tuc_sigcontext.mcontext;
#else
        /* ??? The kernel aligns the pointer down here into padding, but
         * in setup_rt_frame we don't.  Be self-compatible for now.
         */
        mctx = &uctx->tuc_mcontext;
        __put_user(h2g(mctx), &uctx->tuc_regs);
#endif

        save_user_regs(env, mctx);
        host_to_target_sigset(&uctx->tuc_sigmask, &ts->signal_mask);

        unlock_user_struct(uctx, uold_ctx, 1);
    }

    if (unew_ctx) {
        int err;

        if (!lock_user_struct(VERIFY_READ, uctx, unew_ctx, 1)) {
            return -TARGET_EFAULT;
        }
        err = do_setcontext(uctx, env, 0);
        unlock_user_struct(uctx, unew_ctx, 1);

        if (err) {
            /* We cannot return to a partially updated context.  */
            force_sig(TARGET_SIGSEGV);
        }
        return -TARGET_QEMU_ESIGRETURN;
    }

    return 0;
}
