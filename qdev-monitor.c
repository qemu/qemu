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

#include "hw/qdev.h"
#include "hw/sysbus.h"
#include "monitor/monitor.h"
#include "monitor/qdev.h"
#include "qmp-commands.h"
#include "sysemu/arch_init.h"
#include "qemu/config-file.h"

/*
 * Aliases were a bad idea from the start.  Let's keep them
 * from spreading further.
 */
typedef struct QDevAlias
{
    const char *typename;
    const char *alias;
    uint32_t arch_mask;
} QDevAlias;

static const QDevAlias qdev_alias_table[] = {
    { "virtio-blk-pci", "virtio-blk", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-net-pci", "virtio-net", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-serial-pci", "virtio-serial", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-balloon-pci", "virtio-balloon",
            QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-blk-s390", "virtio-blk", QEMU_ARCH_S390X },
    { "virtio-net-s390", "virtio-net", QEMU_ARCH_S390X },
    { "virtio-serial-s390", "virtio-serial", QEMU_ARCH_S390X },
    { "lsi53c895a", "lsi" },
    { "ich9-ahci", "ahci" },
    { "kvm-pci-assign", "pci-assign" },
    { }
};

static const char *qdev_class_get_alias(DeviceClass *dc)
{
    const char *typename = object_class_get_name(OBJECT_CLASS(dc));
    int i;

    for (i = 0; qdev_alias_table[i].typename; i++) {
        if (qdev_alias_table[i].arch_mask &&
            !(qdev_alias_table[i].arch_mask & arch_type)) {
            continue;
        }

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

static void qdev_print_devinfo(DeviceClass *dc)
{
    error_printf("name \"%s\"", object_class_get_name(OBJECT_CLASS(dc)));
    if (dc->bus_type) {
        error_printf(", bus %s", dc->bus_type);
    }
    if (qdev_class_has_alias(dc)) {
        error_printf(", alias \"%s\"", qdev_class_get_alias(dc));
    }
    if (dc->desc) {
        error_printf(", desc \"%s\"", dc->desc);
    }
    if (dc->cannot_instantiate_with_device_add_yet) {
        error_printf(", no-user");
    }
    error_printf("\n");
}

static gint devinfo_cmp(gconstpointer a, gconstpointer b)
{
    return strcasecmp(object_class_get_name((ObjectClass *)a),
                      object_class_get_name((ObjectClass *)b));
}

static void qdev_print_devinfos(bool show_no_user)
{
    static const char *cat_name[DEVICE_CATEGORY_MAX + 1] = {
        [DEVICE_CATEGORY_BRIDGE]  = "Controller/Bridge/Hub",
        [DEVICE_CATEGORY_USB]     = "USB",
        [DEVICE_CATEGORY_STORAGE] = "Storage",
        [DEVICE_CATEGORY_NETWORK] = "Network",
        [DEVICE_CATEGORY_INPUT]   = "Input",
        [DEVICE_CATEGORY_DISPLAY] = "Display",
        [DEVICE_CATEGORY_SOUND]   = "Sound",
        [DEVICE_CATEGORY_MISC]    = "Misc",
        [DEVICE_CATEGORY_MAX]     = "Uncategorized",
    };
    GSList *list, *elt;
    int i;
    bool cat_printed;

    list = g_slist_sort(object_class_get_list(TYPE_DEVICE, false),
                        devinfo_cmp);

    for (i = 0; i <= DEVICE_CATEGORY_MAX; i++) {
        cat_printed = false;
        for (elt = list; elt; elt = elt->next) {
            DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data,
                                                 TYPE_DEVICE);
            if ((i < DEVICE_CATEGORY_MAX
                 ? !test_bit(i, dc->categories)
                 : !bitmap_empty(dc->categories, DEVICE_CATEGORY_MAX))
                || (!show_no_user
                    && dc->cannot_instantiate_with_device_add_yet)) {
                continue;
            }
            if (!cat_printed) {
                error_printf("%s%s devices:\n", i ? "\n" : "",
                             cat_name[i]);
                cat_printed = true;
            }
            qdev_print_devinfo(dc);
        }
    }

    g_slist_free(list);
}

static int set_property(const char *name, const char *value, void *opaque)
{
    Object *obj = opaque;
    Error *err = NULL;

    if (strcmp(name, "driver") == 0)
        return 0;
    if (strcmp(name, "bus") == 0)
        return 0;

    object_property_parse(obj, value, name, &err);
    if (err != NULL) {
        qerror_report_err(err);
        error_free(err);
        return -1;
    }
    return 0;
}

static const char *find_typename_by_alias(const char *alias)
{
    int i;

    for (i = 0; qdev_alias_table[i].alias; i++) {
        if (qdev_alias_table[i].arch_mask &&
            !(qdev_alias_table[i].arch_mask & arch_type)) {
            continue;
        }

        if (strcmp(qdev_alias_table[i].alias, alias) == 0) {
            return qdev_alias_table[i].typename;
        }
    }

    return NULL;
}

int qdev_device_help(QemuOpts *opts)
{
    Error *local_err = NULL;
    const char *driver;
    DevicePropertyInfoList *prop_list;
    DevicePropertyInfoList *prop;

    driver = qemu_opt_get(opts, "driver");
    if (driver && is_help_option(driver)) {
        qdev_print_devinfos(false);
        return 1;
    }

    if (!driver || !qemu_opt_has_help_opt(opts)) {
        return 0;
    }

    if (!object_class_by_name(driver)) {
        const char *typename = find_typename_by_alias(driver);

        if (typename) {
            driver = typename;
        }
    }

    prop_list = qmp_device_list_properties(driver, &local_err);
    if (!prop_list) {
        error_printf("%s\n", error_get_pretty(local_err));
        error_free(local_err);
        return 1;
    }

    for (prop = prop_list; prop; prop = prop->next) {
        error_printf("%s.%s=%s\n", driver,
                     prop->value->name,
                     prop->value->type);
    }

    qapi_free_DevicePropertyInfoList(prop_list);
    return 1;
}

static Object *qdev_get_peripheral(void)
{
    static Object *dev;

    if (dev == NULL) {
        dev = container_get(qdev_get_machine(), "/peripheral");
    }

    return dev;
}

static Object *qdev_get_peripheral_anon(void)
{
    static Object *dev;

    if (dev == NULL) {
        dev = container_get(qdev_get_machine(), "/peripheral-anon");
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
    BusChild *kid;
    const char *sep = " ";

    error_printf("devices at \"%s\":", bus->name);
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
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
    BusChild *kid;

    /*
     * try to match in order:
     *   (1) instance id, if present
     *   (2) driver name
     *   (3) driver alias, if present
     */
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        if (dev->id  &&  strcmp(dev->id, elem) == 0) {
            return dev;
        }
    }
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        if (strcmp(object_get_typename(OBJECT(dev)), elem) == 0) {
            return dev;
        }
    }
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        DeviceClass *dc = DEVICE_GET_CLASS(dev);

        if (qdev_class_has_alias(dc) &&
            strcmp(qdev_class_get_alias(dc), elem) == 0) {
            return dev;
        }
    }
    return NULL;
}

