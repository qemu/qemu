/*
 * QEMU model of the Xilinx Zynq Double Data Rate Controller
 *
 * Copyright (c) Beckhoff Automation GmbH. & Co. KG
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/core/registerfields.h"
#include "system/block-backend.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/dma.h"
#include "hw/misc/xlnx-zynq-ddrc.h"
#include "migration/vmstate.h"

#ifndef DDRCTRL_ERR_DEBUG
#define DDRCTRL_ERR_DEBUG 0
#endif

static void zynq_ddrctrl_post_write(RegisterInfo *reg, uint64_t val)
{
    DDRCTRLState *s = DDRCTRL(reg->opaque);
    if (reg->access->addr == A_DDRC_CTRL) {
        if (val & 0x1) {
            s->reg[R_MODE_STS_REG] |=
                (R_MODE_STS_REG_DDR_REG_OPERATING_MODE_MASK & 0x1);
        } else {
            s->reg[R_MODE_STS_REG] &=
                ~R_MODE_STS_REG_DDR_REG_OPERATING_MODE_MASK;
        }
    }
}

static const RegisterAccessInfo xlnx_zynq_ddrc_regs_info[] = {
    /* 0x00 - 0x3C: Basic DDRC control and config */
    { .name = "DDRC_CTRL",
      .addr = A_DDRC_CTRL,
      .reset = 0x00000200,
      .post_write = zynq_ddrctrl_post_write },
    { .name = "TWO_RANK_CFG",
      .addr = A_TWO_RANK_CFG,
      .reset = 0x000C1076 },
    { .name = "HPR_REG",
      .addr = A_HPR_REG,
      .reset = 0x03C0780F },
    { .name = "LPR_REG",
      .addr = A_LPR_REG,
      .reset = 0x03C0780F },
    { .name = "WR_REG",
      .addr = A_WR_REG,
      .reset = 0x0007F80F },
    { .name = "DRAM_PARAM_REG0",
      .addr = A_DRAM_PARAM_REG0,
      .reset = 0x00041016 },
    { .name = "DRAM_PARAM_REG1",
      .addr = A_DRAM_PARAM_REG1,
      .reset = 0x351B48D9 },
    { .name = "DRAM_PARAM_REG2",
      .addr = A_DRAM_PARAM_REG2,
      .reset = 0x83015904 },
    { .name = "DRAM_PARAM_REG3",
      .addr = A_DRAM_PARAM_REG3,
      .reset = 0x250882D0 },
    { .name = "DRAM_PARAM_REG4",
      .addr = A_DRAM_PARAM_REG4,
      .reset = 0x0000003C },
    { .name = "DRAM_INIT_PARAM",
      .addr = A_DRAM_INIT_PARAM,
      .reset = 0x00002007 },
    { .name = "DRAM_EMR_REG",
      .addr = A_DRAM_EMR_REG,
      .reset = 0x00000008 },
    { .name = "DRAM_EMR_MR_REG",
      .addr = A_DRAM_EMR_MR_REG,
      .reset = 0x00000940 },
    { .name = "DRAM_BURST8_RDWR",
      .addr = A_DRAM_BURST8_RDWR,
      .reset = 0x00020034 },
    { .name = "DRAM_DISABLE_DQ",
      .addr = A_DRAM_DISABLE_DQ },
    { .name = "DRAM_ADDR_MAP_BANK",
      .addr = A_DRAM_ADDR_MAP_BANK,
      .reset = 0x00000F77 },
    { .name = "DRAM_ADDR_MAP_COL",
      .addr = A_DRAM_ADDR_MAP_COL,
      .reset = 0xFFF00000 },
    { .name = "DRAM_ADDR_MAP_ROW",
      .addr = A_DRAM_ADDR_MAP_ROW,
      .reset = 0x0FF55555 },
    { .name = "DRAM_ODT_REG",
      .addr = A_DRAM_ODT_REG,
      .reset = 0x00000249 },

    /* 0x4C - 0x5C: PHY and DLL */
    { .name = "PHY_DBG_REG",
      .addr = A_PHY_DBG_REG },
    { .name = "PHY_CMD_TIMEOUT_RDDATA_CPT",
      .addr = A_PHY_CMD_TIMEOUT_RDDATA_CPT,
      .reset = 0x00010200 },
    { .name = "MODE_STS_REG",
      .addr = A_MODE_STS_REG },
    { .name = "DLL_CALIB",
      .addr = A_DLL_CALIB,
      .reset = 0x00000101 },
    { .name = "ODT_DELAY_HOLD",
      .addr = A_ODT_DELAY_HOLD,
      .reset = 0x00000023 },

    /* 0x60 - 0x7C: Control registers */
    { .name = "CTRL_REG1",
      .addr = A_CTRL_REG1,
      .reset = 0x0000003E },
    { .name = "CTRL_REG2",
      .addr = A_CTRL_REG2,
      .reset = 0x00020000 },
    { .name = "CTRL_REG3",
      .addr = A_CTRL_REG3,
      .reset = 0x00284027 },
    { .name = "CTRL_REG4",
      .addr = A_CTRL_REG4,
      .reset = 0x00001610 },
    { .name = "CTRL_REG5",
      .addr = A_CTRL_REG5,
      .reset = 0x00455111 },
    { .name = "CTRL_REG6",
      .addr = A_CTRL_REG6,
      .reset = 0x00032222 },

    /* 0xA0 - 0xB4: Refresh, ZQ, powerdown, misc */
    { .name = "CHE_REFRESH_TIMER0",
      .addr = A_CHE_REFRESH_TIMER0,
      .reset = 0x00008000 },
    { .name = "CHE_T_ZQ",
      .addr = A_CHE_T_ZQ,
      .reset = 0x10300802 },
    { .name = "CHE_T_ZQ_SHORT_INTERVAL_REG",
      .addr = A_CHE_T_ZQ_SHORT_INTERVAL_REG,
      .reset = 0x0020003A },
    { .name = "DEEP_PWRDWN_REG",
      .addr = A_DEEP_PWRDWN_REG },
    { .name = "REG_2C",
      .addr = A_REG_2C },
    { .name = "REG_2D",
      .addr = A_REG_2D,
      .reset = 0x00000200 },

    /* 0xB8 - 0xF8: ECC, DFI, etc. */
    { .name = "DFI_TIMING",
      .addr = A_DFI_TIMING,
      .reset = 0x00200067 },
    { .name = "CHE_ECC_CONTROL_REG_OFFSET",
      .addr = A_CHE_ECC_CONTROL_REG_OFFSET },
    { .name = "CHE_CORR_ECC_LOG_REG_OFFSET",
      .addr = A_CHE_CORR_ECC_LOG_REG_OFFSET },
    { .name = "CHE_CORR_ECC_ADDR_REG_OFFSET",
      .addr = A_CHE_CORR_ECC_ADDR_REG_OFFSET },
    { .name = "CHE_CORR_ECC_DATA_31_0_REG_OFFSET",
      .addr = A_CHE_CORR_ECC_DATA_31_0_REG_OFFSET },
    { .name = "CHE_CORR_ECC_DATA_63_32_REG_OFFSET",
      .addr = A_CHE_CORR_ECC_DATA_63_32_REG_OFFSET },
    { .name = "CHE_CORR_ECC_DATA_71_64_REG_OFFSET",
      .addr = A_CHE_CORR_ECC_DATA_71_64_REG_OFFSET },
    { .name = "CHE_UNCORR_ECC_LOG_REG_OFFSET",
      .addr = A_CHE_UNCORR_ECC_LOG_REG_OFFSET },
    { .name = "CHE_UNCORR_ECC_ADDR_REG_OFFSET",
      .addr = A_CHE_UNCORR_ECC_ADDR_REG_OFFSET },
    { .name = "CHE_UNCORR_ECC_DATA_31_0_REG_OFFSET",
      .addr = A_CHE_UNCORR_ECC_DATA_31_0_REG_OFFSET },
    { .name = "CHE_UNCORR_ECC_DATA_63_32_REG_OFFSET",
      .addr = A_CHE_UNCORR_ECC_DATA_63_32_REG_OFFSET },
    { .name = "CHE_UNCORR_ECC_DATA_71_64_REG_OFFSET",
      .addr = A_CHE_UNCORR_ECC_DATA_71_64_REG_OFFSET },
    { .name = "CHE_ECC_STATS_REG_OFFSET",
      .addr = A_CHE_ECC_STATS_REG_OFFSET },
    { .name = "ECC_SCRUB",
      .addr = A_ECC_SCRUB,
      .reset = 0x00000008 },
    { .name = "CHE_ECC_CORR_BIT_MASK_31_0_REG_OFFSET",
      .addr = A_CHE_ECC_CORR_BIT_MASK_31_0_REG_OFFSET },
    { .name = "CHE_ECC_CORR_BIT_MASK_63_32_REG_OFFSET",
      .addr = A_CHE_ECC_CORR_BIT_MASK_63_32_REG_OFFSET },

    /* 0x114 - 0x174: PHY config, ratios, DQS, WE */
    { .name = "PHY_RCVER_ENABLE",
      .addr = A_PHY_RCVER_ENABLE },
    { .name = "PHY_CONFIG0",
      .addr = A_PHY_CONFIG0,
      .reset = 0x40000001 },
    { .name = "PHY_CONFIG1",
      .addr = A_PHY_CONFIG1,
      .reset = 0x40000001 },
    { .name = "PHY_CONFIG2",
      .addr = A_PHY_CONFIG2,
      .reset = 0x40000001 },
    { .name = "PHY_CONFIG3",
      .addr = A_PHY_CONFIG3,
      .reset = 0x40000001 },
    { .name = "PHY_INIT_RATIO0",
      .addr = A_PHY_INIT_RATIO0 },
    { .name = "PHY_INIT_RATIO1",
      .addr = A_PHY_INIT_RATIO1 },
    { .name = "PHY_INIT_RATIO2",
      .addr = A_PHY_INIT_RATIO2 },
    { .name = "PHY_INIT_RATIO3",
      .addr = A_PHY_INIT_RATIO3 },
    { .name = "PHY_RD_DQS_CFG0",
      .addr = A_PHY_RD_DQS_CFG0,
      .reset = 0x00000040 },
    { .name = "PHY_RD_DQS_CFG1",
      .addr = A_PHY_RD_DQS_CFG1,
      .reset = 0x00000040 },
    { .name = "PHY_RD_DQS_CFG2",
      .addr = A_PHY_RD_DQS_CFG2,
      .reset = 0x00000040 },
    { .name = "PHY_RD_DQS_CFG3",
      .addr = A_PHY_RD_DQS_CFG3,
      .reset = 0x00000040 },
    { .name = "PHY_WR_DQS_CFG0",
      .addr = A_PHY_WR_DQS_CFG0 },
    { .name = "PHY_WR_DQS_CFG1",
      .addr = A_PHY_WR_DQS_CFG1 },
    { .name = "PHY_WR_DQS_CFG2",
      .addr = A_PHY_WR_DQS_CFG2 },
    { .name = "PHY_WR_DQS_CFG3",
      .addr = A_PHY_WR_DQS_CFG3 },
    { .name = "PHY_WE_CFG0",
      .addr = A_PHY_WE_CFG0,
      .reset = 0x00000040 },
    { .name = "PHY_WE_CFG1",
      .addr = A_PHY_WE_CFG1,
      .reset = 0x00000040 },
    { .name = "PHY_WE_CFG2",
      .addr = A_PHY_WE_CFG2,
      .reset = 0x00000040 },
    { .name = "PHY_WE_CFG3",
      .addr = A_PHY_WE_CFG3,
      .reset = 0x00000040 },

    /* 0x17C - 0x194: Write data slaves, misc */
    { .name = "WR_DATA_SLV0",
      .addr = A_WR_DATA_SLV0,
      .reset = 0x00000080 },
    { .name = "WR_DATA_SLV1",
      .addr = A_WR_DATA_SLV1,
      .reset = 0x00000080 },
    { .name = "WR_DATA_SLV2",
      .addr = A_WR_DATA_SLV2,
      .reset = 0x00000080 },
    { .name = "WR_DATA_SLV3",
      .addr = A_WR_DATA_SLV3,
      .reset = 0x00000080 },
    { .name = "REG_64",
      .addr = A_REG_64,
      .reset = 0x10020000 },
    { .name = "REG_65",
      .addr = A_REG_65 },

    /* 0x1A4 - 0x1C4: Misc registers */
    { .name = "REG69_6A0",
      .addr = A_REG69_6A0 },
    { .name = "REG69_6A1",
      .addr = A_REG69_6A1 },
    { .name = "REG6C_6D2",
      .addr = A_REG6C_6D2 },
    { .name = "REG6C_6D3",
      .addr = A_REG6C_6D3 },
    { .name = "REG6E_710",
      .addr = A_REG6E_710 },
    { .name = "REG6E_711",
      .addr = A_REG6E_711 },
    { .name = "REG6E_712",
      .addr = A_REG6E_712 },
    { .name = "REG6E_713",
      .addr = A_REG6E_713 },

    /* 0x1CC - 0x1E8: DLL, PHY status */
    { .name = "PHY_DLL_STS0",
      .addr = A_PHY_DLL_STS0 },
    { .name = "PHY_DLL_STS1",
      .addr = A_PHY_DLL_STS1 },
    { .name = "PHY_DLL_STS2",
      .addr = A_PHY_DLL_STS2 },
    { .name = "PHY_DLL_STS3",
      .addr = A_PHY_DLL_STS3 },
    { .name = "DLL_LOCK_STS",
      .addr = A_DLL_LOCK_STS },
    { .name = "PHY_CTRL_STS",
      .addr = A_PHY_CTRL_STS },
    { .name = "PHY_CTRL_STS_REG2",
      .addr = A_PHY_CTRL_STS_REG2 },

    /* 0x200 - 0x2B4: AXI, LPDDR, misc */
    { .name = "AXI_ID",
      .addr = A_AXI_ID },
    { .name = "PAGE_MASK",
      .addr = A_PAGE_MASK },
    { .name = "AXI_PRIORITY_WR_PORT0",
      .addr = A_AXI_PRIORITY_WR_PORT0,
      .reset = 0x000803FF },
    { .name = "AXI_PRIORITY_WR_PORT1",
      .addr = A_AXI_PRIORITY_WR_PORT1,
      .reset = 0x000803FF },
    { .name = "AXI_PRIORITY_WR_PORT2",
      .addr = A_AXI_PRIORITY_WR_PORT2,
      .reset = 0x000803FF },
    { .name = "AXI_PRIORITY_WR_PORT3",
      .addr = A_AXI_PRIORITY_WR_PORT3,
      .reset = 0x000803FF },
    { .name = "AXI_PRIORITY_RD_PORT0",
      .addr = A_AXI_PRIORITY_RD_PORT0,
      .reset = 0x000003FF },
    { .name = "AXI_PRIORITY_RD_PORT1",
      .addr = A_AXI_PRIORITY_RD_PORT1,
      .reset = 0x000003FF },
    { .name = "AXI_PRIORITY_RD_PORT2",
      .addr = A_AXI_PRIORITY_RD_PORT2,
      .reset = 0x000003FF },
    { .name = "AXI_PRIORITY_RD_PORT3",
      .addr = A_AXI_PRIORITY_RD_PORT3,
      .reset = 0x000003FF },
    { .name = "EXCL_ACCESS_CFG0",
      .addr = A_EXCL_ACCESS_CFG0 },
    { .name = "EXCL_ACCESS_CFG1",
      .addr = A_EXCL_ACCESS_CFG1 },
    { .name = "EXCL_ACCESS_CFG2",
      .addr = A_EXCL_ACCESS_CFG2 },
    { .name = "EXCL_ACCESS_CFG3",
      .addr = A_EXCL_ACCESS_CFG3 },
    { .name = "MODE_REG_READ",
      .addr = A_MODE_REG_READ },
    { .name = "LPDDR_CTRL0",
      .addr = A_LPDDR_CTRL0 },
    { .name = "LPDDR_CTRL1",
      .addr = A_LPDDR_CTRL1 },
    { .name = "LPDDR_CTRL2",
      .addr = A_LPDDR_CTRL2,
      .reset = 0x003C0015 },
    { .name = "LPDDR_CTRL3",
      .addr = A_LPDDR_CTRL3,
      .reset = 0x00000601 },
};

