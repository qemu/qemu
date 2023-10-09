/*
 *  Dynamic device configuration and creation.
 *
 *  Copyright (c) 2009 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "hw/sysbus.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "monitor/qdev.h"
#include "sysemu/arch_init.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qdev.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qemu/option_int.h"
#include "sysemu/block-backend.h"
#include "migration/misc.h"
#include "migration/migration.h"
#include "qemu/cutils.h"
#include "hw/qdev-properties.h"
#include "hw/clock.h"
#include "hw/boards.h"

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

/* default virtio transport per architecture */
#define QEMU_ARCH_VIRTIO_PCI (QEMU_ARCH_ALPHA | QEMU_ARCH_ARM | \
                              QEMU_ARCH_HPPA | QEMU_ARCH_I386 | \
                              QEMU_ARCH_MIPS | QEMU_ARCH_PPC |  \
                              QEMU_ARCH_RISCV | QEMU_ARCH_SH4 | \
                              QEMU_ARCH_SPARC | QEMU_ARCH_XTENSA | \
                              QEMU_ARCH_LOONGARCH)
#define QEMU_ARCH_VIRTIO_CCW (QEMU_ARCH_S390X)
#define QEMU_ARCH_VIRTIO_MMIO (QEMU_ARCH_M68K)

/* Please keep this table sorted by typename. */
static const QDevAlias qdev_alias_table[] = {
    { "AC97", "ac97" }, /* -soundhw name */
    { "e1000", "e1000-82540em" },
    { "ES1370", "es1370" }, /* -soundhw name */
    { "ich9-ahci", "ahci" },
    { "lsi53c895a", "lsi" },
    { "virtio-9p-device", "virtio-9p", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-9p-ccw", "virtio-9p", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-9p-pci", "virtio-9p", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-balloon-device", "virtio-balloon", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-balloon-ccw", "virtio-balloon", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-balloon-pci", "virtio-balloon", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-blk-device", "virtio-blk", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-blk-ccw", "virtio-blk", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-blk-pci", "virtio-blk", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-gpu-device", "virtio-gpu", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-gpu-ccw", "virtio-gpu", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-gpu-pci", "virtio-gpu", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-gpu-gl-device", "virtio-gpu-gl", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-gpu-gl-pci", "virtio-gpu-gl", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-input-host-device", "virtio-input-host", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-input-host-ccw", "virtio-input-host", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-input-host-pci", "virtio-input-host", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-iommu-pci", "virtio-iommu", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-keyboard-device", "virtio-keyboard", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-keyboard-ccw", "virtio-keyboard", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-keyboard-pci", "virtio-keyboard", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-mouse-device", "virtio-mouse", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-mouse-ccw", "virtio-mouse", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-mouse-pci", "virtio-mouse", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-net-device", "virtio-net", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-net-ccw", "virtio-net", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-net-pci", "virtio-net", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-rng-device", "virtio-rng", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-rng-ccw", "virtio-rng", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-rng-pci", "virtio-rng", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-scsi-device", "virtio-scsi", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-scsi-ccw", "virtio-scsi", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-scsi-pci", "virtio-scsi", QEMU_ARCH_VIRTIO_PCI },
    { "virtio-serial-device", "virtio-serial", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-serial-ccw", "virtio-serial", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-serial-pci", "virtio-serial", QEMU_ARCH_VIRTIO_PCI},
    { "virtio-tablet-device", "virtio-tablet", QEMU_ARCH_VIRTIO_MMIO },
    { "virtio-tablet-ccw", "virtio-tablet", QEMU_ARCH_VIRTIO_CCW },
    { "virtio-tablet-pci", "virtio-tablet", QEMU_ARCH_VIRTIO_PCI },
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
    qemu_printf("name \"%s\"", object_class_get_name(OBJECT_CLASS(dc)));
    if (dc->bus_type) {
        qemu_printf(", bus %s", dc->bus_type);
    }
    if (qdev_class_has_alias(dc)) {
        qemu_printf(", alias \"%s\"", qdev_class_get_alias(dc));
    }
    if (dc->desc) {
        qemu_printf(", desc \"%s\"", dc->desc);
    }
    if (!dc->user_creatable) {
        qemu_printf(", no-user");
    }
    qemu_printf("\n");
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
        [DEVICE_CATEGORY_WATCHDOG]= "Watchdog",
        [DEVICE_CATEGORY_MAX]     = "Uncategorized",
    };
    GSList *list, *elt;
    int i;
    bool cat_printed;

    module_load_qom_all();
    list = object_class_get_list_sorted(TYPE_DEVICE, false);

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
                qemu_printf("%s%s devices:\n", i ? "\n" : "", cat_name[i]);
                cat_printed = true;
            }
            qdev_print_devinfo(dc);
        }
    }

    g_slist_free(list);
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

    oc = module_object_class_by_name(*driver);
    if (!oc) {
        const char *typename = find_typename_by_alias(*driver);

        if (typename) {
            *driver = typename;
            oc = module_object_class_by_name(*driver);
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
                   "a non-abstract device type");
        return NULL;
    }

    dc = DEVICE_CLASS(oc);
    if (!dc->user_creatable ||
        (phase_check(PHASE_MACHINE_READY) && !dc->hotpluggable)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                   "a pluggable device type");
        return NULL;
    }

    if (object_class_dynamic_cast(oc, TYPE_SYS_BUS_DEVICE)) {
        /* sysbus devices need to be allowed by the machine */
        MachineClass *mc = MACHINE_CLASS(object_get_class(qdev_get_machine()));
        if (!device_type_is_dynamic_sysbus(mc, *driver)) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "driver",
                       "a dynamic sysbus device type for the machine");
            return NULL;
        }
    }

    return dc;
}


