/*
 * QEMU ICH9 TCO emulation tests
 *
 * Copyright (c) 2015 Paulo Alcantara <pcacjr@zytor.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "qapi/qmp/qdict.h"
#include "hw/pci/pci_regs.h"
#include "hw/southbridge/ich9.h"
#include "hw/acpi/ich9.h"
#include "hw/acpi/ich9_tco.h"

#define RCBA_BASE_ADDR    0xfed1c000
#define PM_IO_BASE_ADDR   0xb000

enum {
    TCO_RLD_DEFAULT         = 0x0000,
    TCO_DAT_IN_DEFAULT      = 0x00,
    TCO_DAT_OUT_DEFAULT     = 0x00,
    TCO1_STS_DEFAULT        = 0x0000,
    TCO2_STS_DEFAULT        = 0x0000,
    TCO1_CNT_DEFAULT        = 0x0000,
    TCO2_CNT_DEFAULT        = 0x0008,
    TCO_MESSAGE1_DEFAULT    = 0x00,
    TCO_MESSAGE2_DEFAULT    = 0x00,
    TCO_WDCNT_DEFAULT       = 0x00,
    TCO_TMR_DEFAULT         = 0x0004,
    SW_IRQ_GEN_DEFAULT      = 0x03,
};

#define TCO_SECS_TO_TICKS(secs)     (((secs) * 10) / 6)
#define TCO_TICKS_TO_SECS(ticks)    (((ticks) * 6) / 10)

typedef struct {
    const char *args;
    bool noreboot;
    QPCIDevice *dev;
    QPCIBar tco_io_bar;
    QPCIBus *bus;
    QTestState *qts;
} TestData;

static void test_end(TestData *d)
{
    g_free(d->dev);
    qpci_free_pc(d->bus);
    qtest_quit(d->qts);
}

static void test_init(TestData *d)
{
    QTestState *qs;

    qs = qtest_initf("-machine q35 %s %s",
                     d->noreboot ? "-global ICH9-LPC.noreboot=true" : "",
                     !d->args ? "" : d->args);
    qtest_irq_intercept_in(qs, "ioapic");

    d->bus = qpci_new_pc(qs, NULL);
    d->dev = qpci_device_find(d->bus, QPCI_DEVFN(0x1f, 0x00));
    g_assert(d->dev != NULL);

    qpci_device_enable(d->dev);

    /* set ACPI PM I/O space base address */
    qpci_config_writel(d->dev, ICH9_LPC_PMBASE, PM_IO_BASE_ADDR | 0x1);
    /* enable ACPI I/O */
    qpci_config_writeb(d->dev, ICH9_LPC_ACPI_CTRL, 0x80);
    /* set Root Complex BAR */
    qpci_config_writel(d->dev, ICH9_LPC_RCBA, RCBA_BASE_ADDR | 0x1);

    d->tco_io_bar = qpci_legacy_iomap(d->dev, PM_IO_BASE_ADDR + 0x60);
    d->qts = qs;
}

static void stop_tco(const TestData *d)
{
    uint32_t val;

    val = qpci_io_readw(d->dev, d->tco_io_bar, TCO1_CNT);
    val |= TCO_TMR_HLT;
    qpci_io_writew(d->dev, d->tco_io_bar, TCO1_CNT, val);
}

static void start_tco(const TestData *d)
{
    uint32_t val;

    val = qpci_io_readw(d->dev, d->tco_io_bar, TCO1_CNT);
    val &= ~TCO_TMR_HLT;
    qpci_io_writew(d->dev, d->tco_io_bar, TCO1_CNT, val);
}

static void load_tco(const TestData *d)
{
    qpci_io_writew(d->dev, d->tco_io_bar, TCO_RLD, 4);
}

static void set_tco_timeout(const TestData *d, uint16_t ticks)
{
    qpci_io_writew(d->dev, d->tco_io_bar, TCO_TMR, ticks);
}

static void clear_tco_status(const TestData *d)
{
    qpci_io_writew(d->dev, d->tco_io_bar, TCO1_STS, 0x0008);
    qpci_io_writew(d->dev, d->tco_io_bar, TCO2_STS, 0x0002);
    qpci_io_writew(d->dev, d->tco_io_bar, TCO2_STS, 0x0004);
}

