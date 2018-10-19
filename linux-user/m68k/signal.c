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
    abi_ulong  sc_mask;
    abi_ulong  sc_usp;
    abi_ulong  sc_d0;
    abi_ulong  sc_d1;
    abi_ulong  sc_a0;
    abi_ulong  sc_a1;
    unsigned short sc_sr;
    abi_ulong  sc_pc;
};

struct target_sigframe
{
    abi_ulong pretcode;
    int sig;
    int code;
    abi_ulong psc;
    char retcode[8];
    abi_ulong extramask[TARGET_NSIG_WORDS-1];
    struct target_sigcontext sc;
};

typedef int target_greg_t;
#define TARGET_NGREG 18
typedef target_greg_t target_gregset_t[TARGET_NGREG];

typedef struct target_fpregset {
    int f_fpcntl[3];
    int f_fpregs[8*3];
} target_fpregset_t;

struct target_mcontext {
    int version;
    target_gregset_t gregs;
    target_fpregset_t fpregs;
};

#define TARGET_MCONTEXT_VERSION 2

struct target_ucontext {
    abi_ulong tuc_flags;
    abi_ulong tuc_link;
    target_stack_t tuc_stack;
    struct target_mcontext tuc_mcontext;
    abi_long tuc_filler[80];
    target_sigset_t tuc_sigmask;
};

struct target_rt_sigframe
{
    abi_ulong pretcode;
    int sig;
    abi_ulong pinfo;
    abi_ulong puc;
    char retcode[8];
    struct target_siginfo info;
    struct target_ucontext uc;
};

static void setup_sigcontext(struct target_sigcontext *sc, CPUM68KState *env,
                             abi_ulong mask)
{
    uint32_t sr = (env->sr & 0xff00) | cpu_m68k_get_ccr(env);
    __put_user(mask, &sc->sc_mask);
    __put_user(env->aregs[7], &sc->sc_usp);
    __put_user(env->dregs[0], &sc->sc_d0);
    __put_user(env->dregs[1], &sc->sc_d1);
    __put_user(env->aregs[0], &sc->sc_a0);
    __put_user(env->aregs[1], &sc->sc_a1);
    __put_user(sr, &sc->sc_sr);
    __put_user(env->pc, &sc->sc_pc);
}

static void
restore_sigcontext(CPUM68KState *env, struct target_sigcontext *sc)
{
    int temp;

    __get_user(env->aregs[7], &sc->sc_usp);
    __get_user(env->dregs[0], &sc->sc_d0);
    __get_user(env->dregs[1], &sc->sc_d1);
    __get_user(env->aregs[0], &sc->sc_a0);
    __get_user(env->aregs[1], &sc->sc_a1);
    __get_user(env->pc, &sc->sc_pc);
    __get_user(temp, &sc->sc_sr);
    cpu_m68k_set_ccr(env, temp);
}

/*
 * Determine which stack to use..
 */
static inline abi_ulong
get_sigframe(struct target_sigaction *ka, CPUM68KState *regs,
             size_t frame_size)
{
    abi_ulong sp;

    sp = target_sigsp(get_sp_from_cpustate(regs), ka);


    return ((sp - frame_size) & -8UL);
}

void setup_frame(int sig, struct target_sigaction *ka,
                 target_sigset_t *set, CPUM68KState *env)
{
    struct target_sigframe *frame;
    abi_ulong frame_addr;
    abi_ulong retcode_addr;
    abi_ulong sc_addr;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    trace_user_setup_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    __put_user(sig, &frame->sig);

    sc_addr = frame_addr + offsetof(struct target_sigframe, sc);
    __put_user(sc_addr, &frame->psc);

    setup_sigcontext(&frame->sc, env, set->sig[0]);

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->extramask[i - 1]);
    }

    /* Set up to return from userspace.  */

    retcode_addr = frame_addr + offsetof(struct target_sigframe, retcode);
    __put_user(retcode_addr, &frame->pretcode);

    /* moveq #,d0; trap #0 */

    __put_user(0x70004e40 + (TARGET_NR_sigreturn << 16),
               (uint32_t *)(frame->retcode));

    /* Set up to return from userspace */

    env->aregs[7] = frame_addr;
    env->pc = ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    force_sigsegv(sig);
}

