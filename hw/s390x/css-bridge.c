/*
 * css bridge implementation
 *
 * Copyright 2012,2016 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "virtio-ccw.h"
#include "hw/hotplug.h"
#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "hw/s390x/css.h"
#include "hw/s390x/css-bridge.h"

static void virtual_css_bus_reset(BusState *qbus)
{
    /* This should actually be modelled via the generic css */
    css_reset();
}

static void virtual_css_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->reset = virtual_css_bus_reset;
}

static const TypeInfo virtual_css_bus_info = {
    .name = TYPE_VIRTUAL_CSS_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtualCssBus),
    .class_init = virtual_css_bus_class_init,
};

VirtualCssBus *virtual_css_bus_init(void)
{
    VirtualCssBus *cbus;
    BusState *bus;
    DeviceState *dev;

    /* Create bridge device */
    dev = qdev_create(NULL, TYPE_VIRTUAL_CSS_BRIDGE);
    qdev_init_nofail(dev);

    /* Create bus on bridge device */
    bus = qbus_create(TYPE_VIRTUAL_CSS_BUS, dev, "virtual-css");
    cbus = VIRTUAL_CSS_BUS(bus);

    /* Enable hotplugging */
    qbus_set_hotplug_handler(bus, dev, &error_abort);

    return cbus;
 }

/***************** Virtual-css Bus Bridge Device ********************/

static void virtual_css_bridge_class_init(ObjectClass *klass, void *data)
{
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    hc->unplug = virtio_ccw_busdev_unplug;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo virtual_css_bridge_info = {
    .name          = TYPE_VIRTUAL_CSS_BRIDGE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = virtual_css_bridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static void virtual_css_register(void)
{
    type_register_static(&virtual_css_bridge_info);
    type_register_static(&virtual_css_bus_info);
}

type_init(virtual_css_register)