static void reset_on_second_timeout(const TestData *td, bool enable)
{
    uint32_t val;

    val = qtest_readl(td->qts, RCBA_BASE_ADDR + ICH9_CC_GCS);
    if (enable) {
        val &= ~ICH9_CC_GCS_NO_REBOOT;
    } else {
        val |= ICH9_CC_GCS_NO_REBOOT;
    }
    qtest_writel(td->qts, RCBA_BASE_ADDR + ICH9_CC_GCS, val);
}

static void test_tco_defaults(void)
{
    TestData d;

    d.args = NULL;
    d.noreboot = true;
    test_init(&d);
    g_assert_cmpint(qpci_io_readw(d.dev, d.tco_io_bar, TCO_RLD), ==,
                    TCO_RLD_DEFAULT);
    /* TCO_DAT_IN & TCO_DAT_OUT */
    g_assert_cmpint(qpci_io_readw(d.dev, d.tco_io_bar, TCO_DAT_IN), ==,
                    (TCO_DAT_OUT_DEFAULT << 8) | TCO_DAT_IN_DEFAULT);
    /* TCO1_STS & TCO2_STS */
    g_assert_cmpint(qpci_io_readl(d.dev, d.tco_io_bar, TCO1_STS), ==,
                    (TCO2_STS_DEFAULT << 16) | TCO1_STS_DEFAULT);
    /* TCO1_CNT & TCO2_CNT */
    g_assert_cmpint(qpci_io_readl(d.dev, d.tco_io_bar, TCO1_CNT), ==,
                    (TCO2_CNT_DEFAULT << 16) | TCO1_CNT_DEFAULT);
    /* TCO_MESSAGE1 & TCO_MESSAGE2 */
    g_assert_cmpint(qpci_io_readw(d.dev, d.tco_io_bar, TCO_MESSAGE1), ==,
                    (TCO_MESSAGE2_DEFAULT << 8) | TCO_MESSAGE1_DEFAULT);
    g_assert_cmpint(qpci_io_readb(d.dev, d.tco_io_bar, TCO_WDCNT), ==,
                    TCO_WDCNT_DEFAULT);
    g_assert_cmpint(qpci_io_readb(d.dev, d.tco_io_bar, SW_IRQ_GEN), ==,
                    SW_IRQ_GEN_DEFAULT);
    g_assert_cmpint(qpci_io_readw(d.dev, d.tco_io_bar, TCO_TMR), ==,
                    TCO_TMR_DEFAULT);
    test_end(&d);
}

static void test_tco_timeout(void)
{
    TestData d;
    const uint16_t ticks = TCO_SECS_TO_TICKS(4);
    uint32_t val;
    int ret;

    d.args = NULL;
    d.noreboot = true;
    test_init(&d);

    stop_tco(&d);
    clear_tco_status(&d);
    reset_on_second_timeout(&d, false);
    set_tco_timeout(&d, ticks);
    load_tco(&d);
    start_tco(&d);
    qtest_clock_step(d.qts, ticks * TCO_TICK_NSEC);

    /* test first timeout */
    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS);
    ret = val & TCO_TIMEOUT ? 1 : 0;
    g_assert(ret == 1);

    /* test clearing timeout bit */
    val |= TCO_TIMEOUT;
    qpci_io_writew(d.dev, d.tco_io_bar, TCO1_STS, val);
    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS);
    ret = val & TCO_TIMEOUT ? 1 : 0;
    g_assert(ret == 0);

    /* test second timeout */
    qtest_clock_step(d.qts, ticks * TCO_TICK_NSEC);
    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS);
    ret = val & TCO_TIMEOUT ? 1 : 0;
    g_assert(ret == 1);
    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO2_STS);
    ret = val & TCO_SECOND_TO_STS ? 1 : 0;
    g_assert(ret == 1);

    stop_tco(&d);
    test_end(&d);
}

