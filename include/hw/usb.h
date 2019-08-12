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

#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "qemu/iov.h"
#include "qemu/queue.h"

/* Constants related to the USB / PCI interaction */
#define USB_SBRN    0x60 /* Serial Bus Release Number Register */
#define USB_RELEASE_1  0x10 /* USB 1.0 */
#define USB_RELEASE_2  0x20 /* USB 2.0 */
#define USB_RELEASE_3  0x30 /* USB 3.0 */

#define USB_TOKEN_SETUP 0x2d
#define USB_TOKEN_IN    0x69 /* device -> host */
#define USB_TOKEN_OUT   0xe1 /* host -> device */

#define USB_RET_SUCCESS           (0)
#define USB_RET_NODEV             (-1)
#define USB_RET_NAK               (-2)
#define USB_RET_STALL             (-3)
#define USB_RET_BABBLE            (-4)
#define USB_RET_IOERROR           (-5)
#define USB_RET_ASYNC             (-6)
#define USB_RET_ADD_TO_QUEUE      (-7)
#define USB_RET_REMOVE_FROM_QUEUE (-8)

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
#define VendorDeviceRequest ((USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)
#define VendorDeviceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)

#define InterfaceRequest                                        \
        ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_INTERFACE)<<8)
#define InterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_INTERFACE)<<8)
#define ClassInterfaceRequest \
        ((USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_INTERFACE)<<8)
#define ClassInterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_CLASS|USB_RECIP_INTERFACE)<<8)
#define VendorInterfaceRequest \
        ((USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_INTERFACE)<<8)
#define VendorInterfaceOutRequest \
        ((USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_INTERFACE)<<8)

#define EndpointRequest ((USB_DIR_IN|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)
#define EndpointOutRequest \
        ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)

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
#define USB_REQ_SET_SEL                 0x30
#define USB_REQ_SET_ISOCH_DELAY         0x31

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
#define USB_DT_BOS                      0x0F
#define USB_DT_DEVICE_CAPABILITY        0x10
#define USB_DT_CS_INTERFACE             0x24
#define USB_DT_CS_ENDPOINT              0x25
#define USB_DT_ENDPOINT_COMPANION       0x30

#define USB_DEV_CAP_WIRELESS            0x01
#define USB_DEV_CAP_USB2_EXT            0x02
#define USB_DEV_CAP_SUPERSPEED          0x03

#define USB_CFG_ATT_ONE              (1 << 7) /* should always be set */
#define USB_CFG_ATT_SELFPOWER        (1 << 6)
#define USB_CFG_ATT_WAKEUP           (1 << 5)
#define USB_CFG_ATT_BATTERY          (1 << 4)

#define USB_ENDPOINT_XFER_CONTROL	0
#define USB_ENDPOINT_XFER_ISOC		1
#define USB_ENDPOINT_XFER_BULK		2
#define USB_ENDPOINT_XFER_INT		3
#define USB_ENDPOINT_XFER_INVALID     255

#define USB_INTERFACE_INVALID         255

typedef struct USBBus USBBus;
typedef struct USBBusOps USBBusOps;
typedef struct USBPort USBPort;
typedef struct USBDevice USBDevice;
typedef struct USBPacket USBPacket;
typedef struct USBCombinedPacket USBCombinedPacket;
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
typedef struct USBDescMSOS USBDescMSOS;

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
    int max_streams;
    bool pipeline;
    bool halted;
    USBDevice *dev;
    QTAILQ_HEAD(, USBPacket) queue;
};

enum USBDeviceFlags {
    USB_DEV_FLAG_FULL_PATH,
    USB_DEV_FLAG_IS_HOST,
    USB_DEV_FLAG_MSOS_DESC_ENABLE,
    USB_DEV_FLAG_MSOS_DESC_IN_USE,
};

/* definition of a USB device */
struct USBDevice {
    DeviceState qdev;
    USBPort *port;
    char *port_path;
    char *serial;
    void *opaque;
    uint32_t flags;

