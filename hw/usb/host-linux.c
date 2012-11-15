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
#include "trace.h"

#include <dirent.h>
#include <sys/ioctl.h>

#include <linux/usbdevice_fs.h>
#include <linux/version.h>
#include "hw/usb.h"
#include "hw/usb/desc.h"

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

typedef int USBScanFunc(void *opaque, int bus_num, int addr, const char *port,
                        int class_id, int vendor_id, int product_id,
                        const char *product_name, int speed);

//#define DEBUG

#ifdef DEBUG
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

#define PRODUCT_NAME_SZ 32
#define MAX_PORTLEN 16

/* endpoint association data */
#define ISO_FRAME_DESC_PER_URB 32

/* devio.c limits single requests to 16k */
#define MAX_USBFS_BUFFER_SIZE 16384

typedef struct AsyncURB AsyncURB;

struct endp_data {
    uint8_t halted;
    uint8_t iso_started;
    AsyncURB *iso_urb;
    int iso_urb_idx;
    int iso_buffer_used;
    int inflight;
};

struct USBAutoFilter {
    uint32_t bus_num;
    uint32_t addr;
    char     *port;
    uint32_t vendor_id;
    uint32_t product_id;
};

enum USBHostDeviceOptions {
    USB_HOST_OPT_PIPELINE,
};

typedef struct USBHostDevice {
    USBDevice dev;
    int       fd;
    int       hub_fd;
    int       hub_port;

    uint8_t   descr[8192];
    int       descr_len;
    int       closing;
    uint32_t  iso_urb_count;
    uint32_t  options;
    Notifier  exit;
    QEMUBH    *bh;

    struct endp_data ep_in[USB_MAX_ENDPOINTS];
    struct endp_data ep_out[USB_MAX_ENDPOINTS];
    QLIST_HEAD(, AsyncURB) aurbs;

    /* Host side address */
    int bus_num;
    int addr;
    char port[MAX_PORTLEN];
    struct USBAutoFilter match;
    int32_t bootindex;
    int seen, errcount;

    QTAILQ_ENTRY(USBHostDevice) next;
} USBHostDevice;

static QTAILQ_HEAD(, USBHostDevice) hostdevs = QTAILQ_HEAD_INITIALIZER(hostdevs);

static int usb_host_close(USBHostDevice *dev);
static int parse_filter(const char *spec, struct USBAutoFilter *f);
static void usb_host_auto_check(void *unused);
static int usb_host_read_file(char *line, size_t line_size,
                            const char *device_file, const char *device_name);
static void usb_linux_update_endp_table(USBHostDevice *s);

static int usb_host_usbfs_type(USBHostDevice *s, USBPacket *p)
{
    static const int usbfs[] = {
        [USB_ENDPOINT_XFER_CONTROL] = USBDEVFS_URB_TYPE_CONTROL,
        [USB_ENDPOINT_XFER_ISOC]    = USBDEVFS_URB_TYPE_ISO,
        [USB_ENDPOINT_XFER_BULK]    = USBDEVFS_URB_TYPE_BULK,
        [USB_ENDPOINT_XFER_INT]     = USBDEVFS_URB_TYPE_INTERRUPT,
    };
    uint8_t type = p->ep->type;
    assert(type < ARRAY_SIZE(usbfs));
    return usbfs[type];
}

static int usb_host_do_reset(USBHostDevice *dev)
{
    struct timeval s, e;
    uint32_t usecs;
    int ret;

    gettimeofday(&s, NULL);
    ret = ioctl(dev->fd, USBDEVFS_RESET);
    gettimeofday(&e, NULL);
    usecs = (e.tv_sec  - s.tv_sec) * 1000000;
    usecs += e.tv_usec - s.tv_usec;
    if (usecs > 1000000) {
        /* more than a second, something is fishy, broken usb device? */
        fprintf(stderr, "husb: device %d:%d reset took %d.%06d seconds\n",
                dev->bus_num, dev->addr, usecs / 1000000, usecs % 1000000);
    }
    return ret;
}

static struct endp_data *get_endp(USBHostDevice *s, int pid, int ep)
{
    struct endp_data *eps = pid == USB_TOKEN_IN ? s->ep_in : s->ep_out;
    assert(pid == USB_TOKEN_IN || pid == USB_TOKEN_OUT);
    assert(ep > 0 && ep <= USB_MAX_ENDPOINTS);
    return eps + ep - 1;
}

static int is_isoc(USBHostDevice *s, int pid, int ep)
{
    return usb_ep_get_type(&s->dev, pid, ep) == USB_ENDPOINT_XFER_ISOC;
}

static int is_valid(USBHostDevice *s, int pid, int ep)
{
    return usb_ep_get_type(&s->dev, pid, ep) != USB_ENDPOINT_XFER_INVALID;
}

static int is_halted(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->halted;
}

static void clear_halt(USBHostDevice *s, int pid, int ep)
{
    trace_usb_host_ep_clear_halt(s->bus_num, s->addr, ep);
    get_endp(s, pid, ep)->halted = 0;
}

static void set_halt(USBHostDevice *s, int pid, int ep)
{
    if (ep != 0) {
        trace_usb_host_ep_set_halt(s->bus_num, s->addr, ep);
        get_endp(s, pid, ep)->halted = 1;
    }
}

static int is_iso_started(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_started;
}

static void clear_iso_started(USBHostDevice *s, int pid, int ep)
{
    trace_usb_host_iso_stop(s->bus_num, s->addr, ep);
    get_endp(s, pid, ep)->iso_started = 0;
}

static void set_iso_started(USBHostDevice *s, int pid, int ep)
{
    struct endp_data *e = get_endp(s, pid, ep);

    trace_usb_host_iso_start(s->bus_num, s->addr, ep);
    if (!e->iso_started) {
        e->iso_started = 1;
        e->inflight = 0;
    }
}

static int change_iso_inflight(USBHostDevice *s, int pid, int ep, int value)
{
    struct endp_data *e = get_endp(s, pid, ep);

    e->inflight += value;
    return e->inflight;
}

static void set_iso_urb(USBHostDevice *s, int pid, int ep, AsyncURB *iso_urb)
{
    get_endp(s, pid, ep)->iso_urb = iso_urb;
}

static AsyncURB *get_iso_urb(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_urb;
}

static void set_iso_urb_idx(USBHostDevice *s, int pid, int ep, int i)
{
    get_endp(s, pid, ep)->iso_urb_idx = i;
}

