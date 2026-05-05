/*
 * FreeBSD thr emulation support code
 *
 * Copyright (c) 2013-2015 Stacey D. Son
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"

#include <machine/atomic.h>

#include "qemu.h"
#include "qemu-os.h"
#include "signal-common.h"
#include "target_arch_cpu.h"
#include "target_arch_thread.h"
#include "tcg/startup.h"
#include "exec/tb-flush.h"

#include "os-thread.h"

/* #define DEBUG_UMTX(...)   fprintf(stderr, __VA_ARGS__) */
/* #define DEBUG_UMTX(...) qemu_log(__VA_ARGS__) */
#define DEBUG_UMTX(...)

#define DETECT_DEADLOCK 0
#define DEADLOCK_TO     1200

#define NEW_STACK_SIZE  0x40000

/* sys/_umtx.h */
struct target_umtx {
    abi_ulong   u_owner;    /* Owner of the mutex. */
};

struct target_umutex {
    uint32_t    m_owner;    /* Owner of the mutex */
    uint32_t    m_flags;    /* Flags of the mutex */
    uint32_t    m_ceiling[2];   /* Priority protect ceiling */
    abi_ulong   m_rb_lnk;   /* Robust linkage. */
#if TARGET_ABI_BITS == 32
    uint32_t    m_pad;
#endif
    uint32_t    m_spare;
    uint32_t    m_count;    /* QEMU-internal; takes one spare. */
};

struct target_ucond {
    uint32_t    c_has_waiters;  /* Has waiters in kernel */
    uint32_t    c_flags;    /* Flags of the condition variable */
    uint32_t    c_clockid;  /* Clock id */
    uint32_t    c_spare[1];
};

struct target_urwlock {
    uint32_t    rw_state;
    uint32_t    rw_flags;
    uint32_t    rw_blocked_readers;
    uint32_t    rw_blocked_writers;
    uint32_t    rw_spare[4];
};

struct target__usem {
    uint32_t    _has_waiters;
    uint32_t    _count;
    uint32_t    _flags;
};

struct target__usem2 {
    uint32_t    _count;
    uint32_t    _flags;
};

struct target_umtx_robust_lists_params {
    abi_ulong   robust_list_offset;
#if TARGET_ABI_BITS == 32
    uint32_t    m_pad1;
#endif
    abi_ulong   robust_priv_list_offset;
#if TARGET_ABI_BITS == 32
    uint32_t    m_pad2;
#endif
    abi_ulong   robust_inact_offset;
#if TARGET_ABI_BITS == 32
    uint32_t    m_pad3;
#endif
};

/*
 * !!! These mutexes must be reset in fork_end() (in bsd-user/main.c).
 */
static pthread_mutex_t new_thread_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t *new_freebsd_thread_lock_ptr = &new_thread_lock;
static pthread_mutex_t umtx_wait_lck = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t *freebsd_umtx_wait_lck_ptr = &umtx_wait_lck;

static void rtp_to_schedparam(const struct rtprio *rtp, int *policy,
        struct sched_param *param)
{

    switch (rtp->type) {
    case RTP_PRIO_REALTIME:
        *policy = SCHED_RR;
        param->sched_priority = RTP_PRIO_MAX - rtp->prio;
        break;

    case RTP_PRIO_FIFO:
        *policy = SCHED_FIFO;
        param->sched_priority = RTP_PRIO_MAX - rtp->prio;
        break;

    default:
        *policy = SCHED_OTHER;
        param->sched_priority = 0;
        break;
    }
}

void *new_freebsd_thread_start(void *arg)
{
    new_freebsd_thread_info_t *info = arg;
    CPUArchState *env;
    CPUState *cpu;
    long tid;

    rcu_register_thread();
    tcg_register_thread();
    env = info->env;
    cpu = env_cpu(env);
    thread_cpu = cpu;
    (void)thr_self(&tid);

    /* copy out the child TID to both locations */
    if (info->param.child_tid) {
        put_user_ual(tid, info->param.child_tid);
    }
    if (info->param.parent_tid) {
        put_user_ual(tid, info->param.parent_tid);
    }

    /* Set arch dependent registers to start thread. */
    target_thread_set_upcall(env, info->param.start_func, info->param.arg,
        info->param.stack_base, info->param.stack_size);
    target_cpu_set_tls(env, info->param.tls_base);

    /* Enable signals */
    sigprocmask(SIG_SETMASK, &info->sigmask, NULL);
    /* Signal to the parent that we're ready. */
    pthread_mutex_lock(&info->mutex);
    pthread_cond_broadcast(&info->cond);
    pthread_mutex_unlock(&info->mutex);
    /* Wait until the parent has finished. */
    pthread_mutex_lock(new_freebsd_thread_lock_ptr);
    pthread_mutex_unlock(new_freebsd_thread_lock_ptr);

    cpu_loop(env);
    /* never exits */

    return NULL;
}

/*
 * FreeBSD user mutex (_umtx) emulation
 */
static int tcmpset_al(abi_ulong *addr, abi_ulong a, abi_ulong b)
{
    abi_ulong current = tswapal(a);
    abi_ulong new = tswapal(b);

#ifdef TARGET_ABI32
    return atomic_cmpset_acq_32(addr, current, new);
#else
    return atomic_cmpset_acq_64(addr, current, new);
#endif
}

#ifdef _UMTX_OPTIMIZED
static int optimized_umtx_op(abi_ulong obj, int op, abi_ulong val,
    void *uaddr1, void *uaddr2)
{

    return get_errno(safe__umtx_op(g2h_untagged(obj), QEMU_UMTX_OP(op), val,
                                   uaddr1, uaddr2));
}

#else /* !_UMTX_OPTIMIZED */

/*
 * _cv_mutex keeps other threads from doing a signal or broadcast until
 * the thread is actually asleep and ready.  This is a global mutex for all
 * condition vars so I am sure performance may be a problem if there are lots
 * of CVs.
 *
 */
static struct umutex _cv_mutex;

static int tcmpset_32(uint32_t *addr, uint32_t a, uint32_t b)
{
    uint32_t current = tswap32(a);
    uint32_t new = tswap32(b);

    return atomic_cmpset_acq_32(addr, current, new);
}

#endif /* _UMTX_OPTIMIZED */

static abi_long _umtx_wait_uint(uint32_t *addr, uint32_t target_val,
        size_t tsz, void *t, const char *where)
{
#if DETECT_DEADLOCK
    abi_long ret;
    long cnt = 0;

    /* target_val has already been tswap'ed. */
    if (t == NULL) {
        struct timespec ts;

        ts.tv_sec = 5;
        ts.tv_nsec = 0;

        do {
            if (target_val != *addr) {
                return 0;
            }

            ret = get_errno(safe__umtx_op(addr, QEMU_UMTX_OP(UMTX_OP_WAIT_UINT),
                    target_val, NULL, &ts));

            if (ret != -TARGET_ETIMEDOUT) {
                return ret;
            }
            if (cnt++ > DEADLOCK_TO) {
                fprintf(stderr, "QEMU: Deadlock in %s from %s\n",
                        __func__, where);
                abort();
            }
        } while (1);
    } else
#endif
        return get_errno(safe__umtx_op(addr, QEMU_UMTX_OP(UMTX_OP_WAIT_UINT),
            target_val, (void *)tsz, t));
}

abi_long freebsd_umtx_wait_uint(abi_ulong obj, uint32_t target_val,
        size_t tsz, void *t)
{
    /* target_val has already been tswap'ed. */

    DEBUG_UMTX("<WAIT> %s: _umtx_op(%p, %d, 0x%x, %d, %p)\n", __func__,
               g2h_untagged(obj), UMTX_OP_WAIT_UINT, target_val, (int)tsz, t);

    return _umtx_wait_uint(g2h_untagged(obj), target_val, tsz, t, __func__);
}