    /* Actual connected speed */
    int speed;
    /* Supported speeds, not in info because it may be variable (hostdevs) */
    int speedmask;
    uint8_t addr;
    char product_desc[32];
    int auto_attach;
    bool attached;

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
    const USBDesc *usb_desc; /* Overrides class usb_desc if not NULL */
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

typedef void (*USBDeviceRealize)(USBDevice *dev, Error **errp);
typedef void (*USBDeviceUnrealize)(USBDevice *dev, Error **errp);

typedef struct USBDeviceClass {
    DeviceClass parent_class;

    USBDeviceRealize realize;
    USBDeviceUnrealize unrealize;

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
     * Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
     * then the number of bytes transferred is stored in p->actual_length
     */
    void (*handle_control)(USBDevice *dev, USBPacket *p, int request, int value,
                           int index, int length, uint8_t *data);

    /*
     * Process data transfers (both BULK and ISOC).
     * Called from handle_packet().
     *
     * Status gets stored in p->status, and if p->status == USB_RET_SUCCESS
     * then the number of bytes transferred is stored in p->actual_length
     */
    void (*handle_data)(USBDevice *dev, USBPacket *p);

    void (*set_interface)(USBDevice *dev, int interface,
                          int alt_old, int alt_new);

    /*
     * Called when the hcd is done queuing packets for an endpoint, only
     * necessary for devices which can return USB_RET_ADD_TO_QUEUE.
     */
    void (*flush_ep_queue)(USBDevice *dev, USBEndpoint *ep);

    /*
     * Called by the hcd to let the device know the queue for an endpoint
     * has been unlinked / stopped. Optional may be NULL.
     */
    void (*ep_stopped)(USBDevice *dev, USBEndpoint *ep);

    /*
     * Called by the hcd to alloc / free streams on a bulk endpoint.
     * Optional may be NULL.
     */
    int (*alloc_streams)(USBDevice *dev, USBEndpoint **eps, int nr_eps,
                         int streams);
    void (*free_streams)(USBDevice *dev, USBEndpoint **eps, int nr_eps);

    const char *product_desc;
    const USBDesc *usb_desc;
    bool attached_settable;
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
    int hubcount;
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
    uint64_t id;
    USBEndpoint *ep;
    unsigned int stream;
    QEMUIOVector iov;
    uint64_t parameter; /* control transfers */
    bool short_not_ok;
    bool int_req;
    int status; /* USB_RET_* status code */
    int actual_length; /* Number of bytes actually transferred */
    /* Internal use by the USB layer.  */
    USBPacketState state;
    USBCombinedPacket *combined;
    QTAILQ_ENTRY(USBPacket) queue;
    QTAILQ_ENTRY(USBPacket) combined_entry;
};

struct USBCombinedPacket {
    USBPacket *first;
    QTAILQ_HEAD(, USBPacket) packets;
    QEMUIOVector iov;
};

void usb_packet_init(USBPacket *p);
void usb_packet_set_state(USBPacket *p, USBPacketState state);
void usb_packet_check_state(USBPacket *p, USBPacketState expected);
void usb_packet_setup(USBPacket *p, int pid,
                      USBEndpoint *ep, unsigned int stream,
                      uint64_t id, bool short_not_ok, bool int_req);
void usb_packet_addbuf(USBPacket *p, void *ptr, size_t len);
int usb_packet_map(USBPacket *p, QEMUSGList *sgl);
void usb_packet_unmap(USBPacket *p, QEMUSGList *sgl);
void usb_packet_copy(USBPacket *p, void *ptr, size_t bytes);
void usb_packet_skip(USBPacket *p, size_t bytes);
size_t usb_packet_size(USBPacket *p);
void usb_packet_cleanup(USBPacket *p);

static inline bool usb_packet_is_inflight(USBPacket *p)
{
    return (p->state == USB_PACKET_QUEUED ||
            p->state == USB_PACKET_ASYNC);
}

USBDevice *usb_find_device(USBPort *port, uint8_t addr);

void usb_handle_packet(USBDevice *dev, USBPacket *p);
void usb_packet_complete(USBDevice *dev, USBPacket *p);
void usb_packet_complete_one(USBDevice *dev, USBPacket *p);
void usb_cancel_packet(USBPacket * p);

void usb_ep_init(USBDevice *dev);
void usb_ep_reset(USBDevice *dev);
void usb_ep_dump(USBDevice *dev);
struct USBEndpoint *usb_ep_get(USBDevice *dev, int pid, int ep);
uint8_t usb_ep_get_type(USBDevice *dev, int pid, int ep);
void usb_ep_set_type(USBDevice *dev, int pid, int ep, uint8_t type);
void usb_ep_set_ifnum(USBDevice *dev, int pid, int ep, uint8_t ifnum);
void usb_ep_set_max_packet_size(USBDevice *dev, int pid, int ep,
                                uint16_t raw);
void usb_ep_set_max_streams(USBDevice *dev, int pid, int ep, uint8_t raw);
void usb_ep_set_halted(USBDevice *dev, int pid, int ep, bool halted);
USBPacket *usb_ep_find_packet_by_id(USBDevice *dev, int pid, int ep,
                                    uint64_t id);

void usb_ep_combine_input_packets(USBEndpoint *ep);
void usb_combined_input_packet_complete(USBDevice *dev, USBPacket *p);
void usb_combined_packet_cancel(USBDevice *dev, USBPacket *p);

void usb_pick_speed(USBPort *port);
void usb_attach(USBPort *port);
void usb_detach(USBPort *port);
void usb_port_reset(USBPort *port);
void usb_device_reset(USBDevice *dev);
void usb_wakeup(USBEndpoint *ep, unsigned int stream);
void usb_generic_async_ctrl_complete(USBDevice *s, USBPacket *p);

/* usb-linux.c */
void hmp_info_usbhost(Monitor *mon, const QDict *qdict);
bool usb_host_dev_is_scsi_storage(USBDevice *usbdev);

/* usb ports of the VM */

#define VM_USB_HUB_SIZE 8

/* hw/usb/hdc-musb.c */

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

extern CPUReadMemoryFunc * const musb_read[];
extern CPUWriteMemoryFunc * const musb_write[];

MUSBState *musb_init(DeviceState *parent_device, int gpio_base);
void musb_reset(MUSBState *s);
uint32_t musb_core_intr_get(MUSBState *s);
void musb_core_intr_clear(MUSBState *s, uint32_t mask);
void musb_set_size(MUSBState *s, int epnum, int size, int is_tx);

/* usb-bus.c */

#define TYPE_USB_BUS "usb-bus"
#define USB_BUS(obj) OBJECT_CHECK(USBBus, (obj), TYPE_USB_BUS)

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
    void (*register_companion)(USBBus *bus, USBPort *ports[],
                               uint32_t portcount, uint32_t firstport,
                               Error **errp);
    void (*wakeup_endpoint)(USBBus *bus, USBEndpoint *ep, unsigned int stream);
};

