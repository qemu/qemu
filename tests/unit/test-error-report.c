/*
 * Error reporting test
 *
 * Copyright (C) 2022 Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "glib-compat.h"
#include <locale.h>

#include "qemu/error-report.h"
#include "qapi/error.h"

static void
test_error_report_simple(void)
{
    if (g_test_subprocess()) {
        error_report("%s", "test error");
        warn_report("%s", "test warn");
        info_report("%s", "test info");
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("\
test-error-report: test error*\
test-error-report: warning: test warn*\
test-error-report: info: test info*\
");
}

static void
test_error_report_loc(void)
{
    if (g_test_subprocess()) {
        loc_set_file("some-file.c", 7717);
        error_report("%s", "test error1");
        loc_set_none();
        error_report("%s", "test error2");
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("\
test-error-report:some-file.c:7717: test error1*\
test-error-report: test error2*\
");
}

static void
test_error_report_glog(void)
{
    if (g_test_subprocess()) {
        g_message("gmessage");
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("test-error-report: info: gmessage*");
}

static void
test_error_report_once(void)
{
    int i;

    if (g_test_subprocess()) {
        for (i = 0; i < 3; i++) {
            warn_report_once("warn");
            error_report_once("err");
        }
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("\
test-error-report: warning: warn*\
test-error-report: err*\
");
}

static void
test_error_report_timestamp(void)
{
    if (g_test_subprocess()) {
        message_with_timestamp = true;
        warn_report("warn");
        error_report("err");
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("\
*-*-*:*:* test-error-report: warning: warn*\
*-*-*:*:* test-error-report: err*\
");
}

static void
test_error_warn(void)
{
    if (g_test_subprocess()) {
        error_setg(&error_warn, "Testing &error_warn");
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
    g_test_trap_assert_stderr("\
test-error-report: warning: Testing &error_warn*\
");
}


int
main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    g_test_init(&argc, &argv, NULL);
    error_init("test-error-report");

    g_test_add_func("/error-report/simple", test_error_report_simple);
    g_test_add_func("/error-report/loc", test_error_report_loc);
    g_test_add_func("/error-report/glog", test_error_report_glog);
    g_test_add_func("/error-report/once", test_error_report_once);
    g_test_add_func("/error-report/timestamp", test_error_report_timestamp);
    g_test_add_func("/error-report/warn", test_error_warn);

    return g_test_run();
}
