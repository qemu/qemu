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
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/misc/unimp.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/char/serial.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/i2c/aspeed_i2c.h"
#include "net/net.h"
#include "sysemu/sysemu.h"

#define ASPEED_SOC_IOMEM_SIZE       0x00200000

static const hwaddr aspeed_soc_ast2400_memmap[] = {
    [ASPEED_DEV_IOMEM]  = 0x1E600000,
    [ASPEED_DEV_FMC]    = 0x1E620000,
    [ASPEED_DEV_SPI1]   = 0x1E630000,
    [ASPEED_DEV_EHCI1]  = 0x1E6A1000,
    [ASPEED_DEV_VIC]    = 0x1E6C0000,
    [ASPEED_DEV_SDMC]   = 0x1E6E0000,
    [ASPEED_DEV_SCU]    = 0x1E6E2000,
    [ASPEED_DEV_HACE]   = 0x1E6E3000,
    [ASPEED_DEV_XDMA]   = 0x1E6E7000,
    [ASPEED_DEV_VIDEO]  = 0x1E700000,
    [ASPEED_DEV_ADC]    = 0x1E6E9000,
    [ASPEED_DEV_SRAM]   = 0x1E720000,
    [ASPEED_DEV_SDHCI]  = 0x1E740000,
    [ASPEED_DEV_GPIO]   = 0x1E780000,
    [ASPEED_DEV_RTC]    = 0x1E781000,
    [ASPEED_DEV_TIMER1] = 0x1E782000,
    [ASPEED_DEV_WDT]    = 0x1E785000,
    [ASPEED_DEV_PWM]    = 0x1E786000,
    [ASPEED_DEV_LPC]    = 0x1E789000,
    [ASPEED_DEV_IBT]    = 0x1E789140,
    [ASPEED_DEV_I2C]    = 0x1E78A000,
    [ASPEED_DEV_PECI]   = 0x1E78B000,
    [ASPEED_DEV_ETH1]   = 0x1E660000,
    [ASPEED_DEV_ETH2]   = 0x1E680000,
    [ASPEED_DEV_UART1]  = 0x1E783000,
    [ASPEED_DEV_UART2]  = 0x1E78D000,
    [ASPEED_DEV_UART3]  = 0x1E78E000,
    [ASPEED_DEV_UART4]  = 0x1E78F000,
    [ASPEED_DEV_UART5]  = 0x1E784000,
    [ASPEED_DEV_VUART]  = 0x1E787000,
    [ASPEED_DEV_SDRAM]  = 0x40000000,
};

static const hwaddr aspeed_soc_ast2500_memmap[] = {
    [ASPEED_DEV_IOMEM]  = 0x1E600000,
    [ASPEED_DEV_FMC]    = 0x1E620000,
    [ASPEED_DEV_SPI1]   = 0x1E630000,
    [ASPEED_DEV_SPI2]   = 0x1E631000,
    [ASPEED_DEV_EHCI1]  = 0x1E6A1000,
    [ASPEED_DEV_EHCI2]  = 0x1E6A3000,
    [ASPEED_DEV_VIC]    = 0x1E6C0000,
    [ASPEED_DEV_SDMC]   = 0x1E6E0000,
    [ASPEED_DEV_SCU]    = 0x1E6E2000,
    [ASPEED_DEV_HACE]   = 0x1E6E3000,
    [ASPEED_DEV_XDMA]   = 0x1E6E7000,
    [ASPEED_DEV_ADC]    = 0x1E6E9000,
    [ASPEED_DEV_VIDEO]  = 0x1E700000,
    [ASPEED_DEV_SRAM]   = 0x1E720000,
    [ASPEED_DEV_SDHCI]  = 0x1E740000,
    [ASPEED_DEV_GPIO]   = 0x1E780000,
    [ASPEED_DEV_RTC]    = 0x1E781000,
    [ASPEED_DEV_TIMER1] = 0x1E782000,
    [ASPEED_DEV_WDT]    = 0x1E785000,
    [ASPEED_DEV_PWM]    = 0x1E786000,
    [ASPEED_DEV_LPC]    = 0x1E789000,
    [ASPEED_DEV_IBT]    = 0x1E789140,
    [ASPEED_DEV_I2C]    = 0x1E78A000,
    [ASPEED_DEV_PECI]   = 0x1E78B000,
    [ASPEED_DEV_ETH1]   = 0x1E660000,
    [ASPEED_DEV_ETH2]   = 0x1E680000,
    [ASPEED_DEV_UART1]  = 0x1E783000,
    [ASPEED_DEV_UART2]  = 0x1E78D000,
    [ASPEED_DEV_UART3]  = 0x1E78E000,
    [ASPEED_DEV_UART4]  = 0x1E78F000,
    [ASPEED_DEV_UART5]  = 0x1E784000,
    [ASPEED_DEV_VUART]  = 0x1E787000,
    [ASPEED_DEV_SDRAM]  = 0x80000000,
};

