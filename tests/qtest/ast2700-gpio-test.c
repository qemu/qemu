/*
 * QTest testcase for the ASPEED AST2700 GPIO Controller.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 ASPEED Technology Inc.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qobject/qdict.h"
#include "libqtest-single.h"

#define AST2700_GPIO_BASE 0x14C0B000
#define GPIOA0_CONTROL 0x180

static void test_output_pins(const char *machine, const uint32_t base)
{
    QTestState *s = qtest_init(machine);
    uint32_t offset = 0;
    uint32_t value = 0;
    uint32_t pin = 0;

    for (char c = 'A'; c <= 'D'; c++) {
        for (int i = 0; i < 8; i++) {
            offset = base + (pin * 4);

            /* output direction and output hi */
            qtest_writel(s, offset, 0x00000003);
            value = qtest_readl(s, offset);
            g_assert_cmphex(value, ==, 0x00000003);

            /* output direction and output low */
            qtest_writel(s, offset, 0x00000002);
            value = qtest_readl(s, offset);
            g_assert_cmphex(value, ==, 0x00000002);
            pin++;
        }
    }

    qtest_quit(s);
}

static void test_input_pins(const char *machine, const uint32_t base)
{
    QTestState *s = qtest_init(machine);
    char name[16];
    uint32_t offset = 0;
    uint32_t value = 0;
    uint32_t pin = 0;

    for (char c = 'A'; c <= 'D'; c++) {
        for (int i = 0; i < 8; i++) {
            sprintf(name, "gpio%c%d", c, i);
            offset = base + (pin * 4);
            /* input direction */
            qtest_writel(s, offset, 0);

            /* set input */
            qtest_qom_set_bool(s, "/machine/soc/gpio", name, true);
            value = qtest_readl(s, offset);
            g_assert_cmphex(value, ==, 0x00002000);

            /* clear input */
            qtest_qom_set_bool(s, "/machine/soc/gpio", name, false);
            value = qtest_readl(s, offset);
            g_assert_cmphex(value, ==, 0);
            pin++;
        }
    }

    qtest_quit(s);
}

static void test_2700_input_pins(void)
{
    test_input_pins("-machine ast2700-evb",
                    AST2700_GPIO_BASE + GPIOA0_CONTROL);
}

static void test_2700_output_pins(void)
{
    test_output_pins("-machine ast2700-evb",
                     AST2700_GPIO_BASE + GPIOA0_CONTROL);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ast2700/gpio/input_pins", test_2700_input_pins);
    qtest_add_func("/ast2700/gpio/output_pins", test_2700_output_pins);

    return g_test_run();
}
