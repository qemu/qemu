/*
 * USB redirector usb-guest
 *
 * Copyright (c) 2011 Red Hat, Inc.
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

#include "qemu-common.h"
#include "qemu-timer.h"
#include "monitor.h"
#include "sysemu.h"

#include <dirent.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <usbredirparser.h>

#include "hw/usb.h"

#define MAX_ENDPOINTS 32
#define EP2I(ep_address) (((ep_address & 0x80) >> 3) | (ep_address & 0x0f))
#define I2EP(i) (((i & 0x10) << 3) | (i & 0x0f))

typedef struct AsyncURB AsyncURB;
typedef struct USBRedirDevice USBRedirDevice;

/* Struct to hold buffered packets (iso or int input packets) */
struct buf_packet {
    uint8_t *data;
    int len;
    int status;
    QTAILQ_ENTRY(buf_packet)next;
};

struct endp_data {
    uint8_t type;
    uint8_t interval;
    uint8_t interface; /* bInterfaceNumber this ep belongs to */
    uint8_t iso_started;
    uint8_t iso_error; /* For reporting iso errors to the HC */
    uint8_t interrupt_started;
    uint8_t interrupt_error;
    QTAILQ_HEAD(, buf_packet) bufpq;
};

struct USBRedirDevice {
    USBDevice dev;
    /* Properties */
    CharDriverState *cs;
    uint8_t debug;
    /* Data passed from chardev the fd_read cb to the usbredirparser read cb */
    const uint8_t *read_buf;
    int read_buf_size;
    /* For async handling of open/close */
    QEMUBH *open_close_bh;
    /* To delay the usb attach in case of quick chardev close + open */
    QEMUTimer *attach_timer;
    int64_t next_attach_time;
    struct usbredirparser *parser;
    struct endp_data endpoint[MAX_ENDPOINTS];
    uint32_t packet_id;
    QTAILQ_HEAD(, AsyncURB) asyncq;
};

struct AsyncURB {
    USBRedirDevice *dev;
    USBPacket *packet;
    uint32_t packet_id;
    int get;
    union {
        struct usb_redir_control_packet_header control_packet;
        struct usb_redir_bulk_packet_header bulk_packet;
        struct usb_redir_interrupt_packet_header interrupt_packet;
    };
    QTAILQ_ENTRY(AsyncURB)next;
};

static void usbredir_device_connect(void *priv,
    struct usb_redir_device_connect_header *device_connect);
static void usbredir_device_disconnect(void *priv);
static void usbredir_interface_info(void *priv,
    struct usb_redir_interface_info_header *interface_info);
static void usbredir_ep_info(void *priv,
    struct usb_redir_ep_info_header *ep_info);
static void usbredir_configuration_status(void *priv, uint32_t id,
    struct usb_redir_configuration_status_header *configuration_status);
static void usbredir_alt_setting_status(void *priv, uint32_t id,
    struct usb_redir_alt_setting_status_header *alt_setting_status);
static void usbredir_iso_stream_status(void *priv, uint32_t id,
    struct usb_redir_iso_stream_status_header *iso_stream_status);
static void usbredir_interrupt_receiving_status(void *priv, uint32_t id,
    struct usb_redir_interrupt_receiving_status_header
    *interrupt_receiving_status);
static void usbredir_bulk_streams_status(void *priv, uint32_t id,
    struct usb_redir_bulk_streams_status_header *bulk_streams_status);
static void usbredir_control_packet(void *priv, uint32_t id,
    struct usb_redir_control_packet_header *control_packet,
    uint8_t *data, int data_len);
static void usbredir_bulk_packet(void *priv, uint32_t id,
    struct usb_redir_bulk_packet_header *bulk_packet,
    uint8_t *data, int data_len);
static void usbredir_iso_packet(void *priv, uint32_t id,
    struct usb_redir_iso_packet_header *iso_packet,
    uint8_t *data, int data_len);
static void usbredir_interrupt_packet(void *priv, uint32_t id,
    struct usb_redir_interrupt_packet_header *interrupt_header,
    uint8_t *data, int data_len);

static int usbredir_handle_status(USBRedirDevice *dev,
                                       int status, int actual_len);

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
            error_report("usb-redir warning: " __VA_ARGS__); \
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

    error_report("%s\n", msg);
}

