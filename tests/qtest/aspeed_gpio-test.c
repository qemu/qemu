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

static uint32_t qtest_qom_get_uint32(QTestState *s, const char *path,
                                     const char *property)
{
    QDict *r;

    uint32_t res;
    r = qtest_qmp(s, "{ 'execute': 'qom-get', 'arguments': "
                     "{ 'path': %s, 'property': %s } }", path, property);
    res = qdict_get_uint(r, "return");
    qobject_unref(r);

    return res;
}

static void qtest_qom_set_uint32(QTestState *s, const char *path,
                                 const char *property, uint32_t value)
{
    QDict *r;

    r = qtest_qmp(s, "{ 'execute': 'qom-set', 'arguments': "
                     "{ 'path': %s, 'property': %s, 'value': %" PRIu32 " } }",
                     path, property, value);
    qobject_unref(r);
}

static const char *resp_get_error(QDict *r, const char* error_key)
{
    QDict *qdict;

    g_assert(r);

    qdict = qdict_get_qdict(r, "error");
    if (qdict) {
        return qdict_get_str(qdict, error_key);
    }

    return NULL;
}

static bool qtest_qom_check_error(QTestState *s, const char *path,
                                  const char *property, const char *error_msg,
                                  const char *error_msg_key)
{
    QDict *r;
    bool b;

    r = qtest_qmp(s, "{ 'execute': 'qom-get', 'arguments': "
                     "{ 'path': %s, 'property': %s } }", path, property);
    b = g_str_equal(resp_get_error(r, error_msg_key), error_msg);
    qobject_unref(r);

    return b;
}

static void test_set_colocated_pins(const void *data)
{
    QTestState *s = (QTestState *)data;
    const char path[] = "/machine/soc/gpio";
    /*
     * gpioV4-7 occupy bits within a single 32-bit value, so we want to make
     * sure that modifying one doesn't affect the other.
     */
    qtest_qom_set_bool(s, path, "gpioV4", true);
    qtest_qom_set_bool(s, path, "gpioV5", false);
    qtest_qom_set_bool(s, path, "gpioV6", true);
    qtest_qom_set_bool(s, path, "gpioV7", false);
    g_assert(qtest_qom_get_bool(s, path, "gpioV4"));
    g_assert(!qtest_qom_get_bool(s, path, "gpioV5"));
    g_assert(qtest_qom_get_bool(s, path, "gpioV6"));
    g_assert(!qtest_qom_get_bool(s, path, "gpioV7"));

    /*
     * Testing the gpio-set[%d] properties, using individual gpio boolean
     * properties to do cross check.
     * We use gpioR4-7 for test, Setting them to be 0b1010.
     */
    qtest_qom_set_uint32(s, path, "gpio-set[4]", 0x0);
    g_assert(qtest_qom_get_uint32(s, path, "gpio-set[4]") == 0x0);
    qtest_qom_set_uint32(s, path, "gpio-set[4]", 0xa000);
    g_assert(qtest_qom_get_uint32(s, path, "gpio-set[4]") == 0xa000);

    g_assert(!qtest_qom_get_bool(s, path, "gpioR4"));
    g_assert(qtest_qom_get_bool(s, path, "gpioR5"));
    g_assert(!qtest_qom_get_bool(s, path, "gpioR6"));
    g_assert(qtest_qom_get_bool(s, path, "gpioR7"));

    /*
     * Testing the invalid indexing, the response info should contain following
     * info:
     * {key: "class", value: "GenericError"}
     *
     * For pins, it should follow "gpio%2[A-Z]%1d" or "gpio%3[18A-E]%1d" format.
     */
    const char error_msg[] = "GenericError";
    const char error_msg_key[] = "class";

    g_assert(qtest_qom_check_error(s, path, "gpioR+1", error_msg,
                                   error_msg_key));
    g_assert(qtest_qom_check_error(s, path, "gpio-set[99]", error_msg,
                                   error_msg_key));
    g_assert(qtest_qom_check_error(s, path, "gpio-set[-3]", error_msg,
                                   error_msg_key));
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
