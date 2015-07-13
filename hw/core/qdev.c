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

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "hw/fw-path-provider.h"
#include "sysemu/sysemu.h"
#include "qapi/qmp/qerror.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qjson.h"
#include "qemu/error-report.h"
#include "hw/hotplug.h"
#include "hw/boards.h"
#include "qapi-event.h"

int qdev_hotplug = 0;
static bool qdev_hot_added = false;
static bool qdev_hot_removed = false;

const VMStateDescription *qdev_get_vmsd(DeviceState *dev)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    return dc->vmsd;
}

const char *qdev_fw_name(DeviceState *dev)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);

    if (dc->fw_name) {
        return dc->fw_name;
    }

    return object_get_typename(OBJECT(dev));
}

static void qdev_property_add_legacy(DeviceState *dev, Property *prop,
                                     Error **errp);

static void bus_remove_child(BusState *bus, DeviceState *child)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        if (kid->child == child) {
            char name[32];

            snprintf(name, sizeof(name), "child[%d]", kid->index);
            QTAILQ_REMOVE(&bus->children, kid, sibling);

            /* This gives back ownership of kid->child back to us.  */
            object_property_del(OBJECT(bus), name, NULL);
            object_unref(OBJECT(kid->child));
            g_free(kid);
            return;
        }
    }
}

static void bus_add_child(BusState *bus, DeviceState *child)
{
    char name[32];
    BusChild *kid = g_malloc0(sizeof(*kid));

    kid->index = bus->max_index++;
    kid->child = child;
    object_ref(OBJECT(kid->child));

    QTAILQ_INSERT_HEAD(&bus->children, kid, sibling);

    /* This transfers ownership of kid->child to the property.  */
    snprintf(name, sizeof(name), "child[%d]", kid->index);
    object_property_add_link(OBJECT(bus), name,
                             object_get_typename(OBJECT(child)),
                             (Object **)&kid->child,
                             NULL, /* read-only property */
                             0, /* return ownership on prop deletion */
                             NULL);
}

void qdev_set_parent_bus(DeviceState *dev, BusState *bus)
{
    dev->parent_bus = bus;
    object_ref(OBJECT(bus));
    bus_add_child(bus, dev);
}

/* Create a new device.  This only initializes the device state
   structure and allows properties to be set.  The device still needs
   to be realized.  See qdev-core.h.  */
DeviceState *qdev_create(BusState *bus, const char *name)
{
    DeviceState *dev;

    dev = qdev_try_create(bus, name);
    if (!dev) {
        if (bus) {
            error_report("Unknown device '%s' for bus '%s'", name,
                         object_get_typename(OBJECT(bus)));
        } else {
            error_report("Unknown device '%s' for default sysbus", name);
        }
        abort();
    }

    return dev;
}

DeviceState *qdev_try_create(BusState *bus, const char *type)
{
    DeviceState *dev;

    if (object_class_by_name(type) == NULL) {
        return NULL;
    }
    dev = DEVICE(object_new(type));
    if (!dev) {
        return NULL;
    }

    if (!bus) {
        bus = sysbus_get_default();
    }

    qdev_set_parent_bus(dev, bus);
    object_unref(OBJECT(dev));
    return dev;
}

static QTAILQ_HEAD(device_listeners, DeviceListener) device_listeners
    = QTAILQ_HEAD_INITIALIZER(device_listeners);

enum ListenerDirection { Forward, Reverse };

