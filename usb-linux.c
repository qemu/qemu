/*
 * Linux host USB redirector
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Copyright (c) 2008 Max Krasnyansky
 *      Support for host device auto connect & disconnect
 *      Major rewrite to support fully async operation
 *
 * Copyright 2008 TJ <linux@tjworld.net>
 *      Added flexible support for /dev/bus/usb /sys/bus/usb/devices in addition
 *      to the legacy /proc/bus/usb USB device discovery and handling
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

#include <linux/usbdevice_fs.h>
#include <linux/version.h>
#include "hw/usb.h"

/* We redefine it to avoid version problems */
struct usb_ctrltransfer {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint32_t timeout;
    void *data;
};

struct usb_ctrlrequest {
    uint8_t bRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

typedef int USBScanFunc(void *opaque, int bus_num, int addr, int devpath,
                        int class_id, int vendor_id, int product_id,
                        const char *product_name, int speed);

//#define DEBUG

#ifdef DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

#define USBDBG_DEVOPENED "husb: opened %s/devices\n"

#define USBPROCBUS_PATH "/proc/bus/usb"
#define PRODUCT_NAME_SZ 32
#define MAX_ENDPOINTS 16
#define USBDEVBUS_PATH "/dev/bus/usb"
#define USBSYSBUS_PATH "/sys/bus/usb"

static char *usb_host_device_path;

#define USB_FS_NONE 0
#define USB_FS_PROC 1
#define USB_FS_DEV 2
#define USB_FS_SYS 3

static int usb_fs_type;

/* endpoint association data */
struct endp_data {
    uint8_t type;
    uint8_t halted;
};

enum {
    CTRL_STATE_IDLE = 0,
    CTRL_STATE_SETUP,
    CTRL_STATE_DATA,
    CTRL_STATE_ACK
};

/*
 * Control transfer state.
 * Note that 'buffer' _must_ follow 'req' field because
 * we need contigious buffer when we submit control URB.
 */
struct ctrl_struct {
    uint16_t len;
    uint16_t offset;
    uint8_t  state;
    struct   usb_ctrlrequest req;
    uint8_t  buffer[8192];
};

struct USBAutoFilter {
    uint32_t bus_num;
    uint32_t addr;
    uint32_t vendor_id;
    uint32_t product_id;
};

typedef struct USBHostDevice {
    USBDevice dev;
    int       fd;

    uint8_t   descr[1024];
    int       descr_len;
    int       configuration;
    int       ninterfaces;
    int       closing;
    Notifier  exit;

    struct ctrl_struct ctrl;
    struct endp_data endp_table[MAX_ENDPOINTS];

    /* Host side address */
    int bus_num;
    int addr;
    int devpath;
    struct USBAutoFilter match;

    QTAILQ_ENTRY(USBHostDevice) next;
} USBHostDevice;

static QTAILQ_HEAD(, USBHostDevice) hostdevs = QTAILQ_HEAD_INITIALIZER(hostdevs);

static int usb_host_close(USBHostDevice *dev);
static int parse_filter(const char *spec, struct USBAutoFilter *f);
static void usb_host_auto_check(void *unused);
static int usb_host_read_file(char *line, size_t line_size,
                            const char *device_file, const char *device_name);

static int is_isoc(USBHostDevice *s, int ep)
{
    return s->endp_table[ep - 1].type == USBDEVFS_URB_TYPE_ISO;
}

static int is_halted(USBHostDevice *s, int ep)
{
    return s->endp_table[ep - 1].halted;
}

static void clear_halt(USBHostDevice *s, int ep)
{
    s->endp_table[ep - 1].halted = 0;
}

static void set_halt(USBHostDevice *s, int ep)
{
    s->endp_table[ep - 1].halted = 1;
}

/*
 * Async URB state.
 * We always allocate one isoc descriptor even for bulk transfers
 * to simplify allocation and casts.
 */
typedef struct AsyncURB
{
    struct usbdevfs_urb urb;
    struct usbdevfs_iso_packet_desc isocpd;

    USBPacket     *packet;
    USBHostDevice *hdev;
} AsyncURB;

static AsyncURB *async_alloc(void)
{
    return (AsyncURB *) qemu_mallocz(sizeof(AsyncURB));
}

static void async_free(AsyncURB *aurb)
{
    qemu_free(aurb);
}

static void async_complete_ctrl(USBHostDevice *s, USBPacket *p)
{
    switch(s->ctrl.state) {
    case CTRL_STATE_SETUP:
        if (p->len < s->ctrl.len)
            s->ctrl.len = p->len;
        s->ctrl.state = CTRL_STATE_DATA;
        p->len = 8;
        break;

    case CTRL_STATE_ACK:
        s->ctrl.state = CTRL_STATE_IDLE;
        p->len = 0;
        break;

    default:
        break;
    }
}

static void async_complete(void *opaque)
{
    USBHostDevice *s = opaque;
    AsyncURB *aurb;

    while (1) {
        USBPacket *p;

        int r = ioctl(s->fd, USBDEVFS_REAPURBNDELAY, &aurb);
        if (r < 0) {
            if (errno == EAGAIN) {
                return;
            }
            if (errno == ENODEV && !s->closing) {
                printf("husb: device %d.%d disconnected\n",
                       s->bus_num, s->addr);
                usb_host_close(s);
                usb_host_auto_check(NULL);
                return;
            }

            DPRINTF("husb: async. reap urb failed errno %d\n", errno);
            return;
        }

        p = aurb->packet;

        DPRINTF("husb: async completed. aurb %p status %d alen %d\n",
                aurb, aurb->urb.status, aurb->urb.actual_length);

        if (p) {
            switch (aurb->urb.status) {
            case 0:
                p->len = aurb->urb.actual_length;
                if (aurb->urb.type == USBDEVFS_URB_TYPE_CONTROL) {
                    async_complete_ctrl(s, p);
                }
                break;

            case -EPIPE:
                set_halt(s, p->devep);
                p->len = USB_RET_STALL;
                break;

            default:
                p->len = USB_RET_NAK;
                break;
            }

            usb_packet_complete(p);
        }

        async_free(aurb);
    }
}

static void async_cancel(USBPacket *unused, void *opaque)
{
    AsyncURB *aurb = opaque;
    USBHostDevice *s = aurb->hdev;

    DPRINTF("husb: async cancel. aurb %p\n", aurb);

    /* Mark it as dead (see async_complete above) */
    aurb->packet = NULL;

    int r = ioctl(s->fd, USBDEVFS_DISCARDURB, aurb);
    if (r < 0) {
        DPRINTF("husb: async. discard urb failed errno %d\n", errno);
    }
}

static int usb_host_claim_interfaces(USBHostDevice *dev, int configuration)
{
    int dev_descr_len, config_descr_len;
    int interface, nb_interfaces;
    int ret, i;

    if (configuration == 0) /* address state - ignore */
        return 1;

    DPRINTF("husb: claiming interfaces. config %d\n", configuration);

    i = 0;
    dev_descr_len = dev->descr[0];
    if (dev_descr_len > dev->descr_len) {
        goto fail;
    }

    i += dev_descr_len;
    while (i < dev->descr_len) {
        DPRINTF("husb: i is %d, descr_len is %d, dl %d, dt %d\n",
                i, dev->descr_len,
               dev->descr[i], dev->descr[i+1]);

        if (dev->descr[i+1] != USB_DT_CONFIG) {
            i += dev->descr[i];
            continue;
        }
        config_descr_len = dev->descr[i];

        printf("husb: config #%d need %d\n", dev->descr[i + 5], configuration);

        if (configuration < 0 || configuration == dev->descr[i + 5]) {
            configuration = dev->descr[i + 5];
            break;
        }

        i += config_descr_len;
    }

    if (i >= dev->descr_len) {
        fprintf(stderr,
                "husb: update iface failed. no matching configuration\n");
        goto fail;
    }
    nb_interfaces = dev->descr[i + 4];

#ifdef USBDEVFS_DISCONNECT
    /* earlier Linux 2.4 do not support that */
    {
        struct usbdevfs_ioctl ctrl;
        for (interface = 0; interface < nb_interfaces; interface++) {
            ctrl.ioctl_code = USBDEVFS_DISCONNECT;
            ctrl.ifno = interface;
            ctrl.data = 0;
            ret = ioctl(dev->fd, USBDEVFS_IOCTL, &ctrl);
            if (ret < 0 && errno != ENODATA) {
                perror("USBDEVFS_DISCONNECT");
                goto fail;
            }
        }
    }
#endif

    /* XXX: only grab if all interfaces are free */
    for (interface = 0; interface < nb_interfaces; interface++) {
        ret = ioctl(dev->fd, USBDEVFS_CLAIMINTERFACE, &interface);
        if (ret < 0) {
            if (errno == EBUSY) {
                printf("husb: update iface. device already grabbed\n");
            } else {
                perror("husb: failed to claim interface");
            }
        fail:
            return 0;
        }
    }

    printf("husb: %d interfaces claimed for configuration %d\n",
           nb_interfaces, configuration);

    dev->ninterfaces   = nb_interfaces;
    dev->configuration = configuration;
    return 1;
}

static int usb_host_release_interfaces(USBHostDevice *s)
{
    int ret, i;

    DPRINTF("husb: releasing interfaces\n");

    for (i = 0; i < s->ninterfaces; i++) {
        ret = ioctl(s->fd, USBDEVFS_RELEASEINTERFACE, &i);
        if (ret < 0) {
            perror("husb: failed to release interface");
            return 0;
        }
    }

    return 1;
}

static void usb_host_handle_reset(USBDevice *dev)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);

    DPRINTF("husb: reset device %u.%u\n", s->bus_num, s->addr);

    ioctl(s->fd, USBDEVFS_RESET);

    usb_host_claim_interfaces(s, s->configuration);
}

