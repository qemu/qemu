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
    abi_ulong sc_pc;
    abi_ulong sc_ps;
    abi_ulong sc_lbeg;
    abi_ulong sc_lend;
    abi_ulong sc_lcount;
    abi_ulong sc_sar;
    abi_ulong sc_acclo;
    abi_ulong sc_acchi;
    abi_ulong sc_a[16];
    abi_ulong sc_xtregs;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;
};

struct target_rt_sigframe {
    target_siginfo_t info;
    struct target_ucontext uc;
    /* TODO: xtregs */
    uint8_t retcode[6];
    abi_ulong window[4];
};

static abi_ulong get_sigframe(struct target_sigaction *sa,
                              CPUXtensaState *env,
                              unsigned long framesize)
{
    abi_ulong sp;

    sp = target_sigsp(get_sp_from_cpustate(env), sa);

    return (sp - framesize) & -16;
}

static int flush_window_regs(CPUXtensaState *env)
{
    uint32_t wb = env->sregs[WINDOW_BASE];
    uint32_t ws = xtensa_replicate_windowstart(env) >> (wb + 1);
    unsigned d = ctz32(ws) + 1;
    unsigned i;
    int ret = 0;

    for (i = d; i < env->config->nareg / 4; i += d) {
        uint32_t ssp, osp;
        unsigned j;

        ws >>= d;
        xtensa_rotate_window(env, d);

        if (ws & 0x1) {
            ssp = env->regs[5];
            d = 1;
        } else if (ws & 0x2) {
            ssp = env->regs[9];
            ret |= get_user_ual(osp, env->regs[1] - 12);
            osp -= 32;
            d = 2;
        } else if (ws & 0x4) {
            ssp = env->regs[13];
            ret |= get_user_ual(osp, env->regs[1] - 12);
            osp -= 48;
            d = 3;
        } else {
            g_assert_not_reached();
        }

        for (j = 0; j < 4; ++j) {
            ret |= put_user_ual(env->regs[j], ssp - 16 + j * 4);
        }
        for (j = 4; j < d * 4; ++j) {
            ret |= put_user_ual(env->regs[j], osp - 16 + j * 4);
        }
    }
    xtensa_rotate_window(env, d);
    g_assert(env->sregs[WINDOW_BASE] == wb);
    return ret == 0;
}

static int setup_sigcontext(struct target_rt_sigframe *frame,
                            CPUXtensaState *env)
{
    struct target_sigcontext *sc = &frame->uc.tuc_mcontext;
    int i;

    __put_user(env->pc, &sc->sc_pc);
    __put_user(env->sregs[PS], &sc->sc_ps);
    __put_user(env->sregs[LBEG], &sc->sc_lbeg);
    __put_user(env->sregs[LEND], &sc->sc_lend);
    __put_user(env->sregs[LCOUNT], &sc->sc_lcount);
    if (!flush_window_regs(env)) {
        return 0;
    }
    for (i = 0; i < 16; ++i) {
        __put_user(env->regs[i], sc->sc_a + i);
    }
    __put_user(0, &sc->sc_xtregs);
    /* TODO: xtregs */
    return 1;
}

