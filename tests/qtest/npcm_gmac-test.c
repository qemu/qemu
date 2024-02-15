/*
 * QTests for Nuvoton NPCM7xx/8xx GMAC Modules.
 *
 * Copyright 2024 Google LLC
 * Authors:
 * Hao Wu <wuhaotsh@google.com>
 * Nabih Estefan <nabihestefan@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "libqos/libqos.h"

/* Name of the GMAC Device */
#define TYPE_NPCM_GMAC "npcm-gmac"

/* Address of the PCS Module */
#define PCS_BASE_ADDRESS 0xf0780000
#define NPCM_PCS_IND_AC_BA 0x1fe

typedef struct GMACModule {
    int irq;
    uint64_t base_addr;
} GMACModule;

typedef struct TestData {
    const GMACModule *module;
} TestData;

/* Values extracted from hw/arm/npcm7xx.c */
static const GMACModule gmac_module_list[] = {
    {
        .irq        = 14,
        .base_addr  = 0xf0802000
    },
    {
        .irq        = 15,
        .base_addr  = 0xf0804000
    },
};

/* Returns the index of the GMAC module. */
static int gmac_module_index(const GMACModule *mod)
{
    ptrdiff_t diff = mod - gmac_module_list;

    g_assert_true(diff >= 0 && diff < ARRAY_SIZE(gmac_module_list));

    return diff;
}

