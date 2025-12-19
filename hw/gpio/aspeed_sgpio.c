/*
 * ASPEED Serial GPIO Controller
 *
 * Copyright 2025 Google LLC.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/aspeed_sgpio.h"

static uint64_t aspeed_sgpio_2700_read_control_reg(AspeedSGPIOState *s,
                                uint32_t reg)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);
    uint32_t idx = reg - R_SGPIO_0_CONTROL;
    if (idx >= agc->nr_sgpio_pin_pairs) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pin index: %d, out of bounds\n",
                      __func__, idx);
        return 0;
    }
    return s->ctrl_regs[idx];
}

static void aspeed_sgpio_2700_write_control_reg(AspeedSGPIOState *s,
                                uint32_t reg, uint64_t data)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);
    uint32_t idx = reg - R_SGPIO_0_CONTROL;
    if (idx >= agc->nr_sgpio_pin_pairs) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: pin index: %d, out of bounds\n",
                      __func__, idx);
        return;
    }
    s->ctrl_regs[idx] = data;
}

static uint64_t aspeed_sgpio_2700_read(void *opaque, hwaddr offset,
                                uint32_t size)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(opaque);
    uint64_t value = 0;
    uint64_t reg;

    reg = offset >> 2;

    switch (reg) {
    case R_SGPIO_0_CONTROL ... R_SGPIO_255_CONTROL:
        value = aspeed_sgpio_2700_read_control_reg(s, reg);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no getter for offset 0x%"
                      HWADDR_PRIx"\n", __func__, offset);
        return 0;
    }

    return value;
}

static void aspeed_sgpio_2700_write(void *opaque, hwaddr offset, uint64_t data,
                                uint32_t size)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(opaque);
    uint64_t reg;

    reg = offset >> 2;

    switch (reg) {
    case R_SGPIO_0_CONTROL ... R_SGPIO_255_CONTROL:
        aspeed_sgpio_2700_write_control_reg(s, reg, data);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: no setter for offset 0x%"
                      HWADDR_PRIx"\n", __func__, offset);
        return;
    }
}

static const MemoryRegionOps aspeed_sgpio_2700_ops = {
    .read       = aspeed_sgpio_2700_read,
    .write      = aspeed_sgpio_2700_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void aspeed_sgpio_realize(DeviceState *dev, Error **errp)
{
    AspeedSGPIOState *s = ASPEED_SGPIO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AspeedSGPIOClass *agc = ASPEED_SGPIO_GET_CLASS(s);

    /* Interrupt parent line */
    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), agc->reg_ops, s,
                          TYPE_ASPEED_SGPIO, agc->mem_size);

    sysbus_init_mmio(sbd, &s->iomem);
}

static void aspeed_sgpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_sgpio_realize;
    dc->desc = "Aspeed SGPIO Controller";
}

static void aspeed_sgpio_2700_class_init(ObjectClass *klass, const void *data)
{
    AspeedSGPIOClass *agc = ASPEED_SGPIO_CLASS(klass);
    agc->nr_sgpio_pin_pairs = ASPEED_SGPIO_MAX_PIN_PAIR;
    agc->mem_size = 0x1000;
    agc->reg_ops = &aspeed_sgpio_2700_ops;
}

static const TypeInfo aspeed_sgpio_info = {
    .name           = TYPE_ASPEED_SGPIO,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(AspeedSGPIOState),
    .class_size     = sizeof(AspeedSGPIOClass),
    .class_init     = aspeed_sgpio_class_init,
    .abstract       = true,
};

static const TypeInfo aspeed_sgpio_ast2700_info = {
    .name           = TYPE_ASPEED_SGPIO "-ast2700",
    .parent         = TYPE_ASPEED_SGPIO,
    .class_init     = aspeed_sgpio_2700_class_init,
};

static void aspeed_sgpio_register_types(void)
{
    type_register_static(&aspeed_sgpio_info);
    type_register_static(&aspeed_sgpio_ast2700_info);
}

type_init(aspeed_sgpio_register_types);
