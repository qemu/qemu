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
#include "console.h"

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

typedef int USBScanFunc(void *opaque, int bus_num, int addr, int class_id,
                        int vendor_id, int product_id,
                        const char *product_name, int speed);
static int usb_host_find_device(int *pbus_num, int *paddr,
                                char *product_name, int product_name_size,
                                const char *devname);
//#define DEBUG

#ifdef DEBUG
#define dprintf printf
#else
#define dprintf(...)
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
    uint8_t  buffer[1024];
};

typedef struct USBHostDevice {
    USBDevice dev;
    int       fd;

    uint8_t   descr[1024];
    int       descr_len;
    int       configuration;
    int       ninterfaces;
    int       closing;

    struct ctrl_struct ctrl;
    struct endp_data endp_table[MAX_ENDPOINTS];

    /* Host side address */
    int bus_num;
    int addr;

    struct USBHostDevice *next;
} USBHostDevice;

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

static USBHostDevice *hostdev_list;

static void hostdev_link(USBHostDevice *dev)
{
    dev->next = hostdev_list;
    hostdev_list = dev;
}

static void hostdev_unlink(USBHostDevice *dev)
{
    USBHostDevice *pdev = hostdev_list;
    USBHostDevice **prev = &hostdev_list;

    while (pdev) {
	if (pdev == dev) {
            *prev = dev->next;
            return;
        }

        prev = &pdev->next;
        pdev = pdev->next;
    }
}

