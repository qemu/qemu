/*
 * UAS (USB Attached SCSI) emulation
 *
 * Copyright Red Hat, Inc. 2012
 *
 * Author: Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "qemu/config-file.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

#include "hw/usb.h"
#include "desc.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"

/* --------------------------------------------------------------------- */

#define UAS_UI_COMMAND              0x01
#define UAS_UI_SENSE                0x03
#define UAS_UI_RESPONSE             0x04
#define UAS_UI_TASK_MGMT            0x05
#define UAS_UI_READ_READY           0x06
#define UAS_UI_WRITE_READY          0x07

#define UAS_RC_TMF_COMPLETE         0x00
#define UAS_RC_INVALID_INFO_UNIT    0x02
#define UAS_RC_TMF_NOT_SUPPORTED    0x04
#define UAS_RC_TMF_FAILED           0x05
#define UAS_RC_TMF_SUCCEEDED        0x08
#define UAS_RC_INCORRECT_LUN        0x09
#define UAS_RC_OVERLAPPED_TAG       0x0a

#define UAS_TMF_ABORT_TASK          0x01
#define UAS_TMF_ABORT_TASK_SET      0x02
#define UAS_TMF_CLEAR_TASK_SET      0x04
#define UAS_TMF_LOGICAL_UNIT_RESET  0x08
#define UAS_TMF_I_T_NEXUS_RESET     0x10
#define UAS_TMF_CLEAR_ACA           0x40
#define UAS_TMF_QUERY_TASK          0x80
#define UAS_TMF_QUERY_TASK_SET      0x81
#define UAS_TMF_QUERY_ASYNC_EVENT   0x82

#define UAS_PIPE_ID_COMMAND         0x01
#define UAS_PIPE_ID_STATUS          0x02
#define UAS_PIPE_ID_DATA_IN         0x03
#define UAS_PIPE_ID_DATA_OUT        0x04

typedef struct {
    uint8_t    id;
    uint8_t    reserved;
    uint16_t   tag;
} QEMU_PACKED  uas_iu_header;

typedef struct {
    uint8_t    prio_taskattr;   /* 6:3 priority, 2:0 task attribute   */
    uint8_t    reserved_1;
    uint8_t    add_cdb_length;  /* 7:2 additional adb length (dwords) */
    uint8_t    reserved_2;
    uint64_t   lun;
    uint8_t    cdb[16];
    uint8_t    add_cdb[];
} QEMU_PACKED  uas_iu_command;

typedef struct {
    uint16_t   status_qualifier;
    uint8_t    status;
    uint8_t    reserved[7];
    uint16_t   sense_length;
    uint8_t    sense_data[18];
} QEMU_PACKED  uas_iu_sense;

typedef struct {
    uint8_t    add_response_info[3];
    uint8_t    response_code;
} QEMU_PACKED  uas_iu_response;

typedef struct {
    uint8_t    function;
    uint8_t    reserved;
    uint16_t   task_tag;
    uint64_t   lun;
} QEMU_PACKED  uas_iu_task_mgmt;

typedef struct {
    uas_iu_header  hdr;
    union {
        uas_iu_command   command;
        uas_iu_sense     sense;
        uas_iu_task_mgmt task;
        uas_iu_response  response;
    };
} QEMU_PACKED  uas_iu;

/* --------------------------------------------------------------------- */

#define UAS_STREAM_BM_ATTR  4
#define UAS_MAX_STREAMS     (1 << UAS_STREAM_BM_ATTR)

typedef struct UASDevice UASDevice;
typedef struct UASRequest UASRequest;
typedef struct UASStatus UASStatus;

struct UASDevice {
    USBDevice                 dev;
    SCSIBus                   bus;
    QEMUBH                    *status_bh;
    QTAILQ_HEAD(, UASStatus)  results;
    QTAILQ_HEAD(, UASRequest) requests;

    /* properties */
    uint32_t                  requestlog;

    /* usb 2.0 only */
    USBPacket                 *status2;
    UASRequest                *datain2;
    UASRequest                *dataout2;

