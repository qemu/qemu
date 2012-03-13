#ifndef QEMU_USB_H
#define QEMU_USB_H

/*
 * QEMU USB API
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

#include "block.h"
#include "qdev.h"
#include "qemu-queue.h"

/* Constants related to the USB / PCI interaction */
#define USB_SBRN    0x60 /* Serial Bus Release Number Register */
#define USB_RELEASE_1  0x10 /* USB 1.0 */
#define USB_RELEASE_2  0x20 /* USB 2.0 */
#define USB_RELEASE_3  0x30 /* USB 3.0 */

#define USB_TOKEN_SETUP 0x2d
#define USB_TOKEN_IN    0x69 /* device -> host */
#define USB_TOKEN_OUT   0xe1 /* host -> device */

#define USB_RET_NODEV   (-1)
#define USB_RET_NAK     (-2)
#define USB_RET_STALL   (-3)
#define USB_RET_BABBLE  (-4)
#define USB_RET_IOERROR (-5)
#define USB_RET_ASYNC   (-6)

#define USB_SPEED_LOW   0
#define USB_SPEED_FULL  1
#define USB_SPEED_HIGH  2
#define USB_SPEED_SUPER 3

#define USB_SPEED_MASK_LOW   (1 << USB_SPEED_LOW)
#define USB_SPEED_MASK_FULL  (1 << USB_SPEED_FULL)
#define USB_SPEED_MASK_HIGH  (1 << USB_SPEED_HIGH)
#define USB_SPEED_MASK_SUPER (1 << USB_SPEED_SUPER)

#define USB_STATE_NOTATTACHED 0
#define USB_STATE_ATTACHED    1
//#define USB_STATE_POWERED     2
#define USB_STATE_DEFAULT     3
//#define USB_STATE_ADDRESS     4
//#define	USB_STATE_CONFIGURED  5
#define USB_STATE_SUSPENDED   6

#define USB_CLASS_AUDIO			1
#define USB_CLASS_COMM			2
#define USB_CLASS_HID			3
#define USB_CLASS_PHYSICAL		5
#define USB_CLASS_STILL_IMAGE		6
#define USB_CLASS_PRINTER		7
#define USB_CLASS_MASS_STORAGE		8
#define USB_CLASS_HUB			9
#define USB_CLASS_CDC_DATA		0x0a
#define USB_CLASS_CSCID			0x0b
#define USB_CLASS_CONTENT_SEC		0x0d
#define USB_CLASS_APP_SPEC		0xfe
#define USB_CLASS_VENDOR_SPEC		0xff

#define USB_SUBCLASS_UNDEFINED          0
#define USB_SUBCLASS_AUDIO_CONTROL      1
#define USB_SUBCLASS_AUDIO_STREAMING    2
#define USB_SUBCLASS_AUDIO_MIDISTREAMING 3

#define USB_DIR_OUT			0
#define USB_DIR_IN			0x80

#define USB_TYPE_MASK			(0x03 << 5)
#define USB_TYPE_STANDARD		(0x00 << 5)
#define USB_TYPE_CLASS			(0x01 << 5)
#define USB_TYPE_VENDOR			(0x02 << 5)
#define USB_TYPE_RESERVED		(0x03 << 5)

#define USB_RECIP_MASK			0x1f
#define USB_RECIP_DEVICE		0x00
#define USB_RECIP_INTERFACE		0x01
#define USB_RECIP_ENDPOINT		0x02
#define USB_RECIP_OTHER			0x03

#define DeviceRequest ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_DEVICE)<<8)
#define DeviceOutRequest ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_DEVICE)<<8)
#define InterfaceRequest \
        ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_INTERFACE)<<8)
#define InterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_INTERFACE)<<8)
#define EndpointRequest ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)
#define EndpointOutRequest \
        ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)
#define ClassInterfaceRequest \
        ((USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_INTERFACE)<<8)
#define ClassInterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_CLASS|USB_RECIP_INTERFACE)<<8)

#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
#define USB_REQ_SET_FEATURE		0x03
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_GET_INTERFACE		0x0A
#define USB_REQ_SET_INTERFACE		0x0B
#define USB_REQ_SYNCH_FRAME		0x0C

#define USB_DEVICE_SELF_POWERED		0
#define USB_DEVICE_REMOTE_WAKEUP	1

