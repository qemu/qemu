/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * i.MX7 SoC definitions
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * Based on hw/arm/fsl-imx6.c
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
#include "hw/arm/fsl-imx7.h"
#include "hw/misc/unimp.h"
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "qemu/module.h"

#define NAME_SIZE 20

static void fsl_imx7_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    FslIMX7State *s = FSL_IMX7(obj);
    char name[NAME_SIZE];
    int i;

    for (i = 0; i < MIN(ms->smp.cpus, FSL_IMX7_NUM_CPUS); i++) {
        snprintf(name, NAME_SIZE, "cpu%d", i);
        object_initialize_child(obj, name, &s->cpu[i],
                                ARM_CPU_TYPE_NAME("cortex-a7"));
    }

    /*
     * A7MPCORE
     */
    object_initialize_child(obj, "a7mpcore", &s->a7mpcore,
                            TYPE_A15MPCORE_PRIV);

    /*
     * GPIOs 1 to 7
     */
    for (i = 0; i < FSL_IMX7_NUM_GPIOS; i++) {
        snprintf(name, NAME_SIZE, "gpio%d", i);
        object_initialize_child(obj, name, &s->gpio[i], TYPE_IMX_GPIO);
    }

    /*
     * GPT1, 2, 3, 4
     */
    for (i = 0; i < FSL_IMX7_NUM_GPTS; i++) {
        snprintf(name, NAME_SIZE, "gpt%d", i);
        object_initialize_child(obj, name, &s->gpt[i], TYPE_IMX7_GPT);
    }

    /*
     * CCM
     */
    object_initialize_child(obj, "ccm", &s->ccm, TYPE_IMX7_CCM);

    /*
     * Analog
     */
    object_initialize_child(obj, "analog", &s->analog, TYPE_IMX7_ANALOG);

    /*
     * GPCv2
     */
    object_initialize_child(obj, "gpcv2", &s->gpcv2, TYPE_IMX_GPCV2);

    for (i = 0; i < FSL_IMX7_NUM_ECSPIS; i++) {
        snprintf(name, NAME_SIZE, "spi%d", i + 1);
        object_initialize_child(obj, name, &s->spi[i], TYPE_IMX_SPI);
    }


    for (i = 0; i < FSL_IMX7_NUM_I2CS; i++) {
        snprintf(name, NAME_SIZE, "i2c%d", i + 1);
        object_initialize_child(obj, name, &s->i2c[i], TYPE_IMX_I2C);
    }

    /*
     * UART
     */
    for (i = 0; i < FSL_IMX7_NUM_UARTS; i++) {
            snprintf(name, NAME_SIZE, "uart%d", i);
            object_initialize_child(obj, name, &s->uart[i], TYPE_IMX_SERIAL);
    }

    /*
     * Ethernet
     */
    for (i = 0; i < FSL_IMX7_NUM_ETHS; i++) {
            snprintf(name, NAME_SIZE, "eth%d", i);
            object_initialize_child(obj, name, &s->eth[i], TYPE_IMX_ENET);
    }

    /*
     * SDHCI
     */
    for (i = 0; i < FSL_IMX7_NUM_USDHCS; i++) {
            snprintf(name, NAME_SIZE, "usdhc%d", i);
            object_initialize_child(obj, name, &s->usdhc[i], TYPE_IMX_USDHC);
    }

    /*
     * SNVS
     */
    object_initialize_child(obj, "snvs", &s->snvs, TYPE_IMX7_SNVS);

    /*
     * Watchdog
     */
    for (i = 0; i < FSL_IMX7_NUM_WDTS; i++) {
            snprintf(name, NAME_SIZE, "wdt%d", i);
            object_initialize_child(obj, name, &s->wdt[i], TYPE_IMX2_WDT);
    }

    /*
     * GPR
     */
    object_initialize_child(obj, "gpr", &s->gpr, TYPE_IMX7_GPR);

    object_initialize_child(obj, "pcie", &s->pcie, TYPE_DESIGNWARE_PCIE_HOST);

    for (i = 0; i < FSL_IMX7_NUM_USBS; i++) {
        snprintf(name, NAME_SIZE, "usb%d", i);
        object_initialize_child(obj, name, &s->usb[i], TYPE_CHIPIDEA);
    }
}

