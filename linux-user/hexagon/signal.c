/*
 *  Emulation of Linux signals
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
    target_ulong r0,  r1,  r2,  r3;
    target_ulong r4,  r5,  r6,  r7;
    target_ulong r8,  r9, r10, r11;
    target_ulong r12, r13, r14, r15;
    target_ulong r16, r17, r18, r19;
    target_ulong r20, r21, r22, r23;
    target_ulong r24, r25, r26, r27;
    target_ulong r28, r29, r30, r31;
    target_ulong sa0;
    target_ulong lc0;
    target_ulong sa1;
    target_ulong lc1;
    target_ulong m0;
    target_ulong m1;
    target_ulong usr;
    target_ulong p3_0;
    target_ulong gp;
    target_ulong ugp;
    target_ulong pc;
    target_ulong cause;
    target_ulong badva;
    target_ulong pad1;
    target_ulong pad2;
    target_ulong pad3;
};

struct target_ucontext {
    unsigned long uc_flags;
    target_ulong uc_link; /* target pointer */
    target_stack_t uc_stack;
    struct target_sigcontext uc_mcontext;
    target_sigset_t uc_sigmask;
};

struct target_rt_sigframe {
    uint32_t tramp[2];
    struct target_siginfo info;
    struct target_ucontext uc;
};

static abi_ulong get_sigframe(struct target_sigaction *ka,
                              CPUHexagonState *regs, size_t framesize)
{
    abi_ulong sp = get_sp_from_cpustate(regs);

    /* This is the X/Open sanctioned signal stack switching.  */
    sp = target_sigsp(sp, ka) - framesize;

    sp = QEMU_ALIGN_DOWN(sp, 8);

    return sp;
}

static void setup_sigcontext(struct target_sigcontext *sc, CPUHexagonState *env)
{
    __put_user(env->gpr[HEX_REG_R00], &sc->r0);
    __put_user(env->gpr[HEX_REG_R01], &sc->r1);
    __put_user(env->gpr[HEX_REG_R02], &sc->r2);
    __put_user(env->gpr[HEX_REG_R03], &sc->r3);
    __put_user(env->gpr[HEX_REG_R04], &sc->r4);
    __put_user(env->gpr[HEX_REG_R05], &sc->r5);
    __put_user(env->gpr[HEX_REG_R06], &sc->r6);
    __put_user(env->gpr[HEX_REG_R07], &sc->r7);
    __put_user(env->gpr[HEX_REG_R08], &sc->r8);
    __put_user(env->gpr[HEX_REG_R09], &sc->r9);
    __put_user(env->gpr[HEX_REG_R10], &sc->r10);
    __put_user(env->gpr[HEX_REG_R11], &sc->r11);
    __put_user(env->gpr[HEX_REG_R12], &sc->r12);
    __put_user(env->gpr[HEX_REG_R13], &sc->r13);
    __put_user(env->gpr[HEX_REG_R14], &sc->r14);
    __put_user(env->gpr[HEX_REG_R15], &sc->r15);
    __put_user(env->gpr[HEX_REG_R16], &sc->r16);
    __put_user(env->gpr[HEX_REG_R17], &sc->r17);
    __put_user(env->gpr[HEX_REG_R18], &sc->r18);
    __put_user(env->gpr[HEX_REG_R19], &sc->r19);
    __put_user(env->gpr[HEX_REG_R20], &sc->r20);
    __put_user(env->gpr[HEX_REG_R21], &sc->r21);
    __put_user(env->gpr[HEX_REG_R22], &sc->r22);
    __put_user(env->gpr[HEX_REG_R23], &sc->r23);
    __put_user(env->gpr[HEX_REG_R24], &sc->r24);
    __put_user(env->gpr[HEX_REG_R25], &sc->r25);
    __put_user(env->gpr[HEX_REG_R26], &sc->r26);
    __put_user(env->gpr[HEX_REG_R27], &sc->r27);
    __put_user(env->gpr[HEX_REG_R28], &sc->r28);
    __put_user(env->gpr[HEX_REG_R29], &sc->r29);
    __put_user(env->gpr[HEX_REG_R30], &sc->r30);
    __put_user(env->gpr[HEX_REG_R31], &sc->r31);
    __put_user(env->gpr[HEX_REG_SA0], &sc->sa0);
    __put_user(env->gpr[HEX_REG_LC0], &sc->lc0);
    __put_user(env->gpr[HEX_REG_SA1], &sc->sa1);
    __put_user(env->gpr[HEX_REG_LC1], &sc->lc1);
    __put_user(env->gpr[HEX_REG_M0], &sc->m0);
    __put_user(env->gpr[HEX_REG_M1], &sc->m1);
    __put_user(env->gpr[HEX_REG_USR], &sc->usr);
    __put_user(env->gpr[HEX_REG_P3_0], &sc->p3_0);
    __put_user(env->gpr[HEX_REG_GP], &sc->gp);
    __put_user(env->gpr[HEX_REG_UGP], &sc->ugp);
    __put_user(env->gpr[HEX_REG_PC], &sc->pc);
}

static void setup_ucontext(struct target_ucontext *uc,
                           CPUHexagonState *env, target_sigset_t *set)
{
    __put_user(0,    &(uc->uc_flags));
    __put_user(0,    &(uc->uc_link));

    target_save_altstack(&uc->uc_stack, env);

    int i;
    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &(uc->uc_sigmask.sig[i]));
    }

    setup_sigcontext(&uc->uc_mcontext, env);
}