    /* usb 3.0 only */
    USBPacket                 *data3[UAS_MAX_STREAMS + 1];
    USBPacket                 *status3[UAS_MAX_STREAMS + 1];
};

#define TYPE_USB_UAS "usb-uas"
#define USB_UAS(obj) OBJECT_CHECK(UASDevice, (obj), TYPE_USB_UAS)

struct UASRequest {
    uint16_t     tag;
    uint64_t     lun;
    UASDevice    *uas;
    SCSIDevice   *dev;
    SCSIRequest  *req;
    USBPacket    *data;
    bool         data_async;
    bool         active;
    bool         complete;
    uint32_t     buf_off;
    uint32_t     buf_size;
    uint32_t     data_off;
    uint32_t     data_size;
    QTAILQ_ENTRY(UASRequest)  next;
};

struct UASStatus {
    uint32_t                  stream;
    uas_iu                    status;
    uint32_t                  length;
    QTAILQ_ENTRY(UASStatus)   next;
};

/* --------------------------------------------------------------------- */

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_CONFIG_HIGH,
    STR_CONFIG_SUPER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT]      = "USB Attached SCSI HBA",
    [STR_SERIALNUMBER] = "27842",
    [STR_CONFIG_HIGH]  = "High speed config (usb 2.0)",
    [STR_CONFIG_SUPER] = "Super speed config (usb 3.0)",
};

static const USBDescIface desc_iface_high = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 4,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x62, /* UAS  */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_OUT | UAS_PIPE_ID_COMMAND,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_COMMAND,
                0x00,  /*  u8  bReserved */
            },
        },{
            .bEndpointAddress      = USB_DIR_IN | UAS_PIPE_ID_STATUS,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_STATUS,
                0x00,  /*  u8  bReserved */
            },
        },{
            .bEndpointAddress      = USB_DIR_IN | UAS_PIPE_ID_DATA_IN,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_DATA_IN,
                0x00,  /*  u8  bReserved */
            },
        },{
            .bEndpointAddress      = USB_DIR_OUT | UAS_PIPE_ID_DATA_OUT,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 512,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_DATA_OUT,
                0x00,  /*  u8  bReserved */
            },
        },
    }
};

