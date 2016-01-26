/*
 * DMA device simulation in PKUnity SoC
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/sysbus.h"

#undef DEBUG_PUV3
#include "hw/unicore32/puv3.h"

#define PUV3_DMA_CH_NR          (6)
#define PUV3_DMA_CH_MASK        (0xff)
#define PUV3_DMA_CH(offset)     ((offset) >> 8)

#define TYPE_PUV3_DMA "puv3_dma"
#define PUV3_DMA(obj) OBJECT_CHECK(PUV3DMAState, (obj), TYPE_PUV3_DMA)

typedef struct PUV3DMAState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t reg_CFG[PUV3_DMA_CH_NR];
} PUV3DMAState;

static uint64_t puv3_dma_read(void *opaque, hwaddr offset,
        unsigned size)
{
    PUV3DMAState *s = opaque;
    uint32_t ret = 0;

    assert(PUV3_DMA_CH(offset) < PUV3_DMA_CH_NR);

    switch (offset & PUV3_DMA_CH_MASK) {
    case 0x10:
        ret = s->reg_CFG[PUV3_DMA_CH(offset)];
        break;
    default:
        DPRINTF("Bad offset 0x%x\n", offset);
    }
    DPRINTF("offset 0x%x, value 0x%x\n", offset, ret);

    return ret;
}

static void puv3_dma_write(void *opaque, hwaddr offset,
        uint64_t value, unsigned size)
{
    PUV3DMAState *s = opaque;

    assert(PUV3_DMA_CH(offset) < PUV3_DMA_CH_NR);

    switch (offset & PUV3_DMA_CH_MASK) {
    case 0x10:
        s->reg_CFG[PUV3_DMA_CH(offset)] = value;
        break;
    default:
        DPRINTF("Bad offset 0x%x\n", offset);
    }
    DPRINTF("offset 0x%x, value 0x%x\n", offset, value);
}

static const MemoryRegionOps puv3_dma_ops = {
    .read = puv3_dma_read,
    .write = puv3_dma_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int puv3_dma_init(SysBusDevice *dev)
{
    PUV3DMAState *s = PUV3_DMA(dev);
    int i;

    for (i = 0; i < PUV3_DMA_CH_NR; i++) {
        s->reg_CFG[i] = 0x0;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &puv3_dma_ops, s, "puv3_dma",
            PUV3_REGS_OFFSET);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static void puv3_dma_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = puv3_dma_init;
}

static const TypeInfo puv3_dma_info = {
    .name = TYPE_PUV3_DMA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PUV3DMAState),
    .class_init = puv3_dma_class_init,
};

static void puv3_dma_register_type(void)
{
    type_register_static(&puv3_dma_info);
}

type_init(puv3_dma_register_type)
