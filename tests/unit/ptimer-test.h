/*
 * QTest testcase for the ptimer
 *
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef PTIMER_TEST_H
#define PTIMER_TEST_H

extern bool qtest_allowed;

extern int64_t ptimer_test_time_ns;

struct QEMUTimerList {
    QEMUTimer active_timers;
};

#endif
