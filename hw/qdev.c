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

static int qdev_hotplug = 0;
static bool qdev_hot_added = false;
static bool qdev_hot_removed = false;

/* This is a nasty hack to allow passing a NULL bus to qdev_create.  */
static BusState *main_system_bus;
static void main_system_bus_create(void);

DeviceInfo *device_info_list;

static BusState *qbus_find_recursive(BusState *bus, const char *name,
                                     const BusInfo *info);
static BusState *qbus_find(const char *path);

/* Register a new device type.  */
static void qdev_subclass_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->info = data;
    dc->reset = dc->info->reset;

    /* Poison to try to detect future uses */
    dc->info->reset = NULL;

    if (dc->info->class_init) {
        dc->info->class_init(klass, data);
    }
}

DeviceInfo *qdev_get_info(DeviceState *dev)
{
    return DEVICE_GET_CLASS(dev)->info;
}

void qdev_register_subclass(DeviceInfo *info, const char *parent)
{
    TypeInfo type_info = {};

    assert(info->size >= sizeof(DeviceState));
    assert(!info->next);

    type_info.name = info->name;
    type_info.parent = parent;
    type_info.instance_size = info->size;
    type_info.class_init = qdev_subclass_init;
    type_info.class_data = info;

    type_register_static(&type_info);

    info->next = device_info_list;
    device_info_list = info;
}

void qdev_register(DeviceInfo *info)
{
    qdev_register_subclass(info, TYPE_DEVICE);
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

bool qdev_exists(const char *name)
{
    return !!qdev_find_info(NULL, name);
}
static void qdev_property_add_legacy(DeviceState *dev, Property *prop,
                                     Error **errp);

static DeviceState *qdev_create_from_info(BusState *bus, DeviceInfo *info)
{
    DeviceState *dev;
    Property *prop;

    assert(bus->info == info->bus_info);
    dev = DEVICE(object_new(info->name));
    dev->parent_bus = bus;
    qdev_prop_set_defaults(dev, qdev_get_info(dev)->props);
    qdev_prop_set_defaults(dev, dev->parent_bus->info->props);
    qdev_prop_set_globals(dev);
    QTAILQ_INSERT_HEAD(&bus->children, dev, sibling);
    if (qdev_hotplug) {
        assert(bus->allow_hotplug);
        dev->hotplugged = 1;
        qdev_hot_added = true;
    }
    dev->instance_id_alias = -1;
    QTAILQ_INIT(&dev->properties);
    dev->state = DEV_STATE_CREATED;

    for (prop = qdev_get_info(dev)->props; prop && prop->name; prop++) {
        qdev_property_add_legacy(dev, prop, NULL);
        qdev_property_add_static(dev, prop, NULL);
    }

    for (prop = qdev_get_info(dev)->bus_info->props; prop && prop->name; prop++) {
        qdev_property_add_legacy(dev, prop, NULL);
        qdev_property_add_static(dev, prop, NULL);
    }

    qdev_property_add_str(dev, "type", qdev_get_type, NULL, NULL);

    return dev;
}

/* Create a new device.  This only initializes the device state structure
   and allows properties to be set.  qdev_init should be called to
   initialize the actual device emulation.  */
DeviceState *qdev_create(BusState *bus, const char *name)
{
    DeviceState *dev;

    dev = qdev_try_create(bus, name);
    if (!dev) {
        if (bus) {
            hw_error("Unknown device '%s' for bus '%s'\n", name,
                     bus->info->name);
        } else {
            hw_error("Unknown device '%s' for default sysbus\n", name);
        }
    }

    return dev;
}

DeviceState *qdev_try_create(BusState *bus, const char *name)
{
    DeviceInfo *info;

    if (!bus) {
        bus = sysbus_get_default();
    }

    info = qdev_find_info(bus->info, name);
    if (!info) {
        return NULL;
    }

    return qdev_create_from_info(bus, info);
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
            if (info->no_user) {
                continue;       /* not available, don't show */
            }
            qdev_print_devinfo(info);
        }
        return 1;
    }

    if (!driver || !qemu_opt_get(opts, "?")) {
        return 0;
    }

    info = qdev_find_info(NULL, driver);
    if (!info) {
        return 0;
    }

    for (prop = info->props; prop && prop->name; prop++) {
        /*
         * TODO Properties without a parser are just for dirty hacks.
         * qdev_prop_ptr is the only such PropertyInfo.  It's marked
         * for removal.  This conditional should be removed along with
         * it.
         */
        if (!prop->info->parse) {
            continue;           /* no way to set it, don't show */
        }
        error_printf("%s.%s=%s\n", info->name, prop->name,
                     prop->info->legacy_name ?: prop->info->name);
    }
    for (prop = info->bus_info->props; prop && prop->name; prop++) {
        if (!prop->info->parse) {
            continue;           /* no way to set it, don't show */
        }
        error_printf("%s.%s=%s\n", info->name, prop->name,
                     prop->info->legacy_name ?: prop->info->name);
    }
    return 1;
}