int qdev_device_help(QemuOpts *opts)
{
    Error *local_err = NULL;
    const char *driver;
    ObjectPropertyInfoList *prop_list;
    ObjectPropertyInfoList *prop;
    GPtrArray *array;
    int i;

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

    if (prop_list) {
        qemu_printf("%s options:\n", driver);
    } else {
        qemu_printf("There are no options for %s.\n", driver);
    }
    array = g_ptr_array_new();
    for (prop = prop_list; prop; prop = prop->next) {
        g_ptr_array_add(array,
                        object_property_help(prop->value->name,
                                             prop->value->type,
                                             prop->value->default_value,
                                             prop->value->description));
    }
    g_ptr_array_sort(array, (GCompareFunc)qemu_pstrcmp0);
    for (i = 0; i < array->len; i++) {
        qemu_printf("%s\n", (char *)array->pdata[i]);
    }
    g_ptr_array_set_free_func(array, g_free);
    g_ptr_array_free(array, true);
    qapi_free_ObjectPropertyInfoList(prop_list);
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

static void qbus_error_append_bus_list_hint(DeviceState *dev,
                                            Error *const *errp)
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

static void qbus_error_append_dev_list_hint(BusState *bus,
                                            Error *const *errp)
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
    BusClass *bus_class;

    if (bus->full) {
        return true;
    }
    bus_class = BUS_GET_CLASS(bus);
    return bus_class->max_dev && bus->num_children >= bus_class->max_dev;
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
            qbus_error_append_dev_list_hint(bus, errp);
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
                qbus_error_append_bus_list_hint(dev, errp);
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
            qbus_error_append_bus_list_hint(dev, errp);
            return NULL;
        }
    }

    if (qbus_is_full(bus)) {
        error_setg(errp, "Bus '%s' is full", path);
        return NULL;
    }
    return bus;
}

/* Takes ownership of @id, will be freed when deleting the device */
const char *qdev_set_id(DeviceState *dev, char *id, Error **errp)
{
    ObjectProperty *prop;

    assert(!dev->id && !dev->realized);

    /*
     * object_property_[try_]add_child() below will assert the device
     * has no parent
     */
    if (id) {
        prop = object_property_try_add_child(qdev_get_peripheral(), id,
                                             OBJECT(dev), NULL);
        if (prop) {
            dev->id = id;
        } else {
            error_setg(errp, "Duplicate device ID '%s'", id);
            g_free(id);
            return NULL;
        }
    } else {
        static int anon_count;
        gchar *name = g_strdup_printf("device[%d]", anon_count++);
        prop = object_property_add_child(qdev_get_peripheral_anon(), name,
                                         OBJECT(dev));
        g_free(name);
    }

    return prop->name;
}

