/*
 * QTest testcase for TPM CRB
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Authors:
 *   Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "hw/acpi/tpm.h"
#include "io/channel-socket.h"
#include "libqtest-single.h"
#include "qemu/module.h"
#include "tpm-emu.h"

/* Not used but needed for linking */
uint64_t tpm_tis_base_addr = TPM_TIS_ADDR_BASE;

#define TPM_CMD "\x80\x01\x00\x00\x00\x0c\x00\x00\x01\x44\x00\x00"

static void tpm_crb_test(const void *data)
{
    const TPMTestState *s = data;
    uint32_t intfid = readl(TPM_CRB_ADDR_BASE + A_CRB_INTF_ID);
    uint32_t csize = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_CMD_SIZE);
    uint64_t caddr = readq(TPM_CRB_ADDR_BASE + A_CRB_CTRL_CMD_LADDR);
    uint32_t rsize = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_RSP_SIZE);
    uint64_t raddr = readq(TPM_CRB_ADDR_BASE + A_CRB_CTRL_RSP_ADDR);
    uint8_t locstate = readb(TPM_CRB_ADDR_BASE + A_CRB_LOC_STATE);
    uint32_t locctrl = readl(TPM_CRB_ADDR_BASE + A_CRB_LOC_CTRL);
    uint32_t locsts = readl(TPM_CRB_ADDR_BASE + A_CRB_LOC_STS);
    uint32_t sts = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_STS);

    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceType), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceVersion), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapCRBIdleBypass), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapDataXferSizeSupport),
                    ==, 3);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapFIFO), ==, 0);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, CapCRB), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, InterfaceSelector), ==, 1);
    g_assert_cmpint(FIELD_EX32(intfid, CRB_INTF_ID, RID), ==, 0);

    g_assert_cmpint(csize, >=, 128);
    g_assert_cmpint(rsize, >=, 128);
    g_assert_cmpint(caddr, >, TPM_CRB_ADDR_BASE);
    g_assert_cmpint(raddr, >, TPM_CRB_ADDR_BASE);

    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmEstablished), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, locAssigned), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, activeLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, reserved), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmRegValidSts), ==, 1);

    g_assert_cmpint(locctrl, ==, 0);

    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, Granted), ==, 0);
    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, beenSeized), ==, 0);

    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 1);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    /* request access to locality 0 */
    writeb(TPM_CRB_ADDR_BASE + A_CRB_LOC_CTRL, 1);

    /* granted bit must be set now */
    locsts = readl(TPM_CRB_ADDR_BASE + A_CRB_LOC_STS);
    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, Granted), ==, 1);
    g_assert_cmpint(FIELD_EX32(locsts, CRB_LOC_STS, beenSeized), ==, 0);

    /* we must have an assigned locality */
    locstate = readb(TPM_CRB_ADDR_BASE + A_CRB_LOC_STATE);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmEstablished), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, locAssigned), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, activeLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, reserved), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmRegValidSts), ==, 1);

    /* set into ready state */
    writel(TPM_CRB_ADDR_BASE + A_CRB_CTRL_REQ, 1);

    /* TPM must not be in the idle state */
    sts = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 0);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    memwrite(caddr, TPM_CMD, sizeof(TPM_CMD));

    uint32_t start = 1;
    uint64_t end_time = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    writel(TPM_CRB_ADDR_BASE + A_CRB_CTRL_START, start);
    do {
        start = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_START);
        if ((start & 1) == 0) {
            break;
        }
    } while (g_get_monotonic_time() < end_time);
    start = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_START);
    g_assert_cmpint(start & 1, ==, 0);

    /* TPM must still not be in the idle state */
    sts = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 0);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    struct tpm_hdr tpm_msg;
    memread(raddr, &tpm_msg, sizeof(tpm_msg));
    g_assert_cmpmem(&tpm_msg, sizeof(tpm_msg), s->tpm_msg, sizeof(*s->tpm_msg));

    /* set TPM into idle state */
    writel(TPM_CRB_ADDR_BASE + A_CRB_CTRL_REQ, 2);

    /* idle state must be indicated now */
    sts = readl(TPM_CRB_ADDR_BASE + A_CRB_CTRL_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmIdle), ==, 1);
    g_assert_cmpint(FIELD_EX32(sts, CRB_CTRL_STS, tpmSts), ==, 0);

    /* relinquish locality */
    writel(TPM_CRB_ADDR_BASE + A_CRB_LOC_CTRL, 2);

    /* Granted flag must be cleared */
    sts = readl(TPM_CRB_ADDR_BASE + A_CRB_LOC_STS);
    g_assert_cmpint(FIELD_EX32(sts, CRB_LOC_STS, Granted), ==, 0);
    g_assert_cmpint(FIELD_EX32(sts, CRB_LOC_STS, beenSeized), ==, 0);

    /* no locality may be assigned */
    locstate = readb(TPM_CRB_ADDR_BASE + A_CRB_LOC_STATE);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmEstablished), ==, 1);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, locAssigned), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, activeLocality), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, reserved), ==, 0);
    g_assert_cmpint(FIELD_EX32(locstate, CRB_LOC_STATE, tpmRegValidSts), ==, 1);

}

int main(int argc, char **argv)
{
    int ret;
    char *args, *tmp_path = g_dir_make_tmp("qemu-tpm-crb-test.XXXXXX", NULL);
    GThread *thread;
    TPMTestState test;

    module_call_init(MODULE_INIT_QOM);
    g_test_init(&argc, &argv, NULL);

    test.addr = g_new0(SocketAddress, 1);
    test.addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    test.addr->u.q_unix.path = g_build_filename(tmp_path, "sock", NULL);
    g_mutex_init(&test.data_mutex);
    g_cond_init(&test.data_cond);
    test.data_cond_signal = false;
    test.tpm_version = TPM_VERSION_2_0;

    thread = g_thread_new(NULL, tpm_emu_ctrl_thread, &test);
    tpm_emu_test_wait_cond(&test);

    args = g_strdup_printf(
        "-chardev socket,id=chr,path=%s "
        "-tpmdev emulator,id=dev,chardev=chr "
        "-device tpm-crb,tpmdev=dev",
        test.addr->u.q_unix.path);
    qtest_start(args);

    qtest_add_data_func("/tpm-crb/test", &test, tpm_crb_test);
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
