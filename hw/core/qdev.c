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

/* The theory here is that it should be possible to create a machine without
   knowledge of specific devices.  Historically board init routines have
   passed a bunch of arguments to each device, requiring the board know
   exactly which device it is dealing with.  This file provides an abstract
   API for device configuration and initialization.  Devices will generally
   inherit from a particular bus (e.g. PCI or I2C) rather than
   this API directly.  */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-events-qdev.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/qdev-clock.h"
#include "migration/vmstate.h"
#include "trace.h"

static bool qdev_hot_added = false;
bool qdev_hot_removed = false;

const VMStateDescription *qdev_get_vmsd(DeviceState *dev)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    return dc->vmsd;
}

static void bus_free_bus_child(BusChild *kid)
{
    object_unref(OBJECT(kid->child));
    g_free(kid);
}

static void bus_remove_child(BusState *bus, DeviceState *child)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->children, sibling) {
        if (kid->child == child) {
            char name[32];

            snprintf(name, sizeof(name), "child[%d]", kid->index);
            QTAILQ_REMOVE_RCU(&bus->children, kid, sibling);

            bus->num_children--;

            /* This gives back ownership of kid->child back to us.  */
            object_property_del(OBJECT(bus), name);

            /* free the bus kid, when it is safe to do so*/
            call_rcu(kid, bus_free_bus_child, rcu);
            break;
        }
    }
}

static void bus_add_child(BusState *bus, DeviceState *child)
{
    char name[32];
    BusChild *kid = g_malloc0(sizeof(*kid));

    bus->num_children++;
    kid->index = bus->max_index++;
    kid->child = child;
    object_ref(OBJECT(kid->child));

    QTAILQ_INSERT_HEAD_RCU(&bus->children, kid, sibling);

    /* This transfers ownership of kid->child to the property.  */
    snprintf(name, sizeof(name), "child[%d]", kid->index);
    object_property_add_link(OBJECT(bus), name,
                             object_get_typename(OBJECT(child)),
                             (Object **)&kid->child,
                             NULL, /* read-only property */
                             0);
}

static bool bus_check_address(BusState *bus, DeviceState *child, Error **errp)
{
    BusClass *bc = BUS_GET_CLASS(bus);
    return !bc->check_address || bc->check_address(bus, child, errp);
}

bool qdev_set_parent_bus(DeviceState *dev, BusState *bus, Error **errp)
{
    BusState *old_parent_bus = dev->parent_bus;
    DeviceClass *dc = DEVICE_GET_CLASS(dev);

    assert(dc->bus_type && object_dynamic_cast(OBJECT(bus), dc->bus_type));

    if (!bus_check_address(bus, dev, errp)) {
        return false;
    }

    if (old_parent_bus) {
        trace_qdev_update_parent_bus(dev, object_get_typename(OBJECT(dev)),
            old_parent_bus, object_get_typename(OBJECT(old_parent_bus)),
            OBJECT(bus), object_get_typename(OBJECT(bus)));
        /*
         * Keep a reference to the device while it's not plugged into
         * any bus, to avoid it potentially evaporating when it is
         * dereffed in bus_remove_child().
         * Also keep the ref of the parent bus until the end, so that
         * we can safely call resettable_change_parent() below.
         */
        object_ref(OBJECT(dev));
        bus_remove_child(dev->parent_bus, dev);
    }
    dev->parent_bus = bus;
    object_ref(OBJECT(bus));
    bus_add_child(bus, dev);
    if (dev->realized) {
        resettable_change_parent(OBJECT(dev), OBJECT(bus),
                                 OBJECT(old_parent_bus));
    }
    if (old_parent_bus) {
        object_unref(OBJECT(old_parent_bus));
        object_unref(OBJECT(dev));
    }
    return true;
}

DeviceState *qdev_new(const char *name)
{
    if (!object_class_by_name(name)) {
        module_load_qom_one(name);
    }
    return DEVICE(object_new(name));
}

DeviceState *qdev_try_new(const char *name)
{
    if (!module_object_class_by_name(name)) {
        return NULL;
    }
    return DEVICE(object_new(name));
}

