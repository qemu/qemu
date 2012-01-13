/*
 * General purpose implementation of a simple periodic countdown timer.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GNU LGPL.
 */
#include "hw.h"
#include "qemu-timer.h"
#include "ptimer.h"
#include "host-utils.h"

struct ptimer_state
{
    uint8_t enabled; /* 0 = disabled, 1 = periodic, 2 = oneshot.  */
    uint64_t limit;
    uint64_t delta;
    uint32_t period_frac;
    int64_t period;
    int64_t last_event;
    int64_t next_event;
    QEMUBH *bh;
    QEMUTimer *timer;
};

/* Use a bottom-half routine to avoid reentrancy issues.  */
static void ptimer_trigger(ptimer_state *s)
{
    if (s->bh) {
        qemu_bh_schedule(s->bh);
    }
}

static void ptimer_reload(ptimer_state *s)
{
    if (s->delta == 0) {
        ptimer_trigger(s);
        s->delta = s->limit;
    }
    if (s->delta == 0 || s->period == 0) {
        fprintf(stderr, "Timer with period zero, disabling\n");
        s->enabled = 0;
        return;
    }

    s->last_event = s->next_event;
    s->next_event = s->last_event + s->delta * s->period;
    if (s->period_frac) {
        s->next_event += ((int64_t)s->period_frac * s->delta) >> 32;
    }
    qemu_mod_timer(s->timer, s->next_event);
}

static void ptimer_tick(void *opaque)
{
    ptimer_state *s = (ptimer_state *)opaque;
    ptimer_trigger(s);
    s->delta = 0;
    if (s->enabled == 2) {
        s->enabled = 0;
    } else {
        ptimer_reload(s);
    }
}

uint64_t ptimer_get_count(ptimer_state *s)
{
    int64_t now;
    uint64_t counter;

    if (s->enabled) {
        now = qemu_get_clock_ns(vm_clock);
        /* Figure out the current counter value.  */
        if (now - s->next_event > 0
            || s->period == 0) {
            /* Prevent timer underflowing if it should already have
               triggered.  */
            counter = 0;
        } else {
            uint64_t rem;
            uint64_t div;
            int clz1, clz2;
            int shift;

            /* We need to divide time by period, where time is stored in
               rem (64-bit integer) and period is stored in period/period_frac
               (64.32 fixed point).
              
               Doing full precision division is hard, so scale values and
               do a 64-bit division.  The result should be rounded down,
               so that the rounding error never causes the timer to go
               backwards.
            */

            rem = s->next_event - now;
            div = s->period;

            clz1 = clz64(rem);
            clz2 = clz64(div);
            shift = clz1 < clz2 ? clz1 : clz2;

            rem <<= shift;
            div <<= shift;
            if (shift >= 32) {
                div |= ((uint64_t)s->period_frac << (shift - 32));
            } else {
                if (shift != 0)
                    div |= (s->period_frac >> (32 - shift));
                /* Look at remaining bits of period_frac and round div up if 
                   necessary.  */
                if ((uint32_t)(s->period_frac << shift))
                    div += 1;
            }
            counter = rem / div;
        }
    } else {
        counter = s->delta;
    }
    return counter;
}

void ptimer_set_count(ptimer_state *s, uint64_t count)
{
    s->delta = count;
    if (s->enabled) {
        s->next_event = qemu_get_clock_ns(vm_clock);
        ptimer_reload(s);
    }
}

void ptimer_run(ptimer_state *s, int oneshot)
{
    if (s->enabled) {
        return;
    }
    if (s->period == 0) {
        fprintf(stderr, "Timer with period zero, disabling\n");
        return;
    }
    s->enabled = oneshot ? 2 : 1;
    s->next_event = qemu_get_clock_ns(vm_clock);
    ptimer_reload(s);
}

/* Pause a timer.  Note that this may cause it to "lose" time, even if it
   is immediately restarted.  */
void ptimer_stop(ptimer_state *s)
{
    if (!s->enabled)
        return;

    s->delta = ptimer_get_count(s);
    qemu_del_timer(s->timer);
    s->enabled = 0;
}

/* Set counter increment interval in nanoseconds.  */
void ptimer_set_period(ptimer_state *s, int64_t period)
{
    s->period = period;
    s->period_frac = 0;
    if (s->enabled) {
        s->next_event = qemu_get_clock_ns(vm_clock);
        ptimer_reload(s);
    }
}

/* Set counter frequency in Hz.  */
void ptimer_set_freq(ptimer_state *s, uint32_t freq)
{
    s->period = 1000000000ll / freq;
    s->period_frac = (1000000000ll << 32) / freq;
    if (s->enabled) {
        s->next_event = qemu_get_clock_ns(vm_clock);
        ptimer_reload(s);
    }
}

/* Set the initial countdown value.  If reload is nonzero then also set
   count = limit.  */
void ptimer_set_limit(ptimer_state *s, uint64_t limit, int reload)
{
    s->limit = limit;
    if (reload)
        s->delta = limit;
    if (s->enabled && reload) {
        s->next_event = qemu_get_clock_ns(vm_clock);
        ptimer_reload(s);
    }
}

const VMStateDescription vmstate_ptimer = {
    .name = "ptimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8(enabled, ptimer_state),
        VMSTATE_UINT64(limit, ptimer_state),
        VMSTATE_UINT64(delta, ptimer_state),
        VMSTATE_UINT32(period_frac, ptimer_state),
        VMSTATE_INT64(period, ptimer_state),
        VMSTATE_INT64(last_event, ptimer_state),
        VMSTATE_INT64(next_event, ptimer_state),
        VMSTATE_TIMER(timer, ptimer_state),
        VMSTATE_END_OF_LIST()
    }
};

ptimer_state *ptimer_init(QEMUBH *bh)
{
    ptimer_state *s;

    s = (ptimer_state *)g_malloc0(sizeof(ptimer_state));
    s->bh = bh;
    s->timer = qemu_new_timer_ns(vm_clock, ptimer_tick, s);
    return s;
}
