/*
 * Linux host USB redirector
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Support for host device auto connect & disconnect
 *       Copyright (c) 2008 Max Krasnyansky
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
#include "hw/usb.h"
#include "console.h"

#if defined(__linux__)
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <linux/version.h>
#include <signal.h>

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

typedef int USBScanFunc(void *opaque, int bus_num, int addr, int class_id,
                        int vendor_id, int product_id,
                        const char *product_name, int speed);
static int usb_host_find_device(int *pbus_num, int *paddr,
                                char *product_name, int product_name_size,
                                const char *devname);

//#define DEBUG
//#define DEBUG_ISOCH
//#define USE_ASYNCIO

#define USBDEVFS_PATH "/proc/bus/usb"
#define PRODUCT_NAME_SZ 32
#define SIG_ISOCOMPLETE (SIGRTMIN+7)
#define MAX_ENDPOINTS 16

struct sigaction sigact;

/* endpoint association data */
struct endp_data {
    uint8_t type;
};



/* FIXME: move USBPacket to PendingURB */
typedef struct USBHostDevice {
    USBDevice dev;
    int fd;
    int pipe_fds[2];
    USBPacket *packet;
    struct endp_data endp_table[MAX_ENDPOINTS];
    int configuration;
    uint8_t descr[1024];
    int descr_len;
    int urbs_ready;

    QEMUTimer *timer;

    /* Host side address */
    int bus_num;
    int addr;

    struct USBHostDevice *next;
} USBHostDevice;

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

typedef struct PendingURB {
    struct usbdevfs_urb *urb;
    int status;
    struct PendingURB *next;
} PendingURB;

static PendingURB *pending_urbs = NULL;

static int add_pending_urb(struct usbdevfs_urb *urb)
{
    PendingURB *purb = qemu_mallocz(sizeof(PendingURB));
    if (purb) {
        purb->urb = urb;
        purb->status = 0;
        purb->next = pending_urbs;
        pending_urbs = purb;
        return 1;
    }
    return 0;
}

static int del_pending_urb(struct usbdevfs_urb *urb)
{
    PendingURB *purb = pending_urbs;
    PendingURB *prev = NULL;

    while (purb && purb->urb != urb) {
        prev = purb;
        purb = purb->next;
    }

    if (purb && purb->urb == urb) {
        if (prev) {
            prev->next = purb->next;
        } else {
            pending_urbs = purb->next;
        }
        qemu_free(purb);
        return 1;
    }
    return 0;
}

#ifdef USE_ASYNCIO
static PendingURB *get_pending_urb(struct usbdevfs_urb *urb)
{
    PendingURB *purb = pending_urbs;

    while (purb && purb->urb != urb) {
        purb = purb->next;
    }

    if (purb && purb->urb == urb) {
        return purb;
    }
    return NULL;
}
#endif

static int usb_host_update_interfaces(USBHostDevice *dev, int configuration)
{
    int dev_descr_len, config_descr_len;
    int interface, nb_interfaces, nb_configurations;
    int ret, i;

    if (configuration == 0) /* address state - ignore */
        return 1;

    i = 0;
    dev_descr_len = dev->descr[0];
    if (dev_descr_len > dev->descr_len)
        goto fail;
    nb_configurations = dev->descr[17];

    i += dev_descr_len;
    while (i < dev->descr_len) {
#ifdef DEBUG
        printf("i is %d, descr_len is %d, dl %d, dt %d\n", i, dev->descr_len,
               dev->descr[i], dev->descr[i+1]);
#endif
        if (dev->descr[i+1] != USB_DT_CONFIG) {
            i += dev->descr[i];
            continue;
        }
        config_descr_len = dev->descr[i];

#ifdef DEBUG
	printf("config #%d need %d\n", dev->descr[i + 5], configuration); 
#endif

        if (configuration < 0 || configuration == dev->descr[i + 5])
            break;

        i += config_descr_len;
    }

    if (i >= dev->descr_len) {
        printf("usb_host: error - device has no matching configuration\n");
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
                fprintf(stderr,
                        "usb_host: warning - device already grabbed\n");
            } else {
                perror("USBDEVFS_CLAIMINTERFACE");
            }
        fail:
            return 0;
        }
    }

