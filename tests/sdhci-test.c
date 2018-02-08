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

static QSDHCI *machine_start(const struct sdhci_t *test)
{
    QSDHCI *s = g_new0(QSDHCI, 1);

    if (test->pci.vendor_id) {
        /* PCI */
        uint16_t vendor_id, device_id;
        uint64_t barsize;

        global_qtest = qtest_startf("-machine %s -device sdhci-pci",
                                    test->machine);

        s->pci.bus = qpci_init_pc(NULL);

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
        global_qtest = qtest_startf("-machine %s", test->machine);
        s->addr = test->sdhci.addr;
    }

    return s;
}

static void machine_stop(QSDHCI *s)
{
    g_free(s->pci.dev);
    qtest_quit(global_qtest);
}

static void test_machine(const void *data)
{
    const struct sdhci_t *test = data;
    QSDHCI *s;

    s = machine_start(test);

    check_capab_capareg(s, test->sdhci.capab.reg);
    check_capab_readonly(s);
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