static DeviceState *qdev_get_peripheral(void)
{
    static DeviceState *dev;

    if (dev == NULL) {
        dev = qdev_create(NULL, "container");
        qdev_property_add_child(qdev_get_root(), "peripheral", dev, NULL);
        qdev_init_nofail(dev);
    }

    return dev;
}

static DeviceState *qdev_get_peripheral_anon(void)
{
    static DeviceState *dev;

    if (dev == NULL) {
        dev = qdev_create(NULL, "container");
        qdev_property_add_child(qdev_get_root(), "peripheral-anon", dev, NULL);
        qdev_init_nofail(dev);
    }

    return dev;
}

DeviceState *qdev_device_add(QemuOpts *opts)
{
    const char *driver, *path, *id;
    DeviceInfo *info;
    DeviceState *qdev;
    BusState *bus;

    driver = qemu_opt_get(opts, "driver");
    if (!driver) {
        qerror_report(QERR_MISSING_PARAMETER, "driver");
        return NULL;
    }

    /* find driver */
    info = qdev_find_info(NULL, driver);
    if (!info || info->no_user) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "driver", "a driver name");
        error_printf_unless_qmp("Try with argument '?' for a list.\n");
        return NULL;
    }

    /* find bus */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
        bus = qbus_find(path);
        if (!bus) {
            return NULL;
        }
        if (bus->info != info->bus_info) {
            qerror_report(QERR_BAD_BUS_FOR_DEVICE,
                           driver, bus->info->name);
            return NULL;
        }
    } else {
        bus = qbus_find_recursive(main_system_bus, NULL, info->bus_info);
        if (!bus) {
            qerror_report(QERR_NO_BUS_FOR_DEVICE,
                           info->name, info->bus_info->name);
            return NULL;
        }
    }
    if (qdev_hotplug && !bus->allow_hotplug) {
        qerror_report(QERR_BUS_NO_HOTPLUG, bus->name);
        return NULL;
    }

    /* create device, set properties */
    qdev = qdev_create_from_info(bus, info);
    id = qemu_opts_id(opts);
    if (id) {
        qdev->id = id;
        qdev_property_add_child(qdev_get_peripheral(), qdev->id, qdev, NULL);
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        qdev_property_add_child(qdev_get_peripheral_anon(), name,
                                qdev, NULL);
        g_free(name);
    }        
    if (qemu_opt_foreach(opts, set_property, qdev, 1) != 0) {
        qdev_free(qdev);
        return NULL;
    }
    if (qdev_init(qdev) < 0) {
        qerror_report(QERR_DEVICE_INIT_FAILED, driver);
        return NULL;
    }
    qdev->opts = opts;
    return qdev;
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
    rc = qdev_get_info(dev)->init(dev, qdev_get_info(dev));
    if (rc < 0) {
        qdev_free(dev);
        return rc;
    }
    if (qdev_get_info(dev)->vmsd) {
        vmstate_register_with_alias_id(dev, -1, qdev_get_info(dev)->vmsd, dev,
                                       dev->instance_id_alias,
                                       dev->alias_required_for_version);
    }
    dev->state = DEV_STATE_INITIALIZED;
    if (dev->hotplugged) {
        device_reset(dev);
    }
    return 0;
}

void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version)
{
    assert(dev->state == DEV_STATE_CREATED);
    dev->instance_id_alias = alias_id;
    dev->alias_required_for_version = required_for_version;
}

int qdev_unplug(DeviceState *dev)
{
    if (!dev->parent_bus->allow_hotplug) {
        qerror_report(QERR_BUS_NO_HOTPLUG, dev->parent_bus->name);
        return -1;
    }
    assert(qdev_get_info(dev)->unplug != NULL);

    qdev_hot_removed = true;

    return qdev_get_info(dev)->unplug(dev);
}

