/*
 * General purpose implementation of a simple periodic countdown timer.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GNU LGPL.
 */

#include "qemu/osdep.h"
#include "hw/ptimer.h"
#include "migration/vmstate.h"
#include "qemu/host-utils.h"
#include "sysemu/replay.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/qtest.h"
#include "block/aio.h"
#include "sysemu/cpus.h"
#include "hw/clock.h"

#define DELTA_ADJUST     1
#define DELTA_NO_ADJUST -1

struct ptimer_state
{
    uint8_t enabled; /* 0 = disabled, 1 = periodic, 2 = oneshot.  */
    uint64_t limit;
    uint64_t delta;
    uint32_t period_frac;
    int64_t period;
    int64_t last_event;
    int64_t next_event;
    uint8_t policy_mask;
    QEMUTimer *timer;
    ptimer_cb callback;
    void *callback_opaque;
    /*
     * These track whether we're in a transaction block, and if we
     * need to do a timer reload when the block finishes. They don't
     * need to be migrated because migration can never happen in the
     * middle of a transaction block.
     */
    bool in_transaction;
    bool need_reload;
};

/* Use a bottom-half routine to avoid reentrancy issues.  */
static void ptimer_trigger(ptimer_state *s)
{
    s->callback(s->callback_opaque);
}

static void ptimer_reload(ptimer_state *s, int delta_adjust)
{
    uint32_t period_frac;
    uint64_t period;
    uint64_t delta;
    bool suppress_trigger = false;

    /*
     * Note that if delta_adjust is 0 then we must be here because of
     * a count register write or timer start, not because of timer expiry.
     * In that case the policy might require us to suppress the timer trigger
     * that we would otherwise generate for a zero delta.
     */
    if (delta_adjust == 0 &&
        (s->policy_mask & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT)) {
        suppress_trigger = true;
    }
    if (s->delta == 0 && !(s->policy_mask & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER)
        && !suppress_trigger) {
        ptimer_trigger(s);
    }

    /*
     * Note that ptimer_trigger() might call the device callback function,
     * which can then modify timer state, so we must not cache any fields
     * from ptimer_state until after we have called it.
     */
    delta = s->delta;
    period = s->period;
    period_frac = s->period_frac;

    if (delta == 0 && !(s->policy_mask & PTIMER_POLICY_NO_IMMEDIATE_RELOAD)) {
        delta = s->delta = s->limit;
    }

    if (s->period == 0) {
        if (!qtest_enabled()) {
            fprintf(stderr, "Timer with period zero, disabling\n");
        }
        timer_del(s->timer);
        s->enabled = 0;
        return;
    }

    if (s->policy_mask & PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD) {
        if (delta_adjust != DELTA_NO_ADJUST) {
            delta += delta_adjust;
        }
    }

    if (delta == 0 && (s->policy_mask & PTIMER_POLICY_CONTINUOUS_TRIGGER)) {
        if (s->enabled == 1 && s->limit == 0) {
            delta = 1;
        }
    }

    if (delta == 0 && (s->policy_mask & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER)) {
        if (delta_adjust != DELTA_NO_ADJUST) {
            delta = 1;
        }
    }

    if (delta == 0 && (s->policy_mask & PTIMER_POLICY_NO_IMMEDIATE_RELOAD)) {
        if (s->enabled == 1 && s->limit != 0) {
            delta = 1;
        }
    }

    if (delta == 0) {
        if (s->enabled == 0) {
            /* trigger callback disabled the timer already */
            return;
        }
        if (!qtest_enabled()) {
            fprintf(stderr, "Timer with delta zero, disabling\n");
        }
        timer_del(s->timer);
        s->enabled = 0;
        return;
    }

    /*
     * Artificially limit timeout rate to something
     * achievable under QEMU.  Otherwise, QEMU spends all
     * its time generating timer interrupts, and there
     * is no forward progress.
     * About ten microseconds is the fastest that really works
     * on the current generation of host machines.
     */

    if (s->enabled == 1 && (delta * period < 10000) &&
        !icount_enabled() && !qtest_enabled()) {
        period = 10000 / delta;
        period_frac = 0;
    }

    s->last_event = s->next_event;
    s->next_event = s->last_event + delta * period;
    if (period_frac) {
        s->next_event += ((int64_t)period_frac * delta) >> 32;
    }
    timer_mod(s->timer, s->next_event);
}

