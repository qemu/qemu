#include "hw.h"
#include "usb.h"
#include "qdev.h"
#include "sysemu.h"
#include "monitor.h"

static void usb_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent);

static char *usb_get_dev_path(DeviceState *dev);
static char *usb_get_fw_dev_path(DeviceState *qdev);

static struct BusInfo usb_bus_info = {
    .name      = "USB",
    .size      = sizeof(USBBus),
    .print_dev = usb_bus_dev_print,
    .get_dev_path = usb_get_dev_path,
    .get_fw_dev_path = usb_get_fw_dev_path,
    .props      = (Property[]) {
        DEFINE_PROP_STRING("port", USBDevice, port_path),
        DEFINE_PROP_END_OF_LIST()
    },
};
static int next_usb_bus = 0;
static QTAILQ_HEAD(, USBBus) busses = QTAILQ_HEAD_INITIALIZER(busses);

const VMStateDescription vmstate_usb_device = {
    .name = "USBDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT8(addr, USBDevice),
        VMSTATE_INT32(state, USBDevice),
        VMSTATE_INT32(remote_wakeup, USBDevice),
        VMSTATE_INT32(setup_state, USBDevice),
        VMSTATE_INT32(setup_len, USBDevice),
        VMSTATE_INT32(setup_index, USBDevice),
        VMSTATE_UINT8_ARRAY(setup_buf, USBDevice, 8),
        VMSTATE_END_OF_LIST(),
    }
};

void usb_bus_new(USBBus *bus, USBBusOps *ops, DeviceState *host)
{
    qbus_create_inplace(&bus->qbus, &usb_bus_info, host, NULL);
    bus->ops = ops;
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
    QLIST_INIT(&dev->strings);
    rc = dev->info->init(dev);
    if (rc == 0 && dev->auto_attach)
        rc = usb_device_attach(dev);
    return rc;
}

static int usb_qdev_exit(DeviceState *qdev)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    USBBus *bus = usb_bus_from_device(dev);

    if (dev->attached) {
        usb_device_detach(dev);
    }
    bus->ops->device_destroy(bus, dev);
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
        error_report("%s: no bus specified, using \"%s\" for \"%s\"\n",
                __FUNCTION__, bus->qbus.name, name);
    }
#endif

    dev = qdev_create(&bus->qbus, name);
    return DO_UPCAST(USBDevice, qdev, dev);
}

USBDevice *usb_create_simple(USBBus *bus, const char *name)
{
    USBDevice *dev = usb_create(bus, name);
    if (!dev) {
        hw_error("Failed to create USB device '%s'\n", name);
    }
    qdev_init_nofail(&dev->qdev);
    return dev;
}

static void usb_fill_port(USBPort *port, void *opaque, int index,
                          USBPortOps *ops, int speedmask)
{
    port->opaque = opaque;
    port->index = index;
    port->opaque = opaque;
    port->index = index;
    port->ops = ops;
    port->speedmask = speedmask;
    usb_port_location(port, NULL, index + 1);
}

void usb_register_port(USBBus *bus, USBPort *port, void *opaque, int index,
                       USBPortOps *ops, int speedmask)
{
    usb_fill_port(port, opaque, index, ops, speedmask);
    QTAILQ_INSERT_TAIL(&bus->free, port, next);
    bus->nfree++;
}

int usb_register_companion(const char *masterbus, USBPort *ports[],
                           uint32_t portcount, uint32_t firstport,
                           void *opaque, USBPortOps *ops, int speedmask)
{
    USBBus *bus;
    int i;

    QTAILQ_FOREACH(bus, &busses, next) {
        if (strcmp(bus->qbus.name, masterbus) == 0) {
            break;
        }
    }

    if (!bus || !bus->ops->register_companion) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "masterbus",
                      "an USB masterbus");
        if (bus) {
            error_printf_unless_qmp(
                "USB bus '%s' does not allow companion controllers\n",
                masterbus);
        }
        return -1;
    }

    for (i = 0; i < portcount; i++) {
        usb_fill_port(ports[i], opaque, i, ops, speedmask);
    }

    return bus->ops->register_companion(bus, ports, portcount, firstport);
}

void usb_port_location(USBPort *downstream, USBPort *upstream, int portnr)
{
    if (upstream) {
        snprintf(downstream->path, sizeof(downstream->path), "%s.%d",
                 upstream->path, portnr);
    } else {
        snprintf(downstream->path, sizeof(downstream->path), "%d", portnr);
    }
}