static const int aspeed_soc_ast2400_irqmap[] = {
    [ASPEED_DEV_UART1]  = 9,
    [ASPEED_DEV_UART2]  = 32,
    [ASPEED_DEV_UART3]  = 33,
    [ASPEED_DEV_UART4]  = 34,
    [ASPEED_DEV_UART5]  = 10,
    [ASPEED_DEV_VUART]  = 8,
    [ASPEED_DEV_FMC]    = 19,
    [ASPEED_DEV_EHCI1]  = 5,
    [ASPEED_DEV_EHCI2]  = 13,
    [ASPEED_DEV_SDMC]   = 0,
    [ASPEED_DEV_SCU]    = 21,
    [ASPEED_DEV_ADC]    = 31,
    [ASPEED_DEV_GPIO]   = 20,
    [ASPEED_DEV_RTC]    = 22,
    [ASPEED_DEV_TIMER1] = 16,
    [ASPEED_DEV_TIMER2] = 17,
    [ASPEED_DEV_TIMER3] = 18,
    [ASPEED_DEV_TIMER4] = 35,
    [ASPEED_DEV_TIMER5] = 36,
    [ASPEED_DEV_TIMER6] = 37,
    [ASPEED_DEV_TIMER7] = 38,
    [ASPEED_DEV_TIMER8] = 39,
    [ASPEED_DEV_WDT]    = 27,
    [ASPEED_DEV_PWM]    = 28,
    [ASPEED_DEV_LPC]    = 8,
    [ASPEED_DEV_I2C]    = 12,
    [ASPEED_DEV_PECI]   = 15,
    [ASPEED_DEV_ETH1]   = 2,
    [ASPEED_DEV_ETH2]   = 3,
    [ASPEED_DEV_XDMA]   = 6,
    [ASPEED_DEV_SDHCI]  = 26,
    [ASPEED_DEV_HACE]   = 4,
};

#define aspeed_soc_ast2500_irqmap aspeed_soc_ast2400_irqmap

