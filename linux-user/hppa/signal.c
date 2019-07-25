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
    abi_ulong sc_flags;
    abi_ulong sc_gr[32];
    uint64_t sc_fr[32];
    abi_ulong sc_iasq[2];
    abi_ulong sc_iaoq[2];
    abi_ulong sc_sar;
};

struct target_ucontext {
    abi_uint tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    abi_uint pad[1];
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;
};

struct target_rt_sigframe {
    abi_uint tramp[9];
    target_siginfo_t info;
    struct target_ucontext uc;
    /* hidden location of upper halves of pa2.0 64-bit gregs */
};

static void setup_sigcontext(struct target_sigcontext *sc, CPUArchState *env)
{
    int flags = 0;
    int i;

    /* ??? if on_sig_stack, flags |= 1 (PARISC_SC_FLAG_ONSTACK).  */

    if (env->iaoq_f < TARGET_PAGE_SIZE) {
        /* In the gateway page, executing a syscall.  */
        flags |= 2; /* PARISC_SC_FLAG_IN_SYSCALL */
        __put_user(env->gr[31], &sc->sc_iaoq[0]);
        __put_user(env->gr[31] + 4, &sc->sc_iaoq[1]);
    } else {
        __put_user(env->iaoq_f, &sc->sc_iaoq[0]);
        __put_user(env->iaoq_b, &sc->sc_iaoq[1]);
    }
    __put_user(0, &sc->sc_iasq[0]);
    __put_user(0, &sc->sc_iasq[1]);
    __put_user(flags, &sc->sc_flags);

    __put_user(cpu_hppa_get_psw(env), &sc->sc_gr[0]);
    for (i = 1; i < 32; ++i) {
        __put_user(env->gr[i], &sc->sc_gr[i]);
    }

    __put_user((uint64_t)env->fr0_shadow << 32, &sc->sc_fr[0]);
    for (i = 1; i < 32; ++i) {
        __put_user(env->fr[i], &sc->sc_fr[i]);
    }

    __put_user(env->cr[CR_SAR], &sc->sc_sar);
}

static void restore_sigcontext(CPUArchState *env, struct target_sigcontext *sc)
{
    target_ulong psw;
    int i;

    __get_user(psw, &sc->sc_gr[0]);
    cpu_hppa_put_psw(env, psw);

    for (i = 1; i < 32; ++i) {
        __get_user(env->gr[i], &sc->sc_gr[i]);
    }
    for (i = 0; i < 32; ++i) {
        __get_user(env->fr[i], &sc->sc_fr[i]);
    }
    cpu_hppa_loaded_fr0(env);

    __get_user(env->iaoq_f, &sc->sc_iaoq[0]);
    __get_user(env->iaoq_b, &sc->sc_iaoq[1]);
    __get_user(env->cr[CR_SAR], &sc->sc_sar);
}

/* No, this doesn't look right, but it's copied straight from the kernel.  */
#define PARISC_RT_SIGFRAME_SIZE32 \
    ((sizeof(struct target_rt_sigframe) + 48 + 64) & -64)

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUArchState *env)
{
    abi_ulong frame_addr, sp, haddr;
    struct target_rt_sigframe *frame;
    int i;
    TaskState *ts = (TaskState *)thread_cpu->opaque;

    sp = get_sp_from_cpustate(env);
    if ((ka->sa_flags & TARGET_SA_ONSTACK) && !sas_ss_flags(sp)) {
        sp = (ts->sigaltstack_used.ss_sp + 0x7f) & ~0x3f;
    }
    frame_addr = QEMU_ALIGN_UP(sp, 64);
    sp = frame_addr + PARISC_RT_SIGFRAME_SIZE32;

    trace_user_setup_rt_frame(env, frame_addr);

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    tswap_siginfo(&frame->info, info);
    frame->uc.tuc_flags = 0;
    frame->uc.tuc_link = 0;

    target_save_altstack(&frame->uc.tuc_stack, env);

    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    setup_sigcontext(&frame->uc.tuc_mcontext, env);

    __put_user(0x34190000, frame->tramp + 0); /* ldi 0,%r25 */
    __put_user(0x3414015a, frame->tramp + 1); /* ldi __NR_rt_sigreturn,%r20 */
    __put_user(0xe4008200, frame->tramp + 2); /* be,l 0x100(%sr2,%r0) */
    __put_user(0x08000240, frame->tramp + 3); /* nop */

    unlock_user_struct(frame, frame_addr, 1);

    env->gr[2] = h2g(frame->tramp);
    env->gr[30] = sp;
    env->gr[26] = sig;
    env->gr[25] = h2g(&frame->info);
    env->gr[24] = h2g(&frame->uc);

    haddr = ka->_sa_handler;
    if (haddr & 2) {
        /* Function descriptor.  */
        target_ulong *fdesc, dest;

        haddr &= -4;
        if (!lock_user_struct(VERIFY_READ, fdesc, haddr, 1)) {
            goto give_sigsegv;
        }
        __get_user(dest, fdesc);
        __get_user(env->gr[19], fdesc + 1);
        unlock_user_struct(fdesc, haddr, 1);
        haddr = dest;
    }
    env->iaoq_f = haddr;
    env->iaoq_b = haddr + 4;
    return;

 give_sigsegv:
    force_sigsegv(sig);
}

long do_rt_sigreturn(CPUArchState *env)
{
    abi_ulong frame_addr = env->gr[30] - PARISC_RT_SIGFRAME_SIZE32;
    struct target_rt_sigframe *frame;
    sigset_t set;

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }
    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    restore_sigcontext(env, &frame->uc.tuc_mcontext);
    unlock_user_struct(frame, frame_addr, 0);

    if (do_sigaltstack(frame_addr + offsetof(struct target_rt_sigframe,
                                             uc.tuc_stack),
                       0, env->gr[30]) == -EFAULT) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

 badframe:
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
