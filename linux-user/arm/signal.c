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

struct target_sigcontext {
    abi_ulong trap_no;
    abi_ulong error_code;
    abi_ulong oldmask;
    abi_ulong arm_r0;
    abi_ulong arm_r1;
    abi_ulong arm_r2;
    abi_ulong arm_r3;
    abi_ulong arm_r4;
    abi_ulong arm_r5;
    abi_ulong arm_r6;
    abi_ulong arm_r7;
    abi_ulong arm_r8;
    abi_ulong arm_r9;
    abi_ulong arm_r10;
    abi_ulong arm_fp;
    abi_ulong arm_ip;
    abi_ulong arm_sp;
    abi_ulong arm_lr;
    abi_ulong arm_pc;
    abi_ulong arm_cpsr;
    abi_ulong fault_address;
};

struct target_ucontext_v1 {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t  tuc_sigmask;       /* mask last for extensibility */
};

struct target_ucontext_v2 {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t  tuc_sigmask;       /* mask last for extensibility */
    char __unused[128 - sizeof(target_sigset_t)];
    abi_ulong tuc_regspace[128] __attribute__((__aligned__(8)));
};

struct target_user_vfp {
    uint64_t fpregs[32];
    abi_ulong fpscr;
};

struct target_user_vfp_exc {
    abi_ulong fpexc;
    abi_ulong fpinst;
    abi_ulong fpinst2;
};

struct target_vfp_sigframe {
    abi_ulong magic;
    abi_ulong size;
    struct target_user_vfp ufp;
    struct target_user_vfp_exc ufp_exc;
} __attribute__((__aligned__(8)));

struct target_iwmmxt_sigframe {
    abi_ulong magic;
    abi_ulong size;
    uint64_t regs[16];
    /* Note that not all the coprocessor control registers are stored here */
    uint32_t wcssf;
    uint32_t wcasf;
    uint32_t wcgr0;
    uint32_t wcgr1;
    uint32_t wcgr2;
    uint32_t wcgr3;
} __attribute__((__aligned__(8)));

#define TARGET_VFP_MAGIC 0x56465001
#define TARGET_IWMMXT_MAGIC 0x12ef842a

struct sigframe_v1
{
    struct target_sigcontext sc;
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    abi_ulong retcode[4];
};

struct sigframe_v2
{
    struct target_ucontext_v2 uc;
    abi_ulong retcode[4];
};

struct rt_sigframe_v1
{
    abi_ulong pinfo;
    abi_ulong puc;
    struct target_siginfo info;
    struct target_ucontext_v1 uc;
    abi_ulong retcode[4];
};

struct rt_sigframe_v2
{
    struct target_siginfo info;
    struct target_ucontext_v2 uc;
    abi_ulong retcode[4];
};

#define TARGET_CONFIG_CPU_32 1

/*
 * For ARM syscalls, we encode the syscall number into the instruction.
 */
#define SWI_SYS_SIGRETURN       (0xef000000|(TARGET_NR_sigreturn + ARM_SYSCALL_BASE))
#define SWI_SYS_RT_SIGRETURN    (0xef000000|(TARGET_NR_rt_sigreturn + ARM_SYSCALL_BASE))

/*
 * For Thumb syscalls, we pass the syscall number via r7.  We therefore
 * need two 16-bit instructions.
 */
#define SWI_THUMB_SIGRETURN     (0xdf00 << 16 | 0x2700 | (TARGET_NR_sigreturn))
#define SWI_THUMB_RT_SIGRETURN  (0xdf00 << 16 | 0x2700 | (TARGET_NR_rt_sigreturn))

static const abi_ulong retcodes[4] = {
        SWI_SYS_SIGRETURN,      SWI_THUMB_SIGRETURN,
        SWI_SYS_RT_SIGRETURN,   SWI_THUMB_RT_SIGRETURN
};

/*
 * Stub needed to make sure the FD register (r9) contains the right
 * value.
 */
static const unsigned long sigreturn_fdpic_codes[3] = {
    0xe59fc004, /* ldr r12, [pc, #4] to read function descriptor */
    0xe59c9004, /* ldr r9, [r12, #4] to setup GOT */
    0xe59cf000  /* ldr pc, [r12] to jump into restorer */
};