static qemu_irq aspeed_soc_ast2400_get_irq(AspeedSoCState *s, int dev)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    return qdev_get_gpio_in(DEVICE(&s->vic), sc->irqmap[dev]);
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
        object_initialize_child(obj, "cpu[*]", &s->cpu[i], sc->cpu_type);
    }

    snprintf(typename, sizeof(typename), "aspeed.scu-%s", socname);
    object_initialize_child(obj, "scu", &s->scu, typename);
    qdev_prop_set_uint32(DEVICE(&s->scu), "silicon-rev",
                         sc->silicon_rev);
    object_property_add_alias(obj, "hw-strap1", OBJECT(&s->scu),
                              "hw-strap1");
    object_property_add_alias(obj, "hw-strap2", OBJECT(&s->scu),
                              "hw-strap2");
    object_property_add_alias(obj, "hw-prot-key", OBJECT(&s->scu),
                              "hw-prot-key");

    object_initialize_child(obj, "vic", &s->vic, TYPE_ASPEED_VIC);

    object_initialize_child(obj, "rtc", &s->rtc, TYPE_ASPEED_RTC);

    snprintf(typename, sizeof(typename), "aspeed.timer-%s", socname);
    object_initialize_child(obj, "timerctrl", &s->timerctrl, typename);

    snprintf(typename, sizeof(typename), "aspeed.adc-%s", socname);
    object_initialize_child(obj, "adc", &s->adc, typename);

    snprintf(typename, sizeof(typename), "aspeed.i2c-%s", socname);
    object_initialize_child(obj, "i2c", &s->i2c, typename);

    object_initialize_child(obj, "peci", &s->peci, TYPE_ASPEED_PECI);

    snprintf(typename, sizeof(typename), "aspeed.fmc-%s", socname);
    object_initialize_child(obj, "fmc", &s->fmc, typename);

    for (i = 0; i < sc->spis_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.spi%d-%s", i + 1, socname);
        object_initialize_child(obj, "spi[*]", &s->spi[i], typename);
    }

    for (i = 0; i < sc->ehcis_num; i++) {
        object_initialize_child(obj, "ehci[*]", &s->ehci[i],
                                TYPE_PLATFORM_EHCI);
    }

    snprintf(typename, sizeof(typename), "aspeed.sdmc-%s", socname);
    object_initialize_child(obj, "sdmc", &s->sdmc, typename);
    object_property_add_alias(obj, "ram-size", OBJECT(&s->sdmc),
                              "ram-size");

    for (i = 0; i < sc->wdts_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.wdt-%s", socname);
        object_initialize_child(obj, "wdt[*]", &s->wdt[i], typename);
    }

    for (i = 0; i < sc->macs_num; i++) {
        object_initialize_child(obj, "ftgmac100[*]", &s->ftgmac100[i],
                                TYPE_FTGMAC100);
    }

    for (i = 0; i < sc->uarts_num; i++) {
        object_initialize_child(obj, "uart[*]", &s->uart[i], TYPE_SERIAL_MM);
    }

    snprintf(typename, sizeof(typename), TYPE_ASPEED_XDMA "-%s", socname);
    object_initialize_child(obj, "xdma", &s->xdma, typename);

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s", socname);
    object_initialize_child(obj, "gpio", &s->gpio, typename);

    object_initialize_child(obj, "sdc", &s->sdhci, TYPE_ASPEED_SDHCI);

    object_property_set_int(OBJECT(&s->sdhci), "num-slots", 2, &error_abort);

    /* Init sd card slot class here so that they're under the correct parent */
    for (i = 0; i < ASPEED_SDHCI_NUM_SLOTS; ++i) {
        object_initialize_child(obj, "sdhci[*]", &s->sdhci.slots[i],
                                TYPE_SYSBUS_SDHCI);
    }

    object_initialize_child(obj, "lpc", &s->lpc, TYPE_ASPEED_LPC);

    snprintf(typename, sizeof(typename), "aspeed.hace-%s", socname);
    object_initialize_child(obj, "hace", &s->hace, typename);

    object_initialize_child(obj, "iomem", &s->iomem, TYPE_UNIMPLEMENTED_DEVICE);
    object_initialize_child(obj, "video", &s->video, TYPE_UNIMPLEMENTED_DEVICE);
}

