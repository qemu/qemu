/*
 * QTest TPM utilities
 *
 * Copyright (c) 2018 IBM Corporation
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/acpi/tpm.h"
#include "libqtest.h"
#include "tpm-util.h"
#include "qobject/qdict.h"

void tpm_util_crb_transfer(QTestState *s,
                           const unsigned char *req, size_t req_size,
                           unsigned char *rsp, size_t rsp_size)
{
    uint64_t caddr = qtest_readq(s, TPM_CRB_ADDR_BASE + A_CRB_CTRL_CMD_LADDR);
    uint64_t raddr = qtest_readq(s, TPM_CRB_ADDR_BASE + A_CRB_CTRL_RSP_ADDR);

    qtest_writeb(s, TPM_CRB_ADDR_BASE + A_CRB_LOC_CTRL, 1);

    qtest_memwrite(s, caddr, req, req_size);

    uint32_t sts, start = 1;
    uint64_t end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    qtest_writel(s, TPM_CRB_ADDR_BASE + A_CRB_CTRL_START, start);
    while (true) {
        start = qtest_readl(s, TPM_CRB_ADDR_BASE + A_CRB_CTRL_START);
        if ((start & 1) == 0) {
            break;
        }
        if (g_get_monotonic_time() >= end_time) {
            break;
        }
    };
    start = qtest_readl(s, TPM_CRB_ADDR_BASE + A_CRB_CTRL_START);
    g_assert_cmpint(start & 1, ==, 0);
    sts = qtest_readl(s, TPM_CRB_ADDR_BASE + A_CRB_CTRL_STS);
    g_assert_cmpint(sts & 1, ==, 0);

    qtest_memread(s, raddr, rsp, rsp_size);
}

void tpm_util_startup(QTestState *s, tx_func *tx)
{
    unsigned char buffer[1024];
    static const unsigned char tpm_startup[] =
        "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00";
    static const unsigned char tpm_startup_resp[] =
        "\x80\x01\x00\x00\x00\x0a\x00\x00\x00\x00";

    tx(s, tpm_startup, sizeof(tpm_startup), buffer, sizeof(buffer));

    g_assert_cmpmem(buffer, sizeof(tpm_startup_resp),
                    tpm_startup_resp, sizeof(tpm_startup_resp));
}

void tpm_util_pcrextend(QTestState *s, tx_func *tx)
{
    unsigned char buffer[1024];
    static const unsigned char tpm_pcrextend[] =
        "\x80\x02\x00\x00\x00\x41\x00\x00\x01\x82\x00\x00\x00\x0a\x00\x00"
        "\x00\x09\x40\x00\x00\x09\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00"
        "\x0b\x74\x65\x73\x74\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00";

    static const unsigned char tpm_pcrextend_resp[] =
        "\x80\x02\x00\x00\x00\x13\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x01\x00\x00";

    tx(s, tpm_pcrextend, sizeof(tpm_pcrextend), buffer, sizeof(buffer));

    g_assert_cmpmem(buffer, sizeof(tpm_pcrextend_resp),
                    tpm_pcrextend_resp, sizeof(tpm_pcrextend_resp));
}

void tpm_util_pcrread(QTestState *s, tx_func *tx,
                      const unsigned char *exp_resp, size_t exp_resp_size)
{
    unsigned char buffer[1024];
    static const unsigned char tpm_pcrread[] =
        "\x80\x01\x00\x00\x00\x14\x00\x00\x01\x7e\x00\x00\x00\x01\x00\x0b"
        "\x03\x00\x04\x00";

    tx(s, tpm_pcrread, sizeof(tpm_pcrread), buffer, sizeof(buffer));

    /* skip pcrUpdateCounter (14th byte) in comparison */
    g_assert(exp_resp_size >= 15);
    g_assert_cmpmem(buffer, 13, exp_resp, 13);
    g_assert_cmpmem(&buffer[14], exp_resp_size - 14,
                    &exp_resp[14], exp_resp_size - 14);
}

