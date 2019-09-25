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
#include "qemu/error-report.h"
#include "hw/i2c/aspeed_i2c.h"
#include "net/net.h"
#include "sysemu/sysemu.h"

#define ASPEED_SOC_IOMEM_SIZE       0x00200000

static const hwaddr aspeed_soc_ast2400_memmap[] = {
    [ASPEED_IOMEM]  = 0x1E600000,
    [ASPEED_FMC]    = 0x1E620000,
    [ASPEED_SPI1]   = 0x1E630000,
    [ASPEED_VIC]    = 0x1E6C0000,
    [ASPEED_SDMC]   = 0x1E6E0000,
    [ASPEED_SCU]    = 0x1E6E2000,
    [ASPEED_XDMA]   = 0x1E6E7000,
    [ASPEED_VIDEO]  = 0x1E700000,
    [ASPEED_ADC]    = 0x1E6E9000,
    [ASPEED_SRAM]   = 0x1E720000,
    [ASPEED_SDHCI]  = 0x1E740000,
    [ASPEED_GPIO]   = 0x1E780000,
    [ASPEED_RTC]    = 0x1E781000,
    [ASPEED_TIMER1] = 0x1E782000,
    [ASPEED_WDT]    = 0x1E785000,
    [ASPEED_PWM]    = 0x1E786000,
    [ASPEED_LPC]    = 0x1E789000,
    [ASPEED_IBT]    = 0x1E789140,
    [ASPEED_I2C]    = 0x1E78A000,
    [ASPEED_ETH1]   = 0x1E660000,
    [ASPEED_ETH2]   = 0x1E680000,
    [ASPEED_UART1]  = 0x1E783000,
    [ASPEED_UART5]  = 0x1E784000,
    [ASPEED_VUART]  = 0x1E787000,
    [ASPEED_SDRAM]  = 0x40000000,
};

static const hwaddr aspeed_soc_ast2500_memmap[] = {
    [ASPEED_IOMEM]  = 0x1E600000,
    [ASPEED_FMC]    = 0x1E620000,
    [ASPEED_SPI1]   = 0x1E630000,
    [ASPEED_SPI2]   = 0x1E631000,
    [ASPEED_VIC]    = 0x1E6C0000,
    [ASPEED_SDMC]   = 0x1E6E0000,
    [ASPEED_SCU]    = 0x1E6E2000,
    [ASPEED_XDMA]   = 0x1E6E7000,
    [ASPEED_ADC]    = 0x1E6E9000,
    [ASPEED_VIDEO]  = 0x1E700000,
    [ASPEED_SRAM]   = 0x1E720000,
    [ASPEED_SDHCI]  = 0x1E740000,
    [ASPEED_GPIO]   = 0x1E780000,
    [ASPEED_RTC]    = 0x1E781000,
    [ASPEED_TIMER1] = 0x1E782000,
    [ASPEED_WDT]    = 0x1E785000,
    [ASPEED_PWM]    = 0x1E786000,
    [ASPEED_LPC]    = 0x1E789000,
    [ASPEED_IBT]    = 0x1E789140,
    [ASPEED_I2C]    = 0x1E78A000,
    [ASPEED_ETH1]   = 0x1E660000,
    [ASPEED_ETH2]   = 0x1E680000,
    [ASPEED_UART1]  = 0x1E783000,
    [ASPEED_UART5]  = 0x1E784000,
    [ASPEED_VUART]  = 0x1E787000,
    [ASPEED_SDRAM]  = 0x80000000,
};

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
    [ASPEED_XDMA]   = 6,
    [ASPEED_SDHCI]  = 26,
};

#define aspeed_soc_ast2500_irqmap aspeed_soc_ast2400_irqmap

static qemu_irq aspeed_soc_get_irq(AspeedSoCState *s, int ctrl)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    return qdev_get_gpio_in(DEVICE(&s->vic), sc->irqmap[ctrl]);
}

