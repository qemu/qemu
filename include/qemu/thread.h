#ifndef QEMU_THREAD_H
#define QEMU_THREAD_H

#include "qemu/processor.h"
#include "qemu/atomic.h"

typedef struct QemuCond QemuCond;
typedef struct QemuSemaphore QemuSemaphore;
typedef struct QemuEvent QemuEvent;
typedef struct QemuLockCnt QemuLockCnt;
typedef struct QemuThread QemuThread;

#ifdef _WIN32
#include "qemu/thread-win32.h"
#else
#include "qemu/thread-posix.h"
#endif

/* include QSP header once QemuMutex, QemuCond etc. are defined */
#include "qemu/qsp.h"

#define QEMU_THREAD_JOINABLE 0
#define QEMU_THREAD_DETACHED 1

void qemu_mutex_init(QemuMutex *mutex);
void qemu_mutex_destroy(QemuMutex *mutex);
int qemu_mutex_trylock_impl(QemuMutex *mutex, const char *file, const int line);
void qemu_mutex_lock_impl(QemuMutex *mutex, const char *file, const int line);
void qemu_mutex_unlock_impl(QemuMutex *mutex, const char *file, const int line);

void qemu_rec_mutex_init(QemuRecMutex *mutex);
void qemu_rec_mutex_destroy(QemuRecMutex *mutex);
void qemu_rec_mutex_lock_impl(QemuRecMutex *mutex, const char *file, int line);
int qemu_rec_mutex_trylock_impl(QemuRecMutex *mutex, const char *file, int line);
void qemu_rec_mutex_unlock_impl(QemuRecMutex *mutex, const char *file, int line);

typedef void (*QemuMutexLockFunc)(QemuMutex *m, const char *f, int l);
typedef int (*QemuMutexTrylockFunc)(QemuMutex *m, const char *f, int l);
typedef void (*QemuRecMutexLockFunc)(QemuRecMutex *m, const char *f, int l);
typedef int (*QemuRecMutexTrylockFunc)(QemuRecMutex *m, const char *f, int l);
typedef void (*QemuCondWaitFunc)(QemuCond *c, QemuMutex *m, const char *f,
                                 int l);
typedef bool (*QemuCondTimedWaitFunc)(QemuCond *c, QemuMutex *m, int ms,
                                      const char *f, int l);

extern QemuMutexLockFunc qemu_bql_mutex_lock_func;
extern QemuMutexLockFunc qemu_mutex_lock_func;
extern QemuMutexTrylockFunc qemu_mutex_trylock_func;
extern QemuRecMutexLockFunc qemu_rec_mutex_lock_func;
extern QemuRecMutexTrylockFunc qemu_rec_mutex_trylock_func;
extern QemuCondWaitFunc qemu_cond_wait_func;
extern QemuCondTimedWaitFunc qemu_cond_timedwait_func;

/* convenience macros to bypass the profiler */
#define qemu_mutex_lock__raw(m)                         \
        qemu_mutex_lock_impl(m, __FILE__, __LINE__)
#define qemu_mutex_trylock__raw(m)                      \
        qemu_mutex_trylock_impl(m, __FILE__, __LINE__)

#ifdef __COVERITY__
/*
 * Coverity is severely confused by the indirect function calls,
 * hide them.
 */
#define qemu_mutex_lock(m)                                              \
            qemu_mutex_lock_impl(m, __FILE__, __LINE__)
#define qemu_mutex_trylock(m)                                           \
            qemu_mutex_trylock_impl(m, __FILE__, __LINE__)
#define qemu_rec_mutex_lock(m)                                          \
            qemu_rec_mutex_lock_impl(m, __FILE__, __LINE__)
#define qemu_rec_mutex_trylock(m)                                       \
            qemu_rec_mutex_trylock_impl(m, __FILE__, __LINE__)
#define qemu_cond_wait(c, m)                                            \
            qemu_cond_wait_impl(c, m, __FILE__, __LINE__)
#define qemu_cond_timedwait(c, m, ms)                                   \
            qemu_cond_timedwait_impl(c, m, ms, __FILE__, __LINE__)
#else
#define qemu_mutex_lock(m) ({                                           \
            QemuMutexLockFunc _f = qatomic_read(&qemu_mutex_lock_func); \
            _f(m, __FILE__, __LINE__);                                  \
        })

#define qemu_mutex_trylock(m) ({                                              \
            QemuMutexTrylockFunc _f = qatomic_read(&qemu_mutex_trylock_func); \
            _f(m, __FILE__, __LINE__);                                        \
        })

#define qemu_rec_mutex_lock(m) ({                                             \
            QemuRecMutexLockFunc _f = qatomic_read(&qemu_rec_mutex_lock_func);\
            _f(m, __FILE__, __LINE__);                                        \
        })

#define qemu_rec_mutex_trylock(m) ({                            \
            QemuRecMutexTrylockFunc _f;                         \
            _f = qatomic_read(&qemu_rec_mutex_trylock_func);    \
            _f(m, __FILE__, __LINE__);                          \
        })

