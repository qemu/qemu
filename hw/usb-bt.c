/*
 * QEMU Bluetooth HCI USB Transport Layer v1.0
 *
 * Copyright (C) 2007 OpenMoko, Inc.
 * Copyright (C) 2008 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu-common.h"
#include "usb.h"
#include "usb-desc.h"
#include "net.h"
#include "bt.h"

struct USBBtState {
    USBDevice dev;
    struct HCIInfo *hci;

    int altsetting;
    int config;

#define CFIFO_LEN_MASK	255
#define DFIFO_LEN_MASK	4095
    struct usb_hci_in_fifo_s {
        uint8_t data[(DFIFO_LEN_MASK + 1) * 2];
        struct {
            uint8_t *data;
            int len;
        } fifo[CFIFO_LEN_MASK + 1];
        int dstart, dlen, dsize, start, len;
    } evt, acl, sco;

    struct usb_hci_out_fifo_s {
        uint8_t data[4096];
	int len;
    } outcmd, outacl, outsco;
};

#define USB_EVT_EP	1
#define USB_ACL_EP	2
#define USB_SCO_EP	3

enum {
    STR_MANUFACTURER = 1,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "QEMU " QEMU_VERSION,
    [STR_SERIALNUMBER]     = "1",
};

static const USBDescIface desc_iface_bluetooth[] = {
    {
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 3,
        .bInterfaceClass               = 0xe0, /* Wireless */
        .bInterfaceSubClass            = 0x01, /* Radio Frequency */
        .bInterfaceProtocol            = 0x01, /* Bluetooth */
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | USB_EVT_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x10,
                .bInterval             = 0x02,
            },
            {
                .bEndpointAddress      = USB_DIR_OUT | USB_ACL_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize        = 0x40,
                .bInterval             = 0x0a,
            },
            {
                .bEndpointAddress      = USB_DIR_IN | USB_ACL_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize        = 0x40,
                .bInterval             = 0x0a,
            },
        },
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 0,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = 0xe0, /* Wireless */
        .bInterfaceSubClass            = 0x01, /* Radio Frequency */
        .bInterfaceProtocol            = 0x01, /* Bluetooth */
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_OUT | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0,
                .bInterval             = 0x01,
            },
            {
                .bEndpointAddress      = USB_DIR_IN | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0,
                .bInterval             = 0x01,
            },
        },
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 1,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = 0xe0, /* Wireless */
        .bInterfaceSubClass            = 0x01, /* Radio Frequency */
        .bInterfaceProtocol            = 0x01, /* Bluetooth */
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_OUT | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x09,
                .bInterval             = 0x01,
            },
            {
                .bEndpointAddress      = USB_DIR_IN | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x09,
                .bInterval             = 0x01,
            },
        },
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 2,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = 0xe0, /* Wireless */
        .bInterfaceSubClass            = 0x01, /* Radio Frequency */
        .bInterfaceProtocol            = 0x01, /* Bluetooth */
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_OUT | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x11,
                .bInterval             = 0x01,
            },
            {
                .bEndpointAddress      = USB_DIR_IN | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x11,
                .bInterval             = 0x01,
            },
        },
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 3,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = 0xe0, /* Wireless */
        .bInterfaceSubClass            = 0x01, /* Radio Frequency */
        .bInterfaceProtocol            = 0x01, /* Bluetooth */
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_OUT | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x19,
                .bInterval             = 0x01,
            },
            {
                .bEndpointAddress      = USB_DIR_IN | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x19,
                .bInterval             = 0x01,
            },
        },
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 4,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = 0xe0, /* Wireless */
        .bInterfaceSubClass            = 0x01, /* Radio Frequency */
        .bInterfaceProtocol            = 0x01, /* Bluetooth */
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_OUT | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x21,
                .bInterval             = 0x01,
            },
            {
                .bEndpointAddress      = USB_DIR_IN | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x21,
                .bInterval             = 0x01,
            },
        },
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 5,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = 0xe0, /* Wireless */
        .bInterfaceSubClass            = 0x01, /* Radio Frequency */
        .bInterfaceProtocol            = 0x01, /* Bluetooth */
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_OUT | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x31,
                .bInterval             = 0x01,
            },
            {
                .bEndpointAddress      = USB_DIR_IN | USB_SCO_EP,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = 0x31,
                .bInterval             = 0x01,
            },
        },
    }
};

static const USBDescDevice desc_device_bluetooth = {
    .bcdUSB                        = 0x0110,
    .bDeviceClass                  = 0xe0, /* Wireless */
    .bDeviceSubClass               = 0x01, /* Radio Frequency */
    .bDeviceProtocol               = 0x01, /* Bluetooth */
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = 1,
            .bmAttributes          = 0xc0,
            .bMaxPower             = 0,
            .nif = ARRAY_SIZE(desc_iface_bluetooth),
            .ifs = desc_iface_bluetooth,
        },
    },
};

