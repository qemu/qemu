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

/* Periodic timer counter stays with "0" for a one period before wrapping
 * around.  */
#define PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD (1 << 0)

/* Running periodic timer that has counter = limit = 0 would continuously
 * re-trigger every period.  */
#define PTIMER_POLICY_CONTINUOUS_TRIGGER    (1 << 1)

/* Starting to run with/setting counter to "0" won't trigger immediately,
 * but after a one period for both oneshot and periodic modes.  */
#define PTIMER_POLICY_NO_IMMEDIATE_TRIGGER  (1 << 2)

/* Starting to run with/setting counter to "0" won't re-load counter
 * immediately, but after a one period.  */
#define PTIMER_POLICY_NO_IMMEDIATE_RELOAD   (1 << 3)

/* Make counter value of the running timer represent the actual value and
 * not the one less.  */
#define PTIMER_POLICY_NO_COUNTER_ROUND_DOWN (1 << 4)

/* ptimer.c */
typedef struct ptimer_state ptimer_state;
typedef void (*ptimer_cb)(void *opaque);

ptimer_state *ptimer_init(QEMUBH *bh, uint8_t policy_mask);
void ptimer_free(ptimer_state *s);
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
