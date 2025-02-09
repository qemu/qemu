/*
 * QEMU model of the VersalUsb2CtrlRegs Register control/Status block for
 * USB2.0 controller
 *
 * This module should control phy_reset, permanent device plugs, frame length
 * time adjust & setting of coherency paths. None of which are emulated in
 * present model.
 *
 * Copyright (c) 2020 Xilinx Inc. Vikram Garhwal <fnu.vikram@xilinx.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "hw/usb/xlnx-versal-usb2-ctrl-regs.h"

#ifndef XILINX_VERSAL_USB2_CTRL_REGS_ERR_DEBUG
#define XILINX_VERSAL_USB2_CTRL_REGS_ERR_DEBUG 0
#endif

REG32(BUS_FILTER, 0x30)
    FIELD(BUS_FILTER, BYPASS, 0, 4)
REG32(PORT, 0x34)
    FIELD(PORT, HOST_SMI_BAR_WR, 4, 1)
    FIELD(PORT, HOST_SMI_PCI_CMD_REG_WR, 3, 1)
    FIELD(PORT, HOST_MSI_ENABLE, 2, 1)
    FIELD(PORT, PWR_CTRL_PRSNT, 1, 1)
    FIELD(PORT, HUB_PERM_ATTACH, 0, 1)
REG32(JITTER_ADJUST, 0x38)
    FIELD(JITTER_ADJUST, FLADJ, 0, 6)
REG32(BIGENDIAN, 0x40)
    FIELD(BIGENDIAN, ENDIAN_GS, 0, 1)
REG32(COHERENCY, 0x44)
    FIELD(COHERENCY, USB_COHERENCY, 0, 1)
REG32(XHC_BME, 0x48)
    FIELD(XHC_BME, XHC_BME, 0, 1)
REG32(REG_CTRL, 0x60)
    FIELD(REG_CTRL, SLVERR_ENABLE, 0, 1)
REG32(IR_STATUS, 0x64)
    FIELD(IR_STATUS, HOST_SYS_ERR, 1, 1)
    FIELD(IR_STATUS, ADDR_DEC_ERR, 0, 1)
REG32(IR_MASK, 0x68)
    FIELD(IR_MASK, HOST_SYS_ERR, 1, 1)
    FIELD(IR_MASK, ADDR_DEC_ERR, 0, 1)
REG32(IR_ENABLE, 0x6c)
    FIELD(IR_ENABLE, HOST_SYS_ERR, 1, 1)
    FIELD(IR_ENABLE, ADDR_DEC_ERR, 0, 1)
REG32(IR_DISABLE, 0x70)
    FIELD(IR_DISABLE, HOST_SYS_ERR, 1, 1)
    FIELD(IR_DISABLE, ADDR_DEC_ERR, 0, 1)
REG32(USB3, 0x78)

static void ir_update_irq(VersalUsb2CtrlRegs *s)
{
    bool pending = s->regs[R_IR_STATUS] & ~s->regs[R_IR_MASK];
    qemu_set_irq(s->irq_ir, pending);
}

static void ir_status_postw(RegisterInfo *reg, uint64_t val64)
{
    VersalUsb2CtrlRegs *s = XILINX_VERSAL_USB2_CTRL_REGS(reg->opaque);
    /*
     * TODO: This should also clear USBSTS.HSE field in USB XHCI register.
     * May be combine both the modules.
     */
    ir_update_irq(s);
}

static uint64_t ir_enable_prew(RegisterInfo *reg, uint64_t val64)
{
    VersalUsb2CtrlRegs *s = XILINX_VERSAL_USB2_CTRL_REGS(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IR_MASK] &= ~val;
    ir_update_irq(s);
    return 0;
}

static uint64_t ir_disable_prew(RegisterInfo *reg, uint64_t val64)
{
    VersalUsb2CtrlRegs *s = XILINX_VERSAL_USB2_CTRL_REGS(reg->opaque);
    uint32_t val = val64;

    s->regs[R_IR_MASK] |= val;
    ir_update_irq(s);
    return 0;
}

