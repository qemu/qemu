/*
 * General purpose implementation of a simple periodic countdown timer.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GNU LGPL.
 */
#ifndef PTIMER_H
#define PTIMER_H

#include "qemu/timer.h"

/*
 * The ptimer API implements a simple periodic countdown timer.
 * The countdown timer has a value (which can be read and written via
 * ptimer_get_count() and ptimer_set_count()). When it is enabled
 * using ptimer_run(), the value will count downwards at the frequency
 * which has been configured using ptimer_set_period() or ptimer_set_freq().
 * When it reaches zero it will trigger a callback function, and
 * can be set to either reload itself from a specified limit value
 * and keep counting down, or to stop (as a one-shot timer).
 *
 * A transaction-based API is used for modifying ptimer state: all calls
 * to functions which modify ptimer state must be between matched calls to
 * ptimer_transaction_begin() and ptimer_transaction_commit().
 * When ptimer_transaction_commit() is called it will evaluate the state
 * of the timer after all the changes in the transaction, and call the
 * callback if necessary. (See the ptimer_init() documentation for the full
 * list of state-modifying functions and detailed semantics of the callback.)
 *
 * Forgetting to set the period/frequency (or setting it to zero) is a
 * bug in the QEMU device and will cause warning messages to be printed
 * to stderr when the guest attempts to enable the timer.
 */

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

/*
 * Starting to run with a zero counter, or setting the counter to "0" via
 * ptimer_set_count() or ptimer_set_limit() will not trigger the timer
 * (though it will cause a reload). Only a counter decrement to "0"
 * will cause a trigger. Not compatible with NO_IMMEDIATE_TRIGGER;
 * ptimer_init() will assert() that you don't set both.
 */
#define PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT (1 << 5)

/* ptimer.c */
typedef struct ptimer_state ptimer_state;
typedef void (*ptimer_cb)(void *opaque);

/**
 * ptimer_init - Allocate and return a new ptimer
 * @callback: function to call on ptimer expiry
 * @callback_opaque: opaque pointer passed to @callback
 * @policy: PTIMER_POLICY_* bits specifying behaviour
 *
 * The ptimer returned must be freed using ptimer_free().
 *
 * If a ptimer is created using this API then will use the
 * transaction-based API for modifying ptimer state: all calls
 * to functions which modify ptimer state:
 *  - ptimer_set_period()
 *  - ptimer_set_freq()
 *  - ptimer_set_limit()
 *  - ptimer_set_count()
 *  - ptimer_run()
 *  - ptimer_stop()
 * must be between matched calls to ptimer_transaction_begin()
 * and ptimer_transaction_commit(). When ptimer_transaction_commit()
 * is called it will evaluate the state of the timer after all the
 * changes in the transaction, and call the callback if necessary.
 *
 * The callback function is always called from within a transaction
 * begin/commit block, so the callback should not call the
 * ptimer_transaction_begin() function itself. If the callback changes
 * the ptimer state such that another ptimer expiry is triggered, then
 * the callback will be called a second time after the first call returns.
 */
ptimer_state *ptimer_init(ptimer_cb callback,
                          void *callback_opaque,
                          uint8_t policy_mask);

/**
 * ptimer_free - Free a ptimer
 * @s: timer to free
 *
 * Free a ptimer created using ptimer_init().
 */
void ptimer_free(ptimer_state *s);

/**
 * ptimer_transaction_begin() - Start a ptimer modification transaction
 *
 * This function must be called before making any calls to functions
 * which modify the ptimer's state (see the ptimer_init() documentation
 * for a list of these), and must always have a matched call to
 * ptimer_transaction_commit().
 * It is an error to call this function for a BH-based ptimer;
 * attempting to do this will trigger an assert.
 */
void ptimer_transaction_begin(ptimer_state *s);

/**
 * ptimer_transaction_commit() - Commit a ptimer modification transaction
 *
 * This function must be called after calls to functions which modify
 * the ptimer's state, and completes the update of the ptimer. If the
 * ptimer state now means that we should trigger the timer expiry
 * callback, it will be called directly.
 */
void ptimer_transaction_commit(ptimer_state *s);

