/*
 * ASPEED Ast10x0 SoC
 *
 * Copyright (C) 2022 ASPEED Technology Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * Implementation extracted from the AST2600 and adapted for Ast10x0.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"

#define ASPEED_SOC_IOMEM_SIZE 0x00200000

static const hwaddr aspeed_soc_ast1030_memmap[] = {
    [ASPEED_DEV_SRAM]      = 0x00000000,
    [ASPEED_DEV_SECSRAM]   = 0x79000000,
    [ASPEED_DEV_IOMEM]     = 0x7E600000,
    [ASPEED_DEV_PWM]       = 0x7E610000,
    [ASPEED_DEV_FMC]       = 0x7E620000,
    [ASPEED_DEV_SPI1]      = 0x7E630000,
    [ASPEED_DEV_SPI2]      = 0x7E640000,
    [ASPEED_DEV_UDC]       = 0x7E6A2000,
    [ASPEED_DEV_HACE]      = 0x7E6D0000,
    [ASPEED_DEV_SCU]       = 0x7E6E2000,
    [ASPEED_DEV_JTAG0]     = 0x7E6E4000,
    [ASPEED_DEV_JTAG1]     = 0x7E6E4100,
    [ASPEED_DEV_ADC]       = 0x7E6E9000,
    [ASPEED_DEV_ESPI]      = 0x7E6EE000,
    [ASPEED_DEV_SBC]       = 0x7E6F2000,
    [ASPEED_DEV_GPIO]      = 0x7E780000,
    [ASPEED_DEV_SGPIOM]    = 0x7E780500,
    [ASPEED_DEV_TIMER1]    = 0x7E782000,
    [ASPEED_DEV_UART1]     = 0x7E783000,
    [ASPEED_DEV_UART2]     = 0x7E78D000,
    [ASPEED_DEV_UART3]     = 0x7E78E000,
    [ASPEED_DEV_UART4]     = 0x7E78F000,
    [ASPEED_DEV_UART5]     = 0x7E784000,
    [ASPEED_DEV_UART6]     = 0x7E790000,
    [ASPEED_DEV_UART7]     = 0x7E790100,
    [ASPEED_DEV_UART8]     = 0x7E790200,
    [ASPEED_DEV_UART9]     = 0x7E790300,
    [ASPEED_DEV_UART10]    = 0x7E790400,
    [ASPEED_DEV_UART11]    = 0x7E790500,
    [ASPEED_DEV_UART12]    = 0x7E790600,
    [ASPEED_DEV_UART13]    = 0x7E790700,
    [ASPEED_DEV_WDT]       = 0x7E785000,
    [ASPEED_DEV_LPC]       = 0x7E789000,
    [ASPEED_DEV_PECI]      = 0x7E78B000,
    [ASPEED_DEV_I3C]       = 0x7E7A0000,
    [ASPEED_DEV_I2C]       = 0x7E7B0000,
};

static const int aspeed_soc_ast1030_irqmap[] = {
    [ASPEED_DEV_UART1]     = 47,
    [ASPEED_DEV_UART2]     = 48,
    [ASPEED_DEV_UART3]     = 49,
    [ASPEED_DEV_UART4]     = 50,
    [ASPEED_DEV_UART5]     = 8,
    [ASPEED_DEV_UART6]     = 57,
    [ASPEED_DEV_UART7]     = 58,
    [ASPEED_DEV_UART8]     = 59,
    [ASPEED_DEV_UART9]     = 60,
    [ASPEED_DEV_UART10]    = 61,
    [ASPEED_DEV_UART11]    = 62,
    [ASPEED_DEV_UART12]    = 63,
    [ASPEED_DEV_UART13]    = 64,
    [ASPEED_DEV_GPIO]      = 11,
    [ASPEED_DEV_TIMER1]    = 16,
    [ASPEED_DEV_TIMER2]    = 17,
    [ASPEED_DEV_TIMER3]    = 18,
    [ASPEED_DEV_TIMER4]    = 19,
    [ASPEED_DEV_TIMER5]    = 20,
    [ASPEED_DEV_TIMER6]    = 21,
    [ASPEED_DEV_TIMER7]    = 22,
    [ASPEED_DEV_TIMER8]    = 23,
    [ASPEED_DEV_WDT]       = 24,
    [ASPEED_DEV_LPC]       = 35,
    [ASPEED_DEV_PECI]      = 38,
    [ASPEED_DEV_FMC]       = 39,
    [ASPEED_DEV_ESPI]      = 42,
    [ASPEED_DEV_PWM]       = 44,
    [ASPEED_DEV_ADC]       = 46,
    [ASPEED_DEV_SPI1]      = 65,
    [ASPEED_DEV_SPI2]      = 66,
    [ASPEED_DEV_I3C]       = 102, /* 102 -> 105 */
    [ASPEED_DEV_I2C]       = 110, /* 110 ~ 123 */
    [ASPEED_DEV_KCS]       = 138, /* 138 -> 142 */
    [ASPEED_DEV_UDC]       = 9,
    [ASPEED_DEV_SGPIOM]    = 51,
    [ASPEED_DEV_JTAG0]     = 27,
    [ASPEED_DEV_JTAG1]     = 53,
};

