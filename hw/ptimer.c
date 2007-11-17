/*
 * General purpose implementation of a simple periodic countdown timer.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GNU LGPL.
 */
#include "hw.h"
#include "qemu-timer.h"


struct ptimer_state
{
    int enabled; /* 0 = disabled, 1 = periodic, 2 = oneshot.  */
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
        now = qemu_get_clock(vm_clock);
        /* Figure out the current counter value.  */
        if (now - s->next_event > 0
            || s->period == 0) {
            /* Prevent timer underflowing if it should already have
               triggered.  */
            counter = 0;
        } else {
            uint64_t rem;
            uint64_t div;

            rem = s->next_event - now;
            div = s->period;
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
        s->next_event = qemu_get_clock(vm_clock);
        ptimer_reload(s);
    }
}

void ptimer_run(ptimer_state *s, int oneshot)
{
    if (s->period == 0) {
        fprintf(stderr, "Timer with period zero, disabling\n");
        return;
    }
    s->enabled = oneshot ? 2 : 1;
    s->next_event = qemu_get_clock(vm_clock);
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
        s->next_event = qemu_get_clock(vm_clock);
        ptimer_reload(s);
    }
}

/* Set counter frequency in Hz.  */
void ptimer_set_freq(ptimer_state *s, uint32_t freq)
{
    s->period = 1000000000ll / freq;
    s->period_frac = (1000000000ll << 32) / freq;
    if (s->enabled) {
        s->next_event = qemu_get_clock(vm_clock);
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
        s->next_event = qemu_get_clock(vm_clock);
        ptimer_reload(s);
    }
}

void qemu_put_ptimer(QEMUFile *f, ptimer_state *s)
{
    qemu_put_byte(f, s->enabled);
    qemu_put_be64s(f, &s->limit);
    qemu_put_be64s(f, &s->delta);
    qemu_put_be32s(f, &s->period_frac);
    qemu_put_be64s(f, &s->period);
    qemu_put_be64s(f, &s->last_event);
    qemu_put_be64s(f, &s->next_event);
    qemu_put_timer(f, s->timer);
}

void qemu_get_ptimer(QEMUFile *f, ptimer_state *s)
{
    s->enabled = qemu_get_byte(f);
    qemu_get_be64s(f, &s->limit);
    qemu_get_be64s(f, &s->delta);
    qemu_get_be32s(f, &s->period_frac);
    qemu_get_be64s(f, &s->period);
    qemu_get_be64s(f, &s->last_event);
    qemu_get_be64s(f, &s->next_event);
    qemu_get_timer(f, s->timer);
}

ptimer_state *ptimer_init(QEMUBH *bh)
{
    ptimer_state *s;

    s = (ptimer_state *)qemu_mallocz(sizeof(ptimer_state));
    s->bh = bh;
    s->timer = qemu_new_timer(vm_clock, ptimer_tick, s);
    return s;
}