static void aspeed_soc_init(Object *obj)
{
    AspeedSoCState *s = ASPEED_SOC(obj);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int i;
    char socname[8];
    char typename[64];

    if (sscanf(sc->name, "%7s", socname) != 1) {
        g_assert_not_reached();
    }

    for (i = 0; i < sc->num_cpus; i++) {
        object_initialize_child(obj, "cpu[*]", OBJECT(&s->cpu[i]),
                                sizeof(s->cpu[i]), sc->cpu_type,
                                &error_abort, NULL);
    }

    snprintf(typename, sizeof(typename), "aspeed.scu-%s", socname);
    sysbus_init_child_obj(obj, "scu", OBJECT(&s->scu), sizeof(s->scu),
                          typename);
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev",
                         sc->silicon_rev);
    object_property_add_alias(obj, "hw-strap1", OBJECT(&s->scu),
                              "hw-strap1", &error_abort);
    object_property_add_alias(obj, "hw-strap2", OBJECT(&s->scu),
                              "hw-strap2", &error_abort);
    object_property_add_alias(obj, "hw-prot-key", OBJECT(&s->scu),
                              "hw-prot-key", &error_abort);

    sysbus_init_child_obj(obj, "vic", OBJECT(&s->vic), sizeof(s->vic),
                          TYPE_ASPEED_VIC);

    sysbus_init_child_obj(obj, "rtc", OBJECT(&s->rtc), sizeof(s->rtc),
                          TYPE_ASPEED_RTC);

    snprintf(typename, sizeof(typename), "aspeed.timer-%s", socname);
    sysbus_init_child_obj(obj, "timerctrl", OBJECT(&s->timerctrl),
                          sizeof(s->timerctrl), typename);
    object_property_add_const_link(OBJECT(&s->timerctrl), "scu",
                                   OBJECT(&s->scu), &error_abort);

    snprintf(typename, sizeof(typename), "aspeed.i2c-%s", socname);
    sysbus_init_child_obj(obj, "i2c", OBJECT(&s->i2c), sizeof(s->i2c),
                          typename);

    snprintf(typename, sizeof(typename), "aspeed.fmc-%s", socname);
    sysbus_init_child_obj(obj, "fmc", OBJECT(&s->fmc), sizeof(s->fmc),
                          typename);
    object_property_add_alias(obj, "num-cs", OBJECT(&s->fmc), "num-cs",
                              &error_abort);
    object_property_add_alias(obj, "dram", OBJECT(&s->fmc), "dram",
                              &error_abort);

    for (i = 0; i < sc->spis_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.spi%d-%s", i + 1, socname);
        sysbus_init_child_obj(obj, "spi[*]", OBJECT(&s->spi[i]),
                              sizeof(s->spi[i]), typename);
    }

    snprintf(typename, sizeof(typename), "aspeed.sdmc-%s", socname);
    sysbus_init_child_obj(obj, "sdmc", OBJECT(&s->sdmc), sizeof(s->sdmc),
                          typename);
    object_property_add_alias(obj, "ram-size", OBJECT(&s->sdmc),
                              "ram-size", &error_abort);
    object_property_add_alias(obj, "max-ram-size", OBJECT(&s->sdmc),
                              "max-ram-size", &error_abort);

    for (i = 0; i < sc->wdts_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.wdt-%s", socname);
        sysbus_init_child_obj(obj, "wdt[*]", OBJECT(&s->wdt[i]),
                              sizeof(s->wdt[i]), typename);
        object_property_add_const_link(OBJECT(&s->wdt[i]), "scu",
                                       OBJECT(&s->scu), &error_abort);
    }

    for (i = 0; i < sc->macs_num; i++) {
        sysbus_init_child_obj(obj, "ftgmac100[*]", OBJECT(&s->ftgmac100[i]),
                              sizeof(s->ftgmac100[i]), TYPE_FTGMAC100);
    }

    sysbus_init_child_obj(obj, "xdma", OBJECT(&s->xdma), sizeof(s->xdma),
                          TYPE_ASPEED_XDMA);

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s", socname);
    sysbus_init_child_obj(obj, "gpio", OBJECT(&s->gpio), sizeof(s->gpio),
                          typename);

    sysbus_init_child_obj(obj, "sdc", OBJECT(&s->sdhci), sizeof(s->sdhci),
                          TYPE_ASPEED_SDHCI);

    /* Init sd card slot class here so that they're under the correct parent */
    for (i = 0; i < ASPEED_SDHCI_NUM_SLOTS; ++i) {
        sysbus_init_child_obj(obj, "sdhci[*]", OBJECT(&s->sdhci.slots[i]),
                              sizeof(s->sdhci.slots[i]), TYPE_SYSBUS_SDHCI);
    }
}