static const USBDesc desc_bluetooth = {
    .id = {
        .idVendor          = 0x0a12,
        .idProduct         = 0x0001,
        .bcdDevice         = 0x1958,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = 0,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_bluetooth,
    .str  = desc_strings,
};

static void usb_bt_fifo_reset(struct usb_hci_in_fifo_s *fifo)
{
    fifo->dstart = 0;
    fifo->dlen = 0;
    fifo->dsize = DFIFO_LEN_MASK + 1;
    fifo->start = 0;
    fifo->len = 0;
}

static void usb_bt_fifo_enqueue(struct usb_hci_in_fifo_s *fifo,
                const uint8_t *data, int len)
{
    int off = fifo->dstart + fifo->dlen;
    uint8_t *buf;

    fifo->dlen += len;
    if (off <= DFIFO_LEN_MASK) {
        if (off + len > DFIFO_LEN_MASK + 1 &&
                        (fifo->dsize = off + len) > (DFIFO_LEN_MASK + 1) * 2) {
            fprintf(stderr, "%s: can't alloc %i bytes\n", __FUNCTION__, len);
            exit(-1);
        }
        buf = fifo->data + off;
    } else {
        if (fifo->dlen > fifo->dsize) {
            fprintf(stderr, "%s: can't alloc %i bytes\n", __FUNCTION__, len);
            exit(-1);
        }
        buf = fifo->data + off - fifo->dsize;
    }

    off = (fifo->start + fifo->len ++) & CFIFO_LEN_MASK;
    fifo->fifo[off].data = memcpy(buf, data, len);
    fifo->fifo[off].len = len;
}

static inline int usb_bt_fifo_dequeue(struct usb_hci_in_fifo_s *fifo,
                USBPacket *p)
{
    int len;

    if (likely(!fifo->len))
        return USB_RET_STALL;

    len = MIN(p->len, fifo->fifo[fifo->start].len);
    memcpy(p->data, fifo->fifo[fifo->start].data, len);
    if (len == p->len) {
        fifo->fifo[fifo->start].len -= len;
        fifo->fifo[fifo->start].data += len;
    } else {
        fifo->start ++;
        fifo->start &= CFIFO_LEN_MASK;
        fifo->len --;
    }

    fifo->dstart += len;
    fifo->dlen -= len;
    if (fifo->dstart >= fifo->dsize) {
        fifo->dstart = 0;
        fifo->dsize = DFIFO_LEN_MASK + 1;
    }

    return len;
}

static inline void usb_bt_fifo_out_enqueue(struct USBBtState *s,
                struct usb_hci_out_fifo_s *fifo,
                void (*send)(struct HCIInfo *, const uint8_t *, int),
                int (*complete)(const uint8_t *, int),
                const uint8_t *data, int len)
{
    if (fifo->len) {
        memcpy(fifo->data + fifo->len, data, len);
        fifo->len += len;
        if (complete(fifo->data, fifo->len)) {
            send(s->hci, fifo->data, fifo->len);
            fifo->len = 0;
        }
    } else if (complete(data, len))
        send(s->hci, data, len);
    else {
        memcpy(fifo->data, data, len);
        fifo->len = len;
    }

    /* TODO: do we need to loop? */
}

static int usb_bt_hci_cmd_complete(const uint8_t *data, int len)
{
    len -= HCI_COMMAND_HDR_SIZE;
    return len >= 0 &&
            len >= ((struct hci_command_hdr *) data)->plen;
}

static int usb_bt_hci_acl_complete(const uint8_t *data, int len)
{
    len -= HCI_ACL_HDR_SIZE;
    return len >= 0 &&
            len >= le16_to_cpu(((struct hci_acl_hdr *) data)->dlen);
}

static int usb_bt_hci_sco_complete(const uint8_t *data, int len)
{
    len -= HCI_SCO_HDR_SIZE;
    return len >= 0 &&
            len >= ((struct hci_sco_hdr *) data)->dlen;
}

static void usb_bt_handle_reset(USBDevice *dev)
{
    struct USBBtState *s = (struct USBBtState *) dev->opaque;

    usb_bt_fifo_reset(&s->evt);
    usb_bt_fifo_reset(&s->acl);
    usb_bt_fifo_reset(&s->sco);
    s->outcmd.len = 0;
    s->outacl.len = 0;
    s->outsco.len = 0;
    s->altsetting = 0;
}

static int usb_bt_handle_control(USBDevice *dev, int request, int value,
                int index, int length, uint8_t *data)
{
    struct USBBtState *s = (struct USBBtState *) dev->opaque;
    int ret;

    ret = usb_desc_handle_control(dev, request, value, index, length, data);
    if (ret >= 0) {
        switch (request) {
        case DeviceRequest | USB_REQ_GET_CONFIGURATION:
            s->config = 0;
            break;
        case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
            s->config = 1;
            usb_bt_fifo_reset(&s->evt);
            usb_bt_fifo_reset(&s->acl);
            usb_bt_fifo_reset(&s->sco);
            break;
        }
        return ret;
    }

    ret = 0;
    switch (request) {
    case InterfaceRequest | USB_REQ_GET_STATUS:
    case EndpointRequest | USB_REQ_GET_STATUS:
        data[0] = 0x00;
        data[1] = 0x00;
        ret = 2;
        break;
    case InterfaceOutRequest | USB_REQ_CLEAR_FEATURE:
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        goto fail;
    case InterfaceOutRequest | USB_REQ_SET_FEATURE:
    case EndpointOutRequest | USB_REQ_SET_FEATURE:
        goto fail;
        break;
    case InterfaceRequest | USB_REQ_GET_INTERFACE:
        if (value != 0 || (index & ~1) || length != 1)
            goto fail;
        if (index == 1)
            data[0] = s->altsetting;
        else
            data[0] = 0;
        ret = 1;
        break;
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        if ((index & ~1) || length != 0 ||
                        (index == 1 && (value < 0 || value > 4)) ||
                        (index == 0 && value != 0)) {
            printf("%s: Wrong SET_INTERFACE request (%i, %i)\n",
                            __FUNCTION__, index, value);
            goto fail;
        }
        s->altsetting = value;
        ret = 0;
        break;
    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE) << 8):
        if (s->config)
            usb_bt_fifo_out_enqueue(s, &s->outcmd, s->hci->cmd_send,
                            usb_bt_hci_cmd_complete, data, length);
        break;
    default:
    fail:
        ret = USB_RET_STALL;
        break;
    }
    return ret;
}

