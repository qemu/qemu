/*
 * Wrappers around mutex/cond/thread functions
 *
 * Copyright Red Hat, Inc. 2009
 *
 * Author:
 *  Marcelo Tosatti <mtosatti@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/atomic.h"
#include "qemu/notify.h"
#include "qemu-thread-common.h"
#include "qemu/tsan.h"
#include "qemu/bitmap.h"

#ifdef CONFIG_PTHREAD_SET_NAME_NP
#include <pthread_np.h>
#endif

static bool name_threads;

void qemu_thread_naming(bool enable)
{
    name_threads = enable;

#if !defined CONFIG_PTHREAD_SETNAME_NP_W_TID && \
    !defined CONFIG_PTHREAD_SETNAME_NP_WO_TID && \
    !defined CONFIG_PTHREAD_SET_NAME_NP
    /* This is a debugging option, not fatal */
    if (enable) {
        fprintf(stderr, "qemu: thread naming not supported on this host\n");
    }
#endif
}

static void error_exit(int err, const char *msg)
{
    fprintf(stderr, "qemu: %s: %s\n", msg, strerror(err));
    abort();
}

static inline clockid_t qemu_timedwait_clockid(void)
{
#ifdef CONFIG_PTHREAD_CONDATTR_SETCLOCK
    return CLOCK_MONOTONIC;
#else
    return CLOCK_REALTIME;
#endif
}

static void compute_abs_deadline(struct timespec *ts, int ms)
{
    clock_gettime(qemu_timedwait_clockid(), ts);
    ts->tv_nsec += (ms % 1000) * 1000000;
    ts->tv_sec += ms / 1000;
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
}

void qemu_mutex_init(QemuMutex *mutex)
{
    int err;

    err = pthread_mutex_init(&mutex->lock, NULL);
    if (err)
        error_exit(err, __func__);
    qemu_mutex_post_init(mutex);
}

void qemu_mutex_destroy(QemuMutex *mutex)
{
    int err;

    assert(mutex->initialized);
    mutex->initialized = false;
    err = pthread_mutex_destroy(&mutex->lock);
    if (err)
        error_exit(err, __func__);
}

void qemu_mutex_lock_impl(QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(mutex->initialized);
    qemu_mutex_pre_lock(mutex, file, line);
    err = pthread_mutex_lock(&mutex->lock);
    if (err)
        error_exit(err, __func__);
    qemu_mutex_post_lock(mutex, file, line);
}

int qemu_mutex_trylock_impl(QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(mutex->initialized);
    err = pthread_mutex_trylock(&mutex->lock);
    if (err == 0) {
        qemu_mutex_post_lock(mutex, file, line);
        return 0;
    }
    if (err != EBUSY) {
        error_exit(err, __func__);
    }
    return -EBUSY;
}

void qemu_mutex_unlock_impl(QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(mutex->initialized);
    qemu_mutex_pre_unlock(mutex, file, line);
    err = pthread_mutex_unlock(&mutex->lock);
    if (err)
        error_exit(err, __func__);
}

void qemu_rec_mutex_init(QemuRecMutex *mutex)
{
    int err;
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    err = pthread_mutex_init(&mutex->m.lock, &attr);
    pthread_mutexattr_destroy(&attr);
    if (err) {
        error_exit(err, __func__);
    }
    mutex->m.initialized = true;
}

void qemu_rec_mutex_destroy(QemuRecMutex *mutex)
{
    qemu_mutex_destroy(&mutex->m);
}

void qemu_rec_mutex_lock_impl(QemuRecMutex *mutex, const char *file, int line)
{
    qemu_mutex_lock_impl(&mutex->m, file, line);
}

int qemu_rec_mutex_trylock_impl(QemuRecMutex *mutex, const char *file, int line)
{
    return qemu_mutex_trylock_impl(&mutex->m, file, line);
}

void qemu_rec_mutex_unlock_impl(QemuRecMutex *mutex, const char *file, int line)
{
    qemu_mutex_unlock_impl(&mutex->m, file, line);
}

void qemu_cond_init(QemuCond *cond)
{
    pthread_condattr_t attr;
    int err;

    err = pthread_condattr_init(&attr);
    if (err) {
        error_exit(err, __func__);
    }
#ifdef CONFIG_PTHREAD_CONDATTR_SETCLOCK
    err = pthread_condattr_setclock(&attr, qemu_timedwait_clockid());
    if (err) {
        error_exit(err, __func__);
    }
#endif
    err = pthread_cond_init(&cond->cond, &attr);
    if (err) {
        error_exit(err, __func__);
    }
    err = pthread_condattr_destroy(&attr);
    if (err) {
        error_exit(err, __func__);
    }
    cond->initialized = true;
}

