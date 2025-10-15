/*
 * ASPEED Ast27x0 SSP Coprocessor
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/arm/aspeed_coprocessor.h"

#define AST2700_SSP_SDRAM_SIZE (512 * MiB)

static const hwaddr aspeed_soc_ast27x0ssp_memmap[] = {
    [ASPEED_DEV_SDRAM]     =  0x00000000,
    [ASPEED_DEV_SRAM]      =  0x70000000,
    [ASPEED_DEV_INTC]      =  0x72100000,
    [ASPEED_DEV_SCU]       =  0x72C02000,
    [ASPEED_DEV_SCUIO]     =  0x74C02000,
    [ASPEED_DEV_UART0]     =  0x74C33000,
    [ASPEED_DEV_UART1]     =  0x74C33100,
    [ASPEED_DEV_UART2]     =  0x74C33200,
    [ASPEED_DEV_UART3]     =  0x74C33300,
    [ASPEED_DEV_UART4]     =  0x72C1A000,
    [ASPEED_DEV_INTCIO]    =  0x74C18000,
    [ASPEED_DEV_IPC0]      =  0x72C1C000,
    [ASPEED_DEV_IPC1]      =  0x74C39000,
    [ASPEED_DEV_UART5]     =  0x74C33400,
    [ASPEED_DEV_UART6]     =  0x74C33500,
    [ASPEED_DEV_UART7]     =  0x74C33600,
    [ASPEED_DEV_UART8]     =  0x74C33700,
    [ASPEED_DEV_UART9]     =  0x74C33800,
    [ASPEED_DEV_UART10]    =  0x74C33900,
    [ASPEED_DEV_UART11]    =  0x74C33A00,
    [ASPEED_DEV_UART12]    =  0x74C33B00,
    [ASPEED_DEV_TIMER1]    =  0x72C10000,
};

static const int aspeed_soc_ast27x0ssp_irqmap[] = {
    [ASPEED_DEV_SCU]       = 12,
    [ASPEED_DEV_UART0]     = 164,
    [ASPEED_DEV_UART1]     = 164,
    [ASPEED_DEV_UART2]     = 164,
    [ASPEED_DEV_UART3]     = 164,
    [ASPEED_DEV_UART4]     = 8,
    [ASPEED_DEV_UART5]     = 164,
    [ASPEED_DEV_UART6]     = 164,
    [ASPEED_DEV_UART7]     = 164,
    [ASPEED_DEV_UART8]     = 164,
    [ASPEED_DEV_UART9]     = 164,
    [ASPEED_DEV_UART10]    = 164,
    [ASPEED_DEV_UART11]    = 164,
    [ASPEED_DEV_UART12]    = 164,
    [ASPEED_DEV_TIMER1]    = 16,
};

/* SSPINT 164 */
static const int ast2700_ssp132_ssp164_intcmap[] = {
    [ASPEED_DEV_UART0]     = 7,
    [ASPEED_DEV_UART1]     = 8,
    [ASPEED_DEV_UART2]     = 9,
    [ASPEED_DEV_UART3]     = 10,
    [ASPEED_DEV_UART5]     = 11,
    [ASPEED_DEV_UART6]     = 12,
    [ASPEED_DEV_UART7]     = 13,
    [ASPEED_DEV_UART8]     = 14,
    [ASPEED_DEV_UART9]     = 15,
    [ASPEED_DEV_UART10]    = 16,
    [ASPEED_DEV_UART11]    = 17,
    [ASPEED_DEV_UART12]    = 18,
};

struct nvic_intc_irq_info {
    int irq;
    int intc_idx;
    int orgate_idx;
    const int *ptr;
};

static struct nvic_intc_irq_info ast2700_ssp_intcmap[] = {
    {160, 1, 0, NULL},
    {161, 1, 1, NULL},
    {162, 1, 2, NULL},
    {163, 1, 3, NULL},
    {164, 1, 4, ast2700_ssp132_ssp164_intcmap},
    {165, 1, 5, NULL},
    {166, 1, 6, NULL},
    {167, 1, 7, NULL},
    {168, 1, 8, NULL},
    {169, 1, 9, NULL},
    {128, 0, 1, NULL},
    {129, 0, 2, NULL},
    {130, 0, 3, NULL},
    {131, 0, 4, NULL},
    {132, 0, 5, ast2700_ssp132_ssp164_intcmap},
    {133, 0, 6, NULL},
    {134, 0, 7, NULL},
    {135, 0, 8, NULL},
    {136, 0, 9, NULL},
};

