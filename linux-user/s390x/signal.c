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

#define __NUM_GPRS 16
#define __NUM_FPRS 16
#define __NUM_ACRS 16

#define __SIGNAL_FRAMESIZE      160 /* FIXME: 31-bit mode -> 96 */

#define _SIGCONTEXT_NSIG        64
#define _SIGCONTEXT_NSIG_BPW    64 /* FIXME: 31-bit mode -> 32 */
#define _SIGCONTEXT_NSIG_WORDS  (_SIGCONTEXT_NSIG / _SIGCONTEXT_NSIG_BPW)
#define _SIGMASK_COPY_SIZE    (sizeof(unsigned long)*_SIGCONTEXT_NSIG_WORDS)
#define S390_SYSCALL_OPCODE ((uint16_t)0x0a00)

typedef struct {
    target_psw_t psw;
    abi_ulong gprs[__NUM_GPRS];
    abi_uint acrs[__NUM_ACRS];
} target_s390_regs_common;

typedef struct {
    uint32_t fpc;
    uint32_t pad;
    uint64_t fprs[__NUM_FPRS];
} target_s390_fp_regs;

typedef struct {
    target_s390_regs_common regs;
    target_s390_fp_regs     fpregs;
} target_sigregs;

typedef struct {
    uint64_t vxrs_low[16];
    uint64_t vxrs_high[16][2];
    uint8_t reserved[128];
} target_sigregs_ext;

typedef struct {
    abi_ulong oldmask[_SIGCONTEXT_NSIG_WORDS];
    abi_ulong sregs;
} target_sigcontext;

typedef struct {
    uint8_t callee_used_stack[__SIGNAL_FRAMESIZE];
    target_sigcontext sc;
    target_sigregs sregs;
    int signo;
    target_sigregs_ext sregs_ext;
} sigframe;

#define TARGET_UC_VXRS 2

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    target_sigregs tuc_mcontext;
    target_sigset_t tuc_sigmask;
    uint8_t reserved[128 - sizeof(target_sigset_t)];
    target_sigregs_ext tuc_mcontext_ext;
};

typedef struct {
    uint8_t callee_used_stack[__SIGNAL_FRAMESIZE];
    struct target_siginfo info;
    struct target_ucontext uc;
} rt_sigframe;

static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUS390XState *env, size_t frame_size)
{
    abi_ulong sp;

    /* Default to using normal stack */
    sp = get_sp_from_cpustate(env);

    /* This is the X/Open sanctioned signal stack switching.  */
    if (ka->sa_flags & TARGET_SA_ONSTACK) {
        sp = target_sigsp(sp, ka);
    }

    /* This is the legacy signal stack switching. */
    else if (/* FIXME !user_mode(regs) */ 0 &&
             !(ka->sa_flags & TARGET_SA_RESTORER) &&
             ka->sa_restorer) {
        sp = (abi_ulong) ka->sa_restorer;
    }

    return (sp - frame_size) & -8ul;
}

#define PSW_USER_BITS   (PSW_MASK_DAT | PSW_MASK_IO | PSW_MASK_EXT | \
                         PSW_MASK_MCHECK | PSW_MASK_PSTATE | PSW_ASC_PRIMARY)
#define PSW_MASK_USER   (PSW_MASK_ASC | PSW_MASK_CC | PSW_MASK_PM | \
                         PSW_MASK_64 | PSW_MASK_32)

static void save_sigregs(CPUS390XState *env, target_sigregs *sregs)
{
    uint64_t psw_mask = s390_cpu_get_psw_mask(env);
    int i;

    /*
     * Copy a 'clean' PSW mask to the user to avoid leaking
     * information about whether PER is currently on.
     * TODO: qemu does not support PSW_MASK_RI; it will never be set.
     */
    psw_mask = PSW_USER_BITS | (psw_mask & PSW_MASK_USER);
    __put_user(psw_mask, &sregs->regs.psw.mask);
    __put_user(env->psw.addr, &sregs->regs.psw.addr);

    for (i = 0; i < 16; i++) {
        __put_user(env->regs[i], &sregs->regs.gprs[i]);
    }
    for (i = 0; i < 16; i++) {
        __put_user(env->aregs[i], &sregs->regs.acrs[i]);
    }

    /*
     * We have to store the fp registers to current->thread.fp_regs
     * to merge them with the emulated registers.
     */
    for (i = 0; i < 16; i++) {
        __put_user(*get_freg(env, i), &sregs->fpregs.fprs[i]);
    }
}

