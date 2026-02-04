/*
 * ASPEED AST1700 IO Expander
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/boards.h"
#include "qom/object.h"
#include "hw/core/qdev-properties.h"
#include "hw/arm/aspeed_ast1700.h"

#define AST2700_SOC_LTPI_SIZE        0x01000000
#define AST1700_SOC_SRAM_SIZE        0x00040000

enum {
    ASPEED_AST1700_DEV_SRAM,
    ASPEED_AST1700_DEV_UART12,
    ASPEED_AST1700_DEV_LTPI_CTRL,
};

static const hwaddr aspeed_ast1700_io_memmap[] = {
    [ASPEED_AST1700_DEV_SRAM]      =  0x00BC0000,
    [ASPEED_AST1700_DEV_UART12]    =  0x00C33B00,
    [ASPEED_AST1700_DEV_LTPI_CTRL] =  0x00C34000,
};

static void aspeed_ast1700_realize(DeviceState *dev, Error **errp)
{
    AspeedAST1700SoCState *s = ASPEED_AST1700(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    char dev_name[32];

    /* Occupy memory space for all controllers in AST1700 */
    memory_region_init(&s->iomem, OBJECT(s), TYPE_ASPEED_AST1700,
                       AST2700_SOC_LTPI_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    /* SRAM */
    snprintf(dev_name, sizeof(dev_name), "aspeed.ioexp-sram.%d", s->board_idx);
    memory_region_init_ram(&s->sram, OBJECT(s), dev_name,
                           AST1700_SOC_SRAM_SIZE, errp);
    memory_region_add_subregion(&s->iomem,
                            aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_SRAM],
                            &s->sram);

    /* UART */
    qdev_prop_set_uint8(DEVICE(&s->uart), "regshift", 2);
    qdev_prop_set_uint32(DEVICE(&s->uart), "baudbase", 38400);
    qdev_prop_set_uint8(DEVICE(&s->uart), "endianness", DEVICE_LITTLE_ENDIAN);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_UART12],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0));

    /* LTPI controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ltpi), errp)) {
        return;
    }
    memory_region_add_subregion(&s->iomem,
                        aspeed_ast1700_io_memmap[ASPEED_AST1700_DEV_LTPI_CTRL],
                        sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ltpi), 0));
}

static void aspeed_ast1700_instance_init(Object *obj)
{
    AspeedAST1700SoCState *s = ASPEED_AST1700(obj);

    /* UART */
    object_initialize_child(obj, "uart", &s->uart,
                            TYPE_SERIAL_MM);

    /* LTPI controller */
    object_initialize_child(obj, "ltpi-ctrl",
                            &s->ltpi, TYPE_ASPEED_LTPI);

    return;
}

static const Property aspeed_ast1700_props[] = {
    DEFINE_PROP_UINT8("board-idx", AspeedAST1700SoCState, board_idx, 0),
};

static void aspeed_ast1700_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_ast1700_realize;
    device_class_set_props(dc, aspeed_ast1700_props);
}

static const TypeInfo aspeed_ast1700_info = {
    .name          = TYPE_ASPEED_AST1700,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedAST1700SoCState),
    .class_init    = aspeed_ast1700_class_init,
    .instance_init = aspeed_ast1700_instance_init,
};

static void aspeed_ast1700_register_types(void)
{
    type_register_static(&aspeed_ast1700_info);
}

type_init(aspeed_ast1700_register_types);
