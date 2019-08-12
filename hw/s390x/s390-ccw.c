/*
 * s390 CCW Assignment Support
 *
 * Copyright 2017 IBM Corp
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or (at your option) any later version. See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include <libgen.h>
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "hw/s390x/css.h"
#include "hw/s390x/css-bridge.h"
#include "hw/s390x/s390-ccw.h"
#include "sysemu/sysemu.h"

IOInstEnding s390_ccw_cmd_request(SubchDev *sch)
{
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(sch->driver_data);

    if (!cdc->handle_request) {
        return IOINST_CC_STATUS_PRESENT;
    }
    return cdc->handle_request(sch);
}

int s390_ccw_halt(SubchDev *sch)
{
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(sch->driver_data);

    if (!cdc->handle_halt) {
        return -ENOSYS;
    }
    return cdc->handle_halt(sch);
}

int s390_ccw_clear(SubchDev *sch)
{
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_GET_CLASS(sch->driver_data);

    if (!cdc->handle_clear) {
        return -ENOSYS;
    }
    return cdc->handle_clear(sch);
}

static void s390_ccw_get_dev_info(S390CCWDevice *cdev,
                                  char *sysfsdev,
                                  Error **errp)
{
    unsigned int cssid, ssid, devid;
    char dev_path[PATH_MAX] = {0}, *tmp;

    if (!sysfsdev) {
        error_setg(errp, "No host device provided");
        error_append_hint(errp,
                          "Use -device vfio-ccw,sysfsdev=PATH_TO_DEVICE\n");
        return;
    }

    if (!realpath(sysfsdev, dev_path)) {
        error_setg_errno(errp, errno, "Host device '%s' not found", sysfsdev);
        return;
    }

    cdev->mdevid = g_path_get_basename(dev_path);

    tmp = basename(dirname(dev_path));
    if (sscanf(tmp, "%2x.%1x.%4x", &cssid, &ssid, &devid) != 3) {
        error_setg_errno(errp, errno, "Failed to read %s", tmp);
        return;
    }

    cdev->hostid.cssid = cssid;
    cdev->hostid.ssid = ssid;
    cdev->hostid.devid = devid;
    cdev->hostid.valid = true;
}

static void s390_ccw_realize(S390CCWDevice *cdev, char *sysfsdev, Error **errp)
{
    CcwDevice *ccw_dev = CCW_DEVICE(cdev);
    CCWDeviceClass *ck = CCW_DEVICE_GET_CLASS(ccw_dev);
    DeviceState *parent = DEVICE(ccw_dev);
    SubchDev *sch;
    int ret;
    Error *err = NULL;

    s390_ccw_get_dev_info(cdev, sysfsdev, &err);
    if (err) {
        goto out_err_propagate;
    }

    sch = css_create_sch(ccw_dev->devno, &err);
    if (!sch) {
        goto out_mdevid_free;
    }
    sch->driver_data = cdev;
    sch->do_subchannel_work = do_subchannel_work_passthrough;

    ccw_dev->sch = sch;
    ret = css_sch_build_schib(sch, &cdev->hostid);
    if (ret) {
        error_setg_errno(&err, -ret, "%s: Failed to build initial schib",
                         __func__);
        goto out_err;
    }

    ck->realize(ccw_dev, &err);
    if (err) {
        goto out_err;
    }

    css_generate_sch_crws(sch->cssid, sch->ssid, sch->schid,
                          parent->hotplugged, 1);
    return;

out_err:
    css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
    ccw_dev->sch = NULL;
    g_free(sch);
out_mdevid_free:
    g_free(cdev->mdevid);
out_err_propagate:
    error_propagate(errp, err);
}

static void s390_ccw_unrealize(S390CCWDevice *cdev, Error **errp)
{
    CcwDevice *ccw_dev = CCW_DEVICE(cdev);
    SubchDev *sch = ccw_dev->sch;

    if (sch) {
        css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
        g_free(sch);
        ccw_dev->sch = NULL;
    }

    g_free(cdev->mdevid);
}

static void s390_ccw_instance_init(Object *obj)
{
    S390CCWDevice *dev = S390_CCW_DEVICE(obj);

    device_add_bootindex_property(obj, &dev->bootindex, "bootindex",
                                  "/disk@0,0", DEVICE(obj), NULL);
}

static void s390_ccw_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    S390CCWDeviceClass *cdc = S390_CCW_DEVICE_CLASS(klass);

    dc->bus_type = TYPE_VIRTUAL_CSS_BUS;
    cdc->realize = s390_ccw_realize;
    cdc->unrealize = s390_ccw_unrealize;
}

static const TypeInfo s390_ccw_info = {
    .name          = TYPE_S390_CCW,
    .parent        = TYPE_CCW_DEVICE,
    .instance_init = s390_ccw_instance_init,
    .instance_size = sizeof(S390CCWDevice),
    .class_size    = sizeof(S390CCWDeviceClass),
    .class_init    = s390_ccw_class_init,
    .abstract      = true,
};

static void register_s390_ccw_type(void)
{
    type_register_static(&s390_ccw_info);
}

type_init(register_s390_ccw_type)