static void test_tco_max_timeout(void)
{
    TestData d;
    const uint16_t ticks = 0xffff;
    uint32_t val;
    int ret;

    d.args = NULL;
    d.noreboot = true;
    test_init(&d);

    stop_tco(&d);
    clear_tco_status(&d);
    reset_on_second_timeout(&d, false);
    set_tco_timeout(&d, ticks);
    load_tco(&d);
    start_tco(&d);
    qtest_clock_step(d.qts, ((ticks & TCO_TMR_MASK) - 1) * TCO_TICK_NSEC);

    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO_RLD);
    g_assert_cmpint(val & TCO_RLD_MASK, ==, 1);
    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS);
    ret = val & TCO_TIMEOUT ? 1 : 0;
    g_assert(ret == 0);
    qtest_clock_step(d.qts, TCO_TICK_NSEC);
    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS);
    ret = val & TCO_TIMEOUT ? 1 : 0;
    g_assert(ret == 1);

    stop_tco(&d);
    test_end(&d);
}

static QDict *get_watchdog_action(const TestData *td)
{
    QDict *ev = qtest_qmp_eventwait_ref(td->qts, "WATCHDOG");
    QDict *data;

    data = qdict_get_qdict(ev, "data");
    qobject_ref(data);
    qobject_unref(ev);
    return data;
}

static void test_tco_second_timeout_pause(void)
{
    TestData td;
    const uint16_t ticks = TCO_SECS_TO_TICKS(32);
    QDict *ad;

    td.args = "-watchdog-action pause";
    td.noreboot = false;
    test_init(&td);

    stop_tco(&td);
    clear_tco_status(&td);
    reset_on_second_timeout(&td, true);
    set_tco_timeout(&td, TCO_SECS_TO_TICKS(16));
    load_tco(&td);
    start_tco(&td);
    qtest_clock_step(td.qts, ticks * TCO_TICK_NSEC * 2);
    ad = get_watchdog_action(&td);
    g_assert(!strcmp(qdict_get_str(ad, "action"), "pause"));
    qobject_unref(ad);

    stop_tco(&td);
    test_end(&td);
}

static void test_tco_second_timeout_reset(void)
{
    TestData td;
    const uint16_t ticks = TCO_SECS_TO_TICKS(16);
    QDict *ad;

    td.args = "-watchdog-action reset";
    td.noreboot = false;
    test_init(&td);

    stop_tco(&td);
    clear_tco_status(&td);
    reset_on_second_timeout(&td, true);
    set_tco_timeout(&td, TCO_SECS_TO_TICKS(16));
    load_tco(&td);
    start_tco(&td);
    qtest_clock_step(td.qts, ticks * TCO_TICK_NSEC * 2);
    ad = get_watchdog_action(&td);
    g_assert(!strcmp(qdict_get_str(ad, "action"), "reset"));
    qobject_unref(ad);

    stop_tco(&td);
    test_end(&td);
}

static void test_tco_second_timeout_shutdown(void)
{
    TestData td;
    const uint16_t ticks = TCO_SECS_TO_TICKS(128);
    QDict *ad;

    td.args = "-watchdog-action shutdown";
    td.noreboot = false;
    test_init(&td);

    stop_tco(&td);
    clear_tco_status(&td);
    reset_on_second_timeout(&td, true);
    set_tco_timeout(&td, ticks);
    load_tco(&td);
    start_tco(&td);
    qtest_clock_step(td.qts, ticks * TCO_TICK_NSEC * 2);
    ad = get_watchdog_action(&td);
    g_assert(!strcmp(qdict_get_str(ad, "action"), "shutdown"));
    qobject_unref(ad);

    stop_tco(&td);
    test_end(&td);
}

static void test_tco_second_timeout_none(void)
{
    TestData td;
    const uint16_t ticks = TCO_SECS_TO_TICKS(256);
    QDict *ad;

    td.args = "-watchdog-action none";
    td.noreboot = false;
    test_init(&td);

    stop_tco(&td);
    clear_tco_status(&td);
    reset_on_second_timeout(&td, true);
    set_tco_timeout(&td, ticks);
    load_tco(&td);
    start_tco(&td);
    qtest_clock_step(td.qts, ticks * TCO_TICK_NSEC * 2);
    ad = get_watchdog_action(&td);
    g_assert(!strcmp(qdict_get_str(ad, "action"), "none"));
    qobject_unref(ad);

    stop_tco(&td);
    test_end(&td);
}