#define USB_DT_DEVICE			0x01
#define USB_DT_CONFIG			0x02
#define USB_DT_STRING			0x03
#define USB_DT_INTERFACE		0x04
#define USB_DT_ENDPOINT			0x05
#define USB_DT_DEVICE_QUALIFIER         0x06
#define USB_DT_OTHER_SPEED_CONFIG       0x07
#define USB_DT_DEBUG                    0x0A
#define USB_DT_INTERFACE_ASSOC          0x0B
#define USB_DT_CS_INTERFACE             0x24
#define USB_DT_CS_ENDPOINT              0x25

#define USB_ENDPOINT_XFER_CONTROL	0
#define USB_ENDPOINT_XFER_ISOC		1
#define USB_ENDPOINT_XFER_BULK		2
#define USB_ENDPOINT_XFER_INT		3
#define USB_ENDPOINT_XFER_INVALID     255

typedef struct USBBus USBBus;
typedef struct USBBusOps USBBusOps;
typedef struct USBPort USBPort;
typedef struct USBDevice USBDevice;
typedef struct USBPacket USBPacket;
typedef struct USBEndpoint USBEndpoint;

typedef struct USBDesc USBDesc;
typedef struct USBDescID USBDescID;
typedef struct USBDescDevice USBDescDevice;
typedef struct USBDescConfig USBDescConfig;
typedef struct USBDescIfaceAssoc USBDescIfaceAssoc;
typedef struct USBDescIface USBDescIface;
typedef struct USBDescEndpoint USBDescEndpoint;
typedef struct USBDescOther USBDescOther;
typedef struct USBDescString USBDescString;

struct USBDescString {
    uint8_t index;
    char *str;
    QLIST_ENTRY(USBDescString) next;
};

#define USB_MAX_ENDPOINTS  15
#define USB_MAX_INTERFACES 16

struct USBEndpoint {
    uint8_t nr;
    uint8_t pid;
    uint8_t type;
    uint8_t ifnum;
    int max_packet_size;
    bool pipeline;
    USBDevice *dev;
    QTAILQ_HEAD(, USBPacket) queue;
};

/* definition of a USB device */
struct USBDevice {
    DeviceState qdev;
    USBPort *port;
    char *port_path;
    void *opaque;

    /* Actual connected speed */
    int speed;
    /* Supported speeds, not in info because it may be variable (hostdevs) */
    int speedmask;
    uint8_t addr;
    char product_desc[32];
    int auto_attach;
    int attached;

    int32_t state;
    uint8_t setup_buf[8];
    uint8_t data_buf[4096];
    int32_t remote_wakeup;
    int32_t setup_state;
    int32_t setup_len;
    int32_t setup_index;

    USBEndpoint ep_ctl;
    USBEndpoint ep_in[USB_MAX_ENDPOINTS];
    USBEndpoint ep_out[USB_MAX_ENDPOINTS];

    QLIST_HEAD(, USBDescString) strings;
    const USBDescDevice *device;

    int configuration;
    int ninterfaces;
    int altsetting[USB_MAX_INTERFACES];
    const USBDescConfig *config;
    const USBDescIface  *ifaces[USB_MAX_INTERFACES];
};

#define TYPE_USB_DEVICE "usb-device"
#define USB_DEVICE(obj) \
     OBJECT_CHECK(USBDevice, (obj), TYPE_USB_DEVICE)
#define USB_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(USBDeviceClass, (klass), TYPE_USB_DEVICE)
#define USB_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(USBDeviceClass, (obj), TYPE_USB_DEVICE)

typedef struct USBDeviceClass {
    DeviceClass parent_class;

    int (*init)(USBDevice *dev);

    /*
     * Walk (enabled) downstream ports, check for a matching device.
     * Only hubs implement this.
     */
    USBDevice *(*find_device)(USBDevice *dev, uint8_t addr);

    /*
     * Called when a packet is canceled.
     */
    void (*cancel_packet)(USBDevice *dev, USBPacket *p);

    /*
     * Called when device is destroyed.
     */
    void (*handle_destroy)(USBDevice *dev);

    /*
     * Attach the device
     */
    void (*handle_attach)(USBDevice *dev);

    /*
     * Reset the device
     */
    void (*handle_reset)(USBDevice *dev);

    /*
     * Process control request.
     * Called from handle_packet().
     *
     * Returns length or one of the USB_RET_ codes.
     */
    int (*handle_control)(USBDevice *dev, USBPacket *p, int request, int value,
                          int index, int length, uint8_t *data);

    /*
     * Process data transfers (both BULK and ISOC).
     * Called from handle_packet().
     *
     * Returns length or one of the USB_RET_ codes.
     */
    int (*handle_data)(USBDevice *dev, USBPacket *p);

    void (*set_interface)(USBDevice *dev, int interface,
                          int alt_old, int alt_new);

    const char *product_desc;
    const USBDesc *usb_desc;
} USBDeviceClass;