static int get_iso_urb_idx(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_urb_idx;
}

static void set_iso_buffer_used(USBHostDevice *s, int pid, int ep, int i)
{
    get_endp(s, pid, ep)->iso_buffer_used = i;
}

static int get_iso_buffer_used(USBHostDevice *s, int pid, int ep)
{
    return get_endp(s, pid, ep)->iso_buffer_used;
}

/*
 * Async URB state.
 * We always allocate iso packet descriptors even for bulk transfers
 * to simplify allocation and casts.
 */
struct AsyncURB
{
    struct usbdevfs_urb urb;
    struct usbdevfs_iso_packet_desc isocpd[ISO_FRAME_DESC_PER_URB];
    USBHostDevice *hdev;
    QLIST_ENTRY(AsyncURB) next;

    /* For regular async urbs */
    USBPacket     *packet;
    int more; /* large transfer, more urbs follow */

    /* For buffered iso handling */
    int iso_frame_idx; /* -1 means in flight */
};

static AsyncURB *async_alloc(USBHostDevice *s)
{
    AsyncURB *aurb = g_malloc0(sizeof(AsyncURB));
    aurb->hdev = s;
    QLIST_INSERT_HEAD(&s->aurbs, aurb, next);
    return aurb;
}

static void async_free(AsyncURB *aurb)
{
    QLIST_REMOVE(aurb, next);
    g_free(aurb);
}

static void do_disconnect(USBHostDevice *s)
{
    usb_host_close(s);
    usb_host_auto_check(NULL);
}

static void async_complete(void *opaque)
{
    USBHostDevice *s = opaque;
    AsyncURB *aurb;
    int urbs = 0;

    while (1) {
        USBPacket *p;

        int r = ioctl(s->fd, USBDEVFS_REAPURBNDELAY, &aurb);
        if (r < 0) {
            if (errno == EAGAIN) {
                if (urbs > 2) {
                    /* indicates possible latency issues */
                    trace_usb_host_iso_many_urbs(s->bus_num, s->addr, urbs);
                }
                return;
            }
            if (errno == ENODEV) {
                if (!s->closing) {
                    trace_usb_host_disconnect(s->bus_num, s->addr);
                    do_disconnect(s);
                }
                return;
            }

            perror("USBDEVFS_REAPURBNDELAY");
            return;
        }

        DPRINTF("husb: async completed. aurb %p status %d alen %d\n",
                aurb, aurb->urb.status, aurb->urb.actual_length);

        /* If this is a buffered iso urb mark it as complete and don't do
           anything else (it is handled further in usb_host_handle_iso_data) */
        if (aurb->iso_frame_idx == -1) {
            int inflight;
            int pid = (aurb->urb.endpoint & USB_DIR_IN) ?
                USB_TOKEN_IN : USB_TOKEN_OUT;
            int ep = aurb->urb.endpoint & 0xf;
            if (aurb->urb.status == -EPIPE) {
                set_halt(s, pid, ep);
            }
            aurb->iso_frame_idx = 0;
            urbs++;
            inflight = change_iso_inflight(s, pid, ep, -1);
            if (inflight == 0 && is_iso_started(s, pid, ep)) {
                /* can be latency issues, or simply end of stream */
                trace_usb_host_iso_out_of_bufs(s->bus_num, s->addr, ep);
            }
            continue;
        }

        p = aurb->packet;
        trace_usb_host_urb_complete(s->bus_num, s->addr, aurb, aurb->urb.status,
                                    aurb->urb.actual_length, aurb->more);

        if (p) {
            switch (aurb->urb.status) {
            case 0:
                p->actual_length += aurb->urb.actual_length;
                if (!aurb->more) {
                    /* Clear previous ASYNC status */
                    p->status = USB_RET_SUCCESS;
                }
                break;

            case -EPIPE:
                set_halt(s, p->pid, p->ep->nr);
                p->status = USB_RET_STALL;
                break;

            case -EOVERFLOW:
                p->status = USB_RET_BABBLE;
                break;

            default:
                p->status = USB_RET_IOERROR;
                break;
            }

            if (aurb->urb.type == USBDEVFS_URB_TYPE_CONTROL) {
                trace_usb_host_req_complete(s->bus_num, s->addr, p,
                                            p->status, aurb->urb.actual_length);
                usb_generic_async_ctrl_complete(&s->dev, p);
            } else if (!aurb->more) {
                trace_usb_host_req_complete(s->bus_num, s->addr, p,
                                            p->status, aurb->urb.actual_length);
                usb_packet_complete(&s->dev, p);
            }
        }

        async_free(aurb);
    }
}

static void usb_host_async_cancel(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);
    AsyncURB *aurb;

    trace_usb_host_req_canceled(s->bus_num, s->addr, p);

    QLIST_FOREACH(aurb, &s->aurbs, next) {
        if (p != aurb->packet) {
            continue;
        }

        trace_usb_host_urb_canceled(s->bus_num, s->addr, aurb);

        /* Mark it as dead (see async_complete above) */
        aurb->packet = NULL;

        int r = ioctl(s->fd, USBDEVFS_DISCARDURB, aurb);
        if (r < 0) {
            DPRINTF("husb: async. discard urb failed errno %d\n", errno);
        }
    }
}

static int usb_host_open_device(int bus, int addr)
{
    const char *usbfs = NULL;
    char filename[32];
    struct stat st;
    int fd, rc;

    rc = stat("/dev/bus/usb", &st);
    if (rc == 0 && S_ISDIR(st.st_mode)) {
        /* udev-created device nodes available */
        usbfs = "/dev/bus/usb";
    } else {
        /* fallback: usbfs mounted below /proc */
        usbfs = "/proc/bus/usb";
    }

    snprintf(filename, sizeof(filename), "%s/%03d/%03d",
             usbfs, bus, addr);
    fd = open(filename, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "husb: open %s: %s\n", filename, strerror(errno));
    }
    return fd;
}