static const unsigned long sigreturn_fdpic_thumb_codes[3] = {
    0xc008f8df, /* ldr r12, [pc, #8] to read function descriptor */
    0x9004f8dc, /* ldr r9, [r12, #4] to setup GOT */
    0xf000f8dc  /* ldr pc, [r12] to jump into restorer */
};

static inline int valid_user_regs(CPUARMState *regs)
{
    return 1;
}

static void
setup_sigcontext(struct target_sigcontext *sc, /*struct _fpstate *fpstate,*/
                 CPUARMState *env, abi_ulong mask)
{
    __put_user(env->regs[0], &sc->arm_r0);
    __put_user(env->regs[1], &sc->arm_r1);
    __put_user(env->regs[2], &sc->arm_r2);
    __put_user(env->regs[3], &sc->arm_r3);
    __put_user(env->regs[4], &sc->arm_r4);
    __put_user(env->regs[5], &sc->arm_r5);
    __put_user(env->regs[6], &sc->arm_r6);
    __put_user(env->regs[7], &sc->arm_r7);
    __put_user(env->regs[8], &sc->arm_r8);
    __put_user(env->regs[9], &sc->arm_r9);
    __put_user(env->regs[10], &sc->arm_r10);
    __put_user(env->regs[11], &sc->arm_fp);
    __put_user(env->regs[12], &sc->arm_ip);
    __put_user(env->regs[13], &sc->arm_sp);
    __put_user(env->regs[14], &sc->arm_lr);
    __put_user(env->regs[15], &sc->arm_pc);
#ifdef TARGET_CONFIG_CPU_32
    __put_user(cpsr_read(env), &sc->arm_cpsr);
#endif

    __put_user(/* current->thread.trap_no */ 0, &sc->trap_no);
    __put_user(/* current->thread.error_code */ 0, &sc->error_code);
    __put_user(/* current->thread.address */ 0, &sc->fault_address);
    __put_user(mask, &sc->oldmask);
}

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUARMState *regs, int framesize)
{
    unsigned long sp;

    sp = target_sigsp(get_sp_from_cpustate(regs), ka);
    /*
     * ATPCS B01 mandates 8-byte alignment
     */
    return (sp - framesize) & ~7;
}

static int
setup_return(CPUARMState *env, struct target_sigaction *ka,
             abi_ulong *rc, abi_ulong frame_addr, int usig, abi_ulong rc_addr)
{
    abi_ulong handler = 0;
    abi_ulong handler_fdpic_GOT = 0;
    abi_ulong retcode;

    int thumb;
    int is_fdpic = info_is_fdpic(((TaskState *)thread_cpu->opaque)->info);

    if (is_fdpic) {
        /* In FDPIC mode, ka->_sa_handler points to a function
         * descriptor (FD). The first word contains the address of the
         * handler. The second word contains the value of the PIC
         * register (r9).  */
        abi_ulong funcdesc_ptr = ka->_sa_handler;
        if (get_user_ual(handler, funcdesc_ptr)
            || get_user_ual(handler_fdpic_GOT, funcdesc_ptr + 4)) {
            return 1;
        }
    } else {
        handler = ka->_sa_handler;
    }

    thumb = handler & 1;

    uint32_t cpsr = cpsr_read(env);

    cpsr &= ~CPSR_IT;
    if (thumb) {
        cpsr |= CPSR_T;
    } else {
        cpsr &= ~CPSR_T;
    }

    if (ka->sa_flags & TARGET_SA_RESTORER) {
        if (is_fdpic) {
            /* For FDPIC we ensure that the restorer is called with a
             * correct r9 value.  For that we need to write code on
             * the stack that sets r9 and jumps back to restorer
             * value.
             */
            if (thumb) {
                __put_user(sigreturn_fdpic_thumb_codes[0], rc);
                __put_user(sigreturn_fdpic_thumb_codes[1], rc + 1);
                __put_user(sigreturn_fdpic_thumb_codes[2], rc + 2);
                __put_user((abi_ulong)ka->sa_restorer, rc + 3);
            } else {
                __put_user(sigreturn_fdpic_codes[0], rc);
                __put_user(sigreturn_fdpic_codes[1], rc + 1);
                __put_user(sigreturn_fdpic_codes[2], rc + 2);
                __put_user((abi_ulong)ka->sa_restorer, rc + 3);
            }

            retcode = rc_addr + thumb;
        } else {
            retcode = ka->sa_restorer;
        }
    } else {
        unsigned int idx = thumb;

        if (ka->sa_flags & TARGET_SA_SIGINFO) {
            idx += 2;
        }

        __put_user(retcodes[idx], rc);

        retcode = rc_addr + thumb;
    }

    env->regs[0] = usig;
    if (is_fdpic) {
        env->regs[9] = handler_fdpic_GOT;
    }
    env->regs[13] = frame_addr;
    env->regs[14] = retcode;
    env->regs[15] = handler & (thumb ? ~1 : ~3);
    cpsr_write(env, cpsr, CPSR_IT | CPSR_T, CPSRWriteByInstr);

    return 0;
}