/* 32-bit register indices. Taken from npcm_gmac.c */
typedef enum NPCMRegister {
    /* DMA Registers */
    NPCM_DMA_BUS_MODE = 0x1000,
    NPCM_DMA_XMT_POLL_DEMAND = 0x1004,
    NPCM_DMA_RCV_POLL_DEMAND = 0x1008,
    NPCM_DMA_RCV_BASE_ADDR = 0x100c,
    NPCM_DMA_TX_BASE_ADDR = 0x1010,
    NPCM_DMA_STATUS = 0x1014,
    NPCM_DMA_CONTROL = 0x1018,
    NPCM_DMA_INTR_ENA = 0x101c,
    NPCM_DMA_MISSED_FRAME_CTR = 0x1020,
    NPCM_DMA_HOST_TX_DESC = 0x1048,
    NPCM_DMA_HOST_RX_DESC = 0x104c,
    NPCM_DMA_CUR_TX_BUF_ADDR = 0x1050,
    NPCM_DMA_CUR_RX_BUF_ADDR = 0x1054,
    NPCM_DMA_HW_FEATURE = 0x1058,

    /* GMAC Registers */
    NPCM_GMAC_MAC_CONFIG = 0x0,
    NPCM_GMAC_FRAME_FILTER = 0x4,
    NPCM_GMAC_HASH_HIGH = 0x8,
    NPCM_GMAC_HASH_LOW = 0xc,
    NPCM_GMAC_MII_ADDR = 0x10,
    NPCM_GMAC_MII_DATA = 0x14,
    NPCM_GMAC_FLOW_CTRL = 0x18,
    NPCM_GMAC_VLAN_FLAG = 0x1c,
    NPCM_GMAC_VERSION = 0x20,
    NPCM_GMAC_WAKEUP_FILTER = 0x28,
    NPCM_GMAC_PMT = 0x2c,
    NPCM_GMAC_LPI_CTRL = 0x30,
    NPCM_GMAC_TIMER_CTRL = 0x34,
    NPCM_GMAC_INT_STATUS = 0x38,
    NPCM_GMAC_INT_MASK = 0x3c,
    NPCM_GMAC_MAC0_ADDR_HI = 0x40,
    NPCM_GMAC_MAC0_ADDR_LO = 0x44,
    NPCM_GMAC_MAC1_ADDR_HI = 0x48,
    NPCM_GMAC_MAC1_ADDR_LO = 0x4c,
    NPCM_GMAC_MAC2_ADDR_HI = 0x50,
    NPCM_GMAC_MAC2_ADDR_LO = 0x54,
    NPCM_GMAC_MAC3_ADDR_HI = 0x58,
    NPCM_GMAC_MAC3_ADDR_LO = 0x5c,
    NPCM_GMAC_RGMII_STATUS = 0xd8,
    NPCM_GMAC_WATCHDOG = 0xdc,
    NPCM_GMAC_PTP_TCR = 0x700,
    NPCM_GMAC_PTP_SSIR = 0x704,
    NPCM_GMAC_PTP_STSR = 0x708,
    NPCM_GMAC_PTP_STNSR = 0x70c,
    NPCM_GMAC_PTP_STSUR = 0x710,
    NPCM_GMAC_PTP_STNSUR = 0x714,
    NPCM_GMAC_PTP_TAR = 0x718,
    NPCM_GMAC_PTP_TTSR = 0x71c,

    /* PCS Registers */
    NPCM_PCS_SR_CTL_ID1 = 0x3c0008,
    NPCM_PCS_SR_CTL_ID2 = 0x3c000a,
    NPCM_PCS_SR_CTL_STS = 0x3c0010,

    NPCM_PCS_SR_MII_CTRL = 0x3e0000,
    NPCM_PCS_SR_MII_STS = 0x3e0002,
    NPCM_PCS_SR_MII_DEV_ID1 = 0x3e0004,
    NPCM_PCS_SR_MII_DEV_ID2 = 0x3e0006,
    NPCM_PCS_SR_MII_AN_ADV = 0x3e0008,
    NPCM_PCS_SR_MII_LP_BABL = 0x3e000a,
    NPCM_PCS_SR_MII_AN_EXPN = 0x3e000c,
    NPCM_PCS_SR_MII_EXT_STS = 0x3e001e,

    NPCM_PCS_SR_TIM_SYNC_ABL = 0x3e0e10,
    NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_LWR = 0x3e0e12,
    NPCM_PCS_SR_TIM_SYNC_TX_MAX_DLY_UPR = 0x3e0e14,
    NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_LWR = 0x3e0e16,
    NPCM_PCS_SR_TIM_SYNC_TX_MIN_DLY_UPR = 0x3e0e18,
    NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_LWR = 0x3e0e1a,
    NPCM_PCS_SR_TIM_SYNC_RX_MAX_DLY_UPR = 0x3e0e1c,
    NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_LWR = 0x3e0e1e,
    NPCM_PCS_SR_TIM_SYNC_RX_MIN_DLY_UPR = 0x3e0e20,

    NPCM_PCS_VR_MII_MMD_DIG_CTRL1 = 0x3f0000,
    NPCM_PCS_VR_MII_AN_CTRL = 0x3f0002,
    NPCM_PCS_VR_MII_AN_INTR_STS = 0x3f0004,
    NPCM_PCS_VR_MII_TC = 0x3f0006,
    NPCM_PCS_VR_MII_DBG_CTRL = 0x3f000a,
    NPCM_PCS_VR_MII_EEE_MCTRL0 = 0x3f000c,
    NPCM_PCS_VR_MII_EEE_TXTIMER = 0x3f0010,
    NPCM_PCS_VR_MII_EEE_RXTIMER = 0x3f0012,
    NPCM_PCS_VR_MII_LINK_TIMER_CTRL = 0x3f0014,
    NPCM_PCS_VR_MII_EEE_MCTRL1 = 0x3f0016,
    NPCM_PCS_VR_MII_DIG_STS = 0x3f0020,
    NPCM_PCS_VR_MII_ICG_ERRCNT1 = 0x3f0022,
    NPCM_PCS_VR_MII_MISC_STS = 0x3f0030,
    NPCM_PCS_VR_MII_RX_LSTS = 0x3f0040,
    NPCM_PCS_VR_MII_MP_TX_BSTCTRL0 = 0x3f0070,
    NPCM_PCS_VR_MII_MP_TX_LVLCTRL0 = 0x3f0074,
    NPCM_PCS_VR_MII_MP_TX_GENCTRL0 = 0x3f007a,
    NPCM_PCS_VR_MII_MP_TX_GENCTRL1 = 0x3f007c,
    NPCM_PCS_VR_MII_MP_TX_STS = 0x3f0090,
    NPCM_PCS_VR_MII_MP_RX_GENCTRL0 = 0x3f00b0,
    NPCM_PCS_VR_MII_MP_RX_GENCTRL1 = 0x3f00b2,
    NPCM_PCS_VR_MII_MP_RX_LOS_CTRL0 = 0x3f00ba,
    NPCM_PCS_VR_MII_MP_MPLL_CTRL0 = 0x3f00f0,
    NPCM_PCS_VR_MII_MP_MPLL_CTRL1 = 0x3f00f2,
    NPCM_PCS_VR_MII_MP_MPLL_STS = 0x3f0110,
    NPCM_PCS_VR_MII_MP_MISC_CTRL2 = 0x3f0126,
    NPCM_PCS_VR_MII_MP_LVL_CTRL = 0x3f0130,
    NPCM_PCS_VR_MII_MP_MISC_CTRL0 = 0x3f0132,
    NPCM_PCS_VR_MII_MP_MISC_CTRL1 = 0x3f0134,
    NPCM_PCS_VR_MII_DIG_CTRL2 = 0x3f01c2,
    NPCM_PCS_VR_MII_DIG_ERRCNT_SEL = 0x3f01c4,
} NPCMRegister;

static uint32_t gmac_read(QTestState *qts, const GMACModule *mod,
                          NPCMRegister regno)
{
    return qtest_readl(qts, mod->base_addr + regno);
}