static int qdev_reset_one(DeviceState *dev, void *opaque)
{
    device_reset(dev);

    return 0;
}

BusState *sysbus_get_default(void)
{
    if (!main_system_bus) {
        main_system_bus_create();
    }
    return main_system_bus;
}

static int qbus_reset_one(BusState *bus, void *opaque)
{
    if (bus->info->reset) {
        return bus->info->reset(bus);
    }
    return 0;
}

void qdev_reset_all(DeviceState *dev)
{
    qdev_walk_children(dev, qdev_reset_one, qbus_reset_one, NULL);
}

void qbus_reset_all_fn(void *opaque)
{
    BusState *bus = opaque;
    qbus_walk_children(bus, qdev_reset_one, qbus_reset_one, NULL);
}

/* can be used as ->unplug() callback for the simple cases */
int qdev_simple_unplug_cb(DeviceState *dev)
{
    /* just zap it */
    qdev_free(dev);
    return 0;
}


/* Like qdev_init(), but terminate program via error_report() instead of
   returning an error value.  This is okay during machine creation.
   Don't use for hotplug, because there callers need to recover from
   failure.  Exception: if you know the device's init() callback can't
   fail, then qdev_init_nofail() can't fail either, and is therefore
   usable even then.  But relying on the device implementation that
   way is somewhat unclean, and best avoided.  */
void qdev_init_nofail(DeviceState *dev)
{
    DeviceInfo *info = qdev_get_info(dev);

    if (qdev_init(dev) < 0) {
        error_report("Initialization of device %s failed", info->name);
        exit(1);
    }
}

static void qdev_property_del_all(DeviceState *dev)
{
    while (!QTAILQ_EMPTY(&dev->properties)) {
        DeviceProperty *prop = QTAILQ_FIRST(&dev->properties);

        QTAILQ_REMOVE(&dev->properties, prop, node);

        if (prop->release) {
            prop->release(dev, prop->name, prop->opaque);
        }

        g_free(prop->name);
        g_free(prop->type);
        g_free(prop);
    }
}

static void qdev_property_del_child(DeviceState *dev, DeviceState *child, Error **errp)
{
    DeviceProperty *prop;

    QTAILQ_FOREACH(prop, &dev->properties, node) {
        if (strstart(prop->type, "child<", NULL) && prop->opaque == child) {
            break;
        }
    }

    g_assert(prop != NULL);

    QTAILQ_REMOVE(&dev->properties, prop, node);

    if (prop->release) {
        prop->release(dev, prop->name, prop->opaque);
    }

    g_free(prop->name);
    g_free(prop->type);
    g_free(prop);
}

/* Unlink device from bus and free the structure.  */
void qdev_free(DeviceState *dev)
{
    BusState *bus;
    Property *prop;

    qdev_property_del_all(dev);

    if (dev->state == DEV_STATE_INITIALIZED) {
        while (dev->num_child_bus) {
            bus = QLIST_FIRST(&dev->child_bus);
            qbus_free(bus);
        }
        if (qdev_get_info(dev)->vmsd)
            vmstate_unregister(dev, qdev_get_info(dev)->vmsd, dev);
        if (qdev_get_info(dev)->exit)
            qdev_get_info(dev)->exit(dev);
        if (dev->opts)
            qemu_opts_del(dev->opts);
    }
    QTAILQ_REMOVE(&dev->parent_bus->children, dev, sibling);
    for (prop = qdev_get_info(dev)->props; prop && prop->name; prop++) {
        if (prop->info->free) {
            prop->info->free(dev, prop);
        }
    }
    if (dev->parent) {
        qdev_property_del_child(dev->parent, dev, NULL);
    }
    if (dev->ref != 0) {
        qerror_report(QERR_DEVICE_IN_USE, dev->id?:"");
    }
    object_delete(OBJECT(dev));
}

void qdev_machine_creation_done(void)
{
    /*
     * ok, initial machine setup is done, starting from now we can
     * only create hotpluggable devices
     */
    qdev_hotplug = 1;
}

