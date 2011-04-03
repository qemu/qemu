#include "usb.h"
#include "usb-desc.h"
#include "trace.h"

/* ------------------------------------------------------------------ */

static uint8_t usb_lo(uint16_t val)
{
    return val & 0xff;
}

static uint8_t usb_hi(uint16_t val)
{
    return (val >> 8) & 0xff;
}

int usb_desc_device(const USBDescID *id, const USBDescDevice *dev,
                    uint8_t *dest, size_t len)
{
    uint8_t bLength = 0x12;

    if (len < bLength) {
        return -1;
    }

    dest[0x00] = bLength;
    dest[0x01] = USB_DT_DEVICE;

    dest[0x02] = usb_lo(dev->bcdUSB);
    dest[0x03] = usb_hi(dev->bcdUSB);
    dest[0x04] = dev->bDeviceClass;
    dest[0x05] = dev->bDeviceSubClass;
    dest[0x06] = dev->bDeviceProtocol;
    dest[0x07] = dev->bMaxPacketSize0;

    dest[0x08] = usb_lo(id->idVendor);
    dest[0x09] = usb_hi(id->idVendor);
    dest[0x0a] = usb_lo(id->idProduct);
    dest[0x0b] = usb_hi(id->idProduct);
    dest[0x0c] = usb_lo(id->bcdDevice);
    dest[0x0d] = usb_hi(id->bcdDevice);
    dest[0x0e] = id->iManufacturer;
    dest[0x0f] = id->iProduct;
    dest[0x10] = id->iSerialNumber;

    dest[0x11] = dev->bNumConfigurations;

    return bLength;
}

int usb_desc_device_qualifier(const USBDescDevice *dev,
                              uint8_t *dest, size_t len)
{
    uint8_t bLength = 0x0a;

    if (len < bLength) {
        return -1;
    }

    dest[0x00] = bLength;
    dest[0x01] = USB_DT_DEVICE_QUALIFIER;

    dest[0x02] = usb_lo(dev->bcdUSB);
    dest[0x03] = usb_hi(dev->bcdUSB);
    dest[0x04] = dev->bDeviceClass;
    dest[0x05] = dev->bDeviceSubClass;
    dest[0x06] = dev->bDeviceProtocol;
    dest[0x07] = dev->bMaxPacketSize0;
    dest[0x08] = dev->bNumConfigurations;
    dest[0x09] = 0; /* reserved */

    return bLength;
}

int usb_desc_config(const USBDescConfig *conf, uint8_t *dest, size_t len)
{
    uint8_t  bLength = 0x09;
    uint16_t wTotalLength = 0;
    int i, rc;

    if (len < bLength) {
        return -1;
    }

    dest[0x00] = bLength;
    dest[0x01] = USB_DT_CONFIG;
    dest[0x04] = conf->bNumInterfaces;
    dest[0x05] = conf->bConfigurationValue;
    dest[0x06] = conf->iConfiguration;
    dest[0x07] = conf->bmAttributes;
    dest[0x08] = conf->bMaxPower;
    wTotalLength += bLength;

    /* handle grouped interfaces if any*/
    for (i = 0; i < conf->nif_groups; i++) {
        rc = usb_desc_iface_group(&(conf->if_groups[i]),
                                  dest + wTotalLength,
                                  len - wTotalLength);
        if (rc < 0) {
            return rc;
        }
        wTotalLength += rc;
    }

    /* handle normal (ungrouped / no IAD) interfaces if any */
    for (i = 0; i < conf->nif; i++) {
        rc = usb_desc_iface(conf->ifs + i, dest + wTotalLength, len - wTotalLength);
        if (rc < 0) {
            return rc;
        }
        wTotalLength += rc;
    }

    dest[0x02] = usb_lo(wTotalLength);
    dest[0x03] = usb_hi(wTotalLength);
    return wTotalLength;
}

int usb_desc_iface_group(const USBDescIfaceAssoc *iad, uint8_t *dest,
                         size_t len)
{
    int pos = 0;
    int i = 0;

    /* handle interface association descriptor */
    uint8_t bLength = 0x08;

    if (len < bLength) {
        return -1;
    }

    dest[0x00] = bLength;
    dest[0x01] = USB_DT_INTERFACE_ASSOC;
    dest[0x02] = iad->bFirstInterface;
    dest[0x03] = iad->bInterfaceCount;
    dest[0x04] = iad->bFunctionClass;
    dest[0x05] = iad->bFunctionSubClass;
    dest[0x06] = iad->bFunctionProtocol;
    dest[0x07] = iad->iFunction;
    pos += bLength;

    /* handle associated interfaces in this group */
    for (i = 0; i < iad->nif; i++) {
        int rc = usb_desc_iface(&(iad->ifs[i]), dest + pos, len - pos);
        if (rc < 0) {
            return rc;
        }
        pos += rc;
    }

    return pos;
}