static inline void target_rt_save_fpu_state(struct target_ucontext *uc,
                                           CPUM68KState *env)
{
    int i;
    target_fpregset_t *fpregs = &uc->tuc_mcontext.fpregs;

    __put_user(env->fpcr, &fpregs->f_fpcntl[0]);
    __put_user(env->fpsr, &fpregs->f_fpcntl[1]);
    /* fpiar is not emulated */

    for (i = 0; i < 8; i++) {
        uint32_t high = env->fregs[i].d.high << 16;
        __put_user(high, &fpregs->f_fpregs[i * 3]);
        __put_user(env->fregs[i].d.low,
                   (uint64_t *)&fpregs->f_fpregs[i * 3 + 1]);
    }
}

static inline int target_rt_setup_ucontext(struct target_ucontext *uc,
                                           CPUM68KState *env)
{
    target_greg_t *gregs = uc->tuc_mcontext.gregs;
    uint32_t sr = (env->sr & 0xff00) | cpu_m68k_get_ccr(env);

    __put_user(TARGET_MCONTEXT_VERSION, &uc->tuc_mcontext.version);
    __put_user(env->dregs[0], &gregs[0]);
    __put_user(env->dregs[1], &gregs[1]);
    __put_user(env->dregs[2], &gregs[2]);
    __put_user(env->dregs[3], &gregs[3]);
    __put_user(env->dregs[4], &gregs[4]);
    __put_user(env->dregs[5], &gregs[5]);
    __put_user(env->dregs[6], &gregs[6]);
    __put_user(env->dregs[7], &gregs[7]);
    __put_user(env->aregs[0], &gregs[8]);
    __put_user(env->aregs[1], &gregs[9]);
    __put_user(env->aregs[2], &gregs[10]);
    __put_user(env->aregs[3], &gregs[11]);
    __put_user(env->aregs[4], &gregs[12]);
    __put_user(env->aregs[5], &gregs[13]);
    __put_user(env->aregs[6], &gregs[14]);
    __put_user(env->aregs[7], &gregs[15]);
    __put_user(env->pc, &gregs[16]);
    __put_user(sr, &gregs[17]);

    target_rt_save_fpu_state(uc, env);

    return 0;
}

static inline void target_rt_restore_fpu_state(CPUM68KState *env,
                                               struct target_ucontext *uc)
{
    int i;
    target_fpregset_t *fpregs = &uc->tuc_mcontext.fpregs;
    uint32_t fpcr;

    __get_user(fpcr, &fpregs->f_fpcntl[0]);
    cpu_m68k_set_fpcr(env, fpcr);
    __get_user(env->fpsr, &fpregs->f_fpcntl[1]);
    /* fpiar is not emulated */

    for (i = 0; i < 8; i++) {
        uint32_t high;
        __get_user(high, &fpregs->f_fpregs[i * 3]);
        env->fregs[i].d.high = high >> 16;
        __get_user(env->fregs[i].d.low,
                   (uint64_t *)&fpregs->f_fpregs[i * 3 + 1]);
    }
}

static inline int target_rt_restore_ucontext(CPUM68KState *env,
                                             struct target_ucontext *uc)
{
    int temp;
    target_greg_t *gregs = uc->tuc_mcontext.gregs;

    __get_user(temp, &uc->tuc_mcontext.version);
    if (temp != TARGET_MCONTEXT_VERSION)
        goto badframe;

    /* restore passed registers */
    __get_user(env->dregs[0], &gregs[0]);
    __get_user(env->dregs[1], &gregs[1]);
    __get_user(env->dregs[2], &gregs[2]);
    __get_user(env->dregs[3], &gregs[3]);
    __get_user(env->dregs[4], &gregs[4]);
    __get_user(env->dregs[5], &gregs[5]);
    __get_user(env->dregs[6], &gregs[6]);
    __get_user(env->dregs[7], &gregs[7]);
    __get_user(env->aregs[0], &gregs[8]);
    __get_user(env->aregs[1], &gregs[9]);
    __get_user(env->aregs[2], &gregs[10]);
    __get_user(env->aregs[3], &gregs[11]);
    __get_user(env->aregs[4], &gregs[12]);
    __get_user(env->aregs[5], &gregs[13]);
    __get_user(env->aregs[6], &gregs[14]);
    __get_user(env->aregs[7], &gregs[15]);
    __get_user(env->pc, &gregs[16]);
    __get_user(temp, &gregs[17]);
    cpu_m68k_set_ccr(env, temp);

