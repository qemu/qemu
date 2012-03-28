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

#include "qdev.h"
#include "monitor.h"

/*
 * Aliases were a bad idea from the start.  Let's keep them
 * from spreading further.
 */
typedef struct QDevAlias
{
    const char *typename;
    const char *alias;
} QDevAlias;

static const QDevAlias qdev_alias_table[] = {
    { "virtio-blk-pci", "virtio-blk" },
    { "virtio-net-pci", "virtio-net" },
    { "virtio-serial-pci", "virtio-serial" },
    { "virtio-balloon-pci", "virtio-balloon" },
    { "virtio-blk-s390", "virtio-blk" },
    { "virtio-net-s390", "virtio-net" },
    { "virtio-serial-s390", "virtio-serial" },
    { "lsi53c895a", "lsi" },
    { "ich9-ahci", "ahci" },
    { }
};

static const char *qdev_class_get_alias(DeviceClass *dc)
{
    const char *typename = object_class_get_name(OBJECT_CLASS(dc));
    int i;

    for (i = 0; qdev_alias_table[i].typename; i++) {
        if (strcmp(qdev_alias_table[i].typename, typename) == 0) {
            return qdev_alias_table[i].alias;
        }
    }

    return NULL;
}

static bool qdev_class_has_alias(DeviceClass *dc)
{
    return (qdev_class_get_alias(dc) != NULL);
}