static QTAILQ_HEAD(, DeviceListener) device_listeners
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
                                   link) {                        \
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

bool qdev_should_hide_device(const QDict *opts, bool from_json, Error **errp)
{
    ERRP_GUARD();
    DeviceListener *listener;

    QTAILQ_FOREACH(listener, &device_listeners, link) {
        if (listener->hide_device) {
            if (listener->hide_device(listener, opts, from_json, errp)) {
                return true;
            } else if (*errp) {
                return false;
            }
        }
    }

    return false;
}

void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version)
{
    assert(!dev->realized);
    dev->instance_id_alias = alias_id;
    dev->alias_required_for_version = required_for_version;
}

static int qdev_prereset(DeviceState *dev, void *opaque)
{
    trace_qdev_reset_tree(dev, object_get_typename(OBJECT(dev)));
    return 0;
}

static int qbus_prereset(BusState *bus, void *opaque)
{
    trace_qbus_reset_tree(bus, object_get_typename(OBJECT(bus)));
    return 0;
}

static int qdev_reset_one(DeviceState *dev, void *opaque)
{
    device_legacy_reset(dev);

    return 0;
}

static int qbus_reset_one(BusState *bus, void *opaque)
{
    BusClass *bc = BUS_GET_CLASS(bus);
    trace_qbus_reset(bus, object_get_typename(OBJECT(bus)));
    if (bc->reset) {
        bc->reset(bus);
    }
    return 0;
}

void qdev_reset_all(DeviceState *dev)
{
    trace_qdev_reset_all(dev, object_get_typename(OBJECT(dev)));
    qdev_walk_children(dev, qdev_prereset, qbus_prereset,
                       qdev_reset_one, qbus_reset_one, NULL);
}

void qdev_reset_all_fn(void *opaque)
{
    qdev_reset_all(DEVICE(opaque));
}

void qbus_reset_all(BusState *bus)
{
    trace_qbus_reset_all(bus, object_get_typename(OBJECT(bus)));
    qbus_walk_children(bus, qdev_prereset, qbus_prereset,
                       qdev_reset_one, qbus_reset_one, NULL);
}

void qbus_reset_all_fn(void *opaque)
{
    BusState *bus = opaque;
    qbus_reset_all(bus);
}

void device_cold_reset(DeviceState *dev)
{
    resettable_reset(OBJECT(dev), RESET_TYPE_COLD);
}

bool device_is_in_reset(DeviceState *dev)
{
    return resettable_is_in_reset(OBJECT(dev));
}

static ResettableState *device_get_reset_state(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    return &dev->reset;
}

static void device_reset_child_foreach(Object *obj, ResettableChildCallback cb,
                                       void *opaque, ResetType type)
{
    DeviceState *dev = DEVICE(obj);
    BusState *bus;

    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        cb(OBJECT(bus), opaque, type);
    }
}

bool qdev_realize(DeviceState *dev, BusState *bus, Error **errp)
{
    assert(!dev->realized && !dev->parent_bus);

    if (bus) {
        if (!qdev_set_parent_bus(dev, bus, errp)) {
            return false;
        }
    } else {
        assert(!DEVICE_GET_CLASS(dev)->bus_type);
    }

    return object_property_set_bool(OBJECT(dev), "realized", true, errp);
}

bool qdev_realize_and_unref(DeviceState *dev, BusState *bus, Error **errp)
{
    bool ret;

    ret = qdev_realize(dev, bus, errp);
    object_unref(OBJECT(dev));
    return ret;
}

void qdev_unrealize(DeviceState *dev)
{
    object_property_set_bool(OBJECT(dev), "realized", false, &error_abort);
}

static int qdev_assert_realized_properly_cb(Object *obj, void *opaque)
{
    DeviceState *dev = DEVICE(object_dynamic_cast(obj, TYPE_DEVICE));
    DeviceClass *dc;

    if (dev) {
        dc = DEVICE_GET_CLASS(dev);
        assert(dev->realized);
        assert(dev->parent_bus || !dc->bus_type);
    }
    return 0;
}