static void usb_host_handle_destroy(USBDevice *dev)
{
    USBHostDevice *s = (USBHostDevice *)dev;

    usb_host_close(s);
    QTAILQ_REMOVE(&hostdevs, s, next);
    qemu_remove_exit_notifier(&s->exit);
}

static int usb_linux_update_endp_table(USBHostDevice *s);

static int usb_host_handle_data(USBHostDevice *s, USBPacket *p)
{
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret;

    aurb = async_alloc();
    aurb->hdev   = s;
    aurb->packet = p;

    urb = &aurb->urb;

    if (p->pid == USB_TOKEN_IN) {
        urb->endpoint = p->devep | 0x80;
    } else {
        urb->endpoint = p->devep;
    }

    if (is_halted(s, p->devep)) {
        ret = ioctl(s->fd, USBDEVFS_CLEAR_HALT, &urb->endpoint);
        if (ret < 0) {
            DPRINTF("husb: failed to clear halt. ep 0x%x errno %d\n",
                   urb->endpoint, errno);
            return USB_RET_NAK;
        }
        clear_halt(s, p->devep);
    }

    urb->buffer        = p->data;
    urb->buffer_length = p->len;

    if (is_isoc(s, p->devep)) {
        /* Setup ISOC transfer */
        urb->type     = USBDEVFS_URB_TYPE_ISO;
        urb->flags    = USBDEVFS_URB_ISO_ASAP;
        urb->number_of_packets = 1;
        urb->iso_frame_desc[0].length = p->len;
    } else {
        /* Setup bulk transfer */
        urb->type     = USBDEVFS_URB_TYPE_BULK;
    }

    urb->usercontext = s;

    ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);

    DPRINTF("husb: data submit. ep 0x%x len %u aurb %p\n",
            urb->endpoint, p->len, aurb);

    if (ret < 0) {
        DPRINTF("husb: submit failed. errno %d\n", errno);
        async_free(aurb);

        switch(errno) {
        case ETIMEDOUT:
            return USB_RET_NAK;
        case EPIPE:
        default:
            return USB_RET_STALL;
        }
    }

    usb_defer_packet(p, async_cancel, aurb);
    return USB_RET_ASYNC;
}