static void usbredir_log_data(USBRedirDevice *dev, const char *desc,
    const uint8_t *data, int len)
{
    int i, j, n;

    if (dev->debug < usbredirparser_debug_data) {
        return;
    }

    for (i = 0; i < len; i += j) {
        char buf[128];

        n = sprintf(buf, "%s", desc);
        for (j = 0; j < 8 && i + j < len; j++) {
            n += sprintf(buf + n, " %02X", data[i + j]);
        }
        error_report("%s\n", buf);
    }
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

static int usbredir_write(void *priv, uint8_t *data, int count)
{
    USBRedirDevice *dev = priv;

    if (!dev->cs->opened) {
        return 0;
    }

    return qemu_chr_fe_write(dev->cs, data, count);
}

/*
 * Async and buffered packets helpers
 */

static AsyncURB *async_alloc(USBRedirDevice *dev, USBPacket *p)
{
    AsyncURB *aurb = (AsyncURB *) g_malloc0(sizeof(AsyncURB));
    aurb->dev = dev;
    aurb->packet = p;
    aurb->packet_id = dev->packet_id;
    QTAILQ_INSERT_TAIL(&dev->asyncq, aurb, next);
    dev->packet_id++;

    return aurb;
}

static void async_free(USBRedirDevice *dev, AsyncURB *aurb)
{
    QTAILQ_REMOVE(&dev->asyncq, aurb, next);
    g_free(aurb);
}

static AsyncURB *async_find(USBRedirDevice *dev, uint32_t packet_id)
{
    AsyncURB *aurb;

    QTAILQ_FOREACH(aurb, &dev->asyncq, next) {
        if (aurb->packet_id == packet_id) {
            return aurb;
        }
    }
    ERROR("could not find async urb for packet_id %u\n", packet_id);
    return NULL;
}

static void usbredir_cancel_packet(USBDevice *udev, USBPacket *p)
{
    USBRedirDevice *dev = DO_UPCAST(USBRedirDevice, dev, udev);
    AsyncURB *aurb;

    QTAILQ_FOREACH(aurb, &dev->asyncq, next) {
        if (p != aurb->packet) {
            continue;
        }

        DPRINTF("async cancel id %u\n", aurb->packet_id);
        usbredirparser_send_cancel_data_packet(dev->parser, aurb->packet_id);
        usbredirparser_do_write(dev->parser);

        /* Mark it as dead */
        aurb->packet = NULL;
        break;
    }
}

static struct buf_packet *bufp_alloc(USBRedirDevice *dev,
    uint8_t *data, int len, int status, uint8_t ep)
{
    struct buf_packet *bufp = g_malloc(sizeof(struct buf_packet));
    bufp->data   = data;
    bufp->len    = len;
    bufp->status = status;
    QTAILQ_INSERT_TAIL(&dev->endpoint[EP2I(ep)].bufpq, bufp, next);
    return bufp;
}

static void bufp_free(USBRedirDevice *dev, struct buf_packet *bufp,
    uint8_t ep)
{
    QTAILQ_REMOVE(&dev->endpoint[EP2I(ep)].bufpq, bufp, next);
    free(bufp->data);
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
    USBRedirDevice *dev = DO_UPCAST(USBRedirDevice, dev, udev);

    DPRINTF("reset device\n");
    usbredirparser_send_reset(dev->parser);
    usbredirparser_do_write(dev->parser);
}

static int usbredir_handle_iso_data(USBRedirDevice *dev, USBPacket *p,
                                     uint8_t ep)
{
    int status, len;

    if (!dev->endpoint[EP2I(ep)].iso_started &&
            !dev->endpoint[EP2I(ep)].iso_error) {
        struct usb_redir_start_iso_stream_header start_iso = {
            .endpoint = ep,
            /* TODO maybe do something with these depending on ep interval? */
            .pkts_per_urb = 32,
            .no_urbs = 3,
        };
        /* No id, we look at the ep when receiving a status back */
        usbredirparser_send_start_iso_stream(dev->parser, 0, &start_iso);
        usbredirparser_do_write(dev->parser);
        DPRINTF("iso stream started ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].iso_started = 1;
    }

    if (ep & USB_DIR_IN) {
        struct buf_packet *isop;

        isop = QTAILQ_FIRST(&dev->endpoint[EP2I(ep)].bufpq);
        if (isop == NULL) {
            DPRINTF2("iso-token-in ep %02X, no isop\n", ep);
            /* Check iso_error for stream errors, otherwise its an underrun */
            status = dev->endpoint[EP2I(ep)].iso_error;
            dev->endpoint[EP2I(ep)].iso_error = 0;
            return usbredir_handle_status(dev, status, 0);
        }
        DPRINTF2("iso-token-in ep %02X status %d len %d\n", ep, isop->status,
                 isop->len);

        status = isop->status;
        if (status != usb_redir_success) {
            bufp_free(dev, isop, ep);
            return usbredir_handle_status(dev, status, 0);
        }

        len = isop->len;
        if (len > p->iov.size) {
            ERROR("received iso data is larger then packet ep %02X\n", ep);
            bufp_free(dev, isop, ep);
            return USB_RET_NAK;
        }
        usb_packet_copy(p, isop->data, len);
        bufp_free(dev, isop, ep);
        return len;
    } else {
        /* If the stream was not started because of a pending error don't
           send the packet to the usb-host */
        if (dev->endpoint[EP2I(ep)].iso_started) {
            struct usb_redir_iso_packet_header iso_packet = {
                .endpoint = ep,
                .length = p->iov.size
            };
            uint8_t buf[p->iov.size];
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
        return usbredir_handle_status(dev, status, p->iov.size);
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
    usbredir_free_bufpq(dev, ep);
}

static int usbredir_handle_bulk_data(USBRedirDevice *dev, USBPacket *p,
                                      uint8_t ep)
{
    AsyncURB *aurb = async_alloc(dev, p);
    struct usb_redir_bulk_packet_header bulk_packet;

    DPRINTF("bulk-out ep %02X len %zd id %u\n", ep,
            p->iov.size, aurb->packet_id);

    bulk_packet.endpoint  = ep;
    bulk_packet.length    = p->iov.size;
    bulk_packet.stream_id = 0;
    aurb->bulk_packet = bulk_packet;

    if (ep & USB_DIR_IN) {
        usbredirparser_send_bulk_packet(dev->parser, aurb->packet_id,
                                        &bulk_packet, NULL, 0);
    } else {
        uint8_t buf[p->iov.size];
        usb_packet_copy(p, buf, p->iov.size);
        usbredir_log_data(dev, "bulk data out:", buf, p->iov.size);
        usbredirparser_send_bulk_packet(dev->parser, aurb->packet_id,
                                        &bulk_packet, buf, p->iov.size);
    }
    usbredirparser_do_write(dev->parser);
    return USB_RET_ASYNC;
}

static int usbredir_handle_interrupt_data(USBRedirDevice *dev,
                                           USBPacket *p, uint8_t ep)
{
    if (ep & USB_DIR_IN) {
        /* Input interrupt endpoint, buffered packet input */
        struct buf_packet *intp;
        int status, len;

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
        }

        intp = QTAILQ_FIRST(&dev->endpoint[EP2I(ep)].bufpq);
        if (intp == NULL) {
            DPRINTF2("interrupt-token-in ep %02X, no intp\n", ep);
            /* Check interrupt_error for stream errors */
            status = dev->endpoint[EP2I(ep)].interrupt_error;
            dev->endpoint[EP2I(ep)].interrupt_error = 0;
            return usbredir_handle_status(dev, status, 0);
        }
        DPRINTF("interrupt-token-in ep %02X status %d len %d\n", ep,
                intp->status, intp->len);

        status = intp->status;
        if (status != usb_redir_success) {
            bufp_free(dev, intp, ep);
            return usbredir_handle_status(dev, status, 0);
        }

        len = intp->len;
        if (len > p->iov.size) {
            ERROR("received int data is larger then packet ep %02X\n", ep);
            bufp_free(dev, intp, ep);
            return USB_RET_NAK;
        }
        usb_packet_copy(p, intp->data, len);
        bufp_free(dev, intp, ep);
        return len;
    } else {
        /* Output interrupt endpoint, normal async operation */
        AsyncURB *aurb = async_alloc(dev, p);
        struct usb_redir_interrupt_packet_header interrupt_packet;
        uint8_t buf[p->iov.size];

        DPRINTF("interrupt-out ep %02X len %zd id %u\n", ep, p->iov.size,
                aurb->packet_id);

        interrupt_packet.endpoint  = ep;
        interrupt_packet.length    = p->iov.size;
        aurb->interrupt_packet     = interrupt_packet;

        usb_packet_copy(p, buf, p->iov.size);
        usbredir_log_data(dev, "interrupt data out:", buf, p->iov.size);
        usbredirparser_send_interrupt_packet(dev->parser, aurb->packet_id,
                                        &interrupt_packet, buf, p->iov.size);
        usbredirparser_do_write(dev->parser);
        return USB_RET_ASYNC;
    }
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
    usbredir_free_bufpq(dev, ep);
}

static int usbredir_handle_data(USBDevice *udev, USBPacket *p)
{
    USBRedirDevice *dev = DO_UPCAST(USBRedirDevice, dev, udev);
    uint8_t ep;

    ep = p->devep;
    if (p->pid == USB_TOKEN_IN) {
        ep |= USB_DIR_IN;
    }

    switch (dev->endpoint[EP2I(ep)].type) {
    case USB_ENDPOINT_XFER_CONTROL:
        ERROR("handle_data called for control transfer on ep %02X\n", ep);
        return USB_RET_NAK;
    case USB_ENDPOINT_XFER_ISOC:
        return usbredir_handle_iso_data(dev, p, ep);
    case USB_ENDPOINT_XFER_BULK:
        return usbredir_handle_bulk_data(dev, p, ep);;
    case USB_ENDPOINT_XFER_INT:
        return usbredir_handle_interrupt_data(dev, p, ep);;
    default:
        ERROR("handle_data ep %02X has unknown type %d\n", ep,
              dev->endpoint[EP2I(ep)].type);
        return USB_RET_NAK;
    }
}

static int usbredir_set_config(USBRedirDevice *dev, USBPacket *p,
                                int config)
{
    struct usb_redir_set_configuration_header set_config;
    AsyncURB *aurb = async_alloc(dev, p);
    int i;

    DPRINTF("set config %d id %u\n", config, aurb->packet_id);

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        switch (dev->endpoint[i].type) {
        case USB_ENDPOINT_XFER_ISOC:
            usbredir_stop_iso_stream(dev, I2EP(i));
            break;
        case USB_ENDPOINT_XFER_INT:
            if (i & 0x10) {
                usbredir_stop_interrupt_receiving(dev, I2EP(i));
            }
            break;
        }
        usbredir_free_bufpq(dev, I2EP(i));
    }

    set_config.configuration = config;
    usbredirparser_send_set_configuration(dev->parser, aurb->packet_id,
                                          &set_config);
    usbredirparser_do_write(dev->parser);
    return USB_RET_ASYNC;
}

static int usbredir_get_config(USBRedirDevice *dev, USBPacket *p)
{
    AsyncURB *aurb = async_alloc(dev, p);

    DPRINTF("get config id %u\n", aurb->packet_id);

    aurb->get = 1;
    usbredirparser_send_get_configuration(dev->parser, aurb->packet_id);
    usbredirparser_do_write(dev->parser);
    return USB_RET_ASYNC;
}

static int usbredir_set_interface(USBRedirDevice *dev, USBPacket *p,
                                   int interface, int alt)
{
    struct usb_redir_set_alt_setting_header set_alt;
    AsyncURB *aurb = async_alloc(dev, p);
    int i;

    DPRINTF("set interface %d alt %d id %u\n", interface, alt,
            aurb->packet_id);

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        if (dev->endpoint[i].interface == interface) {
            switch (dev->endpoint[i].type) {
            case USB_ENDPOINT_XFER_ISOC:
                usbredir_stop_iso_stream(dev, I2EP(i));
                break;
            case USB_ENDPOINT_XFER_INT:
                if (i & 0x10) {
                    usbredir_stop_interrupt_receiving(dev, I2EP(i));
                }
                break;
            }
            usbredir_free_bufpq(dev, I2EP(i));
        }
    }

    set_alt.interface = interface;
    set_alt.alt = alt;
    usbredirparser_send_set_alt_setting(dev->parser, aurb->packet_id,
                                        &set_alt);
    usbredirparser_do_write(dev->parser);
    return USB_RET_ASYNC;
}

static int usbredir_get_interface(USBRedirDevice *dev, USBPacket *p,
                                   int interface)
{
    struct usb_redir_get_alt_setting_header get_alt;
    AsyncURB *aurb = async_alloc(dev, p);

    DPRINTF("get interface %d id %u\n", interface, aurb->packet_id);

    get_alt.interface = interface;
    aurb->get = 1;
    usbredirparser_send_get_alt_setting(dev->parser, aurb->packet_id,
                                        &get_alt);
    usbredirparser_do_write(dev->parser);
    return USB_RET_ASYNC;
}

static int usbredir_handle_control(USBDevice *udev, USBPacket *p,
        int request, int value, int index, int length, uint8_t *data)
{
    USBRedirDevice *dev = DO_UPCAST(USBRedirDevice, dev, udev);
    struct usb_redir_control_packet_header control_packet;
    AsyncURB *aurb;

    /* Special cases for certain standard device requests */
    switch (request) {
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        DPRINTF("set address %d\n", value);
        dev->dev.addr = value;
        return 0;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        return usbredir_set_config(dev, p, value & 0xff);
    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        return usbredir_get_config(dev, p);
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        return usbredir_set_interface(dev, p, index, value);
    case InterfaceRequest | USB_REQ_GET_INTERFACE:
        return usbredir_get_interface(dev, p, index);
    }

    /* "Normal" ctrl requests */
    aurb = async_alloc(dev, p);

    /* Note request is (bRequestType << 8) | bRequest */
    DPRINTF("ctrl-out type 0x%x req 0x%x val 0x%x index %d len %d id %u\n",
            request >> 8, request & 0xff, value, index, length,
            aurb->packet_id);

    control_packet.request     = request & 0xFF;
    control_packet.requesttype = request >> 8;
    control_packet.endpoint    = control_packet.requesttype & USB_DIR_IN;
    control_packet.value       = value;
    control_packet.index       = index;
    control_packet.length      = length;
    aurb->control_packet       = control_packet;

    if (control_packet.requesttype & USB_DIR_IN) {
        usbredirparser_send_control_packet(dev->parser, aurb->packet_id,
                                           &control_packet, NULL, 0);
    } else {
        usbredir_log_data(dev, "ctrl data out:", data, length);
        usbredirparser_send_control_packet(dev->parser, aurb->packet_id,
                                           &control_packet, data, length);
    }
    usbredirparser_do_write(dev->parser);
    return USB_RET_ASYNC;
}

/*
 * Close events can be triggered by usbredirparser_do_write which gets called
 * from within the USBDevice data / control packet callbacks and doing a
 * usb_detach from within these callbacks is not a good idea.
 *
 * So we use a bh handler to take care of close events. We also handle
 * open events from this callback to make sure that a close directly followed
 * by an open gets handled in the right order.
 */
static void usbredir_open_close_bh(void *opaque)
{
    USBRedirDevice *dev = opaque;

    usbredir_device_disconnect(dev);

    if (dev->parser) {
        usbredirparser_destroy(dev->parser);
        dev->parser = NULL;
    }

    if (dev->cs->opened) {
        dev->parser = qemu_oom_check(usbredirparser_create());
        dev->parser->priv = dev;
        dev->parser->log_func = usbredir_log;
        dev->parser->read_func = usbredir_read;
        dev->parser->write_func = usbredir_write;
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
        dev->parser->control_packet_func = usbredir_control_packet;
        dev->parser->bulk_packet_func = usbredir_bulk_packet;
        dev->parser->iso_packet_func = usbredir_iso_packet;
        dev->parser->interrupt_packet_func = usbredir_interrupt_packet;
        dev->read_buf = NULL;
        dev->read_buf_size = 0;
        usbredirparser_init(dev->parser, VERSION, NULL, 0, 0);
        usbredirparser_do_write(dev->parser);
    }
}

static void usbredir_do_attach(void *opaque)
{
    USBRedirDevice *dev = opaque;

    usb_device_attach(&dev->dev);
}

/*
 * chardev callbacks
 */

static int usbredir_chardev_can_read(void *opaque)
{
    USBRedirDevice *dev = opaque;

    if (dev->parser) {
        /* usbredir_parser_do_read will consume *all* data we give it */
        return 1024 * 1024;
    } else {
        /* usbredir_open_close_bh hasn't handled the open event yet */
        return 0;
    }
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

static void usbredir_chardev_event(void *opaque, int event)
{
    USBRedirDevice *dev = opaque;

    switch (event) {
    case CHR_EVENT_OPENED:
    case CHR_EVENT_CLOSED:
        qemu_bh_schedule(dev->open_close_bh);
        break;
    }
}

/*
 * init + destroy
 */

static int usbredir_initfn(USBDevice *udev)
{
    USBRedirDevice *dev = DO_UPCAST(USBRedirDevice, dev, udev);
    int i;

    if (dev->cs == NULL) {
        qerror_report(QERR_MISSING_PARAMETER, "chardev");
        return -1;
    }

    dev->open_close_bh = qemu_bh_new(usbredir_open_close_bh, dev);
    dev->attach_timer = qemu_new_timer_ms(vm_clock, usbredir_do_attach, dev);

    QTAILQ_INIT(&dev->asyncq);
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        QTAILQ_INIT(&dev->endpoint[i].bufpq);
    }

    /* We'll do the attach once we receive the speed from the usb-host */
    udev->auto_attach = 0;

    /* Let the backend know we are ready */
    qemu_chr_fe_open(dev->cs);
    qemu_chr_add_handlers(dev->cs, usbredir_chardev_can_read,
                          usbredir_chardev_read, usbredir_chardev_event, dev);

    return 0;
}

static void usbredir_cleanup_device_queues(USBRedirDevice *dev)
{
    AsyncURB *aurb, *next_aurb;
    int i;

    QTAILQ_FOREACH_SAFE(aurb, &dev->asyncq, next, next_aurb) {
        async_free(dev, aurb);
    }
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        usbredir_free_bufpq(dev, I2EP(i));
    }
}