static int usb_host_claim_port(USBHostDevice *s)
{
#ifdef USBDEVFS_CLAIM_PORT
    char *h, hub_name[64], line[1024];
    int hub_addr, ret;

    snprintf(hub_name, sizeof(hub_name), "%d-%s",
             s->match.bus_num, s->match.port);

    /* try strip off last ".$portnr" to get hub */
    h = strrchr(hub_name, '.');
    if (h != NULL) {
        s->hub_port = atoi(h+1);
        *h = '\0';
    } else {
        /* no dot in there -> it is the root hub */
        snprintf(hub_name, sizeof(hub_name), "usb%d",
                 s->match.bus_num);
        s->hub_port = atoi(s->match.port);
    }

    if (!usb_host_read_file(line, sizeof(line), "devnum",
                            hub_name)) {
        return -1;
    }
    if (sscanf(line, "%d", &hub_addr) != 1) {
        return -1;
    }

    s->hub_fd = usb_host_open_device(s->match.bus_num, hub_addr);
    if (s->hub_fd < 0) {
        return -1;
    }

    ret = ioctl(s->hub_fd, USBDEVFS_CLAIM_PORT, &s->hub_port);
    if (ret < 0) {
        close(s->hub_fd);
        s->hub_fd = -1;
        return -1;
    }

    trace_usb_host_claim_port(s->match.bus_num, hub_addr, s->hub_port);
    return 0;
#else
    return -1;
#endif
}

static void usb_host_release_port(USBHostDevice *s)
{
    if (s->hub_fd == -1) {
        return;
    }
#ifdef USBDEVFS_RELEASE_PORT
    ioctl(s->hub_fd, USBDEVFS_RELEASE_PORT, &s->hub_port);
#endif
    close(s->hub_fd);
    s->hub_fd = -1;
}

static int usb_host_disconnect_ifaces(USBHostDevice *dev, int nb_interfaces)
{
    /* earlier Linux 2.4 do not support that */
#ifdef USBDEVFS_DISCONNECT
    struct usbdevfs_ioctl ctrl;
    int ret, interface;

    for (interface = 0; interface < nb_interfaces; interface++) {
        ctrl.ioctl_code = USBDEVFS_DISCONNECT;
        ctrl.ifno = interface;
        ctrl.data = 0;
        ret = ioctl(dev->fd, USBDEVFS_IOCTL, &ctrl);
        if (ret < 0 && errno != ENODATA) {
            perror("USBDEVFS_DISCONNECT");
            return -1;
        }
    }
#endif
    return 0;
}

static int usb_linux_get_num_interfaces(USBHostDevice *s)
{
    char device_name[64], line[1024];
    int num_interfaces = 0;

    sprintf(device_name, "%d-%s", s->bus_num, s->port);
    if (!usb_host_read_file(line, sizeof(line), "bNumInterfaces",
                            device_name)) {
        return -1;
    }
    if (sscanf(line, "%d", &num_interfaces) != 1) {
        return -1;
    }
    return num_interfaces;
}

static int usb_host_claim_interfaces(USBHostDevice *dev, int configuration)
{
    const char *op = NULL;
    int dev_descr_len, config_descr_len;
    int interface, nb_interfaces;
    int ret, i;

    for (i = 0; i < USB_MAX_INTERFACES; i++) {
        dev->dev.altsetting[i] = 0;
    }

    if (configuration == 0) { /* address state - ignore */
        dev->dev.ninterfaces   = 0;
        dev->dev.configuration = 0;
        return 1;
    }

    DPRINTF("husb: claiming interfaces. config %d\n", configuration);

    i = 0;
    dev_descr_len = dev->descr[0];
    if (dev_descr_len > dev->descr_len) {
        fprintf(stderr, "husb: update iface failed. descr too short\n");
        return 0;
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

        DPRINTF("husb: config #%d need %d\n", dev->descr[i + 5], configuration);

        if (configuration == dev->descr[i + 5]) {
            configuration = dev->descr[i + 5];
            break;
        }

        i += config_descr_len;
    }

    if (i >= dev->descr_len) {
        fprintf(stderr,
                "husb: update iface failed. no matching configuration\n");
        return 0;
    }
    nb_interfaces = dev->descr[i + 4];

    if (usb_host_disconnect_ifaces(dev, nb_interfaces) < 0) {
        goto fail;
    }

    /* XXX: only grab if all interfaces are free */
    for (interface = 0; interface < nb_interfaces; interface++) {
        op = "USBDEVFS_CLAIMINTERFACE";
        ret = ioctl(dev->fd, USBDEVFS_CLAIMINTERFACE, &interface);
        if (ret < 0) {
            goto fail;
        }
    }

    trace_usb_host_claim_interfaces(dev->bus_num, dev->addr,
                                    nb_interfaces, configuration);

    dev->dev.ninterfaces   = nb_interfaces;
    dev->dev.configuration = configuration;
    return 1;

fail:
    if (errno == ENODEV) {
        do_disconnect(dev);
    }
    perror(op);
    return 0;
}

static int usb_host_release_interfaces(USBHostDevice *s)
{
    int ret, i;

    trace_usb_host_release_interfaces(s->bus_num, s->addr);

    for (i = 0; i < s->dev.ninterfaces; i++) {
        ret = ioctl(s->fd, USBDEVFS_RELEASEINTERFACE, &i);
        if (ret < 0) {
            perror("USBDEVFS_RELEASEINTERFACE");
            return 0;
        }
    }
    return 1;
}

static void usb_host_handle_reset(USBDevice *dev)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);

    trace_usb_host_reset(s->bus_num, s->addr);

    usb_host_do_reset(s);;

    usb_host_claim_interfaces(s, 0);
    usb_linux_update_endp_table(s);
}

static void usb_host_handle_destroy(USBDevice *dev)
{
    USBHostDevice *s = (USBHostDevice *)dev;

    usb_host_release_port(s);
    usb_host_close(s);
    QTAILQ_REMOVE(&hostdevs, s, next);
    qemu_remove_exit_notifier(&s->exit);
}

/* iso data is special, we need to keep enough urbs in flight to make sure
   that the controller never runs out of them, otherwise the device will
   likely suffer a buffer underrun / overrun. */
