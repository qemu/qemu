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

typedef int USBScanFunc(void *opaque, int bus_num, int addr, int class_id,
                        int vendor_id, int product_id, 
                        const char *product_name, int speed);
static int usb_host_find_device(int *pbus_num, int *paddr, 
                                char *product_name, int product_name_size,
                                const char *devname);

//#define DEBUG

#define USBDEVFS_PATH "/proc/bus/usb"
#define PRODUCT_NAME_SZ 32

typedef struct USBHostDevice {
    USBDevice dev;
    int fd;
} USBHostDevice;

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

    if (s->fd >= 0)
        close(s->fd);
    qemu_free(s);
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

static int usb_host_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHostDevice *s = (USBHostDevice *)dev;
    struct usbdevfs_bulktransfer bt;
    int ret;
    uint8_t devep = p->devep;

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

/* XXX: exclude high speed devices or implement EHCI */
USBDevice *usb_host_device_open(const char *devname)
{
    int fd, interface, ret, i;
    USBHostDevice *dev;
    struct usbdevfs_connectinfo ci;
    uint8_t descr[1024];
    char buf[1024];
    int descr_len, dev_descr_len, config_descr_len, nb_interfaces;
    int bus_num, addr;
    char product_name[PRODUCT_NAME_SZ];

    if (usb_host_find_device(&bus_num, &addr, 
                             product_name, sizeof(product_name),
                             devname) < 0) 
        return NULL;
    
    snprintf(buf, sizeof(buf), USBDEVFS_PATH "/%03d/%03d", 
             bus_num, addr);
    fd = open(buf, O_RDWR);
    if (fd < 0) {
        perror(buf);
        return NULL;
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
        fprintf(stderr, "usb_host: only one interface supported\n");
        goto fail;
    }

#ifdef USBDEVFS_DISCONNECT
    /* earlier Linux 2.4 do not support that */
    {
        struct usbdevfs_ioctl ctrl;
        ctrl.ioctl_code = USBDEVFS_DISCONNECT;
        ctrl.ifno = 0;
        ret = ioctl(fd, USBDEVFS_IOCTL, &ctrl);
        if (ret < 0 && errno != ENODATA) {
            perror("USBDEVFS_DISCONNECT");
            goto fail;
        }
    }
#endif

    /* XXX: only grab if all interfaces are free */
    interface = 0;
    ret = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface);
    if (ret < 0) {
        if (errno == EBUSY) {
            fprintf(stderr, "usb_host: device already grabbed\n");
        } else {
            perror("USBDEVFS_CLAIMINTERFACE");
        }
    fail:
        close(fd);
        return NULL;
    }

    ret = ioctl(fd, USBDEVFS_CONNECTINFO, &ci);
    if (ret < 0) {
        perror("USBDEVFS_CONNECTINFO");
        goto fail;
    }

#ifdef DEBUG
    printf("host USB device %d.%d grabbed\n", bus_num, addr);
#endif    

    dev = qemu_mallocz(sizeof(USBHostDevice));
    if (!dev)
        goto fail;
    dev->fd = fd;
    if (ci.slow)
        dev->dev.speed = USB_SPEED_LOW;
    else
        dev->dev.speed = USB_SPEED_HIGH;
    dev->dev.handle_packet = usb_generic_handle_packet;

    dev->dev.handle_reset = usb_host_handle_reset;
    dev->dev.handle_control = usb_host_handle_control;
    dev->dev.handle_data = usb_host_handle_data;
    dev->dev.handle_destroy = usb_host_handle_destroy;

    if (product_name[0] == '\0')
        snprintf(dev->dev.devname, sizeof(dev->dev.devname),
                 "host:%s", devname);
    else
        pstrcpy(dev->dev.devname, sizeof(dev->dev.devname),
                product_name);

    return (USBDevice *)dev;
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
    { USB_CLASS_CSCID, 	"Smart Card" },
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