static void usbredir_handle_destroy(USBDevice *udev)
{
    USBRedirDevice *dev = DO_UPCAST(USBRedirDevice, dev, udev);

    qemu_chr_fe_close(dev->cs);
    qemu_chr_delete(dev->cs);
    /* Note must be done after qemu_chr_close, as that causes a close event */
    qemu_bh_delete(dev->open_close_bh);

    qemu_del_timer(dev->attach_timer);
    qemu_free_timer(dev->attach_timer);

    usbredir_cleanup_device_queues(dev);

    if (dev->parser) {
        usbredirparser_destroy(dev->parser);
    }
}

/*
 * usbredirparser packet complete callbacks
 */

static int usbredir_handle_status(USBRedirDevice *dev,
                                       int status, int actual_len)
{
    switch (status) {
    case usb_redir_success:
        return actual_len;
    case usb_redir_stall:
        return USB_RET_STALL;
    case usb_redir_cancelled:
        WARNING("returning cancelled packet to HC?\n");
    case usb_redir_inval:
    case usb_redir_ioerror:
    case usb_redir_timeout:
    default:
        return USB_RET_NAK;
    }
}

static void usbredir_device_connect(void *priv,
    struct usb_redir_device_connect_header *device_connect)
{
    USBRedirDevice *dev = priv;