static qemu_irq aspeed_soc_ast27x0ssp_get_irq(AspeedCoprocessorState *s,
                                              int dev)
{
    Aspeed27x0CoprocessorState *a = ASPEED27X0SSP_COPROCESSOR(s);
    AspeedCoprocessorClass *sc = ASPEED_COPROCESSOR_GET_CLASS(s);

    int or_idx;
    int idx;
    int i;

    for (i = 0; i < ARRAY_SIZE(ast2700_ssp_intcmap); i++) {
        if (sc->irqmap[dev] == ast2700_ssp_intcmap[i].irq) {
            assert(ast2700_ssp_intcmap[i].ptr);
            or_idx = ast2700_ssp_intcmap[i].orgate_idx;
            idx = ast2700_ssp_intcmap[i].intc_idx;
            return qdev_get_gpio_in(DEVICE(&a->intc[idx].orgates[or_idx]),
                                    ast2700_ssp_intcmap[i].ptr[dev]);
        }
    }

    return qdev_get_gpio_in(DEVICE(&a->armv7m), sc->irqmap[dev]);
}

static void aspeed_soc_ast27x0ssp_init(Object *obj)
{
    Aspeed27x0CoprocessorState *a = ASPEED27X0SSP_COPROCESSOR(obj);
    AspeedCoprocessorState *s = ASPEED_COPROCESSOR(obj);

    object_initialize_child(obj, "armv7m", &a->armv7m, TYPE_ARMV7M);
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);

    object_initialize_child(obj, "intc0", &a->intc[0],
                            TYPE_ASPEED_2700SSP_INTC);
    object_initialize_child(obj, "intc1", &a->intc[1],
                            TYPE_ASPEED_2700SSP_INTCIO);

    object_initialize_child(obj, "timerctrl", &s->timerctrl,
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ipc0", &a->ipc[0],
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "ipc1", &a->ipc[1],
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "scuio", &a->scuio,
                            TYPE_UNIMPLEMENTED_DEVICE);
}

