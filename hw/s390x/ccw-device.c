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
#include "hw/qdev-properties.h"
#include "qemu/module.h"

static void ccw_device_refill_ids(CcwDevice *dev)
{
    SubchDev *sch = dev->sch;

    assert(sch);

    dev->dev_id.cssid = sch->cssid;
    dev->dev_id.ssid = sch->ssid;
    dev->dev_id.devid = sch->devno;
    dev->dev_id.valid = true;

    dev->subch_id.cssid = sch->cssid;
    dev->subch_id.ssid = sch->ssid;
    dev->subch_id.devid = sch->schid;
    dev->subch_id.valid = true;
}

static void ccw_device_realize(CcwDevice *dev, Error **errp)
{
    ccw_device_refill_ids(dev);
}

static Property ccw_device_properties[] = {
    DEFINE_PROP_CSS_DEV_ID("devno", CcwDevice, devno),
    DEFINE_PROP_CSS_DEV_ID_RO("dev_id", CcwDevice, dev_id),
    DEFINE_PROP_CSS_DEV_ID_RO("subch_id", CcwDevice, subch_id),
    DEFINE_PROP_END_OF_LIST(),
};

static void ccw_device_reset(DeviceState *d)
{
    CcwDevice *ccw_dev = CCW_DEVICE(d);

    css_reset_sch(ccw_dev->sch);
}

static void ccw_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CCWDeviceClass *k = CCW_DEVICE_CLASS(klass);

    k->realize = ccw_device_realize;
    k->refill_ids = ccw_device_refill_ids;
    device_class_set_props(dc, ccw_device_properties);
    dc->reset = ccw_device_reset;
}

const VMStateDescription vmstate_ccw_dev = {
    .name = "s390_ccw_dev",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_POINTER(sch, CcwDevice, vmstate_subch_dev, SubchDev),
        VMSTATE_END_OF_LIST()
    }
};

static const TypeInfo ccw_device_info = {
    .name = TYPE_CCW_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CcwDevice),
    .class_size = sizeof(CCWDeviceClass),
    .class_init = ccw_device_class_init,
    .abstract = true,
};

static void ccw_device_register(void)
{
    type_register_static(&ccw_device_info);
}

type_init(ccw_device_register)
