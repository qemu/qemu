/*
 * ASPEED AST1040 SoC
 *
 * Copyright (C) 2026 ASPEED Technology Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/core/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"

static const hwaddr aspeed_soc_ast1040_memmap[] = {
    [ASPEED_DEV_SRAM1]     = 0x00000000, /* Hyper RAM */
    [ASPEED_DEV_FMC]       = 0x74000000,
    [ASPEED_DEV_SPI0]      = 0x74010000,
    [ASPEED_DEV_SPI1]      = 0x74020000,
    [ASPEED_DEV_PWM]       = 0x740C0000,
    [ASPEED_DEV_UDC]       = 0x74120000,
    [ASPEED_DEV_SRAM0]     = 0x74B80000,
    [ASPEED_DEV_ADC]       = 0x74C00000,
    [ASPEED_DEV_JTAG0]     = 0x74C01000,
    [ASPEED_DEV_SCU]       = 0x74C02000,
    [ASPEED_DEV_ESPI]      = 0x74C05000,
    [ASPEED_DEV_JTAG1]     = 0x74C09000,
    [ASPEED_DEV_GPIO]      = 0x74C0B000,
    [ASPEED_DEV_SGPIOM0]   = 0x74C0C000,
    [ASPEED_DEV_SGPIOM1]   = 0x74C0D000,
    [ASPEED_DEV_I2C]       = 0x74C0F000,
    [ASPEED_DEV_PECI]      = 0x74C1F000,
    [ASPEED_DEV_I3C]       = 0x74C20000,
    [ASPEED_DEV_UART0]     = 0x74C33000,
    [ASPEED_DEV_UART1]     = 0x74C33100,
    [ASPEED_DEV_UART2]     = 0x74C33200,
    [ASPEED_DEV_UART3]     = 0x74C33300,
    [ASPEED_DEV_UART4]     = 0x74C33400,
    [ASPEED_DEV_UART5]     = 0x74C33500,
    [ASPEED_DEV_UART6]     = 0x74C33600,
    [ASPEED_DEV_UART7]     = 0x74C33700,
    [ASPEED_DEV_UART8]     = 0x74C33800,
    [ASPEED_DEV_UART9]     = 0x74C33900,
    [ASPEED_DEV_UART10]    = 0x74C33A00,
    [ASPEED_DEV_UART11]    = 0x74C33B00,
    [ASPEED_DEV_UART12]    = 0x74C33C00,
    [ASPEED_DEV_WDT]       = 0x74C37000,
    [ASPEED_DEV_TIMER1]    = 0x74C3A000,
};

static const int aspeed_soc_ast1040_irqmap[] = {
    [ASPEED_DEV_ESPI]      = 10,
    [ASPEED_DEV_I2C]       = 64, /* 64 ~ 77 */
    [ASPEED_DEV_ADC]       = 80,
    [ASPEED_DEV_GPIO]      = 82,
    [ASPEED_DEV_SGPIOM0]   = 85,
    [ASPEED_DEV_TIMER1]    = 92,
    [ASPEED_DEV_I3C]       = 96, /* 96 ~ 103 */
    [ASPEED_DEV_WDT]       = 112,
    [ASPEED_DEV_FMC]       = 121,
    [ASPEED_DEV_SPI0]      = 122,
    [ASPEED_DEV_SPI1]      = 123,
    [ASPEED_DEV_PWM]       = 125,
    [ASPEED_DEV_UART0]     = 135,
    [ASPEED_DEV_UART1]     = 136,
    [ASPEED_DEV_UART2]     = 137,
    [ASPEED_DEV_UART3]     = 138,
    [ASPEED_DEV_UART4]     = 139,
    [ASPEED_DEV_UART5]     = 140,
    [ASPEED_DEV_UART6]     = 141,
    [ASPEED_DEV_UART7]     = 142,
    [ASPEED_DEV_UART8]     = 143,
    [ASPEED_DEV_UART9]     = 144,
    [ASPEED_DEV_UART10]    = 145,
    [ASPEED_DEV_UART11]    = 146,
    [ASPEED_DEV_UART12]    = 147,
    [ASPEED_DEV_JTAG0]     = 162,
    [ASPEED_DEV_PECI]      = 164,
};

static qemu_irq aspeed_soc_ast1040_get_irq(AspeedSoCState *s, int dev)
{
    Aspeed10x0SoCState *a = ASPEED10X0_SOC(s);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    return qdev_get_gpio_in(DEVICE(&a->armv7m), sc->irqmap[dev]);
}

static void aspeed_soc_ast1040_init(Object *obj)
{
    Aspeed10x0SoCState *a = ASPEED10X0_SOC(obj);
    AspeedSoCState *s = ASPEED_SOC(obj);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int i;
    object_initialize_child(obj, "armv7m", &a->armv7m, TYPE_ARMV7M);

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);

    /* AST1040 uses the AST2700 SCUIO model */
    object_initialize_child(obj, "scu", &s->scu, TYPE_ASPEED_2700_SCUIO);
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev", sc->silicon_rev);

    object_property_add_alias(obj, "hw-strap1", OBJECT(&s->scu), "hw-strap1");
    object_property_add_alias(obj, "hw-strap2", OBJECT(&s->scu), "hw-strap2");

    for (i = 0; i < sc->uarts_num; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i], TYPE_SERIAL_MM);
    }

    object_initialize_child(obj, "adc", &s->adc, TYPE_ASPEED_2700_ADC);
    object_initialize_child(obj, "peci", &s->peci, TYPE_ASPEED_PECI);

    object_initialize_child(obj, "pwm", &s->pwm, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "espi", &s->espi, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "udc", &s->udc, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "sgpiom[0]", &s->sgpiom[0],
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "sgpiom[1]", &s->sgpiom[1],
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "jtag[0]", &s->jtag[0],
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "jtag[1]", &s->jtag[1],
                            TYPE_UNIMPLEMENTED_DEVICE);
}