void qdev_assert_realized_properly(void)
{
    object_child_foreach_recursive(object_get_root(),
                                   qdev_assert_realized_properly_cb, NULL);
}

bool qdev_machine_modified(void)
{
    return qdev_hot_added || qdev_hot_removed;
}

BusState *qdev_get_parent_bus(DeviceState *dev)
{
    return dev->parent_bus;
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

    WITH_RCU_READ_LOCK_GUARD() {
        QTAILQ_FOREACH_RCU(kid, &bus->children, sibling) {
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
    }
    return NULL;
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

static bool device_get_realized(Object *obj, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    return dev->realized;
}

static bool check_only_migratable(Object *obj, Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(obj);

    if (!vmstate_check_only_migratable(dc->vmsd)) {
        error_setg(errp, "Device %s is not migratable, but "
                   "--only-migratable was specified",
                   object_get_typename(obj));
        return false;
    }

    return true;
}

static void device_set_realized(Object *obj, bool value, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    HotplugHandler *hotplug_ctrl;
    BusState *bus;
    NamedClockList *ncl;
    Error *local_err = NULL;
    bool unattached_parent = false;
    static int unattached_count;

    if (dev->hotplugged && !dc->hotpluggable) {
        error_setg(errp, QERR_DEVICE_NO_HOTPLUG, object_get_typename(obj));
        return;
    }

    if (value && !dev->realized) {
        if (!check_only_migratable(obj, errp)) {
            goto fail;
        }

        if (!obj->parent) {
            gchar *name = g_strdup_printf("device[%d]", unattached_count++);

            object_property_add_child(container_get(qdev_get_machine(),
                                                    "/unattached"),
                                      name, obj);
            unattached_parent = true;
            g_free(name);
        }

        hotplug_ctrl = qdev_get_hotplug_handler(dev);
        if (hotplug_ctrl) {
            hotplug_handler_pre_plug(hotplug_ctrl, dev, &local_err);
            if (local_err != NULL) {
                goto fail;
            }
        }

        if (dc->realize) {
            dc->realize(dev, &local_err);
            if (local_err != NULL) {
                goto fail;
            }
        }

        DEVICE_LISTENER_CALL(realize, Forward, dev);

        /*
         * always free/re-initialize here since the value cannot be cleaned up
         * in device_unrealize due to its usage later on in the unplug path
         */
        g_free(dev->canonical_path);
        dev->canonical_path = object_get_canonical_path(OBJECT(dev));
        QLIST_FOREACH(ncl, &dev->clocks, node) {
            if (ncl->alias) {
                continue;
            } else {
                clock_setup_canonical_path(ncl->clock);
            }
        }

        if (qdev_get_vmsd(dev)) {
            if (vmstate_register_with_alias_id(VMSTATE_IF(dev),
                                               VMSTATE_INSTANCE_ID_ANY,
                                               qdev_get_vmsd(dev), dev,
                                               dev->instance_id_alias,
                                               dev->alias_required_for_version,
                                               &local_err) < 0) {
                goto post_realize_fail;
            }
        }

        /*
         * Clear the reset state, in case the object was previously unrealized
         * with a dirty state.
         */
        resettable_state_clear(&dev->reset);

        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            if (!qbus_realize(bus, errp)) {
                goto child_realize_fail;
            }
        }
        if (dev->hotplugged) {
            /*
             * Reset the device, as well as its subtree which, at this point,
             * should be realized too.
             */
            resettable_assert_reset(OBJECT(dev), RESET_TYPE_COLD);
            resettable_change_parent(OBJECT(dev), OBJECT(dev->parent_bus),
                                     NULL);
            resettable_release_reset(OBJECT(dev), RESET_TYPE_COLD);
        }
        dev->pending_deleted_event = false;

        if (hotplug_ctrl) {
            hotplug_handler_plug(hotplug_ctrl, dev, &local_err);
            if (local_err != NULL) {
                goto child_realize_fail;
            }
       }

       qatomic_store_release(&dev->realized, value);

    } else if (!value && dev->realized) {

        /*
         * Change the value so that any concurrent users are aware
         * that the device is going to be unrealized
         *
         * TODO: change .realized property to enum that states
         * each phase of the device realization/unrealization
         */

        qatomic_set(&dev->realized, value);
        /*
         * Ensure that concurrent users see this update prior to
         * any other changes done by unrealize.
         */
        smp_wmb();

        QLIST_FOREACH(bus, &dev->child_bus, sibling) {
            qbus_unrealize(bus);
        }
        if (qdev_get_vmsd(dev)) {
            vmstate_unregister(VMSTATE_IF(dev), qdev_get_vmsd(dev), dev);
        }
        if (dc->unrealize) {
            dc->unrealize(dev);
        }
        dev->pending_deleted_event = true;
        DEVICE_LISTENER_CALL(unrealize, Reverse, dev);
    }

    assert(local_err == NULL);
    return;

child_realize_fail:
    QLIST_FOREACH(bus, &dev->child_bus, sibling) {
        qbus_unrealize(bus);
    }

    if (qdev_get_vmsd(dev)) {
        vmstate_unregister(VMSTATE_IF(dev), qdev_get_vmsd(dev), dev);
    }

post_realize_fail:
    g_free(dev->canonical_path);
    dev->canonical_path = NULL;
    if (dc->unrealize) {
        dc->unrealize(dev);
    }

fail:
    error_propagate(errp, local_err);
    if (unattached_parent) {
        /*
         * Beware, this doesn't just revert
         * object_property_add_child(), it also runs bus_remove()!
         */
        object_unparent(OBJECT(dev));
        unattached_count--;
    }
}

static bool device_get_hotpluggable(Object *obj, Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(obj);
    DeviceState *dev = DEVICE(obj);

    return dc->hotpluggable && (dev->parent_bus == NULL ||
                                qbus_is_hotpluggable(dev->parent_bus));
}

static bool device_get_hotplugged(Object *obj, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    return dev->hotplugged;
}

static void device_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);

    if (phase_check(PHASE_MACHINE_READY)) {
        dev->hotplugged = 1;
        qdev_hot_added = true;
    }

    dev->instance_id_alias = -1;
    dev->realized = false;
    dev->allow_unplug_during_migration = false;

    QLIST_INIT(&dev->gpios);
    QLIST_INIT(&dev->clocks);
}