static const USBDescIface desc_iface_super = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 4,
    .bInterfaceClass               = USB_CLASS_MASS_STORAGE,
    .bInterfaceSubClass            = 0x06, /* SCSI */
    .bInterfaceProtocol            = 0x62, /* UAS  */
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_OUT | UAS_PIPE_ID_COMMAND,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_COMMAND,
                0x00,  /*  u8  bReserved */
            },
        },{
            .bEndpointAddress      = USB_DIR_IN | UAS_PIPE_ID_STATUS,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
            .bmAttributes_super    = UAS_STREAM_BM_ATTR,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_STATUS,
                0x00,  /*  u8  bReserved */
            },
        },{
            .bEndpointAddress      = USB_DIR_IN | UAS_PIPE_ID_DATA_IN,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
            .bmAttributes_super    = UAS_STREAM_BM_ATTR,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_DATA_IN,
                0x00,  /*  u8  bReserved */
            },
        },{
            .bEndpointAddress      = USB_DIR_OUT | UAS_PIPE_ID_DATA_OUT,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 1024,
            .bMaxBurst             = 15,
            .bmAttributes_super    = UAS_STREAM_BM_ATTR,
            .extra = (uint8_t[]) {
                0x04,  /*  u8  bLength */
                0x24,  /*  u8  bDescriptorType */
                UAS_PIPE_ID_DATA_OUT,
                0x00,  /*  u8  bReserved */
            },
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

static const USBDescDevice desc_device_super = {
    .bcdUSB                        = 0x0300,
    .bMaxPacketSize0               = 64,
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
        .idProduct         = 0x0003,
        .bcdDevice         = 0,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .high  = &desc_device_high,
    .super = &desc_device_super,
    .str   = desc_strings,
};

/* --------------------------------------------------------------------- */

static bool uas_using_streams(UASDevice *uas)
{
    return uas->dev.speed == USB_SPEED_SUPER;
}

/* --------------------------------------------------------------------- */

static UASStatus *usb_uas_alloc_status(UASDevice *uas, uint8_t id, uint16_t tag)
{
    UASStatus *st = g_new0(UASStatus, 1);

    st->status.hdr.id = id;
    st->status.hdr.tag = cpu_to_be16(tag);
    st->length = sizeof(uas_iu_header);
    if (uas_using_streams(uas)) {
        st->stream = tag;
    }
    return st;
}

static void usb_uas_send_status_bh(void *opaque)
{
    UASDevice *uas = opaque;
    UASStatus *st;
    USBPacket *p;

    while ((st = QTAILQ_FIRST(&uas->results)) != NULL) {
        if (uas_using_streams(uas)) {
            p = uas->status3[st->stream];
            uas->status3[st->stream] = NULL;
        } else {
            p = uas->status2;
            uas->status2 = NULL;
        }
        if (p == NULL) {
            break;
        }

        usb_packet_copy(p, &st->status, st->length);
        QTAILQ_REMOVE(&uas->results, st, next);
        g_free(st);

        p->status = USB_RET_SUCCESS; /* Clear previous ASYNC status */
        usb_packet_complete(&uas->dev, p);
    }
}

static void usb_uas_queue_status(UASDevice *uas, UASStatus *st, int length)
{
    USBPacket *p = uas_using_streams(uas) ?
        uas->status3[st->stream] : uas->status2;

    st->length += length;
    QTAILQ_INSERT_TAIL(&uas->results, st, next);
    if (p) {
        /*
         * Just schedule bh make sure any in-flight data transaction
         * is finished before completing (sending) the status packet.
         */
        qemu_bh_schedule(uas->status_bh);
    } else {
        USBEndpoint *ep = usb_ep_get(&uas->dev, USB_TOKEN_IN,
                                     UAS_PIPE_ID_STATUS);
        usb_wakeup(ep, st->stream);
    }
}

static void usb_uas_queue_response(UASDevice *uas, uint16_t tag, uint8_t code)
{
    UASStatus *st = usb_uas_alloc_status(uas, UAS_UI_RESPONSE, tag);

    trace_usb_uas_response(uas->dev.addr, tag, code);
    st->status.response.response_code = code;
    usb_uas_queue_status(uas, st, sizeof(uas_iu_response));
}

static void usb_uas_queue_sense(UASRequest *req, uint8_t status)
{
    UASStatus *st = usb_uas_alloc_status(req->uas, UAS_UI_SENSE, req->tag);
    int len, slen = 0;

    trace_usb_uas_sense(req->uas->dev.addr, req->tag, status);
    st->status.sense.status = status;
    st->status.sense.status_qualifier = cpu_to_be16(0);
    if (status != GOOD) {
        slen = scsi_req_get_sense(req->req, st->status.sense.sense_data,
                                  sizeof(st->status.sense.sense_data));
        st->status.sense.sense_length = cpu_to_be16(slen);
    }
    len = sizeof(uas_iu_sense) - sizeof(st->status.sense.sense_data) + slen;
    usb_uas_queue_status(req->uas, st, len);
}

static void usb_uas_queue_fake_sense(UASDevice *uas, uint16_t tag,
                                     struct SCSISense sense)
{
    UASStatus *st = usb_uas_alloc_status(uas, UAS_UI_SENSE, tag);
    int len, slen = 0;

    st->status.sense.status = CHECK_CONDITION;
    st->status.sense.status_qualifier = cpu_to_be16(0);
    st->status.sense.sense_data[0] = 0x70;
    st->status.sense.sense_data[2] = sense.key;
    st->status.sense.sense_data[7] = 10;
    st->status.sense.sense_data[12] = sense.asc;
    st->status.sense.sense_data[13] = sense.ascq;
    slen = 18;
    len = sizeof(uas_iu_sense) - sizeof(st->status.sense.sense_data) + slen;
    usb_uas_queue_status(uas, st, len);
}

static void usb_uas_queue_read_ready(UASRequest *req)
{
    UASStatus *st = usb_uas_alloc_status(req->uas, UAS_UI_READ_READY,
                                         req->tag);

    trace_usb_uas_read_ready(req->uas->dev.addr, req->tag);
    usb_uas_queue_status(req->uas, st, 0);
}

static void usb_uas_queue_write_ready(UASRequest *req)
{
    UASStatus *st = usb_uas_alloc_status(req->uas, UAS_UI_WRITE_READY,
                                         req->tag);

    trace_usb_uas_write_ready(req->uas->dev.addr, req->tag);
    usb_uas_queue_status(req->uas, st, 0);
}

/* --------------------------------------------------------------------- */

static int usb_uas_get_lun(uint64_t lun64)
{
    return (lun64 >> 48) & 0xff;
}

static SCSIDevice *usb_uas_get_dev(UASDevice *uas, uint64_t lun64)
{
    if ((lun64 >> 56) != 0x00) {
        return NULL;
    }
    return scsi_device_find(&uas->bus, 0, 0, usb_uas_get_lun(lun64));
}

static void usb_uas_complete_data_packet(UASRequest *req)
{
    USBPacket *p;

    if (!req->data_async) {
        return;
    }
    p = req->data;
    req->data = NULL;
    req->data_async = false;
    p->status = USB_RET_SUCCESS; /* Clear previous ASYNC status */
    usb_packet_complete(&req->uas->dev, p);
}

static void usb_uas_copy_data(UASRequest *req)
{
    uint32_t length;

    length = MIN(req->buf_size - req->buf_off,
                 req->data->iov.size - req->data->actual_length);
    trace_usb_uas_xfer_data(req->uas->dev.addr, req->tag, length,
                            req->data->actual_length, req->data->iov.size,
                            req->buf_off, req->buf_size);
    usb_packet_copy(req->data, scsi_req_get_buf(req->req) + req->buf_off,
                    length);
    req->buf_off += length;
    req->data_off += length;

    if (req->data->actual_length == req->data->iov.size) {
        usb_uas_complete_data_packet(req);
    }
    if (req->buf_size && req->buf_off == req->buf_size) {
        req->buf_off = 0;
        req->buf_size = 0;
        scsi_req_continue(req->req);
    }
}

static void usb_uas_start_next_transfer(UASDevice *uas)
{
    UASRequest *req;

    if (uas_using_streams(uas)) {
        return;
    }

    QTAILQ_FOREACH(req, &uas->requests, next) {
        if (req->active || req->complete) {
            continue;
        }
        if (req->req->cmd.mode == SCSI_XFER_FROM_DEV && uas->datain2 == NULL) {
            uas->datain2 = req;
            usb_uas_queue_read_ready(req);
            req->active = true;
            return;
        }
        if (req->req->cmd.mode == SCSI_XFER_TO_DEV && uas->dataout2 == NULL) {
            uas->dataout2 = req;
            usb_uas_queue_write_ready(req);
            req->active = true;
            return;
        }
    }
}

static UASRequest *usb_uas_alloc_request(UASDevice *uas, uas_iu *iu)
{
    UASRequest *req;

    req = g_new0(UASRequest, 1);
    req->uas = uas;
    req->tag = be16_to_cpu(iu->hdr.tag);
    req->lun = be64_to_cpu(iu->command.lun);
    req->dev = usb_uas_get_dev(req->uas, req->lun);
    return req;
}

static void usb_uas_scsi_free_request(SCSIBus *bus, void *priv)
{
    UASRequest *req = priv;
    UASDevice *uas = req->uas;

    if (req == uas->datain2) {
        uas->datain2 = NULL;
    }
    if (req == uas->dataout2) {
        uas->dataout2 = NULL;
    }
    QTAILQ_REMOVE(&uas->requests, req, next);
    g_free(req);
    usb_uas_start_next_transfer(uas);
}

static UASRequest *usb_uas_find_request(UASDevice *uas, uint16_t tag)
{
    UASRequest *req;

    QTAILQ_FOREACH(req, &uas->requests, next) {
        if (req->tag == tag) {
            return req;
        }
    }
    return NULL;
}

static void usb_uas_scsi_transfer_data(SCSIRequest *r, uint32_t len)
{
    UASRequest *req = r->hba_private;

    trace_usb_uas_scsi_data(req->uas->dev.addr, req->tag, len);
    req->buf_off = 0;
    req->buf_size = len;
    if (req->data) {
        usb_uas_copy_data(req);
    } else {
        usb_uas_start_next_transfer(req->uas);
    }
}

static void usb_uas_scsi_command_complete(SCSIRequest *r,
                                          uint32_t status, size_t resid)
{
    UASRequest *req = r->hba_private;

    trace_usb_uas_scsi_complete(req->uas->dev.addr, req->tag, status, resid);
    req->complete = true;
    if (req->data) {
        usb_uas_complete_data_packet(req);
    }
    usb_uas_queue_sense(req, status);
    scsi_req_unref(req->req);
}

static void usb_uas_scsi_request_cancelled(SCSIRequest *r)
{
    UASRequest *req = r->hba_private;

    /* FIXME: queue notification to status pipe? */
    scsi_req_unref(req->req);
}

static const struct SCSIBusInfo usb_uas_scsi_info = {
    .tcq = true,
    .max_target = 0,
    .max_lun = 255,

    .transfer_data = usb_uas_scsi_transfer_data,
    .complete = usb_uas_scsi_command_complete,
    .cancel = usb_uas_scsi_request_cancelled,
    .free_request = usb_uas_scsi_free_request,
};

/* --------------------------------------------------------------------- */

static void usb_uas_handle_reset(USBDevice *dev)
{
    UASDevice *uas = USB_UAS(dev);
    UASRequest *req, *nreq;
    UASStatus *st, *nst;

    trace_usb_uas_reset(dev->addr);
    QTAILQ_FOREACH_SAFE(req, &uas->requests, next, nreq) {
        scsi_req_cancel(req->req);
    }
    QTAILQ_FOREACH_SAFE(st, &uas->results, next, nst) {
        QTAILQ_REMOVE(&uas->results, st, next);
        g_free(st);
    }
}

static void usb_uas_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }
    error_report("%s: unhandled control request (req 0x%x, val 0x%x, idx 0x%x",
                 __func__, request, value, index);
    p->status = USB_RET_STALL;
}

