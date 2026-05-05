/*
 * FreeBSD thread and user mutex related system call shims
 *
 * Copyright (c) 2013-2015 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef FREEBSD_OS_THREAD_H
#define FREEBSD_OS_THREAD_H

#include <sys/thr.h>
#include <sys/rtprio.h>
#include <sys/umtx.h>

#include "qemu.h"
#include "qemu-os.h"

int safe_thr_suspend(struct timespec *timeout);
int safe__umtx_op(void *, int, unsigned long, void *, void *);

#if defined(HOST_BIG_ENDIAN) == defined(TARGET_BIG_ENDIAN) && \
    (TARGET_ABI_BITS == HOST_LONG_BITS || defined(UMTX_OP__32BIT))
#define _UMTX_OPTIMIZED
#if defined(TARGET_ABI32)
#define QEMU_UMTX_OP(n) (UMTX_OP__32BIT | (n))
#else
#define QEMU_UMTX_OP(n) (n)
#endif /* TARGET_ABI32 */
#else
#define QEMU_UMTX_OP(n) (n)
#endif

static inline abi_long do_freebsd_thr_self(abi_ulong target_id)
{
    abi_long ret;
    long tid;

    ret = get_errno(thr_self(&tid));
    if (!is_error(ret)) {
        if (put_user_sal(tid, target_id)) {
            return -TARGET_EFAULT;
        }
    }
    return ret;
}

static inline abi_long do_freebsd_thr_exit(CPUArchState *env,
        abi_ulong tid_addr)
{
    CPUState *cpu = env_cpu(env);
    TaskState *ts;

    if (block_signals()) {
        return -TARGET_ERESTART;
    }

    pthread_mutex_lock(new_freebsd_thread_lock_ptr);

    ts = cpu->opaque;

    if (tid_addr) {
        /* Signal target userland that it can free the stack. */
        if (!put_user_sal(1, tid_addr)) {
            freebsd_umtx_wake_unsafe(tid_addr, INT_MAX);
        }
    }

    object_unparent(OBJECT(env_cpu(env)));
    object_unref(OBJECT(env_cpu(env)));
    /*
     * At this point the CPU should be unrealized and removed
     * from cpu lists. We can clean-up the rest of the thread
     * data without the lock held.
     */

    pthread_mutex_unlock(new_freebsd_thread_lock_ptr);

    thread_cpu = NULL;
    g_free(ts);
    rcu_unregister_thread();
    pthread_exit(NULL);
    /* Doesn't return */
    return 0;
}

static inline abi_long do_freebsd_thr_kill(long id, int sig)
{

    return get_errno(thr_kill(id, target_to_host_signal(sig)));
}

static inline abi_long do_freebsd_thr_kill2(pid_t pid, long id, int sig)
{

    return get_errno(thr_kill2(pid, id, target_to_host_signal(sig)));
}

static inline abi_long do_freebsd_thr_suspend(abi_ulong target_ts)
{
    abi_long ret;
    struct timespec ts;

    if (target_ts != 0) {
        if (t2h_freebsd_timespec(&ts, target_ts)) {
            return -TARGET_EFAULT;
        }
        ret = get_errno(safe_thr_suspend(&ts));
    } else {
        ret = get_errno(safe_thr_suspend(NULL));
    }
    return ret;
}

static inline abi_long do_freebsd_thr_wake(long tid)
{

    return get_errno(thr_wake(tid));
}

static inline abi_long do_freebsd_thr_set_name(long tid, abi_ulong target_name)
{
    abi_long ret;
    void *p;

    p = lock_user_string(target_name);
    if (p == NULL) {
        return -TARGET_EFAULT;
    }
    ret = get_errno(thr_set_name(tid, p));
    unlock_user(p, target_name, 0);

    return ret;
}

static inline abi_long do_freebsd_rtprio_thread(int function, lwpid_t lwpid,
        abi_ulong target_addr)
{
    int ret;
    struct rtprio rtp;

    ret = t2h_freebsd_rtprio(&rtp, target_addr);
    if (!is_error(ret)) {
        ret = get_errno(rtprio_thread(function, lwpid, &rtp));
    }
    if (!is_error(ret)) {
        ret = h2t_freebsd_rtprio(target_addr, &rtp);
    }
    return ret;
}

