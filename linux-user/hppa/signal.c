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
#include "vdso-asmoffset.h"

struct target_sigcontext {
    abi_ulong sc_flags;
    abi_ulong sc_gr[32];
    abi_ullong sc_fr[32];
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
    abi_uint tramp[2];  /* syscall restart return address */
    target_siginfo_t info;
    struct target_ucontext uc;
    /* hidden location of upper halves of pa2.0 64-bit gregs */
};

QEMU_BUILD_BUG_ON(sizeof(struct target_rt_sigframe) != sizeof_rt_sigframe);
QEMU_BUILD_BUG_ON(offsetof(struct target_rt_sigframe, uc.tuc_mcontext)
                  != offsetof_sigcontext);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_gr)
                  != offsetof_sigcontext_gr);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_fr)
                  != offsetof_sigcontext_fr);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_iaoq)
                  != offsetof_sigcontext_iaoq);
QEMU_BUILD_BUG_ON(offsetof(struct target_sigcontext, sc_sar)
                  != offsetof_sigcontext_sar);


static void setup_sigcontext(struct target_sigcontext *sc, CPUArchState *env)
{
    int i;

    __put_user(env->iaoq_f, &sc->sc_iaoq[0]);
    __put_user(env->iaoq_b, &sc->sc_iaoq[1]);
    __put_user(0, &sc->sc_iasq[0]);
    __put_user(0, &sc->sc_iasq[1]);
    __put_user(0, &sc->sc_flags);

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
    abi_ulong psw;
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

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUArchState *env)
{
    abi_ulong frame_addr, sp, haddr;
    struct target_rt_sigframe *frame;
    int i;
    TaskState *ts = get_task_state(thread_cpu);

    sp = get_sp_from_cpustate(env);
    if ((ka->sa_flags & TARGET_SA_ONSTACK) && !sas_ss_flags(sp)) {
        sp = (ts->sigaltstack_used.ss_sp + 0x7f) & ~0x3f;
    }
    frame_addr = QEMU_ALIGN_UP(sp, SIGFRAME);
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

    unlock_user_struct(frame, frame_addr, 1);

    env->gr[2] = default_rt_sigreturn;
    env->gr[30] = sp;
    env->gr[26] = sig;
    env->gr[25] = h2g(&frame->info);
    env->gr[24] = h2g(&frame->uc);

    haddr = ka->_sa_handler;
    if (haddr & 2) {
        /* Function descriptor.  */
        abi_ptr *fdesc, dest;

        haddr &= -4;
        fdesc = lock_user(VERIFY_READ, haddr, 2 * sizeof(abi_ptr), 1);
        if (!fdesc) {
            goto give_sigsegv;
        }
        __get_user(dest, fdesc);
        __get_user(env->gr[19], fdesc + 1);
        unlock_user(fdesc, haddr, 0);
        haddr = dest;
    }
    env->iaoq_f = haddr;
    env->iaoq_b = haddr + 4;
    env->psw_n = 0;
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
    target_restore_altstack(&frame->uc.tuc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return -QEMU_ESIGRETURN;

 badframe:
    force_sig(TARGET_SIGSEGV);
    return -QEMU_ESIGRETURN;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 6*4, 0);
    abi_ulong SIGFRAME_CONTEXT_REGS32;
    assert(tramp != NULL);

    SIGFRAME_CONTEXT_REGS32 = offsetof(struct target_rt_sigframe, uc.tuc_mcontext);
    SIGFRAME_CONTEXT_REGS32 -= PARISC_RT_SIGFRAME_SIZE32;

    __put_user(SIGFRAME_CONTEXT_REGS32, tramp + 0);
    __put_user(0x08000240, tramp + 1);  /* nop - b/c dwarf2 unwind routines */
    __put_user(0x34190000, tramp + 2);  /* ldi 0, %r25 (in_syscall=0) */
    __put_user(0x3414015a, tramp + 3);  /* ldi __NR_rt_sigreturn, %r20 */
    __put_user(0xe4008200, tramp + 4);  /* ble 0x100(%sr2, %r0) */
    __put_user(0x08000240, tramp + 5);  /* nop */

    default_rt_sigreturn = (sigtramp_page + 8) | 3;
    unlock_user(tramp, sigtramp_page, 6*4);
}