static abi_long _umtx_wait_uint_private(uint32_t *addr, uint32_t target_val,
        size_t tsz, void *t, const char *where)
{
#if DETECT_DEADLOCK
    abi_long ret;
    long cnt = 0;

    /* target_val has already been tswap'ed. */
    if (t == NULL) {
        struct timespec ts;

        ts.tv_sec = 5;
        ts.tv_nsec = 0;

        do {
            if (target_val != *addr) {
                return 0;
            }

            ret = get_errno(safe__umtx_op(addr,
                QEMU_UMTX_OP(UMTX_OP_WAIT_UINT_PRIVATE), target_val, NULL,
                &ts));

            if (ret != -TARGET_ETIMEDOUT) {
                return ret;
            }
            if (cnt++ > DEADLOCK_TO) {
                fprintf(stderr, "QEMU: Deadlock in %s from %s\n", __func__,
                        where);
                abort();
            }
        } while (1);
    } else
#endif /* DETECT_DEADLOCK */
    {
        return get_errno(safe__umtx_op(addr,
            QEMU_UMTX_OP(UMTX_OP_WAIT_UINT_PRIVATE), target_val, (void *)tsz,
            t));
    }
}

abi_long freebsd_umtx_wait_uint_private(abi_ulong obj, uint32_t target_val,
        size_t tsz, void *t)
{
    DEBUG_UMTX("<WAIT_UINT_PRIVATE> %s: _umtx_op(%p (%u), %d, 0x%x, %d, %p)\n",
        __func__, g2h_untagged(obj), tswap32(*(uint32_t *)g2h_untagged(obj)),
        UMTX_OP_WAIT_UINT_PRIVATE, target_val, (int)tsz, t);

    return _umtx_wait_uint_private(g2h_untagged(obj), target_val, tsz, t,
        __func__);
}

static abi_long _umtx_wait(abi_ulong *addr, abi_ulong target_val, size_t tsz,
        void *t, const char *where)
{
#if DETECT_DEADLOCK
    abi_long ret;
    long cnt = 0;

    /* target_val has already been tswap'ed. */

    if (t == NULL) {
        struct timespec ts;

        ts.tv_sec = 5;
        ts.tv_nsec = 0;

        do {
            if (target_val != *addr) {
                return 0;
            }

            ret = get_errno(safe__umtx_op(addr, QEMU_UMTX_OP(UMTX_OP_WAIT),
                        target_val, NULL, &ts));
            if (ret != -TARGET_ETIMEDOUT) {
                return ret;
            }

            if (cnt++ > DEADLOCK_TO) {
                fprintf(stderr, "QEMU: Deadlock in %s from %s\n", __func__,
                        where);
                abort();
            }
        } while (1);
    } else
#endif /* DETECT_DEADLOCK */
    {
        return get_errno(safe__umtx_op(addr, QEMU_UMTX_OP(UMTX_OP_WAIT),
                target_val, (void *)tsz, t));
    }
}

abi_long freebsd_umtx_wait(abi_ulong targ_addr, abi_ulong target_id, size_t tsz,
        void *t)
{

    /* target_id has already been tswap'ed. */

    /* We want to check the user memory but not lock it.  We might sleep. */
    if (!access_ok(VERIFY_READ, targ_addr, sizeof(abi_ulong))) {
        return -TARGET_EFAULT;
    }

    DEBUG_UMTX("<WAIT> %s: _umtx_op(%p, %d, 0x%llx, %d, %p)\n",
        __func__, g2h_untagged(targ_addr), UMTX_OP_WAIT, (long long)target_id,
        (int)tsz, t);
    return _umtx_wait(g2h_untagged(targ_addr), target_id, tsz, t, __func__);
}


abi_long freebsd_umtx_wake_private(abi_ulong obj, uint32_t val)
{

    DEBUG_UMTX("<WAKE_PRIVATE> %s: _umtx_op(%p (%d), %d, %u, NULL, NULL)\n",
        __func__, g2h_untagged(obj), tswap32(*(uint32_t *)g2h_untagged(obj)),
        UMTX_OP_WAKE_PRIVATE, val);
    return get_errno(safe__umtx_op(g2h_untagged(obj),
        QEMU_UMTX_OP(UMTX_OP_WAKE_PRIVATE), val, NULL, NULL));
}

#if defined(UMTX_OP_NWAKE_PRIVATE)
#define BATCH_SIZE 128
abi_long freebsd_umtx_nwake_private(abi_ulong target_array_addr, uint32_t num)
{
#ifdef _UMTX_OPTIMIZED
    abi_ulong *tp;
    uintptr_t uaddrs[BATCH_SIZE];
    int count, error, i, j;

    if (!access_ok(VERIFY_READ, target_array_addr, num * sizeof(abi_ulong))) {
        return -TARGET_EFAULT;
    }

    /*
     * If we haven't relocated the guest, there's a 1:1 mapping so we can avoid
     * having to g2h_untagged() each address and just pass it through as-is.
     */
    if (!have_guest_base && !reserved_va) {
        return optimized_umtx_op(target_array_addr, UMTX_OP_NWAKE_PRIVATE, num,
            NULL, NULL);
    }

    tp = (abi_ulong *)g2h_untagged(target_array_addr);
    for (i = 0, count = num; i < num; i += BATCH_SIZE, count -= BATCH_SIZE) {
        for (j = i; j < i + MIN(BATCH_SIZE, count); j++) {
            uaddrs[j % BATCH_SIZE] = (uintptr_t)g2h_untagged(tp[j]);
        }

        /*
         * This one should not be passed as compat32 at this point; we've
         * converted them all to host pointers.
         */
        error = get_errno(safe__umtx_op(uaddrs, UMTX_OP_NWAKE_PRIVATE,
            MIN(BATCH_SIZE, count), NULL, NULL));
        if (is_error(error)) {
            return error;
        }
    }

    return 0;
#else
    int i;
    abi_ulong *uaddr;
    abi_long ret = 0;

    DEBUG_UMTX("<NWAKE_PRIVATE> %s: _umtx_op(%p, %d, %d, NULL, NULL) Waking: ",
        __func__, g2h_untagged(target_array_addr), UMTX_OP_NWAKE_PRIVATE, num);

    if (!access_ok(VERIFY_READ, target_array_addr, num * sizeof(abi_ulong))) {
        return -TARGET_EFAULT;
    }

    uaddr = (abi_ulong *)g2h_untagged(target_array_addr);
    for (i = 0; i < (int32_t)num; i++) {
        DEBUG_UMTX("%p (%u) ", g2h_untagged(tswapal(uaddr[i])),
            tswap32(*(uint32_t *)g2h_untagged(tswapal(uaddr[i]))));
        ret = get_errno(safe__umtx_op(g2h_untagged(tswapal(uaddr[i])),
            UMTX_OP_WAKE_PRIVATE, INT_MAX, NULL, NULL));
        if (is_error(ret)) {
            DEBUG_UMTX("\n");
            return ret;
        }
    }
    DEBUG_UMTX("\n");
    return ret;
#endif /* _UMTX_OPTIMIZED */
}
#endif /* UMTX_OP_NWAKE_PRIVATE */

#if defined(UMTX_OP_MUTEX_WAKE2)
abi_long freebsd_umtx_mutex_wake2(abi_ulong target_addr, uint32_t flags)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_addr, sizeof(struct target_umutex))) {
        return -TARGET_EFAULT;
    }

    DEBUG_UMTX("<MUTEX WAKE2> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
        __func__, g2h_untagged(target_addr), UMTX_OP_MUTEX_WAKE2, flags);
    return optimized_umtx_op(target_addr, UMTX_OP_MUTEX_WAKE2, flags, NULL,
        NULL);
