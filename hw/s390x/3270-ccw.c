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

/* Handle READ ccw commands from guest */
static int handle_payload_3270_read(EmulatedCcw3270Device *dev, CCW1 *ccw)
{
    EmulatedCcw3270Class *ck = EMULATED_CCW_3270_GET_CLASS(dev);
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    int len;

    if (!ccw->cda) {
        return -EFAULT;
    }

    len = ck->read_payload_3270(dev);
    ccw_dev->sch->curr_status.scsw.count = ccw->count - len;

    return 0;
}

/* Handle WRITE ccw commands to write data to client */
static int handle_payload_3270_write(EmulatedCcw3270Device *dev, CCW1 *ccw)
{
    EmulatedCcw3270Class *ck = EMULATED_CCW_3270_GET_CLASS(dev);
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    int len;

    if (!ccw->cda) {
        return -EFAULT;
    }

    len = ck->write_payload_3270(dev, ccw->cmd_code);

    if (len <= 0) {
        return -EIO;
    }

    ccw_dev->sch->curr_status.scsw.count = ccw->count - len;
    return 0;
}

static int emulated_ccw_3270_cb(SubchDev *sch, CCW1 ccw)
{
    int rc = 0;
    EmulatedCcw3270Device *dev = sch->driver_data;

    switch (ccw.cmd_code) {
    case TC_WRITESF:
    case TC_WRITE:
    case TC_EWRITE:
    case TC_EWRITEA:
        rc = handle_payload_3270_write(dev, &ccw);
        break;
    case TC_RDBUF:
    case TC_READMOD:
        rc = handle_payload_3270_read(dev, &ccw);
        break;
    default:
        rc = -ENOSYS;
        break;
    }

    if (rc == -EIO) {
        /* I/O error, specific devices generate specific conditions */
        SCHIB *schib = &sch->curr_status;

        sch->curr_status.scsw.dstat = SCSW_DSTAT_UNIT_CHECK;
        sch->sense_data[0] = 0x40;    /* intervention-req */
        schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
        schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
        schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                   SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
    }

    return rc;
}

static void emulated_ccw_3270_realize(DeviceState *ds, Error **errp)
{
    uint16_t chpid;
    EmulatedCcw3270Device *dev = EMULATED_CCW_3270(ds);
    EmulatedCcw3270Class *ck = EMULATED_CCW_3270_GET_CLASS(dev);
    CcwDevice *cdev = CCW_DEVICE(ds);
    CCWDeviceClass *cdk = CCW_DEVICE_GET_CLASS(cdev);
    SubchDev *sch;
    Error *err = NULL;

    sch = css_create_sch(cdev->devno, errp);
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
    sch->do_subchannel_work = do_subchannel_work_virtual;
    sch->ccw_cb = emulated_ccw_3270_cb;

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
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
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