#define DEVICE_LISTENER_CALL(_callback, _direction, _args...)     \
    do {                                                          \
        DeviceListener *_listener;                                \
                                                                  \
        switch (_direction) {                                     \
        case Forward:                                             \
            QTAILQ_FOREACH(_listener, &device_listeners, link) {  \
                if (_listener->_callback) {                       \
                    _listener->_callback(_listener, ##_args);     \
                }                                                 \
            }                                                     \
            break;                                                \
        case Reverse:                                             \
            QTAILQ_FOREACH_REVERSE(_listener, &device_listeners,  \
                                   device_listeners, link) {      \
                if (_listener->_callback) {                       \
                    _listener->_callback(_listener, ##_args);     \
                }                                                 \
            }                                                     \
            break;                                                \
        default:                                                  \
            abort();                                              \
        }                                                         \
    } while (0)

static int device_listener_add(DeviceState *dev, void *opaque)
{
    DEVICE_LISTENER_CALL(realize, Forward, dev);

    return 0;
}

void device_listener_register(DeviceListener *listener)
{
    QTAILQ_INSERT_TAIL(&device_listeners, listener, link);

    qbus_walk_children(sysbus_get_default(), NULL, NULL, device_listener_add,
                       NULL, NULL);
}

void device_listener_unregister(DeviceListener *listener)
{
    QTAILQ_REMOVE(&device_listeners, listener, link);
}

static void device_realize(DeviceState *dev, Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);

    if (dc->init) {
        int rc = dc->init(dev);
        if (rc < 0) {
            error_setg(errp, "Device initialization failed.");
            return;
        }
    }
}

static void device_unrealize(DeviceState *dev, Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);

    if (dc->exit) {
        int rc = dc->exit(dev);
        if (rc < 0) {
            error_setg(errp, "Device exit failed.");
            return;
        }
    }
}

void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version)
{
    assert(!dev->realized);
    dev->instance_id_alias = alias_id;
    dev->alias_required_for_version = required_for_version;
}

HotplugHandler *qdev_get_hotplug_handler(DeviceState *dev)
{
    HotplugHandler *hotplug_ctrl = NULL;

    if (dev->parent_bus && dev->parent_bus->hotplug_handler) {
        hotplug_ctrl = dev->parent_bus->hotplug_handler;
    } else if (object_dynamic_cast(qdev_get_machine(), TYPE_MACHINE)) {
        MachineState *machine = MACHINE(qdev_get_machine());
        MachineClass *mc = MACHINE_GET_CLASS(machine);

        if (mc->get_hotplug_handler) {
            hotplug_ctrl = mc->get_hotplug_handler(machine, dev);
        }
    }
    return hotplug_ctrl;
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

static int qdev_reset_one(DeviceState *dev, void *opaque)
{
    device_reset(dev);

    return 0;
}

static int qbus_reset_one(BusState *bus, void *opaque)
{
    BusClass *bc = BUS_GET_CLASS(bus);
    if (bc->reset) {
        bc->reset(bus);
    }
    return 0;
}

void qdev_reset_all(DeviceState *dev)
{
    qdev_walk_children(dev, NULL, NULL, qdev_reset_one, qbus_reset_one, NULL);
}

void qdev_reset_all_fn(void *opaque)
{
    qdev_reset_all(DEVICE(opaque));
}

void qbus_reset_all(BusState *bus)
{
    qbus_walk_children(bus, NULL, NULL, qdev_reset_one, qbus_reset_one, NULL);
}

void qbus_reset_all_fn(void *opaque)
{
    BusState *bus = opaque;
    qbus_reset_all(bus);
}

/* can be used as ->unplug() callback for the simple cases */
void qdev_simple_device_unplug_cb(HotplugHandler *hotplug_dev,
                                  DeviceState *dev, Error **errp)
{
    /* just zap it */
    object_unparent(OBJECT(dev));
}

/*
 * Realize @dev.
 * Device properties should be set before calling this function.  IRQs
 * and MMIO regions should be connected/mapped after calling this
 * function.
 * On failure, report an error with error_report() and terminate the
 * program.  This is okay during machine creation.  Don't use for
 * hotplug, because there callers need to recover from failure.
 * Exception: if you know the device's init() callback can't fail,
 * then qdev_init_nofail() can't fail either, and is therefore usable
 * even then.  But relying on the device implementation that way is
 * somewhat unclean, and best avoided.
 */
void qdev_init_nofail(DeviceState *dev)
{
    Error *err = NULL;

    assert(!dev->realized);

    object_property_set_bool(OBJECT(dev), true, "realized", &err);
    if (err) {
        error_reportf_err(err, "Initialization of device %s failed: ",
                          object_get_typename(OBJECT(dev)));
        exit(1);
    }
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

BusState *qdev_get_parent_bus(DeviceState *dev)
{
    return dev->parent_bus;
}

static NamedGPIOList *qdev_get_named_gpio_list(DeviceState *dev,
                                               const char *name)
{
    NamedGPIOList *ngl;

    QLIST_FOREACH(ngl, &dev->gpios, node) {
        /* NULL is a valid and matchable name, otherwise do a normal
         * strcmp match.
         */
        if ((!ngl->name && !name) ||
                (name && ngl->name && strcmp(name, ngl->name) == 0)) {
            return ngl;
        }
    }

    ngl = g_malloc0(sizeof(*ngl));
    ngl->name = g_strdup(name);
    QLIST_INSERT_HEAD(&dev->gpios, ngl, node);
    return ngl;
}

void qdev_init_gpio_in_named(DeviceState *dev, qemu_irq_handler handler,
                             const char *name, int n)
{
    int i;
    NamedGPIOList *gpio_list = qdev_get_named_gpio_list(dev, name);

    assert(gpio_list->num_out == 0 || !name);
    gpio_list->in = qemu_extend_irqs(gpio_list->in, gpio_list->num_in, handler,
                                     dev, n);

    if (!name) {
        name = "unnamed-gpio-in";
    }
    for (i = gpio_list->num_in; i < gpio_list->num_in + n; i++) {
        gchar *propname = g_strdup_printf("%s[%u]", name, i);

        object_property_add_child(OBJECT(dev), propname,
                                  OBJECT(gpio_list->in[i]), &error_abort);
        g_free(propname);
    }

    gpio_list->num_in += n;
}

void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n)
{
    qdev_init_gpio_in_named(dev, handler, NULL, n);
}

void qdev_init_gpio_out_named(DeviceState *dev, qemu_irq *pins,
                              const char *name, int n)
{
    int i;
    NamedGPIOList *gpio_list = qdev_get_named_gpio_list(dev, name);

    assert(gpio_list->num_in == 0 || !name);

    if (!name) {
        name = "unnamed-gpio-out";
    }
    memset(pins, 0, sizeof(*pins) * n);
    for (i = 0; i < n; ++i) {
        gchar *propname = g_strdup_printf("%s[%u]", name,
                                          gpio_list->num_out + i);

        object_property_add_link(OBJECT(dev), propname, TYPE_IRQ,
                                 (Object **)&pins[i],
                                 object_property_allow_set_link,
                                 OBJ_PROP_LINK_UNREF_ON_RELEASE,
                                 &error_abort);
        g_free(propname);
    }
    gpio_list->num_out += n;
}

void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n)
{
    qdev_init_gpio_out_named(dev, pins, NULL, n);
}