static int ctrl_error(void)
{
    if (errno == ETIMEDOUT) {
        return USB_RET_NAK;
    } else {
        return USB_RET_STALL;
    }
}

static int usb_host_set_address(USBHostDevice *s, int addr)
{
    DPRINTF("husb: ctrl set addr %u\n", addr);
    s->dev.addr = addr;
    return 0;
}

static int usb_host_set_config(USBHostDevice *s, int config)
{
    usb_host_release_interfaces(s);

    int ret = ioctl(s->fd, USBDEVFS_SETCONFIGURATION, &config);

    DPRINTF("husb: ctrl set config %d ret %d errno %d\n", config, ret, errno);

    if (ret < 0) {
        return ctrl_error();
    }
    usb_host_claim_interfaces(s, config);
    return 0;
}

static int usb_host_set_interface(USBHostDevice *s, int iface, int alt)
{
    struct usbdevfs_setinterface si;
    int ret;

    si.interface  = iface;
    si.altsetting = alt;
    ret = ioctl(s->fd, USBDEVFS_SETINTERFACE, &si);

    DPRINTF("husb: ctrl set iface %d altset %d ret %d errno %d\n",
            iface, alt, ret, errno);

    if (ret < 0) {
        return ctrl_error();
    }
    usb_linux_update_endp_table(s);
    return 0;
}

static int usb_host_handle_control(USBHostDevice *s, USBPacket *p)
{
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret, value, index;
    int buffer_len;

    /*
     * Process certain standard device requests.
     * These are infrequent and are processed synchronously.
     */
    value = le16_to_cpu(s->ctrl.req.wValue);
    index = le16_to_cpu(s->ctrl.req.wIndex);

    DPRINTF("husb: ctrl type 0x%x req 0x%x val 0x%x index %u len %u\n",
            s->ctrl.req.bRequestType, s->ctrl.req.bRequest, value, index,
            s->ctrl.len);

    if (s->ctrl.req.bRequestType == 0) {
        switch (s->ctrl.req.bRequest) {
        case USB_REQ_SET_ADDRESS:
            return usb_host_set_address(s, value);

        case USB_REQ_SET_CONFIGURATION:
            return usb_host_set_config(s, value & 0xff);
        }
    }

    if (s->ctrl.req.bRequestType == 1 &&
                  s->ctrl.req.bRequest == USB_REQ_SET_INTERFACE) {
        return usb_host_set_interface(s, index, value);
    }

    /* The rest are asynchronous */

    buffer_len = 8 + s->ctrl.len;
    if (buffer_len > sizeof(s->ctrl.buffer)) {
        fprintf(stderr, "husb: ctrl buffer too small (%u > %zu)\n",
                buffer_len, sizeof(s->ctrl.buffer));
        return USB_RET_STALL;
    }

    aurb = async_alloc();
    aurb->hdev   = s;
    aurb->packet = p;

    /*
     * Setup ctrl transfer.
     *
     * s->ctrl is layed out such that data buffer immediately follows
     * 'req' struct which is exactly what usbdevfs expects.
     */
    urb = &aurb->urb;

    urb->type     = USBDEVFS_URB_TYPE_CONTROL;
    urb->endpoint = p->devep;

    urb->buffer        = &s->ctrl.req;
    urb->buffer_length = buffer_len;

    urb->usercontext = s;

    ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);

    DPRINTF("husb: submit ctrl. len %u aurb %p\n", urb->buffer_length, aurb);

    if (ret < 0) {
        DPRINTF("husb: submit failed. errno %d\n", errno);
        async_free(aurb);

        switch(errno) {
        case ETIMEDOUT:
            return USB_RET_NAK;
        case EPIPE:
        default:
            return USB_RET_STALL;
        }
    }

    usb_defer_packet(p, async_cancel, aurb);
    return USB_RET_ASYNC;
}

static int do_token_setup(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *) dev;
    int ret = 0;

    if (p->len != 8) {
        return USB_RET_STALL;
    }

    memcpy(&s->ctrl.req, p->data, 8);
    s->ctrl.len    = le16_to_cpu(s->ctrl.req.wLength);
    s->ctrl.offset = 0;
    s->ctrl.state  = CTRL_STATE_SETUP;

    if (s->ctrl.req.bRequestType & USB_DIR_IN) {
        ret = usb_host_handle_control(s, p);
        if (ret < 0) {
            return ret;
        }

        if (ret < s->ctrl.len) {
            s->ctrl.len = ret;
        }
        s->ctrl.state = CTRL_STATE_DATA;
    } else {
        if (s->ctrl.len == 0) {
            s->ctrl.state = CTRL_STATE_ACK;
        } else {
            s->ctrl.state = CTRL_STATE_DATA;
        }
    }

    return ret;
}

static int do_token_in(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *) dev;
    int ret = 0;

    if (p->devep != 0) {
        return usb_host_handle_data(s, p);
    }

    switch(s->ctrl.state) {
    case CTRL_STATE_ACK:
        if (!(s->ctrl.req.bRequestType & USB_DIR_IN)) {
            ret = usb_host_handle_control(s, p);
            if (ret == USB_RET_ASYNC) {
                return USB_RET_ASYNC;
            }
            s->ctrl.state = CTRL_STATE_IDLE;
            return ret > 0 ? 0 : ret;
        }

        return 0;

    case CTRL_STATE_DATA:
        if (s->ctrl.req.bRequestType & USB_DIR_IN) {
            int len = s->ctrl.len - s->ctrl.offset;
            if (len > p->len) {
                len = p->len;
            }
            memcpy(p->data, s->ctrl.buffer + s->ctrl.offset, len);
            s->ctrl.offset += len;
            if (s->ctrl.offset >= s->ctrl.len) {
                s->ctrl.state = CTRL_STATE_ACK;
            }
            return len;
        }

        s->ctrl.state = CTRL_STATE_IDLE;
        return USB_RET_STALL;

    default:
        return USB_RET_STALL;
    }
}

