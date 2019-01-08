/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/xen/xen-bus.h"
#include "qapi/error.h"
#include "trace.h"

static void xen_bus_unrealize(BusState *bus, Error **errp)
{
    trace_xen_bus_unrealize();
}

static void xen_bus_realize(BusState *bus, Error **errp)
{
    trace_xen_bus_realize();
}

static void xen_bus_class_init(ObjectClass *class, void *data)
{
    BusClass *bus_class = BUS_CLASS(class);

    bus_class->realize = xen_bus_realize;
    bus_class->unrealize = xen_bus_unrealize;
}

static const TypeInfo xen_bus_type_info = {
    .name = TYPE_XEN_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(XenBus),
    .class_size = sizeof(XenBusClass),
    .class_init = xen_bus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    },
};

static void xen_device_unrealize(DeviceState *dev, Error **errp)
{
    XenDevice *xendev = XEN_DEVICE(dev);
    XenDeviceClass *xendev_class = XEN_DEVICE_GET_CLASS(xendev);
    const char *type = object_get_typename(OBJECT(xendev));

    trace_xen_device_unrealize(type);

    if (xendev_class->unrealize) {
        xendev_class->unrealize(xendev, errp);
    }
}

static void xen_device_realize(DeviceState *dev, Error **errp)
{
    XenDevice *xendev = XEN_DEVICE(dev);
    XenDeviceClass *xendev_class = XEN_DEVICE_GET_CLASS(xendev);
    const char *type = object_get_typename(OBJECT(xendev));
    Error *local_err = NULL;

    trace_xen_device_realize(type);

    if (xendev_class->realize) {
        xendev_class->realize(xendev, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto unrealize;
        }
    }

    return;

unrealize:
    xen_device_unrealize(dev, &error_abort);
}

static void xen_device_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);

    dev_class->realize = xen_device_realize;
    dev_class->unrealize = xen_device_unrealize;
    dev_class->bus_type = TYPE_XEN_BUS;
}

static const TypeInfo xen_device_type_info = {
    .name = TYPE_XEN_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XenDevice),
    .abstract = true,
    .class_size = sizeof(XenDeviceClass),
    .class_init = xen_device_class_init,
};

typedef struct XenBridge {
    SysBusDevice busdev;
} XenBridge;

#define TYPE_XEN_BRIDGE "xen-bridge"

static const TypeInfo xen_bridge_type_info = {
    .name = TYPE_XEN_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenBridge),
};

static void xen_register_types(void)
{
    type_register_static(&xen_bridge_type_info);
    type_register_static(&xen_bus_type_info);
    type_register_static(&xen_device_type_info);
}

type_init(xen_register_types)

void xen_bus_init(void)
{
    DeviceState *dev = qdev_create(NULL, TYPE_XEN_BRIDGE);
    BusState *bus = qbus_create(TYPE_XEN_BUS, dev, NULL);

    qdev_init_nofail(dev);
    qbus_set_bus_hotplug_handler(bus, &error_abort);
}
