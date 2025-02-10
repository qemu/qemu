#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/type-helpers.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "system/system.h"
#include "migration/vmstate.h"
#include "monitor/monitor.h"
#include "trace.h"
#include "qemu/cutils.h"

static void usb_bus_dev_print(Monitor *mon, DeviceState *qdev, int indent);

static char *usb_get_dev_path(DeviceState *dev);
static char *usb_get_fw_dev_path(DeviceState *qdev);
static void usb_qdev_unrealize(DeviceState *qdev);

static const Property usb_props[] = {
    DEFINE_PROP_STRING("port", USBDevice, port_path),
    DEFINE_PROP_STRING("serial", USBDevice, serial),
    DEFINE_PROP_BIT("msos-desc", USBDevice, flags,
                    USB_DEV_FLAG_MSOS_DESC_ENABLE, true),
    DEFINE_PROP_STRING("pcap", USBDevice, pcap_filename),
};

static void usb_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->print_dev = usb_bus_dev_print;
    k->get_dev_path = usb_get_dev_path;
    k->get_fw_dev_path = usb_get_fw_dev_path;
    hc->unplug = qdev_simple_device_unplug_cb;
}

static const TypeInfo usb_bus_info = {
    .name = TYPE_USB_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(USBBus),
    .class_init = usb_bus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    }
};

static int next_usb_bus = 0;
static QTAILQ_HEAD(, USBBus) busses = QTAILQ_HEAD_INITIALIZER(busses);

static int usb_device_post_load(void *opaque, int version_id)
{
    USBDevice *dev = opaque;

    if (dev->state == USB_STATE_NOTATTACHED) {
        dev->attached = false;
    } else {
        dev->attached = true;
    }
    return 0;
}

const VMStateDescription vmstate_usb_device = {
    .name = "USBDevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = usb_device_post_load,
    .fields = (const VMStateField[]) {
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

void usb_bus_new(USBBus *bus, size_t bus_size,
                 USBBusOps *ops, DeviceState *host)
{
    qbus_init(bus, bus_size, TYPE_USB_BUS, host, NULL);
    qbus_set_bus_hotplug_handler(BUS(bus));
    bus->ops = ops;
    bus->busnr = next_usb_bus++;
    QTAILQ_INIT(&bus->free);
    QTAILQ_INIT(&bus->used);
    QTAILQ_INSERT_TAIL(&busses, bus, next);
}

void usb_bus_release(USBBus *bus)
{
    assert(next_usb_bus > 0);

    QTAILQ_REMOVE(&busses, bus, next);
}

static void usb_device_realize(USBDevice *dev, Error **errp)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);

    if (klass->realize) {
        klass->realize(dev, errp);
    }
}

USBDevice *usb_device_find_device(USBDevice *dev, uint8_t addr)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->find_device) {
        return klass->find_device(dev, addr);
    }
    return NULL;
}

static void usb_device_unrealize(USBDevice *dev)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);

    if (klass->unrealize) {
        klass->unrealize(dev);
    }
}

void usb_device_cancel_packet(USBDevice *dev, USBPacket *p)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->cancel_packet) {
        klass->cancel_packet(dev, p);
    }
}

void usb_device_handle_attach(USBDevice *dev)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->handle_attach) {
        klass->handle_attach(dev);
    }
}

void usb_device_handle_reset(USBDevice *dev)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->handle_reset) {
        klass->handle_reset(dev);
    }
}

void usb_device_handle_control(USBDevice *dev, USBPacket *p, int request,
                               int value, int index, int length, uint8_t *data)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->handle_control) {
        klass->handle_control(dev, p, request, value, index, length, data);
    }
}

void usb_device_handle_data(USBDevice *dev, USBPacket *p)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->handle_data) {
        klass->handle_data(dev, p);
    }
}

const char *usb_device_get_product_desc(USBDevice *dev)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    return klass->product_desc;
}

const USBDesc *usb_device_get_usb_desc(USBDevice *dev)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (dev->usb_desc) {
        return dev->usb_desc;
    }
    return klass->usb_desc;
}

void usb_device_set_interface(USBDevice *dev, int interface,
                              int alt_old, int alt_new)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->set_interface) {
        klass->set_interface(dev, interface, alt_old, alt_new);
    }
}

void usb_device_flush_ep_queue(USBDevice *dev, USBEndpoint *ep)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->flush_ep_queue) {
        klass->flush_ep_queue(dev, ep);
    }
}

void usb_device_ep_stopped(USBDevice *dev, USBEndpoint *ep)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->ep_stopped) {
        klass->ep_stopped(dev, ep);
    }
}