qemu_irq qdev_get_gpio_in_named(DeviceState *dev, const char *name, int n)
{
    NamedGPIOList *gpio_list = qdev_get_named_gpio_list(dev, name);

    assert(n >= 0 && n < gpio_list->num_in);
    return gpio_list->in[n];
}

qemu_irq qdev_get_gpio_in(DeviceState *dev, int n)
{
    return qdev_get_gpio_in_named(dev, NULL, n);
}

void qdev_connect_gpio_out_named(DeviceState *dev, const char *name, int n,
                                 qemu_irq pin)
{
    char *propname = g_strdup_printf("%s[%d]",
                                     name ? name : "unnamed-gpio-out", n);
    if (pin) {
        /* We need a name for object_property_set_link to work.  If the
         * object has a parent, object_property_add_child will come back
         * with an error without doing anything.  If it has none, it will
         * never fail.  So we can just call it with a NULL Error pointer.
         */
        object_property_add_child(container_get(qdev_get_machine(),
                                                "/unattached"),
                                  "non-qdev-gpio[*]", OBJECT(pin), NULL);
    }
    object_property_set_link(OBJECT(dev), OBJECT(pin), propname, &error_abort);
    g_free(propname);
}

qemu_irq qdev_get_gpio_out_connector(DeviceState *dev, const char *name, int n)
{
    char *propname = g_strdup_printf("%s[%d]",
                                     name ? name : "unnamed-gpio-out", n);

    qemu_irq ret = (qemu_irq)object_property_get_link(OBJECT(dev), propname,
                                                      NULL);

    return ret;
}

