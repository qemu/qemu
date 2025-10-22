/*
 * QEMU model of the Clock-Reset-LPD (CRL).
 *
 * Copyright (c) 2022-2025 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/register.h"
#include "hw/resettable.h"

#include "target/arm/arm-powerctl.h"
#include "target/arm/multiprocessing.h"
#include "hw/misc/xlnx-versal-crl.h"

#ifndef XLNX_VERSAL_CRL_ERR_DEBUG
#define XLNX_VERSAL_CRL_ERR_DEBUG 0
#endif

static void crl_update_irq(XlnxVersalCRL *s)
{
    bool pending = s->regs[R_IR_STATUS] & ~s->regs[R_IR_MASK];
    qemu_set_irq(s->irq, pending);
}

static void crl_status_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);
    crl_update_irq(s);
}

static uint64_t crl_enable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IR_MASK] &= ~val;
    crl_update_irq(s);
    return 0;
}

static uint64_t crl_disable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IR_MASK] |= val;
    crl_update_irq(s);
    return 0;
}

static DeviceState **versal_decode_periph_rst(XlnxVersalCRLBase *s,
                                              hwaddr addr, size_t *count)
{
    size_t idx;
    XlnxVersalCRL *xvc = XLNX_VERSAL_CRL(s);

    *count = 1;

    switch (addr) {
    case A_RST_CPU_R5:
        return xvc->cfg.rpu;

    case A_RST_ADMA:
        /* A single register fans out to all DMA reset inputs */
        *count = ARRAY_SIZE(xvc->cfg.adma);
        return xvc->cfg.adma;

    case A_RST_UART0 ... A_RST_UART1:
        idx = (addr - A_RST_UART0) / sizeof(uint32_t);
        return xvc->cfg.uart + idx;

    case A_RST_GEM0 ... A_RST_GEM1:
        idx = (addr - A_RST_GEM0) / sizeof(uint32_t);
        return xvc->cfg.gem + idx;

    case A_RST_USB0:
        return xvc->cfg.usb;

    default:
        /* invalid or unimplemented */
        g_assert_not_reached();
    }
}

static DeviceState **versal2_decode_periph_rst(XlnxVersalCRLBase *s,
                                               hwaddr addr, size_t *count)
{
    size_t idx;
    XlnxVersal2CRL *xvc = XLNX_VERSAL2_CRL(s);

    *count = 1;

    switch (addr) {
    case A_VERSAL2_RST_RPU_A ... A_VERSAL2_RST_RPU_E:
        idx = (addr - A_VERSAL2_RST_RPU_A) / sizeof(uint32_t);
        idx *= 2; /* two RPUs per RST_RPU_x registers */
        return xvc->cfg.rpu + idx;

    case A_VERSAL2_RST_ADMA:
        /* A single register fans out to all DMA reset inputs */
        *count = ARRAY_SIZE(xvc->cfg.adma);
        return xvc->cfg.adma;

    case A_VERSAL2_RST_SDMA:
        *count = ARRAY_SIZE(xvc->cfg.sdma);
        return xvc->cfg.sdma;

    case A_VERSAL2_RST_UART0 ... A_VERSAL2_RST_UART1:
        idx = (addr - A_VERSAL2_RST_UART0) / sizeof(uint32_t);
        return xvc->cfg.uart + idx;

    case A_VERSAL2_RST_GEM0 ... A_VERSAL2_RST_GEM1:
        idx = (addr - A_VERSAL2_RST_GEM0) / sizeof(uint32_t);
        return xvc->cfg.gem + idx;

    case A_VERSAL2_RST_USB0 ... A_VERSAL2_RST_USB1:
        idx = (addr - A_VERSAL2_RST_USB0) / sizeof(uint32_t);
        return xvc->cfg.usb + idx;

    case A_VERSAL2_RST_CAN0 ... A_VERSAL2_RST_CAN3:
        idx = (addr - A_VERSAL2_RST_CAN0) / sizeof(uint32_t);
        return xvc->cfg.can + idx;

    default:
        /* invalid or unimplemented */
        return NULL;
    }
}