/**
 * ptimer_set_period - Set counter increment interval in nanoseconds
 * @s: ptimer to configure
 * @period: period of the counter in nanoseconds
 *
 * Note that if your counter behaviour is specified as having a
 * particular frequency rather than a period then ptimer_set_freq()
 * may be more appropriate.
 *
 * This function will assert if it is called outside a
 * ptimer_transaction_begin/commit block.
 */
void ptimer_set_period(ptimer_state *s, int64_t period);

/**
 * ptimer_set_freq - Set counter frequency in Hz
 * @s: ptimer to configure
 * @freq: counter frequency in Hz
 *
 * This does the same thing as ptimer_set_period(), so you only
 * need to call one of them. If the counter behaviour is specified
 * as setting the frequency then this function is more appropriate,
 * because it allows specifying an effective period which is
 * precise to fractions of a nanosecond, avoiding rounding errors.
 *
 * This function will assert if it is called outside a
 * ptimer_transaction_begin/commit block.
 */
void ptimer_set_freq(ptimer_state *s, uint32_t freq);

/**
 * ptimer_get_limit - Get the configured limit of the ptimer
 * @s: ptimer to query
 *
 * This function returns the current limit (reload) value
 * of the down-counter; that is, the value which it will be
 * reset to when it hits zero.
 *
 * Generally timer devices using ptimers should be able to keep
 * their reload register state inside the ptimer using the get
 * and set limit functions rather than needing to also track it
 * in their own state structure.
 */
uint64_t ptimer_get_limit(ptimer_state *s);

/**
 * ptimer_set_limit - Set the limit of the ptimer
 * @s: ptimer
 * @limit: initial countdown value
 * @reload: if nonzero, then reset the counter to the new limit
 *
 * Set the limit value of the down-counter. The @reload flag can
 * be used to emulate the behaviour of timers which immediately
 * reload the counter when their reload register is written to.
 *
 * This function will assert if it is called outside a
 * ptimer_transaction_begin/commit block.
 */
void ptimer_set_limit(ptimer_state *s, uint64_t limit, int reload);

/**
 * ptimer_get_count - Get the current value of the ptimer
 * @s: ptimer
 *
 * Return the current value of the down-counter. This will
 * return the correct value whether the counter is enabled or
 * disabled.
 */
uint64_t ptimer_get_count(ptimer_state *s);

/**
 * ptimer_set_count - Set the current value of the ptimer
 * @s: ptimer
 * @count: count value to set
 *
 * Set the value of the down-counter. If the counter is currently
 * enabled this will arrange for a timer callback at the appropriate
 * point in the future.
 *
 * This function will assert if it is called outside a
 * ptimer_transaction_begin/commit block.
 */
void ptimer_set_count(ptimer_state *s, uint64_t count);

/**
 * ptimer_run - Start a ptimer counting
 * @s: ptimer
 * @oneshot: non-zero if this timer should only count down once
 *
 * Start a ptimer counting down; when it reaches zero the callback function
 * passed to ptimer_init() will be invoked.
 * If the @oneshot argument is zero,
 * the counter value will then be reloaded from the limit and it will
 * start counting down again. If @oneshot is non-zero, then the counter
 * will disable itself when it reaches zero.
 *
 * This function will assert if it is called outside a
 * ptimer_transaction_begin/commit block.
 */
void ptimer_run(ptimer_state *s, int oneshot);

/**
 * ptimer_stop - Stop a ptimer counting
 * @s: ptimer
 *
 * Pause a timer (the count stays at its current value until ptimer_run()
 * is called to start it counting again).
 *
 * Note that this can cause it to "lose" time, even if it is immediately
 * restarted.
 *
 * This function will assert if it is called outside a
 * ptimer_transaction_begin/commit block.
 */
void ptimer_stop(ptimer_state *s);

extern const VMStateDescription vmstate_ptimer;

#define VMSTATE_PTIMER(_field, _state) \
    VMSTATE_STRUCT_POINTER_V(_field, _state, 1, vmstate_ptimer, ptimer_state)

#define VMSTATE_PTIMER_ARRAY(_f, _s, _n)                                \
    VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(_f, _s, _n, 0,                   \
                                       vmstate_ptimer, ptimer_state)

#endif