    target_rt_restore_fpu_state(env, uc);

    return 0;

badframe:
    return 1;
}

void setup_rt_frame(int sig, struct target_sigaction *ka,
                    target_siginfo_t *info,
                    target_sigset_t *set, CPUM68KState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr;
    abi_ulong retcode_addr;
    abi_ulong info_addr;
    abi_ulong uc_addr;
    int err = 0;
    int i;

    frame_addr = get_sigframe(ka, env, sizeof *frame);
    trace_user_setup_rt_frame(env, frame_addr);
    if (!lock_user_struct(VERIFY_WRITE, frame, frame_addr, 0)) {
        goto give_sigsegv;
    }

    __put_user(sig, &frame->sig);

    info_addr = frame_addr + offsetof(struct target_rt_sigframe, info);
    __put_user(info_addr, &frame->pinfo);

    uc_addr = frame_addr + offsetof(struct target_rt_sigframe, uc);
    __put_user(uc_addr, &frame->puc);

    tswap_siginfo(&frame->info, info);

    /* Create the ucontext */

    __put_user(0, &frame->uc.tuc_flags);
    __put_user(0, &frame->uc.tuc_link);
    target_save_altstack(&frame->uc.tuc_stack, env);
    err |= target_rt_setup_ucontext(&frame->uc, env);

    if (err)
        goto give_sigsegv;

    for(i = 0; i < TARGET_NSIG_WORDS; i++) {
        __put_user(set->sig[i], &frame->uc.tuc_sigmask.sig[i]);
    }

    /* Set up to return from userspace.  */

    retcode_addr = frame_addr + offsetof(struct target_sigframe, retcode);
    __put_user(retcode_addr, &frame->pretcode);

    /* moveq #,d0; notb d0; trap #0 */

    __put_user(0x70004600 + ((TARGET_NR_rt_sigreturn ^ 0xff) << 16),
               (uint32_t *)(frame->retcode + 0));
    __put_user(0x4e40, (uint16_t *)(frame->retcode + 4));

    /* Set up to return from userspace */

    env->aregs[7] = frame_addr;
    env->pc = ka->_sa_handler;

    unlock_user_struct(frame, frame_addr, 1);
    return;

give_sigsegv:
    unlock_user_struct(frame, frame_addr, 1);
    force_sigsegv(sig);
}

long do_sigreturn(CPUM68KState *env)
{
    struct target_sigframe *frame;
    abi_ulong frame_addr = env->aregs[7] - 4;
    target_sigset_t target_set;
    sigset_t set;
    int i;

    trace_user_do_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;

    /* set blocked signals */

    __get_user(target_set.sig[0], &frame->sc.sc_mask);

    for(i = 1; i < TARGET_NSIG_WORDS; i++) {
        __get_user(target_set.sig[i], &frame->extramask[i - 1]);
    }

    target_to_host_sigset_internal(&set, &target_set);
    set_sigmask(&set);

    /* restore registers */

    restore_sigcontext(env, &frame->sc);

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}

long do_rt_sigreturn(CPUM68KState *env)
{
    struct target_rt_sigframe *frame;
    abi_ulong frame_addr = env->aregs[7] - 4;
    sigset_t set;

    trace_user_do_rt_sigreturn(env, frame_addr);
    if (!lock_user_struct(VERIFY_READ, frame, frame_addr, 1))
        goto badframe;

    target_to_host_sigset(&set, &frame->uc.tuc_sigmask);
    set_sigmask(&set);

    /* restore registers */

    if (target_rt_restore_ucontext(env, &frame->uc))
        goto badframe;

    if (do_sigaltstack(frame_addr +
                       offsetof(struct target_rt_sigframe, uc.tuc_stack),
                       0, get_sp_from_cpustate(env)) == -EFAULT)
        goto badframe;

    unlock_user_struct(frame, frame_addr, 0);
    return -TARGET_QEMU_ESIGRETURN;

badframe:
    unlock_user_struct(frame, frame_addr, 0);
    force_sig(TARGET_SIGSEGV);
    return -TARGET_QEMU_ESIGRETURN;
}