bool qdev_machine_modified(void)
{
    return qdev_hot_added || qdev_hot_removed;
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
    qdev_prop_set_macaddr(dev, "mac", nd->macaddr.a);
    if (nd->vlan)
        qdev_prop_set_vlan(dev, "vlan", nd->vlan);
    if (nd->netdev)
        qdev_prop_set_netdev(dev, "netdev", nd->netdev);
    if (nd->nvectors != DEV_NVECTORS_UNSPECIFIED &&
        qdev_prop_exists(dev, "vectors")) {
        qdev_prop_set_uint32(dev, "vectors", nd->nvectors);
    }
    nd->instantiated = 1;
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

int qbus_walk_children(BusState *bus, qdev_walkerfn *devfn,
                       qbus_walkerfn *busfn, void *opaque)
{
    DeviceState *dev;
    int err;

    if (busfn) {
        err = busfn(bus, opaque);
        if (err) {
            return err;
        }
    }

    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        err = qdev_walk_children(dev, devfn, busfn, opaque);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

int qdev_walk_children(DeviceState *dev, qdev_walkerfn *devfn,
                       qbus_walkerfn *busfn, void *opaque)
{
    BusState *bus;
    int err;

    if (devfn) {
        err = devfn(dev, opaque);
        if (err) {
            return err;
        }
    }

    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        err = qbus_walk_children(bus, devfn, busfn, opaque);
        if (err < 0) {
            return err;
        }
    }

    return 0;
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

    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qbus_find_recursive(child, name, info);
            if (ret) {
                return ret;
            }
        }
    }
    return NULL;
}

DeviceState *qdev_find_recursive(BusState *bus, const char *id)
{
    DeviceState *dev, *ret;
    BusState *child;

    QTAILQ_FOREACH(dev, &bus->children, sibling) {
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

static void qbus_list_bus(DeviceState *dev)
{
    BusState *child;
    const char *sep = " ";

    error_printf("child busses at \"%s\":",
                 dev->id ? dev->id : object_get_typename(OBJECT(dev)));
    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        error_printf("%s\"%s\"", sep, child->name);
        sep = ", ";
    }
    error_printf("\n");
}

static void qbus_list_dev(BusState *bus)
{
    DeviceState *dev;
    const char *sep = " ";

    error_printf("devices at \"%s\":", bus->name);
    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        error_printf("%s\"%s\"", sep, object_get_typename(OBJECT(dev)));
        if (dev->id)
            error_printf("/\"%s\"", dev->id);
        sep = ", ";
    }
    error_printf("\n");
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
    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        if (dev->id  &&  strcmp(dev->id, elem) == 0) {
            return dev;
        }
    }
    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        if (strcmp(object_get_typename(OBJECT(dev)), elem) == 0) {
            return dev;
        }
    }
    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        if (qdev_get_info(dev)->alias && strcmp(qdev_get_info(dev)->alias, elem) == 0) {
            return dev;
        }
    }
    return NULL;
}

static BusState *qbus_find(const char *path)
{
    DeviceState *dev;
    BusState *bus;
    char elem[128];
    int pos, len;

    /* find start element */
    if (path[0] == '/') {
        bus = main_system_bus;
        pos = 0;
    } else {
        if (sscanf(path, "%127[^/]%n", elem, &len) != 1) {
            assert(!path[0]);
            elem[0] = len = 0;
        }
        bus = qbus_find_recursive(main_system_bus, elem, NULL);
        if (!bus) {
            qerror_report(QERR_BUS_NOT_FOUND, elem);
            return NULL;
        }
        pos = len;
    }

    for (;;) {
        assert(path[pos] == '/' || !path[pos]);
        while (path[pos] == '/') {
            pos++;
        }
        if (path[pos] == '\0') {
            return bus;
        }

        /* find device */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            assert(0);
            elem[0] = len = 0;
        }
        pos += len;
        dev = qbus_find_dev(bus, elem);
        if (!dev) {
            qerror_report(QERR_DEVICE_NOT_FOUND, elem);
            if (!monitor_cur_is_qmp()) {
                qbus_list_dev(bus);
            }
            return NULL;
        }

        assert(path[pos] == '/' || !path[pos]);
        while (path[pos] == '/') {
            pos++;
        }
        if (path[pos] == '\0') {
            /* last specified element is a device.  If it has exactly
             * one child bus accept it nevertheless */
            switch (dev->num_child_bus) {
            case 0:
                qerror_report(QERR_DEVICE_NO_BUS, elem);
                return NULL;
            case 1:
                return QLIST_FIRST(&dev->child_bus);
            default:
                qerror_report(QERR_DEVICE_MULTIPLE_BUSSES, elem);
                if (!monitor_cur_is_qmp()) {
                    qbus_list_bus(dev);
                }
                return NULL;
            }
        }

        /* find bus */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            assert(0);
            elem[0] = len = 0;
        }
        pos += len;
        bus = qbus_find_bus(dev, elem);
        if (!bus) {
            qerror_report(QERR_BUS_NOT_FOUND, elem);
            if (!monitor_cur_is_qmp()) {
                qbus_list_bus(dev);
            }
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
        bus->name = g_strdup(name);
    } else if (parent && parent->id) {
        /* parent device has id -> use it for bus name */
        len = strlen(parent->id) + 16;
        buf = g_malloc(len);
        snprintf(buf, len, "%s.%d", parent->id, parent->num_child_bus);
        bus->name = buf;
    } else {
        /* no id -> use lowercase bus type for bus name */
        len = strlen(info->name) + 16;
        buf = g_malloc(len);
        len = snprintf(buf, len, "%s.%d", info->name,
                       parent ? parent->num_child_bus : 0);
        for (i = 0; i < len; i++)
            buf[i] = qemu_tolower(buf[i]);
        bus->name = buf;
    }

    QTAILQ_INIT(&bus->children);
    if (parent) {
        QLIST_INSERT_HEAD(&parent->child_bus, bus, sibling);
        parent->num_child_bus++;
    } else if (bus != main_system_bus) {
        /* TODO: once all bus devices are qdevified,
           only reset handler for main_system_bus should be registered here. */
        qemu_register_reset(qbus_reset_all_fn, bus);
    }
}