static USBHostDevice *hostdev_find(int bus_num, int addr)
{
    USBHostDevice *s = hostdev_list;
    while (s) {
        if (s->bus_num == bus_num && s->addr == addr)
            return s;
        s = s->next;
    }
    return NULL;
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
            if (errno == EAGAIN)
                return;

            if (errno == ENODEV && !s->closing) {
                printf("husb: device %d.%d disconnected\n", s->bus_num, s->addr);
	        usb_device_del_addr(0, s->dev.addr);
                return;
            }

            dprintf("husb: async. reap urb failed errno %d\n", errno);
            return;
        }

        p = aurb->packet;

	dprintf("husb: async completed. aurb %p status %d alen %d\n", 
                aurb, aurb->urb.status, aurb->urb.actual_length);

	if (p) {
            switch (aurb->urb.status) {
            case 0:
                p->len = aurb->urb.actual_length;
                if (aurb->urb.type == USBDEVFS_URB_TYPE_CONTROL)
                    async_complete_ctrl(s, p);
                break;

            case -EPIPE:
                set_halt(s, p->devep);
                /* fall through */
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

    dprintf("husb: async cancel. aurb %p\n", aurb);

    /* Mark it as dead (see async_complete above) */
    aurb->packet = NULL;

    int r = ioctl(s->fd, USBDEVFS_DISCARDURB, aurb);
    if (r < 0) {
        dprintf("husb: async. discard urb failed errno %d\n", errno);
    }
}

static int usb_host_claim_interfaces(USBHostDevice *dev, int configuration)
{
    int dev_descr_len, config_descr_len;
    int interface, nb_interfaces, nb_configurations;
    int ret, i;

    if (configuration == 0) /* address state - ignore */
        return 1;

    dprintf("husb: claiming interfaces. config %d\n", configuration);

    i = 0;
    dev_descr_len = dev->descr[0];
    if (dev_descr_len > dev->descr_len)
        goto fail;
    nb_configurations = dev->descr[17];

    i += dev_descr_len;
    while (i < dev->descr_len) {
        dprintf("husb: i is %d, descr_len is %d, dl %d, dt %d\n", i, dev->descr_len,
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
        fprintf(stderr, "husb: update iface failed. no matching configuration\n");
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

    dprintf("husb: releasing interfaces\n");

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
    USBHostDevice *s = (USBHostDevice *) dev;

    dprintf("husb: reset device %u.%u\n", s->bus_num, s->addr);

    ioctl(s->fd, USBDEVFS_RESET);

    usb_host_claim_interfaces(s, s->configuration);
}

static void usb_host_handle_destroy(USBDevice *dev)
{
    USBHostDevice *s = (USBHostDevice *)dev;

    s->closing = 1;

    qemu_set_fd_handler(s->fd, NULL, NULL, NULL);

    hostdev_unlink(s);

    async_complete(s);

    if (s->fd >= 0)
        close(s->fd);

    qemu_free(s);
}

static int usb_linux_update_endp_table(USBHostDevice *s);

static int usb_host_handle_data(USBHostDevice *s, USBPacket *p)
{
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret;

    aurb = async_alloc();
    if (!aurb) {
        dprintf("husb: async malloc failed\n");
        return USB_RET_NAK;
    }
    aurb->hdev   = s;
    aurb->packet = p;

    urb = &aurb->urb;

    if (p->pid == USB_TOKEN_IN)
    	urb->endpoint = p->devep | 0x80;
    else
    	urb->endpoint = p->devep;

    if (is_halted(s, p->devep)) {
	ret = ioctl(s->fd, USBDEVFS_CLEAR_HALT, &urb->endpoint);
        if (ret < 0) {
            dprintf("husb: failed to clear halt. ep 0x%x errno %d\n", 
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

    dprintf("husb: data submit. ep 0x%x len %u aurb %p\n", urb->endpoint, p->len, aurb);

    if (ret < 0) {
        dprintf("husb: submit failed. errno %d\n", errno);
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
    if (errno == ETIMEDOUT)
        return USB_RET_NAK;
    else 
        return USB_RET_STALL;
}

static int usb_host_set_address(USBHostDevice *s, int addr)
{
    dprintf("husb: ctrl set addr %u\n", addr);
    s->dev.addr = addr;
    return 0;
}

static int usb_host_set_config(USBHostDevice *s, int config)
{
    usb_host_release_interfaces(s);

    int ret = ioctl(s->fd, USBDEVFS_SETCONFIGURATION, &config);
 
    dprintf("husb: ctrl set config %d ret %d errno %d\n", config, ret, errno);
    
    if (ret < 0)
        return ctrl_error();
 
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
    
    dprintf("husb: ctrl set iface %d altset %d ret %d errno %d\n", 
    	iface, alt, ret, errno);
    
    if (ret < 0)
        return ctrl_error();

    usb_linux_update_endp_table(s);
    return 0;
}

static int usb_host_handle_control(USBHostDevice *s, USBPacket *p)
{
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret, value, index;

    /* 
     * Process certain standard device requests.
     * These are infrequent and are processed synchronously.
     */
    value = le16_to_cpu(s->ctrl.req.wValue);
    index = le16_to_cpu(s->ctrl.req.wIndex);

    dprintf("husb: ctrl type 0x%x req 0x%x val 0x%x index %u len %u\n",
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
                  s->ctrl.req.bRequest == USB_REQ_SET_INTERFACE)
        return usb_host_set_interface(s, index, value);

    /* The rest are asynchronous */

    aurb = async_alloc();
    if (!aurb) {
        dprintf("husb: async malloc failed\n");
        return USB_RET_NAK;
    }
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
    urb->buffer_length = 8 + s->ctrl.len;

    urb->usercontext = s;

    ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);

    dprintf("husb: submit ctrl. len %u aurb %p\n", urb->buffer_length, aurb);

    if (ret < 0) {
        dprintf("husb: submit failed. errno %d\n", errno);
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

    if (p->len != 8)
        return USB_RET_STALL;
 
    memcpy(&s->ctrl.req, p->data, 8);
    s->ctrl.len    = le16_to_cpu(s->ctrl.req.wLength);
    s->ctrl.offset = 0;
    s->ctrl.state  = CTRL_STATE_SETUP;

    if (s->ctrl.req.bRequestType & USB_DIR_IN) {
        ret = usb_host_handle_control(s, p);
        if (ret < 0)
            return ret;

        if (ret < s->ctrl.len)
            s->ctrl.len = ret;
        s->ctrl.state = CTRL_STATE_DATA;
    } else {
        if (s->ctrl.len == 0)
            s->ctrl.state = CTRL_STATE_ACK;
        else
            s->ctrl.state = CTRL_STATE_DATA;
    }

    return ret;
}

static int do_token_in(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *) dev;
    int ret = 0;

    if (p->devep != 0)
        return usb_host_handle_data(s, p);

    switch(s->ctrl.state) {
    case CTRL_STATE_ACK:
        if (!(s->ctrl.req.bRequestType & USB_DIR_IN)) {
            ret = usb_host_handle_control(s, p);
            if (ret == USB_RET_ASYNC)
                return USB_RET_ASYNC;

            s->ctrl.state = CTRL_STATE_IDLE;
            return ret > 0 ? 0 : ret;
        }

        return 0;

    case CTRL_STATE_DATA:
        if (s->ctrl.req.bRequestType & USB_DIR_IN) {
            int len = s->ctrl.len - s->ctrl.offset;
            if (len > p->len)
                len = p->len;
            memcpy(p->data, s->ctrl.buffer + s->ctrl.offset, len);
            s->ctrl.offset += len;
            if (s->ctrl.offset >= s->ctrl.len)
                s->ctrl.state = CTRL_STATE_ACK;
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

    if (p->devep != 0)
        return usb_host_handle_data(s, p);

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
            if (len > p->len)
                len = p->len;
            memcpy(s->ctrl.buffer + s->ctrl.offset, p->data, len);
            s->ctrl.offset += len;
            if (s->ctrl.offset >= s->ctrl.len)
                s->ctrl.state = CTRL_STATE_ACK;
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
        s->handle_reset(s);
        return 0;
    }

    /* Rest of the PIDs must match our address */
    if (s->state < USB_STATE_DEFAULT || p->devaddr != s->addr)
        return USB_RET_NODEV;

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

/* returns 1 on problem encountered or 0 for success */
static int usb_linux_update_endp_table(USBHostDevice *s)
{
    uint8_t *descriptors;
    uint8_t devep, type, configuration, alt_interface;
    struct usb_ctrltransfer ct;
    int interface, ret, length, i;

    ct.bRequestType = USB_DIR_IN;
    ct.bRequest = USB_REQ_GET_CONFIGURATION;
    ct.wValue = 0;
    ct.wIndex = 0;
    ct.wLength = 1;
    ct.data = &configuration;
    ct.timeout = 50;

    ret = ioctl(s->fd, USBDEVFS_CONTROL, &ct);
    if (ret < 0) {
        perror("usb_linux_update_endp_table");
        return 1;
    }

    /* in address state */
    if (configuration == 0)
        return 1;

    /* get the desired configuration, interface, and endpoint descriptors
     * from device description */
    descriptors = &s->descr[18];
    length = s->descr_len - 18;
    i = 0;

    if (descriptors[i + 1] != USB_DT_CONFIG ||
        descriptors[i + 5] != configuration) {
        dprintf("invalid descriptor data - configuration\n");
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
            perror("usb_linux_update_endp_table");
            return 1;
        }

        /* the current interface descriptor is the active interface
         * and has endpoints */
        if (descriptors[i + 3] != alt_interface) {
            i += descriptors[i];
            continue;
        }

        /* advance to the endpoints */
        while (i < length && descriptors[i +1] != USB_DT_ENDPOINT)
            i += descriptors[i];

        if (i >= length)
            break;

        while (i < length) {
            if (descriptors[i + 1] != USB_DT_ENDPOINT)
                break;

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
                dprintf("usb_host: malformed endpoint type\n");
                type = USBDEVFS_URB_TYPE_BULK;
            }
            s->endp_table[(devep & 0xf) - 1].type = type;
            s->endp_table[(devep & 0xf) - 1].halted = 0;

            i += descriptors[i];
        }
    }
    return 0;
}

static USBDevice *usb_host_device_open_addr(int bus_num, int addr, const char *prod_name)
{
    int fd = -1, ret;
    USBHostDevice *dev = NULL;
    struct usbdevfs_connectinfo ci;
    char buf[1024];

    dev = qemu_mallocz(sizeof(USBHostDevice));
    if (!dev)
        goto fail;

    dev->bus_num = bus_num;
    dev->addr = addr;

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
    dprintf("husb: opened %s\n", buf);

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
        for (x = 0; x < dev->descr_len; x++)
            printf("%02x ", dev->descr[x]);
        printf("\n=== end dumping device descriptor data ===\n");
    }
#endif

    dev->fd = fd;

    /* 
     * Initial configuration is -1 which makes us claim first 
     * available config. We used to start with 1, which does not
     * always work. I've seen devices where first config starts 
     * with 2.
     */
    if (!usb_host_claim_interfaces(dev, -1))
        goto fail;

    ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
    if (ret < 0) {
        perror("usb_host_device_open: USBDEVFS_CONNECTINFO");
        goto fail;
    }

    printf("husb: grabbed usb device %d.%d\n", bus_num, addr);

    ret = usb_linux_update_endp_table(dev);
    if (ret)
        goto fail;

    if (ci.slow)
        dev->dev.speed = USB_SPEED_LOW;
    else
        dev->dev.speed = USB_SPEED_HIGH;

    dev->dev.handle_packet  = usb_host_handle_packet;
    dev->dev.handle_reset   = usb_host_handle_reset;
    dev->dev.handle_destroy = usb_host_handle_destroy;

    if (!prod_name || prod_name[0] == '\0')
        snprintf(dev->dev.devname, sizeof(dev->dev.devname),
                 "host:%d.%d", bus_num, addr);
    else
        pstrcpy(dev->dev.devname, sizeof(dev->dev.devname),
                prod_name);

    /* USB devio uses 'write' flag to check for async completions */
    qemu_set_fd_handler(dev->fd, NULL, async_complete, dev);

    hostdev_link(dev);

    return (USBDevice *) dev;

fail:
    if (dev)
        qemu_free(dev);

    close(fd);
    return NULL;
}

static int usb_host_auto_add(const char *spec);
static int usb_host_auto_del(const char *spec);

USBDevice *usb_host_device_open(const char *devname)
{
    int bus_num, addr;
    char product_name[PRODUCT_NAME_SZ];

    if (strstr(devname, "auto:")) {
        usb_host_auto_add(devname);
        return NULL;
    }

    if (usb_host_find_device(&bus_num, &addr, product_name, sizeof(product_name),
                             devname) < 0)
        return NULL;

    if (hostdev_find(bus_num, addr)) {
       term_printf("husb: host usb device %d.%d is already open\n", bus_num, addr);
       return NULL;
    }

    return usb_host_device_open_addr(bus_num, addr, product_name);
}

int usb_host_device_close(const char *devname)
{
    char product_name[PRODUCT_NAME_SZ];
    int bus_num, addr;
    USBHostDevice *s;

    if (strstr(devname, "auto:"))
        return usb_host_auto_del(devname);

    if (usb_host_find_device(&bus_num, &addr, product_name, sizeof(product_name),
                             devname) < 0)
        return -1;
 
    s = hostdev_find(bus_num, addr);
    if (s) {
        usb_device_del_addr(0, s->dev.addr);
        return 0;
    }

    return -1;
}
 
static int get_tag_value(char *buf, int buf_size,
                         const char *str, const char *tag,
                         const char *stopchars)
{
    const char *p;
    char *q;
    p = strstr(str, tag);
    if (!p)
        return -1;
    p += strlen(tag);
    while (qemu_isspace(*p))
        p++;
    q = buf;
    while (*p != '\0' && !strchr(stopchars, *p)) {
        if ((q - buf) < (buf_size - 1))
            *q++ = *p;
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
    FILE *f = 0;
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
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        if (strlen(line) > 0)
            line[strlen(line) - 1] = '\0';
        if (line[0] == 'T' && line[1] == ':') {
            if (device_count && (vendor_id || product_id)) {
                /* New device.  Add the previously discovered device.  */
                ret = func(opaque, bus_num, addr, class_id, vendor_id,
                           product_id, product_name, speed);
                if (ret)
                    goto the_end;
            }
            if (get_tag_value(buf, sizeof(buf), line, "Bus=", " ") < 0)
                goto fail;
            bus_num = atoi(buf);
            if (get_tag_value(buf, sizeof(buf), line, "Dev#=", " ") < 0)
                goto fail;
            addr = atoi(buf);
            if (get_tag_value(buf, sizeof(buf), line, "Spd=", " ") < 0)
                goto fail;
            if (!strcmp(buf, "480"))
                speed = USB_SPEED_HIGH;
            else if (!strcmp(buf, "1.5"))
                speed = USB_SPEED_LOW;
            else
                speed = USB_SPEED_FULL;
            product_name[0] = '\0';
            class_id = 0xff;
            device_count++;
            product_id = 0;
            vendor_id = 0;
        } else if (line[0] == 'P' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Vendor=", " ") < 0)
                goto fail;
            vendor_id = strtoul(buf, NULL, 16);
            if (get_tag_value(buf, sizeof(buf), line, "ProdID=", " ") < 0)
                goto fail;
            product_id = strtoul(buf, NULL, 16);
        } else if (line[0] == 'S' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Product=", "") < 0)
                goto fail;
            pstrcpy(product_name, sizeof(product_name), buf);
        } else if (line[0] == 'D' && line[1] == ':') {
            if (get_tag_value(buf, sizeof(buf), line, "Cls=", " (") < 0)
                goto fail;
            class_id = strtoul(buf, NULL, 16);
        }
    fail: ;
    }
    if (device_count && (vendor_id || product_id)) {
        /* Add the last device.  */
        ret = func(opaque, bus_num, addr, class_id, vendor_id,
                   product_id, product_name, speed);
    }
 the_end:
    if (f)
        fclose(f);
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
static int usb_host_read_file(char *line, size_t line_size, const char *device_file, const char *device_name)
{
    FILE *f;
    int ret = 0;
    char filename[PATH_MAX];

    snprintf(filename, PATH_MAX, USBSYSBUS_PATH "/devices/%s/%s", device_name,
             device_file);
    f = fopen(filename, "r");
    if (f) {
        fgets(line, line_size, f);
        fclose(f);
        ret = 1;
    } else {
        term_printf("husb: could not open %s\n", filename);
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
    DIR *dir = 0;
    char line[1024];
    int bus_num, addr, speed, class_id, product_id, vendor_id;
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
            if (!strncmp(de->d_name, "usb", 3))
                tmpstr += 3;
            bus_num = atoi(tmpstr);

            if (!usb_host_read_file(line, sizeof(line), "devnum", de->d_name))
                goto the_end;
            if (sscanf(line, "%d", &addr) != 1)
                goto the_end;

            if (!usb_host_read_file(line, sizeof(line), "bDeviceClass",
                                    de->d_name))
                goto the_end;
            if (sscanf(line, "%x", &class_id) != 1)
                goto the_end;

            if (!usb_host_read_file(line, sizeof(line), "idVendor", de->d_name))
                goto the_end;
            if (sscanf(line, "%x", &vendor_id) != 1)
                goto the_end;

            if (!usb_host_read_file(line, sizeof(line), "idProduct",
                                    de->d_name))
                goto the_end;
            if (sscanf(line, "%x", &product_id) != 1)
                goto the_end;

            if (!usb_host_read_file(line, sizeof(line), "product",
                                    de->d_name)) {
                *product_name = 0;
            } else {
                if (strlen(line) > 0)
                    line[strlen(line) - 1] = '\0';
                pstrcpy(product_name, sizeof(product_name), line);
            }

            if (!usb_host_read_file(line, sizeof(line), "speed", de->d_name))
                goto the_end;
            if (!strcmp(line, "480\n"))
                speed = USB_SPEED_HIGH;
            else if (!strcmp(line, "1.5\n"))
                speed = USB_SPEED_LOW;
            else
                speed = USB_SPEED_FULL;

            ret = func(opaque, bus_num, addr, class_id, vendor_id,
                       product_id, product_name, speed);
            if (ret)
                goto the_end;
        }
    }
 the_end:
    if (dir)
        closedir(dir);
    return ret;
}

/*
 * Determine how to access the host's USB devices and call the
 * specific support function.
 */
static int usb_host_scan(void *opaque, USBScanFunc *func)
{
    FILE *f = 0;
    DIR *dir = 0;
    int ret = 0;
    const char *fs_type[] = {"unknown", "proc", "dev", "sys"};
    char devpath[PATH_MAX];

    /* only check the host once */
    if (!usb_fs_type) {
        f = fopen(USBPROCBUS_PATH "/devices", "r");
        if (f) {
            /* devices found in /proc/bus/usb/ */
            strcpy(devpath, USBPROCBUS_PATH);
            usb_fs_type = USB_FS_PROC;
            fclose(f);
            dprintf(USBDBG_DEVOPENED, USBPROCBUS_PATH);
            goto found_devices;
        }
        /* try additional methods if an access method hasn't been found yet */
        f = fopen(USBDEVBUS_PATH "/devices", "r");
        if (f) {
            /* devices found in /dev/bus/usb/ */
            strcpy(devpath, USBDEVBUS_PATH);
            usb_fs_type = USB_FS_DEV;
            fclose(f);
            dprintf(USBDBG_DEVOPENED, USBDEVBUS_PATH);
            goto found_devices;
        }
        dir = opendir(USBSYSBUS_PATH "/devices");
        if (dir) {
            /* devices found in /dev/bus/usb/ (yes - not a mistake!) */
            strcpy(devpath, USBDEVBUS_PATH);
            usb_fs_type = USB_FS_SYS;
            closedir(dir);
            dprintf(USBDBG_DEVOPENED, USBSYSBUS_PATH);
            goto found_devices;
        }
    found_devices:
        if (!usb_fs_type) {
            term_printf("husb: unable to access USB devices\n");
            return -ENOENT;
        }

        /* the module setting (used later for opening devices) */
        usb_host_device_path = qemu_mallocz(strlen(devpath)+1);
        if (usb_host_device_path) {
            strcpy(usb_host_device_path, devpath);
            term_printf("husb: using %s file-system with %s\n", fs_type[usb_fs_type], usb_host_device_path);
        } else {
            /* out of memory? */
            perror("husb: unable to allocate memory for device path");
            return -ENOMEM;
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

struct USBAutoFilter {
    struct USBAutoFilter *next;
    int bus_num;
    int addr;
    int vendor_id;
    int product_id;
};

static QEMUTimer *usb_auto_timer;
static struct USBAutoFilter *usb_auto_filter;

static int usb_host_auto_scan(void *opaque, int bus_num, int addr,
                     int class_id, int vendor_id, int product_id,
                     const char *product_name, int speed)
{
    struct USBAutoFilter *f;
    struct USBDevice *dev;

    /* Ignore hubs */
    if (class_id == 9)
        return 0;

    for (f = usb_auto_filter; f; f = f->next) {
	if (f->bus_num >= 0 && f->bus_num != bus_num)
            continue;

	if (f->addr >= 0 && f->addr != addr)
            continue;

	if (f->vendor_id >= 0 && f->vendor_id != vendor_id)
            continue;

	if (f->product_id >= 0 && f->product_id != product_id)
            continue;

        /* We got a match */

        /* Allredy attached ? */
        if (hostdev_find(bus_num, addr))
            return 0;

        dprintf("husb: auto open: bus_num %d addr %d\n", bus_num, addr);

	dev = usb_host_device_open_addr(bus_num, addr, product_name);
	if (dev)
	    usb_device_add_dev(dev);
    }

    return 0;
}

static void usb_host_auto_timer(void *unused)
{
    usb_host_scan(NULL, usb_host_auto_scan);
    qemu_mod_timer(usb_auto_timer, qemu_get_clock(rt_clock) + 2000);
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

    f->bus_num    = -1;
    f->addr       = -1;
    f->vendor_id  = -1;
    f->product_id = -1;

    for (i = BUS; i < DONE; i++) {
    	p = strpbrk(p, ":.");
    	if (!p) break;
        p++;
 
    	if (*p == '*')
            continue;

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

static int match_filter(const struct USBAutoFilter *f1, 
                        const struct USBAutoFilter *f2)
{
    return f1->bus_num    == f2->bus_num &&
           f1->addr       == f2->addr &&
           f1->vendor_id  == f2->vendor_id &&
           f1->product_id == f2->product_id;
}

static int usb_host_auto_add(const char *spec)
{
    struct USBAutoFilter filter, *f;

    if (parse_filter(spec, &filter) < 0)
        return -1;

    f = qemu_mallocz(sizeof(*f));
    if (!f) {
        fprintf(stderr, "husb: failed to allocate auto filter\n");
        return -1;
    }

    *f = filter; 

    if (!usb_auto_filter) {
        /*
         * First entry. Init and start the monitor.
         * Right now we're using timer to check for new devices.
         * If this turns out to be too expensive we can move that into a 
         * separate thread.
         */
	usb_auto_timer = qemu_new_timer(rt_clock, usb_host_auto_timer, NULL);
	if (!usb_auto_timer) {
            fprintf(stderr, "husb: failed to allocate auto scan timer\n");
            qemu_free(f);
            return -1;
        }

        /* Check for new devices every two seconds */
        qemu_mod_timer(usb_auto_timer, qemu_get_clock(rt_clock) + 2000);
    }

    dprintf("husb: added auto filter: bus_num %d addr %d vid %d pid %d\n",
	f->bus_num, f->addr, f->vendor_id, f->product_id);

    f->next = usb_auto_filter;
    usb_auto_filter = f;

    return 0;
}

static int usb_host_auto_del(const char *spec)
{
    struct USBAutoFilter *pf = usb_auto_filter;
    struct USBAutoFilter **prev = &usb_auto_filter;
    struct USBAutoFilter filter;

    if (parse_filter(spec, &filter) < 0)
        return -1;

    while (pf) {
        if (match_filter(pf, &filter)) {
            dprintf("husb: removed auto filter: bus_num %d addr %d vid %d pid %d\n",
	             pf->bus_num, pf->addr, pf->vendor_id, pf->product_id);

            *prev = pf->next;

	    if (!usb_auto_filter) {
                /* No more filters. Stop scanning. */
                qemu_del_timer(usb_auto_timer);
                qemu_free_timer(usb_auto_timer);
            }

            return 0;
        }

        prev = &pf->next;
        pf   = pf->next;
    }

    return -1;
}

typedef struct FindDeviceState {
    int vendor_id;
    int product_id;
    int bus_num;
    int addr;
    char product_name[PRODUCT_NAME_SZ];
} FindDeviceState;

static int usb_host_find_device_scan(void *opaque, int bus_num, int addr,
                                     int class_id,
                                     int vendor_id, int product_id,
                                     const char *product_name, int speed)
{
    FindDeviceState *s = opaque;
    if ((vendor_id == s->vendor_id &&
        product_id == s->product_id) ||
        (bus_num == s->bus_num &&
        addr == s->addr)) {
        pstrcpy(s->product_name, PRODUCT_NAME_SZ, product_name);
        s->bus_num = bus_num;
        s->addr = addr;
        return 1;
    } else {
        return 0;
    }
}

/* the syntax is :
   'bus.addr' (decimal numbers) or
   'vendor_id:product_id' (hexa numbers) */
static int usb_host_find_device(int *pbus_num, int *paddr,
                                char *product_name, int product_name_size,
                                const char *devname)
{
    const char *p;
    int ret;
    FindDeviceState fs;

    p = strchr(devname, '.');
    if (p) {
        *pbus_num = strtoul(devname, NULL, 0);
        *paddr = strtoul(p + 1, NULL, 0);
        fs.bus_num = *pbus_num;
        fs.addr = *paddr;
        ret = usb_host_scan(&fs, usb_host_find_device_scan);
        if (ret)
            pstrcpy(product_name, product_name_size, fs.product_name);
        return 0;
    }

    p = strchr(devname, ':');
    if (p) {
        fs.vendor_id = strtoul(devname, NULL, 16);
        fs.product_id = strtoul(p + 1, NULL, 16);
        ret = usb_host_scan(&fs, usb_host_find_device_scan);
        if (ret) {
            *pbus_num = fs.bus_num;
            *paddr = fs.addr;
            pstrcpy(product_name, product_name_size, fs.product_name);
            return 0;
        }
    }
    return -1;
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
        if (p->class == class)
            break;
    }
    return p->class_name;
}

static void usb_info_device(int bus_num, int addr, int class_id,
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

    term_printf("  Device %d.%d, speed %s Mb/s\n",
                bus_num, addr, speed_str);
    class_str = usb_class_str(class_id);
    if (class_str)
        term_printf("    %s:", class_str);
    else
        term_printf("    Class %02x:", class_id);
    term_printf(" USB device %04x:%04x", vendor_id, product_id);
    if (product_name[0] != '\0')
        term_printf(", %s", product_name);
    term_printf("\n");
}

static int usb_host_info_device(void *opaque, int bus_num, int addr,
                                int class_id,
                                int vendor_id, int product_id,
                                const char *product_name,
                                int speed)
{
    usb_info_device(bus_num, addr, class_id, vendor_id, product_id,
                    product_name, speed);
    return 0;
}

static void dec2str(int val, char *str, size_t size)
{
    if (val == -1)
        snprintf(str, size, "*");
    else
        snprintf(str, size, "%d", val); 
}

static void hex2str(int val, char *str, size_t size)
{
    if (val == -1)
        snprintf(str, size, "*");
    else
        snprintf(str, size, "%x", val);
}

void usb_host_info(void)
{
    struct USBAutoFilter *f;

    usb_host_scan(NULL, usb_host_info_device);

    if (usb_auto_filter)
        term_printf("  Auto filters:\n");
    for (f = usb_auto_filter; f; f = f->next) {
        char bus[10], addr[10], vid[10], pid[10];
        dec2str(f->bus_num, bus, sizeof(bus));
        dec2str(f->addr, addr, sizeof(addr));
        hex2str(f->vendor_id, vid, sizeof(vid));
        hex2str(f->product_id, pid, sizeof(pid));
    	term_printf("    Device %s.%s ID %s:%s\n", bus, addr, vid, pid);
    }
}
