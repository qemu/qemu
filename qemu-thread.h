#ifndef __QEMU_THREAD_H
#define __QEMU_THREAD_H 1
#include "semaphore.h"
#include "pthread.h"

struct QemuMutex {
    pthread_mutex_t lock;
};

struct QemuCond {
    pthread_cond_t cond;
};

struct QemuThread {
    pthread_t thread;
};

typedef struct QemuMutex QemuMutex;
typedef struct QemuCond QemuCond;
typedef struct QemuThread QemuThread;

void qemu_mutex_init(QemuMutex *mutex);
void qemu_mutex_lock(QemuMutex *mutex);
int qemu_mutex_trylock(QemuMutex *mutex);
int qemu_mutex_timedlock(QemuMutex *mutex, uint64_t msecs);
void qemu_mutex_unlock(QemuMutex *mutex);

void qemu_cond_init(QemuCond *cond);
void qemu_cond_signal(QemuCond *cond);
void qemu_cond_broadcast(QemuCond *cond);
void qemu_cond_wait(QemuCond *cond, QemuMutex *mutex);
int qemu_cond_timedwait(QemuCond *cond, QemuMutex *mutex, uint64_t msecs);

void qemu_thread_create(QemuThread *thread,
                       void *(*start_routine)(void*),
                       void *arg);
void qemu_thread_signal(QemuThread *thread, int sig);
void qemu_thread_self(QemuThread *thread);
int qemu_thread_equal(QemuThread *thread1, QemuThread *thread2);
#endif