    if (qemu_timer_pending(dev->attach_timer) || dev->dev.attached) {
        ERROR("Received device connect while already connected\n");
        return;
    }

    switch (device_connect->speed) {
    case usb_redir_speed_low:
        DPRINTF("attaching low speed device\n");
        dev->dev.speed = USB_SPEED_LOW;
        break;
    case usb_redir_speed_full:
        DPRINTF("attaching full speed device\n");
        dev->dev.speed = USB_SPEED_FULL;
        break;
    case usb_redir_speed_high:
        DPRINTF("attaching high speed device\n");
        dev->dev.speed = USB_SPEED_HIGH;
        break;
    case usb_redir_speed_super:
        DPRINTF("attaching super speed device\n");
        dev->dev.speed = USB_SPEED_SUPER;
        break;
    default:
        DPRINTF("attaching unknown speed device, assuming full speed\n");
        dev->dev.speed = USB_SPEED_FULL;
    }
    dev->dev.speedmask = (1 << dev->dev.speed);
    qemu_mod_timer(dev->attach_timer, dev->next_attach_time);
}

static void usbredir_device_disconnect(void *priv)
{
    USBRedirDevice *dev = priv;
    int i;

    /* Stop any pending attaches */
    qemu_del_timer(dev->attach_timer);

    if (dev->dev.attached) {
        usb_device_detach(&dev->dev);
        /*
         * Delay next usb device attach to give the guest a chance to see
         * see the detach / attach in case of quick close / open succession
         */
        dev->next_attach_time = qemu_get_clock_ms(vm_clock) + 200;
    }

    /* Reset state so that the next dev connected starts with a clean slate */
    usbredir_cleanup_device_queues(dev);
    memset(dev->endpoint, 0, sizeof(dev->endpoint));
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        QTAILQ_INIT(&dev->endpoint[i].bufpq);
    }
}