static void qdev_print_devinfo(ObjectClass *klass, void *opaque)
{
    DeviceClass *dc;
    bool *show_no_user = opaque;

    dc = (DeviceClass *)object_class_dynamic_cast(klass, TYPE_DEVICE);

    if (!dc || (show_no_user && !*show_no_user && dc->no_user)) {
        return;
    }

    error_printf("name \"%s\"", object_class_get_name(klass));
    if (dc->bus_info) {
        error_printf(", bus %s", dc->bus_info->name);
    }
    if (qdev_class_has_alias(dc)) {
        error_printf(", alias \"%s\"", qdev_class_get_alias(dc));
    }
    if (dc->desc) {
        error_printf(", desc \"%s\"", dc->desc);
    }
    if (dc->no_user) {
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

static const char *find_typename_by_alias(const char *alias)
{
    int i;

    for (i = 0; qdev_alias_table[i].alias; i++) {
        if (strcmp(qdev_alias_table[i].alias, alias) == 0) {
            return qdev_alias_table[i].typename;
        }
    }

    return NULL;
}

int qdev_device_help(QemuOpts *opts)
{
    const char *driver;
    Property *prop;
    ObjectClass *klass;
    DeviceClass *info;

    driver = qemu_opt_get(opts, "driver");
    if (driver && !strcmp(driver, "?")) {
        bool show_no_user = false;
        object_class_foreach(qdev_print_devinfo, TYPE_DEVICE, false, &show_no_user);
        return 1;
    }

    if (!driver || !qemu_opt_get(opts, "?")) {
        return 0;
    }

    klass = object_class_by_name(driver);
    if (!klass) {
        const char *typename = find_typename_by_alias(driver);

        if (typename) {
            driver = typename;
            klass = object_class_by_name(driver);
        }
    }

    if (!klass) {
        return 0;
    }
    info = DEVICE_CLASS(klass);

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
        error_printf("%s.%s=%s\n", driver, prop->name,
                     prop->info->legacy_name ?: prop->info->name);
    }
    if (info->bus_info) {
        for (prop = info->bus_info->props; prop && prop->name; prop++) {
            if (!prop->info->parse) {
                continue;           /* no way to set it, don't show */
            }
            error_printf("%s.%s=%s\n", driver, prop->name,
                         prop->info->legacy_name ?: prop->info->name);
        }
    }
    return 1;
}

static Object *qdev_get_peripheral(void)
{
    static Object *dev;

    if (dev == NULL) {
        dev = container_get("/machine/peripheral");
    }

    return dev;
}

static Object *qdev_get_peripheral_anon(void)
{
    static Object *dev;

    if (dev == NULL) {
        dev = container_get("/machine/peripheral-anon");
    }

    return dev;
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
        DeviceClass *dc = DEVICE_GET_CLASS(dev);

        if (qdev_class_has_alias(dc) &&
            strcmp(qdev_class_get_alias(dc), elem) == 0) {
            return dev;
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

static BusState *qbus_find(const char *path)
{
    DeviceState *dev;
    BusState *bus;
    char elem[128];
    int pos, len;

    /* find start element */
    if (path[0] == '/') {
        bus = sysbus_get_default();
        pos = 0;
    } else {
        if (sscanf(path, "%127[^/]%n", elem, &len) != 1) {
            assert(!path[0]);
            elem[0] = len = 0;
        }
        bus = qbus_find_recursive(sysbus_get_default(), elem, NULL);
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

DeviceState *qdev_device_add(QemuOpts *opts)
{
    ObjectClass *obj;
    DeviceClass *k;
    const char *driver, *path, *id;
    DeviceState *qdev;
    BusState *bus;

    driver = qemu_opt_get(opts, "driver");
    if (!driver) {
        qerror_report(QERR_MISSING_PARAMETER, "driver");
        return NULL;
    }

    /* find driver */
    obj = object_class_by_name(driver);
    if (!obj) {
        const char *typename = find_typename_by_alias(driver);

        if (typename) {
            driver = typename;
            obj = object_class_by_name(driver);
        }
    }

    if (!obj) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "driver", "device type");
        return NULL;
    }

    k = DEVICE_CLASS(obj);

    /* find bus */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
        bus = qbus_find(path);
        if (!bus) {
            return NULL;
        }
        if (bus->info != k->bus_info) {
            qerror_report(QERR_BAD_BUS_FOR_DEVICE,
                           driver, bus->info->name);
            return NULL;
        }
    } else {
        bus = qbus_find_recursive(sysbus_get_default(), NULL, k->bus_info);
        if (!bus) {
            qerror_report(QERR_NO_BUS_FOR_DEVICE,
                          driver, k->bus_info->name);
            return NULL;
        }
    }
    if (qdev_hotplug && !bus->allow_hotplug) {
        qerror_report(QERR_BUS_NO_HOTPLUG, bus->name);
        return NULL;
    }

    if (!bus) {
        bus = sysbus_get_default();
    }

    /* create device, set properties */
    qdev = DEVICE(object_new(driver));
    qdev_set_parent_bus(qdev, bus);
    qdev_prop_set_globals(qdev);

    id = qemu_opts_id(opts);
    if (id) {
        qdev->id = id;
    }
    if (qemu_opt_foreach(opts, set_property, qdev, 1) != 0) {
        qdev_free(qdev);
        return NULL;
    }
    if (qdev->id) {
        object_property_add_child(qdev_get_peripheral(), qdev->id,
                                  OBJECT(qdev), NULL);
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        object_property_add_child(qdev_get_peripheral_anon(), name,
                                  OBJECT(qdev), NULL);
        g_free(name);
    }        
    if (qdev_init(qdev) < 0) {
        qerror_report(QERR_DEVICE_INIT_FAILED, driver);
        return NULL;
    }
    qdev->opts = opts;
    return qdev;
}


#define qdev_printf(fmt, ...) monitor_printf(mon, "%*s" fmt, indent, "", ## __VA_ARGS__)
static void qbus_print(Monitor *mon, BusState *bus, int indent);

static void qdev_print_props(Monitor *mon, DeviceState *dev, Property *props,
                             const char *prefix, int indent)
{
    if (!props)
        return;
    for (; props->name; props++) {
        Error *err = NULL;
        char *value;
        char *legacy_name = g_strdup_printf("legacy-%s", props->name);
        if (object_property_get_type(OBJECT(dev), legacy_name, NULL)) {
            value = object_property_get_str(OBJECT(dev), legacy_name, &err);
        } else {
            value = object_property_get_str(OBJECT(dev), props->name, &err);
        }
        g_free(legacy_name);

        if (err) {
            error_free(err);
            continue;
        }
        qdev_printf("%s-prop: %s = %s\n", prefix, props->name,
                    value && *value ? value : "<null>");
        g_free(value);
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
    qdev_print_props(mon, dev, qdev_get_props(dev), "dev", indent);
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
    if (sysbus_get_default())
        qbus_print(mon, sysbus_get_default(), 0);
}

void do_info_qdm(Monitor *mon)
{
    object_class_foreach(qdev_print_devinfo, TYPE_DEVICE, false, NULL);
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

    dev = qdev_find_recursive(sysbus_get_default(), id);
    if (NULL == dev) {
        qerror_report(QERR_DEVICE_NOT_FOUND, id);
        return -1;
    }
    return qdev_unplug(dev);
}

void qdev_machine_init(void)
{
    qdev_get_peripheral_anon();
    qdev_get_peripheral();
}