bool tpm_util_swtpm_has_tpm2(void)
{
    bool has_tpm2 = false;
    char *out = NULL;
    static const char *argv[] = {
        "swtpm", "socket", "--help", NULL
    };

    if (!g_spawn_sync(NULL /* working_dir */,
                      (char **)argv,
                      NULL /* envp */,
                      G_SPAWN_SEARCH_PATH,
                      NULL /* child_setup */,
                      NULL /* user_data */,
                      &out,
                      NULL /* err */,
                      NULL /* exit_status */,
                      NULL)) {
        return false;
    }

    if (strstr(out, "--tpm2")) {
        has_tpm2 = true;
    }

    g_free(out);
    return has_tpm2;
}

gboolean tpm_util_swtpm_start(const char *path, GPid *pid,
                              SocketAddress **addr, GError **error)
{
    char *swtpm_argv_tpmstate = g_strdup_printf("dir=%s", path);
    char *swtpm_argv_ctrl = g_strdup_printf("type=unixio,path=%s/sock",
                                            path);
    gchar *swtpm_argv[] = {
        g_strdup("swtpm"), g_strdup("socket"),
        g_strdup("--tpmstate"), swtpm_argv_tpmstate,
        g_strdup("--ctrl"), swtpm_argv_ctrl,
        g_strdup("--tpm2"),
        NULL
    };
    gboolean succ;
    unsigned i;

    *addr = g_new0(SocketAddress, 1);
    (*addr)->type = SOCKET_ADDRESS_TYPE_UNIX;
    (*addr)->u.q_unix.path = g_build_filename(path, "sock", NULL);

    succ = g_spawn_async(NULL, swtpm_argv, NULL, G_SPAWN_SEARCH_PATH,
                         NULL, NULL, pid, error);

    for (i = 0; swtpm_argv[i]; i++) {
        g_free(swtpm_argv[i]);
    }

    return succ;
}

void tpm_util_swtpm_kill(GPid pid)
{
    int n;

    if (!pid) {
        return;
    }

    g_spawn_close_pid(pid);

    n = kill(pid, 0);
    if (n < 0) {
        return;
    }

    kill(pid, SIGKILL);
}

void tpm_util_migrate(QTestState *who, const char *uri)
{
    QDict *rsp;

    rsp = qtest_qmp(who,
                    "{ 'execute': 'migrate', 'arguments': { 'uri': %s } }",
                    uri);
    g_assert(qdict_haskey(rsp, "return"));
    qobject_unref(rsp);
}

void tpm_util_wait_for_migration_complete(QTestState *who)
{
    while (true) {
        QDict *rsp;
        QDict *rsp_return;
        bool completed;
        const char *status;

        rsp = qtest_qmp(who, "{ 'execute': 'query-migrate' }");
        g_assert(qdict_haskey(rsp, "return"));
        rsp_return = qdict_get_qdict(rsp, "return");

        g_assert(!qdict_haskey(rsp_return, "error"));
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

void tpm_util_migration_start_qemu(QTestState **src_qemu,
                                   QTestState **dst_qemu,
                                   SocketAddress *src_tpm_addr,
                                   SocketAddress *dst_tpm_addr,
                                   const char *miguri,
                                   const char *ifmodel,
                                   const char *machine_options)
{
    char *src_qemu_args, *dst_qemu_args;

    src_qemu_args = g_strdup_printf(
        "%s "
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device %s,tpmdev=dev ",
        machine_options ? : "", src_tpm_addr->u.q_unix.path, ifmodel);

    *src_qemu = qtest_init(src_qemu_args);

    dst_qemu_args = g_strdup_printf(
        "%s "
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device %s,tpmdev=dev "
        "-incoming %s",
        machine_options ? : "",
        dst_tpm_addr->u.q_unix.path,
        ifmodel, miguri);

    *dst_qemu = qtest_init(dst_qemu_args);

    g_free(src_qemu_args);
    g_free(dst_qemu_args);
}

/* Remove directory with remainders of swtpm */
void tpm_util_rmdir(const char *path)
{
    char *filename;
    int ret;

    filename = g_strdup_printf("%s/tpm2-00.permall", path);
    g_unlink(filename);
    g_free(filename);

    filename = g_strdup_printf("%s/.lock", path);
    g_unlink(filename);
    g_free(filename);

    ret = g_rmdir(path);
    g_assert(!ret);
}
