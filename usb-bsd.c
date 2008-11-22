/*
 * BSD host USB redirector
 *
 * Copyright (c) 2006 Lonnie Mendez
 * Portions of code and concepts borrowed from
 * usb-linux.c and libusb's bsd.c and are copyright their respective owners.
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
#include "console.h"
#include "hw/usb.h"

/* usb.h declares these */
#undef USB_SPEED_HIGH
#undef USB_SPEED_FULL
#undef USB_SPEED_LOW

#include <sys/ioctl.h>
#include <dev/usb/usb.h>
#include <signal.h>

/* This value has maximum potential at 16.
 * You should also set hw.usb.debug to gain
 * more detailed view.
 */
//#define DEBUG
#define UGEN_DEBUG_LEVEL 0


typedef int USBScanFunc(void *opaque, int bus_num, int addr, int class_id,
                        int vendor_id, int product_id,
                        const char *product_name, int speed);
static int usb_host_find_device(int *pbus_num, int *paddr,
                                const char *devname);

typedef struct USBHostDevice {
    USBDevice dev;
    int ep_fd[USB_MAX_ENDPOINTS];
    int devfd;
    char devpath[32];
} USBHostDevice;


static int ensure_ep_open(USBHostDevice *dev, int ep, int mode)
{
    char buf[32];
    int fd;

    /* Get the address for this endpoint */
    ep = UE_GET_ADDR(ep);

    if (dev->ep_fd[ep] < 0) {
#if __FreeBSD__
        snprintf(buf, sizeof(buf) - 1, "%s.%d", dev->devpath, ep);
#else
        snprintf(buf, sizeof(buf) - 1, "%s.%02d", dev->devpath, ep);
#endif
        /* Try to open it O_RDWR first for those devices which have in and out
         * endpoints with the same address (eg 0x02 and 0x82)
         */
        fd = open(buf, O_RDWR);
        if (fd < 0 && errno == ENXIO)
            fd = open(buf, mode);
        if (fd < 0) {
#ifdef DEBUG
            printf("ensure_ep_open: failed to open device endpoint %s: %s\n",
                   buf, strerror(errno));
#endif
        }
        dev->ep_fd[ep] = fd;
    }

    return dev->ep_fd[ep];
}

static void ensure_eps_closed(USBHostDevice *dev)
{
    int epnum = 1;

    if (!dev)
        return;

    while (epnum < USB_MAX_ENDPOINTS) {
        if (dev->ep_fd[epnum] >= 0) {
            close(dev->ep_fd[epnum]);
            dev->ep_fd[epnum] = -1;
        }
        epnum++;
    }
}

static void usb_host_handle_reset(USBDevice *dev)
{
#if 0
    USBHostDevice *s = (USBHostDevice *)dev;
#endif
}

/* XXX:
 * -check device states against transfer requests
 *  and return appropriate response
 */
static int usb_host_handle_control(USBDevice *dev,
                                   int request,
                                   int value,
                                   int index,
                                   int length,
                                   uint8_t *data)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    struct usb_ctl_request req;
    struct usb_alt_interface aiface;
    int ret, timeout = 50;

    if ((request >> 8) == UT_WRITE_DEVICE &&
        (request & 0xff) == UR_SET_ADDRESS) {

        /* specific SET_ADDRESS support */
        dev->addr = value;
        return 0;
    } else if ((request >> 8) == UT_WRITE_DEVICE &&
               (request & 0xff) == UR_SET_CONFIG) {

        ensure_eps_closed(s); /* can't do this without all eps closed */

        ret = ioctl(s->devfd, USB_SET_CONFIG, &value);
        if (ret < 0) {
#ifdef DEBUG
            printf("handle_control: failed to set configuration - %s\n",
                   strerror(errno));
#endif
            return USB_RET_STALL;
        }

        return 0;
    } else if ((request >> 8) == UT_WRITE_INTERFACE &&
               (request & 0xff) == UR_SET_INTERFACE) {

        aiface.uai_interface_index = index;
        aiface.uai_alt_no = value;

        ensure_eps_closed(s); /* can't do this without all eps closed */
        ret = ioctl(s->devfd, USB_SET_ALTINTERFACE, &aiface);
        if (ret < 0) {
#ifdef DEBUG
            printf("handle_control: failed to set alternate interface - %s\n",
                   strerror(errno));
#endif
            return USB_RET_STALL;
        }

        return 0;
    } else {
        req.ucr_request.bmRequestType = request >> 8;
        req.ucr_request.bRequest = request & 0xff;
        USETW(req.ucr_request.wValue, value);
        USETW(req.ucr_request.wIndex, index);
        USETW(req.ucr_request.wLength, length);
        req.ucr_data = data;
        req.ucr_flags = USBD_SHORT_XFER_OK;

        ret = ioctl(s->devfd, USB_SET_TIMEOUT, &timeout);
#if (__NetBSD__ || __OpenBSD__)
        if (ret < 0 && errno != EINVAL) {
#else
        if (ret < 0) {
#endif
#ifdef DEBUG
            printf("handle_control: setting timeout failed - %s\n",
                   strerror(errno));
#endif
        }

        ret = ioctl(s->devfd, USB_DO_REQUEST, &req);
        /* ugen returns EIO for usbd_do_request_ no matter what
         * happens with the transfer */
        if (ret < 0) {
#ifdef DEBUG
            printf("handle_control: error after request - %s\n",
                   strerror(errno));
#endif
            return USB_RET_NAK; // STALL
        } else {
            return req.ucr_actlen;
        }
    }
}