#ifdef DEBUG
    printf("usb_host: %d interfaces claimed for configuration %d\n",
           nb_interfaces, configuration);
#endif

    return 1;
}

static void usb_host_handle_reset(USBDevice *dev)
{
#if 0
    USBHostDevice *s = (USBHostDevice *)dev;
    /* USBDEVFS_RESET, but not the first time as it has already be
       done by the host OS */
    ioctl(s->fd, USBDEVFS_RESET);
#endif
}

static void usb_host_handle_destroy(USBDevice *dev)
{
    USBHostDevice *s = (USBHostDevice *)dev;

    qemu_del_timer(s->timer);

    hostdev_unlink(s);

    if (s->fd >= 0)
        close(s->fd);

    qemu_free(s);
}

static int usb_linux_update_endp_table(USBHostDevice *s);

static int usb_host_handle_control(USBDevice *dev,
                                   int request,
                                   int value,
                                   int index,
                                   int length,
                                   uint8_t *data)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    struct usb_ctrltransfer ct;
    struct usbdevfs_setinterface si;
    int intf_update_required = 0;
    int ret;

    if (request == (DeviceOutRequest | USB_REQ_SET_ADDRESS)) {
        /* specific SET_ADDRESS support */
        dev->addr = value;
        return 0;
    } else if (request == ((USB_RECIP_INTERFACE << 8) |
                           USB_REQ_SET_INTERFACE)) {
        /* set alternate setting for the interface */
        si.interface = index;
        si.altsetting = value;
        ret = ioctl(s->fd, USBDEVFS_SETINTERFACE, &si);
        usb_linux_update_endp_table(s);
    } else if (request == (DeviceOutRequest | USB_REQ_SET_CONFIGURATION)) {
#ifdef DEBUG
        printf("usb_host_handle_control: SET_CONFIGURATION request - "
               "config %d\n", value & 0xff);
#endif
        if (s->configuration != (value & 0xff)) {
            s->configuration = (value & 0xff);
            intf_update_required = 1;
        }
        goto do_request;
    } else {
    do_request:
        ct.bRequestType = request >> 8;
        ct.bRequest = request;
        ct.wValue = value;
        ct.wIndex = index;
        ct.wLength = length;
        ct.timeout = 50;
        ct.data = data;
        ret = ioctl(s->fd, USBDEVFS_CONTROL, &ct);
    }

    if (ret < 0) {
        switch(errno) {
        case ETIMEDOUT:
            return USB_RET_NAK;
        default:
            return USB_RET_STALL;
        }
    } else {
        if (intf_update_required) {
#ifdef DEBUG
            printf("usb_host_handle_control: updating interfaces\n");
#endif
            usb_host_update_interfaces(s, value & 0xff);
        }
        return ret;
    }
}

static int usb_host_handle_isoch(USBDevice *dev, USBPacket *p);

static int usb_host_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    struct usbdevfs_bulktransfer bt;
    int ret;
    uint8_t devep = p->devep;

    if (s->endp_table[p->devep - 1].type == USBDEVFS_URB_TYPE_ISO) {
        return usb_host_handle_isoch(dev, p);
    }

    /* XXX: optimize and handle all data types by looking at the
       config descriptor */
    if (p->pid == USB_TOKEN_IN)
        devep |= 0x80;
    bt.ep = devep;
    bt.len = p->len;
    bt.timeout = 50;
    bt.data = p->data;
    ret = ioctl(s->fd, USBDEVFS_BULK, &bt);
    if (ret < 0) {
        switch(errno) {
        case ETIMEDOUT:
            return USB_RET_NAK;
        case EPIPE:
        default:
#ifdef DEBUG
            printf("handle_data: errno=%d\n", errno);
#endif
            return USB_RET_STALL;
        }
    } else {
        return ret;
    }
}

