/*
 * QTest testcase for the Aspeed GPIO Controller.
 *
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qobject/qdict.h"
#include "libqtest-single.h"

#define AST2600_GPIO_BASE 0x1E780000

#define GPIO_ABCD_DATA_VALUE 0x000
#define GPIO_ABCD_DIRECTION  0x004

static void test_set_colocated_pins(const void *data)
{
    QTestState *s = (QTestState *)data;

    /*
     * gpioV4-7 occupy bits within a single 32-bit value, so we want to make
     * sure that modifying one doesn't affect the other.
     */
    qtest_qom_set_bool(s, "/machine/soc/gpio", "gpioV4", true);
    qtest_qom_set_bool(s, "/machine/soc/gpio", "gpioV5", false);
    qtest_qom_set_bool(s, "/machine/soc/gpio", "gpioV6", true);
    qtest_qom_set_bool(s, "/machine/soc/gpio", "gpioV7", false);
    g_assert(qtest_qom_get_bool(s, "/machine/soc/gpio", "gpioV4"));
    g_assert(!qtest_qom_get_bool(s, "/machine/soc/gpio", "gpioV5"));
    g_assert(qtest_qom_get_bool(s, "/machine/soc/gpio", "gpioV6"));
    g_assert(!qtest_qom_get_bool(s, "/machine/soc/gpio", "gpioV7"));
}

static void test_set_input_pins(const void *data)
{
    QTestState *s = (QTestState *)data;
    char name[16];
    uint32_t value;

    qtest_writel(s, AST2600_GPIO_BASE + GPIO_ABCD_DIRECTION, 0x00000000);
    for (char c = 'A'; c <= 'D'; c++) {
        for (int i = 0; i < 8; i++) {
            sprintf(name, "gpio%c%d", c, i);
            qtest_qom_set_bool(s, "/machine/soc/gpio", name, true);
        }
    }
    value = qtest_readl(s, AST2600_GPIO_BASE + GPIO_ABCD_DATA_VALUE);
    g_assert_cmphex(value, ==, 0xffffffff);

    qtest_writel(s, AST2600_GPIO_BASE + GPIO_ABCD_DATA_VALUE, 0x00000000);
    value = qtest_readl(s, AST2600_GPIO_BASE + GPIO_ABCD_DATA_VALUE);
    g_assert_cmphex(value, ==, 0xffffffff);
}

int main(int argc, char **argv)
{
    QTestState *s;
    int r;

    g_test_init(&argc, &argv, NULL);

    s = qtest_init("-machine ast2600-evb");
    qtest_add_data_func("/ast2600/gpio/set_colocated_pins", s,
                        test_set_colocated_pins);
    qtest_add_data_func("/ast2600/gpio/set_input_pins", s, test_set_input_pins);
    r = g_test_run();
    qtest_quit(s);

    return r;
}