#else
    uint32_t count, owner, *addr;
    struct target_umutex *target_umutex;

    if (!lock_user_struct(VERIFY_WRITE, target_umutex, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    pthread_mutex_lock(&umtx_wait_lck);
    __get_user(count, &target_umutex->m_count);
    __get_user(owner, &target_umutex->m_owner);
    while ((owner & TARGET_UMUTEX_CONTESTED) == 0 && (count > 1 ||
            (count == 1 && (owner & ~TARGET_UMUTEX_CONTESTED) != 0))) {
        if (tcmpset_32(&target_umutex->m_owner, owner,
                    (owner | TARGET_UMUTEX_CONTESTED)))
            break;

        /* owner has changed */
        __get_user(owner, &target_umutex->m_owner);
    }
    pthread_mutex_unlock(&umtx_wait_lck);
    addr = &target_umutex->m_owner;
    /* tcmpset_32 above writes to guest, and addr is just a key below */
    unlock_user_struct(target_umutex, target_addr, 0);

    return get_errno(safe__umtx_op(addr, UMTX_OP_WAKE_PRIVATE, 1, NULL,
        NULL));
#endif /* _UMTX_OPTIMIZED */
}
#endif /* UMTX_OP_MUTEX_WAKE2 */

abi_long freebsd_umtx_sem2_wait(abi_ulong obj, size_t tsz, void *t)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, obj, sizeof(struct target__usem2))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(obj, UMTX_OP_SEM2_WAIT, 1,
        (void *)(uintptr_t)tsz, t);
#else
    struct target__usem2 *t__usem2;
    uint32_t count, flags;
    uint32_t *addr;
    abi_long ret = 0;

    if (!lock_user_struct(VERIFY_WRITE, t__usem2, obj, 0)) {
        return -TARGET_EFAULT;
    }

    /*
     * Make sure the count field has the has USEM_HAS_WAITERS flag set
     * so userland will always call freebsd_umtx_sem2_wake().
     */
    for (;;) {
        __get_user(count, &t__usem2->_count);
        if (USEM_COUNT(count) != 0) {
            unlock_user_struct(t__usem2, obj, 1);
            return 0;
        }
        if ((count & USEM_HAS_WAITERS) != 0) {
            break;
        }
        if (tcmpset_32(&t__usem2->_count, count, (count | USEM_HAS_WAITERS))) {
            break;
        }
    }

    __get_user(flags, &t__usem2->_flags);
    addr = &t__usem2->_count;
    unlock_user_struct(t__usem2, obj, 1);

    if ((flags & USYNC_PROCESS_SHARED) == 0) {
        DEBUG_UMTX("<WAIT SEM2> %s: _umtx_op(%p, %d, %p)\n",
            __func__, addr, UMTX_OP_WAIT_UINT_PRIVATE, (int)tsz, t);

#if DETECT_DEADLOCK
        if (t != NULL) {
            ret = _umtx_wait_uint_private(addr, tswap32(USEM_HAS_WAITERS), tsz,
                t, __func__);
        } else {
            for (;;) {
                struct timespec ts;

                ts.tv_sec = 120;
                ts.tv_nsec = 0;

                ret = _umtx_wait_uint_private(addr, tswap32(USEM_HAS_WAITERS),
                                              0, (void *)&ts, __func__);
                if (ret == 0) {
                    break;
                }
                if (ret != -ETIMEDOUT) {
                    break;
                }
                if (!lock_user_struct(VERIFY_READ, t__usem2, obj, 1)) {
                    return -TARGET_EFAULT;
                }
                __get_user(count, &t__usem2->_count);
                unlock_user_struct(t__usem2, obj, 0);
                if (USEM_COUNT(count) != 0) {
                    fprintf(stderr, "QEMU:(%s) TIMEOUT (count!=0)\n", __func__);
                    ret = 0;
                    break;
                }
                if (ret == -ETIMEDOUT) {
                    fprintf(stderr, "QEMU:(%s) TIMEOUT (exiting)\n", __func__);
                    exit(-1);
                }
            }
        }
#else
        ret = _umtx_wait_uint_private(addr, tswap32(USEM_HAS_WAITERS),
                                      tsz, t, __func__);
#endif /* DETECT_DEADLOCK */
    } else {
        DEBUG_UMTX("<WAIT SEM2> %s: _umtx_op(%p, %d, %p)\n",
                   __func__, addr, UMTX_OP_WAIT_UINT, (int)tsz, t);
#if DETECT_DEADLOCK
        if (t != NULL) {
            ret = _umtx_wait_uint(addr, tswap32(USEM_HAS_WAITERS), tsz, t,
                                  __func__);
        } else {
            for (;;) {
                struct timespec ts;

                ts.tv_sec = 120;
                ts.tv_nsec = 0;

                ret = _umtx_wait_uint(addr, tswap32(USEM_HAS_WAITERS), 0,
                                      (void *)&ts, __func__);
                if (ret == 0) {
                    break;
                }
                if (ret != -ETIMEDOUT) {
                    break;
                }
                if (!lock_user_struct(VERIFY_READ, t__usem2, obj, 1)) {
                    return -TARGET_EFAULT;
                }
                __get_user(count, &t__usem2->_count);
                unlock_user_struct(t__usem2, obj, 0);
                if (USEM_COUNT(count) != 0) {
                    fprintf(stderr, "QEMU:(%s) TIMEOUT (count!=0)\n", __func__);
                    ret = 0;
                    break;
                }
                if (ret == -ETIMEDOUT) {
                    fprintf(stderr, "QEMU:(%s) TIMEOUT (exiting)\n", __func__);
                    exit(-1);
                }
            }
        }
#else
        ret = _umtx_wait_uint(addr, tswap32(USEM_HAS_WAITERS), tsz, t,
                              __func__);
#endif /* DETECT_DEADLOCK */
    }
    return ret;
#endif /* _UMTX_OPTIMIZED */
}

abi_long freebsd_umtx_sem2_wake(abi_ulong obj)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_READ, obj, sizeof(struct target__usem2))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(obj, UMTX_OP_SEM2_WAKE, 1, NULL, NULL);
#else
    struct target__usem2 *t__usem2;
    uint32_t *addr, flags;
    abi_long ret;

    if (!lock_user_struct(VERIFY_READ, t__usem2, obj, 1)) {
        return -TARGET_EFAULT;
    }

    __get_user(flags, &t__usem2->_flags);
    addr = &t__usem2->_count;
    unlock_user_struct(t__usem2, obj, 0);

    if ((flags & USYNC_PROCESS_SHARED) == 0) {
        DEBUG_UMTX("<WAKE SEM2> %s: _umtx_op(%p, %d, %d, NULL, NULL)\n",
                   __func__,  addr, UMTX_OP_WAKE_PRIVATE, INT_MAX);
        ret = get_errno(safe__umtx_op(addr, UMTX_OP_WAKE_PRIVATE, INT_MAX, NULL,
                                      NULL));
    } else {
        DEBUG_UMTX("<WAKE SEM2> %s: _umtx_op(%p, %d, %d, NULL, NULL)\n",
                   __func__,  addr, UMTX_OP_WAKE, INT_MAX);
        ret = get_errno(safe__umtx_op(addr, UMTX_OP_WAKE, INT_MAX, NULL, NULL));
    }

    return ret;
#endif /* _UMTX_OPTIMIZED */
}

abi_long freebsd_umtx_sem_wait(abi_ulong obj, size_t tsz, void *t)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, obj, sizeof(struct target__usem))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(obj, UMTX_OP_SEM_WAIT, 1,
        (void *)(uintptr_t)tsz, t);