static void save_sigregs_ext(CPUS390XState *env, target_sigregs_ext *ext)
{
    int i;

    /*
     * if (MACHINE_HAS_VX) ...
     * That said, we always allocate the stack storage and the
     * space is always available in env.
     */
    for (i = 0; i < 16; ++i) {
        __put_user(env->vregs[i][1], &ext->vxrs_low[i]);
    }
    for (i = 0; i < 16; ++i) {
        __put_user(env->vregs[i + 16][0], &ext->vxrs_high[i][0]);
        __put_user(env->vregs[i + 16][1], &ext->vxrs_high[i][1]);
    }
}

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUS390XState *env)
{
    sigframe *frame;
    abi_ulong frame_addr;
    abi_ulong restorer;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        force_sigsegv(sig);
        return;
    }

    /* Set up backchain. */
    __put_user(env->regs[15], (abi_ulong *) frame);

    /* Create struct sigcontext on the signal stack. */
    /* Make sure that we're initializing all of oldmask. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(frame->sc.oldmask) != 1);
    __put_user(set->sig[0], &frame->sc.oldmask[0]);
    __put_user(frame_addr + offsetof(sigframe, sregs), &frame->sc.sregs);

    /* Create _sigregs on the signal stack */
    save_sigregs(env, &frame->sregs);

    /*
     * ??? The kernel uses regs->gprs[2] here, which is not yet the signo.
     * Moreover the comment talks about allowing backtrace, which is really
     * done by the r15 copy above.
     */
    __put_user(sig, &frame->signo);

    /* Create sigregs_ext on the signal stack. */
    save_sigregs_ext(env, &frame->sregs_ext);

    /*
     * Set up to return from userspace.
     * If provided, use a stub already in userspace.
     */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        restorer = ka->sa_restorer;
    } else {
        restorer = default_sigreturn;
    }

    /* Set up registers for signal handler */
    env->regs[14] = restorer;
    env->regs[15] = frame_addr;
    /* Force default amode and default user address space control. */
    env->psw.mask = PSW_MASK_64 | PSW_MASK_32 | PSW_ASC_PRIMARY
                  | (env->psw.mask & ~PSW_MASK_ASC);
    env->psw.addr = ka->_sa_handler;

    env->regs[2] = sig;
    env->regs[3] = frame_addr + offsetof(typeof(*frame), sc);

    /*
     * We forgot to include these in the sigcontext.
     * To avoid breaking binary compatibility, they are passed as args.
     */
    env->regs[4] = 0; /* FIXME: regs->int_code & 127 */
    env->regs[5] = 0; /* FIXME: regs->int_parm_long */
    env->regs[6] = 0; /* FIXME: current->thread.last_break */

    unlock_user_struct(frame, frame_addr, 1);
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUS390XState *env)
{
    rt_sigframe *frame;
    abi_ulong frame_addr;
    abi_ulong restorer;
    abi_ulong uc_flags;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        force_sigsegv(sig);
        return;
    }

    /* Set up backchain. */
    __put_user(env->regs[15], (abi_ulong *) frame);

    /*
     * Set up to return from userspace.
     * If provided, use a stub already in userspace.
     */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        restorer = ka->sa_restorer;
    } else {
        restorer = default_rt_sigreturn;
    }

    /* Create siginfo on the signal stack. */
    tswap_siginfo(&frame->info, info);

    /* Create ucontext on the signal stack. */
    uc_flags = 0;
    if (s390_has_feat(S390_FEAT_VECTOR)) {
        uc_flags |= TARGET_UC_VXRS;
    }
    __put_user(uc_flags, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    save_sigregs(env, &frame->uc.tuc_mcontext);
    save_sigregs_ext(env, &frame->uc.tuc_mcontext_ext);
    tswap_sigset(&frame->uc.tuc_sigmask, set);

    /* Set up registers for signal handler */
    env->regs[14] = restorer;
    env->regs[15] = frame_addr;
    /* Force default amode and default user address space control. */
    env->psw.mask = PSW_MASK_64 | PSW_MASK_32 | PSW_ASC_PRIMARY
                  | (env->psw.mask & ~PSW_MASK_ASC);
    env->psw.addr = ka->_sa_handler;

    env->regs[2] = sig;
    env->regs[3] = frame_addr + offsetof(typeof(*frame), info);
    env->regs[4] = frame_addr + offsetof(typeof(*frame), uc);
    env->regs[5] = 0; /* FIXME: current->thread.last_break */
}

