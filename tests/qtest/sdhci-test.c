/*
 * QTest testcase for SDHCI controllers
 *
 * Written by Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/pci-pc.h"
#include "hw/pci/pci.h"
#include "libqos/qgraph.h"
#include "libqos/sdhci.h"

#define SDHC_CAPAB                      0x40
FIELD(SDHC_CAPAB, BASECLKFREQ,               8, 8); /* since v2 */
FIELD(SDHC_CAPAB, SDMA,                     22, 1);
FIELD(SDHC_CAPAB, SDR,                      32, 3); /* since v3 */
FIELD(SDHC_CAPAB, DRIVER,                   36, 3); /* since v3 */
#define SDHC_HCVER                      0xFE

static void check_specs_version(QSDHCI *s, uint8_t version)
{
    uint32_t v;

    v = s->readw(s, SDHC_HCVER);
    v &= 0xff;
    v += 1;
    g_assert_cmpuint(v, ==, version);
}

static void check_capab_capareg(QSDHCI *s, uint64_t expec_capab)
{
    uint64_t capab;

    capab = s->readq(s, SDHC_CAPAB);
    g_assert_cmphex(capab, ==, expec_capab);
}

static void check_capab_readonly(QSDHCI *s)
{
    const uint64_t vrand = 0x123456789abcdef;
    uint64_t capab0, capab1;

    capab0 = s->readq(s, SDHC_CAPAB);
    g_assert_cmpuint(capab0, !=, vrand);

    s->writeq(s, SDHC_CAPAB, vrand);
    capab1 = s->readq(s, SDHC_CAPAB);
    g_assert_cmpuint(capab1, !=, vrand);
    g_assert_cmpuint(capab1, ==, capab0);
}

static void check_capab_baseclock(QSDHCI *s, uint8_t expec_freq)
{
    uint64_t capab, capab_freq;

    if (!expec_freq) {
        return;
    }
    capab = s->readq(s, SDHC_CAPAB);
    capab_freq = FIELD_EX64(capab, SDHC_CAPAB, BASECLKFREQ);
    g_assert_cmpuint(capab_freq, ==, expec_freq);
}

static void check_capab_sdma(QSDHCI *s, bool supported)
{
    uint64_t capab, capab_sdma;

    capab = s->readq(s, SDHC_CAPAB);
    capab_sdma = FIELD_EX64(capab, SDHC_CAPAB, SDMA);
    g_assert_cmpuint(capab_sdma, ==, supported);
}

static void check_capab_v3(QSDHCI *s, uint8_t version)
{
    uint64_t capab, capab_v3;

    if (version < 3) {
        /* before v3 those fields are RESERVED */
        capab = s->readq(s, SDHC_CAPAB);
        capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, SDR);
        g_assert_cmpuint(capab_v3, ==, 0);
        capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, DRIVER);
        g_assert_cmpuint(capab_v3, ==, 0);
    }
}

static void test_registers(void *obj, void *data, QGuestAllocator *alloc)
{
    QSDHCI *s = obj;

    check_specs_version(s, s->props.version);
    check_capab_capareg(s, s->props.capab.reg);
    check_capab_readonly(s);
    check_capab_v3(s, s->props.version);
    check_capab_sdma(s, s->props.capab.sdma);
    check_capab_baseclock(s, s->props.baseclock);
}

static void register_sdhci_test(void)
{
    qos_add_test("registers", "sdhci", test_registers, NULL);
}

libqos_init(register_sdhci_test);