void usb_unregister_port(USBBus *bus, USBPort *port)
{
    if (port->dev)
        qdev_free(&port->dev->qdev);
    QTAILQ_REMOVE(&bus->free, port, next);
    bus->nfree--;
}

static int do_attach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port;

    if (dev->attached) {
        error_report("Error: tried to attach usb device %s twice\n",
                dev->product_desc);
        return -1;
    }
    if (bus->nfree == 0) {
        error_report("Error: tried to attach usb device %s to a bus with no free ports\n",
                dev->product_desc);
        return -1;
    }
    if (dev->port_path) {
        QTAILQ_FOREACH(port, &bus->free, next) {
            if (strcmp(port->path, dev->port_path) == 0) {
                break;
            }
        }
        if (port == NULL) {
            error_report("Error: usb port %s (bus %s) not found\n",
                    dev->port_path, bus->qbus.name);
            return -1;
        }
    } else {
        port = QTAILQ_FIRST(&bus->free);
    }
    if (!(port->speedmask & dev->speedmask)) {
        error_report("Warning: speed mismatch trying to attach usb device %s to bus %s\n",
                dev->product_desc, bus->qbus.name);
        return -1;
    }

    dev->attached++;
    QTAILQ_REMOVE(&bus->free, port, next);
    bus->nfree--;

    usb_attach(port, dev);

    QTAILQ_INSERT_TAIL(&bus->used, port, next);
    bus->nused++;

    return 0;
}

int usb_device_attach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);

    if (bus->nfree == 1 && dev->port_path == NULL) {
        /* Create a new hub and chain it on
           (unless a physical port location is specified). */
        usb_create_simple(bus, "usb-hub");
    }
    return do_attach(dev);
}

int usb_device_detach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port;

    if (!dev->attached) {
        error_report("Error: tried to detach unattached usb device %s\n",
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
        [ USB_SPEED_SUPER ] = "5000",
    };
    if (speed >= ARRAY_SIZE(txt))
        return "?";
    return txt[speed];
}

static void usb_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    USBBus *bus = usb_bus_from_device(dev);

    monitor_printf(mon, "%*saddr %d.%d, port %s, speed %s, name %s%s\n",
                   indent, "", bus->busnr, dev->addr,
                   dev->port ? dev->port->path : "-",
                   usb_speed(dev->speed), dev->product_desc,
                   dev->attached ? ", attached" : "");
}

static char *usb_get_dev_path(DeviceState *qdev)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    return qemu_strdup(dev->port->path);
}

static char *usb_get_fw_dev_path(DeviceState *qdev)
{
    USBDevice *dev = DO_UPCAST(USBDevice, qdev, qdev);
    char *fw_path, *in;
    ssize_t pos = 0, fw_len;
    long nr;

    fw_len = 32 + strlen(dev->port->path) * 6;
    fw_path = qemu_malloc(fw_len);
    in = dev->port->path;
    while (fw_len - pos > 0) {
        nr = strtol(in, &in, 10);
        if (in[0] == '.') {
            /* some hub between root port and device */
            pos += snprintf(fw_path + pos, fw_len - pos, "hub@%ld/", nr);
            in++;
        } else {
            /* the device itself */
            pos += snprintf(fw_path + pos, fw_len - pos, "%s@%ld",
                            qdev_fw_name(qdev), nr);
            break;
        }
    }
    return fw_path;
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
            monitor_printf(mon, "  Device %d.%d, Port %s, Speed %s Mb/s, Product %s\n",
                           bus->busnr, dev->addr, port->path, usb_speed(dev->speed),
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
    char driver[32];
    const char *params;
    int len;

    params = strchr(cmdline,':');
    if (params) {
        params++;
        len = params - cmdline;
        if (len > sizeof(driver))
            len = sizeof(driver);
        pstrcpy(driver, len, cmdline);
    } else {
        params = "";
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
        error_report("usbdevice %s not found", driver);
#endif
        return NULL;
    }

    if (!usb->usbdevice_init) {
        if (*params) {
            error_report("usbdevice %s accepts no params", driver);
            return NULL;
        }
        return usb_create_simple(bus, usb->qdev.name);
    }
    return usb->usbdevice_init(params);
}
