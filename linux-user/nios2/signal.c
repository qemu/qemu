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

#define MCONTEXT_VERSION 2

struct target_sigcontext {
    int version;
    unsigned long gregs[32];
};

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_sigcontext tuc_mcontext;
    target_sigset_t tuc_sigmask;   /* mask last for extensibility */
};

struct target_rt_sigframe {
    struct target_siginfo info;
    struct target_ucontext uc;
};

static void rt_setup_ucontext(struct target_ucontext *uc, CPUNios2State *env)
{
    unsigned long *gregs = uc->tuc_mcontext.gregs;

    __put_user(MCONTEXT_VERSION, &uc->tuc_mcontext.version);
    __put_user(env->regs[1], &gregs[0]);
    __put_user(env->regs[2], &gregs[1]);
    __put_user(env->regs[3], &gregs[2]);
    __put_user(env->regs[4], &gregs[3]);
    __put_user(env->regs[5], &gregs[4]);
    __put_user(env->regs[6], &gregs[5]);
    __put_user(env->regs[7], &gregs[6]);
    __put_user(env->regs[8], &gregs[7]);
    __put_user(env->regs[9], &gregs[8]);
    __put_user(env->regs[10], &gregs[9]);
    __put_user(env->regs[11], &gregs[10]);
    __put_user(env->regs[12], &gregs[11]);
    __put_user(env->regs[13], &gregs[12]);
    __put_user(env->regs[14], &gregs[13]);
    __put_user(env->regs[15], &gregs[14]);
    __put_user(env->regs[16], &gregs[15]);
    __put_user(env->regs[17], &gregs[16]);
    __put_user(env->regs[18], &gregs[17]);
    __put_user(env->regs[19], &gregs[18]);
    __put_user(env->regs[20], &gregs[19]);
    __put_user(env->regs[21], &gregs[20]);
    __put_user(env->regs[22], &gregs[21]);
    __put_user(env->regs[23], &gregs[22]);
    __put_user(env->regs[R_RA], &gregs[23]);
    __put_user(env->regs[R_FP], &gregs[24]);
    __put_user(env->regs[R_GP], &gregs[25]);
    __put_user(env->regs[R_PC], &gregs[27]);
    __put_user(env->regs[R_SP], &gregs[28]);
}

static int rt_restore_ucontext(CPUNios2State *env, struct target_ucontext *uc,
                               int *pr2)
{
    int temp;
    unsigned long *gregs = uc->tuc_mcontext.gregs;

    /* Always make any pending restarted system calls return -EINTR */
    /* current->restart_block.fn = do_no_restart_syscall; */

    __get_user(temp, &uc->tuc_mcontext.version);
    if (temp != MCONTEXT_VERSION) {
        return 1;
    }

    /* restore passed registers */
    __get_user(env->regs[1], &gregs[0]);
    __get_user(env->regs[2], &gregs[1]);
    __get_user(env->regs[3], &gregs[2]);
    __get_user(env->regs[4], &gregs[3]);
    __get_user(env->regs[5], &gregs[4]);
    __get_user(env->regs[6], &gregs[5]);
    __get_user(env->regs[7], &gregs[6]);
    __get_user(env->regs[8], &gregs[7]);
    __get_user(env->regs[9], &gregs[8]);
    __get_user(env->regs[10], &gregs[9]);
    __get_user(env->regs[11], &gregs[10]);
    __get_user(env->regs[12], &gregs[11]);
    __get_user(env->regs[13], &gregs[12]);
    __get_user(env->regs[14], &gregs[13]);
    __get_user(env->regs[15], &gregs[14]);
    __get_user(env->regs[16], &gregs[15]);
    __get_user(env->regs[17], &gregs[16]);
    __get_user(env->regs[18], &gregs[17]);
    __get_user(env->regs[19], &gregs[18]);
    __get_user(env->regs[20], &gregs[19]);
    __get_user(env->regs[21], &gregs[20]);
    __get_user(env->regs[22], &gregs[21]);
    __get_user(env->regs[23], &gregs[22]);
    /* gregs[23] is handled below */
    /* Verify, should this be settable */
    __get_user(env->regs[R_FP], &gregs[24]);
    /* Verify, should this be settable */
    __get_user(env->regs[R_GP], &gregs[25]);
    /* Not really necessary no user settable bits */
    __get_user(temp, &gregs[26]);
    __get_user(env->regs[R_PC], &gregs[27]);

    __get_user(env->regs[R_RA], &gregs[23]);
    __get_user(env->regs[R_SP], &gregs[28]);

    target_restore_altstack(&uc->tuc_stack, env);

    *pr2 = env->regs[2];
    return 0;
}

static abi_ptr get_sigframe(struct target_sigaction *ka, CPUNios2State *env,
                            size_t frame_size)
{
    unsigned long usp;

    /* This is the X/Open sanctioned signal stack switching.  */
    usp = target_sigsp(get_sp_from_cpustate(env), ka);

    /* Verify, is it 32 or 64 bit aligned */
    return (usp - frame_size) & -8;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set,
                    CPUNios2State *env)
{
    struct target_rt_sigframe *frame;
    abi_ptr frame_addr;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof(*frame));
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        force_sigsegv(sig);
        return;
    }

    tswap_siginfo(&frame->info, info);

    /* Create the ucontext.  */
    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    rt_setup_ucontext(&frame->uc, env);
    for (i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    /* Set up to return from userspace; jump to fixed address sigreturn
       trampoline on kuser page.  */
    env->regs[R_RA] = (unsigned long) (0x1044);

    /* Set up registers for signal handler */
    env->regs[R_SP] = frame_addr;
    env->regs[4] = sig;
    env->regs[5] = frame_addr + offsetof(struct target_rt_sigframe, info);
    env->regs[6] = frame_addr + offsetof(struct target_rt_sigframe, uc);
    env->regs[R_PC] = ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
}

long do_sigreturn(CPUNios2State *env)
{
    trace_user_do_sigreturn(env, 0);
    qemu_log_mask(LOG_UNIMP, "do_sigreturn: not implemented\n");
    return -TARGET_ENOSYS;
}

long do_rt_sigreturn(CPUNios2State *env)
{
    /* Verify, can we follow the stack back */
    abi_ulong frame_addr = env->regs[R_SP];
    struct target_rt_sigframe *frame;
    sigset_t set;
    int rval;

    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1)) {
        goto badframe;
    }

    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    if (rt_restore_ucontext(env, &frame->uc, &rval)) {
        goto badframe;
    }

    unlock_user_struct(frame, frame_addr, 0);
    return rval;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return 0;
}