static void device_post_init(Object *obj)
{
    /*
     * Note: ordered so that the user's global properties take
     * precedence.
     */
    object_apply_compat_props(obj);
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

    qdev_finalize_clocklist(dev);

    /* Only send event if the device had been completely realized */
    if (dev->pending_deleted_event) {
        g_assert(dev->canonical_path);

        qapi_event_send_device_deleted(!!dev->id, dev->id, dev->canonical_path);
        g_free(dev->canonical_path);
        dev->canonical_path = NULL;
    }

    qobject_unref(dev->opts);
    g_free(dev->id);
}

static void device_class_base_init(ObjectClass *class, void *data)
{
    DeviceClass *klass = DEVICE_CLASS(class);

    /* We explicitly look up properties in the superclasses,
     * so do not propagate them to the subclasses.
     */
    klass->props_ = NULL;
}

static void device_unparent(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    BusState *bus;

    if (dev->realized) {
        qdev_unrealize(dev);
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
}

static char *
device_vmstate_if_get_id(VMStateIf *obj)
{
    DeviceState *dev = DEVICE(obj);

    return qdev_get_dev_path(dev);
}

/**
 * device_phases_reset:
 * Transition reset method for devices to allow moving
 * smoothly from legacy reset method to multi-phases
 */
static void device_phases_reset(DeviceState *dev)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(dev);

    if (rc->phases.enter) {
        rc->phases.enter(OBJECT(dev), RESET_TYPE_COLD);
    }
    if (rc->phases.hold) {
        rc->phases.hold(OBJECT(dev));
    }
    if (rc->phases.exit) {
        rc->phases.exit(OBJECT(dev));
    }
}

