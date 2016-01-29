/*
 * USB Mass Storage Device emulation
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "hw/scsi/scsi.h"
#include "ui/console.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "qapi/visitor.h"

//#define DEBUG_MSD

#ifdef DEBUG_MSD
#define DPRINTF(fmt, ...) \
do { printf("usb-msd: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

/* USB requests.  */
#define MassStorageReset  0xff
#define GetMaxLun         0xfe

enum USBMSDMode {
    USB_MSDM_CBW, /* Command Block.  */
    USB_MSDM_DATAOUT, /* Transfer data to device.  */
    USB_MSDM_DATAIN, /* Transfer data from device.  */
    USB_MSDM_CSW /* Command Status.  */
};

struct usb_msd_csw {
    uint32_t sig;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
};

typedef struct {
    USBDevice dev;
    enum USBMSDMode mode;
    uint32_t scsi_off;
    uint32_t scsi_len;
    uint32_t data_len;
    struct usb_msd_csw csw;
    SCSIRequest *req;
    SCSIBus bus;
    /* For async completion.  */
    USBPacket *packet;
    /* usb-storage only */
    BlockConf conf;
    uint32_t removable;
    SCSIDevice *scsi_dev;
} MSDState;

#define TYPE_USB_STORAGE "usb-storage-dev"
#define USB_STORAGE_DEV(obj) OBJECT_CHECK(MSDState, (obj), TYPE_USB_STORAGE)

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

static void usb_msd_copy_data(MSDState *s, USBPacket *p)
{
    uint32_t len;
    len = p->iov.size - p->actual_length;
    if (len > s->scsi_len)
        len = s->scsi_len;
    usb_packet_copy(p, scsi_req_get_buf(s->req) + s->scsi_off, len);
    s->scsi_len -= len;
    s->scsi_off += len;
    s->data_len -= len;
    if (s->scsi_len == 0 || s->data_len == 0) {
        scsi_req_continue(s->req);
    }
}

static void usb_msd_send_status(MSDState *s, USBPacket *p)
{
    int len;

    DPRINTF("Command status %d tag 0x%x, len %zd\n",
            s->csw.status, le32_to_cpu(s->csw.tag), p->iov.size);

    assert(s->csw.sig == cpu_to_le32(0x53425355));
    len = MIN(sizeof(s->csw), p->iov.size);
    usb_packet_copy(p, &s->csw, len);
    memset(&s->csw, 0, sizeof(s->csw));
}

static void usb_msd_packet_complete(MSDState *s)
{
    USBPacket *p = s->packet;

    /* Set s->packet to NULL before calling usb_packet_complete
       because another request may be issued before
       usb_packet_complete returns.  */
    DPRINTF("Packet complete %p\n", p);
    s->packet = NULL;
    usb_packet_complete(&s->dev, p);
}

static void usb_msd_transfer_data(SCSIRequest *req, uint32_t len)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);
    USBPacket *p = s->packet;

    assert((s->mode == USB_MSDM_DATAOUT) == (req->cmd.mode == SCSI_XFER_TO_DEV));
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

static void usb_msd_command_complete(SCSIRequest *req, uint32_t status, size_t resid)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);
    USBPacket *p = s->packet;

    DPRINTF("Command complete %d tag 0x%x\n", status, req->tag);

    s->csw.sig = cpu_to_le32(0x53425355);
    s->csw.tag = cpu_to_le32(req->tag);
    s->csw.residue = cpu_to_le32(s->data_len);
    s->csw.status = status != 0;

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

static void usb_msd_request_cancelled(SCSIRequest *req)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);

    if (req == s->req) {
        scsi_req_unref(s->req);
        s->req = NULL;
        s->scsi_len = 0;
    }
}

