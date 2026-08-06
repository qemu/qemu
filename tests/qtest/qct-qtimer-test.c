/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcase for the QCT QTimer
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/hexagon/hexagon.h"
#include "qemu/bitops.h"

#include "hw/hexagon/machine_cfg_v68n_1024.h.inc"
#include "hw/hexagon/machine_cfg_v66g_1024.h.inc"

#define QTIMER_DEFAULT_FREQ_HZ 19200000ULL

#define QCT_QTIMER_CNTPCT_LO (0x000)
#define QCT_QTIMER_CNTPCT_HI (0x004)
#define QCT_QTIMER_CNT_FREQ (0x010)
#define QCT_QTIMER_CNTP_CVAL_LO (0x020)
#define QCT_QTIMER_CNTP_TVAL (0x028)
#define QCT_QTIMER_CNTP_CTL (0x02c)
#define QCT_QTIMER_CNTP_CTL_ENABLE (1 << 0)

#define QTIMER_FRAME_STRIDE 0x1000
/* Frames instantiated by hex-subsys, and the L2VIC input frame 0 drives. */
#define QTIMER_NR_FRAMES 3
#define QTIMER_L2VIC_IRQ_BASE 2

#define QCT_QTIMER_AC_CNTFRQ (0x000)
#define QCT_QTIMER_AC_CNTSR (0x004)
#define QCT_QTIMER_AC_CNTTID_0 (0x08)
#define QCT_QTIMER_AC_CNTACR_START (0x40)
#define QCT_QTIMER_AC_CNTACR_ALL (0x3f)
#define QCT_QTIMER_AC_CNTACR_RWPT (1 << 5) /* R/W of CNTP_* regs */
#define QCT_QTIMER_AC_CNTACR_RFRQ (1 << 2) /* R/W of CNTFRQ register */
#define QCT_QTIMER_AC_CNTACR_RPCT (1 << 0) /* R/W of CNTPCT register */

static uint64_t qtimer_view_base;
static uint64_t qtimer_ac_base;

#define TIMER_TEST_OFFSET 1000
/* TIMER_TEST_OFFSET ticks expressed in nanoseconds of QEMU_CLOCK_VIRTUAL */
#define TIMER_TEST_NS \
    ((TIMER_TEST_OFFSET * 1000000000ULL) / QTIMER_DEFAULT_FREQ_HZ)

static uint32_t qtimer_read32(uint64_t base, uint32_t offset)
{
    return readl(base + offset);
}

static void qtimer_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    writel(base + offset, value);
}

static uint64_t qtimer_read64(uint64_t base, uint32_t offset)
{
    uint32_t lo = qtimer_read32(base, offset);
    uint32_t hi = qtimer_read32(base, offset + 4);

    return ((uint64_t)hi << 32) | lo;
}

static void qtimer_write64(uint64_t base, uint32_t offset, uint64_t value)
{
    qtimer_write32(base, offset, extract64(value, 0, 32));
    qtimer_write32(base, offset + 4, extract64(value, 32, 32));
}

static void test_qtimer_basic_access(void)
{
    uint32_t val;

    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);
}

static void test_qtimer_multiple_frames(void)
{
    uint32_t val;
    uint64_t frame0_base = qtimer_view_base;
    uint64_t frame1_base = qtimer_view_base + 0x1000;

    val = qtimer_read32(frame0_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);

    val = qtimer_read32(frame1_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);
}

static void test_qtimer_register_reads(void)
{
    qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_TVAL);
}

static void test_qtimer_control_registers(void)
{
    uint32_t ctl_val;
    uint64_t cval_before, cval_after;

    cval_before = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_TVAL, 1000);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 1);
    ctl_val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(ctl_val & 1, ==, 1);

    /* CVAL should be greater than before since we set TVAL */
    cval_after = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(cval_after, >, cval_before);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 0);
    ctl_val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(ctl_val & 1, ==, 0);
}

static void test_qtimer_cval_access(void)
{
    uint64_t current_time, test_cval, read_cval;

    current_time = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    test_cval = current_time + 10000;

    qtimer_write64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO, test_cval);
    read_cval = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(read_cval, ==, test_cval);
}

static void test_qtimer_counter_progression(void)
{
    uint32_t freq;
    uint64_t count1, count2;

    /*
     * In qtest mode the virtual clock does not advance on its own, so
     * reading the counter twice must give the same value.
     */
    count1 = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    count2 = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(count2, ==, count1);

    freq = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(freq, ==, QTIMER_DEFAULT_FREQ_HZ);
}