#else
        struct target__usem *t__usem;
        uint32_t count, flags, *addr;
        abi_long ret;

        if (!lock_user_struct(VERIFY_WRITE, t__usem, obj, 0)) {
                return -TARGET_EFAULT;
        }

        __get_user(count, &t__usem->_count);
        if (count != 0) {
                unlock_user_struct(t__usem, obj, 1);
                return 0;
        }

        /*
         * Make sure the _has_waiters field is set so userland will always
         * call freebsd_umtx_sem_wake().
         */
        __put_user(1, &t__usem->_has_waiters);

        __get_user(flags, &t__usem->_flags);
        addr = &t__usem->_count;
        unlock_user_struct(t__usem, obj, 1);

        if ((flags &  USYNC_PROCESS_SHARED) == 0) {
            DEBUG_UMTX("<WAIT SEM> %s: _umtx_op(%p, %d, %d, NULL, NULL)\n",
                       __func__,  &t__usem->_count, UMTX_OP_WAKE_PRIVATE, 0);
            ret = _umtx_wait_uint_private(addr, 0, tsz, t, __func__);
        } else {
            DEBUG_UMTX("<WAIT SEM> %s: _umtx_op(%p, %d, %d, NULL, NULL)\n",
                       __func__,  &t__usem->_count, UMTX_OP_WAKE, 0);
            ret = _umtx_wait_uint(addr, 0, tsz, t, __func__);
        }

        return ret;
#endif /* _UMTX_OPTIMIZED */
}

abi_long freebsd_umtx_sem_wake(abi_ulong obj)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, obj, sizeof(struct target__usem))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(obj, UMTX_OP_SEM_WAKE, 1, NULL, NULL);
#else
    struct target__usem *t__usem;
        uint32_t flags, *addr;
        abi_long ret;

        if (!lock_user_struct(VERIFY_READ, t__usem, obj, 1)) {
                return -TARGET_EFAULT;
        }
        __get_user(flags, &t__usem->_flags);
        addr = &t__usem->_count;
        unlock_user_struct(t__usem, obj, 0);

        if ((flags & USYNC_PROCESS_SHARED) == 0) {
            DEBUG_UMTX("<WAKE SEM> %s: _umtx_op(%p, %d, %d, NULL, NULL)\n",
                       __func__,  &t__usem->_count, UMTX_OP_WAKE_PRIVATE, 1);
            ret = get_errno(safe__umtx_op(addr, UMTX_OP_WAKE_PRIVATE, 1, NULL,
                                          NULL));
        } else {
            DEBUG_UMTX("<WAKE SEM> %s: _umtx_op(%p, %d, %d, NULL, NULL)\n",
                       __func__,  &t__usem->_count, UMTX_OP_WAKE, 1);
            ret = get_errno(safe__umtx_op(addr, UMTX_OP_WAKE, 1, NULL, NULL));
        }

        return ret;
#endif /* _UMTX_OPTIMIZED */
}

abi_long t2h_freebsd_rtprio(struct rtprio *host_rtp, abi_ulong target_addr)
{
    struct target_freebsd_rtprio *target_rtp;

    if (!lock_user_struct(VERIFY_READ, target_rtp, target_addr, 1)) {
        return -TARGET_EFAULT;
    }
    __get_user(host_rtp->type, &target_rtp->type);
    __get_user(host_rtp->prio, &target_rtp->prio);
    unlock_user_struct(target_rtp, target_addr, 0);
    return 0;
}

