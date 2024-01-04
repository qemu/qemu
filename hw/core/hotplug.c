/*
 * Hotplug handler interface.
 *
 * Copyright (c) 2014 Red Hat Inc.
 *
 * Authors:
 *  Igor Mammedov <imammedo@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "hw/hotplug.h"
#include "qemu/module.h"

void hotplug_handler_pre_plug(HotplugHandler *plug_handler,
                              DeviceState *plugged_dev,
                              Error **errp)
{
    HotplugHandlerClass *hdc = HOTPLUG_HANDLER_GET_CLASS(plug_handler);

    if (hdc->pre_plug) {
        hdc->pre_plug(plug_handler, plugged_dev, errp);
    }
}

void hotplug_handler_plug(HotplugHandler *plug_handler,
                          DeviceState *plugged_dev,
                          Error **errp)
{
    HotplugHandlerClass *hdc = HOTPLUG_HANDLER_GET_CLASS(plug_handler);

    if (hdc->plug) {
        hdc->plug(plug_handler, plugged_dev, errp);
    }
}

void hotplug_handler_unplug_request(HotplugHandler *plug_handler,
                                    DeviceState *plugged_dev,
                                    Error **errp)
{
    HotplugHandlerClass *hdc = HOTPLUG_HANDLER_GET_CLASS(plug_handler);

    if (hdc->unplug_request) {
        hdc->unplug_request(plug_handler, plugged_dev, errp);
    }
}

void hotplug_handler_unplug(HotplugHandler *plug_handler,
                            DeviceState *plugged_dev,
                            Error **errp)
{
    HotplugHandlerClass *hdc = HOTPLUG_HANDLER_GET_CLASS(plug_handler);

    if (hdc->unplug) {
        hdc->unplug(plug_handler, plugged_dev, errp);
    }
}

static const TypeInfo hotplug_handler_info = {
    .name          = TYPE_HOTPLUG_HANDLER,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(HotplugHandlerClass),
};

static void hotplug_handler_register_types(void)
{
    type_register_static(&hotplug_handler_info);
}

type_init(hotplug_handler_register_types)