static void device_transitional_reset(Object *obj)
{
    DeviceClass *dc = DEVICE_GET_CLASS(obj);

    /*
     * This will call either @device_phases_reset (for multi-phases transitioned
     * devices) or a device's specific method for not-yet transitioned devices.
     * In both case, it does not reset children.
     */
    if (dc->reset) {
        dc->reset(DEVICE(obj));
    }
}

/**
 * device_get_transitional_reset:
 * check if the device's class is ready for multi-phase
 */
static ResettableTrFunction device_get_transitional_reset(Object *obj)
{
    DeviceClass *dc = DEVICE_GET_CLASS(obj);
    if (dc->reset != device_phases_reset) {
        /*
         * dc->reset has been overridden by a subclass,
         * the device is not ready for multi phase yet.
         */
        return device_transitional_reset;
    }
    return NULL;
}

static void device_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    VMStateIfClass *vc = VMSTATE_IF_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);

    class->unparent = device_unparent;

    /* by default all devices were considered as hotpluggable,
     * so with intent to check it in generic qdev_unplug() /
     * device_set_realized() functions make every device
     * hotpluggable. Devices that shouldn't be hotpluggable,
     * should override it in their class_init()
     */
    dc->hotpluggable = true;
    dc->user_creatable = true;
    vc->get_id = device_vmstate_if_get_id;
    rc->get_state = device_get_reset_state;
    rc->child_foreach = device_reset_child_foreach;

    /*
     * @device_phases_reset is put as the default reset method below, allowing
     * to do the multi-phase transition from base classes to leaf classes. It
     * allows a legacy-reset Device class to extend a multi-phases-reset
     * Device class for the following reason:
     * + If a base class B has been moved to multi-phase, then it does not
     *   override this default reset method and may have defined phase methods.
     * + A child class C (extending class B) which uses
     *   device_class_set_parent_reset() (or similar means) to override the
     *   reset method will still work as expected. @device_phases_reset function
     *   will be registered as the parent reset method and effectively call
     *   parent reset phases.
     */
    dc->reset = device_phases_reset;
    rc->get_transitional_function = device_get_transitional_reset;

    object_class_property_add_bool(class, "realized",
                                   device_get_realized, device_set_realized);
    object_class_property_add_bool(class, "hotpluggable",
                                   device_get_hotpluggable, NULL);
    object_class_property_add_bool(class, "hotplugged",
                                   device_get_hotplugged, NULL);
    object_class_property_add_link(class, "parent_bus", TYPE_BUS,
                                   offsetof(DeviceState, parent_bus), NULL, 0);
}

void device_class_set_parent_reset(DeviceClass *dc,
                                   DeviceReset dev_reset,
                                   DeviceReset *parent_reset)
{
    *parent_reset = dc->reset;
    dc->reset = dev_reset;
}

void device_class_set_parent_realize(DeviceClass *dc,
                                     DeviceRealize dev_realize,
                                     DeviceRealize *parent_realize)
{
    *parent_realize = dc->realize;
    dc->realize = dev_realize;
}

void device_class_set_parent_unrealize(DeviceClass *dc,
                                       DeviceUnrealize dev_unrealize,
                                       DeviceUnrealize *parent_unrealize)
{
    *parent_unrealize = dc->unrealize;
    dc->unrealize = dev_unrealize;
}

void device_legacy_reset(DeviceState *dev)
{
    DeviceClass *klass = DEVICE_GET_CLASS(dev);

    trace_qdev_reset(dev, object_get_typename(OBJECT(dev)));
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

static MachineInitPhase machine_phase;

bool phase_check(MachineInitPhase phase)
{
    return machine_phase >= phase;
}

void phase_advance(MachineInitPhase phase)
{
    assert(machine_phase == phase - 1);
    machine_phase = phase;
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
    .interfaces = (InterfaceInfo[]) {
        { TYPE_VMSTATE_IF },
        { TYPE_RESETTABLE_INTERFACE },
        { }
    }
};

static void qdev_register_types(void)
{
    type_register_static(&device_type_info);
}

type_init(qdev_register_types)