static void usb_uas_cancel_io(USBDevice *dev, USBPacket *p)
{
    UASDevice *uas = USB_UAS(dev);
    UASRequest *req, *nreq;
    int i;

    if (uas->status2 == p) {
        uas->status2 = NULL;
        qemu_bh_cancel(uas->status_bh);
        return;
    }
    if (uas_using_streams(uas)) {
        for (i = 0; i <= UAS_MAX_STREAMS; i++) {
            if (uas->status3[i] == p) {
                uas->status3[i] = NULL;
                return;
            }
            if (uas->data3[i] == p) {
                uas->data3[i] = NULL;
                return;
            }
        }
    }
    QTAILQ_FOREACH_SAFE(req, &uas->requests, next, nreq) {
        if (req->data == p) {
            req->data = NULL;
            return;
        }
    }
    assert(!"canceled usb packet not found");
}

static void usb_uas_command(UASDevice *uas, uas_iu *iu)
{
    UASRequest *req;
    uint32_t len;
    uint16_t tag = be16_to_cpu(iu->hdr.tag);

    if (uas_using_streams(uas) && tag > UAS_MAX_STREAMS) {
        goto invalid_tag;
    }
    req = usb_uas_find_request(uas, tag);
    if (req) {
        goto overlapped_tag;
    }
    req = usb_uas_alloc_request(uas, iu);
    if (req->dev == NULL) {
        goto bad_target;
    }

    trace_usb_uas_command(uas->dev.addr, req->tag,
                          usb_uas_get_lun(req->lun),
                          req->lun >> 32, req->lun & 0xffffffff);
    QTAILQ_INSERT_TAIL(&uas->requests, req, next);
    if (uas_using_streams(uas) && uas->data3[req->tag] != NULL) {
        req->data = uas->data3[req->tag];
        req->data_async = true;
        uas->data3[req->tag] = NULL;
    }

    req->req = scsi_req_new(req->dev, req->tag,
                            usb_uas_get_lun(req->lun),
                            iu->command.cdb, req);
    if (uas->requestlog) {
        scsi_req_print(req->req);
    }
    len = scsi_req_enqueue(req->req);
    if (len) {
        req->data_size = len;
        scsi_req_continue(req->req);
    }
    return;

invalid_tag:
    usb_uas_queue_fake_sense(uas, tag, sense_code_INVALID_TAG);
    return;

overlapped_tag:
    usb_uas_queue_fake_sense(uas, tag, sense_code_OVERLAPPED_COMMANDS);
    return;

bad_target:
    usb_uas_queue_fake_sense(uas, tag, sense_code_LUN_NOT_SUPPORTED);
    g_free(req);
}

