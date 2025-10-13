/*
 * ASPEED Ast27x0 SSP SoC
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

#define AST2700_SSP_RAM_SIZE (32 * MiB)

static const hwaddr aspeed_soc_ast27x0ssp_memmap[] = {
    [ASPEED_DEV_SRAM]      =  0x00000000,
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

static qemu_irq aspeed_soc_ast27x0ssp_get_irq(AspeedSoCState *s, int dev)
{
    Aspeed27x0SSPSoCState *a = ASPEED27X0SSP_SOC(s);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

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
    Aspeed27x0SSPSoCState *a = ASPEED27X0SSP_SOC(obj);
    AspeedSoCState *s = ASPEED_SOC(obj);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int i;

    object_initialize_child(obj, "armv7m", &a->armv7m, TYPE_ARMV7M);
    object_initialize_child(obj, "scu", &s->scu, TYPE_ASPEED_2700_SCU);
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev", sc->silicon_rev);

    for (i = 0; i < sc->uarts_num; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i], TYPE_SERIAL_MM);
    }

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
    Aspeed27x0SSPSoCState *a = ASPEED27X0SSP_SOC(dev_soc);
    AspeedSoCState *s = ASPEED_SOC(dev_soc);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    DeviceState *armv7m;
    g_autofree char *sram_name = NULL;
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

    sram_name = g_strdup_printf("aspeed.dram.%d",
                                CPU(a->armv7m.cpu)->cpu_index);

    if (!memory_region_init_ram(&s->sram, OBJECT(s), sram_name, sc->sram_size,
                                errp)) {
        return;
    }
    memory_region_add_subregion(s->memory,
                                sc->memmap[ASPEED_DEV_SRAM],
                                &s->sram);

    /* SCU */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    aspeed_mmio_map(s->memory, SYS_BUS_DEVICE(&s->scu), 0,
                    sc->memmap[ASPEED_DEV_SCU]);

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
    if (!aspeed_soc_uart_realize(s, errp)) {
        return;
    }

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

static void aspeed_soc_ast27x0ssp_class_init(ObjectClass *klass, const void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"), /* TODO: cortex-m4f */
        NULL
    };
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(dc);

    /* Reason: The Aspeed SoC can only be instantiated from a board */
    dc->user_creatable = false;
    dc->realize = aspeed_soc_ast27x0ssp_realize;

    sc->valid_cpu_types = valid_cpu_types;
    sc->silicon_rev = AST2700_A1_SILICON_REV;
    sc->sram_size = AST2700_SSP_RAM_SIZE;
    sc->spis_num = 0;
    sc->ehcis_num = 0;
    sc->wdts_num = 0;
    sc->macs_num = 0;
    sc->uarts_num = 13;
    sc->uarts_base = ASPEED_DEV_UART0;
    sc->irqmap = aspeed_soc_ast27x0ssp_irqmap;
    sc->memmap = aspeed_soc_ast27x0ssp_memmap;
    sc->num_cpus = 1;
    sc->get_irq = aspeed_soc_ast27x0ssp_get_irq;
}

static const TypeInfo aspeed_soc_ast27x0ssp_types[] = {
    {
        .name           = TYPE_ASPEED27X0SSP_SOC,
        .parent         = TYPE_ASPEED_SOC,
        .instance_size  = sizeof(Aspeed27x0SSPSoCState),
        .instance_init  = aspeed_soc_ast27x0ssp_init,
        .class_init     = aspeed_soc_ast27x0ssp_class_init,
    },
};

DEFINE_TYPES(aspeed_soc_ast27x0ssp_types)