static BusState *qbus_find_recursive(BusState *bus, const char *name,
                                     const char *bus_typename)
{
    BusClass *bus_class = BUS_GET_CLASS(bus);
    BusChild *kid;
    BusState *child, *ret;
    int match = 1;

    if (name && (strcmp(bus->name, name) != 0)) {
        match = 0;
    } else if (bus_typename && !object_dynamic_cast(OBJECT(bus), bus_typename)) {
        match = 0;
    } else if ((bus_class->max_dev != 0) && (bus_class->max_dev <= bus->max_index)) {
        if (name != NULL) {
            /* bus was explicitly specified: return an error. */
            qerror_report(ERROR_CLASS_GENERIC_ERROR, "Bus '%s' is full",
                          bus->name);
            return NULL;
        } else {
            /* bus was not specified: try to find another one. */
            match = 0;
        }
    }
    if (match) {
        return bus;
    }

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qbus_find_recursive(child, name, bus_typename);
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
            g_assert_not_reached();
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
                qerror_report(ERROR_CLASS_GENERIC_ERROR,
                              "Device '%s' has no child bus", elem);
                return NULL;
            case 1:
                return QLIST_FIRST(&dev->child_bus);
            default:
                qerror_report(ERROR_CLASS_GENERIC_ERROR,
                              "Device '%s' has multiple child busses", elem);
                if (!monitor_cur_is_qmp()) {
                    qbus_list_bus(dev);
                }
                return NULL;
            }
        }

        /* find bus */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            g_assert_not_reached();
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
    ObjectClass *oc;
    DeviceClass *dc;
    const char *driver, *path, *id;
    DeviceState *dev;
    BusState *bus = NULL;
    Error *err = NULL;

    driver = qemu_opt_get(opts, "driver");
    if (!driver) {
        qerror_report(QERR_MISSING_PARAMETER, "driver");
        return NULL;
    }

    /* find driver */
    oc = object_class_by_name(driver);
    if (!oc) {
        const char *typename = find_typename_by_alias(driver);

        if (typename) {
            driver = typename;
            oc = object_class_by_name(driver);
        }
    }

    if (!object_class_dynamic_cast(oc, TYPE_DEVICE)) {
        qerror_report(ERROR_CLASS_GENERIC_ERROR,
                      "'%s' is not a valid device model name", driver);
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "driver",
                      "non-abstract device type");
        return NULL;
    }

    dc = DEVICE_CLASS(oc);
    if (dc->cannot_instantiate_with_device_add_yet) {
        qerror_report(QERR_INVALID_PARAMETER_VALUE, "driver",
                      "pluggable device type");
        return NULL;
    }

    /* find bus */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
        bus = qbus_find(path);
        if (!bus) {
            return NULL;
        }
        if (!object_dynamic_cast(OBJECT(bus), dc->bus_type)) {
            qerror_report(ERROR_CLASS_GENERIC_ERROR,
                          "Device '%s' can't go on a %s bus",
                          driver, object_get_typename(OBJECT(bus)));
            return NULL;
        }
    } else if (dc->bus_type != NULL) {
        bus = qbus_find_recursive(sysbus_get_default(), NULL, dc->bus_type);
        if (!bus) {
            qerror_report(ERROR_CLASS_GENERIC_ERROR,
                          "No '%s' bus found for device '%s'",
                          dc->bus_type, driver);
            return NULL;
        }
    }
    if (qdev_hotplug && bus && !bus->allow_hotplug) {
        qerror_report(QERR_BUS_NO_HOTPLUG, bus->name);
        return NULL;
    }

    /* create device */
    dev = DEVICE(object_new(driver));

    if (bus) {
        qdev_set_parent_bus(dev, bus);
    }

    id = qemu_opts_id(opts);
    if (id) {
        dev->id = id;
    }

    if (dev->id) {
        object_property_add_child(qdev_get_peripheral(), dev->id,
                                  OBJECT(dev), NULL);
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        object_property_add_child(qdev_get_peripheral_anon(), name,
                                  OBJECT(dev), NULL);
        g_free(name);
    }

    /* set properties */
    if (qemu_opt_foreach(opts, set_property, dev, 1) != 0) {
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }

    dev->opts = opts;
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
    if (err != NULL) {
        qerror_report_err(err);
        error_free(err);
        dev->opts = NULL;
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        qerror_report(QERR_DEVICE_INIT_FAILED, driver);
        return NULL;
    }
    return dev;
}


