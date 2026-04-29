/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcase for TPM TIS over I2C talking to external swtpm
 *
 * Copyright (c) 2018, 2026 IBM Corporation
 *  with parts borrowed from migration-test.c that is:
 *     Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.ibm.com>
 *
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "qemu/module.h"
#include "tpm-tests.h"
#include "tpm-tis-i2c-util.h"
#include "qtest_aspeed.h"

typedef struct TestState {
    char *src_tpm_path;
    char *dst_tpm_path;
    char *uri;
    const char *machine_options;
    char *ifmodel;
} TestState;

static void tpm_tis_i2c_swtpm_test(const void *data)
{
    const TestState *ts = data;

    tpm_test_swtpm_test(ts->src_tpm_path, tpm_tis_i2c_transfer,
                        ts->ifmodel, ts->machine_options);
}

static void tpm_tis_swtpm_migration_test(const void *data)
{
    const TestState *ts = data;

    tpm_test_swtpm_migration_test(ts->src_tpm_path, ts->dst_tpm_path,
                                  ts->uri, tpm_tis_i2c_transfer,
                                  ts->ifmodel, ts->machine_options);
}


int main(int argc, char **argv)
{
    int ret;
    TestState ts;

    ts.src_tpm_path = g_dir_make_tmp("qemu-tpm-tis-i2c-swtpm-test.XXXXXX",
                                     NULL);
    ts.dst_tpm_path = g_dir_make_tmp("qemu-tpm-tis-i2c-swtpm-test.XXXXXX",
                                     NULL);
    ts.uri = g_strdup_printf("unix:%s/migsocket", ts.src_tpm_path);
    ts.machine_options = "-machine rainier-bmc -accel tcg";
    ts.ifmodel = g_strdup_printf(
                            "tpm-tis-i2c,bus=aspeed.i2c.bus.%d,address=0x%x",
                            I2C_DEV_BUS_NUM, I2C_SLAVE_ADDR);

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    aspeed_bus_addr = ast2600_i2c_calc_bus_addr(I2C_DEV_BUS_NUM);

    qtest_add_data_func("/tpm/tis-i2c-swtpm/test", &ts, tpm_tis_i2c_swtpm_test);
    qtest_add_data_func("/tpm/tis-i2c-swtpm-migration/test", &ts,
                        tpm_tis_swtpm_migration_test);
    ret = g_test_run();

    tpm_util_rmdir(ts.dst_tpm_path);
    g_free(ts.dst_tpm_path);
    tpm_util_rmdir(ts.src_tpm_path);
    g_free(ts.src_tpm_path);
    g_free(ts.uri);
    g_free(ts.ifmodel);

    return ret;
}