static void ptimer_tick(void *opaque)
{
    ptimer_state *s = (ptimer_state *)opaque;
    bool trigger = true;

    /*
     * We perform all the tick actions within a begin/commit block
     * because the callback function that ptimer_trigger() calls
     * might make calls into the ptimer APIs that provoke another
     * trigger, and we want that to cause the callback function
     * to be called iteratively, not recursively.
     */
    ptimer_transaction_begin(s);

    if (s->enabled == 2) {
        s->delta = 0;
        s->enabled = 0;
    } else {
        int delta_adjust = DELTA_ADJUST;

        if (s->delta == 0 || s->limit == 0) {
            /* If a "continuous trigger" policy is not used and limit == 0,
               we should error out. delta == 0 means that this tick is
               caused by a "no immediate reload" policy, so it shouldn't
               be adjusted.  */
            delta_adjust = DELTA_NO_ADJUST;
        }

        if (!(s->policy_mask & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER)) {
            /* Avoid re-trigger on deferred reload if "no immediate trigger"
               policy isn't used.  */
            trigger = (delta_adjust == DELTA_ADJUST);
        }

        s->delta = s->limit;

        ptimer_reload(s, delta_adjust);
    }

    if (trigger) {
        ptimer_trigger(s);
    }

    ptimer_transaction_commit(s);
}

uint64_t ptimer_get_count(ptimer_state *s)
{
    uint64_t counter;

    if (s->enabled && s->delta != 0) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t next = s->next_event;
        int64_t last = s->last_event;
        bool expired = (now - next >= 0);
        bool oneshot = (s->enabled == 2);

        /* Figure out the current counter value.  */
        if (expired) {
            /* Prevent timer underflowing if it should already have
               triggered.  */
            counter = 0;
        } else {
            uint64_t rem;
            uint64_t div;
            int clz1, clz2;
            int shift;
            uint32_t period_frac = s->period_frac;
            uint64_t period = s->period;

            if (!oneshot && (s->delta * period < 10000) &&
                !icount_enabled() && !qtest_enabled()) {
                period = 10000 / s->delta;
                period_frac = 0;
            }

            /* We need to divide time by period, where time is stored in
               rem (64-bit integer) and period is stored in period/period_frac
               (64.32 fixed point).

               Doing full precision division is hard, so scale values and
               do a 64-bit division.  The result should be rounded down,
               so that the rounding error never causes the timer to go
               backwards.
            */

            rem = next - now;
            div = period;

            clz1 = clz64(rem);
            clz2 = clz64(div);
            shift = clz1 < clz2 ? clz1 : clz2;

            rem <<= shift;
            div <<= shift;
            if (shift >= 32) {
                div |= ((uint64_t)period_frac << (shift - 32));
            } else {
                if (shift != 0)
                    div |= (period_frac >> (32 - shift));
                /* Look at remaining bits of period_frac and round div up if 
                   necessary.  */
                if ((uint32_t)(period_frac << shift))
                    div += 1;
            }
            counter = rem / div;

            if (s->policy_mask & PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD) {
                /* Before wrapping around, timer should stay with counter = 0
                   for a one period.  */
                if (!oneshot && s->delta == s->limit) {
                    if (now == last) {
                        /* Counter == delta here, check whether it was
                           adjusted and if it was, then right now it is
                           that "one period".  */
                        if (counter == s->limit + DELTA_ADJUST) {
                            return 0;
                        }
                    } else if (counter == s->limit) {
                        /* Since the counter is rounded down and now != last,
                           the counter == limit means that delta was adjusted
                           by +1 and right now it is that adjusted period.  */
                        return 0;
                    }
                }
            }
        }

        if (s->policy_mask & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN) {
            /* If now == last then delta == limit, i.e. the counter already
               represents the correct value. It would be rounded down a 1ns
               later.  */
            if (now != last) {
                counter += 1;
            }
        }
    } else {
        counter = s->delta;
    }
    return counter;
}

void ptimer_set_count(ptimer_state *s, uint64_t count)
{
    assert(s->in_transaction);
    s->delta = count;
    if (s->enabled) {
        s->need_reload = true;
    }
}

void ptimer_run(ptimer_state *s, int oneshot)
{
    bool was_disabled = !s->enabled;

    assert(s->in_transaction);

    if (was_disabled && s->period == 0) {
        if (!qtest_enabled()) {
            fprintf(stderr, "Timer with period zero, disabling\n");
        }
        return;
    }
    s->enabled = oneshot ? 2 : 1;
    if (was_disabled) {
        s->need_reload = true;
    }
}

/* Pause a timer.  Note that this may cause it to "lose" time, even if it
   is immediately restarted.  */
