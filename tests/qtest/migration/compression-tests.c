/*
 * QTest testcases for migration compression
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
#include "qemu/module.h"


static char *tmpfs;

#ifdef CONFIG_ZSTD
static void *
migrate_hook_start_precopy_tcp_multifd_zstd(QTestState *from,
                                            QTestState *to)
{
    migrate_set_parameter_int(from, "multifd-zstd-level", 2);
    migrate_set_parameter_int(to, "multifd-zstd-level", 2);

    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "zstd");
}

static void test_multifd_tcp_zstd(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .start_hook = migrate_hook_start_precopy_tcp_multifd_zstd,
    };
    test_precopy_common(&args);
}

static void test_multifd_postcopy_tcp_zstd(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
            .caps[MIGRATION_CAPABILITY_POSTCOPY_RAM] = true,
        },
        .start_hook = migrate_hook_start_precopy_tcp_multifd_zstd,
    };

    test_precopy_common(&args);
}
#endif /* CONFIG_ZSTD */

#ifdef CONFIG_QATZIP
static void *
migrate_hook_start_precopy_tcp_multifd_qatzip(QTestState *from,
                                              QTestState *to)
{
    migrate_set_parameter_int(from, "multifd-qatzip-level", 2);
    migrate_set_parameter_int(to, "multifd-qatzip-level", 2);

    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "qatzip");
}

static void test_multifd_tcp_qatzip(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .start_hook = migrate_hook_start_precopy_tcp_multifd_qatzip,
    };
    test_precopy_common(&args);
}
#endif

#ifdef CONFIG_QPL
static void *
migrate_hook_start_precopy_tcp_multifd_qpl(QTestState *from,
                                           QTestState *to)
{
    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "qpl");
}

static void test_multifd_tcp_qpl(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .start_hook = migrate_hook_start_precopy_tcp_multifd_qpl,
    };
    test_precopy_common(&args);
}
#endif /* CONFIG_QPL */

#ifdef CONFIG_UADK
static void *
migrate_hook_start_precopy_tcp_multifd_uadk(QTestState *from,
                                            QTestState *to)
{
    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "uadk");
}

static void test_multifd_tcp_uadk(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .start_hook = migrate_hook_start_precopy_tcp_multifd_uadk,
    };
    test_precopy_common(&args);
}
#endif /* CONFIG_UADK */

static void *
migrate_hook_start_xbzrle(QTestState *from,
                          QTestState *to)
{
    migrate_set_parameter_int(from, "xbzrle-cache-size", 33554432);
    return NULL;
}

static void test_precopy_unix_xbzrle(void)
{
    g_autofree char *uri = g_strdup_printf("unix:%s/migsocket", tmpfs);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = uri,
        .start_hook = migrate_hook_start_xbzrle,
        .iterations = 2,
        .start = {
            .caps[MIGRATION_CAPABILITY_XBZRLE] = true,
        },
        /*
         * XBZRLE needs pages to be modified when doing the 2nd+ round
         * iteration to have real data pushed to the stream.
         */
        .live = true,
    };

    test_precopy_common(&args);
}

static void *
migrate_hook_start_precopy_tcp_multifd_zlib(QTestState *from,
                                            QTestState *to)
{
    /*
     * Overloading this test to also check that set_parameter does not error.
     * This is also done in the tests for the other compression methods.
     */
    migrate_set_parameter_int(from, "multifd-zlib-level", 2);
    migrate_set_parameter_int(to, "multifd-zlib-level", 2);

    return migrate_hook_start_precopy_tcp_multifd_common(from, to, "zlib");
}

static void test_multifd_tcp_zlib(void)
{
    MigrateCommon args = {
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
        .start_hook = migrate_hook_start_precopy_tcp_multifd_zlib,
    };
    test_precopy_common(&args);
}

static void migration_test_add_compression_smoke(MigrationTestEnv *env)
{
    migration_test_add("/migration/multifd/tcp/plain/zlib",
                       test_multifd_tcp_zlib);
}

void migration_test_add_compression(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    migration_test_add_compression_smoke(env);

    if (!env->full_set) {
        return;
    }

#ifdef CONFIG_ZSTD
    migration_test_add("/migration/multifd/tcp/plain/zstd",
                       test_multifd_tcp_zstd);
    if (env->has_uffd) {
        migration_test_add("/migration/multifd+postcopy/tcp/plain/zstd",
                           test_multifd_postcopy_tcp_zstd);
    }
#endif

#ifdef CONFIG_QATZIP
    migration_test_add("/migration/multifd/tcp/plain/qatzip",
                       test_multifd_tcp_qatzip);
#endif

#ifdef CONFIG_QPL
    migration_test_add("/migration/multifd/tcp/plain/qpl",
                       test_multifd_tcp_qpl);
#endif

#ifdef CONFIG_UADK
    migration_test_add("/migration/multifd/tcp/plain/uadk",
                       test_multifd_tcp_uadk);
#endif

    if (g_test_slow()) {
        migration_test_add("/migration/precopy/unix/xbzrle",
                           test_precopy_unix_xbzrle);
    }
}
