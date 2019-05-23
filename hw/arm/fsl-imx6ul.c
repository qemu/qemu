/*
 * Copyright (c) 2018 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * i.MX6UL SOC emulation.
 *
 * Based on hw/arm/fsl-imx7.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/fsl-imx6ul.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

#define NAME_SIZE 20

static void fsl_imx6ul_init(Object *obj)
{
    FslIMX6ULState *s = FSL_IMX6UL(obj);
    char name[NAME_SIZE];
    int i;

    for (i = 0; i < MIN(smp_cpus, FSL_IMX6UL_NUM_CPUS); i++) {
        snprintf(name, NAME_SIZE, "cpu%d", i);
        object_initialize_child(obj, name, &s->cpu[i], sizeof(s->cpu[i]),
                                "cortex-a7-" TYPE_ARM_CPU, &error_abort, NULL);
    }

    /*
     * A7MPCORE
     */
    sysbus_init_child_obj(obj, "a7mpcore", &s->a7mpcore, sizeof(s->a7mpcore),
                          TYPE_A15MPCORE_PRIV);

    /*
     * CCM
     */
    sysbus_init_child_obj(obj, "ccm", &s->ccm, sizeof(s->ccm), TYPE_IMX6UL_CCM);

    /*
     * SRC
     */
    sysbus_init_child_obj(obj, "src", &s->src, sizeof(s->src), TYPE_IMX6_SRC);

    /*
     * GPCv2
     */
    sysbus_init_child_obj(obj, "gpcv2", &s->gpcv2, sizeof(s->gpcv2),
                          TYPE_IMX_GPCV2);

    /*
     * SNVS
     */
    sysbus_init_child_obj(obj, "snvs", &s->snvs, sizeof(s->snvs),
                          TYPE_IMX7_SNVS);

    /*
     * GPR
     */
    sysbus_init_child_obj(obj, "gpr", &s->gpr, sizeof(s->gpr),
                          TYPE_IMX7_GPR);

    /*
     * GPIOs 1 to 5
     */
    for (i = 0; i < FSL_IMX6UL_NUM_GPIOS; i++) {
        snprintf(name, NAME_SIZE, "gpio%d", i);
        sysbus_init_child_obj(obj, name, &s->gpio[i], sizeof(s->gpio[i]),
                              TYPE_IMX_GPIO);
    }

    /*
     * GPT 1, 2
     */
    for (i = 0; i < FSL_IMX6UL_NUM_GPTS; i++) {
        snprintf(name, NAME_SIZE, "gpt%d", i);
        sysbus_init_child_obj(obj, name, &s->gpt[i], sizeof(s->gpt[i]),
                              TYPE_IMX7_GPT);
    }

    /*
     * EPIT 1, 2
     */
    for (i = 0; i < FSL_IMX6UL_NUM_EPITS; i++) {
        snprintf(name, NAME_SIZE, "epit%d", i + 1);
        sysbus_init_child_obj(obj, name, &s->epit[i], sizeof(s->epit[i]),
                              TYPE_IMX_EPIT);
    }

    /*
     * eCSPI
     */
    for (i = 0; i < FSL_IMX6UL_NUM_ECSPIS; i++) {
        snprintf(name, NAME_SIZE, "spi%d", i + 1);
        sysbus_init_child_obj(obj, name, &s->spi[i], sizeof(s->spi[i]),
                              TYPE_IMX_SPI);
    }

    /*
     * I2C
     */
    for (i = 0; i < FSL_IMX6UL_NUM_I2CS; i++) {
        snprintf(name, NAME_SIZE, "i2c%d", i + 1);
        sysbus_init_child_obj(obj, name, &s->i2c[i], sizeof(s->i2c[i]),
                              TYPE_IMX_I2C);
    }

    /*
     * UART
     */
    for (i = 0; i < FSL_IMX6UL_NUM_UARTS; i++) {
        snprintf(name, NAME_SIZE, "uart%d", i);
        sysbus_init_child_obj(obj, name, &s->uart[i], sizeof(s->uart[i]),
                              TYPE_IMX_SERIAL);
    }

    /*
     * Ethernet
     */
    for (i = 0; i < FSL_IMX6UL_NUM_ETHS; i++) {
        snprintf(name, NAME_SIZE, "eth%d", i);
        sysbus_init_child_obj(obj, name, &s->eth[i], sizeof(s->eth[i]),
                              TYPE_IMX_ENET);
    }

    /*
     * SDHCI
     */
    for (i = 0; i < FSL_IMX6UL_NUM_USDHCS; i++) {
        snprintf(name, NAME_SIZE, "usdhc%d", i);
        sysbus_init_child_obj(obj, name, &s->usdhc[i], sizeof(s->usdhc[i]),
                              TYPE_IMX_USDHC);
    }

    /*
     * Watchdog
     */
    for (i = 0; i < FSL_IMX6UL_NUM_WDTS; i++) {
        snprintf(name, NAME_SIZE, "wdt%d", i);
        sysbus_init_child_obj(obj, name, &s->wdt[i], sizeof(s->wdt[i]),
                              TYPE_IMX2_WDT);
    }
}

