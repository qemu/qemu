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

    if (addr >= ASPEED_XDMA_REG_SIZE) {
        return;
    }

    switch (addr) {
    case XDMA_BMC_CMDQ_ENDP:
        xdma->regs[TO_REG(addr)] = val32 & XDMA_BMC_CMDQ_W_MASK;
        break;
    case XDMA_BMC_CMDQ_WRP:
        idx = TO_REG(addr);
        xdma->regs[idx] = val32 & XDMA_BMC_CMDQ_W_MASK;
        xdma->regs[TO_REG(XDMA_BMC_CMDQ_RDP)] = xdma->regs[idx];

        trace_aspeed_xdma_write(addr, val);

        if (xdma->bmc_cmdq_readp_set) {
            xdma->bmc_cmdq_readp_set = 0;
        } else {
            xdma->regs[TO_REG(XDMA_IRQ_ENG_STAT)] |=
                XDMA_IRQ_ENG_STAT_US_COMP | XDMA_IRQ_ENG_STAT_DS_COMP;

            if (xdma->regs[TO_REG(XDMA_IRQ_ENG_CTRL)] &
                (XDMA_IRQ_ENG_CTRL_US_COMP | XDMA_IRQ_ENG_CTRL_DS_COMP))
                qemu_irq_raise(xdma->irq);
        }
        break;
    case XDMA_BMC_CMDQ_RDP:
        trace_aspeed_xdma_write(addr, val);

        if (val32 == XDMA_BMC_CMDQ_RDP_MAGIC) {
            xdma->bmc_cmdq_readp_set = 1;
        }
        break;
    case XDMA_IRQ_ENG_CTRL:
        xdma->regs[TO_REG(addr)] = val32 & XDMA_IRQ_ENG_CTRL_W_MASK;
        break;
    case XDMA_IRQ_ENG_STAT:
        trace_aspeed_xdma_write(addr, val);

        idx = TO_REG(addr);
        if (val32 & (XDMA_IRQ_ENG_STAT_US_COMP | XDMA_IRQ_ENG_STAT_DS_COMP)) {
            xdma->regs[idx] &=
                ~(XDMA_IRQ_ENG_STAT_US_COMP | XDMA_IRQ_ENG_STAT_DS_COMP);
            qemu_irq_lower(xdma->irq);
        }
        break;
    default:
        xdma->regs[TO_REG(addr)] = val32;
        break;
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

    xdma->bmc_cmdq_readp_set = 0;
    memset(xdma->regs, 0, ASPEED_XDMA_REG_SIZE);
    xdma->regs[TO_REG(XDMA_IRQ_ENG_STAT)] = XDMA_IRQ_ENG_STAT_RESET;

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
};

static void aspeed_xdma_register_type(void)
{
    type_register_static(&aspeed_xdma_info);
}
type_init(aspeed_xdma_register_type);
