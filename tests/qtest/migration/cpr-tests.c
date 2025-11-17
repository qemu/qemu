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

    return NULL;
}

static void test_mode_reboot(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    MigrateCommon args = {
        .start.mem_type = MEM_TYPE_SHMEM,
        .connect_uri = uri,
        .listen_uri = "defer",
        .start_hook = migrate_hook_start_mode_reboot,
        .start = {
            .caps[MIGRATION_CAPABILITY_X_IGNORE_SHARED] = true,
        },
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
    g_autofree char *opts_target = NULL;

    const char *opts = "-machine aux-ram-share=on -nodefaults";
    g_autofree const char *cpr_channel = g_strdup_printf(
        "cpr,addr.transport=socket,addr.type=unix,addr.path=%s",
        cpr_path);

    g_autofree char *connect_channels = g_strdup_printf(
        "[ { 'channel-type': 'main',"
        "    'addr': { 'transport': 'socket',"
        "              'type': 'unix',"
        "              'path': '%s' } } ]",
        mig_path);

    /*
     * Set up a UNIX domain socket for the CPR channel before
     * launching the destination VM, to avoid timing issues
     * during connection setup.
     */
    int cpr_sockfd = qtest_socket_server(cpr_path);
    g_assert(cpr_sockfd >= 0);

    opts_target = g_strdup_printf("-incoming cpr,addr.transport=socket,"
                                  "addr.type=fd,addr.str=%d %s",
                                  cpr_sockfd, opts);
    MigrateCommon args = {
        .start.opts_source = opts,
        .start.opts_target = opts_target,
        .start.defer_target_connect = true,
        .start.mem_type = MEM_TYPE_MEMFD,
        .listen_uri = incoming_defer ? "defer" : uri,
        .connect_channels = connect_channels,
        .cpr_channel = cpr_channel,
        .start_hook = test_mode_transfer_start,
    };

    if (test_precopy_common(&args) < 0) {
        close(cpr_sockfd);
        unlink(cpr_path);
    }
}

static void test_mode_transfer(void)
{
    test_mode_transfer_common(NULL);
}

static void test_mode_transfer_defer(void)
{
    test_mode_transfer_common(true);
}

static void set_cpr_exec_args(QTestState *who, MigrateCommon *args)
{
    g_autofree char *qtest_from_args = NULL;
    g_autofree char *from_args = NULL;
    g_autofree char *to_args = NULL;
    g_autofree char *exec_args = NULL;
    g_auto(GStrv) argv = NULL;
    char *from_str, *src, *dst;
    int ret;

    /*
     * hide_stderr appends "2>/dev/null" to the command line, but cpr-exec
     * passes the command-line words to execv, not to the shell, so suppress it
     * here.  fd 2 was already bound in the source VM, and execv preserves it.
     */
    g_assert(args->start.hide_stderr == false);

    ret = migrate_args(&from_args, &to_args, args->listen_uri, &args->start);
    g_assert(!ret);
    qtest_from_args = qtest_qemu_args(from_args);

    /*
     * The generated args may have been formatted using "%s %s" with empty
     * strings, which can produce consecutive spaces, which g_strsplit would
     * convert into empty strings.  Ditto for leading and trailing space.
     * De-dup spaces to avoid that.
     */

    from_str = src = dst = g_strstrip(qtest_from_args);
    do {
        if (*src != ' ' || src[-1] != ' ') {
            *dst++ = *src;
        }
    } while (*src++);

    exec_args = g_strconcat(qtest_qemu_binary(migration_get_env()->qemu_dst),
                            " -incoming defer ", from_str, NULL);
    argv = g_strsplit(exec_args, " ", -1);
    migrate_set_parameter_strv(who, "cpr-exec-command", argv);
}

static void wait_for_migration_event(QTestState *who, const char *waitfor)
{
    QDict *rsp, *data;
    char *status;
    bool done = false;

    while (!done) {
        rsp = qtest_qmp_eventwait_ref(who, "MIGRATION");
        g_assert(qdict_haskey(rsp, "data"));
        data = qdict_get_qdict(rsp, "data");
        g_assert(qdict_haskey(data, "status"));
        status = g_strdup(qdict_get_str(data, "status"));
        g_assert(strcmp(status, "failed"));
        done = !strcmp(status, waitfor);
        qobject_unref(rsp);
    }
}

static void test_cpr_exec(MigrateCommon *args)
{
    QTestState *from, *to;
    void *data_hook = NULL;
    g_autofree char *connect_uri = g_strdup(args->connect_uri);
    g_autofree char *filename = g_strdup_printf("%s/%s", tmpfs,
                                                FILE_TEST_FILENAME);

    if (migrate_start(&from, NULL, args->listen_uri, &args->start)) {
        return;
    }

    /* Source and dest never run concurrently */
    g_assert_false(args->live);

    if (args->start_hook) {
        data_hook = args->start_hook(from, NULL);
    }

    wait_for_serial("src_serial");
    set_cpr_exec_args(from, args);
    migrate_set_capability(from, "events", true);
    migrate_qmp(from, NULL, connect_uri, NULL, "{}");
    wait_for_migration_event(from, "completed");

    to = qtest_init_after_exec(from);

    qtest_qmp_assert_success(to, "{ 'execute': 'migrate-incoming',"
                             "  'arguments': { "
                             "      'channels': [ { 'channel-type': 'main',"
                             "      'addr': { 'transport': 'file',"
                             "                'filename': %s,"
                             "                'offset': 0  } } ] } }",
                             filename);
    wait_for_migration_complete(to);

    wait_for_resume(to, get_dst());
    /* Device on target is still named src_serial because args do not change */
    wait_for_serial("src_serial");

    if (args->end_hook) {
        args->end_hook(from, to, data_hook);
    }

    migrate_end(from, to, args->result == MIG_TEST_SUCCEED);
}

static void *test_mode_exec_start(QTestState *from, QTestState *to)
{
    assert(!to);
    migrate_set_parameter_str(from, "mode", "cpr-exec");
    return NULL;
}

static void test_mode_exec(void)
{
    g_autofree char *uri = g_strdup_printf("file:%s/%s", tmpfs,
                                           FILE_TEST_FILENAME);
    g_autofree char *listen_uri = g_strdup_printf("defer");

    MigrateCommon args = {
        .start.only_source = true,
        .start.opts_source = "-machine aux-ram-share=on -nodefaults",
        .start.mem_type = MEM_TYPE_MEMFD,
        .connect_uri = uri,
        .listen_uri = listen_uri,
        .start_hook = test_mode_exec_start,
    };

    test_cpr_exec(&args);
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
        migration_test_add("/migration/mode/exec", test_mode_exec);
    }
}
