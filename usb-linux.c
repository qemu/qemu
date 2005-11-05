/*
 * Linux host USB redirector
 *
 * Copyright (c) 2005 Fabrice Bellard
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
#include "vl.h"

#if defined(__linux__)
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <linux/version.h>

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

//#define DEBUG

#define MAX_DEVICES 8

#define USBDEVFS_PATH "/proc/bus/usb"

typedef struct USBHostDevice {
    USBDevice dev;
    int fd;
} USBHostDevice;

typedef struct USBHostHubState {
    USBDevice *hub_dev;
    USBPort *hub_ports[MAX_DEVICES];
    USBDevice *hub_devices[MAX_DEVICES];
} USBHostHubState;

static void usb_host_handle_reset(USBDevice *dev)
{
#if 0
    USBHostDevice *s = (USBHostDevice *)dev;
    /* USBDEVFS_RESET, but not the first time as it has already be
       done by the host OS */
    ioctl(s->fd, USBDEVFS_RESET);
#endif
} 

static int usb_host_handle_control(USBDevice *dev,
                                   int request,
                                   int value,
                                   int index,
                                   int length,
                                   uint8_t *data)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    struct usb_ctrltransfer ct;
    int ret;

    if (request == (DeviceOutRequest | USB_REQ_SET_ADDRESS)) {
        /* specific SET_ADDRESS support */
        dev->addr = value;
        return 0;
    } else {
        ct.bRequestType = request >> 8;
        ct.bRequest = request;
        ct.wValue = value;
        ct.wIndex = index;
        ct.wLength = length;
        ct.timeout = 50;
        ct.data = data;
        ret = ioctl(s->fd, USBDEVFS_CONTROL, &ct);
        if (ret < 0) {
            switch(errno) {
            case ETIMEDOUT:
                return USB_RET_NAK;
            default:
                return USB_RET_STALL;
            }
        } else {
            return ret;
        }
   }
}

static int usb_host_handle_data(USBDevice *dev, int pid, 
                                uint8_t devep,
                                uint8_t *data, int len)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    struct usbdevfs_bulktransfer bt;
    int ret;

    /* XXX: optimize and handle all data types by looking at the
       config descriptor */
    if (pid == USB_TOKEN_IN)
        devep |= 0x80;
    bt.ep = devep;
    bt.len = len;
    bt.timeout = 50;
    bt.data = data;
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

static int usb_host_handle_packet(USBDevice *dev, int pid, 
                                  uint8_t devaddr, uint8_t devep,
                                  uint8_t *data, int len)
{
    return usb_generic_handle_packet(dev, pid, devaddr, devep, data, len);
}

/* XXX: exclude high speed devices or implement EHCI */
static void scan_host_device(USBHostHubState *s, const char *filename)
{
    int fd, interface, ret, i;
    USBHostDevice *dev;
    struct usbdevfs_connectinfo ci;
    uint8_t descr[1024];
    int descr_len, dev_descr_len, config_descr_len, nb_interfaces;

#ifdef DEBUG
    printf("scanning %s\n", filename);
#endif
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        perror(filename);
        return;
    }

    /* read the config description */
    descr_len = read(fd, descr, sizeof(descr));
    if (descr_len <= 0) {
        perror("read descr");
        goto fail;
    }
    
    i = 0;
    dev_descr_len = descr[0];
    if (dev_descr_len > descr_len)
        goto fail;
    i += dev_descr_len;
    config_descr_len = descr[i];
    if (i + config_descr_len > descr_len)
        goto fail;
    nb_interfaces = descr[i + 4];
    if (nb_interfaces != 1) {
        /* NOTE: currently we grab only one interface */
        goto fail;
    }
    /* XXX: only grab if all interfaces are free */
    interface = 0;
    ret = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface);
    if (ret < 0) {
        if (errno == EBUSY) {
#ifdef DEBUG
            printf("%s already grabbed\n", filename);
#endif            
        } else {
            perror("USBDEVFS_CLAIMINTERFACE");
        }
    fail:
        close(fd);
        return;
    }

    ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
    if (ret < 0) {
        perror("USBDEVFS_CONNECTINFO");
        goto fail;
    }

#ifdef DEBUG
    printf("%s grabbed\n", filename);
#endif    

    /* find a free slot */
    for(i = 0; i < MAX_DEVICES; i++) {
        if (!s->hub_devices[i])
            break;
    }
    if (i == MAX_DEVICES) {
#ifdef DEBUG
        printf("too many host devices\n");
        goto fail;
#endif
    }

    dev = qemu_mallocz(sizeof(USBHostDevice));
    if (!dev)
        goto fail;
    dev->fd = fd;
    if (ci.slow)
        dev->dev.speed = USB_SPEED_LOW;
    else
        dev->dev.speed = USB_SPEED_HIGH;
    dev->dev.handle_packet = usb_host_handle_packet;

    dev->dev.handle_reset = usb_host_handle_reset;
    dev->dev.handle_control = usb_host_handle_control;
    dev->dev.handle_data = usb_host_handle_data;

    s->hub_devices[i] = (USBDevice *)dev;

    /* activate device on hub */
    usb_attach(s->hub_ports[i], s->hub_devices[i]);
}

static void scan_host_devices(USBHostHubState *s, const char *bus_path)
{
    DIR *d;
    struct dirent *de;
    char buf[1024];

    d = opendir(bus_path);
    if (!d)
        return;
    for(;;) {
        de = readdir(d);
        if (!de)
            break;
        if (de->d_name[0] != '.') {
            snprintf(buf, sizeof(buf), "%s/%s", bus_path, de->d_name);
            scan_host_device(s, buf);
        }
    }
    closedir(d);
}

static void scan_host_buses(USBHostHubState *s)
{
    DIR *d;
    struct dirent *de;
    char buf[1024];

    d = opendir(USBDEVFS_PATH);
    if (!d)
        return;
    for(;;) {
        de = readdir(d);
        if (!de)
            break;
        if (isdigit(de->d_name[0])) {
            snprintf(buf, sizeof(buf), "%s/%s", USBDEVFS_PATH, de->d_name);
            scan_host_devices(s, buf);
        }
    }
    closedir(d);
}

/* virtual hub containing the USB devices of the host */
USBDevice *usb_host_hub_init(void)
{
    USBHostHubState *s;
    s = qemu_mallocz(sizeof(USBHostHubState));
    if (!s)
        return NULL;
    s->hub_dev = usb_hub_init(s->hub_ports, MAX_DEVICES);
    if (!s->hub_dev) {
        free(s);
        return NULL;
    }
    scan_host_buses(s);
    return s->hub_dev;
}

#else

/* XXX: modify configure to compile the right host driver */
USBDevice *usb_host_hub_init(void)
{
    return NULL;
}

#endif
