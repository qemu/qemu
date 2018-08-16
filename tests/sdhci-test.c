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
#include "libqos/pci-pc.h"
#include "hw/pci/pci.h"

#define SDHC_CAPAB                      0x40
FIELD(SDHC_CAPAB, BASECLKFREQ,               8, 8); /* since v2 */
FIELD(SDHC_CAPAB, SDMA,                     22, 1);
FIELD(SDHC_CAPAB, SDR,                      32, 3); /* since v3 */
FIELD(SDHC_CAPAB, DRIVER,                   36, 3); /* since v3 */
#define SDHC_HCVER                      0xFE

static const struct sdhci_t {
    const char *arch, *machine;
    struct {
        uintptr_t addr;
        uint8_t version;
        uint8_t baseclock;
        struct {
            bool sdma;
            uint64_t reg;
        } capab;
    } sdhci;
    struct {
        uint16_t vendor_id, device_id;
    } pci;
} models[] = {
    /* PC via PCI */
    { "x86_64", "pc",
        {-1,         2, 0,  {1, 0x057834b4} },
        .pci = { PCI_VENDOR_ID_REDHAT, PCI_DEVICE_ID_REDHAT_SDHCI } },

    /* Exynos4210 */
    { "arm",    "smdkc210",
        {0x12510000, 2, 0,  {1, 0x5e80080} } },

    /* i.MX 6 */
    { "arm",    "sabrelite",
        {0x02190000, 3, 0,  {1, 0x057834b4} } },

    /* BCM2835 */
    { "arm",    "raspi2",
        {0x3f300000, 3, 52, {0, 0x052134b4} } },

    /* Zynq-7000 */
    { "arm",    "xilinx-zynq-a9",   /* Datasheet: UG585 (v1.12.1) */
        {0xe0100000, 2, 0,  {1, 0x69ec0080} } },

    /* ZynqMP */
    { "aarch64", "xlnx-zcu102",     /* Datasheet: UG1085 (v1.7) */
        {0xff160000, 3, 0,  {1, 0x280737ec6481} } },

};

typedef struct QSDHCI {
    struct {
        QPCIBus *bus;
        QPCIDevice *dev;
    } pci;
    union {
        QPCIBar mem_bar;
        uint64_t addr;
    };
} QSDHCI;

static uint16_t sdhci_readw(QSDHCI *s, uint32_t reg)
{
    uint16_t val;

    if (s->pci.dev) {
        val = qpci_io_readw(s->pci.dev, s->mem_bar, reg);
    } else {
        val = qtest_readw(global_qtest, s->addr + reg);
    }

    return val;
}

static uint64_t sdhci_readq(QSDHCI *s, uint32_t reg)
{
    uint64_t val;

    if (s->pci.dev) {
        val = qpci_io_readq(s->pci.dev, s->mem_bar, reg);
    } else {
        val = qtest_readq(global_qtest, s->addr + reg);
    }

    return val;
}

static void sdhci_writeq(QSDHCI *s, uint32_t reg, uint64_t val)
{
    if (s->pci.dev) {
        qpci_io_writeq(s->pci.dev, s->mem_bar, reg, val);
    } else {
        qtest_writeq(global_qtest, s->addr + reg, val);
    }
}

static void check_specs_version(QSDHCI *s, uint8_t version)
{
    uint32_t v;

    v = sdhci_readw(s, SDHC_HCVER);
    v &= 0xff;
    v += 1;
    g_assert_cmpuint(v, ==, version);
}

static void check_capab_capareg(QSDHCI *s, uint64_t expec_capab)
{
    uint64_t capab;

    capab = sdhci_readq(s, SDHC_CAPAB);
    g_assert_cmphex(capab, ==, expec_capab);
}

