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
#include "qapi/error.h"
#include "qemu/thread.h"
#include "trace.h"

struct QIOTask {
    Object *source;
    QIOTaskFunc func;
    gpointer opaque;
    GDestroyNotify destroy;
    Error *err;
    gpointer result;
    GDestroyNotify destroyResult;
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
    if (task->destroyResult) {
        task->destroyResult(task->result);
    }
    if (task->err) {
        error_free(task->err);
    }
    object_unref(task->source);

    g_free(task);
}


struct QIOTaskThreadData {
    QIOTask *task;
    QIOTaskWorker worker;
    gpointer opaque;
    GDestroyNotify destroy;
};


static gboolean gio_task_thread_result(gpointer opaque)
{
    struct QIOTaskThreadData *data = opaque;

    trace_qio_task_thread_result(data->task);
    qio_task_complete(data->task);

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
    data->worker(data->task, data->opaque);

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
    task->func(task, task->opaque);
    trace_qio_task_complete(task);
    qio_task_free(task);
}


void qio_task_set_error(QIOTask *task,
                        Error *err)
{
    error_propagate(&task->err, err);
}


bool qio_task_propagate_error(QIOTask *task,
                              Error **errp)
{
    if (task->err) {
        error_propagate(errp, task->err);
        task->err = NULL;
        return true;
    }

    return false;
}


void qio_task_set_result_pointer(QIOTask *task,
                                 gpointer result,
                                 GDestroyNotify destroy)
{
    task->result = result;
    task->destroyResult = destroy;
}


gpointer qio_task_get_result_pointer(QIOTask *task)
{
    return task->result;
}


Object *qio_task_get_source(QIOTask *task)
{
    return task->source;
}
