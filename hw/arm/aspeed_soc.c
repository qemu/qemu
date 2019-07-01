/*
 * ASPEED SoC family
 *
 * Andrew Jeffery <andrew@aj.id.au>
 * Jeremy Kerr <jk@ozlabs.org>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/char/serial.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/aspeed_i2c.h"
#include "net/net.h"

#define ASPEED_SOC_UART_5_BASE      0x00184000
#define ASPEED_SOC_IOMEM_SIZE       0x00200000
#define ASPEED_SOC_IOMEM_BASE       0x1E600000
#define ASPEED_SOC_FMC_BASE         0x1E620000
#define ASPEED_SOC_SPI_BASE         0x1E630000
#define ASPEED_SOC_SPI2_BASE        0x1E631000
#define ASPEED_SOC_VIC_BASE         0x1E6C0000
#define ASPEED_SOC_SDMC_BASE        0x1E6E0000
#define ASPEED_SOC_SCU_BASE         0x1E6E2000
#define ASPEED_SOC_SRAM_BASE        0x1E720000
#define ASPEED_SOC_TIMER_BASE       0x1E782000
#define ASPEED_SOC_WDT_BASE         0x1E785000
#define ASPEED_SOC_I2C_BASE         0x1E78A000
#define ASPEED_SOC_ETH1_BASE        0x1E660000
#define ASPEED_SOC_ETH2_BASE        0x1E680000

static const int aspeed_soc_ast2400_irqmap[] = {
    [ASPEED_UART1]  = 9,
    [ASPEED_UART2]  = 32,
    [ASPEED_UART3]  = 33,
    [ASPEED_UART4]  = 34,
    [ASPEED_UART5]  = 10,
    [ASPEED_VUART]  = 8,
    [ASPEED_FMC]    = 19,
    [ASPEED_SDMC]   = 0,
    [ASPEED_SCU]    = 21,
    [ASPEED_ADC]    = 31,
    [ASPEED_GPIO]   = 20,
    [ASPEED_RTC]    = 22,
    [ASPEED_TIMER1] = 16,
    [ASPEED_TIMER2] = 17,
    [ASPEED_TIMER3] = 18,
    [ASPEED_TIMER4] = 35,
    [ASPEED_TIMER5] = 36,
    [ASPEED_TIMER6] = 37,
    [ASPEED_TIMER7] = 38,
    [ASPEED_TIMER8] = 39,
    [ASPEED_WDT]    = 27,
    [ASPEED_PWM]    = 28,
    [ASPEED_LPC]    = 8,
    [ASPEED_IBT]    = 8, /* LPC */
    [ASPEED_I2C]    = 12,
    [ASPEED_ETH1]   = 2,
    [ASPEED_ETH2]   = 3,
};

#define AST2400_SDRAM_BASE       0x40000000
#define AST2500_SDRAM_BASE       0x80000000

/* AST2500 uses the same IRQs as the AST2400 */
#define aspeed_soc_ast2500_irqmap aspeed_soc_ast2400_irqmap

static const hwaddr aspeed_soc_ast2400_spi_bases[] = { ASPEED_SOC_SPI_BASE };
static const char *aspeed_soc_ast2400_typenames[] = { "aspeed.smc.spi" };

static const hwaddr aspeed_soc_ast2500_spi_bases[] = { ASPEED_SOC_SPI_BASE,
                                                       ASPEED_SOC_SPI2_BASE};
static const char *aspeed_soc_ast2500_typenames[] = {
    "aspeed.smc.ast2500-spi1", "aspeed.smc.ast2500-spi2" };

