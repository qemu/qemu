/*
 * Emulated ccw-attached 3270 implementation
 *
 * Copyright 2017 IBM Corp.
 * Author(s): Yang Chen <bjcyang@linux.vnet.ibm.com>
 *            Jing Liu <liujbjl@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "cpu.h"
#include "hw/s390x/css.h"
#include "hw/s390x/css-bridge.h"
#include "hw/s390x/3270-ccw.h"

static void emulated_ccw_3270_realize(DeviceState *ds, Error **errp)
{
    uint16_t chpid;
    EmulatedCcw3270Device *dev = EMULATED_CCW_3270(ds);
    EmulatedCcw3270Class *ck = EMULATED_CCW_3270_GET_CLASS(dev);
    CcwDevice *cdev = CCW_DEVICE(ds);
    CCWDeviceClass *cdk = CCW_DEVICE_GET_CLASS(cdev);
    SubchDev *sch = css_create_virtual_sch(cdev->devno, errp);
    Error *err = NULL;

    if (!sch) {
        return;
    }

    if (!ck->init) {
        goto out_err;
    }

    sch->driver_data = dev;
    cdev->sch = sch;
    chpid = css_find_free_chpid(sch->cssid);

    if (chpid > MAX_CHPID) {
        error_setg(&err, "No available chpid to use.");
        goto out_err;
    }

    sch->id.reserved = 0xff;
    sch->id.cu_type = EMULATED_CCW_3270_CU_TYPE;
    css_sch_build_virtual_schib(sch, (uint8_t)chpid,
                                EMULATED_CCW_3270_CHPID_TYPE);

    ck->init(dev, &err);
    if (err) {
        goto out_err;
    }

    cdk->realize(cdev, &err);
    if (err) {
        goto out_err;
    }

    return;

out_err:
    error_propagate(errp, err);
    css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
    cdev->sch = NULL;
    g_free(sch);
}

static Property emulated_ccw_3270_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void emulated_ccw_3270_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = emulated_ccw_3270_properties;
    dc->bus_type = TYPE_VIRTUAL_CSS_BUS;
    dc->realize = emulated_ccw_3270_realize;
    dc->hotpluggable = false;
}

static const TypeInfo emulated_ccw_3270_info = {
    .name = TYPE_EMULATED_CCW_3270,
    .parent = TYPE_CCW_DEVICE,
    .instance_size = sizeof(EmulatedCcw3270Device),
    .class_init = emulated_ccw_3270_class_init,
    .class_size = sizeof(EmulatedCcw3270Class),
    .abstract = true,
};

static void emulated_ccw_register(void)
{
    type_register_static(&emulated_ccw_3270_info);
}

type_init(emulated_ccw_register)
