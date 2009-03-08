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
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "qemu-common.h"
#include "usb.h"
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

static const uint8_t qemu_bt_dev_descriptor[] = {
    0x12,		/*  u8 bLength; */
    USB_DT_DEVICE,	/*  u8 bDescriptorType; Device */
    0x10, 0x01,		/*  u16 bcdUSB; v1.10 */

    0xe0,	/*  u8  bDeviceClass; Wireless */
    0x01,	/*  u8  bDeviceSubClass; Radio Frequency */
    0x01,	/*  u8  bDeviceProtocol; Bluetooth */
    0x40,	/*  u8  bMaxPacketSize0; 64 Bytes */

    0x12, 0x0a,	/*  u16 idVendor; */
    0x01, 0x00,	/*  u16 idProduct; Bluetooth Dongle (HCI mode) */
    0x58, 0x19,	/*  u16 bcdDevice; (some devices have 0x48, 0x02) */

    0x00,	/*  u8  iManufacturer; */
    0x00,	/*  u8  iProduct; */
    0x00,	/*  u8  iSerialNumber; */
    0x01,	/*  u8  bNumConfigurations; */
};

static const uint8_t qemu_bt_config_descriptor[] = {
    /* one configuration */
    0x09,		/*  u8  bLength; */
    USB_DT_CONFIG,	/*  u8  bDescriptorType; */
    0xb1, 0x00,		/*  u16 wTotalLength; */
    0x02,		/*  u8  bNumInterfaces; (2) */
    0x01,		/*  u8  bConfigurationValue; */
    0x00,		/*  u8  iConfiguration; */
    0xc0,		/*  u8  bmAttributes;
				     Bit 7: must be set,
					 6: Self-powered,
					 5: Remote wakeup,
					 4..0: resvd */
    0x00,		/*  u8  MaxPower; */

    /* USB 1.1:
     * USB 2.0, single TT organization (mandatory):
     *	one interface, protocol 0
     *
     * USB 2.0, multiple TT organization (optional):
     *	two interfaces, protocols 1 (like single TT)
     *	and 2 (multiple TT mode) ... config is
     *	sometimes settable
     *	NOT IMPLEMENTED
     */

    /* interface one */
    0x09,		/*  u8  if_bLength; */
    USB_DT_INTERFACE,	/*  u8  if_bDescriptorType; */
    0x00,		/*  u8  if_bInterfaceNumber; */
    0x00,		/*  u8  if_bAlternateSetting; */
    0x03,		/*  u8  if_bNumEndpoints; */
    0xe0,		/*  u8  if_bInterfaceClass; Wireless */
    0x01,		/*  u8  if_bInterfaceSubClass; Radio Frequency */
    0x01,		/*  u8  if_bInterfaceProtocol; Bluetooth */
    0x00,		/*  u8  if_iInterface; */

    /* endpoint one */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_EVT_EP,	/*  u8  ep_bEndpointAddress; */
    0x03,		/*  u8  ep_bmAttributes; Interrupt */
    0x10, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x02,		/*  u8  ep_bInterval; */

    /* endpoint two */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_OUT | USB_ACL_EP,	/*  u8  ep_bEndpointAddress; */
    0x02,		/*  u8  ep_bmAttributes; Bulk */
    0x40, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x0a,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* endpoint three */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_ACL_EP,	/*  u8  ep_bEndpointAddress; */
    0x02,		/*  u8  ep_bmAttributes; Bulk */
    0x40, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x0a,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* interface two setting one */
    0x09,		/*  u8  if_bLength; */
    USB_DT_INTERFACE,	/*  u8  if_bDescriptorType; */
    0x01,		/*  u8  if_bInterfaceNumber; */
    0x00,		/*  u8  if_bAlternateSetting; */
    0x02,		/*  u8  if_bNumEndpoints; */
    0xe0,		/*  u8  if_bInterfaceClass; Wireless */
    0x01,		/*  u8  if_bInterfaceSubClass; Radio Frequency */
    0x01,		/*  u8  if_bInterfaceProtocol; Bluetooth */
    0x00,		/*  u8  if_iInterface; */

    /* endpoint one */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_OUT | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x00, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* endpoint two */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x00, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* interface two setting two */
    0x09,		/*  u8  if_bLength; */
    USB_DT_INTERFACE,	/*  u8  if_bDescriptorType; */
    0x01,		/*  u8  if_bInterfaceNumber; */
    0x01,		/*  u8  if_bAlternateSetting; */
    0x02,		/*  u8  if_bNumEndpoints; */
    0xe0,		/*  u8  if_bInterfaceClass; Wireless */
    0x01,		/*  u8  if_bInterfaceSubClass; Radio Frequency */
    0x01,		/*  u8  if_bInterfaceProtocol; Bluetooth */
    0x00,		/*  u8  if_iInterface; */

    /* endpoint one */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_OUT | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x09, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* endpoint two */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x09, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* interface two setting three */
    0x09,		/*  u8  if_bLength; */
    USB_DT_INTERFACE,	/*  u8  if_bDescriptorType; */
    0x01,		/*  u8  if_bInterfaceNumber; */
    0x02,		/*  u8  if_bAlternateSetting; */
    0x02,		/*  u8  if_bNumEndpoints; */
    0xe0,		/*  u8  if_bInterfaceClass; Wireless */
    0x01,		/*  u8  if_bInterfaceSubClass; Radio Frequency */
    0x01,		/*  u8  if_bInterfaceProtocol; Bluetooth */
    0x00,		/*  u8  if_iInterface; */

    /* endpoint one */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_OUT | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x11, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* endpoint two */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x11, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* interface two setting four */
    0x09,		/*  u8  if_bLength; */
    USB_DT_INTERFACE,	/*  u8  if_bDescriptorType; */
    0x01,		/*  u8  if_bInterfaceNumber; */
    0x03,		/*  u8  if_bAlternateSetting; */
    0x02,		/*  u8  if_bNumEndpoints; */
    0xe0,		/*  u8  if_bInterfaceClass; Wireless */
    0x01,		/*  u8  if_bInterfaceSubClass; Radio Frequency */
    0x01,		/*  u8  if_bInterfaceProtocol; Bluetooth */
    0x00,		/*  u8  if_iInterface; */

    /* endpoint one */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_OUT | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x19, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* endpoint two */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x19, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* interface two setting five */
    0x09,		/*  u8  if_bLength; */
    USB_DT_INTERFACE,	/*  u8  if_bDescriptorType; */
    0x01,		/*  u8  if_bInterfaceNumber; */
    0x04,		/*  u8  if_bAlternateSetting; */
    0x02,		/*  u8  if_bNumEndpoints; */
    0xe0,		/*  u8  if_bInterfaceClass; Wireless */
    0x01,		/*  u8  if_bInterfaceSubClass; Radio Frequency */
    0x01,		/*  u8  if_bInterfaceProtocol; Bluetooth */
    0x00,		/*  u8  if_iInterface; */

    /* endpoint one */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_OUT | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x21, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* endpoint two */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x21, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* interface two setting six */
    0x09,		/*  u8  if_bLength; */
    USB_DT_INTERFACE,	/*  u8  if_bDescriptorType; */
    0x01,		/*  u8  if_bInterfaceNumber; */
    0x05,		/*  u8  if_bAlternateSetting; */
    0x02,		/*  u8  if_bNumEndpoints; */
    0xe0,		/*  u8  if_bInterfaceClass; Wireless */
    0x01,		/*  u8  if_bInterfaceSubClass; Radio Frequency */
    0x01,		/*  u8  if_bInterfaceProtocol; Bluetooth */
    0x00,		/*  u8  if_iInterface; */

    /* endpoint one */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_OUT | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x31, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* endpoint two */
    0x07,		/*  u8  ep_bLength; */
    USB_DT_ENDPOINT,	/*  u8  ep_bDescriptorType; */
    USB_DIR_IN | USB_SCO_EP,	/*  u8  ep_bEndpointAddress; */
    0x01,		/*  u8  ep_bmAttributes; Isochronous */
    0x31, 0x00,		/*  u16 ep_wMaxPacketSize; */
    0x01,		/*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */

    /* If implemented, the DFU interface descriptor goes here with no
     * endpoints or alternative settings.  */
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

static void inline usb_bt_fifo_out_enqueue(struct USBBtState *s,
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
    int ret = 0;

    switch (request) {
    case DeviceRequest | USB_REQ_GET_STATUS:
    case InterfaceRequest | USB_REQ_GET_STATUS:
    case EndpointRequest | USB_REQ_GET_STATUS:
        data[0] = (1 << USB_DEVICE_SELF_POWERED) |
            (dev->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP);
        data[1] = 0x00;
        ret = 2;
        break;
    case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
    case InterfaceOutRequest | USB_REQ_CLEAR_FEATURE:
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 0;
        } else {
            goto fail;
        }
        ret = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_FEATURE:
    case InterfaceOutRequest | USB_REQ_SET_FEATURE:
    case EndpointOutRequest | USB_REQ_SET_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 1;
        } else {
            goto fail;
        }
        ret = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        dev->addr = value;
        ret = 0;
        break;
    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        switch (value >> 8) {
        case USB_DT_DEVICE:
            ret = sizeof(qemu_bt_dev_descriptor);
            memcpy(data, qemu_bt_dev_descriptor, ret);
            break;
        case USB_DT_CONFIG:
            ret = sizeof(qemu_bt_config_descriptor);
            memcpy(data, qemu_bt_config_descriptor, ret);
            break;
        case USB_DT_STRING:
            switch(value & 0xff) {
            case 0:
                /* language ids */
                data[0] = 4;
                data[1] = 3;
                data[2] = 0x09;
                data[3] = 0x04;
                ret = 4;
                break;
            default:
                goto fail;
            }
            break;
        default:
            goto fail;
        }
        break;
    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        data[0] = qemu_bt_config_descriptor[0x5];
        ret = 1;
        s->config = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        ret = 0;
        if (value != qemu_bt_config_descriptor[0x5] && value != 0) {
            printf("%s: Wrong SET_CONFIGURATION request (%i)\n",
                            __FUNCTION__, value);
            goto fail;
        }
        s->config = 1;
        usb_bt_fifo_reset(&s->evt);
        usb_bt_fifo_reset(&s->acl);
        usb_bt_fifo_reset(&s->sco);
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
    qemu_free(s);
}

USBDevice *usb_bt_init(HCIInfo *hci)
{
    struct USBBtState *s;

    if (!hci)
        return NULL;
    s = qemu_mallocz(sizeof(struct USBBtState));
    s->dev.opaque = s;
    s->dev.speed = USB_SPEED_HIGH;
    s->dev.handle_packet = usb_generic_handle_packet;
    pstrcpy(s->dev.devname, sizeof(s->dev.devname), "QEMU BT dongle");

    s->dev.handle_reset = usb_bt_handle_reset;
    s->dev.handle_control = usb_bt_handle_control;
    s->dev.handle_data = usb_bt_handle_data;
    s->dev.handle_destroy = usb_bt_handle_destroy;

    s->hci = hci;
    s->hci->opaque = s;
    s->hci->evt_recv = usb_bt_out_hci_packet_event;
    s->hci->acl_recv = usb_bt_out_hci_packet_acl;

    usb_bt_handle_reset(&s->dev);

    return &s->dev;
}