static AsyncURB *usb_host_alloc_iso(USBHostDevice *s, int pid, uint8_t ep)
{
    AsyncURB *aurb;
    int i, j, len = usb_ep_get_max_packet_size(&s->dev, pid, ep);

    aurb = g_malloc0(s->iso_urb_count * sizeof(*aurb));
    for (i = 0; i < s->iso_urb_count; i++) {
        aurb[i].urb.endpoint      = ep;
        aurb[i].urb.buffer_length = ISO_FRAME_DESC_PER_URB * len;
        aurb[i].urb.buffer        = g_malloc(aurb[i].urb.buffer_length);
        aurb[i].urb.type          = USBDEVFS_URB_TYPE_ISO;
        aurb[i].urb.flags         = USBDEVFS_URB_ISO_ASAP;
        aurb[i].urb.number_of_packets = ISO_FRAME_DESC_PER_URB;
        for (j = 0 ; j < ISO_FRAME_DESC_PER_URB; j++)
            aurb[i].urb.iso_frame_desc[j].length = len;
        if (pid == USB_TOKEN_IN) {
            aurb[i].urb.endpoint |= 0x80;
            /* Mark as fully consumed (idle) */
            aurb[i].iso_frame_idx = ISO_FRAME_DESC_PER_URB;
        }
    }
    set_iso_urb(s, pid, ep, aurb);

    return aurb;
}

static void usb_host_stop_n_free_iso(USBHostDevice *s, int pid, uint8_t ep)
{
    AsyncURB *aurb;
    int i, ret, killed = 0, free = 1;

    aurb = get_iso_urb(s, pid, ep);
    if (!aurb) {
        return;
    }

    for (i = 0; i < s->iso_urb_count; i++) {
        /* in flight? */
        if (aurb[i].iso_frame_idx == -1) {
            ret = ioctl(s->fd, USBDEVFS_DISCARDURB, &aurb[i]);
            if (ret < 0) {
                perror("USBDEVFS_DISCARDURB");
                free = 0;
                continue;
            }
            killed++;
        }
    }

    /* Make sure any urbs we've killed are reaped before we free them */
    if (killed) {
        async_complete(s);
    }

    for (i = 0; i < s->iso_urb_count; i++) {
        g_free(aurb[i].urb.buffer);
    }

    if (free)
        g_free(aurb);
    else
        printf("husb: leaking iso urbs because of discard failure\n");
    set_iso_urb(s, pid, ep, NULL);
    set_iso_urb_idx(s, pid, ep, 0);
    clear_iso_started(s, pid, ep);
}

static void urb_status_to_usb_ret(int status, USBPacket *p)
{
    switch (status) {
    case -EPIPE:
        p->status = USB_RET_STALL;
        break;
    case -EOVERFLOW:
        p->status = USB_RET_BABBLE;
        break;
    default:
        p->status = USB_RET_IOERROR;
    }
}

static void usb_host_handle_iso_data(USBHostDevice *s, USBPacket *p, int in)
{
    AsyncURB *aurb;
    int i, j, max_packet_size, offset, len;
    uint8_t *buf;

    max_packet_size = p->ep->max_packet_size;
    if (max_packet_size == 0) {
        p->status = USB_RET_NAK;
        return;
    }

    aurb = get_iso_urb(s, p->pid, p->ep->nr);
    if (!aurb) {
        aurb = usb_host_alloc_iso(s, p->pid, p->ep->nr);
    }

    i = get_iso_urb_idx(s, p->pid, p->ep->nr);
    j = aurb[i].iso_frame_idx;
    if (j >= 0 && j < ISO_FRAME_DESC_PER_URB) {
        if (in) {
            /* Check urb status  */
            if (aurb[i].urb.status) {
                urb_status_to_usb_ret(aurb[i].urb.status, p);
                /* Move to the next urb */
                aurb[i].iso_frame_idx = ISO_FRAME_DESC_PER_URB - 1;
            /* Check frame status */
            } else if (aurb[i].urb.iso_frame_desc[j].status) {
                urb_status_to_usb_ret(aurb[i].urb.iso_frame_desc[j].status, p);
            /* Check the frame fits */
            } else if (aurb[i].urb.iso_frame_desc[j].actual_length
                       > p->iov.size) {
                printf("husb: received iso data is larger then packet\n");
                p->status = USB_RET_BABBLE;
            /* All good copy data over */
            } else {
                len = aurb[i].urb.iso_frame_desc[j].actual_length;
                buf  = aurb[i].urb.buffer +
                    j * aurb[i].urb.iso_frame_desc[0].length;
                usb_packet_copy(p, buf, len);
            }
        } else {
            len = p->iov.size;
            offset = (j == 0) ? 0 : get_iso_buffer_used(s, p->pid, p->ep->nr);

            /* Check the frame fits */
            if (len > max_packet_size) {
                printf("husb: send iso data is larger then max packet size\n");
                p->status = USB_RET_NAK;
                return;
            }

            /* All good copy data over */
            usb_packet_copy(p, aurb[i].urb.buffer + offset, len);
            aurb[i].urb.iso_frame_desc[j].length = len;
            offset += len;
            set_iso_buffer_used(s, p->pid, p->ep->nr, offset);

            /* Start the stream once we have buffered enough data */
            if (!is_iso_started(s, p->pid, p->ep->nr) && i == 1 && j == 8) {
                set_iso_started(s, p->pid, p->ep->nr);
            }
        }
        aurb[i].iso_frame_idx++;
        if (aurb[i].iso_frame_idx == ISO_FRAME_DESC_PER_URB) {
            i = (i + 1) % s->iso_urb_count;
            set_iso_urb_idx(s, p->pid, p->ep->nr, i);
        }
    } else {
        if (in) {
            set_iso_started(s, p->pid, p->ep->nr);
        } else {
            DPRINTF("hubs: iso out error no free buffer, dropping packet\n");
        }
    }

    if (is_iso_started(s, p->pid, p->ep->nr)) {
        /* (Re)-submit all fully consumed / filled urbs */
        for (i = 0; i < s->iso_urb_count; i++) {
            if (aurb[i].iso_frame_idx == ISO_FRAME_DESC_PER_URB) {
                if (ioctl(s->fd, USBDEVFS_SUBMITURB, &aurb[i]) < 0) {
                    perror("USBDEVFS_SUBMITURB");
                    if (!in || p->status == USB_RET_SUCCESS) {
                        switch(errno) {
                        case ETIMEDOUT:
                            p->status = USB_RET_NAK;
                            break;
                        case EPIPE:
                        default:
                            p->status = USB_RET_STALL;
                        }
                    }
                    break;
                }
                aurb[i].iso_frame_idx = -1;
                change_iso_inflight(s, p->pid, p->ep->nr, 1);
            }
        }
    }
}

