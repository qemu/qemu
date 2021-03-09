/*
 *  ASPEED LPC Controller
 *
 *  Copyright (C) 2017-2018 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_lpc.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TO_REG(offset) ((offset) >> 2)

#define HICR0                TO_REG(0x00)
#define HICR1                TO_REG(0x04)
#define HICR2                TO_REG(0x08)
#define HICR3                TO_REG(0x0C)
#define HICR4                TO_REG(0x10)
#define HICR5                TO_REG(0x80)
#define HICR6                TO_REG(0x84)
#define HICR7                TO_REG(0x88)
#define HICR8                TO_REG(0x8C)

static uint64_t aspeed_lpc_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedLPCState *s = ASPEED_LPC(opaque);
    int reg = TO_REG(offset);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    return s->regs[reg];
}

static void aspeed_lpc_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned int size)
{
    AspeedLPCState *s = ASPEED_LPC(opaque);
    int reg = TO_REG(offset);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }

    s->regs[reg] = data;
}

static const MemoryRegionOps aspeed_lpc_ops = {
    .read = aspeed_lpc_read,
    .write = aspeed_lpc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_lpc_reset(DeviceState *dev)
{
    struct AspeedLPCState *s = ASPEED_LPC(dev);

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[HICR7] = s->hicr7;
}

static void aspeed_lpc_realize(DeviceState *dev, Error **errp)
{
    AspeedLPCState *s = ASPEED_LPC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_lpc_ops, s,
            TYPE_ASPEED_LPC, 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_lpc = {
    .name = TYPE_ASPEED_LPC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedLPCState, ASPEED_LPC_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static Property aspeed_lpc_properties[] = {
    DEFINE_PROP_UINT32("hicr7", AspeedLPCState, hicr7, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_lpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_lpc_realize;
    dc->reset = aspeed_lpc_reset;
    dc->desc = "Aspeed LPC Controller",
    dc->vmsd = &vmstate_aspeed_lpc;
    device_class_set_props(dc, aspeed_lpc_properties);
}

static const TypeInfo aspeed_lpc_info = {
    .name = TYPE_ASPEED_LPC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedLPCState),
    .class_init = aspeed_lpc_class_init,
};

static void aspeed_lpc_register_types(void)
{
    type_register_static(&aspeed_lpc_info);
}

type_init(aspeed_lpc_register_types);
