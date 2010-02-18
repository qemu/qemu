/*
 *  Dynamic device configuration and creation.
 *
 *  Copyright (c) 2009 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/* The theory here is that it should be possible to create a machine without
   knowledge of specific devices.  Historically board init routines have
   passed a bunch of arguments to each device, requiring the board know
   exactly which device it is dealing with.  This file provides an abstract
   API for device configuration and initialization.  Devices will generally
   inherit from a particular bus (e.g. PCI or I2C) rather than
   this API directly.  */

#include "net.h"
#include "qdev.h"
#include "sysemu.h"
#include "monitor.h"
#include "qerror.h"

static int qdev_hotplug = 0;

/* This is a nasty hack to allow passing a NULL bus to qdev_create.  */
static BusState *main_system_bus;

DeviceInfo *device_info_list;

static BusState *qbus_find_recursive(BusState *bus, const char *name,
                                     const BusInfo *info);
static BusState *qbus_find(const char *path);

/* Register a new device type.  */
void qdev_register(DeviceInfo *info)
{
    assert(info->size >= sizeof(DeviceState));
    assert(!info->next);

    info->next = device_info_list;
    device_info_list = info;
}

static DeviceInfo *qdev_find_info(BusInfo *bus_info, const char *name)
{
    DeviceInfo *info;

    /* first check device names */
    for (info = device_info_list; info != NULL; info = info->next) {
        if (bus_info && info->bus_info != bus_info)
            continue;
        if (strcmp(info->name, name) != 0)
            continue;
        return info;
    }

    /* failing that check the aliases */
    for (info = device_info_list; info != NULL; info = info->next) {
        if (bus_info && info->bus_info != bus_info)
            continue;
        if (!info->alias)
            continue;
        if (strcmp(info->alias, name) != 0)
            continue;
        return info;
    }
    return NULL;
}

/* Create a new device.  This only initializes the device state structure
   and allows properties to be set.  qdev_init should be called to
   initialize the actual device emulation.  */
DeviceState *qdev_create(BusState *bus, const char *name)
{
    DeviceInfo *info;
    DeviceState *dev;

    if (!bus) {
        if (!main_system_bus) {
            main_system_bus = qbus_create(&system_bus_info, NULL, "main-system-bus");
        }
        bus = main_system_bus;
    }

    info = qdev_find_info(bus->info, name);
    if (!info) {
        hw_error("Unknown device '%s' for bus '%s'\n", name, bus->info->name);
    }

    dev = qemu_mallocz(info->size);
    dev->info = info;
    dev->parent_bus = bus;
    qdev_prop_set_defaults(dev, dev->info->props);
    qdev_prop_set_defaults(dev, dev->parent_bus->info->props);
    qdev_prop_set_globals(dev);
    QLIST_INSERT_HEAD(&bus->children, dev, sibling);
    if (qdev_hotplug) {
        assert(bus->allow_hotplug);
        dev->hotplugged = 1;
    }
    dev->state = DEV_STATE_CREATED;
    return dev;
}

static void qdev_print_devinfo(DeviceInfo *info)
{
    error_printf("name \"%s\", bus %s",
                 info->name, info->bus_info->name);
    if (info->alias) {
        error_printf(", alias \"%s\"", info->alias);
    }
    if (info->desc) {
        error_printf(", desc \"%s\"", info->desc);
    }
    if (info->no_user) {
        error_printf(", no-user");
    }
    error_printf("\n");
}

static int set_property(const char *name, const char *value, void *opaque)
{
    DeviceState *dev = opaque;

    if (strcmp(name, "driver") == 0)
        return 0;
    if (strcmp(name, "bus") == 0)
        return 0;

    if (qdev_prop_parse(dev, name, value) == -1) {
        qemu_error("can't set property \"%s\" to \"%s\" for \"%s\"\n",
                   name, value, dev->info->name);
        return -1;
    }
    return 0;
}

