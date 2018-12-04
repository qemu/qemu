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
#ifndef HOTPLUG_H
#define HOTPLUG_H

#include "qom/object.h"

#define TYPE_HOTPLUG_HANDLER "hotplug-handler"

#define HOTPLUG_HANDLER_CLASS(klass) \
     OBJECT_CLASS_CHECK(HotplugHandlerClass, (klass), TYPE_HOTPLUG_HANDLER)
#define HOTPLUG_HANDLER_GET_CLASS(obj) \
     OBJECT_GET_CLASS(HotplugHandlerClass, (obj), TYPE_HOTPLUG_HANDLER)
#define HOTPLUG_HANDLER(obj) \
     INTERFACE_CHECK(HotplugHandler, (obj), TYPE_HOTPLUG_HANDLER)

typedef struct HotplugHandler HotplugHandler;

/**
 * hotplug_fn:
 * @plug_handler: a device performing plug/uplug action
 * @plugged_dev: a device that has been (un)plugged
 * @errp: returns an error if this function fails
 */
typedef void (*hotplug_fn)(HotplugHandler *plug_handler,
                           DeviceState *plugged_dev, Error **errp);

/**
 * HotplugDeviceClass:
 *
 * Interface to be implemented by a device performing
 * hardware (un)plug functions.
 *
 * @parent: Opaque parent interface.
 * @pre_plug: pre plug callback called at start of device.realize(true)
 * @plug: plug callback called at end of device.realize(true).
 * @unplug_request: unplug request callback.
 *                  Used as a means to initiate device unplug for devices that
 *                  require asynchronous unplug handling.
 * @unplug: unplug callback.
 *          Used for device removal with devices that implement
 *          asynchronous and synchronous (surprise) removal.
 */
typedef struct HotplugHandlerClass {
    /* <private> */
    InterfaceClass parent;

    /* <public> */
    hotplug_fn pre_plug;
    hotplug_fn plug;
    hotplug_fn unplug_request;
    hotplug_fn unplug;
} HotplugHandlerClass;

/**
 * hotplug_handler_plug:
 *
 * Call #HotplugHandlerClass.plug callback of @plug_handler.
 */
void hotplug_handler_plug(HotplugHandler *plug_handler,
                          DeviceState *plugged_dev,
                          Error **errp);

/**
 * hotplug_handler_pre_plug:
 *
 * Call #HotplugHandlerClass.pre_plug callback of @plug_handler.
 */
void hotplug_handler_pre_plug(HotplugHandler *plug_handler,
                              DeviceState *plugged_dev,
                              Error **errp);

/**
 * hotplug_handler_unplug_request:
 *
 * Calls #HotplugHandlerClass.unplug_request callback of @plug_handler.
 */
void hotplug_handler_unplug_request(HotplugHandler *plug_handler,
                                    DeviceState *plugged_dev,
                                    Error **errp);
/**
 * hotplug_handler_unplug:
 *
 * Calls #HotplugHandlerClass.unplug callback of @plug_handler.
 */
void hotplug_handler_unplug(HotplugHandler *plug_handler,
                            DeviceState *plugged_dev,
                            Error **errp);
#endif
