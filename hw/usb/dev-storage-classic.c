/*
 * USB Mass Storage Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "hw/usb/msd.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"

static const struct SCSIBusInfo usb_msd_scsi_info_storage = {
    .tcq = false,
    .max_target = 0,
    .max_lun = 0,

    .transfer_data = usb_msd_transfer_data,
    .complete = usb_msd_command_complete,
    .cancel = usb_msd_request_cancelled,
    .load_request = usb_msd_load_request,
};

static void usb_msd_storage_realize(USBDevice *dev, Error **errp)
{
    MSDState *s = USB_STORAGE_DEV(dev);
    BlockBackend *blk = s->conf.blk;
    SCSIDevice *scsi_dev;

    if (!blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (!blkconf_blocksizes(&s->conf, errp)) {
        return;
    }

    if (!blkconf_apply_backend_options(&s->conf, !blk_supports_write_perm(blk),
                                       true, errp)) {
        return;
    }

    /*
     * Hack alert: this pretends to be a block device, but it's really
     * a SCSI bus that can serve only a single device, which it
     * creates automatically.  But first it needs to detach from its
     * blockdev, or else scsi_bus_legacy_add_drive() dies when it
     * attaches again. We also need to take another reference so that
     * blk_detach_dev() doesn't free blk while we still need it.
     *
     * The hack is probably a bad idea.
     */
    blk_ref(blk);
    blk_detach_dev(blk, DEVICE(s));
    s->conf.blk = NULL;

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    dev->flags |= (1 << USB_DEV_FLAG_IS_SCSI_STORAGE);
    scsi_bus_init(&s->bus, sizeof(s->bus), DEVICE(dev),
                 &usb_msd_scsi_info_storage);
    scsi_dev = scsi_bus_legacy_add_drive(&s->bus, blk, 0, !!s->removable,
                                         s->conf.bootindex, s->conf.share_rw,
                                         s->conf.rerror, s->conf.werror,
                                         dev->serial,
                                         errp);
    blk_unref(blk);
    if (!scsi_dev) {
        return;
    }
    usb_msd_handle_reset(dev);
    s->scsi_dev = scsi_dev;
}

static Property msd_properties[] = {
    DEFINE_BLOCK_PROPERTIES(MSDState, conf),
    DEFINE_BLOCK_ERROR_PROPERTIES(MSDState, conf),
    DEFINE_PROP_BOOL("removable", MSDState, removable, false),
    DEFINE_PROP_BOOL("commandlog", MSDState, commandlog, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_msd_class_storage_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize = usb_msd_storage_realize;
    device_class_set_props(dc, msd_properties);
}

static void usb_msd_get_bootindex(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    USBDevice *dev = USB_DEVICE(obj);
    MSDState *s = USB_STORAGE_DEV(dev);

    visit_type_int32(v, name, &s->conf.bootindex, errp);
}

static void usb_msd_set_bootindex(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    USBDevice *dev = USB_DEVICE(obj);
    MSDState *s = USB_STORAGE_DEV(dev);
    int32_t boot_index;
    Error *local_err = NULL;

    if (!visit_type_int32(v, name, &boot_index, errp)) {
        return;
    }
    /* check whether bootindex is present in fw_boot_order list  */
    check_boot_index(boot_index, &local_err);
    if (local_err) {
        goto out;
    }
    /* change bootindex to a new one */
    s->conf.bootindex = boot_index;

    if (s->scsi_dev) {
        object_property_set_int(OBJECT(s->scsi_dev), "bootindex", boot_index,
                                &error_abort);
    }

out:
    error_propagate(errp, local_err);
}

static void usb_msd_instance_init(Object *obj)
{
    object_property_add(obj, "bootindex", "int32",
                        usb_msd_get_bootindex,
                        usb_msd_set_bootindex, NULL, NULL);
    object_property_set_int(obj, "bootindex", -1, NULL);
}

static const TypeInfo msd_info = {
    .name          = "usb-storage",
    .parent        = TYPE_USB_STORAGE,
    .class_init    = usb_msd_class_storage_initfn,
    .instance_init = usb_msd_instance_init,
};

static void register_types(void)
{
    type_register_static(&msd_info);
}

type_init(register_types)