static uint64_t crl_rst_cpu_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRLBase *s = XLNX_VERSAL_CRL_BASE(reg->opaque);
    XlnxVersalCRLBaseClass *xvcbc = XLNX_VERSAL_CRL_BASE_GET_CLASS(s);
    DeviceState **dev;
    size_t i, count;

    dev = xvcbc->decode_periph_rst(s, reg->access->addr, &count);

    for (i = 0; i < 2; i++) {
        bool prev, new;
        uint64_t aff;

        prev = extract32(s->regs[reg->access->addr / 4], i, 1);
        new = extract32(val64, i, 1);

        if (prev == new) {
            continue;
        }

        aff = arm_cpu_mp_affinity(ARM_CPU(dev[i]));

        if (new) {
            arm_set_cpu_off(aff);
        } else {
            arm_set_cpu_on_and_reset(aff);
        }
    }

    return val64;
}

static uint64_t crl_rst_dev_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRLBase *s = XLNX_VERSAL_CRL_BASE(reg->opaque);
    XlnxVersalCRLBaseClass *xvcbc = XLNX_VERSAL_CRL_BASE_GET_CLASS(s);
    DeviceState **dev;
    bool prev, new;
    size_t i, count;

    dev = xvcbc->decode_periph_rst(s, reg->access->addr, &count);

    if (dev == NULL) {
        return val64;
    }

    prev = s->regs[reg->access->addr / 4] & 0x1;
    new = val64 & 0x1;

    if (prev == new) {
        return val64;
    }

    for (i = 0; i < count; i++) {
        if (dev[i]) {
            device_cold_reset(dev[i]);
        }
    }

    return val64;
}

