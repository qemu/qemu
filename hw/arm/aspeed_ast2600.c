/*
 * ASPEED SoC 2600 family
 *
 * Copyright (c) 2016-2019, IBM Corporation.
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

static const hwaddr aspeed_soc_ast2600_memmap[] = {
    [ASPEED_SRAM]      = 0x10000000,
    /* 0x16000000     0x17FFFFFF : AHB BUS do LPC Bus bridge */
    [ASPEED_IOMEM]     = 0x1E600000,
    [ASPEED_PWM]       = 0x1E610000,
    [ASPEED_FMC]       = 0x1E620000,
    [ASPEED_SPI1]      = 0x1E630000,
    [ASPEED_SPI2]      = 0x1E641000,
    [ASPEED_EHCI1]     = 0x1E6A1000,
    [ASPEED_EHCI2]     = 0x1E6A3000,
    [ASPEED_MII1]      = 0x1E650000,
    [ASPEED_MII2]      = 0x1E650008,
    [ASPEED_MII3]      = 0x1E650010,
    [ASPEED_MII4]      = 0x1E650018,
    [ASPEED_ETH1]      = 0x1E660000,
    [ASPEED_ETH3]      = 0x1E670000,
    [ASPEED_ETH2]      = 0x1E680000,
    [ASPEED_ETH4]      = 0x1E690000,
    [ASPEED_VIC]       = 0x1E6C0000,
    [ASPEED_SDMC]      = 0x1E6E0000,
    [ASPEED_SCU]       = 0x1E6E2000,
    [ASPEED_XDMA]      = 0x1E6E7000,
    [ASPEED_ADC]       = 0x1E6E9000,
    [ASPEED_VIDEO]     = 0x1E700000,
    [ASPEED_SDHCI]     = 0x1E740000,
    [ASPEED_EMMC]      = 0x1E750000,
    [ASPEED_GPIO]      = 0x1E780000,
    [ASPEED_GPIO_1_8V] = 0x1E780800,
    [ASPEED_RTC]       = 0x1E781000,
    [ASPEED_TIMER1]    = 0x1E782000,
    [ASPEED_WDT]       = 0x1E785000,
    [ASPEED_LPC]       = 0x1E789000,
    [ASPEED_IBT]       = 0x1E789140,
    [ASPEED_I2C]       = 0x1E78A000,
    [ASPEED_UART1]     = 0x1E783000,
    [ASPEED_UART5]     = 0x1E784000,
    [ASPEED_VUART]     = 0x1E787000,
    [ASPEED_SDRAM]     = 0x80000000,
};

#define ASPEED_A7MPCORE_ADDR 0x40460000

#define ASPEED_SOC_AST2600_MAX_IRQ 128

/* Shared Peripheral Interrupt values below are offset by -32 from datasheet */
static const int aspeed_soc_ast2600_irqmap[] = {
    [ASPEED_UART1]     = 47,
    [ASPEED_UART2]     = 48,
    [ASPEED_UART3]     = 49,
    [ASPEED_UART4]     = 50,
    [ASPEED_UART5]     = 8,
    [ASPEED_VUART]     = 8,
    [ASPEED_FMC]       = 39,
    [ASPEED_SDMC]      = 0,
    [ASPEED_SCU]       = 12,
    [ASPEED_ADC]       = 78,
    [ASPEED_XDMA]      = 6,
    [ASPEED_SDHCI]     = 43,
    [ASPEED_EHCI1]     = 5,
    [ASPEED_EHCI2]     = 9,
    [ASPEED_EMMC]      = 15,
    [ASPEED_GPIO]      = 40,
    [ASPEED_GPIO_1_8V] = 11,
    [ASPEED_RTC]       = 13,
    [ASPEED_TIMER1]    = 16,
    [ASPEED_TIMER2]    = 17,
    [ASPEED_TIMER3]    = 18,
    [ASPEED_TIMER4]    = 19,
    [ASPEED_TIMER5]    = 20,
    [ASPEED_TIMER6]    = 21,
    [ASPEED_TIMER7]    = 22,
    [ASPEED_TIMER8]    = 23,
    [ASPEED_WDT]       = 24,
    [ASPEED_PWM]       = 44,
    [ASPEED_LPC]       = 35,
    [ASPEED_IBT]       = 35,    /* LPC */
    [ASPEED_I2C]       = 110,   /* 110 -> 125 */
    [ASPEED_ETH1]      = 2,
    [ASPEED_ETH2]      = 3,
    [ASPEED_ETH3]      = 32,
    [ASPEED_ETH4]      = 33,

};

