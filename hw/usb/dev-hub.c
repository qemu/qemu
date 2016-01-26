/*
 * QEMU USB HUB emulation
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
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "trace.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "qemu/error-report.h"

#define NUM_PORTS 8

typedef struct USBHubPort {
    USBPort port;
    uint16_t wPortStatus;
    uint16_t wPortChange;
} USBHubPort;

typedef struct USBHubState {
    USBDevice dev;
    USBEndpoint *intr;
    USBHubPort ports[NUM_PORTS];
} USBHubState;

#define TYPE_USB_HUB "usb-hub"
#define USB_HUB(obj) OBJECT_CHECK(USBHubState, (obj), TYPE_USB_HUB)

#define ClearHubFeature		(0x2000 | USB_REQ_CLEAR_FEATURE)
#define ClearPortFeature	(0x2300 | USB_REQ_CLEAR_FEATURE)
#define GetHubDescriptor	(0xa000 | USB_REQ_GET_DESCRIPTOR)
#define GetHubStatus		(0xa000 | USB_REQ_GET_STATUS)
#define GetPortStatus		(0xa300 | USB_REQ_GET_STATUS)
#define SetHubFeature		(0x2000 | USB_REQ_SET_FEATURE)
#define SetPortFeature		(0x2300 | USB_REQ_SET_FEATURE)

#define PORT_STAT_CONNECTION	0x0001
#define PORT_STAT_ENABLE	0x0002
#define PORT_STAT_SUSPEND	0x0004
#define PORT_STAT_OVERCURRENT	0x0008
#define PORT_STAT_RESET		0x0010
#define PORT_STAT_POWER		0x0100
#define PORT_STAT_LOW_SPEED	0x0200
#define PORT_STAT_HIGH_SPEED    0x0400
#define PORT_STAT_TEST          0x0800
#define PORT_STAT_INDICATOR     0x1000

#define PORT_STAT_C_CONNECTION	0x0001
#define PORT_STAT_C_ENABLE	0x0002
#define PORT_STAT_C_SUSPEND	0x0004
#define PORT_STAT_C_OVERCURRENT	0x0008
#define PORT_STAT_C_RESET	0x0010

#define PORT_CONNECTION	        0
#define PORT_ENABLE		1
#define PORT_SUSPEND		2
#define PORT_OVERCURRENT	3
#define PORT_RESET		4
#define PORT_POWER		8
#define PORT_LOWSPEED		9
#define PORT_HIGHSPEED		10
#define PORT_C_CONNECTION	16
#define PORT_C_ENABLE		17
#define PORT_C_SUSPEND		18
#define PORT_C_OVERCURRENT	19
#define PORT_C_RESET		20
#define PORT_TEST               21
#define PORT_INDICATOR          22

/* same as Linux kernel root hubs */

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT]      = "QEMU USB Hub",
    [STR_SERIALNUMBER] = "314159",
};

static const USBDescIface desc_iface_hub = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 1,
    .bInterfaceClass               = USB_CLASS_HUB,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 1 + (NUM_PORTS + 7) / 8,
            .bInterval             = 0xff,
        },
    }
};

static const USBDescDevice desc_device_hub = {
    .bcdUSB                        = 0x0110,
    .bDeviceClass                  = USB_CLASS_HUB,
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER |
                                     USB_CFG_ATT_WAKEUP,
            .nif = 1,
            .ifs = &desc_iface_hub,
        },
    },
};

