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

void usb_bus_new(USBBus *bus, DeviceState *host)
{
    qbus_create_inplace(&bus->qbus, &usb_bus_info, host, NULL);
    bus->busnr = next_usb_bus++;
    bus->qbus.allow_hotplug = 1; /* Yes, we can */
    QTAILQ_INIT(&bus->free);
    QTAILQ_INIT(&bus->used);
    QTAILQ_INSERT_TAIL(&busses, bus, next);
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

    pstrcpy(dev->product_desc, sizeof(dev->product_desc), info->product_desc);
    dev->info = info;
    dev->auto_attach = 1;
    rc = dev->info->init(dev);
    if (rc == 0 && dev->auto_attach)
        usb_device_attach(dev);
    return rc;
}

static int usb_qdev_exit(DeviceState *qdev)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);

    usb_device_detach(dev);
    if (dev->info->handle_destroy) {
        dev->info->handle_destroy(dev);
    }
    return 0;
}

void usb_qdev_register(USBDeviceInfo *info)
{
    info->qdev.bus_info = &usb_bus_info;
    info->qdev.init     = usb_qdev_init;
    info->qdev.unplug   = qdev_simple_unplug_cb;
    info->qdev.exit     = usb_qdev_exit;
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
    qdev_init_nofail(&dev->qdev);
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

void usb_unregister_port(USBBus *bus, USBPort *port)
{
    if (port->dev)
        qdev_free(&port->dev->qdev);
    QTAILQ_REMOVE(&bus->free, port, next);
    bus->nfree--;
}

static void do_attach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port;

    if (dev->attached) {
        fprintf(stderr, "Warning: tried to attach usb device %s twice\n",
                dev->product_desc);
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
        hub = usb_create_simple(bus, "usb-hub");
    }
    do_attach(dev);
    return 0;
}

int usb_device_detach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port;

    if (!dev->attached) {
        fprintf(stderr, "Warning: tried to detach unattached usb device %s\n",
                dev->product_desc);
        return -1;
    }
    dev->attached--;

    QTAILQ_FOREACH(port, &bus->used, next) {
        if (port->dev == dev)
            break;
    }
    assert(port != NULL);

    QTAILQ_REMOVE(&bus->used, port, next);
    bus->nused--;

    usb_attach(port, NULL);

    QTAILQ_INSERT_TAIL(&bus->free, port, next);
    bus->nfree++;
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

    qdev_free(&dev->qdev);
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

    monitor_printf(mon, "%*saddr %d.%d, speed %s, name %s%s\n",
                   indent, "", bus->busnr, dev->addr,
                   usb_speed(dev->speed), dev->product_desc,
                   dev->attached ? ", attached" : "");
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
                           bus->busnr, dev->addr, usb_speed(dev->speed),
                           dev->product_desc);
        }
    }
}

/* handle legacy -usbdevice cmd line option */
USBDevice *usbdevice_create(const char *cmdline)
{
    USBBus *bus = usb_bus_find(-1 /* any */);
    DeviceInfo *info;
    USBDeviceInfo *usb;
    char driver[32], *params;
    int len;

    params = strchr(cmdline,':');
    if (params) {
        params++;
        len = params - cmdline;
        if (len > sizeof(driver))
            len = sizeof(driver);
        pstrcpy(driver, len, cmdline);
    } else {
        pstrcpy(driver, sizeof(driver), cmdline);
    }

    for (info = device_info_list; info != NULL; info = info->next) {
        if (info->bus_info != &usb_bus_info)
            continue;
        usb = DO_UPCAST(USBDeviceInfo, qdev, info);
        if (usb->usbdevice_name == NULL)
            continue;
        if (strcmp(usb->usbdevice_name, driver) != 0)
            continue;
        break;
    }
    if (info == NULL) {
#if 0
        /* no error because some drivers are not converted (yet) */
        qemu_error("usbdevice %s not found\n", driver);
#endif
        return NULL;
    }

    if (!usb->usbdevice_init) {
        if (params) {
            qemu_error("usbdevice %s accepts no params\n", driver);
            return NULL;
        }
        return usb_create_simple(bus, usb->qdev.name);
    }
    return usb->usbdevice_init(params);
}
