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


#include "vnc.h"
#include "vnc-jobs.h"
#include "qemu_socket.h"

/*
 * Locking:
 *
 * There is three levels of locking:
 * - jobs queue lock: for each operation on the queue (push, pop, isEmpty?)
 * - VncDisplay global lock: mainly used for framebuffer updates to avoid
 *                      screen corruption if the framebuffer is updated
 *			while the worker is doing something.
 * - VncState::output lock: used to make sure the output buffer is not corrupted
 * 		   	 if two threads try to write on it at the same time
 *
 * While the VNC worker thread is working, the VncDisplay global lock is hold
 * to avoid screen corruptions (this does not block vnc_refresh() because it
 * uses trylock()) but the output lock is not hold because the thread work on
 * its own output buffer.
 * When the encoding job is done, the worker thread will hold the output lock
 * and copy its output buffer in vs->output.
*/

struct VncJobQueue {
    QemuCond cond;
    QemuMutex mutex;
    QemuThread thread;
    Buffer buffer;
    bool exit;
    QTAILQ_HEAD(, VncJob) jobs;
};

typedef struct VncJobQueue VncJobQueue;

/*
 * We use a single global queue, but most of the functions are
 * already reetrant, so we can easilly add more than one encoding thread
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
    VncJob *job = g_malloc0(sizeof(VncJob));

    job->vs = vs;
    vnc_lock_queue(queue);
    QLIST_INIT(&job->rectangles);
    vnc_unlock_queue(queue);
    return job;
}

int vnc_job_add_rect(VncJob *job, int x, int y, int w, int h)
{
    VncRectEntry *entry = g_malloc0(sizeof(VncRectEntry));

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

bool vnc_has_job(VncState *vs)
{
    bool ret;

    vnc_lock_queue(queue);
    ret = vnc_has_job_locked(vs);
    vnc_unlock_queue(queue);
    return ret;
}

void vnc_jobs_clear(VncState *vs)
{
    VncJob *job, *tmp;

    vnc_lock_queue(queue);
    QTAILQ_FOREACH_SAFE(job, &queue->jobs, next, tmp) {
        if (job->vs == vs || !vs) {
            QTAILQ_REMOVE(&queue->jobs, job, next);
        }
    }
    vnc_unlock_queue(queue);
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
        vnc_write(vs, vs->jobs_buffer.buffer, vs->jobs_buffer.offset);
        buffer_reset(&vs->jobs_buffer);
    }
    flush = vs->csock != -1 && vs->abort != true;
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
    local->vnc_encoding = orig->vnc_encoding;
    local->features = orig->features;
    local->ds = orig->ds;
    local->vd = orig->vd;
    local->lossy_rect = orig->lossy_rect;
    local->write_pixels = orig->write_pixels;
    local->clientds = orig->clientds;
    local->tight = orig->tight;
    local->zlib = orig->zlib;
    local->hextile = orig->hextile;
    local->zrle = orig->zrle;
    local->output =  queue->buffer;
    local->csock = -1; /* Don't do any network work on this thread */

    buffer_reset(&local->output);
}

static void vnc_async_encoding_end(VncState *orig, VncState *local)
{
    orig->tight = local->tight;
    orig->zlib = local->zlib;
    orig->hextile = local->hextile;
    orig->zrle = local->zrle;
    orig->lossy_rect = local->lossy_rect;

    queue->buffer = local->output;
}

static int vnc_worker_thread_loop(VncJobQueue *queue)
{
    VncJob *job;
    VncRectEntry *entry, *tmp;
    VncState vs;
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
    if (job->vs->csock == -1 || job->vs->abort == true) {
        vnc_unlock_output(job->vs);
        goto disconnected;
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

        if (job->vs->csock == -1) {
            vnc_unlock_display(job->vs->vd);
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
    if (job->vs->csock != -1) {
        buffer_reserve(&job->vs->jobs_buffer, vs.output.offset);
        buffer_append(&job->vs->jobs_buffer, vs.output.buffer,
                      vs.output.offset);
        /* Copy persistent encoding data */
        vnc_async_encoding_end(job->vs, &vs);

	qemu_bh_schedule(job->vs->bh);
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
    VncJobQueue *queue = g_malloc0(sizeof(VncJobQueue));

    qemu_cond_init(&queue->cond);
    qemu_mutex_init(&queue->mutex);
    QTAILQ_INIT(&queue->jobs);
    return queue;
}

static void vnc_queue_clear(VncJobQueue *q)
{
    qemu_cond_destroy(&queue->cond);
    qemu_mutex_destroy(&queue->mutex);
    buffer_free(&queue->buffer);
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

void vnc_start_worker_thread(void)
{
    VncJobQueue *q;

    if (vnc_worker_thread_running())
        return ;

    q = vnc_queue_init();
    qemu_thread_create(&q->thread, vnc_worker_thread, q, QEMU_THREAD_DETACHED);
    queue = q; /* Set global queue */
}

bool vnc_worker_thread_running(void)
{
    return queue; /* Check global queue */
}

void vnc_stop_worker_thread(void)
{
    if (!vnc_worker_thread_running())
        return ;

    /* Remove all jobs and wake up the thread */
    vnc_lock_queue(queue);
    queue->exit = true;
    vnc_unlock_queue(queue);
    vnc_jobs_clear(NULL);
    qemu_cond_broadcast(&queue->cond);
}