/* disconnect a GPIO output, returning the disconnected input (if any) */

static qemu_irq qdev_disconnect_gpio_out_named(DeviceState *dev,
                                               const char *name, int n)
{
    char *propname = g_strdup_printf("%s[%d]",
                                     name ? name : "unnamed-gpio-out", n);

    qemu_irq ret = (qemu_irq)object_property_get_link(OBJECT(dev), propname,
                                                      NULL);
    if (ret) {
        object_property_set_link(OBJECT(dev), NULL, propname, NULL);
    }
    g_free(propname);
    return ret;
}

qemu_irq qdev_intercept_gpio_out(DeviceState *dev, qemu_irq icpt,
                                 const char *name, int n)
{
    qemu_irq disconnected = qdev_disconnect_gpio_out_named(dev, name, n);
    qdev_connect_gpio_out_named(dev, name, n, icpt);
    return disconnected;
}

void qdev_connect_gpio_out(DeviceState * dev, int n, qemu_irq pin)
{
    qdev_connect_gpio_out_named(dev, NULL, n, pin);
}

void qdev_pass_gpios(DeviceState *dev, DeviceState *container,
                     const char *name)
{
    int i;
    NamedGPIOList *ngl = qdev_get_named_gpio_list(dev, name);

    for (i = 0; i < ngl->num_in; i++) {
        const char *nm = ngl->name ? ngl->name : "unnamed-gpio-in";
        char *propname = g_strdup_printf("%s[%d]", nm, i);

        object_property_add_alias(OBJECT(container), propname,
                                  OBJECT(dev), propname,
                                  &error_abort);
        g_free(propname);
    }
    for (i = 0; i < ngl->num_out; i++) {
        const char *nm = ngl->name ? ngl->name : "unnamed-gpio-out";
        char *propname = g_strdup_printf("%s[%d]", nm, i);

        object_property_add_alias(OBJECT(container), propname,
                                  OBJECT(dev), propname,
                                  &error_abort);
        g_free(propname);
    }
    QLIST_REMOVE(ngl, node);
    QLIST_INSERT_HEAD(&container->gpios, ngl, node);
}

BusState *qdev_get_child_bus(DeviceState *dev, const char *name)
{
    BusState *bus;
    Object *child = object_resolve_path_component(OBJECT(dev), name);

    bus = (BusState *)object_dynamic_cast(child, TYPE_BUS);
    if (bus) {
        return bus;
    }

    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        if (strcmp(name, bus->name) == 0) {
            return bus;
        }
    }
    return NULL;
}

int qdev_walk_children(DeviceState *dev,
                       qdev_walkerfn *pre_devfn, qbus_walkerfn *pre_busfn,
                       qdev_walkerfn *post_devfn, qbus_walkerfn *post_busfn,
                       void *opaque)
{
    BusState *bus;
    int err;

    if (pre_devfn) {
        err = pre_devfn(dev, opaque);
        if (err) {
            return err;
        }
    }

    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        err = qbus_walk_children(bus, pre_devfn, pre_busfn,
                                 post_devfn, post_busfn, opaque);
        if (err < 0) {
            return err;
        }
    }

    if (post_devfn) {
        err = post_devfn(dev, opaque);
        if (err) {
            return err;
        }
    }

    return 0;
}

DeviceState *qdev_find_recursive(BusState *bus, const char *id)
{
    BusChild *kid;
    DeviceState *ret;
    BusState *child;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        DeviceState *dev = kid->child;

        if (dev->id && strcmp(dev->id, id) == 0) {
            return dev;
        }

        QLIST_FOREACH(child, &dev->child_bus, sibling) {
            ret = qdev_find_recursive(child, id);
            if (ret) {
                return ret;
            }
        }
    }
    return NULL;
}

static char *bus_get_fw_dev_path(BusState *bus, DeviceState *dev)
{
    BusClass *bc = BUS_GET_CLASS(bus);

    if (bc->get_fw_dev_path) {
        return bc->get_fw_dev_path(dev);
    }

    return NULL;
}

