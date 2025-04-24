/*
 * QEMU event-loop base
 *
 * Copyright (C) 2022 Red Hat Inc
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *  Nicolas Saenz Julienne <nsaenzju@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "block/thread-pool.h"
#include "system/event-loop-base.h"

typedef struct {
    const char *name;
    ptrdiff_t offset; /* field's byte offset in EventLoopBase struct */
} EventLoopBaseParamInfo;

static void event_loop_base_instance_init(Object *obj)
{
    EventLoopBase *base = EVENT_LOOP_BASE(obj);

    base->thread_pool_max = THREAD_POOL_MAX_THREADS_DEFAULT;
}

static EventLoopBaseParamInfo aio_max_batch_info = {
    "aio-max-batch", offsetof(EventLoopBase, aio_max_batch),
};
static EventLoopBaseParamInfo thread_pool_min_info = {
    "thread-pool-min", offsetof(EventLoopBase, thread_pool_min),
};
static EventLoopBaseParamInfo thread_pool_max_info = {
    "thread-pool-max", offsetof(EventLoopBase, thread_pool_max),
};

static void event_loop_base_get_param(Object *obj, Visitor *v,
        const char *name, void *opaque, Error **errp)
{
    EventLoopBase *event_loop_base = EVENT_LOOP_BASE(obj);
    EventLoopBaseParamInfo *info = opaque;
    int64_t *field = (void *)event_loop_base + info->offset;

    visit_type_int64(v, name, field, errp);
}

static void event_loop_base_set_param(Object *obj, Visitor *v,
        const char *name, void *opaque, Error **errp)
{
    EventLoopBaseClass *bc = EVENT_LOOP_BASE_GET_CLASS(obj);
    EventLoopBase *base = EVENT_LOOP_BASE(obj);
    EventLoopBaseParamInfo *info = opaque;
    int64_t *field = (void *)base + info->offset;
    int64_t value;

    if (!visit_type_int64(v, name, &value, errp)) {
        return;
    }

    if (value < 0) {
        error_setg(errp, "%s value must be in range [0, %" PRId64 "]",
                   info->name, INT64_MAX);
        return;
    }

    *field = value;

    if (bc->update_params) {
        bc->update_params(base, errp);
    }
}

static void event_loop_base_complete(UserCreatable *uc, Error **errp)
{
    EventLoopBaseClass *bc = EVENT_LOOP_BASE_GET_CLASS(uc);
    EventLoopBase *base = EVENT_LOOP_BASE(uc);

    if (bc->init) {
        bc->init(base, errp);
    }
}

static bool event_loop_base_can_be_deleted(UserCreatable *uc)
{
    EventLoopBaseClass *bc = EVENT_LOOP_BASE_GET_CLASS(uc);
    EventLoopBase *backend = EVENT_LOOP_BASE(uc);

    if (bc->can_be_deleted) {
        return bc->can_be_deleted(backend);
    }

    return true;
}

static void event_loop_base_class_init(ObjectClass *klass, void *class_data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(klass);
    ucc->complete = event_loop_base_complete;
    ucc->can_be_deleted = event_loop_base_can_be_deleted;

    object_class_property_add(klass, "aio-max-batch", "int",
                              event_loop_base_get_param,
                              event_loop_base_set_param,
                              NULL, &aio_max_batch_info);
    object_class_property_add(klass, "thread-pool-min", "int",
                              event_loop_base_get_param,
                              event_loop_base_set_param,
                              NULL, &thread_pool_min_info);
    object_class_property_add(klass, "thread-pool-max", "int",
                              event_loop_base_get_param,
                              event_loop_base_set_param,
                              NULL, &thread_pool_max_info);
}

static const TypeInfo event_loop_base_info = {
    .name = TYPE_EVENT_LOOP_BASE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(EventLoopBase),
    .instance_init = event_loop_base_instance_init,
    .class_size = sizeof(EventLoopBaseClass),
    .class_init = event_loop_base_class_init,
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&event_loop_base_info);
}
type_init(register_types);