static inline abi_long do_freebsd_getcontext(CPUArchState *env, abi_ulong arg1)
{
    abi_long ret;
    target_ucontext_t *ucp;
    sigset_t sigmask;

    if (arg1 == 0) {
        return -TARGET_EINVAL;
    }
    ret = do_sigprocmask(0, NULL, &sigmask);
    if (!is_error(ret)) {
        ucp = lock_user(VERIFY_WRITE, arg1, sizeof(target_ucontext_t), 0);
        if (ucp == 0) {
            return -TARGET_EFAULT;
        }
        ret = get_mcontext(env, &ucp->uc_mcontext, TARGET_MC_GET_CLEAR_RET);
        host_to_target_sigset(&ucp->uc_sigmask, &sigmask);
        memset(ucp->__spare__, 0, sizeof(ucp->__spare__));
        unlock_user(ucp, arg1, sizeof(target_ucontext_t));
    }
    return ret;
}

static inline abi_long do_freebsd_setcontext(CPUArchState *env, abi_ulong arg1)
{
    abi_long ret;
    target_ucontext_t *ucp;
    sigset_t sigmask;
    if (arg1 == 0) {
        return -TARGET_EINVAL;
    }
    ucp = lock_user(VERIFY_READ, arg1, sizeof(target_ucontext_t), 1);
    if (ucp == 0) {
        return -TARGET_EFAULT;
    }
    ret = set_mcontext(env, &ucp->uc_mcontext, 0);
    target_to_host_sigset(&sigmask, &ucp->uc_sigmask);
    unlock_user(ucp, arg1, 0);
    if (!is_error(ret)) {
        (void)do_sigprocmask(SIG_SETMASK, &sigmask, NULL);
    }
    return ret == 0 ? -TARGET_EJUSTRETURN : ret;
}

/* swapcontext(2) */
static inline abi_long do_freebsd_swapcontext(CPUArchState *env, abi_ulong arg1,
        abi_ulong arg2)
{
    abi_long ret;
    target_ucontext_t *ucp;
    sigset_t sigmask;

    if (arg1 == 0 || arg2 == 0) {
        return -TARGET_EINVAL;
    }
    /* Save current context in arg1. */
    ret = do_sigprocmask(0, NULL, &sigmask);
    if (!is_error(ret)) {
        ucp = lock_user(VERIFY_WRITE, arg1, sizeof(target_ucontext_t), 0);
        if (ucp == 0) {
            return -TARGET_EFAULT;
        }
        ret = get_mcontext(env, &ucp->uc_mcontext, TARGET_MC_GET_CLEAR_RET);
        host_to_target_sigset(&ucp->uc_sigmask, &sigmask);
        memset(ucp->__spare__, 0, sizeof(ucp->__spare__));
        unlock_user(ucp, arg1, sizeof(target_ucontext_t));
    }
    if (is_error(ret)) {
        return ret;
    }

    /* Restore the context in arg2 to the current context. */
    ucp = lock_user(VERIFY_READ, arg2, sizeof(target_ucontext_t), 1);
    if (ucp == 0) {
        return -TARGET_EFAULT;
    }
    ret = set_mcontext(env, &ucp->uc_mcontext, 0);
    target_to_host_sigset(&sigmask, &ucp->uc_sigmask);
    unlock_user(ucp, arg2, 0);
    if (!is_error(ret)) {
        (void)do_sigprocmask(SIG_SETMASK, &sigmask, NULL);
    }
    return ret == 0 ? -TARGET_EJUSTRETURN : ret;
}


#define safe_g2h_untagged(x) ((x) != 0 ? g2h_untagged(x) : NULL)

/*
 * undocumented _umtx_op(void *obj, int op, u_long val, void *uaddr,
 *                           void *target_time);
 */

#endif /* FREEBSD_OS_THREAD_H */
