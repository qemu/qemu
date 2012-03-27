/*
 * Device Container
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/object.h"
#include "module.h"
#include <assert.h>

static TypeInfo container_info = {
    .name          = "container",
    .instance_size = sizeof(Object),
    .parent        = TYPE_OBJECT,
};

static void container_register_types(void)
{
    type_register_static(&container_info);
}

Object *container_get(const char *path)
{
    Object *obj, *child;
    gchar **parts;
    int i;

    parts = g_strsplit(path, "/", 0);
    assert(parts != NULL && parts[0] != NULL && !parts[0][0]);
    obj = object_get_root();

    for (i = 1; parts[i] != NULL; i++, obj = child) {
        child = object_resolve_path_component(obj, parts[i]);
        if (!child) {
            child = object_new("container");
            object_property_add_child(obj, parts[i], child, NULL);
        }
    }

    return obj;
}


type_init(container_register_types)
