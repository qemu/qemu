/*
 * Hardware Clocks
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
#include "qemu/cutils.h"
#include "hw/clock.h"
#include "trace.h"

#define CLOCK_PATH(_clk) (_clk->canonical_path)

void clock_setup_canonical_path(Clock *clk)
{
    g_free(clk->canonical_path);
    clk->canonical_path = object_get_canonical_path(OBJECT(clk));
}

Clock *clock_new(Object *parent, const char *name)
{
    Object *obj;
    Clock *clk;

    obj = object_new(TYPE_CLOCK);
    object_property_add_child(parent, name, obj);
    object_unref(obj);

    clk = CLOCK(obj);
    clock_setup_canonical_path(clk);

    return clk;
}

void clock_set_callback(Clock *clk, ClockCallback *cb, void *opaque)
{
    clk->callback = cb;
    clk->callback_opaque = opaque;
}

void clock_clear_callback(Clock *clk)
{
    clock_set_callback(clk, NULL, NULL);
}

bool clock_set(Clock *clk, uint64_t period)
{
    if (clk->period == period) {
        return false;
    }
    trace_clock_set(CLOCK_PATH(clk), CLOCK_PERIOD_TO_HZ(clk->period),
                    CLOCK_PERIOD_TO_HZ(period));
    clk->period = period;

    return true;
}

static void clock_propagate_period(Clock *clk, bool call_callbacks)
{
    Clock *child;

    QLIST_FOREACH(child, &clk->children, sibling) {
        if (child->period != clk->period) {
            child->period = clk->period;
            trace_clock_update(CLOCK_PATH(child), CLOCK_PATH(clk),
                               CLOCK_PERIOD_TO_HZ(clk->period),
                               call_callbacks);
            if (call_callbacks && child->callback) {
                child->callback(child->callback_opaque);
            }
            clock_propagate_period(child, call_callbacks);
        }
    }
}

void clock_propagate(Clock *clk)
{
    assert(clk->source == NULL);
    trace_clock_propagate(CLOCK_PATH(clk));
    clock_propagate_period(clk, true);
}

void clock_set_source(Clock *clk, Clock *src)
{
    /* changing clock source is not supported */
    assert(!clk->source);

    trace_clock_set_source(CLOCK_PATH(clk), CLOCK_PATH(src));

    clk->period = src->period;
    QLIST_INSERT_HEAD(&src->children, clk, sibling);
    clk->source = src;
    clock_propagate_period(clk, false);
}

static void clock_disconnect(Clock *clk)
{
    if (clk->source == NULL) {
        return;
    }

    trace_clock_disconnect(CLOCK_PATH(clk));

    clk->source = NULL;
    QLIST_REMOVE(clk, sibling);
}

char *clock_display_freq(Clock *clk)
{
    return freq_to_str(clock_get_hz(clk));
}

static void clock_initfn(Object *obj)
{
    Clock *clk = CLOCK(obj);

    QLIST_INIT(&clk->children);
}

static void clock_finalizefn(Object *obj)
{
    Clock *clk = CLOCK(obj);
    Clock *child, *next;

    /* clear our list of children */
    QLIST_FOREACH_SAFE(child, &clk->children, sibling, next) {
        clock_disconnect(child);
    }

    /* remove us from source's children list */
    clock_disconnect(clk);

    g_free(clk->canonical_path);
}

static const TypeInfo clock_info = {
    .name              = TYPE_CLOCK,
    .parent            = TYPE_OBJECT,
    .instance_size     = sizeof(Clock),
    .instance_init     = clock_initfn,
    .instance_finalize = clock_finalizefn,
};

static void clock_register_types(void)
{
    type_register_static(&clock_info);
}

type_init(clock_register_types)