static int usb_host_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    int ret, fd, mode;
    int one = 1, shortpacket = 0, timeout = 50;
    sigset_t new_mask, old_mask;
    uint8_t devep = p->devep;

    /* protect data transfers from SIGALRM signal */
    sigemptyset(&new_mask);
    sigaddset(&new_mask, SIGALRM);
    sigprocmask(SIG_BLOCK, &new_mask, &old_mask);

    if (p->pid == USB_TOKEN_IN) {
        devep |= 0x80;
        mode = O_RDONLY;
        shortpacket = 1;
    } else {
        mode = O_WRONLY;
    }

    fd = ensure_ep_open(s, devep, mode);
    if (fd < 0) {
        sigprocmask(SIG_SETMASK, &old_mask, NULL);
        return USB_RET_NODEV;
    }

    if (ioctl(fd, USB_SET_TIMEOUT, &timeout) < 0) {
#ifdef DEBUG
        printf("handle_data: failed to set timeout - %s\n",
               strerror(errno));
#endif
    }

    if (shortpacket) {
        if (ioctl(fd, USB_SET_SHORT_XFER, &one) < 0) {
#ifdef DEBUG
            printf("handle_data: failed to set short xfer mode - %s\n",
                   strerror(errno));
#endif
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
        }
    }

    if (p->pid == USB_TOKEN_IN)
        ret = read(fd, p->data, p->len);
    else
        ret = write(fd, p->data, p->len);

    sigprocmask(SIG_SETMASK, &old_mask, NULL);

    if (ret < 0) {
#ifdef DEBUG
        printf("handle_data: error after %s data - %s\n",
               pid == USB_TOKEN_IN ? "reading" : "writing", strerror(errno));
#endif
        switch(errno) {
        case ETIMEDOUT:
        case EINTR:
            return USB_RET_NAK;
        default:
            return USB_RET_STALL;
        }
    } else {
        return ret;
    }
}

static void usb_host_handle_destroy(USBDevice *opaque)
{
    USBHostDevice *s = (USBHostDevice *)opaque;
    int i;

    for (i = 0; i < USB_MAX_ENDPOINTS; i++)
        if (s->ep_fd[i] >= 0)
            close(s->ep_fd[i]);

    if (s->devfd < 0)
        return;

    close(s->devfd);

    qemu_free(s);
}

USBDevice *usb_host_device_open(const char *devname)
{
    struct usb_device_info bus_info, dev_info;
    USBHostDevice *dev;
    char ctlpath[PATH_MAX + 1];
    char buspath[PATH_MAX + 1];
    int bfd, dfd, bus, address, i;
    int ugendebug = UGEN_DEBUG_LEVEL;

    if (usb_host_find_device(&bus, &address, devname) < 0)
        return NULL;

    snprintf(buspath, PATH_MAX, "/dev/usb%d", bus);

    bfd = open(buspath, O_RDWR);
    if (bfd < 0) {
#ifdef DEBUG
        printf("usb_host_device_open: failed to open usb bus - %s\n",
               strerror(errno));
#endif
        return NULL;
    }

    bus_info.udi_addr = address;
    if (ioctl(bfd, USB_DEVICEINFO, &bus_info) < 0) {
#ifdef DEBUG
        printf("usb_host_device_open: failed to grab bus information - %s\n",
               strerror(errno));
#endif
        return NULL;
    }

#if __FreeBSD__
    snprintf(ctlpath, PATH_MAX, "/dev/%s", bus_info.udi_devnames[0]);
#else
    snprintf(ctlpath, PATH_MAX, "/dev/%s.00", bus_info.udi_devnames[0]);
#endif

    dfd  = open(ctlpath, O_RDWR);
    if (dfd < 0) {
        dfd = open(ctlpath, O_RDONLY);
        if (dfd < 0) {
#ifdef DEBUG
            printf("usb_host_device_open: failed to open usb device %s - %s\n",
                   ctlpath, strerror(errno));
#endif
        }
    }

    if (dfd >= 0) {
        dev = qemu_mallocz(sizeof(USBHostDevice));
        if (!dev)
            goto fail;
        dev->devfd = dfd;

        if (ioctl(dfd, USB_GET_DEVICEINFO, &dev_info) < 0) {
#ifdef DEBUG
            printf("usb_host_device_open: failed to grab device info - %s\n",
                   strerror(errno));
#endif
            goto fail;
        }

        if (dev_info.udi_speed == 1)
            dev->dev.speed = USB_SPEED_LOW - 1;
        else
            dev->dev.speed = USB_SPEED_FULL - 1;

        dev->dev.handle_packet = usb_generic_handle_packet;

        dev->dev.handle_reset = usb_host_handle_reset;
        dev->dev.handle_control = usb_host_handle_control;
        dev->dev.handle_data = usb_host_handle_data;
        dev->dev.handle_destroy = usb_host_handle_destroy;

        if (strncmp(dev_info.udi_product, "product", 7) != 0)
            pstrcpy(dev->dev.devname, sizeof(dev->dev.devname),
                    dev_info.udi_product);
        else
            snprintf(dev->dev.devname, sizeof(dev->dev.devname),
                     "host:%s", devname);

        pstrcpy(dev->devpath, sizeof(dev->devpath), "/dev/");
	strcat(dev->devpath, dev_info.udi_devnames[0]);

        /* Mark the endpoints as not yet open */
        for (i = 0; i < USB_MAX_ENDPOINTS; i++)
           dev->ep_fd[i] = -1;

        ioctl(dfd, USB_SETDEBUG, &ugendebug);

        return (USBDevice *)dev;
    }

fail:
    return NULL;
}

