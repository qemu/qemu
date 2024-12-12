/*
 * QTest testcases for CPR
 *
 * Copyright (c) 2016-2018 Red Hat, Inc. and/or its affiliates
 *   based on the vhost-user-test.c that is:
 *      Copyright (c) 2014 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"


static char *tmpfs;

static void *migrate_hook_start_mode_reboot(QTestState *from, QTestState *to)
{
    migrate_set_parameter_str(from, "mode", "cpr-reboot");
    migrate_set_parameter_str(to, "mode", "cpr-reboot");

    migrate_set_capability(from, "x-ignore-shared", true);
    migrate_set_capability(to, "x-ignore-shared", true);

    return NULL;
}

static void test_mode_reboot(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .start.use_shmem = true,
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_mode_reboot,
    };

    test_file_common(&args, true);
}

void migration_test_add_cpr(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    /*
     * Our CI system has problems with shared memory.
     * Don't run this test until we find a workaround.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        migration_test_add("/migration/mode/reboot", test_mode_reboot);
    }
}
