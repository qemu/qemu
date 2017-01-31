#include "qemu/osdep.h"

#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "trace.h"

/* ------------------------------------------------------------------ */

int usb_desc_device(const USBDescID *id, const USBDescDevice *dev,
                    bool msos, uint8_t *dest, size_t len)
{
    uint8_t bLength = 0x12;
    USBDescriptor *d = (void *)dest;

    if (len < bLength) {
        return -1;
    }

    d->bLength                     = bLength;
    d->bDescriptorType             = USB_DT_DEVICE;

    if (msos && dev->bcdUSB < 0x0200) {
        /*
         * Version 2.0+ required for microsoft os descriptors to work.
         * Done this way so msos-desc compat property will handle both
         * the version and the new descriptors being present.
         */
        d->u.device.bcdUSB_lo          = usb_lo(0x0200);
        d->u.device.bcdUSB_hi          = usb_hi(0x0200);
    } else {
        d->u.device.bcdUSB_lo          = usb_lo(dev->bcdUSB);
        d->u.device.bcdUSB_hi          = usb_hi(dev->bcdUSB);
    }
    d->u.device.bDeviceClass       = dev->bDeviceClass;
    d->u.device.bDeviceSubClass    = dev->bDeviceSubClass;
    d->u.device.bDeviceProtocol    = dev->bDeviceProtocol;
    d->u.device.bMaxPacketSize0    = dev->bMaxPacketSize0;

    d->u.device.idVendor_lo        = usb_lo(id->idVendor);
    d->u.device.idVendor_hi        = usb_hi(id->idVendor);
    d->u.device.idProduct_lo       = usb_lo(id->idProduct);
    d->u.device.idProduct_hi       = usb_hi(id->idProduct);
    d->u.device.bcdDevice_lo       = usb_lo(id->bcdDevice);
    d->u.device.bcdDevice_hi       = usb_hi(id->bcdDevice);
    d->u.device.iManufacturer      = id->iManufacturer;
    d->u.device.iProduct           = id->iProduct;
    d->u.device.iSerialNumber      = id->iSerialNumber;

    d->u.device.bNumConfigurations = dev->bNumConfigurations;

    return bLength;
}

int usb_desc_device_qualifier(const USBDescDevice *dev,
                              uint8_t *dest, size_t len)
{
    uint8_t bLength = 0x0a;
    USBDescriptor *d = (void *)dest;

    if (len < bLength) {
        return -1;
    }

    d->bLength                               = bLength;
    d->bDescriptorType                       = USB_DT_DEVICE_QUALIFIER;

    d->u.device_qualifier.bcdUSB_lo          = usb_lo(dev->bcdUSB);
    d->u.device_qualifier.bcdUSB_hi          = usb_hi(dev->bcdUSB);
    d->u.device_qualifier.bDeviceClass       = dev->bDeviceClass;
    d->u.device_qualifier.bDeviceSubClass    = dev->bDeviceSubClass;
    d->u.device_qualifier.bDeviceProtocol    = dev->bDeviceProtocol;
    d->u.device_qualifier.bMaxPacketSize0    = dev->bMaxPacketSize0;
    d->u.device_qualifier.bNumConfigurations = dev->bNumConfigurations;
    d->u.device_qualifier.bReserved          = 0;

    return bLength;
}

