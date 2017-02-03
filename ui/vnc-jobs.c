/*
 * QEMU VNC display driver
 *
 * Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2009 Red Hat, Inc
 * Copyright (C) 2010 Corentin Chary <corentin.chary@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


#include "qemu/osdep.h"
#include "vnc.h"
#include "vnc-jobs.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"
#include "block/aio.h"

/*
 * Locking:
 *
 * There are three levels of locking:
 * - jobs queue lock: for each operation on the queue (push, pop, isEmpty?)
 * - VncDisplay global lock: mainly used for framebuffer updates to avoid
 *                      screen corruption if the framebuffer is updated
 *                      while the worker is doing something.
 * - VncState::output lock: used to make sure the output buffer is not corrupted
 *                          if two threads try to write on it at the same time
 *
 * While the VNC worker thread is working, the VncDisplay global lock is held
 * to avoid screen corruption (this does not block vnc_refresh() because it
 * uses trylock()) but the output lock is not held because the thread works on
 * its own output buffer.
 * When the encoding job is done, the worker thread will hold the output lock
 * and copy its output buffer in vs->output.
 */

struct VncJobQueue {
    QemuCond cond;
    QemuMutex mutex;
    QemuThread thread;
    bool exit;
    QTAILQ_HEAD(, VncJob) jobs;
};

typedef struct VncJobQueue VncJobQueue;

/*
 * We use a single global queue, but most of the functions are
 * already reentrant, so we can easily add more than one encoding thread
 */
static VncJobQueue *queue;

static void vnc_lock_queue(VncJobQueue *queue)
{
    qemu_mutex_lock(&queue->mutex);
}

static void vnc_unlock_queue(VncJobQueue *queue)
{
    qemu_mutex_unlock(&queue->mutex);
}

VncJob *vnc_job_new(VncState *vs)
{
    VncJob *job = g_new0(VncJob, 1);

    job->vs = vs;
    vnc_lock_queue(queue);
    QLIST_INIT(&job->rectangles);
    vnc_unlock_queue(queue);
    return job;
}

int vnc_job_add_rect(VncJob *job, int x, int y, int w, int h)
{
    VncRectEntry *entry = g_new0(VncRectEntry, 1);

    entry->rect.x = x;
    entry->rect.y = y;
    entry->rect.w = w;
    entry->rect.h = h;

    vnc_lock_queue(queue);
    QLIST_INSERT_HEAD(&job->rectangles, entry, next);
    vnc_unlock_queue(queue);
    return 1;
}

void vnc_job_push(VncJob *job)
{
    vnc_lock_queue(queue);
    if (queue->exit || QLIST_EMPTY(&job->rectangles)) {
        g_free(job);
    } else {
        QTAILQ_INSERT_TAIL(&queue->jobs, job, next);
        qemu_cond_broadcast(&queue->cond);
    }
    vnc_unlock_queue(queue);
}

static bool vnc_has_job_locked(VncState *vs)
{
    VncJob *job;

    QTAILQ_FOREACH(job, &queue->jobs, next) {
        if (job->vs == vs || !vs) {
            return true;
        }
    }
    return false;
}

void vnc_jobs_join(VncState *vs)
{
    vnc_lock_queue(queue);
    while (vnc_has_job_locked(vs)) {
        qemu_cond_wait(&queue->cond, &queue->mutex);
    }
    vnc_unlock_queue(queue);
    vnc_jobs_consume_buffer(vs);
}

void vnc_jobs_consume_buffer(VncState *vs)
{
    bool flush;

    vnc_lock_output(vs);
    if (vs->jobs_buffer.offset) {
        if (vs->ioc != NULL && buffer_empty(&vs->output)) {
            if (vs->ioc_tag) {
                g_source_remove(vs->ioc_tag);
            }
            vs->ioc_tag = qio_channel_add_watch(
                vs->ioc, G_IO_IN | G_IO_OUT, vnc_client_io, vs, NULL);
        }
        buffer_move(&vs->output, &vs->jobs_buffer);
    }
    flush = vs->ioc != NULL && vs->abort != true;
    vnc_unlock_output(vs);

    if (flush) {
      vnc_flush(vs);
    }
}

/*
 * Copy data for local use
 */
static void vnc_async_encoding_start(VncState *orig, VncState *local)
{
    buffer_init(&local->output, "vnc-worker-output");
    local->sioc = NULL; /* Don't do any network work on this thread */
    local->ioc = NULL; /* Don't do any network work on this thread */

    local->vnc_encoding = orig->vnc_encoding;
    local->features = orig->features;
    local->vd = orig->vd;
    local->lossy_rect = orig->lossy_rect;
    local->write_pixels = orig->write_pixels;
    local->client_pf = orig->client_pf;
    local->client_be = orig->client_be;
    local->tight = orig->tight;
    local->zlib = orig->zlib;
    local->hextile = orig->hextile;
    local->zrle = orig->zrle;
}

