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
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "hw/usb.h"
#include "hw/usb/msd.h"
#include "desc.h"
#include "hw/qdev-properties.h"
#include "hw/scsi/scsi.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qom/object.h"
#include "trace.h"

/* USB requests.  */
#define MassStorageReset  0xff
#define GetMaxLun         0xfe

struct usb_msd_cbw {
    uint32_t sig;
    uint32_t tag;
    uint32_t data_len;
    uint8_t flags;
    uint8_t lun;
    uint8_t cmd_len;
    uint8_t cmd[16];
};

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_CONFIG_FULL,
    STR_CONFIG_HIGH,
    STR_CONFIG_SUPER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT]      = "QEMU USB HARDDRIVE",
    [STR_SERIALNUMBER] = "1",
    [STR_CONFIG_FULL]  = "Full speed config (usb 1.1)",
    [STR_CONFIG_HIGH]  = "High speed config (usb 2.0)",
    [STR_CONFIG_SUPER] = "Super speed config (usb 3.0)",
};

static const USBDescIface desc_iface_full = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x50, /* Bulk */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },
    }
};

static const USBDescDevice desc_device_full = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_FULL,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_full,
        },
    },
};

static const USBDescIface desc_iface_high = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x50, /* Bulk */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
        },
    }
};

static const USBDescDevice desc_device_high = {
    .bcdUSB                        = 0x0200,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_HIGH,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_high,
        },
    },
};

static const USBDescIface desc_iface_super = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x50, /* Bulk */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
        },
    }
};

static const USBDescDevice desc_device_super = {
    .bcdUSB                        = 0x0300,
    .bMaxPacketSize0               = 9,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .iConfiguration        = STR_CONFIG_SUPER,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .nif = 1,
            .ifs = &desc_iface_super,
        },
    },
};

static const USBDesc desc = {
    .id = {
        .idVendor          = 0x46f4, /* CRC16() of "QEMU" */
        .idProduct         = 0x0001,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full  = &desc_device_full,
    .high  = &desc_device_high,
    .super = &desc_device_super,
    .str   = desc_strings,
};

static void usb_msd_packet_complete(MSDState *s)
{
    USBPacket *p = s->packet;

    /*
     * Set s->packet to NULL before calling usb_packet_complete
     * because another request may be issued before
     * usb_packet_complete returns.
     */
    trace_usb_msd_packet_complete();
    s->packet = NULL;
    usb_packet_complete(&s->dev, p);
}

static void usb_msd_fatal_error(MSDState *s)
{
    trace_usb_msd_fatal_error();

    if (s->packet) {
        s->packet->status = USB_RET_STALL;
        usb_msd_packet_complete(s);
    }

    /*
     * Guest messed up up device state with illegal requests.  Go
     * ignore any requests until the guests resets the device (and
     * brings it into a known state that way).
     */
    s->needs_reset = true;
}

static void usb_msd_copy_data(MSDState *s, USBPacket *p)
{
    uint32_t len;
    len = p->iov.size - p->actual_length;
    if (len > s->scsi_len)
        len = s->scsi_len;
    usb_packet_copy(p, scsi_req_get_buf(s->req) + s->scsi_off, len);
    s->scsi_len -= len;
    s->scsi_off += len;
    if (len > s->data_len) {
        len = s->data_len;
    }
    s->data_len -= len;
    if (s->scsi_len == 0 || s->data_len == 0) {
        scsi_req_continue(s->req);
    }
}

static void usb_msd_send_status(MSDState *s, USBPacket *p)
{
    int len;

    trace_usb_msd_send_status(s->csw.status, le32_to_cpu(s->csw.tag),
                              p->iov.size);

    assert(s->csw.sig == cpu_to_le32(0x53425355));
    len = MIN(sizeof(s->csw), p->iov.size);
    usb_packet_copy(p, &s->csw, len);
    memset(&s->csw, 0, sizeof(s->csw));
}

void usb_msd_transfer_data(SCSIRequest *req, uint32_t len)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);
    USBPacket *p = s->packet;

    if ((s->mode == USB_MSDM_DATAOUT) != (req->cmd.mode == SCSI_XFER_TO_DEV)) {
        usb_msd_fatal_error(s);
        return;
    }

    s->scsi_len = len;
    s->scsi_off = 0;
    if (p) {
        usb_msd_copy_data(s, p);
        p = s->packet;
        if (p && p->actual_length == p->iov.size) {
            p->status = USB_RET_SUCCESS; /* Clear previous ASYNC status */
            usb_msd_packet_complete(s);
        }
    }
}