int usb_desc_config(const USBDescConfig *conf, int flags,
                    uint8_t *dest, size_t len)
{
    uint8_t  bLength = 0x09;
    uint16_t wTotalLength = 0;
    USBDescriptor *d = (void *)dest;
    int i, rc;

    if (len < bLength) {
        return -1;
    }

    d->bLength                      = bLength;
    d->bDescriptorType              = USB_DT_CONFIG;

    d->u.config.bNumInterfaces      = conf->bNumInterfaces;
    d->u.config.bConfigurationValue = conf->bConfigurationValue;
    d->u.config.iConfiguration      = conf->iConfiguration;
    d->u.config.bmAttributes        = conf->bmAttributes;
    d->u.config.bMaxPower           = conf->bMaxPower;
    wTotalLength += bLength;

    /* handle grouped interfaces if any */
    for (i = 0; i < conf->nif_groups; i++) {
        rc = usb_desc_iface_group(&(conf->if_groups[i]), flags,
                                  dest + wTotalLength,
                                  len - wTotalLength);
        if (rc < 0) {
            return rc;
        }
        wTotalLength += rc;
    }

    /* handle normal (ungrouped / no IAD) interfaces if any */
    for (i = 0; i < conf->nif; i++) {
        rc = usb_desc_iface(conf->ifs + i, flags,
                            dest + wTotalLength, len - wTotalLength);
        if (rc < 0) {
            return rc;
        }
        wTotalLength += rc;
    }

    d->u.config.wTotalLength_lo = usb_lo(wTotalLength);
    d->u.config.wTotalLength_hi = usb_hi(wTotalLength);
    return wTotalLength;
}

int usb_desc_iface_group(const USBDescIfaceAssoc *iad, int flags,
                         uint8_t *dest, size_t len)
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
        int rc = usb_desc_iface(&(iad->ifs[i]), flags, dest + pos, len - pos);
        if (rc < 0) {
            return rc;
        }
        pos += rc;
    }

    return pos;
}

int usb_desc_iface(const USBDescIface *iface, int flags,
                   uint8_t *dest, size_t len)
{
    uint8_t bLength = 0x09;
    int i, rc, pos = 0;
    USBDescriptor *d = (void *)dest;

    if (len < bLength) {
        return -1;
    }

    d->bLength                        = bLength;
    d->bDescriptorType                = USB_DT_INTERFACE;

    d->u.interface.bInterfaceNumber   = iface->bInterfaceNumber;
    d->u.interface.bAlternateSetting  = iface->bAlternateSetting;
    d->u.interface.bNumEndpoints      = iface->bNumEndpoints;
    d->u.interface.bInterfaceClass    = iface->bInterfaceClass;
    d->u.interface.bInterfaceSubClass = iface->bInterfaceSubClass;
    d->u.interface.bInterfaceProtocol = iface->bInterfaceProtocol;
    d->u.interface.iInterface         = iface->iInterface;
    pos += bLength;

    for (i = 0; i < iface->ndesc; i++) {
        rc = usb_desc_other(iface->descs + i, dest + pos, len - pos);
        if (rc < 0) {
            return rc;
        }
        pos += rc;
    }

    for (i = 0; i < iface->bNumEndpoints; i++) {
        rc = usb_desc_endpoint(iface->eps + i, flags, dest + pos, len - pos);
        if (rc < 0) {
            return rc;
        }
        pos += rc;
    }

    return pos;
}

