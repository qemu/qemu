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
#include "hw/usb/imx-usb-phy.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

#define NAME_SIZE 20

static void fsl_imx6ul_init(Object *obj)
{
    FslIMX6ULState *s = FSL_IMX6UL(obj);
    char name[NAME_SIZE];
    int i;

    object_initialize_child(obj, "cpu0", &s->cpu,
                            ARM_CPU_TYPE_NAME("cortex-a7"));

    /*
     * A7MPCORE
     */
    object_initialize_child(obj, "a7mpcore", &s->a7mpcore,
                            TYPE_A15MPCORE_PRIV);

    /*
     * CCM
     */
    object_initialize_child(obj, "ccm", &s->ccm, TYPE_IMX6UL_CCM);

    /*
     * SRC
     */
    object_initialize_child(obj, "src", &s->src, TYPE_IMX6_SRC);

    /*
     * GPCv2
     */
    object_initialize_child(obj, "gpcv2", &s->gpcv2, TYPE_IMX_GPCV2);

    /*
     * SNVS
     */
    object_initialize_child(obj, "snvs", &s->snvs, TYPE_IMX7_SNVS);

    /*
     * GPR
     */
    object_initialize_child(obj, "gpr", &s->gpr, TYPE_IMX7_GPR);

    /*
     * GPIOs 1 to 5
     */
    for (i = 0; i < FSL_IMX6UL_NUM_GPIOS; i++) {
        snprintf(name, NAME_SIZE, "gpio%d", i);
        object_initialize_child(obj, name, &s->gpio[i], TYPE_IMX_GPIO);
    }

    /*
     * GPT 1, 2
     */
    for (i = 0; i < FSL_IMX6UL_NUM_GPTS; i++) {
        snprintf(name, NAME_SIZE, "gpt%d", i);
        object_initialize_child(obj, name, &s->gpt[i], TYPE_IMX7_GPT);
    }

    /*
     * EPIT 1, 2
     */
    for (i = 0; i < FSL_IMX6UL_NUM_EPITS; i++) {
        snprintf(name, NAME_SIZE, "epit%d", i + 1);
        object_initialize_child(obj, name, &s->epit[i], TYPE_IMX_EPIT);
    }

    /*
     * eCSPI
     */
    for (i = 0; i < FSL_IMX6UL_NUM_ECSPIS; i++) {
        snprintf(name, NAME_SIZE, "spi%d", i + 1);
        object_initialize_child(obj, name, &s->spi[i], TYPE_IMX_SPI);
    }

    /*
     * I2C
     */
    for (i = 0; i < FSL_IMX6UL_NUM_I2CS; i++) {
        snprintf(name, NAME_SIZE, "i2c%d", i + 1);
        object_initialize_child(obj, name, &s->i2c[i], TYPE_IMX_I2C);
    }

    /*
     * UART
     */
    for (i = 0; i < FSL_IMX6UL_NUM_UARTS; i++) {
        snprintf(name, NAME_SIZE, "uart%d", i);
        object_initialize_child(obj, name, &s->uart[i], TYPE_IMX_SERIAL);
    }

    /*
     * Ethernet
     */
    for (i = 0; i < FSL_IMX6UL_NUM_ETHS; i++) {
        snprintf(name, NAME_SIZE, "eth%d", i);
        object_initialize_child(obj, name, &s->eth[i], TYPE_IMX_ENET);
    }

    /* USB */
    for (i = 0; i < FSL_IMX6UL_NUM_USB_PHYS; i++) {
        snprintf(name, NAME_SIZE, "usbphy%d", i);
        object_initialize_child(obj, name, &s->usbphy[i], TYPE_IMX_USBPHY);
    }
    for (i = 0; i < FSL_IMX6UL_NUM_USBS; i++) {
        snprintf(name, NAME_SIZE, "usb%d", i);
        object_initialize_child(obj, name, &s->usb[i], TYPE_CHIPIDEA);
    }

    /*
     * SDHCI
     */
    for (i = 0; i < FSL_IMX6UL_NUM_USDHCS; i++) {
        snprintf(name, NAME_SIZE, "usdhc%d", i);
        object_initialize_child(obj, name, &s->usdhc[i], TYPE_IMX_USDHC);
    }

    /*
     * Watchdog
     */
    for (i = 0; i < FSL_IMX6UL_NUM_WDTS; i++) {
        snprintf(name, NAME_SIZE, "wdt%d", i);
        object_initialize_child(obj, name, &s->wdt[i], TYPE_IMX2_WDT);
    }
}

