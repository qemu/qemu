/*
 * Boot order test cases.
 *
 * Copyright (c) 2013 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include "libqtest.h"

static void test_pc_cmos_byte(int reg, int expected)
{
    int actual;

    outb(0x70, reg);
    actual = inb(0x71);
    g_assert_cmphex(actual, ==, expected);
}

static void test_pc_cmos(uint8_t boot1, uint8_t boot2)
{
    test_pc_cmos_byte(0x38, boot1);
    test_pc_cmos_byte(0x3d, boot2);
}

static void test_pc_with_args(const char *test_args,
                              uint8_t boot1, uint8_t boot2,
                              uint8_t reboot1, uint8_t reboot2)
{
    char *args = g_strdup_printf("-nodefaults -display none %s", test_args);

    qtest_start(args);
    test_pc_cmos(boot1, boot2);
    qmp("{ 'execute': 'system_reset' }");
    /*
     * system_reset only requests reset.  We get a RESET event after
     * the actual reset completes.  Need to wait for that.
     */
    qmp("");                    /* HACK: wait for event */
    test_pc_cmos(reboot1, reboot2);
    qtest_quit(global_qtest);
    g_free(args);
}

static void test_pc_boot_order(void)
{
    test_pc_with_args("", 0x30, 0x12, 0x30, 0x12);
    test_pc_with_args("-no-fd-bootchk", 0x31, 0x12, 0x31, 0x12);
    test_pc_with_args("-boot c", 0, 0x02, 0, 0x02);
    test_pc_with_args("-boot nda", 0x10, 0x34, 0x10, 0x34);
    test_pc_with_args("-boot order=", 0, 0, 0, 0);
    test_pc_with_args("-boot order= -boot order=c", 0, 0x02, 0, 0x02);
    test_pc_with_args("-boot once=a", 0, 0x01, 0x30, 0x12);
    test_pc_with_args("-boot once=a -no-fd-bootchk", 0x01, 0x01, 0x31, 0x12);
    test_pc_with_args("-boot once=a,order=c", 0, 0x01, 0, 0x02);
    test_pc_with_args("-boot once=d -boot order=nda", 0, 0x03, 0x10, 0x34);
    test_pc_with_args("-boot once=a -boot once=b -boot once=c",
                      0, 0x02, 0x30, 0x12);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("boot-order/pc", test_pc_boot_order);

    return g_test_run();
}
