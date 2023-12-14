/*
 * QTest testcase for the watchdog timer of HiFive 1 rev b.
 *
 * Copyright (c) 2023 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "libqtest.h"
#include "hw/registerfields.h"
#include "hw/misc/sifive_e_aon.h"

FIELD(AON_WDT_WDOGCFG, SCALE, 0, 4)
FIELD(AON_WDT_WDOGCFG, RSVD0, 4, 4)
FIELD(AON_WDT_WDOGCFG, RSTEN, 8, 1)
FIELD(AON_WDT_WDOGCFG, ZEROCMP, 9, 1)
FIELD(AON_WDT_WDOGCFG, RSVD1, 10, 2)
FIELD(AON_WDT_WDOGCFG, EN_ALWAYS, 12, 1)
FIELD(AON_WDT_WDOGCFG, EN_CORE_AWAKE, 13, 1)
FIELD(AON_WDT_WDOGCFG, RSVD2, 14, 14)
FIELD(AON_WDT_WDOGCFG, IP0, 28, 1)
FIELD(AON_WDT_WDOGCFG, RSVD3, 29, 3)

#define WDOG_BASE (0x10000000)
#define WDOGCFG (0x0)
#define WDOGCOUNT (0x8)
#define WDOGS (0x10)
#define WDOGFEED (0x18)
#define WDOGKEY (0x1c)
#define WDOGCMP0 (0x20)

#define SIFIVE_E_AON_WDOGKEY (0x51F15E)
#define SIFIVE_E_AON_WDOGFEED (0xD09F00D)
#define SIFIVE_E_LFCLK_DEFAULT_FREQ (32768)

static void test_init(QTestState *qts)
{
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, 0);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, 0);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCMP0, 0xBEEF);
}

static void test_wdogcount(void)
{
    uint64_t tmp;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    tmp = qtest_readl(qts, WDOG_BASE + WDOGCOUNT);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, 0xBEEF);
    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCOUNT) == tmp);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, 0xBEEF);
    g_assert(0xBEEF == qtest_readl(qts, WDOG_BASE + WDOGCOUNT));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, 0xAAAAAAAA);
    g_assert(0x2AAAAAAA == qtest_readl(qts, WDOG_BASE + WDOGCOUNT));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGFEED, 0xAAAAAAAA);
    g_assert(0x2AAAAAAA == qtest_readl(qts, WDOG_BASE + WDOGCOUNT));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGFEED, SIFIVE_E_AON_WDOGFEED);
    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGCOUNT));

    qtest_quit(qts);
}

static void test_wdogcfg(void)
{
    uint32_t tmp_cfg;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    tmp_cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, 0xFFFFFFFF);
    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCFG) == tmp_cfg);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, 0xFFFFFFFF);
    g_assert(0xFFFFFFFF == qtest_readl(qts, WDOG_BASE + WDOGCFG));

    tmp_cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(15 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(1 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(1 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(1 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(1 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(1 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, IP0));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, 0);
    tmp_cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(0 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(0 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(0 == FIELD_EX32(tmp_cfg, AON_WDT_WDOGCFG, IP0));
    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGCFG));

    qtest_quit(qts);
}

static void test_wdogcmp0(void)
{
    uint32_t tmp;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    tmp = qtest_readl(qts, WDOG_BASE + WDOGCMP0);
    qtest_writel(qts, WDOG_BASE + WDOGCMP0, 0xBEEF);
    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCMP0) == tmp);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCMP0, 0xBEEF);
    g_assert(0xBEEF == qtest_readl(qts, WDOG_BASE + WDOGCMP0));

    qtest_quit(qts);
}

static void test_wdogkey(void)
{
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGKEY));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, 0xFFFF);
    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGKEY));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    g_assert(1 == qtest_readl(qts, WDOG_BASE + WDOGKEY));

    qtest_writel(qts, WDOG_BASE + WDOGFEED, 0xAAAAAAAA);
    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGKEY));

    qtest_quit(qts);
}

static void test_wdogfeed(void)
{
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGFEED));

    qtest_writel(qts, WDOG_BASE + WDOGFEED, 0xFFFF);
    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGFEED));

    qtest_quit(qts);
}

static void test_scaled_wdogs(void)
{
    uint32_t cfg;
    uint32_t fake_count = 0x12345678;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, fake_count);
    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCOUNT) == fake_count);
    g_assert((uint16_t)qtest_readl(qts, WDOG_BASE + WDOGS) ==
             (uint16_t)fake_count);

    for (int i = 0; i < 16; i++) {
        cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
        cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, SCALE, i);
        qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
        qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);
        g_assert((uint16_t)qtest_readl(qts, WDOG_BASE + WDOGS) ==
                 (uint16_t)(fake_count >>
                            FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE)));
    }

    qtest_quit(qts);
}

static void test_watchdog(void)
{
    uint32_t cfg;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCMP0, SIFIVE_E_LFCLK_DEFAULT_FREQ);

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, SCALE, 0);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS, 1);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);

    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);

    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ);
    g_assert(qtest_readl(qts, WDOG_BASE + WDOGS) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ);

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, 0);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, IP0, 0);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);
    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_quit(qts);
}

static void test_scaled_watchdog(void)
{
    uint32_t cfg;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCMP0, 10);

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, SCALE, 15);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS, 1);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);

    qtest_clock_step(qts, NANOSECONDS_PER_SECOND * 10);

    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 10);

    g_assert(10 == qtest_readl(qts, WDOG_BASE + WDOGS));

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(15 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, 0);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, IP0, 0);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);
    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_quit(qts);
}

static void test_periodic_int(void)
{
    uint32_t cfg;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCMP0, SIFIVE_E_LFCLK_DEFAULT_FREQ);

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, SCALE, 0);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, ZEROCMP, 1);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS, 1);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);

    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);

    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGCOUNT));
    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGS));

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, IP0, 0);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);
    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_clock_step(qts, NANOSECONDS_PER_SECOND);

    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGCOUNT));
    g_assert(0 == qtest_readl(qts, WDOG_BASE + WDOGS));

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, IP0, 0);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);
    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_quit(qts);
}

static void test_enable_disable(void)
{
    uint32_t cfg;
    QTestState *qts = qtest_init("-machine sifive_e");

    test_init(qts);

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCMP0, 10);

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, SCALE, 15);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS, 1);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);

    qtest_clock_step(qts, NANOSECONDS_PER_SECOND * 2);

    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 2);
    g_assert(2 == qtest_readl(qts, WDOG_BASE + WDOGS));

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(15 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS, 0);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);

    qtest_clock_step(qts, NANOSECONDS_PER_SECOND * 8);

    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 2);
    g_assert(2 == qtest_readl(qts, WDOG_BASE + WDOGS));

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(15 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS, 1);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);

    qtest_clock_step(qts, NANOSECONDS_PER_SECOND * 8);

    g_assert(qtest_readl(qts, WDOG_BASE + WDOGCOUNT) ==
             SIFIVE_E_LFCLK_DEFAULT_FREQ * 10);
    g_assert(10 == qtest_readl(qts, WDOG_BASE + WDOGS));

    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(15 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, SCALE));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, RSTEN));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, ZEROCMP));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_ALWAYS));
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, EN_CORE_AWAKE));
    g_assert(1 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCOUNT, 0);
    cfg = FIELD_DP32(cfg, AON_WDT_WDOGCFG, IP0, 0);
    qtest_writel(qts, WDOG_BASE + WDOGKEY, SIFIVE_E_AON_WDOGKEY);
    qtest_writel(qts, WDOG_BASE + WDOGCFG, cfg);
    cfg = qtest_readl(qts, WDOG_BASE + WDOGCFG);
    g_assert(0 == FIELD_EX32(cfg, AON_WDT_WDOGCFG, IP0));

    qtest_quit(qts);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogcount",
                   test_wdogcount);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogcfg",
                   test_wdogcfg);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogcmp0",
                   test_wdogcmp0);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogkey",
                   test_wdogkey);
    qtest_add_func("/sifive-e-aon-watchdog-test/wdogfeed",
                   test_wdogfeed);
    qtest_add_func("/sifive-e-aon-watchdog-test/scaled_wdogs",
                   test_scaled_wdogs);
    qtest_add_func("/sifive-e-aon-watchdog-test/watchdog",
                   test_watchdog);
    qtest_add_func("/sifive-e-aon-watchdog-test/scaled_watchdog",
                   test_scaled_watchdog);
    qtest_add_func("/sifive-e-aon-watchdog-test/periodic_int",
                   test_periodic_int);
    qtest_add_func("/sifive-e-aon-watchdog-test/enable_disable",
                   test_enable_disable);
    return g_test_run();
}