static void fsl_imx6ul_realize(DeviceState *dev, Error **errp)
{
    FslIMX6ULState *s = FSL_IMX6UL(dev);
    int i;
    qemu_irq irq;
    char name[NAME_SIZE];

    if (smp_cpus > FSL_IMX6UL_NUM_CPUS) {
        error_setg(errp, "%s: Only %d CPUs are supported (%d requested)",
                   TYPE_FSL_IMX6UL, FSL_IMX6UL_NUM_CPUS, smp_cpus);
        return;
    }

    for (i = 0; i < smp_cpus; i++) {
        Object *o = OBJECT(&s->cpu[i]);

        object_property_set_int(o, QEMU_PSCI_CONDUIT_SMC,
                                "psci-conduit", &error_abort);

        /* On uniprocessor, the CBAR is set to 0 */
        if (smp_cpus > 1) {
            object_property_set_int(o, FSL_IMX6UL_A7MPCORE_ADDR,
                                    "reset-cbar", &error_abort);
        }

        if (i) {
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(o, true,
                                     "start-powered-off", &error_abort);
        }

        object_property_set_bool(o, true, "realized", &error_abort);
    }

    /*
     * A7MPCORE
     */
    object_property_set_int(OBJECT(&s->a7mpcore), smp_cpus, "num-cpu",
                            &error_abort);
    object_property_set_int(OBJECT(&s->a7mpcore),
                            FSL_IMX6UL_MAX_IRQ + GIC_INTERNAL,
                            "num-irq", &error_abort);
    object_property_set_bool(OBJECT(&s->a7mpcore), true, "realized",
                             &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a7mpcore), 0, FSL_IMX6UL_A7MPCORE_ADDR);

    for (i = 0; i < smp_cpus; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->a7mpcore);
        DeviceState  *d   = DEVICE(qemu_get_cpu(i));

        irq = qdev_get_gpio_in(d, ARM_CPU_IRQ);
        sysbus_connect_irq(sbd, i, irq);
        sysbus_connect_irq(sbd, i + smp_cpus, qdev_get_gpio_in(d, ARM_CPU_FIQ));
        sysbus_connect_irq(sbd, i + 2 * smp_cpus,
                           qdev_get_gpio_in(d, ARM_CPU_VIRQ));
        sysbus_connect_irq(sbd, i + 3 * smp_cpus,
                           qdev_get_gpio_in(d, ARM_CPU_VFIQ));
    }

    /*
     * A7MPCORE DAP
     */
    create_unimplemented_device("a7mpcore-dap", FSL_IMX6UL_A7MPCORE_DAP_ADDR,
                                0x100000);

    /*
     * GPT 1, 2
     */
    for (i = 0; i < FSL_IMX6UL_NUM_GPTS; i++) {
        static const hwaddr FSL_IMX6UL_GPTn_ADDR[FSL_IMX6UL_NUM_GPTS] = {
            FSL_IMX6UL_GPT1_ADDR,
            FSL_IMX6UL_GPT2_ADDR,
        };

        static const int FSL_IMX6UL_GPTn_IRQ[FSL_IMX6UL_NUM_GPTS] = {
            FSL_IMX6UL_GPT1_IRQ,
            FSL_IMX6UL_GPT2_IRQ,
        };

        s->gpt[i].ccm = IMX_CCM(&s->ccm);
        object_property_set_bool(OBJECT(&s->gpt[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                        FSL_IMX6UL_GPTn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_GPTn_IRQ[i]));
    }

    /*
     * EPIT 1, 2
     */
    for (i = 0; i < FSL_IMX6UL_NUM_EPITS; i++) {
        static const hwaddr FSL_IMX6UL_EPITn_ADDR[FSL_IMX6UL_NUM_EPITS] = {
            FSL_IMX6UL_EPIT1_ADDR,
            FSL_IMX6UL_EPIT2_ADDR,
        };

        static const int FSL_IMX6UL_EPITn_IRQ[FSL_IMX6UL_NUM_EPITS] = {
            FSL_IMX6UL_EPIT1_IRQ,
            FSL_IMX6UL_EPIT2_IRQ,
        };

        s->epit[i].ccm = IMX_CCM(&s->ccm);
        object_property_set_bool(OBJECT(&s->epit[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->epit[i]), 0,
                        FSL_IMX6UL_EPITn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->epit[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_EPITn_IRQ[i]));
    }

    /*
     * GPIO
     */
    for (i = 0; i < FSL_IMX6UL_NUM_GPIOS; i++) {
        static const hwaddr FSL_IMX6UL_GPIOn_ADDR[FSL_IMX6UL_NUM_GPIOS] = {
            FSL_IMX6UL_GPIO1_ADDR,
            FSL_IMX6UL_GPIO2_ADDR,
            FSL_IMX6UL_GPIO3_ADDR,
            FSL_IMX6UL_GPIO4_ADDR,
            FSL_IMX6UL_GPIO5_ADDR,
        };

        static const int FSL_IMX6UL_GPIOn_LOW_IRQ[FSL_IMX6UL_NUM_GPIOS] = {
            FSL_IMX6UL_GPIO1_LOW_IRQ,
            FSL_IMX6UL_GPIO2_LOW_IRQ,
            FSL_IMX6UL_GPIO3_LOW_IRQ,
            FSL_IMX6UL_GPIO4_LOW_IRQ,
            FSL_IMX6UL_GPIO5_LOW_IRQ,
        };

        static const int FSL_IMX6UL_GPIOn_HIGH_IRQ[FSL_IMX6UL_NUM_GPIOS] = {
            FSL_IMX6UL_GPIO1_HIGH_IRQ,
            FSL_IMX6UL_GPIO2_HIGH_IRQ,
            FSL_IMX6UL_GPIO3_HIGH_IRQ,
            FSL_IMX6UL_GPIO4_HIGH_IRQ,
            FSL_IMX6UL_GPIO5_HIGH_IRQ,
        };

        object_property_set_bool(OBJECT(&s->gpio[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                        FSL_IMX6UL_GPIOn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_GPIOn_LOW_IRQ[i]));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 1,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_GPIOn_HIGH_IRQ[i]));
    }

    /*
     * IOMUXC and IOMUXC_GPR
     */
    for (i = 0; i < 1; i++) {
        static const hwaddr FSL_IMX6UL_IOMUXCn_ADDR[FSL_IMX6UL_NUM_IOMUXCS] = {
            FSL_IMX6UL_IOMUXC_ADDR,
            FSL_IMX6UL_IOMUXC_GPR_ADDR,
        };

        snprintf(name, NAME_SIZE, "iomuxc%d", i);
        create_unimplemented_device(name, FSL_IMX6UL_IOMUXCn_ADDR[i], 0x4000);
    }

    /*
     * CCM
     */
    object_property_set_bool(OBJECT(&s->ccm), true, "realized", &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0, FSL_IMX6UL_CCM_ADDR);

    /*
     * SRC
     */
    object_property_set_bool(OBJECT(&s->src), true, "realized", &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->src), 0, FSL_IMX6UL_SRC_ADDR);

    /*
     * GPCv2
     */
    object_property_set_bool(OBJECT(&s->gpcv2), true,
                             "realized", &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpcv2), 0, FSL_IMX6UL_GPC_ADDR);

    /* Initialize all ECSPI */
    for (i = 0; i < FSL_IMX6UL_NUM_ECSPIS; i++) {
        static const hwaddr FSL_IMX6UL_SPIn_ADDR[FSL_IMX6UL_NUM_ECSPIS] = {
            FSL_IMX6UL_ECSPI1_ADDR,
            FSL_IMX6UL_ECSPI2_ADDR,
            FSL_IMX6UL_ECSPI3_ADDR,
            FSL_IMX6UL_ECSPI4_ADDR,
        };

        static const int FSL_IMX6UL_SPIn_IRQ[FSL_IMX6UL_NUM_ECSPIS] = {
            FSL_IMX6UL_ECSPI1_IRQ,
            FSL_IMX6UL_ECSPI2_IRQ,
            FSL_IMX6UL_ECSPI3_IRQ,
            FSL_IMX6UL_ECSPI4_IRQ,
        };

        /* Initialize the SPI */
        object_property_set_bool(OBJECT(&s->spi[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0,
                        FSL_IMX6UL_SPIn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_SPIn_IRQ[i]));
    }

    /*
     * I2C
     */
    for (i = 0; i < FSL_IMX6UL_NUM_I2CS; i++) {
        static const hwaddr FSL_IMX6UL_I2Cn_ADDR[FSL_IMX6UL_NUM_I2CS] = {
            FSL_IMX6UL_I2C1_ADDR,
            FSL_IMX6UL_I2C2_ADDR,
            FSL_IMX6UL_I2C3_ADDR,
            FSL_IMX6UL_I2C4_ADDR,
        };

        static const int FSL_IMX6UL_I2Cn_IRQ[FSL_IMX6UL_NUM_I2CS] = {
            FSL_IMX6UL_I2C1_IRQ,
            FSL_IMX6UL_I2C2_IRQ,
            FSL_IMX6UL_I2C3_IRQ,
            FSL_IMX6UL_I2C4_IRQ,
        };

        object_property_set_bool(OBJECT(&s->i2c[i]), true, "realized",
                                 &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[i]), 0, FSL_IMX6UL_I2Cn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_I2Cn_IRQ[i]));
    }

    /*
     * UART
     */
    for (i = 0; i < FSL_IMX6UL_NUM_UARTS; i++) {
        static const hwaddr FSL_IMX6UL_UARTn_ADDR[FSL_IMX6UL_NUM_UARTS] = {
            FSL_IMX6UL_UART1_ADDR,
            FSL_IMX6UL_UART2_ADDR,
            FSL_IMX6UL_UART3_ADDR,
            FSL_IMX6UL_UART4_ADDR,
            FSL_IMX6UL_UART5_ADDR,
            FSL_IMX6UL_UART6_ADDR,
            FSL_IMX6UL_UART7_ADDR,
            FSL_IMX6UL_UART8_ADDR,
        };

        static const int FSL_IMX6UL_UARTn_IRQ[FSL_IMX6UL_NUM_UARTS] = {
            FSL_IMX6UL_UART1_IRQ,
            FSL_IMX6UL_UART2_IRQ,
            FSL_IMX6UL_UART3_IRQ,
            FSL_IMX6UL_UART4_IRQ,
            FSL_IMX6UL_UART5_IRQ,
            FSL_IMX6UL_UART6_IRQ,
            FSL_IMX6UL_UART7_IRQ,
            FSL_IMX6UL_UART8_IRQ,
        };

        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));

        object_property_set_bool(OBJECT(&s->uart[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0,
                        FSL_IMX6UL_UARTn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_UARTn_IRQ[i]));
    }

    /*
     * Ethernet
     */
    for (i = 0; i < FSL_IMX6UL_NUM_ETHS; i++) {
        static const hwaddr FSL_IMX6UL_ENETn_ADDR[FSL_IMX6UL_NUM_ETHS] = {
            FSL_IMX6UL_ENET1_ADDR,
            FSL_IMX6UL_ENET2_ADDR,
        };

        static const int FSL_IMX6UL_ENETn_IRQ[FSL_IMX6UL_NUM_ETHS] = {
            FSL_IMX6UL_ENET1_IRQ,
            FSL_IMX6UL_ENET2_IRQ,
        };

        static const int FSL_IMX6UL_ENETn_TIMER_IRQ[FSL_IMX6UL_NUM_ETHS] = {
            FSL_IMX6UL_ENET1_TIMER_IRQ,
            FSL_IMX6UL_ENET2_TIMER_IRQ,
        };

        object_property_set_uint(OBJECT(&s->eth[i]),
                                 FSL_IMX6UL_ETH_NUM_TX_RINGS,
                                 "tx-ring-num", &error_abort);
        qdev_set_nic_properties(DEVICE(&s->eth[i]), &nd_table[i]);
        object_property_set_bool(OBJECT(&s->eth[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->eth[i]), 0,
                        FSL_IMX6UL_ENETn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_ENETn_IRQ[i]));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 1,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_ENETn_TIMER_IRQ[i]));
    }

    /*
     * USDHC
     */
    for (i = 0; i < FSL_IMX6UL_NUM_USDHCS; i++) {
        static const hwaddr FSL_IMX6UL_USDHCn_ADDR[FSL_IMX6UL_NUM_USDHCS] = {
            FSL_IMX6UL_USDHC1_ADDR,
            FSL_IMX6UL_USDHC2_ADDR,
        };

        static const int FSL_IMX6UL_USDHCn_IRQ[FSL_IMX6UL_NUM_USDHCS] = {
            FSL_IMX6UL_USDHC1_IRQ,
            FSL_IMX6UL_USDHC2_IRQ,
        };

        object_property_set_bool(OBJECT(&s->usdhc[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                        FSL_IMX6UL_USDHCn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_USDHCn_IRQ[i]));
    }

    /*
     * SNVS
     */
    object_property_set_bool(OBJECT(&s->snvs), true, "realized", &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->snvs), 0, FSL_IMX6UL_SNVS_HP_ADDR);

    /*
     * Watchdog
     */
    for (i = 0; i < FSL_IMX6UL_NUM_WDTS; i++) {
        static const hwaddr FSL_IMX6UL_WDOGn_ADDR[FSL_IMX6UL_NUM_WDTS] = {
            FSL_IMX6UL_WDOG1_ADDR,
            FSL_IMX6UL_WDOG2_ADDR,
            FSL_IMX6UL_WDOG3_ADDR,
        };

        object_property_set_bool(OBJECT(&s->wdt[i]), true, "realized",
                                 &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                        FSL_IMX6UL_WDOGn_ADDR[i]);
    }

    /*
     * GPR
     */
    object_property_set_bool(OBJECT(&s->gpr), true, "realized",
                             &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpr), 0, FSL_IMX6UL_IOMUXC_GPR_ADDR);

    /*
     * SDMA
     */
    create_unimplemented_device("sdma", FSL_IMX6UL_SDMA_ADDR, 0x4000);

    /*
     * APHB_DMA
     */
    create_unimplemented_device("aphb_dma", FSL_IMX6UL_APBH_DMA_ADDR,
                                FSL_IMX6UL_APBH_DMA_SIZE);

    /*
     * ADCs
     */
    for (i = 0; i < FSL_IMX6UL_NUM_ADCS; i++) {
        static const hwaddr FSL_IMX6UL_ADCn_ADDR[FSL_IMX6UL_NUM_ADCS] = {
            FSL_IMX6UL_ADC1_ADDR,
            FSL_IMX6UL_ADC2_ADDR,
        };

        snprintf(name, NAME_SIZE, "adc%d", i);
        create_unimplemented_device(name, FSL_IMX6UL_ADCn_ADDR[i], 0x4000);
    }

    /*
     * LCD
     */
    create_unimplemented_device("lcdif", FSL_IMX6UL_LCDIF_ADDR, 0x4000);

    /*
     * ROM memory
     */
    memory_region_init_rom(&s->rom, NULL, "imx6ul.rom",
                           FSL_IMX6UL_ROM_SIZE, &error_abort);
    memory_region_add_subregion(get_system_memory(), FSL_IMX6UL_ROM_ADDR,
                                &s->rom);

    /*
     * CAAM memory
     */
    memory_region_init_rom(&s->caam, NULL, "imx6ul.caam",
                           FSL_IMX6UL_CAAM_MEM_SIZE, &error_abort);
    memory_region_add_subregion(get_system_memory(), FSL_IMX6UL_CAAM_MEM_ADDR,
                                &s->caam);

    /*
     * OCRAM memory
     */
    memory_region_init_ram(&s->ocram, NULL, "imx6ul.ocram",
                           FSL_IMX6UL_OCRAM_MEM_SIZE,
                           &error_abort);
    memory_region_add_subregion(get_system_memory(), FSL_IMX6UL_OCRAM_MEM_ADDR,
                                &s->ocram);

    /*
     * internal OCRAM (128 KB) is aliased over 512 KB
     */
    memory_region_init_alias(&s->ocram_alias, NULL, "imx6ul.ocram_alias",
                             &s->ocram, 0, FSL_IMX6UL_OCRAM_ALIAS_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                FSL_IMX6UL_OCRAM_ALIAS_ADDR, &s->ocram_alias);
}

static void fsl_imx6ul_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = fsl_imx6ul_realize;
    dc->desc = "i.MX6UL SOC";
    /* Reason: Uses serial_hds and nd_table in realize() directly */
    dc->user_creatable = false;
}

static const TypeInfo fsl_imx6ul_type_info = {
    .name = TYPE_FSL_IMX6UL,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FslIMX6ULState),
    .instance_init = fsl_imx6ul_init,
    .class_init = fsl_imx6ul_class_init,
};

static void fsl_imx6ul_register_types(void)
{
    type_register_static(&fsl_imx6ul_type_info);
}
type_init(fsl_imx6ul_register_types)