static void test_tco_ticks_counter(void)
{
    TestData d;
    uint16_t ticks = TCO_SECS_TO_TICKS(8);
    uint16_t rld;

    d.args = NULL;
    d.noreboot = true;
    test_init(&d);

    stop_tco(&d);
    clear_tco_status(&d);
    reset_on_second_timeout(&d, false);
    set_tco_timeout(&d, ticks);
    load_tco(&d);
    start_tco(&d);

    do {
        rld = qpci_io_readw(d.dev, d.tco_io_bar, TCO_RLD) & TCO_RLD_MASK;
        g_assert_cmpint(rld, ==, ticks);
        qtest_clock_step(d.qts, TCO_TICK_NSEC);
        ticks--;
    } while (!(qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS) & TCO_TIMEOUT));

    stop_tco(&d);
    test_end(&d);
}

static void test_tco1_control_bits(void)
{
    TestData d;
    uint16_t val;

    d.args = NULL;
    d.noreboot = true;
    test_init(&d);

    val = TCO_LOCK;
    qpci_io_writew(d.dev, d.tco_io_bar, TCO1_CNT, val);
    val &= ~TCO_LOCK;
    qpci_io_writew(d.dev, d.tco_io_bar, TCO1_CNT, val);
    g_assert_cmpint(qpci_io_readw(d.dev, d.tco_io_bar, TCO1_CNT), ==,
                    TCO_LOCK);
    test_end(&d);
}

static void test_tco1_status_bits(void)
{
    TestData d;
    uint16_t ticks = 8;
    uint16_t val;
    int ret;

    d.args = NULL;
    d.noreboot = true;
    test_init(&d);

    stop_tco(&d);
    clear_tco_status(&d);
    reset_on_second_timeout(&d, false);
    set_tco_timeout(&d, ticks);
    load_tco(&d);
    start_tco(&d);
    qtest_clock_step(d.qts, ticks * TCO_TICK_NSEC);

    qpci_io_writeb(d.dev, d.tco_io_bar, TCO_DAT_IN, 0);
    qpci_io_writeb(d.dev, d.tco_io_bar, TCO_DAT_OUT, 0);
    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS);
    ret = val & (TCO_TIMEOUT | SW_TCO_SMI | TCO_INT_STS) ? 1 : 0;
    g_assert(ret == 1);
    qpci_io_writew(d.dev, d.tco_io_bar, TCO1_STS, val);
    g_assert_cmpint(qpci_io_readw(d.dev, d.tco_io_bar, TCO1_STS), ==, 0);
    test_end(&d);
}

static void test_tco2_status_bits(void)
{
    TestData d;
    uint16_t ticks = 8;
    uint16_t val;
    int ret;

    d.args = NULL;
    d.noreboot = true;
    test_init(&d);

    stop_tco(&d);
    clear_tco_status(&d);
    reset_on_second_timeout(&d, true);
    set_tco_timeout(&d, ticks);
    load_tco(&d);
    start_tco(&d);
    qtest_clock_step(d.qts, ticks * TCO_TICK_NSEC * 2);

    val = qpci_io_readw(d.dev, d.tco_io_bar, TCO2_STS);
    ret = val & (TCO_SECOND_TO_STS | TCO_BOOT_STS) ? 1 : 0;
    g_assert(ret == 1);
    qpci_io_writew(d.dev, d.tco_io_bar, TCO2_STS, val);
    g_assert_cmpint(qpci_io_readw(d.dev, d.tco_io_bar, TCO2_STS), ==, 0);
    test_end(&d);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("tco/defaults", test_tco_defaults);
    qtest_add_func("tco/timeout/no_action", test_tco_timeout);
    qtest_add_func("tco/timeout/no_action/max", test_tco_max_timeout);
    qtest_add_func("tco/second_timeout/pause", test_tco_second_timeout_pause);
    qtest_add_func("tco/second_timeout/reset", test_tco_second_timeout_reset);
    qtest_add_func("tco/second_timeout/shutdown",
                   test_tco_second_timeout_shutdown);
    qtest_add_func("tco/second_timeout/none", test_tco_second_timeout_none);
    qtest_add_func("tco/counter", test_tco_ticks_counter);
    qtest_add_func("tco/tco1_control/bits", test_tco1_control_bits);
    qtest_add_func("tco/tco1_status/bits", test_tco1_status_bits);
    qtest_add_func("tco/tco2_status/bits", test_tco2_status_bits);
    return g_test_run();
}
