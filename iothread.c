/*
 * Event loop thread
 *
 * Copyright Red Hat Inc., 2013
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"
#include "qemu/module.h"
#include "block/aio.h"
#include "sysemu/iothread.h"
#include "qmp-commands.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"

typedef ObjectClass IOThreadClass;

#define IOTHREAD_GET_CLASS(obj) \
   OBJECT_GET_CLASS(IOThreadClass, obj, TYPE_IOTHREAD)
#define IOTHREAD_CLASS(klass) \
   OBJECT_CLASS_CHECK(IOThreadClass, klass, TYPE_IOTHREAD)

static void *iothread_run(void *opaque)
{
    IOThread *iothread = opaque;
    bool blocking;

    rcu_register_thread();

    qemu_mutex_lock(&iothread->init_done_lock);
    iothread->thread_id = qemu_get_thread_id();
    qemu_cond_signal(&iothread->init_done_cond);
    qemu_mutex_unlock(&iothread->init_done_lock);

    while (!iothread->stopping) {
        aio_context_acquire(iothread->ctx);
        blocking = true;
        while (!iothread->stopping && aio_poll(iothread->ctx, blocking)) {
            /* Progress was made, keep going */
            blocking = false;
        }
        aio_context_release(iothread->ctx);
    }

    rcu_unregister_thread();
    return NULL;
}

static void iothread_instance_finalize(Object *obj)
{
    IOThread *iothread = IOTHREAD(obj);

    if (!iothread->ctx) {
        return;
    }
    iothread->stopping = true;
    aio_notify(iothread->ctx);
    qemu_thread_join(&iothread->thread);
    qemu_cond_destroy(&iothread->init_done_cond);
    qemu_mutex_destroy(&iothread->init_done_lock);
    aio_context_unref(iothread->ctx);
}

static void iothread_complete(UserCreatable *obj, Error **errp)
{
    Error *local_error = NULL;
    IOThread *iothread = IOTHREAD(obj);
    char *name, *thread_name;

    iothread->stopping = false;
    iothread->thread_id = -1;
    iothread->ctx = aio_context_new(&local_error);
    if (!iothread->ctx) {
        error_propagate(errp, local_error);
        return;
    }

    qemu_mutex_init(&iothread->init_done_lock);
    qemu_cond_init(&iothread->init_done_cond);

    /* This assumes we are called from a thread with useful CPU affinity for us
     * to inherit.
     */
    name = object_get_canonical_path_component(OBJECT(obj));
    thread_name = g_strdup_printf("IO %s", name);
    qemu_thread_create(&iothread->thread, thread_name, iothread_run,
                       iothread, QEMU_THREAD_JOINABLE);
    g_free(thread_name);
    g_free(name);

    /* Wait for initialization to complete */
    qemu_mutex_lock(&iothread->init_done_lock);
    while (iothread->thread_id == -1) {
        qemu_cond_wait(&iothread->init_done_cond,
                       &iothread->init_done_lock);
    }
    qemu_mutex_unlock(&iothread->init_done_lock);
}

static void iothread_class_init(ObjectClass *klass, void *class_data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(klass);
    ucc->complete = iothread_complete;
}

static const TypeInfo iothread_info = {
    .name = TYPE_IOTHREAD,
    .parent = TYPE_OBJECT,
    .class_init = iothread_class_init,
    .instance_size = sizeof(IOThread),
    .instance_finalize = iothread_instance_finalize,
    .interfaces = (InterfaceInfo[]) {
        {TYPE_USER_CREATABLE},
        {}
    },
};

static void iothread_register_types(void)
{
    type_register_static(&iothread_info);
}

type_init(iothread_register_types)

char *iothread_get_id(IOThread *iothread)
{
    return object_get_canonical_path_component(OBJECT(iothread));
}

AioContext *iothread_get_aio_context(IOThread *iothread)
{
    return iothread->ctx;
}

static int query_one_iothread(Object *object, void *opaque)
{
    IOThreadInfoList ***prev = opaque;
    IOThreadInfoList *elem;
    IOThreadInfo *info;
    IOThread *iothread;

    iothread = (IOThread *)object_dynamic_cast(object, TYPE_IOTHREAD);
    if (!iothread) {
        return 0;
    }

    info = g_new0(IOThreadInfo, 1);
    info->id = iothread_get_id(iothread);
    info->thread_id = iothread->thread_id;

    elem = g_new0(IOThreadInfoList, 1);
    elem->value = info;
    elem->next = NULL;

    **prev = elem;
    *prev = &elem->next;
    return 0;
}

IOThreadInfoList *qmp_query_iothreads(Error **errp)
{
    IOThreadInfoList *head = NULL;
    IOThreadInfoList **prev = &head;
    Object *container = object_get_objects_root();

    object_child_foreach(container, query_one_iothread, &prev);
    return head;
}
