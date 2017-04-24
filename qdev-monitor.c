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

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "hw/sysbus.h"
#include "monitor/monitor.h"
#include "monitor/qdev.h"
#include "qmp-commands.h"
#include "sysemu/arch_init.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"
#include "sysemu/block-backend.h"
#include "migration/misc.h"

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

/* Please keep this table sorted by typename. */
static const QDevAlias qdev_alias_table[] = {
    { "e1000", "e1000-82540em" },
    { "ich9-ahci", "ahci" },
    { "kvm-pci-assign", "pci-assign" },
    { "lsi53c895a", "lsi" },
    { "virtio-9p-ccw", "virtio-9p", QEMU_ARCH_S390X },
    { "virtio-9p-pci", "virtio-9p", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-balloon-ccw", "virtio-balloon", QEMU_ARCH_S390X },
    { "virtio-balloon-pci", "virtio-balloon",
            QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-blk-ccw", "virtio-blk", QEMU_ARCH_S390X },
    { "virtio-blk-pci", "virtio-blk", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-gpu-ccw", "virtio-gpu", QEMU_ARCH_S390X },
    { "virtio-gpu-pci", "virtio-gpu", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-input-host-ccw", "virtio-input-host", QEMU_ARCH_S390X },
    { "virtio-input-host-pci", "virtio-input-host",
            QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-keyboard-ccw", "virtio-keyboard", QEMU_ARCH_S390X },
    { "virtio-keyboard-pci", "virtio-keyboard",
            QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-mouse-ccw", "virtio-mouse", QEMU_ARCH_S390X },
    { "virtio-mouse-pci", "virtio-mouse", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-net-ccw", "virtio-net", QEMU_ARCH_S390X },
    { "virtio-net-pci", "virtio-net", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-rng-ccw", "virtio-rng", QEMU_ARCH_S390X },
    { "virtio-rng-pci", "virtio-rng", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-scsi-ccw", "virtio-scsi", QEMU_ARCH_S390X },
    { "virtio-scsi-pci", "virtio-scsi", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-serial-ccw", "virtio-serial", QEMU_ARCH_S390X },
    { "virtio-serial-pci", "virtio-serial", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
    { "virtio-tablet-ccw", "virtio-tablet", QEMU_ARCH_S390X },
    { "virtio-tablet-pci", "virtio-tablet", QEMU_ARCH_ALL & ~QEMU_ARCH_S390X },
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
    if (!dc->user_creatable) {
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
        [DEVICE_CATEGORY_CPU]     = "CPU",
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
                    && !dc->user_creatable)) {
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

static int set_property(void *opaque, const char *name, const char *value,
                        Error **errp)
{
    Object *obj = opaque;
    Error *err = NULL;

    if (strcmp(name, "driver") == 0)
        return 0;
    if (strcmp(name, "bus") == 0)
        return 0;

    object_property_parse(obj, value, name, &err);
    if (err != NULL) {
        error_propagate(errp, err);
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

static DeviceClass *qdev_get_device_class(const char **driver, Error **errp)
{
    ObjectClass *oc;
    DeviceClass *dc;
    const char *original_name = *driver;

    oc = object_class_by_name(*driver);
    if (!oc) {
        const char *typename = find_typename_by_alias(*driver);

        if (typename) {
            *driver = typename;
            oc = object_class_by_name(*driver);
        }
    }

    if (!object_class_dynamic_cast(oc, TYPE_DEVICE)) {
        if (*driver != original_name) {
            error_setg(errp, "'%s' (alias '%s') is not a valid device model"
                       " name", original_name, *driver);
        } else {
            error_setg(errp, "'%s' is not a valid device model name", *driver);
        }
        return NULL;
    }

    if (object_class_is_abstract(oc)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "non-abstract device type");
        return NULL;
    }

    dc = DEVICE_CLASS(oc);
    if (!dc->user_creatable ||
        (qdev_hotplug && !dc->hotpluggable)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "pluggable device type");
        return NULL;
    }

    return dc;
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
    if (local_err) {
        goto error;
    }

    for (prop = prop_list; prop; prop = prop->next) {
        error_printf("%s.%s=%s", driver,
                     prop->value->name,
                     prop->value->type);
        if (prop->value->has_description) {
            error_printf(" (%s)\n", prop->value->description);
        } else {
            error_printf("\n");
        }
    }

    qapi_free_DevicePropertyInfoList(prop_list);
    return 1;

error:
    error_report_err(local_err);
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

static void qbus_list_bus(DeviceState *dev, Error **errp)
{
    BusState *child;
    const char *sep = " ";

    error_append_hint(errp, "child buses at \"%s\":",
                      dev->id ? dev->id : object_get_typename(OBJECT(dev)));
    QLIST_FOREACH(child, &dev->child_bus, sibling) {
        error_append_hint(errp, "%s\"%s\"", sep, child->name);
        sep = ", ";
    }
    error_append_hint(errp, "\n");
}

static void qbus_list_dev(BusState *bus, Error **errp)
{
    BusChild *kid;
    const char *sep = " ";

    error_append_hint(errp, "devices at \"%s\":", bus->name);
    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        error_append_hint(errp, "%s\"%s\"", sep,
                          object_get_typename(OBJECT(dev)));
        if (dev->id) {
            error_append_hint(errp, "/\"%s\"", dev->id);
        }
        sep = ", ";
    }
    error_append_hint(errp, "\n");
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

static inline bool qbus_is_full(BusState *bus)
{
    BusClass *bus_class = BUS_GET_CLASS(bus);
    return bus_class->max_dev && bus->max_index >= bus_class->max_dev;
}

/*
 * Search the tree rooted at @bus for a bus.
 * If @name, search for a bus with that name.  Note that bus names
 * need not be unique.  Yes, that's screwed up.
 * Else search for a bus that is a subtype of @bus_typename.
 * If more than one exists, prefer one that can take another device.
 * Return the bus if found, else %NULL.
 */
static BusState *qbus_find_recursive(BusState *bus, const char *name,
                                     const char *bus_typename)
{
    BusChild *kid;
    BusState *pick, *child, *ret;
    bool match;

    assert(name || bus_typename);
    if (name) {
        match = !strcmp(bus->name, name);
    } else {
        match = !!object_dynamic_cast(OBJECT(bus), bus_typename);
    }

    if (match && !qbus_is_full(bus)) {
        return bus;             /* root matches and isn't full */
    }

    pick = match ? bus : NULL;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;
        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qbus_find_recursive(child, name, bus_typename);
            if (ret && !qbus_is_full(ret)) {
                return ret;     /* a descendant matches and isn't full */
            }
            if (ret && !pick) {
                pick = ret;
            }
        }
    }

    /* root or a descendant matches, but is full */
    return pick;
}

static BusState *qbus_find(const char *path, Error **errp)
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
            error_setg(errp, "Bus '%s' not found", elem);
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
            break;
        }

        /* find device */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            g_assert_not_reached();
            elem[0] = len = 0;
        }
        pos += len;
        dev = qbus_find_dev(bus, elem);
        if (!dev) {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", elem);
            qbus_list_dev(bus, errp);
            return NULL;
        }

        assert(path[pos] == '/' || !path[pos]);
        while (path[pos] == '/') {
            pos++;
        }
        if (path[pos] == '\0') {
            /* last specified element is a device.  If it has exactly
             * one child bus accept it nevertheless */
            if (dev->num_child_bus == 1) {
                bus = QLIST_FIRST(&dev->child_bus);
                break;
            }
            if (dev->num_child_bus) {
                error_setg(errp, "Device '%s' has multiple child buses",
                           elem);
                qbus_list_bus(dev, errp);
            } else {
                error_setg(errp, "Device '%s' has no child bus", elem);
            }
            return NULL;
        }

        /* find bus */
        if (sscanf(path+pos, "%127[^/]%n", elem, &len) != 1) {
            g_assert_not_reached();
            elem[0] = len = 0;
        }
        pos += len;
        bus = qbus_find_bus(dev, elem);
        if (!bus) {
            error_setg(errp, "Bus '%s' not found", elem);
            qbus_list_bus(dev, errp);
            return NULL;
        }
    }

    if (qbus_is_full(bus)) {
        error_setg(errp, "Bus '%s' is full", path);
        return NULL;
    }
    return bus;
}

void qdev_set_id(DeviceState *dev, const char *id)
{
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
}

DeviceState *qdev_device_add(QemuOpts *opts, Error **errp)
{
    DeviceClass *dc;
    const char *driver, *path;
    DeviceState *dev;
    BusState *bus = NULL;
    Error *err = NULL;

    driver = qemu_opt_get(opts, "driver");
    if (!driver) {
        error_setg(errp, QERR_MISSING_PARAMETER, "driver");
        return NULL;
    }

    /* find driver */
    dc = qdev_get_device_class(&driver, errp);
    if (!dc) {
        return NULL;
    }

    /* find bus */
    path = qemu_opt_get(opts, "bus");
    if (path != NULL) {
        bus = qbus_find(path, errp);
        if (!bus) {
            return NULL;
        }
        if (!object_dynamic_cast(OBJECT(bus), dc->bus_type)) {
            error_setg(errp, "Device '%s' can't go on %s bus",
                       driver, object_get_typename(OBJECT(bus)));
            return NULL;
        }
    } else if (dc->bus_type != NULL) {
        bus = qbus_find_recursive(sysbus_get_default(), NULL, dc->bus_type);
        if (!bus || qbus_is_full(bus)) {
            error_setg(errp, "No '%s' bus found for device '%s'",
                       dc->bus_type, driver);
            return NULL;
        }
    }
    if (qdev_hotplug && bus && !qbus_is_hotpluggable(bus)) {
        error_setg(errp, QERR_BUS_NO_HOTPLUG, bus->name);
        return NULL;
    }

    if (!migration_is_idle()) {
        error_setg(errp, "device_add not allowed while migrating");
        return NULL;
    }

    /* create device */
    dev = DEVICE(object_new(driver));

    if (bus) {
        qdev_set_parent_bus(dev, bus);
    }

    qdev_set_id(dev, qemu_opts_id(opts));

    /* set properties */
    if (qemu_opt_foreach(opts, set_property, dev, &err)) {
        error_propagate(errp, err);
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
        return NULL;
    }

    dev->opts = opts;
    object_property_set_bool(OBJECT(dev), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        dev->opts = NULL;
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
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

void hmp_info_qtree(Monitor *mon, const QDict *qdict)
{
    if (sysbus_get_default())
        qbus_print(mon, sysbus_get_default(), 0);
}

void hmp_info_qdm(Monitor *mon, const QDict *qdict)
{
    qdev_print_devinfos(true);
}

typedef struct QOMCompositionState {
    Monitor *mon;
    int indent;
} QOMCompositionState;

static void print_qom_composition(Monitor *mon, Object *obj, int indent);

static int print_qom_composition_child(Object *obj, void *opaque)
{
    QOMCompositionState *s = opaque;

    print_qom_composition(s->mon, obj, s->indent);

    return 0;
}

static void print_qom_composition(Monitor *mon, Object *obj, int indent)
{
    QOMCompositionState s = {
        .mon = mon,
        .indent = indent + 2,
    };
    char *name;

    if (obj == object_get_root()) {
        name = g_strdup("");
    } else {
        name = object_get_canonical_path_component(obj);
    }
    monitor_printf(mon, "%*s/%s (%s)\n", indent, "", name,
                   object_get_typename(obj));
    g_free(name);
    object_child_foreach(obj, print_qom_composition_child, &s);
}

void hmp_info_qom_tree(Monitor *mon, const QDict *dict)
{
    const char *path = qdict_get_try_str(dict, "path");
    Object *obj;
    bool ambiguous = false;

    if (path) {
        obj = object_resolve_path(path, &ambiguous);
        if (!obj) {
            monitor_printf(mon, "Path '%s' could not be resolved.\n", path);
            return;
        }
        if (ambiguous) {
            monitor_printf(mon, "Warning: Path '%s' is ambiguous.\n", path);
            return;
        }
    } else {
        obj = qdev_get_machine();
    }
    print_qom_composition(mon, obj, 0);
}

void qmp_device_add(QDict *qdict, QObject **ret_data, Error **errp)
{
    Error *local_err = NULL;
    QemuOpts *opts;
    DeviceState *dev;

    opts = qemu_opts_from_qdict(qemu_find_opts("device"), qdict, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (!monitor_cur_is_qmp() && qdev_device_help(opts)) {
        qemu_opts_del(opts);
        return;
    }
    dev = qdev_device_add(opts, &local_err);
    if (!dev) {
        error_propagate(errp, local_err);
        qemu_opts_del(opts);
        return;
    }
    object_unref(OBJECT(dev));
}

static DeviceState *find_device_state(const char *id, Error **errp)
{
    Object *obj;

    if (id[0] == '/') {
        obj = object_resolve_path(id, NULL);
    } else {
        char *root_path = object_get_canonical_path(qdev_get_peripheral());
        char *path = g_strdup_printf("%s/%s", root_path, id);

        g_free(root_path);
        obj = object_resolve_path_type(path, TYPE_DEVICE, NULL);
        g_free(path);
    }

    if (!obj) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", id);
        return NULL;
    }

    if (!object_dynamic_cast(obj, TYPE_DEVICE)) {
        error_setg(errp, "%s is not a hotpluggable device", id);
        return NULL;
    }

    return DEVICE(obj);
}

void qdev_unplug(DeviceState *dev, Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    HotplugHandler *hotplug_ctrl;
    HotplugHandlerClass *hdc;

    if (dev->parent_bus && !qbus_is_hotpluggable(dev->parent_bus)) {
        error_setg(errp, QERR_BUS_NO_HOTPLUG, dev->parent_bus->name);
        return;
    }

    if (!dc->hotpluggable) {
        error_setg(errp, QERR_DEVICE_NO_HOTPLUG,
                   object_get_typename(OBJECT(dev)));
        return;
    }

    if (!migration_is_idle()) {
        error_setg(errp, "device_del not allowed while migrating");
        return;
    }

    qdev_hot_removed = true;

    hotplug_ctrl = qdev_get_hotplug_handler(dev);
    /* hotpluggable device MUST have HotplugHandler, if it doesn't
     * then something is very wrong with it */
    g_assert(hotplug_ctrl);

    /* If device supports async unplug just request it to be done,
     * otherwise just remove it synchronously */
    hdc = HOTPLUG_HANDLER_GET_CLASS(hotplug_ctrl);
    if (hdc->unplug_request) {
        hotplug_handler_unplug_request(hotplug_ctrl, dev, errp);
    } else {
        hotplug_handler_unplug(hotplug_ctrl, dev, errp);
    }
}

void qmp_device_del(const char *id, Error **errp)
{
    DeviceState *dev = find_device_state(id, errp);
    if (dev != NULL) {
        qdev_unplug(dev, errp);
    }
}

BlockBackend *blk_by_qdev_id(const char *id, Error **errp)
{
    DeviceState *dev;
    BlockBackend *blk;

    dev = find_device_state(id, errp);
    if (dev == NULL) {
        return NULL;
    }

    blk = blk_by_dev(dev);
    if (!blk) {
        error_setg(errp, "Device does not have a block device backend");
    }
    return blk;
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

    rc = sscanf(str, "%63[^.=].%63[^=]%n", driver, property, &offset);
    if (rc == 2 && str[offset] == '=') {
        opts = qemu_opts_create(&qemu_global_opts, NULL, 0, &error_abort);
        qemu_opt_set(opts, "driver", driver, &error_abort);
        qemu_opt_set(opts, "property", property, &error_abort);
        qemu_opt_set(opts, "value", str + offset + 1, &error_abort);
        return 0;
    }

    opts = qemu_opts_parse_noisily(&qemu_global_opts, str, false);
    if (!opts) {
        return -1;
    }

    return 0;
}
