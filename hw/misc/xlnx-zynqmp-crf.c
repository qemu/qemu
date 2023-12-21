/*
 * QEMU model of the CRF - Clock Reset FPD.
 *
 * Copyright (c) 2022 Xilinx Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/misc/xlnx-zynqmp-crf.h"
#include "target/arm/arm-powerctl.h"

#ifndef XLNX_ZYNQMP_CRF_ERR_DEBUG
#define XLNX_ZYNQMP_CRF_ERR_DEBUG 0
#endif

#define CRF_MAX_CPU    4

static void ir_update_irq(XlnxZynqMPCRF *s)
{
    bool pending = s->regs[R_IR_STATUS] & ~s->regs[R_IR_MASK];
    qemu_set_irq(s->irq_ir, pending);
}

static void ir_status_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(reg->opaque);
    ir_update_irq(s);
}

static uint64_t ir_enable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IR_MASK] &= ~val;
    ir_update_irq(s);
    return 0;
}

static uint64_t ir_disable_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IR_MASK] |= val;
    ir_update_irq(s);
    return 0;
}

static uint64_t rst_fpd_apu_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(reg->opaque);
    uint32_t val = val64;
    uint32_t val_old = s->regs[R_RST_FPD_APU];
    unsigned int i;

    for (i = 0; i < CRF_MAX_CPU; i++) {
        uint32_t mask = (1 << (R_RST_FPD_APU_ACPU0_RESET_SHIFT + i));

        if ((val ^ val_old) & mask) {
            if (val & mask) {
                arm_set_cpu_off(i);
            } else {
                arm_set_cpu_on_and_reset(i);
            }
        }
    }
    return val64;
}

static const RegisterAccessInfo crf_regs_info[] = {
    {   .name = "ERR_CTRL",  .addr = A_ERR_CTRL,
    },{ .name = "IR_STATUS",  .addr = A_IR_STATUS,
        .w1c = 0x1,
        .post_write = ir_status_postw,
    },{ .name = "IR_MASK",  .addr = A_IR_MASK,
        .reset = 0x1,
        .ro = 0x1,
    },{ .name = "IR_ENABLE",  .addr = A_IR_ENABLE,
        .pre_write = ir_enable_prew,
    },{ .name = "IR_DISABLE",  .addr = A_IR_DISABLE,
        .pre_write = ir_disable_prew,
    },{ .name = "CRF_WPROT",  .addr = A_CRF_WPROT,
    },{ .name = "APLL_CTRL",  .addr = A_APLL_CTRL,
        .reset = 0x12c09,
        .rsvd = 0xf88c80f6,
    },{ .name = "APLL_CFG",  .addr = A_APLL_CFG,
        .rsvd = 0x1801210,
    },{ .name = "APLL_FRAC_CFG",  .addr = A_APLL_FRAC_CFG,
        .rsvd = 0x7e330000,
    },{ .name = "DPLL_CTRL",  .addr = A_DPLL_CTRL,
        .reset = 0x2c09,
        .rsvd = 0xf88c80f6,
    },{ .name = "DPLL_CFG",  .addr = A_DPLL_CFG,
        .rsvd = 0x1801210,
    },{ .name = "DPLL_FRAC_CFG",  .addr = A_DPLL_FRAC_CFG,
        .rsvd = 0x7e330000,
    },{ .name = "VPLL_CTRL",  .addr = A_VPLL_CTRL,
        .reset = 0x12809,
        .rsvd = 0xf88c80f6,
    },{ .name = "VPLL_CFG",  .addr = A_VPLL_CFG,
        .rsvd = 0x1801210,
    },{ .name = "VPLL_FRAC_CFG",  .addr = A_VPLL_FRAC_CFG,
        .rsvd = 0x7e330000,
    },{ .name = "PLL_STATUS",  .addr = A_PLL_STATUS,
        .reset = 0x3f,
        .rsvd = 0xc0,
        .ro = 0x3f,
    },{ .name = "APLL_TO_LPD_CTRL",  .addr = A_APLL_TO_LPD_CTRL,
        .reset = 0x400,
        .rsvd = 0xc0ff,
    },{ .name = "DPLL_TO_LPD_CTRL",  .addr = A_DPLL_TO_LPD_CTRL,
        .reset = 0x400,
        .rsvd = 0xc0ff,
    },{ .name = "VPLL_TO_LPD_CTRL",  .addr = A_VPLL_TO_LPD_CTRL,
        .reset = 0x400,
        .rsvd = 0xc0ff,
    },{ .name = "ACPU_CTRL",  .addr = A_ACPU_CTRL,
        .reset = 0x3000400,
        .rsvd = 0xfcffc0f8,
    },{ .name = "DBG_TRACE_CTRL",  .addr = A_DBG_TRACE_CTRL,
        .reset = 0x2500,
        .rsvd = 0xfeffc0f8,
    },{ .name = "DBG_FPD_CTRL",  .addr = A_DBG_FPD_CTRL,
        .reset = 0x1002500,
        .rsvd = 0xfeffc0f8,
    },{ .name = "DP_VIDEO_REF_CTRL",  .addr = A_DP_VIDEO_REF_CTRL,
        .reset = 0x1002300,
        .rsvd = 0xfec0c0f8,
    },{ .name = "DP_AUDIO_REF_CTRL",  .addr = A_DP_AUDIO_REF_CTRL,
        .reset = 0x1032300,
        .rsvd = 0xfec0c0f8,
    },{ .name = "DP_STC_REF_CTRL",  .addr = A_DP_STC_REF_CTRL,
        .reset = 0x1203200,
        .rsvd = 0xfec0c0f8,
    },{ .name = "DDR_CTRL",  .addr = A_DDR_CTRL,
        .reset = 0x1000500,
        .rsvd = 0xfeffc0f8,
    },{ .name = "GPU_REF_CTRL",  .addr = A_GPU_REF_CTRL,
        .reset = 0x1500,
        .rsvd = 0xf8ffc0f8,
    },{ .name = "SATA_REF_CTRL",  .addr = A_SATA_REF_CTRL,
        .reset = 0x1001600,
        .rsvd = 0xfeffc0f8,
    },{ .name = "PCIE_REF_CTRL",  .addr = A_PCIE_REF_CTRL,
        .reset = 0x1500,
        .rsvd = 0xfeffc0f8,
    },{ .name = "GDMA_REF_CTRL",  .addr = A_GDMA_REF_CTRL,
        .reset = 0x1000500,
        .rsvd = 0xfeffc0f8,
    },{ .name = "DPDMA_REF_CTRL",  .addr = A_DPDMA_REF_CTRL,
        .reset = 0x1000500,
        .rsvd = 0xfeffc0f8,
    },{ .name = "TOPSW_MAIN_CTRL",  .addr = A_TOPSW_MAIN_CTRL,
        .reset = 0x1000400,
        .rsvd = 0xfeffc0f8,
    },{ .name = "TOPSW_LSBUS_CTRL",  .addr = A_TOPSW_LSBUS_CTRL,
        .reset = 0x1000800,
        .rsvd = 0xfeffc0f8,
    },{ .name = "DBG_TSTMP_CTRL",  .addr = A_DBG_TSTMP_CTRL,
        .reset = 0xa00,
        .rsvd = 0xffffc0f8,
    },
    {   .name = "RST_FPD_TOP",  .addr = A_RST_FPD_TOP,
        .reset = 0xf9ffe,
        .rsvd = 0xf06001,
    },{ .name = "RST_FPD_APU",  .addr = A_RST_FPD_APU,
        .reset = 0x3d0f,
        .rsvd = 0xc2f0,
        .pre_write = rst_fpd_apu_prew,
    },{ .name = "RST_DDR_SS",  .addr = A_RST_DDR_SS,
        .reset = 0xf,
        .rsvd = 0xf3,
    }
};

static void crf_reset_enter(Object *obj, ResetType type)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void crf_reset_hold(Object *obj)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(obj);
    ir_update_irq(s);
}

static const MemoryRegionOps crf_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void crf_init(Object *obj)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->reg_array =
        register_init_block32(DEVICE(obj), crf_regs_info,
                              ARRAY_SIZE(crf_regs_info),
                              s->regs_info, s->regs,
                              &crf_ops,
                              XLNX_ZYNQMP_CRF_ERR_DEBUG,
                              CRF_R_MAX * 4);
    sysbus_init_mmio(sbd, &s->reg_array->mem);
    sysbus_init_irq(sbd, &s->irq_ir);
}

static void crf_finalize(Object *obj)
{
    XlnxZynqMPCRF *s = XLNX_ZYNQMP_CRF(obj);
    register_finalize_block(s->reg_array);
}

static const VMStateDescription vmstate_crf = {
    .name = TYPE_XLNX_ZYNQMP_CRF,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxZynqMPCRF, CRF_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void crf_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_crf;
    rc->phases.enter = crf_reset_enter;
    rc->phases.hold = crf_reset_hold;
}

static const TypeInfo crf_info = {
    .name              = TYPE_XLNX_ZYNQMP_CRF,
    .parent            = TYPE_SYS_BUS_DEVICE,
    .instance_size     = sizeof(XlnxZynqMPCRF),
    .class_init        = crf_class_init,
    .instance_init     = crf_init,
    .instance_finalize = crf_finalize,
};

static void crf_register_types(void)
{
    type_register_static(&crf_info);
}

type_init(crf_register_types)