int qdev_device_help(QemuOpts *opts)
{
    const char *driver;
    DeviceInfo *info;
    Property *prop;

    driver = qemu_opt_get(opts, "driver");
    if (driver && !strcmp(driver, "?")) {
        for (info = device_info_list; info != NULL; info = info->next) {
            qdev_print_devinfo(info);
        }
        return 1;
    }

    if (!qemu_opt_get(opts, "?")) {
        return 0;
    }

    info = qdev_find_info(NULL, driver);
    if (!info) {
        return 0;
    }

    for (prop = info->props; prop && prop->name; prop++) {
        error_printf("%s.%s=%s\n", info->name, prop->name, prop->info->name);
    }
    return 1;
}

DeviceState *qdev_device_add(QemuOpts *opts)
{
    const char *driver, *path, *id;
    DeviceInfo *info;
    DeviceState *qdev;
    BusState *bus;

    driver = qemu_opt_get(opts, "driver");
    if (!driver) {
        qemu_error("-device: no driver specified\n");
        return NULL;
    }

    /* find driver */
    info = qdev_find_info(NULL, driver);
    if (!info) {
        qemu_error_new(QERR_DEVICE_NOT_FOUND, driver);
        return NULL;
    }
    if (info->no_user) {
        qemu_error("device \"%s\" can't be added via command line\n",
                   info->name);
        return NULL;
    }

    /* find bus */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
        bus = qbus_find(path);
    } else {
        bus = qbus_find_recursive(main_system_bus, NULL, info->bus_info);
    }
    if (!bus) {
        qemu_error("Did not find %s bus for %s\n",
                   path ? path : info->bus_info->name, info->name);
        return NULL;
    }
    if (qdev_hotplug && !bus->allow_hotplug) {
        qemu_error("Bus %s does not support hotplugging\n",
                   bus->name);
        return NULL;
    }

    /* create device, set properties */
    qdev = qdev_create(bus, driver);
    id = qemu_opts_id(opts);
    if (id) {
        qdev->id = id;
    }
    if (qemu_opt_foreach(opts, set_property, qdev, 1) != 0) {
        qdev_free(qdev);
        return NULL;
    }
    if (qdev_init(qdev) < 0) {
        qemu_error("Error initializing device %s\n", driver);
        return NULL;
    }
    qdev->opts = opts;
    return qdev;
}

static void qdev_reset(void *opaque)
{
    DeviceState *dev = opaque;
    if (dev->info->reset)
        dev->info->reset(dev);
}

/* Initialize a device.  Device properties should be set before calling
   this function.  IRQs and MMIO regions should be connected/mapped after
   calling this function.
   On failure, destroy the device and return negative value.
   Return 0 on success.  */
int qdev_init(DeviceState *dev)
{
    int rc;

    assert(dev->state == DEV_STATE_CREATED);
    rc = dev->info->init(dev, dev->info);
    if (rc < 0) {
        qdev_free(dev);
        return rc;
    }
    qemu_register_reset(qdev_reset, dev);
    if (dev->info->vmsd)
        vmstate_register(-1, dev->info->vmsd, dev);
    dev->state = DEV_STATE_INITIALIZED;
    return 0;
}

int qdev_unplug(DeviceState *dev)
{
    if (!dev->parent_bus->allow_hotplug) {
        qemu_error("Bus %s does not support hotplugging\n",
                   dev->parent_bus->name);
        return -1;
    }
    assert(dev->info->unplug != NULL);

    return dev->info->unplug(dev);
}

/* can be used as ->unplug() callback for the simple cases */
int qdev_simple_unplug_cb(DeviceState *dev)
{
    /* just zap it */
    qdev_free(dev);
    return 0;
}

/* Like qdev_init(), but terminate program via hw_error() instead of
   returning an error value.  This is okay during machine creation.
   Don't use for hotplug, because there callers need to recover from
   failure.  Exception: if you know the device's init() callback can't
   fail, then qdev_init_nofail() can't fail either, and is therefore
   usable even then.  But relying on the device implementation that
   way is somewhat unclean, and best avoided.  */
void qdev_init_nofail(DeviceState *dev)
{
    DeviceInfo *info = dev->info;

    if (qdev_init(dev) < 0)
        hw_error("Initialization of device %s failed\n", info->name);
}