static void usbredir_interface_info(void *priv,
    struct usb_redir_interface_info_header *interface_info)
{
    /* The intention is to allow specifying acceptable interface classes
       for redirection on the cmdline and in the future verify this here,
       and disconnect (or never connect) the device if a not accepted
       interface class is detected */
}

static void usbredir_ep_info(void *priv,
    struct usb_redir_ep_info_header *ep_info)
{
    USBRedirDevice *dev = priv;
    int i;

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        dev->endpoint[i].type = ep_info->type[i];
        dev->endpoint[i].interval = ep_info->interval[i];
        dev->endpoint[i].interface = ep_info->interface[i];
        if (dev->endpoint[i].type != usb_redir_type_invalid) {
            DPRINTF("ep: %02X type: %d interface: %d\n", I2EP(i),
                    dev->endpoint[i].type, dev->endpoint[i].interface);
        }
    }
}

static void usbredir_configuration_status(void *priv, uint32_t id,
    struct usb_redir_configuration_status_header *config_status)
{
    USBRedirDevice *dev = priv;
    AsyncURB *aurb;
    int len = 0;

    DPRINTF("set config status %d config %d id %u\n", config_status->status,
            config_status->configuration, id);

    aurb = async_find(dev, id);
    if (!aurb) {
        return;
    }
    if (aurb->packet) {
        if (aurb->get) {
            dev->dev.data_buf[0] = config_status->configuration;
            len = 1;
        }
        aurb->packet->result =
            usbredir_handle_status(dev, config_status->status, len);
        usb_generic_async_ctrl_complete(&dev->dev, aurb->packet);
    }
    async_free(dev, aurb);
}

