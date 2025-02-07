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

static void *test_mode_transfer_start(QTestState *from, QTestState *to)
{
    migrate_set_parameter_str(from, "mode", "cpr-transfer");
    return NULL;
}

/*
 * cpr-transfer mode cannot use the target monitor prior to starting the
 * migration, and cannot connect synchronously to the monitor, so defer
 * the target connection.
 */
static void test_mode_transfer_common(bool incoming_defer)
{
    g_autofree char *cpr_path = g_strdup_printf("%s/cpr.sock", tmpfs);
    g_autofree char *mig_path = g_strdup_printf("%s/migsocket", tmpfs);
    g_autofree char *uri = g_strdup_printf("unix:%s", mig_path);

    const char *opts = "-machine aux-ram-share=on -nodefaults";
    g_autofree const char *cpr_channel = g_strdup_printf(
        "cpr,addr.transport=socket,addr.type=unix,addr.path=%s",
        cpr_path);
    g_autofree char *opts_target = g_strdup_printf("-incoming %s %s",
                                                   cpr_channel, opts);

    g_autofree char *connect_channels = g_strdup_printf(
        "[ { 'channel-type': 'main',"
        "    'addr': { 'transport': 'socket',"
        "              'type': 'unix',"
        "              'path': '%s' } } ]",
        mig_path);

    MigrateCommon args = {
        .start.opts_source = opts,
        .start.opts_target = opts_target,
        .start.defer_target_connect = true,
        .start.memory_backend = "-object memory-backend-memfd,id=pc.ram,size=%s"
                                " -machine memory-backend=pc.ram",
        .listen_uri = incoming_defer ? "defer" : uri,
        .connect_channels = connect_channels,
        .cpr_channel = cpr_channel,
        .start_hook = test_mode_transfer_start,
    };

    test_precopy_common(&args);
}

static void test_mode_transfer(void)
{
    test_mode_transfer_common(NULL);
}

static void test_mode_transfer_defer(void)
{
    test_mode_transfer_common(true);
}

void migration_test_add_cpr(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    /* no tests in the smoke set for now */

    if (!env->full_set) {
        return;
    }

    /*
     * Our CI system has problems with shared memory.
     * Don't run this test until we find a workaround.
     */
    if (getenv("QEMU_TEST_FLAKY_TESTS")) {
        migration_test_add("/migration/mode/reboot", test_mode_reboot);
    }

    if (env->has_kvm) {
        migration_test_add("/migration/mode/transfer", test_mode_transfer);
        migration_test_add("/migration/mode/transfer/defer",
                           test_mode_transfer_defer);
    }
}