static void aspeed_soc_realize(DeviceState *dev, Error **errp)
{
    int i;
    AspeedSoCState *s = ASPEED_SOC(dev);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    Error *err = NULL, *local_err = NULL;

    /* IO space */
    create_unimplemented_device("aspeed_soc.io", sc->memmap[ASPEED_IOMEM],
                                ASPEED_SOC_IOMEM_SIZE);

    /* Video engine stub */
    create_unimplemented_device("aspeed.video", sc->memmap[ASPEED_VIDEO],
                                0x1000);

    if (s->num_cpus > sc->num_cpus) {
        warn_report("%s: invalid number of CPUs %d, using default %d",
                    sc->name, s->num_cpus, sc->num_cpus);
        s->num_cpus = sc->num_cpus;
    }

    /* CPU */
    for (i = 0; i < s->num_cpus; i++) {
        object_property_set_bool(OBJECT(&s->cpu[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "aspeed.sram",
                           sc->sram_size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(get_system_memory(),
                                sc->memmap[ASPEED_SRAM], &s->sram);

    /* SCU */
    object_property_set_bool(OBJECT(&s->scu), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->scu), 0, sc->memmap[ASPEED_SCU]);

    /* VIC */
    object_property_set_bool(OBJECT(&s->vic), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->vic), 0, sc->memmap[ASPEED_VIC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic), 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));

    /* RTC */
    object_property_set_bool(OBJECT(&s->rtc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rtc), 0, sc->memmap[ASPEED_RTC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0,
                       aspeed_soc_get_irq(s, ASPEED_RTC));

    /* Timer */
    object_property_set_bool(OBJECT(&s->timerctrl), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timerctrl), 0,
                    sc->memmap[ASPEED_TIMER1]);
    for (i = 0; i < ASPEED_TIMER_NR_TIMERS; i++) {
        qemu_irq irq = aspeed_soc_get_irq(s, ASPEED_TIMER1 + i);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timerctrl), i, irq);
    }

    /* UART - attach an 8250 to the IO space as our UART5 */
    if (serial_hd(0)) {
        qemu_irq uart5 = aspeed_soc_get_irq(s, ASPEED_UART5);
        serial_mm_init(get_system_memory(), sc->memmap[ASPEED_UART5], 2,
                       uart5, 38400, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    }

    /* I2C */
    object_property_set_bool(OBJECT(&s->i2c), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c), 0, sc->memmap[ASPEED_I2C]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c), 0,
                       aspeed_soc_get_irq(s, ASPEED_I2C));

    /* FMC, The number of CS is set at the board level */
    object_property_set_int(OBJECT(&s->fmc), sc->memmap[ASPEED_SDRAM],
                            "sdram-base", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->fmc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fmc), 0, sc->memmap[ASPEED_FMC]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fmc), 1,
                    s->fmc.ctrl->flash_window_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fmc), 0,
                       aspeed_soc_get_irq(s, ASPEED_FMC));

    /* SPI */
    for (i = 0; i < sc->spis_num; i++) {
        object_property_set_int(OBJECT(&s->spi[i]), 1, "num-cs", &err);
        object_property_set_bool(OBJECT(&s->spi[i]), true, "realized",
                                 &local_err);
        error_propagate(&err, local_err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0,
                        sc->memmap[ASPEED_SPI1 + i]);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 1,
                        s->spi[i].ctrl->flash_window_base);
    }

    /* SDMC - SDRAM Memory Controller */
    object_property_set_bool(OBJECT(&s->sdmc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdmc), 0, sc->memmap[ASPEED_SDMC]);

    /* Watch dog */
    for (i = 0; i < sc->wdts_num; i++) {
        AspeedWDTClass *awc = ASPEED_WDT_GET_CLASS(&s->wdt[i]);

        object_property_set_bool(OBJECT(&s->wdt[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                        sc->memmap[ASPEED_WDT] + i * awc->offset);
    }

    /* Net */
    for (i = 0; i < nb_nics && i < sc->macs_num; i++) {
        qdev_set_nic_properties(DEVICE(&s->ftgmac100[i]), &nd_table[i]);
        object_property_set_bool(OBJECT(&s->ftgmac100[i]), true, "aspeed",
                                 &err);
        object_property_set_bool(OBJECT(&s->ftgmac100[i]), true, "realized",
                                 &local_err);
        error_propagate(&err, local_err);
        if (err) {
            error_propagate(errp, err);
           return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                        sc->memmap[ASPEED_ETH1 + i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                           aspeed_soc_get_irq(s, ASPEED_ETH1 + i));
    }

    /* XDMA */
    object_property_set_bool(OBJECT(&s->xdma), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xdma), 0,
                    sc->memmap[ASPEED_XDMA]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->xdma), 0,
                       aspeed_soc_get_irq(s, ASPEED_XDMA));

    /* GPIO */
    object_property_set_bool(OBJECT(&s->gpio), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, sc->memmap[ASPEED_GPIO]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), 0,
                       aspeed_soc_get_irq(s, ASPEED_GPIO));

    /* SDHCI */
    object_property_set_bool(OBJECT(&s->sdhci), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdhci), 0,
                    sc->memmap[ASPEED_SDHCI]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci), 0,
                       aspeed_soc_get_irq(s, ASPEED_SDHCI));
}
static Property aspeed_soc_properties[] = {
    DEFINE_PROP_UINT32("num-cpus", AspeedSoCState, num_cpus, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aspeed_soc_realize;
    /* Reason: Uses serial_hds and nd_table in realize() directly */
    dc->user_creatable = false;
    dc->props = aspeed_soc_properties;
}

static const TypeInfo aspeed_soc_type_info = {
    .name           = TYPE_ASPEED_SOC,
    .parent         = TYPE_DEVICE,
    .instance_size  = sizeof(AspeedSoCState),
    .class_size     = sizeof(AspeedSoCClass),
    .class_init     = aspeed_soc_class_init,
    .abstract       = true,
};

static void aspeed_soc_ast2400_class_init(ObjectClass *oc, void *data)
{
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(oc);

    sc->name         = "ast2400-a1";
    sc->cpu_type     = ARM_CPU_TYPE_NAME("arm926");
    sc->silicon_rev  = AST2400_A1_SILICON_REV;
    sc->sram_size    = 0x8000;
    sc->spis_num     = 1;
    sc->wdts_num     = 2;
    sc->macs_num     = 2;
    sc->irqmap       = aspeed_soc_ast2400_irqmap;
    sc->memmap       = aspeed_soc_ast2400_memmap;
    sc->num_cpus     = 1;
}

static const TypeInfo aspeed_soc_ast2400_type_info = {
    .name           = "ast2400-a1",
    .parent         = TYPE_ASPEED_SOC,
    .instance_init  = aspeed_soc_init,
    .instance_size  = sizeof(AspeedSoCState),
    .class_init     = aspeed_soc_ast2400_class_init,
};

static void aspeed_soc_ast2500_class_init(ObjectClass *oc, void *data)
{
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(oc);

    sc->name         = "ast2500-a1";
    sc->cpu_type     = ARM_CPU_TYPE_NAME("arm1176");
    sc->silicon_rev  = AST2500_A1_SILICON_REV;
    sc->sram_size    = 0x9000;
    sc->spis_num     = 2;
    sc->wdts_num     = 3;
    sc->macs_num     = 2;
    sc->irqmap       = aspeed_soc_ast2500_irqmap;
    sc->memmap       = aspeed_soc_ast2500_memmap;
    sc->num_cpus     = 1;
}

static const TypeInfo aspeed_soc_ast2500_type_info = {
    .name           = "ast2500-a1",
    .parent         = TYPE_ASPEED_SOC,
    .instance_init  = aspeed_soc_init,
    .instance_size  = sizeof(AspeedSoCState),
    .class_init     = aspeed_soc_ast2500_class_init,
};
static void aspeed_soc_register_types(void)
{
    type_register_static(&aspeed_soc_type_info);
    type_register_static(&aspeed_soc_ast2400_type_info);
    type_register_static(&aspeed_soc_ast2500_type_info);
};

type_init(aspeed_soc_register_types)
