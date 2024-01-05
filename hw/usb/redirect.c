/*
 * USB redirector usb-guest
 *
 * Copyright (c) 2011-2012 Red Hat, Inc.
 *
 * Red Hat Authors:
 * Hans de Goede <hdegoede@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "chardev/char-fe.h"

#include <usbredirparser.h>
#include <usbredirfilter.h>

#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/usb.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "qom/object.h"

/* ERROR is defined below. Remove any previous definition. */
#undef ERROR

#define MAX_ENDPOINTS 32
#define NO_INTERFACE_INFO 255 /* Valid interface_count always <= 32 */
#define EP2I(ep_address) (((ep_address & 0x80) >> 3) | (ep_address & 0x0f))
#define I2EP(i) (((i & 0x10) << 3) | (i & 0x0f))
#define USBEP2I(usb_ep) (((usb_ep)->pid == USB_TOKEN_IN) ? \
                         ((usb_ep)->nr | 0x10) : ((usb_ep)->nr))
#define I2USBEP(d, i) (usb_ep_get(&(d)->dev, \
                       ((i) & 0x10) ? USB_TOKEN_IN : USB_TOKEN_OUT, \
                       (i) & 0x0f))

#ifndef USBREDIR_VERSION /* This is not defined in older usbredir versions */
#define USBREDIR_VERSION 0
#endif

typedef struct USBRedirDevice USBRedirDevice;

/* Struct to hold buffered packets */
struct buf_packet {
    uint8_t *data;
    void *free_on_destroy;
    uint16_t len;
    uint16_t offset;
    uint8_t status;
    QTAILQ_ENTRY(buf_packet)next;
};

struct endp_data {
    USBRedirDevice *dev;
    uint8_t type;
    uint8_t interval;
    uint8_t interface; /* bInterfaceNumber this ep belongs to */
    uint16_t max_packet_size; /* In bytes, not wMaxPacketSize format !! */
    uint32_t max_streams;
    uint8_t iso_started;
    uint8_t iso_error; /* For reporting iso errors to the HC */
    uint8_t interrupt_started;
    uint8_t interrupt_error;
    uint8_t bulk_receiving_enabled;
    uint8_t bulk_receiving_started;
    uint8_t bufpq_prefilled;
    uint8_t bufpq_dropping_packets;
    QTAILQ_HEAD(, buf_packet) bufpq;
    int32_t bufpq_size;
    int32_t bufpq_target_size;
    USBPacket *pending_async_packet;
};

struct PacketIdQueueEntry {
    uint64_t id;
    QTAILQ_ENTRY(PacketIdQueueEntry)next;
};

struct PacketIdQueue {
    USBRedirDevice *dev;
    const char *name;
    QTAILQ_HEAD(, PacketIdQueueEntry) head;
    int size;
};

struct USBRedirDevice {
    USBDevice dev;
    /* Properties */
    CharBackend cs;
    bool enable_streams;
    bool suppress_remote_wake;
    bool in_write;
    uint8_t debug;
    int32_t bootindex;
    char *filter_str;
    /* Data passed from chardev the fd_read cb to the usbredirparser read cb */
    const uint8_t *read_buf;
    int read_buf_size;
    /* Active chardev-watch-tag */
    guint watch;
    /* For async handling of close / reject */
    QEMUBH *chardev_close_bh;
    QEMUBH *device_reject_bh;
    /* To delay the usb attach in case of quick chardev close + open */
    QEMUTimer *attach_timer;
    int64_t next_attach_time;
    struct usbredirparser *parser;
    struct endp_data endpoint[MAX_ENDPOINTS];
    struct PacketIdQueue cancelled;
    struct PacketIdQueue already_in_flight;
    void (*buffered_bulk_in_complete)(USBRedirDevice *, USBPacket *, uint8_t);
    /* Data for device filtering */
    struct usb_redir_device_connect_header device_info;
    struct usb_redir_interface_info_header interface_info;
    struct usbredirfilter_rule *filter_rules;
    int filter_rules_count;
    int compatible_speedmask;
    VMChangeStateEntry *vmstate;
};

#define TYPE_USB_REDIR "usb-redir"
DECLARE_INSTANCE_CHECKER(USBRedirDevice, USB_REDIRECT,
                         TYPE_USB_REDIR)

static void usbredir_hello(void *priv, struct usb_redir_hello_header *h);
static void usbredir_device_connect(void *priv,
    struct usb_redir_device_connect_header *device_connect);
static void usbredir_device_disconnect(void *priv);
static void usbredir_interface_info(void *priv,
    struct usb_redir_interface_info_header *interface_info);
static void usbredir_ep_info(void *priv,
    struct usb_redir_ep_info_header *ep_info);
static void usbredir_configuration_status(void *priv, uint64_t id,
    struct usb_redir_configuration_status_header *configuration_status);
static void usbredir_alt_setting_status(void *priv, uint64_t id,
    struct usb_redir_alt_setting_status_header *alt_setting_status);
static void usbredir_iso_stream_status(void *priv, uint64_t id,
    struct usb_redir_iso_stream_status_header *iso_stream_status);
static void usbredir_interrupt_receiving_status(void *priv, uint64_t id,
    struct usb_redir_interrupt_receiving_status_header
    *interrupt_receiving_status);
static void usbredir_bulk_streams_status(void *priv, uint64_t id,
    struct usb_redir_bulk_streams_status_header *bulk_streams_status);
static void usbredir_bulk_receiving_status(void *priv, uint64_t id,
    struct usb_redir_bulk_receiving_status_header *bulk_receiving_status);
static void usbredir_control_packet(void *priv, uint64_t id,
    struct usb_redir_control_packet_header *control_packet,
    uint8_t *data, int data_len);
static void usbredir_bulk_packet(void *priv, uint64_t id,
    struct usb_redir_bulk_packet_header *bulk_packet,
    uint8_t *data, int data_len);
static void usbredir_iso_packet(void *priv, uint64_t id,
    struct usb_redir_iso_packet_header *iso_packet,
    uint8_t *data, int data_len);
static void usbredir_interrupt_packet(void *priv, uint64_t id,
    struct usb_redir_interrupt_packet_header *interrupt_header,
    uint8_t *data, int data_len);
static void usbredir_buffered_bulk_packet(void *priv, uint64_t id,
    struct usb_redir_buffered_bulk_packet_header *buffered_bulk_packet,
    uint8_t *data, int data_len);

static void usbredir_handle_status(USBRedirDevice *dev, USBPacket *p,
    int status);

#define VERSION "qemu usb-redir guest " QEMU_VERSION

/*
 * Logging stuff
 */

#define ERROR(...) \
    do { \
        if (dev->debug >= usbredirparser_error) { \
            error_report("usb-redir error: " __VA_ARGS__); \
        } \
    } while (0)
#define WARNING(...) \
    do { \
        if (dev->debug >= usbredirparser_warning) { \
            warn_report("" __VA_ARGS__); \
        } \
    } while (0)
#define INFO(...) \
    do { \
        if (dev->debug >= usbredirparser_info) { \
            error_report("usb-redir: " __VA_ARGS__); \
        } \
    } while (0)
#define DPRINTF(...) \
    do { \
        if (dev->debug >= usbredirparser_debug) { \
            error_report("usb-redir: " __VA_ARGS__); \
        } \
    } while (0)
#define DPRINTF2(...) \
    do { \
        if (dev->debug >= usbredirparser_debug_data) { \
            error_report("usb-redir: " __VA_ARGS__); \
        } \
    } while (0)

static void usbredir_log(void *priv, int level, const char *msg)
{
    USBRedirDevice *dev = priv;

    if (dev->debug < level) {
        return;
    }

    error_report("%s", msg);
}

static void usbredir_log_data(USBRedirDevice *dev, const char *desc,
    const uint8_t *data, int len)
{
    if (dev->debug < usbredirparser_debug_data) {
        return;
    }
    qemu_hexdump(stderr, desc, data, len);
}

/*
 * usbredirparser io functions
 */

static int usbredir_read(void *priv, uint8_t *data, int count)
{
    USBRedirDevice *dev = priv;

    if (dev->read_buf_size < count) {
        count = dev->read_buf_size;
    }

    memcpy(data, dev->read_buf, count);

    dev->read_buf_size -= count;
    if (dev->read_buf_size) {
        dev->read_buf += count;
    } else {
        dev->read_buf = NULL;
    }

    return count;
}

static gboolean usbredir_write_unblocked(void *do_not_use, GIOCondition cond,
                                         void *opaque)
{
    USBRedirDevice *dev = opaque;

    dev->watch = 0;
    usbredirparser_do_write(dev->parser);

    return G_SOURCE_REMOVE;
}

static int usbredir_write(void *priv, uint8_t *data, int count)
{
    USBRedirDevice *dev = priv;
    int r;

    if (!qemu_chr_fe_backend_open(&dev->cs)) {
        return 0;
    }

    /* Don't send new data to the chardev until our state is fully synced */
    if (!runstate_check(RUN_STATE_RUNNING)) {
        return 0;
    }

    /* Recursion check */
    if (dev->in_write) {
        DPRINTF("usbredir_write recursion\n");
        return 0;
    }
    dev->in_write = true;

    r = qemu_chr_fe_write(&dev->cs, data, count);
    if (r < count) {
        if (!dev->watch) {
            dev->watch = qemu_chr_fe_add_watch(&dev->cs, G_IO_OUT | G_IO_HUP,
                                               usbredir_write_unblocked, dev);
        }
        if (r < 0) {
            r = 0;
        }
    }
    dev->in_write = false;
    return r;
}

/*
 * Cancelled and buffered packets helpers
 */

static void packet_id_queue_init(struct PacketIdQueue *q,
    USBRedirDevice *dev, const char *name)
{
    q->dev = dev;
    q->name = name;
    QTAILQ_INIT(&q->head);
    q->size = 0;
}

static void packet_id_queue_add(struct PacketIdQueue *q, uint64_t id)
{
    USBRedirDevice *dev = q->dev;
    struct PacketIdQueueEntry *e;

    DPRINTF("adding packet id %"PRIu64" to %s queue\n", id, q->name);

    e = g_new0(struct PacketIdQueueEntry, 1);
    e->id = id;
    QTAILQ_INSERT_TAIL(&q->head, e, next);
    q->size++;
}

static int packet_id_queue_remove(struct PacketIdQueue *q, uint64_t id)
{
    USBRedirDevice *dev = q->dev;
    struct PacketIdQueueEntry *e;

    QTAILQ_FOREACH(e, &q->head, next) {
        if (e->id == id) {
            DPRINTF("removing packet id %"PRIu64" from %s queue\n",
                    id, q->name);
            QTAILQ_REMOVE(&q->head, e, next);
            q->size--;
            g_free(e);
            return 1;
        }
    }
    return 0;
}

static void packet_id_queue_empty(struct PacketIdQueue *q)
{
    USBRedirDevice *dev = q->dev;
    struct PacketIdQueueEntry *e, *next_e;

    DPRINTF("removing %d packet-ids from %s queue\n", q->size, q->name);

    QTAILQ_FOREACH_SAFE(e, &q->head, next, next_e) {
        QTAILQ_REMOVE(&q->head, e, next);
        g_free(e);
    }
    q->size = 0;
}