#ifdef USE_ASYNCIO
static void urb_completion_pipe_read(void *opaque)
{
    USBHostDevice *s = opaque;
    USBPacket *p = s->packet;
    PendingURB *pending_urb = NULL;
    struct usbdevfs_urb *purb = NULL;
    int len, ret;

    len = read(s->pipe_fds[0], &pending_urb, sizeof(pending_urb));
    if (len != sizeof(pending_urb)) {
        printf("urb_completion: error reading pending_urb, len=%d\n", len);
        return;
    }

    /* FIXME: handle pending_urb->status */
    del_pending_urb(pending_urb->urb);

    if (!p) {
        s->urbs_ready++;
        return;
    }

    ret = ioctl(s->fd, USBDEVFS_REAPURBNDELAY, &purb);
    if (ret < 0) {
        printf("urb_completion: REAPURBNDELAY ioctl=%d errno=%d\n",
               ret, errno);
        return;
    }

#ifdef DEBUG_ISOCH
    if (purb == pending_urb->urb) {
        printf("urb_completion: urb mismatch reaped=%p pending=%p\n",
               purb, urb);
    }
#endif

    p->len = purb->actual_length;
    usb_packet_complete(p);
    qemu_free(purb);
    s->packet = NULL;
}

static void isoch_done(int signum, siginfo_t *info, void *context)
{
    struct usbdevfs_urb *urb = (struct usbdevfs_urb *)info->si_addr;
    USBHostDevice *s = (USBHostDevice *)urb->usercontext;
    PendingURB *purb;

    if (info->si_code != SI_ASYNCIO ||
        info->si_signo != SIG_ISOCOMPLETE) {
        return;
    }

    purb = get_pending_urb(urb);
    if (purb) {
        purb->status = info->si_errno;
        write(s->pipe_fds[1], &purb, sizeof(purb));
    }
}
#endif

static int usb_host_handle_isoch(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    struct usbdevfs_urb *urb, *purb = NULL;
    int ret;
    uint8_t devep = p->devep;

    if (p->pid == USB_TOKEN_IN)
        devep |= 0x80;

    urb = qemu_mallocz(sizeof(struct usbdevfs_urb) +
                       sizeof(struct usbdevfs_iso_packet_desc));
    if (!urb) {
        printf("usb_host_handle_isoch: malloc failed\n");
        return 0;
    }

    urb->type = USBDEVFS_URB_TYPE_ISO;
    urb->endpoint = devep;
    urb->status = 0;
    urb->flags = USBDEVFS_URB_ISO_ASAP;
    urb->buffer = p->data;
    urb->buffer_length = p->len;
    urb->actual_length = 0;
    urb->start_frame = 0;
    urb->error_count = 0;
#ifdef USE_ASYNCIO
    urb->signr = SIG_ISOCOMPLETE;
#else
    urb->signr = 0;
#endif
    urb->usercontext = s;
    urb->number_of_packets = 1;
    urb->iso_frame_desc[0].length = p->len;
    urb->iso_frame_desc[0].actual_length = 0;
    urb->iso_frame_desc[0].status = 0;
    ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);
    if (ret == 0) {
        if (!add_pending_urb(urb)) {
            printf("usb_host_handle_isoch: add_pending_urb failed %p\n", urb);
        }
    } else {
        printf("usb_host_handle_isoch: SUBMITURB ioctl=%d errno=%d\n",
               ret, errno);
        qemu_free(urb);
        switch(errno) {
        case ETIMEDOUT:
            return USB_RET_NAK;
        case EPIPE:
        default:
            return USB_RET_STALL;
        }
    }
#ifdef USE_ASYNCIO
    /* FIXME: handle urbs_ready together with sync io
     * workaround for injecting the signaled urbs into current frame */
    if (s->urbs_ready > 0) {
        ret = ioctl(s->fd, USBDEVFS_REAPURBNDELAY, &purb);
        if (ret == 0) {
            ret = purb->actual_length;
            qemu_free(purb);
            s->urbs_ready--;
        }
        return ret;
    }
    s->packet = p;
    return USB_RET_ASYNC;
