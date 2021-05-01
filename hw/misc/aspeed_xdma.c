/*
 * ASPEED XDMA Controller
 * Eddie James <eajames@linux.ibm.com>
 *
 * Copyright (C) 2019 IBM Corp
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/misc/aspeed_xdma.h"
#include "migration/vmstate.h"
#include "qapi/error.h"

#include "trace.h"

#define XDMA_BMC_CMDQ_ADDR         0x10
#define XDMA_BMC_CMDQ_ENDP         0x14
#define XDMA_BMC_CMDQ_WRP          0x18
#define  XDMA_BMC_CMDQ_W_MASK      0x0003FFFF
#define XDMA_BMC_CMDQ_RDP          0x1C
#define  XDMA_BMC_CMDQ_RDP_MAGIC   0xEE882266
#define XDMA_IRQ_ENG_CTRL          0x20
#define  XDMA_IRQ_ENG_CTRL_US_COMP BIT(4)
#define  XDMA_IRQ_ENG_CTRL_DS_COMP BIT(5)
#define  XDMA_IRQ_ENG_CTRL_W_MASK  0xBFEFF07F
#define XDMA_IRQ_ENG_STAT          0x24
#define  XDMA_IRQ_ENG_STAT_US_COMP BIT(4)
#define  XDMA_IRQ_ENG_STAT_DS_COMP BIT(5)
#define  XDMA_IRQ_ENG_STAT_RESET   0xF8000000

#define XDMA_AST2600_BMC_CMDQ_ADDR   0x14
#define XDMA_AST2600_BMC_CMDQ_ENDP   0x18
#define XDMA_AST2600_BMC_CMDQ_WRP    0x1c
#define XDMA_AST2600_BMC_CMDQ_RDP    0x20
#define XDMA_AST2600_IRQ_CTRL        0x38
#define  XDMA_AST2600_IRQ_CTRL_US_COMP    BIT(16)
#define  XDMA_AST2600_IRQ_CTRL_DS_COMP    BIT(17)
#define  XDMA_AST2600_IRQ_CTRL_W_MASK     0x017003FF
#define XDMA_AST2600_IRQ_STATUS      0x3c
#define  XDMA_AST2600_IRQ_STATUS_US_COMP  BIT(16)
#define  XDMA_AST2600_IRQ_STATUS_DS_COMP  BIT(17)

#define XDMA_MEM_SIZE              0x1000

#define TO_REG(addr) ((addr) / sizeof(uint32_t))

static uint64_t aspeed_xdma_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t val = 0;
    AspeedXDMAState *xdma = opaque;

    if (addr < ASPEED_XDMA_REG_SIZE) {
        val = xdma->regs[TO_REG(addr)];
    }

    return (uint64_t)val;
}

static void aspeed_xdma_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned int size)
{
    unsigned int idx;
    uint32_t val32 = (uint32_t)val;
    AspeedXDMAState *xdma = opaque;
    AspeedXDMAClass *axc = ASPEED_XDMA_GET_CLASS(xdma);

    if (addr >= ASPEED_XDMA_REG_SIZE) {
        return;
    }

    if (addr == axc->cmdq_endp) {
        xdma->regs[TO_REG(addr)] = val32 & XDMA_BMC_CMDQ_W_MASK;
    } else if (addr == axc->cmdq_wrp) {
        idx = TO_REG(addr);
        xdma->regs[idx] = val32 & XDMA_BMC_CMDQ_W_MASK;
        xdma->regs[TO_REG(axc->cmdq_rdp)] = xdma->regs[idx];

        trace_aspeed_xdma_write(addr, val);

        if (xdma->bmc_cmdq_readp_set) {
            xdma->bmc_cmdq_readp_set = 0;
        } else {
            xdma->regs[TO_REG(axc->intr_status)] |= axc->intr_complete;

            if (xdma->regs[TO_REG(axc->intr_ctrl)] & axc->intr_complete) {
                qemu_irq_raise(xdma->irq);
            }
        }
    } else if (addr == axc->cmdq_rdp) {
        trace_aspeed_xdma_write(addr, val);

        if (val32 == XDMA_BMC_CMDQ_RDP_MAGIC) {
            xdma->bmc_cmdq_readp_set = 1;
        }
    } else if (addr == axc->intr_ctrl) {
        xdma->regs[TO_REG(addr)] = val32 & axc->intr_ctrl_mask;
    } else if (addr == axc->intr_status) {
        trace_aspeed_xdma_write(addr, val);

        idx = TO_REG(addr);
        if (val32 & axc->intr_complete) {
            xdma->regs[idx] &= ~axc->intr_complete;
            qemu_irq_lower(xdma->irq);
        }
    } else {
        xdma->regs[TO_REG(addr)] = val32;
    }
}

static const MemoryRegionOps aspeed_xdma_ops = {
    .read = aspeed_xdma_read,
    .write = aspeed_xdma_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aspeed_xdma_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedXDMAState *xdma = ASPEED_XDMA(dev);

    sysbus_init_irq(sbd, &xdma->irq);
    memory_region_init_io(&xdma->iomem, OBJECT(xdma), &aspeed_xdma_ops, xdma,
                          TYPE_ASPEED_XDMA, XDMA_MEM_SIZE);
    sysbus_init_mmio(sbd, &xdma->iomem);
}

static void aspeed_xdma_reset(DeviceState *dev)
{
    AspeedXDMAState *xdma = ASPEED_XDMA(dev);
    AspeedXDMAClass *axc = ASPEED_XDMA_GET_CLASS(xdma);

    xdma->bmc_cmdq_readp_set = 0;
    memset(xdma->regs, 0, ASPEED_XDMA_REG_SIZE);
    xdma->regs[TO_REG(axc->intr_status)] = XDMA_IRQ_ENG_STAT_RESET;

    qemu_irq_lower(xdma->irq);
}

static const VMStateDescription aspeed_xdma_vmstate = {
    .name = TYPE_ASPEED_XDMA,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedXDMAState, ASPEED_XDMA_NUM_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void aspeed_2600_xdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedXDMAClass *axc = ASPEED_XDMA_CLASS(klass);

    dc->desc = "ASPEED 2600 XDMA Controller";

    axc->cmdq_endp = XDMA_AST2600_BMC_CMDQ_ENDP;
    axc->cmdq_wrp = XDMA_AST2600_BMC_CMDQ_WRP;
    axc->cmdq_rdp = XDMA_AST2600_BMC_CMDQ_RDP;
    axc->intr_ctrl = XDMA_AST2600_IRQ_CTRL;
    axc->intr_ctrl_mask = XDMA_AST2600_IRQ_CTRL_W_MASK;
    axc->intr_status = XDMA_AST2600_IRQ_STATUS;
    axc->intr_complete = XDMA_AST2600_IRQ_STATUS_US_COMP |
        XDMA_AST2600_IRQ_STATUS_DS_COMP;
}

static const TypeInfo aspeed_2600_xdma_info = {
    .name = TYPE_ASPEED_2600_XDMA,
    .parent = TYPE_ASPEED_XDMA,
    .class_init = aspeed_2600_xdma_class_init,
};

static void aspeed_2500_xdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedXDMAClass *axc = ASPEED_XDMA_CLASS(klass);

    dc->desc = "ASPEED 2500 XDMA Controller";

    axc->cmdq_endp = XDMA_BMC_CMDQ_ENDP;
    axc->cmdq_wrp = XDMA_BMC_CMDQ_WRP;
    axc->cmdq_rdp = XDMA_BMC_CMDQ_RDP;
    axc->intr_ctrl = XDMA_IRQ_ENG_CTRL;
    axc->intr_ctrl_mask = XDMA_IRQ_ENG_CTRL_W_MASK;
    axc->intr_status = XDMA_IRQ_ENG_STAT;
    axc->intr_complete = XDMA_IRQ_ENG_STAT_US_COMP | XDMA_IRQ_ENG_STAT_DS_COMP;
};

static const TypeInfo aspeed_2500_xdma_info = {
    .name = TYPE_ASPEED_2500_XDMA,
    .parent = TYPE_ASPEED_XDMA,
    .class_init = aspeed_2500_xdma_class_init,
};

static void aspeed_2400_xdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedXDMAClass *axc = ASPEED_XDMA_CLASS(klass);

    dc->desc = "ASPEED 2400 XDMA Controller";

    axc->cmdq_endp = XDMA_BMC_CMDQ_ENDP;
    axc->cmdq_wrp = XDMA_BMC_CMDQ_WRP;
    axc->cmdq_rdp = XDMA_BMC_CMDQ_RDP;
    axc->intr_ctrl = XDMA_IRQ_ENG_CTRL;
    axc->intr_ctrl_mask = XDMA_IRQ_ENG_CTRL_W_MASK;
    axc->intr_status = XDMA_IRQ_ENG_STAT;
    axc->intr_complete = XDMA_IRQ_ENG_STAT_US_COMP | XDMA_IRQ_ENG_STAT_DS_COMP;
};

static const TypeInfo aspeed_2400_xdma_info = {
    .name = TYPE_ASPEED_2400_XDMA,
    .parent = TYPE_ASPEED_XDMA,
    .class_init = aspeed_2400_xdma_class_init,
};

static void aspeed_xdma_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->realize = aspeed_xdma_realize;
    dc->reset = aspeed_xdma_reset;
    dc->vmsd = &aspeed_xdma_vmstate;
}

static const TypeInfo aspeed_xdma_info = {
    .name          = TYPE_ASPEED_XDMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedXDMAState),
    .class_init    = aspeed_xdma_class_init,
    .class_size    = sizeof(AspeedXDMAClass),
    .abstract      = true,
};

static void aspeed_xdma_register_type(void)
{
    type_register_static(&aspeed_xdma_info);
    type_register_static(&aspeed_2400_xdma_info);
    type_register_static(&aspeed_2500_xdma_info);
    type_register_static(&aspeed_2600_xdma_info);
}
type_init(aspeed_xdma_register_type);