typedef struct USBPortOps {
    void (*attach)(USBPort *port);
    void (*detach)(USBPort *port);
    /*
     * This gets called when a device downstream from the device attached to
     * the port (iow attached through a hub) gets detached.
     */
    void (*child_detach)(USBPort *port, USBDevice *child);
    void (*wakeup)(USBPort *port);
    /*
     * Note that port->dev will be different then the device from which
     * the packet originated when a hub is involved.
     */
    void (*complete)(USBPort *port, USBPacket *p);
} USBPortOps;

/* USB port on which a device can be connected */
struct USBPort {
    USBDevice *dev;
    int speedmask;
    char path[16];
    USBPortOps *ops;
    void *opaque;
    int index; /* internal port index, may be used with the opaque */
    QTAILQ_ENTRY(USBPort) next;
};

typedef void USBCallback(USBPacket * packet, void *opaque);

typedef enum USBPacketState {
    USB_PACKET_UNDEFINED = 0,
    USB_PACKET_SETUP,
    USB_PACKET_QUEUED,
    USB_PACKET_ASYNC,
    USB_PACKET_COMPLETE,
    USB_PACKET_CANCELED,
} USBPacketState;

/* Structure used to hold information about an active USB packet.  */
struct USBPacket {
    /* Data fields for use by the driver.  */
    int pid;
    USBEndpoint *ep;
    QEMUIOVector iov;
    uint64_t parameter; /* control transfers */
    int result; /* transfer length or USB_RET_* status code */
    /* Internal use by the USB layer.  */
    USBPacketState state;
    QTAILQ_ENTRY(USBPacket) queue;
};

void usb_packet_init(USBPacket *p);
void usb_packet_set_state(USBPacket *p, USBPacketState state);
void usb_packet_check_state(USBPacket *p, USBPacketState expected);
void usb_packet_setup(USBPacket *p, int pid, USBEndpoint *ep);
void usb_packet_addbuf(USBPacket *p, void *ptr, size_t len);
int usb_packet_map(USBPacket *p, QEMUSGList *sgl);
void usb_packet_unmap(USBPacket *p);
void usb_packet_copy(USBPacket *p, void *ptr, size_t bytes);
void usb_packet_skip(USBPacket *p, size_t bytes);
void usb_packet_cleanup(USBPacket *p);

static inline bool usb_packet_is_inflight(USBPacket *p)
{
    return (p->state == USB_PACKET_QUEUED ||
            p->state == USB_PACKET_ASYNC);
}

USBDevice *usb_find_device(USBPort *port, uint8_t addr);

int usb_handle_packet(USBDevice *dev, USBPacket *p);
void usb_packet_complete(USBDevice *dev, USBPacket *p);
void usb_cancel_packet(USBPacket * p);

void usb_ep_init(USBDevice *dev);
void usb_ep_dump(USBDevice *dev);
struct USBEndpoint *usb_ep_get(USBDevice *dev, int pid, int ep);
uint8_t usb_ep_get_type(USBDevice *dev, int pid, int ep);
uint8_t usb_ep_get_ifnum(USBDevice *dev, int pid, int ep);
void usb_ep_set_type(USBDevice *dev, int pid, int ep, uint8_t type);
void usb_ep_set_ifnum(USBDevice *dev, int pid, int ep, uint8_t ifnum);
void usb_ep_set_max_packet_size(USBDevice *dev, int pid, int ep,
                                uint16_t raw);
int usb_ep_get_max_packet_size(USBDevice *dev, int pid, int ep);
void usb_ep_set_pipeline(USBDevice *dev, int pid, int ep, bool enabled);

void usb_attach(USBPort *port);
void usb_detach(USBPort *port);
void usb_port_reset(USBPort *port);
void usb_device_reset(USBDevice *dev);
void usb_wakeup(USBEndpoint *ep);
void usb_generic_async_ctrl_complete(USBDevice *s, USBPacket *p);
int set_usb_string(uint8_t *buf, const char *str);

