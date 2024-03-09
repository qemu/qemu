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
    abi_long sc_onstack;
    abi_long sc_mask;
    abi_long sc_pc;
    abi_long sc_ps;
    abi_long sc_regs[32];
    abi_long sc_ownedfp;
    abi_long sc_fpregs[32];
    abi_ulong sc_fpcr;
    abi_ulong sc_fp_control;
    abi_ulong sc_reserved1;
    abi_ulong sc_reserved2;
    abi_ulong sc_ssize;
    abi_ulong sc_sbase;
    abi_ulong sc_traparg_a0;
    abi_ulong sc_traparg_a1;
    abi_ulong sc_traparg_a2;
    abi_ulong sc_fp_trap_pc;
    abi_ulong sc_fp_trigger_sum;
    abi_ulong sc_fp_trigger_inst;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    abi_ulong tuc_osf_sigmask;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;
};

struct target_sigframe {
    struct target_sigcontext sc;
};

struct target_rt_sigframe {
    target_siginfo_t info;
    struct target_ucontext uc;
};

#define INSN_MOV_R30_R16        0x47fe0410
#define INSN_LDI_R0             0x201f0000
#define INSN_CALLSYS            0x00000083

static void setup_sigcontext(struct target_sigcontext *sc, CPUAlphaState *env,
                             abi_ulong frame_addr, target_sigset_t *set)
{
    int i;

    __put_user(on_sig_stack(frame_addr), &sc->sc_onstack);
    __put_user(set->sig[0], &sc->sc_mask);
    __put_user(env->pc, &sc->sc_pc);
    __put_user(8, &sc->sc_ps);

    for (i = 0; i < 31; ++i) {
        __put_user(env->ir[i], &sc->sc_regs[i]);
    }
    __put_user(0, &sc->sc_regs[31]);

    for (i = 0; i < 31; ++i) {
        __put_user(env->fir[i], &sc->sc_fpregs[i]);
    }
    __put_user(0, &sc->sc_fpregs[31]);
    __put_user(cpu_alpha_load_fpcr(env), &sc->sc_fpcr);

    __put_user(0, &sc->sc_traparg_a0); /* FIXME */
    __put_user(0, &sc->sc_traparg_a1); /* FIXME */
    __put_user(0, &sc->sc_traparg_a2); /* FIXME */
}

static void restore_sigcontext(CPUAlphaState *env,
                               struct target_sigcontext *sc)
{
    uint64_t fpcr;
    int i;

    __get_user(env->pc, &sc->sc_pc);

    for (i = 0; i < 31; ++i) {
        __get_user(env->ir[i], &sc->sc_regs[i]);
    }
    for (i = 0; i < 31; ++i) {
        __get_user(env->fir[i], &sc->sc_fpregs[i]);
    }

    __get_user(fpcr, &sc->sc_fpcr);
    cpu_alpha_store_fpcr(env, fpcr);
}

static inline abi_ulong get_sigframe(struct target_sigaction *sa,
                                     CPUAlphaState *env,
                                     unsigned long framesize)
{
    abi_ulong sp;

    sp = target_sigsp(get_sp_from_cpustate(env), sa);

    return (sp - framesize) & -32;
}

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUAlphaState *env)
{
    abi_ulong frame_addr, r26;
    struct target_sigframe *frame;
    int err = 0;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    setup_sigcontext(&frame->sc, env, frame_addr, set);

    if (ka->ka_restorer) {
        r26 = ka->ka_restorer;
    } else {
        r26 = default_sigreturn;
    }

    unlock_user_struct(frame, frame_addr, 1);

    if (err) {
give_sigsegv:
        force_sigsegv(sig);
        return;
    }

    env->ir[IR_RA] = r26;
    env->ir[IR_PV] = env->pc = ka->_sa_handler;
    env->ir[IR_A0] = sig;
    env->ir[IR_A1] = 0;
    env->ir[IR_A2] = frame_addr + offsetof(struct target_sigframe, sc);
    env->ir[IR_SP] = frame_addr;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUAlphaState *env)
{
    abi_ulong frame_addr, r26;
    struct target_rt_sigframe *frame;
    int i, err = 0;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    frame->info = *info;

    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    __put_user(set->sig[0], &frame->uc.tuc_osf_sigmask);

    target_save_altstack(&frame->uc.tuc_stack, env);

    setup_sigcontext(&frame->uc.tuc_mcontext, env, frame_addr, set);
    for (i = 0; i < TARGET_NSIG_WORDS; ++i) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    if (ka->ka_restorer) {
        r26 = ka->ka_restorer;
    } else {
        r26 = default_rt_sigreturn;
    }

    if (err) {
give_sigsegv:
        force_sigsegv(sig);
        return;
    }

    env->ir[IR_RA] = r26;
    env->ir[IR_PV] = env->pc = ka->_sa_handler;
    env->ir[IR_A0] = sig;
    env->ir[IR_A1] = frame_addr + offsetof(struct target_rt_sigframe, info);
    env->ir[IR_A2] = frame_addr + offsetof(struct target_rt_sigframe, uc);
    env->ir[IR_SP] = frame_addr;
}

long do_sigreturn(CPUAlphaState *env)
{
    struct target_sigcontext *sc;
    abi_ulong sc_addr = env->ir[IR_A0];
    target_sigset_t target_set;
    sigset_t set;

    if (!lock_user_struct(VERIFY_READ, sc, sc_addr, 1)) {
        goto badframe;
    }

    target_sigemptyset(&target_set);
    __get_user(target_set.sig[0], &sc->sc_mask);

    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set);

    restore_sigcontext(env, sc);
    unlock_user_struct(sc, sc_addr, 0);
    return -QEMU_ESIGRETURN;

badframe:
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUAlphaState *env)
{
    abi_ulong frame_addr = env->ir[IR_A0];
    struct target_rt_sigframe *frame;
    sigset_t set;

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    restore_sigcontext(env, &frame->uc.tuc_mcontext);
    target_restore_altstack(&frame->uc.tuc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;


badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 6 * 4, 0);
    assert(tramp != NULL);

    default_sigreturn = sigtramp_page;
    __put_user(INSN_MOV_R30_R16, &tramp[0]);
    __put_user(INSN_LDI_R0 + TARGET_NR_sigreturn, &tramp[1]);
    __put_user(INSN_CALLSYS, &tramp[2]);

    default_rt_sigreturn = sigtramp_page + 3 * 4;
    __put_user(INSN_MOV_R30_R16, &tramp[3]);
    __put_user(INSN_LDI_R0 + TARGET_NR_rt_sigreturn, &tramp[4]);
    __put_user(INSN_CALLSYS, &tramp[5]);

    unlock_user(tramp, sigtramp_page, 6 * 4);
}
