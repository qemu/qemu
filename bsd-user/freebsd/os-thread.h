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
static inline abi_long do_freebsd__umtx_op(abi_ulong obj, int op, abi_ulong val,
        abi_ulong uaddr, abi_ulong target_time)
{
    abi_long ret;
#ifndef _UMTX_OPTIMIZED
    struct _umtx_time ut[2];
    struct timespec ts;
    size_t utsz;
    long tid;
#endif

    switch (op) {
    case TARGET_UMTX_OP_WAIT:
        /* args: obj *, val, (void *)sizeof(ut), ut * */
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_umtx_wait(obj, val, uaddr,
                                safe_g2h_untagged(target_time));
#else
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_umtx_wait(obj, tswapal(val), utsz, &ut);
        } else {
            ret = freebsd_umtx_wait(obj, tswapal(val), 0, NULL);
        }
#endif
        break;

    case TARGET_UMTX_OP_WAKE:
        /* args: obj *, nr_wakeup */
        ret = freebsd_umtx_wake(obj, val);
        break;

    case TARGET_UMTX_OP_MUTEX_LOCK:
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_lock_umutex(obj, 0, safe_g2h_untagged(target_time), uaddr,
                                  0, val);
#else
        ret = get_errno(thr_self(&tid));
        if (is_error(ret)) {
            return ret;
        }
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_lock_umutex(obj, tid, ut, utsz, 0, tswapal(val));
        } else {
            ret = freebsd_lock_umutex(obj, tid, NULL, 0, 0, tswapal(val));
        }
#endif
        break;

    case TARGET_UMTX_OP_MUTEX_UNLOCK:
#ifdef _UMTX_OPTIMIZED
        ret = freebsd_unlock_umutex(obj, 0);
#else
        ret = get_errno(thr_self(&tid));
        if (is_error(ret)) {
            return ret;
        }
        ret = freebsd_unlock_umutex(obj, tid);
#endif
        break;

    case TARGET_UMTX_OP_MUTEX_TRYLOCK:
#ifdef _UMTX_OPTIMIZED
        ret = freebsd_lock_umutex(obj, 0, NULL, 0, TARGET_UMUTEX_TRY, val);
#else
        ret = get_errno(thr_self(&tid));
        if (is_error(ret)) {
            return ret;
        }
        ret = freebsd_lock_umutex(obj, tid, NULL, 0, TARGET_UMUTEX_TRY,
            tswapal(val));
#endif
        break;

    case TARGET_UMTX_OP_MUTEX_WAIT:
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_lock_umutex(obj, 0, safe_g2h_untagged(target_time), uaddr,
            TARGET_UMUTEX_WAIT, val);
#else
        ret = get_errno(thr_self(&tid));
        if (is_error(ret)) {
            return ret;
        }
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_lock_umutex(obj, tid, ut, utsz, TARGET_UMUTEX_WAIT,
                tswapal(val));
        } else {
            ret = freebsd_lock_umutex(obj, tid, NULL, 0, TARGET_UMUTEX_WAIT,
                tswapal(val));
        }
#endif
        break;

    case TARGET_UMTX_OP_MUTEX_WAKE:
        /* Don't need to do access_ok(). */
        ret = freebsd_umtx_mutex_wake(obj, val);
        break;

    case TARGET_UMTX_OP_SET_CEILING:
        ret = 0; /* XXX quietly ignore these things for now */
        break;

    case TARGET_UMTX_OP_CV_WAIT:
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_cv_wait(obj, uaddr, safe_g2h_untagged(target_time), val);
#else
        /*
         * Initialization of the struct conv is done by
         * bzero'ing everything in userland.
         */
        if (target_time != 0) {
            if (t2h_freebsd_timespec(&ts, target_time)) {
                return -TARGET_EFAULT;
            }
            ret = freebsd_cv_wait(obj, uaddr, &ts, val);
        } else {
            ret = freebsd_cv_wait(obj, uaddr, NULL, val);
        }
#endif
        break;

    case TARGET_UMTX_OP_CV_SIGNAL:
        /*
         * XXX
         * User code may check if c_has_waiters is zero.  Other
         * than that it is assume that user code doesn't do
         * much with the struct conv fields and is pretty
         * much opauque to userland.
         */
        ret = freebsd_cv_signal(obj);
        break;

    case TARGET_UMTX_OP_CV_BROADCAST:
        /*
         * XXX
         * User code may check if c_has_waiters is zero.  Other
         * than that it is assume that user code doesn't do
         * much with the struct conv fields and is pretty
         * much opauque to userland.
         */
        ret = freebsd_cv_broadcast(obj);
        break;

    case TARGET_UMTX_OP_WAIT_UINT:
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_umtx_wait_uint(obj, val, uaddr,
                                     safe_g2h_untagged(target_time));
#else
        if (!access_ok(VERIFY_READ, obj, sizeof(abi_ulong))) {
            return -TARGET_EFAULT;
        }
        /* args: obj *, val, (void *)sizeof(ut), ut * */
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_umtx_wait_uint(obj, tswap32((uint32_t)val),
                                         utsz, &ut);
        } else {
            ret = freebsd_umtx_wait_uint(obj, tswap32((uint32_t)val), 0, NULL);
        }
