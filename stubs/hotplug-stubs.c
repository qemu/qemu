/*
 * Hotplug handler stubs
 *
 * Copyright (c) Red Hat
 *
 * Authors:
 *  Philippe Mathieu-Daudé <philmd@redhat.com>,
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "hw/qdev-core.h"

HotplugHandler *qdev_get_hotplug_handler(DeviceState *dev)
{
    return NULL;
}

void hotplug_handler_pre_plug(HotplugHandler *plug_handler,
                              DeviceState *plugged_dev,
                              Error **errp)
{
    g_assert_not_reached();
}

void hotplug_handler_plug(HotplugHandler *plug_handler,
                          DeviceState *plugged_dev,
                          Error **errp)
{
    g_assert_not_reached();
}