int usb_device_alloc_streams(USBDevice *dev, USBEndpoint **eps, int nr_eps,
                             int streams)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->alloc_streams) {
        return klass->alloc_streams(dev, eps, nr_eps, streams);
    }
    return 0;
}

void usb_device_free_streams(USBDevice *dev, USBEndpoint **eps, int nr_eps)
{
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);
    if (klass->free_streams) {
        klass->free_streams(dev, eps, nr_eps);
    }
}

static void usb_qdev_realize(DeviceState *qdev, Error **errp)
{
    USBDevice *dev = USB_DEVICE(qdev);
    Error *local_err = NULL;

    pstrcpy(dev->product_desc, sizeof(dev->product_desc),
            usb_device_get_product_desc(dev));
    dev->auto_attach = 1;
    QLIST_INIT(&dev->strings);
    usb_ep_init(dev);

    usb_claim_port(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    usb_device_realize(dev, &local_err);
    if (local_err) {
        usb_release_port(dev);
        error_propagate(errp, local_err);
        return;
    }

    if (dev->auto_attach) {
        usb_device_attach(dev, &local_err);
        if (local_err) {
            usb_qdev_unrealize(qdev);
            error_propagate(errp, local_err);
            return;
        }
    }

    if (dev->pcap_filename) {
        int fd = qemu_open_old(dev->pcap_filename,
                               O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, 0666);
        if (fd < 0) {
            error_setg(errp, "open %s failed", dev->pcap_filename);
            usb_qdev_unrealize(qdev);
            return;
        }
        dev->pcap = fdopen(fd, "wb");
        usb_pcap_init(dev->pcap);
    }
}

static void usb_qdev_unrealize(DeviceState *qdev)
{
    USBDevice *dev = USB_DEVICE(qdev);
    USBDescString *s, *next;

    QLIST_FOREACH_SAFE(s, &dev->strings, next, next) {
        QLIST_REMOVE(s, next);
        g_free(s->str);
        g_free(s);
    }

    if (dev->pcap) {
        fclose(dev->pcap);
    }

    if (dev->attached) {
        usb_device_detach(dev);
    }
    usb_device_unrealize(dev);
    if (dev->port) {
        usb_release_port(dev);
    }
}

typedef struct LegacyUSBFactory
{
    const char *name;
    const char *usbdevice_name;
    USBDevice *(*usbdevice_init)(void);
} LegacyUSBFactory;

static GSList *legacy_usb_factory;

void usb_legacy_register(const char *typename, const char *usbdevice_name,
                         USBDevice *(*usbdevice_init)(void))
{
    if (usbdevice_name) {
        LegacyUSBFactory *f = g_malloc0(sizeof(*f));
        f->name = typename;
        f->usbdevice_name = usbdevice_name;
        f->usbdevice_init = usbdevice_init;
        legacy_usb_factory = g_slist_append(legacy_usb_factory, f);
    }
}

static void usb_fill_port(USBPort *port, void *opaque, int index,
                          USBPortOps *ops, int speedmask)
{
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

void usb_register_companion(const char *masterbus, USBPort *ports[],
                            uint32_t portcount, uint32_t firstport,
                            void *opaque, USBPortOps *ops, int speedmask,
                            Error **errp)
{
    USBBus *bus;
    int i;

    QTAILQ_FOREACH(bus, &busses, next) {
        if (strcmp(bus->qbus.name, masterbus) == 0) {
            break;
        }
    }

    if (!bus) {
        error_setg(errp, "USB bus '%s' not found", masterbus);
        return;
    }
    if (!bus->ops->register_companion) {
        error_setg(errp, "Can't use USB bus '%s' as masterbus,"
                   " it doesn't support companion controllers",
                   masterbus);
        return;
    }

    for (i = 0; i < portcount; i++) {
        usb_fill_port(ports[i], opaque, i, ops, speedmask);
    }

    bus->ops->register_companion(bus, ports, portcount, firstport, errp);
}

void usb_port_location(USBPort *downstream, USBPort *upstream, int portnr)
{
    if (upstream) {
        int l = snprintf(downstream->path, sizeof(downstream->path), "%s.%d",
                         upstream->path, portnr);
        /* Max string is nn.nn.nn.nn.nn, which fits in 16 bytes */
        assert(l < sizeof(downstream->path));
        downstream->hubcount = upstream->hubcount + 1;
    } else {
        snprintf(downstream->path, sizeof(downstream->path), "%d", portnr);
        downstream->hubcount = 0;
    }
}

void usb_unregister_port(USBBus *bus, USBPort *port)
{
    if (port->dev) {
        object_unparent(OBJECT(port->dev));
    }
    QTAILQ_REMOVE(&bus->free, port, next);
    bus->nfree--;
}

void usb_claim_port(USBDevice *dev, Error **errp)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port;
    USBDevice *hub;

    assert(dev->port == NULL);

    if (dev->port_path) {
        QTAILQ_FOREACH(port, &bus->free, next) {
            if (strcmp(port->path, dev->port_path) == 0) {
                break;
            }
        }
        if (port == NULL) {
            error_setg(errp, "usb port %s (bus %s) not found (in use?)",
                       dev->port_path, bus->qbus.name);
            return;
        }
    } else {
        if (bus->nfree == 1 && strcmp(object_get_typename(OBJECT(dev)), "usb-hub") != 0) {
            /* Create a new hub and chain it on */
            hub = USB_DEVICE(qdev_try_new("usb-hub"));
            if (hub) {
                usb_realize_and_unref(hub, bus, NULL);
            }
        }
        if (bus->nfree == 0) {
            error_setg(errp, "tried to attach usb device %s to a bus "
                       "with no free ports", dev->product_desc);
            return;
        }
        port = QTAILQ_FIRST(&bus->free);
    }
    trace_usb_port_claim(bus->busnr, port->path);

    QTAILQ_REMOVE(&bus->free, port, next);
    bus->nfree--;

    dev->port = port;
    port->dev = dev;

    QTAILQ_INSERT_TAIL(&bus->used, port, next);
    bus->nused++;
}

void usb_release_port(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port = dev->port;

    assert(port != NULL);
    trace_usb_port_release(bus->busnr, port->path);

    QTAILQ_REMOVE(&bus->used, port, next);
    bus->nused--;

    dev->port = NULL;
    port->dev = NULL;

    QTAILQ_INSERT_TAIL(&bus->free, port, next);
    bus->nfree++;
}

static void usb_mask_to_str(char *dest, size_t size,
                            unsigned int speedmask)
{
    static const struct {
        unsigned int mask;
        const char *name;
    } speeds[] = {
        { .mask = USB_SPEED_MASK_FULL,  .name = "full"  },
        { .mask = USB_SPEED_MASK_HIGH,  .name = "high"  },
        { .mask = USB_SPEED_MASK_SUPER, .name = "super" },
    };
    int i, pos = 0;

    for (i = 0; i < ARRAY_SIZE(speeds); i++) {
        if (speeds[i].mask & speedmask) {
            pos += snprintf(dest + pos, size - pos, "%s%s",
                            pos ? "+" : "",
                            speeds[i].name);
        }
    }

    if (pos == 0) {
        snprintf(dest, size, "unknown");
    }
}

void usb_check_attach(USBDevice *dev, Error **errp)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port = dev->port;
    char devspeed[32], portspeed[32];

    assert(port != NULL);
    assert(!dev->attached);
    usb_mask_to_str(devspeed, sizeof(devspeed), dev->speedmask);
    usb_mask_to_str(portspeed, sizeof(portspeed), port->speedmask);
    trace_usb_port_attach(bus->busnr, port->path,
                          devspeed, portspeed);

    if (!(port->speedmask & dev->speedmask)) {
        error_setg(errp, "Warning: speed mismatch trying to attach"
                   " usb device \"%s\" (%s speed)"
                   " to bus \"%s\", port \"%s\" (%s speed)",
                   dev->product_desc, devspeed,
                   bus->qbus.name, port->path, portspeed);
        return;
    }
}

