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

    sysbus_init_child_obj(obj, "a7mpcore", &s->a7mpcore,
                          sizeof(s->a7mpcore), TYPE_A15MPCORE_PRIV);

    sysbus_init_child_obj(obj, "rtc", OBJECT(&s->rtc), sizeof(s->rtc),
                          TYPE_ASPEED_RTC);

    snprintf(typename, sizeof(typename), "aspeed.timer-%s", socname);
    sysbus_init_child_obj(obj, "timerctrl", OBJECT(&s->timerctrl),
                          sizeof(s->timerctrl), typename);

    snprintf(typename, sizeof(typename), "aspeed.i2c-%s", socname);
    sysbus_init_child_obj(obj, "i2c", OBJECT(&s->i2c), sizeof(s->i2c),
                          typename);

    snprintf(typename, sizeof(typename), "aspeed.fmc-%s", socname);
    sysbus_init_child_obj(obj, "fmc", OBJECT(&s->fmc), sizeof(s->fmc),
                          typename);
    object_property_add_alias(obj, "num-cs", OBJECT(&s->fmc), "num-cs",
                              &error_abort);

    for (i = 0; i < sc->spis_num; i++) {
        snprintf(typename, sizeof(typename), "aspeed.spi%d-%s", i + 1, socname);
        sysbus_init_child_obj(obj, "spi[*]", OBJECT(&s->spi[i]),
                              sizeof(s->spi[i]), typename);
    }

    for (i = 0; i < sc->ehcis_num; i++) {
        sysbus_init_child_obj(obj, "ehci[*]", OBJECT(&s->ehci[i]),
                              sizeof(s->ehci[i]), TYPE_PLATFORM_EHCI);
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
    }

    for (i = 0; i < sc->macs_num; i++) {
        sysbus_init_child_obj(obj, "ftgmac100[*]", OBJECT(&s->ftgmac100[i]),
                              sizeof(s->ftgmac100[i]), TYPE_FTGMAC100);

        sysbus_init_child_obj(obj, "mii[*]", &s->mii[i], sizeof(s->mii[i]),
                              TYPE_ASPEED_MII);
    }

    sysbus_init_child_obj(obj, "xdma", OBJECT(&s->xdma), sizeof(s->xdma),
                          TYPE_ASPEED_XDMA);

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s", socname);
    sysbus_init_child_obj(obj, "gpio", OBJECT(&s->gpio), sizeof(s->gpio),
                          typename);

    snprintf(typename, sizeof(typename), "aspeed.gpio-%s-1_8v", socname);
    sysbus_init_child_obj(obj, "gpio_1_8v", OBJECT(&s->gpio_1_8v),
                          sizeof(s->gpio_1_8v), typename);

    sysbus_init_child_obj(obj, "sd-controller", OBJECT(&s->sdhci),
                          sizeof(s->sdhci), TYPE_ASPEED_SDHCI);

    object_property_set_int(OBJECT(&s->sdhci), 2, "num-slots", &error_abort);

    /* Init sd card slot class here so that they're under the correct parent */
    for (i = 0; i < ASPEED_SDHCI_NUM_SLOTS; ++i) {
        sysbus_init_child_obj(obj, "sd-controller.sdhci[*]",
                              OBJECT(&s->sdhci.slots[i]),
                              sizeof(s->sdhci.slots[i]), TYPE_SYSBUS_SDHCI);
    }

    sysbus_init_child_obj(obj, "emmc-controller", OBJECT(&s->emmc),
                          sizeof(s->emmc), TYPE_ASPEED_SDHCI);

    object_property_set_int(OBJECT(&s->emmc), 1, "num-slots", &error_abort);

    sysbus_init_child_obj(obj, "emmc-controller.sdhci",
                          OBJECT(&s->emmc.slots[0]), sizeof(s->emmc.slots[0]),
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
    Error *err = NULL, *local_err = NULL;
    qemu_irq irq;

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
        object_property_set_int(OBJECT(&s->cpu[i]), QEMU_PSCI_CONDUIT_SMC,
                                "psci-conduit", &error_abort);
        if (s->num_cpus > 1) {
            object_property_set_int(OBJECT(&s->cpu[i]),
                                    ASPEED_A7MPCORE_ADDR,
                                    "reset-cbar", &error_abort);
        }
        object_property_set_int(OBJECT(&s->cpu[i]), aspeed_calc_affinity(i),
                                "mp-affinity", &error_abort);

        object_property_set_int(OBJECT(&s->cpu[i]), 1125000000, "cntfrq",
                                &error_abort);

        /*
         * TODO: the secondary CPUs are started and a boot helper
         * is needed when using -kernel
         */

        object_property_set_bool(OBJECT(&s->cpu[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    /* A7MPCORE */
    object_property_set_int(OBJECT(&s->a7mpcore), s->num_cpus, "num-cpu",
                            &error_abort);
    object_property_set_int(OBJECT(&s->a7mpcore),
                            ASPEED_SOC_AST2600_MAX_IRQ + GIC_INTERNAL,
                            "num-irq", &error_abort);

    object_property_set_bool(OBJECT(&s->a7mpcore), true, "realized",
                             &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a7mpcore), 0, ASPEED_A7MPCORE_ADDR);

    for (i = 0; i < s->num_cpus; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->a7mpcore);
        DeviceState  *d   = DEVICE(qemu_get_cpu(i));

        irq = qdev_get_gpio_in(d, ARM_CPU_IRQ);
        sysbus_connect_irq(sbd, i, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_FIQ);
        sysbus_connect_irq(sbd, i + s->num_cpus, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_VIRQ);
        sysbus_connect_irq(sbd, i + 2 * s->num_cpus, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_VFIQ);
        sysbus_connect_irq(sbd, i + 3 * s->num_cpus, irq);
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
    object_property_set_link(OBJECT(&s->timerctrl),
                             OBJECT(&s->scu), "scu", &error_abort);
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
    object_property_set_link(OBJECT(&s->i2c), OBJECT(s->dram_mr), "dram", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    object_property_set_bool(OBJECT(&s->i2c), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
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
    object_property_set_link(OBJECT(&s->fmc), OBJECT(s->dram_mr), "dram", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
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
        object_property_set_link(OBJECT(&s->spi[i]), OBJECT(s->dram_mr),
                                 "dram", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
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

    /* EHCI */
    for (i = 0; i < sc->ehcis_num; i++) {
        object_property_set_bool(OBJECT(&s->ehci[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                        sc->memmap[ASPEED_EHCI1 + i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                           aspeed_soc_get_irq(s, ASPEED_EHCI1 + i));
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

        object_property_set_link(OBJECT(&s->wdt[i]),
                                 OBJECT(&s->scu), "scu", &error_abort);
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

        object_property_set_link(OBJECT(&s->mii[i]), OBJECT(&s->ftgmac100[i]),
                                 "nic", &error_abort);
        object_property_set_bool(OBJECT(&s->mii[i]), true, "realized",
                                 &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->mii[i]), 0,
                        sc->memmap[ASPEED_MII1 + i]);
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

    object_property_set_bool(OBJECT(&s->gpio_1_8v), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio_1_8v), 0,
                    sc->memmap[ASPEED_GPIO_1_8V]);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio_1_8v), 0,
                       aspeed_soc_get_irq(s, ASPEED_GPIO_1_8V));

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

    /* eMMC */
    object_property_set_bool(OBJECT(&s->emmc), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
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

    sc->name         = "ast2600-a0";
    sc->cpu_type     = ARM_CPU_TYPE_NAME("cortex-a7");
    sc->silicon_rev  = AST2600_A0_SILICON_REV;
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
    .name           = "ast2600-a0",
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