void usb_bus_new(USBBus *bus, size_t bus_size,
                 USBBusOps *ops, DeviceState *host);
void usb_bus_release(USBBus *bus);
USBBus *usb_bus_find(int busnr);
void usb_legacy_register(const char *typename, const char *usbdevice_name,
                         USBDevice *(*usbdevice_init)(USBBus *bus,
                                                      const char *params));
USBDevice *usb_create(USBBus *bus, const char *name);
USBDevice *usb_create_simple(USBBus *bus, const char *name);
USBDevice *usbdevice_create(const char *cmdline);
void usb_register_port(USBBus *bus, USBPort *port, void *opaque, int index,
                       USBPortOps *ops, int speedmask);
void usb_register_companion(const char *masterbus, USBPort *ports[],
                            uint32_t portcount, uint32_t firstport,
                            void *opaque, USBPortOps *ops, int speedmask,
                            Error **errp);
void usb_port_location(USBPort *downstream, USBPort *upstream, int portnr);
void usb_unregister_port(USBBus *bus, USBPort *port);
void usb_claim_port(USBDevice *dev, Error **errp);
void usb_release_port(USBDevice *dev);
void usb_device_attach(USBDevice *dev, Error **errp);
int usb_device_detach(USBDevice *dev);
void usb_check_attach(USBDevice *dev, Error **errp);

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

void usb_device_handle_control(USBDevice *dev, USBPacket *p, int request,
                               int val, int index, int length, uint8_t *data);

void usb_device_handle_data(USBDevice *dev, USBPacket *p);

void usb_device_set_interface(USBDevice *dev, int interface,
                              int alt_old, int alt_new);

void usb_device_flush_ep_queue(USBDevice *dev, USBEndpoint *ep);

void usb_device_ep_stopped(USBDevice *dev, USBEndpoint *ep);

int usb_device_alloc_streams(USBDevice *dev, USBEndpoint **eps, int nr_eps,
                             int streams);
void usb_device_free_streams(USBDevice *dev, USBEndpoint **eps, int nr_eps);

const char *usb_device_get_product_desc(USBDevice *dev);

const USBDesc *usb_device_get_usb_desc(USBDevice *dev);

/* quirks.c */

/* In bulk endpoints are streaming data sources (iow behave like isoc eps) */
#define USB_QUIRK_BUFFER_BULK_IN	0x01
/* Bulk pkts in FTDI format, need special handling when combining packets */
#define USB_QUIRK_IS_FTDI		0x02

int usb_get_quirks(uint16_t vendor_id, uint16_t product_id,
                   uint8_t interface_class, uint8_t interface_subclass,
                   uint8_t interface_protocol);

#endif