static void usbredir_alt_setting_status(void *priv, uint32_t id,
    struct usb_redir_alt_setting_status_header *alt_setting_status)
{
    USBRedirDevice *dev = priv;
    AsyncURB *aurb;
    int len = 0;

    DPRINTF("alt status %d intf %d alt %d id: %u\n",
            alt_setting_status->status,
            alt_setting_status->interface,
            alt_setting_status->alt, id);

    aurb = async_find(dev, id);
    if (!aurb) {
        return;
    }
    if (aurb->packet) {
        if (aurb->get) {
            dev->dev.data_buf[0] = alt_setting_status->alt;
            len = 1;
        }
        aurb->packet->result =
            usbredir_handle_status(dev, alt_setting_status->status, len);
        usb_generic_async_ctrl_complete(&dev->dev, aurb->packet);
    }
    async_free(dev, aurb);
}

static void usbredir_iso_stream_status(void *priv, uint32_t id,
    struct usb_redir_iso_stream_status_header *iso_stream_status)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = iso_stream_status->endpoint;

    DPRINTF("iso status %d ep %02X id %u\n", iso_stream_status->status,
            ep, id);

    if (!dev->dev.attached) {
        return;
    }

    dev->endpoint[EP2I(ep)].iso_error = iso_stream_status->status;
    if (iso_stream_status->status == usb_redir_stall) {
        DPRINTF("iso stream stopped by peer ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].iso_started = 0;
    }
}