static void usb_host_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret, rem, prem, v;
    uint8_t *pbuf;
    uint8_t ep;

    trace_usb_host_req_data(s->bus_num, s->addr, p,
                            p->pid == USB_TOKEN_IN,
                            p->ep->nr, p->iov.size);

    if (!is_valid(s, p->pid, p->ep->nr)) {
        p->status = USB_RET_NAK;
        trace_usb_host_req_complete(s->bus_num, s->addr, p,
                                    p->status, p->actual_length);
        return;
    }

    if (p->pid == USB_TOKEN_IN) {
        ep = p->ep->nr | 0x80;
    } else {
        ep = p->ep->nr;
    }

    if (is_halted(s, p->pid, p->ep->nr)) {
        unsigned int arg = ep;
        ret = ioctl(s->fd, USBDEVFS_CLEAR_HALT, &arg);
        if (ret < 0) {
            perror("USBDEVFS_CLEAR_HALT");
            p->status = USB_RET_NAK;
            trace_usb_host_req_complete(s->bus_num, s->addr, p,
                                        p->status, p->actual_length);
            return;
        }
        clear_halt(s, p->pid, p->ep->nr);
    }

    if (is_isoc(s, p->pid, p->ep->nr)) {
        usb_host_handle_iso_data(s, p, p->pid == USB_TOKEN_IN);
        return;
    }

    v = 0;
    prem = 0;
    pbuf = NULL;
    rem = p->iov.size;
    do {
        if (prem == 0 && rem > 0) {
            assert(v < p->iov.niov);
            prem = p->iov.iov[v].iov_len;
            pbuf = p->iov.iov[v].iov_base;
            assert(prem <= rem);
            v++;
        }
        aurb = async_alloc(s);
        aurb->packet = p;

        urb = &aurb->urb;
        urb->endpoint      = ep;
        urb->type          = usb_host_usbfs_type(s, p);
        urb->usercontext   = s;
        urb->buffer        = pbuf;
        urb->buffer_length = prem;

        if (urb->buffer_length > MAX_USBFS_BUFFER_SIZE) {
            urb->buffer_length = MAX_USBFS_BUFFER_SIZE;
        }
        pbuf += urb->buffer_length;
        prem -= urb->buffer_length;
        rem  -= urb->buffer_length;
        if (rem) {
            aurb->more         = 1;
        }

        trace_usb_host_urb_submit(s->bus_num, s->addr, aurb,
                                  urb->buffer_length, aurb->more);
        ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);

        DPRINTF("husb: data submit: ep 0x%x, len %u, more %d, packet %p, aurb %p\n",
                urb->endpoint, urb->buffer_length, aurb->more, p, aurb);

        if (ret < 0) {
            perror("USBDEVFS_SUBMITURB");
            async_free(aurb);

            switch(errno) {
            case ETIMEDOUT:
                p->status = USB_RET_NAK;
                trace_usb_host_req_complete(s->bus_num, s->addr, p,
                                            p->status, p->actual_length);
                break;
            case EPIPE:
            default:
                p->status = USB_RET_STALL;
                trace_usb_host_req_complete(s->bus_num, s->addr, p,
                                            p->status, p->actual_length);
            }
            return;
        }
    } while (rem > 0);

    p->status = USB_RET_ASYNC;
}

static int ctrl_error(void)
{
    if (errno == ETIMEDOUT) {
        return USB_RET_NAK;
    } else {
        return USB_RET_STALL;
    }
}

static void usb_host_set_address(USBHostDevice *s, int addr)
{
    trace_usb_host_set_address(s->bus_num, s->addr, addr);
    s->dev.addr = addr;
}

static void usb_host_set_config(USBHostDevice *s, int config, USBPacket *p)
{
    int ret, first = 1;

    trace_usb_host_set_config(s->bus_num, s->addr, config);

    usb_host_release_interfaces(s);

again:
    ret = ioctl(s->fd, USBDEVFS_SETCONFIGURATION, &config);

    DPRINTF("husb: ctrl set config %d ret %d errno %d\n", config, ret, errno);

    if (ret < 0 && errno == EBUSY && first) {
        /* happens if usb device is in use by host drivers */
        int count = usb_linux_get_num_interfaces(s);
        if (count > 0) {
            DPRINTF("husb: busy -> disconnecting %d interfaces\n", count);
            usb_host_disconnect_ifaces(s, count);
            first = 0;
            goto again;
        }
    }

    if (ret < 0) {
        p->status = ctrl_error();
        return;
    }
    usb_host_claim_interfaces(s, config);
    usb_linux_update_endp_table(s);
}

static void usb_host_set_interface(USBHostDevice *s, int iface, int alt,
                                   USBPacket *p)
{
    struct usbdevfs_setinterface si;
    int i, ret;

    trace_usb_host_set_interface(s->bus_num, s->addr, iface, alt);

    for (i = 1; i <= USB_MAX_ENDPOINTS; i++) {
        if (is_isoc(s, USB_TOKEN_IN, i)) {
            usb_host_stop_n_free_iso(s, USB_TOKEN_IN, i);
        }
        if (is_isoc(s, USB_TOKEN_OUT, i)) {
            usb_host_stop_n_free_iso(s, USB_TOKEN_OUT, i);
        }
    }

    if (iface >= USB_MAX_INTERFACES) {
        p->status = USB_RET_STALL;
        return;
    }

    si.interface  = iface;
    si.altsetting = alt;
    ret = ioctl(s->fd, USBDEVFS_SETINTERFACE, &si);

    DPRINTF("husb: ctrl set iface %d altset %d ret %d errno %d\n",
            iface, alt, ret, errno);

    if (ret < 0) {
        p->status = ctrl_error();
        return;
    }

    s->dev.altsetting[iface] = alt;
    usb_linux_update_endp_table(s);
}

