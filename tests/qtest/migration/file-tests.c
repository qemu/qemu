/*
 * QTest testcases for migration to file
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
#include "qobject/qlist.h"


static char *tmpfs;

static void test_precopy_file(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
    };

    test_file_common(&args, true);
}

#ifndef _WIN32
static void fdset_add_fds(QTestState *qts, const char *file, int flags,
                          int num_fds, bool direct_io)
{
    for (int i = 0; i < num_fds; i++) {
        int fd;

#ifdef O_DIRECT
        /* only secondary channels can use direct-io */
        if (direct_io && i != 0) {
            flags |= O_DIRECT;
        }
#endif

        fd = open(file, flags, 0660);
        assert(fd != -1);

        qtest_qmp_fds_assert_success(qts, &fd, 1, "{'execute': 'add-fd', "
                                     "'arguments': {'fdset-id': 1}}");
        close(fd);
    }
}

static void *migrate_hook_start_file_offset_fdset(QTestState *from,
                                                  QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);

    fdset_add_fds(from, file, O_WRONLY, 1, false);
    fdset_add_fds(to, file, O_RDONLY, 1, false);

    return NULL;
}

static void test_precopy_file_offset_fdset(void)
{
    g_autofree char *uri = g_strdup_printf("file:/dev/fdset/1,offset=%d",
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_file_offset_fdset,
    };

    test_file_common(&args, false);
}
#endif

static void test_precopy_file_offset(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s,offset=%d", tmpfs,
                                           FILE_TEST_FILENAME,
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
    };

    test_file_common(&args, false);
}

static void test_precopy_file_offset_bad(void)
{
    /* using a value not supported by qemu_strtosz() */
    g_autofree char *uri = g_strdup_printf("file:%s/%s,offset=0x20M",
                                           tmpfs, FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .result = MIG_TEST_QMP_ERROR,
    };

    test_file_common(&args, false);
}

static void test_precopy_file_mapped_ram_live(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MAPPED_RAM] = true,
        },
    };

    test_file_common(&args, false);
}

static void test_precopy_file_mapped_ram(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MAPPED_RAM] = true,
        },
    };

    test_file_common(&args, true);
}

static void test_multifd_file_mapped_ram_live(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
            .caps[MIGRATION_CAPABILITY_MAPPED_RAM] = true,
        },
    };

    test_file_common(&args, false);
}

static void test_multifd_file_mapped_ram(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start = {
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
            .caps[MIGRATION_CAPABILITY_MAPPED_RAM] = true,
        },
    };

    test_file_common(&args, true);
}

static void *migrate_hook_start_multifd_mapped_ram_dio(QTestState *from,
                                                       QTestState *to)
{
    migrate_set_parameter_bool(from, "direct-io", true);
    migrate_set_parameter_bool(to, "direct-io", true);

    return NULL;
}

static void test_multifd_file_mapped_ram_dio(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_mapped_ram_dio,
        .start = {
            .caps[MIGRATION_CAPABILITY_MAPPED_RAM] = true,
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
    };

    if (!probe_o_direct_support(tmpfs)) {
        g_test_skip("Filesystem does not support O_DIRECT");
        return;
    }

    test_file_common(&args, true);
}

#ifndef _WIN32
static void migrate_hook_end_multifd_mapped_ram_fdset(QTestState *from,
                                                      QTestState *to,
                                                      void *opaque)
{
    QDict *resp;
    QList *fdsets;

    /*
     * Remove the fdsets after migration, otherwise a second migration
     * would fail due fdset reuse.
     */
    qtest_qmp_assert_success(from, "{'execute': 'remove-fd', "
                             "'arguments': { 'fdset-id': 1}}");

    /*
     * Make sure no fdsets are left after migration, otherwise a
     * second migration would fail due fdset reuse.
     */
    resp = qtest_qmp(from, "{'execute': 'query-fdsets', "
                     "'arguments': {}}");
    g_assert(qdict_haskey(resp, "return"));
    fdsets = qdict_get_qlist(resp, "return");
    g_assert(fdsets && qlist_empty(fdsets));
    qobject_unref(resp);
}

