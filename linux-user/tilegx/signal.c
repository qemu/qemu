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
    union {
        /* General-purpose registers.  */
        abi_ulong gregs[56];
        struct {
            abi_ulong __gregs[53];
            abi_ulong tp;        /* Aliases gregs[TREG_TP].  */
            abi_ulong sp;        /* Aliases gregs[TREG_SP].  */
            abi_ulong lr;        /* Aliases gregs[TREG_LR].  */
        };
    };
    abi_ulong pc;        /* Program counter.  */
    abi_ulong ics;       /* In Interrupt Critical Section?  */
    abi_ulong faultnum;  /* Fault number.  */
    abi_ulong pad[5];
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;   /* mask last for extensibility */
};

struct target_rt_sigframe {
    unsigned char save_area[16]; /* caller save area */
    struct target_siginfo info;
    struct target_ucontext uc;
    abi_ulong retcode[2];
};

#define INSN_MOVELI_R10_139  0x00045fe551483000ULL /* { moveli r10, 139 } */
#define INSN_SWINT1          0x286b180051485000ULL /* { swint1 } */


static void setup_sigcontext(struct target_sigcontext *sc,
                             CPUArchState *env, int signo)
{
    int i;

    for (i = 0; i < TILEGX_R_COUNT; ++i) {
        __put_user(env->regs[i], &sc->gregs[i]);
    }

    __put_user(env->pc, &sc->pc);
    __put_user(0, &sc->ics);
    __put_user(signo, &sc->faultnum);
}

static void restore_sigcontext(CPUTLGState *env, struct target_sigcontext *sc)
{
    int i;

    for (i = 0; i < TILEGX_R_COUNT; ++i) {
        __get_user(env->regs[i], &sc->gregs[i]);
    }

    __get_user(env->pc, &sc->pc);
}

static abi_ulong get_sigframe(struct target_sigaction *ka, CPUArchState *env,
                              size_t frame_size)
{
    unsigned long sp = get_sp_from_cpustate(env);

    if (on_sig_stack(sp) && !likely(on_sig_stack(sp - frame_size))) {
        return -1UL;
    }

    sp = target_sigsp(sp, ka) - frame_size;
    sp &= -16UL;
    return sp;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUArchState *env)
{
    abi_ulong frame_addr;
    struct target_rt_sigframe *frame;
    unsigned long restorer;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    /* Always write at least the signal number for the stack backtracer. */
    if (ka->sa_flags & TARGET_SA_SIGINFO) {
        /* At sigreturn time, restore the callee-save registers too. */
        tswap_siginfo(&frame->info, info);
        /* regs->flags |= PT_FLAGS_RESTORE_REGS; FIXME: we can skip it? */
    } else {
        __put_user(info->si_signo, &frame->info.si_signo);
    }

    /* Create the ucontext.  */
    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    setup_sigcontext(&frame->uc.tuc_mcontext, env, info->si_signo);

    if (ka->sa_flags & TARGET_SA_RESTORER) {
        restorer = (unsigned long) ka->sa_restorer;
    } else {
        __put_user(INSN_MOVELI_R10_139, &frame->retcode[0]);
        __put_user(INSN_SWINT1, &frame->retcode[1]);
        restorer = frame_addr + offsetof(struct target_rt_sigframe, retcode);
    }
    env->pc = (unsigned long) ka->_sa_handler;
    env->regs[TILEGX_R_SP] = (unsigned long) frame;
    env->regs[TILEGX_R_LR] = restorer;
    env->regs[0] = (unsigned long) sig;
    env->regs[1] = (unsigned long) &frame->info;
    env->regs[2] = (unsigned long) &frame->uc;
    /* regs->flags |= PT_FLAGS_CALLER_SAVES; FIXME: we can skip it? */

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    force_sigsegv(sig);
}

long do_rt_sigreturn(CPUTLGState *env)
{
    abi_ulong frame_addr = env->regs[TILEGX_R_SP];
    struct target_rt_sigframe *frame;
    sigset_t set;

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    restore_sigcontext(env, &frame->uc.tuc_mcontext);
    if (do_sigaltstack(frame_addr + offsetof(struct target_rt_sigframe,
                                             uc.tuc_stack),
                       0, env->regs[TILEGX_R_SP]) == -EFAULT) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;


 badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