static void aspeed_soc_realize(DeviceState *dev, Error **errp)
{
    int i;
    AspeedSoCState *s = ASPEED_SOC(dev);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    Error *err = NULL;
    g_autofree char *sram_name = NULL;

    /* IO space */
    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->iomem), "aspeed.io",
                                  sc->memmap[ASPEED_DEV_IOMEM],
                                  ASPEED_SOC_IOMEM_SIZE);

    /* Video engine stub */
    aspeed_mmio_map_unimplemented(s, SYS_BUS_DEVICE(&s->video), "aspeed.video",
                                  sc->memmap[ASPEED_DEV_VIDEO], 0x1000);

    /* CPU */
    for (i = 0; i < sc->num_cpus; i++) {
        object_property_set_link(OBJECT(&s->cpu[i]), "memory",
                                 OBJECT(s->memory), &error_abort);
        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* SRAM */
    sram_name = g_strdup_printf("aspeed.sram.%d", CPU(&s->cpu[0])->cpu_index);
    memory_region_init_ram(&s->sram, OBJECT(s), sram_name, sc->sram_size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(s->memory,
                                sc->memmap[ASPEED_DEV_SRAM], &s->sram);

    /* SCU */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->scu), 0, sc->memmap[ASPEED_DEV_SCU]);

    /* VIC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->vic), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->vic), 0, sc->memmap[ASPEED_DEV_VIC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic), 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));

    /* RTC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->rtc), 0, sc->memmap[ASPEED_DEV_RTC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_RTC));

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

    /* UART */
    if (!aspeed_soc_uart_realize(s, errp)) {
        return;
    }

    /* I2C */
    object_property_set_link(OBJECT(&s->i2c), "dram", OBJECT(s->dram_mr),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->i2c), 0, sc->memmap[ASPEED_DEV_I2C]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_I2C));

    /* PECI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->peci), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->peci), 0,
                    sc->memmap[ASPEED_DEV_PECI]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peci), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_PECI));

    /* FMC, The number of CS is set at the board level */
    object_property_set_link(OBJECT(&s->fmc), "dram", OBJECT(s->dram_mr),
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
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->spi[i]), 0,
                        sc->memmap[ASPEED_DEV_SPI1 + i]);
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->spi[i]), 1,
                        ASPEED_SMC_GET_CLASS(&s->spi[i])->flash_window_base);
    }

    /* EHCI */
    for (i = 0; i < sc->ehcis_num; i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->ehci[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->ehci[i]), 0,
                        sc->memmap[ASPEED_DEV_EHCI1 + i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                           aspeed_soc_get_irq(s, ASPEED_DEV_EHCI1 + i));
    }

    /* SDMC - SDRAM Memory Controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdmc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->sdmc), 0,
                    sc->memmap[ASPEED_DEV_SDMC]);

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

    /* RAM  */
    if (!aspeed_soc_dram_init(s, errp)) {
        return;
    }

    /* Net */
    for (i = 0; i < sc->macs_num; i++) {
        object_property_set_bool(OBJECT(&s->ftgmac100[i]), "aspeed", true,
                                 &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->ftgmac100[i]), errp)) {
            return;
        }
        aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                        sc->memmap[ASPEED_DEV_ETH1 + i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                           aspeed_soc_get_irq(s, ASPEED_DEV_ETH1 + i));
    }

    /* XDMA */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xdma), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->xdma), 0,
                    sc->memmap[ASPEED_DEV_XDMA]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->xdma), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_XDMA));

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->gpio), 0,
                    sc->memmap[ASPEED_DEV_GPIO]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_GPIO));

    /* SDHCI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdhci), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->sdhci), 0,
                    sc->memmap[ASPEED_DEV_SDHCI]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_SDHCI));

    /* LPC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->lpc), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->lpc), 0, sc->memmap[ASPEED_DEV_LPC]);

    /* Connect the LPC IRQ to the VIC */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_LPC));

    /*
     * On the AST2400 and AST2500 the one LPC IRQ is shared between all of the
     * subdevices. Connect the LPC subdevice IRQs to the LPC controller IRQ (by
     * contrast, on the AST2600, the subdevice IRQs are connected straight to
     * the GIC).
     *
     * LPC subdevice IRQ sources are offset from 1 because the shared IRQ output
     * to the VIC is at offset 0.
     */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_1,
                       qdev_get_gpio_in(DEVICE(&s->lpc), aspeed_lpc_kcs_1));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_2,
                       qdev_get_gpio_in(DEVICE(&s->lpc), aspeed_lpc_kcs_2));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_3,
                       qdev_get_gpio_in(DEVICE(&s->lpc), aspeed_lpc_kcs_3));

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->lpc), 1 + aspeed_lpc_kcs_4,
                       qdev_get_gpio_in(DEVICE(&s->lpc), aspeed_lpc_kcs_4));

    /* HACE */
    object_property_set_link(OBJECT(&s->hace), "dram", OBJECT(s->dram_mr),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->hace), errp)) {
        return;
    }
    aspeed_mmio_map(s, SYS_BUS_DEVICE(&s->hace), 0,
                    sc->memmap[ASPEED_DEV_HACE]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->hace), 0,
                       aspeed_soc_get_irq(s, ASPEED_DEV_HACE));
}
static Property aspeed_soc_properties[] = {
    DEFINE_PROP_LINK("memory", AspeedSoCState, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("dram", AspeedSoCState, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aspeed_soc_realize;
    /* Reason: Uses serial_hds and nd_table in realize() directly */
    dc->user_creatable = false;
    device_class_set_props(dc, aspeed_soc_properties);
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
    sc->ehcis_num    = 1;
    sc->wdts_num     = 2;
    sc->macs_num     = 2;
    sc->uarts_num    = 5;
    sc->irqmap       = aspeed_soc_ast2400_irqmap;
    sc->memmap       = aspeed_soc_ast2400_memmap;
    sc->num_cpus     = 1;
    sc->get_irq      = aspeed_soc_ast2400_get_irq;
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
    sc->ehcis_num    = 2;
    sc->wdts_num     = 3;
    sc->macs_num     = 2;
    sc->uarts_num    = 5;
    sc->irqmap       = aspeed_soc_ast2500_irqmap;
    sc->memmap       = aspeed_soc_ast2500_memmap;
    sc->num_cpus     = 1;
    sc->get_irq      = aspeed_soc_ast2400_get_irq;
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

type_init(aspeed_soc_register_types);

qemu_irq aspeed_soc_get_irq(AspeedSoCState *s, int dev)
{
    return ASPEED_SOC_GET_CLASS(s)->get_irq(s, dev);
}

bool aspeed_soc_uart_realize(AspeedSoCState *s, Error **errp)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    SerialMM *smm;

    for (int i = 0, uart = ASPEED_DEV_UART1; i < sc->uarts_num; i++, uart++) {
        smm = &s->uart[i];

        /* Chardev property is set by the machine. */
        qdev_prop_set_uint8(DEVICE(smm), "regshift", 2);
        qdev_prop_set_uint32(DEVICE(smm), "baudbase", 38400);
        qdev_set_legacy_instance_id(DEVICE(smm), sc->memmap[uart], 2);
        qdev_prop_set_uint8(DEVICE(smm), "endianness", DEVICE_LITTLE_ENDIAN);
        if (!sysbus_realize(SYS_BUS_DEVICE(smm), errp)) {
            return false;
        }

        sysbus_connect_irq(SYS_BUS_DEVICE(smm), 0, aspeed_soc_get_irq(s, uart));
        aspeed_mmio_map(s, SYS_BUS_DEVICE(smm), 0, sc->memmap[uart]);
    }

    return true;
}

void aspeed_soc_uart_set_chr(AspeedSoCState *s, int dev, Chardev *chr)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int i = dev - ASPEED_DEV_UART1;

    g_assert(0 <= i && i < ARRAY_SIZE(s->uart) && i < sc->uarts_num);
    qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", chr);
}

/*
 * SDMC should be realized first to get correct RAM size and max size
 * values
 */
bool aspeed_soc_dram_init(AspeedSoCState *s, Error **errp)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    ram_addr_t ram_size, max_ram_size;

    ram_size = object_property_get_uint(OBJECT(&s->sdmc), "ram-size",
                                        &error_abort);
    max_ram_size = object_property_get_uint(OBJECT(&s->sdmc), "max-ram-size",
                                            &error_abort);

    memory_region_init(&s->dram_container, OBJECT(s), "ram-container",
                       max_ram_size);
    memory_region_add_subregion(&s->dram_container, 0, s->dram_mr);

    /*
     * Add a memory region beyond the RAM region to let firmwares scan
     * the address space with load/store and guess how much RAM the
     * SoC has.
     */
    if (ram_size < max_ram_size) {
        DeviceState *dev = qdev_new(TYPE_UNIMPLEMENTED_DEVICE);

        qdev_prop_set_string(dev, "name", "ram-empty");
        qdev_prop_set_uint64(dev, "size", max_ram_size  - ram_size);
        if (!sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), errp)) {
            return false;
        }

        memory_region_add_subregion_overlap(&s->dram_container, ram_size,
                      sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0), -1000);
    }

    memory_region_add_subregion(s->memory,
                      sc->memmap[ASPEED_DEV_SDRAM], &s->dram_container);
    return true;
}

void aspeed_mmio_map(AspeedSoCState *s, SysBusDevice *dev, int n, hwaddr addr)
{
    memory_region_add_subregion(s->memory, addr,
                                sysbus_mmio_get_region(dev, n));
}

void aspeed_mmio_map_unimplemented(AspeedSoCState *s, SysBusDevice *dev,
                                   const char *name, hwaddr addr, uint64_t size)
{
    qdev_prop_set_string(DEVICE(dev), "name", name);
    qdev_prop_set_uint64(DEVICE(dev), "size", size);
    sysbus_realize(dev, &error_abort);

    memory_region_add_subregion_overlap(s->memory, addr,
                                        sysbus_mmio_get_region(dev, 0), -1000);
}