void usb_msd_command_complete(SCSIRequest *req, size_t resid)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);
    USBPacket *p = s->packet;

    trace_usb_msd_cmd_complete(req->status, req->tag);

    s->csw.sig = cpu_to_le32(0x53425355);
    s->csw.tag = cpu_to_le32(req->tag);
    s->csw.residue = cpu_to_le32(s->data_len);
    s->csw.status = req->status != 0;

    if (s->packet) {
        if (s->data_len == 0 && s->mode == USB_MSDM_DATAOUT) {
            /* A deferred packet with no write data remaining must be
               the status read packet.  */
            usb_msd_send_status(s, p);
            s->mode = USB_MSDM_CBW;
        } else if (s->mode == USB_MSDM_CSW) {
            usb_msd_send_status(s, p);
            s->mode = USB_MSDM_CBW;
        } else {
            if (s->data_len) {
                int len = (p->iov.size - p->actual_length);
                usb_packet_skip(p, len);
                if (len > s->data_len) {
                    len = s->data_len;
                }
                s->data_len -= len;
            }
            if (s->data_len == 0) {
                s->mode = USB_MSDM_CSW;
            }
        }
        p->status = USB_RET_SUCCESS; /* Clear previous ASYNC status */
        usb_msd_packet_complete(s);
    } else if (s->data_len == 0) {
        s->mode = USB_MSDM_CSW;
    }
    scsi_req_unref(req);
    s->req = NULL;
}

void usb_msd_request_cancelled(SCSIRequest *req)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);

    trace_usb_msd_cmd_cancel(req->tag);

    if (req == s->req) {
        s->csw.sig = cpu_to_le32(0x53425355);
        s->csw.tag = cpu_to_le32(req->tag);
        s->csw.status = 1; /* error */

        scsi_req_unref(s->req);
        s->req = NULL;
        s->scsi_len = 0;
    }
}

void usb_msd_handle_reset(USBDevice *dev)
{
    MSDState *s = (MSDState *)dev;

    trace_usb_msd_reset();
    if (s->req) {
        scsi_req_cancel(s->req);
    }
    assert(s->req == NULL);

    if (s->packet) {
        s->packet->status = USB_RET_STALL;
        usb_msd_packet_complete(s);
    }

    memset(&s->csw, 0, sizeof(s->csw));
    s->mode = USB_MSDM_CBW;

    s->needs_reset = false;
}

static void usb_msd_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    MSDState *s = (MSDState *)dev;
    SCSIDevice *scsi_dev;
    int ret, maxlun;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        break;
        /* Class specific requests.  */
    case ClassInterfaceOutRequest | MassStorageReset:
        /* Reset state ready for the next CBW.  */
        s->mode = USB_MSDM_CBW;
        break;
    case ClassInterfaceRequest | GetMaxLun:
        maxlun = 0;
        for (;;) {
            scsi_dev = scsi_device_find(&s->bus, 0, 0, maxlun+1);
            if (scsi_dev == NULL) {
                break;
            }
            if (scsi_dev->lun != maxlun+1) {
                break;
            }
            maxlun++;
        }
        trace_usb_msd_maxlun(maxlun);
        data[0] = maxlun;
        p->actual_length = 1;
        break;
    default:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_msd_cancel_io(USBDevice *dev, USBPacket *p)
{
    MSDState *s = USB_STORAGE_DEV(dev);

    assert(s->packet == p);
    s->packet = NULL;

    if (s->req) {
        scsi_req_cancel(s->req);
    }
}