static void usb_host_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);
    struct usbdevfs_urb *urb;
    AsyncURB *aurb;
    int ret;

    /*
     * Process certain standard device requests.
     * These are infrequent and are processed synchronously.
     */

    /* Note request is (bRequestType << 8) | bRequest */
    trace_usb_host_req_control(s->bus_num, s->addr, p, request, value, index);

    switch (request) {
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        usb_host_set_address(s, value);
        trace_usb_host_req_emulated(s->bus_num, s->addr, p, p->status);
        return;

    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        usb_host_set_config(s, value & 0xff, p);
        trace_usb_host_req_emulated(s->bus_num, s->addr, p, p->status);
        return;

    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        usb_host_set_interface(s, index, value, p);
        trace_usb_host_req_emulated(s->bus_num, s->addr, p, p->status);
        return;

    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value == 0) { /* clear halt */
            int pid = (index & USB_DIR_IN) ? USB_TOKEN_IN : USB_TOKEN_OUT;
            ioctl(s->fd, USBDEVFS_CLEAR_HALT, &index);
            clear_halt(s, pid, index & 0x0f);
            trace_usb_host_req_emulated(s->bus_num, s->addr, p, 0);
            return;
        }
    }

    /* The rest are asynchronous */
    if (length > sizeof(dev->data_buf)) {
        fprintf(stderr, "husb: ctrl buffer too small (%d > %zu)\n",
                length, sizeof(dev->data_buf));
        p->status = USB_RET_STALL;
        return;
    }

    aurb = async_alloc(s);
    aurb->packet = p;

    /*
     * Setup ctrl transfer.
     *
     * s->ctrl is laid out such that data buffer immediately follows
     * 'req' struct which is exactly what usbdevfs expects.
     */
    urb = &aurb->urb;

    urb->type     = USBDEVFS_URB_TYPE_CONTROL;
    urb->endpoint = p->ep->nr;

    urb->buffer        = &dev->setup_buf;
    urb->buffer_length = length + 8;

    urb->usercontext = s;

    trace_usb_host_urb_submit(s->bus_num, s->addr, aurb,
                              urb->buffer_length, aurb->more);
    ret = ioctl(s->fd, USBDEVFS_SUBMITURB, urb);

    DPRINTF("husb: submit ctrl. len %u aurb %p\n", urb->buffer_length, aurb);

    if (ret < 0) {
        DPRINTF("husb: submit failed. errno %d\n", errno);
        async_free(aurb);

        switch(errno) {
        case ETIMEDOUT:
            p->status = USB_RET_NAK;
            break;
        case EPIPE:
        default:
            p->status = USB_RET_STALL;
            break;
        }
        return;
    }

    p->status = USB_RET_ASYNC;
}

static void usb_linux_update_endp_table(USBHostDevice *s)
{
    static const char *tname[] = {
        [USB_ENDPOINT_XFER_CONTROL] = "control",
        [USB_ENDPOINT_XFER_ISOC]    = "isoc",
        [USB_ENDPOINT_XFER_BULK]    = "bulk",
        [USB_ENDPOINT_XFER_INT]     = "int",
    };
    uint8_t devep, type;
    uint16_t mps, v, p;
    int ep, pid;
    unsigned int i, configuration = -1, interface = -1, altsetting = -1;
    struct endp_data *epd;
    USBDescriptor *d;
    bool active = false;

    usb_ep_reset(&s->dev);

    for (i = 0;; i += d->bLength) {
        if (i+2 >= s->descr_len) {
            break;
        }
        d = (void *)(s->descr + i);
        if (d->bLength < 2) {
            trace_usb_host_parse_error(s->bus_num, s->addr,
                                       "descriptor too short");
            return;
        }
        if (i + d->bLength > s->descr_len) {
            trace_usb_host_parse_error(s->bus_num, s->addr,
                                       "descriptor too long");
            return;
        }
        switch (d->bDescriptorType) {
        case 0:
            trace_usb_host_parse_error(s->bus_num, s->addr,
                                       "invalid descriptor type");
            return;
        case USB_DT_DEVICE:
            if (d->bLength < 0x12) {
                trace_usb_host_parse_error(s->bus_num, s->addr,
                                           "device descriptor too short");
                return;
            }
            v = (d->u.device.idVendor_hi << 8) | d->u.device.idVendor_lo;
            p = (d->u.device.idProduct_hi << 8) | d->u.device.idProduct_lo;
            trace_usb_host_parse_device(s->bus_num, s->addr, v, p);
            break;
        case USB_DT_CONFIG:
            if (d->bLength < 0x09) {
                trace_usb_host_parse_error(s->bus_num, s->addr,
                                           "config descriptor too short");
                return;
            }
            configuration = d->u.config.bConfigurationValue;
            active = (configuration == s->dev.configuration);
            trace_usb_host_parse_config(s->bus_num, s->addr,
                                        configuration, active);
            break;
        case USB_DT_INTERFACE:
            if (d->bLength < 0x09) {
                trace_usb_host_parse_error(s->bus_num, s->addr,
                                           "interface descriptor too short");
                return;
            }
            interface = d->u.interface.bInterfaceNumber;
            altsetting = d->u.interface.bAlternateSetting;
            active = (configuration == s->dev.configuration) &&
                (altsetting == s->dev.altsetting[interface]);
            trace_usb_host_parse_interface(s->bus_num, s->addr,
                                           interface, altsetting, active);
            break;
        case USB_DT_ENDPOINT:
            if (d->bLength < 0x07) {
                trace_usb_host_parse_error(s->bus_num, s->addr,
                                           "endpoint descriptor too short");
                return;
            }
            devep = d->u.endpoint.bEndpointAddress;
            pid = (devep & USB_DIR_IN) ? USB_TOKEN_IN : USB_TOKEN_OUT;
            ep = devep & 0xf;
            if (ep == 0) {
                trace_usb_host_parse_error(s->bus_num, s->addr,
                                           "invalid endpoint address");
                return;
            }

            type = d->u.endpoint.bmAttributes & 0x3;
            mps = d->u.endpoint.wMaxPacketSize_lo |
                (d->u.endpoint.wMaxPacketSize_hi << 8);
            trace_usb_host_parse_endpoint(s->bus_num, s->addr, ep,
                                          (devep & USB_DIR_IN) ? "in" : "out",
                                          tname[type], active);

            if (active) {
                usb_ep_set_max_packet_size(&s->dev, pid, ep, mps);
                assert(usb_ep_get_type(&s->dev, pid, ep) ==
                       USB_ENDPOINT_XFER_INVALID);
                usb_ep_set_type(&s->dev, pid, ep, type);
                usb_ep_set_ifnum(&s->dev, pid, ep, interface);
                if ((s->options & (1 << USB_HOST_OPT_PIPELINE)) &&
                    (type == USB_ENDPOINT_XFER_BULK) &&
                    (pid == USB_TOKEN_OUT)) {
                    usb_ep_set_pipeline(&s->dev, pid, ep, true);
                }

                epd = get_endp(s, pid, ep);
                epd->halted = 0;
            }

            break;
        default:
            trace_usb_host_parse_unknown(s->bus_num, s->addr,
                                         d->bLength, d->bDescriptorType);
            break;
        }
    }
}