static const USBDesc desc_hub = {
    .id = {
        .idVendor          = 0x0409,
        .idProduct         = 0x55aa,
        .bcdDevice         = 0x0101,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_hub,
    .str  = desc_strings,
};

static const uint8_t qemu_hub_hub_descriptor[] =
{
	0x00,			/*  u8  bLength; patched in later */
	0x29,			/*  u8  bDescriptorType; Hub-descriptor */
	0x00,			/*  u8  bNbrPorts; (patched later) */
	0x0a,			/* u16  wHubCharacteristics; */
	0x00,			/*   (per-port OC, no power switching) */
	0x01,			/*  u8  bPwrOn2pwrGood; 2ms */
	0x00			/*  u8  bHubContrCurrent; 0 mA */

        /* DeviceRemovable and PortPwrCtrlMask patched in later */
};

static void usb_hub_attach(USBPort *port1)
{
    USBHubState *s = port1->opaque;
    USBHubPort *port = &s->ports[port1->index];

    trace_usb_hub_attach(s->dev.addr, port1->index + 1);
    port->wPortStatus |= PORT_STAT_CONNECTION;
    port->wPortChange |= PORT_STAT_C_CONNECTION;
    if (port->port.dev->speed == USB_SPEED_LOW) {
        port->wPortStatus |= PORT_STAT_LOW_SPEED;
    } else {
        port->wPortStatus &= ~PORT_STAT_LOW_SPEED;
    }
    usb_wakeup(s->intr, 0);
}

static void usb_hub_detach(USBPort *port1)
{
    USBHubState *s = port1->opaque;
    USBHubPort *port = &s->ports[port1->index];

    trace_usb_hub_detach(s->dev.addr, port1->index + 1);
    usb_wakeup(s->intr, 0);

    /* Let upstream know the device on this port is gone */
    s->dev.port->ops->child_detach(s->dev.port, port1->dev);

    port->wPortStatus &= ~PORT_STAT_CONNECTION;
    port->wPortChange |= PORT_STAT_C_CONNECTION;
    if (port->wPortStatus & PORT_STAT_ENABLE) {
        port->wPortStatus &= ~PORT_STAT_ENABLE;
        port->wPortChange |= PORT_STAT_C_ENABLE;
    }
    usb_wakeup(s->intr, 0);
}

static void usb_hub_child_detach(USBPort *port1, USBDevice *child)
{
    USBHubState *s = port1->opaque;

    /* Pass along upstream */
    s->dev.port->ops->child_detach(s->dev.port, child);
}

static void usb_hub_wakeup(USBPort *port1)
{
    USBHubState *s = port1->opaque;
    USBHubPort *port = &s->ports[port1->index];

    if (port->wPortStatus & PORT_STAT_SUSPEND) {
        port->wPortChange |= PORT_STAT_C_SUSPEND;
        usb_wakeup(s->intr, 0);
    }
}

static void usb_hub_complete(USBPort *port, USBPacket *packet)
{
    USBHubState *s = port->opaque;

    /*
     * Just pass it along upstream for now.
     *
     * If we ever implement usb 2.0 split transactions this will
     * become a little more complicated ...
     *
     * Can't use usb_packet_complete() here because packet->owner is
     * cleared already, go call the ->complete() callback directly
     * instead.
     */
    s->dev.port->ops->complete(s->dev.port, packet);
}

static USBDevice *usb_hub_find_device(USBDevice *dev, uint8_t addr)
{
    USBHubState *s = USB_HUB(dev);
    USBHubPort *port;
    USBDevice *downstream;
    int i;

    for (i = 0; i < NUM_PORTS; i++) {
        port = &s->ports[i];
        if (!(port->wPortStatus & PORT_STAT_ENABLE)) {
            continue;
        }
        downstream = usb_find_device(&port->port, addr);
        if (downstream != NULL) {
            return downstream;
        }
    }
    return NULL;
}

static void usb_hub_handle_reset(USBDevice *dev)
{
    USBHubState *s = USB_HUB(dev);
    USBHubPort *port;
    int i;

    trace_usb_hub_reset(s->dev.addr);
    for (i = 0; i < NUM_PORTS; i++) {
        port = s->ports + i;
        port->wPortStatus = PORT_STAT_POWER;
        port->wPortChange = 0;
        if (port->port.dev && port->port.dev->attached) {
            port->wPortStatus |= PORT_STAT_CONNECTION;
            port->wPortChange |= PORT_STAT_C_CONNECTION;
            if (port->port.dev->speed == USB_SPEED_LOW) {
                port->wPortStatus |= PORT_STAT_LOW_SPEED;
            }
        }
    }
}

static const char *feature_name(int feature)
{
    static const char *name[] = {
        [PORT_CONNECTION]    = "connection",
        [PORT_ENABLE]        = "enable",
        [PORT_SUSPEND]       = "suspend",
        [PORT_OVERCURRENT]   = "overcurrent",
        [PORT_RESET]         = "reset",
        [PORT_POWER]         = "power",
        [PORT_LOWSPEED]      = "lowspeed",
        [PORT_HIGHSPEED]     = "highspeed",
        [PORT_C_CONNECTION]  = "change connection",
        [PORT_C_ENABLE]      = "change enable",
        [PORT_C_SUSPEND]     = "change suspend",
        [PORT_C_OVERCURRENT] = "change overcurrent",
        [PORT_C_RESET]       = "change reset",
        [PORT_TEST]          = "test",
        [PORT_INDICATOR]     = "indicator",
    };
    if (feature < 0 || feature >= ARRAY_SIZE(name)) {
        return "?";
    }
    return name[feature] ?: "?";
}

static void usb_hub_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBHubState *s = (USBHubState *)dev;
    int ret;

    trace_usb_hub_control(s->dev.addr, request, value, index, length);

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch(request) {
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value == 0 && index != 0x81) { /* clear ep halt */
            goto fail;
        }
        break;
        /* usb specific requests */
    case GetHubStatus:
        data[0] = 0;
        data[1] = 0;
        data[2] = 0;
        data[3] = 0;
        p->actual_length = 4;
        break;
    case GetPortStatus:
        {
            unsigned int n = index - 1;
            USBHubPort *port;
            if (n >= NUM_PORTS) {
                goto fail;
            }
            port = &s->ports[n];
            trace_usb_hub_get_port_status(s->dev.addr, index,
                                          port->wPortStatus,
                                          port->wPortChange);
            data[0] = port->wPortStatus;
            data[1] = port->wPortStatus >> 8;
            data[2] = port->wPortChange;
            data[3] = port->wPortChange >> 8;
            p->actual_length = 4;
        }
        break;
    case SetHubFeature:
    case ClearHubFeature:
        if (value != 0 && value != 1) {
            goto fail;
        }
        break;
    case SetPortFeature:
        {
            unsigned int n = index - 1;
            USBHubPort *port;
            USBDevice *dev;

            trace_usb_hub_set_port_feature(s->dev.addr, index,
                                           feature_name(value));

            if (n >= NUM_PORTS) {
                goto fail;
            }
            port = &s->ports[n];
            dev = port->port.dev;
            switch(value) {
            case PORT_SUSPEND:
                port->wPortStatus |= PORT_STAT_SUSPEND;
                break;
            case PORT_RESET:
                if (dev && dev->attached) {
                    usb_device_reset(dev);
                    port->wPortChange |= PORT_STAT_C_RESET;
                    /* set enable bit */
                    port->wPortStatus |= PORT_STAT_ENABLE;
                    usb_wakeup(s->intr, 0);
                }
                break;
            case PORT_POWER:
                break;
            default:
                goto fail;
            }
        }
        break;
    case ClearPortFeature:
        {
            unsigned int n = index - 1;
            USBHubPort *port;

            trace_usb_hub_clear_port_feature(s->dev.addr, index,
                                             feature_name(value));

            if (n >= NUM_PORTS) {
                goto fail;
            }
            port = &s->ports[n];
            switch(value) {
            case PORT_ENABLE:
                port->wPortStatus &= ~PORT_STAT_ENABLE;
                break;
            case PORT_C_ENABLE:
                port->wPortChange &= ~PORT_STAT_C_ENABLE;
                break;
            case PORT_SUSPEND:
                port->wPortStatus &= ~PORT_STAT_SUSPEND;
                break;
            case PORT_C_SUSPEND:
                port->wPortChange &= ~PORT_STAT_C_SUSPEND;
                break;
            case PORT_C_CONNECTION:
                port->wPortChange &= ~PORT_STAT_C_CONNECTION;
                break;
            case PORT_C_OVERCURRENT:
                port->wPortChange &= ~PORT_STAT_C_OVERCURRENT;
                break;
            case PORT_C_RESET:
                port->wPortChange &= ~PORT_STAT_C_RESET;
                break;
            default:
                goto fail;
            }
        }
        break;
    case GetHubDescriptor:
        {
            unsigned int n, limit, var_hub_size = 0;
            memcpy(data, qemu_hub_hub_descriptor,
                   sizeof(qemu_hub_hub_descriptor));
            data[2] = NUM_PORTS;

            /* fill DeviceRemovable bits */
            limit = ((NUM_PORTS + 1 + 7) / 8) + 7;
            for (n = 7; n < limit; n++) {
                data[n] = 0x00;
                var_hub_size++;
            }

            /* fill PortPwrCtrlMask bits */
            limit = limit + ((NUM_PORTS + 7) / 8);
            for (;n < limit; n++) {
                data[n] = 0xff;
                var_hub_size++;
            }

            p->actual_length = sizeof(qemu_hub_hub_descriptor) + var_hub_size;
            data[0] = p->actual_length;
            break;
        }
    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_hub_handle_data(USBDevice *dev, USBPacket *p)
{
    USBHubState *s = (USBHubState *)dev;

    switch(p->pid) {
    case USB_TOKEN_IN:
        if (p->ep->nr == 1) {
            USBHubPort *port;
            unsigned int status;
            uint8_t buf[4];
            int i, n;
            n = (NUM_PORTS + 1 + 7) / 8;
            if (p->iov.size == 1) { /* FreeBSD workaround */
                n = 1;
            } else if (n > p->iov.size) {
                p->status = USB_RET_BABBLE;
                return;
            }
            status = 0;
            for(i = 0; i < NUM_PORTS; i++) {
                port = &s->ports[i];
                if (port->wPortChange)
                    status |= (1 << (i + 1));
            }
            if (status != 0) {
                trace_usb_hub_status_report(s->dev.addr, status);
                for(i = 0; i < n; i++) {
                    buf[i] = status >> (8 * i);
                }
                usb_packet_copy(p, buf, n);
            } else {
                p->status = USB_RET_NAK; /* usb11 11.13.1 */
            }
        } else {
            goto fail;
        }
        break;
    case USB_TOKEN_OUT:
    default:
    fail:
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_hub_handle_destroy(USBDevice *dev)
{
    USBHubState *s = (USBHubState *)dev;
    int i;

    for (i = 0; i < NUM_PORTS; i++) {
        usb_unregister_port(usb_bus_from_device(dev),
                            &s->ports[i].port);
    }
}

static USBPortOps usb_hub_port_ops = {
    .attach = usb_hub_attach,
    .detach = usb_hub_detach,
    .child_detach = usb_hub_child_detach,
    .wakeup = usb_hub_wakeup,
    .complete = usb_hub_complete,
};

static void usb_hub_realize(USBDevice *dev, Error **errp)
{
    USBHubState *s = USB_HUB(dev);
    USBHubPort *port;
    int i;

    if (dev->port->hubcount == 5) {
        error_setg(errp, "usb hub chain too deep");
        return;
    }

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 1);
    for (i = 0; i < NUM_PORTS; i++) {
        port = &s->ports[i];
        usb_register_port(usb_bus_from_device(dev),
                          &port->port, s, i, &usb_hub_port_ops,
                          USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL);
        usb_port_location(&port->port, dev->port, i+1);
    }
    usb_hub_handle_reset(dev);
}

static const VMStateDescription vmstate_usb_hub_port = {
    .name = "usb-hub-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(wPortStatus, USBHubPort),
        VMSTATE_UINT16(wPortChange, USBHubPort),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_usb_hub = {
    .name = "usb-hub",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBHubState),
        VMSTATE_STRUCT_ARRAY(ports, USBHubState, NUM_PORTS, 0,
                             vmstate_usb_hub_port, USBHubPort),
        VMSTATE_END_OF_LIST()
    }
};

static void usb_hub_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_hub_realize;
    uc->product_desc   = "QEMU USB Hub";
    uc->usb_desc       = &desc_hub;
    uc->find_device    = usb_hub_find_device;
    uc->handle_reset   = usb_hub_handle_reset;
    uc->handle_control = usb_hub_handle_control;
    uc->handle_data    = usb_hub_handle_data;
    uc->handle_destroy = usb_hub_handle_destroy;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "hub";
    dc->vmsd = &vmstate_usb_hub;
}

static const TypeInfo hub_info = {
    .name          = TYPE_USB_HUB,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBHubState),
    .class_init    = usb_hub_class_initfn,
};

static void usb_hub_register_types(void)
{
    type_register_static(&hub_info);
}

type_init(usb_hub_register_types)
