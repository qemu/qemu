/*
 * QEMU model of the Xilinx XRAM Controller.
 *
 * Copyright (c) 2021 Xilinx Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/register.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "hw/misc/xlnx-versal-xramc.h"

#ifndef XLNX_XRAM_CTRL_ERR_DEBUG
#define XLNX_XRAM_CTRL_ERR_DEBUG 0
#endif

static void xram_update_irq(XlnxXramCtrl *s)
{
    bool pending = s->regs[R_XRAM_ISR] & ~s->regs[R_XRAM_IMR];
    qemu_set_irq(s->irq, pending);
}

static void xram_isr_postw(RegisterInfo *reg, uint64_t val64)
{
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(reg->opaque);
    xram_update_irq(s);
}

static uint64_t xram_ien_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_XRAM_IMR] &= ~val;
    xram_update_irq(s);
    return 0;
}

static uint64_t xram_ids_prew(RegisterInfo *reg, uint64_t val64)
{
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(reg->opaque);
    uint32_t val = val64;

    s->regs[R_XRAM_IMR] |= val;
    xram_update_irq(s);
    return 0;
}

static const RegisterAccessInfo xram_ctrl_regs_info[] = {
    {   .name = "XRAM_ERR_CTRL",  .addr = A_XRAM_ERR_CTRL,
        .reset = 0xf,
        .rsvd = 0xfffffff0,
    },{ .name = "XRAM_ISR",  .addr = A_XRAM_ISR,
        .rsvd = 0xfffff800,
        .w1c = 0x7ff,
        .post_write = xram_isr_postw,
    },{ .name = "XRAM_IMR",  .addr = A_XRAM_IMR,
        .reset = 0x7ff,
        .rsvd = 0xfffff800,
        .ro = 0x7ff,
    },{ .name = "XRAM_IEN",  .addr = A_XRAM_IEN,
        .rsvd = 0xfffff800,
        .pre_write = xram_ien_prew,
    },{ .name = "XRAM_IDS",  .addr = A_XRAM_IDS,
        .rsvd = 0xfffff800,
        .pre_write = xram_ids_prew,
    },{ .name = "XRAM_ECC_CNTL",  .addr = A_XRAM_ECC_CNTL,
        .rsvd = 0xfffffff8,
    },{ .name = "XRAM_CLR_EXE",  .addr = A_XRAM_CLR_EXE,
        .rsvd = 0xffffff00,
    },{ .name = "XRAM_CE_FFA",  .addr = A_XRAM_CE_FFA,
        .rsvd = 0xfff00000,
        .ro = 0xfffff,
    },{ .name = "XRAM_CE_FFD0",  .addr = A_XRAM_CE_FFD0,
        .ro = 0xffffffff,
    },{ .name = "XRAM_CE_FFD1",  .addr = A_XRAM_CE_FFD1,
        .ro = 0xffffffff,
    },{ .name = "XRAM_CE_FFD2",  .addr = A_XRAM_CE_FFD2,
        .ro = 0xffffffff,
    },{ .name = "XRAM_CE_FFD3",  .addr = A_XRAM_CE_FFD3,
        .ro = 0xffffffff,
    },{ .name = "XRAM_CE_FFE",  .addr = A_XRAM_CE_FFE,
        .rsvd = 0xffff0000,
        .ro = 0xffff,
    },{ .name = "XRAM_UE_FFA",  .addr = A_XRAM_UE_FFA,
        .rsvd = 0xfff00000,
        .ro = 0xfffff,
    },{ .name = "XRAM_UE_FFD0",  .addr = A_XRAM_UE_FFD0,
        .ro = 0xffffffff,
    },{ .name = "XRAM_UE_FFD1",  .addr = A_XRAM_UE_FFD1,
        .ro = 0xffffffff,
    },{ .name = "XRAM_UE_FFD2",  .addr = A_XRAM_UE_FFD2,
        .ro = 0xffffffff,
    },{ .name = "XRAM_UE_FFD3",  .addr = A_XRAM_UE_FFD3,
        .ro = 0xffffffff,
    },{ .name = "XRAM_UE_FFE",  .addr = A_XRAM_UE_FFE,
        .rsvd = 0xffff0000,
        .ro = 0xffff,
    },{ .name = "XRAM_FI_D0",  .addr = A_XRAM_FI_D0,
    },{ .name = "XRAM_FI_D1",  .addr = A_XRAM_FI_D1,
    },{ .name = "XRAM_FI_D2",  .addr = A_XRAM_FI_D2,
    },{ .name = "XRAM_FI_D3",  .addr = A_XRAM_FI_D3,
    },{ .name = "XRAM_FI_SY",  .addr = A_XRAM_FI_SY,
        .rsvd = 0xffff0000,
    },{ .name = "XRAM_RMW_UE_FFA",  .addr = A_XRAM_RMW_UE_FFA,
        .rsvd = 0xfff00000,
        .ro = 0xfffff,
    },{ .name = "XRAM_FI_CNTR",  .addr = A_XRAM_FI_CNTR,
        .rsvd = 0xff000000,
    },{ .name = "XRAM_IMP",  .addr = A_XRAM_IMP,
        .reset = 0x4,
        .rsvd = 0xfffffff0,
        .ro = 0xf,
    },{ .name = "XRAM_PRDY_DBG",  .addr = A_XRAM_PRDY_DBG,
        .reset = 0xffff,
        .rsvd = 0xffff0000,
        .ro = 0xffff,
    },{ .name = "XRAM_SAFETY_CHK",  .addr = A_XRAM_SAFETY_CHK,
    }
};

static void xram_ctrl_reset_enter(Object *obj, ResetType type)
{
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }

    ARRAY_FIELD_DP32(s->regs, XRAM_IMP, SIZE, s->cfg.encoded_size);
}

static void xram_ctrl_reset_hold(Object *obj)
{
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(obj);

    xram_update_irq(s);
}

static const MemoryRegionOps xram_ctrl_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void xram_ctrl_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(dev);

    switch (s->cfg.size) {
    case 64 * KiB:
        s->cfg.encoded_size = 0;
        break;
    case 128 * KiB:
        s->cfg.encoded_size = 1;
        break;
    case 256 * KiB:
        s->cfg.encoded_size = 2;
        break;
    case 512 * KiB:
        s->cfg.encoded_size = 3;
        break;
    case 1 * MiB:
        s->cfg.encoded_size = 4;
        break;
    default:
        error_setg(errp, "Unsupported XRAM size %" PRId64, s->cfg.size);
        return;
    }

    memory_region_init_ram(&s->ram, OBJECT(s),
                           object_get_canonical_path_component(OBJECT(s)),
                           s->cfg.size, &error_fatal);
    sysbus_init_mmio(sbd, &s->ram);
}

static void xram_ctrl_init(Object *obj)
{
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->reg_array =
        register_init_block32(DEVICE(obj), xram_ctrl_regs_info,
                              ARRAY_SIZE(xram_ctrl_regs_info),
                              s->regs_info, s->regs,
                              &xram_ctrl_ops,
                              XLNX_XRAM_CTRL_ERR_DEBUG,
                              XRAM_CTRL_R_MAX * 4);
    sysbus_init_mmio(sbd, &s->reg_array->mem);
    sysbus_init_irq(sbd, &s->irq);
}

static void xram_ctrl_finalize(Object *obj)
{
    XlnxXramCtrl *s = XLNX_XRAM_CTRL(obj);
    register_finalize_block(s->reg_array);
}

static const VMStateDescription vmstate_xram_ctrl = {
    .name = TYPE_XLNX_XRAM_CTRL,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, XlnxXramCtrl, XRAM_CTRL_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static Property xram_ctrl_properties[] = {
    DEFINE_PROP_UINT64("size", XlnxXramCtrl, cfg.size, 1 * MiB),
    DEFINE_PROP_END_OF_LIST(),
};

static void xram_ctrl_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xram_ctrl_realize;
    dc->vmsd = &vmstate_xram_ctrl;
    device_class_set_props(dc, xram_ctrl_properties);

    rc->phases.enter = xram_ctrl_reset_enter;
    rc->phases.hold = xram_ctrl_reset_hold;
}

static const TypeInfo xram_ctrl_info = {
    .name              = TYPE_XLNX_XRAM_CTRL,
    .parent            = TYPE_SYS_BUS_DEVICE,
    .instance_size     = sizeof(XlnxXramCtrl),
    .class_init        = xram_ctrl_class_init,
    .instance_init     = xram_ctrl_init,
    .instance_finalize = xram_ctrl_finalize,
};

static void xram_ctrl_register_types(void)
{
    type_register_static(&xram_ctrl_info);
}

type_init(xram_ctrl_register_types)