static void usbredir_cancel_packet(USBDevice *udev, USBPacket *p)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);
    int i = USBEP2I(p->ep);

    if (p->combined) {
        usb_combined_packet_cancel(udev, p);
        return;
    }

    if (dev->endpoint[i].pending_async_packet) {
        assert(dev->endpoint[i].pending_async_packet == p);
        dev->endpoint[i].pending_async_packet = NULL;
        return;
    }

    packet_id_queue_add(&dev->cancelled, p->id);
    usbredirparser_send_cancel_data_packet(dev->parser, p->id);
    usbredirparser_do_write(dev->parser);
}

static int usbredir_is_cancelled(USBRedirDevice *dev, uint64_t id)
{
    if (!dev->dev.attached) {
        return 1; /* Treat everything as cancelled after a disconnect */
    }
    return packet_id_queue_remove(&dev->cancelled, id);
}

static void usbredir_fill_already_in_flight_from_ep(USBRedirDevice *dev,
    struct USBEndpoint *ep)
{
    static USBPacket *p;

    /* async handled packets for bulk receiving eps do not count as inflight */
    if (dev->endpoint[USBEP2I(ep)].bulk_receiving_started) {
        return;
    }

    QTAILQ_FOREACH(p, &ep->queue, queue) {
        /* Skip combined packets, except for the first */
        if (p->combined && p != p->combined->first) {
            continue;
        }
        if (p->state == USB_PACKET_ASYNC) {
            packet_id_queue_add(&dev->already_in_flight, p->id);
        }
    }
}

static void usbredir_fill_already_in_flight(USBRedirDevice *dev)
{
    int ep;
    struct USBDevice *udev = &dev->dev;

    usbredir_fill_already_in_flight_from_ep(dev, &udev->ep_ctl);

    for (ep = 0; ep < USB_MAX_ENDPOINTS; ep++) {
        usbredir_fill_already_in_flight_from_ep(dev, &udev->ep_in[ep]);
        usbredir_fill_already_in_flight_from_ep(dev, &udev->ep_out[ep]);
    }
}

static int usbredir_already_in_flight(USBRedirDevice *dev, uint64_t id)
{
    return packet_id_queue_remove(&dev->already_in_flight, id);
}

static USBPacket *usbredir_find_packet_by_id(USBRedirDevice *dev,
    uint8_t ep, uint64_t id)
{
    USBPacket *p;

    if (usbredir_is_cancelled(dev, id)) {
        return NULL;
    }

    p = usb_ep_find_packet_by_id(&dev->dev,
                            (ep & USB_DIR_IN) ? USB_TOKEN_IN : USB_TOKEN_OUT,
                            ep & 0x0f, id);
    if (p == NULL) {
        ERROR("could not find packet with id %"PRIu64"\n", id);
    }
    return p;
}

static int bufp_alloc(USBRedirDevice *dev, uint8_t *data, uint16_t len,
    uint8_t status, uint8_t ep, void *free_on_destroy)
{
    struct buf_packet *bufp;

    if (!dev->endpoint[EP2I(ep)].bufpq_dropping_packets &&
        dev->endpoint[EP2I(ep)].bufpq_size >
            2 * dev->endpoint[EP2I(ep)].bufpq_target_size) {
        DPRINTF("bufpq overflow, dropping packets ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].bufpq_dropping_packets = 1;
    }
    /* Since we're interrupting the stream anyways, drop enough packets to get
       back to our target buffer size */
    if (dev->endpoint[EP2I(ep)].bufpq_dropping_packets) {
        if (dev->endpoint[EP2I(ep)].bufpq_size >
                dev->endpoint[EP2I(ep)].bufpq_target_size) {
            free(free_on_destroy);
            return -1;
        }
        dev->endpoint[EP2I(ep)].bufpq_dropping_packets = 0;
    }

    bufp = g_new(struct buf_packet, 1);
    bufp->data   = data;
    bufp->len    = len;
    bufp->offset = 0;
    bufp->status = status;
    bufp->free_on_destroy = free_on_destroy;
    QTAILQ_INSERT_TAIL(&dev->endpoint[EP2I(ep)].bufpq, bufp, next);
    dev->endpoint[EP2I(ep)].bufpq_size++;
    return 0;
}

static void bufp_free(USBRedirDevice *dev, struct buf_packet *bufp,
    uint8_t ep)
{
    QTAILQ_REMOVE(&dev->endpoint[EP2I(ep)].bufpq, bufp, next);
    dev->endpoint[EP2I(ep)].bufpq_size--;
    free(bufp->free_on_destroy);
    g_free(bufp);
}

static void usbredir_free_bufpq(USBRedirDevice *dev, uint8_t ep)
{
    struct buf_packet *buf, *buf_next;

    QTAILQ_FOREACH_SAFE(buf, &dev->endpoint[EP2I(ep)].bufpq, next, buf_next) {
        bufp_free(dev, buf, ep);
    }
}

/*
 * USBDevice callbacks
 */

static void usbredir_handle_reset(USBDevice *udev)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);

    DPRINTF("reset device\n");
    usbredirparser_send_reset(dev->parser);
    usbredirparser_do_write(dev->parser);
}

static void usbredir_handle_iso_data(USBRedirDevice *dev, USBPacket *p,
                                     uint8_t ep)
{
    int status, len;
    if (!dev->endpoint[EP2I(ep)].iso_started &&
            !dev->endpoint[EP2I(ep)].iso_error) {
        struct usb_redir_start_iso_stream_header start_iso = {
            .endpoint = ep,
        };
        int pkts_per_sec;

        if (dev->dev.speed == USB_SPEED_HIGH) {
            pkts_per_sec = 8000 / dev->endpoint[EP2I(ep)].interval;
        } else {
            pkts_per_sec = 1000 / dev->endpoint[EP2I(ep)].interval;
        }
        /* Testing has shown that we need circa 60 ms buffer */
        dev->endpoint[EP2I(ep)].bufpq_target_size = (pkts_per_sec * 60) / 1000;

        /* Aim for approx 100 interrupts / second on the client to
           balance latency and interrupt load */
        start_iso.pkts_per_urb = pkts_per_sec / 100;
        if (start_iso.pkts_per_urb < 1) {
            start_iso.pkts_per_urb = 1;
        } else if (start_iso.pkts_per_urb > 32) {
            start_iso.pkts_per_urb = 32;
        }

        start_iso.no_urbs = DIV_ROUND_UP(
                                     dev->endpoint[EP2I(ep)].bufpq_target_size,
                                     start_iso.pkts_per_urb);
        /* Output endpoints pre-fill only 1/2 of the packets, keeping the rest
           as overflow buffer. Also see the usbredir protocol documentation */
        if (!(ep & USB_DIR_IN)) {
            start_iso.no_urbs *= 2;
        }
        if (start_iso.no_urbs > 16) {
            start_iso.no_urbs = 16;
        }

        /* No id, we look at the ep when receiving a status back */
        usbredirparser_send_start_iso_stream(dev->parser, 0, &start_iso);
        usbredirparser_do_write(dev->parser);
        DPRINTF("iso stream started pkts/sec %d pkts/urb %d urbs %d ep %02X\n",
                pkts_per_sec, start_iso.pkts_per_urb, start_iso.no_urbs, ep);
        dev->endpoint[EP2I(ep)].iso_started = 1;
        dev->endpoint[EP2I(ep)].bufpq_prefilled = 0;
        dev->endpoint[EP2I(ep)].bufpq_dropping_packets = 0;
    }

    if (ep & USB_DIR_IN) {
        struct buf_packet *isop;

        if (dev->endpoint[EP2I(ep)].iso_started &&
                !dev->endpoint[EP2I(ep)].bufpq_prefilled) {
            if (dev->endpoint[EP2I(ep)].bufpq_size <
                    dev->endpoint[EP2I(ep)].bufpq_target_size) {
                return;
            }
            dev->endpoint[EP2I(ep)].bufpq_prefilled = 1;
        }

        isop = QTAILQ_FIRST(&dev->endpoint[EP2I(ep)].bufpq);
        if (isop == NULL) {
            DPRINTF("iso-token-in ep %02X, no isop, iso_error: %d\n",
                    ep, dev->endpoint[EP2I(ep)].iso_error);
            /* Re-fill the buffer */
            dev->endpoint[EP2I(ep)].bufpq_prefilled = 0;
            /* Check iso_error for stream errors, otherwise its an underrun */
            status = dev->endpoint[EP2I(ep)].iso_error;
            dev->endpoint[EP2I(ep)].iso_error = 0;
            p->status = status ? USB_RET_IOERROR : USB_RET_SUCCESS;
            return;
        }
        DPRINTF2("iso-token-in ep %02X status %d len %d queue-size: %d\n", ep,
                 isop->status, isop->len, dev->endpoint[EP2I(ep)].bufpq_size);

        status = isop->status;
        len = isop->len;
        if (len > p->iov.size) {
            ERROR("received iso data is larger then packet ep %02X (%d > %d)\n",
                  ep, len, (int)p->iov.size);
            len = p->iov.size;
            status = usb_redir_babble;
        }
        usb_packet_copy(p, isop->data, len);
        bufp_free(dev, isop, ep);
        usbredir_handle_status(dev, p, status);
    } else {
        /* If the stream was not started because of a pending error don't
           send the packet to the usb-host */
        if (dev->endpoint[EP2I(ep)].iso_started) {
            struct usb_redir_iso_packet_header iso_packet = {
                .endpoint = ep,
                .length = p->iov.size
            };
            g_autofree uint8_t *buf = g_malloc(p->iov.size);
            /* No id, we look at the ep when receiving a status back */
            usb_packet_copy(p, buf, p->iov.size);
            usbredirparser_send_iso_packet(dev->parser, 0, &iso_packet,
                                           buf, p->iov.size);
            usbredirparser_do_write(dev->parser);
        }
        status = dev->endpoint[EP2I(ep)].iso_error;
        dev->endpoint[EP2I(ep)].iso_error = 0;
        DPRINTF2("iso-token-out ep %02X status %d len %zd\n", ep, status,
                 p->iov.size);
        usbredir_handle_status(dev, p, status);
    }
}

