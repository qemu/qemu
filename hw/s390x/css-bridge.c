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
#include "hw/hotplug.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "hw/s390x/css.h"
#include "ccw-device.h"
#include "hw/s390x/css-bridge.h"
#include "cpu.h"

/*
 * Invoke device-specific unplug handler, disable the subchannel
 * (including sending a channel report to the guest) and remove the
 * device from the virtual css bus.
 */
static void ccw_device_unplug(HotplugHandler *hotplug_dev,
                              DeviceState *dev, Error **errp)
{
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    CCWDeviceClass *k = CCW_DEVICE_GET_CLASS(ccw_dev);
    SubchDev *sch = ccw_dev->sch;
    Error *err = NULL;

    if (k->unplug) {
        k->unplug(hotplug_dev, dev, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    /*
     * We should arrive here only for device_del, since we don't support
     * direct hot(un)plug of channels.
     */
    assert(sch != NULL);
    /* Subchannel is now disabled and no longer valid. */
    sch->curr_status.pmcw.flags &= ~(PMCW_FLAGS_MASK_ENA |
                                     PMCW_FLAGS_MASK_DNV);

    css_generate_sch_crws(sch->cssid, sch->ssid, sch->schid, 1, 0);

    object_property_set_bool(OBJECT(dev), false, "realized", NULL);
}

static void virtual_css_bus_reset(BusState *qbus)
{
    /* This should actually be modelled via the generic css */
    css_reset();
}

static char *virtual_css_bus_get_dev_path(DeviceState *dev)
{
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    SubchDev *sch = ccw_dev->sch;
    VirtualCssBridge *bridge =
        VIRTUAL_CSS_BRIDGE(qdev_get_parent_bus(dev)->parent);

    /*
     * We can't provide a dev path for backward compatibility on
     * older machines, as it is visible in the migration stream.
     */
    return bridge->css_dev_path ?
        g_strdup_printf("/%02x.%1x.%04x", sch->cssid, sch->ssid, sch->devno) :
        NULL;
}

static void virtual_css_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->reset = virtual_css_bus_reset;
    k->get_dev_path = virtual_css_bus_get_dev_path;
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
    object_property_add_child(qdev_get_machine(), TYPE_VIRTUAL_CSS_BRIDGE,
                              OBJECT(dev), NULL);
    qdev_init_nofail(dev);

    /* Create bus on bridge device */
    bus = qbus_create(TYPE_VIRTUAL_CSS_BUS, dev, "virtual-css");
    cbus = VIRTUAL_CSS_BUS(bus);

    /* Enable hotplugging */
    qbus_set_hotplug_handler(bus, OBJECT(dev), &error_abort);

    css_register_io_adapters(CSS_IO_ADAPTER_VIRTIO, true, false,
                             0, &error_abort);

    return cbus;
 }

/***************** Virtual-css Bus Bridge Device ********************/

static Property virtual_css_bridge_properties[] = {
    DEFINE_PROP_BOOL("css_dev_path", VirtualCssBridge, css_dev_path,
                     true),
    DEFINE_PROP_END_OF_LIST(),
};

static bool prop_get_true(Object *obj, Error **errp)
{
    return true;
}

static void virtual_css_bridge_class_init(ObjectClass *klass, void *data)
{
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    hc->unplug = ccw_device_unplug;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    device_class_set_props(dc, virtual_css_bridge_properties);
    object_class_property_add_bool(klass, "cssid-unrestricted",
                                   prop_get_true, NULL, NULL);
    object_class_property_set_description(klass, "cssid-unrestricted",
            "A css device can use any cssid, regardless whether virtual"
            " or not (read only, always true)",
            NULL);
}

static const TypeInfo virtual_css_bridge_info = {
    .name          = TYPE_VIRTUAL_CSS_BRIDGE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtualCssBridge),
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