/* Unlink device from bus and free the structure.  */
void qdev_free(DeviceState *dev)
{
    BusState *bus;

    if (dev->state == DEV_STATE_INITIALIZED) {
        while (dev->num_child_bus) {
            bus = QLIST_FIRST(&dev->child_bus);
            qbus_free(bus);
        }
        if (dev->info->vmsd)
            vmstate_unregister(dev->info->vmsd, dev);
        if (dev->info->exit)
            dev->info->exit(dev);
        if (dev->opts)
            qemu_opts_del(dev->opts);
    }
    qemu_unregister_reset(qdev_reset, dev);
    QLIST_REMOVE(dev, sibling);
    qemu_free(dev);
}

void qdev_machine_creation_done(void)
{
    /*
     * ok, initial machine setup is done, starting from now we can
     * only create hotpluggable devices
     */
    qdev_hotplug = 1;
}

/* Get a character (serial) device interface.  */
CharDriverState *qdev_init_chardev(DeviceState *dev)
{
    static int next_serial;

    /* FIXME: This function needs to go away: use chardev properties!  */
    return serial_hds[next_serial++];
}

BusState *qdev_get_parent_bus(DeviceState *dev)
{
    return dev->parent_bus;
}

void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n)
{
    assert(dev->num_gpio_in == 0);
    dev->num_gpio_in = n;
    dev->gpio_in = qemu_allocate_irqs(handler, dev, n);
}

void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n)
{
    assert(dev->num_gpio_out == 0);
    dev->num_gpio_out = n;
    dev->gpio_out = pins;
}

qemu_irq qdev_get_gpio_in(DeviceState *dev, int n)
{
    assert(n >= 0 && n < dev->num_gpio_in);
    return dev->gpio_in[n];
}

void qdev_connect_gpio_out(DeviceState * dev, int n, qemu_irq pin)
{
    assert(n >= 0 && n < dev->num_gpio_out);
    dev->gpio_out[n] = pin;
}

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd)
{
    qdev_prop_set_macaddr(dev, "mac", nd->macaddr);
    if (nd->vlan)
        qdev_prop_set_vlan(dev, "vlan", nd->vlan);
    if (nd->netdev)
        qdev_prop_set_netdev(dev, "netdev", nd->netdev);
    if (nd->nvectors != DEV_NVECTORS_UNSPECIFIED &&
        qdev_prop_exists(dev, "vectors")) {
        qdev_prop_set_uint32(dev, "vectors", nd->nvectors);
    }
}

static int next_block_unit[IF_COUNT];

/* Get a block device.  This should only be used for single-drive devices
   (e.g. SD/Floppy/MTD).  Multi-disk devices (scsi/ide) should use the
   appropriate bus.  */
BlockDriverState *qdev_init_bdrv(DeviceState *dev, BlockInterfaceType type)
{
    int unit = next_block_unit[type]++;
    DriveInfo *dinfo;

    dinfo = drive_get(type, 0, unit);
    return dinfo ? dinfo->bdrv : NULL;
}

BusState *qdev_get_child_bus(DeviceState *dev, const char *name)
{
    BusState *bus;

    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        if (strcmp(name, bus->name) == 0) {
            return bus;
        }
    }
    return NULL;
}

static BusState *qbus_find_recursive(BusState *bus, const char *name,
                                     const BusInfo *info)
{
    DeviceState *dev;
    BusState *child, *ret;
    int match = 1;

    if (name && (strcmp(bus->name, name) != 0)) {
        match = 0;
    }
    if (info && (bus->info != info)) {
        match = 0;
    }
    if (match) {
        return bus;
    }

    QLIST_FOREACH(dev, &bus->children, sibling) {
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qbus_find_recursive(child, name, info);
            if (ret) {
                return ret;
            }
        }
    }
    return NULL;
}

static DeviceState *qdev_find_recursive(BusState *bus, const char *id)
{
    DeviceState *dev, *ret;
    BusState *child;

    QLIST_FOREACH(dev, &bus->children, sibling) {
        if (dev->id && strcmp(dev->id, id) == 0)
            return dev;
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qdev_find_recursive(child, id);
            if (ret) {
                return ret;
            }
        }
    }
    return NULL;
}

static void qbus_list_bus(DeviceState *dev, char *dest, int len)
{
    BusState *child;
    const char *sep = " ";
    int pos = 0;

    pos += snprintf(dest+pos, len-pos,"child busses at \"%s\":",
                    dev->id ? dev->id : dev->info->name);
    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        pos += snprintf(dest+pos, len-pos, "%s\"%s\"", sep, child->name);
        sep = ", ";
    }
}