static void usbredir_stop_iso_stream(USBRedirDevice *dev, uint8_t ep)
{
    struct usb_redir_stop_iso_stream_header stop_iso_stream = {
        .endpoint = ep
    };
    if (dev->endpoint[EP2I(ep)].iso_started) {
        usbredirparser_send_stop_iso_stream(dev->parser, 0, &stop_iso_stream);
        DPRINTF("iso stream stopped ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].iso_started = 0;
    }
    dev->endpoint[EP2I(ep)].iso_error = 0;
    usbredir_free_bufpq(dev, ep);
}

/*
 * The usb-host may poll the endpoint faster then our guest, resulting in lots
 * of smaller bulkp-s. The below buffered_bulk_in_complete* functions combine
 * data from multiple bulkp-s into a single packet, avoiding bufpq overflows.
 */
static void usbredir_buffered_bulk_add_data_to_packet(USBRedirDevice *dev,
    struct buf_packet *bulkp, int count, USBPacket *p, uint8_t ep)
{
    usb_packet_copy(p, bulkp->data + bulkp->offset, count);
    bulkp->offset += count;
    if (bulkp->offset == bulkp->len) {
        /* Store status in the last packet with data from this bulkp */
        usbredir_handle_status(dev, p, bulkp->status);
        bufp_free(dev, bulkp, ep);
    }
}

static void usbredir_buffered_bulk_in_complete_raw(USBRedirDevice *dev,
    USBPacket *p, uint8_t ep)
{
    struct buf_packet *bulkp;
    int count;

    while ((bulkp = QTAILQ_FIRST(&dev->endpoint[EP2I(ep)].bufpq)) &&
           p->actual_length < p->iov.size && p->status == USB_RET_SUCCESS) {
        count = bulkp->len - bulkp->offset;
        if (count > (p->iov.size - p->actual_length)) {
            count = p->iov.size - p->actual_length;
        }
        usbredir_buffered_bulk_add_data_to_packet(dev, bulkp, count, p, ep);
    }
}

static void usbredir_buffered_bulk_in_complete_ftdi(USBRedirDevice *dev,
    USBPacket *p, uint8_t ep)
{
    const int maxp = dev->endpoint[EP2I(ep)].max_packet_size;
    uint8_t header[2] = { 0, 0 };
    struct buf_packet *bulkp;
    int count;

    while ((bulkp = QTAILQ_FIRST(&dev->endpoint[EP2I(ep)].bufpq)) &&
           p->actual_length < p->iov.size && p->status == USB_RET_SUCCESS) {
        if (bulkp->len < 2) {
            WARNING("malformed ftdi bulk in packet\n");
            bufp_free(dev, bulkp, ep);
            continue;
        }

        if ((p->actual_length % maxp) == 0) {
            usb_packet_copy(p, bulkp->data, 2);
            memcpy(header, bulkp->data, 2);
        } else {
            if (bulkp->data[0] != header[0] || bulkp->data[1] != header[1]) {
                break; /* Different header, add to next packet */
            }
        }

        if (bulkp->offset == 0) {
            bulkp->offset = 2; /* Skip header */
        }
        count = bulkp->len - bulkp->offset;
        /* Must repeat the header at maxp interval */
        if (count > (maxp - (p->actual_length % maxp))) {
            count = maxp - (p->actual_length % maxp);
        }
        usbredir_buffered_bulk_add_data_to_packet(dev, bulkp, count, p, ep);
    }
}

static void usbredir_buffered_bulk_in_complete(USBRedirDevice *dev,
    USBPacket *p, uint8_t ep)
{
    p->status = USB_RET_SUCCESS; /* Clear previous ASYNC status */
    dev->buffered_bulk_in_complete(dev, p, ep);
    DPRINTF("bulk-token-in ep %02X status %d len %d id %"PRIu64"\n",
            ep, p->status, p->actual_length, p->id);
}

static void usbredir_handle_buffered_bulk_in_data(USBRedirDevice *dev,
    USBPacket *p, uint8_t ep)
{
    /* Input bulk endpoint, buffered packet input */
    if (!dev->endpoint[EP2I(ep)].bulk_receiving_started) {
        int bpt;
        struct usb_redir_start_bulk_receiving_header start = {
            .endpoint = ep,
            .stream_id = 0,
            .no_transfers = 5,
        };
        /* Round bytes_per_transfer up to a multiple of max_packet_size */
        bpt = 512 + dev->endpoint[EP2I(ep)].max_packet_size - 1;
        bpt /= dev->endpoint[EP2I(ep)].max_packet_size;
        bpt *= dev->endpoint[EP2I(ep)].max_packet_size;
        start.bytes_per_transfer = bpt;
        /* No id, we look at the ep when receiving a status back */
        usbredirparser_send_start_bulk_receiving(dev->parser, 0, &start);
        usbredirparser_do_write(dev->parser);
        DPRINTF("bulk receiving started bytes/transfer %u count %d ep %02X\n",
                start.bytes_per_transfer, start.no_transfers, ep);
        dev->endpoint[EP2I(ep)].bulk_receiving_started = 1;
        /* We don't really want to drop bulk packets ever, but
           having some upper limit to how much we buffer is good. */
        dev->endpoint[EP2I(ep)].bufpq_target_size = 5000;
        dev->endpoint[EP2I(ep)].bufpq_dropping_packets = 0;
    }

    if (QTAILQ_EMPTY(&dev->endpoint[EP2I(ep)].bufpq)) {
        DPRINTF("bulk-token-in ep %02X, no bulkp\n", ep);
        assert(dev->endpoint[EP2I(ep)].pending_async_packet == NULL);
        dev->endpoint[EP2I(ep)].pending_async_packet = p;
        p->status = USB_RET_ASYNC;
        return;
    }
    usbredir_buffered_bulk_in_complete(dev, p, ep);
}

static void usbredir_stop_bulk_receiving(USBRedirDevice *dev, uint8_t ep)
{
    struct usb_redir_stop_bulk_receiving_header stop_bulk = {
        .endpoint = ep,
        .stream_id = 0,
    };
    if (dev->endpoint[EP2I(ep)].bulk_receiving_started) {
        usbredirparser_send_stop_bulk_receiving(dev->parser, 0, &stop_bulk);
        DPRINTF("bulk receiving stopped ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].bulk_receiving_started = 0;
    }
    usbredir_free_bufpq(dev, ep);
}

static void usbredir_handle_bulk_data(USBRedirDevice *dev, USBPacket *p,
                                      uint8_t ep)
{
    struct usb_redir_bulk_packet_header bulk_packet;
    size_t size = usb_packet_size(p);
    const int maxp = dev->endpoint[EP2I(ep)].max_packet_size;

    if (usbredir_already_in_flight(dev, p->id)) {
        p->status = USB_RET_ASYNC;
        return;
    }

    if (dev->endpoint[EP2I(ep)].bulk_receiving_enabled) {
        if (size != 0 && (size % maxp) == 0) {
            usbredir_handle_buffered_bulk_in_data(dev, p, ep);
            return;
        }
        WARNING("bulk recv invalid size %zd ep %02x, disabling\n", size, ep);
        assert(dev->endpoint[EP2I(ep)].pending_async_packet == NULL);
        usbredir_stop_bulk_receiving(dev, ep);
        dev->endpoint[EP2I(ep)].bulk_receiving_enabled = 0;
    }

    DPRINTF("bulk-out ep %02X stream %u len %zd id %"PRIu64"\n",
            ep, p->stream, size, p->id);

    bulk_packet.endpoint  = ep;
    bulk_packet.length    = size;
    bulk_packet.stream_id = p->stream;
    bulk_packet.length_high = size >> 16;
    assert(bulk_packet.length_high == 0 ||
           usbredirparser_peer_has_cap(dev->parser,
                                       usb_redir_cap_32bits_bulk_length));

    if (ep & USB_DIR_IN || size == 0) {
        usbredirparser_send_bulk_packet(dev->parser, p->id,
                                        &bulk_packet, NULL, 0);
    } else {
        g_autofree uint8_t *buf = g_malloc(size);
        usb_packet_copy(p, buf, size);
        usbredir_log_data(dev, "bulk data out:", buf, size);
        usbredirparser_send_bulk_packet(dev->parser, p->id,
                                        &bulk_packet, buf, size);
    }
    usbredirparser_do_write(dev->parser);
    p->status = USB_RET_ASYNC;
}

static void usbredir_handle_interrupt_in_data(USBRedirDevice *dev,
                                              USBPacket *p, uint8_t ep)
{
    /* Input interrupt endpoint, buffered packet input */
    struct buf_packet *intp, *intp_to_free;
    int status, len, sum;

    if (!dev->endpoint[EP2I(ep)].interrupt_started &&
            !dev->endpoint[EP2I(ep)].interrupt_error) {
        struct usb_redir_start_interrupt_receiving_header start_int = {
            .endpoint = ep,
        };
        /* No id, we look at the ep when receiving a status back */
        usbredirparser_send_start_interrupt_receiving(dev->parser, 0,
                                                      &start_int);
        usbredirparser_do_write(dev->parser);
        DPRINTF("interrupt recv started ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].interrupt_started = 1;
        /* We don't really want to drop interrupt packets ever, but
           having some upper limit to how much we buffer is good. */
        dev->endpoint[EP2I(ep)].bufpq_target_size = 1000;
        dev->endpoint[EP2I(ep)].bufpq_dropping_packets = 0;
    }

    /* check for completed interrupt message (with all fragments) */
    sum = 0;
    QTAILQ_FOREACH(intp, &dev->endpoint[EP2I(ep)].bufpq, next) {
        sum += intp->len;
        if (intp->len < dev->endpoint[EP2I(ep)].max_packet_size ||
            sum >= p->iov.size)
            break;
    }

    if (intp == NULL) {
        DPRINTF2("interrupt-token-in ep %02X, no intp, buffered %d\n", ep, sum);
        /* Check interrupt_error for stream errors */
        status = dev->endpoint[EP2I(ep)].interrupt_error;
        dev->endpoint[EP2I(ep)].interrupt_error = 0;
        if (status) {
            usbredir_handle_status(dev, p, status);
        } else {
            p->status = USB_RET_NAK;
        }
        return;
    }

    /* copy of completed interrupt message */
    sum = 0;
    status = usb_redir_success;
    intp_to_free = NULL;
    QTAILQ_FOREACH(intp, &dev->endpoint[EP2I(ep)].bufpq, next) {
        if (intp_to_free) {
            bufp_free(dev, intp_to_free, ep);
        }
        DPRINTF("interrupt-token-in ep %02X fragment status %d len %d\n", ep,
                intp->status, intp->len);

        sum += intp->len;
        len = intp->len;
        if (status == usb_redir_success) {
            status = intp->status;
        }
        if (sum > p->iov.size) {
            ERROR("received int data is larger then packet ep %02X\n", ep);
            len -= (sum - p->iov.size);
            sum = p->iov.size;
            status = usb_redir_babble;
        }

        usb_packet_copy(p, intp->data, len);

        intp_to_free = intp;
        if (intp->len < dev->endpoint[EP2I(ep)].max_packet_size ||
            sum >= p->iov.size)
            break;
    }
    if (intp_to_free) {
        bufp_free(dev, intp_to_free, ep);
    }
    DPRINTF("interrupt-token-in ep %02X summary status %d len %d\n", ep,
            status, sum);
    usbredir_handle_status(dev, p, status);
}

/*
 * Handle interrupt out data, the usbredir protocol expects us to do this
 * async, so that it can report back a completion status. But guests will
 * expect immediate completion for an interrupt endpoint, and handling this
 * async causes migration issues. So we report success directly, counting
 * on the fact that output interrupt packets normally always succeed.
 */
static void usbredir_handle_interrupt_out_data(USBRedirDevice *dev,
                                               USBPacket *p, uint8_t ep)
{
    struct usb_redir_interrupt_packet_header interrupt_packet;
    g_autofree uint8_t *buf = g_malloc(p->iov.size);

    DPRINTF("interrupt-out ep %02X len %zd id %"PRIu64"\n", ep,
            p->iov.size, p->id);

    interrupt_packet.endpoint  = ep;
    interrupt_packet.length    = p->iov.size;

    usb_packet_copy(p, buf, p->iov.size);
    usbredir_log_data(dev, "interrupt data out:", buf, p->iov.size);
    usbredirparser_send_interrupt_packet(dev->parser, p->id,
                                    &interrupt_packet, buf, p->iov.size);
    usbredirparser_do_write(dev->parser);
}

static void usbredir_stop_interrupt_receiving(USBRedirDevice *dev,
    uint8_t ep)
{
    struct usb_redir_stop_interrupt_receiving_header stop_interrupt_recv = {
        .endpoint = ep
    };
    if (dev->endpoint[EP2I(ep)].interrupt_started) {
        usbredirparser_send_stop_interrupt_receiving(dev->parser, 0,
                                                     &stop_interrupt_recv);
        DPRINTF("interrupt recv stopped ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].interrupt_started = 0;
    }
    dev->endpoint[EP2I(ep)].interrupt_error = 0;
    usbredir_free_bufpq(dev, ep);
}

static void usbredir_handle_data(USBDevice *udev, USBPacket *p)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);
    uint8_t ep;

    ep = p->ep->nr;
    if (p->pid == USB_TOKEN_IN) {
        ep |= USB_DIR_IN;
    }

    switch (dev->endpoint[EP2I(ep)].type) {
    case USB_ENDPOINT_XFER_CONTROL:
        ERROR("handle_data called for control transfer on ep %02X\n", ep);
        p->status = USB_RET_NAK;
        break;
    case USB_ENDPOINT_XFER_BULK:
        if (p->state == USB_PACKET_SETUP && p->pid == USB_TOKEN_IN &&
                p->ep->pipeline) {
            p->status = USB_RET_ADD_TO_QUEUE;
            break;
        }
        usbredir_handle_bulk_data(dev, p, ep);
        break;
    case USB_ENDPOINT_XFER_ISOC:
        usbredir_handle_iso_data(dev, p, ep);
        break;
    case USB_ENDPOINT_XFER_INT:
        if (ep & USB_DIR_IN) {
            usbredir_handle_interrupt_in_data(dev, p, ep);
        } else {
            usbredir_handle_interrupt_out_data(dev, p, ep);
        }
        break;
    default:
        ERROR("handle_data ep %02X has unknown type %d\n", ep,
              dev->endpoint[EP2I(ep)].type);
        p->status = USB_RET_NAK;
    }
}

static void usbredir_flush_ep_queue(USBDevice *dev, USBEndpoint *ep)
{
    if (ep->pid == USB_TOKEN_IN && ep->pipeline) {
        usb_ep_combine_input_packets(ep);
    }
}

static void usbredir_stop_ep(USBRedirDevice *dev, int i)
{
    uint8_t ep = I2EP(i);

    switch (dev->endpoint[i].type) {
    case USB_ENDPOINT_XFER_BULK:
        if (ep & USB_DIR_IN) {
            usbredir_stop_bulk_receiving(dev, ep);
        }
        break;
    case USB_ENDPOINT_XFER_ISOC:
        usbredir_stop_iso_stream(dev, ep);
        break;
    case USB_ENDPOINT_XFER_INT:
        if (ep & USB_DIR_IN) {
            usbredir_stop_interrupt_receiving(dev, ep);
        }
        break;
    }
    usbredir_free_bufpq(dev, ep);
}

static void usbredir_ep_stopped(USBDevice *udev, USBEndpoint *uep)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);

    usbredir_stop_ep(dev, USBEP2I(uep));
    usbredirparser_do_write(dev->parser);
}