static const RegisterAccessInfo crl_regs_info[] = {
    {   .name = "ERR_CTRL",  .addr = A_ERR_CTRL,
    },{ .name = "IR_STATUS",  .addr = A_IR_STATUS,
        .w1c = 0x1,
        .post_write = crl_status_postw,
    },{ .name = "IR_MASK",  .addr = A_IR_MASK,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "IR_ENABLE",  .addr = A_IR_ENABLE,
        .pre_write = crl_enable_prew,
    },{ .name = "IR_DISABLE",  .addr = A_IR_DISABLE,
        .pre_write = crl_disable_prew,
    },{ .name = "WPROT",  .addr = A_WPROT,
    },{ .name = "PLL_CLK_OTHER_DMN",  .addr = A_PLL_CLK_OTHER_DMN,
        .reset = 0x1,
        .rsvd = 0xe,
    },{ .name = "RPLL_CTRL",  .addr = A_RPLL_CTRL,
        .reset = 0x24809,
        .rsvd = 0xf88c00f6,
    },{ .name = "RPLL_CFG",  .addr = A_RPLL_CFG,
        .reset = 0x2000000,
        .rsvd = 0x1801210,
    },{ .name = "RPLL_FRAC_CFG",  .addr = A_RPLL_FRAC_CFG,
        .rsvd = 0x7e330000,
    },{ .name = "PLL_STATUS",  .addr = A_PLL_STATUS,
        .reset = R_PLL_STATUS_RPLL_STABLE_MASK |
                 R_PLL_STATUS_RPLL_LOCK_MASK,
        .rsvd = 0xfa,
        .ro = 0x5,
    },{ .name = "RPLL_TO_XPD_CTRL",  .addr = A_RPLL_TO_XPD_CTRL,
        .reset = 0x2000100,
        .rsvd = 0xfdfc00ff,
    },{ .name = "LPD_TOP_SWITCH_CTRL",  .addr = A_LPD_TOP_SWITCH_CTRL,
        .reset = 0x6000300,
        .rsvd = 0xf9fc00f8,
    },{ .name = "LPD_LSBUS_CTRL",  .addr = A_LPD_LSBUS_CTRL,
        .reset = 0x2000800,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CPU_R5_CTRL",  .addr = A_CPU_R5_CTRL,
        .reset = 0xe000300,
        .rsvd = 0xe1fc00f8,
    },{ .name = "IOU_SWITCH_CTRL",  .addr = A_IOU_SWITCH_CTRL,
        .reset = 0x2000500,
        .rsvd = 0xfdfc00f8,
    },{ .name = "GEM0_REF_CTRL",  .addr = A_GEM0_REF_CTRL,
        .reset = 0xe000a00,
        .rsvd = 0xf1fc00f8,
    },{ .name = "GEM1_REF_CTRL",  .addr = A_GEM1_REF_CTRL,
        .reset = 0xe000a00,
        .rsvd = 0xf1fc00f8,
    },{ .name = "GEM_TSU_REF_CTRL",  .addr = A_GEM_TSU_REF_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "USB0_BUS_REF_CTRL",  .addr = A_USB0_BUS_REF_CTRL,
        .reset = 0x2001900,
        .rsvd = 0xfdfc00f8,
    },{ .name = "UART0_REF_CTRL",  .addr = A_UART0_REF_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "UART1_REF_CTRL",  .addr = A_UART1_REF_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "SPI0_REF_CTRL",  .addr = A_SPI0_REF_CTRL,
        .reset = 0x600,
        .rsvd = 0xfdfc00f8,
    },{ .name = "SPI1_REF_CTRL",  .addr = A_SPI1_REF_CTRL,
        .reset = 0x600,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CAN0_REF_CTRL",  .addr = A_CAN0_REF_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CAN1_REF_CTRL",  .addr = A_CAN1_REF_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I2C0_REF_CTRL",  .addr = A_I2C0_REF_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I2C1_REF_CTRL",  .addr = A_I2C1_REF_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "DBG_LPD_CTRL",  .addr = A_DBG_LPD_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "TIMESTAMP_REF_CTRL",  .addr = A_TIMESTAMP_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CRL_SAFETY_CHK",  .addr = A_CRL_SAFETY_CHK,
    },{ .name = "PSM_REF_CTRL",  .addr = A_PSM_REF_CTRL,
        .reset = 0xf04,
        .rsvd = 0xfffc00f8,
    },{ .name = "DBG_TSTMP_CTRL",  .addr = A_DBG_TSTMP_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CPM_TOPSW_REF_CTRL",  .addr = A_CPM_TOPSW_REF_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "USB3_DUAL_REF_CTRL",  .addr = A_USB3_DUAL_REF_CTRL,
        .reset = 0x3c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "RST_CPU_R5",  .addr = A_RST_CPU_R5,
        .reset = 0x17,
        .rsvd = 0x8,
        .pre_write = crl_rst_cpu_prew,
    },{ .name = "RST_ADMA",  .addr = A_RST_ADMA,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_GEM0",  .addr = A_RST_GEM0,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_GEM1",  .addr = A_RST_GEM1,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_SPARE",  .addr = A_RST_SPARE,
        .reset = 0x1,
    },{ .name = "RST_USB0",  .addr = A_RST_USB0,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_UART0",  .addr = A_RST_UART0,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_UART1",  .addr = A_RST_UART1,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_SPI0",  .addr = A_RST_SPI0,
        .reset = 0x1,
    },{ .name = "RST_SPI1",  .addr = A_RST_SPI1,
        .reset = 0x1,
    },{ .name = "RST_CAN0",  .addr = A_RST_CAN0,
        .reset = 0x1,
    },{ .name = "RST_CAN1",  .addr = A_RST_CAN1,
        .reset = 0x1,
    },{ .name = "RST_I2C0",  .addr = A_RST_I2C0,
        .reset = 0x1,
    },{ .name = "RST_I2C1",  .addr = A_RST_I2C1,
        .reset = 0x1,
    },{ .name = "RST_DBG_LPD",  .addr = A_RST_DBG_LPD,
        .reset = 0x33,
        .rsvd = 0xcc,
    },{ .name = "RST_GPIO",  .addr = A_RST_GPIO,
        .reset = 0x1,
    },{ .name = "RST_TTC",  .addr = A_RST_TTC,
        .reset = 0xf,
    },{ .name = "RST_TIMESTAMP",  .addr = A_RST_TIMESTAMP,
        .reset = 0x1,
    },{ .name = "RST_SWDT",  .addr = A_RST_SWDT,
        .reset = 0x1,
    },{ .name = "RST_OCM",  .addr = A_RST_OCM,
    },{ .name = "RST_IPI",  .addr = A_RST_IPI,
    },{ .name = "RST_FPD",  .addr = A_RST_FPD,
        .reset = 0x3,
    },{ .name = "PSM_RST_MODE",  .addr = A_PSM_RST_MODE,
        .reset = 0x1,
        .rsvd = 0xf8,
    }
};

