/*
 * QEMU I/O task tests
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
#include "qemu/module.h"

#define TYPE_DUMMY "qemu:dummy"

typedef struct DummyObject DummyObject;
typedef struct DummyObjectClass DummyObjectClass;

struct DummyObject {
    Object parent;
};

struct DummyObjectClass {
    ObjectClass parent;
};

static const TypeInfo dummy_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_DUMMY,
    .instance_size = sizeof(DummyObject),
    .class_size = sizeof(DummyObjectClass),
};

struct TestTaskData {
    Object *source;
    Error *err;
    bool freed;
};


static void task_callback(QIOTask *task,
                          gpointer opaque)
{
    struct TestTaskData *data = opaque;

    data->source = qio_task_get_source(task);
    qio_task_propagate_error(task, &data->err);
}


static void test_task_complete(void)
{
    QIOTask *task;
    Object *obj = object_new(TYPE_DUMMY);
    Object *src;
    struct TestTaskData data = { NULL, NULL, false };

    task = qio_task_new(obj, task_callback, &data, NULL);
    src = qio_task_get_source(task);

    qio_task_complete(task);

    g_assert(obj == src);

    object_unref(obj);

    g_assert(data.source == obj);
    g_assert(data.err == NULL);
    g_assert(data.freed == false);
}


static void task_data_free(gpointer opaque)
{
    struct TestTaskData *data = opaque;

    data->freed = true;
}


static void test_task_data_free(void)
{
    QIOTask *task;
    Object *obj = object_new(TYPE_DUMMY);
    struct TestTaskData data = { NULL, NULL, false };

    task = qio_task_new(obj, task_callback, &data, task_data_free);

    qio_task_complete(task);

    object_unref(obj);

    g_assert(data.source == obj);
    g_assert(data.err == NULL);
    g_assert(data.freed == true);
}


static void test_task_failure(void)
{
    QIOTask *task;
    Object *obj = object_new(TYPE_DUMMY);
    struct TestTaskData data = { NULL, NULL, false };
    Error *err = NULL;

    task = qio_task_new(obj, task_callback, &data, NULL);

    error_setg(&err, "Some error");

    qio_task_set_error(task, err);
    qio_task_complete(task);

    object_unref(obj);

    g_assert(data.source == obj);
    g_assert(data.err == err);
    g_assert(data.freed == false);
    error_free(data.err);
}


struct TestThreadWorkerData {
    Object *source;
    Error *err;
    bool fail;
    GThread *worker;
    GThread *complete;
    GMainLoop *loop;
};

static void test_task_thread_worker(QIOTask *task,
                                    gpointer opaque)
{
    struct TestThreadWorkerData *data = opaque;

    data->worker = g_thread_self();

    if (data->fail) {
        Error *err = NULL;
        error_setg(&err, "Testing fail");
        qio_task_set_error(task, err);
    }
}


static void test_task_thread_callback(QIOTask *task,
                                      gpointer opaque)
{
    struct TestThreadWorkerData *data = opaque;

    data->source = qio_task_get_source(task);
    qio_task_propagate_error(task, &data->err);

    data->complete = g_thread_self();

    g_main_loop_quit(data->loop);
}


static void test_task_thread_complete(void)
{
    QIOTask *task;
    Object *obj = object_new(TYPE_DUMMY);
    struct TestThreadWorkerData data = { 0 };
    GThread *self;

    data.loop = g_main_loop_new(g_main_context_default(),
                                TRUE);

    task = qio_task_new(obj,
                        test_task_thread_callback,
                        &data,
                        NULL);

    qio_task_run_in_thread(task,
                           test_task_thread_worker,
                           &data,
                           NULL,
                           NULL);

    g_main_loop_run(data.loop);

    g_main_loop_unref(data.loop);
    object_unref(obj);

    g_assert(data.source == obj);
    g_assert(data.err == NULL);

    self = g_thread_self();

    /* Make sure the test_task_thread_worker actually got
     * run in a different thread */
    g_assert(data.worker != self);

    /* And that the test_task_thread_callback got rnu in
     * the main loop thread (ie this one) */
    g_assert(data.complete == self);
}


static void test_task_thread_failure(void)
{
    QIOTask *task;
    Object *obj = object_new(TYPE_DUMMY);
    struct TestThreadWorkerData data = { 0 };
    GThread *self;

    data.loop = g_main_loop_new(g_main_context_default(),
                                TRUE);
    data.fail = true;

    task = qio_task_new(obj,
                        test_task_thread_callback,
                        &data,
                        NULL);

    qio_task_run_in_thread(task,
                           test_task_thread_worker,
                           &data,
                           NULL,
                           NULL);

    g_main_loop_run(data.loop);

    g_main_loop_unref(data.loop);
    object_unref(obj);

    g_assert(data.source == obj);
    g_assert(data.err != NULL);

    error_free(data.err);

    self = g_thread_self();

    /* Make sure the test_task_thread_worker actually got
     * run in a different thread */
    g_assert(data.worker != self);

    /* And that the test_task_thread_callback got rnu in
     * the main loop thread (ie this one) */
    g_assert(data.complete == self);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    module_call_init(MODULE_INIT_QOM);
    type_register_static(&dummy_info);
    g_test_add_func("/crypto/task/complete", test_task_complete);
    g_test_add_func("/crypto/task/datafree", test_task_data_free);
    g_test_add_func("/crypto/task/failure", test_task_failure);
    g_test_add_func("/crypto/task/thread_complete", test_task_thread_complete);
    g_test_add_func("/crypto/task/thread_failure", test_task_thread_failure);
    return g_test_run();
}
