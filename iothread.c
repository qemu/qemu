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
#include "block/block.h"
#include "sysemu/iothread.h"
#include "qmp-commands.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"
#include "qemu/main-loop.h"

typedef ObjectClass IOThreadClass;

#define IOTHREAD_GET_CLASS(obj) \
   OBJECT_GET_CLASS(IOThreadClass, obj, TYPE_IOTHREAD)
#define IOTHREAD_CLASS(klass) \
   OBJECT_CLASS_CHECK(IOThreadClass, klass, TYPE_IOTHREAD)

/* Benchmark results from 2016 on NVMe SSD drives show max polling times around
 * 16-32 microseconds yield IOPS improvements for both iodepth=1 and iodepth=32
 * workloads.
 */
#define IOTHREAD_POLL_MAX_NS_DEFAULT 32768ULL

static __thread IOThread *my_iothread;

AioContext *qemu_get_current_aio_context(void)
{
    return my_iothread ? my_iothread->ctx : qemu_get_aio_context();
}

static void *iothread_run(void *opaque)
{
    IOThread *iothread = opaque;

    rcu_register_thread();

    my_iothread = iothread;
    qemu_mutex_lock(&iothread->init_done_lock);
    iothread->thread_id = qemu_get_thread_id();
    qemu_cond_signal(&iothread->init_done_cond);
    qemu_mutex_unlock(&iothread->init_done_lock);

    while (!atomic_read(&iothread->stopping)) {
        aio_poll(iothread->ctx, true);
    }

    rcu_unregister_thread();
    return NULL;
}

static int iothread_stop(Object *object, void *opaque)
{
    IOThread *iothread;

    iothread = (IOThread *)object_dynamic_cast(object, TYPE_IOTHREAD);
    if (!iothread || !iothread->ctx) {
        return 0;
    }
    iothread->stopping = true;
    aio_notify(iothread->ctx);
    qemu_thread_join(&iothread->thread);
    return 0;
}

static void iothread_instance_init(Object *obj)
{
    IOThread *iothread = IOTHREAD(obj);

    iothread->poll_max_ns = IOTHREAD_POLL_MAX_NS_DEFAULT;
}

static void iothread_instance_finalize(Object *obj)
{
    IOThread *iothread = IOTHREAD(obj);

    iothread_stop(obj, NULL);
    qemu_cond_destroy(&iothread->init_done_cond);
    qemu_mutex_destroy(&iothread->init_done_lock);
    if (!iothread->ctx) {
        return;
    }
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

    aio_context_set_poll_params(iothread->ctx,
                                iothread->poll_max_ns,
                                iothread->poll_grow,
                                iothread->poll_shrink,
                                &local_error);
    if (local_error) {
        error_propagate(errp, local_error);
        aio_context_unref(iothread->ctx);
        iothread->ctx = NULL;
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

typedef struct {
    const char *name;
    ptrdiff_t offset; /* field's byte offset in IOThread struct */
} PollParamInfo;

static PollParamInfo poll_max_ns_info = {
    "poll-max-ns", offsetof(IOThread, poll_max_ns),
};
static PollParamInfo poll_grow_info = {
    "poll-grow", offsetof(IOThread, poll_grow),
};
static PollParamInfo poll_shrink_info = {
    "poll-shrink", offsetof(IOThread, poll_shrink),
};

static void iothread_get_poll_param(Object *obj, Visitor *v,
        const char *name, void *opaque, Error **errp)
{
    IOThread *iothread = IOTHREAD(obj);
    PollParamInfo *info = opaque;
    int64_t *field = (void *)iothread + info->offset;

    visit_type_int64(v, name, field, errp);
}

static void iothread_set_poll_param(Object *obj, Visitor *v,
        const char *name, void *opaque, Error **errp)
{
    IOThread *iothread = IOTHREAD(obj);
    PollParamInfo *info = opaque;
    int64_t *field = (void *)iothread + info->offset;
    Error *local_err = NULL;
    int64_t value;

    visit_type_int64(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }

    if (value < 0) {
        error_setg(&local_err, "%s value must be in range [0, %"PRId64"]",
                   info->name, INT64_MAX);
        goto out;
    }

    *field = value;

    if (iothread->ctx) {
        aio_context_set_poll_params(iothread->ctx,
                                    iothread->poll_max_ns,
                                    iothread->poll_grow,
                                    iothread->poll_shrink,
                                    &local_err);
    }

out:
    error_propagate(errp, local_err);
}

static void iothread_class_init(ObjectClass *klass, void *class_data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(klass);
    ucc->complete = iothread_complete;

    object_class_property_add(klass, "poll-max-ns", "int",
                              iothread_get_poll_param,
                              iothread_set_poll_param,
                              NULL, &poll_max_ns_info, &error_abort);
    object_class_property_add(klass, "poll-grow", "int",
                              iothread_get_poll_param,
                              iothread_set_poll_param,
                              NULL, &poll_grow_info, &error_abort);
    object_class_property_add(klass, "poll-shrink", "int",
                              iothread_get_poll_param,
                              iothread_set_poll_param,
                              NULL, &poll_shrink_info, &error_abort);
}

static const TypeInfo iothread_info = {
    .name = TYPE_IOTHREAD,
    .parent = TYPE_OBJECT,
    .class_init = iothread_class_init,
    .instance_size = sizeof(IOThread),
    .instance_init = iothread_instance_init,
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
    info->poll_max_ns = iothread->poll_max_ns;
    info->poll_grow = iothread->poll_grow;
    info->poll_shrink = iothread->poll_shrink;

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

void iothread_stop_all(void)
{
    Object *container = object_get_objects_root();
    BlockDriverState *bs;
    BdrvNextIterator it;

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        AioContext *ctx = bdrv_get_aio_context(bs);
        if (ctx == qemu_get_aio_context()) {
            continue;
        }
        aio_context_acquire(ctx);
        bdrv_set_aio_context(bs, qemu_get_aio_context());
        aio_context_release(ctx);
    }

    object_child_foreach(container, iothread_stop, NULL);
}