static void usb_msd_handle_reset(USBDevice *dev)
{
    MSDState *s = (MSDState *)dev;

    DPRINTF("Reset\n");
    if (s->req) {
        scsi_req_cancel(s->req);
    }
    assert(s->req == NULL);

    if (s->packet) {
        s->packet->status = USB_RET_STALL;
        usb_msd_packet_complete(s);
    }

    s->mode = USB_MSDM_CBW;
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
        DPRINTF("MaxLun %d\n", maxlun);
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
            DPRINTF("Command on LUN %d\n", cbw.lun);
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
            DPRINTF("Command tag 0x%x flags %08x len %d data %d\n",
                    tag, cbw.flags, cbw.cmd_len, s->data_len);
            assert(le32_to_cpu(s->csw.residue) == 0);
            s->scsi_len = 0;
            s->req = scsi_req_new(scsi_dev, tag, cbw.lun, cbw.cmd, NULL);
#ifdef DEBUG_MSD
            scsi_req_print(s->req);
#endif
            len = scsi_req_enqueue(s->req);
            if (len) {
                scsi_req_continue(s->req);
            }
            break;

        case USB_MSDM_DATAOUT:
            DPRINTF("Data out %zd/%d\n", p->iov.size, s->data_len);
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
                    s->data_len -= len;
                    if (s->data_len == 0) {
                        s->mode = USB_MSDM_CSW;
                    }
                }
            }
            if (p->actual_length < p->iov.size) {
                DPRINTF("Deferring packet %p [wait data-out]\n", p);
                s->packet = p;
                p->status = USB_RET_ASYNC;
            }
            break;

        default:
            DPRINTF("Unexpected write (len %zd)\n", p->iov.size);
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
            s->packet = p;
            p->status = USB_RET_ASYNC;
            break;

        case USB_MSDM_CSW:
            if (p->iov.size < 13) {
                goto fail;
            }

            if (s->req) {
                /* still in flight */
                DPRINTF("Deferring packet %p [wait status]\n", p);
                s->packet = p;
                p->status = USB_RET_ASYNC;
            } else {
                usb_msd_send_status(s, p);
                s->mode = USB_MSDM_CBW;
            }
            break;

        case USB_MSDM_DATAIN:
            DPRINTF("Data in %zd/%d, scsi_len %d\n",
                    p->iov.size, s->data_len, s->scsi_len);
            if (s->scsi_len) {
                usb_msd_copy_data(s, p);
            }
            if (le32_to_cpu(s->csw.residue)) {
                int len = p->iov.size - p->actual_length;
                if (len) {
                    usb_packet_skip(p, len);
                    s->data_len -= len;
                    if (s->data_len == 0) {
                        s->mode = USB_MSDM_CSW;
                    }
                }
            }
            if (p->actual_length < p->iov.size) {
                DPRINTF("Deferring packet %p [wait data-in]\n", p);
                s->packet = p;
                p->status = USB_RET_ASYNC;
            }
            break;

        default:
            DPRINTF("Unexpected read (len %zd)\n", p->iov.size);
            goto fail;
        }
        break;

    default:
        DPRINTF("Bad token\n");
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_msd_password_cb(void *opaque, int err)
{
    MSDState *s = opaque;
    Error *local_err = NULL;

    if (!err) {
        usb_device_attach(&s->dev, &local_err);
    }

    if (local_err) {
        error_report_err(local_err);
        qdev_unplug(&s->dev.qdev, NULL);
    }
}

static void *usb_msd_load_request(QEMUFile *f, SCSIRequest *req)
{
    MSDState *s = DO_UPCAST(MSDState, dev.qdev, req->bus->qbus.parent);

    /* nothing to load, just store req in our state struct */
    assert(s->req == NULL);
    scsi_req_ref(req);
    s->req = req;
    return NULL;
}

static const struct SCSIBusInfo usb_msd_scsi_info_storage = {
    .tcq = false,
    .max_target = 0,
    .max_lun = 0,

    .transfer_data = usb_msd_transfer_data,
    .complete = usb_msd_command_complete,
    .cancel = usb_msd_request_cancelled,
    .load_request = usb_msd_load_request,
};

static const struct SCSIBusInfo usb_msd_scsi_info_bot = {
    .tcq = false,
    .max_target = 0,
    .max_lun = 15,

    .transfer_data = usb_msd_transfer_data,
    .complete = usb_msd_command_complete,
    .cancel = usb_msd_request_cancelled,
    .load_request = usb_msd_load_request,
};