static void usbredir_set_config(USBRedirDevice *dev, USBPacket *p,
                                int config)
{
    struct usb_redir_set_configuration_header set_config;
    int i;

    DPRINTF("set config %d id %"PRIu64"\n", config, p->id);

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        usbredir_stop_ep(dev, i);
    }

    set_config.configuration = config;
    usbredirparser_send_set_configuration(dev->parser, p->id, &set_config);
    usbredirparser_do_write(dev->parser);
    p->status = USB_RET_ASYNC;
}

static void usbredir_get_config(USBRedirDevice *dev, USBPacket *p)
{
    DPRINTF("get config id %"PRIu64"\n", p->id);

    usbredirparser_send_get_configuration(dev->parser, p->id);
    usbredirparser_do_write(dev->parser);
    p->status = USB_RET_ASYNC;
}

static void usbredir_set_interface(USBRedirDevice *dev, USBPacket *p,
                                   int interface, int alt)
{
    struct usb_redir_set_alt_setting_header set_alt;
    int i;

    DPRINTF("set interface %d alt %d id %"PRIu64"\n", interface, alt, p->id);

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        if (dev->endpoint[i].interface == interface) {
            usbredir_stop_ep(dev, i);
        }
    }

    set_alt.interface = interface;
    set_alt.alt = alt;
    usbredirparser_send_set_alt_setting(dev->parser, p->id, &set_alt);
    usbredirparser_do_write(dev->parser);
    p->status = USB_RET_ASYNC;
}

static void usbredir_get_interface(USBRedirDevice *dev, USBPacket *p,
                                   int interface)
{
    struct usb_redir_get_alt_setting_header get_alt;

    DPRINTF("get interface %d id %"PRIu64"\n", interface, p->id);

    get_alt.interface = interface;
    usbredirparser_send_get_alt_setting(dev->parser, p->id, &get_alt);
    usbredirparser_do_write(dev->parser);
    p->status = USB_RET_ASYNC;
}

static void usbredir_handle_control(USBDevice *udev, USBPacket *p,
        int request, int value, int index, int length, uint8_t *data)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);
    struct usb_redir_control_packet_header control_packet;

    if (usbredir_already_in_flight(dev, p->id)) {
        p->status = USB_RET_ASYNC;
        return;
    }

    /* Special cases for certain standard device requests */
    switch (request) {
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        DPRINTF("set address %d\n", value);
        dev->dev.addr = value;
        return;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        usbredir_set_config(dev, p, value & 0xff);
        return;
    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        usbredir_get_config(dev, p);
        return;
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        usbredir_set_interface(dev, p, index, value);
        return;
    case InterfaceRequest | USB_REQ_GET_INTERFACE:
        usbredir_get_interface(dev, p, index);
        return;
    }

    /* Normal ctrl requests, note request is (bRequestType << 8) | bRequest */
    DPRINTF(
        "ctrl-out type 0x%x req 0x%x val 0x%x index %d len %d id %"PRIu64"\n",
        request >> 8, request & 0xff, value, index, length, p->id);

    control_packet.request     = request & 0xFF;
    control_packet.requesttype = request >> 8;
    control_packet.endpoint    = control_packet.requesttype & USB_DIR_IN;
    control_packet.value       = value;
    control_packet.index       = index;
    control_packet.length      = length;

    if (control_packet.requesttype & USB_DIR_IN) {
        usbredirparser_send_control_packet(dev->parser, p->id,
                                           &control_packet, NULL, 0);
    } else {
        usbredir_log_data(dev, "ctrl data out:", data, length);
        usbredirparser_send_control_packet(dev->parser, p->id,
                                           &control_packet, data, length);
    }
    usbredirparser_do_write(dev->parser);
    p->status = USB_RET_ASYNC;
}

static int usbredir_alloc_streams(USBDevice *udev, USBEndpoint **eps,
                                  int nr_eps, int streams)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);
#if USBREDIR_VERSION >= 0x000700
    struct usb_redir_alloc_bulk_streams_header alloc_streams;
    int i;

    if (!usbredirparser_peer_has_cap(dev->parser,
                                     usb_redir_cap_bulk_streams)) {
        ERROR("peer does not support streams\n");
        goto reject;
    }

    if (streams == 0) {
        ERROR("request to allocate 0 streams\n");
        return -1;
    }

    alloc_streams.no_streams = streams;
    alloc_streams.endpoints = 0;
    for (i = 0; i < nr_eps; i++) {
        alloc_streams.endpoints |= 1 << USBEP2I(eps[i]);
    }
    usbredirparser_send_alloc_bulk_streams(dev->parser, 0, &alloc_streams);
    usbredirparser_do_write(dev->parser);

    return 0;
#else
    ERROR("usbredir_alloc_streams not implemented\n");
    goto reject;
#endif
reject:
    ERROR("streams are not available, disconnecting\n");
    qemu_bh_schedule(dev->device_reject_bh);
    return -1;
}

static void usbredir_free_streams(USBDevice *udev, USBEndpoint **eps,
                                  int nr_eps)
{
#if USBREDIR_VERSION >= 0x000700
    USBRedirDevice *dev = USB_REDIRECT(udev);
    struct usb_redir_free_bulk_streams_header free_streams;
    int i;

    if (!usbredirparser_peer_has_cap(dev->parser,
                                     usb_redir_cap_bulk_streams)) {
        return;
    }

    free_streams.endpoints = 0;
    for (i = 0; i < nr_eps; i++) {
        free_streams.endpoints |= 1 << USBEP2I(eps[i]);
    }
    usbredirparser_send_free_bulk_streams(dev->parser, 0, &free_streams);
    usbredirparser_do_write(dev->parser);
#endif
}

/*
 * Close events can be triggered by usbredirparser_do_write which gets called
 * from within the USBDevice data / control packet callbacks and doing a
 * usb_detach from within these callbacks is not a good idea.
 *
 * So we use a bh handler to take care of close events.
 */
static void usbredir_chardev_close_bh(void *opaque)
{
    USBRedirDevice *dev = opaque;

    qemu_bh_cancel(dev->device_reject_bh);
    usbredir_device_disconnect(dev);

    if (dev->parser) {
        DPRINTF("destroying usbredirparser\n");
        usbredirparser_destroy(dev->parser);
        dev->parser = NULL;
    }
    if (dev->watch) {
        g_source_remove(dev->watch);
        dev->watch = 0;
    }
}

static void usbredir_create_parser(USBRedirDevice *dev)
{
    uint32_t caps[USB_REDIR_CAPS_SIZE] = { 0, };
    int flags = 0;

    DPRINTF("creating usbredirparser\n");

    dev->parser = usbredirparser_create();
    if (!dev->parser) {
        error_report("usbredirparser_create() failed");
        exit(1);
    }
    dev->parser->priv = dev;
    dev->parser->log_func = usbredir_log;
    dev->parser->read_func = usbredir_read;
    dev->parser->write_func = usbredir_write;
    dev->parser->hello_func = usbredir_hello;
    dev->parser->device_connect_func = usbredir_device_connect;
    dev->parser->device_disconnect_func = usbredir_device_disconnect;
    dev->parser->interface_info_func = usbredir_interface_info;
    dev->parser->ep_info_func = usbredir_ep_info;
    dev->parser->configuration_status_func = usbredir_configuration_status;
    dev->parser->alt_setting_status_func = usbredir_alt_setting_status;
    dev->parser->iso_stream_status_func = usbredir_iso_stream_status;
    dev->parser->interrupt_receiving_status_func =
        usbredir_interrupt_receiving_status;
    dev->parser->bulk_streams_status_func = usbredir_bulk_streams_status;
    dev->parser->bulk_receiving_status_func = usbredir_bulk_receiving_status;
    dev->parser->control_packet_func = usbredir_control_packet;
    dev->parser->bulk_packet_func = usbredir_bulk_packet;
    dev->parser->iso_packet_func = usbredir_iso_packet;
    dev->parser->interrupt_packet_func = usbredir_interrupt_packet;
    dev->parser->buffered_bulk_packet_func = usbredir_buffered_bulk_packet;
    dev->read_buf = NULL;
    dev->read_buf_size = 0;

    usbredirparser_caps_set_cap(caps, usb_redir_cap_connect_device_version);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_filter);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_ep_info_max_packet_size);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_64bits_ids);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_32bits_bulk_length);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_bulk_receiving);
