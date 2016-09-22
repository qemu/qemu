/*
 * QTest testcase for the ptimer
 *
 * Author: Dmitry Osipenko <digetx@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <glib/gprintf.h>

#include "qemu/osdep.h"
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
}

static void check_oneshot(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_count(ptimer, 10);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 2 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 7);
    g_assert_false(triggered);

    ptimer_stop(ptimer);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 7);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 11);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 7);
    g_assert_false(triggered);

    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 7 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    qemu_clock_step(4000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 10);

    qemu_clock_step(20000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 10);
    g_assert_false(triggered);

    ptimer_set_limit(ptimer, 9, 1);

    qemu_clock_step(20000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 9);
    g_assert_false(triggered);

    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 7);
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 20);

    qemu_clock_step(2000000 * 19 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    ptimer_stop(ptimer);

    triggered = false;

    qemu_clock_step(2000000 * 12 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);
}

static void check_periodic(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 10, 1);
    ptimer_run(ptimer, 0);

    qemu_clock_step(2000000 * 10 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 9);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 8);
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 20);

    qemu_clock_step(2000000 * 11 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 8);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 10);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 8);
    g_assert_true(triggered);

    ptimer_stop(ptimer);
    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 8);
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 3);
    ptimer_run(ptimer, 0);

    qemu_clock_step(2000000 * 3 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 9);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 8);
    g_assert_false(triggered);

    ptimer_set_count(ptimer, 0);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 10);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 * 12 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 7);
    g_assert_true(triggered);

    ptimer_stop(ptimer);

    triggered = false;

    qemu_clock_step(2000000 * 12 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 7);
    g_assert_false(triggered);

    ptimer_run(ptimer, 0);
    ptimer_set_period(ptimer, 0);

    qemu_clock_step(2000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 7);
    g_assert_false(triggered);
}

static void check_on_the_fly_mode_change(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 10, 1);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 9 + 100000);

    ptimer_run(ptimer, 0);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 9);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 * 9);

    ptimer_run(ptimer, 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 3);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);
}

static void check_on_the_fly_period_change(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 8, 1);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 4 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 3);
    g_assert_false(triggered);

    ptimer_set_period(ptimer, 4000000);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 3);

    qemu_clock_step(4000000 * 2 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    qemu_clock_step(4000000 * 2);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);
}

static void check_on_the_fly_freq_change(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_freq(ptimer, 500);
    ptimer_set_limit(ptimer, 8, 1);
    ptimer_run(ptimer, 1);

    qemu_clock_step(2000000 * 4 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 3);
    g_assert_false(triggered);

    ptimer_set_freq(ptimer, 250);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 3);

    qemu_clock_step(2000000 * 4 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 4);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);
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
}

static void check_run_with_delta_0(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_set_limit(ptimer, 99, 0);
    ptimer_run(ptimer, 1);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 99);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 97);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 97);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 2);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    triggered = false;

    ptimer_set_count(ptimer, 0);
    ptimer_run(ptimer, 0);
    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 99);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 97);
    g_assert_false(triggered);

    qemu_clock_step(2000000 * 98);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 98);
    g_assert_true(triggered);

    ptimer_stop(ptimer);
}

static void check_periodic_with_load_0(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_run(ptimer, 0);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    ptimer_stop(ptimer);
}

static void check_oneshot_with_load_0(gconstpointer arg)
{
    const uint8_t *policy = arg;
    QEMUBH *bh = qemu_bh_new(ptimer_trigger, NULL);
    ptimer_state *ptimer = ptimer_init(bh, *policy);

    triggered = false;

    ptimer_set_period(ptimer, 2000000);
    ptimer_run(ptimer, 1);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_true(triggered);

    triggered = false;

    qemu_clock_step(2000000 + 100000);

    g_assert_cmpuint(ptimer_get_count(ptimer), ==, 0);
    g_assert_false(triggered);

    triggered = false;

    qemu_clock_step(2000000 + 100000);

    g_assert_false(triggered);
}

static void add_ptimer_tests(uint8_t policy)
{
    uint8_t *ppolicy = g_malloc(1);
    char *policy_name = g_malloc(64);

    *ppolicy = policy;

    if (policy == PTIMER_POLICY_DEFAULT) {
        g_sprintf(policy_name, "default");
    }

    qtest_add_data_func(
        g_strdup_printf("/ptimer/set_count policy=%s", policy_name),
        ppolicy, check_set_count);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/set_limit policy=%s", policy_name),
        ppolicy, check_set_limit);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/oneshot policy=%s", policy_name),
        ppolicy, check_oneshot);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/periodic policy=%s", policy_name),
        ppolicy, check_periodic);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/on_the_fly_mode_change policy=%s", policy_name),
        ppolicy, check_on_the_fly_mode_change);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/on_the_fly_period_change policy=%s", policy_name),
        ppolicy, check_on_the_fly_period_change);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/on_the_fly_freq_change policy=%s", policy_name),
        ppolicy, check_on_the_fly_freq_change);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/run_with_period_0 policy=%s", policy_name),
        ppolicy, check_run_with_period_0);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/run_with_delta_0 policy=%s", policy_name),
        ppolicy, check_run_with_delta_0);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/periodic_with_load_0 policy=%s", policy_name),
        ppolicy, check_periodic_with_load_0);

    qtest_add_data_func(
        g_strdup_printf("/ptimer/oneshot_with_load_0 policy=%s", policy_name),
        ppolicy, check_oneshot_with_load_0);
}

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < QEMU_CLOCK_MAX; i++) {
        main_loop_tlg.tl[i] = g_new0(QEMUTimerList, 1);
    }

    add_ptimer_tests(PTIMER_POLICY_DEFAULT);

    qtest_allowed = true;

    return g_test_run();
}