static void zynq_ddrctrl_reset(DeviceState *dev)
{
    DDRCTRLState *s = DDRCTRL(dev);
    int i;

    for (i = 0; i < ZYNQ_DDRCTRL_NUM_REG; ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static const MemoryRegionOps ddrctrl_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_zynq_ddrctrl = {
    .name = "zynq_ddrc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, DDRCTRLState, ZYNQ_DDRCTRL_NUM_REG),
        VMSTATE_END_OF_LIST()
    }
};

static void zynq_ddrctrl_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DDRCTRLState *s = DDRCTRL(obj);

    s->reg_array =
        register_init_block32(DEVICE(obj), xlnx_zynq_ddrc_regs_info,
                              ARRAY_SIZE(xlnx_zynq_ddrc_regs_info),
                              s->regs_info, s->reg,
                              &ddrctrl_ops,
                              DDRCTRL_ERR_DEBUG,
                              ZYNQ_DDRCTRL_MMIO_SIZE);

    sysbus_init_mmio(sbd, &s->reg_array->mem);
}

static void zynq_ddrctrl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, zynq_ddrctrl_reset);
    dc->vmsd = &vmstate_zynq_ddrctrl;
}

static const TypeInfo ddrctrl_info = {
    .name = TYPE_DDRCTRL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DDRCTRLState),
    .instance_init = zynq_ddrctrl_init,
    .class_init = zynq_ddrctrl_class_init,
};

static void ddrctrl_register_types(void)
{
    type_register_static(&ddrctrl_info);
}

type_init(ddrctrl_register_types)