#if USBREDIR_VERSION >= 0x000700
    if (dev->enable_streams) {
        usbredirparser_caps_set_cap(caps, usb_redir_cap_bulk_streams);
    }
#endif

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        flags |= usbredirparser_fl_no_hello;
    }
    usbredirparser_init(dev->parser, VERSION, caps, USB_REDIR_CAPS_SIZE,
                        flags);
    usbredirparser_do_write(dev->parser);
}

static void usbredir_reject_device(USBRedirDevice *dev)
{
    usbredir_device_disconnect(dev);
    if (usbredirparser_peer_has_cap(dev->parser, usb_redir_cap_filter)) {
        usbredirparser_send_filter_reject(dev->parser);
        usbredirparser_do_write(dev->parser);
    }
}

/*
 * We may need to reject the device when the hcd calls alloc_streams, doing
 * an usb_detach from within a hcd call is not a good idea, hence this bh.
 */
static void usbredir_device_reject_bh(void *opaque)
{
    USBRedirDevice *dev = opaque;

    usbredir_reject_device(dev);
}

static void usbredir_do_attach(void *opaque)
{
    USBRedirDevice *dev = opaque;
    Error *local_err = NULL;

    /* In order to work properly with XHCI controllers we need these caps */
    if ((dev->dev.port->speedmask & USB_SPEED_MASK_SUPER) && !(
        usbredirparser_peer_has_cap(dev->parser,
                                    usb_redir_cap_ep_info_max_packet_size) &&
        usbredirparser_peer_has_cap(dev->parser,
                                    usb_redir_cap_32bits_bulk_length) &&
        usbredirparser_peer_has_cap(dev->parser,
                                    usb_redir_cap_64bits_ids))) {
        ERROR("usb-redir-host lacks capabilities needed for use with XHCI\n");
        usbredir_reject_device(dev);
        return;
    }

    usb_device_attach(&dev->dev, &local_err);
    if (local_err) {
        error_report_err(local_err);
        WARNING("rejecting device due to speed mismatch\n");
        usbredir_reject_device(dev);
    }
}

/*
 * chardev callbacks
 */

static int usbredir_chardev_can_read(void *opaque)
{
    USBRedirDevice *dev = opaque;

    if (!dev->parser) {
        WARNING("chardev_can_read called on non open chardev!\n");
        return 0;
    }

    /* Don't read new data from the chardev until our state is fully synced */
    if (!runstate_check(RUN_STATE_RUNNING)) {
        return 0;
    }

    /* usbredir_parser_do_read will consume *all* data we give it */
    return 1 * MiB;
}

static void usbredir_chardev_read(void *opaque, const uint8_t *buf, int size)
{
    USBRedirDevice *dev = opaque;

    /* No recursion allowed! */
    assert(dev->read_buf == NULL);

    dev->read_buf = buf;
    dev->read_buf_size = size;

    usbredirparser_do_read(dev->parser);
    /* Send any acks, etc. which may be queued now */
    usbredirparser_do_write(dev->parser);
}

static void usbredir_chardev_event(void *opaque, QEMUChrEvent event)
{
    USBRedirDevice *dev = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
        DPRINTF("chardev open\n");
        /* Make sure any pending closes are handled (no-op if none pending) */
        usbredir_chardev_close_bh(dev);
        qemu_bh_cancel(dev->chardev_close_bh);
        usbredir_create_parser(dev);
        break;
    case CHR_EVENT_CLOSED:
        DPRINTF("chardev close\n");
        qemu_bh_schedule(dev->chardev_close_bh);
        break;
    case CHR_EVENT_BREAK:
    case CHR_EVENT_MUX_IN:
    case CHR_EVENT_MUX_OUT:
        /* Ignore */
        break;
    }
}

/*
 * init + destroy
 */

static void usbredir_vm_state_change(void *priv, bool running, RunState state)
{
    USBRedirDevice *dev = priv;

    if (running && dev->parser != NULL) {
        usbredirparser_do_write(dev->parser); /* Flush any pending writes */
    }
}

static void usbredir_init_endpoints(USBRedirDevice *dev)
{
    int i;

    usb_ep_init(&dev->dev);
    memset(dev->endpoint, 0, sizeof(dev->endpoint));
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        dev->endpoint[i].dev = dev;
        QTAILQ_INIT(&dev->endpoint[i].bufpq);
    }
}

static void usbredir_realize(USBDevice *udev, Error **errp)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);
    int i;

    if (!qemu_chr_fe_backend_connected(&dev->cs)) {
        error_setg(errp, QERR_MISSING_PARAMETER, "chardev");
        return;
    }

    if (dev->filter_str) {
        i = usbredirfilter_string_to_rules(dev->filter_str, ":", "|",
                                           &dev->filter_rules,
                                           &dev->filter_rules_count);
        if (i) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "filter",
                       "a usb device filter string");
            return;
        }
    }

    dev->chardev_close_bh = qemu_bh_new_guarded(usbredir_chardev_close_bh, dev,
                                                &DEVICE(dev)->mem_reentrancy_guard);
    dev->device_reject_bh = qemu_bh_new_guarded(usbredir_device_reject_bh, dev,
                                                &DEVICE(dev)->mem_reentrancy_guard);
    dev->attach_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, usbredir_do_attach, dev);

    packet_id_queue_init(&dev->cancelled, dev, "cancelled");
    packet_id_queue_init(&dev->already_in_flight, dev, "already-in-flight");
    usbredir_init_endpoints(dev);

    /* We'll do the attach once we receive the speed from the usb-host */
    udev->auto_attach = 0;

    /* Will be cleared during setup when we find conflicts */
    dev->compatible_speedmask = USB_SPEED_MASK_FULL | USB_SPEED_MASK_HIGH;

    /* Let the backend know we are ready */
    qemu_chr_fe_set_handlers(&dev->cs, usbredir_chardev_can_read,
                             usbredir_chardev_read, usbredir_chardev_event,
                             NULL, dev, NULL, true);

    dev->vmstate =
        qemu_add_vm_change_state_handler(usbredir_vm_state_change, dev);
}

static void usbredir_cleanup_device_queues(USBRedirDevice *dev)
{
    int i;

    packet_id_queue_empty(&dev->cancelled);
    packet_id_queue_empty(&dev->already_in_flight);
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        usbredir_free_bufpq(dev, I2EP(i));
    }
}

static void usbredir_unrealize(USBDevice *udev)
{
    USBRedirDevice *dev = USB_REDIRECT(udev);

    qemu_chr_fe_deinit(&dev->cs, true);

    /* Note must be done after qemu_chr_close, as that causes a close event */
    qemu_bh_delete(dev->chardev_close_bh);
    qemu_bh_delete(dev->device_reject_bh);

    timer_free(dev->attach_timer);

    usbredir_cleanup_device_queues(dev);

    if (dev->parser) {
        usbredirparser_destroy(dev->parser);
    }
    if (dev->watch) {
        g_source_remove(dev->watch);
    }

    free(dev->filter_rules);
    qemu_del_vm_change_state_handler(dev->vmstate);
}

static int usbredir_check_filter(USBRedirDevice *dev)
{
    if (dev->interface_info.interface_count == NO_INTERFACE_INFO) {
        ERROR("No interface info for device\n");
        goto error;
    }

    if (dev->filter_rules) {
        if (!usbredirparser_peer_has_cap(dev->parser,
                                    usb_redir_cap_connect_device_version)) {
            ERROR("Device filter specified and peer does not have the "
                  "connect_device_version capability\n");
            goto error;
        }

        if (usbredirfilter_check(
                dev->filter_rules,
                dev->filter_rules_count,
                dev->device_info.device_class,
                dev->device_info.device_subclass,
                dev->device_info.device_protocol,
                dev->interface_info.interface_class,
                dev->interface_info.interface_subclass,
                dev->interface_info.interface_protocol,
                dev->interface_info.interface_count,
                dev->device_info.vendor_id,
                dev->device_info.product_id,
                dev->device_info.device_version_bcd,
                0) != 0) {
            goto error;
        }
    }

    return 0;

error:
    usbredir_reject_device(dev);
    return -1;
}

static void usbredir_check_bulk_receiving(USBRedirDevice *dev)
{
    int i, j, quirks;

    if (!usbredirparser_peer_has_cap(dev->parser,
                                     usb_redir_cap_bulk_receiving)) {
        return;
    }

    for (i = EP2I(USB_DIR_IN); i < MAX_ENDPOINTS; i++) {
        dev->endpoint[i].bulk_receiving_enabled = 0;
    }

    if (dev->interface_info.interface_count == NO_INTERFACE_INFO) {
        return;
    }

    for (i = 0; i < dev->interface_info.interface_count; i++) {
        quirks = usb_get_quirks(dev->device_info.vendor_id,
                                dev->device_info.product_id,
                                dev->interface_info.interface_class[i],
                                dev->interface_info.interface_subclass[i],
                                dev->interface_info.interface_protocol[i]);
        if (!(quirks & USB_QUIRK_BUFFER_BULK_IN)) {
            continue;
        }
        if (quirks & USB_QUIRK_IS_FTDI) {
            dev->buffered_bulk_in_complete =
                usbredir_buffered_bulk_in_complete_ftdi;
        } else {
            dev->buffered_bulk_in_complete =
                usbredir_buffered_bulk_in_complete_raw;
        }

        for (j = EP2I(USB_DIR_IN); j < MAX_ENDPOINTS; j++) {
            if (dev->endpoint[j].interface ==
                                    dev->interface_info.interface[i] &&
                    dev->endpoint[j].type == USB_ENDPOINT_XFER_BULK &&
                    dev->endpoint[j].max_packet_size != 0) {
                dev->endpoint[j].bulk_receiving_enabled = 1;
                /*
                 * With buffering pipelining is not necessary. Also packet
                 * combining and bulk in buffering don't play nice together!
                 */
                I2USBEP(dev, j)->pipeline = false;
                break; /* Only buffer for the first ep of each intf */
            }
        }
    }
}

/*
 * usbredirparser packet complete callbacks
 */

static void usbredir_handle_status(USBRedirDevice *dev, USBPacket *p,
    int status)
{
    switch (status) {
    case usb_redir_success:
        p->status = USB_RET_SUCCESS; /* Clear previous ASYNC status */
        break;
    case usb_redir_stall:
        p->status = USB_RET_STALL;
        break;
    case usb_redir_cancelled:
        /*
         * When the usbredir-host unredirects a device, it will report a status
         * of cancelled for all pending packets, followed by a disconnect msg.
         */
        p->status = USB_RET_IOERROR;
        break;
    case usb_redir_inval:
        WARNING("got invalid param error from usb-host?\n");
        p->status = USB_RET_IOERROR;
        break;
    case usb_redir_babble:
        p->status = USB_RET_BABBLE;
        break;
    case usb_redir_ioerror:
    case usb_redir_timeout:
    default:
        p->status = USB_RET_IOERROR;
    }
}