/*
 * Check if we can safely redirect a usb2 device to a usb1 virtual controller,
 * this function assumes this is safe, if:
 * 1) There are no isoc endpoints
 * 2) There are no interrupt endpoints with a max_packet_size > 64
 * Note bulk endpoints with a max_packet_size > 64 in theory also are not
 * usb1 compatible, but in practice this seems to work fine.
 */
static int usb_linux_full_speed_compat(USBHostDevice *dev)
{
    int i, packet_size;

    /*
     * usb_linux_update_endp_table only registers info about ep in the current
     * interface altsettings, so we need to parse the descriptors again.
     */
    for (i = 0; (i + 5) < dev->descr_len; i += dev->descr[i]) {
        if (dev->descr[i + 1] == USB_DT_ENDPOINT) {
            switch (dev->descr[i + 3] & 0x3) {
            case 0x00: /* CONTROL */
                break;
            case 0x01: /* ISO */
                return 0;
            case 0x02: /* BULK */
                break;
            case 0x03: /* INTERRUPT */
                packet_size = dev->descr[i + 4] + (dev->descr[i + 5] << 8);
                if (packet_size > 64)
                    return 0;
                break;
            }
        }
    }
    return 1;
}

static int usb_host_open(USBHostDevice *dev, int bus_num,
                         int addr, const char *port,
                         const char *prod_name, int speed)
{
    int fd = -1, ret;

    trace_usb_host_open_started(bus_num, addr);

    if (dev->fd != -1) {
        goto fail;
    }

    fd = usb_host_open_device(bus_num, addr);
    if (fd < 0) {
        goto fail;
    }
    DPRINTF("husb: opened %s\n", buf);

    dev->bus_num = bus_num;
    dev->addr = addr;
    strcpy(dev->port, port);
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


    /* start unconfigured -- we'll wait for the guest to set a configuration */
    if (!usb_host_claim_interfaces(dev, 0)) {
        goto fail;
    }

    usb_ep_init(&dev->dev);
    usb_linux_update_endp_table(dev);

    if (speed == -1) {
        struct usbdevfs_connectinfo ci;

        ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
        if (ret < 0) {
            perror("usb_host_device_open: USBDEVFS_CONNECTINFO");
            goto fail;
        }

        if (ci.slow) {
            speed = USB_SPEED_LOW;
        } else {
            speed = USB_SPEED_HIGH;
        }
    }
    dev->dev.speed = speed;
    dev->dev.speedmask = (1 << speed);
    if (dev->dev.speed == USB_SPEED_HIGH && usb_linux_full_speed_compat(dev)) {
        dev->dev.speedmask |= USB_SPEED_MASK_FULL;
    }

    trace_usb_host_open_success(bus_num, addr);

    if (!prod_name || prod_name[0] == '\0') {
        snprintf(dev->dev.product_desc, sizeof(dev->dev.product_desc),
                 "host:%d.%d", bus_num, addr);
    } else {
        pstrcpy(dev->dev.product_desc, sizeof(dev->dev.product_desc),
                prod_name);
    }

    ret = usb_device_attach(&dev->dev);
    if (ret) {
        goto fail;
    }

    /* USB devio uses 'write' flag to check for async completions */
    qemu_set_fd_handler(dev->fd, NULL, async_complete, dev);

    return 0;

fail:
    trace_usb_host_open_failure(bus_num, addr);
    if (dev->fd != -1) {
        close(dev->fd);
        dev->fd = -1;
    }
    return -1;
}

static int usb_host_close(USBHostDevice *dev)
{
    int i;

    if (dev->fd == -1) {
        return -1;
    }

    trace_usb_host_close(dev->bus_num, dev->addr);

    qemu_set_fd_handler(dev->fd, NULL, NULL, NULL);
    dev->closing = 1;
    for (i = 1; i <= USB_MAX_ENDPOINTS; i++) {
        if (is_isoc(dev, USB_TOKEN_IN, i)) {
            usb_host_stop_n_free_iso(dev, USB_TOKEN_IN, i);
        }
        if (is_isoc(dev, USB_TOKEN_OUT, i)) {
            usb_host_stop_n_free_iso(dev, USB_TOKEN_OUT, i);
        }
    }
    async_complete(dev);
    dev->closing = 0;
    if (dev->dev.attached) {
        usb_device_detach(&dev->dev);
    }
    usb_host_do_reset(dev);
    close(dev->fd);
    dev->fd = -1;
    return 0;
}

static void usb_host_exit_notifier(struct Notifier *n, void *data)
{
    USBHostDevice *s = container_of(n, USBHostDevice, exit);

    usb_host_release_port(s);
    if (s->fd != -1) {
        usb_host_do_reset(s);;
    }
}

/*
 * This is *NOT* about restoring state.  We have absolutely no idea
 * what state the host device is in at the moment and whenever it is
 * still present in the first place.  Attemping to contine where we
 * left off is impossible.
 *
 * What we are going to to to here is emulate a surprise removal of
 * the usb device passed through, then kick host scan so the device
 * will get re-attached (and re-initialized by the guest) in case it
 * is still present.
 *
 * As the device removal will change the state of other devices (usb
 * host controller, most likely interrupt controller too) we have to
 * wait with it until *all* vmstate is loaded.  Thus post_load just
 * kicks a bottom half which then does the actual work.
 */
static void usb_host_post_load_bh(void *opaque)
{
    USBHostDevice *dev = opaque;

    if (dev->fd != -1) {
        usb_host_close(dev);
    }
    if (dev->dev.attached) {
        usb_device_detach(&dev->dev);
    }
    usb_host_auto_check(NULL);
}

static int usb_host_post_load(void *opaque, int version_id)
{
    USBHostDevice *dev = opaque;

    qemu_bh_schedule(dev->bh);
    return 0;
}

static int usb_host_initfn(USBDevice *dev)
{
    USBHostDevice *s = DO_UPCAST(USBHostDevice, dev, dev);

    dev->auto_attach = 0;
    s->fd = -1;
    s->hub_fd = -1;

    QTAILQ_INSERT_TAIL(&hostdevs, s, next);
    s->exit.notify = usb_host_exit_notifier;
    qemu_add_exit_notifier(&s->exit);
    s->bh = qemu_bh_new(usb_host_post_load_bh, s);
    usb_host_auto_check(NULL);

    if (s->match.bus_num != 0 && s->match.port != NULL) {
        usb_host_claim_port(s);
    }
    add_boot_device_path(s->bootindex, &dev->qdev, NULL);
    return 0;
}

