/*
 * blockdev.c test cases
 *
 * Copyright (C) 2013 Red Hat Inc.
 *
 * Authors:
 *  Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include "libqtest.h"

static void test_drive_add_empty(void)
{
    QDict *response;
    const char *response_return;

    /* Start with an empty drive */
    qtest_start("-drive if=none,id=drive0");

    /* Delete the drive */
    response = qmp("{\"execute\": \"human-monitor-command\","
                   " \"arguments\": {"
                   "   \"command-line\": \"drive_del drive0\""
                   "}}");
    g_assert(response);
    response_return = qdict_get_try_str(response, "return");
    g_assert(response_return);
    g_assert(strcmp(response_return, "") == 0);
    QDECREF(response);

    /* Ensure re-adding the drive works - there should be no duplicate ID error
     * because the old drive must be gone.
     */
    response = qmp("{\"execute\": \"human-monitor-command\","
                   " \"arguments\": {"
                   "   \"command-line\": \"drive_add 0 if=none,id=drive0\""
                   "}}");
    g_assert(response);
    response_return = qdict_get_try_str(response, "return");
    g_assert(response_return);
    g_assert(strcmp(response_return, "OK\r\n") == 0);
    QDECREF(response);

    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/qmp/drive_add_empty", test_drive_add_empty);

    return g_test_run();
}
