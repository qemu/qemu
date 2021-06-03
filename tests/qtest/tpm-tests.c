/*
 * QTest TPM commont test code
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

#include "libqtest-single.h"
#include "tpm-tests.h"

static bool
tpm_test_swtpm_skip(void)
{
    if (!tpm_util_swtpm_has_tpm2()) {
        g_test_skip("swtpm not in PATH or missing --tpm2 support");
        return true;
    }

    return false;
}

void tpm_test_swtpm_test(const char *src_tpm_path, tx_func *tx,
                         const char *ifmodel, const char *machine_options)
{
    char *args = NULL;
    QTestState *s;
    SocketAddress *addr = NULL;
    gboolean succ;
    GPid swtpm_pid;
    GError *error = NULL;

    if (tpm_test_swtpm_skip()) {
        return;
    }

    succ = tpm_util_swtpm_start(src_tpm_path, &swtpm_pid, &addr, &error);
    g_assert_true(succ);

    args = g_strdup_printf(
        "%s "
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device %s,tpmdev=dev",
        machine_options ? : "", addr->u.q_unix.path, ifmodel);

    s = qtest_start(args);
    g_free(args);

    tpm_util_startup(s, tx);
    tpm_util_pcrextend(s, tx);

    static const unsigned char tpm_pcrread_resp[] =
        "\x80\x01\x00\x00\x00\x3e\x00\x00\x00\x00\x00\x00\x00\x16\x00\x00"
        "\x00\x01\x00\x0b\x03\x00\x04\x00\x00\x00\x00\x01\x00\x20\xf6\x85"
        "\x98\xe5\x86\x8d\xe6\x8b\x97\x29\x99\x60\xf2\x71\x7d\x17\x67\x89"
        "\xa4\x2f\x9a\xae\xa8\xc7\xb7\xaa\x79\xa8\x62\x56\xc1\xde";
    tpm_util_pcrread(s, tx, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    qtest_end();
    tpm_util_swtpm_kill(swtpm_pid);

    g_unlink(addr->u.q_unix.path);
    qapi_free_SocketAddress(addr);
}

void tpm_test_swtpm_migration_test(const char *src_tpm_path,
                                   const char *dst_tpm_path,
                                   const char *uri, tx_func *tx,
                                   const char *ifmodel,
                                   const char *machine_options)
{
    gboolean succ;
    GPid src_tpm_pid, dst_tpm_pid;
    SocketAddress *src_tpm_addr = NULL, *dst_tpm_addr = NULL;
    GError *error = NULL;
    QTestState *src_qemu, *dst_qemu;

    if (tpm_test_swtpm_skip()) {
        return;
    }

    succ = tpm_util_swtpm_start(src_tpm_path, &src_tpm_pid,
                                &src_tpm_addr, &error);
    g_assert_true(succ);

    succ = tpm_util_swtpm_start(dst_tpm_path, &dst_tpm_pid,
                                &dst_tpm_addr, &error);
    g_assert_true(succ);

    tpm_util_migration_start_qemu(&src_qemu, &dst_qemu,
                                  src_tpm_addr, dst_tpm_addr, uri,
                                  ifmodel, machine_options);

    tpm_util_startup(src_qemu, tx);
    tpm_util_pcrextend(src_qemu, tx);

    static const unsigned char tpm_pcrread_resp[] =
        "\x80\x01\x00\x00\x00\x3e\x00\x00\x00\x00\x00\x00\x00\x16\x00\x00"
        "\x00\x01\x00\x0b\x03\x00\x04\x00\x00\x00\x00\x01\x00\x20\xf6\x85"
        "\x98\xe5\x86\x8d\xe6\x8b\x97\x29\x99\x60\xf2\x71\x7d\x17\x67\x89"
        "\xa4\x2f\x9a\xae\xa8\xc7\xb7\xaa\x79\xa8\x62\x56\xc1\xde";
    tpm_util_pcrread(src_qemu, tx, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    tpm_util_migrate(src_qemu, uri);
    tpm_util_wait_for_migration_complete(src_qemu);

    tpm_util_pcrread(dst_qemu, tx, tpm_pcrread_resp,
                     sizeof(tpm_pcrread_resp));

    qtest_quit(dst_qemu);
    qtest_quit(src_qemu);

    tpm_util_swtpm_kill(dst_tpm_pid);
    g_unlink(dst_tpm_addr->u.q_unix.path);
    qapi_free_SocketAddress(dst_tpm_addr);

    tpm_util_swtpm_kill(src_tpm_pid);
    g_unlink(src_tpm_addr->u.q_unix.path);
    qapi_free_SocketAddress(src_tpm_addr);
}