static void fsl_imx7_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    FslIMX7State *s = FSL_IMX7(dev);
    Object *o;
    int i;
    qemu_irq irq;
    char name[NAME_SIZE];
    unsigned int smp_cpus = ms->smp.cpus;

    if (smp_cpus > FSL_IMX7_NUM_CPUS) {
        error_setg(errp, "%s: Only %d CPUs are supported (%d requested)",
                   TYPE_FSL_IMX7, FSL_IMX7_NUM_CPUS, smp_cpus);
        return;
    }

    for (i = 0; i < smp_cpus; i++) {
        o = OBJECT(&s->cpu[i]);

        /* On uniprocessor, the CBAR is set to 0 */
        if (smp_cpus > 1) {
            object_property_set_int(o, "reset-cbar", FSL_IMX7_A7MPCORE_ADDR,
                                    &error_abort);
        }

        if (i) {
            /*
             * Secondary CPUs start in powered-down state (and can be
             * powered up via the SRC system reset controller)
             */
            object_property_set_bool(o, "start-powered-off", true,
                                     &error_abort);
        }

        qdev_realize(DEVICE(o), NULL, &error_abort);
    }

    /*
     * A7MPCORE
     */
    object_property_set_int(OBJECT(&s->a7mpcore), "num-cpu", smp_cpus,
                            &error_abort);
    object_property_set_int(OBJECT(&s->a7mpcore), "num-irq",
                            FSL_IMX7_MAX_IRQ + GIC_INTERNAL, &error_abort);

    sysbus_realize(SYS_BUS_DEVICE(&s->a7mpcore), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->a7mpcore), 0, FSL_IMX7_A7MPCORE_ADDR);

    for (i = 0; i < smp_cpus; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->a7mpcore);
        DeviceState  *d   = DEVICE(qemu_get_cpu(i));

        irq = qdev_get_gpio_in(d, ARM_CPU_IRQ);
        sysbus_connect_irq(sbd, i, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_FIQ);
        sysbus_connect_irq(sbd, i + smp_cpus, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_VIRQ);
        sysbus_connect_irq(sbd, i + 2 * smp_cpus, irq);
        irq = qdev_get_gpio_in(d, ARM_CPU_VFIQ);
        sysbus_connect_irq(sbd, i + 3 * smp_cpus, irq);
    }

    /*
     * A7MPCORE DAP
     */
    create_unimplemented_device("a7mpcore-dap", FSL_IMX7_A7MPCORE_DAP_ADDR,
                                0x100000);

    /*
     * GPT1, 2, 3, 4
     */
    for (i = 0; i < FSL_IMX7_NUM_GPTS; i++) {
        static const hwaddr FSL_IMX7_GPTn_ADDR[FSL_IMX7_NUM_GPTS] = {
            FSL_IMX7_GPT1_ADDR,
            FSL_IMX7_GPT2_ADDR,
            FSL_IMX7_GPT3_ADDR,
            FSL_IMX7_GPT4_ADDR,
        };

        static const int FSL_IMX7_GPTn_IRQ[FSL_IMX7_NUM_GPTS] = {
            FSL_IMX7_GPT1_IRQ,
            FSL_IMX7_GPT2_IRQ,
            FSL_IMX7_GPT3_IRQ,
            FSL_IMX7_GPT4_IRQ,
        };

        s->gpt[i].ccm = IMX_CCM(&s->ccm);
        sysbus_realize(SYS_BUS_DEVICE(&s->gpt[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpt[i]), 0, FSL_IMX7_GPTn_ADDR[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX7_GPTn_IRQ[i]));
    }

    for (i = 0; i < FSL_IMX7_NUM_GPIOS; i++) {
        static const hwaddr FSL_IMX7_GPIOn_ADDR[FSL_IMX7_NUM_GPIOS] = {
            FSL_IMX7_GPIO1_ADDR,
            FSL_IMX7_GPIO2_ADDR,
            FSL_IMX7_GPIO3_ADDR,
            FSL_IMX7_GPIO4_ADDR,
            FSL_IMX7_GPIO5_ADDR,
            FSL_IMX7_GPIO6_ADDR,
            FSL_IMX7_GPIO7_ADDR,
        };

        static const int FSL_IMX7_GPIOn_LOW_IRQ[FSL_IMX7_NUM_GPIOS] = {
            FSL_IMX7_GPIO1_LOW_IRQ,
            FSL_IMX7_GPIO2_LOW_IRQ,
            FSL_IMX7_GPIO3_LOW_IRQ,
            FSL_IMX7_GPIO4_LOW_IRQ,
            FSL_IMX7_GPIO5_LOW_IRQ,
            FSL_IMX7_GPIO6_LOW_IRQ,
            FSL_IMX7_GPIO7_LOW_IRQ,
        };

        static const int FSL_IMX7_GPIOn_HIGH_IRQ[FSL_IMX7_NUM_GPIOS] = {
            FSL_IMX7_GPIO1_HIGH_IRQ,
            FSL_IMX7_GPIO2_HIGH_IRQ,
            FSL_IMX7_GPIO3_HIGH_IRQ,
            FSL_IMX7_GPIO4_HIGH_IRQ,
            FSL_IMX7_GPIO5_HIGH_IRQ,
            FSL_IMX7_GPIO6_HIGH_IRQ,
            FSL_IMX7_GPIO7_HIGH_IRQ,
        };

        sysbus_realize(SYS_BUS_DEVICE(&s->gpio[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                        FSL_IMX7_GPIOn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX7_GPIOn_LOW_IRQ[i]));

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 1,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX7_GPIOn_HIGH_IRQ[i]));
    }

    /*
     * IOMUXC and IOMUXC_LPSR
     */
    for (i = 0; i < FSL_IMX7_NUM_IOMUXCS; i++) {
        static const hwaddr FSL_IMX7_IOMUXCn_ADDR[FSL_IMX7_NUM_IOMUXCS] = {
            FSL_IMX7_IOMUXC_ADDR,
            FSL_IMX7_IOMUXC_LPSR_ADDR,
        };

        snprintf(name, NAME_SIZE, "iomuxc%d", i);
        create_unimplemented_device(name, FSL_IMX7_IOMUXCn_ADDR[i],
                                    FSL_IMX7_IOMUXCn_SIZE);
    }

    /*
     * CCM
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->ccm), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0, FSL_IMX7_CCM_ADDR);

    /*
     * Analog
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->analog), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->analog), 0, FSL_IMX7_ANALOG_ADDR);

    /*
     * GPCv2
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->gpcv2), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpcv2), 0, FSL_IMX7_GPC_ADDR);

    /* Initialize all ECSPI */
    for (i = 0; i < FSL_IMX7_NUM_ECSPIS; i++) {
        static const hwaddr FSL_IMX7_SPIn_ADDR[FSL_IMX7_NUM_ECSPIS] = {
            FSL_IMX7_ECSPI1_ADDR,
            FSL_IMX7_ECSPI2_ADDR,
            FSL_IMX7_ECSPI3_ADDR,
            FSL_IMX7_ECSPI4_ADDR,
        };

        static const int FSL_IMX7_SPIn_IRQ[FSL_IMX7_NUM_ECSPIS] = {
            FSL_IMX7_ECSPI1_IRQ,
            FSL_IMX7_ECSPI2_IRQ,
            FSL_IMX7_ECSPI3_IRQ,
            FSL_IMX7_ECSPI4_IRQ,
        };

        /* Initialize the SPI */
        sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[i]), 0,
                        FSL_IMX7_SPIn_ADDR[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX7_SPIn_IRQ[i]));
    }

    for (i = 0; i < FSL_IMX7_NUM_I2CS; i++) {
        static const hwaddr FSL_IMX7_I2Cn_ADDR[FSL_IMX7_NUM_I2CS] = {
            FSL_IMX7_I2C1_ADDR,
            FSL_IMX7_I2C2_ADDR,
            FSL_IMX7_I2C3_ADDR,
            FSL_IMX7_I2C4_ADDR,
        };

        static const int FSL_IMX7_I2Cn_IRQ[FSL_IMX7_NUM_I2CS] = {
            FSL_IMX7_I2C1_IRQ,
            FSL_IMX7_I2C2_IRQ,
            FSL_IMX7_I2C3_IRQ,
            FSL_IMX7_I2C4_IRQ,
        };

        sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[i]), 0, FSL_IMX7_I2Cn_ADDR[i]);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX7_I2Cn_IRQ[i]));
    }

    /*
     * UART
     */
    for (i = 0; i < FSL_IMX7_NUM_UARTS; i++) {
        static const hwaddr FSL_IMX7_UARTn_ADDR[FSL_IMX7_NUM_UARTS] = {
            FSL_IMX7_UART1_ADDR,
            FSL_IMX7_UART2_ADDR,
            FSL_IMX7_UART3_ADDR,
            FSL_IMX7_UART4_ADDR,
            FSL_IMX7_UART5_ADDR,
            FSL_IMX7_UART6_ADDR,
            FSL_IMX7_UART7_ADDR,
        };

        static const int FSL_IMX7_UARTn_IRQ[FSL_IMX7_NUM_UARTS] = {
            FSL_IMX7_UART1_IRQ,
            FSL_IMX7_UART2_IRQ,
            FSL_IMX7_UART3_IRQ,
            FSL_IMX7_UART4_IRQ,
            FSL_IMX7_UART5_IRQ,
            FSL_IMX7_UART6_IRQ,
            FSL_IMX7_UART7_IRQ,
        };


        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));

        sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, FSL_IMX7_UARTn_ADDR[i]);

        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_UARTn_IRQ[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0, irq);
    }

    /*
     * Ethernet
     */
    for (i = 0; i < FSL_IMX7_NUM_ETHS; i++) {
        static const hwaddr FSL_IMX7_ENETn_ADDR[FSL_IMX7_NUM_ETHS] = {
            FSL_IMX7_ENET1_ADDR,
            FSL_IMX7_ENET2_ADDR,
        };

        object_property_set_uint(OBJECT(&s->eth[i]), "phy-num",
                                 s->phy_num[i], &error_abort);
        object_property_set_uint(OBJECT(&s->eth[i]), "tx-ring-num",
                                 FSL_IMX7_ETH_NUM_TX_RINGS, &error_abort);
        qdev_set_nic_properties(DEVICE(&s->eth[i]), &nd_table[i]);
        sysbus_realize(SYS_BUS_DEVICE(&s->eth[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->eth[i]), 0, FSL_IMX7_ENETn_ADDR[i]);

        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_ENET_IRQ(i, 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 0, irq);
        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_ENET_IRQ(i, 3));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->eth[i]), 1, irq);
    }

    /*
     * USDHC
     */
    for (i = 0; i < FSL_IMX7_NUM_USDHCS; i++) {
        static const hwaddr FSL_IMX7_USDHCn_ADDR[FSL_IMX7_NUM_USDHCS] = {
            FSL_IMX7_USDHC1_ADDR,
            FSL_IMX7_USDHC2_ADDR,
            FSL_IMX7_USDHC3_ADDR,
        };

        static const int FSL_IMX7_USDHCn_IRQ[FSL_IMX7_NUM_USDHCS] = {
            FSL_IMX7_USDHC1_IRQ,
            FSL_IMX7_USDHC2_IRQ,
            FSL_IMX7_USDHC3_IRQ,
        };

        object_property_set_uint(OBJECT(&s->usdhc[i]), "vendor",
                                 SDHCI_VENDOR_IMX, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->usdhc[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usdhc[i]), 0,
                        FSL_IMX7_USDHCn_ADDR[i]);

        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_USDHCn_IRQ[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usdhc[i]), 0, irq);
    }

    /*
     * SNVS
     */
    sysbus_realize(SYS_BUS_DEVICE(&s->snvs), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->snvs), 0, FSL_IMX7_SNVS_ADDR);

    /*
     * SRC
     */
    create_unimplemented_device("src", FSL_IMX7_SRC_ADDR, FSL_IMX7_SRC_SIZE);

    /*
     * Watchdog
     */
    for (i = 0; i < FSL_IMX7_NUM_WDTS; i++) {
        static const hwaddr FSL_IMX7_WDOGn_ADDR[FSL_IMX7_NUM_WDTS] = {
            FSL_IMX7_WDOG1_ADDR,
            FSL_IMX7_WDOG2_ADDR,
            FSL_IMX7_WDOG3_ADDR,
            FSL_IMX7_WDOG4_ADDR,
        };
        static const int FSL_IMX7_WDOGn_IRQ[FSL_IMX7_NUM_WDTS] = {
            FSL_IMX7_WDOG1_IRQ,
            FSL_IMX7_WDOG2_IRQ,
            FSL_IMX7_WDOG3_IRQ,
            FSL_IMX7_WDOG4_IRQ,
        };

        object_property_set_bool(OBJECT(&s->wdt[i]), "pretimeout-support",
                                 true, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->wdt[i]), &error_abort);

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->wdt[i]), 0, FSL_IMX7_WDOGn_ADDR[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->wdt[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->a7mpcore),
                                            FSL_IMX7_WDOGn_IRQ[i]));
    }

    /*
     * SDMA
     */
    create_unimplemented_device("sdma", FSL_IMX7_SDMA_ADDR, FSL_IMX7_SDMA_SIZE);

    /*
     * CAAM
     */
    create_unimplemented_device("caam", FSL_IMX7_CAAM_ADDR, FSL_IMX7_CAAM_SIZE);

    /*
     * PWM
     */
    create_unimplemented_device("pwm1", FSL_IMX7_PWM1_ADDR, FSL_IMX7_PWMn_SIZE);
    create_unimplemented_device("pwm2", FSL_IMX7_PWM2_ADDR, FSL_IMX7_PWMn_SIZE);
    create_unimplemented_device("pwm3", FSL_IMX7_PWM3_ADDR, FSL_IMX7_PWMn_SIZE);
    create_unimplemented_device("pwm4", FSL_IMX7_PWM4_ADDR, FSL_IMX7_PWMn_SIZE);

    /*
     * CAN
     */
    create_unimplemented_device("can1", FSL_IMX7_CAN1_ADDR, FSL_IMX7_CANn_SIZE);
    create_unimplemented_device("can2", FSL_IMX7_CAN2_ADDR, FSL_IMX7_CANn_SIZE);

    /*
     * SAI (Audio SSI (Synchronous Serial Interface))
     */
    create_unimplemented_device("sai1", FSL_IMX7_SAI1_ADDR, FSL_IMX7_SAIn_SIZE);
    create_unimplemented_device("sai2", FSL_IMX7_SAI2_ADDR, FSL_IMX7_SAIn_SIZE);
    create_unimplemented_device("sai2", FSL_IMX7_SAI3_ADDR, FSL_IMX7_SAIn_SIZE);

    /*
     * OCOTP
     */
    create_unimplemented_device("ocotp", FSL_IMX7_OCOTP_ADDR,
                                FSL_IMX7_OCOTP_SIZE);

    sysbus_realize(SYS_BUS_DEVICE(&s->gpr), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpr), 0, FSL_IMX7_GPR_ADDR);

    sysbus_realize(SYS_BUS_DEVICE(&s->pcie), &error_abort);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->pcie), 0, FSL_IMX7_PCIE_REG_ADDR);

    irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_PCI_INTA_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 0, irq);
    irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_PCI_INTB_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 1, irq);
    irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_PCI_INTC_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 2, irq);
    irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_PCI_INTD_IRQ);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pcie), 3, irq);


    for (i = 0; i < FSL_IMX7_NUM_USBS; i++) {
        static const hwaddr FSL_IMX7_USBMISCn_ADDR[FSL_IMX7_NUM_USBS] = {
            FSL_IMX7_USBMISC1_ADDR,
            FSL_IMX7_USBMISC2_ADDR,
            FSL_IMX7_USBMISC3_ADDR,
        };

        static const hwaddr FSL_IMX7_USBn_ADDR[FSL_IMX7_NUM_USBS] = {
            FSL_IMX7_USB1_ADDR,
            FSL_IMX7_USB2_ADDR,
            FSL_IMX7_USB3_ADDR,
        };

        static const int FSL_IMX7_USBn_IRQ[FSL_IMX7_NUM_USBS] = {
            FSL_IMX7_USB1_IRQ,
            FSL_IMX7_USB2_IRQ,
            FSL_IMX7_USB3_IRQ,
        };

        sysbus_realize(SYS_BUS_DEVICE(&s->usb[i]), &error_abort);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->usb[i]), 0,
                        FSL_IMX7_USBn_ADDR[i]);

        irq = qdev_get_gpio_in(DEVICE(&s->a7mpcore), FSL_IMX7_USBn_IRQ[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usb[i]), 0, irq);

        snprintf(name, NAME_SIZE, "usbmisc%d", i);
        create_unimplemented_device(name, FSL_IMX7_USBMISCn_ADDR[i],
                                    FSL_IMX7_USBMISCn_SIZE);
    }

    /*
     * ADCs
     */
    for (i = 0; i < FSL_IMX7_NUM_ADCS; i++) {
        static const hwaddr FSL_IMX7_ADCn_ADDR[FSL_IMX7_NUM_ADCS] = {
            FSL_IMX7_ADC1_ADDR,
            FSL_IMX7_ADC2_ADDR,
        };

        snprintf(name, NAME_SIZE, "adc%d", i);
        create_unimplemented_device(name, FSL_IMX7_ADCn_ADDR[i],
                                    FSL_IMX7_ADCn_SIZE);
    }

    /*
     * LCD
     */
    create_unimplemented_device("lcdif", FSL_IMX7_LCDIF_ADDR,
                                FSL_IMX7_LCDIF_SIZE);

    /*
     * DMA APBH
     */
    create_unimplemented_device("dma-apbh", FSL_IMX7_DMA_APBH_ADDR,
                                FSL_IMX7_DMA_APBH_SIZE);
    /*
     * PCIe PHY
     */
    create_unimplemented_device("pcie-phy", FSL_IMX7_PCIE_PHY_ADDR,
                                FSL_IMX7_PCIE_PHY_SIZE);
}

static Property fsl_imx7_properties[] = {
    DEFINE_PROP_UINT32("fec1-phy-num", FslIMX7State, phy_num[0], 0),
    DEFINE_PROP_UINT32("fec2-phy-num", FslIMX7State, phy_num[1], 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void fsl_imx7_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, fsl_imx7_properties);
    dc->realize = fsl_imx7_realize;

    /* Reason: Uses serial_hds and nd_table in realize() directly */
    dc->user_creatable = false;
    dc->desc = "i.MX7 SOC";
}

static const TypeInfo fsl_imx7_type_info = {
    .name = TYPE_FSL_IMX7,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FslIMX7State),
    .instance_init = fsl_imx7_init,
    .class_init = fsl_imx7_class_init,
};

static void fsl_imx7_register_types(void)
{
    type_register_static(&fsl_imx7_type_info);
}
type_init(fsl_imx7_register_types)
