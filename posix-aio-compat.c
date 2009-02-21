/*
 * QEMU posix-aio emulation
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "osdep.h"

#include "posix-aio-compat.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_t thread_id;
static pthread_attr_t attr;
static int max_threads = 64;
static int cur_threads = 0;
static int idle_threads = 0;
static TAILQ_HEAD(, qemu_paiocb) request_list;

static void die2(int err, const char *what)
{
    fprintf(stderr, "%s failed: %s\n", what, strerror(err));
    abort();
}

static void die(const char *what)
{
    die2(errno, what);
}

static void mutex_lock(pthread_mutex_t *mutex)
{
    int ret = pthread_mutex_lock(mutex);
    if (ret) die2(ret, "pthread_mutex_lock");
}

static void mutex_unlock(pthread_mutex_t *mutex)
{
    int ret = pthread_mutex_unlock(mutex);
    if (ret) die2(ret, "pthread_mutex_unlock");
}

static int cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           struct timespec *ts)
{
    int ret = pthread_cond_timedwait(cond, mutex, ts);
    if (ret && ret != ETIMEDOUT) die2(ret, "pthread_cond_timedwait");
    return ret;
}

static void cond_signal(pthread_cond_t *cond)
{
    int ret = pthread_cond_signal(cond);
    if (ret) die2(ret, "pthread_cond_signal");
}

static void thread_create(pthread_t *thread, pthread_attr_t *attr,
                          void *(*start_routine)(void*), void *arg)
{
    int ret = pthread_create(thread, attr, start_routine, arg);
    if (ret) die2(ret, "pthread_create");
}

static void *aio_thread(void *unused)
{
    pid_t pid;
    sigset_t set;

    pid = getpid();

    /* block all signals */
    if (sigfillset(&set)) die("sigfillset");
    if (sigprocmask(SIG_BLOCK, &set, NULL)) die("sigprocmask");

    while (1) {
        struct qemu_paiocb *aiocb;
        size_t offset;
        int ret = 0;
        qemu_timeval tv;
        struct timespec ts;

        qemu_gettimeofday(&tv);
        ts.tv_sec = tv.tv_sec + 10;
        ts.tv_nsec = 0;

        mutex_lock(&lock);

        while (TAILQ_EMPTY(&request_list) &&
               !(ret == ETIMEDOUT)) {
            ret = cond_timedwait(&cond, &lock, &ts);
        }

        if (ret == ETIMEDOUT)
            break;

        aiocb = TAILQ_FIRST(&request_list);
        TAILQ_REMOVE(&request_list, aiocb, node);

        offset = 0;
        aiocb->active = 1;

        idle_threads--;
        mutex_unlock(&lock);

        while (offset < aiocb->aio_nbytes) {
            ssize_t len;

            if (aiocb->is_write)
                len = pwrite(aiocb->aio_fildes,
                             (const char *)aiocb->aio_buf + offset,
                             aiocb->aio_nbytes - offset,
                             aiocb->aio_offset + offset);
            else
                len = pread(aiocb->aio_fildes,
                            (char *)aiocb->aio_buf + offset,
                            aiocb->aio_nbytes - offset,
                            aiocb->aio_offset + offset);

            if (len == -1 && errno == EINTR)
                continue;
            else if (len == -1) {
                offset = -errno;
                break;
            } else if (len == 0)
                break;

            offset += len;
        }

        mutex_lock(&lock);
        aiocb->ret = offset;
        idle_threads++;
        mutex_unlock(&lock);

        if (kill(pid, aiocb->ev_signo)) die("kill failed");
    }

    idle_threads--;
    cur_threads--;
    mutex_unlock(&lock);

    return NULL;
}

static void spawn_thread(void)
{
    cur_threads++;
    idle_threads++;
    thread_create(&thread_id, &attr, aio_thread, NULL);
}

int qemu_paio_init(struct qemu_paioinit *aioinit)
{
    int ret;

    ret = pthread_attr_init(&attr);
    if (ret) die2(ret, "pthread_attr_init");

    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (ret) die2(ret, "pthread_attr_setdetachstate");

    TAILQ_INIT(&request_list);

    return 0;
}

static int qemu_paio_submit(struct qemu_paiocb *aiocb, int is_write)
{
    aiocb->is_write = is_write;
    aiocb->ret = -EINPROGRESS;
    aiocb->active = 0;
    mutex_lock(&lock);
    if (idle_threads == 0 && cur_threads < max_threads)
        spawn_thread();
    TAILQ_INSERT_TAIL(&request_list, aiocb, node);
    mutex_unlock(&lock);
    cond_signal(&cond);

    return 0;
}

int qemu_paio_read(struct qemu_paiocb *aiocb)
{
    return qemu_paio_submit(aiocb, 0);
}

int qemu_paio_write(struct qemu_paiocb *aiocb)
{
    return qemu_paio_submit(aiocb, 1);
}

ssize_t qemu_paio_return(struct qemu_paiocb *aiocb)
{
    ssize_t ret;

    mutex_lock(&lock);
    ret = aiocb->ret;
    mutex_unlock(&lock);

    return ret;
}

int qemu_paio_error(struct qemu_paiocb *aiocb)
{
    ssize_t ret = qemu_paio_return(aiocb);

    if (ret < 0)
        ret = -ret;
    else
        ret = 0;

    return ret;
}

int qemu_paio_cancel(int fd, struct qemu_paiocb *aiocb)
{
    int ret;

    mutex_lock(&lock);
    if (!aiocb->active) {
        TAILQ_REMOVE(&request_list, aiocb, node);
        aiocb->ret = -ECANCELED;
        ret = QEMU_PAIO_CANCELED;
    } else if (aiocb->ret == -EINPROGRESS)
        ret = QEMU_PAIO_NOTCANCELED;
    else
        ret = QEMU_PAIO_ALLDONE;
    mutex_unlock(&lock);

    return ret;
}
