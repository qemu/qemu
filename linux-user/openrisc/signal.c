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
#include "target_signal.h"
#include "signal-common.h"
#include "linux-user/trace.h"

struct target_sigcontext {
    struct target_pt_regs regs;
    abi_ulong oldmask;
    abi_ulong usp;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;   /* mask last for extensibility */
};

struct target_rt_sigframe {
    abi_ulong pinfo;
    uint64_t puc;
    struct target_siginfo info;
    struct target_sigcontext sc;
    struct target_ucontext uc;
    unsigned char retcode[16];  /* trampoline code */
};

/* This is the asm-generic/ucontext.h version */
#if 0
static int restore_sigcontext(CPUOpenRISCState *regs,
                              struct target_sigcontext *sc)
{
    unsigned int err = 0;
    unsigned long old_usp;

    /* Alwys make any pending restarted system call return -EINTR */
    current_thread_info()->restart_block.fn = do_no_restart_syscall;

    /* restore the regs from &sc->regs (same as sc, since regs is first)
     * (sc is already checked for VERIFY_READ since the sigframe was
     *  checked in sys_sigreturn previously)
     */

    if (copy_from_user(regs, &sc, sizeof(struct target_pt_regs))) {
        goto badframe;
    }

    /* make sure the U-flag is set so user-mode cannot fool us */

    regs->sr &= ~SR_SM;

    /* restore the old USP as it was before we stacked the sc etc.
     * (we cannot just pop the sigcontext since we aligned the sp and
     *  stuff after pushing it)
     */

    __get_user(old_usp, &sc->usp);
    phx_signal("old_usp 0x%lx", old_usp);

    __PHX__ REALLY           /* ??? */
    wrusp(old_usp);
    regs->gpr[1] = old_usp;

    /* TODO: the other ports use regs->orig_XX to disable syscall checks
     * after this completes, but we don't use that mechanism. maybe we can
     * use it now ?
     */

    return err;

badframe:
    return 1;
}
#endif

/* Set up a signal frame.  */

static void setup_sigcontext(struct target_sigcontext *sc,
                             CPUOpenRISCState *regs,
                             unsigned long mask)
{
    unsigned long usp = cpu_get_gpr(regs, 1);

    /* copy the regs. they are first in sc so we can use sc directly */

    /*copy_to_user(&sc, regs, sizeof(struct target_pt_regs));*/

    /* Set the frametype to CRIS_FRAME_NORMAL for the execution of
       the signal handler. The frametype will be restored to its previous
       value in restore_sigcontext. */
    /*regs->frametype = CRIS_FRAME_NORMAL;*/

    /* then some other stuff */
    __put_user(mask, &sc->oldmask);
    __put_user(usp, &sc->usp);
}

static inline unsigned long align_sigframe(unsigned long sp)
{
    return sp & ~3UL;
}

static inline abi_ulong get_sigframe(struct target_sigaction *ka,
                                     CPUOpenRISCState *regs,
                                     size_t frame_size)
{
    unsigned long sp = get_sp_from_cpustate(regs);
    int onsigstack = on_sig_stack(sp);

    /* redzone */
    sp = target_sigsp(sp, ka);

    sp = align_sigframe(sp - frame_size);

    /*
     * If we are on the alternate signal stack and would overflow it, don't.
     * Return an always-bogus address instead so we will die with SIGSEGV.
     */

    if (onsigstack && !likely(on_sig_stack(sp))) {
        return -1L;
    }

    return sp;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUOpenRISCState *env)
{
    int err = 0;
    abi_ulong frame_addr;
    unsigned long return_ip;
    struct target_rt_sigframe *frame;
    abi_ulong info_addr, uc_addr;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    info_addr = frame_addr + offsetof(struct target_rt_sigframe, info);
    __put_user(info_addr, &frame->pinfo);
    uc_addr = frame_addr + offsetof(struct target_rt_sigframe, uc);
    __put_user(uc_addr, &frame->puc);

    if (ka->sa_flags & SA_SIGINFO) {
        tswap_siginfo(&frame->info, info);
    }

    /*err |= __clear_user(&frame->uc, offsetof(ucontext_t, uc_mcontext));*/
    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    setup_sigcontext(&frame->sc, env, set->sig[0]);

    /*err |= copy_to_user(frame->uc.tuc_sigmask, set, sizeof(*set));*/

    /* trampoline - the desired return ip is the retcode itself */
    return_ip = (unsigned long)&frame->retcode;
    /* This is l.ori r11,r0,__NR_sigreturn, l.sys 1 */
    __put_user(0xa960, (short *)(frame->retcode + 0));
    __put_user(TARGET_NR_rt_sigreturn, (short *)(frame->retcode + 2));
    __put_user(0x20000001, (unsigned long *)(frame->retcode + 4));
    __put_user(0x15000000, (unsigned long *)(frame->retcode + 8));

    if (err) {
        goto give_sigsegv;
    }

    /* TODO what is the current->exec_domain stuff and invmap ? */

    /* Set up registers for signal handler */
    env->pc = (unsigned long)ka->_sa_handler; /* what we enter NOW */
    cpu_set_gpr(env, 9, (unsigned long)return_ip);     /* what we enter LATER */
    cpu_set_gpr(env, 3, (unsigned long)sig);           /* arg 1: signo */
    cpu_set_gpr(env, 4, (unsigned long)&frame->info);  /* arg 2: (siginfo_t*) */
    cpu_set_gpr(env, 5, (unsigned long)&frame->uc);    /* arg 3: ucontext */

    /* actually move the usp to reflect the stacked frame */
    cpu_set_gpr(env, 1, (unsigned long)frame);

    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(sig);
}

long do_sigreturn(CPUOpenRISCState *env)
{
    trace_user_do_sigreturn(env, 0);
    fprintf(stderr, "do_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

long do_rt_sigreturn(CPUOpenRISCState *env)
{
    trace_user_do_rt_sigreturn(env, 0);
    fprintf(stderr, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}
