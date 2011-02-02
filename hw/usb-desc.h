#ifndef QEMU_HW_USB_DESC_H
#define QEMU_HW_USB_DESC_H

#include <inttypes.h>

struct USBDescID {
    uint16_t                  idVendor;
    uint16_t                  idProduct;
    uint16_t                  bcdDevice;
    uint8_t                   iManufacturer;
    uint8_t                   iProduct;
    uint8_t                   iSerialNumber;
};

struct USBDescDevice {
    uint16_t                  bcdUSB;
    uint8_t                   bDeviceClass;
    uint8_t                   bDeviceSubClass;
    uint8_t                   bDeviceProtocol;
    uint8_t                   bMaxPacketSize0;
    uint8_t                   bNumConfigurations;

    const USBDescConfig       *confs;
};

struct USBDescConfig {
    uint8_t                   bNumInterfaces;
    uint8_t                   bConfigurationValue;
    uint8_t                   iConfiguration;
    uint8_t                   bmAttributes;
    uint8_t                   bMaxPower;

    /* grouped interfaces */
    uint8_t                   nif_groups;
    const USBDescIfaceAssoc   *if_groups;

    /* "normal" interfaces */
    uint8_t                   nif;
    const USBDescIface        *ifs;
};

/* conceptually an Interface Association Descriptor, and releated interfaces */
struct USBDescIfaceAssoc {
    uint8_t                   bFirstInterface;
    uint8_t                   bInterfaceCount;
    uint8_t                   bFunctionClass;
    uint8_t                   bFunctionSubClass;
    uint8_t                   bFunctionProtocol;
    uint8_t                   iFunction;

    uint8_t                   nif;
    const USBDescIface        *ifs;
};

struct USBDescIface {
    uint8_t                   bInterfaceNumber;
    uint8_t                   bAlternateSetting;
    uint8_t                   bNumEndpoints;
    uint8_t                   bInterfaceClass;
    uint8_t                   bInterfaceSubClass;
    uint8_t                   bInterfaceProtocol;
    uint8_t                   iInterface;

    uint8_t                   ndesc;
    USBDescOther              *descs;
    USBDescEndpoint           *eps;
};

struct USBDescEndpoint {
    uint8_t                   bEndpointAddress;
    uint8_t                   bmAttributes;
    uint16_t                  wMaxPacketSize;
    uint8_t                   bInterval;
};

struct USBDescOther {
    uint8_t                   length;
    uint8_t                   *data;
};

typedef const char *USBDescStrings[256];

struct USBDesc {
    USBDescID                 id;
    const USBDescDevice       *full;
    const USBDescDevice       *high;
    const char* const         *str;
};

/* generate usb packages from structs */
int usb_desc_device(const USBDescID *id, const USBDescDevice *dev,
                    uint8_t *dest, size_t len);
int usb_desc_device_qualifier(const USBDescDevice *dev,
                              uint8_t *dest, size_t len);
int usb_desc_config(const USBDescConfig *conf, uint8_t *dest, size_t len);
int usb_desc_iface_group(const USBDescIfaceAssoc *iad, uint8_t *dest,
                         size_t len);
int usb_desc_iface(const USBDescIface *iface, uint8_t *dest, size_t len);
int usb_desc_endpoint(const USBDescEndpoint *ep, uint8_t *dest, size_t len);
int usb_desc_other(const USBDescOther *desc, uint8_t *dest, size_t len);

/* control message emulation helpers */
void usb_desc_init(USBDevice *dev);
void usb_desc_attach(USBDevice *dev);
void usb_desc_set_string(USBDevice *dev, uint8_t index, const char *str);
const char *usb_desc_get_string(USBDevice *dev, uint8_t index);
int usb_desc_string(USBDevice *dev, int index, uint8_t *dest, size_t len);
int usb_desc_get_descriptor(USBDevice *dev, int value, uint8_t *dest, size_t len);
int usb_desc_handle_control(USBDevice *dev, USBPacket *p,
        int request, int value, int index, int length, uint8_t *data);

#endif /* QEMU_HW_USB_DESC_H */