abi_long h2t_freebsd_rtprio(abi_ulong target_addr, struct rtprio *host_rtp)
{
    struct target_freebsd_rtprio *target_rtp;

    if (!lock_user_struct(VERIFY_WRITE, target_rtp, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __put_user(host_rtp->type, &target_rtp->type);
    __put_user(host_rtp->prio, &target_rtp->prio);
    unlock_user_struct(target_rtp, target_addr, 1);
    return 0;
}

/* XXX We should never see this? OP_LOCK and OP_UNLOCK are now RESERVED{0,1} */
abi_long freebsd_lock_umtx(abi_ulong target_addr, abi_long id, size_t tsz,
        void *t)
{
    abi_long ret;
    abi_long owner;

    gemu_log("This is unreachable.");

    /*
     * XXX Note that memory at umtx_addr can change and so we need to be
     * careful and check for faults.
     */
    for (;;) {
        struct target_umtx *target_umtx;

        if (!lock_user_struct(VERIFY_WRITE, target_umtx, target_addr, 0)) {
            return -TARGET_EFAULT;
        }
        /* Check the simple uncontested case. */
        if (tcmpset_al(&target_umtx->u_owner,
                TARGET_UMTX_UNOWNED, id)) {
            unlock_user_struct(target_umtx, target_addr, 1);
            return 0;
        }
        /* Check to see if the lock is contested but free. */
        __get_user(owner, &target_umtx->u_owner);

        if (TARGET_UMTX_CONTESTED == owner) {
            if (tcmpset_al(&target_umtx->u_owner, TARGET_UMTX_CONTESTED,
                        id | TARGET_UMTX_CONTESTED)) {
                unlock_user_struct(target_umtx, target_addr, 1);
                return 0;
            }
            /* We failed because it changed on us, restart. */
            unlock_user_struct(target_umtx, target_addr, 1);
            continue;
        }

        /* Set the contested bit and sleep. */
        do {
            __get_user(owner, &target_umtx->u_owner);
            if (owner & TARGET_UMTX_CONTESTED) {
                break;
            }
        } while (!tcmpset_al(&target_umtx->u_owner, owner,
                    owner | TARGET_UMTX_CONTESTED));

        __get_user(owner, &target_umtx->u_owner);
        unlock_user_struct(target_umtx, target_addr, 1);

        /* Byte swap, if needed, to match what is stored in user mem. */
        owner = tswapal(owner);
        DEBUG_UMTX("<WAIT> %s: _umtx_op(%p, %d, 0x%llx, NULL, NULL)\n",
            __func__, g2h_untagged(target_addr), UMTX_OP_WAIT,
            (long long)owner);
        ret = _umtx_wait(g2h_untagged(target_addr), owner, tsz, t, __func__);
        if (is_error(ret)) {
            return ret;
        }
    }
}

/* XXX We should never see this? OP_LOCK and OP_UNLOCK are now RESERVED{0,1} */
abi_long freebsd_unlock_umtx(abi_ulong target_addr, abi_long id)
{
    abi_ulong owner;
    struct target_umtx *target_umtx;

    gemu_log("This is unreachable.");
    if (!lock_user_struct(VERIFY_WRITE, target_umtx, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    __get_user(owner, &target_umtx->u_owner);
    if ((owner & ~TARGET_UMTX_CONTESTED) != id) {
        unlock_user_struct(target_umtx, target_addr, 1);
        return -TARGET_EPERM;
    }
    /* Check the simple uncontested case. */
    if ((owner & ~TARGET_UMTX_CONTESTED) == 0) {
        if (tcmpset_al(&target_umtx->u_owner, owner,
            TARGET_UMTX_UNOWNED)) {
            unlock_user_struct(target_umtx, target_addr, 1);
            return 0;
        }
    }
    /* This is a contested lock. Unlock it. */
    __put_user(TARGET_UMTX_UNOWNED, &target_umtx->u_owner);
    unlock_user_struct(target_umtx, target_addr, 1);

    /* Wake up all those contesting it. */
    DEBUG_UMTX("<WAKE> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
            __func__, g2h_untagged(target_addr), UMTX_OP_WAKE, 0);
    return get_errno(safe__umtx_op(g2h_untagged(target_addr),
        QEMU_UMTX_OP(UMTX_OP_WAKE), 0, 0, 0));
}

abi_long freebsd_umtx_wake(abi_ulong target_addr, uint32_t n_wake)
{

    DEBUG_UMTX("<WAKE> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
            __func__, g2h_untagged(target_addr), UMTX_OP_WAKE, n_wake);
    return get_errno(safe__umtx_op(g2h_untagged(target_addr),
        QEMU_UMTX_OP(UMTX_OP_WAKE), n_wake, NULL, 0));
}

abi_long freebsd_umtx_wake_unsafe(abi_ulong target_addr, uint32_t n_wake)
{

    DEBUG_UMTX("<WAKE> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
            __func__, g2h_untagged(target_addr), UMTX_OP_WAKE, n_wake);
    return get_errno(_umtx_op(g2h_untagged(target_addr),
        QEMU_UMTX_OP(UMTX_OP_WAKE), n_wake, NULL, 0));
}

abi_long freebsd_umtx_mutex_wake(abi_ulong obj, abi_long val)
{

    DEBUG_UMTX("<WAKE> %s: _umtx_op(%p, %d, 0x%llx, NULL, NULL)\n",
            __func__, g2h_untagged(obj), UMTX_OP_WAKE, (long long)val);
    return get_errno(safe__umtx_op(g2h_untagged(obj),
        QEMU_UMTX_OP(UMTX_OP_MUTEX_WAKE), val, NULL, NULL));
}

abi_long freebsd_lock_umutex(abi_ulong target_addr, uint32_t id,
        void *ts, size_t tsz, int mode, abi_ulong val)
{
#ifdef _UMTX_OPTIMIZED
    int op;

    if (!access_ok(VERIFY_WRITE, target_addr, sizeof(struct target_umutex))) {
        return -TARGET_EFAULT;
    }

    switch (mode) {
    case TARGET_UMUTEX_WAIT:
        op = UMTX_OP_MUTEX_WAIT;
        DEBUG_UMTX("<MUTEX WAIT> %s: _umtx_op(%p, %d, 0x%llx, %p, %p)\n",
                __func__, g2h_untagged(target_addr), op, (long long)val,
                (void *)(uintptr_t)tsz, ts);
        break;
    case TARGET_UMUTEX_TRY:
        op = UMTX_OP_MUTEX_TRYLOCK;
        DEBUG_UMTX("<MUTEX TRYLOCK> %s: _umtx_op(%p, %d, 0x%llx, %p, %p)\n",
                __func__, g2h_untagged(target_addr), op, (long long)val,
                (void *)(uintptr_t)tsz, ts);
        break;
    default:
        op = UMTX_OP_MUTEX_LOCK;
        DEBUG_UMTX("<MUTEX LOCK> %s: _umtx_op(%p, %d, 0x%llx, %p, %p)\n",
                __func__, g2h_untagged(target_addr), op, (long long)val,
                (void *)(uintptr_t)tsz, ts);
        break;
    }

    return optimized_umtx_op(target_addr, op, val, (void *)(uintptr_t)tsz, ts);
#else
    struct target_umutex *target_umutex;
    uint32_t owner, flags, count, *addr;
    int ret = 0;

    if (!lock_user_struct(VERIFY_WRITE, target_umutex, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    for (;;) {

        __get_user(owner, &target_umutex->m_owner);

        if ((owner & ~TARGET_UMUTEX_CONTESTED) == 0) {
            /* Lock is unowned. */
            if (TARGET_UMUTEX_WAIT == mode) {
                /* Waiting on an unlocked mutex; bail out. */
                unlock_user_struct(target_umutex, target_addr, 1);
                return 0;
            }

            /* Attempt to acquire it, preserve the contested bit ("owner"). */
            while ((owner & ~TARGET_UMUTEX_CONTESTED) == 0 &&
                    !tcmpset_32(&target_umutex->m_owner, owner, owner | id)) {
                __get_user(owner, &target_umutex->m_owner);
            }

            if ((owner & ~TARGET_UMUTEX_CONTESTED) == 0) {
                /*
                 * The acquire succeeded, because we didn't observe owner with
                 * a different id.
                 */
                unlock_user_struct(target_umutex, target_addr, 1);
                return 0;
            }

            /* Otherwise, someone beat us to it; carry on. */
        }

        __get_user(flags, &target_umutex->m_flags);
        if ((flags & TARGET_UMUTEX_ERROR_CHECK) != 0 &&
                (owner & ~TARGET_UMUTEX_CONTESTED) == id) {
            unlock_user_struct(target_umutex, target_addr, 1);
            return -TARGET_EDEADLK;
        }

        if (TARGET_UMUTEX_TRY == mode) {
            unlock_user_struct(target_umutex, target_addr, 1);
            return -TARGET_EBUSY;
        }

        /* Set the contested bit and sleep. */
        while ((owner & TARGET_UMUTEX_CONTESTED) == 0) {
            if (tcmpset_32(&target_umutex->m_owner, owner,
                    owner | TARGET_UMUTEX_CONTESTED)) {
                /*
                 * Keep our local view of owner consistent with what we think
                 * we've set it to.  We're about to sleep on it, and we don't
                 * really want a spurious return from _umtx_op because of this.
                 */
                owner |= TARGET_UMUTEX_CONTESTED;

                break;
            } else {
                __get_user(owner, &target_umutex->m_owner);
            }
        }

        /*
         * If it changed during the above loop, we may be able to acquire now.
         */
        if ((owner & ~TARGET_UMUTEX_CONTESTED) == 0) {
            continue;
        }

        pthread_mutex_lock(&umtx_wait_lck);
        __get_user(count, &target_umutex->m_count);
        count++;
        __put_user(count, &target_umutex->m_count);
        pthread_mutex_unlock(&umtx_wait_lck);

        addr = &target_umutex->m_owner;

        /* addr used only as key below, not dereferenced */
        unlock_user_struct(target_umutex, target_addr, 1);

        DEBUG_UMTX("<WAIT UMUTEX> %s: _umtx_op(%p, %d, 0x%x, %d, %jx) "
            "count = %d\n", __func__, g2h_untagged(target_addr),
            UMTX_OP_WAIT_PRIVATE, tswap32(target_umutex->m_owner), tsz,
            (uintmax_t)ts, count);
        ret = _umtx_wait_uint_private(addr, owner, tsz, (void *)ts, __func__);

        if (!lock_user_struct(VERIFY_WRITE, target_umutex, target_addr, 0)) {
            return -TARGET_EFAULT;
        }

        pthread_mutex_lock(&umtx_wait_lck);
        __get_user(count, &target_umutex->m_count);
        count--;
        __put_user(count, &target_umutex->m_count);
        pthread_mutex_unlock(&umtx_wait_lck);
        if (ret != 0) {
            unlock_user_struct(target_umutex, target_addr, 1);
            break;
        }
    }
    return ret;
#endif  /* _UMTX_OPTIMIZED */
}

abi_long freebsd_unlock_umutex(abi_ulong target_addr, uint32_t id)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_addr, sizeof(struct target_umutex))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(target_addr, UMTX_OP_MUTEX_UNLOCK, 0, NULL, NULL);
#else
    struct target_umutex *target_umutex;
    uint32_t count, owner, *addr, flags;

    if (!lock_user_struct(VERIFY_WRITE, target_umutex, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    /* Make sure we own this mutex. */
    __get_user(owner, &target_umutex->m_owner);
    if ((owner & ~TARGET_UMUTEX_CONTESTED) != id) {
        unlock_user_struct(target_umutex, target_addr, 1);
        return -TARGET_EPERM;
    }
    pthread_mutex_lock(&umtx_wait_lck);
    __get_user(count, &target_umutex->m_count);

    /* Unlock it; set the contested bit as needed. */
    flags = TARGET_UMUTEX_UNOWNED;
    if (count > 1) {
        flags |= TARGET_UMUTEX_CONTESTED;
    }
    __put_user(flags, &target_umutex->m_owner);
    pthread_mutex_unlock(&umtx_wait_lck);

    addr = &target_umutex->m_owner;

    /* addr is used only as a key, so we can unlock before we use it below */
    unlock_user_struct(target_umutex, target_addr, 1);

    /*
     * And wake up any that may be contested it.  We used to only do this if the
     * lock wasn't contested coming in, but that could have changed in the
     * interim.  Unconditionally issue the wakeup, in conjunction with the
     * previous change of owner this should catch all cases.
     */
    DEBUG_UMTX("<WAKE> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
        __func__, g2h_untagged(target_addr), UMTX_OP_WAKE, 0);
    return get_errno(safe__umtx_op(addr, UMTX_OP_WAKE_PRIVATE, 1, NULL, NULL));
#endif  /* _UMTX_OPTIMIZED */
}

/*
 * wflags CVWAIT_CHECK_UNPARKING, CVWAIT_ABSTIME, CVWAIT_CLOCKID
 */
abi_long freebsd_cv_wait(abi_ulong target_ucond_addr,
        abi_ulong target_umtx_addr, struct timespec *ts, int wflags)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_ucond_addr,
        sizeof(struct target_ucond))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(target_ucond_addr, UMTX_OP_CV_WAIT, wflags,
        g2h_untagged(target_umtx_addr), ts);
#else
    abi_long ret;
    long tid;
    struct target_ucond *target_ucond;

    if (!lock_user_struct(VERIFY_WRITE, target_ucond, target_ucond_addr, 0)) {
        return -TARGET_EFAULT;
    }

    /* Check the clock ID if needed. */
    if ((wflags & TARGET_CVWAIT_CLOCKID) != 0) {
        uint32_t clockid;

        __get_user(clockid, &target_ucond->c_clockid);
        if (clockid >= CLOCK_THREAD_CPUTIME_ID) {
            /* Only HW clock id will work. */
            unlock_user_struct(target_ucond, target_ucond_addr, 1);
            return -TARGET_EINVAL;
        }
    }

    thr_self(&tid);

    /* Lock the _cv_mutex so we can safely unlock the user mutex */
    safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_LOCK, 0, NULL, NULL);

    /* Set c_has_waiters before releasing the user mutex! */
    __put_user(1, &target_ucond->c_has_waiters);

    /* unlock the user mutex */
    ret = freebsd_unlock_umutex(target_umtx_addr, tid);
    if (is_error(ret)) {
        safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_UNLOCK, 0, NULL, NULL);
        unlock_user_struct(target_ucond, target_ucond_addr, 1);
        return ret;
    }

    /* UMTX_OP_CV_WAIT unlocks _cv_mutex */
    DEBUG_UMTX("<CV_WAIT> %s: _umtx_op(%p, %d, 0x%x, %p, NULL)\n",
        __func__, g2h_untagged(target_ucond_addr), UMTX_OP_CV_WAIT, wflags,
        &_cv_mutex);
    ret = safe__umtx_op(g2h_untagged(target_ucond_addr), UMTX_OP_CV_WAIT,
                        wflags, &_cv_mutex, ts);

    if (is_error(ret)) {
        safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_UNLOCK, 0, NULL, NULL);
        unlock_user_struct(target_ucond, target_ucond_addr, 1);
        return ret;
    }
    ret = freebsd_lock_umutex(target_umtx_addr, tid, NULL, 0, TARGET_UMUTEX_TRY,
        0);
    safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_UNLOCK, 0, NULL, NULL);
    unlock_user_struct(target_ucond, target_ucond_addr, 1);

    return ret;
#endif  /* _UMTX_OPTIMIZED */
}