int usb_desc_endpoint(const USBDescEndpoint *ep, int flags,
                      uint8_t *dest, size_t len)
{
    uint8_t bLength = ep->is_audio ? 0x09 : 0x07;
    uint8_t extralen = ep->extra ? ep->extra[0] : 0;
    uint8_t superlen = (flags & USB_DESC_FLAG_SUPER) ? 0x06 : 0;
    USBDescriptor *d = (void *)dest;

    if (len < bLength + extralen + superlen) {
        return -1;
    }

    d->bLength                      = bLength;
    d->bDescriptorType              = USB_DT_ENDPOINT;

    d->u.endpoint.bEndpointAddress  = ep->bEndpointAddress;
    d->u.endpoint.bmAttributes      = ep->bmAttributes;
    d->u.endpoint.wMaxPacketSize_lo = usb_lo(ep->wMaxPacketSize);
    d->u.endpoint.wMaxPacketSize_hi = usb_hi(ep->wMaxPacketSize);
    d->u.endpoint.bInterval         = ep->bInterval;
    if (ep->is_audio) {
        d->u.endpoint.bRefresh      = ep->bRefresh;
        d->u.endpoint.bSynchAddress = ep->bSynchAddress;
    }

    if (superlen) {
        USBDescriptor *d = (void *)(dest + bLength);

        d->bLength                       = 0x06;
        d->bDescriptorType               = USB_DT_ENDPOINT_COMPANION;

        d->u.super_endpoint.bMaxBurst    = ep->bMaxBurst;
        d->u.super_endpoint.bmAttributes = ep->bmAttributes_super;
        d->u.super_endpoint.wBytesPerInterval_lo =
            usb_lo(ep->wBytesPerInterval);
        d->u.super_endpoint.wBytesPerInterval_hi =
            usb_hi(ep->wBytesPerInterval);
    }

    if (ep->extra) {
        memcpy(dest + bLength + superlen, ep->extra, extralen);
    }

    return bLength + extralen + superlen;
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

static int usb_desc_cap_usb2_ext(const USBDesc *desc, uint8_t *dest, size_t len)
{
    uint8_t  bLength = 0x07;
    USBDescriptor *d = (void *)dest;

    if (len < bLength) {
        return -1;
    }

    d->bLength                          = bLength;
    d->bDescriptorType                  = USB_DT_DEVICE_CAPABILITY;
    d->u.cap.bDevCapabilityType         = USB_DEV_CAP_USB2_EXT;

    d->u.cap.u.usb2_ext.bmAttributes_1  = (1 << 1);  /* LPM */
    d->u.cap.u.usb2_ext.bmAttributes_2  = 0;
    d->u.cap.u.usb2_ext.bmAttributes_3  = 0;
    d->u.cap.u.usb2_ext.bmAttributes_4  = 0;

    return bLength;
}

static int usb_desc_cap_super(const USBDesc *desc, uint8_t *dest, size_t len)
{
    uint8_t  bLength = 0x0a;
    USBDescriptor *d = (void *)dest;

    if (len < bLength) {
        return -1;
    }

    d->bLength                           = bLength;
    d->bDescriptorType                   = USB_DT_DEVICE_CAPABILITY;
    d->u.cap.bDevCapabilityType          = USB_DEV_CAP_SUPERSPEED;

    d->u.cap.u.super.bmAttributes        = 0;
    d->u.cap.u.super.wSpeedsSupported_lo = 0;
    d->u.cap.u.super.wSpeedsSupported_hi = 0;
    d->u.cap.u.super.bFunctionalitySupport = 0;
    d->u.cap.u.super.bU1DevExitLat       = 0x0a;
    d->u.cap.u.super.wU2DevExitLat_lo    = 0x20;
    d->u.cap.u.super.wU2DevExitLat_hi    = 0;

    if (desc->full) {
        d->u.cap.u.super.wSpeedsSupported_lo |= (1 << 1);
        d->u.cap.u.super.bFunctionalitySupport = 1;
    }
    if (desc->high) {
        d->u.cap.u.super.wSpeedsSupported_lo |= (1 << 2);
        if (!d->u.cap.u.super.bFunctionalitySupport) {
            d->u.cap.u.super.bFunctionalitySupport = 2;
        }
    }
    if (desc->super) {
        d->u.cap.u.super.wSpeedsSupported_lo |= (1 << 3);
        if (!d->u.cap.u.super.bFunctionalitySupport) {
            d->u.cap.u.super.bFunctionalitySupport = 3;
        }
    }

    return bLength;
}

static int usb_desc_bos(const USBDesc *desc, uint8_t *dest, size_t len)
{
    uint8_t  bLength = 0x05;
    uint16_t wTotalLength = 0;
    uint8_t  bNumDeviceCaps = 0;
    USBDescriptor *d = (void *)dest;
    int rc;

    if (len < bLength) {
        return -1;
    }

    d->bLength                      = bLength;
    d->bDescriptorType              = USB_DT_BOS;

    wTotalLength += bLength;

    if (desc->high != NULL) {
        rc = usb_desc_cap_usb2_ext(desc, dest + wTotalLength,
                                   len - wTotalLength);
        if (rc < 0) {
            return rc;
        }
        wTotalLength += rc;
        bNumDeviceCaps++;
    }

    if (desc->super != NULL) {
        rc = usb_desc_cap_super(desc, dest + wTotalLength,
                                len - wTotalLength);
        if (rc < 0) {
            return rc;
        }
        wTotalLength += rc;
        bNumDeviceCaps++;
    }

    d->u.bos.wTotalLength_lo = usb_lo(wTotalLength);
    d->u.bos.wTotalLength_hi = usb_hi(wTotalLength);
    d->u.bos.bNumDeviceCaps  = bNumDeviceCaps;
    return wTotalLength;
}

/* ------------------------------------------------------------------ */

static void usb_desc_ep_init(USBDevice *dev)
{
    const USBDescIface *iface;
    int i, e, pid, ep;

    usb_ep_init(dev);
    for (i = 0; i < dev->ninterfaces; i++) {
        iface = dev->ifaces[i];
        if (iface == NULL) {
            continue;
        }
        for (e = 0; e < iface->bNumEndpoints; e++) {
            pid = (iface->eps[e].bEndpointAddress & USB_DIR_IN) ?
                USB_TOKEN_IN : USB_TOKEN_OUT;
            ep = iface->eps[e].bEndpointAddress & 0x0f;
            usb_ep_set_type(dev, pid, ep, iface->eps[e].bmAttributes & 0x03);
            usb_ep_set_ifnum(dev, pid, ep, iface->bInterfaceNumber);
            usb_ep_set_max_packet_size(dev, pid, ep,
                                       iface->eps[e].wMaxPacketSize);
            usb_ep_set_max_streams(dev, pid, ep,
                                   iface->eps[e].bmAttributes_super);
        }
    }
}

static const USBDescIface *usb_desc_find_interface(USBDevice *dev,
                                                   int nif, int alt)
{
    const USBDescIface *iface;
    int g, i;

    if (!dev->config) {
        return NULL;
    }
    for (g = 0; g < dev->config->nif_groups; g++) {
        for (i = 0; i < dev->config->if_groups[g].nif; i++) {
            iface = &dev->config->if_groups[g].ifs[i];
            if (iface->bInterfaceNumber == nif &&
                iface->bAlternateSetting == alt) {
                return iface;
            }
        }
    }
    for (i = 0; i < dev->config->nif; i++) {
        iface = &dev->config->ifs[i];
        if (iface->bInterfaceNumber == nif &&
            iface->bAlternateSetting == alt) {
            return iface;
        }
    }
    return NULL;
}

static int usb_desc_set_interface(USBDevice *dev, int index, int value)
{
    const USBDescIface *iface;
    int old;

    iface = usb_desc_find_interface(dev, index, value);
    if (iface == NULL) {
        return -1;
    }

    old = dev->altsetting[index];
    dev->altsetting[index] = value;
    dev->ifaces[index] = iface;
    usb_desc_ep_init(dev);

    if (old != value) {
        usb_device_set_interface(dev, index, old, value);
    }
    return 0;
}

static int usb_desc_set_config(USBDevice *dev, int value)
{
    int i;

    if (value == 0) {
        dev->configuration = 0;
        dev->ninterfaces   = 0;
        dev->config = NULL;
    } else {
        for (i = 0; i < dev->device->bNumConfigurations; i++) {
            if (dev->device->confs[i].bConfigurationValue == value) {
                dev->configuration = value;
                dev->ninterfaces   = dev->device->confs[i].bNumInterfaces;
                dev->config = dev->device->confs + i;
                assert(dev->ninterfaces <= USB_MAX_INTERFACES);
            }
        }
        if (i < dev->device->bNumConfigurations) {
            return -1;
        }
    }

    for (i = 0; i < dev->ninterfaces; i++) {
        usb_desc_set_interface(dev, i, 0);
    }
    for (; i < USB_MAX_INTERFACES; i++) {
        dev->altsetting[i] = 0;
        dev->ifaces[i] = NULL;
    }

    return 0;
}

static void usb_desc_setdefaults(USBDevice *dev)
{
    const USBDesc *desc = usb_device_get_usb_desc(dev);

    assert(desc != NULL);
    switch (dev->speed) {
    case USB_SPEED_LOW:
    case USB_SPEED_FULL:
        dev->device = desc->full;
        break;
    case USB_SPEED_HIGH:
        dev->device = desc->high;
        break;
    case USB_SPEED_SUPER:
        dev->device = desc->super;
        break;
    }
    usb_desc_set_config(dev, 0);
}

void usb_desc_init(USBDevice *dev)
{
    const USBDesc *desc = usb_device_get_usb_desc(dev);

    assert(desc != NULL);
    dev->speed = USB_SPEED_FULL;
    dev->speedmask = 0;
    if (desc->full) {
        dev->speedmask |= USB_SPEED_MASK_FULL;
    }
    if (desc->high) {
        dev->speedmask |= USB_SPEED_MASK_HIGH;
    }
    if (desc->super) {
        dev->speedmask |= USB_SPEED_MASK_SUPER;
    }
    if (desc->msos && (dev->flags & (1 << USB_DEV_FLAG_MSOS_DESC_ENABLE))) {
        dev->flags |= (1 << USB_DEV_FLAG_MSOS_DESC_IN_USE);
        usb_desc_set_string(dev, 0xee, "MSFT100Q");
    }
    usb_desc_setdefaults(dev);
}

void usb_desc_attach(USBDevice *dev)
{
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
        s = g_malloc0(sizeof(*s));
        s->index = index;
        QLIST_INSERT_HEAD(&dev->strings, s, next);
    }
    g_free(s->str);
    s->str = g_strdup(str);
}

