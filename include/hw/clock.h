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

#ifndef QEMU_HW_CLOCK_H
#define QEMU_HW_CLOCK_H

#include "qom/object.h"
#include "qemu/queue.h"

#define TYPE_CLOCK "clock"
#define CLOCK(obj) OBJECT_CHECK(Clock, (obj), TYPE_CLOCK)

typedef void ClockCallback(void *opaque);

/*
 * clock store a value representing the clock's period in 2^-32ns unit.
 * It can represent:
 *  + periods from 2^-32ns up to 4seconds
 *  + frequency from ~0.25Hz 2e10Ghz
 * Resolution of frequency representation decreases with frequency:
 * + at 100MHz, resolution is ~2mHz
 * + at 1Ghz,   resolution is ~0.2Hz
 * + at 10Ghz,  resolution is ~20Hz
 */
#define CLOCK_PERIOD_1SEC (1000000000llu << 32)

/*
 * macro helpers to convert to hertz / nanosecond
 */
#define CLOCK_PERIOD_FROM_NS(ns) ((ns) * (CLOCK_PERIOD_1SEC / 1000000000llu))
#define CLOCK_PERIOD_TO_NS(per) ((per) / (CLOCK_PERIOD_1SEC / 1000000000llu))
#define CLOCK_PERIOD_FROM_HZ(hz) (((hz) != 0) ? CLOCK_PERIOD_1SEC / (hz) : 0u)
#define CLOCK_PERIOD_TO_HZ(per) (((per) != 0) ? CLOCK_PERIOD_1SEC / (per) : 0u)

/**
 * Clock:
 * @parent_obj: parent class
 * @period: unsigned integer representing the period of the clock
 * @canonical_path: clock path string cache (used for trace purpose)
 * @callback: called when clock changes
 * @callback_opaque: argument for @callback
 * @source: source (or parent in clock tree) of the clock
 * @children: list of clocks connected to this one (it is their source)
 * @sibling: structure used to form a clock list
 */

typedef struct Clock Clock;

struct Clock {
    /*< private >*/
    Object parent_obj;

    /* all fields are private and should not be modified directly */

    /* fields */
    uint64_t period;
    char *canonical_path;
    ClockCallback *callback;
    void *callback_opaque;

    /* Clocks are organized in a clock tree */
    Clock *source;
    QLIST_HEAD(, Clock) children;
    QLIST_ENTRY(Clock) sibling;
};

/*
 * vmstate description entry to be added in device vmsd.
 */
extern const VMStateDescription vmstate_clock;
#define VMSTATE_CLOCK(field, state) \
    VMSTATE_CLOCK_V(field, state, 0)
#define VMSTATE_CLOCK_V(field, state, version) \
    VMSTATE_STRUCT_POINTER_V(field, state, version, vmstate_clock, Clock)

/**
 * clock_setup_canonical_path:
 * @clk: clock
 *
 * compute the canonical path of the clock (used by log messages)
 */
void clock_setup_canonical_path(Clock *clk);

/**
 * clock_set_callback:
 * @clk: the clock to register the callback into
 * @cb: the callback function
 * @opaque: the argument to the callback
 *
 * Register a callback called on every clock update.
 */
void clock_set_callback(Clock *clk, ClockCallback *cb, void *opaque);

/**
 * clock_clear_callback:
 * @clk: the clock to delete the callback from
 *
 * Unregister the callback registered with clock_set_callback.
 */
void clock_clear_callback(Clock *clk);

/**
 * clock_set_source:
 * @clk: the clock.
 * @src: the source clock
 *
 * Setup @src as the clock source of @clk. The current @src period
 * value is also copied to @clk and its subtree but no callback is
 * called.
 * Further @src update will be propagated to @clk and its subtree.
 */
void clock_set_source(Clock *clk, Clock *src);

/**
 * clock_set:
 * @clk: the clock to initialize.
 * @value: the clock's value, 0 means unclocked
 *
 * Set the local cached period value of @clk to @value.
 */
void clock_set(Clock *clk, uint64_t value);

static inline void clock_set_hz(Clock *clk, unsigned hz)
{
    clock_set(clk, CLOCK_PERIOD_FROM_HZ(hz));
}

static inline void clock_set_ns(Clock *clk, unsigned ns)
{
    clock_set(clk, CLOCK_PERIOD_FROM_NS(ns));
}

/**
 * clock_propagate:
 * @clk: the clock
 *
 * Propagate the clock period that has been previously configured using
 * @clock_set(). This will update recursively all connected clocks.
 * It is an error to call this function on a clock which has a source.
 * Note: this function must not be called during device inititialization
 * or migration.
 */
void clock_propagate(Clock *clk);

/**
 * clock_update:
 * @clk: the clock to update.
 * @value: the new clock's value, 0 means unclocked
 *
 * Update the @clk to the new @value. All connected clocks will be informed
 * of this update. This is equivalent to call @clock_set() then
 * @clock_propagate().
 */
static inline void clock_update(Clock *clk, uint64_t value)
{
    clock_set(clk, value);
    clock_propagate(clk);
}

static inline void clock_update_hz(Clock *clk, unsigned hz)
{
    clock_update(clk, CLOCK_PERIOD_FROM_HZ(hz));
}

static inline void clock_update_ns(Clock *clk, unsigned ns)
{
    clock_update(clk, CLOCK_PERIOD_FROM_NS(ns));
}

/**
 * clock_get:
 * @clk: the clk to fetch the clock
 *
 * @return: the current period.
 */
static inline uint64_t clock_get(const Clock *clk)
{
    return clk->period;
}

static inline unsigned clock_get_hz(Clock *clk)
{
    return CLOCK_PERIOD_TO_HZ(clock_get(clk));
}

static inline unsigned clock_get_ns(Clock *clk)
{
    return CLOCK_PERIOD_TO_NS(clock_get(clk));
}

/**
 * clock_is_enabled:
 * @clk: a clock
 *
 * @return: true if the clock is running.
 */
static inline bool clock_is_enabled(const Clock *clk)
{
    return clock_get(clk) != 0;
}

static inline void clock_init(Clock *clk, uint64_t value)
{
    clock_set(clk, value);
}
static inline void clock_init_hz(Clock *clk, uint64_t value)
{
    clock_set_hz(clk, value);
}
static inline void clock_init_ns(Clock *clk, uint64_t value)
{
    clock_set_ns(clk, value);
}

#endif /* QEMU_HW_CLOCK_H */
