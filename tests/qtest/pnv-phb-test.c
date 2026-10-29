/*
 * QTest testcase for PowerNV PHB
 *
 * Copyright (c) 2026, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "hw/pci-host/pnv_phb4_regs.h"
#include "pnv-xscom.h"
#include "pnv-qtest-common.h"

#define PHB3_PBCQ_SPCI_ASB_ADDR      0x0
#define PHB3_PBCQ_SPCI_ASB_DATA      0x2

#define PHB5_XSCOM              0x40084800ull

/* Index of PNV_CHIP_POWER10 in pnv_chips[] within "pnv-xscom.h" */
#define PNV_P10_CHIP_INDEX      2

/* SCOM to PCBA address conversion */
#define SCOM_TO_PCBA(scom, addr) (((scom) >> 3) + (addr))

/*
 * Indirect XSCOM write:
 * - Write 'Indirect Address Register' with register-offset to write.
 * - Write 'Indirect Data Register' with the value.
 */
static void pnv_phb_xscom_write(QTestState *qts, const PnvChip *chip,
        uint64_t scom, uint32_t indirect_addr, uint32_t indirect_data,
        uint64_t reg, uint64_t val)
{
    qtest_writeq(qts, pnv_xscom_addr(chip, (scom >> 3) + indirect_addr), reg);
    qtest_writeq(qts, pnv_xscom_addr(chip, (scom >> 3) + indirect_data), val);
}

/*
 * Indirect XSCOM read::
 * - Write 'Indirect Address Register' with register-offset to read.
 * - Read 'Indirect Data Register' to get the value.
 */
static uint64_t pnv_phb_xscom_read(QTestState *qts, const PnvChip *chip,
        uint64_t scom, uint32_t indirect_addr, uint32_t indirect_data,
        uint64_t reg)
{
    qtest_writeq(qts, pnv_xscom_addr(chip, SCOM_TO_PCBA(scom, indirect_addr)),
                 reg);
    return qtest_readq(qts, pnv_xscom_addr(chip, SCOM_TO_PCBA(scom,
                                                              indirect_data)));
}

#define PHB5_XSCOM_WRITE(a, v) pnv_phb_xscom_write(qts, \
                                   &pnv_chips[PNV_P10_CHIP_INDEX], PHB5_XSCOM, \
                                   PHB_SCOM_HV_IND_ADDR, PHB_SCOM_HV_IND_DATA, \
                                   PPC_BIT(0) | (a), (v))

#define PHB5_XSCOM_READ(a) pnv_phb_xscom_read(qts, \
                                   &pnv_chips[PNV_P10_CHIP_INDEX], PHB5_XSCOM, \
                                   PHB_SCOM_HV_IND_ADDR, PHB_SCOM_HV_IND_DATA, \
                                   PPC_BIT(0) | (a))

/* Assert that 'PHB PBL Control' register has correct reset value */
static void phb5_reset_test(QTestState *qts)
{
    g_assert_cmpuint(PHB5_XSCOM_READ(PHB_PBL_CONTROL), ==, 0xC009000000000000);
}

/* Check sticky-reset */
static void phb5_sticky_rst_test(QTestState *qts)
{
    uint64_t val;

    /*
     * Sticky reset test of PHB_PBL_ERR_STATUS.
     *
     * Write all 1's to reg PHB_PBL_ERR_INJECT.
     * RO-only bits will not be written and
     * updated value will be copied to reg PHB_PBL_ERR_STATUS.
     *
     * Reset PBL core by setting PHB_PCIE_CRESET_PBL in reg PHB_PCIE_CRESET.
     * Verify the sticky bits are still set.
     */
    PHB5_XSCOM_WRITE(PHB_PBL_ERR_INJECT, PPC_BITMASK(0, 63));
    PHB5_XSCOM_WRITE(PHB_PCIE_CRESET, PHB_PCIE_CRESET_PBL); /*Reset*/
    val = PHB5_XSCOM_READ(PHB_PBL_ERR_STATUS);
    g_assert_cmpuint(val, ==, (PPC_BITMASK(24, 27) | PPC_BITMASK(36, 37) |
                     PPC_BITMASK(39, 45) | PPC_BITMASK(47, 48) |
                     PPC_BITMASK(52, 54)));

}