abi_long freebsd_cv_signal(abi_ulong target_ucond_addr)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_ucond_addr,
        sizeof(struct target_ucond))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(target_ucond_addr, UMTX_OP_CV_SIGNAL, 0, NULL,
        NULL);
#else
    abi_long ret;

    if (!access_ok(VERIFY_WRITE, target_ucond_addr,
                sizeof(struct target_ucond))) {
        return -TARGET_EFAULT;
    }

    /* Lock the _cv_mutex to prevent a race in do_cv_wait(). */
    safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_LOCK, 0, NULL, NULL);
    DEBUG_UMTX("<CV_SIGNAL> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
            __func__, g2h_untagged(target_ucond_addr), UMTX_OP_CV_SIGNAL, 0);
    ret = get_errno(safe__umtx_op(g2h_untagged(target_ucond_addr),
                                  UMTX_OP_CV_SIGNAL, 0, NULL, NULL));
    safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_UNLOCK, 0, NULL, NULL);

    return ret;
#endif /* _UMTX_OPTIMIZED */
}

abi_long freebsd_cv_broadcast(abi_ulong target_ucond_addr)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_ucond_addr,
        sizeof(struct target_ucond))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(target_ucond_addr, UMTX_OP_CV_BROADCAST, 0, NULL,
        NULL);
#else
    int ret;

    if (!access_ok(VERIFY_WRITE, target_ucond_addr,
                sizeof(struct target_ucond))) {
        return -TARGET_EFAULT;
    }

    /* Lock the _cv_mutex to prevent a race in do_cv_wait(). */
    safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_LOCK, 0, NULL, NULL);
    DEBUG_UMTX("<CV_BROADCAST> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
        __func__, g2h_untagged(target_ucond_addr), UMTX_OP_CV_BROADCAST, 0);
    ret = get_errno(safe__umtx_op(g2h_untagged(target_ucond_addr),
                                  UMTX_OP_CV_BROADCAST, 0, NULL, NULL));
    safe__umtx_op(&_cv_mutex, UMTX_OP_MUTEX_UNLOCK, 0, NULL, NULL);

    return ret;
#endif /* _UMTX_OPTIMIZED */
}

abi_long freebsd_rw_rdlock(abi_ulong target_addr, long fflag, size_t tsz,
        void *t)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_addr, sizeof(struct target_urwlock))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(target_addr, UMTX_OP_RW_RDLOCK, fflag,
        (void *)(uintptr_t)tsz, t);
#else
    struct target_urwlock *target_urwlock;
    uint32_t flags, wrflags;
    uint32_t state;
    uint32_t blocked_readers;
    abi_long ret;

    if (!lock_user_struct(VERIFY_WRITE, target_urwlock, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    __get_user(flags, &target_urwlock->rw_flags);
    wrflags = TARGET_URWLOCK_WRITE_OWNER;
    if (!(fflag & TARGET_URWLOCK_PREFER_READER) &&
            !(flags & TARGET_URWLOCK_PREFER_READER)) {
        wrflags |= TARGET_URWLOCK_WRITE_WAITERS;
    }
    for (;;) {
        __get_user(state, &target_urwlock->rw_state);
        /* try to lock it */
        while (!(state & wrflags)) {
            if (TARGET_URWLOCK_READER_COUNT(state) ==
                TARGET_URWLOCK_MAX_READERS) {
                unlock_user_struct(target_urwlock,
                    target_addr, 1);
                return -TARGET_EAGAIN;
            }
            if (tcmpset_32(&target_urwlock->rw_state, state,
                (state + 1))) {
                /* The acquired succeeded. */
                unlock_user_struct(target_urwlock,
                    target_addr, 1);
                return 0;
            }
            __get_user(state, &target_urwlock->rw_state);
        }
        /* set read contention bit */
        if (!tcmpset_32(&target_urwlock->rw_state, state,
            state | TARGET_URWLOCK_READ_WAITERS)) {
            /* The state has changed.  Start over. */
            continue;
        }

        /* contention bit is set, increase read waiter count */
        __get_user(blocked_readers, &target_urwlock->rw_blocked_readers);
        while (!tcmpset_32(&target_urwlock->rw_blocked_readers,
                    blocked_readers, blocked_readers + 1)) {
            __get_user(blocked_readers, &target_urwlock->rw_blocked_readers);
        }

        ret = 0;
        while (state & wrflags) {
            /* sleep/wait */
            unlock_user_struct(target_urwlock, target_addr, 1);
            DEBUG_UMTX("<WAIT> %s: _umtx_op(%p, %d, 0x%x (0x%x), NULL, NULL)\n",
                    __func__, &target_urwlock->rw_state,
                    UMTX_OP_WAIT_UINT, tswap32(state),
                    target_urwlock->rw_state);
            ret = _umtx_wait_uint(&target_urwlock->rw_state, tswap32(state),
                    tsz, t, __func__);
            if (is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_urwlock,
                                      target_addr, 0)) {
                    return ret;
                }
                goto rdlock_decrement;
            }
            if (!lock_user_struct(VERIFY_WRITE, target_urwlock, target_addr,
                        0)) {
                return -TARGET_EFAULT;
            }
            __get_user(state, &target_urwlock->rw_state);
        }

        /* decrease read waiter count */
rdlock_decrement:
        __get_user(blocked_readers, &target_urwlock->rw_blocked_readers);
        while (!tcmpset_32(&target_urwlock->rw_blocked_readers,
                    blocked_readers, (blocked_readers - 1))) {
            __get_user(blocked_readers, &target_urwlock->rw_blocked_readers);
        }
        if (blocked_readers == 1) {
            /* clear read contention bit */
            __get_user(state, &target_urwlock->rw_state);
            while (!tcmpset_32(&target_urwlock->rw_state, state,
                state & ~TARGET_URWLOCK_READ_WAITERS)) {
                __get_user(state, &target_urwlock->rw_state);
            }
        }
        if (is_error(ret)) {
            /* tcmpset_32 operates on the target memory, so don't double copy */
            unlock_user_struct(target_urwlock, target_addr, 0);
            return ret;
        }
    }
