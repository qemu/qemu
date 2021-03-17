/*
 * QTest testcase for fuzz case
 *
 * Copyright (c) 2020 Li Qiang <liq3ea@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqos/libqtest.h"

static void test_lp1878642_pci_bus_get_irq_level_assert(void)
{
    QTestState *s;

    s = qtest_init("-M pc-q35-5.0 "
                   "-nographic -monitor none -serial none");

    qtest_outl(s, 0xcf8, 0x8400f841);
    qtest_outl(s, 0xcfc, 0xebed205d);
    qtest_outl(s, 0x5d02, 0xebed205d);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("fuzz/test_lp1878642_pci_bus_get_irq_level_assert",
                       test_lp1878642_pci_bus_get_irq_level_assert);
    }

    return g_test_run();
}