BusState *qbus_create(BusInfo *info, DeviceState *parent, const char *name)
{
    BusState *bus;

    bus = g_malloc0(info->size);
    bus->qdev_allocated = 1;
    qbus_create_inplace(bus, info, parent, name);
    return bus;
}

static void main_system_bus_create(void)
{
    /* assign main_system_bus before qbus_create_inplace()
     * in order to make "if (bus != main_system_bus)" work */
    main_system_bus = g_malloc0(system_bus_info.size);
    main_system_bus->qdev_allocated = 1;
    qbus_create_inplace(main_system_bus, &system_bus_info, NULL,
                        "main-system-bus");
}

void qbus_free(BusState *bus)
{
    DeviceState *dev;

    while ((dev = QTAILQ_FIRST(&bus->children)) != NULL) {
        qdev_free(dev);
    }
    if (bus->parent) {
        QLIST_REMOVE(bus, sibling);
        bus->parent->num_child_bus--;
    } else {
        assert(bus != main_system_bus); /* main_system_bus is never freed */
        qemu_unregister_reset(qbus_reset_all_fn, bus);
    }
    g_free((void*)bus->name);
    if (bus->qdev_allocated) {
        g_free(bus);
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
        /*
         * TODO Properties without a print method are just for dirty
         * hacks.  qdev_prop_ptr is the only such PropertyInfo.  It's
         * marked for removal.  The test props->info->print should be
         * removed along with it.
         */
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
    qdev_printf("dev: %s, id \"%s\"\n", object_get_typename(OBJECT(dev)),
                dev->id ? dev->id : "");
    indent += 2;
    if (dev->num_gpio_in) {
        qdev_printf("gpio-in %d\n", dev->num_gpio_in);
    }
    if (dev->num_gpio_out) {
        qdev_printf("gpio-out %d\n", dev->num_gpio_out);
    }
    qdev_print_props(mon, dev, qdev_get_info(dev)->props, "dev", indent);
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
    QTAILQ_FOREACH(dev, &bus->children, sibling) {
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

int do_device_add(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    QemuOpts *opts;

    opts = qemu_opts_from_qdict(qemu_find_opts("device"), qdict);
    if (!opts) {
        return -1;
    }
    if (!monitor_cur_is_qmp() && qdev_device_help(opts)) {
        qemu_opts_del(opts);
        return 0;
    }
    if (!qdev_device_add(opts)) {
        qemu_opts_del(opts);
        return -1;
    }
    return 0;
}

int do_device_del(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    const char *id = qdict_get_str(qdict, "id");
    DeviceState *dev;

    dev = qdev_find_recursive(main_system_bus, id);
    if (NULL == dev) {
        qerror_report(QERR_DEVICE_NOT_FOUND, id);
        return -1;
    }
    return qdev_unplug(dev);
}

static int qdev_get_fw_dev_path_helper(DeviceState *dev, char *p, int size)
{
    int l = 0;

    if (dev && dev->parent_bus) {
        char *d;
        l = qdev_get_fw_dev_path_helper(dev->parent_bus->parent, p, size);
        if (dev->parent_bus->info->get_fw_dev_path) {
            d = dev->parent_bus->info->get_fw_dev_path(dev);
            l += snprintf(p + l, size - l, "%s", d);
            g_free(d);
        } else {
            l += snprintf(p + l, size - l, "%s", object_get_typename(OBJECT(dev)));
        }
    }
    l += snprintf(p + l , size - l, "/");

    return l;
}

char* qdev_get_fw_dev_path(DeviceState *dev)
{
    char path[128];
    int l;

    l = qdev_get_fw_dev_path_helper(dev, path, 128);

    path[l-1] = '\0';

    return strdup(path);
}

char *qdev_get_type(DeviceState *dev, Error **errp)
{
    return g_strdup(object_get_typename(OBJECT(dev)));
}

void qdev_ref(DeviceState *dev)
{
    dev->ref++;
}

void qdev_unref(DeviceState *dev)
{
    g_assert(dev->ref > 0);
    dev->ref--;
}

void qdev_property_add(DeviceState *dev, const char *name, const char *type,
                       DevicePropertyAccessor *get, DevicePropertyAccessor *set,
                       DevicePropertyRelease *release,
                       void *opaque, Error **errp)
{
    DeviceProperty *prop = g_malloc0(sizeof(*prop));

    prop->name = g_strdup(name);
    prop->type = g_strdup(type);

    prop->get = get;
    prop->set = set;
    prop->release = release;
    prop->opaque = opaque;

    QTAILQ_INSERT_TAIL(&dev->properties, prop, node);
}

static DeviceProperty *qdev_property_find(DeviceState *dev, const char *name)
{
    DeviceProperty *prop;

    QTAILQ_FOREACH(prop, &dev->properties, node) {
        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }

    return NULL;
}

void qdev_property_get(DeviceState *dev, Visitor *v, const char *name,
                       Error **errp)
{
    DeviceProperty *prop = qdev_property_find(dev, name);

    if (prop == NULL) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND, dev->id?:"", name);
        return;
    }

    if (!prop->get) {
        error_set(errp, QERR_PERMISSION_DENIED);
    } else {
        prop->get(dev, v, prop->opaque, name, errp);
    }
}

void qdev_property_set(DeviceState *dev, Visitor *v, const char *name,
                       Error **errp)
{
    DeviceProperty *prop = qdev_property_find(dev, name);

    if (prop == NULL) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND, dev->id?:"", name);
        return;
    }

    if (!prop->set) {
        error_set(errp, QERR_PERMISSION_DENIED);
    } else {
        prop->set(dev, v, prop->opaque, name, errp);
    }
}