static void check_capab_readonly(QSDHCI *s)
{
    const uint64_t vrand = 0x123456789abcdef;
    uint64_t capab0, capab1;

    capab0 = sdhci_readq(s, SDHC_CAPAB);
    g_assert_cmpuint(capab0, !=, vrand);

    sdhci_writeq(s, SDHC_CAPAB, vrand);
    capab1 = sdhci_readq(s, SDHC_CAPAB);
    g_assert_cmpuint(capab1, !=, vrand);
    g_assert_cmpuint(capab1, ==, capab0);
}

static void check_capab_baseclock(QSDHCI *s, uint8_t expec_freq)
{
    uint64_t capab, capab_freq;

    if (!expec_freq) {
        return;
    }
    capab = sdhci_readq(s, SDHC_CAPAB);
    capab_freq = FIELD_EX64(capab, SDHC_CAPAB, BASECLKFREQ);
    g_assert_cmpuint(capab_freq, ==, expec_freq);
}

static void check_capab_sdma(QSDHCI *s, bool supported)
{
    uint64_t capab, capab_sdma;

    capab = sdhci_readq(s, SDHC_CAPAB);
    capab_sdma = FIELD_EX64(capab, SDHC_CAPAB, SDMA);
    g_assert_cmpuint(capab_sdma, ==, supported);
}

static void check_capab_v3(QSDHCI *s, uint8_t version)
{
    uint64_t capab, capab_v3;

    if (version < 3) {
        /* before v3 those fields are RESERVED */
        capab = sdhci_readq(s, SDHC_CAPAB);
        capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, SDR);
        g_assert_cmpuint(capab_v3, ==, 0);
        capab_v3 = FIELD_EX64(capab, SDHC_CAPAB, DRIVER);
        g_assert_cmpuint(capab_v3, ==, 0);
    }
}

static QSDHCI *machine_start(const struct sdhci_t *test)
{
    QSDHCI *s = g_new0(QSDHCI, 1);

    if (test->pci.vendor_id) {
        /* PCI */
        uint16_t vendor_id, device_id;
        uint64_t barsize;

        global_qtest = qtest_initf("-machine %s -device sdhci-pci",
                                   test->machine);

        s->pci.bus = qpci_init_pc(global_qtest, NULL);

        /* Find PCI device and verify it's the right one */
        s->pci.dev = qpci_device_find(s->pci.bus, QPCI_DEVFN(4, 0));
        g_assert_nonnull(s->pci.dev);
        vendor_id = qpci_config_readw(s->pci.dev, PCI_VENDOR_ID);
        device_id = qpci_config_readw(s->pci.dev, PCI_DEVICE_ID);
        g_assert(vendor_id == test->pci.vendor_id);
        g_assert(device_id == test->pci.device_id);
        s->mem_bar = qpci_iomap(s->pci.dev, 0, &barsize);
        qpci_device_enable(s->pci.dev);
    } else {
        /* SysBus */
        global_qtest = qtest_initf("-machine %s", test->machine);
        s->addr = test->sdhci.addr;
    }

    return s;
}

static void machine_stop(QSDHCI *s)
{
    qpci_free_pc(s->pci.bus);
    g_free(s->pci.dev);
    qtest_quit(global_qtest);
    g_free(s);
}

static void test_machine(const void *data)
{
    const struct sdhci_t *test = data;
    QSDHCI *s;

    s = machine_start(test);

    check_specs_version(s, test->sdhci.version);
    check_capab_capareg(s, test->sdhci.capab.reg);
    check_capab_readonly(s);
    check_capab_v3(s, test->sdhci.version);
    check_capab_sdma(s, test->sdhci.capab.sdma);
    check_capab_baseclock(s, test->sdhci.baseclock);

    machine_stop(s);
}

int main(int argc, char *argv[])
{
    const char *arch = qtest_get_arch();
    char *name;
    int i;

    g_test_init(&argc, &argv, NULL);
    for (i = 0; i < ARRAY_SIZE(models); i++) {
        if (strcmp(arch, models[i].arch)) {
            continue;
        }
        name = g_strdup_printf("sdhci/%s", models[i].machine);
        qtest_add_data_func(name, &models[i], test_machine);
        g_free(name);
    }

    return g_test_run();
}