static void usb_uas_task(UASDevice *uas, uas_iu *iu)
{
    uint16_t tag = be16_to_cpu(iu->hdr.tag);
    uint64_t lun64 = be64_to_cpu(iu->task.lun);
    SCSIDevice *dev = usb_uas_get_dev(uas, lun64);
    int lun = usb_uas_get_lun(lun64);
    UASRequest *req;
    uint16_t task_tag;

    if (uas_using_streams(uas) && tag > UAS_MAX_STREAMS) {
        goto invalid_tag;
    }
    req = usb_uas_find_request(uas, be16_to_cpu(iu->hdr.tag));
    if (req) {
        goto overlapped_tag;
    }
    if (dev == NULL) {
        goto incorrect_lun;
    }

    switch (iu->task.function) {
    case UAS_TMF_ABORT_TASK:
        task_tag = be16_to_cpu(iu->task.task_tag);
        trace_usb_uas_tmf_abort_task(uas->dev.addr, tag, task_tag);
        req = usb_uas_find_request(uas, task_tag);
        if (req && req->dev == dev) {
            scsi_req_cancel(req->req);
        }
        usb_uas_queue_response(uas, tag, UAS_RC_TMF_COMPLETE);
        break;

    case UAS_TMF_LOGICAL_UNIT_RESET:
        trace_usb_uas_tmf_logical_unit_reset(uas->dev.addr, tag, lun);
        qdev_reset_all(&dev->qdev);
        usb_uas_queue_response(uas, tag, UAS_RC_TMF_COMPLETE);
        break;

    default:
        trace_usb_uas_tmf_unsupported(uas->dev.addr, tag, iu->task.function);
        usb_uas_queue_response(uas, tag, UAS_RC_TMF_NOT_SUPPORTED);
        break;
    }
    return;

invalid_tag:
    usb_uas_queue_response(uas, tag, UAS_RC_INVALID_INFO_UNIT);
    return;

overlapped_tag:
    usb_uas_queue_response(uas, req->tag, UAS_RC_OVERLAPPED_TAG);
    return;

incorrect_lun:
    usb_uas_queue_response(uas, tag, UAS_RC_INCORRECT_LUN);
}