const char *qdev_property_get_type(DeviceState *dev, const char *name, Error **errp)
{
    DeviceProperty *prop = qdev_property_find(dev, name);

    if (prop == NULL) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND, dev->id?:"", name);
        return NULL;
    }

    return prop->type;
}

/**
 * Legacy property handling
 */

static void qdev_get_legacy_property(DeviceState *dev, Visitor *v, void *opaque,
                                     const char *name, Error **errp)
{
    Property *prop = opaque;

    char buffer[1024];
    char *ptr = buffer;

    prop->info->print(dev, prop, buffer, sizeof(buffer));
    visit_type_str(v, &ptr, name, errp);
}

static void qdev_set_legacy_property(DeviceState *dev, Visitor *v, void *opaque,
                                     const char *name, Error **errp)
{
    Property *prop = opaque;
    Error *local_err = NULL;
    char *ptr = NULL;
    int ret;

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_str(v, &ptr, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    ret = prop->info->parse(dev, prop, ptr);
    error_set_from_qdev_prop_error(errp, ret, dev, prop, ptr);
    g_free(ptr);
}

/**
 * @qdev_add_legacy_property - adds a legacy property
 *
 * Do not use this is new code!  Properties added through this interface will
 * be given names and types in the "legacy" namespace.
 *
 * Legacy properties are always processed as strings.  The format of the string
 * depends on the property type.
 */
void qdev_property_add_legacy(DeviceState *dev, Property *prop,
                              Error **errp)
{
    gchar *name, *type;

    name = g_strdup_printf("legacy-%s", prop->name);
    type = g_strdup_printf("legacy<%s>",
                           prop->info->legacy_name ?: prop->info->name);

    qdev_property_add(dev, name, type,
                      prop->info->print ? qdev_get_legacy_property : NULL,
                      prop->info->parse ? qdev_set_legacy_property : NULL,
                      NULL,
                      prop, errp);

    g_free(type);
    g_free(name);
}

/**
 * @qdev_property_add_static - add a @Property to a device.
 *
 * Static properties access data in a struct.  The actual type of the
 * property and the field depends on the property type.
 */
void qdev_property_add_static(DeviceState *dev, Property *prop,
                              Error **errp)
{
    qdev_property_add(dev, prop->name, prop->info->name,
                      prop->info->get, prop->info->set,
                      NULL,
                      prop, errp);
}

DeviceState *qdev_get_root(void)
{
    static DeviceState *qdev_root;

    if (!qdev_root) {
        qdev_root = qdev_create(NULL, "container");
        qdev_init_nofail(qdev_root);
    }

    return qdev_root;
}

static void qdev_get_child_property(DeviceState *dev, Visitor *v, void *opaque,
                                    const char *name, Error **errp)
{
    DeviceState *child = opaque;
    gchar *path;

    path = qdev_get_canonical_path(child);
    visit_type_str(v, &path, name, errp);
    g_free(path);
}

static void qdev_release_child_property(DeviceState *dev, const char *name,
                                        void *opaque)
{
    DeviceState *child = opaque;

    qdev_unref(child);
}

void qdev_property_add_child(DeviceState *dev, const char *name,
                             DeviceState *child, Error **errp)
{
    gchar *type;

    type = g_strdup_printf("child<%s>", object_get_typename(OBJECT(child)));

    qdev_property_add(dev, name, type, qdev_get_child_property,
                      NULL, qdev_release_child_property,
                      child, errp);

    qdev_ref(child);
    g_assert(child->parent == NULL);
    child->parent = dev;

    g_free(type);
}

static void qdev_get_link_property(DeviceState *dev, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    DeviceState **child = opaque;
    gchar *path;

    if (*child) {
        path = qdev_get_canonical_path(*child);
        visit_type_str(v, &path, name, errp);
        g_free(path);
    } else {
        path = (gchar *)"";
        visit_type_str(v, &path, name, errp);
    }
}

static void qdev_set_link_property(DeviceState *dev, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    DeviceState **child = opaque;
    bool ambiguous = false;
    const char *type;
    char *path;

    type = qdev_property_get_type(dev, name, NULL);

    visit_type_str(v, &path, name, errp);

    if (*child) {
        qdev_unref(*child);
    }

    if (strcmp(path, "") != 0) {
        DeviceState *target;

        target = qdev_resolve_path(path, &ambiguous);
        if (target) {
            gchar *target_type;

            target_type = g_strdup_printf("link<%s>", object_get_typename(OBJECT(target)));
            if (strcmp(target_type, type) == 0) {
                *child = target;
                qdev_ref(target);
            } else {
                error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, type);
            }

            g_free(target_type);
        } else {
            error_set(errp, QERR_DEVICE_NOT_FOUND, path);
        }
    } else {
        *child = NULL;
    }

    g_free(path);
}