#define qemu_cond_wait(c, m) ({                                         \
            QemuCondWaitFunc _f = qatomic_read(&qemu_cond_wait_func);   \
            _f(c, m, __FILE__, __LINE__);                               \
        })

#define qemu_cond_timedwait(c, m, ms) ({                                       \
            QemuCondTimedWaitFunc _f = qatomic_read(&qemu_cond_timedwait_func);\
            _f(c, m, ms, __FILE__, __LINE__);                                  \
        })
#endif

#define qemu_mutex_unlock(mutex) \
        qemu_mutex_unlock_impl(mutex, __FILE__, __LINE__)

#define qemu_rec_mutex_unlock(mutex) \
        qemu_rec_mutex_unlock_impl(mutex, __FILE__, __LINE__)

static inline void (qemu_mutex_lock)(QemuMutex *mutex)
{
    qemu_mutex_lock(mutex);
}

static inline int (qemu_mutex_trylock)(QemuMutex *mutex)
{
    return qemu_mutex_trylock(mutex);
}

static inline void (qemu_mutex_unlock)(QemuMutex *mutex)
{
    qemu_mutex_unlock(mutex);
}

static inline void (qemu_rec_mutex_lock)(QemuRecMutex *mutex)
{
    qemu_rec_mutex_lock(mutex);
}

static inline int (qemu_rec_mutex_trylock)(QemuRecMutex *mutex)
{
    return qemu_rec_mutex_trylock(mutex);
}

static inline void (qemu_rec_mutex_unlock)(QemuRecMutex *mutex)
{
    qemu_rec_mutex_unlock(mutex);
}

void qemu_cond_init(QemuCond *cond);
void qemu_cond_destroy(QemuCond *cond);

/*
 * IMPORTANT: The implementation does not guarantee that pthread_cond_signal
 * and pthread_cond_broadcast can be called except while the same mutex is
 * held as in the corresponding pthread_cond_wait calls!
 */
void qemu_cond_signal(QemuCond *cond);
void qemu_cond_broadcast(QemuCond *cond);
void qemu_cond_wait_impl(QemuCond *cond, QemuMutex *mutex,
                         const char *file, const int line);
bool qemu_cond_timedwait_impl(QemuCond *cond, QemuMutex *mutex, int ms,
                              const char *file, const int line);

static inline void (qemu_cond_wait)(QemuCond *cond, QemuMutex *mutex)
{
    qemu_cond_wait(cond, mutex);
}

/* Returns true if timeout has not expired, and false otherwise */
static inline bool (qemu_cond_timedwait)(QemuCond *cond, QemuMutex *mutex,
                                         int ms)
{
    return qemu_cond_timedwait(cond, mutex, ms);
}

void qemu_sem_init(QemuSemaphore *sem, int init);
void qemu_sem_post(QemuSemaphore *sem);
void qemu_sem_wait(QemuSemaphore *sem);
int qemu_sem_timedwait(QemuSemaphore *sem, int ms);
void qemu_sem_destroy(QemuSemaphore *sem);

void qemu_event_init(QemuEvent *ev, bool init);
void qemu_event_set(QemuEvent *ev);
void qemu_event_reset(QemuEvent *ev);
void qemu_event_wait(QemuEvent *ev);
void qemu_event_destroy(QemuEvent *ev);

void qemu_thread_create(QemuThread *thread, const char *name,
                        void *(*start_routine)(void *),
                        void *arg, int mode);
int qemu_thread_set_affinity(QemuThread *thread, unsigned long *host_cpus,
                             unsigned long nbits);
int qemu_thread_get_affinity(QemuThread *thread, unsigned long **host_cpus,
                             unsigned long *nbits);
void *qemu_thread_join(QemuThread *thread);
void qemu_thread_get_self(QemuThread *thread);
bool qemu_thread_is_self(QemuThread *thread);
G_NORETURN void qemu_thread_exit(void *retval);
void qemu_thread_naming(bool enable);

struct Notifier;
/**
 * qemu_thread_atexit_add:
 * @notifier: Notifier to add
 *
 * Add the specified notifier to a list which will be run via
 * notifier_list_notify() when this thread exits (either by calling
 * qemu_thread_exit() or by returning from its start_routine).
 * The usual usage is that the caller passes a Notifier which is
 * a per-thread variable; it can then use the callback to free
 * other per-thread data.
 *
 * If the thread exits as part of the entire process exiting,
 * it is unspecified whether notifiers are called or not.
 */
void qemu_thread_atexit_add(struct Notifier *notifier);
/**
 * qemu_thread_atexit_remove:
 * @notifier: Notifier to remove
 *
 * Remove the specified notifier from the thread-exit notification
 * list. It is not valid to try to remove a notifier which is not
 * on the list.
 */
void qemu_thread_atexit_remove(struct Notifier *notifier);

#ifdef CONFIG_TSAN
#include <sanitizer/tsan_interface.h>
#endif

struct QemuSpin {
    int value;
};

static inline void qemu_spin_init(QemuSpin *spin)
{
    qatomic_set(&spin->value, 0);
#ifdef CONFIG_TSAN
    __tsan_mutex_create(spin, __tsan_mutex_not_static);
#endif
}