DeviceState *qdev_device_add_from_qdict(const QDict *opts,
                                        bool from_json, Error **errp)
{
    ERRP_GUARD();
    DeviceClass *dc;
    const char *driver, *path;
    char *id;
    DeviceState *dev = NULL;
    BusState *bus = NULL;

    driver = qdict_get_try_str(opts, "driver");
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
    path = qdict_get_try_str(opts, "bus");
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

    if (qdev_should_hide_device(opts, from_json, errp)) {
        if (bus && !qbus_is_hotpluggable(bus)) {
            error_setg(errp, QERR_BUS_NO_HOTPLUG, bus->name);
        }
        return NULL;
    } else if (*errp) {
        return NULL;
    }

    if (phase_check(PHASE_MACHINE_READY) && bus && !qbus_is_hotpluggable(bus)) {
        error_setg(errp, QERR_BUS_NO_HOTPLUG, bus->name);
        return NULL;
    }

    if (!migration_is_idle()) {
        error_setg(errp, "device_add not allowed while migrating");
        return NULL;
    }

    /* create device */
    dev = qdev_new(driver);

    /* Check whether the hotplug is allowed by the machine */
    if (phase_check(PHASE_MACHINE_READY)) {
        if (!qdev_hotplug_allowed(dev, errp)) {
            goto err_del_dev;
        }

        if (!bus && !qdev_get_machine_hotplug_handler(dev)) {
            /* No bus, no machine hotplug handler --> device is not hotpluggable */
            error_setg(errp, "Device '%s' can not be hotplugged on this machine",
                       driver);
            goto err_del_dev;
        }
    }

    /*
     * set dev's parent and register its id.
     * If it fails it means the id is already taken.
     */
    id = g_strdup(qdict_get_try_str(opts, "id"));
    if (!qdev_set_id(dev, id, errp)) {
        goto err_del_dev;
    }

    /* set properties */
    dev->opts = qdict_clone_shallow(opts);
    qdict_del(dev->opts, "driver");
    qdict_del(dev->opts, "bus");
    qdict_del(dev->opts, "id");

    object_set_properties_from_keyval(&dev->parent_obj, dev->opts, from_json,
                                      errp);
    if (*errp) {
        goto err_del_dev;
    }

    if (!qdev_realize(dev, bus, errp)) {
        goto err_del_dev;
    }
    return dev;

err_del_dev:
    if (dev) {
        object_unparent(OBJECT(dev));
        object_unref(OBJECT(dev));
    }
    return NULL;
}

/* Takes ownership of @opts on success */
DeviceState *qdev_device_add(QemuOpts *opts, Error **errp)
{
    QDict *qdict = qemu_opts_to_qdict(opts, NULL);
    DeviceState *ret;

    ret = qdev_device_add_from_qdict(qdict, false, errp);
    if (ret) {
        qemu_opts_del(opts);
    }
    qobject_unref(qdict);
    return ret;
}

#define qdev_printf(fmt, ...) monitor_printf(mon, "%*s" fmt, indent, "", ## __VA_ARGS__)
static void qbus_print(Monitor *mon, BusState *bus, int indent);