#endif
        break;

    case TARGET_UMTX_OP_WAIT_UINT_PRIVATE:
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_umtx_wait_uint_private(obj, val, uaddr,
            safe_g2h_untagged(target_time));
#else
        if (!access_ok(VERIFY_READ, obj, sizeof(abi_ulong))) {
            return -TARGET_EFAULT;
        }
        /* args: obj *, val, (void *)sizeof(ut), ut * */
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_umtx_wait_uint_private(obj, tswap32((uint32_t)val),
                    utsz, &ut);
        } else {
            ret = freebsd_umtx_wait_uint_private(obj, tswap32((uint32_t)val),
                    0, NULL);
        }
#endif
        break;

    case TARGET_UMTX_OP_WAKE_PRIVATE:
        /* Don't need to do access_ok(). */
        ret = freebsd_umtx_wake_private(obj, val);
        break;

    case TARGET_UMTX_OP_RW_RDLOCK:
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_rw_rdlock(obj, val, uaddr,
                                safe_g2h_untagged(target_time));
#else
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_rw_rdlock(obj, val, utsz, &ut);
        } else {
            ret = freebsd_rw_rdlock(obj, val, 0, NULL);
        }
#endif
        break;

    case TARGET_UMTX_OP_RW_WRLOCK:
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_rw_wrlock(obj, val, uaddr,
                                safe_g2h_untagged(target_time));
#else
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_rw_wrlock(obj, val, utsz, &ut);
        } else {
            ret = freebsd_rw_wrlock(obj, val, 0, NULL);
        }
#endif
        break;

    case TARGET_UMTX_OP_RW_UNLOCK:
        ret = freebsd_rw_unlock(obj);
        break;

#ifdef UMTX_OP_MUTEX_WAKE2
    case TARGET_UMTX_OP_MUTEX_WAKE2:
        ret = freebsd_umtx_mutex_wake2(obj, val);
        break;
#endif /* UMTX_OP_MUTEX_WAKE2 */

#ifdef UMTX_OP_NWAKE_PRIVATE
    case TARGET_UMTX_OP_NWAKE_PRIVATE:
        ret = freebsd_umtx_nwake_private(obj, val);
        break;
#endif /* UMTX_OP_NWAKE_PRIVATE */

    case TARGET_UMTX_OP_SEM2_WAIT:
        /* args: obj *, val, (void *)sizeof(ut), ut * */
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(
            (uaddr > sizeof(struct target_freebsd__umtx_time) ? VERIFY_WRITE :
             VERIFY_READ), target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_umtx_sem2_wait(obj, uaddr,
                                     safe_g2h_untagged(target_time));
#else
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            /* Kernel writes out the ut[1] if utsz >= _umtx_time + timespec. */
            ret = freebsd_umtx_sem2_wait(obj, utsz, ut);
            if (ret == -TARGET_EINTR && (ut[0]._flags & UMTX_ABSTIME) == 0 &&
                utsz >= sizeof(struct target_freebsd__umtx_time) +
                sizeof(struct target_freebsd_timespec)) {
                abi_ulong cret;

                cret = h2t_freebsd_timespec(target_time +
                    sizeof(struct target_freebsd__umtx_time), &ut[1]._timeout);
                if (is_error(cret)) {
                    ret = cret;
                }
            }
        } else {
            ret = freebsd_umtx_sem2_wait(obj, 0, NULL);
        }
#endif
        break;

    case TARGET_UMTX_OP_SEM2_WAKE:
        /* Don't need to do access_ok(). */
        ret = freebsd_umtx_sem2_wake(obj);
        break;
    case TARGET_UMTX_OP_SEM_WAIT:
        /* args: obj *, val, (void *)sizeof(ut), ut * */
#ifdef _UMTX_OPTIMIZED
        if (target_time != 0 && !access_ok(VERIFY_READ, target_time, uaddr)) {
            return -TARGET_EFAULT;
        }
        ret = freebsd_umtx_sem_wait(obj, uaddr, safe_g2h_untagged(target_time));
#else
        if (target_time != 0) {
            ret = t2h_freebsd_umtx_time(target_time, uaddr, ut, &utsz);
            if (is_error(ret)) {
                return ret;
            }
            ret = freebsd_umtx_sem_wait(obj, utsz, ut);
        } else {
            ret = freebsd_umtx_sem_wait(obj, 0, NULL);
        }
#endif
        break;

    case TARGET_UMTX_OP_SEM_WAKE:
        /* Don't need to do access_ok(). */
        ret = freebsd_umtx_sem_wake(obj);
        break;
    case TARGET_UMTX_OP_SHM:
        ret = freebsd_umtx_shm(uaddr, val);
        break;
    case TARGET_UMTX_OP_ROBUST_LISTS:
        ret = freebsd_umtx_robust_list(uaddr, val);
        break;
    default:
        return -TARGET_EINVAL;
    }
    return ret;
}

#endif /* FREEBSD_OS_THREAD_H */