static void usbredir_hello(void *priv, struct usb_redir_hello_header *h)
{
    USBRedirDevice *dev = priv;

    /* Try to send the filter info now that we've the usb-host's caps */
    if (usbredirparser_peer_has_cap(dev->parser, usb_redir_cap_filter) &&
            dev->filter_rules) {
        usbredirparser_send_filter_filter(dev->parser, dev->filter_rules,
                                          dev->filter_rules_count);
        usbredirparser_do_write(dev->parser);
    }
}

static void usbredir_device_connect(void *priv,
    struct usb_redir_device_connect_header *device_connect)
{
    USBRedirDevice *dev = priv;
    const char *speed;

    if (timer_pending(dev->attach_timer) || dev->dev.attached) {
        ERROR("Received device connect while already connected\n");
        return;
    }

    switch (device_connect->speed) {
    case usb_redir_speed_low:
        speed = "low speed";
        dev->dev.speed = USB_SPEED_LOW;
        dev->compatible_speedmask &= ~USB_SPEED_MASK_FULL;
        dev->compatible_speedmask &= ~USB_SPEED_MASK_HIGH;
        break;
    case usb_redir_speed_full:
        speed = "full speed";
        dev->dev.speed = USB_SPEED_FULL;
        dev->compatible_speedmask &= ~USB_SPEED_MASK_HIGH;
        break;
    case usb_redir_speed_high:
        speed = "high speed";
        dev->dev.speed = USB_SPEED_HIGH;
        break;
    case usb_redir_speed_super:
        speed = "super speed";
        dev->dev.speed = USB_SPEED_SUPER;
        break;
    default:
        speed = "unknown speed";
        dev->dev.speed = USB_SPEED_FULL;
    }

    if (usbredirparser_peer_has_cap(dev->parser,
                                    usb_redir_cap_connect_device_version)) {
        INFO("attaching %s device %04x:%04x version %d.%d class %02x\n",
             speed, device_connect->vendor_id, device_connect->product_id,
             ((device_connect->device_version_bcd & 0xf000) >> 12) * 10 +
             ((device_connect->device_version_bcd & 0x0f00) >>  8),
             ((device_connect->device_version_bcd & 0x00f0) >>  4) * 10 +
             ((device_connect->device_version_bcd & 0x000f) >>  0),
             device_connect->device_class);
    } else {
        INFO("attaching %s device %04x:%04x class %02x\n", speed,
             device_connect->vendor_id, device_connect->product_id,
             device_connect->device_class);
    }

    dev->dev.speedmask = (1 << dev->dev.speed) | dev->compatible_speedmask;
    dev->device_info = *device_connect;

    if (usbredir_check_filter(dev)) {
        WARNING("Device %04x:%04x rejected by device filter, not attaching\n",
                device_connect->vendor_id, device_connect->product_id);
        return;
    }

    usbredir_check_bulk_receiving(dev);
    timer_mod(dev->attach_timer, dev->next_attach_time);
}

static void usbredir_device_disconnect(void *priv)
{
    USBRedirDevice *dev = priv;

    /* Stop any pending attaches */
    timer_del(dev->attach_timer);

    if (dev->dev.attached) {
        DPRINTF("detaching device\n");
        usb_device_detach(&dev->dev);
        /*
         * Delay next usb device attach to give the guest a chance to see
         * see the detach / attach in case of quick close / open succession
         */
        dev->next_attach_time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 200;
    }

    /* Reset state so that the next dev connected starts with a clean slate */
    usbredir_cleanup_device_queues(dev);
    usbredir_init_endpoints(dev);
    dev->interface_info.interface_count = NO_INTERFACE_INFO;
    dev->dev.addr = 0;
    dev->dev.speed = 0;
    dev->compatible_speedmask = USB_SPEED_MASK_FULL | USB_SPEED_MASK_HIGH;
}

static void usbredir_interface_info(void *priv,
    struct usb_redir_interface_info_header *interface_info)
{
    USBRedirDevice *dev = priv;

    dev->interface_info = *interface_info;

    /*
     * If we receive interface info after the device has already been
     * connected (ie on a set_config), re-check interface dependent things.
     */
    if (timer_pending(dev->attach_timer) || dev->dev.attached) {
        usbredir_check_bulk_receiving(dev);
        if (usbredir_check_filter(dev)) {
            ERROR("Device no longer matches filter after interface info "
                  "change, disconnecting!\n");
        }
    }
}

static void usbredir_mark_speed_incompatible(USBRedirDevice *dev, int speed)
{
    dev->compatible_speedmask &= ~(1 << speed);
    dev->dev.speedmask = (1 << dev->dev.speed) | dev->compatible_speedmask;
}

static void usbredir_set_pipeline(USBRedirDevice *dev, struct USBEndpoint *uep)
{
    if (uep->type != USB_ENDPOINT_XFER_BULK) {
        return;
    }
    if (uep->pid == USB_TOKEN_OUT) {
        uep->pipeline = true;
    }
    if (uep->pid == USB_TOKEN_IN && uep->max_packet_size != 0 &&
        usbredirparser_peer_has_cap(dev->parser,
                                    usb_redir_cap_32bits_bulk_length)) {
        uep->pipeline = true;
    }
}

static void usbredir_setup_usb_eps(USBRedirDevice *dev)
{
    struct USBEndpoint *usb_ep;
    int i;

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        usb_ep = I2USBEP(dev, i);
        usb_ep->type = dev->endpoint[i].type;
        usb_ep->ifnum = dev->endpoint[i].interface;
        usb_ep->max_packet_size = dev->endpoint[i].max_packet_size;
        usb_ep->max_streams = dev->endpoint[i].max_streams;
        usbredir_set_pipeline(dev, usb_ep);
    }
}

static void usbredir_ep_info(void *priv,
    struct usb_redir_ep_info_header *ep_info)
{
    USBRedirDevice *dev = priv;
    int i;

    assert(dev != NULL);
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        dev->endpoint[i].type = ep_info->type[i];
        dev->endpoint[i].interval = ep_info->interval[i];
        dev->endpoint[i].interface = ep_info->interface[i];
        if (usbredirparser_peer_has_cap(dev->parser,
                                     usb_redir_cap_ep_info_max_packet_size)) {
            dev->endpoint[i].max_packet_size = ep_info->max_packet_size[i];
        }
#if USBREDIR_VERSION >= 0x000700
        if (usbredirparser_peer_has_cap(dev->parser,
                                        usb_redir_cap_bulk_streams)) {
            dev->endpoint[i].max_streams = ep_info->max_streams[i];
        }
#endif
        switch (dev->endpoint[i].type) {
        case usb_redir_type_invalid:
            break;
        case usb_redir_type_iso:
            usbredir_mark_speed_incompatible(dev, USB_SPEED_FULL);
            usbredir_mark_speed_incompatible(dev, USB_SPEED_HIGH);
            /* Fall through */
        case usb_redir_type_interrupt:
            if (!usbredirparser_peer_has_cap(dev->parser,
                                     usb_redir_cap_ep_info_max_packet_size) ||
                    ep_info->max_packet_size[i] > 64) {
                usbredir_mark_speed_incompatible(dev, USB_SPEED_FULL);
            }
            if (!usbredirparser_peer_has_cap(dev->parser,
                                     usb_redir_cap_ep_info_max_packet_size) ||
                    ep_info->max_packet_size[i] > 1024) {
                usbredir_mark_speed_incompatible(dev, USB_SPEED_HIGH);
            }
            if (dev->endpoint[i].interval == 0) {
                ERROR("Received 0 interval for isoc or irq endpoint\n");
                usbredir_reject_device(dev);
                return;
            }
            /* Fall through */
        case usb_redir_type_control:
        case usb_redir_type_bulk:
            DPRINTF("ep: %02X type: %d interface: %d\n", I2EP(i),
                    dev->endpoint[i].type, dev->endpoint[i].interface);
            break;
        default:
            ERROR("Received invalid endpoint type\n");
            usbredir_reject_device(dev);
            return;
        }
    }
    /* The new ep info may have caused a speed incompatibility, recheck */
    if (dev->dev.attached &&
            !(dev->dev.port->speedmask & dev->dev.speedmask)) {
        ERROR("Device no longer matches speed after endpoint info change, "
              "disconnecting!\n");
        usbredir_reject_device(dev);
        return;
    }
    usbredir_setup_usb_eps(dev);
    usbredir_check_bulk_receiving(dev);
}

static void usbredir_configuration_status(void *priv, uint64_t id,
    struct usb_redir_configuration_status_header *config_status)
{
    USBRedirDevice *dev = priv;
    USBPacket *p;

    DPRINTF("set config status %d config %d id %"PRIu64"\n",
            config_status->status, config_status->configuration, id);

    p = usbredir_find_packet_by_id(dev, 0, id);
    if (p) {
        if (dev->dev.setup_buf[0] & USB_DIR_IN) {
            dev->dev.data_buf[0] = config_status->configuration;
            p->actual_length = 1;
        }
        usbredir_handle_status(dev, p, config_status->status);
        usb_generic_async_ctrl_complete(&dev->dev, p);
    }
}

static void usbredir_alt_setting_status(void *priv, uint64_t id,
    struct usb_redir_alt_setting_status_header *alt_setting_status)
{
    USBRedirDevice *dev = priv;
    USBPacket *p;

    DPRINTF("alt status %d intf %d alt %d id: %"PRIu64"\n",
            alt_setting_status->status, alt_setting_status->interface,
            alt_setting_status->alt, id);

    p = usbredir_find_packet_by_id(dev, 0, id);
    if (p) {
        if (dev->dev.setup_buf[0] & USB_DIR_IN) {
            dev->dev.data_buf[0] = alt_setting_status->alt;
            p->actual_length = 1;
        }
        usbredir_handle_status(dev, p, alt_setting_status->status);
        usb_generic_async_ctrl_complete(&dev->dev, p);
    }
}

static void usbredir_iso_stream_status(void *priv, uint64_t id,
    struct usb_redir_iso_stream_status_header *iso_stream_status)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = iso_stream_status->endpoint;

    DPRINTF("iso status %d ep %02X id %"PRIu64"\n", iso_stream_status->status,
            ep, id);

    if (!dev->dev.attached || !dev->endpoint[EP2I(ep)].iso_started) {
        return;
    }

    dev->endpoint[EP2I(ep)].iso_error = iso_stream_status->status;
    if (iso_stream_status->status == usb_redir_stall) {
        DPRINTF("iso stream stopped by peer ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].iso_started = 0;
    }
}

static void usbredir_interrupt_receiving_status(void *priv, uint64_t id,
    struct usb_redir_interrupt_receiving_status_header
    *interrupt_receiving_status)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = interrupt_receiving_status->endpoint;

    DPRINTF("interrupt recv status %d ep %02X id %"PRIu64"\n",
            interrupt_receiving_status->status, ep, id);

    if (!dev->dev.attached || !dev->endpoint[EP2I(ep)].interrupt_started) {
        return;
    }

    dev->endpoint[EP2I(ep)].interrupt_error =
        interrupt_receiving_status->status;
    if (interrupt_receiving_status->status == usb_redir_stall) {
        DPRINTF("interrupt receiving stopped by peer ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].interrupt_started = 0;
    }
}

