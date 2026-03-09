/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcases for COLO migration
 *
 * Copyright (c) 2025 Lukas Straub <lukasstraub2@web.de>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/framework.h"
#include "migration/migration-qmp.h"
#include "migration/migration-util.h"
#include "qemu/module.h"

static int test_colo_common(MigrateCommon *args,
                            bool failover_during_checkpoint,
                            bool primary_failover)
{
    QTestState *from, *to;
    void *data_hook = NULL;

    /*
     * For the COLO test, both VMs will run in parallel. Thus both VMs want to
     * open the image read/write at the same time. Using read-only=on is not
     * possible here, because ide-hd does not support read-only backing image.
     *
     * So use -snapshot, where each qemu instance creates its own writable
     * snapshot internally while leaving the real image read-only.
     */
    args->start.opts_source = "-snapshot";
    args->start.opts_target = "-snapshot";

    /*
     * COLO migration code logs many errors when the migration socket
     * is shut down, these are expected so we hide them here.
     */
    args->start.hide_stderr = true;

    /*
     * Test with yank with out of band capability since that is how it is
     * used in production.
     */
    args->start.oob = true;
    args->start.caps[MIGRATION_CAPABILITY_RETURN_PATH] = true;
    args->start.caps[MIGRATION_CAPABILITY_X_COLO] = true;

    if (migrate_start(&from, &to, args->listen_uri, &args->start)) {
        return -1;
    }

    migrate_set_parameter_int(from, "x-checkpoint-delay", 300);

    if (args->start_hook) {
        data_hook = args->start_hook(from, to);
    }

    migrate_ensure_converge(from);
    wait_for_serial("src_serial");

    migrate_qmp(from, to, args->connect_uri, NULL, "{}");

    wait_for_migration_status(from, "colo", NULL);
    wait_for_resume(to, get_dst());

    wait_for_serial("src_serial");
    wait_for_serial("dest_serial");

    /* wait for 3 checkpoints */
    for (int i = 0; i < 3; i++) {
        qtest_qmp_eventwait(to, "RESUME");
        wait_for_serial("src_serial");
        wait_for_serial("dest_serial");
    }

    if (failover_during_checkpoint) {
        qtest_qmp_eventwait(to, "STOP");
    }
    if (primary_failover) {
        qtest_qmp_assert_success(from, "{'exec-oob': 'yank', 'id': 'yank-cmd', "
                                            "'arguments': {'instances':"
                                                "[{'type': 'migration'}]}}");
        qtest_qmp_assert_success(from, "{'execute': 'x-colo-lost-heartbeat'}");
        wait_for_serial("src_serial");
    } else {
        qtest_qmp_assert_success(to, "{'exec-oob': 'yank', 'id': 'yank-cmd', "
                                        "'arguments': {'instances':"
                                            "[{'type': 'migration'}]}}");
        qtest_qmp_assert_success(to, "{'execute': 'x-colo-lost-heartbeat'}");
        wait_for_serial("dest_serial");
    }

    if (args->end_hook) {
        args->end_hook(from, to, data_hook);
    }

    migrate_end(from, to, !primary_failover);

    return 0;
}

static void test_colo_plain_common(MigrateCommon *args,
                                   bool failover_during_checkpoint,
                                   bool primary_failover)
{
    args->listen_uri = "tcp:127.0.0.1:0";
    test_colo_common(args, failover_during_checkpoint, primary_failover);
}

static void *hook_start_multifd(QTestState *from, QTestState *to)
{
    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "none");
}

static void test_colo_multifd_common(MigrateCommon *args,
                                     bool failover_during_checkpoint,
                                     bool primary_failover)
{
    args->listen_uri = "defer";
    args->start_hook = hook_start_multifd;
    args->start.caps[MIGRATION_CAPABILITY_MULTIFD] = true;
    test_colo_common(args, failover_during_checkpoint, primary_failover);
}

static void test_colo_plain_primary_failover(char *name, MigrateCommon *args)
{
    test_colo_plain_common(args, false, true);
}

static void test_colo_plain_secondary_failover(char *name, MigrateCommon *args)
{
    test_colo_plain_common(args, false, false);
}

static void test_colo_multifd_primary_failover(char *name, MigrateCommon *args)
{
    test_colo_multifd_common(args, false, true);
}

static void test_colo_multifd_secondary_failover(char *name,
                                                 MigrateCommon *args)
{
    test_colo_multifd_common(args, false, false);
}

static void test_colo_plain_primary_failover_checkpoint(char *name,
                                                        MigrateCommon *args)
{
    test_colo_plain_common(args, true, true);
}

static void test_colo_plain_secondary_failover_checkpoint(char *name,
                                                          MigrateCommon *args)
{
    test_colo_plain_common(args, true, false);
}

static void test_colo_multifd_primary_failover_checkpoint(char *name,
                                                          MigrateCommon *args)
{
    test_colo_multifd_common(args, true, true);
}

static void test_colo_multifd_secondary_failover_checkpoint(char *name,
                                                            MigrateCommon *args)
{
    test_colo_multifd_common(args, true, false);
}

void migration_test_add_colo(MigrationTestEnv *env)
{
    if (!env->full_set) {
        return;
    }

    migration_test_add("/migration/colo/plain/primary_failover",
                       test_colo_plain_primary_failover);
    migration_test_add("/migration/colo/plain/secondary_failover",
                       test_colo_plain_secondary_failover);

    migration_test_add("/migration/colo/multifd/primary_failover",
                       test_colo_multifd_primary_failover);
    migration_test_add("/migration/colo/multifd/secondary_failover",
                       test_colo_multifd_secondary_failover);

    migration_test_add("/migration/colo/plain/primary_failover_checkpoint",
                       test_colo_plain_primary_failover_checkpoint);
    migration_test_add("/migration/colo/plain/secondary_failover_checkpoint",
                       test_colo_plain_secondary_failover_checkpoint);

    migration_test_add("/migration/colo/multifd/primary_failover_checkpoint",
                       test_colo_multifd_primary_failover_checkpoint);
    migration_test_add("/migration/colo/multifd/secondary_failover_checkpoint",
                       test_colo_multifd_secondary_failover_checkpoint);
}
