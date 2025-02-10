/*
 * QTest testcase for migration
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
#include "migration/framework.h"
#include "qemu/module.h"

static void parse_args(int *argc_p, char ***argv_p, bool *full_set)
{
    int argc = *argc_p;
    char **argv = *argv_p;
    int i, j;

    j = 1;
    for (i = 1; i < argc; i++) {
        if (g_str_equal(argv[i], "--full")) {
            *full_set = true;
            continue;
        }
        argv[j++] = argv[i];
        if (i >= j) {
            argv[i] = NULL;
        }
    }
    *argc_p = j;
}

int main(int argc, char **argv)
{
    MigrationTestEnv *env;
    int ret;
    bool full_set = false;

    /* strip the --full option if it's present */
    parse_args(&argc, &argv, &full_set);

    g_test_init(&argc, &argv, NULL);
    env = migration_get_env();
    env->full_set = full_set;
    module_call_init(MODULE_INIT_QOM);

    migration_test_add_tls(env);
    migration_test_add_compression(env);
    migration_test_add_postcopy(env);
    migration_test_add_file(env);
    migration_test_add_precopy(env);
    migration_test_add_cpr(env);
    migration_test_add_misc(env);

    ret = g_test_run();

    g_assert_cmpint(ret, ==, 0);

    ret = migration_env_clean(env);

    return ret;
}