/* const parameter because the only purpose here is the TSAN annotation */
static inline void qemu_spin_destroy(const QemuSpin *spin)
{
#ifdef CONFIG_TSAN
    __tsan_mutex_destroy((void *)spin, __tsan_mutex_not_static);
#endif
}

static inline void qemu_spin_lock(QemuSpin *spin)
{
#ifdef CONFIG_TSAN
    __tsan_mutex_pre_lock(spin, 0);
#endif
    while (unlikely(qatomic_xchg(&spin->value, 1))) {
        while (qatomic_read(&spin->value)) {
            cpu_relax();
        }
    }
#ifdef CONFIG_TSAN
    __tsan_mutex_post_lock(spin, 0, 0);
#endif
}

static inline bool qemu_spin_trylock(QemuSpin *spin)
{
#ifdef CONFIG_TSAN
    __tsan_mutex_pre_lock(spin, __tsan_mutex_try_lock);
#endif
    bool busy = qatomic_xchg(&spin->value, true);
#ifdef CONFIG_TSAN
    unsigned flags = __tsan_mutex_try_lock;
    flags |= busy ? __tsan_mutex_try_lock_failed : 0;
    __tsan_mutex_post_lock(spin, flags, 0);
#endif
    return busy;
}

static inline bool qemu_spin_locked(QemuSpin *spin)
{
    return qatomic_read(&spin->value);
}

static inline void qemu_spin_unlock(QemuSpin *spin)
{
#ifdef CONFIG_TSAN
    __tsan_mutex_pre_unlock(spin, 0);
#endif
    qatomic_store_release(&spin->value, 0);
#ifdef CONFIG_TSAN
    __tsan_mutex_post_unlock(spin, 0);
#endif
}

struct QemuLockCnt {
#ifndef CONFIG_LINUX
    QemuMutex mutex;
#endif
    unsigned count;
};

/**
 * qemu_lockcnt_init: initialize a QemuLockcnt
 * @lockcnt: the lockcnt to initialize
 *
 * Initialize lockcnt's counter to zero and prepare its mutex
 * for usage.
 */
void qemu_lockcnt_init(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_destroy: destroy a QemuLockcnt
 * @lockcnt: the lockcnt to destruct
 *
 * Destroy lockcnt's mutex.
 */
void qemu_lockcnt_destroy(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_inc: increment a QemuLockCnt's counter
 * @lockcnt: the lockcnt to operate on
 *
 * If the lockcnt's count is zero, wait for critical sections
 * to finish and increment lockcnt's count to 1.  If the count
 * is not zero, just increment it.
 *
 * Because this function can wait on the mutex, it must not be
 * called while the lockcnt's mutex is held by the current thread.
 * For the same reason, qemu_lockcnt_inc can also contribute to
 * AB-BA deadlocks.  This is a sample deadlock scenario:
 *
 *            thread 1                      thread 2
 *            -------------------------------------------------------
 *            qemu_lockcnt_lock(&lc1);
 *                                          qemu_lockcnt_lock(&lc2);
 *            qemu_lockcnt_inc(&lc2);
 *                                          qemu_lockcnt_inc(&lc1);
 */
void qemu_lockcnt_inc(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_dec: decrement a QemuLockCnt's counter
 * @lockcnt: the lockcnt to operate on
 */
void qemu_lockcnt_dec(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_dec_and_lock: decrement a QemuLockCnt's counter and
 * possibly lock it.
 * @lockcnt: the lockcnt to operate on
 *
 * Decrement lockcnt's count.  If the new count is zero, lock
 * the mutex and return true.  Otherwise, return false.
 */
bool qemu_lockcnt_dec_and_lock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_dec_if_lock: possibly decrement a QemuLockCnt's counter and
 * lock it.
 * @lockcnt: the lockcnt to operate on
 *
 * If the count is 1, decrement the count to zero, lock
 * the mutex and return true.  Otherwise, return false.
 */
bool qemu_lockcnt_dec_if_lock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_lock: lock a QemuLockCnt's mutex.
 * @lockcnt: the lockcnt to operate on
 *
 * Remember that concurrent visits are not blocked unless the count is
 * also zero.  You can use qemu_lockcnt_count to check for this inside a
 * critical section.
 */
void qemu_lockcnt_lock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_unlock: release a QemuLockCnt's mutex.
 * @lockcnt: the lockcnt to operate on.
 */
void qemu_lockcnt_unlock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_inc_and_unlock: combined unlock/increment on a QemuLockCnt.
 * @lockcnt: the lockcnt to operate on.
 *
 * This is the same as
 *
 *     qemu_lockcnt_unlock(lockcnt);
 *     qemu_lockcnt_inc(lockcnt);
 *
 * but more efficient.
 */
void qemu_lockcnt_inc_and_unlock(QemuLockCnt *lockcnt);

/**
 * qemu_lockcnt_count: query a LockCnt's count.
 * @lockcnt: the lockcnt to query.
 *
 * Note that the count can change at any time.  Still, while the
 * lockcnt is locked, one can usefully check whether the count
 * is non-zero.
 */
unsigned qemu_lockcnt_count(QemuLockCnt *lockcnt);

#endif