#endif /* _UMTX_OPTIMIZED */
}

abi_long freebsd_rw_wrlock(abi_ulong target_addr, long fflag, size_t tsz,
        void *t)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_addr, sizeof(struct target_urwlock))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(target_addr, UMTX_OP_RW_WRLOCK, fflag,
        (void *)(uintptr_t)tsz, t);
#else
    struct target_urwlock *target_urwlock;
    uint32_t blocked_readers, blocked_writers;
    uint32_t state;
    abi_long ret;

    if (!lock_user_struct(VERIFY_WRITE, target_urwlock, target_addr, 0)) {
        return -TARGET_EFAULT;
    }
    blocked_readers = 0;
    for (;;) {
        __get_user(state, &target_urwlock->rw_state);
        while (!(state & TARGET_URWLOCK_WRITE_OWNER) &&
            TARGET_URWLOCK_READER_COUNT(state) == 0) {
            if (tcmpset_32(&target_urwlock->rw_state, state,
                        state | TARGET_URWLOCK_WRITE_OWNER)) {
                unlock_user_struct(target_urwlock, target_addr, 1);
                return 0;
            }
            __get_user(state, &target_urwlock->rw_state);
        }

        if (!(state & (TARGET_URWLOCK_WRITE_OWNER |
                        TARGET_URWLOCK_WRITE_WAITERS)) &&
                blocked_readers != 0) {
            DEBUG_UMTX("<WAKE> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
                __func__, &target_urwlock->rw_state, UMTX_OP_WAKE,
                tswap32(state));
            ret = get_errno(safe__umtx_op(&target_urwlock->rw_state,
                UMTX_OP_WAKE, INT_MAX, NULL, NULL));
            unlock_user_struct(target_urwlock, target_addr, ret == 0);
            return ret;
        }
        /* re-read the state */
        __get_user(state, &target_urwlock->rw_state);

        /* and set TARGET_URWLOCK_WRITE_WAITERS */
        while (((state & TARGET_URWLOCK_WRITE_OWNER) ||
                    TARGET_URWLOCK_READER_COUNT(state) != 0) &&
                (state & TARGET_URWLOCK_WRITE_WAITERS) == 0) {
            if (tcmpset_32(&target_urwlock->rw_state, state,
                        state | TARGET_URWLOCK_WRITE_WAITERS)) {
                break;
            }
            __get_user(state, &target_urwlock->rw_state);
        }

        /* contention bit is set, increase write waiter count */
        __get_user(blocked_writers, &target_urwlock->rw_blocked_writers);
        while (!tcmpset_32(&target_urwlock->rw_blocked_writers,
                    blocked_writers, blocked_writers + 1)) {
            __get_user(blocked_writers, &target_urwlock->rw_blocked_writers);
        }

        /* sleep */
        ret = 0;
        while ((state & TARGET_URWLOCK_WRITE_OWNER) ||
                (TARGET_URWLOCK_READER_COUNT(state) != 0)) {
            unlock_user_struct(target_urwlock, target_addr, 1);
            DEBUG_UMTX("<WAIT> %s: _umtx_op(%p, %d, 0x%x(0x%x), NULL, NULL)\n",
                    __func__, &target_urwlock->rw_blocked_writers,
                    UMTX_OP_WAIT_UINT, tswap32(state),
                    target_urwlock->rw_state);
            ret = _umtx_wait_uint(&target_urwlock->rw_state,
                        tswap32(state), tsz, t, __func__);
            if (is_error(ret)) {
                if (!lock_user_struct(VERIFY_WRITE, target_urwlock,
                                      target_addr, 0)) {
                    return ret;
                }
                goto wrlock_decrement;
            }
            if (!lock_user_struct(VERIFY_WRITE, target_urwlock, target_addr,
                        0)) {
                return -TARGET_EFAULT;
            }
            __get_user(state, &target_urwlock->rw_state);
        }

        /* decrease the write waiter count */
wrlock_decrement:
        __get_user(blocked_writers, &target_urwlock->rw_blocked_writers);
        while (!tcmpset_32(&target_urwlock->rw_blocked_writers,
                    blocked_writers, (blocked_writers - 1))) {
            __get_user(blocked_writers, &target_urwlock->rw_blocked_writers);
        }
        if (blocked_writers == 1) {
            /* clear write contention bit */
            __get_user(state, &target_urwlock->rw_state);
            while (!tcmpset_32(&target_urwlock->rw_state, state,
                        state & ~TARGET_URWLOCK_WRITE_WAITERS)) {
                __get_user(state, &target_urwlock->rw_state);
            }
            __get_user(blocked_readers, &target_urwlock->rw_blocked_readers);
        } else {
            blocked_readers = 0;
        }
        if (is_error(ret)) {
            /* tcmpset_32 operates on the target memory, so don't double copy */
            unlock_user_struct(target_urwlock, target_addr, 0);
            return ret;
        }
    }
#endif /* _UMTX_OPTIMIZED */
}

