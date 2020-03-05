/*
 * QTest testcase for SYSBUS TPM TIS
 *
 * Copyright (c) 2018 Red Hat, Inc.
 * Copyright (c) 2018 IBM Corporation
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "io/channel-socket.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "tpm-emu.h"
#include "tpm-util.h"
#include "tpm-tis-util.h"

/*
 * As the Sysbus tpm-tis-device is instantiated on the ARM virt
 * platform bus and it is the only sysbus device dynamically
 * instantiated, it gets plugged at its base address
 */
uint64_t tpm_tis_base_addr = 0xc000000;

int main(int argc, char **argv)
{
    char *tmp_path = g_dir_make_tmp("qemu-tpm-tis-device-test.XXXXXX", NULL);
    GThread *thread;
    TestState test;
    char *args;
    int ret;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);
    test.data_cond_signal = false;

    thread = g_thread_new(NULL, tpm_emu_ctrl_thread, &test);
    tpm_emu_test_wait_cond(&test);

    args = g_strdup_printf(
        "-machine virt,gic-version=max -accel tcg "
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device tpm-tis-device,tpmdev=dev",
        test.addr->u.q_unix.path);
    qtest_start(args);

    qtest_add_data_func("/tpm-tis/test_check_localities", &test,
                        tpm_tis_test_check_localities);

    qtest_add_data_func("/tpm-tis/test_check_access_reg", &test,
                        tpm_tis_test_check_access_reg);

    qtest_add_data_func("/tpm-tis/test_check_access_reg_seize", &test,
                        tpm_tis_test_check_access_reg_seize);

    qtest_add_data_func("/tpm-tis/test_check_access_reg_release", &test,
                        tpm_tis_test_check_access_reg_release);

    qtest_add_data_func("/tpm-tis/test_check_transmit", &test,
                        tpm_tis_test_check_transmit);

    ret = g_test_run();

    qtest_end();

    g_thread_join(thread);
    g_unlink(test.addr->u.q_unix.path);
    qapi_free_SocketAddress(test.addr);
    g_rmdir(tmp_path);
    g_free(tmp_path);
    g_free(args);
    return ret;
}
