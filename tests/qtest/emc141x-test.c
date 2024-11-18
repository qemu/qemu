/*
 * QTest testcase for the EMC141X temperature sensor
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest-single.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"
#include "qobject/qdict.h"
#include "hw/sensor/emc141x_regs.h"

#define EMC1414_TEST_ID   "emc1414-test"

static int qmp_emc1414_get_temperature(const char *id)
{
    QDict *response;
    int ret;

    response = qmp("{ 'execute': 'qom-get', 'arguments': { 'path': %s, "
                   "'property': 'temperature0' } }", id);
    g_assert(qdict_haskey(response, "return"));
    ret = qdict_get_int(response, "return");
    qobject_unref(response);
    return ret;
}

static void qmp_emc1414_set_temperature(const char *id, int value)
{
    QDict *response;

    response = qmp("{ 'execute': 'qom-set', 'arguments': { 'path': %s, "
                   "'property': 'temperature0', 'value': %d } }", id, value);
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void send_and_receive(void *obj, void *data, QGuestAllocator *alloc)
{
    uint16_t value;
    QI2CDevice *i2cdev = (QI2CDevice *)obj;

    value = qmp_emc1414_get_temperature(EMC1414_TEST_ID);
    g_assert_cmpuint(value, ==, 0);

    value = i2c_get8(i2cdev, EMC141X_TEMP_HIGH0);
    g_assert_cmphex(value, ==, 0);

    /* The default max value is 85C, 0x55=85 */
    value = i2c_get8(i2cdev, EMC141X_TEMP_MAX_HIGH0);
    g_assert_cmphex(value, ==, 0x55);

    value = i2c_get8(i2cdev, EMC141X_TEMP_MIN_HIGH0);
    g_assert_cmphex(value, ==, 0);

    /* 3000mc = 30C */
    qmp_emc1414_set_temperature(EMC1414_TEST_ID, 30000);
    value = qmp_emc1414_get_temperature(EMC1414_TEST_ID);
    g_assert_cmpuint(value, ==, 30000);

    value = i2c_get8(i2cdev, EMC141X_TEMP_HIGH0);
    g_assert_cmphex(value, ==, 30);

}

static void emc1414_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "id=" EMC1414_TEST_ID ",address=0x70"
    };
    add_qi2c_address(&opts, &(QI2CAddress) { 0x70 });

    qos_node_create_driver("emc1414", i2c_device_create);
    qos_node_consumes("emc1414", "i2c-bus", &opts);

    qos_add_test("tx-rx", "emc1414", send_and_receive, NULL);
}
libqos_init(emc1414_register_nodes);
