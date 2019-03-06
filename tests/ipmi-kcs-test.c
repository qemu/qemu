/*
 * IPMI KCS test cases, using the local interface.
 *
 * Copyright (c) 2012 Corey Minyard <cminyard@mvista.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"


#include "libqtest.h"

#define IPMI_IRQ        5

#define IPMI_KCS_BASE   0xca2

#define IPMI_KCS_STATUS_ABORT           0x60
#define IPMI_KCS_CMD_WRITE_START        0x61
#define IPMI_KCS_CMD_WRITE_END          0x62
#define IPMI_KCS_CMD_READ               0x68

#define IPMI_KCS_ABORTED_BY_CMD         0x01

#define IPMI_KCS_CMDREG_GET_STATE() ((kcs_get_cmdreg() >> 6) & 3)
#define IPMI_KCS_STATE_IDLE     0
#define IPMI_KCS_STATE_READ     1
#define IPMI_KCS_STATE_WRITE    2
#define IPMI_KCS_STATE_ERROR    3
#define IPMI_KCS_CMDREG_GET_CD()    ((kcs_get_cmdreg() >> 3) & 1)
#define IPMI_KCS_CMDREG_GET_ATN()   ((kcs_get_cmdreg() >> 2) & 1)
#define IPMI_KCS_CMDREG_GET_IBF()   ((kcs_get_cmdreg() >> 1) & 1)
#define IPMI_KCS_CMDREG_GET_OBF()   ((kcs_get_cmdreg() >> 0) & 1)

static int kcs_ints_enabled;

static uint8_t kcs_get_cmdreg(void)
{
    return inb(IPMI_KCS_BASE + 1);
}

static void kcs_write_cmdreg(uint8_t val)
{
    outb(IPMI_KCS_BASE + 1, val);
}

static uint8_t kcs_get_datareg(void)
{
    return inb(IPMI_KCS_BASE);
}

static void kcs_write_datareg(uint8_t val)
{
    outb(IPMI_KCS_BASE, val);
}

static void kcs_wait_ibf(void)
{
    unsigned int count = 1000;
    while (IPMI_KCS_CMDREG_GET_IBF() != 0) {
        g_assert(--count != 0);
    }
}

static void kcs_wait_obf(void)
{
    unsigned int count = 1000;
    while (IPMI_KCS_CMDREG_GET_OBF() == 0) {
        g_assert(--count != 0);
    }
}

static void kcs_clear_obf(void)
{
    if (kcs_ints_enabled) {
        g_assert(get_irq(IPMI_IRQ));
    } else {
        g_assert(!get_irq(IPMI_IRQ));
    }
    g_assert(IPMI_KCS_CMDREG_GET_OBF() == 1);
    kcs_get_datareg();
    g_assert(IPMI_KCS_CMDREG_GET_OBF() == 0);
    g_assert(!get_irq(IPMI_IRQ));
}

static void kcs_check_state(uint8_t state)
{
    g_assert(IPMI_KCS_CMDREG_GET_STATE() == state);
}

static void kcs_cmd(uint8_t *cmd, unsigned int cmd_len,
                    uint8_t *rsp, unsigned int *rsp_len)
{
    unsigned int i, j = 0;

    /* Should be idle */
    g_assert(kcs_get_cmdreg() == 0);

    kcs_write_cmdreg(IPMI_KCS_CMD_WRITE_START);
    kcs_wait_ibf();
    kcs_check_state(IPMI_KCS_STATE_WRITE);
    kcs_clear_obf();
    for (i = 0; i < cmd_len; i++) {
        kcs_write_datareg(cmd[i]);
        kcs_wait_ibf();
        kcs_check_state(IPMI_KCS_STATE_WRITE);
        kcs_clear_obf();
    }
    kcs_write_cmdreg(IPMI_KCS_CMD_WRITE_END);
    kcs_wait_ibf();
    kcs_check_state(IPMI_KCS_STATE_WRITE);
    kcs_clear_obf();
    kcs_write_datareg(0);
 next_read_byte:
    kcs_wait_ibf();
    switch (IPMI_KCS_CMDREG_GET_STATE()) {
    case IPMI_KCS_STATE_READ:
        kcs_wait_obf();
        g_assert(j < *rsp_len);
        rsp[j++] = kcs_get_datareg();
        kcs_write_datareg(IPMI_KCS_CMD_READ);
        goto next_read_byte;
        break;

    case IPMI_KCS_STATE_IDLE:
        kcs_wait_obf();
        kcs_get_datareg();
        break;

    default:
        g_assert(0);
    }
    *rsp_len = j;
}