static void test_qtimer_timer_behavior(void)
{
    uint64_t current_count, target_count, read_cval, new_count;
    uint64_t ctl_val, count_after_disable;

    current_count = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);

    target_count = current_count + TIMER_TEST_OFFSET;
    qtimer_write64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO, target_count);

    read_cval = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(read_cval, ==, target_count);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 1);

    ctl_val = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    /* EN set, IMASK clear, ISTAT not yet pending */
    g_assert_cmpuint(ctl_val, ==, 0x1);

    /* Step forward but not past the target */
    qtest_clock_step(global_qtest, TIMER_TEST_NS / 2);
    new_count = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(new_count, >=, current_count);

    /* Step past the target */
    qtest_clock_step(global_qtest, TIMER_TEST_NS);
    new_count = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(new_count, >=, target_count);

    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 0);

    ctl_val = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    /* EN cleared, ISTAT set since new_count >= target_count */
    g_assert_cmpuint(ctl_val, ==, 0x4);

    /*
     * CNTPCT runs independently of CNTP_CTL.EN: only the compare/IRQ
     * logic is gated by EN, so the counter must keep advancing.
     */
    qtest_clock_step(global_qtest, TIMER_TEST_NS / 2);
    count_after_disable = qtimer_read64(qtimer_view_base,
                                        QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(count_after_disable, >, new_count);

    /* ISTAT remains set while CNTPCT >= CVAL, even with EN=0 */
    ctl_val = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(ctl_val, ==, 0x4);
}

/* Test the access-control region: CNTFRQ, CNTSR, CNTTID_0, CNTACR frame 0 */
static void test_qtimer_ac_region(void)
{
    uint32_t freq, sr, tid0, acr0;

    freq = qtimer_read32(qtimer_ac_base, QCT_QTIMER_AC_CNTFRQ);
    g_assert_cmpuint(freq, ==, QTIMER_DEFAULT_FREQ_HZ);

    /* A write of 0 to CNTFRQ must be ignored (freq-hz must stay nonzero). */
    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTFRQ, 0);
    freq = qtimer_read32(qtimer_ac_base, QCT_QTIMER_AC_CNTFRQ);
    g_assert_cmpuint(freq, ==, QTIMER_DEFAULT_FREQ_HZ);

    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTSR, 0x3);
    sr = qtimer_read32(qtimer_ac_base, QCT_QTIMER_AC_CNTSR);
    g_assert_cmpuint(sr, ==, 0x3);

    tid0 = qtimer_read32(qtimer_ac_base, QCT_QTIMER_AC_CNTTID_0);
    g_assert_cmpuint(tid0, ==, 0x111);

    /* CNTACR for frame 0 defaults to full read/write permissions. */
    acr0 = qtimer_read32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START);
    g_assert_cmpuint(acr0, !=, 0);

    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START, 0);
    acr0 = qtimer_read32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START);
    g_assert_cmpuint(acr0, ==, 0);

    /* Restore full permissions so later view-region tests keep working. */
    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START,
                   QCT_QTIMER_AC_CNTACR_ALL);
}