static int do_token_out(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *) dev;

    if (p->devep != 0) {
        return usb_host_handle_data(s, p);
    }

    switch(s->ctrl.state) {
    case CTRL_STATE_ACK:
        if (s->ctrl.req.bRequestType & USB_DIR_IN) {
            s->ctrl.state = CTRL_STATE_IDLE;
            /* transfer OK */
        } else {
            /* ignore additional output */
        }
        return 0;

    case CTRL_STATE_DATA:
        if (!(s->ctrl.req.bRequestType & USB_DIR_IN)) {
            int len = s->ctrl.len - s->ctrl.offset;
            if (len > p->len) {
                len = p->len;
            }
            memcpy(s->ctrl.buffer + s->ctrl.offset, p->data, len);
            s->ctrl.offset += len;
            if (s->ctrl.offset >= s->ctrl.len) {
                s->ctrl.state = CTRL_STATE_ACK;
            }
            return len;
        }

        s->ctrl.state = CTRL_STATE_IDLE;
        return USB_RET_STALL;

    default:
        return USB_RET_STALL;
    }
}

/*
 * Packet handler.
 * Called by the HC (host controller).
 *
 * Returns length of the transaction or one of the USB_RET_XXX codes.
 */
static int usb_host_handle_packet(USBDevice *s, USBPacket *p)
{
    switch(p->pid) {
    case USB_MSG_ATTACH:
        s->state = USB_STATE_ATTACHED;
        return 0;

    case USB_MSG_DETACH:
        s->state = USB_STATE_NOTATTACHED;
        return 0;

    case USB_MSG_RESET:
        s->remote_wakeup = 0;
        s->addr = 0;
        s->state = USB_STATE_DEFAULT;
        s->info->handle_reset(s);
        return 0;
    }

    /* Rest of the PIDs must match our address */
    if (s->state < USB_STATE_DEFAULT || p->devaddr != s->addr) {
        return USB_RET_NODEV;
    }

    switch (p->pid) {
    case USB_TOKEN_SETUP:
        return do_token_setup(s, p);

    case USB_TOKEN_IN:
        return do_token_in(s, p);

    case USB_TOKEN_OUT:
        return do_token_out(s, p);

    default:
        return USB_RET_STALL;
    }
}

static int usb_linux_get_configuration(USBHostDevice *s)
{
    uint8_t configuration;
    struct usb_ctrltransfer ct;
    int ret;

    if (usb_fs_type == USB_FS_SYS) {
        char device_name[32], line[1024];
        int configuration;

        sprintf(device_name, "%d-%d", s->bus_num, s->devpath);

        if (!usb_host_read_file(line, sizeof(line), "bConfigurationValue",
                                device_name)) {
            goto usbdevfs;
        }
        if (sscanf(line, "%d", &configuration) != 1) {
            goto usbdevfs;
        }
        return configuration;
    }

usbdevfs:
    ct.bRequestType = USB_DIR_IN;
    ct.bRequest = USB_REQ_GET_CONFIGURATION;
    ct.wValue = 0;
    ct.wIndex = 0;
    ct.wLength = 1;
    ct.data = &configuration;
    ct.timeout = 50;

    ret = ioctl(s->fd, USBDEVFS_CONTROL, &ct);
    if (ret < 0) {
        perror("usb_linux_get_configuration");
        return -1;
    }

    /* in address state */
    if (configuration == 0) {
        return -1;
    }

    return configuration;
}

/* returns 1 on problem encountered or 0 for success */
static int usb_linux_update_endp_table(USBHostDevice *s)
{
    uint8_t *descriptors;
    uint8_t devep, type, configuration, alt_interface;
    struct usb_ctrltransfer ct;
    int interface, ret, length, i;

    i = usb_linux_get_configuration(s);
    if (i < 0)
        return 1;
    configuration = i;

    /* get the desired configuration, interface, and endpoint descriptors
     * from device description */
    descriptors = &s->descr[18];
    length = s->descr_len - 18;
    i = 0;

    if (descriptors[i + 1] != USB_DT_CONFIG ||
        descriptors[i + 5] != configuration) {
        DPRINTF("invalid descriptor data - configuration\n");
        return 1;
    }
    i += descriptors[i];

    while (i < length) {
        if (descriptors[i + 1] != USB_DT_INTERFACE ||
            (descriptors[i + 1] == USB_DT_INTERFACE &&
             descriptors[i + 4] == 0)) {
            i += descriptors[i];
            continue;
        }

        interface = descriptors[i + 2];

        ct.bRequestType = USB_DIR_IN | USB_RECIP_INTERFACE;
        ct.bRequest = USB_REQ_GET_INTERFACE;
        ct.wValue = 0;
        ct.wIndex = interface;
        ct.wLength = 1;
        ct.data = &alt_interface;
        ct.timeout = 50;

        ret = ioctl(s->fd, USBDEVFS_CONTROL, &ct);
        if (ret < 0) {
            alt_interface = interface;
        }

        /* the current interface descriptor is the active interface
         * and has endpoints */
        if (descriptors[i + 3] != alt_interface) {
            i += descriptors[i];
            continue;
        }

        /* advance to the endpoints */
        while (i < length && descriptors[i +1] != USB_DT_ENDPOINT) {
            i += descriptors[i];
        }

        if (i >= length)
            break;

        while (i < length) {
            if (descriptors[i + 1] != USB_DT_ENDPOINT) {
                break;
            }

            devep = descriptors[i + 2];
            switch (descriptors[i + 3] & 0x3) {
            case 0x00:
                type = USBDEVFS_URB_TYPE_CONTROL;
                break;
            case 0x01:
                type = USBDEVFS_URB_TYPE_ISO;
                break;
            case 0x02:
                type = USBDEVFS_URB_TYPE_BULK;
                break;
            case 0x03:
                type = USBDEVFS_URB_TYPE_INTERRUPT;
                break;
            default:
                DPRINTF("usb_host: malformed endpoint type\n");
                type = USBDEVFS_URB_TYPE_BULK;
            }
            s->endp_table[(devep & 0xf) - 1].type = type;
            s->endp_table[(devep & 0xf) - 1].halted = 0;

            i += descriptors[i];
        }
    }
    return 0;
}