static void *migrate_hook_start_multifd_mapped_ram_fdset_dio(QTestState *from,
                                                             QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);

    fdset_add_fds(from, file, O_WRONLY, 2, true);
    fdset_add_fds(to, file, O_RDONLY, 2, true);

    migrate_set_parameter_bool(from, "direct-io", true);
    migrate_set_parameter_bool(to, "direct-io", true);

    return NULL;
}

static void *migrate_hook_start_multifd_mapped_ram_fdset(QTestState *from,
                                                         QTestState *to)
{
    g_autofree char *file = g_strdup_printf("%s/%s", tmpfs, FILE_TEST_FILENAME);

    fdset_add_fds(from, file, O_WRONLY, 2, false);
    fdset_add_fds(to, file, O_RDONLY, 2, false);

    return NULL;
}

static void test_multifd_file_mapped_ram_fdset(void)
{
    g_autofree char *uri = g_strdup_printf("file:/dev/fdset/1,offset=%d",
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_mapped_ram_fdset,
        .end_hook = migrate_hook_end_multifd_mapped_ram_fdset,
        .start = {
            .caps[MIGRATION_CAPABILITY_MAPPED_RAM] = true,
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
    };

    test_file_common(&args, true);
}

static void test_multifd_file_mapped_ram_fdset_dio(void)
{
    g_autofree char *uri = g_strdup_printf("file:/dev/fdset/1,offset=%d",
                                           FILE_TEST_OFFSET);
    MigrateCommon args = {
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_multifd_mapped_ram_fdset_dio,
        .end_hook = migrate_hook_end_multifd_mapped_ram_fdset,
        .start = {
            .caps[MIGRATION_CAPABILITY_MAPPED_RAM] = true,
            .caps[MIGRATION_CAPABILITY_MULTIFD] = true,
        },
    };

    if (!probe_o_direct_support(tmpfs)) {
        g_test_skip("Filesystem does not support O_DIRECT");
        return;
    }

    test_file_common(&args, true);
}
#endif /* !_WIN32 */

static void migration_test_add_file_smoke(MigrationTestEnv *env)
{
    migration_test_add("/migration/precopy/file",
                       test_precopy_file);

    migration_test_add("/migration/multifd/file/mapped-ram/dio",
                       test_multifd_file_mapped_ram_dio);
}

void migration_test_add_file(MigrationTestEnv *env)
{
    tmpfs = env->tmpfs;

    migration_test_add_file_smoke(env);

    if (!env->full_set) {
        return;
    }

    migration_test_add("/migration/precopy/file/offset",
                       test_precopy_file_offset);
#ifndef _WIN32
    migration_test_add("/migration/precopy/file/offset/fdset",
                       test_precopy_file_offset_fdset);
#endif
    migration_test_add("/migration/precopy/file/offset/bad",
                       test_precopy_file_offset_bad);

    migration_test_add("/migration/precopy/file/mapped-ram",
                       test_precopy_file_mapped_ram);
    migration_test_add("/migration/precopy/file/mapped-ram/live",
                       test_precopy_file_mapped_ram_live);

    migration_test_add("/migration/multifd/file/mapped-ram",
                       test_multifd_file_mapped_ram);
    migration_test_add("/migration/multifd/file/mapped-ram/live",
                       test_multifd_file_mapped_ram_live);

#ifndef _WIN32
    migration_test_add("/migration/multifd/file/mapped-ram/fdset",
                       test_multifd_file_mapped_ram_fdset);
    migration_test_add("/migration/multifd/file/mapped-ram/fdset/dio",
                       test_multifd_file_mapped_ram_fdset_dio);
#endif
}
