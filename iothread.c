/*
 * Event loop thread
 *
 * Copyright Red Hat Inc., 2013, 2020
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
#include "system/event-loop-base.h"
#include "system/iothread.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-misc.h"
#include "qemu/error-report.h"
#include "qemu/rcu.h"
#include "qemu/main-loop.h"


#ifdef CONFIG_POSIX
/* Benchmark results from 2016 on NVMe SSD drives show max polling times around
 * 16-32 microseconds yield IOPS improvements for both iodepth=1 and iodepth=32
 * workloads.
 */
#define IOTHREAD_POLL_MAX_NS_DEFAULT 32768ULL
#else
#define IOTHREAD_POLL_MAX_NS_DEFAULT 0ULL
#endif

static void *iothread_run(void *opaque)
{
    IOThread *iothread = opaque;

    rcu_register_thread();
    /*
     * g_main_context_push_thread_default() must be called before anything
     * in this new thread uses glib.
     */
    g_main_context_push_thread_default(iothread->worker_context);
    qemu_set_current_aio_context(iothread->ctx);
    iothread->thread_id = qemu_get_thread_id();
    qemu_sem_post(&iothread->init_done_sem);

    while (iothread->running) {
        /*
         * Note: from functional-wise the g_main_loop_run() below can
         * already cover the aio_poll() events, but we can't run the
         * main loop unconditionally because explicit aio_poll() here
         * is faster than g_main_loop_run() when we do not need the
         * gcontext at all (e.g., pure block layer iothreads).  In
         * other words, when we want to run the gcontext with the
         * iothread we need to pay some performance for functionality.
         */
        aio_poll(iothread->ctx, true);

        /*
         * We must check the running state again in case it was
         * changed in previous aio_poll()
         */
        if (iothread->running && qatomic_read(&iothread->run_gcontext)) {
            g_main_loop_run(iothread->main_loop);
        }
    }

    g_main_context_pop_thread_default(iothread->worker_context);
    rcu_unregister_thread();
    return NULL;
}

/* Runs in iothread_run() thread */
static void iothread_stop_bh(void *opaque)
{
    IOThread *iothread = opaque;

    iothread->running = false; /* stop iothread_run() */

    if (iothread->main_loop) {
        g_main_loop_quit(iothread->main_loop);
    }
}

void iothread_stop(IOThread *iothread)
{
    if (!iothread->ctx || iothread->stopping) {
        return;
    }
    iothread->stopping = true;
    aio_bh_schedule_oneshot(iothread->ctx, iothread_stop_bh, iothread);
    qemu_thread_join(&iothread->thread);
}

static void iothread_instance_init(Object *obj)
{
    IOThread *iothread = IOTHREAD(obj);

    iothread->poll_max_ns = IOTHREAD_POLL_MAX_NS_DEFAULT;
    iothread->thread_id = -1;
    qemu_sem_init(&iothread->init_done_sem, 0);
    /* By default, we don't run gcontext */
    qatomic_set(&iothread->run_gcontext, 0);
}

static void iothread_instance_finalize(Object *obj)
{
    IOThread *iothread = IOTHREAD(obj);

    iothread_stop(iothread);

    /*
     * Before glib2 2.33.10, there is a glib2 bug that GSource context
     * pointer may not be cleared even if the context has already been
     * destroyed (while it should).  Here let's free the AIO context
     * earlier to bypass that glib bug.
     *
     * We can remove this comment after the minimum supported glib2
     * version boosts to 2.33.10.  Before that, let's free the
     * GSources first before destroying any GMainContext.
     */
    if (iothread->ctx) {
        aio_context_unref(iothread->ctx);
        iothread->ctx = NULL;
    }
    if (iothread->worker_context) {
        g_main_context_unref(iothread->worker_context);
        iothread->worker_context = NULL;
        g_main_loop_unref(iothread->main_loop);
        iothread->main_loop = NULL;
    }
    qemu_sem_destroy(&iothread->init_done_sem);
}

static void iothread_init_gcontext(IOThread *iothread, const char *thread_name)
{
    GSource *source;
    g_autofree char *name = g_strdup_printf("%s aio-context", thread_name);

    iothread->worker_context = g_main_context_new();
    source = aio_get_g_source(iothread_get_aio_context(iothread));
    g_source_set_name(source, name);
    g_source_attach(source, iothread->worker_context);
    g_source_unref(source);
    iothread->main_loop = g_main_loop_new(iothread->worker_context, TRUE);
}

static void iothread_set_aio_context_params(EventLoopBase *base, Error **errp)
{
    ERRP_GUARD();
    IOThread *iothread = IOTHREAD(base);

    if (!iothread->ctx) {
        return;
    }

    aio_context_set_poll_params(iothread->ctx,
                                iothread->poll_max_ns,
                                iothread->poll_grow,
                                iothread->poll_shrink,
                                errp);
    if (*errp) {
        return;
    }

    aio_context_set_aio_params(iothread->ctx,
                               iothread->parent_obj.aio_max_batch);

    aio_context_set_thread_pool_params(iothread->ctx, base->thread_pool_min,
                                       base->thread_pool_max, errp);
}