static void vnc_async_encoding_end(VncState *orig, VncState *local)
{
    orig->tight = local->tight;
    orig->zlib = local->zlib;
    orig->hextile = local->hextile;
    orig->zrle = local->zrle;
    orig->lossy_rect = local->lossy_rect;
}

static int vnc_worker_thread_loop(VncJobQueue *queue)
{
    VncJob *job;
    VncRectEntry *entry, *tmp;
    VncState vs = {};
    int n_rectangles;
    int saved_offset;

    vnc_lock_queue(queue);
    while (QTAILQ_EMPTY(&queue->jobs) && !queue->exit) {
        qemu_cond_wait(&queue->cond, &queue->mutex);
    }
    /* Here job can only be NULL if queue->exit is true */
    job = QTAILQ_FIRST(&queue->jobs);
    vnc_unlock_queue(queue);

    if (queue->exit) {
        return -1;
    }

    vnc_lock_output(job->vs);
    if (job->vs->ioc == NULL || job->vs->abort == true) {
        vnc_unlock_output(job->vs);
        goto disconnected;
    }
    if (buffer_empty(&job->vs->output)) {
        /*
         * Looks like a NOP as it obviously moves no data.  But it
         * moves the empty buffer, so we don't have to malloc a new
         * one for vs.output
         */
        buffer_move_empty(&vs.output, &job->vs->output);
    }
    vnc_unlock_output(job->vs);

    /* Make a local copy of vs and switch output buffers */
    vnc_async_encoding_start(job->vs, &vs);

    /* Start sending rectangles */
    n_rectangles = 0;
    vnc_write_u8(&vs, VNC_MSG_SERVER_FRAMEBUFFER_UPDATE);
    vnc_write_u8(&vs, 0);
    saved_offset = vs.output.offset;
    vnc_write_u16(&vs, 0);

    vnc_lock_display(job->vs->vd);
    QLIST_FOREACH_SAFE(entry, &job->rectangles, next, tmp) {
        int n;

        if (job->vs->ioc == NULL) {
            vnc_unlock_display(job->vs->vd);
            /* Copy persistent encoding data */
            vnc_async_encoding_end(job->vs, &vs);
            goto disconnected;
        }

        n = vnc_send_framebuffer_update(&vs, entry->rect.x, entry->rect.y,
                                        entry->rect.w, entry->rect.h);

        if (n >= 0) {
            n_rectangles += n;
        }
        g_free(entry);
    }
    vnc_unlock_display(job->vs->vd);

    /* Put n_rectangles at the beginning of the message */
    vs.output.buffer[saved_offset] = (n_rectangles >> 8) & 0xFF;
    vs.output.buffer[saved_offset + 1] = n_rectangles & 0xFF;

    vnc_lock_output(job->vs);
    if (job->vs->ioc != NULL) {
        buffer_move(&job->vs->jobs_buffer, &vs.output);
        /* Copy persistent encoding data */
        vnc_async_encoding_end(job->vs, &vs);

	qemu_bh_schedule(job->vs->bh);
    }  else {
        buffer_reset(&vs.output);
        /* Copy persistent encoding data */
        vnc_async_encoding_end(job->vs, &vs);
    }
    vnc_unlock_output(job->vs);

disconnected:
    vnc_lock_queue(queue);
    QTAILQ_REMOVE(&queue->jobs, job, next);
    vnc_unlock_queue(queue);
    qemu_cond_broadcast(&queue->cond);
    g_free(job);
    return 0;
}

static VncJobQueue *vnc_queue_init(void)
{
    VncJobQueue *queue = g_new0(VncJobQueue, 1);

    qemu_cond_init(&queue->cond);
    qemu_mutex_init(&queue->mutex);
    QTAILQ_INIT(&queue->jobs);
    return queue;
}

static void vnc_queue_clear(VncJobQueue *q)
{
    qemu_cond_destroy(&queue->cond);
    qemu_mutex_destroy(&queue->mutex);
    g_free(q);
    queue = NULL; /* Unset global queue */
}

static void *vnc_worker_thread(void *arg)
{
    VncJobQueue *queue = arg;

    qemu_thread_get_self(&queue->thread);

    while (!vnc_worker_thread_loop(queue)) ;
    vnc_queue_clear(queue);
    return NULL;
}

static bool vnc_worker_thread_running(void)
{
    return queue; /* Check global queue */
}

void vnc_start_worker_thread(void)
{
    VncJobQueue *q;

    if (vnc_worker_thread_running())
        return ;

    q = vnc_queue_init();
    qemu_thread_create(&q->thread, "vnc_worker", vnc_worker_thread, q,
                       QEMU_THREAD_DETACHED);
    queue = q; /* Set global queue */
}