static abi_ulong *setup_sigframe_v2_vfp(abi_ulong *regspace, CPUARMState *env)
{
    int i;
    struct target_vfp_sigframe *vfpframe;
    vfpframe = (struct target_vfp_sigframe *)regspace;
    __put_user(TARGET_VFP_MAGIC, &vfpframe->magic);
    __put_user(sizeof(*vfpframe), &vfpframe->size);
    for (i = 0; i < 32; i++) {
        __put_user(*aa32_vfp_dreg(env, i), &vfpframe->ufp.fpregs[i]);
    }
    __put_user(vfp_get_fpscr(env), &vfpframe->ufp.fpscr);
    __put_user(env->vfp.xregs[ARM_VFP_FPEXC], &vfpframe->ufp_exc.fpexc);
    __put_user(env->vfp.xregs[ARM_VFP_FPINST], &vfpframe->ufp_exc.fpinst);
    __put_user(env->vfp.xregs[ARM_VFP_FPINST2], &vfpframe->ufp_exc.fpinst2);
    return (abi_ulong*)(vfpframe+1);
}

static abi_ulong *setup_sigframe_v2_iwmmxt(abi_ulong *regspace,
                                           CPUARMState *env)
{
    int i;
    struct target_iwmmxt_sigframe *iwmmxtframe;
    iwmmxtframe = (struct target_iwmmxt_sigframe *)regspace;
    __put_user(TARGET_IWMMXT_MAGIC, &iwmmxtframe->magic);
    __put_user(sizeof(*iwmmxtframe), &iwmmxtframe->size);
    for (i = 0; i < 16; i++) {
        __put_user(env->iwmmxt.regs[i], &iwmmxtframe->regs[i]);
    }
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCSSF], &iwmmxtframe->wcssf);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCASF], &iwmmxtframe->wcssf);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR0], &iwmmxtframe->wcgr0);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR1], &iwmmxtframe->wcgr1);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR2], &iwmmxtframe->wcgr2);
    __put_user(env->vfp.xregs[ARM_IWMMXT_wCGR3], &iwmmxtframe->wcgr3);
    return (abi_ulong*)(iwmmxtframe+1);
}

static void setup_sigframe_v2(struct target_ucontext_v2 *uc,
                              target_sigset_t *set, CPUARMState *env)
{
    struct target_sigaltstack stack;
    int i;
    abi_ulong *regspace;

    /* Clear all the bits of the ucontext we don't use.  */
    memset(uc, 0, offsetof(struct target_ucontext_v2, tuc_mcontext));

    memset(&stack, 0, sizeof(stack));
    target_save_altstack(&stack, env);
    memcpy(&uc->tuc_stack, &stack, sizeof(stack));

    setup_sigcontext(&uc->tuc_mcontext, env, set->sig[0]);
    /* Save coprocessor signal frame.  */
    regspace = uc->tuc_regspace;
    if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
        regspace = setup_sigframe_v2_vfp(regspace, env);
    }
    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        regspace = setup_sigframe_v2_iwmmxt(regspace, env);
    }

    /* Write terminating magic word */
    __put_user(0, regspace);

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &uc->tuc_sigmask.sig[i]);
    }
}

/* compare linux/arch/arm/kernel/signal.c:setup_frame() */
static void setup_frame_v1(int usig, struct target_sigaction *ka,
                           target_sigset_t *set, CPUARMState *regs)
{
    struct sigframe_v1 *frame;
    abi_ulong frame_addr = get_sigframe(ka, regs, sizeof(*frame));
    int i;

    trace_user_setup_frame(regs, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto sigsegv;
    }

    setup_sigcontext(&frame->sc, regs, set->sig[0]);

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->extramask[i - 1]);
    }

    if (setup_return(regs, ka, frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct sigframe_v1, retcode))) {
        goto sigsegv;
    }

    unlock_user_struct(frame, frame_addr, 1);
    return;
sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(usig);
}

static void setup_frame_v2(int usig, struct target_sigaction *ka,
                           target_sigset_t *set, CPUARMState *regs)
{
    struct sigframe_v2 *frame;
    abi_ulong frame_addr = get_sigframe(ka, regs, sizeof(*frame));

    trace_user_setup_frame(regs, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto sigsegv;
    }

    setup_sigframe_v2(&frame->uc, set, regs);

    if (setup_return(regs, ka, frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct sigframe_v2, retcode))) {
        goto sigsegv;
    }

    unlock_user_struct(frame, frame_addr, 1);
    return;
sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(usig);
}

void setup_frame(int usig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUARMState *regs)
{
    if (get_osversion() >= 0x020612) {
        setup_frame_v2(usig, ka, set, regs);
    } else {
        setup_frame_v1(usig, ka, set, regs);
    }
}

/* compare linux/arch/arm/kernel/signal.c:setup_rt_frame() */
static void setup_rt_frame_v1(int usig, struct target_sigaction *ka,
                              target_siginfo_t *info,
                              target_sigset_t *set, CPUARMState *env)
{
    struct rt_sigframe_v1 *frame;
    abi_ulong frame_addr = get_sigframe(ka, env, sizeof(*frame));
    struct target_sigaltstack stack;
    int i;
    abi_ulong info_addr, uc_addr;

    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto sigsegv;
    }

    info_addr = frame_addr + offsetof(struct rt_sigframe_v1, info);
    __put_user(info_addr, &frame->pinfo);
    uc_addr = frame_addr + offsetof(struct rt_sigframe_v1, uc);
    __put_user(uc_addr, &frame->puc);
    tswap_siginfo(&frame->info, info);

    /* Clear all the bits of the ucontext we don't use.  */
    memset(&frame->uc, 0, offsetof(struct target_ucontext_v1, tuc_mcontext));

    memset(&stack, 0, sizeof(stack));
    target_save_altstack(&stack, env);
    memcpy(&frame->uc.tuc_stack, &stack, sizeof(stack));

    setup_sigcontext(&frame->uc.tuc_mcontext, env, set->sig[0]);
    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    if (setup_return(env, ka, frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct rt_sigframe_v1, retcode))) {
        goto sigsegv;
    }

    env->regs[1] = info_addr;
    env->regs[2] = uc_addr;

    unlock_user_struct(frame, frame_addr, 1);
    return;
sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(usig);
}

static void setup_rt_frame_v2(int usig, struct target_sigaction *ka,
                              target_siginfo_t *info,
                              target_sigset_t *set, CPUARMState *env)
{
    struct rt_sigframe_v2 *frame;
    abi_ulong frame_addr = get_sigframe(ka, env, sizeof(*frame));
    abi_ulong info_addr, uc_addr;

    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto sigsegv;
    }

    info_addr = frame_addr + offsetof(struct rt_sigframe_v2, info);
    uc_addr = frame_addr + offsetof(struct rt_sigframe_v2, uc);
    tswap_siginfo(&frame->info, info);

    setup_sigframe_v2(&frame->uc, set, env);

    if (setup_return(env, ka, frame->retcode, frame_addr, usig,
                     frame_addr + offsetof(struct rt_sigframe_v2, retcode))) {
        goto sigsegv;
    }

    env->regs[1] = info_addr;
    env->regs[2] = uc_addr;

    unlock_user_struct(frame, frame_addr, 1);
    return;
sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(usig);
}

void setup_rt_frame(int usig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUARMState *env)
{
    if (get_osversion() >= 0x020612) {
        setup_rt_frame_v2(usig, ka, info, set, env);
    } else {
        setup_rt_frame_v1(usig, ka, info, set, env);
    }
}

