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

/* Signal handler invocation must be transparent for the code being
   interrupted. Complete CPU (hart) state is saved on entry and restored
   before returning from the handler. Process sigmask is also saved to block
   signals while the handler is running. The handler gets its own stack,
   which also doubles as storage for the CPU state and sigmask.

   The code below is qemu re-implementation of arch/riscv/kernel/signal.c */

struct target_sigcontext {
    abi_long pc;
    abi_long gpr[31]; /* x0 is not present, so all offsets must be -1 */
    uint64_t fpr[32];
    uint32_t fcsr;
}; /* cf. riscv-linux:arch/riscv/include/uapi/asm/ptrace.h */

struct target_ucontext {
    unsigned long uc_flags;
    struct target_ucontext *uc_link;
    target_stack_t uc_stack;
    target_sigset_t uc_sigmask;
    uint8_t   __unused[1024 / 8 - sizeof(target_sigset_t)];
    struct target_sigcontext uc_mcontext QEMU_ALIGNED(16);
};

struct target_rt_sigframe {
    uint32_t tramp[2]; /* not in kernel, which uses VDSO instead */
    struct target_siginfo info;
    struct target_ucontext uc;
};

static abi_ulong get_sigframe(struct target_sigaction *ka,
                              CPURISCVState *regs, size_t framesize)
{
    abi_ulong sp = get_sp_from_cpustate(regs);

    /* If we are on the alternate signal stack and would overflow it, don't.
       Return an always-bogus address instead so we will die with SIGSEGV. */
    if (on_sig_stack(sp) && !likely(on_sig_stack(sp - framesize))) {
        return -1L;
    }

    /* This is the X/Open sanctioned signal stack switching.  */
    sp = target_sigsp(sp, ka) - framesize;

    /* XXX: kernel aligns with 0xf ? */
    sp &= ~3UL; /* align sp on 4-byte boundary */

    return sp;
}

static void setup_sigcontext(struct target_sigcontext *sc, CPURISCVState *env)
{
    int i;

    __put_user(env->pc, &sc->pc);

    for (i = 1; i < 32; i++) {
        __put_user(env->gpr[i], &sc->gpr[i - 1]);
    }
    for (i = 0; i < 32; i++) {
        __put_user(env->fpr[i], &sc->fpr[i]);
    }

    uint32_t fcsr = riscv_csr_read(env, CSR_FCSR);
    __put_user(fcsr, &sc->fcsr);
}

static void setup_ucontext(struct target_ucontext *uc,
                           CPURISCVState *env, target_sigset_t *set)
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
    __put_user(0x08b00893, tramp + 0);  /* li a7, 139 = __NR_rt_sigreturn */
    __put_user(0x00000073, tramp + 1);  /* ecall */
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPURISCVState *env)
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
    install_sigtramp(frame->tramp);

    env->pc = ka->_sa_handler;
    env->gpr[xSP] = frame_addr;
    env->gpr[xA0] = sig;
    env->gpr[xA1] = frame_addr + offsetof(struct target_rt_sigframe, info);
    env->gpr[xA2] = frame_addr + offsetof(struct target_rt_sigframe, uc);
    env->gpr[xRA] = frame_addr + offsetof(struct target_rt_sigframe, tramp);

    return;

badframe:
    unlock_user_struct(frame, frame_addr, 1);
    if (sig == TARGET_SIGSEGV) {
        ka->_sa_handler = TARGET_SIG_DFL;
    }
    force_sig(TARGET_SIGSEGV);
}

static void restore_sigcontext(CPURISCVState *env, struct target_sigcontext *sc)
{
    int i;

    __get_user(env->pc, &sc->pc);

    for (i = 1; i < 32; ++i) {
        __get_user(env->gpr[i], &sc->gpr[i - 1]);
    }
    for (i = 0; i < 32; ++i) {
        __get_user(env->fpr[i], &sc->fpr[i]);
    }

    uint32_t fcsr;
    __get_user(fcsr, &sc->fcsr);
    riscv_csr_write(env, CSR_FCSR, fcsr);
}

static void restore_ucontext(CPURISCVState *env, struct target_ucontext *uc)
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

long do_rt_sigreturn(CPURISCVState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;

    frame_addr = env->gpr[xSP];
    trace_user_do_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    restore_ucontext(env, &frame->uc);

    if (do_sigaltstack(frame_addr + offsetof(struct target_rt_sigframe,
            uc.uc_stack), 0, get_sp_from_cpustate(env)) == -EFAULT) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}
