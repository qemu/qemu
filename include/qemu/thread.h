#ifndef __QEMU_THREAD_H
#define __QEMU_THREAD_H 1

#include <inttypes.h>
#include <stdbool.h>

typedef struct QemuMutex QemuMutex;
typedef struct QemuCond QemuCond;
typedef struct QemuSemaphore QemuSemaphore;
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

#define rcu_read_lock() do { } while (0)
#define rcu_read_unlock() do { } while (0)

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

void qemu_thread_create(QemuThread *thread,
                        void *(*start_routine)(void *),
                        void *arg, int mode);
void *qemu_thread_join(QemuThread *thread);
void qemu_thread_get_self(QemuThread *thread);
bool qemu_thread_is_self(QemuThread *thread);
void qemu_thread_exit(void *retval);

#endif