static void usbredir_bulk_streams_status(void *priv, uint64_t id,
    struct usb_redir_bulk_streams_status_header *bulk_streams_status)
{
#if USBREDIR_VERSION >= 0x000700
    USBRedirDevice *dev = priv;

    if (bulk_streams_status->status == usb_redir_success) {
        DPRINTF("bulk streams status %d eps %08x\n",
                bulk_streams_status->status, bulk_streams_status->endpoints);
    } else {
        ERROR("bulk streams %s failed status %d eps %08x\n",
              (bulk_streams_status->no_streams == 0) ? "free" : "alloc",
              bulk_streams_status->status, bulk_streams_status->endpoints);
        ERROR("usb-redir-host does not provide streams, disconnecting\n");
        usbredir_reject_device(dev);
    }
#endif
}

static void usbredir_bulk_receiving_status(void *priv, uint64_t id,
    struct usb_redir_bulk_receiving_status_header *bulk_receiving_status)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = bulk_receiving_status->endpoint;

    DPRINTF("bulk recv status %d ep %02X id %"PRIu64"\n",
            bulk_receiving_status->status, ep, id);

    if (!dev->dev.attached || !dev->endpoint[EP2I(ep)].bulk_receiving_started) {
        return;
    }

    if (bulk_receiving_status->status == usb_redir_stall) {
        DPRINTF("bulk receiving stopped by peer ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].bulk_receiving_started = 0;
    }
}

static void usbredir_control_packet(void *priv, uint64_t id,
    struct usb_redir_control_packet_header *control_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    USBPacket *p;
    int len = control_packet->length;

    DPRINTF("ctrl-in status %d len %d id %"PRIu64"\n", control_packet->status,
            len, id);

    /* Fix up USB-3 ep0 maxpacket size to allow superspeed connected devices
     * to work redirected to a not superspeed capable hcd */
    if (dev->dev.speed == USB_SPEED_SUPER &&
            !((dev->dev.port->speedmask & USB_SPEED_MASK_SUPER)) &&
            control_packet->requesttype == 0x80 &&
            control_packet->request == 6 &&
            control_packet->value == 0x100 && control_packet->index == 0 &&
            data_len >= 18 && data[7] == 9) {
        data[7] = 64;
    }

    p = usbredir_find_packet_by_id(dev, 0, id);
    if (p) {
        usbredir_handle_status(dev, p, control_packet->status);
        if (data_len > 0) {
            usbredir_log_data(dev, "ctrl data in:", data, data_len);
            if (data_len > sizeof(dev->dev.data_buf)) {
                ERROR("ctrl buffer too small (%d > %zu)\n",
                      data_len, sizeof(dev->dev.data_buf));
                p->status = USB_RET_STALL;
                data_len = len = sizeof(dev->dev.data_buf);
            }
            memcpy(dev->dev.data_buf, data, data_len);
        }
        p->actual_length = len;
        /*
         * If this is GET_DESCRIPTOR request for configuration descriptor,
         * remove 'remote wakeup' flag from it to prevent idle power down
         * in Windows guest
         */
        if (dev->suppress_remote_wake &&
            control_packet->requesttype == USB_DIR_IN &&
            control_packet->request == USB_REQ_GET_DESCRIPTOR &&
            control_packet->value == (USB_DT_CONFIG << 8) &&
            control_packet->index == 0 &&
            /* bmAttributes field of config descriptor */
            len > 7 && (dev->dev.data_buf[7] & USB_CFG_ATT_WAKEUP)) {
                DPRINTF("Removed remote wake %04X:%04X\n",
                    dev->device_info.vendor_id,
                    dev->device_info.product_id);
                dev->dev.data_buf[7] &= ~USB_CFG_ATT_WAKEUP;
            }
        usb_generic_async_ctrl_complete(&dev->dev, p);
    }
    free(data);
}

static void usbredir_bulk_packet(void *priv, uint64_t id,
    struct usb_redir_bulk_packet_header *bulk_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = bulk_packet->endpoint;
    int len = (bulk_packet->length_high << 16) | bulk_packet->length;
    USBPacket *p;

    DPRINTF("bulk-in status %d ep %02X stream %u len %d id %"PRIu64"\n",
            bulk_packet->status, ep, bulk_packet->stream_id, len, id);

    p = usbredir_find_packet_by_id(dev, ep, id);
    if (p) {
        size_t size = usb_packet_size(p);
        usbredir_handle_status(dev, p, bulk_packet->status);
        if (data_len > 0) {
            usbredir_log_data(dev, "bulk data in:", data, data_len);
            if (data_len > size) {
                ERROR("bulk got more data then requested (%d > %zd)\n",
                      data_len, p->iov.size);
                p->status = USB_RET_BABBLE;
                data_len = len = size;
            }
            usb_packet_copy(p, data, data_len);
        }
        p->actual_length = len;
        if (p->pid == USB_TOKEN_IN && p->ep->pipeline) {
            usb_combined_input_packet_complete(&dev->dev, p);
        } else {
            usb_packet_complete(&dev->dev, p);
        }
    }
    free(data);
}

static void usbredir_iso_packet(void *priv, uint64_t id,
    struct usb_redir_iso_packet_header *iso_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = iso_packet->endpoint;

    DPRINTF2("iso-in status %d ep %02X len %d id %"PRIu64"\n",
             iso_packet->status, ep, data_len, id);

    if (dev->endpoint[EP2I(ep)].type != USB_ENDPOINT_XFER_ISOC) {
        ERROR("received iso packet for non iso endpoint %02X\n", ep);
        free(data);
        return;
    }

    if (dev->endpoint[EP2I(ep)].iso_started == 0) {
        DPRINTF("received iso packet for non started stream ep %02X\n", ep);
        free(data);
        return;
    }

    /* bufp_alloc also adds the packet to the ep queue */
    bufp_alloc(dev, data, data_len, iso_packet->status, ep, data);
}

static void usbredir_interrupt_packet(void *priv, uint64_t id,
    struct usb_redir_interrupt_packet_header *interrupt_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = interrupt_packet->endpoint;

    DPRINTF("interrupt-in status %d ep %02X len %d id %"PRIu64"\n",
            interrupt_packet->status, ep, data_len, id);

    if (dev->endpoint[EP2I(ep)].type != USB_ENDPOINT_XFER_INT) {
        ERROR("received int packet for non interrupt endpoint %02X\n", ep);
        free(data);
        return;
    }

    if (ep & USB_DIR_IN) {
        if (dev->endpoint[EP2I(ep)].interrupt_started == 0) {
            DPRINTF("received int packet while not started ep %02X\n", ep);
            free(data);
            return;
        }

        /* bufp_alloc also adds the packet to the ep queue */
        bufp_alloc(dev, data, data_len, interrupt_packet->status, ep, data);

        /* insufficient data solved with USB_RET_NAK */
        usb_wakeup(usb_ep_get(&dev->dev, USB_TOKEN_IN, ep & 0x0f), 0);
    } else {
        /*
         * We report output interrupt packets as completed directly upon
         * submission, so all we can do here if one failed is warn.
         */
        if (interrupt_packet->status) {
            WARNING("interrupt output failed status %d ep %02X id %"PRIu64"\n",
                    interrupt_packet->status, ep, id);
        }
    }
}

static void usbredir_buffered_bulk_packet(void *priv, uint64_t id,
    struct usb_redir_buffered_bulk_packet_header *buffered_bulk_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    uint8_t status, ep = buffered_bulk_packet->endpoint;
    void *free_on_destroy;
    int i, len;

    DPRINTF("buffered-bulk-in status %d ep %02X len %d id %"PRIu64"\n",
            buffered_bulk_packet->status, ep, data_len, id);

    if (dev->endpoint[EP2I(ep)].type != USB_ENDPOINT_XFER_BULK) {
        ERROR("received buffered-bulk packet for non bulk ep %02X\n", ep);
        free(data);
        return;
    }

    if (dev->endpoint[EP2I(ep)].bulk_receiving_started == 0) {
        DPRINTF("received buffered-bulk packet on not started ep %02X\n", ep);
        free(data);
        return;
    }

    /* Data must be in maxp chunks for buffered_bulk_add_*_data_to_packet */
    len = dev->endpoint[EP2I(ep)].max_packet_size;
    status = usb_redir_success;
    free_on_destroy = NULL;
    for (i = 0; i < data_len; i += len) {
        int r;
        if (len >= (data_len - i)) {
            len = data_len - i;
            status = buffered_bulk_packet->status;
            free_on_destroy = data;
        }
        /* bufp_alloc also adds the packet to the ep queue */
        r = bufp_alloc(dev, data + i, len, status, ep, free_on_destroy);
        if (r) {
            break;
        }
    }

    if (dev->endpoint[EP2I(ep)].pending_async_packet) {
        USBPacket *p = dev->endpoint[EP2I(ep)].pending_async_packet;
        dev->endpoint[EP2I(ep)].pending_async_packet = NULL;
        usbredir_buffered_bulk_in_complete(dev, p, ep);
        usb_packet_complete(&dev->dev, p);
    }
}

/*
 * Migration code
 */

static int usbredir_pre_save(void *priv)
{
    USBRedirDevice *dev = priv;

    usbredir_fill_already_in_flight(dev);

    return 0;
}

static int usbredir_post_load(void *priv, int version_id)
{
    USBRedirDevice *dev = priv;

    if (dev == NULL || dev->parser == NULL) {
        return 0;
    }

    switch (dev->device_info.speed) {
    case usb_redir_speed_low:
        dev->dev.speed = USB_SPEED_LOW;
        break;
    case usb_redir_speed_full:
        dev->dev.speed = USB_SPEED_FULL;
        break;
    case usb_redir_speed_high:
        dev->dev.speed = USB_SPEED_HIGH;
        break;
    case usb_redir_speed_super:
        dev->dev.speed = USB_SPEED_SUPER;
        break;
    default:
        dev->dev.speed = USB_SPEED_FULL;
    }
    dev->dev.speedmask = (1 << dev->dev.speed);

    usbredir_setup_usb_eps(dev);
    usbredir_check_bulk_receiving(dev);

    return 0;
}

/* For usbredirparser migration */
static int usbredir_put_parser(QEMUFile *f, void *priv, size_t unused,
                               const VMStateField *field, JSONWriter *vmdesc)
{
    USBRedirDevice *dev = priv;
    uint8_t *data;
    int len;

    if (dev->parser == NULL) {
        qemu_put_be32(f, 0);
        return 0;
    }

    usbredirparser_serialize(dev->parser, &data, &len);
    if (!data) {
        error_report("usbredirparser_serialize failed");
        exit(1);
    }

    qemu_put_be32(f, len);
    qemu_put_buffer(f, data, len);

    free(data);

    return 0;
}

