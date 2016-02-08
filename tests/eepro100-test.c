/*
 * QTest testcase for eepro100 NIC
 *
 * Copyright (c) 2013-2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "libqtest.h"

static void test_device(gconstpointer data)
{
    const char *model = data;
    QTestState *s;
    char *args;

    args = g_strdup_printf("-device %s", model);
    s = qtest_start(args);

    /* Tests only initialization so far. TODO: Implement functional tests */

    if (s) {
        qtest_quit(s);
    }
    g_free(args);
}

static const char *models[] = {
    "i82550",
    "i82551",
    "i82557a",
    "i82557b",
    "i82557c",
    "i82558a",
    "i82558b",
    "i82559a",
    "i82559b",
    "i82559c",
    "i82559er",
    "i82562",
    "i82801",
};

int main(int argc, char **argv)
{
    int i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(models); i++) {
        char *path;

        path = g_strdup_printf("eepro100/%s", models[i]);
        qtest_add_data_func(path, models[i], test_device);
    }

    return g_test_run();
}