static int
restore_sigcontext(CPUARMState *env, struct target_sigcontext *sc)
{
    int err = 0;
    uint32_t cpsr;

    __get_user(env->regs[0], &sc->arm_r0);
    __get_user(env->regs[1], &sc->arm_r1);
    __get_user(env->regs[2], &sc->arm_r2);
    __get_user(env->regs[3], &sc->arm_r3);
    __get_user(env->regs[4], &sc->arm_r4);
    __get_user(env->regs[5], &sc->arm_r5);
    __get_user(env->regs[6], &sc->arm_r6);
    __get_user(env->regs[7], &sc->arm_r7);
    __get_user(env->regs[8], &sc->arm_r8);
    __get_user(env->regs[9], &sc->arm_r9);
    __get_user(env->regs[10], &sc->arm_r10);
    __get_user(env->regs[11], &sc->arm_fp);
    __get_user(env->regs[12], &sc->arm_ip);
    __get_user(env->regs[13], &sc->arm_sp);
    __get_user(env->regs[14], &sc->arm_lr);
    __get_user(env->regs[15], &sc->arm_pc);
#ifdef TARGET_CONFIG_CPU_32
    __get_user(cpsr, &sc->arm_cpsr);
    cpsr_write(env, cpsr, CPSR_USER | CPSR_EXEC, CPSRWriteByInstr);
#endif

    err |= !valid_user_regs(env);

    return err;
}

static long do_sigreturn_v1(CPUARMState *env)
{
    abi_ulong frame_addr;
    struct sigframe_v1 *frame = NULL;
    target_sigset_t set;
    sigset_t host_set;
    int i;

    /*
     * Since we stacked the signal on a 64-bit boundary,
     * then 'sp' should be word aligned here.  If it's
     * not, then the user is trying to mess with us.
     */
    frame_addr = env->regs[13];
    trace_user_do_sigreturn(env, frame_addr);
    if (frame_addr & 7) {
        goto badframe;
    }

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    __get_user(set.sig[0], &frame->sc.oldmask);
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(set.sig[i], &frame->extramask[i - 1]);
    }

    target_to_host_sigset_internal(&host_set, &set);
    set_sigmask(&host_set);

    if (restore_sigcontext(env, &frame->sc)) {
        goto badframe;
    }

#if 0
    /* Send SIGTRAP if we're single-stepping */
    if (ptrace_cancel_bpt(current))
        send_sig(SIGTRAP, current, 1);
#endif
    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

static abi_ulong *restore_sigframe_v2_vfp(CPUARMState *env, abi_ulong *regspace)
{
    int i;
    abi_ulong magic, sz;
    uint32_t fpscr, fpexc;
    struct target_vfp_sigframe *vfpframe;
    vfpframe = (struct target_vfp_sigframe *)regspace;

    __get_user(magic, &vfpframe->magic);
    __get_user(sz, &vfpframe->size);
    if (magic != TARGET_VFP_MAGIC || sz != sizeof(*vfpframe)) {
        return 0;
    }
    for (i = 0; i < 32; i++) {
        __get_user(*aa32_vfp_dreg(env, i), &vfpframe->ufp.fpregs[i]);
    }
    __get_user(fpscr, &vfpframe->ufp.fpscr);
    vfp_set_fpscr(env, fpscr);
    __get_user(fpexc, &vfpframe->ufp_exc.fpexc);
    /* Sanitise FPEXC: ensure VFP is enabled, FPINST2 is invalid
     * and the exception flag is cleared
     */
    fpexc |= (1 << 30);
    fpexc &= ~((1 << 31) | (1 << 28));
    env->vfp.xregs[ARM_VFP_FPEXC] = fpexc;
    __get_user(env->vfp.xregs[ARM_VFP_FPINST], &vfpframe->ufp_exc.fpinst);
    __get_user(env->vfp.xregs[ARM_VFP_FPINST2], &vfpframe->ufp_exc.fpinst2);
    return (abi_ulong*)(vfpframe + 1);
}

static abi_ulong *restore_sigframe_v2_iwmmxt(CPUARMState *env,
                                             abi_ulong *regspace)
{
    int i;
    abi_ulong magic, sz;
    struct target_iwmmxt_sigframe *iwmmxtframe;
    iwmmxtframe = (struct target_iwmmxt_sigframe *)regspace;

    __get_user(magic, &iwmmxtframe->magic);
    __get_user(sz, &iwmmxtframe->size);
    if (magic != TARGET_IWMMXT_MAGIC || sz != sizeof(*iwmmxtframe)) {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        __get_user(env->iwmmxt.regs[i], &iwmmxtframe->regs[i]);
    }
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCSSF], &iwmmxtframe->wcssf);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCASF], &iwmmxtframe->wcssf);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR0], &iwmmxtframe->wcgr0);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR1], &iwmmxtframe->wcgr1);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR2], &iwmmxtframe->wcgr2);
    __get_user(env->vfp.xregs[ARM_IWMMXT_wCGR3], &iwmmxtframe->wcgr3);
    return (abi_ulong*)(iwmmxtframe + 1);
}