static qemu_irq aspeed_soc_ast1030_get_irq(AspeedSoCState *s, int dev)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    return qdev_get_gpio_in(DEVICE(&s->armv7m), sc->irqmap[dev]);
}

static void aspeed_soc_ast1030_init(Object *obj)
{
    AspeedSoCState *s = ASPEED_SOC(obj);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    char socname[8];
    char typename[64];
    int i;

    if (sscanf(sc->name, "%7s", socname) != 1) {
        g_assert_not_reached();
    }

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);

    snprintf(typename, sizeof(typename), "aspeed.scu-%s", socname);
    object_initialize_child(obj, "scu", &s->scu, typename);
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev", sc->silicon_rev);

    object_property_add_alias(obj, "hw-strap1", OBJECT(&s->scu), "hw-strap1");
    object_property_add_alias(obj, "hw-strap2", OBJECT(&s->scu), "hw-strap2");

    snprintf(typename, sizeof(typename), "aspeed.i2c-%s", socname);
    object_initialize_child(obj, "i2c", &s->i2c, typename);

    object_initialize_child(obj, "i3c", &s->i3c, TYPE_ASPEED_I3C);

    snprintf(typename, sizeof(typename), "aspeed.timer-%s", socname);
    object_initialize_child(obj, "timerctrl", &s->timerctrl, typename);

    snprintf(typename, sizeof(typename), "aspeed.adc-%s", socname);
    object_initialize_child(obj, "adc", &s->adc, typename);

    snprintf(typename, sizeof(typename), "aspeed.fmc-%s", socname);
    object_initialize_child(obj, "fmc", &s->fmc, typename);

    for (i = 0; i < sc->spis_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.spi%d-%s", i + 1, socname);
        object_initialize_child(obj, "spi[*]", &s->spi[i], typename);
    }

    object_initialize_child(obj, "lpc", &s->lpc, TYPE_ASPEED_LPC);

    object_initialize_child(obj, "peci", &s->peci, TYPE_ASPEED_PECI);

    object_initialize_child(obj, "sbc", &s->sbc, TYPE_ASPEED_SBC);

    for (i = 0; i < sc->wdts_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.wdt-%s", socname);
        object_initialize_child(obj, "wdt[*]", &s->wdt[i], typename);
    }

    for (i = 0; i < sc->uarts_num; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i], TYPE_SERIAL_MM);
    }

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s", socname);
    object_initialize_child(obj, "gpio", &s->gpio, typename);

    snprintf(typename, sizeof(typename), "aspeed.hace-%s", socname);
    object_initialize_child(obj, "hace", &s->hace, typename);

    object_initialize_child(obj, "iomem", &s->iomem, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "sbc-unimplemented", &s->sbc_unimplemented,
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "pwm", &s->pwm, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "espi", &s->espi, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "udc", &s->udc, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "sgpiom", &s->sgpiom,
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "jtag[0]", &s->jtag[0],
                            TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "jtag[1]", &s->jtag[1],
                            TYPE_UNIMPLEMENTED_DEVICE);
}