/*
 * This function creates a serial number for a usb device.
 * The serial number should:
 *   (a) Be unique within the virtual machine.
 *   (b) Be constant, so you don't get a new one each
 *       time the guest is started.
 * So we are using the physical location to generate a serial number
 * from it.  It has three pieces:  First a fixed, device-specific
 * prefix.  Second the device path of the host controller (which is
 * the pci address in most cases).  Third the physical port path.
 * Results in serial numbers like this: "314159-0000:00:1d.7-3".
 */
void usb_desc_create_serial(USBDevice *dev)
{
    DeviceState *hcd = dev->qdev.parent_bus->parent;
    const USBDesc *desc = usb_device_get_usb_desc(dev);
    int index = desc->id.iSerialNumber;
    char *path, *serial;

    if (dev->serial) {
        /* 'serial' usb bus property has priority if present */
        usb_desc_set_string(dev, index, dev->serial);
        return;
    }

    assert(index != 0 && desc->str[index] != NULL);
    path = qdev_get_dev_path(hcd);
    if (path) {
        serial = g_strdup_printf("%s-%s-%s", desc->str[index],
                                 path, dev->port->path);
    } else {
        serial = g_strdup_printf("%s-%s", desc->str[index], dev->port->path);
    }
    usb_desc_set_string(dev, index, serial);
    g_free(path);
    g_free(serial);
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
        str = usb_device_get_usb_desc(dev)->str[index];
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

int usb_desc_get_descriptor(USBDevice *dev, USBPacket *p,
                            int value, uint8_t *dest, size_t len)
{
    bool msos = (dev->flags & (1 << USB_DEV_FLAG_MSOS_DESC_IN_USE));
    const USBDesc *desc = usb_device_get_usb_desc(dev);
    const USBDescDevice *other_dev;
    uint8_t buf[256];
    uint8_t type = value >> 8;
    uint8_t index = value & 0xff;
    int flags, ret = -1;

    if (dev->speed == USB_SPEED_HIGH) {
        other_dev = usb_device_get_usb_desc(dev)->full;
    } else {
        other_dev = usb_device_get_usb_desc(dev)->high;
    }

    flags = 0;
    if (dev->device->bcdUSB >= 0x0300) {
        flags |= USB_DESC_FLAG_SUPER;
    }

    switch(type) {
    case USB_DT_DEVICE:
        ret = usb_desc_device(&desc->id, dev->device, msos, buf, sizeof(buf));
        trace_usb_desc_device(dev->addr, len, ret);
        break;
    case USB_DT_CONFIG:
        if (index < dev->device->bNumConfigurations) {
            ret = usb_desc_config(dev->device->confs + index, flags,
                                  buf, sizeof(buf));
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
            ret = usb_desc_config(other_dev->confs + index, flags,
                                  buf, sizeof(buf));
            buf[0x01] = USB_DT_OTHER_SPEED_CONFIG;
        }
        trace_usb_desc_other_speed_config(dev->addr, index, len, ret);
        break;
    case USB_DT_BOS:
        ret = usb_desc_bos(desc, buf, sizeof(buf));
        trace_usb_desc_bos(dev->addr, len, ret);
        break;

    case USB_DT_DEBUG:
        /* ignore silently */
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
        p->actual_length = ret;
        ret = 0;
    }
    return ret;
}

int usb_desc_handle_control(USBDevice *dev, USBPacket *p,
        int request, int value, int index, int length, uint8_t *data)
{
    bool msos = (dev->flags & (1 << USB_DEV_FLAG_MSOS_DESC_IN_USE));
    const USBDesc *desc = usb_device_get_usb_desc(dev);
    int ret = -1;

    assert(desc != NULL);
    switch(request) {
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        dev->addr = value;
        trace_usb_set_addr(dev->addr);
        ret = 0;
        break;

    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        ret = usb_desc_get_descriptor(dev, p, value, data, length);
        break;

    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        /*
         * 9.4.2: 0 should be returned if the device is unconfigured, otherwise
         * the non zero value of bConfigurationValue.
         */
        data[0] = dev->config ? dev->config->bConfigurationValue : 0;
        p->actual_length = 1;
        ret = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        ret = usb_desc_set_config(dev, value);
        trace_usb_set_config(dev->addr, value, ret);
        break;

    case DeviceRequest | USB_REQ_GET_STATUS: {
        const USBDescConfig *config = dev->config ?
            dev->config : &dev->device->confs[0];

        data[0] = 0;
        /*
         * Default state: Device behavior when this request is received while
         *                the device is in the Default state is not specified.
         * We return the same value that a configured device would return if
         * it used the first configuration.
         */
        if (config->bmAttributes & USB_CFG_ATT_SELFPOWER) {
            data[0] |= 1 << USB_DEVICE_SELF_POWERED;
        }
        if (dev->remote_wakeup) {
            data[0] |= 1 << USB_DEVICE_REMOTE_WAKEUP;
        }
        data[1] = 0x00;
        p->actual_length = 2;
        ret = 0;
        break;
    }
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

    case DeviceOutRequest | USB_REQ_SET_SEL:
    case DeviceOutRequest | USB_REQ_SET_ISOCH_DELAY:
        if (dev->speed == USB_SPEED_SUPER) {
            ret = 0;
        }
        break;

    case InterfaceRequest | USB_REQ_GET_INTERFACE:
        if (index < 0 || index >= dev->ninterfaces) {
            break;
        }
        data[0] = dev->altsetting[index];
        p->actual_length = 1;
        ret = 0;
        break;
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        ret = usb_desc_set_interface(dev, index, value);
        trace_usb_set_interface(dev->addr, index, value, ret);
        break;

    case VendorDeviceRequest | 'Q':
        if (msos) {
            ret = usb_desc_msos(desc, p, index, data, length);
            trace_usb_desc_msos(dev->addr, index, length, ret);
        }
        break;
    case VendorInterfaceRequest | 'Q':
        if (msos) {
            ret = usb_desc_msos(desc, p, index, data, length);
            trace_usb_desc_msos(dev->addr, index, length, ret);
        }
        break;

    }
    return ret;
}
