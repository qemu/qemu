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

struct target_sigcontext {
    struct target_pt_regs regs;  /* needs to be first */
    uint32_t oldmask;
    uint32_t usp;    /* usp before stacking this gunk on it */
};

/* Signal frames. */
struct target_signal_frame {
    struct target_sigcontext sc;
    uint32_t extramask[TARGET_NSIG_WORDS - 1];
    uint16_t retcode[4];      /* Trampoline code. */
};

struct rt_signal_frame {
    siginfo_t *pinfo;
    void *puc;
    siginfo_t info;
    ucontext_t uc;
    uint16_t retcode[4];      /* Trampoline code. */
};

static void setup_sigcontext(struct target_sigcontext *sc, CPUCRISState *env)
{
    __put_user(env->regs[0], &sc->regs.r0);
    __put_user(env->regs[1], &sc->regs.r1);
    __put_user(env->regs[2], &sc->regs.r2);
    __put_user(env->regs[3], &sc->regs.r3);
    __put_user(env->regs[4], &sc->regs.r4);
    __put_user(env->regs[5], &sc->regs.r5);
    __put_user(env->regs[6], &sc->regs.r6);
    __put_user(env->regs[7], &sc->regs.r7);
    __put_user(env->regs[8], &sc->regs.r8);
    __put_user(env->regs[9], &sc->regs.r9);
    __put_user(env->regs[10], &sc->regs.r10);
    __put_user(env->regs[11], &sc->regs.r11);
    __put_user(env->regs[12], &sc->regs.r12);
    __put_user(env->regs[13], &sc->regs.r13);
    __put_user(env->regs[14], &sc->usp);
    __put_user(env->regs[15], &sc->regs.acr);
    __put_user(env->pregs[PR_MOF], &sc->regs.mof);
    __put_user(env->pregs[PR_SRP], &sc->regs.srp);
    __put_user(env->pc, &sc->regs.erp);
}

static void restore_sigcontext(struct target_sigcontext *sc, CPUCRISState *env)
{
    __get_user(env->regs[0], &sc->regs.r0);
    __get_user(env->regs[1], &sc->regs.r1);
    __get_user(env->regs[2], &sc->regs.r2);
    __get_user(env->regs[3], &sc->regs.r3);
    __get_user(env->regs[4], &sc->regs.r4);
    __get_user(env->regs[5], &sc->regs.r5);
    __get_user(env->regs[6], &sc->regs.r6);
    __get_user(env->regs[7], &sc->regs.r7);
    __get_user(env->regs[8], &sc->regs.r8);
    __get_user(env->regs[9], &sc->regs.r9);
    __get_user(env->regs[10], &sc->regs.r10);
    __get_user(env->regs[11], &sc->regs.r11);
    __get_user(env->regs[12], &sc->regs.r12);
    __get_user(env->regs[13], &sc->regs.r13);
    __get_user(env->regs[14], &sc->usp);
    __get_user(env->regs[15], &sc->regs.acr);
    __get_user(env->pregs[PR_MOF], &sc->regs.mof);
    __get_user(env->pregs[PR_SRP], &sc->regs.srp);
    __get_user(env->pc, &sc->regs.erp);
}

static abi_ulong get_sigframe(CPUCRISState *env, int framesize)
{
    abi_ulong sp;
    /* Align the stack downwards to 4.  */
    sp = (env->regs[R_SP] & ~3);
    return sp - framesize;
}

static void setup_sigreturn(uint16_t *retcode)
{
    /* This is movu.w __NR_sigreturn, r9; break 13; */
    __put_user(0x9c5f, retcode + 0);
    __put_user(TARGET_NR_sigreturn, retcode + 1);
    __put_user(0xe93d, retcode + 2);
}

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUCRISState *env)
{
    struct target_signal_frame *frame;
    abi_ulong frame_addr;
    int i;

    frame_addr = get_sigframe(env, sizeof *frame);
    trace_user_setup_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
        goto badframe;

    /*
     * The CRIS signal return trampoline. A real linux/CRIS kernel doesn't
     * use this trampoline anymore but it sets it up for GDB.
     */
    setup_sigreturn(frame->retcode);

    /* Save the mask.  */
    __put_user(set->sig[0], &frame->sc.oldmask);

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->extramask[i - 1]);
    }

    setup_sigcontext(&frame->sc, env);

    /* Move the stack and setup the arguments for the handler.  */
    env->regs[R_SP] = frame_addr;
    env->regs[10] = sig;
    env->pc = (unsigned long) ka->_sa_handler;
    /* Link SRP so the guest returns through the trampoline.  */
    env->pregs[PR_SRP] = default_sigreturn;

    unlock_user_struct(frame, frame_addr, 1);
    return;
badframe:
    force_sigsegv(sig);
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                     target_sigset_t *set, CPUCRISState *env)
{
    qemu_log_mask(LOG_UNIMP, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUCRISState *env)
{
    struct target_signal_frame *frame;
    abi_ulong frame_addr;
    target_sigset_t target_set;
    sigset_t set;
    int i;

    frame_addr = env->regs[R_SP];
    trace_user_do_sigreturn(env, frame_addr);
    /* Make sure the guest isn't playing games.  */
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 1)) {
        goto badframe;
    }

    /* Restore blocked signals */
    __get_user(target_set.sig[0], &frame->sc.oldmask);
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &frame->extramask[i - 1]);
    }
    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set);

    restore_sigcontext(&frame->sc, env);
    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;
badframe:
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUCRISState *env)
{
    trace_user_do_rt_sigreturn(env, 0);
    qemu_log_mask(LOG_UNIMP, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint16_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 6, 0);
    assert(tramp != NULL);

    default_sigreturn = sigtramp_page;
    setup_sigreturn(tramp);

    unlock_user(tramp, sigtramp_page, 6);
}
