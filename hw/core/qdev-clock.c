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
#include "hw/qdev-clock.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"

/*
 * qdev_init_clocklist:
 * Add a new clock in a device
 */
static NamedClockList *qdev_init_clocklist(DeviceState *dev, const char *name,
                                           bool output, Clock *clk)
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
    ncl->output = output;
    ncl->alias = (clk != NULL);

    /*
     * Trying to create a clock whose name clashes with some other
     * clock or property is a bug in the caller and we will abort().
     */
    if (clk == NULL) {
        clk = CLOCK(object_new(TYPE_CLOCK));
        object_property_add_child(OBJECT(dev), name, OBJECT(clk));
        if (output) {
            /*
             * Remove object_new()'s initial reference.
             * Note that for inputs, the reference created by object_new()
             * will be deleted in qdev_finalize_clocklist().
             */
            object_unref(OBJECT(clk));
        }
    } else {
        object_property_add_link(OBJECT(dev), name,
                                 object_get_typename(OBJECT(clk)),
                                 (Object **) &ncl->clock,
                                 NULL, OBJ_PROP_LINK_STRONG);
    }

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
        if (!ncl->output && !ncl->alias) {
            /*
             * We kept a reference on the input clock to ensure it lives up to
             * this point so we can safely remove the callback.
             * It avoids having a callback to a deleted object if ncl->clock
             * is still referenced somewhere else (eg: by a clock output).
             */
            clock_clear_callback(ncl->clock);
            object_unref(OBJECT(ncl->clock));
        }
        g_free(ncl->name);
        g_free(ncl);
    }
}

Clock *qdev_init_clock_out(DeviceState *dev, const char *name)
{
    NamedClockList *ncl;

    assert(name);

    ncl = qdev_init_clocklist(dev, name, true, NULL);

    return ncl->clock;
}

Clock *qdev_init_clock_in(DeviceState *dev, const char *name,
                            ClockCallback *callback, void *opaque)
{
    NamedClockList *ncl;

    assert(name);

    ncl = qdev_init_clocklist(dev, name, false, NULL);

    if (callback) {
        clock_set_callback(ncl->clock, callback, opaque);
    }
    return ncl->clock;
}

void qdev_init_clocks(DeviceState *dev, const ClockPortInitArray clocks)
{
    const struct ClockPortInitElem *elem;

    for (elem = &clocks[0]; elem->name != NULL; elem++) {
        Clock **clkp;
        /* offset cannot be inside the DeviceState part */
        assert(elem->offset > sizeof(DeviceState));
        clkp = (Clock **)(((void *) dev) + elem->offset);
        if (elem->is_output) {
            *clkp = qdev_init_clock_out(dev, elem->name);
        } else {
            *clkp = qdev_init_clock_in(dev, elem->name, elem->callback, dev);
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
    assert(!ncl->output);

    return ncl->clock;
}

Clock *qdev_get_clock_out(DeviceState *dev, const char *name)
{
    NamedClockList *ncl;

    assert(name);

    ncl = qdev_get_clocklist(dev, name);
    assert(ncl->output);

    return ncl->clock;
}

Clock *qdev_alias_clock(DeviceState *dev, const char *name,
                        DeviceState *alias_dev, const char *alias_name)
{
    NamedClockList *ncl;

    assert(name && alias_name);

    ncl = qdev_get_clocklist(dev, name);

    qdev_init_clocklist(alias_dev, alias_name, ncl->output, ncl->clock);

    return ncl->clock;
}
