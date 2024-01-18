/*
 * QEMU model of the Clock-Reset-LPD (CRL).
 *
 * Copyright (c) 2022 Advanced Micro Devices, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
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

static void crl_reset_dev(XlnxVersalCRL *s, DeviceState *dev,
                          bool rst_old, bool rst_new)
{
    device_cold_reset(dev);
}

static void crl_reset_cpu(XlnxVersalCRL *s, ARMCPU *armcpu,
                          bool rst_old, bool rst_new)
{
    if (rst_new) {
        arm_set_cpu_off(arm_cpu_mp_affinity(armcpu));
    } else {
        arm_set_cpu_on_and_reset(arm_cpu_mp_affinity(armcpu));
    }
}

#define REGFIELD_RESET(type, s, reg, f, new_val, dev) {     \
    bool old_f = ARRAY_FIELD_EX32((s)->regs, reg, f);       \
    bool new_f = FIELD_EX32(new_val, reg, f);               \
                                                            \
    /* Detect edges.  */                                    \
    if (dev && old_f != new_f) {                            \
        crl_reset_ ## type(s, dev, old_f, new_f);           \
    }                                                       \
}

static uint64_t crl_rst_r5_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);

    REGFIELD_RESET(cpu, s, RST_CPU_R5, RESET_CPU0, val64, s->cfg.cpu_r5[0]);
    REGFIELD_RESET(cpu, s, RST_CPU_R5, RESET_CPU1, val64, s->cfg.cpu_r5[1]);
    return val64;
}

static uint64_t crl_rst_adma_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);
    int i;

    /* A single register fans out to all ADMA reset inputs.  */
    for (i = 0; i < ARRAY_SIZE(s->cfg.adma); i++) {
        REGFIELD_RESET(dev, s, RST_ADMA, RESET, val64, s->cfg.adma[i]);
    }
    return val64;
}

static uint64_t crl_rst_uart0_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);

    REGFIELD_RESET(dev, s, RST_UART0, RESET, val64, s->cfg.uart[0]);
    return val64;
}

static uint64_t crl_rst_uart1_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);

    REGFIELD_RESET(dev, s, RST_UART1, RESET, val64, s->cfg.uart[1]);
    return val64;
}

static uint64_t crl_rst_gem0_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);

    REGFIELD_RESET(dev, s, RST_GEM0, RESET, val64, s->cfg.gem[0]);
    return val64;
}

static uint64_t crl_rst_gem1_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);

    REGFIELD_RESET(dev, s, RST_GEM1, RESET, val64, s->cfg.gem[1]);
    return val64;
}

static uint64_t crl_rst_usb_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(reg->opaque);

    REGFIELD_RESET(dev, s, RST_USB0, RESET, val64, s->cfg.usb);
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
        .pre_write = crl_rst_r5_prew,
    },{ .name = "RST_ADMA",  .addr = A_RST_ADMA,
        .reset = 0x1,
        .pre_write = crl_rst_adma_prew,
    },{ .name = "RST_GEM0",  .addr = A_RST_GEM0,
        .reset = 0x1,
        .pre_write = crl_rst_gem0_prew,
    },{ .name = "RST_GEM1",  .addr = A_RST_GEM1,
        .reset = 0x1,
        .pre_write = crl_rst_gem1_prew,
    },{ .name = "RST_SPARE",  .addr = A_RST_SPARE,
        .reset = 0x1,
    },{ .name = "RST_USB0",  .addr = A_RST_USB0,
        .reset = 0x1,
        .pre_write = crl_rst_usb_prew,
    },{ .name = "RST_UART0",  .addr = A_RST_UART0,
        .reset = 0x1,
        .pre_write = crl_rst_uart0_prew,
    },{ .name = "RST_UART1",  .addr = A_RST_UART1,
        .reset = 0x1,
        .pre_write = crl_rst_uart1_prew,
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

static void crl_reset_enter(Object *obj, ResetType type)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void crl_reset_hold(Object *obj)
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

static void crl_init(Object *obj)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    s->reg_array =
        register_init_block32(DEVICE(obj), crl_regs_info,
                              ARRAY_SIZE(crl_regs_info),
                              s->regs_info, s->regs,
                              &crl_ops,
                              XLNX_VERSAL_CRL_ERR_DEBUG,
                              CRL_R_MAX * 4);
    sysbus_init_mmio(sbd, &s->reg_array->mem);
    sysbus_init_irq(sbd, &s->irq);

    for (i = 0; i < ARRAY_SIZE(s->cfg.cpu_r5); ++i) {
        object_property_add_link(obj, "cpu_r5[*]", TYPE_ARM_CPU,
                                 (Object **)&s->cfg.cpu_r5[i],
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

    object_property_add_link(obj, "usb", TYPE_DEVICE,
                             (Object **)&s->cfg.gem[i],
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
}

static void crl_finalize(Object *obj)
{
    XlnxVersalCRL *s = XLNX_VERSAL_CRL(obj);
    register_finalize_block(s->reg_array);
}

static const VMStateDescription vmstate_crl = {
    .name = TYPE_XLNX_VERSAL_CRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxVersalCRL, CRL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void crl_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_crl;

    rc->phases.enter = crl_reset_enter;
    rc->phases.hold = crl_reset_hold;
}

static const TypeInfo crl_info = {
    .name          = TYPE_XLNX_VERSAL_CRL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XlnxVersalCRL),
    .class_init    = crl_class_init,
    .instance_init = crl_init,
    .instance_finalize = crl_finalize,
};

static void crl_register_types(void)
{
    type_register_static(&crl_info);
}

type_init(crl_register_types)