static void kcs_abort(uint8_t *cmd, unsigned int cmd_len,
                      uint8_t *rsp, unsigned int *rsp_len)
{
    unsigned int i, j = 0;
    unsigned int retries = 4;

    /* Should be idle */
    g_assert(kcs_get_cmdreg() == 0);

    kcs_write_cmdreg(IPMI_KCS_CMD_WRITE_START);
    kcs_wait_ibf();
    kcs_check_state(IPMI_KCS_STATE_WRITE);
    kcs_clear_obf();
    for (i = 0; i < cmd_len; i++) {
        kcs_write_datareg(cmd[i]);
        kcs_wait_ibf();
        kcs_check_state(IPMI_KCS_STATE_WRITE);
        kcs_clear_obf();
    }
    kcs_write_cmdreg(IPMI_KCS_CMD_WRITE_END);
    kcs_wait_ibf();
    kcs_check_state(IPMI_KCS_STATE_WRITE);
    kcs_clear_obf();
    kcs_write_datareg(0);
    kcs_wait_ibf();
    switch (IPMI_KCS_CMDREG_GET_STATE()) {
    case IPMI_KCS_STATE_READ:
        kcs_wait_obf();
        g_assert(j < *rsp_len);
        rsp[j++] = kcs_get_datareg();
        kcs_write_datareg(IPMI_KCS_CMD_READ);
        break;

    default:
        g_assert(0);
    }

    /* Start the abort here */
 retry_abort:
    g_assert(retries > 0);

    kcs_wait_ibf();
    kcs_write_cmdreg(IPMI_KCS_STATUS_ABORT);
    kcs_wait_ibf();
    kcs_clear_obf();
    kcs_write_datareg(0);
    kcs_wait_ibf();
    if (IPMI_KCS_CMDREG_GET_STATE() != IPMI_KCS_STATE_READ) {
        retries--;
        goto retry_abort;
    }
    kcs_wait_obf();
    rsp[0] = kcs_get_datareg();
    kcs_write_datareg(IPMI_KCS_CMD_READ);
    kcs_wait_ibf();
    if (IPMI_KCS_CMDREG_GET_STATE() != IPMI_KCS_STATE_IDLE) {
        retries--;
        goto retry_abort;
    }
    kcs_wait_obf();
    kcs_clear_obf();

    *rsp_len = j;
}


static uint8_t get_dev_id_cmd[] = { 0x18, 0x01 };
static uint8_t get_dev_id_rsp[] = { 0x1c, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
                                    0x02, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00 };

/*
 * Send a get_device_id to do a basic test.
 */
static void test_kcs_base(void)
{
    uint8_t rsp[20];
    unsigned int rsplen = sizeof(rsp);

    kcs_cmd(get_dev_id_cmd, sizeof(get_dev_id_cmd), rsp, &rsplen);
    g_assert(rsplen == sizeof(get_dev_id_rsp));
    g_assert(memcmp(get_dev_id_rsp, rsp, rsplen) == 0);
}

/*
 * Abort a kcs operation while reading
 */
static void test_kcs_abort(void)
{
    uint8_t rsp[20];
    unsigned int rsplen = sizeof(rsp);

    kcs_abort(get_dev_id_cmd, sizeof(get_dev_id_cmd), rsp, &rsplen);
    g_assert(rsp[0] == IPMI_KCS_ABORTED_BY_CMD);
}

static uint8_t set_bmc_globals_cmd[] = { 0x18, 0x2e, 0x0f };
static uint8_t set_bmc_globals_rsp[] = { 0x1c, 0x2e, 0x00 };

/*
 * Enable interrupts
 */
static void test_enable_irq(void)
{
    uint8_t rsp[20];
    unsigned int rsplen = sizeof(rsp);

    kcs_cmd(set_bmc_globals_cmd, sizeof(set_bmc_globals_cmd), rsp, &rsplen);
    g_assert(rsplen == sizeof(set_bmc_globals_rsp));
    g_assert(memcmp(set_bmc_globals_rsp, rsp, rsplen) == 0);
    kcs_ints_enabled = 1;
}

int main(int argc, char **argv)
{
    char *cmdline;
    int ret;

    /* Run the tests */
    g_test_init(&argc, &argv, NULL);

    cmdline = g_strdup_printf("-device ipmi-bmc-sim,id=bmc0"
                              " -device isa-ipmi-kcs,bmc=bmc0");
    qtest_start(cmdline);
    g_free(cmdline);
    qtest_irq_intercept_in(global_qtest, "ioapic");
    qtest_add_func("/ipmi/local/kcs_base", test_kcs_base);
    qtest_add_func("/ipmi/local/kcs_abort", test_kcs_abort);
    qtest_add_func("/ipmi/local/kcs_enable_irq", test_enable_irq);
    qtest_add_func("/ipmi/local/kcs_base_irq", test_kcs_base);
    qtest_add_func("/ipmi/local/kcs_abort_irq", test_kcs_abort);
    ret = g_test_run();
    qtest_quit(global_qtest);

    return ret;
}
