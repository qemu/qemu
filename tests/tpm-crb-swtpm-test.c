/*
 * QTest testcase for TPM CRB talking to external swtpm and swtpm migration
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
#include <glib/gstdio.h>

#include "hw/acpi/tpm.h"
#include "io/channel-socket.h"
#include "libqtest.h"
#include "tpm-util.h"
#include "sysemu/tpm.h"
#include "qapi/qmp/qdict.h"

typedef struct TestState {
    char *src_tpm_path;
    char *dst_tpm_path;
    char *uri;
} TestState;

bool got_stop;

static void migrate(QTestState *who, const char *uri)
{
    QDict *rsp;
    gchar *cmd;

    cmd = g_strdup_printf("{ 'execute': 'migrate',"
                          "'arguments': { 'uri': '%s' } }",
                          uri);
    rsp = qtest_qmp(who, cmd);
    g_free(cmd);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
}

/*
 * Events can get in the way of responses we are actually waiting for.
 */
static QDict *wait_command(QTestState *who, const char *command)
{
    const char *event_string;
    QDict *response;

    response = qtest_qmp(who, command);

    while (qdict_haskey(response, "event")) {
        /* OK, it was an event */
        event_string = qdict_get_str(response, "event");
        if (!strcmp(event_string, "STOP")) {
            got_stop = true;
        }
        qobject_unref(response);
        response = qtest_qmp_receive(who);
    }
    return response;
}

static void wait_for_migration_complete(QTestState *who)
{
    while (true) {
        QDict *rsp, *rsp_return;
        bool completed;
        const char *status;

        rsp = wait_command(who, "{ 'execute': 'query-migrate' }");
        rsp_return = qdict_get_qdict(rsp, "return");
        status = qdict_get_str(rsp_return, "status");
        completed = strcmp(status, "completed") == 0;
        g_assert_cmpstr(status, !=,  "failed");
        qobject_unref(rsp);
        if (completed) {
            return;
        }
        usleep(1000);
    }
}

static void migration_start_qemu(QTestState **src_qemu, QTestState **dst_qemu,
                                 SocketAddress *src_tpm_addr,
                                 SocketAddress *dst_tpm_addr,
                                 const char *miguri)
{
    char *src_qemu_args, *dst_qemu_args;

    src_qemu_args = g_strdup_printf(
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device tpm-crb,tpmdev=dev ",
        src_tpm_addr->u.q_unix.path);

    *src_qemu = qtest_init(src_qemu_args);

    dst_qemu_args = g_strdup_printf(
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device tpm-crb,tpmdev=dev "
        "-incoming %s",
        dst_tpm_addr->u.q_unix.path,
        miguri);

    *dst_qemu = qtest_init(dst_qemu_args);

    free(src_qemu_args);
    free(dst_qemu_args);
}

