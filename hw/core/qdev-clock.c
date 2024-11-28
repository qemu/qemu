/*
 * Device's clock input and output
 *
 * Copyright GreenSocs 2016-2020
 *
 * Authors:
 *  Frederic Konrad
 *  Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"

/*
 * qdev_init_clocklist:
 * Add a new clock in a device
 */
static NamedClockList *qdev_init_clocklist(DeviceState *dev, const char *name,
                                           bool alias, bool output, Clock *clk)
{
    NamedClockList *ncl;

    /*
     * Clock must be added before realize() so that we can compute the
     * clock's canonical path during device_realize().
     */
    assert(!dev->realized);

    /*
     * The ncl structure is freed by qdev_finalize_clocklist() which will
     * be called during @dev's device_finalize().
     */
    ncl = g_new0(NamedClockList, 1);
    ncl->name = g_strdup(name);
    ncl->alias = alias;
    ncl->output = output;
    ncl->clock = clk;

    QLIST_INSERT_HEAD(&dev->clocks, ncl, node);
    return ncl;
}

void qdev_finalize_clocklist(DeviceState *dev)
{
    /* called by @dev's device_finalize() */
    NamedClockList *ncl, *ncl_next;

    QLIST_FOREACH_SAFE(ncl, &dev->clocks, node, ncl_next) {
        QLIST_REMOVE(ncl, node);
        if (!ncl->alias) {
            /*
             * We kept a reference on the input clock to ensure it lives up to
             * this point; it is used by the monitor to show the frequency.
             */
            object_unref(OBJECT(ncl->clock));
        }
        g_free(ncl->name);
        g_free(ncl);
    }
}

Clock *qdev_init_clock_out(DeviceState *dev, const char *name)
{
    Clock *clk = CLOCK(object_new(TYPE_CLOCK));
    object_property_add_child(OBJECT(dev), name, OBJECT(clk));

    qdev_init_clocklist(dev, name, false, true, clk);
    return clk;
}

Clock *qdev_init_clock_in(DeviceState *dev, const char *name,
                          ClockCallback *callback, void *opaque,
                          unsigned int events)
{
    Clock *clk = CLOCK(object_new(TYPE_CLOCK));
    object_property_add_child(OBJECT(dev), name, OBJECT(clk));

    qdev_init_clocklist(dev, name, false, false, clk);
    if (callback) {
        clock_set_callback(clk, callback, opaque, events);
    }
    return clk;
}

void qdev_init_clocks(DeviceState *dev, const ClockPortInitArray clocks)
{
    const struct ClockPortInitElem *elem;

    for (elem = &clocks[0]; elem->name != NULL; elem++) {
        Clock **clkp;
        /* offset cannot be inside the DeviceState part */
        assert(elem->offset > sizeof(DeviceState));
        clkp = ((void *)dev) + elem->offset;
        if (elem->is_output) {
            *clkp = qdev_init_clock_out(dev, elem->name);
        } else {
            *clkp = qdev_init_clock_in(dev, elem->name, elem->callback, dev,
                                       elem->callback_events);
        }
    }
}

static NamedClockList *qdev_get_clocklist(DeviceState *dev, const char *name)
{
    NamedClockList *ncl;

    QLIST_FOREACH(ncl, &dev->clocks, node) {
        if (strcmp(name, ncl->name) == 0) {
            return ncl;
        }
    }

    return NULL;
}

Clock *qdev_get_clock_in(DeviceState *dev, const char *name)
{
    NamedClockList *ncl;

    assert(name);

    ncl = qdev_get_clocklist(dev, name);
    if (!ncl) {
        error_report("Can not find clock-in '%s' for device type '%s'",
                     name, object_get_typename(OBJECT(dev)));
        abort();
    }
    assert(!ncl->output);

    return ncl->clock;
}

Clock *qdev_get_clock_out(DeviceState *dev, const char *name)
{
    NamedClockList *ncl;

    assert(name);

    ncl = qdev_get_clocklist(dev, name);
    if (!ncl) {
        error_report("Can not find clock-out '%s' for device type '%s'",
                     name, object_get_typename(OBJECT(dev)));
        abort();
    }
    assert(ncl->output);

    return ncl->clock;
}

Clock *qdev_alias_clock(DeviceState *dev, const char *name,
                        DeviceState *alias_dev, const char *alias_name)
{
    NamedClockList *ncl = qdev_get_clocklist(dev, name);
    Clock *clk = ncl->clock;

    ncl = qdev_init_clocklist(alias_dev, alias_name, true, ncl->output, clk);

    object_property_add_link(OBJECT(alias_dev), alias_name,
                             TYPE_CLOCK,
                             (Object **) &ncl->clock,
                             NULL, OBJ_PROP_LINK_STRONG);
    /*
     * Since the link property has the OBJ_PROP_LINK_STRONG flag, the clk
     * object reference count gets decremented on property deletion.
     * However object_property_add_link does not increment it since it
     * doesn't know the linked object. Increment it here to ensure the
     * aliased clock stays alive during this device life-time.
     */
    object_ref(OBJECT(clk));

    return clk;
}

void qdev_connect_clock_in(DeviceState *dev, const char *name, Clock *source)
{
    assert(!dev->realized);
    clock_set_source(qdev_get_clock_in(dev, name), source);
}
