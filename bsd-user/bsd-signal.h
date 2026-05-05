/*
 * signal related system call shims
 *
 * Copyright (c) 2013 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef BSD_SIGNAL_H
#define BSD_SIGNAL_H

#include <signal.h>

/* sigaction(2) */
static inline abi_long do_bsd_sigaction(abi_long arg1, abi_long arg2,
                                        abi_long arg3)
{
    abi_long ret;
    struct target_sigaction *old_act, act, oact, *pact;

    if (arg2) {
        if (!lock_user_struct(VERIFY_READ, old_act, arg2, 1)) {
            return -TARGET_EFAULT;
        }
        act._sa_handler = old_act->_sa_handler;
        act.sa_flags = old_act->sa_flags;
        memcpy(&act.sa_mask, &old_act->sa_mask, sizeof(target_sigset_t));
        unlock_user_struct(old_act, arg2, 0);
        pact = &act;
    } else {
        pact = NULL;
    }
    ret = get_errno(do_sigaction(arg1, pact, &oact));
    if (!is_error(ret) && arg3) {
        if (!lock_user_struct(VERIFY_WRITE, old_act, arg3, 0)) {
            return -TARGET_EFAULT;
        }
        old_act->_sa_handler = oact._sa_handler;
        old_act->sa_flags = oact.sa_flags;
        memcpy(&old_act->sa_mask, &oact.sa_mask, sizeof(target_sigset_t));
        unlock_user_struct(old_act, arg3, 1);
    }
    return ret;
}


/* sigprocmask(2) */
static inline abi_long do_bsd_sigprocmask(abi_long arg1, abi_ulong arg2,
                                          abi_ulong arg3)
{
    abi_long ret;
    void *p;
    sigset_t set, oldset, *set_ptr;
    int how;

    ret = 0;
    if (arg2) {
        p = lock_user(VERIFY_READ, arg2, sizeof(target_sigset_t), 1);
        if (p == NULL) {
            return -TARGET_EFAULT;
        }
        target_to_host_sigset(&set, p);
        unlock_user(p, arg2, 0);
        set_ptr = &set;
        switch (arg1) {
        case TARGET_SIG_BLOCK:
            how = SIG_BLOCK;
            break;
        case TARGET_SIG_UNBLOCK:
            how = SIG_UNBLOCK;
            break;
        case TARGET_SIG_SETMASK:
            how = SIG_SETMASK;
            break;
        default:
            return -TARGET_EINVAL;
        }
    } else {
        how = 0;
        set_ptr = NULL;
    }
    ret = do_sigprocmask(how, set_ptr, &oldset);
    if (!is_error(ret) && arg3) {
        p = lock_user(VERIFY_WRITE, arg3, sizeof(target_sigset_t), 0);
        if (p == NULL) {
            return -TARGET_EFAULT;
        }
        host_to_target_sigset(p, &oldset);
        unlock_user(p, arg3, sizeof(target_sigset_t));
    }
    return ret;
}

/* sigpending(2) */
static inline abi_long do_bsd_sigpending(abi_long arg1)
{
    abi_long ret;
    void *p;
    sigset_t set;

    ret = get_errno(sigpending(&set));
    if (!is_error(ret)) {
        p = lock_user(VERIFY_WRITE, arg1, sizeof(target_sigset_t), 0);
        if (p == NULL) {
            return -TARGET_EFAULT;
        }
        host_to_target_sigset(p, &set);
        unlock_user(p, arg1, sizeof(target_sigset_t));
    }
    return ret;
}

/* sigsuspend(2) */
static inline abi_long do_bsd_sigsuspend(CPUArchState *env, abi_long arg1,
                                         abi_long arg2)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts = cpu->opaque;
    void *p;
    abi_long ret;

    p = lock_user(VERIFY_READ, arg1, sizeof(target_sigset_t), 1);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    target_to_host_sigset(&ts->sigsuspend_mask, p);
    unlock_user(p, arg1, 0);

    ret = get_errno(sigsuspend(&ts->sigsuspend_mask));
    /* XXX Trivially true until safe_syscall */
    if (ret != -TARGET_ERESTART) {
        ts->in_sigsuspend = true;
    }

    return ret;
}

/* sigreturn(2) */
static inline abi_long do_bsd_sigreturn(CPUArchState *env, abi_long arg1)
{
    if (block_signals()) {
        return -TARGET_ERESTART;
    }
    return do_sigreturn(env, arg1);
}

/* sigvec(2) - not defined */
/* sigblock(2) - not defined */
/* sigsetmask(2) - not defined */
/* sigstack(2) - not defined */

#endif /* BSD_SIGNAL_H */
