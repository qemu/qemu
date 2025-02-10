/*
 * QTest testcases for postcopy migration
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
#include "migration/migration-util.h"
#include "qapi/qmp/qlist.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/range.h"
#include "qemu/sockets.h"

static void test_postcopy(void)
{
    MigrateCommon args = { };

    test_postcopy_common(&args);
}

static void test_postcopy_suspend(void)
{
    MigrateCommon args = {
        .start.suspend_me = true,
    };

    test_postcopy_common(&args);
}

static void test_postcopy_preempt(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
    };

    test_postcopy_common(&args);
}

static void test_postcopy_recovery(void)
{
    MigrateCommon args = { };

    test_postcopy_recovery_common(&args);
}

static void test_postcopy_recovery_fail_handshake(void)
{
    MigrateCommon args = {
        .postcopy_recovery_fail_stage = POSTCOPY_FAIL_RECOVERY,
    };

    test_postcopy_recovery_common(&args);
}

static void test_postcopy_recovery_fail_reconnect(void)
{
    MigrateCommon args = {
        .postcopy_recovery_fail_stage = POSTCOPY_FAIL_CHANNEL_ESTABLISH,
    };

    test_postcopy_recovery_common(&args);
}

static void test_postcopy_preempt_recovery(void)
{
    MigrateCommon args = {
        .postcopy_preempt = true,
    };

    test_postcopy_recovery_common(&args);
}

void migration_test_add_postcopy(MigrationTestEnv *env)
{
    if (env->has_uffd) {
        migration_test_add("/migration/postcopy/plain", test_postcopy);
        migration_test_add("/migration/postcopy/recovery/plain",
                           test_postcopy_recovery);
        migration_test_add("/migration/postcopy/preempt/plain",
                           test_postcopy_preempt);
        migration_test_add("/migration/postcopy/preempt/recovery/plain",
                           test_postcopy_preempt_recovery);

        migration_test_add(
            "/migration/postcopy/recovery/double-failures/handshake",
            test_postcopy_recovery_fail_handshake);

        migration_test_add(
            "/migration/postcopy/recovery/double-failures/reconnect",
            test_postcopy_recovery_fail_reconnect);

        if (env->is_x86) {
            migration_test_add("/migration/postcopy/suspend",
                               test_postcopy_suspend);
        }
    }
}