#else
    ret = ioctl(s->fd, USBDEVFS_REAPURBNDELAY, &purb);
    if (ret == 0) {
        if (del_pending_urb(purb)) {
            ret = purb->actual_length;
            qemu_free(purb);
        } else {
            printf("usb_host_handle_isoch: del_pending_urb failed %p\n", purb);
        }
    } else {
#ifdef DEBUG_ISOCH
        printf("usb_host_handle_isoch: REAPURBNDELAY ioctl=%d errno=%d\n",
               ret, errno);
#endif
    }
    return ret;
#endif
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
        printf("invalid descriptor data - configuration\n");
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
                printf("usb_host: malformed endpoint type\n");
                type = USBDEVFS_URB_TYPE_BULK;
            }
            s->endp_table[(devep & 0xf) - 1].type = type;

            i += descriptors[i];
        }
    }
    return 0;
}

static void usb_host_device_check(void *priv)
{
    USBHostDevice *s = priv;
    struct usbdevfs_connectinfo ci;
    int err;

    err = ioctl(s->fd, USBDEVFS_CONNECTINFO, &ci);
    if (err < 0) {
        printf("usb device %d.%d disconnected\n", 0, s->dev.addr);
	usb_device_del_addr(0, s->dev.addr);
	return;
    }

    qemu_mod_timer(s->timer, qemu_get_clock(rt_clock) + 1000);
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

    dev->timer = qemu_new_timer(rt_clock, usb_host_device_check, (void *) dev);
    if (!dev->timer)
	goto fail;

#ifdef DEBUG
    printf("usb_host_device_open %d.%d\n", bus_num, addr);
#endif

    snprintf(buf, sizeof(buf), USBDEVFS_PATH "/%03d/%03d",
             bus_num, addr);
    fd = open(buf, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror(buf);
        goto fail;
    }

    /* read the device description */
    dev->descr_len = read(fd, dev->descr, sizeof(dev->descr));
    if (dev->descr_len <= 0) {
        perror("usb_host_device_open: reading device data failed");
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
    dev->configuration = 1;

    /* XXX - do something about initial configuration */
    if (!usb_host_update_interfaces(dev, -1))
        goto fail;

    ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
    if (ret < 0) {
        perror("usb_host_device_open: USBDEVFS_CONNECTINFO");
        goto fail;
    }

#ifdef DEBUG
    printf("host USB device %d.%d grabbed\n", bus_num, addr);
#endif

    ret = usb_linux_update_endp_table(dev);
    if (ret)
        goto fail;

    if (ci.slow)
        dev->dev.speed = USB_SPEED_LOW;
    else
        dev->dev.speed = USB_SPEED_HIGH;
    dev->dev.handle_packet = usb_generic_handle_packet;

    dev->dev.handle_reset = usb_host_handle_reset;
    dev->dev.handle_control = usb_host_handle_control;
    dev->dev.handle_data = usb_host_handle_data;
    dev->dev.handle_destroy = usb_host_handle_destroy;

    if (!prod_name || prod_name[0] == '\0')
        snprintf(dev->dev.devname, sizeof(dev->dev.devname),
                 "host:%d.%d", bus_num, addr);
    else
        pstrcpy(dev->dev.devname, sizeof(dev->dev.devname),
                prod_name);

#ifdef USE_ASYNCIO
    /* set up the signal handlers */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_sigaction = isoch_done;
    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_restorer = 0;
    ret = sigaction(SIG_ISOCOMPLETE, &sigact, NULL);
    if (ret < 0) {
        perror("usb_host_device_open: sigaction failed");
        goto fail;
    }

    if (pipe(dev->pipe_fds) < 0) {
        perror("usb_host_device_open: pipe creation failed");
        goto fail;
    }
    fcntl(dev->pipe_fds[0], F_SETFL, O_NONBLOCK | O_ASYNC);
    fcntl(dev->pipe_fds[1], F_SETFL, O_NONBLOCK);
    qemu_set_fd_handler(dev->pipe_fds[0], urb_completion_pipe_read, NULL, dev);
#endif

    /* Start the timer to detect disconnect */
    qemu_mod_timer(dev->timer, qemu_get_clock(rt_clock) + 1000);

    hostdev_link(dev);

    dev->urbs_ready = 0;
    return (USBDevice *)dev;

fail:
    if (dev) {
	if (dev->timer)
		qemu_del_timer(dev->timer);
        qemu_free(dev);
    }
    close(fd);
    return NULL;
}

USBDevice *usb_host_device_open(const char *devname)
{
    int bus_num, addr;
    char product_name[PRODUCT_NAME_SZ];

    if (usb_host_find_device(&bus_num, &addr,
                             product_name, sizeof(product_name),
                             devname) < 0)
        return NULL;

     if (hostdev_find(bus_num, addr)) {
        printf("host usb device %d.%d is already open\n", bus_num, addr);
        return NULL;
     }

    return usb_host_device_open_addr(bus_num, addr, product_name);
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
    while (isspace(*p))
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

static int usb_host_scan(void *opaque, USBScanFunc *func)
{
    FILE *f;
    char line[1024];
    char buf[1024];
    int bus_num, addr, speed, device_count, class_id, product_id, vendor_id;
    int ret;
    char product_name[512];

    f = fopen(USBDEVFS_PATH "/devices", "r");
    if (!f) {
        term_printf("Could not open %s\n", USBDEVFS_PATH "/devices");
        return 0;
    }
    device_count = 0;
    bus_num = addr = speed = class_id = product_id = vendor_id = 0;
    ret = 0;
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
    fclose(f);
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
        // printf("Auto match: bus_num %d addr %d vid %d pid %d\n",
	//    bus_num, addr, vendor_id, product_id);

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

        printf("Auto open: bus_num %d addr %d\n", bus_num, addr);

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
 * Add autoconnect filter
 * -1 means 'any' (device, vendor, etc)
 */
static void usb_host_auto_add(int bus_num, int addr, int vendor_id, int product_id)
{
    struct USBAutoFilter *f = qemu_mallocz(sizeof(*f));
    if (!f) {
        printf("Failed to allocate auto filter\n");
	return;
    }

    f->bus_num = bus_num;
    f->addr    = addr;
    f->vendor_id  = vendor_id;
    f->product_id = product_id;

    if (!usb_auto_filter) {
        /*
         * First entry. Init and start the monitor.
         * Right now we're using timer to check for new devices.
         * If this turns out to be too expensive we can move that into a 
         * separate thread.
         */
	usb_auto_timer = qemu_new_timer(rt_clock, usb_host_auto_timer, NULL);
	if (!usb_auto_timer) {
            printf("Failed to allocate timer\n");
            qemu_free(f);
            return;
        }

        /* Check for new devices every two seconds */
        qemu_mod_timer(usb_auto_timer, qemu_get_clock(rt_clock) + 2000);
    }

    printf("Auto filter: bus_num %d addr %d vid %d pid %d\n",
	bus_num, addr, vendor_id, product_id);

    f->next = usb_auto_filter;
    usb_auto_filter = f;
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

        if (*(p + 1) == '*') {
            usb_host_auto_add(*pbus_num, -1, -1, -1);
	    return -1;
	}

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

        if (*(p + 1) == '*') {
            usb_host_auto_add(-1, -1, fs.vendor_id, -1);
	    return -1;
	}

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

void usb_host_info(void)
{
    usb_host_scan(NULL, usb_host_info_device);
}




#else

void usb_host_info(void)
{
    term_printf("USB host devices not supported\n");
}

/* XXX: modify configure to compile the right host driver */
USBDevice *usb_host_device_open(const char *devname)
{
    return NULL;
}

#endif