static void qdev_print_props(Monitor *mon, DeviceState *dev, Property *props,
                             int indent)
{
    if (!props)
        return;
    for (; props->name; props++) {
        char *value;
        char *legacy_name = g_strdup_printf("legacy-%s", props->name);

        if (object_property_get_type(OBJECT(dev), legacy_name, NULL)) {
            value = object_property_get_str(OBJECT(dev), legacy_name, NULL);
        } else {
            value = object_property_print(OBJECT(dev), props->name, true,
                                          NULL);
        }
        g_free(legacy_name);

        if (!value) {
            continue;
        }
        qdev_printf("%s = %s\n", props->name,
                    *value ? value : "<null>");
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
    NamedClockList *ncl;

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
    QLIST_FOREACH(ncl, &dev->clocks, node) {
        g_autofree char *freq_str = clock_display_freq(ncl->clock);
        qdev_printf("clock-%s%s \"%s\" freq_hz=%s\n",
                    ncl->output ? "out" : "in",
                    ncl->alias ? " (alias)" : "",
                    ncl->name, freq_str);
    }
    class = object_get_class(OBJECT(dev));
    do {
        qdev_print_props(mon, dev, DEVICE_CLASS(class)->props_, indent);
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

void qmp_device_add(QDict *qdict, QObject **ret_data, Error **errp)
{
    QemuOpts *opts;
    DeviceState *dev;

    opts = qemu_opts_from_qdict(qemu_find_opts("device"), qdict, errp);
    if (!opts) {
        return;
    }
    if (!monitor_cur_is_qmp() && qdev_device_help(opts)) {
        qemu_opts_del(opts);
        return;
    }
    dev = qdev_device_add(opts, errp);

    /*
     * Drain all pending RCU callbacks. This is done because
     * some bus related operations can delay a device removal
     * (in this case this can happen if device is added and then
     * removed due to a configuration error)
     * to a RCU callback, but user might expect that this interface
     * will finish its job completely once qmp command returns result
     * to the user
     */
    drain_call_rcu();

    if (!dev) {
        qemu_opts_del(opts);
        return;
    }
    object_unref(OBJECT(dev));
}

static DeviceState *find_device_state(const char *id, Error **errp)
{
    Object *obj = object_resolve_path_at(qdev_get_peripheral(), id);
    DeviceState *dev;

    if (!obj) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", id);
        return NULL;
    }

    dev = (DeviceState *)object_dynamic_cast(obj, TYPE_DEVICE);
    if (!dev) {
        error_setg(errp, "%s is not a hotpluggable device", id);
        return NULL;
    }

    return dev;
}

void qdev_unplug(DeviceState *dev, Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    HotplugHandler *hotplug_ctrl;
    HotplugHandlerClass *hdc;
    Error *local_err = NULL;

    if (qdev_unplug_blocked(dev, errp)) {
        return;
    }

    if (dev->parent_bus && !qbus_is_hotpluggable(dev->parent_bus)) {
        error_setg(errp, QERR_BUS_NO_HOTPLUG, dev->parent_bus->name);
        return;
    }

    if (!dc->hotpluggable) {
        error_setg(errp, QERR_DEVICE_NO_HOTPLUG,
                   object_get_typename(OBJECT(dev)));
        return;
    }

    if (!migration_is_idle() && !dev->allow_unplug_during_migration) {
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
        hotplug_handler_unplug_request(hotplug_ctrl, dev, &local_err);
    } else {
        hotplug_handler_unplug(hotplug_ctrl, dev, &local_err);
        if (!local_err) {
            object_unparent(OBJECT(dev));
        }
    }
    error_propagate(errp, local_err);
}

void qmp_device_del(const char *id, Error **errp)
{
    DeviceState *dev = find_device_state(id, errp);
    if (dev != NULL) {
        if (dev->pending_deleted_event &&
            (dev->pending_deleted_expires_ms == 0 ||
             dev->pending_deleted_expires_ms > qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL))) {
            error_setg(errp, "Device %s is already in the "
                             "process of unplug", id);
            return;
        }

        qdev_unplug(dev, errp);
    }
}

void hmp_device_add(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qmp_device_add((QDict *)qdict, NULL, &err);
    hmp_handle_error(mon, err);
}

void hmp_device_del(Monitor *mon, const QDict *qdict)
{
    const char *id = qdict_get_str(qdict, "id");
    Error *err = NULL;

    qmp_device_del(id, &err);
    hmp_handle_error(mon, err);
}

void device_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    GSList *list, *elt;
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    list = elt = object_class_get_list(TYPE_DEVICE, false);
    while (elt) {
        DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data,
                                             TYPE_DEVICE);

        if (dc->user_creatable) {
            readline_add_completion_of(rs, str,
                                object_class_get_name(OBJECT_CLASS(dc)));
        }
        elt = elt->next;
    }
    g_slist_free(list);
}

static int qdev_add_hotpluggable_device(Object *obj, void *opaque)
{
    GSList **list = opaque;
    DeviceState *dev = (DeviceState *)object_dynamic_cast(obj, TYPE_DEVICE);

    if (dev == NULL) {
        return 0;
    }

    if (dev->realized && object_property_get_bool(obj, "hotpluggable", NULL)) {
        *list = g_slist_append(*list, dev);
    }

    return 0;
}

static GSList *qdev_build_hotpluggable_device_list(Object *peripheral)
{
    GSList *list = NULL;

    object_child_foreach(peripheral, qdev_add_hotpluggable_device, &list);

    return list;
}

static void peripheral_device_del_completion(ReadLineState *rs,
                                             const char *str)
{
    Object *peripheral = container_get(qdev_get_machine(), "/peripheral");
    GSList *list, *item;

    list = qdev_build_hotpluggable_device_list(peripheral);
    if (!list) {
        return;
    }

    for (item = list; item; item = g_slist_next(item)) {
        DeviceState *dev = item->data;

        if (dev->id) {
            readline_add_completion_of(rs, str, dev->id);
        }
    }

    g_slist_free(list);
}

void device_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args != 2) {
        return;
    }

    readline_set_completion_index(rs, strlen(str));
    peripheral_device_del_completion(rs, str);
}

BlockBackend *blk_by_qdev_id(const char *id, Error **errp)
{
    DeviceState *dev;
    BlockBackend *blk;

    GLOBAL_STATE_CODE();

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
    if (!qemu_opt_get(opts, "driver")
        || !qemu_opt_get(opts, "property")
        || !qemu_opt_get(opts, "value")) {
        error_report("options 'driver', 'property', and 'value'"
                     " are required");
        return -1;
    }

    return 0;
}

bool qmp_command_available(const QmpCommand *cmd, Error **errp)
{
    if (!phase_check(PHASE_MACHINE_READY) &&
        !(cmd->options & QCO_ALLOW_PRECONFIG)) {
        error_setg(errp, "The command '%s' is permitted only after machine initialization has completed",
                   cmd->name);
        return false;
    }
    return true;
}
