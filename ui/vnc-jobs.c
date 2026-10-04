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
#include "trace.h"

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

    assert(vs->magic == VNC_MAGIC);
    job->vs = vs;
    QLIST_INIT(&job->rectangles);
    return job;
}

/*
 * Do not call this after pushing the job.
 */
int vnc_job_add_rect(VncJob *job, int x, int y, int w, int h)
{
    VncRectEntry *entry = g_new0(VncRectEntry, 1);

    trace_vnc_job_add_rect(job->vs, job, x, y, w, h);
    assert(!QTAILQ_IN_USE(job, next));
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);

    entry->rect.x = x;
    entry->rect.y = y;
    entry->rect.w = w;
    entry->rect.h = h;

    QLIST_INSERT_HEAD(&job->rectangles, entry, next);
    return 1;
}

static void vnc_job_free(VncJob *job)
{
    VncRectEntry *entry, *tmp;

    if (!job) {
        return;
    }
    QLIST_FOREACH_SAFE(entry, &job->rectangles, next, tmp) {
        /* no need for QLIST_REMOVE(entry, next) */
        g_free(entry);
    }
    g_free(job);
}

/*
 * Push a job onto the queue. Ownership of the job is transferred.
 */
void vnc_job_push(VncJob *job)
{
    VncJobQueue *queue = job->vs->vd->queue;

    assert(!QTAILQ_IN_USE(job, next));

    if (QLIST_EMPTY(&job->rectangles)) {
        vnc_job_free(job);
    } else {
        vnc_lock_queue(queue);
        assert(!queue->exit);
        QTAILQ_INSERT_TAIL(&queue->jobs, job, next);
        qemu_cond_broadcast(&queue->cond);
        vnc_unlock_queue(queue);
    }
}

static bool vnc_has_job_locked(VncState *vs)
{
    VncJobQueue *queue = vs->vd->queue;
    VncJob *job;

    QTAILQ_FOREACH(job, &queue->jobs, next) {
        if (job->vs == vs) {
            return true;
        }
    }
    return false;
}

void vnc_jobs_join(VncState *vs)
{
    VncJobQueue *queue = vs->vd->queue;

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
    if (!buffer_empty(&vs->jobs_buffer)) {
        if (vs->ioc != NULL && buffer_empty(&vs->output)) {
            g_clear_handle_id(&vs->ioc_tag, g_source_remove);
            if (vs->disconnecting == FALSE) {
                vs->ioc_tag = qio_channel_add_watch(
                    vs->ioc, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_OUT,
                    vnc_client_io, vs, NULL);
            }
        }
        buffer_move(&vs->output, &vs->jobs_buffer);

        if (vs->job_update == VNC_STATE_UPDATE_FORCE) {
            vs->force_update_offset = vs->output.offset;
        }
        vs->job_update = VNC_STATE_UPDATE_NONE;
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
    local->write_pixels = orig->write_pixels;
    local->client_pf = orig->client_pf;
    local->client_endian = orig->client_endian;
    local->hextile = orig->hextile;
    local->client_width = orig->client_width;
    local->client_height = orig->client_height;
}

static void vnc_async_encoding_end(VncState *orig, VncState *local)
{
    buffer_free(&local->output);
    orig->hextile = local->hextile;
}

static bool vnc_worker_clamp_rect(VncState *vs, VncJob *job, VncRect *rect)
{
    trace_vnc_job_clamp_rect(vs, job, rect->x, rect->y, rect->w, rect->h);

    if (rect->x >= vs->client_width) {
        goto discard;
    }
    rect->w = MIN(vs->client_width - rect->x, rect->w);
    if (rect->w == 0) {
        goto discard;
    }

    if (rect->y >= vs->client_height) {
        goto discard;
    }
    rect->h = MIN(vs->client_height - rect->y, rect->h);
    if (rect->h == 0) {
        goto discard;
    }

    trace_vnc_job_clamped_rect(vs, job, rect->x, rect->y, rect->w, rect->h);
    return true;

 discard:
    trace_vnc_job_discard_rect(vs, job, rect->x, rect->y, rect->w, rect->h);
    return false;
}

static int vnc_worker_thread_loop(VncJobQueue *queue)
{
    VncConnection *vc;
    VncJob *job;
    VncRectEntry *entry, *tmp;
    VncState vs = {};
    int n_rectangles;
    int saved_offset;

    vnc_lock_queue(queue);
    while (QTAILQ_EMPTY(&queue->jobs) && !queue->exit) {
        qemu_cond_wait(&queue->cond, &queue->mutex);
    }
    if (queue->exit) {
        vnc_unlock_queue(queue);
        return 1;
    }
    job = QTAILQ_FIRST(&queue->jobs);
    vnc_unlock_queue(queue);

    assert(job->vs->magic == VNC_MAGIC);
    vc = container_of(job->vs, VncConnection, vs);

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
    vs.magic = VNC_MAGIC;

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

        if (vnc_worker_clamp_rect(&vs, job, &entry->rect)) {
            n = vnc_send_framebuffer_update(&vs, &vc->worker,
                                            entry->rect.x, entry->rect.y,
                                            entry->rect.w, entry->rect.h);

            if (n >= 0) {
                n_rectangles += n;
            }
        }
        QLIST_REMOVE(entry, next);
        g_free(entry);
    }
    trace_vnc_job_nrects(&vs, job, n_rectangles);
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
        /* Copy persistent encoding data */
        vnc_async_encoding_end(job->vs, &vs);
    }
    vnc_unlock_output(job->vs);

disconnected:
    vnc_lock_queue(queue);
    QTAILQ_REMOVE(&queue->jobs, job, next);
    vnc_unlock_queue(queue);
    qemu_cond_broadcast(&queue->cond);
    vnc_job_free(job);
    vs.magic = 0;
    return 0;
}

static VncJobQueue *vnc_queue_new(void)
{
    VncJobQueue *queue = g_new0(VncJobQueue, 1);

    qemu_cond_init(&queue->cond);
    qemu_mutex_init(&queue->mutex);
    QTAILQ_INIT(&queue->jobs);
    return queue;
}

static void vnc_queue_free(VncJobQueue *queue)
{
    qemu_cond_destroy(&queue->cond);
    qemu_mutex_destroy(&queue->mutex);
    g_free(queue);
}

static void *vnc_worker_thread(void *arg)
{
    VncJobQueue *queue = arg;

    while (!vnc_worker_thread_loop(queue)) ;

    return NULL;
}

void vnc_start_worker_thread(VncDisplay *vd)
{
    assert(vd->queue == NULL);

    vd->queue = vnc_queue_new();
    qemu_thread_create(&vd->queue->thread, "vnc_worker", vnc_worker_thread, vd->queue,
                       QEMU_THREAD_JOINABLE);
}

void vnc_stop_worker_thread(VncDisplay *vd)
{
    VncJobQueue *queue = vd->queue;

    if (!queue) {
        return;
    }

    /* all VNC clients must have finished before we can stop the worker thread */
    vnc_lock_queue(queue);
    assert(QTAILQ_EMPTY(&queue->jobs));
    queue->exit = true;
    qemu_cond_broadcast(&queue->cond);
    vnc_unlock_queue(queue);

    qemu_thread_join(&queue->thread);
    g_clear_pointer(&vd->queue, vnc_queue_free);
}