static void qbus_list_dev(BusState *bus, char *dest, int len)
{
    DeviceState *dev;
    const char *sep = " ";
    int pos = 0;

    pos += snprintf(dest+pos, len-pos, "devices at \"%s\":",
                    bus->name);
    QLIST_FOREACH(dev, &bus->children, sibling) {
        pos += snprintf(dest+pos, len-pos, "%s\"%s\"",
                        sep, dev->info->name);
        if (dev->id)
            pos += snprintf(dest+pos, len-pos, "/\"%s\"", dev->id);
        sep = ", ";
    }
}

static BusState *qbus_find_bus(DeviceState *dev, char *elem)
{
    BusState *child;

    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        if (strcmp(child->name, elem) == 0) {
            return child;
        }
    }
    return NULL;
}

static DeviceState *qbus_find_dev(BusState *bus, char *elem)
{
    DeviceState *dev;

    /*
     * try to match in order:
     *   (1) instance id, if present
     *   (2) driver name
     *   (3) driver alias, if present
     */
    QLIST_FOREACH(dev, &bus->children, sibling) {
        if (dev->id  &&  strcmp(dev->id, elem) == 0) {
            return dev;
        }
    }
    QLIST_FOREACH(dev, &bus->children, sibling) {
        if (strcmp(dev->info->name, elem) == 0) {
            return dev;
        }
    }
    QLIST_FOREACH(dev, &bus->children, sibling) {
        if (dev->info->alias && strcmp(dev->info->alias, elem) == 0) {
            return dev;
        }
    }
    return NULL;
}

static BusState *qbus_find(const char *path)
{
    DeviceState *dev;
    BusState *bus;
    char elem[128], msg[256];
    int pos, len;

    /* find start element */
    if (path[0] == '/') {
        bus = main_system_bus;
        pos = 0;
    } else {
        if (sscanf(path, "%127[^/]%n", elem, &len) != 1) {
            qemu_error("path parse error (\"%s\")\n", path);
            return NULL;
        }
        bus = qbus_find_recursive(main_system_bus, elem, NULL);
        if (!bus) {
            qemu_error("bus \"%s\" not found\n", elem);
            return NULL;
        }
        pos = len;
    }

    for (;;) {
        if (path[pos] == '\0') {
            /* we are done */
            return bus;
        }

        /* find device */
        if (sscanf(path+pos, "/%127[^/]%n", elem, &len) != 1) {
            qemu_error("path parse error (\"%s\" pos %d)\n", path, pos);
            return NULL;
        }
        pos += len;
        dev = qbus_find_dev(bus, elem);
        if (!dev) {
            qbus_list_dev(bus, msg, sizeof(msg));
            qemu_error("device \"%s\" not found\n%s\n", elem, msg);
            return NULL;
        }
        if (path[pos] == '\0') {
            /* last specified element is a device.  If it has exactly
             * one child bus accept it nevertheless */
            switch (dev->num_child_bus) {
            case 0:
                qemu_error("device has no child bus (%s)\n", path);
                return NULL;
            case 1:
                return QLIST_FIRST(&dev->child_bus);
            default:
                qbus_list_bus(dev, msg, sizeof(msg));
                qemu_error("device has multiple child busses (%s)\n%s\n",
                           path, msg);
                return NULL;
            }
        }

        /* find bus */
        if (sscanf(path+pos, "/%127[^/]%n", elem, &len) != 1) {
            qemu_error("path parse error (\"%s\" pos %d)\n", path, pos);
            return NULL;
        }
        pos += len;
        bus = qbus_find_bus(dev, elem);
        if (!bus) {
            qbus_list_bus(dev, msg, sizeof(msg));
            qemu_error("child bus \"%s\" not found\n%s\n", elem, msg);
            return NULL;
        }
    }
}