#define qdev_printf(fmt, ...) monitor_printf(mon, "%*s" fmt, indent, "", ## __VA_ARGS__)
static void qbus_print(Monitor *mon, BusState *bus, int indent);

static void qdev_print_props(Monitor *mon, DeviceState *dev, Property *props,
                             int indent)
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
            value = object_property_print(OBJECT(dev), props->name, true, &err);
        }
        g_free(legacy_name);

        if (err) {
            error_free(err);
            continue;
        }
        qdev_printf("%s = %s\n", props->name,
                    value && *value ? value : "<null>");
        g_free(value);
    }
}

static void bus_print_dev(BusState *bus, Monitor *mon, DeviceState *dev, int indent)
{
    BusClass *bc = BUS_GET_CLASS(bus);

    if (bc->print_dev) {
        bc->print_dev(mon, dev, indent);
    }
}

static void qdev_print(Monitor *mon, DeviceState *dev, int indent)
{
    ObjectClass *class;
    BusState *child;
    NamedGPIOList *ngl;

    qdev_printf("dev: %s, id \"%s\"\n", object_get_typename(OBJECT(dev)),
                dev->id ? dev->id : "");
    indent += 2;
    QLIST_FOREACH(ngl, &dev->gpios, node) {
        if (ngl->num_in) {
            qdev_printf("gpio-in \"%s\" %d\n", ngl->name ? ngl->name : "",
                        ngl->num_in);
        }
        if (ngl->num_out) {
            qdev_printf("gpio-out \"%s\" %d\n", ngl->name ? ngl->name : "",
                        ngl->num_out);
        }
    }
    class = object_get_class(OBJECT(dev));
    do {
        qdev_print_props(mon, dev, DEVICE_CLASS(class)->props, indent);
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));
    bus_print_dev(dev->parent_bus, mon, dev, indent);
    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        qbus_print(mon, child, indent);
    }
}