static qemu_irq aspeed_soc_get_irq(AspeedSoCState *s, int ctrl)
{
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    return qdev_get_gpio_in(DEVICE(&s->a7mpcore), sc->irqmap[ctrl]);
}

static void aspeed_soc_ast2600_init(Object *obj)
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

    object_initialize_child(obj, "a7mpcore", &s->a7mpcore,
                            TYPE_A15MPCORE_PRIV);

    object_initialize_child(obj, "rtc", &s->rtc, TYPE_ASPEED_RTC);

    snprintf(typename, sizeof(typename), "aspeed.timer-%s", socname);
    object_initialize_child(obj, "timerctrl", &s->timerctrl, typename);

    snprintf(typename, sizeof(typename), "aspeed.i2c-%s", socname);
    object_initialize_child(obj, "i2c", &s->i2c, typename);

    snprintf(typename, sizeof(typename), "aspeed.fmc-%s", socname);
    object_initialize_child(obj, "fmc", &s->fmc, typename);
    object_property_add_alias(obj, "num-cs", OBJECT(&s->fmc), "num-cs");

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
    object_property_add_alias(obj, "max-ram-size", OBJECT(&s->sdmc),
                              "max-ram-size");

    for (i = 0; i < sc->wdts_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.wdt-%s", socname);
        object_initialize_child(obj, "wdt[*]", &s->wdt[i], typename);
    }

    for (i = 0; i < sc->macs_num; i++) {
        object_initialize_child(obj, "ftgmac100[*]", &s->ftgmac100[i],
                                TYPE_FTGMAC100);

        object_initialize_child(obj, "mii[*]", &s->mii[i], TYPE_ASPEED_MII);
    }

    object_initialize_child(obj, "xdma", &s->xdma, TYPE_ASPEED_XDMA);

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s", socname);
    object_initialize_child(obj, "gpio", &s->gpio, typename);

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s-1_8v", socname);
    object_initialize_child(obj, "gpio_1_8v", &s->gpio_1_8v, typename);

    object_initialize_child(obj, "sd-controller", &s->sdhci,
                            TYPE_ASPEED_SDHCI);

    object_property_set_int(OBJECT(&s->sdhci), "num-slots", 2, &error_abort);

    /* Init sd card slot class here so that they're under the correct parent */
    for (i = 0; i < ASPEED_SDHCI_NUM_SLOTS; ++i) {
        object_initialize_child(obj, "sd-controller.sdhci[*]",
                                &s->sdhci.slots[i], TYPE_SYSBUS_SDHCI);
    }

    object_initialize_child(obj, "emmc-controller", &s->emmc,
                            TYPE_ASPEED_SDHCI);

    object_property_set_int(OBJECT(&s->emmc), "num-slots", 1, &error_abort);

    object_initialize_child(obj, "emmc-controller.sdhci", &s->emmc.slots[0],
                            TYPE_SYSBUS_SDHCI);
}

/*
 * ASPEED ast2600 has 0xf as cluster ID
 *
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0388e/CIHEBGFG.html
 */
static uint64_t aspeed_calc_affinity(int cpu)
{
    return (0xf << ARM_AFF1_SHIFT) | cpu;
}