static const VMStateDescription vmstate_usb_host = {
    .name = "usb-host",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = usb_host_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBHostDevice),
        VMSTATE_END_OF_LIST()
    }
};

static Property usb_host_dev_properties[] = {
    DEFINE_PROP_UINT32("hostbus",  USBHostDevice, match.bus_num,    0),
    DEFINE_PROP_UINT32("hostaddr", USBHostDevice, match.addr,       0),
    DEFINE_PROP_STRING("hostport", USBHostDevice, match.port),
    DEFINE_PROP_HEX32("vendorid",  USBHostDevice, match.vendor_id,  0),
    DEFINE_PROP_HEX32("productid", USBHostDevice, match.product_id, 0),
    DEFINE_PROP_UINT32("isobufs",  USBHostDevice, iso_urb_count,    4),
    DEFINE_PROP_INT32("bootindex", USBHostDevice, bootindex,        -1),
    DEFINE_PROP_BIT("pipeline",    USBHostDevice, options,
                    USB_HOST_OPT_PIPELINE, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_host_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->init           = usb_host_initfn;
    uc->product_desc   = "USB Host Device";
    uc->cancel_packet  = usb_host_async_cancel;
    uc->handle_data    = usb_host_handle_data;
    uc->handle_control = usb_host_handle_control;
    uc->handle_reset   = usb_host_handle_reset;
    uc->handle_destroy = usb_host_handle_destroy;
    dc->vmsd = &vmstate_usb_host;
    dc->props = usb_host_dev_properties;
}

static TypeInfo usb_host_dev_info = {
    .name          = "usb-host",
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBHostDevice),
    .class_init    = usb_host_class_initfn,
};

static void usb_host_register_types(void)
{
    type_register_static(&usb_host_dev_info);
    usb_legacy_register("usb-host", "host", usb_host_device_open);
}

type_init(usb_host_register_types)

USBDevice *usb_host_device_open(USBBus *bus, const char *devname)
{
    struct USBAutoFilter filter;
    USBDevice *dev;
    char *p;

    dev = usb_create(bus, "usb-host");

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

    snprintf(filename, PATH_MAX, "/sys/bus/usb/devices/%s/%s", device_name,
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
static int usb_host_scan(void *opaque, USBScanFunc *func)
{
    DIR *dir = NULL;
    char line[1024];
    int bus_num, addr, speed, class_id, product_id, vendor_id;
    int ret = 0;
    char port[MAX_PORTLEN];
    char product_name[512];
    struct dirent *de;

    dir = opendir("/sys/bus/usb/devices");
    if (!dir) {
        perror("husb: opendir /sys/bus/usb/devices");
        fprintf(stderr, "husb: please make sure sysfs is mounted at /sys\n");
        goto the_end;
    }

    while ((de = readdir(dir))) {
        if (de->d_name[0] != '.' && !strchr(de->d_name, ':')) {
            if (sscanf(de->d_name, "%d-%7[0-9.]", &bus_num, port) < 2) {
                continue;
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
            if (!strcmp(line, "5000\n")) {
                speed = USB_SPEED_SUPER;
            } else if (!strcmp(line, "480\n")) {
                speed = USB_SPEED_HIGH;
            } else if (!strcmp(line, "1.5\n")) {
                speed = USB_SPEED_LOW;
            } else {
                speed = USB_SPEED_FULL;
            }

            ret = func(opaque, bus_num, addr, port, class_id, vendor_id,
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

static QEMUTimer *usb_auto_timer;
static VMChangeStateEntry *usb_vmstate;

static int usb_host_auto_scan(void *opaque, int bus_num,
                              int addr, const char *port,
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
        if (f->port != NULL && (port == NULL || strcmp(f->port, port) != 0)) {
            continue;
        }

        if (f->vendor_id > 0 && f->vendor_id != vendor_id) {
            continue;
        }

        if (f->product_id > 0 && f->product_id != product_id) {
            continue;
        }
        /* We got a match */
        s->seen++;
        if (s->errcount >= 3) {
            return 0;
        }

        /* Already attached ? */
        if (s->fd != -1) {
            return 0;
        }
        DPRINTF("husb: auto open: bus_num %d addr %d\n", bus_num, addr);

        if (usb_host_open(s, bus_num, addr, port, product_name, speed) < 0) {
            s->errcount++;
        }
        break;
    }

    return 0;
}

static void usb_host_vm_state(void *unused, int running, RunState state)
{
    if (running) {
        usb_host_auto_check(unused);
    }
}

static void usb_host_auto_check(void *unused)
{
    struct USBHostDevice *s;
    int unconnected = 0;

    if (runstate_is_running()) {
        usb_host_scan(NULL, usb_host_auto_scan);

        QTAILQ_FOREACH(s, &hostdevs, next) {
            if (s->fd == -1) {
                unconnected++;
            }
            if (s->seen == 0) {
                s->errcount = 0;
            }
            s->seen = 0;
        }

        if (unconnected == 0) {
            /* nothing to watch */
            if (usb_auto_timer) {
                qemu_del_timer(usb_auto_timer);
                trace_usb_host_auto_scan_disabled();
            }
            return;
        }
    }

    if (!usb_vmstate) {
        usb_vmstate = qemu_add_vm_change_state_handler(usb_host_vm_state, NULL);
    }
    if (!usb_auto_timer) {
        usb_auto_timer = qemu_new_timer_ms(rt_clock, usb_host_auto_check, NULL);
        if (!usb_auto_timer) {
            return;
        }
        trace_usb_host_auto_scan_enabled();
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

static void usb_info_device(Monitor *mon, int bus_num,
                            int addr, const char *port,
                            int class_id, int vendor_id, int product_id,
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
    case USB_SPEED_SUPER:
        speed_str = "5000";
        break;
    default:
        speed_str = "?";
        break;
    }

    monitor_printf(mon, "  Bus %d, Addr %d, Port %s, Speed %s Mb/s\n",
                   bus_num, addr, port, speed_str);
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
                                const char *path, int class_id,
                                int vendor_id, int product_id,
                                const char *product_name,
                                int speed)
{
    Monitor *mon = opaque;

    usb_info_device(mon, bus_num, addr, path, class_id, vendor_id, product_id,
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
        monitor_printf(mon, "    Bus %s, Addr %s, Port %s, ID %s:%s\n",
                       bus, addr, f->port ? f->port : "*", vid, pid);
    }
}