static void usb_msd_handle_data(USBDevice *dev, USBPacket *p)
{
    MSDState *s = (MSDState *)dev;
    uint32_t tag;
    struct usb_msd_cbw cbw;
    uint8_t devep = p->ep->nr;
    SCSIDevice *scsi_dev;
    uint32_t len;

    if (s->needs_reset) {
        p->status = USB_RET_STALL;
        return;
    }

    switch (p->pid) {
    case USB_TOKEN_OUT:
        if (devep != 2)
            goto fail;

        switch (s->mode) {
        case USB_MSDM_CBW:
            if (p->iov.size != 31) {
                error_report("usb-msd: Bad CBW size");
                goto fail;
            }
            usb_packet_copy(p, &cbw, 31);
            if (le32_to_cpu(cbw.sig) != 0x43425355) {
                error_report("usb-msd: Bad signature %08x",
                             le32_to_cpu(cbw.sig));
                goto fail;
            }
            scsi_dev = scsi_device_find(&s->bus, 0, 0, cbw.lun);
            if (scsi_dev == NULL) {
                error_report("usb-msd: Bad LUN %d", cbw.lun);
                goto fail;
            }
            tag = le32_to_cpu(cbw.tag);
            s->data_len = le32_to_cpu(cbw.data_len);
            if (s->data_len == 0) {
                s->mode = USB_MSDM_CSW;
            } else if (cbw.flags & 0x80) {
                s->mode = USB_MSDM_DATAIN;
            } else {
                s->mode = USB_MSDM_DATAOUT;
            }
            trace_usb_msd_cmd_submit(cbw.lun, tag, cbw.flags,
                                     cbw.cmd_len, s->data_len);
            assert(le32_to_cpu(s->csw.residue) == 0);
            s->scsi_len = 0;
            s->req = scsi_req_new(scsi_dev, tag, cbw.lun, cbw.cmd, cbw.cmd_len, NULL);
            if (s->commandlog) {
                scsi_req_print(s->req);
            }
            len = scsi_req_enqueue(s->req);
            if (len) {
                scsi_req_continue(s->req);
            }
            break;

        case USB_MSDM_DATAOUT:
            trace_usb_msd_data_out(p->iov.size, s->data_len);
            if (p->iov.size > s->data_len) {
                goto fail;
            }

            if (s->scsi_len) {
                usb_msd_copy_data(s, p);
            }
            if (le32_to_cpu(s->csw.residue)) {
                int len = p->iov.size - p->actual_length;
                if (len) {
                    usb_packet_skip(p, len);
                    if (len > s->data_len) {
                        len = s->data_len;
                    }
                    s->data_len -= len;
                    if (s->data_len == 0) {
                        s->mode = USB_MSDM_CSW;
                    }
                }
            }
            if (p->actual_length < p->iov.size) {
                trace_usb_msd_packet_async();
                s->packet = p;
                p->status = USB_RET_ASYNC;
            }
            break;

        default:
            goto fail;
        }
        break;

    case USB_TOKEN_IN:
        if (devep != 1)
            goto fail;

        switch (s->mode) {
        case USB_MSDM_DATAOUT:
            if (s->data_len != 0 || p->iov.size < 13) {
                goto fail;
            }
            /* Waiting for SCSI write to complete.  */
            trace_usb_msd_packet_async();
            s->packet = p;
            p->status = USB_RET_ASYNC;
            break;

        case USB_MSDM_CSW:
            if (p->iov.size < 13) {
                goto fail;
            }

            if (s->req) {
                /* still in flight */
                trace_usb_msd_packet_async();
                s->packet = p;
                p->status = USB_RET_ASYNC;
            } else {
                usb_msd_send_status(s, p);
                s->mode = USB_MSDM_CBW;
            }
            break;

        case USB_MSDM_DATAIN:
            trace_usb_msd_data_in(p->iov.size, s->data_len, s->scsi_len);
            if (s->scsi_len) {
                usb_msd_copy_data(s, p);
            }
            if (le32_to_cpu(s->csw.residue)) {
                int len = p->iov.size - p->actual_length;
                if (len) {
                    usb_packet_skip(p, len);
                    if (len > s->data_len) {
                        len = s->data_len;
                    }
                    s->data_len -= len;
                    if (s->data_len == 0) {
                        s->mode = USB_MSDM_CSW;
                    }
                }
            }
            if (p->actual_length < p->iov.size && s->mode == USB_MSDM_DATAIN) {
                trace_usb_msd_packet_async();
                s->packet = p;
                p->status = USB_RET_ASYNC;
            }
            break;

        default:
            goto fail;
        }
        break;

    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

void *usb_msd_load_request(QEMUFile *f, SCSIRequest *req)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);

    /* nothing to load, just store req in our state struct */
    assert(s->req == NULL);
    scsi_req_ref(req);
    s->req = req;
    return NULL;
}

static const VMStateDescription vmstate_usb_msd = {
    .name = "usb-storage",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, MSDState),
        VMSTATE_UINT32(mode, MSDState),
        VMSTATE_UINT32(scsi_len, MSDState),
        VMSTATE_UINT32(scsi_off, MSDState),
        VMSTATE_UINT32(data_len, MSDState),
        VMSTATE_UINT32(csw.sig, MSDState),
        VMSTATE_UINT32(csw.tag, MSDState),
        VMSTATE_UINT32(csw.residue, MSDState),
        VMSTATE_UINT8(csw.status, MSDState),
        VMSTATE_END_OF_LIST()
    }
};

static void usb_msd_class_initfn_common(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = "QEMU USB MSD";
    uc->usb_desc       = &desc;
    uc->cancel_packet  = usb_msd_cancel_io;
    uc->handle_attach  = usb_desc_attach;
    uc->handle_reset   = usb_msd_handle_reset;
    uc->handle_control = usb_msd_handle_control;
    uc->handle_data    = usb_msd_handle_data;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->fw_name = "storage";
    dc->vmsd = &vmstate_usb_msd;
}

static const TypeInfo usb_storage_dev_type_info = {
    .name = TYPE_USB_STORAGE,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(MSDState),
    .abstract = true,
    .class_init = usb_msd_class_initfn_common,
};

static void usb_msd_register_types(void)
{
    type_register_static(&usb_storage_dev_type_info);
}

type_init(usb_msd_register_types)