static void install_sigtramp(uint8_t *tramp)
{
#if TARGET_BIG_ENDIAN
    /* Generate instruction:  MOVI a2, __NR_rt_sigreturn */
    __put_user(0x22, &tramp[0]);
    __put_user(0x0a, &tramp[1]);
    __put_user(TARGET_NR_rt_sigreturn, &tramp[2]);
    /* Generate instruction:  SYSCALL */
    __put_user(0x00, &tramp[3]);
    __put_user(0x05, &tramp[4]);
    __put_user(0x00, &tramp[5]);
#else
    /* Generate instruction:  MOVI a2, __NR_rt_sigreturn */
    __put_user(0x22, &tramp[0]);
    __put_user(0xa0, &tramp[1]);
    __put_user(TARGET_NR_rt_sigreturn, &tramp[2]);
    /* Generate instruction:  SYSCALL */
    __put_user(0x00, &tramp[3]);
    __put_user(0x50, &tramp[4]);
    __put_user(0x00, &tramp[5]);
#endif
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUXtensaState *env)
{
    abi_ulong frame_addr;
    struct target_rt_sigframe *frame;
    int is_fdpic = info_is_fdpic(get_task_state(thread_cpu)->info);
    abi_ulong handler = 0;
    abi_ulong handler_fdpic_GOT = 0;
    uint32_t ra;
    bool abi_call0;
    unsigned base;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);

    if (is_fdpic) {
        abi_ulong funcdesc_ptr = ka->_sa_handler;

        if (get_user_ual(handler, funcdesc_ptr)
            || get_user_ual(handler_fdpic_GOT, funcdesc_ptr + 4)) {
            goto give_sigsegv;
        }
    } else {
        handler = ka->_sa_handler;
    }

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    if (ka->sa_flags & SA_SIGINFO) {
        frame->info = *info;
    }

    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    if (!setup_sigcontext(frame, env)) {
        unlock_user_struct(frame, frame_addr, 0);
        goto give_sigsegv;
    }
    for (i = 0; i < TARGET_NSIG_WORDS; ++i) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    if (ka->sa_flags & TARGET_SA_RESTORER) {
        if (is_fdpic) {
            if (get_user_ual(ra, ka->sa_restorer)) {
                unlock_user_struct(frame, frame_addr, 0);
                goto give_sigsegv;
            }
        } else {
            ra = ka->sa_restorer;
        }
    } else {
        /* Not used, but retain for ABI compatibility. */
        install_sigtramp(frame->retcode);
        ra = default_rt_sigreturn;
    }
    memset(env->regs, 0, sizeof(env->regs));
    env->pc = handler;
    env->regs[1] = frame_addr;
    env->sregs[WINDOW_BASE] = 0;
    env->sregs[WINDOW_START] = 1;

    abi_call0 = (env->sregs[PS] & PS_WOE) == 0;
    env->sregs[PS] = PS_UM | (3 << PS_RING_SHIFT);

    if (abi_call0) {
        base = 0;
        env->regs[base] = ra;
    } else {
        env->sregs[PS] |= PS_WOE | (1 << PS_CALLINC_SHIFT);
        base = 4;
        env->regs[base] = (ra & 0x3fffffff) | 0x40000000;
    }
    env->regs[base + 2] = sig;
    env->regs[base + 3] = frame_addr + offsetof(struct target_rt_sigframe,
                                                info);
    env->regs[base + 4] = frame_addr + offsetof(struct target_rt_sigframe, uc);
    if (is_fdpic) {
        env->regs[base + 11] = handler_fdpic_GOT;
    }
    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    force_sigsegv(sig);
}

static void restore_sigcontext(CPUXtensaState *env,
                               struct target_rt_sigframe *frame)
{
    struct target_sigcontext *sc = &frame->uc.tuc_mcontext;
    uint32_t ps;
    int i;

    __get_user(env->pc, &sc->sc_pc);
    __get_user(ps, &sc->sc_ps);
    __get_user(env->sregs[LBEG], &sc->sc_lbeg);
    __get_user(env->sregs[LEND], &sc->sc_lend);
    __get_user(env->sregs[LCOUNT], &sc->sc_lcount);

    env->sregs[WINDOW_BASE] = 0;
    env->sregs[WINDOW_START] = 1;
    env->sregs[PS] = deposit32(env->sregs[PS],
                               PS_CALLINC_SHIFT,
                               PS_CALLINC_LEN,
                               extract32(ps, PS_CALLINC_SHIFT,
                                         PS_CALLINC_LEN));
    for (i = 0; i < 16; ++i) {
        __get_user(env->regs[i], sc->sc_a + i);
    }
    /* TODO: xtregs */
}

long do_rt_sigreturn(CPUXtensaState *env)
{
    abi_ulong frame_addr = env->regs[1];
    struct target_rt_sigframe *frame;
    sigset_t set;

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    restore_sigcontext(env, frame);
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
    uint8_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 6, 0);
    assert(tramp != NULL);

    default_rt_sigreturn = sigtramp_page;
    install_sigtramp(tramp);
    unlock_user(tramp, sigtramp_page, 6);
}
