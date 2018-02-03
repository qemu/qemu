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

#define QEMU_THREAD_JOINABLE 0
#define QEMU_THREAD_DETACHED 1

void qemu_mutex_init(QemuMutex *mutex);
void qemu_mutex_destroy(QemuMutex *mutex);
int qemu_mutex_trylock_impl(QemuMutex *mutex, const char *file, const int line);
void qemu_mutex_lock_impl(QemuMutex *mutex, const char *file, const int line);
void qemu_mutex_unlock_impl(QemuMutex *mutex, const char *file, const int line);

#define qemu_mutex_lock(mutex) \
        qemu_mutex_lock_impl(mutex, __FILE__, __LINE__)
#define qemu_mutex_trylock(mutex) \
        qemu_mutex_trylock_impl(mutex, __FILE__, __LINE__)
#define qemu_mutex_unlock(mutex) \
        qemu_mutex_unlock_impl(mutex, __FILE__, __LINE__)

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

/* Prototypes for other functions are in thread-posix.h/thread-win32.h.  */
void qemu_rec_mutex_init(QemuRecMutex *mutex);

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

#define qemu_cond_wait(cond, mutex) \
        qemu_cond_wait_impl(cond, mutex, __FILE__, __LINE__)

static inline void (qemu_cond_wait)(QemuCond *cond, QemuMutex *mutex)
{
    qemu_cond_wait(cond, mutex);
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
void *qemu_thread_join(QemuThread *thread);
void qemu_thread_get_self(QemuThread *thread);
bool qemu_thread_is_self(QemuThread *thread);
void qemu_thread_exit(void *retval);
void qemu_thread_naming(bool enable);

struct Notifier;
void qemu_thread_atexit_add(struct Notifier *notifier);
void qemu_thread_atexit_remove(struct Notifier *notifier);

struct QemuSpin {
    int value;
};

static inline void qemu_spin_init(QemuSpin *spin)
{
    __sync_lock_release(&spin->value);
}

static inline void qemu_spin_lock(QemuSpin *spin)
{
    while (unlikely(__sync_lock_test_and_set(&spin->value, true))) {
        while (atomic_read(&spin->value)) {
            cpu_relax();
        }
    }
}

static inline bool qemu_spin_trylock(QemuSpin *spin)
{
    return __sync_lock_test_and_set(&spin->value, true);
}

static inline bool qemu_spin_locked(QemuSpin *spin)
{
    return atomic_read(&spin->value);
}

static inline void qemu_spin_unlock(QemuSpin *spin)
{
    __sync_lock_release(&spin->value);
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
