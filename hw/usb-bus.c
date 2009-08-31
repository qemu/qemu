#include "hw.h"
#include "usb.h"
#include "qdev.h"

static struct BusInfo usb_bus_info = {
    .name  = "USB",
    .size  = sizeof(USBBus),
};
static int next_usb_bus = 0;
static TAILQ_HEAD(, USBBus) busses = TAILQ_HEAD_INITIALIZER(busses);

USBBus *usb_bus_new(DeviceState *host)
{
    USBBus *bus;

    bus = FROM_QBUS(USBBus, qbus_create(&usb_bus_info, host, NULL));
    bus->busnr = next_usb_bus++;
    TAILQ_INIT(&bus->free);
    TAILQ_INIT(&bus->used);
    TAILQ_INSERT_TAIL(&busses, bus, next);
    return bus;
}

USBBus *usb_bus_find(int busnr)
{
    USBBus *bus;

    if (-1 == busnr)
        return TAILQ_FIRST(&busses);
    TAILQ_FOREACH(bus, &busses, next) {
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

USBDevice *usb_create_simple(USBBus *bus, const char *name)
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
    qdev_init(dev);
    return DO_UPCAST(USBDevice, qdev, dev);
}