static void test_qtimer_access_denied(void)
{
    uint32_t acr_all = QCT_QTIMER_AC_CNTACR_ALL;
    uint64_t cval, saved_cval;
    uint32_t val;

    /* Park CVAL at a known nonzero value while access is still permitted. */
    saved_cval = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTPCT_LO) + 10000;
    qtimer_write64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO, saved_cval);
    g_assert_cmpuint(qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO),
                     ==, saved_cval);

    /* Clearing RFRQ denies CNTFRQ reads. */
    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START,
                   acr_all & ~QCT_QTIMER_AC_CNTACR_RFRQ);
    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, 0);

    /* Clearing RPCT denies CNTPCT reads, both halves. */
    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START,
                   acr_all & ~QCT_QTIMER_AC_CNTACR_RPCT);
    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTPCT_LO);
    g_assert_cmpuint(val, ==, 0);
    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTPCT_HI);
    g_assert_cmpuint(val, ==, 0);

    /*
     * Clearing RWPT denies the whole CNTP_* set: CVAL/TVAL/CTL reads all
     * read back 0 even though CVAL holds saved_cval.
     */
    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START,
                   acr_all & ~QCT_QTIMER_AC_CNTACR_RWPT);
    cval = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(cval, ==, 0);
    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_TVAL);
    g_assert_cmpuint(val, ==, 0);
    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(val, ==, 0);

    /* Denied writes must be dropped rather than applied. */
    qtimer_write64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO, 0x1234);
    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_TVAL, 0x5678);
    qtimer_write32(qtimer_view_base, QCT_QTIMER_CNTP_CTL, 1);

    /* Restoring RWPT reveals that CVAL still holds the pre-denial value. */
    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START, acr_all);
    cval = qtimer_read64(qtimer_view_base, QCT_QTIMER_CNTP_CVAL_LO);
    g_assert_cmpuint(cval, ==, saved_cval);
    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNTP_CTL);
    g_assert_cmpuint(val & 1, ==, 0);

    /* CNTFRQ and CNTPCT work again once permissions are back. */
    val = qtimer_read32(qtimer_view_base, QCT_QTIMER_CNT_FREQ);
    g_assert_cmpuint(val, ==, QTIMER_DEFAULT_FREQ_HZ);

    /*
     * An unimplemented offset in the view region is also an access error,
     * and reads back as 0.
     */
    val = qtimer_read32(qtimer_view_base, 0x100);
    g_assert_cmpuint(val, ==, 0);

    /* Leave the frame with full permissions for any later test. */
    qtimer_write32(qtimer_ac_base, QCT_QTIMER_AC_CNTACR_START, acr_all);
}

static void test_qtimer_frame_irq_routing(void)
{
    unsigned int frame, other;
    uint64_t frame_base;

    qtest_irq_intercept_in(global_qtest, "/machine/l2vic");

    for (frame = 0; frame < QTIMER_NR_FRAMES; frame++) {
        frame_base = qtimer_view_base + frame * QTIMER_FRAME_STRIDE;

        qtimer_write32(frame_base, QCT_QTIMER_CNTP_TVAL, TIMER_TEST_OFFSET);
        qtimer_write32(frame_base, QCT_QTIMER_CNTP_CTL,
                       QCT_QTIMER_CNTP_CTL_ENABLE);
        g_assert_false(qtest_get_irq(global_qtest,
                                     QTIMER_L2VIC_IRQ_BASE + frame));

        /* Step past the deadline so the frame raises its interrupt. */
        qtest_clock_step(global_qtest, TIMER_TEST_NS * 2);
        g_assert_true(qtest_get_irq(global_qtest,
                                    QTIMER_L2VIC_IRQ_BASE + frame));

        /* No other frame's line may be disturbed. */
        for (other = 0; other < QTIMER_NR_FRAMES; other++) {
            if (other != frame) {
                g_assert_false(qtest_get_irq(global_qtest,
                                             QTIMER_L2VIC_IRQ_BASE + other));
            }
        }

        /* Clearing EN drops the interrupt, leaving a clean slate. */
        qtimer_write32(frame_base, QCT_QTIMER_CNTP_CTL, 0);
        g_assert_false(qtest_get_irq(global_qtest,
                                     QTIMER_L2VIC_IRQ_BASE + frame));
    }
}

typedef struct {
    const char *machine;
    const struct hexagon_machine_config *cfg;
} QtimerMachineCfg;

static const QtimerMachineCfg qtimer_machines[] = {
    { "virt",      &v68n_1024 },
    { "V66G_1024", &v66g_1024 },
};

static void test_qtimer_on_machine(gconstpointer data)
{
    const QtimerMachineCfg *mc = data;
    g_autofree char *args = g_strdup_printf(
        "-machine %s -global qct-qtimer.freq-scale=1", mc->machine);

    qtimer_view_base = mc->cfg->qtmr_region;
    qtimer_ac_base = mc->cfg->csr_base;

    qtest_start(args);

    test_qtimer_basic_access();
    test_qtimer_multiple_frames();
    test_qtimer_register_reads();
    test_qtimer_control_registers();
    test_qtimer_cval_access();
    test_qtimer_counter_progression();
    test_qtimer_timer_behavior();
    test_qtimer_ac_region();
    test_qtimer_access_denied();
    test_qtimer_frame_irq_routing();

    qtest_end();
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(qtimer_machines); i++) {
        g_autofree char *path = g_strdup_printf("/qct-qtimer/%s/all-tests",
                                                qtimer_machines[i].machine);
        qtest_add_data_func(path, &qtimer_machines[i], test_qtimer_on_machine);
    }

    return g_test_run();
}