void qdev_property_add_link(DeviceState *dev, const char *name,
                            const char *type, DeviceState **child,
                            Error **errp)
{
    gchar *full_type;

    full_type = g_strdup_printf("link<%s>", type);

    qdev_property_add(dev, name, full_type,
                      qdev_get_link_property,
                      qdev_set_link_property,
                      NULL, child, errp);

    g_free(full_type);
}

gchar *qdev_get_canonical_path(DeviceState *dev)
{
    DeviceState *root = qdev_get_root();
    char *newpath = NULL, *path = NULL;

    while (dev != root) {
        DeviceProperty *prop = NULL;

        g_assert(dev->parent != NULL);

        QTAILQ_FOREACH(prop, &dev->parent->properties, node) {
            if (!strstart(prop->type, "child<", NULL)) {
                continue;
            }

            if (prop->opaque == dev) {
                if (path) {
                    newpath = g_strdup_printf("%s/%s", prop->name, path);
                    g_free(path);
                    path = newpath;
                } else {
                    path = g_strdup(prop->name);
                }
                break;
            }
        }

        g_assert(prop != NULL);

        dev = dev->parent;
    }

    newpath = g_strdup_printf("/%s", path);
    g_free(path);

    return newpath;
}

static DeviceState *qdev_resolve_abs_path(DeviceState *parent,
                                          gchar **parts,
                                          int index)
{
    DeviceProperty *prop;
    DeviceState *child;

    if (parts[index] == NULL) {
        return parent;
    }

    if (strcmp(parts[index], "") == 0) {
        return qdev_resolve_abs_path(parent, parts, index + 1);
    }

    prop = qdev_property_find(parent, parts[index]);
    if (prop == NULL) {
        return NULL;
    }

    child = NULL;
    if (strstart(prop->type, "link<", NULL)) {
        DeviceState **pchild = prop->opaque;
        if (*pchild) {
            child = *pchild;
        }
    } else if (strstart(prop->type, "child<", NULL)) {
        child = prop->opaque;
    }

    if (!child) {
        return NULL;
    }

    return qdev_resolve_abs_path(child, parts, index + 1);
}