static void aspeed_soc_ast27x0ssp_realize(DeviceState *dev_soc, Error **errp)
{
    Aspeed27x0CoprocessorState *a = ASPEED27X0SSP_COPROCESSOR(dev_soc);
    AspeedCoprocessorState *s = ASPEED_COPROCESSOR(dev_soc);
    AspeedCoprocessorClass *sc = ASPEED_COPROCESSOR_GET_CLASS(s);
    DeviceState *armv7m;
    g_autofree char *sdram_name = NULL;
    int i;

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    /* AST27X0 SSP Core */
    armv7m = DEVICE(&a->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 256);
    qdev_prop_set_string(armv7m, "cpu-type",
                         aspeed_soc_cpu_type(sc->valid_cpu_types));
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    object_property_set_link(OBJECT(&a->armv7m), "memory",
                             OBJECT(s->memory), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&a->armv7m), &error_abort);

    /* SDRAM */
    sdram_name = g_strdup_printf("aspeed.sdram.%d",
                                 CPU(a->armv7m.cpu)->cpu_index);
    if (!memory_region_init_ram(&s->sdram, OBJECT(s), sdram_name,
                                AST2700_SSP_SDRAM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->memory,
                                sc->memmap[ASPEED_DEV_SDRAM],
                                &s->sdram);

    /* SRAM */
    memory_region_init_alias(&s->sram_alias, OBJECT(s), "sram.alias",
                             s->sram, 0, memory_region_size(s->sram));
    memory_region_add_subregion(s->memory, sc->memmap[ASPEED_DEV_SRAM],
                                &s->sram_alias);

    /* SCU */
    memory_region_init_alias(&s->scu_alias, OBJECT(s), "scu.alias",
                             &s->scu->iomem, 0,
                             memory_region_size(&s->scu->iomem));
    memory_region_add_subregion(s->memory, sc->memmap[ASPEED_DEV_SCU],
                                &s->scu_alias);

    /* INTC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&a->intc[0]), errp)) {
        return;
    }

    aspeed_mmio_map(s->memory, SYS_BUS_DEVICE(&a->intc[0]), 0,
                    sc->memmap[ASPEED_DEV_INTC]);

    /* INTCIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&a->intc[1]), errp)) {
        return;
    }

    aspeed_mmio_map(s->memory, SYS_BUS_DEVICE(&a->intc[1]), 0,
                    sc->memmap[ASPEED_DEV_INTCIO]);

    /* irq source orgates -> INTC0 */
    for (i = 0; i < ASPEED_INTC_GET_CLASS(&a->intc[0])->num_inpins; i++) {
        qdev_connect_gpio_out(DEVICE(&a->intc[0].orgates[i]), 0,
                              qdev_get_gpio_in(DEVICE(&a->intc[0]), i));
    }
    for (i = 0; i < ASPEED_INTC_GET_CLASS(&a->intc[0])->num_outpins; i++) {
        assert(i < ARRAY_SIZE(ast2700_ssp_intcmap));
        sysbus_connect_irq(SYS_BUS_DEVICE(&a->intc[0]), i,
                           qdev_get_gpio_in(DEVICE(&a->armv7m),
                                            ast2700_ssp_intcmap[i].irq));
    }
    /* irq source orgates -> INTCIO */
    for (i = 0; i < ASPEED_INTC_GET_CLASS(&a->intc[1])->num_inpins; i++) {
        qdev_connect_gpio_out(DEVICE(&a->intc[1].orgates[i]), 0,
                              qdev_get_gpio_in(DEVICE(&a->intc[1]), i));
    }
    /* INTCIO -> INTC */
    for (i = 0; i < ASPEED_INTC_GET_CLASS(&a->intc[1])->num_outpins; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&a->intc[1]), i,
                        qdev_get_gpio_in(DEVICE(&a->intc[0].orgates[0]), i));
    }

    /* UART */
    memory_region_init_alias(&s->uart_alias, OBJECT(s), "uart.alias",
                             &s->uart->serial.io, 0,
                             memory_region_size(&s->uart->serial.io));
    memory_region_add_subregion(s->memory, sc->memmap[s->uart_dev],
                                &s->uart_alias);
    /*
     * Redirect the UART interrupt to the NVIC, replacing the default routing
     * to the PSP's GIC.
     */
    sysbus_connect_irq(SYS_BUS_DEVICE(s->uart), 0,
                       aspeed_soc_ast27x0ssp_get_irq(s, s->uart_dev));

    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->timerctrl),
                                  "aspeed.timerctrl",
                                  sc->memmap[ASPEED_DEV_TIMER1], 0x200);
    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&a->ipc[0]),
                                  "aspeed.ipc0",
                                  sc->memmap[ASPEED_DEV_IPC0], 0x1000);
    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&a->ipc[1]),
                                  "aspeed.ipc1",
                                  sc->memmap[ASPEED_DEV_IPC1], 0x1000);
    aspeed_mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&a->scuio),
                                  "aspeed.scuio",
                                  sc->memmap[ASPEED_DEV_SCUIO], 0x1000);
}

static void aspeed_soc_ast27x0ssp_class_init(ObjectClass *klass,
                                             const void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"), /* TODO: cortex-m4f */
        NULL
    };
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedCoprocessorClass *sc = ASPEED_COPROCESSOR_CLASS(dc);

    /* Reason: The Aspeed Coprocessor can only be instantiated from a board */
    dc->user_creatable = false;
    dc->realize = aspeed_soc_ast27x0ssp_realize;

    sc->valid_cpu_types = valid_cpu_types;
    sc->irqmap = aspeed_soc_ast27x0ssp_irqmap;
    sc->memmap = aspeed_soc_ast27x0ssp_memmap;
}

static const TypeInfo aspeed_soc_ast27x0ssp_types[] = {
    {
        .name           = TYPE_ASPEED27X0SSP_COPROCESSOR,
        .parent         = TYPE_ASPEED_COPROCESSOR,
        .instance_size  = sizeof(Aspeed27x0CoprocessorState),
        .instance_init  = aspeed_soc_ast27x0ssp_init,
        .class_init     = aspeed_soc_ast27x0ssp_class_init,
    },
};

DEFINE_TYPES(aspeed_soc_ast27x0ssp_types)