int usb_desc_iface(const USBDescIface *iface, uint8_t *dest, size_t len)
{
    uint8_t bLength = 0x09;
    int i, rc, pos = 0;

    if (len < bLength) {
        return -1;
    }

    dest[0x00] = bLength;
    dest[0x01] = USB_DT_INTERFACE;
    dest[0x02] = iface->bInterfaceNumber;
    dest[0x03] = iface->bAlternateSetting;
    dest[0x04] = iface->bNumEndpoints;
    dest[0x05] = iface->bInterfaceClass;
    dest[0x06] = iface->bInterfaceSubClass;
    dest[0x07] = iface->bInterfaceProtocol;
    dest[0x08] = iface->iInterface;
    pos += bLength;

    for (i = 0; i < iface->ndesc; i++) {
        rc = usb_desc_other(iface->descs + i, dest + pos, len - pos);
        if (rc < 0) {
            return rc;
        }
        pos += rc;
    }

    for (i = 0; i < iface->bNumEndpoints; i++) {
        rc = usb_desc_endpoint(iface->eps + i, dest + pos, len - pos);
        if (rc < 0) {
            return rc;
        }
        pos += rc;
    }

    return pos;
}

int usb_desc_endpoint(const USBDescEndpoint *ep, uint8_t *dest, size_t len)
{
    uint8_t bLength = 0x07;

    if (len < bLength) {
        return -1;
    }

    dest[0x00] = bLength;
    dest[0x01] = USB_DT_ENDPOINT;
    dest[0x02] = ep->bEndpointAddress;
    dest[0x03] = ep->bmAttributes;
    dest[0x04] = usb_lo(ep->wMaxPacketSize);
    dest[0x05] = usb_hi(ep->wMaxPacketSize);
    dest[0x06] = ep->bInterval;

    return bLength;
}

int usb_desc_other(const USBDescOther *desc, uint8_t *dest, size_t len)
{
    int bLength = desc->length ? desc->length : desc->data[0];

    if (len < bLength) {
        return -1;
    }

    memcpy(dest, desc->data, bLength);
    return bLength;
}

/* ------------------------------------------------------------------ */

static void usb_desc_setdefaults(USBDevice *dev)
{
    const USBDesc *desc = dev->info->usb_desc;

    assert(desc != NULL);
    switch (dev->speed) {
    case USB_SPEED_LOW:
    case USB_SPEED_FULL:
        dev->device = desc->full;
        break;
    case USB_SPEED_HIGH:
        dev->device = desc->high;
        break;
    }
    dev->config = dev->device->confs;
}

void usb_desc_init(USBDevice *dev)
{
    dev->speed = USB_SPEED_FULL;
    usb_desc_setdefaults(dev);
}

void usb_desc_attach(USBDevice *dev)
{
    const USBDesc *desc = dev->info->usb_desc;

    assert(desc != NULL);
    if (desc->high && (dev->port->speedmask & USB_SPEED_MASK_HIGH)) {
        dev->speed = USB_SPEED_HIGH;
    } else if (desc->full && (dev->port->speedmask & USB_SPEED_MASK_FULL)) {
        dev->speed = USB_SPEED_FULL;
    } else {
        fprintf(stderr, "usb: port/device speed mismatch for \"%s\"\n",
                dev->info->product_desc);
        return;
    }
    usb_desc_setdefaults(dev);
}

void usb_desc_set_string(USBDevice *dev, uint8_t index, const char *str)
{
    USBDescString *s;

    QLIST_FOREACH(s, &dev->strings, next) {
        if (s->index == index) {
            break;
        }
    }
    if (s == NULL) {
        s = qemu_mallocz(sizeof(*s));
        s->index = index;
        QLIST_INSERT_HEAD(&dev->strings, s, next);
    }
    qemu_free(s->str);
    s->str = qemu_strdup(str);
}

const char *usb_desc_get_string(USBDevice *dev, uint8_t index)
{
    USBDescString *s;

    QLIST_FOREACH(s, &dev->strings, next) {
        if (s->index == index) {
            return s->str;
        }
    }
    return NULL;
}