static void restore_sigregs(CPUS390XState *env, target_sigregs *sc)
{
    uint64_t prev_addr, prev_mask, mask, addr;
    int i;

    for (i = 0; i < 16; i++) {
        __get_user(env->regs[i], &sc->regs.gprs[i]);
    }

    prev_addr = env->psw.addr;
    __get_user(mask, &sc->regs.psw.mask);
    __get_user(addr, &sc->regs.psw.addr);
    trace_user_s390x_restore_sigregs(env, addr, prev_addr);

    /*
     * Use current psw.mask to preserve PER bit.
     * TODO:
     *  if (!is_ri_task(current) && (user_sregs.regs.psw.mask & PSW_MASK_RI))
     *          return -EINVAL;
     * Simply do not allow it to be set in mask.
     */
    prev_mask = s390_cpu_get_psw_mask(env);
    mask = (prev_mask & ~PSW_MASK_USER) | (mask & PSW_MASK_USER);
    /* Check for invalid user address space control. */
    if ((mask & PSW_MASK_ASC) == PSW_ASC_HOME) {
        mask = (mask & ~PSW_MASK_ASC) | PSW_ASC_PRIMARY;
    }
    /* Check for invalid amode. */
    if (mask & PSW_MASK_64) {
        mask |= PSW_MASK_32;
    }
    s390_cpu_set_psw(env, mask, addr);

    for (i = 0; i < 16; i++) {
        __get_user(env->aregs[i], &sc->regs.acrs[i]);
    }
    for (i = 0; i < 16; i++) {
        __get_user(*get_freg(env, i), &sc->fpregs.fprs[i]);
    }
}

static void restore_sigregs_ext(CPUS390XState *env, target_sigregs_ext *ext)
{
    int i;

    /*
     * if (MACHINE_HAS_VX) ...
     * That said, we always allocate the stack storage and the
     * space is always available in env.
     */
    for (i = 0; i < 16; ++i) {
        __get_user(env->vregs[i][1], &ext->vxrs_low[i]);
    }
    for (i = 0; i < 16; ++i) {
        __get_user(env->vregs[i + 16][0], &ext->vxrs_high[i][0]);
        __get_user(env->vregs[i + 16][1], &ext->vxrs_high[i][1]);
    }
}

long do_sigreturn(CPUS390XState *env)
{
    sigframe *frame;
    abi_ulong frame_addr = env->regs[15];
    target_sigset_t target_set;
    sigset_t set;

    trace_user_do_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        force_sig(TARGET_SIGSEGV);
        return -QEMU_ESIGRETURN;
    }

    /* Make sure that we're initializing all of target_set. */
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(target_set.sig) != 1);
    __get_user(target_set.sig[0], &frame->sc.oldmask[0]);

    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set); /* ~_BLOCKABLE? */

    restore_sigregs(env, &frame->sregs);
    restore_sigregs_ext(env, &frame->sregs_ext);

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUS390XState *env)
{
    rt_sigframe *frame;
    abi_ulong frame_addr = env->regs[15];
    sigset_t set;

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        force_sig(TARGET_SIGSEGV);
        return -QEMU_ESIGRETURN;
    }
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);

    set_sigmask(&set); /* ~_BLOCKABLE? */

    restore_sigregs(env, &frame->uc.tuc_mcontext);
    restore_sigregs_ext(env, &frame->uc.tuc_mcontext_ext);

    target_restore_altstack(&frame->uc.tuc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint16_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 2 + 2, 0);
    assert(tramp != NULL);

    default_sigreturn = sigtramp_page;
    __put_user(S390_SYSCALL_OPCODE | TARGET_NR_sigreturn, &tramp[0]);

    default_rt_sigreturn = sigtramp_page + 2;
    __put_user(S390_SYSCALL_OPCODE | TARGET_NR_rt_sigreturn, &tramp[1]);

    unlock_user(tramp, sigtramp_page, 2 + 2);
}