static char *qdev_get_fw_dev_path_from_handler(BusState *bus, DeviceState *dev)
{
    Object *obj = OBJECT(dev);
    char *d = NULL;

    while (!d && obj->parent) {
        obj = obj->parent;
        d = fw_path_provider_try_get_dev_path(obj, bus, dev);
    }
    return d;
}

char *qdev_get_own_fw_dev_path_from_handler(BusState *bus, DeviceState *dev)
{
    Object *obj = OBJECT(dev);

    return fw_path_provider_try_get_dev_path(obj, bus, dev);
}

static int qdev_get_fw_dev_path_helper(DeviceState *dev, char *p, int size)
{
    int l = 0;

    if (dev && dev->parent_bus) {
        char *d;
        l = qdev_get_fw_dev_path_helper(dev->parent_bus->parent, p, size);
        d = qdev_get_fw_dev_path_from_handler(dev->parent_bus, dev);
        if (!d) {
            d = bus_get_fw_dev_path(dev->parent_bus, dev);
        }
        if (d) {
            l += snprintf(p + l, size - l, "%s", d);
            g_free(d);
        } else {
            return l;
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

    return g_strdup(path);
}

char *qdev_get_dev_path(DeviceState *dev)
{
    BusClass *bc;

    if (!dev || !dev->parent_bus) {
        return NULL;
    }

    bc = BUS_GET_CLASS(dev->parent_bus);
    if (bc->get_dev_path) {
        return bc->get_dev_path(dev);
    }

    return NULL;
}

/**
 * Legacy property handling
 */

static void qdev_get_legacy_property(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;

    char buffer[1024];
    char *ptr = buffer;

    prop->info->print(dev, prop, buffer, sizeof(buffer));
    visit_type_str(v, name, &ptr, errp);
}

/**
 * @qdev_add_legacy_property - adds a legacy property
 *
 * Do not use this is new code!  Properties added through this interface will
 * be given names and types in the "legacy" namespace.
 *
 * Legacy properties are string versions of other OOM properties.  The format
 * of the string depends on the property type.
 */
static void qdev_property_add_legacy(DeviceState *dev, Property *prop,
                                     Error **errp)
{
    gchar *name;

    /* Register pointer properties as legacy properties */
    if (!prop->info->print && prop->info->get) {
        return;
    }

    name = g_strdup_printf("legacy-%s", prop->name);
    object_property_add(OBJECT(dev), name, "str",
                        prop->info->print ? qdev_get_legacy_property : prop->info->get,
                        NULL,
                        NULL,
                        prop, errp);

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
    Error *local_err = NULL;
    Object *obj = OBJECT(dev);

    /*
     * TODO qdev_prop_ptr does not have getters or setters.  It must
     * go now that it can be replaced with links.  The test should be
     * removed along with it: all static properties are read/write.
     */
    if (!prop->info->get && !prop->info->set) {
        return;
    }

    object_property_add(obj, prop->name, prop->info->name,
                        prop->info->get, prop->info->set,
                        prop->info->release,
                        prop, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    object_property_set_description(obj, prop->name,
                                    prop->info->description,
                                    &error_abort);

    if (prop->qtype == QTYPE_NONE) {
        return;
    }

    if (prop->qtype == QTYPE_QBOOL) {
        object_property_set_bool(obj, prop->defval, prop->name, &error_abort);
    } else if (prop->info->enum_table) {
        object_property_set_str(obj, prop->info->enum_table[prop->defval],
                                prop->name, &error_abort);
    } else if (prop->qtype == QTYPE_QINT) {
        object_property_set_int(obj, prop->defval, prop->name, &error_abort);
    }
}

/* @qdev_alias_all_properties - Add alias properties to the source object for
 * all qdev properties on the target DeviceState.
 */
void qdev_alias_all_properties(DeviceState *target, Object *source)
{
    ObjectClass *class;
    Property *prop;

    class = object_get_class(OBJECT(target));
    do {
        DeviceClass *dc = DEVICE_CLASS(class);

        for (prop = dc->props; prop && prop->name; prop++) {
            object_property_add_alias(source, prop->name,
                                      OBJECT(target), prop->name,
                                      &error_abort);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));
}

static int qdev_add_hotpluggable_device(Object *obj, void *opaque)
{
    GSList **list = opaque;
    DeviceState *dev = (DeviceState *)object_dynamic_cast(OBJECT(obj),
                                                          TYPE_DEVICE);

    if (dev == NULL) {
        return 0;
    }

    if (dev->realized && object_property_get_bool(obj, "hotpluggable", NULL)) {
        *list = g_slist_append(*list, dev);
    }

    return 0;
}

GSList *qdev_build_hotpluggable_device_list(Object *peripheral)
{
    GSList *list = NULL;

    object_child_foreach(peripheral, qdev_add_hotpluggable_device, &list);

    return list;
}

static bool device_get_realized(Object *obj, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    return dev->realized;
}

static void device_set_realized(Object *obj, bool value, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    HotplugHandler *hotplug_ctrl;
    BusState *bus;
    Error *local_err = NULL;

    if (dev->hotplugged && !dc->hotpluggable) {
        error_setg(errp, QERR_DEVICE_NO_HOTPLUG, object_get_typename(obj));
        return;
    }

    if (value && !dev->realized) {
        if (!obj->parent) {
            static int unattached_count;
            gchar *name = g_strdup_printf("device[%d]", unattached_count++);

            object_property_add_child(container_get(qdev_get_machine(),
                                                    "/unattached"),
                                      name, obj, &error_abort);
            g_free(name);
        }

        if (dc->realize) {
            dc->realize(dev, &local_err);
        }

        if (local_err != NULL) {
            goto fail;
        }

        DEVICE_LISTENER_CALL(realize, Forward, dev);

        hotplug_ctrl = qdev_get_hotplug_handler(dev);
        if (hotplug_ctrl) {
            hotplug_handler_plug(hotplug_ctrl, dev, &local_err);
        }

        if (local_err != NULL) {
            goto post_realize_fail;
        }

        if (qdev_get_vmsd(dev)) {
            vmstate_register_with_alias_id(dev, -1, qdev_get_vmsd(dev), dev,
                                           dev->instance_id_alias,
                                           dev->alias_required_for_version);
        }

        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            object_property_set_bool(OBJECT(bus), true, "realized",
                                         &local_err);
            if (local_err != NULL) {
                goto child_realize_fail;
            }
        }
        if (dev->hotplugged) {
            device_reset(dev);
        }
        dev->pending_deleted_event = false;
    } else if (!value && dev->realized) {
        Error **local_errp = NULL;
        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            local_errp = local_err ? NULL : &local_err;
            object_property_set_bool(OBJECT(bus), false, "realized",
                                     local_errp);
        }
        if (qdev_get_vmsd(dev)) {
            vmstate_unregister(dev, qdev_get_vmsd(dev), dev);
        }
        if (dc->unrealize) {
            local_errp = local_err ? NULL : &local_err;
            dc->unrealize(dev, local_errp);
        }
        dev->pending_deleted_event = true;
        DEVICE_LISTENER_CALL(unrealize, Reverse, dev);
    }

    if (local_err != NULL) {
        goto fail;
    }

    dev->realized = value;
    return;

child_realize_fail:
    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        object_property_set_bool(OBJECT(bus), false, "realized",
                                 NULL);
    }

    if (qdev_get_vmsd(dev)) {
        vmstate_unregister(dev, qdev_get_vmsd(dev), dev);
    }

post_realize_fail:
    if (dc->unrealize) {
        dc->unrealize(dev, NULL);
    }

fail:
    error_propagate(errp, local_err);
}