/* usb-linux.c */
USBDevice *usb_host_device_open(USBBus *bus, const char *devname);
int usb_host_device_close(const char *devname);
void usb_host_info(Monitor *mon);

/* usb-bt.c */
USBDevice *usb_bt_init(USBBus *bus, HCIInfo *hci);

/* usb ports of the VM */

#define VM_USB_HUB_SIZE 8

/* usb-musb.c */
enum musb_irq_source_e {
    musb_irq_suspend = 0,
    musb_irq_resume,
    musb_irq_rst_babble,
    musb_irq_sof,
    musb_irq_connect,
    musb_irq_disconnect,
    musb_irq_vbus_request,
    musb_irq_vbus_error,
    musb_irq_rx,
    musb_irq_tx,
    musb_set_vbus,
    musb_set_session,
    /* Add new interrupts here */
    musb_irq_max, /* total number of interrupts defined */
};

typedef struct MUSBState MUSBState;
MUSBState *musb_init(DeviceState *parent_device, int gpio_base);
void musb_reset(MUSBState *s);
uint32_t musb_core_intr_get(MUSBState *s);
void musb_core_intr_clear(MUSBState *s, uint32_t mask);
void musb_set_size(MUSBState *s, int epnum, int size, int is_tx);

/* usb-bus.c */

struct USBBus {
    BusState qbus;
    USBBusOps *ops;
    int busnr;
    int nfree;
    int nused;
    QTAILQ_HEAD(, USBPort) free;
    QTAILQ_HEAD(, USBPort) used;
    QTAILQ_ENTRY(USBBus) next;
};

struct USBBusOps {
    int (*register_companion)(USBBus *bus, USBPort *ports[],
                              uint32_t portcount, uint32_t firstport);
    void (*wakeup_endpoint)(USBBus *bus, USBEndpoint *ep);
};

void usb_bus_new(USBBus *bus, USBBusOps *ops, DeviceState *host);
USBBus *usb_bus_find(int busnr);
void usb_legacy_register(const char *typename, const char *usbdevice_name,
                         USBDevice *(*usbdevice_init)(USBBus *bus,
                                                      const char *params));
USBDevice *usb_create(USBBus *bus, const char *name);
USBDevice *usb_create_simple(USBBus *bus, const char *name);
USBDevice *usbdevice_create(const char *cmdline);
void usb_register_port(USBBus *bus, USBPort *port, void *opaque, int index,
                       USBPortOps *ops, int speedmask);
int usb_register_companion(const char *masterbus, USBPort *ports[],
                           uint32_t portcount, uint32_t firstport,
                           void *opaque, USBPortOps *ops, int speedmask);
void usb_port_location(USBPort *downstream, USBPort *upstream, int portnr);
void usb_unregister_port(USBBus *bus, USBPort *port);
int usb_claim_port(USBDevice *dev);
void usb_release_port(USBDevice *dev);
int usb_device_attach(USBDevice *dev);
int usb_device_detach(USBDevice *dev);
int usb_device_delete_addr(int busnr, int addr);

static inline USBBus *usb_bus_from_device(USBDevice *d)
{
    return DO_UPCAST(USBBus, qbus, d->qdev.parent_bus);
}

extern const VMStateDescription vmstate_usb_device;

#define VMSTATE_USB_DEVICE(_field, _state) {                         \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(USBDevice),                                 \
    .vmsd       = &vmstate_usb_device,                               \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, USBDevice),   \
}

USBDevice *usb_device_find_device(USBDevice *dev, uint8_t addr);

void usb_device_cancel_packet(USBDevice *dev, USBPacket *p);

void usb_device_handle_attach(USBDevice *dev);

void usb_device_handle_reset(USBDevice *dev);

int usb_device_handle_control(USBDevice *dev, USBPacket *p, int request, int value,
                              int index, int length, uint8_t *data);

int usb_device_handle_data(USBDevice *dev, USBPacket *p);

void usb_device_set_interface(USBDevice *dev, int interface,
                              int alt_old, int alt_new);

const char *usb_device_get_product_desc(USBDevice *dev);

const USBDesc *usb_device_get_usb_desc(USBDevice *dev);

#endif

