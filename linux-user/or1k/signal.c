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
#include "target_ptrace.h"

typedef struct target_sigcontext {
    struct target_user_regs_struct regs;
    abi_ulong oldmask;
} target_sigcontext;

typedef struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;   /* mask last for extensibility */
} target_ucontext;

typedef struct target_rt_sigframe {
    struct target_siginfo info;
    target_ucontext uc;
} target_rt_sigframe;

static void restore_sigcontext(CPUOpenRISCState *env, target_sigcontext *sc)
{
    int i;
    abi_ulong v;

    for (i = 0; i < 32; ++i) {
        __get_user(v, &sc->regs.gpr[i]);
        cpu_set_gpr(env, i, v);
    }
    __get_user(env->pc, &sc->regs.pc);

    /* Make sure the supervisor flag is clear.  */
    __get_user(v, &sc->regs.sr);
    cpu_set_sr(env, v & ~SR_SM);
}

/* Set up a signal frame.  */

static void setup_sigcontext(target_sigcontext *sc, CPUOpenRISCState *env)
{
    int i;

    for (i = 0; i < 32; ++i) {
        __put_user(cpu_get_gpr(env, i), &sc->regs.gpr[i]);
    }

    __put_user(env->pc, &sc->regs.pc);
    __put_user(cpu_get_sr(env), &sc->regs.sr);
}

static inline abi_ulong get_sigframe(struct target_sigaction *ka,
                                     CPUOpenRISCState *env,
                                     size_t frame_size)
{
    target_ulong sp = get_sp_from_cpustate(env);

    /* Honor redzone now.  If we swap to signal stack, no need to waste
     * the 128 bytes by subtracting afterward.
     */
    sp -= 128;

    sp = target_sigsp(sp, ka);
    sp -= frame_size;
    sp = QEMU_ALIGN_DOWN(sp, 4);

    return sp;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUOpenRISCState *env)
{
    abi_ulong frame_addr;
    target_rt_sigframe *frame;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    if (ka->sa_flags & SA_SIGINFO) {
        frame->info = *info;
    }

    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);

    target_save_altstack(&frame->uc.tuc_stack, env);
    setup_sigcontext(&frame->uc.tuc_mcontext, env);
    for (i = 0; i < TARGET_NSIG_WORDS; ++i) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    /* Set up registers for signal handler */
    cpu_set_gpr(env, 9, default_rt_sigreturn);
    cpu_set_gpr(env, 3, sig);
    cpu_set_gpr(env, 4, frame_addr + offsetof(target_rt_sigframe, info));
    cpu_set_gpr(env, 5, frame_addr + offsetof(target_rt_sigframe, uc));
    cpu_set_gpr(env, 1, frame_addr);

    /* For debugging convenience, set ppc to the insn that faulted.  */
    env->ppc = env->pc;
    /* When setting the PC for the signal handler, exit delay slot.  */
    env->pc = ka->_sa_handler;
    env->dflag = 0;
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(sig);
}

long do_rt_sigreturn(CPUOpenRISCState *env)
{
    abi_ulong frame_addr = get_sp_from_cpustate(env);
    target_rt_sigframe *frame;
    sigset_t set;

    trace_user_do_rt_sigreturn(env, 0);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    if (frame_addr & 3) {
        goto badframe;
    }

    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    restore_sigcontext(env, &frame->uc.tuc_mcontext);
    target_restore_altstack(&frame->uc.tuc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return cpu_get_gpr(env, 11);

 badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 8, 0);
    assert(tramp != NULL);

    /* This is l.ori r11,r0,__NR_sigreturn; l.sys 1 */
    __put_user(0xa9600000 | TARGET_NR_rt_sigreturn, tramp + 0);
    __put_user(0x20000001, tramp + 1);

    default_rt_sigreturn = sigtramp_page;
    unlock_user(tramp, sigtramp_page, 8);
}
