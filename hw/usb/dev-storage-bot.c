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
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "hw/usb/msd.h"

static const struct SCSIBusInfo usb_msd_scsi_info_bot = {
    .tcq = false,
    .max_target = 0,
    .max_lun = 15,

    .transfer_data = usb_msd_transfer_data,
    .complete = usb_msd_command_complete,
    .cancel = usb_msd_request_cancelled,
    .load_request = usb_msd_load_request,
};

static void usb_msd_bot_realize(USBDevice *dev, Error **errp)
{
    MSDState *s = USB_STORAGE_DEV(dev);
    DeviceState *d = DEVICE(dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    dev->flags |= (1 << USB_DEV_FLAG_IS_SCSI_STORAGE);
    if (d->hotplugged) {
        s->dev.auto_attach = 0;
    }

    scsi_bus_init(&s->bus, sizeof(s->bus), DEVICE(dev), &usb_msd_scsi_info_bot);
    usb_msd_handle_reset(dev);
}

static void usb_msd_class_bot_initfn(ObjectClass *klass, void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize = usb_msd_bot_realize;
    uc->attached_settable = true;
}

static const TypeInfo bot_info = {
    .name          = "usb-bot",
    .parent        = TYPE_USB_STORAGE,
    .class_init    = usb_msd_class_bot_initfn,
};

static void register_types(void)
{
    type_register_static(&bot_info);
}

type_init(register_types)
