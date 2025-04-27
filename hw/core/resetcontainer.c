/*
 * Reset container
 *
 * Copyright (c) 2024 Linaro, Ltd
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * The "reset container" is an object which implements the Resettable
 * interface. It contains a list of arbitrary other objects which also
 * implement Resettable. Resetting the reset container resets all the
 * objects in it.
 */

#include "qemu/osdep.h"
#include "hw/resettable.h"
#include "hw/core/resetcontainer.h"

struct ResettableContainer {
    Object parent;
    ResettableState reset_state;
    GPtrArray *children;
};

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(ResettableContainer, resettable_container, RESETTABLE_CONTAINER, OBJECT, { TYPE_RESETTABLE_INTERFACE }, { })

void resettable_container_add(ResettableContainer *rc, Object *obj)
{
    INTERFACE_CHECK(void, obj, TYPE_RESETTABLE_INTERFACE);
    g_ptr_array_add(rc->children, obj);
}

void resettable_container_remove(ResettableContainer *rc, Object *obj)
{
    g_ptr_array_remove(rc->children, obj);
}

static ResettableState *resettable_container_get_state(Object *obj)
{
    ResettableContainer *rc = RESETTABLE_CONTAINER(obj);
    return &rc->reset_state;
}

static void resettable_container_child_foreach(Object *obj,
                                               ResettableChildCallback cb,
                                               void *opaque, ResetType type)
{
    ResettableContainer *rc = RESETTABLE_CONTAINER(obj);
    unsigned int len = rc->children->len;

    for (unsigned int i = 0; i < len; i++) {
        cb(g_ptr_array_index(rc->children, i), opaque, type);
        /* Detect callbacks trying to unregister themselves */
        assert(len == rc->children->len);
    }
}

static void resettable_container_init(Object *obj)
{
    ResettableContainer *rc = RESETTABLE_CONTAINER(obj);

    rc->children = g_ptr_array_new();
}

static void resettable_container_finalize(Object *obj)
{
}

static void resettable_container_class_init(ObjectClass *klass,
                                            const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->get_state = resettable_container_get_state;
    rc->child_foreach = resettable_container_child_foreach;
}