static const RegisterAccessInfo versal2_crl_regs_info[] = {
    {   .name = "ERR_CTRL",  .addr = A_VERSAL2_ERR_CTRL,
        .reset = 0x1,
    },{ .name = "WPROT",  .addr = A_VERSAL2_WPROT,
    },{ .name = "RPLL_CTRL",  .addr = A_VERSAL2_RPLL_CTRL,
        .reset = 0x24809,
        .rsvd = 0xf88c00f6,
    },{ .name = "RPLL_CFG",  .addr = A_VERSAL2_RPLL_CFG,
        .reset = 0x7e5dcc6c,
        .rsvd = 0x1801210,
    },{ .name = "FLXPLL_CTRL",  .addr = A_VERSAL2_FLXPLL_CTRL,
        .reset = 0x24809,
        .rsvd = 0xf88c00f6,
    },{ .name = "FLXPLL_CFG",  .addr = A_VERSAL2_FLXPLL_CFG,
        .reset = 0x7e5dcc6c,
        .rsvd = 0x1801210,
    },{ .name = "PLL_STATUS",  .addr = A_VERSAL2_PLL_STATUS,
        .reset = 0xf,
        .rsvd = 0xf0,
        .ro = 0xf,
    },{ .name = "RPLL_TO_XPD_CTRL",  .addr = A_VERSAL2_RPLL_TO_XPD_CTRL,
        .reset = 0x2000100,
        .rsvd = 0xfdfc00ff,
    },{ .name = "LPX_TOP_SWITCH_CTRL",  .addr = A_VERSAL2_LPX_TOP_SWITCH_CTRL,
        .reset = 0xe000300,
        .rsvd = 0xf1fc00f8,
    },{ .name = "LPX_LSBUS_CLK_CTRL",  .addr = A_VERSAL2_LPX_LSBUS_CLK_CTRL,
        .reset = 0x2000800,
        .rsvd = 0xfdfc00f8,
    },{ .name = "RPU_CLK_CTRL",  .addr = A_VERSAL2_RPU_CLK_CTRL,
        .reset = 0x3f00300,
        .rsvd = 0xfc0c00f8,
    },{ .name = "OCM_CLK_CTRL",  .addr = A_VERSAL2_OCM_CLK_CTRL,
        .reset = 0x1e00000,
        .rsvd = 0xfe1fffff,
    },{ .name = "IOU_SWITCH_CLK_CTRL",  .addr = A_VERSAL2_IOU_SWITCH_CLK_CTRL,
        .reset = 0x2000500,
        .rsvd = 0xfdfc00f8,
    },{ .name = "GEM0_REF_CTRL",  .addr = A_VERSAL2_GEM0_REF_CTRL,
        .reset = 0xe000a00,
        .rsvd = 0xf1fc00f8,
    },{ .name = "GEM1_REF_CTRL",  .addr = A_VERSAL2_GEM1_REF_CTRL,
        .reset = 0xe000a00,
        .rsvd = 0xf1fc00f8,
    },{ .name = "GEM_TSU_REF_CLK_CTRL",  .addr = A_VERSAL2_GEM_TSU_REF_CLK_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "USB0_BUS_REF_CLK_CTRL",
        .addr = A_VERSAL2_USB0_BUS_REF_CLK_CTRL,
        .reset = 0x2001900,
        .rsvd = 0xfdfc00f8,
    },{ .name = "USB1_BUS_REF_CLK_CTRL",
        .addr = A_VERSAL2_USB1_BUS_REF_CLK_CTRL,
        .reset = 0x2001900,
        .rsvd = 0xfdfc00f8,
    },{ .name = "UART0_REF_CLK_CTRL",  .addr = A_VERSAL2_UART0_REF_CLK_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "UART1_REF_CLK_CTRL",  .addr = A_VERSAL2_UART1_REF_CLK_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "SPI0_REF_CLK_CTRL",  .addr = A_VERSAL2_SPI0_REF_CLK_CTRL,
        .reset = 0x600,
        .rsvd = 0xfdfc00f8,
    },{ .name = "SPI1_REF_CLK_CTRL",  .addr = A_VERSAL2_SPI1_REF_CLK_CTRL,
        .reset = 0x600,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CAN0_REF_2X_CTRL",  .addr = A_VERSAL2_CAN0_REF_2X_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CAN1_REF_2X_CTRL",  .addr = A_VERSAL2_CAN1_REF_2X_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CAN2_REF_2X_CTRL",  .addr = A_VERSAL2_CAN2_REF_2X_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "CAN3_REF_2X_CTRL",  .addr = A_VERSAL2_CAN3_REF_2X_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C0_REF_CTRL",  .addr = A_VERSAL2_I3C0_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C1_REF_CTRL",  .addr = A_VERSAL2_I3C1_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C2_REF_CTRL",  .addr = A_VERSAL2_I3C2_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C3_REF_CTRL",  .addr = A_VERSAL2_I3C3_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C4_REF_CTRL",  .addr = A_VERSAL2_I3C4_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C5_REF_CTRL",  .addr = A_VERSAL2_I3C5_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C6_REF_CTRL",  .addr = A_VERSAL2_I3C6_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "I3C7_REF_CTRL",  .addr = A_VERSAL2_I3C7_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "DBG_LPX_CTRL",  .addr = A_VERSAL2_DBG_LPX_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "TIMESTAMP_REF_CTRL",  .addr = A_VERSAL2_TIMESTAMP_REF_CTRL,
        .reset = 0x2000c00,
        .rsvd = 0xfdfc00f8,
    },{ .name = "SAFETY_CHK",  .addr = A_VERSAL2_SAFETY_CHK,
    },{ .name = "ASU_CLK_CTRL",  .addr = A_VERSAL2_ASU_CLK_CTRL,
        .reset = 0x2000f04,
        .rsvd = 0xfdfc00f8,
    },{ .name = "DBG_TSTMP_CLK_CTRL",  .addr = A_VERSAL2_DBG_TSTMP_CLK_CTRL,
        .reset = 0x300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "MMI_TOPSW_CLK_CTRL",  .addr = A_VERSAL2_MMI_TOPSW_CLK_CTRL,
        .reset = 0x2000300,
        .rsvd = 0xfdfc00f8,
    },{ .name = "WWDT_PLL_CLK_CTRL",  .addr = A_VERSAL2_WWDT_PLL_CLK_CTRL,
        .reset = 0xc00,
        .rsvd = 0xfffc00f8,
    },{ .name = "RCLK_CTRL",  .addr = A_VERSAL2_RCLK_CTRL,
        .rsvd = 0xc040,
    },{ .name = "RST_RPU_A",  .addr = A_VERSAL2_RST_RPU_A,
        .reset = 0x10303,
        .rsvd = 0xfffefcfc,
        .pre_write = crl_rst_cpu_prew,
    },{ .name = "RST_RPU_B",  .addr = A_VERSAL2_RST_RPU_B,
        .reset = 0x10303,
        .rsvd = 0xfffefcfc,
        .pre_write = crl_rst_cpu_prew,
    },{ .name = "RST_RPU_C",  .addr = A_VERSAL2_RST_RPU_C,
        .reset = 0x10303,
        .rsvd = 0xfffefcfc,
        .pre_write = crl_rst_cpu_prew,
    },{ .name = "RST_RPU_D",  .addr = A_VERSAL2_RST_RPU_D,
        .reset = 0x10303,
        .rsvd = 0xfffefcfc,
        .pre_write = crl_rst_cpu_prew,
    },{ .name = "RST_RPU_E",  .addr = A_VERSAL2_RST_RPU_E,
        .reset = 0x10303,
        .rsvd = 0xfffefcfc,
        .pre_write = crl_rst_cpu_prew,
    },{ .name = "RST_RPU_GD_0",  .addr = A_VERSAL2_RST_RPU_GD_0,
        .reset = 0x3,
    },{ .name = "RST_RPU_GD_1",  .addr = A_VERSAL2_RST_RPU_GD_1,
        .reset = 0x3,
    },{ .name = "RST_ASU_GD",  .addr = A_VERSAL2_RST_ASU_GD,
        .reset = 0x3,
    },{ .name = "RST_ADMA",  .addr = A_VERSAL2_RST_ADMA,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_SDMA",  .addr = A_VERSAL2_RST_SDMA,
        .pre_write = crl_rst_dev_prew,
        .reset = 0x1,
    },{ .name = "RST_GEM0",  .addr = A_VERSAL2_RST_GEM0,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_GEM1",  .addr = A_VERSAL2_RST_GEM1,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_USB0",  .addr = A_VERSAL2_RST_USB0,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_USB1",  .addr = A_VERSAL2_RST_USB1,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_UART0",  .addr = A_VERSAL2_RST_UART0,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_UART1",  .addr = A_VERSAL2_RST_UART1,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_SPI0",  .addr = A_VERSAL2_RST_SPI0,
        .reset = 0x1,
    },{ .name = "RST_SPI1",  .addr = A_VERSAL2_RST_SPI1,
        .reset = 0x1,
    },{ .name = "RST_CAN0",  .addr = A_VERSAL2_RST_CAN0,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_CAN1",  .addr = A_VERSAL2_RST_CAN1,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_CAN2",  .addr = A_VERSAL2_RST_CAN2,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_CAN3",  .addr = A_VERSAL2_RST_CAN3,
        .reset = 0x1,
        .pre_write = crl_rst_dev_prew,
    },{ .name = "RST_I3C0",  .addr = A_VERSAL2_RST_I3C0,
        .reset = 0x1,
    },{ .name = "RST_I3C1",  .addr = A_VERSAL2_RST_I3C1,
        .reset = 0x1,
    },{ .name = "RST_I3C2",  .addr = A_VERSAL2_RST_I3C2,
        .reset = 0x1,
    },{ .name = "RST_I3C3",  .addr = A_VERSAL2_RST_I3C3,
        .reset = 0x1,
    },{ .name = "RST_I3C4",  .addr = A_VERSAL2_RST_I3C4,
        .reset = 0x1,
    },{ .name = "RST_I3C5",  .addr = A_VERSAL2_RST_I3C5,
        .reset = 0x1,
    },{ .name = "RST_I3C6",  .addr = A_VERSAL2_RST_I3C6,
        .reset = 0x1,
    },{ .name = "RST_I3C7",  .addr = A_VERSAL2_RST_I3C7,
        .reset = 0x1,
    },{ .name = "RST_DBG_LPX",  .addr = A_VERSAL2_RST_DBG_LPX,
        .reset = 0x3,
        .rsvd = 0xfc,
    },{ .name = "RST_GPIO",  .addr = A_VERSAL2_RST_GPIO,
        .reset = 0x1,
    },{ .name = "RST_TTC",  .addr = A_VERSAL2_RST_TTC,
        .reset = 0xff,
    },{ .name = "RST_TIMESTAMP",  .addr = A_VERSAL2_RST_TIMESTAMP,
        .reset = 0x1,
    },{ .name = "RST_SWDT0",  .addr = A_VERSAL2_RST_SWDT0,
        .reset = 0x1,
    },{ .name = "RST_SWDT1",  .addr = A_VERSAL2_RST_SWDT1,
        .reset = 0x1,
    },{ .name = "RST_SWDT2",  .addr = A_VERSAL2_RST_SWDT2,
        .reset = 0x1,
    },{ .name = "RST_SWDT3",  .addr = A_VERSAL2_RST_SWDT3,
        .reset = 0x1,
    },{ .name = "RST_SWDT4",  .addr = A_VERSAL2_RST_SWDT4,
        .reset = 0x1,
    },{ .name = "RST_IPI",  .addr = A_VERSAL2_RST_IPI,
    },{ .name = "RST_SYSMON",  .addr = A_VERSAL2_RST_SYSMON,
    },{ .name = "ASU_MB_RST_MODE",  .addr = A_VERSAL2_ASU_MB_RST_MODE,
        .reset = 0x1,
        .rsvd = 0xf8,
    },{ .name = "FPX_TOPSW_MUX_CTRL",  .addr = A_VERSAL2_FPX_TOPSW_MUX_CTRL,
        .reset = 0x1,
    },{ .name = "RST_FPX",  .addr = A_VERSAL2_RST_FPX,
        .reset = 0x3,
    },{ .name = "RST_MMI",  .addr = A_VERSAL2_RST_MMI,
        .reset = 0x1,
    },{ .name = "RST_OCM",  .addr = A_VERSAL2_RST_OCM,
    }
};