static const AspeedSoCInfo aspeed_socs[] = {
    {
        .name         = "ast2400-a0",
        .cpu_type     = ARM_CPU_TYPE_NAME("arm926"),
        .silicon_rev  = AST2400_A0_SILICON_REV,
        .sdram_base   = AST2400_SDRAM_BASE,
        .sram_size    = 0x8000,
        .spis_num     = 1,
        .spi_bases    = aspeed_soc_ast2400_spi_bases,
        .fmc_typename = "aspeed.smc.fmc",
        .spi_typename = aspeed_soc_ast2400_typenames,
        .wdts_num     = 2,
        .irqmap       = aspeed_soc_ast2400_irqmap,
    }, {
        .name         = "ast2400-a1",
        .cpu_type     = ARM_CPU_TYPE_NAME("arm926"),
        .silicon_rev  = AST2400_A1_SILICON_REV,
        .sdram_base   = AST2400_SDRAM_BASE,
        .sram_size    = 0x8000,
        .spis_num     = 1,
        .spi_bases    = aspeed_soc_ast2400_spi_bases,
        .fmc_typename = "aspeed.smc.fmc",
        .spi_typename = aspeed_soc_ast2400_typenames,
        .wdts_num     = 2,
        .irqmap       = aspeed_soc_ast2400_irqmap,
    }, {
        .name         = "ast2400",
        .cpu_type     = ARM_CPU_TYPE_NAME("arm926"),
        .silicon_rev  = AST2400_A0_SILICON_REV,
        .sdram_base   = AST2400_SDRAM_BASE,
        .sram_size    = 0x8000,
        .spis_num     = 1,
        .spi_bases    = aspeed_soc_ast2400_spi_bases,
        .fmc_typename = "aspeed.smc.fmc",
        .spi_typename = aspeed_soc_ast2400_typenames,
        .wdts_num     = 2,
        .irqmap       = aspeed_soc_ast2400_irqmap,
    }, {
        .name         = "ast2500-a1",
        .cpu_type     = ARM_CPU_TYPE_NAME("arm1176"),
        .silicon_rev  = AST2500_A1_SILICON_REV,
        .sdram_base   = AST2500_SDRAM_BASE,
        .sram_size    = 0x9000,
        .spis_num     = 2,
        .spi_bases    = aspeed_soc_ast2500_spi_bases,
        .fmc_typename = "aspeed.smc.ast2500-fmc",
        .spi_typename = aspeed_soc_ast2500_typenames,
        .wdts_num     = 3,
        .irqmap       = aspeed_soc_ast2500_irqmap,
    },
};

static qemu_irq aspeed_soc_get_irq(AspeedSoCState *s, int ctrl)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    return qdev_get_gpio_in(DEVICE(&s->vic), sc->info->irqmap[ctrl]);
}

static void aspeed_soc_init(Object *obj)
{
    AspeedSoCState *s = ASPEED_SOC(obj);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int i;

    object_initialize_child(obj, "cpu", OBJECT(&s->cpu), sizeof(s->cpu),
                            sc->info->cpu_type, &error_abort, NULL);

    sysbus_init_child_obj(obj, "scu", OBJECT(&s->scu), sizeof(s->scu),
                          TYPE_ASPEED_SCU);
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev",
                         sc->info->silicon_rev);
    object_property_add_alias(obj, "hw-strap1", OBJECT(&s->scu),
                              "hw-strap1", &error_abort);
    object_property_add_alias(obj, "hw-strap2", OBJECT(&s->scu),
                              "hw-strap2", &error_abort);
    object_property_add_alias(obj, "hw-prot-key", OBJECT(&s->scu),
                              "hw-prot-key", &error_abort);

    sysbus_init_child_obj(obj, "vic", OBJECT(&s->vic), sizeof(s->vic),
                          TYPE_ASPEED_VIC);

    sysbus_init_child_obj(obj, "timerctrl", OBJECT(&s->timerctrl),
                          sizeof(s->timerctrl), TYPE_ASPEED_TIMER);
    object_property_add_const_link(OBJECT(&s->timerctrl), "scu",
                                   OBJECT(&s->scu), &error_abort);

    sysbus_init_child_obj(obj, "i2c", OBJECT(&s->i2c), sizeof(s->i2c),
                          TYPE_ASPEED_I2C);

    sysbus_init_child_obj(obj, "fmc", OBJECT(&s->fmc), sizeof(s->fmc),
                          sc->info->fmc_typename);
    object_property_add_alias(obj, "num-cs", OBJECT(&s->fmc), "num-cs",
                              &error_abort);

    for (i = 0; i < sc->info->spis_num; i++) {
        sysbus_init_child_obj(obj, "spi[*]", OBJECT(&s->spi[i]),
                              sizeof(s->spi[i]), sc->info->spi_typename[i]);
    }

    sysbus_init_child_obj(obj, "sdmc", OBJECT(&s->sdmc), sizeof(s->sdmc),
                          TYPE_ASPEED_SDMC);
    qdev_prop_set_uint32(DEVICE(&s->sdmc), "silicon-rev",
                         sc->info->silicon_rev);
    object_property_add_alias(obj, "ram-size", OBJECT(&s->sdmc),
                              "ram-size", &error_abort);
    object_property_add_alias(obj, "max-ram-size", OBJECT(&s->sdmc),
                              "max-ram-size", &error_abort);

    for (i = 0; i < sc->info->wdts_num; i++) {
        sysbus_init_child_obj(obj, "wdt[*]", OBJECT(&s->wdt[i]),
                              sizeof(s->wdt[i]), TYPE_ASPEED_WDT);
        qdev_prop_set_uint32(DEVICE(&s->wdt[i]), "silicon-rev",
                                    sc->info->silicon_rev);
    }

    sysbus_init_child_obj(obj, "ftgmac100", OBJECT(&s->ftgmac100),
                          sizeof(s->ftgmac100), TYPE_FTGMAC100);
}

