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
#include "qemu/host-utils.h"
#include "qemu/bitops.h"

#define TYPE_CLOCK "clock"
OBJECT_DECLARE_SIMPLE_TYPE(Clock, CLOCK)

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
#define VMSTATE_ARRAY_CLOCK(field, state, num) \
    VMSTATE_ARRAY_CLOCK_V(field, state, num, 0)
#define VMSTATE_ARRAY_CLOCK_V(field, state, num, version)          \
    VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(field, state, num, version, \
                                       vmstate_clock, Clock)

/**
 * clock_setup_canonical_path:
 * @clk: clock
 *
 * compute the canonical path of the clock (used by log messages)
 */
void clock_setup_canonical_path(Clock *clk);

/**
 * clock_new:
 * @parent: the clock parent
 * @name: the clock object name
 *
 * Helper function to create a new clock and parent it to @parent. There is no
 * need to call clock_setup_canonical_path on the returned clock as it is done
 * by this function.
 *
 * @return the newly created clock
 */
Clock *clock_new(Object *parent, const char *name);

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
 * clock_has_source:
 * @clk: the clock
 *
 * Returns true if the clock has a source clock connected to it.
 * This is useful for devices which have input clocks which must
 * be connected by the board/SoC code which creates them. The
 * device code can use this to check in its realize method that
 * the clock has been connected.
 */
static inline bool clock_has_source(const Clock *clk)
{
    return clk->source != NULL;
}

/**
 * clock_set:
 * @clk: the clock to initialize.
 * @value: the clock's value, 0 means unclocked
 *
 * Set the local cached period value of @clk to @value.
 *
 * @return: true if the clock is changed.
 */
bool clock_set(Clock *clk, uint64_t value);

static inline bool clock_set_hz(Clock *clk, unsigned hz)
{
    return clock_set(clk, CLOCK_PERIOD_FROM_HZ(hz));
}

static inline bool clock_set_ns(Clock *clk, unsigned ns)
{
    return clock_set(clk, CLOCK_PERIOD_FROM_NS(ns));
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
    if (clock_set(clk, value)) {
        clock_propagate(clk);
    }
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

/**
 * clock_ticks_to_ns:
 * @clk: the clock to query
 * @ticks: number of ticks
 *
 * Returns the length of time in nanoseconds for this clock
 * to tick @ticks times. Because a clock can have a period
 * which is not a whole number of nanoseconds, it is important
 * to use this function when calculating things like timer
 * expiry deadlines, rather than attempting to obtain a "period
 * in nanoseconds" value and then multiplying that by a number
 * of ticks.
 *
 * The result could in theory be too large to fit in a 64-bit
 * value if the number of ticks and the clock period are both
 * large; to avoid overflow the result will be saturated to INT64_MAX
 * (because this is the largest valid input to the QEMUTimer APIs).
 * Since INT64_MAX nanoseconds is almost 300 years, anything with
 * an expiry later than that is in the "will never happen" category
 * and callers can reasonably not special-case the saturated result.
 */
static inline uint64_t clock_ticks_to_ns(const Clock *clk, uint64_t ticks)
{
    uint64_t ns_low, ns_high;

    /*
     * clk->period is the period in units of 2^-32 ns, so
     * (clk->period * ticks) is the required length of time in those
     * units, and we can convert to nanoseconds by multiplying by
     * 2^32, which is the same as shifting the 128-bit multiplication
     * result right by 32.
     */
    mulu64(&ns_low, &ns_high, clk->period, ticks);
    if (ns_high & MAKE_64BIT_MASK(31, 33)) {
        return INT64_MAX;
    }
    return ns_low >> 32 | ns_high << 32;
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

/**
 * clock_display_freq: return human-readable representation of clock frequency
 * @clk: clock
 *
 * Return a string which has a human-readable representation of the
 * clock's frequency, e.g. "33.3 MHz". This is intended for debug
 * and display purposes.
 *
 * The caller is responsible for freeing the string with g_free().
 */
char *clock_display_freq(Clock *clk);

#endif /* QEMU_HW_CLOCK_H */