static void aspeed_soc_ast1030_realize(DeviceState *dev_soc, Error **errp)
{
    AspeedSoCState *s = ASPEED_SOC(dev_soc);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    DeviceState *armv7m;
    Error *err = NULL;
    int i;
    g_autofree char *sram_name = NULL;

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    /* General I/O memory space to catch all unimplemented device */
    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->iomem), "aspeed.io",
                                  sc->memmap[ASPEED_DEV_IOMEM],
                                  ASPEED_SOC_IOMEM_SIZE);
    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->sbc_unimplemented),
                                  "aspeed.sbc", sc->memmap[ASPEED_DEV_SBC],
                                  0x40000);

    /* AST1030 CPU Core */
    armv7m = DEVICE(&s->armv7m);
    qdev_prop_set_uint32(armv7m, "num-irq", 256);
    qdev_prop_set_string(armv7m, "cpu-type", sc->cpu_type);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(s->memory), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), &error_abort);

    /* Internal SRAM */
    sram_name = g_strdup_printf("aspeed.sram.%d",
                                CPU(s->armv7m.cpu)->cpu_index);
    memory_region_init_ram(&s->sram, OBJECT(s), sram_name, sc->sram_size, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(s->memory,
                                sc->memmap[ASPEED_DEV_SRAM],
                                &s->sram);
    memory_region_init_ram(&s->secsram, OBJECT(s), "sec.sram",
                           sc->secsram_size, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(s->memory, sc->memmap[ASPEED_DEV_SECSRAM],
                                &s->secsram);

    /* SCU */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->scu), 0, sc->memmap[ASPEED_DEV_SCU]);

    /* I2C */

    object_property_set_link(OBJECT(&s->i2c), "dram", OBJECT(&s->sram),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->i2c), 0, sc->memmap[ASPEED_DEV_I2C]);
    for (i = 0; i < ASPEED_I2C_GET_CLASS(&s->i2c)->num_busses; i++) {
        qemu_irq irq = qdev_get_gpio_in(DEVICE(&s->armv7m),
                                        sc->irqmap[ASPEED_DEV_I2C] + i);
        /* The AST1030 I2C controller has one IRQ per bus. */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c.busses[i]), 0, irq);
    }

    /* I3C */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i3c), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->i3c), 0, sc->memmap[ASPEED_DEV_I3C]);
    for (i = 0; i < ASPEED_I3C_NR_DEVICES; i++) {
        qemu_irq irq = qdev_get_gpio_in(DEVICE(&s->armv7m),
                                        sc->irqmap[ASPEED_DEV_I3C] + i);
        /* The AST1030 I3C controller has one IRQ per bus. */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i3c.devices[i]), 0, irq);
    }

    /* PECI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->peci), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->peci), 0,
                    sc->memmap[ASPEED_DEV_PECI]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peci), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_PECI));

    /* LPC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->lpc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->lpc), 0, sc->memmap[ASPEED_DEV_LPC]);

    /* Connect the LPC IRQ to the GIC. It is otherwise unused. */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_LPC));

    /*
     * On the AST1030 LPC subdevice IRQs are connected straight to the GIC.
     */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_1,
                       qdev_get_gpio_in(DEVICE(&s->armv7m),
                                sc->irqmap[ASPEED_DEV_KCS] + aspeed_lpc_kcs_1));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_2,
                       qdev_get_gpio_in(DEVICE(&s->armv7m),
                                sc->irqmap[ASPEED_DEV_KCS] + aspeed_lpc_kcs_2));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_3,
                       qdev_get_gpio_in(DEVICE(&s->armv7m),
                                sc->irqmap[ASPEED_DEV_KCS] + aspeed_lpc_kcs_3));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_4,
                       qdev_get_gpio_in(DEVICE(&s->armv7m),
                                sc->irqmap[ASPEED_DEV_KCS] + aspeed_lpc_kcs_4));

    /* UART */
    if (!aspeed_soc_uart_realize(s, errp)) {
        return;
    }

    /* Timer */
    object_property_set_link(OBJECT(&s->timerctrl), "scu", OBJECT(&s->scu),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timerctrl), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->timerctrl), 0,
                    sc->memmap[ASPEED_DEV_TIMER1]);
    for (i = 0; i < ASPEED_TIMER_NR_TIMERS; i++) {
        qemu_irq irq = aspeed_soc_get_irq(s, ASPEED_DEV_TIMER1 + i);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timerctrl), i, irq);
    }

    /* ADC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->adc), 0, sc->memmap[ASPEED_DEV_ADC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_ADC));

    /* FMC, The number of CS is set at the board level */
    object_property_set_link(OBJECT(&s->fmc), "dram", OBJECT(&s->sram),
            &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fmc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->fmc), 0, sc->memmap[ASPEED_DEV_FMC]);
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->fmc), 1,
                    ASPEED_SMC_GET_CLASS(&s->fmc)->flash_window_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fmc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_FMC));

    /* SPI */
    for (i = 0; i < sc->spis_num; i++) {
        object_property_set_link(OBJECT(&s->spi[i]), "dram",
                                 OBJECT(&s->sram), &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->spi[i]), 0,
                        sc->memmap[ASPEED_DEV_SPI1 + i]);
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->spi[i]), 1,
                        ASPEED_SMC_GET_CLASS(&s->spi[i])->flash_window_base);
    }

    /* Secure Boot Controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sbc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->sbc), 0, sc->memmap[ASPEED_DEV_SBC]);

    /* HACE */
    object_property_set_link(OBJECT(&s->hace), "dram", OBJECT(&s->sram),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hace), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->hace), 0,
                    sc->memmap[ASPEED_DEV_HACE]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hace), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_HACE));

    /* Watch dog */
    for (i = 0; i < sc->wdts_num; i++) {
        AspeedWDTClass *awc = ASPEED_WDT_GET_CLASS(&s->wdt[i]);
        hwaddr wdt_offset = sc->memmap[ASPEED_DEV_WDT] + i * awc->iosize;

        object_property_set_link(OBJECT(&s->wdt[i]), "scu", OBJECT(&s->scu),
                                 &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->wdt[i]), 0, wdt_offset);
    }

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->gpio), 0,
                    sc->memmap[ASPEED_DEV_GPIO]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_GPIO));

    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->pwm), "aspeed.pwm",
                                  sc->memmap[ASPEED_DEV_PWM], 0x100);

    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->espi), "aspeed.espi",
                                  sc->memmap[ASPEED_DEV_ESPI], 0x800);

    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->udc), "aspeed.udc",
                                  sc->memmap[ASPEED_DEV_UDC], 0x1000);
    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->sgpiom), "aspeed.sgpiom",
                                  sc->memmap[ASPEED_DEV_SGPIOM], 0x100);

    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->jtag[0]), "aspeed.jtag",
                                  sc->memmap[ASPEED_DEV_JTAG0], 0x20);
    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->jtag[1]), "aspeed.jtag",
                                  sc->memmap[ASPEED_DEV_JTAG1], 0x20);
}