static inline void install_sigtramp(uint32_t *tramp)
{
    __put_user(0x7800d166, tramp + 0); /*  { r6=#__NR_rt_sigreturn } */
    __put_user(0x5400c004, tramp + 1); /*  { trap0(#1) } */
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUHexagonState *env)
{
    abi_ulong frame_addr;
    struct target_rt_sigframe *frame;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    trace_user_setup_rt_frame(env, frame_addr);

    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto badframe;
    }

    setup_ucontext(&frame->uc, env, set);
    tswap_siginfo(&frame->info, info);
    /*
     * The on-stack signal trampoline is no longer executed;
     * however, the libgcc signal frame unwinding code checks
     * for the presence of these two numeric magic values.
     */
    install_sigtramp(frame->tramp);

    env->gpr[HEX_REG_PC] = ka->_sa_handler;
    env->gpr[HEX_REG_SP] = frame_addr;
    env->gpr[HEX_REG_R00] = sig;
    env->gpr[HEX_REG_R01] =
        frame_addr + offsetof(struct target_rt_sigframe, info);
    env->gpr[HEX_REG_R02] =
        frame_addr + offsetof(struct target_rt_sigframe, uc);
    env->gpr[HEX_REG_LR] = default_rt_sigreturn;

    return;

badframe:
    unlock_user_struct(frame, frame_addr, 1);
    if (sig == TARGET_SIGSEGV) {
        ka->_sa_handler = TARGET_SIG_DFL;
    }
    force_sig(TARGET_SIGSEGV);
}

static void restore_sigcontext(CPUHexagonState *env,
                               struct target_sigcontext *sc)
{
    __get_user(env->gpr[HEX_REG_R00], &sc->r0);
    __get_user(env->gpr[HEX_REG_R01], &sc->r1);
    __get_user(env->gpr[HEX_REG_R02], &sc->r2);
    __get_user(env->gpr[HEX_REG_R03], &sc->r3);
    __get_user(env->gpr[HEX_REG_R04], &sc->r4);
    __get_user(env->gpr[HEX_REG_R05], &sc->r5);
    __get_user(env->gpr[HEX_REG_R06], &sc->r6);
    __get_user(env->gpr[HEX_REG_R07], &sc->r7);
    __get_user(env->gpr[HEX_REG_R08], &sc->r8);
    __get_user(env->gpr[HEX_REG_R09], &sc->r9);
    __get_user(env->gpr[HEX_REG_R10], &sc->r10);
    __get_user(env->gpr[HEX_REG_R11], &sc->r11);
    __get_user(env->gpr[HEX_REG_R12], &sc->r12);
    __get_user(env->gpr[HEX_REG_R13], &sc->r13);
    __get_user(env->gpr[HEX_REG_R14], &sc->r14);
    __get_user(env->gpr[HEX_REG_R15], &sc->r15);
    __get_user(env->gpr[HEX_REG_R16], &sc->r16);
    __get_user(env->gpr[HEX_REG_R17], &sc->r17);
    __get_user(env->gpr[HEX_REG_R18], &sc->r18);
    __get_user(env->gpr[HEX_REG_R19], &sc->r19);
    __get_user(env->gpr[HEX_REG_R20], &sc->r20);
    __get_user(env->gpr[HEX_REG_R21], &sc->r21);
    __get_user(env->gpr[HEX_REG_R22], &sc->r22);
    __get_user(env->gpr[HEX_REG_R23], &sc->r23);
    __get_user(env->gpr[HEX_REG_R24], &sc->r24);
    __get_user(env->gpr[HEX_REG_R25], &sc->r25);
    __get_user(env->gpr[HEX_REG_R26], &sc->r26);
    __get_user(env->gpr[HEX_REG_R27], &sc->r27);
    __get_user(env->gpr[HEX_REG_R28], &sc->r28);
    __get_user(env->gpr[HEX_REG_R29], &sc->r29);
    __get_user(env->gpr[HEX_REG_R30], &sc->r30);
    __get_user(env->gpr[HEX_REG_R31], &sc->r31);
    __get_user(env->gpr[HEX_REG_SA0], &sc->sa0);
    __get_user(env->gpr[HEX_REG_LC0], &sc->lc0);
    __get_user(env->gpr[HEX_REG_SA1], &sc->sa1);
    __get_user(env->gpr[HEX_REG_LC1], &sc->lc1);
    __get_user(env->gpr[HEX_REG_M0], &sc->m0);
    __get_user(env->gpr[HEX_REG_M1], &sc->m1);
    __get_user(env->gpr[HEX_REG_USR], &sc->usr);
    __get_user(env->gpr[HEX_REG_P3_0], &sc->p3_0);
    __get_user(env->gpr[HEX_REG_GP], &sc->gp);
    __get_user(env->gpr[HEX_REG_UGP], &sc->ugp);
    __get_user(env->gpr[HEX_REG_PC], &sc->pc);
}

static void restore_ucontext(CPUHexagonState *env, struct target_ucontext *uc)
{
    sigset_t blocked;
    target_sigset_t target_set;
    int i;

    target_sigemptyset(&target_set);
    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &(uc->uc_sigmask.sig[i]));
    }

    target_to_host_sigset_internal(&blocked, &target_set);
    set_sigmask(&blocked);

    restore_sigcontext(env, &uc->uc_mcontext);
}

long do_rt_sigreturn(CPUHexagonState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;

    frame_addr = env->gpr[HEX_REG_SP];
    trace_user_do_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    restore_ucontext(env, &frame->uc);
    target_restore_altstack(&frame->uc.uc_stack, env);

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}

void setup_sigtramp(abi_ulong sigtramp_page)
{
    uint32_t *tramp = lock_user(VERIFY_WRITE, sigtramp_page, 4 * 2, 0);
    assert(tramp != NULL);

    default_rt_sigreturn = sigtramp_page;
    install_sigtramp(tramp);

    unlock_user(tramp, sigtramp_page, 4 * 2);
}