static void aspeed_soc_realize(DeviceState *dev, Error **errp)
{
    int i;
    AspeedSoCState *s = ASPEED_SOC(dev);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    Error *err = NULL, *local_err = NULL;

    /* IO space */
    create_unimplemented_device("aspeed_soc.io",
                                ASPEED_SOC_IOMEM_BASE, ASPEED_SOC_IOMEM_SIZE);

    /* CPU */
    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "aspeed.sram",
                           sc->info->sram_size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(get_system_memory(), ASPEED_SOC_SRAM_BASE,
                                &s->sram);

    /* SCU */
    object_property_set_bool(OBJECT(&s->scu), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->scu), 0, ASPEED_SOC_SCU_BASE);

    /* VIC */
    object_property_set_bool(OBJECT(&s->vic), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->vic), 0, ASPEED_SOC_VIC_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic), 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));

    /* Timer */
    object_property_set_bool(OBJECT(&s->timerctrl), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timerctrl), 0, ASPEED_SOC_TIMER_BASE);
    for (i = 0; i < ASPEED_TIMER_NR_TIMERS; i++) {
        qemu_irq irq = aspeed_soc_get_irq(s, ASPEED_TIMER1 + i);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timerctrl), i, irq);
    }

    /* UART - attach an 8250 to the IO space as our UART5 */
    if (serial_hd(0)) {
        qemu_irq uart5 = aspeed_soc_get_irq(s, ASPEED_UART5);
        serial_mm_init(get_system_memory(),
                       ASPEED_SOC_IOMEM_BASE + ASPEED_SOC_UART_5_BASE, 2,
                       uart5, 38400, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    }

    /* I2C */
    object_property_set_bool(OBJECT(&s->i2c), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c), 0, ASPEED_SOC_I2C_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c), 0,
                       aspeed_soc_get_irq(s, ASPEED_I2C));

    /* FMC, The number of CS is set at the board level */
    object_property_set_bool(OBJECT(&s->fmc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fmc), 0, ASPEED_SOC_FMC_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fmc), 1,
                    s->fmc.ctrl->flash_window_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fmc), 0,
                       aspeed_soc_get_irq(s, ASPEED_FMC));

    /* SPI */
    for (i = 0; i < sc->info->spis_num; i++) {
        object_property_set_int(OBJECT(&s->spi[i]), 1, "num-cs", &err);
        object_property_set_bool(OBJECT(&s->spi[i]), true, "realized",
                                 &local_err);
        error_propagate(&err, local_err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0, sc->info->spi_bases[i]);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 1,
                        s->spi[i].ctrl->flash_window_base);
    }

    /* SDMC - SDRAM Memory Controller */
    object_property_set_bool(OBJECT(&s->sdmc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdmc), 0, ASPEED_SOC_SDMC_BASE);

    /* Watch dog */
    for (i = 0; i < sc->info->wdts_num; i++) {
        object_property_set_bool(OBJECT(&s->wdt[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                        ASPEED_SOC_WDT_BASE + i * 0x20);
    }

    /* Net */
    qdev_set_nic_properties(DEVICE(&s->ftgmac100), &nd_table[0]);
    object_property_set_bool(OBJECT(&s->ftgmac100), true, "aspeed", &err);
    object_property_set_bool(OBJECT(&s->ftgmac100), true, "realized",
                             &local_err);
    error_propagate(&err, local_err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ftgmac100), 0, ASPEED_SOC_ETH1_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ftgmac100), 0,
                       aspeed_soc_get_irq(s, ASPEED_ETH1));
}

static void aspeed_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(oc);

    sc->info = (AspeedSoCInfo *) data;
    dc->realize = aspeed_soc_realize;
    /* Reason: Uses serial_hds and nd_table in realize() directly */
    dc->user_creatable = false;
}

static const TypeInfo aspeed_soc_type_info = {
    .name           = TYPE_ASPEED_SOC,
    .parent         = TYPE_DEVICE,
    .instance_init  = aspeed_soc_init,
    .instance_size  = sizeof(AspeedSoCState),
    .class_size     = sizeof(AspeedSoCClass),
    .abstract       = true,
};

static void aspeed_soc_register_types(void)
{
    int i;

    type_register_static(&aspeed_soc_type_info);
    for (i = 0; i < ARRAY_SIZE(aspeed_socs); ++i) {
        TypeInfo ti = {
            .name       = aspeed_socs[i].name,
            .parent     = TYPE_ASPEED_SOC,
            .class_init = aspeed_soc_class_init,
            .class_data = (void *) &aspeed_socs[i],
        };
        type_register(&ti);
    }
}

type_init(aspeed_soc_register_types)