static void usb_uas_handle_data(USBDevice *dev, USBPacket *p)
{
    UASDevice *uas = USB_UAS(dev);
    uas_iu iu;
    UASStatus *st;
    UASRequest *req;
    int length;

    switch (p->ep->nr) {
    case UAS_PIPE_ID_COMMAND:
        length = MIN(sizeof(iu), p->iov.size);
        usb_packet_copy(p, &iu, length);
        switch (iu.hdr.id) {
        case UAS_UI_COMMAND:
            usb_uas_command(uas, &iu);
            break;
        case UAS_UI_TASK_MGMT:
            usb_uas_task(uas, &iu);
            break;
        default:
            error_report("%s: unknown command iu: id 0x%x",
                         __func__, iu.hdr.id);
            p->status = USB_RET_STALL;
            break;
        }
        break;
    case UAS_PIPE_ID_STATUS:
        if (p->stream) {
            QTAILQ_FOREACH(st, &uas->results, next) {
                if (st->stream == p->stream) {
                    break;
                }
            }
            if (st == NULL) {
                assert(uas->status3[p->stream] == NULL);
                uas->status3[p->stream] = p;
                p->status = USB_RET_ASYNC;
                break;
            }
        } else {
            st = QTAILQ_FIRST(&uas->results);
            if (st == NULL) {
                assert(uas->status2 == NULL);
                uas->status2 = p;
                p->status = USB_RET_ASYNC;
                break;
            }
        }
        usb_packet_copy(p, &st->status, st->length);
        QTAILQ_REMOVE(&uas->results, st, next);
        g_free(st);
        break;
    case UAS_PIPE_ID_DATA_IN:
    case UAS_PIPE_ID_DATA_OUT:
        if (p->stream) {
            req = usb_uas_find_request(uas, p->stream);
        } else {
            req = (p->ep->nr == UAS_PIPE_ID_DATA_IN)
                ? uas->datain2 : uas->dataout2;
        }
        if (req == NULL) {
            if (p->stream) {
                assert(uas->data3[p->stream] == NULL);
                uas->data3[p->stream] = p;
                p->status = USB_RET_ASYNC;
                break;
            } else {
                error_report("%s: no inflight request", __func__);
                p->status = USB_RET_STALL;
                break;
            }
        }
        scsi_req_ref(req->req);
        req->data = p;
        usb_uas_copy_data(req);
        if (p->actual_length == p->iov.size || req->complete) {
            req->data = NULL;
        } else {
            req->data_async = true;
            p->status = USB_RET_ASYNC;
        }
        scsi_req_unref(req->req);
        usb_uas_start_next_transfer(uas);
        break;
    default:
        error_report("%s: invalid endpoint %d", __func__, p->ep->nr);
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_uas_unrealize(USBDevice *dev, Error **errp)
{
    UASDevice *uas = USB_UAS(dev);

    qemu_bh_delete(uas->status_bh);
}

static void usb_uas_realize(USBDevice *dev, Error **errp)
{
    UASDevice *uas = USB_UAS(dev);
    DeviceState *d = DEVICE(dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    if (d->hotplugged) {
        uas->dev.auto_attach = 0;
    }

    QTAILQ_INIT(&uas->results);
    QTAILQ_INIT(&uas->requests);
    uas->status_bh = qemu_bh_new(usb_uas_send_status_bh, uas);

    scsi_bus_new(&uas->bus, sizeof(uas->bus), DEVICE(dev),
                 &usb_uas_scsi_info, NULL);
}

static const VMStateDescription vmstate_usb_uas = {
    .name = "usb-uas",
    .unmigratable = 1,
    .fields = (VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, UASDevice),
        VMSTATE_END_OF_LIST()
    }
};

static Property uas_properties[] = {
    DEFINE_PROP_UINT32("log-scsi-req", UASDevice, requestlog, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_uas_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_uas_realize;
    uc->product_desc   = desc_strings[STR_PRODUCT];
    uc->usb_desc       = &desc;
    uc->cancel_packet  = usb_uas_cancel_io;
    uc->handle_attach  = usb_desc_attach;
    uc->handle_reset   = usb_uas_handle_reset;
    uc->handle_control = usb_uas_handle_control;
    uc->handle_data    = usb_uas_handle_data;
    uc->unrealize      = usb_uas_unrealize;
    uc->attached_settable = true;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->fw_name = "storage";
    dc->vmsd = &vmstate_usb_uas;
    dc->props = uas_properties;
}

static const TypeInfo uas_info = {
    .name          = TYPE_USB_UAS,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(UASDevice),
    .class_init    = usb_uas_class_initfn,
};

static void usb_uas_register_types(void)
{
    type_register_static(&uas_info);
}

type_init(usb_uas_register_types)