static void aspeed_soc_ast1040_realize(DeviceState *dev_soc, Error **errp)
{
    Aspeed10x0SoCState *a = ASPEED10X0_SOC(dev_soc);
    AspeedSoCState *s = ASPEED_SOC(dev_soc);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    g_autofree char *hyperram_name = NULL;
    g_autofree char *sram_name = NULL;
    DeviceState *armv7m;
    Error *err = NULL;
    int uart;
    int i;

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    /* AST1040 CPU Core */
    armv7m = DEVICE(&a->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 256);
    qdev_prop_set_string(armv7m, "cpu-type",
                         aspeed_soc_cpu_type(sc->valid_cpu_types));
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    object_property_set_link(OBJECT(&a->armv7m), "memory",
                             OBJECT(s->memory), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&a->armv7m), &error_abort);

    /* Internal SRAM */
    sram_name = g_strdup_printf("aspeed.sram.%d",
                                CPU(a->armv7m.cpu)->cpu_index);
    memory_region_init_ram(&s->sram[0], OBJECT(s), sram_name,
                           sc->sram_size[0], &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(s->memory, sc->memmap[ASPEED_DEV_SRAM0],
                                &s->sram[0]);

    /* Internal Hyper RAM */
    hyperram_name = g_strdup_printf("aspeed.hyperram.%d",
                                    CPU(a->armv7m.cpu)->cpu_index);
    memory_region_init_ram(&s->sram[1], OBJECT(s), hyperram_name,
                           sc->sram_size[1], &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(s->memory, sc->memmap[ASPEED_DEV_SRAM1],
                                &s->sram[1]);

    /* SCU */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    aspeed_mmio_map(s->memory, SYS_BUS_DEVICE(&s->scu), 0,
                    sc->memmap[ASPEED_DEV_SCU]);

    /* UART */
    for (i = 0, uart = sc->uarts_base; i < sc->uarts_num; i++, uart++) {
        if (!aspeed_soc_uart_realize(s->memory, &s->uart[i],
                                     sc->memmap[uart], errp)) {
            return;
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           aspeed_soc_ast1040_get_irq(s, uart));
    }

    /* ADC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc), errp)) {
        return;
    }
    aspeed_mmio_map(s->memory, SYS_BUS_DEVICE(&s->adc), 0,
                    sc->memmap[ASPEED_DEV_ADC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
                       aspeed_soc_ast1040_get_irq(s, ASPEED_DEV_ADC));

    /* PECI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->peci), errp)) {
        return;
    }
    aspeed_mmio_map(s->memory, SYS_BUS_DEVICE(&s->peci), 0,
                    sc->memmap[ASPEED_DEV_PECI]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peci), 0,
                       aspeed_soc_ast1040_get_irq(s, ASPEED_DEV_PECI));

    /* Unimplemented peripherals */
    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->pwm),
                                  "aspeed.pwm",
                                  sc->memmap[ASPEED_DEV_PWM], 0x10000);

    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->espi),
                                  "aspeed.espi",
                                  sc->memmap[ASPEED_DEV_ESPI], 0x1000);

    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->udc),
                                  "aspeed.udc",
                                  sc->memmap[ASPEED_DEV_UDC], 0x4000);

    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->sgpiom[0]),
                                  "aspeed.sgpiom0",
                                  sc->memmap[ASPEED_DEV_SGPIOM0], 0x1000);

    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->sgpiom[1]),
                                  "aspeed.sgpiom1",
                                  sc->memmap[ASPEED_DEV_SGPIOM1], 0x1000);

    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->jtag[0]),
                                  "aspeed.jtag0",
                                  sc->memmap[ASPEED_DEV_JTAG0], 0x100);

    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->jtag[1]),
                                  "aspeed.jtag1",
                                  sc->memmap[ASPEED_DEV_JTAG1], 0x100);
}

static void aspeed_soc_ast1040_class_init(ObjectClass *klass, const void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"), /* TODO cortex-m4f */
        NULL
    };
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(dc);

    /* Reason: The Aspeed SoC can only be instantiated from a board */
    dc->user_creatable = false;
    dc->realize = aspeed_soc_ast1040_realize;

    sc->valid_cpu_types = valid_cpu_types;
    sc->silicon_rev     = AST1040_A0_SILICON_REV;
    sc->sram_size[0]    = 128 * KiB;
    sc->sram_size[1]    = 16 * MiB; /* Hyper RAM */
    sc->uarts_num       = 13;
    sc->uarts_base      = ASPEED_DEV_UART0;
    sc->irqmap          = aspeed_soc_ast1040_irqmap;
    sc->memmap          = aspeed_soc_ast1040_memmap;
    sc->num_cpus        = 1;
}

static const TypeInfo aspeed_soc_ast1040_types[] = {
    {
        .name           = "ast1040-a0",
        .parent         = TYPE_ASPEED10X0_SOC,
        .instance_init  = aspeed_soc_ast1040_init,
        .class_init     = aspeed_soc_ast1040_class_init,
    }
};

DEFINE_TYPES(aspeed_soc_ast1040_types)