static void versal_crl_reset_enter(Object *obj, ResetType type)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void versal2_crl_reset_enter(Object *obj, ResetType type)
{
    XlnxVersal2CRL *s = XLNX_VERSAL2_CRL(obj);
    size_t i;

    for (i = 0; i < VERSAL2_CRL_R_MAX; ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void versal_crl_reset_hold(Object *obj, ResetType type)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(obj);

    crl_update_irq(s);
}

static const MemoryRegionOps crl_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void versal_crl_init(Object *obj)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(obj);
    XlnxVersalCRLBase *xvcb = XLNX_VERSAL_CRL_BASE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    int i;

    reg_array = register_init_block32(DEVICE(obj), crl_regs_info,
                                      ARRAY_SIZE(crl_regs_info),
                                      s->regs_info, s->regs,
                                      &crl_ops,
                                      XLNX_VERSAL_CRL_ERR_DEBUG,
                                      CRL_R_MAX * 4);
    xvcb->regs = s->regs;
    sysbus_init_mmio(sbd, &reg_array->mem);
    sysbus_init_irq(sbd, &s->irq);

    for (i = 0; i < ARRAY_SIZE(s->cfg.rpu); ++i) {
        object_property_add_link(obj, "rpu[*]", TYPE_ARM_CPU,
                                 (Object **)&s->cfg.rpu[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.adma); ++i) {
        object_property_add_link(obj, "adma[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.adma[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.uart); ++i) {
        object_property_add_link(obj, "uart[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.uart[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.gem); ++i) {
        object_property_add_link(obj, "gem[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.gem[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.usb); ++i) {
        object_property_add_link(obj, "usb[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.usb[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }
}

static void versal2_crl_init(Object *obj)
{
    XlnxVersal2CRL *s = XLNX_VERSAL2_CRL(obj);
    XlnxVersalCRLBase *xvcb = XLNX_VERSAL_CRL_BASE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;
    size_t i;

    reg_array = register_init_block32(DEVICE(obj), versal2_crl_regs_info,
                                      ARRAY_SIZE(versal2_crl_regs_info),
                                      s->regs_info, s->regs,
                                      &crl_ops,
                                      XLNX_VERSAL_CRL_ERR_DEBUG,
                                      VERSAL2_CRL_R_MAX * 4);
    xvcb->regs = s->regs;

    sysbus_init_mmio(sbd, &reg_array->mem);

    for (i = 0; i < ARRAY_SIZE(s->cfg.rpu); ++i) {
        object_property_add_link(obj, "rpu[*]", TYPE_ARM_CPU,
                                 (Object **)&s->cfg.rpu[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.adma); ++i) {
        object_property_add_link(obj, "adma[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.adma[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.sdma); ++i) {
        object_property_add_link(obj, "sdma[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.sdma[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.uart); ++i) {
        object_property_add_link(obj, "uart[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.uart[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.gem); ++i) {
        object_property_add_link(obj, "gem[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.gem[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.usb); ++i) {
        object_property_add_link(obj, "usb[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.usb[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    for (i = 0; i < ARRAY_SIZE(s->cfg.can); ++i) {
        object_property_add_link(obj, "can[*]", TYPE_DEVICE,
                                 (Object **)&s->cfg.can[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }
}

static const VMStateDescription vmstate_versal_crl = {
    .name = TYPE_XLNX_VERSAL_CRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxVersalCRL, CRL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static const VMStateDescription vmstate_versal2_crl = {
    .name = TYPE_XLNX_VERSAL2_CRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxVersal2CRL, VERSAL2_CRL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void versal_crl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XlnxVersalCRLBaseClass *xvcc = XLNX_VERSAL_CRL_BASE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_versal_crl;
    rc->phases.enter = versal_crl_reset_enter;
    rc->phases.hold = versal_crl_reset_hold;
    xvcc->decode_periph_rst = versal_decode_periph_rst;
}

static void versal2_crl_class_init(ObjectClass *klass, const void *data)
{
    XlnxVersalCRLBaseClass *xvcc = XLNX_VERSAL_CRL_BASE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_versal2_crl;
    rc->phases.enter = versal2_crl_reset_enter;
    xvcc->decode_periph_rst = versal2_decode_periph_rst;
}

static const TypeInfo crl_base_info = {
    .name          = TYPE_XLNX_VERSAL_CRL_BASE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalCRLBase),
    .class_size    = sizeof(XlnxVersalCRLBaseClass),
    .abstract      = true,
};

static const TypeInfo versal_crl_info = {
    .name          = TYPE_XLNX_VERSAL_CRL,
    .parent        = TYPE_XLNX_VERSAL_CRL_BASE,
    .instance_size = sizeof(XlnxVersalCRL),
    .instance_init = versal_crl_init,
    .class_init    = versal_crl_class_init,
};

static const TypeInfo versal2_crl_info = {
    .name          = TYPE_XLNX_VERSAL2_CRL,
    .parent        = TYPE_XLNX_VERSAL_CRL_BASE,
    .instance_size = sizeof(XlnxVersal2CRL),
    .instance_init = versal2_crl_init,
    .class_init    = versal2_crl_class_init,
};

static void crl_register_types(void)
{
    type_register_static(&crl_base_info);
    type_register_static(&versal_crl_info);
    type_register_static(&versal2_crl_info);
}

type_init(crl_register_types)