static int usbredir_get_parser(QEMUFile *f, void *priv, size_t unused,
                               const VMStateField *field)
{
    USBRedirDevice *dev = priv;
    uint8_t *data;
    int len, ret;

    len = qemu_get_be32(f);
    if (len == 0) {
        return 0;
    }

    /*
     * If our chardev is not open already at this point the usbredir connection
     * has been broken (non seamless migration, or restore from disk).
     *
     * In this case create a temporary parser to receive the migration data,
     * and schedule the close_bh to report the device as disconnected to the
     * guest and to destroy the parser again.
     */
    if (dev->parser == NULL) {
        WARNING("usb-redir connection broken during migration\n");
        usbredir_create_parser(dev);
        qemu_bh_schedule(dev->chardev_close_bh);
    }

    data = g_malloc(len);
    qemu_get_buffer(f, data, len);

    ret = usbredirparser_unserialize(dev->parser, data, len);

    g_free(data);

    return ret;
}

static const VMStateInfo usbredir_parser_vmstate_info = {
    .name = "usb-redir-parser",
    .put  = usbredir_put_parser,
    .get  = usbredir_get_parser,
};


/* For buffered packets (iso/irq) queue migration */
static int usbredir_put_bufpq(QEMUFile *f, void *priv, size_t unused,
                              const VMStateField *field, JSONWriter *vmdesc)
{
    struct endp_data *endp = priv;
    USBRedirDevice *dev = endp->dev;
    struct buf_packet *bufp;
    int len, i = 0;

    qemu_put_be32(f, endp->bufpq_size);
    QTAILQ_FOREACH(bufp, &endp->bufpq, next) {
        len = bufp->len - bufp->offset;
        DPRINTF("put_bufpq %d/%d len %d status %d\n", i + 1, endp->bufpq_size,
                len, bufp->status);
        qemu_put_be32(f, len);
        qemu_put_be32(f, bufp->status);
        qemu_put_buffer(f, bufp->data + bufp->offset, len);
        i++;
    }
    assert(i == endp->bufpq_size);

    return 0;
}

static int usbredir_get_bufpq(QEMUFile *f, void *priv, size_t unused,
                              const VMStateField *field)
{
    struct endp_data *endp = priv;
    USBRedirDevice *dev = endp->dev;
    struct buf_packet *bufp;
    int i;

    endp->bufpq_size = qemu_get_be32(f);
    for (i = 0; i < endp->bufpq_size; i++) {
        bufp = g_new(struct buf_packet, 1);
        bufp->len = qemu_get_be32(f);
        bufp->status = qemu_get_be32(f);
        bufp->offset = 0;
        bufp->data = malloc(bufp->len); /* regular malloc! */
        if (!bufp->data) {
            error_report("usbredir_get_bufpq: out of memory");
            exit(1);
        }
        bufp->free_on_destroy = bufp->data;
        qemu_get_buffer(f, bufp->data, bufp->len);
        QTAILQ_INSERT_TAIL(&endp->bufpq, bufp, next);
        DPRINTF("get_bufpq %d/%d len %d status %d\n", i + 1, endp->bufpq_size,
                bufp->len, bufp->status);
    }
    return 0;
}

static const VMStateInfo usbredir_ep_bufpq_vmstate_info = {
    .name = "usb-redir-bufpq",
    .put  = usbredir_put_bufpq,
    .get  = usbredir_get_bufpq,
};


/* For endp_data migration */
static bool usbredir_bulk_receiving_needed(void *priv)
{
    struct endp_data *endp = priv;

    return endp->bulk_receiving_started;
}

static const VMStateDescription usbredir_bulk_receiving_vmstate = {
    .name = "usb-redir-ep/bulk-receiving",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = usbredir_bulk_receiving_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(bulk_receiving_started, struct endp_data),
        VMSTATE_END_OF_LIST()
    }
};

static bool usbredir_stream_needed(void *priv)
{
    struct endp_data *endp = priv;

    return endp->max_streams;
}

static const VMStateDescription usbredir_stream_vmstate = {
    .name = "usb-redir-ep/stream-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = usbredir_stream_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(max_streams, struct endp_data),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription usbredir_ep_vmstate = {
    .name = "usb-redir-ep",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(type, struct endp_data),
        VMSTATE_UINT8(interval, struct endp_data),
        VMSTATE_UINT8(interface, struct endp_data),
        VMSTATE_UINT16(max_packet_size, struct endp_data),
        VMSTATE_UINT8(iso_started, struct endp_data),
        VMSTATE_UINT8(iso_error, struct endp_data),
        VMSTATE_UINT8(interrupt_started, struct endp_data),
        VMSTATE_UINT8(interrupt_error, struct endp_data),
        VMSTATE_UINT8(bufpq_prefilled, struct endp_data),
        VMSTATE_UINT8(bufpq_dropping_packets, struct endp_data),
        {
            .name         = "bufpq",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,
            .info         = &usbredir_ep_bufpq_vmstate_info,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_INT32(bufpq_target_size, struct endp_data),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &usbredir_bulk_receiving_vmstate,
        &usbredir_stream_vmstate,
        NULL
    }
};


/* For PacketIdQueue migration */
static int usbredir_put_packet_id_q(QEMUFile *f, void *priv, size_t unused,
                                    const VMStateField *field,
                                    JSONWriter *vmdesc)
{
    struct PacketIdQueue *q = priv;
    USBRedirDevice *dev = q->dev;
    struct PacketIdQueueEntry *e;
    int remain = q->size;

    DPRINTF("put_packet_id_q %s size %d\n", q->name, q->size);
    qemu_put_be32(f, q->size);
    QTAILQ_FOREACH(e, &q->head, next) {
        qemu_put_be64(f, e->id);
        remain--;
    }
    assert(remain == 0);

    return 0;
}

static int usbredir_get_packet_id_q(QEMUFile *f, void *priv, size_t unused,
                                    const VMStateField *field)
{
    struct PacketIdQueue *q = priv;
    USBRedirDevice *dev = q->dev;
    int i, size;
    uint64_t id;

    size = qemu_get_be32(f);
    DPRINTF("get_packet_id_q %s size %d\n", q->name, size);
    for (i = 0; i < size; i++) {
        id = qemu_get_be64(f);
        packet_id_queue_add(q, id);
    }
    assert(q->size == size);
    return 0;
}

static const VMStateInfo usbredir_ep_packet_id_q_vmstate_info = {
    .name = "usb-redir-packet-id-q",
    .put  = usbredir_put_packet_id_q,
    .get  = usbredir_get_packet_id_q,
};

static const VMStateDescription usbredir_ep_packet_id_queue_vmstate = {
    .name = "usb-redir-packet-id-queue",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        {
            .name         = "queue",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,
            .info         = &usbredir_ep_packet_id_q_vmstate_info,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_END_OF_LIST()
    }
};


/* For usb_redir_device_connect_header migration */
static const VMStateDescription usbredir_device_info_vmstate = {
    .name = "usb-redir-device-info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(speed, struct usb_redir_device_connect_header),
        VMSTATE_UINT8(device_class, struct usb_redir_device_connect_header),
        VMSTATE_UINT8(device_subclass, struct usb_redir_device_connect_header),
        VMSTATE_UINT8(device_protocol, struct usb_redir_device_connect_header),
        VMSTATE_UINT16(vendor_id, struct usb_redir_device_connect_header),
        VMSTATE_UINT16(product_id, struct usb_redir_device_connect_header),
        VMSTATE_UINT16(device_version_bcd,
                       struct usb_redir_device_connect_header),
        VMSTATE_END_OF_LIST()
    }
};


/* For usb_redir_interface_info_header migration */
static const VMStateDescription usbredir_interface_info_vmstate = {
    .name = "usb-redir-interface-info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(interface_count,
                       struct usb_redir_interface_info_header),
        VMSTATE_UINT8_ARRAY(interface,
                            struct usb_redir_interface_info_header, 32),
        VMSTATE_UINT8_ARRAY(interface_class,
                            struct usb_redir_interface_info_header, 32),
        VMSTATE_UINT8_ARRAY(interface_subclass,
                            struct usb_redir_interface_info_header, 32),
        VMSTATE_UINT8_ARRAY(interface_protocol,
                            struct usb_redir_interface_info_header, 32),
        VMSTATE_END_OF_LIST()
    }
};


/* And finally the USBRedirDevice vmstate itself */
static const VMStateDescription usbredir_vmstate = {
    .name = "usb-redir",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = usbredir_pre_save,
    .post_load = usbredir_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBRedirDevice),
        VMSTATE_TIMER_PTR(attach_timer, USBRedirDevice),
        {
            .name         = "parser",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,
            .info         = &usbredir_parser_vmstate_info,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_STRUCT_ARRAY(endpoint, USBRedirDevice, MAX_ENDPOINTS, 1,
                             usbredir_ep_vmstate, struct endp_data),
        VMSTATE_STRUCT(cancelled, USBRedirDevice, 1,
                       usbredir_ep_packet_id_queue_vmstate,
                       struct PacketIdQueue),
        VMSTATE_STRUCT(already_in_flight, USBRedirDevice, 1,
                       usbredir_ep_packet_id_queue_vmstate,
                       struct PacketIdQueue),
        VMSTATE_STRUCT(device_info, USBRedirDevice, 1,
                       usbredir_device_info_vmstate,
                       struct usb_redir_device_connect_header),
        VMSTATE_STRUCT(interface_info, USBRedirDevice, 1,
                       usbredir_interface_info_vmstate,
                       struct usb_redir_interface_info_header),
        VMSTATE_END_OF_LIST()
    }
};

static Property usbredir_properties[] = {
    DEFINE_PROP_CHR("chardev", USBRedirDevice, cs),
    DEFINE_PROP_UINT8("debug", USBRedirDevice, debug, usbredirparser_warning),
    DEFINE_PROP_STRING("filter", USBRedirDevice, filter_str),
    DEFINE_PROP_BOOL("streams", USBRedirDevice, enable_streams, true),
    DEFINE_PROP_BOOL("suppress-remote-wake", USBRedirDevice,
                     suppress_remote_wake, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void usbredir_class_initfn(ObjectClass *klass, void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    uc->realize        = usbredir_realize;
    uc->product_desc   = "USB Redirection Device";
    uc->unrealize      = usbredir_unrealize;
    uc->cancel_packet  = usbredir_cancel_packet;
    uc->handle_reset   = usbredir_handle_reset;
    uc->handle_data    = usbredir_handle_data;
    uc->handle_control = usbredir_handle_control;
    uc->flush_ep_queue = usbredir_flush_ep_queue;
    uc->ep_stopped     = usbredir_ep_stopped;
    uc->alloc_streams  = usbredir_alloc_streams;
    uc->free_streams   = usbredir_free_streams;
    dc->vmsd           = &usbredir_vmstate;
    device_class_set_props(dc, usbredir_properties);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void usbredir_instance_init(Object *obj)
{
    USBDevice *udev = USB_DEVICE(obj);
    USBRedirDevice *dev = USB_REDIRECT(udev);

    device_add_bootindex_property(obj, &dev->bootindex,
                                  "bootindex", NULL,
                                  &udev->qdev);
}

static const TypeInfo usbredir_dev_info = {
    .name          = TYPE_USB_REDIR,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBRedirDevice),
    .class_init    = usbredir_class_initfn,
    .instance_init = usbredir_instance_init,
};
module_obj(TYPE_USB_REDIR);
module_kconfig(USB);

static void usbredir_register_types(void)
{
    type_register_static(&usbredir_dev_info);
}

type_init(usbredir_register_types)
