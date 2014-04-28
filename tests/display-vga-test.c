/*
 * QTest testcase for vga cards
 *
 * Copyright (c) 2014 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include "libqtest.h"
#include "qemu/osdep.h"

static void pci_cirrus(void)
{
    qtest_start("-vga none -device cirrus-vga");
    qtest_end();
}

static void pci_stdvga(void)
{
    qtest_start("-vga none -device VGA");
    qtest_end();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/display/pci/cirrus", pci_cirrus);
    qtest_add_func("/display/pci/stdvga", pci_stdvga);
    ret = g_test_run();

    return ret;
}