static int usb_bt_handle_data(USBDevice *dev, USBPacket *p)
{
    struct USBBtState *s = (struct USBBtState *) dev->opaque;
    int ret = 0;

    if (!s->config)
        goto fail;

    switch (p->pid) {
    case USB_TOKEN_IN:
        switch (p->devep & 0xf) {
        case USB_EVT_EP:
            ret = usb_bt_fifo_dequeue(&s->evt, p);
            break;

        case USB_ACL_EP:
            ret = usb_bt_fifo_dequeue(&s->acl, p);
            break;

        case USB_SCO_EP:
            ret = usb_bt_fifo_dequeue(&s->sco, p);
            break;

        default:
            goto fail;
        }
        break;

    case USB_TOKEN_OUT:
        switch (p->devep & 0xf) {
        case USB_ACL_EP:
            usb_bt_fifo_out_enqueue(s, &s->outacl, s->hci->acl_send,
                            usb_bt_hci_acl_complete, p->data, p->len);
            break;

        case USB_SCO_EP:
            usb_bt_fifo_out_enqueue(s, &s->outsco, s->hci->sco_send,
                            usb_bt_hci_sco_complete, p->data, p->len);
            break;

        default:
            goto fail;
        }
        break;

    default:
    fail:
        ret = USB_RET_STALL;
        break;
    }

    return ret;
}

static void usb_bt_out_hci_packet_event(void *opaque,
                const uint8_t *data, int len)
{
    struct USBBtState *s = (struct USBBtState *) opaque;

    usb_bt_fifo_enqueue(&s->evt, data, len);
}

static void usb_bt_out_hci_packet_acl(void *opaque,
                const uint8_t *data, int len)
{
    struct USBBtState *s = (struct USBBtState *) opaque;

    usb_bt_fifo_enqueue(&s->acl, data, len);
}

static void usb_bt_handle_destroy(USBDevice *dev)
{
    struct USBBtState *s = (struct USBBtState *) dev->opaque;

    s->hci->opaque = NULL;
    s->hci->evt_recv = NULL;
    s->hci->acl_recv = NULL;
}

static int usb_bt_initfn(USBDevice *dev)
{
    usb_desc_init(dev);
    return 0;
}

USBDevice *usb_bt_init(HCIInfo *hci)
{
    USBDevice *dev;
    struct USBBtState *s;

    if (!hci)
        return NULL;
    dev = usb_create_simple(NULL /* FIXME */, "usb-bt-dongle");
    s = DO_UPCAST(struct USBBtState, dev, dev);
    s->dev.opaque = s;

    s->hci = hci;
    s->hci->opaque = s;
    s->hci->evt_recv = usb_bt_out_hci_packet_event;
    s->hci->acl_recv = usb_bt_out_hci_packet_acl;

    usb_bt_handle_reset(&s->dev);

    return dev;
}

static struct USBDeviceInfo bt_info = {
    .product_desc   = "QEMU BT dongle",
    .qdev.name      = "usb-bt-dongle",
    .qdev.size      = sizeof(struct USBBtState),
    .usb_desc       = &desc_bluetooth,
    .init           = usb_bt_initfn,
    .handle_packet  = usb_generic_handle_packet,
    .handle_reset   = usb_bt_handle_reset,
    .handle_control = usb_bt_handle_control,
    .handle_data    = usb_bt_handle_data,
    .handle_destroy = usb_bt_handle_destroy,
};

static void usb_bt_register_devices(void)
{
    usb_qdev_register(&bt_info);
}
device_init(usb_bt_register_devices)
