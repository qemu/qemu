/*
 * QTest testcase for e1000 NIC
 *
 * Copyright (c) 2013-2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void test_device(gconstpointer data)
{
    const char *model = data;
    QTestState *s;
    char *args;

    args = g_strdup_printf("-device %s", model);
    s = qtest_start(args);

    if (s) {
        qtest_quit(s);
    }
    g_free(args);
}

static const char *models[] = {
    "e1000",
    "e1000-82540em",
    "e1000-82544gc",
    "e1000-82545em",
};

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(models); i++) {
        char *path;

        path = g_strdup_printf("e1000/%s", models[i]);
        qtest_add_data_func(path, models[i], test_device);
    }

    return g_test_run();
}