static void aspeed_soc_ast1030_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(dc);

    dc->realize = aspeed_soc_ast1030_realize;

    sc->name = "ast1030-a1";
    sc->cpu_type = ARM_CPU_TYPE_NAME("cortex-m4"); /* TODO cortex-m4f */
    sc->silicon_rev = AST1030_A1_SILICON_REV;
    sc->sram_size = 0xc0000;
    sc->secsram_size = 0x40000; /* 256 * KiB */
    sc->spis_num = 2;
    sc->ehcis_num = 0;
    sc->wdts_num = 4;
    sc->macs_num = 1;
    sc->uarts_num = 13;
    sc->irqmap = aspeed_soc_ast1030_irqmap;
    sc->memmap = aspeed_soc_ast1030_memmap;
    sc->num_cpus = 1;
    sc->get_irq = aspeed_soc_ast1030_get_irq;
}

static const TypeInfo aspeed_soc_ast1030_type_info = {
    .name          = "ast1030-a1",
    .parent        = TYPE_ASPEED_SOC,
    .instance_size = sizeof(AspeedSoCState),
    .instance_init = aspeed_soc_ast1030_init,
    .class_init    = aspeed_soc_ast1030_class_init,
    .class_size    = sizeof(AspeedSoCClass),
};

static void aspeed_soc_register_types(void)
{
    type_register_static(&aspeed_soc_ast1030_type_info);
}

type_init(aspeed_soc_register_types)