static const RegisterAccessInfo usb2_ctrl_regs_regs_info[] = {
    {   .name = "BUS_FILTER",  .addr = A_BUS_FILTER,
        .rsvd = 0xfffffff0,
    },{ .name = "PORT",  .addr = A_PORT,
        .rsvd = 0xffffffe0,
    },{ .name = "JITTER_ADJUST",  .addr = A_JITTER_ADJUST,
        .reset = 0x20,
        .rsvd = 0xffffffc0,
    },{ .name = "BIGENDIAN",  .addr = A_BIGENDIAN,
        .rsvd = 0xfffffffe,
    },{ .name = "COHERENCY",  .addr = A_COHERENCY,
        .rsvd = 0xfffffffe,
    },{ .name = "XHC_BME",  .addr = A_XHC_BME,
        .reset = 0x1,
        .rsvd = 0xfffffffe,
    },{ .name = "REG_CTRL",  .addr = A_REG_CTRL,
        .rsvd = 0xfffffffe,
    },{ .name = "IR_STATUS",  .addr = A_IR_STATUS,
        .rsvd = 0xfffffffc,
        .w1c = 0x3,
        .post_write = ir_status_postw,
    },{ .name = "IR_MASK",  .addr = A_IR_MASK,
        .reset = 0x3,
        .rsvd = 0xfffffffc,
        .ro = 0x3,
    },{ .name = "IR_ENABLE",  .addr = A_IR_ENABLE,
        .rsvd = 0xfffffffc,
        .pre_write = ir_enable_prew,
    },{ .name = "IR_DISABLE",  .addr = A_IR_DISABLE,
        .rsvd = 0xfffffffc,
        .pre_write = ir_disable_prew,
    },{ .name = "USB3",  .addr = A_USB3,
    }
};

static void usb2_ctrl_regs_reset_init(Object *obj, ResetType type)
{
    VersalUsb2CtrlRegs *s = XILINX_VERSAL_USB2_CTRL_REGS(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->regs_info); ++i) {
        register_reset(&s->regs_info[i]);
    }
}

static void usb2_ctrl_regs_reset_hold(Object *obj, ResetType type)
{
    VersalUsb2CtrlRegs *s = XILINX_VERSAL_USB2_CTRL_REGS(obj);

    ir_update_irq(s);
}

static const MemoryRegionOps usb2_ctrl_regs_ops = {
    .read = register_read_memory,
    .write = register_write_memory,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void usb2_ctrl_regs_init(Object *obj)
{
    VersalUsb2CtrlRegs *s = XILINX_VERSAL_USB2_CTRL_REGS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RegisterInfoArray *reg_array;

    memory_region_init(&s->iomem, obj, TYPE_XILINX_VERSAL_USB2_CTRL_REGS,
                       USB2_REGS_R_MAX * 4);
    reg_array =
        register_init_block32(DEVICE(obj), usb2_ctrl_regs_regs_info,
                              ARRAY_SIZE(usb2_ctrl_regs_regs_info),
                              s->regs_info, s->regs,
                              &usb2_ctrl_regs_ops,
                              XILINX_VERSAL_USB2_CTRL_REGS_ERR_DEBUG,
                              USB2_REGS_R_MAX * 4);
    memory_region_add_subregion(&s->iomem,
                                0x0,
                                &reg_array->mem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_ir);
}

static const VMStateDescription vmstate_usb2_ctrl_regs = {
    .name = TYPE_XILINX_VERSAL_USB2_CTRL_REGS,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, VersalUsb2CtrlRegs, USB2_REGS_R_MAX),
        VMSTATE_END_OF_LIST(),
    }
};

static void usb2_ctrl_regs_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = usb2_ctrl_regs_reset_init;
    rc->phases.hold  = usb2_ctrl_regs_reset_hold;
    dc->vmsd = &vmstate_usb2_ctrl_regs;
}

static const TypeInfo usb2_ctrl_regs_info = {
    .name          = TYPE_XILINX_VERSAL_USB2_CTRL_REGS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VersalUsb2CtrlRegs),
    .class_init    = usb2_ctrl_regs_class_init,
    .instance_init = usb2_ctrl_regs_init,
};

static void usb2_ctrl_regs_register_types(void)
{
    type_register_static(&usb2_ctrl_regs_info);
}

type_init(usb2_ctrl_regs_register_types)
