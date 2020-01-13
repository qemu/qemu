/*
 * QTest testcase for Q35 northbridge
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * Author: Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci.h"
#include "libqos/pci-pc.h"
#include "hw/pci-host/q35.h"
#include "qapi/qmp/qdict.h"

#define TSEG_SIZE_TEST_GUEST_RAM_MBYTES 128

/* @esmramc_tseg_sz: ESMRAMC.TSEG_SZ bitmask for selecting the requested TSEG
 *                   size. Must be a subset of
 *                   MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK.
 *
 * @extended_tseg_mbytes: Size of the extended TSEG. Only consulted if
 *                        @esmramc_tseg_sz equals
 *                        MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK precisely.
 *
 * @expected_tseg_mbytes: Expected guest-visible TSEG size in megabytes,
 *                        matching @esmramc_tseg_sz and @extended_tseg_mbytes
 *                        above.
 */
struct TsegSizeArgs {
    uint8_t esmramc_tseg_sz;
    uint16_t extended_tseg_mbytes;
    uint16_t expected_tseg_mbytes;
};
typedef struct TsegSizeArgs TsegSizeArgs;

static const TsegSizeArgs tseg_1mb = {
    .esmramc_tseg_sz      = MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_1MB,
    .extended_tseg_mbytes = 0,
    .expected_tseg_mbytes = 1,
};
static const TsegSizeArgs tseg_2mb = {
    .esmramc_tseg_sz      = MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_2MB,
    .extended_tseg_mbytes = 0,
    .expected_tseg_mbytes = 2,
};
static const TsegSizeArgs tseg_8mb = {
    .esmramc_tseg_sz      = MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_8MB,
    .extended_tseg_mbytes = 0,
    .expected_tseg_mbytes = 8,
};
static const TsegSizeArgs tseg_ext_16mb = {
    .esmramc_tseg_sz      = MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK,
    .extended_tseg_mbytes = 16,
    .expected_tseg_mbytes = 16,
};

static void smram_set_bit(QPCIDevice *pcidev, uint8_t mask, bool enabled)
{
    uint8_t smram;

    smram = qpci_config_readb(pcidev, MCH_HOST_BRIDGE_SMRAM);
    if (enabled) {
        smram |= mask;
    } else {
        smram &= ~mask;
    }
    qpci_config_writeb(pcidev, MCH_HOST_BRIDGE_SMRAM, smram);
}

static bool smram_test_bit(QPCIDevice *pcidev, uint8_t mask)
{
    uint8_t smram;

    smram = qpci_config_readb(pcidev, MCH_HOST_BRIDGE_SMRAM);
    return smram & mask;
}

static void test_smram_lock(void)
{
    QPCIBus *pcibus;
    QPCIDevice *pcidev;
    QDict *response;
    QTestState *qts;

    qts = qtest_init("-M q35");

    pcibus = qpci_new_pc(qts, NULL);
    g_assert(pcibus != NULL);

    pcidev = qpci_device_find(pcibus, 0);
    g_assert(pcidev != NULL);

    /* check open is settable */
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, false);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == true);

    /* lock, check open is cleared & not settable */
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_LCK, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);

    /* reset */
    response = qtest_qmp(qts, "{'execute': 'system_reset', 'arguments': {} }");
    g_assert(response);
    g_assert(!qdict_haskey(response, "error"));
    qobject_unref(response);

    /* check open is settable again */
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, false);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == false);
    smram_set_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN, true);
    g_assert(smram_test_bit(pcidev, MCH_HOST_BRIDGE_SMRAM_D_OPEN) == true);

    g_free(pcidev);
    qpci_free_pc(pcibus);

    qtest_quit(qts);
}

static void test_tseg_size(const void *data)
{
    const TsegSizeArgs *args = data;
    QPCIBus *pcibus;
    QPCIDevice *pcidev;
    uint8_t smram_val;
    uint8_t esmramc_val;
    uint32_t ram_offs;
    QTestState *qts;

    if (args->esmramc_tseg_sz == MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK) {
        qts = qtest_initf("-M q35 -m %uM -global mch.extended-tseg-mbytes=%u",
                          TSEG_SIZE_TEST_GUEST_RAM_MBYTES,
                          args->extended_tseg_mbytes);
    } else {
        qts = qtest_initf("-M q35 -m %uM", TSEG_SIZE_TEST_GUEST_RAM_MBYTES);
    }

    /* locate the DRAM controller */
    pcibus = qpci_new_pc(qts, NULL);
    g_assert(pcibus != NULL);
    pcidev = qpci_device_find(pcibus, 0);
    g_assert(pcidev != NULL);

    /* Set TSEG size. Restrict TSEG visibility to SMM by setting T_EN. */
    esmramc_val = qpci_config_readb(pcidev, MCH_HOST_BRIDGE_ESMRAMC);
    esmramc_val &= ~MCH_HOST_BRIDGE_ESMRAMC_TSEG_SZ_MASK;
    esmramc_val |= args->esmramc_tseg_sz;
    esmramc_val |= MCH_HOST_BRIDGE_ESMRAMC_T_EN;
    qpci_config_writeb(pcidev, MCH_HOST_BRIDGE_ESMRAMC, esmramc_val);

    /* Enable TSEG by setting G_SMRAME. Close TSEG by setting D_CLS. */
    smram_val = qpci_config_readb(pcidev, MCH_HOST_BRIDGE_SMRAM);
    smram_val &= ~(MCH_HOST_BRIDGE_SMRAM_D_OPEN |
                   MCH_HOST_BRIDGE_SMRAM_D_LCK);
    smram_val |= (MCH_HOST_BRIDGE_SMRAM_D_CLS |
                  MCH_HOST_BRIDGE_SMRAM_G_SMRAME);
    qpci_config_writeb(pcidev, MCH_HOST_BRIDGE_SMRAM, smram_val);

    /* lock TSEG */
    smram_val |= MCH_HOST_BRIDGE_SMRAM_D_LCK;
    qpci_config_writeb(pcidev, MCH_HOST_BRIDGE_SMRAM, smram_val);

    /* Now check that the byte right before the TSEG is r/w, and that the first
     * byte in the TSEG always reads as 0xff.
     */
    ram_offs = (TSEG_SIZE_TEST_GUEST_RAM_MBYTES - args->expected_tseg_mbytes) *
               1024 * 1024 - 1;
    g_assert_cmpint(qtest_readb(qts, ram_offs), ==, 0);
    qtest_writeb(qts, ram_offs, 1);
    g_assert_cmpint(qtest_readb(qts, ram_offs), ==, 1);

    ram_offs++;
    g_assert_cmpint(qtest_readb(qts, ram_offs), ==, 0xff);
    qtest_writeb(qts, ram_offs, 1);
    g_assert_cmpint(qtest_readb(qts, ram_offs), ==, 0xff);

    g_free(pcidev);
    qpci_free_pc(pcibus);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/q35/smram/lock", test_smram_lock);

    qtest_add_data_func("/q35/tseg-size/1mb", &tseg_1mb, test_tseg_size);
    qtest_add_data_func("/q35/tseg-size/2mb", &tseg_2mb, test_tseg_size);
    qtest_add_data_func("/q35/tseg-size/8mb", &tseg_8mb, test_tseg_size);
    qtest_add_data_func("/q35/tseg-size/ext/16mb", &tseg_ext_16mb,
                        test_tseg_size);
    return g_test_run();
}
