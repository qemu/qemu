/*
 * General purpose implementation of a simple periodic countdown timer.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GNU LGPL.
 */
#ifndef PTIMER_H
#define PTIMER_H

#include "qemu-common.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"

/* The default ptimer policy retains backward compatibility with the legacy
 * timers. Custom policies are adjusting the default one. Consider providing
 * a correct policy for your timer.
 *
 * The rough edges of the default policy:
 *  - Starting to run with a period = 0 emits error message and stops the
 *    timer without a trigger.
 *
 *  - Setting period to 0 of the running timer emits error message and
 *    stops the timer without a trigger.
 *
 *  - Starting to run with counter = 0 or setting it to "0" while timer
 *    is running causes a trigger and reloads counter with a limit value.
 *    If limit = 0, ptimer emits error message and stops the timer.
 *
 *  - Counter value of the running timer is one less than the actual value.
 *
 *  - Changing period/frequency of the running timer loses time elapsed
 *    since the last period, effectively restarting the timer with a
 *    counter = counter value at the moment of change (.i.e. one less).
 */
#define PTIMER_POLICY_DEFAULT               0

/* ptimer.c */
typedef struct ptimer_state ptimer_state;
typedef void (*ptimer_cb)(void *opaque);

ptimer_state *ptimer_init(QEMUBH *bh, uint8_t policy_mask);
void ptimer_set_period(ptimer_state *s, int64_t period);
void ptimer_set_freq(ptimer_state *s, uint32_t freq);
uint64_t ptimer_get_limit(ptimer_state *s);
void ptimer_set_limit(ptimer_state *s, uint64_t limit, int reload);
uint64_t ptimer_get_count(ptimer_state *s);
void ptimer_set_count(ptimer_state *s, uint64_t count);
void ptimer_run(ptimer_state *s, int oneshot);
void ptimer_stop(ptimer_state *s);

extern const VMStateDescription vmstate_ptimer;

#define VMSTATE_PTIMER(_field, _state) \
    VMSTATE_STRUCT_POINTER_V(_field, _state, 1, vmstate_ptimer, ptimer_state)

#define VMSTATE_PTIMER_ARRAY(_f, _s, _n)                                \
    VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(_f, _s, _n, 0,                   \
                                       vmstate_ptimer, ptimer_state)

#endif