static void usbredir_interrupt_receiving_status(void *priv, uint32_t id,
    struct usb_redir_interrupt_receiving_status_header
    *interrupt_receiving_status)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = interrupt_receiving_status->endpoint;

    DPRINTF("interrupt recv status %d ep %02X id %u\n",
            interrupt_receiving_status->status, ep, id);

    if (!dev->dev.attached) {
        return;
    }

    dev->endpoint[EP2I(ep)].interrupt_error =
        interrupt_receiving_status->status;
    if (interrupt_receiving_status->status == usb_redir_stall) {
        DPRINTF("interrupt receiving stopped by peer ep %02X\n", ep);
        dev->endpoint[EP2I(ep)].interrupt_started = 0;
    }
}

static void usbredir_bulk_streams_status(void *priv, uint32_t id,
    struct usb_redir_bulk_streams_status_header *bulk_streams_status)
{
}

static void usbredir_control_packet(void *priv, uint32_t id,
    struct usb_redir_control_packet_header *control_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    int len = control_packet->length;
    AsyncURB *aurb;

    DPRINTF("ctrl-in status %d len %d id %u\n", control_packet->status,
            len, id);

    aurb = async_find(dev, id);
    if (!aurb) {
        free(data);
        return;
    }

    aurb->control_packet.status = control_packet->status;
    aurb->control_packet.length = control_packet->length;
    if (memcmp(&aurb->control_packet, control_packet,
               sizeof(*control_packet))) {
        ERROR("return control packet mismatch, please report this!\n");
        len = USB_RET_NAK;
    }

    if (aurb->packet) {
        len = usbredir_handle_status(dev, control_packet->status, len);
        if (len > 0) {
            usbredir_log_data(dev, "ctrl data in:", data, data_len);
            if (data_len <= sizeof(dev->dev.data_buf)) {
                memcpy(dev->dev.data_buf, data, data_len);
            } else {
                ERROR("ctrl buffer too small (%d > %zu)\n",
                      data_len, sizeof(dev->dev.data_buf));
                len = USB_RET_STALL;
            }
        }
        aurb->packet->result = len;
        usb_generic_async_ctrl_complete(&dev->dev, aurb->packet);
    }
    async_free(dev, aurb);
    free(data);
}