static int usb_host_open(USBHostDevice *dev, int bus_num,
                         int addr, int devpath, const char *prod_name)
{
    int fd = -1, ret;
    struct usbdevfs_connectinfo ci;
    char buf[1024];

    if (dev->fd != -1) {
        goto fail;
    }
    printf("husb: open device %d.%d\n", bus_num, addr);

    if (!usb_host_device_path) {
        perror("husb: USB Host Device Path not set");
        goto fail;
    }
    snprintf(buf, sizeof(buf), "%s/%03d/%03d", usb_host_device_path,
             bus_num, addr);
    fd = open(buf, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror(buf);
        goto fail;
    }
    DPRINTF("husb: opened %s\n", buf);

    dev->bus_num = bus_num;
    dev->addr = addr;
    dev->devpath = devpath;
    dev->fd = fd;

    /* read the device description */
    dev->descr_len = read(fd, dev->descr, sizeof(dev->descr));
    if (dev->descr_len <= 0) {
        perror("husb: reading device data failed");
        goto fail;
    }

#ifdef DEBUG
    {
        int x;
        printf("=== begin dumping device descriptor data ===\n");
        for (x = 0; x < dev->descr_len; x++) {
            printf("%02x ", dev->descr[x]);
        }
        printf("\n=== end dumping device descriptor data ===\n");
    }
#endif


    /*
     * Initial configuration is -1 which makes us claim first
     * available config. We used to start with 1, which does not
     * always work. I've seen devices where first config starts
     * with 2.
     */
    if (!usb_host_claim_interfaces(dev, -1)) {
        goto fail;
    }

    ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
    if (ret < 0) {
        perror("usb_host_device_open: USBDEVFS_CONNECTINFO");
        goto fail;
    }

    printf("husb: grabbed usb device %d.%d\n", bus_num, addr);

    ret = usb_linux_update_endp_table(dev);
    if (ret) {
        goto fail;
    }

    if (ci.slow) {
        dev->dev.speed = USB_SPEED_LOW;
    } else {
        dev->dev.speed = USB_SPEED_HIGH;
    }

    if (!prod_name || prod_name[0] == '\0') {
        snprintf(dev->dev.product_desc, sizeof(dev->dev.product_desc),
                 "host:%d.%d", bus_num, addr);
    } else {
        pstrcpy(dev->dev.product_desc, sizeof(dev->dev.product_desc),
                prod_name);
    }

    /* USB devio uses 'write' flag to check for async completions */
    qemu_set_fd_handler(dev->fd, NULL, async_complete, dev);

    usb_device_attach(&dev->dev);
    return 0;

fail:
    dev->fd = -1;
    if (fd != -1) {
        close(fd);
    }
    return -1;
}

static int usb_host_close(USBHostDevice *dev)
{
    if (dev->fd == -1) {
        return -1;
    }

    qemu_set_fd_handler(dev->fd, NULL, NULL, NULL);
    dev->closing = 1;
    async_complete(dev);
    dev->closing = 0;
    usb_device_detach(&dev->dev);
    ioctl(dev->fd, USBDEVFS_RESET);
    close(dev->fd);
    dev->fd = -1;
    return 0;
}

static void usb_host_exit_notifier(struct Notifier* n)
{
    USBHostDevice *s = container_of(n, USBHostDevice, exit);

    if (s->fd != -1) {
        ioctl(s->fd, USBDEVFS_RESET);
    }
}

static int usb_host_initfn(USBDevice *dev)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);

    dev->auto_attach = 0;
    s->fd = -1;
    QTAILQ_INSERT_TAIL(&hostdevs, s, next);
    s->exit.notify = usb_host_exit_notifier;
    qemu_add_exit_notifier(&s->exit);
    usb_host_auto_check(NULL);
    return 0;
}