abi_long freebsd_rw_unlock(abi_ulong target_addr)
{
#ifdef _UMTX_OPTIMIZED
    if (!access_ok(VERIFY_WRITE, target_addr, sizeof(struct target_urwlock))) {
        return -TARGET_EFAULT;
    }

    return optimized_umtx_op(target_addr, UMTX_OP_RW_UNLOCK, 0, NULL, NULL);
#else
    struct target_urwlock *target_urwlock;
    uint32_t flags, state, count = 0;

    if (!lock_user_struct(VERIFY_WRITE, target_urwlock, target_addr, 0)) {
        return -TARGET_EFAULT;
    }

    __get_user(flags, &target_urwlock->rw_flags);
    __get_user(state, &target_urwlock->rw_state);

    if (state & TARGET_URWLOCK_WRITE_OWNER) {
        for (;;) {
            if (!tcmpset_32(&target_urwlock->rw_state, state,
                state & ~TARGET_URWLOCK_WRITE_OWNER)) {
                /*
                 * Update the state here because we want to make sure that
                 * another thread didn't unste the flag from underneath us.
                 * If they did, we throw EPERM as the kernel does.
                 */
                __get_user(state, &target_urwlock->rw_state);
                if (!(state & TARGET_URWLOCK_WRITE_OWNER)) {
                    unlock_user_struct(target_urwlock, target_addr, 1);
                    return -TARGET_EPERM;
                }
            } else {
                break;
            }
        }
    } else if (TARGET_URWLOCK_READER_COUNT(state) != 0) {
        /* decrement reader count */
        for (;;) {
            if (!tcmpset_32(&target_urwlock->rw_state, state, (state  - 1))) {
                /*
                 * Just as in the branch above; we update the state here because
                 * we want to make sure the reader count didn't hit 0 while we
                 * are still trying to decrement this. The kernel also returns
                 * EPERM here.
                 */
                __get_user(state, &target_urwlock->rw_state);
                if (TARGET_URWLOCK_READER_COUNT(state) == 0) {
                    unlock_user_struct(target_urwlock, target_addr, 1);
                    return -TARGET_EPERM;
                 }
            } else {
                break;
            }
        }
    } else {
        unlock_user_struct(target_urwlock, target_addr, 1);
        return -TARGET_EPERM;
    }

    if (!(flags & TARGET_URWLOCK_PREFER_READER)) {
        if (state & TARGET_URWLOCK_WRITE_WAITERS) {
            count = 1;
        } else if (state & TARGET_URWLOCK_READ_WAITERS) {
            count = INT_MAX;
        }
    } else {
        if (state & TARGET_URWLOCK_READ_WAITERS) {
            count = INT_MAX;
        } else if (state & TARGET_URWLOCK_WRITE_WAITERS) {
            count = 1;
        }
    }

    /* rw_state used below as key only */
    unlock_user_struct(target_urwlock, target_addr, 1);
    if (count != 0) {
        DEBUG_UMTX("<WAKE> %s: _umtx_op(%p, %d, 0x%x, NULL, NULL)\n",
                __func__, &target_urwlock->rw_state, UMTX_OP_WAKE, count);
        return get_errno(safe__umtx_op(&target_urwlock->rw_state, UMTX_OP_WAKE,
                    count, NULL, NULL));
    } else {
        return 0;
    }
#endif  /* _UMTX_OPTIMIZED */
}

abi_long
freebsd_umtx_shm(abi_ulong target_addr, long fflag)
{

    return get_errno(safe__umtx_op(NULL, QEMU_UMTX_OP(UMTX_OP_SHM), fflag,
        g2h_untagged(target_addr), NULL));
}

abi_long
freebsd_umtx_robust_list(abi_ulong target_addr, size_t rbsize)
{
#ifdef _UMTX_OPTIMIZED
    struct target_umtx_robust_lists_params *tparams;
    struct umtx_robust_lists_params hparams;
    abi_long error;

    if (rbsize < sizeof(*tparams)) {
        return -TARGET_EINVAL;
    }

    if (!lock_user_struct(VERIFY_READ, tparams, target_addr, 1)) {
        return -TARGET_EFAULT;
    }

    hparams.robust_list_offset =
        (uintptr_t)g2h_untagged(tparams->robust_list_offset);
    hparams.robust_priv_list_offset =
        (uintptr_t)g2h_untagged(tparams->robust_priv_list_offset);
    hparams.robust_inact_offset =
        (uintptr_t)g2h_untagged(tparams->robust_inact_offset);

    error = optimized_umtx_op(0, UMTX_OP_ROBUST_LISTS, sizeof(hparams),
        &hparams, NULL);
    unlock_user_struct(tparams, target_addr, 0);
    return error;
#else
    gemu_log("safe__umtx_op(..., UMTX_OP_ROBUST_LISTS. ...)  not supported\n");
    return -TARGET_EOPNOTSUPP;
#endif
}

abi_long do_freebsd_thr_new(CPUArchState *env,
        abi_ulong target_param_addr, int32_t param_size)
{
    new_freebsd_thread_info_t info;
    pthread_attr_t attr;
    TaskState *ts;
    CPUArchState *new_env;
    CPUState *new_cpu;
    struct target_freebsd_thr_param *target_param;
    abi_ulong target_rtp_addr;
    struct target_freebsd_rtprio *target_rtp;
    struct rtprio *rtp_ptr, rtp;
    CPUState *cpu = env_cpu(env);
    TaskState *parent_ts = (TaskState *)cpu->opaque;
    sigset_t sigmask;
    struct sched_param sched_param;
    int sched_policy;
    int ret = 0;

    memset(&info, 0, sizeof(info));

    if (!lock_user_struct(VERIFY_READ, target_param, target_param_addr, 1)) {
        return -TARGET_EFAULT;
    }
    info.param.start_func = tswapal(target_param->start_func);
    info.param.arg = tswapal(target_param->arg);
    info.param.stack_base = tswapal(target_param->stack_base);
    info.param.stack_size = tswapal(target_param->stack_size);
    info.param.tls_base = tswapal(target_param->tls_base);
    info.param.tls_size = tswapal(target_param->tls_size);
    info.param.child_tid = tswapal(target_param->child_tid);
    info.param.parent_tid = tswapal(target_param->parent_tid);
    info.param.flags = tswap32(target_param->flags);
    target_rtp_addr = info.param.rtp = tswapal(target_param->rtp);
    unlock_user_struct(target_param, target_param_addr, 0);

    thr_self(&info.parent_tid);

    if (target_rtp_addr) {
        if (!lock_user_struct(VERIFY_READ, target_rtp, target_rtp_addr, 1)) {
            return -TARGET_EFAULT;
        }
        rtp.type = tswap16(target_rtp->type);
        rtp.prio = tswap16(target_rtp->prio);
        unlock_user_struct(target_rtp, target_rtp_addr, 0);
        rtp_ptr = &rtp;
    } else {
        rtp_ptr = NULL;
    }

    /* Create a new CPU instance. */
    ts = g_malloc0(sizeof(TaskState));
    init_task_state(ts);

    /* Grab a mutex so that thread setup appears atomic. */
    pthread_mutex_lock(new_freebsd_thread_lock_ptr);

    /*
     * If this is our first additional thread, we need to ensure we
     * generate code for parallel execution and flush old translations.
     * Do this now so that the copy gets CF_PARALLEL too.
     */
    if (!(cpu->tcg_cflags & CF_PARALLEL)) {
        cpu->tcg_cflags |= CF_PARALLEL;
        tb_flush__exclusive_or_serial();
    }

    new_env = cpu_copy(env);

    new_cpu = env_cpu(new_env);
    new_cpu->opaque = ts;
    ts->bprm = parent_ts->bprm;
    ts->info = parent_ts->info;
    ts->signal_mask = parent_ts->signal_mask;
    ts->ts_tid = qemu_get_thread_id();

    pthread_mutex_init(&info.mutex, NULL);
    pthread_mutex_lock(&info.mutex);
    pthread_cond_init(&info.cond, NULL);
    info.env = new_env;

    /* XXX check return values... */
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, NEW_STACK_SIZE);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (rtp_ptr) {
        rtp_to_schedparam(&rtp, &sched_policy, &sched_param);
        pthread_attr_setschedpolicy(&attr, sched_policy);
        pthread_attr_setschedparam(&attr, &sched_param);
    }

    /*
     * It is not safe to deliver signals until the child has finished
     * initializing, so temporarily block all signals.
     */
    sigfillset(&sigmask);
    sigprocmask(SIG_BLOCK, &sigmask, &info.sigmask);

    ret = pthread_create(&info.thread, &attr, new_freebsd_thread_start, &info);

    sigprocmask(SIG_SETMASK, &info.sigmask, NULL);
    pthread_attr_destroy(&attr);
    if (ret == 0) {
        /* Wait for the child to initialize. */
        pthread_cond_wait(&info.cond, &info.mutex);
    } else {
        /* Creation of new thread failed. */
        object_unparent(OBJECT(new_cpu));
        object_unref(OBJECT(new_cpu));
        g_free(ts);
        ret = -host_to_target_errno(ret);
    }

    pthread_mutex_unlock(&info.mutex);
    pthread_cond_destroy(&info.cond);
    pthread_mutex_destroy(&info.mutex);
    pthread_mutex_unlock(new_freebsd_thread_lock_ptr);

    return ret;
}