static void aspeed_soc_ast2600_realize(DeviceState *dev, Error **errp)
{
    int i;
    AspeedSoCState *s = ASPEED_SOC(dev);
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    Error *err = NULL;
    qemu_irq irq;

    /* IO space */
    create_unimplemented_device("aspeed_soc.io", sc->memmap[ASPEED_IOMEM],
                                ASPEED_SOC_IOMEM_SIZE);

    /* Video engine stub */
    create_unimplemented_device("aspeed.video", sc->memmap[ASPEED_VIDEO],
                                0x1000);

    /* CPU */
    for (i = 0; i < sc->num_cpus; i++) {
        object_property_set_int(OBJECT(&s->cpu[i]), "psci-conduit",
                                QEMU_PSCI_CONDUIT_SMC, &error_abort);
        if (sc->num_cpus > 1) {
            object_property_set_int(OBJECT(&s->cpu[i]), "reset-cbar",
                                    ASPEED_A7MPCORE_ADDR, &error_abort);
        }
        object_property_set_int(OBJECT(&s->cpu[i]), "mp-affinity",
                                aspeed_calc_affinity(i), &error_abort);

        object_property_set_int(OBJECT(&s->cpu[i]), "cntfrq", 1125000000,
                                &error_abort);

        /*
         * TODO: the secondary CPUs are started and a boot helper
         * is needed when using -kernel
         */

        if (!qdev_realize(DEVICE(&s->cpu[i]), NULL, errp)) {
            return;
        }
    }

    /* A7MPCORE */
    object_property_set_int(OBJECT(&s->a7mpcore), "num-cpu", sc->num_cpus,
                            &error_abort);
    object_property_set_int(OBJECT(&s->a7mpcore), "num-irq",
                            ASPEED_SOC_AST2600_MAX_IRQ + GIC_INTERNAL,
                            &error_abort);

    sysbus_realize(SYS_BUS_DEVICE(&s->a7mpcore), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a7mpcore), 0, ASPEED_A7MPCORE_ADDR);

    for (i = 0; i < sc->num_cpus; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->a7mpcore);
        DeviceState  *d   = DEVICE(qemu_get_cpu(i));

        irq = qdev_get_gpio_in(d, ARM_CPU_IRQ);
        sysbus_connect_irq(sbd, i, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_FIQ);
        sysbus_connect_irq(sbd, i + sc->num_cpus, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_VIRQ);
        sysbus_connect_irq(sbd, i + 2 * sc->num_cpus, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_VFIQ);
        sysbus_connect_irq(sbd, i + 3 * sc->num_cpus, irq);
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
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->scu), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->scu), 0, sc->memmap[ASPEED_SCU]);

    /* RTC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->rtc), 0, sc->memmap[ASPEED_RTC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0,
                       aspeed_soc_get_irq(s, ASPEED_RTC));

    /* Timer */
    object_property_set_link(OBJECT(&s->timerctrl), "scu", OBJECT(&s->scu),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timerctrl), errp)) {
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
    object_property_set_link(OBJECT(&s->i2c), "dram", OBJECT(s->dram_mr),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2c), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c), 0, sc->memmap[ASPEED_I2C]);
    for (i = 0; i < ASPEED_I2C_GET_CLASS(&s->i2c)->num_busses; i++) {
        qemu_irq irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                        sc->irqmap[ASPEED_I2C] + i);
        /*
         * The AST2600 SoC has one IRQ per I2C bus. Skip the common
         * IRQ (AST2400 and AST2500) and connect all bussses.
         */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c), i + 1, irq);
    }

    /* FMC, The number of CS is set at the board level */
    object_property_set_link(OBJECT(&s->fmc), "dram", OBJECT(s->dram_mr),
                             &error_abort);
    if (!object_property_set_int(OBJECT(&s->fmc), "sdram-base",
                                 sc->memmap[ASPEED_SDRAM], errp)) {
        return;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->fmc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fmc), 0, sc->memmap[ASPEED_FMC]);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fmc), 1,
                    s->fmc.ctrl->flash_window_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fmc), 0,
                       aspeed_soc_get_irq(s, ASPEED_FMC));

    /* SPI */
    for (i = 0; i < sc->spis_num; i++) {
        object_property_set_link(OBJECT(&s->spi[i]), "dram",
                                 OBJECT(s->dram_mr), &error_abort);
        object_property_set_int(OBJECT(&s->spi[i]), "num-cs", 1, &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0,
                        sc->memmap[ASPEED_SPI1 + i]);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 1,
                        s->spi[i].ctrl->flash_window_base);
    }

    /* EHCI */
    for (i = 0; i < sc->ehcis_num; i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->ehci[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                        sc->memmap[ASPEED_EHCI1 + i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                           aspeed_soc_get_irq(s, ASPEED_EHCI1 + i));
    }

    /* SDMC - SDRAM Memory Controller */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdmc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdmc), 0, sc->memmap[ASPEED_SDMC]);

    /* Watch dog */
    for (i = 0; i < sc->wdts_num; i++) {
        AspeedWDTClass *awc = ASPEED_WDT_GET_CLASS(&s->wdt[i]);

        object_property_set_link(OBJECT(&s->wdt[i]), "scu", OBJECT(&s->scu),
                                 &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                        sc->memmap[ASPEED_WDT] + i * awc->offset);
    }

    /* Net */
    for (i = 0; i < sc->macs_num; i++) {
        object_property_set_bool(OBJECT(&s->ftgmac100[i]), "aspeed", true,
                                 &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->ftgmac100[i]), errp)) {
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                        sc->memmap[ASPEED_ETH1 + i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ftgmac100[i]), 0,
                           aspeed_soc_get_irq(s, ASPEED_ETH1 + i));

        object_property_set_link(OBJECT(&s->mii[i]), "nic",
                                 OBJECT(&s->ftgmac100[i]), &error_abort);
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->mii[i]), errp)) {
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->mii[i]), 0,
                        sc->memmap[ASPEED_MII1 + i]);
    }

    /* XDMA */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xdma), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->xdma), 0,
                    sc->memmap[ASPEED_XDMA]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->xdma), 0,
                       aspeed_soc_get_irq(s, ASPEED_XDMA));

    /* GPIO */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, sc->memmap[ASPEED_GPIO]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), 0,
                       aspeed_soc_get_irq(s, ASPEED_GPIO));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio_1_8v), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio_1_8v), 0,
                    sc->memmap[ASPEED_GPIO_1_8V]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio_1_8v), 0,
                       aspeed_soc_get_irq(s, ASPEED_GPIO_1_8V));

    /* SDHCI */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdhci), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sdhci), 0,
                    sc->memmap[ASPEED_SDHCI]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdhci), 0,
                       aspeed_soc_get_irq(s, ASPEED_SDHCI));

    /* eMMC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->emmc), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->emmc), 0, sc->memmap[ASPEED_EMMC]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->emmc), 0,
                       aspeed_soc_get_irq(s, ASPEED_EMMC));
}

static void aspeed_soc_ast2600_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(oc);

    dc->realize      = aspeed_soc_ast2600_realize;

    sc->name         = "ast2600-a1";
    sc->cpu_type     = ARM_CPU_TYPE_NAME("cortex-a7");
    sc->silicon_rev  = AST2600_A1_SILICON_REV;
    sc->sram_size    = 0x10000;
    sc->spis_num     = 2;
    sc->ehcis_num    = 2;
    sc->wdts_num     = 4;
    sc->macs_num     = 4;
    sc->irqmap       = aspeed_soc_ast2600_irqmap;
    sc->memmap       = aspeed_soc_ast2600_memmap;
    sc->num_cpus     = 2;
}

static const TypeInfo aspeed_soc_ast2600_type_info = {
    .name           = "ast2600-a1",
    .parent         = TYPE_ASPEED_SOC,
    .instance_size  = sizeof(AspeedSoCState),
    .instance_init  = aspeed_soc_ast2600_init,
    .class_init     = aspeed_soc_ast2600_class_init,
    .class_size     = sizeof(AspeedSoCClass),
};

static void aspeed_soc_register_types(void)
{
    type_register_static(&aspeed_soc_ast2600_type_info);
};

type_init(aspeed_soc_register_types)
