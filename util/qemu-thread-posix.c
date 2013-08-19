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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>
#include "qemu/thread.h"

static void error_exit(int err, const char *msg)
{
    fprintf(stderr, "qemu: %s: %s\n", msg, strerror(err));
    abort();
}

void qemu_mutex_init(QemuMutex *mutex)
{
    int err;
    pthread_mutexattr_t mutexattr;

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_ERRORCHECK);
    err = pthread_mutex_init(&mutex->lock, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);
    if (err)
        error_exit(err, __func__);
}

void qemu_mutex_destroy(QemuMutex *mutex)
{
    int err;

    err = pthread_mutex_destroy(&mutex->lock);
    if (err)
        error_exit(err, __func__);
}

void qemu_mutex_lock(QemuMutex *mutex)
{
    int err;

    err = pthread_mutex_lock(&mutex->lock);
    if (err)
        error_exit(err, __func__);
}

int qemu_mutex_trylock(QemuMutex *mutex)
{
    return pthread_mutex_trylock(&mutex->lock);
}

void qemu_mutex_unlock(QemuMutex *mutex)
{
    int err;

    err = pthread_mutex_unlock(&mutex->lock);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_init(QemuCond *cond)
{
    int err;

    err = pthread_cond_init(&cond->cond, NULL);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_destroy(QemuCond *cond)
{
    int err;

    err = pthread_cond_destroy(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_signal(QemuCond *cond)
{
    int err;

    err = pthread_cond_signal(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_broadcast(QemuCond *cond)
{
    int err;

    err = pthread_cond_broadcast(&cond->cond);
    if (err)
        error_exit(err, __func__);
}

void qemu_cond_wait(QemuCond *cond, QemuMutex *mutex)
{
    int err;

    err = pthread_cond_wait(&cond->cond, &mutex->lock);
    if (err)
        error_exit(err, __func__);
}

void qemu_sem_init(QemuSemaphore *sem, int init)
{
    int rc;

#if defined(__APPLE__) || defined(__NetBSD__)
    rc = pthread_mutex_init(&sem->lock, NULL);
    if (rc != 0) {
        error_exit(rc, __func__);
    }
    rc = pthread_cond_init(&sem->cond, NULL);
    if (rc != 0) {
        error_exit(rc, __func__);
    }
    if (init < 0) {
        error_exit(EINVAL, __func__);
    }
    sem->count = init;
#else
    rc = sem_init(&sem->sem, 0, init);
    if (rc < 0) {
        error_exit(errno, __func__);
    }
#endif
}

void qemu_sem_destroy(QemuSemaphore *sem)
{
    int rc;

#if defined(__APPLE__) || defined(__NetBSD__)
    rc = pthread_cond_destroy(&sem->cond);
    if (rc < 0) {
        error_exit(rc, __func__);
    }
    rc = pthread_mutex_destroy(&sem->lock);
    if (rc < 0) {
        error_exit(rc, __func__);
    }
#else
    rc = sem_destroy(&sem->sem);
    if (rc < 0) {
        error_exit(errno, __func__);
    }
#endif
}

void qemu_sem_post(QemuSemaphore *sem)
{
    int rc;

#if defined(__APPLE__) || defined(__NetBSD__)
    pthread_mutex_lock(&sem->lock);
    if (sem->count == UINT_MAX) {
        rc = EINVAL;
    } else {
        sem->count++;
        rc = pthread_cond_signal(&sem->cond);
    }
    pthread_mutex_unlock(&sem->lock);
    if (rc != 0) {
        error_exit(rc, __func__);
    }
#else
    rc = sem_post(&sem->sem);
    if (rc < 0) {
        error_exit(errno, __func__);
    }
#endif
}

static void compute_abs_deadline(struct timespec *ts, int ms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts->tv_nsec = tv.tv_usec * 1000 + (ms % 1000) * 1000000;
    ts->tv_sec = tv.tv_sec + ms / 1000;
    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
}

int qemu_sem_timedwait(QemuSemaphore *sem, int ms)
{
    int rc;
    struct timespec ts;

#if defined(__APPLE__) || defined(__NetBSD__)
    rc = 0;
    compute_abs_deadline(&ts, ms);
    pthread_mutex_lock(&sem->lock);
    while (sem->count == 0) {
        rc = pthread_cond_timedwait(&sem->cond, &sem->lock, &ts);
        if (rc == ETIMEDOUT) {
            break;
        }
        if (rc != 0) {
            error_exit(rc, __func__);
        }
    }
    if (rc != ETIMEDOUT) {
        --sem->count;
    }
    pthread_mutex_unlock(&sem->lock);
    return (rc == ETIMEDOUT ? -1 : 0);
#else
    if (ms <= 0) {
        /* This is cheaper than sem_timedwait.  */
        do {
            rc = sem_trywait(&sem->sem);
        } while (rc == -1 && errno == EINTR);
        if (rc == -1 && errno == EAGAIN) {
            return -1;
        }
    } else {
        compute_abs_deadline(&ts, ms);
        do {
            rc = sem_timedwait(&sem->sem, &ts);
        } while (rc == -1 && errno == EINTR);
        if (rc == -1 && errno == ETIMEDOUT) {
            return -1;
        }
    }
    if (rc < 0) {
        error_exit(errno, __func__);
    }
    return 0;
#endif
}

void qemu_sem_wait(QemuSemaphore *sem)
{
    int rc;

#if defined(__APPLE__) || defined(__NetBSD__)
    pthread_mutex_lock(&sem->lock);
    while (sem->count == 0) {
        rc = pthread_cond_wait(&sem->cond, &sem->lock);
        if (rc != 0) {
            error_exit(rc, __func__);
        }
    }
    --sem->count;
    pthread_mutex_unlock(&sem->lock);
#else
    do {
        rc = sem_wait(&sem->sem);
    } while (rc == -1 && errno == EINTR);
    if (rc < 0) {
        error_exit(errno, __func__);
    }
#endif
}

void qemu_thread_create(QemuThread *thread,
                       void *(*start_routine)(void*),
                       void *arg, int mode)
{
    sigset_t set, oldset;
    int err;
    pthread_attr_t attr;

    err = pthread_attr_init(&attr);
    if (err) {
        error_exit(err, __func__);
    }
    if (mode == QEMU_THREAD_DETACHED) {
        err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (err) {
            error_exit(err, __func__);
        }
    }

    /* Leave signal handling to the iothread.  */
    sigfillset(&set);
    pthread_sigmask(SIG_SETMASK, &set, &oldset);
    err = pthread_create(&thread->thread, &attr, start_routine, arg);
    if (err)
        error_exit(err, __func__);

    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    pthread_attr_destroy(&attr);
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
