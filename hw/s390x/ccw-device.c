/*
 * Common device infrastructure for devices in the virtual css
 *
 * Copyright 2016 IBM Corp.
 * Author(s): Jing Liu <liujbjl@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"
#include "ccw-device.h"

static const TypeInfo ccw_device_info = {
    .name = TYPE_CCW_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CcwDevice),
    .class_size = sizeof(CCWDeviceClass),
    .abstract = true,
};

static void ccw_device_register(void)
{
    type_register_static(&ccw_device_info);
}

type_init(ccw_device_register)