static void usbredir_bulk_packet(void *priv, uint32_t id,
    struct usb_redir_bulk_packet_header *bulk_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = bulk_packet->endpoint;
    int len = bulk_packet->length;
    AsyncURB *aurb;

    DPRINTF("bulk-in status %d ep %02X len %d id %u\n", bulk_packet->status,
            ep, len, id);

    aurb = async_find(dev, id);
    if (!aurb) {
        free(data);
        return;
    }

    if (aurb->bulk_packet.endpoint != bulk_packet->endpoint ||
            aurb->bulk_packet.stream_id != bulk_packet->stream_id) {
        ERROR("return bulk packet mismatch, please report this!\n");
        len = USB_RET_NAK;
    }

    if (aurb->packet) {
        len = usbredir_handle_status(dev, bulk_packet->status, len);
        if (len > 0) {
            usbredir_log_data(dev, "bulk data in:", data, data_len);
            if (data_len <= aurb->packet->iov.size) {
                usb_packet_copy(aurb->packet, data, data_len);
            } else {
                ERROR("bulk buffer too small (%d > %zd)\n", data_len,
                      aurb->packet->iov.size);
                len = USB_RET_STALL;
            }
        }
        aurb->packet->result = len;
        usb_packet_complete(&dev->dev, aurb->packet);
    }
    async_free(dev, aurb);
    free(data);
}

static void usbredir_iso_packet(void *priv, uint32_t id,
    struct usb_redir_iso_packet_header *iso_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = iso_packet->endpoint;

    DPRINTF2("iso-in status %d ep %02X len %d id %u\n", iso_packet->status, ep,
             data_len, id);

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
    bufp_alloc(dev, data, data_len, iso_packet->status, ep);
}

static void usbredir_interrupt_packet(void *priv, uint32_t id,
    struct usb_redir_interrupt_packet_header *interrupt_packet,
    uint8_t *data, int data_len)
{
    USBRedirDevice *dev = priv;
    uint8_t ep = interrupt_packet->endpoint;

    DPRINTF("interrupt-in status %d ep %02X len %d id %u\n",
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
        bufp_alloc(dev, data, data_len, interrupt_packet->status, ep);
    } else {
        int len = interrupt_packet->length;

        AsyncURB *aurb = async_find(dev, id);
        if (!aurb) {
            return;
        }

        if (aurb->interrupt_packet.endpoint != interrupt_packet->endpoint) {
            ERROR("return int packet mismatch, please report this!\n");
            len = USB_RET_NAK;
        }

        if (aurb->packet) {
            aurb->packet->result = usbredir_handle_status(dev,
                                               interrupt_packet->status, len);
            usb_packet_complete(&dev->dev, aurb->packet);
        }
        async_free(dev, aurb);
    }
}

static struct USBDeviceInfo usbredir_dev_info = {
    .product_desc   = "USB Redirection Device",
    .qdev.name      = "usb-redir",
    .qdev.size      = sizeof(USBRedirDevice),
    .init           = usbredir_initfn,
    .handle_destroy = usbredir_handle_destroy,
    .handle_packet  = usb_generic_handle_packet,
    .cancel_packet  = usbredir_cancel_packet,
    .handle_reset   = usbredir_handle_reset,
    .handle_data    = usbredir_handle_data,
    .handle_control = usbredir_handle_control,
    .qdev.props     = (Property[]) {
        DEFINE_PROP_CHR("chardev", USBRedirDevice, cs),
        DEFINE_PROP_UINT8("debug", USBRedirDevice, debug, 0),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void usbredir_register_devices(void)
{
    usb_qdev_register(&usbredir_dev_info);
}
device_init(usbredir_register_devices);