void usb_device_attach(USBDevice *dev, Error **errp)
{
    USBPort *port = dev->port;
    Error *local_err = NULL;

    usb_check_attach(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    dev->attached = true;
    usb_attach(port);
}

int usb_device_detach(USBDevice *dev)
{
    USBBus *bus = usb_bus_from_device(dev);
    USBPort *port = dev->port;

    assert(port != NULL);
    assert(dev->attached);
    trace_usb_port_detach(bus->busnr, port->path);

    usb_detach(port);
    dev->attached = false;
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
    USBDevice *dev = USB_DEVICE(qdev);
    USBBus *bus = usb_bus_from_device(dev);

    monitor_printf(mon, "%*saddr %d.%d, port %s, speed %s, name %s%s\n",
                   indent, "", bus->busnr, dev->addr,
                   dev->port ? dev->port->path : "-",
                   usb_speed(dev->speed), dev->product_desc,
                   dev->attached ? ", attached" : "");
}

static char *usb_get_dev_path(DeviceState *qdev)
{
    USBDevice *dev = USB_DEVICE(qdev);
    DeviceState *hcd = qdev->parent_bus->parent;
    char *id = qdev_get_dev_path(hcd);

    if (id) {
        char *ret = g_strdup_printf("%s/%s", id, dev->port->path);
        g_free(id);
        return ret;
    } else {
        return g_strdup(dev->port->path);
    }
}

static char *usb_get_fw_dev_path(DeviceState *qdev)
{
    USBDevice *dev = USB_DEVICE(qdev);
    char *fw_path, *in;
    ssize_t pos = 0, fw_len;
    long nr;

    fw_len = 32 + strlen(dev->port->path) * 6;
    fw_path = g_malloc(fw_len);
    in = dev->port->path;
    while (fw_len - pos > 0) {
        nr = strtol(in, &in, 10);
        if (in[0] == '.') {
            /* some hub between root port and device */
            pos += snprintf(fw_path + pos, fw_len - pos, "hub@%lx/", nr);
            in++;
        } else {
            /* the device itself */
            snprintf(fw_path + pos, fw_len - pos, "%s@%lx",
                     qdev_fw_name(qdev), nr);
            break;
        }
    }
    return fw_path;
}

HumanReadableText *qmp_x_query_usb(Error **errp)
{
    g_autoptr(GString) buf = g_string_new("");
    USBBus *bus;
    USBDevice *dev;
    USBPort *port;

    if (QTAILQ_EMPTY(&busses)) {
        error_setg(errp, "USB support not enabled");
        return NULL;
    }

    QTAILQ_FOREACH(bus, &busses, next) {
        QTAILQ_FOREACH(port, &bus->used, next) {
            dev = port->dev;
            if (!dev)
                continue;
            g_string_append_printf(buf,
                                   "  Device %d.%d, Port %s, Speed %s Mb/s, "
                                   "Product %s%s%s\n",
                                   bus->busnr, dev->addr, port->path,
                                   usb_speed(dev->speed), dev->product_desc,
                                   dev->qdev.id ? ", ID: " : "",
                                   dev->qdev.id ?: "");
        }
    }

    return human_readable_text_from_str(buf);
}

/* handle legacy -usbdevice cmd line option */
USBDevice *usbdevice_create(const char *driver)
{
    USBBus *bus = QTAILQ_FIRST(&busses);
    LegacyUSBFactory *f = NULL;
    Error *err = NULL;
    GSList *i;
    USBDevice *dev;

    if (strchr(driver, ':')) {
        error_report("usbdevice parameters are not supported anymore");
        return NULL;
    }

    for (i = legacy_usb_factory; i; i = i->next) {
        f = i->data;
        if (strcmp(f->usbdevice_name, driver) == 0) {
            break;
        }
    }
    if (i == NULL) {
#if 0
        /* no error because some drivers are not converted (yet) */
        error_report("usbdevice %s not found", driver);
#endif
        return NULL;
    }

    if (!bus) {
        error_report("Error: no usb bus to attach usbdevice %s, "
                     "please try -machine usb=on and check that "
                     "the machine model supports USB", driver);
        return NULL;
    }

    dev = f->usbdevice_init ? f->usbdevice_init()
                            : USB_DEVICE(qdev_new(f->name));
    if (!dev) {
        error_report("Failed to create USB device '%s'", f->name);
        return NULL;
    }
    if (!usb_realize_and_unref(dev, bus, &err)) {
        error_reportf_err(err, "Failed to initialize USB device '%s': ",
                          f->name);
        object_unparent(OBJECT(dev));
        return NULL;
    }
    return dev;
}

static bool usb_get_attached(Object *obj, Error **errp)
{
    USBDevice *dev = USB_DEVICE(obj);

    return dev->attached;
}

static void usb_set_attached(Object *obj, bool value, Error **errp)
{
    USBDevice *dev = USB_DEVICE(obj);

    if (dev->attached == value) {
        return;
    }

    if (value) {
        usb_device_attach(dev, errp);
    } else {
        usb_device_detach(dev);
    }
}

static void usb_device_instance_init(Object *obj)
{
    USBDevice *dev = USB_DEVICE(obj);
    USBDeviceClass *klass = USB_DEVICE_GET_CLASS(dev);

    if (klass->attached_settable) {
        object_property_add_bool(obj, "attached",
                                 usb_get_attached, usb_set_attached);
    } else {
        object_property_add_bool(obj, "attached",
                                 usb_get_attached, NULL);
    }
}

static void usb_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->bus_type = TYPE_USB_BUS;
    k->realize  = usb_qdev_realize;
    k->unrealize = usb_qdev_unrealize;
    device_class_set_props(k, usb_props);
}

static const TypeInfo usb_device_type_info = {
    .name = TYPE_USB_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(USBDevice),
    .instance_init = usb_device_instance_init,
    .abstract = true,
    .class_size = sizeof(USBDeviceClass),
    .class_init = usb_device_class_init,
};

static void usb_register_types(void)
{
    type_register_static(&usb_bus_info);
    type_register_static(&usb_device_type_info);
}

type_init(usb_register_types)
