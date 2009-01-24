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
#include <sys/time.h>
#include "osdep.h"

#include "posix-aio-compat.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_t thread_id;
static int max_threads = 64;
static int cur_threads = 0;
static int idle_threads = 0;
static TAILQ_HEAD(, qemu_paiocb) request_list;

static void *aio_thread(void *unused)
{
    sigset_t set;

    /* block all signals */
    sigfillset(&set);
    sigprocmask(SIG_BLOCK, &set, NULL);

    while (1) {
        struct qemu_paiocb *aiocb;
        size_t offset;
        int ret = 0;

        pthread_mutex_lock(&lock);

        while (TAILQ_EMPTY(&request_list) &&
               !(ret == ETIMEDOUT)) {
            struct timespec ts = { 0 };
            qemu_timeval tv;

            qemu_gettimeofday(&tv);
            ts.tv_sec = tv.tv_sec + 10;
            ret = pthread_cond_timedwait(&cond, &lock, &ts);
        }

        if (ret == ETIMEDOUT)
            break;

        aiocb = TAILQ_FIRST(&request_list);
        TAILQ_REMOVE(&request_list, aiocb, node);

        offset = 0;
        aiocb->active = 1;

        idle_threads--;
        pthread_mutex_unlock(&lock);

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

        pthread_mutex_lock(&lock);
        aiocb->ret = offset;
        idle_threads++;
        pthread_mutex_unlock(&lock);

        kill(getpid(), aiocb->ev_signo);
    }

    idle_threads--;
    cur_threads--;
    pthread_mutex_unlock(&lock);

    return NULL;
}

static int spawn_thread(void)
{
    pthread_attr_t attr;
    int ret;

    cur_threads++;
    idle_threads++;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&thread_id, &attr, aio_thread, NULL);
    pthread_attr_destroy(&attr);

    return ret;
}

int qemu_paio_init(struct qemu_paioinit *aioinit)
{
    TAILQ_INIT(&request_list);

    return 0;
}

static int qemu_paio_submit(struct qemu_paiocb *aiocb, int is_write)
{
    aiocb->is_write = is_write;
    aiocb->ret = -EINPROGRESS;
    aiocb->active = 0;
    pthread_mutex_lock(&lock);
    if (idle_threads == 0 && cur_threads < max_threads)
        spawn_thread();
    TAILQ_INSERT_TAIL(&request_list, aiocb, node);
    pthread_mutex_unlock(&lock);
    pthread_cond_broadcast(&cond);

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

    pthread_mutex_lock(&lock);
    ret = aiocb->ret;
    pthread_mutex_unlock(&lock);

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

    pthread_mutex_lock(&lock);
    if (!aiocb->active) {
        TAILQ_REMOVE(&request_list, aiocb, node);
        aiocb->ret = -ECANCELED;
        ret = QEMU_PAIO_CANCELED;
    } else if (aiocb->ret == -EINPROGRESS)
        ret = QEMU_PAIO_NOTCANCELED;
    else
        ret = QEMU_PAIO_ALLDONE;
    pthread_mutex_unlock(&lock);

    return ret;
}