void qemu_cond_destroy(QemuCond *cond)
{
    int err;

    assert(cond->initialized);
    cond->initialized = false;
    err = pthread_cond_destroy(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_signal(QemuCond *cond)
{
    int err;

    assert(cond->initialized);
    err = pthread_cond_signal(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_broadcast(QemuCond *cond)
{
    int err;

    assert(cond->initialized);
    err = pthread_cond_broadcast(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_wait_impl(QemuCond *cond, QemuMutex *mutex, const char *file, const int line)
{
    int err;

    assert(cond->initialized);
    qemu_mutex_pre_unlock(mutex, file, line);
    err = pthread_cond_wait(&cond->cond, &mutex->lock);
    qemu_mutex_post_lock(mutex, file, line);
    if (err)
        error_exit(err, __func__);
}

static bool TSA_NO_TSA
qemu_cond_timedwait_ts(QemuCond *cond, QemuMutex *mutex, struct timespec *ts,
                       const char *file, const int line)
{
    int err;

    assert(cond->initialized);
    trace_qemu_mutex_unlock(mutex, file, line);
    err = pthread_cond_timedwait(&cond->cond, &mutex->lock, ts);
    trace_qemu_mutex_locked(mutex, file, line);
    if (err && err != ETIMEDOUT) {
        error_exit(err, __func__);
    }
    return err != ETIMEDOUT;
}

bool qemu_cond_timedwait_impl(QemuCond *cond, QemuMutex *mutex, int ms,
                              const char *file, const int line)
{
    struct timespec ts;

    compute_abs_deadline(&ts, ms);
    return qemu_cond_timedwait_ts(cond, mutex, &ts, file, line);
}

void qemu_sem_init(QemuSemaphore *sem, int init)
{
    qemu_mutex_init(&sem->mutex);
    qemu_cond_init(&sem->cond);

    if (init < 0) {
        error_exit(EINVAL, __func__);
    }
    sem->count = init;
}

void qemu_sem_destroy(QemuSemaphore *sem)
{
    qemu_cond_destroy(&sem->cond);
    qemu_mutex_destroy(&sem->mutex);
}

void qemu_sem_post(QemuSemaphore *sem)
{
    qemu_mutex_lock(&sem->mutex);
    if (sem->count == UINT_MAX) {
        error_exit(EINVAL, __func__);
    } else {
        sem->count++;
        qemu_cond_signal(&sem->cond);
    }
    qemu_mutex_unlock(&sem->mutex);
}

int qemu_sem_timedwait(QemuSemaphore *sem, int ms)
{
    bool rc = true;
    struct timespec ts;

    compute_abs_deadline(&ts, ms);
    qemu_mutex_lock(&sem->mutex);
    while (sem->count == 0) {
        if (ms == 0) {
            rc = false;
        } else {
            rc = qemu_cond_timedwait_ts(&sem->cond, &sem->mutex, &ts,
                                        __FILE__, __LINE__);
        }
        if (!rc) { /* timeout */
            break;
        }
    }
    if (rc) {
        --sem->count;
    }
    qemu_mutex_unlock(&sem->mutex);
    return (rc ? 0 : -1);
}

void qemu_sem_wait(QemuSemaphore *sem)
{
    qemu_mutex_lock(&sem->mutex);
    while (sem->count == 0) {
        qemu_cond_wait(&sem->cond, &sem->mutex);
    }
    --sem->count;
    qemu_mutex_unlock(&sem->mutex);
}

#ifdef __linux__
#include "qemu/futex.h"
#else
static inline void qemu_futex_wake(QemuEvent *ev, int n)
{
    assert(ev->initialized);
    pthread_mutex_lock(&ev->lock);
    if (n == 1) {
        pthread_cond_signal(&ev->cond);
    } else {
        pthread_cond_broadcast(&ev->cond);
    }
    pthread_mutex_unlock(&ev->lock);
}

static inline void qemu_futex_wait(QemuEvent *ev, unsigned val)
{
    assert(ev->initialized);
    pthread_mutex_lock(&ev->lock);
    if (ev->value == val) {
        pthread_cond_wait(&ev->cond, &ev->lock);
    }
    pthread_mutex_unlock(&ev->lock);
}
#endif

/* Valid transitions:
 * - free->set, when setting the event
 * - busy->set, when setting the event, followed by qemu_futex_wake
 * - set->free, when resetting the event
 * - free->busy, when waiting
 *
 * set->busy does not happen (it can be observed from the outside but
 * it really is set->free->busy).
 *
 * busy->free provably cannot happen; to enforce it, the set->free transition
 * is done with an OR, which becomes a no-op if the event has concurrently
 * transitioned to free or busy.
 */

#define EV_SET         0
#define EV_FREE        1
#define EV_BUSY       -1

void qemu_event_init(QemuEvent *ev, bool init)
{
#ifndef __linux__
    pthread_mutex_init(&ev->lock, NULL);
    pthread_cond_init(&ev->cond, NULL);
#endif

    ev->value = (init ? EV_SET : EV_FREE);
    ev->initialized = true;
}

void qemu_event_destroy(QemuEvent *ev)
{
    assert(ev->initialized);
    ev->initialized = false;
#ifndef __linux__
    pthread_mutex_destroy(&ev->lock);
    pthread_cond_destroy(&ev->cond);
#endif
}

void qemu_event_set(QemuEvent *ev)
{
    assert(ev->initialized);

    /*
     * Pairs with both qemu_event_reset() and qemu_event_wait().
     *
     * qemu_event_set has release semantics, but because it *loads*
     * ev->value we need a full memory barrier here.
     */
    smp_mb();
    if (qatomic_read(&ev->value) != EV_SET) {
        int old = qatomic_xchg(&ev->value, EV_SET);

        /* Pairs with memory barrier in kernel futex_wait system call.  */
        smp_mb__after_rmw();
        if (old == EV_BUSY) {
            /* There were waiters, wake them up.  */
            qemu_futex_wake(ev, INT_MAX);
        }
    }
}

void qemu_event_reset(QemuEvent *ev)
{
    assert(ev->initialized);

    /*
     * If there was a concurrent reset (or even reset+wait),
     * do nothing.  Otherwise change EV_SET->EV_FREE.
     */
    qatomic_or(&ev->value, EV_FREE);

    /*
     * Order reset before checking the condition in the caller.
     * Pairs with the first memory barrier in qemu_event_set().
     */
    smp_mb__after_rmw();
}

void qemu_event_wait(QemuEvent *ev)
{
    unsigned value;

    assert(ev->initialized);

    /*
     * qemu_event_wait must synchronize with qemu_event_set even if it does
     * not go down the slow path, so this load-acquire is needed that
     * synchronizes with the first memory barrier in qemu_event_set().
     *
     * If we do go down the slow path, there is no requirement at all: we
     * might miss a qemu_event_set() here but ultimately the memory barrier in
     * qemu_futex_wait() will ensure the check is done correctly.
     */
    value = qatomic_load_acquire(&ev->value);
    if (value != EV_SET) {
        if (value == EV_FREE) {
            /*
             * Leave the event reset and tell qemu_event_set that there are
             * waiters.  No need to retry, because there cannot be a concurrent
             * busy->free transition.  After the CAS, the event will be either
             * set or busy.
             *
             * This cmpxchg doesn't have particular ordering requirements if it
             * succeeds (moving the store earlier can only cause qemu_event_set()
             * to issue _more_ wakeups), the failing case needs acquire semantics
             * like the load above.
             */
            if (qatomic_cmpxchg(&ev->value, EV_FREE, EV_BUSY) == EV_SET) {
                return;
            }
        }

        /*
         * This is the final check for a concurrent set, so it does need
         * a smp_mb() pairing with the second barrier of qemu_event_set().
         * The barrier is inside the FUTEX_WAIT system call.
         */
        qemu_futex_wait(ev, EV_BUSY);
    }
}

static __thread NotifierList thread_exit;

/*
 * Note that in this implementation you can register a thread-exit
 * notifier for the main thread, but it will never be called.
 * This is OK because main thread exit can only happen when the
 * entire process is exiting, and the API allows notifiers to not
 * be called on process exit.
 */
void qemu_thread_atexit_add(Notifier *notifier)
{
    notifier_list_add(&thread_exit, notifier);
}

void qemu_thread_atexit_remove(Notifier *notifier)
{
    notifier_remove(notifier);
}

static void qemu_thread_atexit_notify(void *arg)
{
    /*
     * Called when non-main thread exits (via qemu_thread_exit()
     * or by returning from its start routine.)
     */
    notifier_list_notify(&thread_exit, NULL);
}

typedef struct {
    void *(*start_routine)(void *);
    void *arg;
    char *name;
} QemuThreadArgs;

static void *qemu_thread_start(void *args)
{
    QemuThreadArgs *qemu_thread_args = args;
    void *(*start_routine)(void *) = qemu_thread_args->start_routine;
    void *arg = qemu_thread_args->arg;
    void *r;

    /* Attempt to set the threads name; note that this is for debug, so
     * we're not going to fail if we can't set it.
     */
    if (name_threads && qemu_thread_args->name) {
# if defined(CONFIG_PTHREAD_SETNAME_NP_W_TID)
        pthread_setname_np(pthread_self(), qemu_thread_args->name);
# elif defined(CONFIG_PTHREAD_SETNAME_NP_WO_TID)
        pthread_setname_np(qemu_thread_args->name);
# elif defined(CONFIG_PTHREAD_SET_NAME_NP)
        pthread_set_name_np(pthread_self(), qemu_thread_args->name);
# endif
    }
    QEMU_TSAN_ANNOTATE_THREAD_NAME(qemu_thread_args->name);
    g_free(qemu_thread_args->name);
    g_free(qemu_thread_args);

    /*
     * GCC 11 with glibc 2.17 on PowerPC reports
     *
     * qemu-thread-posix.c:540:5: error: ‘__sigsetjmp’ accessing 656 bytes
     *   in a region of size 528 [-Werror=stringop-overflow=]
     * 540 |     pthread_cleanup_push(qemu_thread_atexit_notify, NULL);
     *     |     ^~~~~~~~~~~~~~~~~~~~
     *
     * which is clearly nonsense.
     */
#pragma GCC diagnostic push
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

    pthread_cleanup_push(qemu_thread_atexit_notify, NULL);
    r = start_routine(arg);
    pthread_cleanup_pop(1);

#pragma GCC diagnostic pop

    return r;
}

void qemu_thread_create(QemuThread *thread, const char *name,
                       void *(*start_routine)(void*),
                       void *arg, int mode)
{
    sigset_t set, oldset;
    int err;
    pthread_attr_t attr;
    QemuThreadArgs *qemu_thread_args;

    err = pthread_attr_init(&attr);
    if (err) {
        error_exit(err, __func__);
    }

    if (mode == QEMU_THREAD_DETACHED) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

    /* Leave signal handling to the iothread.  */
    sigfillset(&set);
    /* Blocking the signals can result in undefined behaviour. */
    sigdelset(&set, SIGSEGV);
    sigdelset(&set, SIGFPE);
    sigdelset(&set, SIGILL);
    /* TODO avoid SIGBUS loss on macOS */
    pthread_sigmask(SIG_SETMASK, &set, &oldset);

    qemu_thread_args = g_new0(QemuThreadArgs, 1);
    qemu_thread_args->name = g_strdup(name);
    qemu_thread_args->start_routine = start_routine;
    qemu_thread_args->arg = arg;

    err = pthread_create(&thread->thread, &attr,
                         qemu_thread_start, qemu_thread_args);

    if (err)
        error_exit(err, __func__);

    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    pthread_attr_destroy(&attr);
}

int qemu_thread_set_affinity(QemuThread *thread, unsigned long *host_cpus,
                             unsigned long nbits)
{
#if defined(CONFIG_PTHREAD_AFFINITY_NP)
    const size_t setsize = CPU_ALLOC_SIZE(nbits);
    unsigned long value;
    cpu_set_t *cpuset;
    int err;

    cpuset = CPU_ALLOC(nbits);
    g_assert(cpuset);

    CPU_ZERO_S(setsize, cpuset);
    value = find_first_bit(host_cpus, nbits);
    while (value < nbits) {
        CPU_SET_S(value, setsize, cpuset);
        value = find_next_bit(host_cpus, nbits, value + 1);
    }

    err = pthread_setaffinity_np(thread->thread, setsize, cpuset);
    CPU_FREE(cpuset);
    return err;
#else
    return -ENOSYS;
#endif
}

int qemu_thread_get_affinity(QemuThread *thread, unsigned long **host_cpus,
                             unsigned long *nbits)
{
#if defined(CONFIG_PTHREAD_AFFINITY_NP)
    unsigned long tmpbits;
    cpu_set_t *cpuset;
    size_t setsize;
    int i, err;

    tmpbits = CPU_SETSIZE;
    while (true) {
        setsize = CPU_ALLOC_SIZE(tmpbits);
        cpuset = CPU_ALLOC(tmpbits);
        g_assert(cpuset);

        err = pthread_getaffinity_np(thread->thread, setsize, cpuset);
        if (err) {
            CPU_FREE(cpuset);
            if (err != -EINVAL) {
                return err;
            }
            tmpbits *= 2;
        } else {
            break;
        }
    }

    /* Convert the result into a proper bitmap. */
    *nbits = tmpbits;
    *host_cpus = bitmap_new(tmpbits);
    for (i = 0; i < tmpbits; i++) {
        if (CPU_ISSET(i, cpuset)) {
            set_bit(i, *host_cpus);
        }
    }
    CPU_FREE(cpuset);
    return 0;
#else
    return -ENOSYS;
#endif
}

void qemu_thread_get_self(QemuThread *thread)
{
    thread->thread = pthread_self();
}

bool qemu_thread_is_self(QemuThread *thread)
{
   return pthread_equal(pthread_self(), thread->thread);
}

void qemu_thread_exit(void *retval)
{
    pthread_exit(retval);
}

void *qemu_thread_join(QemuThread *thread)
{
    int err;
    void *ret;

    err = pthread_join(thread->thread, &ret);
    if (err) {
        error_exit(err, __func__);
    }
    return ret;
}
