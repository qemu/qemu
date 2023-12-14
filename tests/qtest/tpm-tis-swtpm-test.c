/*
 * QTest testcase for TPM TIS talking to external swtpm and swtpm migration
 *
 * Copyright (c) 2018 IBM Corporation
 *  with parts borrowed from migration-test.c that is:
 *     Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "qemu/module.h"
#include "tpm-tests.h"
#include "tpm-tis-util.h"
#include "hw/acpi/tpm.h"

uint64_t tpm_tis_base_addr = TPM_TIS_ADDR_BASE;

typedef struct TestState {
    char *src_tpm_path;
    char *dst_tpm_path;
    char *uri;
} TestState;

static void tpm_tis_swtpm_test(const void *data)
{
    const TestState *ts = data;

    tpm_test_swtpm_test(ts->src_tpm_path, tpm_tis_transfer,
                        "tpm-tis", NULL);
}

static void tpm_tis_swtpm_migration_test(const void *data)
{
    const TestState *ts = data;

    tpm_test_swtpm_migration_test(ts->src_tpm_path, ts->dst_tpm_path, ts->uri,
                                  tpm_tis_transfer, "tpm-tis", NULL);
}

int main(int argc, char **argv)
{
    int ret;
    TestState ts = { 0 };

    ts.src_tpm_path = g_dir_make_tmp("qemu-tpm-tis-swtpm-test.XXXXXX", NULL);
    ts.dst_tpm_path = g_dir_make_tmp("qemu-tpm-tis-swtpm-test.XXXXXX", NULL);
    ts.uri = g_strdup_printf("unix:%s/migsocket", ts.src_tpm_path);

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/tpm/tis-swtpm/test", &ts, tpm_tis_swtpm_test);
    qtest_add_data_func("/tpm/tis-swtpm-migration/test", &ts,
                        tpm_tis_swtpm_migration_test);
    ret = g_test_run();

    tpm_util_rmdir(ts.dst_tpm_path);
    g_free(ts.dst_tpm_path);
    tpm_util_rmdir(ts.src_tpm_path);
    g_free(ts.src_tpm_path);
    g_free(ts.uri);

    return ret;
}
