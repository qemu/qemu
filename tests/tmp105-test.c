/*
 * QTest testcase for the TMP105 temperature sensor
 *
 * Copyright (c) 2012 Andreas Färber
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "libqos/i2c.h"
#include "qapi/qmp/qdict.h"
#include "hw/misc/tmp105_regs.h"

#define TMP105_TEST_ID   "tmp105-test"
#define TMP105_TEST_ADDR 0x49

static I2CAdapter *i2c;

static int qmp_tmp105_get_temperature(const char *id)
{
    QDict *response;
    int ret;

    response = qmp("{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
                   "'property': 'temperature' } }", id);
    g_assert(qdict_haskey(response, "return"));
    ret = qdict_get_int(response, "return");
    qobject_unref(response);
    return ret;
}

static void qmp_tmp105_set_temperature(const char *id, int value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': 'temperature', 'value': %d } }", id, value);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

#define TMP105_PRECISION (1000/16)
static void send_and_receive(void)
{
    uint16_t value;

    value = qmp_tmp105_get_temperature(TMP105_TEST_ID);
    g_assert_cmpuint(value, ==, 0);

    value = i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmphex(value, ==, 0);

    qmp_tmp105_set_temperature(TMP105_TEST_ID, 20000);
    value = qmp_tmp105_get_temperature(TMP105_TEST_ID);
    g_assert_cmpuint(value, ==, 20000);

    value = i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmphex(value, ==, 0x1400);

    qmp_tmp105_set_temperature(TMP105_TEST_ID, 20938); /* 20 + 15/16 */
    value = qmp_tmp105_get_temperature(TMP105_TEST_ID);
    g_assert_cmpuint(value, >=, 20938 - TMP105_PRECISION/2);
    g_assert_cmpuint(value, <, 20938 + TMP105_PRECISION/2);

    /* Set config */
    i2c_set8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG, 0x60);
    value = i2c_get8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG);
    g_assert_cmphex(value, ==, 0x60);

    value = i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmphex(value, ==, 0x14f0);

    /* Set precision to 9, 10, 11 bits.  */
    i2c_set8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG, 0x00);
    g_assert_cmphex(i2c_get8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG), ==, 0x00);
    value = i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmphex(value, ==, 0x1480);

    i2c_set8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG, 0x20);
    g_assert_cmphex(i2c_get8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG), ==, 0x20);
    value = i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmphex(value, ==, 0x14c0);

    i2c_set8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG, 0x40);
    g_assert_cmphex(i2c_get8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG), ==, 0x40);
    value = i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmphex(value, ==, 0x14e0);

    /* stored precision remains the same */
    value = qmp_tmp105_get_temperature(TMP105_TEST_ID);
    g_assert_cmpuint(value, >=, 20938 - TMP105_PRECISION/2);
    g_assert_cmpuint(value, <, 20938 + TMP105_PRECISION/2);

    i2c_set8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG, 0x60);
    g_assert_cmphex(i2c_get8(i2c, TMP105_TEST_ADDR, TMP105_REG_CONFIG), ==, 0x60);
    value = i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_TEMPERATURE);
    g_assert_cmphex(value, ==, 0x14f0);

    i2c_set16(i2c, TMP105_TEST_ADDR, TMP105_REG_T_LOW, 0x1234);
    g_assert_cmphex(i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_T_LOW), ==, 0x1234);
    i2c_set16(i2c, TMP105_TEST_ADDR, TMP105_REG_T_HIGH, 0x4231);
    g_assert_cmphex(i2c_get16(i2c, TMP105_TEST_ADDR, TMP105_REG_T_HIGH), ==, 0x4231);
}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    s = qtest_start("-machine n800 "
                    "-device tmp105,bus=i2c-bus.0,id=" TMP105_TEST_ID
                    ",address=0x49");
    i2c = omap_i2c_create(s, OMAP2_I2C_1_BASE);

    qtest_add_func("/tmp105/tx-rx", send_and_receive);

    ret = g_test_run();

    qtest_quit(s);
    g_free(i2c);

    return ret;
}