void ptimer_stop(ptimer_state *s)
{
    assert(s->in_transaction);

    if (!s->enabled)
        return;

    s->delta = ptimer_get_count(s);
    timer_del(s->timer);
    s->enabled = 0;
    s->need_reload = false;
}

/* Set counter increment interval in nanoseconds.  */
void ptimer_set_period(ptimer_state *s, int64_t period)
{
    assert(s->in_transaction);
    s->delta = ptimer_get_count(s);
    s->period = period;
    s->period_frac = 0;
    if (s->enabled) {
        s->need_reload = true;
    }
}

/* Set counter increment interval from a Clock */
void ptimer_set_period_from_clock(ptimer_state *s, const Clock *clk,
                                  unsigned int divisor)
{
    /*
     * The raw clock period is a 64-bit value in units of 2^-32 ns;
     * put another way it's a 32.32 fixed-point ns value. Our internal
     * representation of the period is 64.32 fixed point ns, so
     * the conversion is simple.
     */
    uint64_t raw_period = clock_get(clk);
    uint64_t period_frac;

    assert(s->in_transaction);
    s->delta = ptimer_get_count(s);
    s->period = extract64(raw_period, 32, 32);
    period_frac = extract64(raw_period, 0, 32);
    /*
     * divisor specifies a possible frequency divisor between the
     * clock and the timer, so it is a multiplier on the period.
     * We do the multiply after splitting the raw period out into
     * period and frac to avoid having to do a 32*64->96 multiply.
     */
    s->period *= divisor;
    period_frac *= divisor;
    s->period += extract64(period_frac, 32, 32);
    s->period_frac = (uint32_t)period_frac;

    if (s->enabled) {
        s->need_reload = true;
    }
}

/* Set counter frequency in Hz.  */
void ptimer_set_freq(ptimer_state *s, uint32_t freq)
{
    assert(s->in_transaction);
    s->delta = ptimer_get_count(s);
    s->period = 1000000000ll / freq;
    s->period_frac = (1000000000ll << 32) / freq;
    if (s->enabled) {
        s->need_reload = true;
    }
}

/* Set the initial countdown value.  If reload is nonzero then also set
   count = limit.  */
void ptimer_set_limit(ptimer_state *s, uint64_t limit, int reload)
{
    assert(s->in_transaction);
    s->limit = limit;
    if (reload)
        s->delta = limit;
    if (s->enabled && reload) {
        s->need_reload = true;
    }
}

uint64_t ptimer_get_limit(ptimer_state *s)
{
    return s->limit;
}

void ptimer_transaction_begin(ptimer_state *s)
{
    assert(!s->in_transaction);
    s->in_transaction = true;
    s->need_reload = false;
}

void ptimer_transaction_commit(ptimer_state *s)
{
    assert(s->in_transaction);
    /*
     * We must loop here because ptimer_reload() can call the callback
     * function, which might then update ptimer state in a way that
     * means we need to do another reload and possibly another callback.
     * A disabled timer never needs reloading (and if we don't check
     * this then we loop forever if ptimer_reload() disables the timer).
     */
    while (s->need_reload && s->enabled) {
        s->need_reload = false;
        s->next_event = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        ptimer_reload(s, 0);
    }
    /* Now we've finished reload we can leave the transaction block. */
    s->in_transaction = false;
}

const VMStateDescription vmstate_ptimer = {
    .name = "ptimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(enabled, ptimer_state),
        VMSTATE_UINT64(limit, ptimer_state),
        VMSTATE_UINT64(delta, ptimer_state),
        VMSTATE_UINT32(period_frac, ptimer_state),
        VMSTATE_INT64(period, ptimer_state),
        VMSTATE_INT64(last_event, ptimer_state),
        VMSTATE_INT64(next_event, ptimer_state),
        VMSTATE_TIMER_PTR(timer, ptimer_state),
        VMSTATE_END_OF_LIST()
    }
};

ptimer_state *ptimer_init(ptimer_cb callback, void *callback_opaque,
                          uint8_t policy_mask)
{
    ptimer_state *s;

    /* The callback function is mandatory. */
    assert(callback);

    s = g_new0(ptimer_state, 1);
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ptimer_tick, s);
    s->policy_mask = policy_mask;
    s->callback = callback;
    s->callback_opaque = callback_opaque;

    /*
     * These two policies are incompatible -- trigger-on-decrement implies
     * a timer trigger when the count becomes 0, but no-immediate-trigger
     * implies a trigger when the count stops being 0.
     */
    assert(!((policy_mask & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT) &&
             (policy_mask & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER)));
    return s;
}

void ptimer_free(ptimer_state *s)
{
    timer_free(s->timer);
    g_free(s);
}
