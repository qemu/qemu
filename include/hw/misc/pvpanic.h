/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *     Hu Tao <hutao@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_MISC_PVPANIC_H
#define HW_MISC_PVPANIC_H

#include "qom/object.h"

#define TYPE_PVPANIC "pvpanic"

#define PVPANIC_IOPORT_PROP "ioport"

static inline uint16_t pvpanic_port(void)
{
    Object *o = object_resolve_path_type("", TYPE_PVPANIC, NULL);
    if (!o) {
        return 0;
    }
    return object_property_get_uint(o, PVPANIC_IOPORT_PROP, NULL);
}

#endif