static void usb_msd_realize_storage(USBDevice *dev, Error **errp)
{
    MSDState *s = USB_STORAGE_DEV(dev);
    BlockBackend *blk = s->conf.blk;
    SCSIDevice *scsi_dev;
    Error *err = NULL;

    if (!blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    if (blk_bs(blk)) {
        bdrv_add_key(blk_bs(blk), NULL, &err);
        if (err) {
            if (monitor_cur_is_qmp()) {
                error_propagate(errp, err);
                return;
            }
            error_free(err);
            err = NULL;
            if (cur_mon) {
                monitor_read_bdrv_key_start(cur_mon, blk_bs(blk),
                                            usb_msd_password_cb, s);
                s->dev.auto_attach = 0;
            } else {
                autostart = 0;
            }
        }
    }

    blkconf_serial(&s->conf, &dev->serial);
    blkconf_blocksizes(&s->conf);

    /*
     * Hack alert: this pretends to be a block device, but it's really
     * a SCSI bus that can serve only a single device, which it
     * creates automatically.  But first it needs to detach from its
     * blockdev, or else scsi_bus_legacy_add_drive() dies when it
     * attaches again.
     *
     * The hack is probably a bad idea.
     */
    blk_detach_dev(blk, &s->dev.qdev);
    s->conf.blk = NULL;

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    scsi_bus_new(&s->bus, sizeof(s->bus), DEVICE(dev),
                 &usb_msd_scsi_info_storage, NULL);
    scsi_dev = scsi_bus_legacy_add_drive(&s->bus, blk, 0, !!s->removable,
                                         s->conf.bootindex, dev->serial,
                                         &err);
    if (!scsi_dev) {
        error_propagate(errp, err);
        return;
    }
    usb_msd_handle_reset(dev);
    s->scsi_dev = scsi_dev;
}

static void usb_msd_realize_bot(USBDevice *dev, Error **errp)
{
    MSDState *s = USB_STORAGE_DEV(dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    scsi_bus_new(&s->bus, sizeof(s->bus), DEVICE(dev),
                 &usb_msd_scsi_info_bot, NULL);
    usb_msd_handle_reset(dev);
}

static USBDevice *usb_msd_init(USBBus *bus, const char *filename)
{
    static int nr=0;
    Error *err = NULL;
    char id[8];
    QemuOpts *opts;
    DriveInfo *dinfo;
    USBDevice *dev;
    const char *p1;
    char fmt[32];

    /* parse -usbdevice disk: syntax into drive opts */
    do {
        snprintf(id, sizeof(id), "usb%d", nr++);
        opts = qemu_opts_create(qemu_find_opts("drive"), id, 1, NULL);
    } while (!opts);

    p1 = strchr(filename, ':');
    if (p1++) {
        const char *p2;

        if (strstart(filename, "format=", &p2)) {
            int len = MIN(p1 - p2, sizeof(fmt));
            pstrcpy(fmt, len, p2);
            qemu_opt_set(opts, "format", fmt, &error_abort);
        } else if (*filename != ':') {
            error_report("unrecognized USB mass-storage option %s", filename);
            return NULL;
        }
        filename = p1;
    }
    if (!*filename) {
        error_report("block device specification needed");
        return NULL;
    }
    qemu_opt_set(opts, "file", filename, &error_abort);
    qemu_opt_set(opts, "if", "none", &error_abort);

    /* create host drive */
    dinfo = drive_new(opts, 0);
    if (!dinfo) {
        qemu_opts_del(opts);
        return NULL;
    }

    /* create guest device */
    dev = usb_create(bus, "usb-storage");
    qdev_prop_set_drive(&dev->qdev, "drive", blk_by_legacy_dinfo(dinfo),
                        &err);
    if (err) {
        error_report_err(err);
        object_unparent(OBJECT(dev));
        return NULL;
    }
    return dev;
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

static Property msd_properties[] = {
    DEFINE_BLOCK_PROPERTIES(MSDState, conf),
    DEFINE_PROP_BIT("removable", MSDState, removable, 0, false),
    DEFINE_PROP_END_OF_LIST(),
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

static void usb_msd_class_initfn_storage(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize = usb_msd_realize_storage;
    dc->props = msd_properties;
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

    visit_type_int32(v, name, &boot_index, &local_err);
    if (local_err) {
        goto out;
    }
    /* check whether bootindex is present in fw_boot_order list  */
    check_boot_index(boot_index, &local_err);
    if (local_err) {
        goto out;
    }
    /* change bootindex to a new one */
    s->conf.bootindex = boot_index;

    if (s->scsi_dev) {
        object_property_set_int(OBJECT(s->scsi_dev), boot_index, "bootindex",
                                &error_abort);
    }

out:
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

static const TypeInfo usb_storage_dev_type_info = {
    .name = TYPE_USB_STORAGE,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(MSDState),
    .abstract = true,
    .class_init = usb_msd_class_initfn_common,
};

static void usb_msd_instance_init(Object *obj)
{
    object_property_add(obj, "bootindex", "int32",
                        usb_msd_get_bootindex,
                        usb_msd_set_bootindex, NULL, NULL, NULL);
    object_property_set_int(obj, -1, "bootindex", NULL);
}

static void usb_msd_class_initfn_bot(ObjectClass *klass, void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    uc->realize = usb_msd_realize_bot;
    dc->hotpluggable = false;
}

static const TypeInfo msd_info = {
    .name          = "usb-storage",
    .parent        = TYPE_USB_STORAGE,
    .class_init    = usb_msd_class_initfn_storage,
    .instance_init = usb_msd_instance_init,
};

static const TypeInfo bot_info = {
    .name          = "usb-bot",
    .parent        = TYPE_USB_STORAGE,
    .class_init    = usb_msd_class_initfn_bot,
};

static void usb_msd_register_types(void)
{
    type_register_static(&usb_storage_dev_type_info);
    type_register_static(&msd_info);
    type_register_static(&bot_info);
    usb_legacy_register("usb-storage", "disk", usb_msd_init);
}

type_init(usb_msd_register_types)
