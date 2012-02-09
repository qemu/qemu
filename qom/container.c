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

static TypeInfo container_info = {
    .name          = "container",
    .instance_size = sizeof(Object),
    .parent        = TYPE_OBJECT,
};

static void container_register_types(void)
{
    type_register_static(&container_info);
}

type_init(container_register_types)
