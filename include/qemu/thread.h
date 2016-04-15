#ifndef __QEMU_THREAD_H
#define __QEMU_THREAD_H 1

#include <errno.h>
#include "qemu/processor.h"
#include "qemu/atomic.h"

typedef struct QemuMutex QemuMutex;
typedef struct QemuCond QemuCond;
typedef struct QemuSemaphore QemuSemaphore;
typedef struct QemuEvent QemuEvent;
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
void qemu_mutex_lock(QemuMutex *mutex);
int qemu_mutex_trylock(QemuMutex *mutex);
void qemu_mutex_unlock(QemuMutex *mutex);

void qemu_cond_init(QemuCond *cond);
void qemu_cond_destroy(QemuCond *cond);

/*
 * IMPORTANT: The implementation does not guarantee that pthread_cond_signal
 * and pthread_cond_broadcast can be called except while the same mutex is
 * held as in the corresponding pthread_cond_wait calls!
 */
void qemu_cond_signal(QemuCond *cond);
void qemu_cond_broadcast(QemuCond *cond);
void qemu_cond_wait(QemuCond *cond, QemuMutex *mutex);

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

typedef struct QemuSpin {
    int value;
} QemuSpin;

static inline void qemu_spin_init(QemuSpin *spin)
{
    spin->value = 0;
}

static inline void qemu_spin_lock(QemuSpin *spin)
{
    while (atomic_xchg(&spin->value, true)) {
        while (atomic_read(&spin->value)) {
            cpu_relax();
        }
    }
}

static inline int qemu_spin_trylock(QemuSpin *spin)
{
    if (atomic_read(&spin->value) || atomic_xchg(&spin->value, true)) {
        return -EBUSY;
    }
    return 0;
}

static inline void qemu_spin_unlock(QemuSpin *spin)
{
    atomic_mb_set(&spin->value, 0);
}

#endif