static void tpm_crb_swtpm_test(const void *data)
{
    char *args = NULL;
    QTestState *s;
    SocketAddress *addr = NULL;
    gboolean succ;
    GPid swtpm_pid;
    GError *error = NULL;
    const TestState *ts = data;

    succ = tpm_util_swtpm_start(ts->src_tpm_path, &swtpm_pid, &addr, &error);
    /* succ may be false if swtpm is not available */
    if (!succ) {
        return;
    }

    args = g_strdup_printf(
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device tpm-crb,tpmdev=dev",
        addr->u.q_unix.path);

    s = qtest_start(args);
    g_free(args);

    tpm_util_startup(s, tpm_util_crb_transfer);
    tpm_util_pcrextend(s, tpm_util_crb_transfer);

    unsigned char tpm_pcrread_resp[] =
        "\x80\x01\x00\x00\x00\x3e\x00\x00\x00\x00\x00\x00\x00\x16\x00\x00"
        "\x00\x01\x00\x0b\x03\x00\x04\x00\x00\x00\x00\x01\x00\x20\xf6\x85"
        "\x98\xe5\x86\x8d\xe6\x8b\x97\x29\x99\x60\xf2\x71\x7d\x17\x67\x89"
        "\xa4\x2f\x9a\xae\xa8\xc7\xb7\xaa\x79\xa8\x62\x56\xc1\xde";
    tpm_util_pcrread(s, tpm_util_crb_transfer, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    qtest_end();
    tpm_util_swtpm_kill(swtpm_pid);

    if (addr) {
        g_unlink(addr->u.q_unix.path);
        qapi_free_SocketAddress(addr);
    }
}

static void tpm_crb_swtpm_migration_test(const void *data)
{
    const TestState *ts = data;
    gboolean succ;
    GPid src_tpm_pid, dst_tpm_pid;
    SocketAddress *src_tpm_addr = NULL, *dst_tpm_addr = NULL;
    GError *error = NULL;
    QTestState *src_qemu, *dst_qemu;

    succ = tpm_util_swtpm_start(ts->src_tpm_path, &src_tpm_pid,
                                &src_tpm_addr, &error);
    /* succ may be false if swtpm is not available */
    if (!succ) {
        return;
    }

    succ = tpm_util_swtpm_start(ts->dst_tpm_path, &dst_tpm_pid,
                                &dst_tpm_addr, &error);
    /* succ may be false if swtpm is not available */
    if (!succ) {
        goto err_src_tpm_kill;
    }

    migration_start_qemu(&src_qemu, &dst_qemu, src_tpm_addr, dst_tpm_addr,
                         ts->uri);

    tpm_util_startup(src_qemu, tpm_util_crb_transfer);
    tpm_util_pcrextend(src_qemu, tpm_util_crb_transfer);

    unsigned char tpm_pcrread_resp[] =
        "\x80\x01\x00\x00\x00\x3e\x00\x00\x00\x00\x00\x00\x00\x16\x00\x00"
        "\x00\x01\x00\x0b\x03\x00\x04\x00\x00\x00\x00\x01\x00\x20\xf6\x85"
        "\x98\xe5\x86\x8d\xe6\x8b\x97\x29\x99\x60\xf2\x71\x7d\x17\x67\x89"
        "\xa4\x2f\x9a\xae\xa8\xc7\xb7\xaa\x79\xa8\x62\x56\xc1\xde";
    tpm_util_pcrread(src_qemu, tpm_util_crb_transfer, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    migrate(src_qemu, ts->uri);
    wait_for_migration_complete(src_qemu);

    tpm_util_pcrread(dst_qemu, tpm_util_crb_transfer, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    qtest_quit(dst_qemu);
    qtest_quit(src_qemu);

    tpm_util_swtpm_kill(dst_tpm_pid);
    if (dst_tpm_addr) {
        g_unlink(dst_tpm_addr->u.q_unix.path);
        qapi_free_SocketAddress(dst_tpm_addr);
    }

err_src_tpm_kill:
    tpm_util_swtpm_kill(src_tpm_pid);
    if (src_tpm_addr) {
        g_unlink(src_tpm_addr->u.q_unix.path);
        qapi_free_SocketAddress(src_tpm_addr);
    }
}

int main(int argc, char **argv)
{
    int ret;
    TestState ts = { 0 };

    ts.src_tpm_path = g_dir_make_tmp("qemu-tpm-crb-swtpm-test.XXXXXX", NULL);
    ts.dst_tpm_path = g_dir_make_tmp("qemu-tpm-crb-swtpm-test.XXXXXX", NULL);
    ts.uri = g_strdup_printf("unix:%s/migsocket", ts.src_tpm_path);

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/tpm/crb-swtpm/test", &ts, tpm_crb_swtpm_test);
    qtest_add_data_func("/tpm/crb-swtpm-migration/test", &ts,
                        tpm_crb_swtpm_migration_test);
    ret = g_test_run();

    g_rmdir(ts.dst_tpm_path);
    g_free(ts.dst_tpm_path);
    g_rmdir(ts.src_tpm_path);
    g_free(ts.src_tpm_path);
    g_free(ts.uri);

    return ret;
}