static void fsl_imx6ul_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    FslIMX6ULState *s = FSL_IMX6UL(dev);
    int i;
    char name[NAME_SIZE];
    SysBusDevice *sbd;
    DeviceState *d;

    if (ms->smp.cpus > 1) {
        error_setg(errp, "%s: Only a single CPU is supported (%d requested)",
                   TYPE_FSL_IMX6UL, ms->smp.cpus);
        return;
    }

    qdev_realize(DEVICE(&s->cpu), NULL, &error_abort);

    /*
     * A7MPCORE
     */
    object_property_set_int(OBJECT(&s->a7mpcore), "num-cpu", 1, &error_abort);
    object_property_set_int(OBJECT(&s->a7mpcore), "num-irq",
                            FSL_IMX6UL_MAX_IRQ + GIC_INTERNAL, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->a7mpcore), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a7mpcore), 0, FSL_IMX6UL_A7MPCORE_ADDR);

    sbd = SYS_BUS_DEVICE(&s->a7mpcore);
    d = DEVICE(&s->cpu);

    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(d, ARM_CPU_IRQ));
    sysbus_connect_irq(sbd, 1, qdev_get_gpio_in(d, ARM_CPU_FIQ));
    sysbus_connect_irq(sbd, 2, qdev_get_gpio_in(d, ARM_CPU_VIRQ));
    sysbus_connect_irq(sbd, 3, qdev_get_gpio_in(d, ARM_CPU_VFIQ));

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
        sysbus_realize(SYS_BUS_DEVICE(&s->gpt[i]), &error_abort);

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
        sysbus_realize(SYS_BUS_DEVICE(&s->epit[i]), &error_abort);

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

        sysbus_realize(SYS_BUS_DEVICE(&s->gpio[i]), &error_abort);

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
    sysbus_realize(SYS_BUS_DEVICE(&s->ccm), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0, FSL_IMX6UL_CCM_ADDR);

    /*
     * SRC
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->src), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->src), 0, FSL_IMX6UL_SRC_ADDR);

    /*
     * GPCv2
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->gpcv2), &error_abort);
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
        sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), &error_abort);

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

        sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), &error_abort);
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

        sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), &error_abort);

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

        object_property_set_uint(OBJECT(&s->eth[i]), "phy-num",
                                 s->phy_num[i], &error_abort);
        object_property_set_uint(OBJECT(&s->eth[i]), "tx-ring-num",
                                 FSL_IMX6UL_ETH_NUM_TX_RINGS, &error_abort);
        qdev_set_nic_properties(DEVICE(&s->eth[i]), &nd_table[i]);
        sysbus_realize(SYS_BUS_DEVICE(&s->eth[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->eth[i]), 0,
                        FSL_IMX6UL_ENETn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_ENETn_IRQ[i]));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 1,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_ENETn_TIMER_IRQ[i]));
    }

    /* USB */
    for (i = 0; i < FSL_IMX6UL_NUM_USB_PHYS; i++) {
        sysbus_realize(SYS_BUS_DEVICE(&s->usbphy[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usbphy[i]), 0,
                        FSL_IMX6UL_USBPHY1_ADDR + i * 0x1000);
    }

    for (i = 0; i < FSL_IMX6UL_NUM_USBS; i++) {
        static const int FSL_IMX6UL_USBn_IRQ[] = {
            FSL_IMX6UL_USB1_IRQ,
            FSL_IMX6UL_USB2_IRQ,
        };
        sysbus_realize(SYS_BUS_DEVICE(&s->usb[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb[i]), 0,
                        FSL_IMX6UL_USBO2_USB_ADDR + i * 0x200);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_USBn_IRQ[i]));
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

        object_property_set_uint(OBJECT(&s->usdhc[i]), "vendor",
                                 SDHCI_VENDOR_IMX, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->usdhc[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                        FSL_IMX6UL_USDHCn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_USDHCn_IRQ[i]));
    }

    /*
     * SNVS
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->snvs), &error_abort);
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
        static const int FSL_IMX6UL_WDOGn_IRQ[FSL_IMX6UL_NUM_WDTS] = {
            FSL_IMX6UL_WDOG1_IRQ,
            FSL_IMX6UL_WDOG2_IRQ,
            FSL_IMX6UL_WDOG3_IRQ,
        };

        object_property_set_bool(OBJECT(&s->wdt[i]), "pretimeout-support",
                                 true, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                        FSL_IMX6UL_WDOGn_ADDR[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX6UL_WDOGn_IRQ[i]));
    }

    /*
     * GPR
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->gpr), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpr), 0, FSL_IMX6UL_IOMUXC_GPR_ADDR);

    /*
     * SDMA
     */
    create_unimplemented_device("sdma", FSL_IMX6UL_SDMA_ADDR, 0x4000);

    /*
     * SAI (Audio SSI (Synchronous Serial Interface))
     */
    create_unimplemented_device("sai1", FSL_IMX6UL_SAI1_ADDR, 0x4000);
    create_unimplemented_device("sai2", FSL_IMX6UL_SAI2_ADDR, 0x4000);
    create_unimplemented_device("sai3", FSL_IMX6UL_SAI3_ADDR, 0x4000);

    /*
     * PWM
     */
    create_unimplemented_device("pwm1", FSL_IMX6UL_PWM1_ADDR, 0x4000);
    create_unimplemented_device("pwm2", FSL_IMX6UL_PWM2_ADDR, 0x4000);
    create_unimplemented_device("pwm3", FSL_IMX6UL_PWM3_ADDR, 0x4000);
    create_unimplemented_device("pwm4", FSL_IMX6UL_PWM4_ADDR, 0x4000);

    /*
     * Audio ASRC (asynchronous sample rate converter)
     */
    create_unimplemented_device("asrc", FSL_IMX6UL_ASRC_ADDR, 0x4000);

    /*
     * CAN
     */
    create_unimplemented_device("can1", FSL_IMX6UL_CAN1_ADDR, 0x4000);
    create_unimplemented_device("can2", FSL_IMX6UL_CAN2_ADDR, 0x4000);

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
    memory_region_init_rom(&s->rom, OBJECT(dev), "imx6ul.rom",
                           FSL_IMX6UL_ROM_SIZE, &error_abort);
    memory_region_add_subregion(get_system_memory(), FSL_IMX6UL_ROM_ADDR,
                                &s->rom);

    /*
     * CAAM memory
     */
    memory_region_init_rom(&s->caam, OBJECT(dev), "imx6ul.caam",
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
    memory_region_init_alias(&s->ocram_alias, OBJECT(dev),
                             "imx6ul.ocram_alias", &s->ocram, 0,
                             FSL_IMX6UL_OCRAM_ALIAS_SIZE);
    memory_region_add_subregion(get_system_memory(),
                                FSL_IMX6UL_OCRAM_ALIAS_ADDR, &s->ocram_alias);
}

static Property fsl_imx6ul_properties[] = {
    DEFINE_PROP_UINT32("fec1-phy-num", FslIMX6ULState, phy_num[0], 0),
    DEFINE_PROP_UINT32("fec2-phy-num", FslIMX6ULState, phy_num[1], 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void fsl_imx6ul_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, fsl_imx6ul_properties);
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