static void iothread_init(EventLoopBase *base, Error **errp)
{
    Error *local_error = NULL;
    IOThread *iothread = IOTHREAD(base);
    g_autofree char *thread_name = NULL;

    iothread->stopping = false;
    iothread->running = true;
    iothread->ctx = aio_context_new(errp);
    if (!iothread->ctx) {
        return;
    }

    thread_name = g_strdup_printf("IO %s",
                        object_get_canonical_path_component(OBJECT(base)));

    /*
     * Init one GMainContext for the iothread unconditionally, even if
     * it's not used
     */
    iothread_init_gcontext(iothread, thread_name);

    iothread_set_aio_context_params(base, &local_error);
    if (local_error) {
        error_propagate(errp, local_error);
        aio_context_unref(iothread->ctx);
        iothread->ctx = NULL;
        return;
    }

    /* This assumes we are called from a thread with useful CPU affinity for us
     * to inherit.
     */
    qemu_thread_create(&iothread->thread, thread_name, iothread_run,
                       iothread, QEMU_THREAD_JOINABLE);

    /* Wait for initialization to complete */
    while (iothread->thread_id == -1) {
        qemu_sem_wait(&iothread->init_done_sem);
    }
}

typedef struct {
    const char *name;
    ptrdiff_t offset; /* field's byte offset in IOThread struct */
} IOThreadParamInfo;

static IOThreadParamInfo poll_max_ns_info = {
    "poll-max-ns", offsetof(IOThread, poll_max_ns),
};
static IOThreadParamInfo poll_grow_info = {
    "poll-grow", offsetof(IOThread, poll_grow),
};
static IOThreadParamInfo poll_shrink_info = {
    "poll-shrink", offsetof(IOThread, poll_shrink),
};

static void iothread_get_param(Object *obj, Visitor *v,
        const char *name, IOThreadParamInfo *info, Error **errp)
{
    IOThread *iothread = IOTHREAD(obj);
    int64_t *field = (void *)iothread + info->offset;

    visit_type_int64(v, name, field, errp);
}

static bool iothread_set_param(Object *obj, Visitor *v,
        const char *name, IOThreadParamInfo *info, Error **errp)
{
    IOThread *iothread = IOTHREAD(obj);
    int64_t *field = (void *)iothread + info->offset;
    int64_t value;

    if (!visit_type_int64(v, name, &value, errp)) {
        return false;
    }

    if (value < 0) {
        error_setg(errp, "%s value must be in range [0, %" PRId64 "]",
                   info->name, INT64_MAX);
        return false;
    }

    *field = value;

    return true;
}

static void iothread_get_poll_param(Object *obj, Visitor *v,
        const char *name, void *opaque, Error **errp)
{
    IOThreadParamInfo *info = opaque;

    iothread_get_param(obj, v, name, info, errp);
}

static void iothread_set_poll_param(Object *obj, Visitor *v,
        const char *name, void *opaque, Error **errp)
{
    IOThread *iothread = IOTHREAD(obj);
    IOThreadParamInfo *info = opaque;

    if (!iothread_set_param(obj, v, name, info, errp)) {
        return;
    }

    if (iothread->ctx) {
        aio_context_set_poll_params(iothread->ctx,
                                    iothread->poll_max_ns,
                                    iothread->poll_grow,
                                    iothread->poll_shrink,
                                    errp);
    }
}

static void iothread_class_init(ObjectClass *klass, const void *class_data)
{
    EventLoopBaseClass *bc = EVENT_LOOP_BASE_CLASS(klass);

    bc->init = iothread_init;
    bc->update_params = iothread_set_aio_context_params;

    object_class_property_add(klass, "poll-max-ns", "int",
                              iothread_get_poll_param,
                              iothread_set_poll_param,
                              NULL, &poll_max_ns_info);
    object_class_property_add(klass, "poll-grow", "int",
                              iothread_get_poll_param,
                              iothread_set_poll_param,
                              NULL, &poll_grow_info);
    object_class_property_add(klass, "poll-shrink", "int",
                              iothread_get_poll_param,
                              iothread_set_poll_param,
                              NULL, &poll_shrink_info);
}

static const TypeInfo iothread_info = {
    .name = TYPE_IOTHREAD,
    .parent = TYPE_EVENT_LOOP_BASE,
    .class_init = iothread_class_init,
    .instance_size = sizeof(IOThread),
    .instance_init = iothread_instance_init,
    .instance_finalize = iothread_instance_finalize,
};

static void iothread_register_types(void)
{
    type_register_static(&iothread_info);
}

type_init(iothread_register_types)

char *iothread_get_id(IOThread *iothread)
{
    return g_strdup(object_get_canonical_path_component(OBJECT(iothread)));
}

AioContext *iothread_get_aio_context(IOThread *iothread)
{
    return iothread->ctx;
}

static int query_one_iothread(Object *object, void *opaque)
{
    IOThreadInfoList ***tail = opaque;
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
    info->aio_max_batch = iothread->parent_obj.aio_max_batch;

    QAPI_LIST_APPEND(*tail, info);
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

GMainContext *iothread_get_g_main_context(IOThread *iothread)
{
    qatomic_set(&iothread->run_gcontext, 1);
    aio_notify(iothread->ctx);
    return iothread->worker_context;
}

IOThread *iothread_create(const char *id, Error **errp)
{
    Object *obj;

    obj = object_new_with_props(TYPE_IOTHREAD,
                                object_get_internal_root(),
                                id, errp, NULL);

    return IOTHREAD(obj);
}

void iothread_destroy(IOThread *iothread)
{
    object_unparent(OBJECT(iothread));
}

/* Lookup IOThread by its id.  Only finds user-created objects, not internal
 * iothread_create() objects. */
IOThread *iothread_by_id(const char *id)
{
    return IOTHREAD(object_resolve_path_type(id, TYPE_IOTHREAD, NULL));
}

bool qemu_in_iothread(void)
{
    return qemu_get_current_aio_context() != qemu_get_aio_context();
}
