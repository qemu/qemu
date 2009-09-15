#include "hw.h"
#include "usb.h"
#include "qdev.h"
#include "sysemu.h"
#include "monitor.h"

static void usb_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent);

static struct BusInfo usb_bus_info = {
    .name      = "USB",
    .size      = sizeof(USBBus),
    .print_dev = usb_bus_dev_print,
};
static int next_usb_bus = 0;
static QTAILQ_HEAD(, USBBus) busses = QTAILQ_HEAD_INITIALIZER(busses);

USBBus *usb_bus_new(DeviceState *host)
{
    USBBus *bus;

    bus = FROM_QBUS(USBBus, qbus_create(&usb_bus_info, host, NULL));
    bus->busnr = next_usb_bus++;
    QTAILQ_INIT(&bus->free);
    QTAILQ_INIT(&bus->used);
    QTAILQ_INSERT_TAIL(&busses, bus, next);
    return bus;
}

USBBus *usb_bus_find(int busnr)
{
    USBBus *bus;

    if (-1 == busnr)
        return QTAILQ_FIRST(&busses);
    QTAILQ_FOREACH(bus, &busses, next) {
        if (bus->busnr == busnr)
            return bus;
    }
    return NULL;
}

static int usb_qdev_init(DeviceState *qdev, DeviceInfo *base)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    USBDeviceInfo *info = DO_UPCAST(USBDeviceInfo, qdev, base);
    int rc;

    pstrcpy(dev->devname, sizeof(dev->devname), qdev->info->name);
    dev->info = info;
    rc = dev->info->init(dev);
    if (rc == 0)
        usb_device_attach(dev);
    return rc;
}

void usb_qdev_register(USBDeviceInfo *info)
{
    info->qdev.bus_info = &usb_bus_info;
    info->qdev.init     = usb_qdev_init;
    qdev_register(&info->qdev);
}

void usb_qdev_register_many(USBDeviceInfo *info)
{
    while (info->qdev.name) {
        usb_qdev_register(info);
        info++;
    }
}

USBDevice *usb_create(USBBus *bus, const char *name)
{
    DeviceState *dev;

#if 1
    /* temporary stopgap until all usb is properly qdev-ified */
    if (!bus) {
        bus = usb_bus_find(-1);
        if (!bus)
            return NULL;
        fprintf(stderr, "%s: no bus specified, using \"%s\" for \"%s\"\n",
                __FUNCTION__, bus->qbus.name, name);
    }
#endif

    dev = qdev_create(&bus->qbus, name);
    return DO_UPCAST(USBDevice, qdev, dev);
}

USBDevice *usb_create_simple(USBBus *bus, const char *name)
{
    USBDevice *dev = usb_create(bus, name);
    qdev_init(&dev->qdev);
    return dev;
}

void usb_register_port(USBBus *bus, USBPort *port, void *opaque, int index,
                       usb_attachfn attach)
{
    port->opaque = opaque;
    port->index = index;
    port->attach = attach;
    QTAILQ_INSERT_TAIL(&bus->free, port, next);
    bus->nfree++;
}

static void do_attach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port;

    if (dev->attached) {
        fprintf(stderr, "Warning: tried to attach usb device %s twice\n",
                dev->devname);
        return;
    }
    dev->attached++;

    port = QTAILQ_FIRST(&bus->free);
    QTAILQ_REMOVE(&bus->free, port, next);
    bus->nfree--;

    usb_attach(port, dev);

    QTAILQ_INSERT_TAIL(&bus->used, port, next);
    bus->nused++;
}

int usb_device_attach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBDevice *hub;

    if (bus->nfree == 1) {
        /* Create a new hub and chain it on.  */
        hub = usb_create_simple(bus, "QEMU USB Hub");
    }
    do_attach(dev);
    return 0;
}

int usb_device_delete_addr(int busnr, int addr)
{
    USBBus *bus;
    USBPort *port;
    USBDevice *dev;

    bus = usb_bus_find(busnr);
    if (!bus)
        return -1;

    QTAILQ_FOREACH(port, &bus->used, next) {
        if (port->dev->addr == addr)
            break;
    }
    if (!port)
        return -1;

    dev = port->dev;
    QTAILQ_REMOVE(&bus->used, port, next);
    bus->nused--;

    usb_attach(port, NULL);
    dev->info->handle_destroy(dev);

    QTAILQ_INSERT_TAIL(&bus->free, port, next);
    bus->nfree++;
    return 0;
}

static const char *usb_speed(unsigned int speed)
{
    static const char *txt[] = {
        [ USB_SPEED_LOW  ] = "1.5",
        [ USB_SPEED_FULL ] = "12",
        [ USB_SPEED_HIGH ] = "480",
    };
    if (speed >= ARRAY_SIZE(txt))
        return "?";
    return txt[speed];
}

static void usb_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    USBBus *bus = usb_bus_from_device(dev);

    monitor_printf(mon, "%*saddr %d.%d, speed %s, name %s\n", indent, "",
                   bus->busnr, dev->addr,
                   usb_speed(dev->speed), dev->devname);
}

void usb_info(Monitor *mon)
{
    USBBus *bus;
    USBDevice *dev;
    USBPort *port;

    if (QTAILQ_EMPTY(&busses)) {
        monitor_printf(mon, "USB support not enabled\n");
        return;
    }

    QTAILQ_FOREACH(bus, &busses, next) {
        QTAILQ_FOREACH(port, &bus->used, next) {
            dev = port->dev;
            if (!dev)
                continue;
            monitor_printf(mon, "  Device %d.%d, Speed %s Mb/s, Product %s\n",
                           bus->busnr, dev->addr, usb_speed(dev->speed), dev->devname);
        }
    }
}

