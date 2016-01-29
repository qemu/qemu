/*
 * QEMU I/O task
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "io/task.h"
#include "qemu/thread.h"
#include "trace.h"

struct QIOTask {
    Object *source;
    QIOTaskFunc func;
    gpointer opaque;
    GDestroyNotify destroy;
};


QIOTask *qio_task_new(Object *source,
                      QIOTaskFunc func,
                      gpointer opaque,
                      GDestroyNotify destroy)
{
    QIOTask *task;

    task = g_new0(QIOTask, 1);

    task->source = source;
    object_ref(source);
    task->func = func;
    task->opaque = opaque;
    task->destroy = destroy;

    trace_qio_task_new(task, source, func, opaque);

    return task;
}

static void qio_task_free(QIOTask *task)
{
    if (task->destroy) {
        task->destroy(task->opaque);
    }
    object_unref(task->source);

    g_free(task);
}


struct QIOTaskThreadData {
    QIOTask *task;
    QIOTaskWorker worker;
    gpointer opaque;
    GDestroyNotify destroy;
    Error *err;
    int ret;
};


static gboolean gio_task_thread_result(gpointer opaque)
{
    struct QIOTaskThreadData *data = opaque;

    trace_qio_task_thread_result(data->task);
    if (data->ret == 0) {
        qio_task_complete(data->task);
    } else {
        qio_task_abort(data->task, data->err);
    }

    error_free(data->err);
    if (data->destroy) {
        data->destroy(data->opaque);
    }

    g_free(data);

    return FALSE;
}


static gpointer qio_task_thread_worker(gpointer opaque)
{
    struct QIOTaskThreadData *data = opaque;

    trace_qio_task_thread_run(data->task);
    data->ret = data->worker(data->task, &data->err, data->opaque);
    if (data->ret < 0 && data->err == NULL) {
        error_setg(&data->err, "Task worker failed but did not set an error");
    }

    /* We're running in the background thread, and must only
     * ever report the task results in the main event loop
     * thread. So we schedule an idle callback to report
     * the worker results
     */
    trace_qio_task_thread_exit(data->task);
    g_idle_add(gio_task_thread_result, data);
    return NULL;
}


void qio_task_run_in_thread(QIOTask *task,
                            QIOTaskWorker worker,
                            gpointer opaque,
                            GDestroyNotify destroy)
{
    struct QIOTaskThreadData *data = g_new0(struct QIOTaskThreadData, 1);
    QemuThread thread;

    data->task = task;
    data->worker = worker;
    data->opaque = opaque;
    data->destroy = destroy;

    trace_qio_task_thread_start(task, worker, opaque);
    qemu_thread_create(&thread,
                       "io-task-worker",
                       qio_task_thread_worker,
                       data,
                       QEMU_THREAD_DETACHED);
}


void qio_task_complete(QIOTask *task)
{
    task->func(task->source, NULL, task->opaque);
    trace_qio_task_complete(task);
    qio_task_free(task);
}

void qio_task_abort(QIOTask *task,
                    Error *err)
{
    task->func(task->source, err, task->opaque);
    trace_qio_task_abort(task);
    qio_task_free(task);
}


Object *qio_task_get_source(QIOTask *task)
{
    object_ref(task->source);
    return task->source;
}