static int do_sigframe_return_v2(CPUARMState *env,
                                 target_ulong context_addr,
                                 struct target_ucontext_v2 *uc)
{
    sigset_t host_set;
    abi_ulong *regspace;

    target_to_host_sigset(&host_set, &uc->tuc_sigmask);
    set_sigmask(&host_set);

    if (restore_sigcontext(env, &uc->tuc_mcontext))
        return 1;

    /* Restore coprocessor signal frame */
    regspace = uc->tuc_regspace;
    if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
        regspace = restore_sigframe_v2_vfp(env, regspace);
        if (!regspace) {
            return 1;
        }
    }
    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        regspace = restore_sigframe_v2_iwmmxt(env, regspace);
        if (!regspace) {
            return 1;
        }
    }

    if (do_sigaltstack(context_addr
                       + offsetof(struct target_ucontext_v2, tuc_stack),
                       0, get_sp_from_cpustate(env)) == -EFAULT) {
        return 1;
    }

#if 0
    /* Send SIGTRAP if we're single-stepping */
    if (ptrace_cancel_bpt(current))
        send_sig(SIGTRAP, current, 1);
#endif

    return 0;
}

static long do_sigreturn_v2(CPUARMState *env)
{
    abi_ulong frame_addr;
    struct sigframe_v2 *frame = NULL;

    /*
     * Since we stacked the signal on a 64-bit boundary,
     * then 'sp' should be word aligned here.  If it's
     * not, then the user is trying to mess with us.
     */
    frame_addr = env->regs[13];
    trace_user_do_sigreturn(env, frame_addr);
    if (frame_addr & 7) {
        goto badframe;
    }

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    if (do_sigframe_return_v2(env,
                              frame_addr
                              + offsetof(struct sigframe_v2, uc),
                              &frame->uc)) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

long do_sigreturn(CPUARMState *env)
{
    if (get_osversion() >= 0x020612) {
        return do_sigreturn_v2(env);
    } else {
        return do_sigreturn_v1(env);
    }
}

static long do_rt_sigreturn_v1(CPUARMState *env)
{
    abi_ulong frame_addr;
    struct rt_sigframe_v1 *frame = NULL;
    sigset_t host_set;

    /*
     * Since we stacked the signal on a 64-bit boundary,
     * then 'sp' should be word aligned here.  If it's
     * not, then the user is trying to mess with us.
     */
    frame_addr = env->regs[13];
    trace_user_do_rt_sigreturn(env, frame_addr);
    if (frame_addr & 7) {
        goto badframe;
    }

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    target_to_host_sigset(&host_set, &frame->uc.tuc_sigmask);
    set_sigmask(&host_set);

    if (restore_sigcontext(env, &frame->uc.tuc_mcontext)) {
        goto badframe;
    }

    if (do_sigaltstack(frame_addr + offsetof(struct rt_sigframe_v1, uc.tuc_stack), 0, get_sp_from_cpustate(env)) == -EFAULT)
        goto badframe;

#if 0
    /* Send SIGTRAP if we're single-stepping */
    if (ptrace_cancel_bpt(current))
        send_sig(SIGTRAP, current, 1);
#endif
    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

static long do_rt_sigreturn_v2(CPUARMState *env)
{
    abi_ulong frame_addr;
    struct rt_sigframe_v2 *frame = NULL;

    /*
     * Since we stacked the signal on a 64-bit boundary,
     * then 'sp' should be word aligned here.  If it's
     * not, then the user is trying to mess with us.
     */
    frame_addr = env->regs[13];
    trace_user_do_rt_sigreturn(env, frame_addr);
    if (frame_addr & 7) {
        goto badframe;
    }

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    if (do_sigframe_return_v2(env,
                              frame_addr
                              + offsetof(struct rt_sigframe_v2, uc),
                              &frame->uc)) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUARMState *env)
{
    if (get_osversion() >= 0x020612) {
        return do_rt_sigreturn_v2(env);
    } else {
        return do_rt_sigreturn_v1(env);
    }
}
