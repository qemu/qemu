/*
 * ASPEED SLI Controller
 *
 * Copyright (C) 2024 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"
#include "hw/misc/aspeed_sli.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

#define SLI_REGION_SIZE 0x500
#define TO_REG(addr) ((addr) >> 2)

static uint64_t aspeed_sli_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedSLIState *s = ASPEED_SLI(opaque);
    int reg = TO_REG(addr);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    trace_aspeed_sli_read(addr, size, s->regs[reg]);
    return s->regs[reg];
}

static void aspeed_sli_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedSLIState *s = ASPEED_SLI(opaque);
    int reg = TO_REG(addr);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    trace_aspeed_sli_write(addr, size, data);
    s->regs[reg] = data;
}

static uint64_t aspeed_sliio_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedSLIState *s = ASPEED_SLI(opaque);
    int reg = TO_REG(addr);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    trace_aspeed_sliio_read(addr, size, s->regs[reg]);
    return s->regs[reg];
}

static void aspeed_sliio_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedSLIState *s = ASPEED_SLI(opaque);
    int reg = TO_REG(addr);

    if (reg >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    trace_aspeed_sliio_write(addr, size, data);
    s->regs[reg] = data;
}

static const MemoryRegionOps aspeed_sli_ops = {
    .read = aspeed_sli_read,
    .write = aspeed_sli_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps aspeed_sliio_ops = {
    .read = aspeed_sliio_read,
    .write = aspeed_sliio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_sli_realize(DeviceState *dev, Error **errp)
{
    AspeedSLIState *s = ASPEED_SLI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sli_ops, s,
                          TYPE_ASPEED_SLI, SLI_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_sliio_realize(DeviceState *dev, Error **errp)
{
    AspeedSLIState *s = ASPEED_SLI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_sliio_ops, s,
                          TYPE_ASPEED_SLI, SLI_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_sli_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Aspeed SLI Controller";
    dc->realize = aspeed_sli_realize;
}

static const TypeInfo aspeed_sli_info = {
    .name          = TYPE_ASPEED_SLI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedSLIState),
    .class_init    = aspeed_sli_class_init,
    .abstract      = true,
};

static void aspeed_2700_sli_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "AST2700 SLI Controller";
}

static void aspeed_2700_sliio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "AST2700 I/O SLI Controller";
    dc->realize = aspeed_sliio_realize;
}

static const TypeInfo aspeed_2700_sli_info = {
    .name           = TYPE_ASPEED_2700_SLI,
    .parent         = TYPE_ASPEED_SLI,
    .class_init     = aspeed_2700_sli_class_init,
};

static const TypeInfo aspeed_2700_sliio_info = {
    .name           = TYPE_ASPEED_2700_SLIIO,
    .parent         = TYPE_ASPEED_SLI,
    .class_init     = aspeed_2700_sliio_class_init,
};

static void aspeed_sli_register_types(void)
{
    type_register_static(&aspeed_sli_info);
    type_register_static(&aspeed_2700_sli_info);
    type_register_static(&aspeed_2700_sliio_info);
}

type_init(aspeed_sli_register_types);