static bool device_get_hotpluggable(Object *obj, Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(obj);
    DeviceState *dev = DEVICE(obj);

    return dc->hotpluggable && (dev->parent_bus == NULL ||
                                qbus_is_hotpluggable(dev->parent_bus));
}

static bool device_get_hotplugged(Object *obj, Error **err)
{
    DeviceState *dev = DEVICE(obj);

    return dev->hotplugged;
}

static void device_set_hotplugged(Object *obj, bool value, Error **err)
{
    DeviceState *dev = DEVICE(obj);

    dev->hotplugged = value;
}

static void device_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    ObjectClass *class;
    Property *prop;

    if (qdev_hotplug) {
        dev->hotplugged = 1;
        qdev_hot_added = true;
    }

    dev->instance_id_alias = -1;
    dev->realized = false;

    object_property_add_bool(obj, "realized",
                             device_get_realized, device_set_realized, NULL);
    object_property_add_bool(obj, "hotpluggable",
                             device_get_hotpluggable, NULL, NULL);
    object_property_add_bool(obj, "hotplugged",
                             device_get_hotplugged, device_set_hotplugged,
                             &error_abort);

    class = object_get_class(OBJECT(dev));
    do {
        for (prop = DEVICE_CLASS(class)->props; prop && prop->name; prop++) {
            qdev_property_add_legacy(dev, prop, &error_abort);
            qdev_property_add_static(dev, prop, &error_abort);
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    object_property_add_link(OBJECT(dev), "parent_bus", TYPE_BUS,
                             (Object **)&dev->parent_bus, NULL, 0,
                             &error_abort);
    QLIST_INIT(&dev->gpios);
}

static void device_post_init(Object *obj)
{
    qdev_prop_set_globals(DEVICE(obj));
}

/* Unlink device from bus and free the structure.  */
static void device_finalize(Object *obj)
{
    NamedGPIOList *ngl, *next;

    DeviceState *dev = DEVICE(obj);

    QLIST_FOREACH_SAFE(ngl, &dev->gpios, node, next) {
        QLIST_REMOVE(ngl, node);
        qemu_free_irqs(ngl->in, ngl->num_in);
        g_free(ngl->name);
        g_free(ngl);
        /* ngl->out irqs are owned by the other end and should not be freed
         * here
         */
    }
}

static void device_class_base_init(ObjectClass *class, void *data)
{
    DeviceClass *klass = DEVICE_CLASS(class);

    /* We explicitly look up properties in the superclasses,
     * so do not propagate them to the subclasses.
     */
    klass->props = NULL;
}

static void device_unparent(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    BusState *bus;

    if (dev->realized) {
        object_property_set_bool(obj, false, "realized", NULL);
    }
    while (dev->num_child_bus) {
        bus = QLIST_FIRST(&dev->child_bus);
        object_unparent(OBJECT(bus));
    }
    if (dev->parent_bus) {
        bus_remove_child(dev->parent_bus, dev);
        object_unref(OBJECT(dev->parent_bus));
        dev->parent_bus = NULL;
    }

    /* Only send event if the device had been completely realized */
    if (dev->pending_deleted_event) {
        gchar *path = object_get_canonical_path(OBJECT(dev));

        qapi_event_send_device_deleted(!!dev->id, dev->id, path, &error_abort);
        g_free(path);
    }

    qemu_opts_del(dev->opts);
    dev->opts = NULL;
}

static void device_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);

    class->unparent = device_unparent;
    dc->realize = device_realize;
    dc->unrealize = device_unrealize;

    /* by default all devices were considered as hotpluggable,
     * so with intent to check it in generic qdev_unplug() /
     * device_set_realized() functions make every device
     * hotpluggable. Devices that shouldn't be hotpluggable,
     * should override it in their class_init()
     */
    dc->hotpluggable = true;
}

void device_reset(DeviceState *dev)
{
    DeviceClass *klass = DEVICE_GET_CLASS(dev);

    if (klass->reset) {
        klass->reset(dev);
    }
}

Object *qdev_get_machine(void)
{
    static Object *dev;

    if (dev == NULL) {
        dev = container_get(object_get_root(), "/machine");
    }

    return dev;
}

static const TypeInfo device_type_info = {
    .name = TYPE_DEVICE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(DeviceState),
    .instance_init = device_initfn,
    .instance_post_init = device_post_init,
    .instance_finalize = device_finalize,
    .class_base_init = device_class_base_init,
    .class_init = device_class_init,
    .abstract = true,
    .class_size = sizeof(DeviceClass),
};

static void qdev_register_types(void)
{
    type_register_static(&device_type_info);
}

type_init(qdev_register_types)