static int usb_host_scan(void *opaque, USBScanFunc *func)
{
    struct usb_device_info bus_info;
    struct usb_device_info dev_info;
    uint16_t vendor_id, product_id, class_id, speed;
    int bfd, dfd, bus, address;
    char busbuf[20], devbuf[20], product_name[256];
    int ret = 0;

    for (bus = 0; bus < 10; bus++) {

        snprintf(busbuf, sizeof(busbuf) - 1, "/dev/usb%d", bus);
        bfd = open(busbuf, O_RDWR);
        if (bfd < 0)
	    continue;

        for (address = 1; address < 127; address++) {

            bus_info.udi_addr = address;
            if (ioctl(bfd, USB_DEVICEINFO, &bus_info) < 0)
                continue;

            /* only list devices that can be used by generic layer */
            if (strncmp(bus_info.udi_devnames[0], "ugen", 4) != 0)
                continue;

#if __FreeBSD__
            snprintf(devbuf, sizeof(devbuf) - 1, "/dev/%s", bus_info.udi_devnames[0]);
#else
            snprintf(devbuf, sizeof(devbuf) - 1, "/dev/%s.00", bus_info.udi_devnames[0]);
#endif

            dfd = open(devbuf, O_RDONLY);
            if (dfd < 0) {
#ifdef DEBUG
                printf("usb_host_scan: couldn't open device %s - %s\n", devbuf,
                       strerror(errno));
#endif
                continue;
            }

            if (ioctl(dfd, USB_GET_DEVICEINFO, &dev_info) < 0)
                printf("usb_host_scan: couldn't get device information for %s - %s\n",
                       devbuf, strerror(errno));

            // XXX: might need to fixup endianess of word values before copying over

            vendor_id = dev_info.udi_vendorNo;
            product_id = dev_info.udi_productNo;
            class_id = dev_info.udi_class;
            speed = dev_info.udi_speed;

            if (strncmp(dev_info.udi_product, "product", 7) != 0)
                pstrcpy(product_name, sizeof(product_name),
                        dev_info.udi_product);
            else
                product_name[0] = '\0';

            ret = func(opaque, bus, address, class_id, vendor_id,
                       product_id, product_name, speed);

            close(dfd);

            if (ret)
                goto the_end;
        }

        close(bfd);
    }

the_end:
    return ret;
}

typedef struct FindDeviceState {
    int vendor_id;
    int product_id;
    int bus_num;
    int addr;
} FindDeviceState;

static int usb_host_find_device_scan(void *opaque, int bus_num, int addr,
                                     int class_id,
                                     int vendor_id, int product_id,
                                     const char *product_name, int speed)
{
    FindDeviceState *s = opaque;
    if (vendor_id == s->vendor_id &&
        product_id == s->product_id) {
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
                                const char *devname)
{
    const char *p;
    int ret;
    FindDeviceState fs;

    p = strchr(devname, '.');
    if (p) {
        *pbus_num = strtoul(devname, NULL, 0);
        *paddr = strtoul(p + 1, NULL, 0);
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
    for (p = usb_class_info; p->class != -1; p++) {
        if (p->class == class)
            break;
    }
    return p->class_name;
}

void usb_info_device(int bus_num, int addr, int class_id,
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

/* XXX add this */
int usb_host_device_close(const char *devname)
{
    return 0;
}
