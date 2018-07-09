/*
 * QTest testcase for the ptimer
 *
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <glib/gprintf.h>

#include "qemu/main-loop.h"
#include "hw/ptimer.h"

#include "libqtest.h"
#include "ptimer-test.h"

static bool triggered;

static void ptimer_trigger(void *opaque)
{
    triggered = true;
}

static void ptimer_test_expire_qemu_timers(int64_t expire_time,
                                           QEMUClockType type)
{
    QEMUTimerList *timer_list = main_loop_tlg.tl[type];
    QEMUTimer *t = timer_list->active_timers.next;

    while (t != NULL) {
        if (t->expire_time == expire_time) {
            timer_del(t);

            if (t->cb != NULL) {
                t->cb(t->opaque);
            }
        }

        t = t->next;
    }
}

static void ptimer_test_set_qemu_time_ns(int64_t ns)
{
    ptimer_test_time_ns = ns;
}

static void qemu_clock_step(uint64_t ns)
{
    int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);
    int64_t advanced_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns;

    while (deadline != -1 && deadline <= advanced_time) {
        ptimer_test_set_qemu_time_ns(deadline);
        ptimer_test_expire_qemu_timers(deadline, QEMU_CLOCK_VIRTUAL);
        deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL);
    }

    ptimer_test_set_qemu_time_ns(advanced_time);
}

static void check_set_count(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_count(ptimer, 1000);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 1000);
    g_assert_false(triggered);
    ptimer_free(ptimer);
}

static void check_set_limit(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_limit(ptimer, 1000, 0);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_cmpuint(ptimer_get_limit(ptimer), ==, 1000);
    g_assert_false(triggered);

    ptimer_set_limit(ptimer, 2000, 1);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 2000);
    g_assert_cmpuint(ptimer_get_limit(ptimer), ==, 2000);
    g_assert_false(triggered);
    ptimer_free(ptimer);
}

static void check_oneshot(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool no_round_down = (*policy & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_count(ptimer, 10);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 2 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 8 : 7);
    g_assert_false(triggered);

    ptimer_stop(ptimer);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 8 : 7);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 11);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 8 : 7);
    g_assert_false(triggered);

    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 7 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 1 : 0);

    if (no_round_down) {
        g_assert_false(triggered);
    } else {
        g_assert_true(triggered);

        triggered = false;
    }

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);

    if (no_round_down) {
        g_assert_true(triggered);

        triggered = false;
    } else {
        g_assert_false(triggered);
    }

    qemu_clock_step(4000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 10);

    qemu_clock_step(20000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 10);
    g_assert_false(triggered);

    ptimer_set_limit(ptimer, 9, 1);

    qemu_clock_step(20000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 9);
    g_assert_false(triggered);

    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 8 : 7);
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 20);

    qemu_clock_step(2000000 * 19 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 1 : 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    ptimer_stop(ptimer);

    triggered = false;

    qemu_clock_step(2000000 * 12 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);
    ptimer_free(ptimer);
}

static void check_periodic(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool wrap_policy = (*policy & PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD);
    bool no_immediate_trigger = (*policy & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER);
    bool no_immediate_reload = (*policy & PTIMER_POLICY_NO_IMMEDIATE_RELOAD);
    bool no_round_down = (*policy & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
    bool trig_only_on_dec = (*policy & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 10, 1);
    ptimer_run(ptimer, 0);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 10);
    g_assert_false(triggered);

    qemu_clock_step(1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 10 : 9);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 10 - 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, wrap_policy ? 0 : 10);
    g_assert_true(triggered);

    qemu_clock_step(1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                     wrap_policy ? 0 : (no_round_down ? 10 : 9));
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 9 : 8) + (wrap_policy ? 1 : 0));
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 20);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 20);
    g_assert_false(triggered);

    qemu_clock_step(1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 20 : 19);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 11 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 9 : 8);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 10);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 9 : 8) + (wrap_policy ? 1 : 0));
    g_assert_true(triggered);

    triggered = false;

    ptimer_set_count(ptimer, 3);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 3);
    g_assert_false(triggered);

    qemu_clock_step(1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 3 : 2);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 4);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 9 : 8) + (wrap_policy ? 1 : 0));
    g_assert_true(triggered);

    ptimer_stop(ptimer);
    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 9 : 8) + (wrap_policy ? 1 : 0));
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 3);
    ptimer_run(ptimer, 0);

    qemu_clock_step(2000000 * 3 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                     wrap_policy ? 0 : (no_round_down ? 10 : 9));
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 9 : 8) + (wrap_policy ? 1 : 0));
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 0);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                     no_immediate_reload ? 0 : 10);

    if (no_immediate_trigger || trig_only_on_dec) {
        g_assert_false(triggered);
    } else {
        g_assert_true(triggered);
    }

    triggered = false;

    qemu_clock_step(1);

    if (no_immediate_reload) {
        g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
        g_assert_false(triggered);

        qemu_clock_step(2000000);

        if (no_immediate_trigger) {
            g_assert_true(triggered);
        } else {
            g_assert_false(triggered);
        }

        triggered = false;
    }

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 10 : 9);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 12);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 8 : 7) + (wrap_policy ? 1 : 0));
    g_assert_true(triggered);

    ptimer_stop(ptimer);

    triggered = false;

    qemu_clock_step(2000000 * 10);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 8 : 7) + (wrap_policy ? 1 : 0));
    g_assert_false(triggered);

    ptimer_run(ptimer, 0);
    ptimer_set_period(ptimer, 0);

    qemu_clock_step(2000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    (no_round_down ? 8 : 7) + (wrap_policy ? 1 : 0));
    g_assert_false(triggered);
    ptimer_free(ptimer);
}

static void check_on_the_fly_mode_change(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool wrap_policy = (*policy & PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD);
    bool no_round_down = (*policy & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 10, 1);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 9 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 1 : 0);
    g_assert_false(triggered);

    ptimer_run(ptimer, 0);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 1 : 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    wrap_policy ? 0 : (no_round_down ? 10 : 9));
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 * 9);

    ptimer_run(ptimer, 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                     (no_round_down ? 1 : 0) + (wrap_policy ? 1 : 0));
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 3);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);
    ptimer_free(ptimer);
}

static void check_on_the_fly_period_change(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool no_round_down = (*policy & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 8, 1);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 4 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 4 : 3);
    g_assert_false(triggered);

    ptimer_set_period(ptimer, 4000000);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 4 : 3);

    qemu_clock_step(4000000 * 2 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 2 : 0);
    g_assert_false(triggered);

    qemu_clock_step(4000000 * 2);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);
    ptimer_free(ptimer);
}

static void check_on_the_fly_freq_change(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool no_round_down = (*policy & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);

    triggered = false;

    ptimer_set_freq(ptimer, 500);
    ptimer_set_limit(ptimer, 8, 1);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 4 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 4 : 3);
    g_assert_false(triggered);

    ptimer_set_freq(ptimer, 250);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 4 : 3);

    qemu_clock_step(2000000 * 4 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 2 : 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 4);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);
    ptimer_free(ptimer);
}

static void check_run_with_period_0(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_count(ptimer, 99);
    ptimer_run(ptimer, 1);

    qemu_clock_step(10 * NANOSECONDS_PER_SECOND);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 99);
    g_assert_false(triggered);
    ptimer_free(ptimer);
}

static void check_run_with_delta_0(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool wrap_policy = (*policy & PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD);
    bool no_immediate_trigger = (*policy & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER);
    bool no_immediate_reload = (*policy & PTIMER_POLICY_NO_IMMEDIATE_RELOAD);
    bool no_round_down = (*policy & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
    bool trig_only_on_dec = (*policy & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 99, 0);
    ptimer_run(ptimer, 1);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                     no_immediate_reload ? 0 : 99);

    if (no_immediate_trigger || trig_only_on_dec) {
        g_assert_false(triggered);
    } else {
        g_assert_true(triggered);
    }

    triggered = false;

    if (no_immediate_trigger || no_immediate_reload) {
        qemu_clock_step(2000000 + 1);

        g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                         no_immediate_reload ? 0 : (no_round_down ? 98 : 97));

        if (no_immediate_trigger && no_immediate_reload) {
            g_assert_true(triggered);

            triggered = false;
        } else {
            g_assert_false(triggered);
        }

        ptimer_set_count(ptimer, 99);
        ptimer_run(ptimer, 1);
    }

    qemu_clock_step(2000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 98 : 97);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 97);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 1 : 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 2);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    triggered = false;

    ptimer_set_count(ptimer, 0);
    ptimer_run(ptimer, 0);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                     no_immediate_reload ? 0 : 99);

    if (no_immediate_trigger || trig_only_on_dec) {
        g_assert_false(triggered);
    } else {
        g_assert_true(triggered);
    }

    triggered = false;

    qemu_clock_step(1);

    if (no_immediate_reload) {
        qemu_clock_step(2000000);
    }

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 99 : 98);

    if (no_immediate_reload && no_immediate_trigger) {
        g_assert_true(triggered);
    } else {
        g_assert_false(triggered);
    }

    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, no_round_down ? 98 : 97);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 98);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==,
                    wrap_policy ? 0 : (no_round_down ? 99 : 98));
    g_assert_true(triggered);

    ptimer_stop(ptimer);
    ptimer_free(ptimer);
}

static void check_periodic_with_load_0(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool continuous_trigger = (*policy & PTIMER_POLICY_CONTINUOUS_TRIGGER);
    bool no_immediate_trigger = (*policy & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER);
    bool trig_only_on_dec = (*policy & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_run(ptimer, 0);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);

    if (no_immediate_trigger || trig_only_on_dec) {
        g_assert_false(triggered);
    } else {
        g_assert_true(triggered);
    }

    triggered = false;

    qemu_clock_step(2000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);

    if (continuous_trigger || no_immediate_trigger) {
        g_assert_true(triggered);
    } else {
        g_assert_false(triggered);
    }

    triggered = false;

    ptimer_set_count(ptimer, 10);
    ptimer_run(ptimer, 0);

    qemu_clock_step(2000000 * 10 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);

    if (continuous_trigger) {
        g_assert_true(triggered);
    } else {
        g_assert_false(triggered);
    }

    ptimer_stop(ptimer);
    ptimer_free(ptimer);
}

static void check_oneshot_with_load_0(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);
    bool no_immediate_trigger = (*policy & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER);
    bool trig_only_on_dec = (*policy & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_run(ptimer, 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);

    if (no_immediate_trigger || trig_only_on_dec) {
        g_assert_false(triggered);
    } else {
        g_assert_true(triggered);
    }

    triggered = false;

    qemu_clock_step(2000000 + 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);

    if (no_immediate_trigger) {
        g_assert_true(triggered);
    } else {
        g_assert_false(triggered);
    }

    ptimer_free(ptimer);
}

static void add_ptimer_tests(uint8_t policy)
{
    char policy_name[256] = "";
    char *tmp;

    if (policy == PTIMER_POLICY_DEFAULT) {
        g_sprintf(policy_name, "default");
    }

    if (policy & PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD) {
        g_strlcat(policy_name, "wrap_after_one_period,", 256);
    }

    if (policy & PTIMER_POLICY_CONTINUOUS_TRIGGER) {
        g_strlcat(policy_name, "continuous_trigger,", 256);
    }

    if (policy & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER) {
        g_strlcat(policy_name, "no_immediate_trigger,", 256);
    }

    if (policy & PTIMER_POLICY_NO_IMMEDIATE_RELOAD) {
        g_strlcat(policy_name, "no_immediate_reload,", 256);
    }

    if (policy & PTIMER_POLICY_NO_COUNTER_ROUND_DOWN) {
        g_strlcat(policy_name, "no_counter_rounddown,", 256);
    }

    if (policy & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT) {
        g_strlcat(policy_name, "trigger_only_on_decrement,", 256);
    }

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/set_count policy=%s", policy_name),
        g_memdup(&policy, 1), check_set_count, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/set_limit policy=%s", policy_name),
        g_memdup(&policy, 1), check_set_limit, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/oneshot policy=%s", policy_name),
        g_memdup(&policy, 1), check_oneshot, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/periodic policy=%s", policy_name),
        g_memdup(&policy, 1), check_periodic, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/on_the_fly_mode_change policy=%s",
                              policy_name),
        g_memdup(&policy, 1), check_on_the_fly_mode_change, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/on_the_fly_period_change policy=%s",
                              policy_name),
        g_memdup(&policy, 1), check_on_the_fly_period_change, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/on_the_fly_freq_change policy=%s",
                              policy_name),
        g_memdup(&policy, 1), check_on_the_fly_freq_change, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/run_with_period_0 policy=%s",
                              policy_name),
        g_memdup(&policy, 1), check_run_with_period_0, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/run_with_delta_0 policy=%s",
                              policy_name),
        g_memdup(&policy, 1), check_run_with_delta_0, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/periodic_with_load_0 policy=%s",
                              policy_name),
        g_memdup(&policy, 1), check_periodic_with_load_0, g_free);
    g_free(tmp);

    g_test_add_data_func_full(
        tmp = g_strdup_printf("/ptimer/oneshot_with_load_0 policy=%s",
                              policy_name),
        g_memdup(&policy, 1), check_oneshot_with_load_0, g_free);
    g_free(tmp);
}

static void add_all_ptimer_policies_comb_tests(void)
{
    int last_policy = PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT;
    int policy = PTIMER_POLICY_DEFAULT;

    for (; policy < (last_policy << 1); policy++) {
        if ((policy & PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT) &&
            (policy & PTIMER_POLICY_NO_IMMEDIATE_TRIGGER)) {
            /* Incompatible policy flag settings -- don't try to test them */
            continue;
        }
        add_ptimer_tests(policy);
    }
}

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < QEMU_CLOCK_MAX; i++) {
        main_loop_tlg.tl[i] = g_new0(QEMUTimerList, 1);
    }

    add_all_ptimer_policies_comb_tests();

    qtest_allowed = true;

    return g_test_run();
}