static struct USBDeviceInfo usb_host_dev_info = {
    .product_desc   = "USB Host Device",
    .qdev.name      = "usb-host",
    .qdev.size      = sizeof(USBHostDevice),
    .init           = usb_host_initfn,
    .handle_packet  = usb_host_handle_packet,
    .handle_reset   = usb_host_handle_reset,
    .handle_destroy = usb_host_handle_destroy,
    .usbdevice_name = "host",
    .usbdevice_init = usb_host_device_open,
    .qdev.props     = (Property[]) {
        DEFINE_PROP_UINT32("hostbus",  USBHostDevice, match.bus_num,    0),
        DEFINE_PROP_UINT32("hostaddr", USBHostDevice, match.addr,       0),
        DEFINE_PROP_HEX32("vendorid",  USBHostDevice, match.vendor_id,  0),
        DEFINE_PROP_HEX32("productid", USBHostDevice, match.product_id, 0),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void usb_host_register_devices(void)
{
    usb_qdev_register(&usb_host_dev_info);
}
device_init(usb_host_register_devices)

USBDevice *usb_host_device_open(const char *devname)
{
    struct USBAutoFilter filter;
    USBDevice *dev;
    char *p;

    dev = usb_create(NULL /* FIXME */, "usb-host");

    if (strstr(devname, "auto:")) {
        if (parse_filter(devname, &filter) < 0) {
            goto fail;
        }
    } else {
        if ((p = strchr(devname, '.'))) {
            filter.bus_num    = strtoul(devname, NULL, 0);
            filter.addr       = strtoul(p + 1, NULL, 0);
            filter.vendor_id  = 0;
            filter.product_id = 0;
        } else if ((p = strchr(devname, ':'))) {
            filter.bus_num    = 0;
            filter.addr       = 0;
            filter.vendor_id  = strtoul(devname, NULL, 16);
            filter.product_id = strtoul(p + 1, NULL, 16);
        } else {
            goto fail;
        }
    }

    qdev_prop_set_uint32(&dev->qdev, "hostbus",   filter.bus_num);
    qdev_prop_set_uint32(&dev->qdev, "hostaddr",  filter.addr);
    qdev_prop_set_uint32(&dev->qdev, "vendorid",  filter.vendor_id);
    qdev_prop_set_uint32(&dev->qdev, "productid", filter.product_id);
    qdev_init_nofail(&dev->qdev);
    return dev;

fail:
    qdev_free(&dev->qdev);
    return NULL;
}

int usb_host_device_close(const char *devname)
{
#if 0
    char product_name[PRODUCT_NAME_SZ];
    int bus_num, addr;
    USBHostDevice *s;

    if (strstr(devname, "auto:")) {
        return usb_host_auto_del(devname);
    }
    if (usb_host_find_device(&bus_num, &addr, product_name,
                                    sizeof(product_name), devname) < 0) {
        return -1;
    }
    s = hostdev_find(bus_num, addr);
    if (s) {
        usb_device_delete_addr(s->bus_num, s->dev.addr);
        return 0;
    }
#endif

    return -1;
}

static int get_tag_value(char *buf, int buf_size,
                         const char *str, const char *tag,
                         const char *stopchars)
{
    const char *p;
    char *q;
    p = strstr(str, tag);
    if (!p) {
        return -1;
    }
    p += strlen(tag);
    while (qemu_isspace(*p)) {
        p++;
    }
    q = buf;
    while (*p != '\0' && !strchr(stopchars, *p)) {
        if ((q - buf) < (buf_size - 1)) {
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
    return q - buf;
}

/*
 * Use /proc/bus/usb/devices or /dev/bus/usb/devices file to determine
 * host's USB devices. This is legacy support since many distributions
 * are moving to /sys/bus/usb
 */
static int usb_host_scan_dev(void *opaque, USBScanFunc *func)
{
    FILE *f = NULL;
    char line[1024];
    char buf[1024];
    int bus_num, addr, speed, device_count, class_id, product_id, vendor_id;
    char product_name[512];
    int ret = 0;

    if (!usb_host_device_path) {
        perror("husb: USB Host Device Path not set");
        goto the_end;
    }
    snprintf(line, sizeof(line), "%s/devices", usb_host_device_path);
    f = fopen(line, "r");
    if (!f) {
        perror("husb: cannot open devices file");
        goto the_end;
    }

    device_count = 0;
    bus_num = addr = speed = class_id = product_id = vendor_id = 0;
    for(;;) {
        if (fgets(line, sizeof(line), f) == NULL) {
            break;
        }
        if (strlen(line) > 0) {
            line[strlen(line) - 1] = '\0';
        }
        if (line[0] == 'T' && line[1] == ':') {
            if (device_count && (vendor_id || product_id)) {
                /* New device.  Add the previously discovered device.  */
                ret = func(opaque, bus_num, addr, 0, class_id, vendor_id,
                           product_id, product_name, speed);
                if (ret) {
                    goto the_end;
                }
            }
            if (get_tag_value(buf, sizeof(buf), line, "Bus=", " ") < 0) {
                goto fail;
            }
            bus_num = atoi(buf);
            if (get_tag_value(buf, sizeof(buf), line, "Dev#=", " ") < 0) {
                goto fail;
            }
            addr = atoi(buf);
            if (get_tag_value(buf, sizeof(buf), line, "Spd=", " ") < 0) {
                goto fail;
            }
            if (!strcmp(buf, "480")) {
                speed = USB_SPEED_HIGH;
            } else if (!strcmp(buf, "1.5")) {
                speed = USB_SPEED_LOW;
            } else {
                speed = USB_SPEED_FULL;
            }
            product_name[0] = '\0';
            class_id = 0xff;
            device_count++;
            product_id = 0;
            vendor_id = 0;
        } else if (line[0] == 'P' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Vendor=", " ") < 0) {
                goto fail;
            }
            vendor_id = strtoul(buf, NULL, 16);
            if (get_tag_value(buf, sizeof(buf), line, "ProdID=", " ") < 0) {
                goto fail;
            }
            product_id = strtoul(buf, NULL, 16);
        } else if (line[0] == 'S' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Product=", "") < 0) {
                goto fail;
            }
            pstrcpy(product_name, sizeof(product_name), buf);
        } else if (line[0] == 'D' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Cls=", " (") < 0) {
                goto fail;
            }
            class_id = strtoul(buf, NULL, 16);
        }
    fail: ;
    }
    if (device_count && (vendor_id || product_id)) {
        /* Add the last device.  */
        ret = func(opaque, bus_num, addr, 0, class_id, vendor_id,
                   product_id, product_name, speed);
    }
 the_end:
    if (f) {
        fclose(f);
    }
    return ret;
}

/*
 * Read sys file-system device file
 *
 * @line address of buffer to put file contents in
 * @line_size size of line
 * @device_file path to device file (printf format string)
 * @device_name device being opened (inserted into device_file)
 *
 * @return 0 failed, 1 succeeded ('line' contains data)
 */
static int usb_host_read_file(char *line, size_t line_size,
                              const char *device_file, const char *device_name)
{
    FILE *f;
    int ret = 0;
    char filename[PATH_MAX];

    snprintf(filename, PATH_MAX, USBSYSBUS_PATH "/devices/%s/%s", device_name,
             device_file);
    f = fopen(filename, "r");
    if (f) {
        ret = fgets(line, line_size, f) != NULL;
        fclose(f);
    }

    return ret;
}

/*
 * Use /sys/bus/usb/devices/ directory to determine host's USB
 * devices.
 *
 * This code is based on Robert Schiele's original patches posted to
 * the Novell bug-tracker https://bugzilla.novell.com/show_bug.cgi?id=241950
 */
static int usb_host_scan_sys(void *opaque, USBScanFunc *func)
{
    DIR *dir = NULL;
    char line[1024];
    int bus_num, addr, devpath, speed, class_id, product_id, vendor_id;
    int ret = 0;
    char product_name[512];
    struct dirent *de;

    dir = opendir(USBSYSBUS_PATH "/devices");
    if (!dir) {
        perror("husb: cannot open devices directory");
        goto the_end;
    }

    while ((de = readdir(dir))) {
        if (de->d_name[0] != '.' && !strchr(de->d_name, ':')) {
            char *tmpstr = de->d_name;
            if (!strncmp(de->d_name, "usb", 3)) {
                tmpstr += 3;
            }
            if (sscanf(tmpstr, "%d-%d", &bus_num, &devpath) < 1) {
                goto the_end;
            }

            if (!usb_host_read_file(line, sizeof(line), "devnum", de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%d", &addr) != 1) {
                goto the_end;
            }
            if (!usb_host_read_file(line, sizeof(line), "bDeviceClass",
                                    de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%x", &class_id) != 1) {
                goto the_end;
            }

            if (!usb_host_read_file(line, sizeof(line), "idVendor",
                                    de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%x", &vendor_id) != 1) {
                goto the_end;
            }
            if (!usb_host_read_file(line, sizeof(line), "idProduct",
                                    de->d_name)) {
                goto the_end;
            }
            if (sscanf(line, "%x", &product_id) != 1) {
                goto the_end;
            }
            if (!usb_host_read_file(line, sizeof(line), "product",
                                    de->d_name)) {
                *product_name = 0;
            } else {
                if (strlen(line) > 0) {
                    line[strlen(line) - 1] = '\0';
                }
                pstrcpy(product_name, sizeof(product_name), line);
            }

            if (!usb_host_read_file(line, sizeof(line), "speed", de->d_name)) {
                goto the_end;
            }
            if (!strcmp(line, "480\n")) {
                speed = USB_SPEED_HIGH;
            } else if (!strcmp(line, "1.5\n")) {
                speed = USB_SPEED_LOW;
            } else {
                speed = USB_SPEED_FULL;
            }

            ret = func(opaque, bus_num, addr, devpath, class_id, vendor_id,
                       product_id, product_name, speed);
            if (ret) {
                goto the_end;
            }
        }
    }
 the_end:
    if (dir) {
        closedir(dir);
    }
    return ret;
}

/*
 * Determine how to access the host's USB devices and call the
 * specific support function.
 */
static int usb_host_scan(void *opaque, USBScanFunc *func)
{
    Monitor *mon = cur_mon;
    FILE *f = NULL;
    DIR *dir = NULL;
    int ret = 0;
    const char *fs_type[] = {"unknown", "proc", "dev", "sys"};
    char devpath[PATH_MAX];

    /* only check the host once */
    if (!usb_fs_type) {
        dir = opendir(USBSYSBUS_PATH "/devices");
        if (dir) {
            /* devices found in /dev/bus/usb/ (yes - not a mistake!) */
            strcpy(devpath, USBDEVBUS_PATH);
            usb_fs_type = USB_FS_SYS;
            closedir(dir);
            DPRINTF(USBDBG_DEVOPENED, USBSYSBUS_PATH);
            goto found_devices;
        }
        f = fopen(USBPROCBUS_PATH "/devices", "r");
        if (f) {
            /* devices found in /proc/bus/usb/ */
            strcpy(devpath, USBPROCBUS_PATH);
            usb_fs_type = USB_FS_PROC;
            fclose(f);
            DPRINTF(USBDBG_DEVOPENED, USBPROCBUS_PATH);
            goto found_devices;
        }
        /* try additional methods if an access method hasn't been found yet */
        f = fopen(USBDEVBUS_PATH "/devices", "r");
        if (f) {
            /* devices found in /dev/bus/usb/ */
            strcpy(devpath, USBDEVBUS_PATH);
            usb_fs_type = USB_FS_DEV;
            fclose(f);
            DPRINTF(USBDBG_DEVOPENED, USBDEVBUS_PATH);
            goto found_devices;
        }
    found_devices:
        if (!usb_fs_type) {
            if (mon) {
                monitor_printf(mon, "husb: unable to access USB devices\n");
            }
            return -ENOENT;
        }

        /* the module setting (used later for opening devices) */
        usb_host_device_path = qemu_mallocz(strlen(devpath)+1);
        strcpy(usb_host_device_path, devpath);
        if (mon) {
            monitor_printf(mon, "husb: using %s file-system with %s\n",
                           fs_type[usb_fs_type], usb_host_device_path);
        }
    }

    switch (usb_fs_type) {
    case USB_FS_PROC:
    case USB_FS_DEV:
        ret = usb_host_scan_dev(opaque, func);
        break;
    case USB_FS_SYS:
        ret = usb_host_scan_sys(opaque, func);
        break;
    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

static QEMUTimer *usb_auto_timer;

static int usb_host_auto_scan(void *opaque, int bus_num, int addr, int devpath,
                              int class_id, int vendor_id, int product_id,
                              const char *product_name, int speed)
{
    struct USBAutoFilter *f;
    struct USBHostDevice *s;

    /* Ignore hubs */
    if (class_id == 9)
        return 0;

    QTAILQ_FOREACH(s, &hostdevs, next) {
        f = &s->match;

        if (f->bus_num > 0 && f->bus_num != bus_num) {
            continue;
        }
        if (f->addr > 0 && f->addr != addr) {
            continue;
        }

        if (f->vendor_id > 0 && f->vendor_id != vendor_id) {
            continue;
        }

        if (f->product_id > 0 && f->product_id != product_id) {
            continue;
        }
        /* We got a match */

        /* Already attached ? */
        if (s->fd != -1) {
            return 0;
        }
        DPRINTF("husb: auto open: bus_num %d addr %d\n", bus_num, addr);

        usb_host_open(s, bus_num, addr, devpath, product_name);
    }

    return 0;
}

static void usb_host_auto_check(void *unused)
{
    struct USBHostDevice *s;
    int unconnected = 0;

    usb_host_scan(NULL, usb_host_auto_scan);

    QTAILQ_FOREACH(s, &hostdevs, next) {
        if (s->fd == -1) {
            unconnected++;
        }
    }

    if (unconnected == 0) {
        /* nothing to watch */
        if (usb_auto_timer) {
            qemu_del_timer(usb_auto_timer);
        }
        return;
    }

    if (!usb_auto_timer) {
        usb_auto_timer = qemu_new_timer_ms(rt_clock, usb_host_auto_check, NULL);
        if (!usb_auto_timer) {
            return;
        }
    }
    qemu_mod_timer(usb_auto_timer, qemu_get_clock_ms(rt_clock) + 2000);
}

/*
 * Autoconnect filter
 * Format:
 *    auto:bus:dev[:vid:pid]
 *    auto:bus.dev[:vid:pid]
 *
 *    bus  - bus number    (dec, * means any)
 *    dev  - device number (dec, * means any)
 *    vid  - vendor id     (hex, * means any)
 *    pid  - product id    (hex, * means any)
 *
 *    See 'lsusb' output.
 */
static int parse_filter(const char *spec, struct USBAutoFilter *f)
{
    enum { BUS, DEV, VID, PID, DONE };
    const char *p = spec;
    int i;

    f->bus_num    = 0;
    f->addr       = 0;
    f->vendor_id  = 0;
    f->product_id = 0;

    for (i = BUS; i < DONE; i++) {
        p = strpbrk(p, ":.");
        if (!p) {
            break;
        }
        p++;

        if (*p == '*') {
            continue;
        }
        switch(i) {
        case BUS: f->bus_num = strtol(p, NULL, 10);    break;
        case DEV: f->addr    = strtol(p, NULL, 10);    break;
        case VID: f->vendor_id  = strtol(p, NULL, 16); break;
        case PID: f->product_id = strtol(p, NULL, 16); break;
        }
    }

    if (i < DEV) {
        fprintf(stderr, "husb: invalid auto filter spec %s\n", spec);
        return -1;
    }

    return 0;
}

/**********************/
/* USB host device info */

struct usb_class_info {
    int class;
    const char *class_name;
};

static const struct usb_class_info usb_class_info[] = {
    { USB_CLASS_AUDIO, "Audio"},
    { USB_CLASS_COMM, "Communication"},
    { USB_CLASS_HID, "HID"},
    { USB_CLASS_HUB, "Hub" },
    { USB_CLASS_PHYSICAL, "Physical" },
    { USB_CLASS_PRINTER, "Printer" },
    { USB_CLASS_MASS_STORAGE, "Storage" },
    { USB_CLASS_CDC_DATA, "Data" },
    { USB_CLASS_APP_SPEC, "Application Specific" },
    { USB_CLASS_VENDOR_SPEC, "Vendor Specific" },
    { USB_CLASS_STILL_IMAGE, "Still Image" },
    { USB_CLASS_CSCID, "Smart Card" },
    { USB_CLASS_CONTENT_SEC, "Content Security" },
    { -1, NULL }
};

static const char *usb_class_str(uint8_t class)
{
    const struct usb_class_info *p;
    for(p = usb_class_info; p->class != -1; p++) {
        if (p->class == class) {
            break;
        }
    }
    return p->class_name;
}

static void usb_info_device(Monitor *mon, int bus_num, int addr, int class_id,
                            int vendor_id, int product_id,
                            const char *product_name,
                            int speed)
{
    const char *class_str, *speed_str;

    switch(speed) {
    case USB_SPEED_LOW:
        speed_str = "1.5";
        break;
    case USB_SPEED_FULL:
        speed_str = "12";
        break;
    case USB_SPEED_HIGH:
        speed_str = "480";
        break;
    default:
        speed_str = "?";
        break;
    }

    monitor_printf(mon, "  Device %d.%d, speed %s Mb/s\n",
                bus_num, addr, speed_str);
    class_str = usb_class_str(class_id);
    if (class_str) {
        monitor_printf(mon, "    %s:", class_str);
    } else {
        monitor_printf(mon, "    Class %02x:", class_id);
    }
    monitor_printf(mon, " USB device %04x:%04x", vendor_id, product_id);
    if (product_name[0] != '\0') {
        monitor_printf(mon, ", %s", product_name);
    }
    monitor_printf(mon, "\n");
}

static int usb_host_info_device(void *opaque, int bus_num, int addr,
                                int devpath, int class_id,
                                int vendor_id, int product_id,
                                const char *product_name,
                                int speed)
{
    Monitor *mon = opaque;

    usb_info_device(mon, bus_num, addr, class_id, vendor_id, product_id,
                    product_name, speed);
    return 0;
}

static void dec2str(int val, char *str, size_t size)
{
    if (val == 0) {
        snprintf(str, size, "*");
    } else {
        snprintf(str, size, "%d", val);
    }
}

static void hex2str(int val, char *str, size_t size)
{
    if (val == 0) {
        snprintf(str, size, "*");
    } else {
        snprintf(str, size, "%04x", val);
    }
}

void usb_host_info(Monitor *mon)
{
    struct USBAutoFilter *f;
    struct USBHostDevice *s;

    usb_host_scan(mon, usb_host_info_device);

    if (QTAILQ_EMPTY(&hostdevs)) {
        return;
    }

    monitor_printf(mon, "  Auto filters:\n");
    QTAILQ_FOREACH(s, &hostdevs, next) {
        char bus[10], addr[10], vid[10], pid[10];
        f = &s->match;
        dec2str(f->bus_num, bus, sizeof(bus));
        dec2str(f->addr, addr, sizeof(addr));
        hex2str(f->vendor_id, vid, sizeof(vid));
        hex2str(f->product_id, pid, sizeof(pid));
        monitor_printf(mon, "    Device %s.%s ID %s:%s\n",
                       bus, addr, vid, pid);
    }
}
