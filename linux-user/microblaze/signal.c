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
    struct target_pt_regs regs;  /* needs to be first */
    uint32_t oldmask;
};

struct target_stack_t {
    abi_ulong ss_sp;
    int ss_flags;
    unsigned int ss_size;
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    struct target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    uint32_t tuc_extramask[TARGET_NSIG_WORDS - 1];
};

/* Signal frames. */
struct target_signal_frame {
    struct target_ucontext uc;
    uint32_t extramask[TARGET_NSIG_WORDS - 1];
    uint32_t tramp[2];
};

struct rt_signal_frame {
    siginfo_t info;
    ucontext_t uc;
    uint32_t tramp[2];
};

static void setup_sigcontext(struct target_sigcontext *sc, CPUMBState *env)
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
    __put_user(env->regs[14], &sc->regs.r14);
    __put_user(env->regs[15], &sc->regs.r15);
    __put_user(env->regs[16], &sc->regs.r16);
    __put_user(env->regs[17], &sc->regs.r17);
    __put_user(env->regs[18], &sc->regs.r18);
    __put_user(env->regs[19], &sc->regs.r19);
    __put_user(env->regs[20], &sc->regs.r20);
    __put_user(env->regs[21], &sc->regs.r21);
    __put_user(env->regs[22], &sc->regs.r22);
    __put_user(env->regs[23], &sc->regs.r23);
    __put_user(env->regs[24], &sc->regs.r24);
    __put_user(env->regs[25], &sc->regs.r25);
    __put_user(env->regs[26], &sc->regs.r26);
    __put_user(env->regs[27], &sc->regs.r27);
    __put_user(env->regs[28], &sc->regs.r28);
    __put_user(env->regs[29], &sc->regs.r29);
    __put_user(env->regs[30], &sc->regs.r30);
    __put_user(env->regs[31], &sc->regs.r31);
    __put_user(env->sregs[SR_PC], &sc->regs.pc);
}

static void restore_sigcontext(struct target_sigcontext *sc, CPUMBState *env)
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
    __get_user(env->regs[14], &sc->regs.r14);
    __get_user(env->regs[15], &sc->regs.r15);
    __get_user(env->regs[16], &sc->regs.r16);
    __get_user(env->regs[17], &sc->regs.r17);
    __get_user(env->regs[18], &sc->regs.r18);
    __get_user(env->regs[19], &sc->regs.r19);
    __get_user(env->regs[20], &sc->regs.r20);
    __get_user(env->regs[21], &sc->regs.r21);
    __get_user(env->regs[22], &sc->regs.r22);
    __get_user(env->regs[23], &sc->regs.r23);
    __get_user(env->regs[24], &sc->regs.r24);
    __get_user(env->regs[25], &sc->regs.r25);
    __get_user(env->regs[26], &sc->regs.r26);
    __get_user(env->regs[27], &sc->regs.r27);
    __get_user(env->regs[28], &sc->regs.r28);
    __get_user(env->regs[29], &sc->regs.r29);
    __get_user(env->regs[30], &sc->regs.r30);
    __get_user(env->regs[31], &sc->regs.r31);
    __get_user(env->sregs[SR_PC], &sc->regs.pc);
}

static abi_ulong get_sigframe(struct target_sigaction *ka,
                              CPUMBState *env, int frame_size)
{
    abi_ulong sp = env->regs[1];

    sp = target_sigsp(sp, ka);

    return ((sp - frame_size) & -8UL);
}

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUMBState *env)
{
    struct target_signal_frame *frame;
    abi_ulong frame_addr;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    trace_user_setup_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0))
        goto badframe;

    /* Save the mask.  */
    __put_user(set->sig[0], &frame->uc.tuc_mcontext.oldmask);

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->extramask[i - 1]);
    }

    setup_sigcontext(&frame->uc.tuc_mcontext, env);

    /* Set up to return from userspace. If provided, use a stub
       already in userspace. */
    /* minus 8 is offset to cater for "rtsd r15,8" offset */
    if (ka->sa_flags & TARGET_SA_RESTORER) {
        env->regs[15] = ((unsigned long)ka->sa_restorer)-8;
    } else {
        uint32_t t;
        /* Note, these encodings are _big endian_! */
        /* addi r12, r0, __NR_sigreturn */
        t = 0x31800000UL | TARGET_NR_sigreturn;
        __put_user(t, frame->tramp + 0);
        /* brki r14, 0x8 */
        t = 0xb9cc0008UL;
        __put_user(t, frame->tramp + 1);

        /* Return from sighandler will jump to the tramp.
           Negative 8 offset because return is rtsd r15, 8 */
        env->regs[15] = frame_addr + offsetof(struct target_signal_frame, tramp)
                                   - 8;
    }

    /* Set up registers for signal handler */
    env->regs[1] = frame_addr;
    /* Signal handler args: */
    env->regs[5] = sig; /* Arg 0: signum */
    env->regs[6] = 0;
    /* arg 1: sigcontext */
    env->regs[7] = frame_addr += offsetof(typeof(*frame), uc);

    /* Offset of 4 to handle microblaze rtid r14, 0 */
    env->sregs[SR_PC] = (unsigned long)ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;
badframe:
    force_sigsegv(sig);
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUMBState *env)
{
    qemu_log_mask(LOG_UNIMP, "setup_rt_frame: not implemented\n");
}

long do_sigreturn(CPUMBState *env)
{
    struct target_signal_frame *frame;
    abi_ulong frame_addr;
    target_sigset_t target_set;
    sigset_t set;
    int i;

    frame_addr = env->regs[R_SP];
    trace_user_do_sigreturn(env, frame_addr);
    /* Make sure the guest isn't playing games.  */
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 1))
        goto badframe;

    /* Restore blocked signals */
    __get_user(target_set.sig[0], &frame->uc.tuc_mcontext.oldmask);
    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &frame->extramask[i - 1]);
    }
    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set);

    restore_sigcontext(&frame->uc.tuc_mcontext, env);
    /* We got here through a sigreturn syscall, our path back is via an
       rtb insn so setup r14 for that.  */
    env->regs[14] = env->sregs[SR_PC];

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;
badframe:
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUMBState *env)
{
    trace_user_do_rt_sigreturn(env, 0);
    qemu_log_mask(LOG_UNIMP, "do_rt_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}