void qbus_create_inplace(BusState *bus, BusInfo *info,
                         DeviceState *parent, const char *name)
{
    char *buf;
    int i,len;

    bus->info = info;
    bus->parent = parent;

    if (name) {
        /* use supplied name */
        bus->name = qemu_strdup(name);
    } else if (parent && parent->id) {
        /* parent device has id -> use it for bus name */
        len = strlen(parent->id) + 16;
        buf = qemu_malloc(len);
        snprintf(buf, len, "%s.%d", parent->id, parent->num_child_bus);
        bus->name = buf;
    } else {
        /* no id -> use lowercase bus type for bus name */
        len = strlen(info->name) + 16;
        buf = qemu_malloc(len);
        len = snprintf(buf, len, "%s.%d", info->name,
                       parent ? parent->num_child_bus : 0);
        for (i = 0; i < len; i++)
            buf[i] = qemu_tolower(buf[i]);
        bus->name = buf;
    }

    QLIST_INIT(&bus->children);
    if (parent) {
        QLIST_INSERT_HEAD(&parent->child_bus, bus, sibling);
        parent->num_child_bus++;
    }

}

BusState *qbus_create(BusInfo *info, DeviceState *parent, const char *name)
{
    BusState *bus;

    bus = qemu_mallocz(info->size);
    bus->qdev_allocated = 1;
    qbus_create_inplace(bus, info, parent, name);
    return bus;
}

void qbus_free(BusState *bus)
{
    DeviceState *dev;

    while ((dev = QLIST_FIRST(&bus->children)) != NULL) {
        qdev_free(dev);
    }
    if (bus->parent) {
        QLIST_REMOVE(bus, sibling);
        bus->parent->num_child_bus--;
    }
    if (bus->qdev_allocated) {
        qemu_free(bus);
    }
}

#define qdev_printf(fmt, ...) monitor_printf(mon, "%*s" fmt, indent, "", ## __VA_ARGS__)
static void qbus_print(Monitor *mon, BusState *bus, int indent);

static void qdev_print_props(Monitor *mon, DeviceState *dev, Property *props,
                             const char *prefix, int indent)
{
    char buf[64];

    if (!props)
        return;
    while (props->name) {
        if (props->info->print) {
            props->info->print(dev, props, buf, sizeof(buf));
            qdev_printf("%s-prop: %s = %s\n", prefix, props->name, buf);
        }
        props++;
    }
}

static void qdev_print(Monitor *mon, DeviceState *dev, int indent)
{
    BusState *child;
    qdev_printf("dev: %s, id \"%s\"\n", dev->info->name,
                dev->id ? dev->id : "");
    indent += 2;
    if (dev->num_gpio_in) {
        qdev_printf("gpio-in %d\n", dev->num_gpio_in);
    }
    if (dev->num_gpio_out) {
        qdev_printf("gpio-out %d\n", dev->num_gpio_out);
    }
    qdev_print_props(mon, dev, dev->info->props, "dev", indent);
    qdev_print_props(mon, dev, dev->parent_bus->info->props, "bus", indent);
    if (dev->parent_bus->info->print_dev)
        dev->parent_bus->info->print_dev(mon, dev, indent);
    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        qbus_print(mon, child, indent);
    }
}

static void qbus_print(Monitor *mon, BusState *bus, int indent)
{
    struct DeviceState *dev;

    qdev_printf("bus: %s\n", bus->name);
    indent += 2;
    qdev_printf("type %s\n", bus->info->name);
    QLIST_FOREACH(dev, &bus->children, sibling) {
        qdev_print(mon, dev, indent);
    }
}
#undef qdev_printf

void do_info_qtree(Monitor *mon)
{
    if (main_system_bus)
        qbus_print(mon, main_system_bus, 0);
}

void do_info_qdm(Monitor *mon)
{
    DeviceInfo *info;

    for (info = device_info_list; info != NULL; info = info->next) {
        qdev_print_devinfo(info);
    }
}

void do_device_add(Monitor *mon, const QDict *qdict)
{
    QemuOpts *opts;

    opts = qemu_opts_parse(&qemu_device_opts,
                           qdict_get_str(qdict, "config"), "driver");
    if (opts) {
        if (qdev_device_help(opts) || qdev_device_add(opts) == NULL) {
            qemu_opts_del(opts);
        }
    }
}

void do_device_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    DeviceState *dev;

    dev = qdev_find_recursive(main_system_bus, id);
    if (NULL == dev) {
        qemu_error("Device '%s' not found\n", id);
        return;
    }
    qdev_unplug(dev);
}