static void qbus_print(Monitor *mon, BusState *bus, int indent)
{
    BusChild *kid;

    qdev_printf("bus: %s\n", bus->name);
    indent += 2;
    qdev_printf("type %s\n", object_get_typename(OBJECT(bus)));
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        qdev_print(mon, dev, indent);
    }
}
#undef qdev_printf

void do_info_qtree(Monitor *mon, const QDict *qdict)
{
    if (sysbus_get_default())
        qbus_print(mon, sysbus_get_default(), 0);
}

void do_info_qdm(Monitor *mon, const QDict *qdict)
{
    qdev_print_devinfos(true);
}

int do_device_add(Monitor *mon, const QDict *qdict, QObject **ret_data)
{
    Error *local_err = NULL;
    QemuOpts *opts;
    DeviceState *dev;

    opts = qemu_opts_from_qdict(qemu_find_opts("device"), qdict, &local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }
    if (!monitor_cur_is_qmp() && qdev_device_help(opts)) {
        qemu_opts_del(opts);
        return 0;
    }
    dev = qdev_device_add(opts);
    if (!dev) {
        qemu_opts_del(opts);
        return -1;
    }
    object_unref(OBJECT(dev));
    return 0;
}

void qmp_device_del(const char *id, Error **errp)
{
    DeviceState *dev;

    dev = qdev_find_recursive(sysbus_get_default(), id);
    if (!dev) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, id);
        return;
    }

    qdev_unplug(dev, errp);
}

void qdev_machine_init(void)
{
    qdev_get_peripheral_anon();
    qdev_get_peripheral();
}

QemuOptsList qemu_device_opts = {
    .name = "device",
    .implied_opt_name = "driver",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_device_opts.head),
    .desc = {
        /*
         * no elements => accept any
         * sanity checking will happen later
         * when setting device properties
         */
        { /* end of list */ }
    },
};

QemuOptsList qemu_global_opts = {
    .name = "global",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_global_opts.head),
    .desc = {
        {
            .name = "driver",
            .type = QEMU_OPT_STRING,
        },{
            .name = "property",
            .type = QEMU_OPT_STRING,
        },{
            .name = "value",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

int qemu_global_option(const char *str)
{
    char driver[64], property[64];
    QemuOpts *opts;
    int rc, offset;

    rc = sscanf(str, "%63[^.].%63[^=]%n", driver, property, &offset);
    if (rc < 2 || str[offset] != '=') {
        error_report("can't parse: \"%s\"", str);
        return -1;
    }

    opts = qemu_opts_create(&qemu_global_opts, NULL, 0, &error_abort);
    qemu_opt_set(opts, "driver", driver);
    qemu_opt_set(opts, "property", property);
    qemu_opt_set(opts, "value", str+offset+1);
    return 0;
}
