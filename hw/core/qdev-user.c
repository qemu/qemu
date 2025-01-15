/*
 * QDev helpers specific to user emulation.
 *
 * Copyright 2025 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qom/object.h"
#include "hw/qdev-core.h"

void qdev_create_fake_machine(void)
{
    Object *fake_machine_obj;

    fake_machine_obj = object_property_add_new_container(object_get_root(),
                                                         "machine");
    object_property_add_new_container(fake_machine_obj, "unattached");
}
