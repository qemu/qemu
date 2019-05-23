/*
 * ap bridge
 *
 * Copyright 2018 IBM Corp.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "hw/s390x/ap-bridge.h"
#include "cpu.h"

static char *ap_bus_get_dev_path(DeviceState *dev)
{
    /* at most one */
    return g_strdup_printf("/1");
}

static void ap_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *k = BUS_CLASS(oc);

    k->get_dev_path = ap_bus_get_dev_path;
    /* More than one ap device does not make sense */
    k->max_dev = 1;
}

static const TypeInfo ap_bus_info = {
    .name = TYPE_AP_BUS,
    .parent = TYPE_BUS,
    .instance_size = 0,
    .class_init = ap_bus_class_init,
};

void s390_init_ap(void)
{
    DeviceState *dev;
    BusState *bus;

    /* If no AP instructions then no need for AP bridge */
    if (!s390_has_feat(S390_FEAT_AP)) {
        return;
    }

    /* Create bridge device */
    dev = qdev_create(NULL, TYPE_AP_BRIDGE);
    object_property_add_child(qdev_get_machine(), TYPE_AP_BRIDGE,
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);

    /* Create bus on bridge device */
    bus = qbus_create(TYPE_AP_BUS, dev, TYPE_AP_BUS);

    /* Enable hotplugging */
    qbus_set_hotplug_handler(bus, OBJECT(dev), &error_abort);
 }

static void ap_bridge_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    hc->unplug = qdev_simple_device_unplug_cb;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo ap_bridge_info = {
    .name          = TYPE_AP_BRIDGE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = 0,
    .class_init    = ap_bridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void ap_register(void)
{
    type_register_static(&ap_bridge_info);
    type_register_static(&ap_bus_info);
}

type_init(ap_register)