static DeviceState *qdev_resolve_partial_path(DeviceState *parent,
                                              gchar **parts,
                                              bool *ambiguous)
{
    DeviceState *dev;
    DeviceProperty *prop;

    dev = qdev_resolve_abs_path(parent, parts, 0);

    QTAILQ_FOREACH(prop, &parent->properties, node) {
        DeviceState *found;

        if (!strstart(prop->type, "child<", NULL)) {
            continue;
        }

        found = qdev_resolve_partial_path(prop->opaque, parts, ambiguous);
        if (found) {
            if (dev) {
                if (ambiguous) {
                    *ambiguous = true;
                }
                return NULL;
            }
            dev = found;
        }

        if (ambiguous && *ambiguous) {
            return NULL;
        }
    }

    return dev;
}

DeviceState *qdev_resolve_path(const char *path, bool *ambiguous)
{
    bool partial_path = true;
    DeviceState *dev;
    gchar **parts;

    parts = g_strsplit(path, "/", 0);
    if (parts == NULL || parts[0] == NULL) {
        g_strfreev(parts);
        return qdev_get_root();
    }

    if (strcmp(parts[0], "") == 0) {
        partial_path = false;
    }

    if (partial_path) {
        if (ambiguous) {
            *ambiguous = false;
        }
        dev = qdev_resolve_partial_path(qdev_get_root(), parts, ambiguous);
    } else {
        dev = qdev_resolve_abs_path(qdev_get_root(), parts, 1);
    }

    g_strfreev(parts);

    return dev;
}

typedef struct StringProperty
{
    char *(*get)(DeviceState *, Error **);
    void (*set)(DeviceState *, const char *, Error **);
} StringProperty;

static void qdev_property_get_str(DeviceState *dev, Visitor *v, void *opaque,
                                  const char *name, Error **errp)
{
    StringProperty *prop = opaque;
    char *value;

    value = prop->get(dev, errp);
    if (value) {
        visit_type_str(v, &value, name, errp);
        g_free(value);
    }
}

static void qdev_property_set_str(DeviceState *dev, Visitor *v, void *opaque,
                                  const char *name, Error **errp)
{
    StringProperty *prop = opaque;
    char *value;
    Error *local_err = NULL;

    visit_type_str(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    prop->set(dev, value, errp);
    g_free(value);
}

static void qdev_property_release_str(DeviceState *dev, const char *name,
                                      void *opaque)
{
    StringProperty *prop = opaque;
    g_free(prop);
}

void qdev_property_add_str(DeviceState *dev, const char *name,
                           char *(*get)(DeviceState *, Error **),
                           void (*set)(DeviceState *, const char *, Error **),
                           Error **errp)
{
    StringProperty *prop = g_malloc0(sizeof(*prop));

    prop->get = get;
    prop->set = set;

    qdev_property_add(dev, name, "string",
                      get ? qdev_property_get_str : NULL,
                      set ? qdev_property_set_str : NULL,
                      qdev_property_release_str,
                      prop, errp);
}

void qdev_machine_init(void)
{
    qdev_get_peripheral_anon();
    qdev_get_peripheral();
}

void device_reset(DeviceState *dev)
{
    DeviceClass *klass = DEVICE_GET_CLASS(dev);

    if (klass->reset) {
        klass->reset(dev);
    }
}

static TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DeviceState),
    .abstract = true,
    .class_size = sizeof(DeviceClass),
};

static void init_qdev(void)
{
    type_register_static(&device_type_info);
}

device_init(init_qdev);