int usb_desc_string(USBDevice *dev, int index, uint8_t *dest, size_t len)
{
    uint8_t bLength, pos, i;
    const char *str;

    if (len < 4) {
        return -1;
    }

    if (index == 0) {
        /* language ids */
        dest[0] = 4;
        dest[1] = USB_DT_STRING;
        dest[2] = 0x09;
        dest[3] = 0x04;
        return 4;
    }

    str = usb_desc_get_string(dev, index);
    if (str == NULL) {
        str = dev->info->usb_desc->str[index];
        if (str == NULL) {
            return 0;
        }
    }

    bLength = strlen(str) * 2 + 2;
    dest[0] = bLength;
    dest[1] = USB_DT_STRING;
    i = 0; pos = 2;
    while (pos+1 < bLength && pos+1 < len) {
        dest[pos++] = str[i++];
        dest[pos++] = 0;
    }
    return pos;
}

int usb_desc_get_descriptor(USBDevice *dev, int value, uint8_t *dest, size_t len)
{
    const USBDesc *desc = dev->info->usb_desc;
    const USBDescDevice *other_dev;
    uint8_t buf[256];
    uint8_t type = value >> 8;
    uint8_t index = value & 0xff;
    int ret = -1;

    if (dev->speed == USB_SPEED_HIGH) {
        other_dev = dev->info->usb_desc->full;
    } else {
        other_dev = dev->info->usb_desc->high;
    }

    switch(type) {
    case USB_DT_DEVICE:
        ret = usb_desc_device(&desc->id, dev->device, buf, sizeof(buf));
        trace_usb_desc_device(dev->addr, len, ret);
        break;
    case USB_DT_CONFIG:
        if (index < dev->device->bNumConfigurations) {
            ret = usb_desc_config(dev->device->confs + index, buf, sizeof(buf));
        }
        trace_usb_desc_config(dev->addr, index, len, ret);
        break;
    case USB_DT_STRING:
        ret = usb_desc_string(dev, index, buf, sizeof(buf));
        trace_usb_desc_string(dev->addr, index, len, ret);
        break;

    case USB_DT_DEVICE_QUALIFIER:
        if (other_dev != NULL) {
            ret = usb_desc_device_qualifier(other_dev, buf, sizeof(buf));
        }
        trace_usb_desc_device_qualifier(dev->addr, len, ret);
        break;
    case USB_DT_OTHER_SPEED_CONFIG:
        if (other_dev != NULL && index < other_dev->bNumConfigurations) {
            ret = usb_desc_config(other_dev->confs + index, buf, sizeof(buf));
            buf[0x01] = USB_DT_OTHER_SPEED_CONFIG;
        }
        trace_usb_desc_other_speed_config(dev->addr, index, len, ret);
        break;

    default:
        fprintf(stderr, "%s: %d unknown type %d (len %zd)\n", __FUNCTION__,
                dev->addr, type, len);
        break;
    }

    if (ret > 0) {
        if (ret > len) {
            ret = len;
        }
        memcpy(dest, buf, ret);
    }
    return ret;
}

int usb_desc_handle_control(USBDevice *dev, int request, int value,
                            int index, int length, uint8_t *data)
{
    const USBDesc *desc = dev->info->usb_desc;
    int i, ret = -1;

    assert(desc != NULL);
    switch(request) {
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        dev->addr = value;
        trace_usb_set_addr(dev->addr);
        ret = 0;
        break;

    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        ret = usb_desc_get_descriptor(dev, value, data, length);
        break;

    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        data[0] = dev->config->bConfigurationValue;
        ret = 1;
        break;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        for (i = 0; i < dev->device->bNumConfigurations; i++) {
            if (dev->device->confs[i].bConfigurationValue == value) {
                dev->config = dev->device->confs + i;
                ret = 0;
            }
        }
        trace_usb_set_config(dev->addr, value, ret);
        break;

    case DeviceRequest | USB_REQ_GET_STATUS:
        data[0] = 0;
        if (dev->config->bmAttributes & 0x40) {
            data[0] |= 1 << USB_DEVICE_SELF_POWERED;
        }
        if (dev->remote_wakeup) {
            data[0] |= 1 << USB_DEVICE_REMOTE_WAKEUP;
        }
        data[1] = 0x00;
        ret = 2;
        break;
    case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 0;
            ret = 0;
        }
        trace_usb_clear_device_feature(dev->addr, value, ret);
        break;
    case DeviceOutRequest | USB_REQ_SET_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 1;
            ret = 0;
        }
        trace_usb_set_device_feature(dev->addr, value, ret);
        break;
    }
    return ret;
}