/* Check that write-only bits/regs return 0 when read */
static void phb4_writeonly_read_test(QTestState *qts)
{
    uint64_t val;

    /*
     * Set all bits of PHB_DMA_SYNC,
     * bits 0 and 2 are write-only and should be read as 0.
     */
    PHB5_XSCOM_WRITE(PHB_DMA_SYNC, PPC_BITMASK(0, 63));
    val = PHB5_XSCOM_READ(PHB_DMA_SYNC);
    g_assert_cmpuint(val & PPC_BIT(0), ==, 0x0);
    g_assert_cmpuint(val & PPC_BIT(2), ==, 0x0);

    /*
     * Set all bits of PHB_PCIE_HOTPLUG_STATUS,
     * bit 9 is write-only and should be read as 0.
     */
    PHB5_XSCOM_WRITE(PHB_PCIE_HOTPLUG_STATUS, PPC_BITMASK(0, 63));
    val = PHB5_XSCOM_READ(PHB_PCIE_HOTPLUG_STATUS);
    g_assert_cmpuint(val & PPC_BIT(9), ==, 0x0);

    /*
     * Set all bits of PHB_PCIE_LMR,
     * bits 0 and 1 are write-only and should be read as 0.
     */
    PHB5_XSCOM_WRITE(PHB_PCIE_LMR, PPC_BITMASK(0, 63));
    val = PHB5_XSCOM_READ(PHB_PCIE_LMR);
    g_assert_cmpuint(val & PPC_BIT(0), ==, 0x0);
    g_assert_cmpuint(val & PPC_BIT(1), ==, 0x0);

    /*
     * Set all bits of PHB_PCIE_DLP_TRWCTL,
     * write-only bit-1 should be read as 0.
     */
    PHB5_XSCOM_WRITE(PHB_PCIE_DLP_TRWCTL, PPC_BITMASK(0, 63));
    val = PHB5_XSCOM_READ(PHB_PCIE_DLP_TRWCTL);
    g_assert_cmpuint(val & PPC_BIT(1), ==, 0x0);

    /*
     * Set all bits of PHB_LEM_ERROR_AND_MASK, PHB_LEM_ERROR_OR_MASK,
     * both regs are write-only and should be read as 0.
     */
    PHB5_XSCOM_WRITE(PHB_LEM_ERROR_AND_MASK, PPC_BITMASK(0, 63));
    val = PHB5_XSCOM_READ(PHB_LEM_ERROR_AND_MASK);
    g_assert_cmpuint(val, ==, 0x0);

    PHB5_XSCOM_WRITE(PHB_LEM_ERROR_OR_MASK, PPC_BITMASK(0, 63));
    val = PHB5_XSCOM_READ(PHB_LEM_ERROR_OR_MASK);
    g_assert_cmpuint(val, ==, 0x0);
}

static void phb5_tests(void)
{
    QTestState *qts = NULL;

    qts = qtest_initf("-machine powernv10 -accel tcg");

    /* Check reset value of a register */
    phb5_reset_test(qts);

    /* Check sticky reset of a register */
    phb5_sticky_rst_test(qts);

    /* Check write-only logic */
    phb4_writeonly_read_test(qts);

    qtest_quit(qts);
}

/* Assert that 'PHB - Version Register' bits[24:31] are as expected */
static void phb_version_test(const void *data)
{
    const PnvChip *chip = (PnvChip *)data;
    QTestState *qts;
    const char *machine = pnv_get_machine_type(chip->chip_type);

    /* Defaults to PNV_CHIP_POWER8 */
    uint64_t phb_xscom = 0x4809e000;
    uint64_t reg_phb_version = PHB_VERSION;
    uint32_t indirect_addr = PHB3_PBCQ_SPCI_ASB_ADDR;
    uint32_t indirect_data = PHB3_PBCQ_SPCI_ASB_DATA;
    uint32_t expected_ver = 0xA3;
    uint64_t ver;

    if (chip->chip_type == PNV_CHIP_POWER9) {
        phb_xscom = 0x68084800;
        indirect_addr = PHB_SCOM_HV_IND_ADDR;
        indirect_data = PHB_SCOM_HV_IND_DATA;
        reg_phb_version |= PPC_BIT(0);
        expected_ver = 0xA4;
    } else if ((chip->chip_type == PNV_CHIP_POWER10) ||
               (chip->chip_type == PNV_CHIP_POWER11)) {
        phb_xscom = PHB5_XSCOM;
        indirect_addr = PHB_SCOM_HV_IND_ADDR;
        indirect_data = PHB_SCOM_HV_IND_DATA;
        reg_phb_version |= PPC_BIT(0);
        expected_ver = 0xA5;
    }

    qts = qtest_initf("-M %s -accel tcg -cpu %s", machine, chip->cpu_model);

    ver = pnv_phb_xscom_read(qts, chip, phb_xscom,
                                indirect_addr, indirect_data, reg_phb_version);

    /* PHB Version register bits [24:31] */
    ver = GETFIELD(PPC_BITMASK(24, 31), ver);
    g_assert_cmpuint(ver, ==, expected_ver);

    qtest_quit(qts);
}

/* Verify versions of all supported PHB's */
static void add_phbX_version_test(void)
{
    for (int i = 0; i < ARRAY_SIZE(pnv_chips); i++) {
        g_autofree char *tname = g_strdup_printf("pnv-phb/%s",
                                      pnv_chips[i].cpu_model);
        qtest_add_data_func(tname, &pnv_chips[i], phb_version_test);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    /* PHB[345] tests */
    add_phbX_version_test();

    /* PHB5 specific tests */
    qtest_add_func("phb5", phb5_tests);

    return g_test_run();
}