/* Check that GMAC registers are reset to default value */
static void test_init(gconstpointer test_data)
{
    const TestData *td = test_data;
    const GMACModule *mod = td->module;
    QTestState *qts = qtest_init("-machine npcm750-evb");

#define CHECK_REG32(regno, value) \
    do { \
        g_assert_cmphex(gmac_read(qts, mod, (regno)), ==, (value)); \
    } while (0)

    CHECK_REG32(NPCM_DMA_BUS_MODE, 0x00020100);
    CHECK_REG32(NPCM_DMA_XMT_POLL_DEMAND, 0);
    CHECK_REG32(NPCM_DMA_RCV_POLL_DEMAND, 0);
    CHECK_REG32(NPCM_DMA_RCV_BASE_ADDR, 0);
    CHECK_REG32(NPCM_DMA_TX_BASE_ADDR, 0);
    CHECK_REG32(NPCM_DMA_STATUS, 0);
    CHECK_REG32(NPCM_DMA_CONTROL, 0);
    CHECK_REG32(NPCM_DMA_INTR_ENA, 0);
    CHECK_REG32(NPCM_DMA_MISSED_FRAME_CTR, 0);
    CHECK_REG32(NPCM_DMA_HOST_TX_DESC, 0);
    CHECK_REG32(NPCM_DMA_HOST_RX_DESC, 0);
    CHECK_REG32(NPCM_DMA_CUR_TX_BUF_ADDR, 0);
    CHECK_REG32(NPCM_DMA_CUR_RX_BUF_ADDR, 0);
    CHECK_REG32(NPCM_DMA_HW_FEATURE, 0x100d4f37);

    CHECK_REG32(NPCM_GMAC_MAC_CONFIG, 0);
    CHECK_REG32(NPCM_GMAC_FRAME_FILTER, 0);
    CHECK_REG32(NPCM_GMAC_HASH_HIGH, 0);
    CHECK_REG32(NPCM_GMAC_HASH_LOW, 0);
    CHECK_REG32(NPCM_GMAC_MII_ADDR, 0);
    CHECK_REG32(NPCM_GMAC_MII_DATA, 0);
    CHECK_REG32(NPCM_GMAC_FLOW_CTRL, 0);
    CHECK_REG32(NPCM_GMAC_VLAN_FLAG, 0);
    CHECK_REG32(NPCM_GMAC_VERSION, 0x00001032);
    CHECK_REG32(NPCM_GMAC_WAKEUP_FILTER, 0);
    CHECK_REG32(NPCM_GMAC_PMT, 0);
    CHECK_REG32(NPCM_GMAC_LPI_CTRL, 0);
    CHECK_REG32(NPCM_GMAC_TIMER_CTRL, 0x03e80000);
    CHECK_REG32(NPCM_GMAC_INT_STATUS, 0);
    CHECK_REG32(NPCM_GMAC_INT_MASK, 0);
    CHECK_REG32(NPCM_GMAC_MAC0_ADDR_HI, 0x8000ffff);
    CHECK_REG32(NPCM_GMAC_MAC0_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_MAC1_ADDR_HI, 0x0000ffff);
    CHECK_REG32(NPCM_GMAC_MAC1_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_MAC2_ADDR_HI, 0x0000ffff);
    CHECK_REG32(NPCM_GMAC_MAC2_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_MAC3_ADDR_HI, 0x0000ffff);
    CHECK_REG32(NPCM_GMAC_MAC3_ADDR_LO, 0xffffffff);
    CHECK_REG32(NPCM_GMAC_RGMII_STATUS, 0);
    CHECK_REG32(NPCM_GMAC_WATCHDOG, 0);
    CHECK_REG32(NPCM_GMAC_PTP_TCR, 0x00002000);
    CHECK_REG32(NPCM_GMAC_PTP_SSIR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STSR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STNSR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STSUR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_STNSUR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_TAR, 0);
    CHECK_REG32(NPCM_GMAC_PTP_TTSR, 0);

    qtest_quit(qts);
}

static void gmac_add_test(const char *name, const TestData* td,
                          GTestDataFunc fn)
{
    g_autofree char *full_name = g_strdup_printf(
            "npcm7xx_gmac/gmac[%d]/%s", gmac_module_index(td->module), name);
    qtest_add_data_func(full_name, td, fn);
}

int main(int argc, char **argv)
{
    TestData test_data_list[ARRAY_SIZE(gmac_module_list)];

    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < ARRAY_SIZE(gmac_module_list); ++i) {
        TestData *td = &test_data_list[i];

        td->module = &gmac_module_list[i];

        gmac_add_test("init", td, test_init);
    }

    return g_test_run();
}
