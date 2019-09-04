/*
 * Copyright (c) 2013 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * i.MX25 SOC emulation.
 *
 * Based on hw/arm/xlnx-zynqmp.c
 *
 * Copyright (C) 2015 Xilinx Inc
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/arm/fsl-imx25.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/qdev-properties.h"
#include "chardev/char.h"

static void fsl_imx25_init(Object *obj)
{
    FslIMX25State *s = FSL_IMX25(obj);
    int i;

    object_initialize_child(obj, "cpu", &s->cpu, sizeof(s->cpu),
                            ARM_CPU_TYPE_NAME("arm926"),
                            &error_abort, NULL);

    sysbus_init_child_obj(obj, "avic", &s->avic, sizeof(s->avic),
                          TYPE_IMX_AVIC);

    sysbus_init_child_obj(obj, "ccm", &s->ccm, sizeof(s->ccm), TYPE_IMX25_CCM);

    for (i = 0; i < FSL_IMX25_NUM_UARTS; i++) {
        sysbus_init_child_obj(obj, "uart[*]", &s->uart[i], sizeof(s->uart[i]),
                              TYPE_IMX_SERIAL);
    }

    for (i = 0; i < FSL_IMX25_NUM_GPTS; i++) {
        sysbus_init_child_obj(obj, "gpt[*]", &s->gpt[i], sizeof(s->gpt[i]),
                              TYPE_IMX25_GPT);
    }

    for (i = 0; i < FSL_IMX25_NUM_EPITS; i++) {
        sysbus_init_child_obj(obj, "epit[*]", &s->epit[i], sizeof(s->epit[i]),
                              TYPE_IMX_EPIT);
    }

    sysbus_init_child_obj(obj, "fec", &s->fec, sizeof(s->fec), TYPE_IMX_FEC);

    for (i = 0; i < FSL_IMX25_NUM_I2CS; i++) {
        sysbus_init_child_obj(obj, "i2c[*]", &s->i2c[i], sizeof(s->i2c[i]),
                              TYPE_IMX_I2C);
    }

    for (i = 0; i < FSL_IMX25_NUM_GPIOS; i++) {
        sysbus_init_child_obj(obj, "gpio[*]", &s->gpio[i], sizeof(s->gpio[i]),
                              TYPE_IMX_GPIO);
    }
}

static void fsl_imx25_realize(DeviceState *dev, Error **errp)
{
    FslIMX25State *s = FSL_IMX25(dev);
    uint8_t i;
    Error *err = NULL;

    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->avic), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->avic), 0, FSL_IMX25_AVIC_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->avic), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->avic), 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));

    object_property_set_bool(OBJECT(&s->ccm), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccm), 0, FSL_IMX25_CCM_ADDR);

    /* Initialize all UARTs */
    for (i = 0; i < FSL_IMX25_NUM_UARTS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } serial_table[FSL_IMX25_NUM_UARTS] = {
            { FSL_IMX25_UART1_ADDR, FSL_IMX25_UART1_IRQ },
            { FSL_IMX25_UART2_ADDR, FSL_IMX25_UART2_IRQ },
            { FSL_IMX25_UART3_ADDR, FSL_IMX25_UART3_IRQ },
            { FSL_IMX25_UART4_ADDR, FSL_IMX25_UART4_IRQ },
            { FSL_IMX25_UART5_ADDR, FSL_IMX25_UART5_IRQ }
        };

        qdev_prop_set_chr(DEVICE(&s->uart[i]), "chardev", serial_hd(i));

        object_property_set_bool(OBJECT(&s->uart[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart[i]), 0, serial_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->avic),
                                            serial_table[i].irq));
    }

    /* Initialize all GPT timers */
    for (i = 0; i < FSL_IMX25_NUM_GPTS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } gpt_table[FSL_IMX25_NUM_GPTS] = {
            { FSL_IMX25_GPT1_ADDR, FSL_IMX25_GPT1_IRQ },
            { FSL_IMX25_GPT2_ADDR, FSL_IMX25_GPT2_IRQ },
            { FSL_IMX25_GPT3_ADDR, FSL_IMX25_GPT3_IRQ },
            { FSL_IMX25_GPT4_ADDR, FSL_IMX25_GPT4_IRQ }
        };

        s->gpt[i].ccm = IMX_CCM(&s->ccm);

        object_property_set_bool(OBJECT(&s->gpt[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpt[i]), 0, gpt_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpt[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->avic),
                                            gpt_table[i].irq));
    }

    /* Initialize all EPIT timers */
    for (i = 0; i < FSL_IMX25_NUM_EPITS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } epit_table[FSL_IMX25_NUM_EPITS] = {
            { FSL_IMX25_EPIT1_ADDR, FSL_IMX25_EPIT1_IRQ },
            { FSL_IMX25_EPIT2_ADDR, FSL_IMX25_EPIT2_IRQ }
        };

        s->epit[i].ccm = IMX_CCM(&s->ccm);

        object_property_set_bool(OBJECT(&s->epit[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->epit[i]), 0, epit_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->epit[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->avic),
                                            epit_table[i].irq));
    }

    qdev_set_nic_properties(DEVICE(&s->fec), &nd_table[0]);

    object_property_set_bool(OBJECT(&s->fec), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->fec), 0, FSL_IMX25_FEC_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->fec), 0,
                       qdev_get_gpio_in(DEVICE(&s->avic), FSL_IMX25_FEC_IRQ));


    /* Initialize all I2C */
    for (i = 0; i < FSL_IMX25_NUM_I2CS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } i2c_table[FSL_IMX25_NUM_I2CS] = {
            { FSL_IMX25_I2C1_ADDR, FSL_IMX25_I2C1_IRQ },
            { FSL_IMX25_I2C2_ADDR, FSL_IMX25_I2C2_IRQ },
            { FSL_IMX25_I2C3_ADDR, FSL_IMX25_I2C3_IRQ }
        };

        object_property_set_bool(OBJECT(&s->i2c[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[i]), 0, i2c_table[i].addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2c[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->avic),
                                            i2c_table[i].irq));
    }

    /* Initialize all GPIOs */
    for (i = 0; i < FSL_IMX25_NUM_GPIOS; i++) {
        static const struct {
            hwaddr addr;
            unsigned int irq;
        } gpio_table[FSL_IMX25_NUM_GPIOS] = {
            { FSL_IMX25_GPIO1_ADDR, FSL_IMX25_GPIO1_IRQ },
            { FSL_IMX25_GPIO2_ADDR, FSL_IMX25_GPIO2_IRQ },
            { FSL_IMX25_GPIO3_ADDR, FSL_IMX25_GPIO3_IRQ },
            { FSL_IMX25_GPIO4_ADDR, FSL_IMX25_GPIO4_IRQ }
        };

        object_property_set_bool(OBJECT(&s->gpio[i]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio[i]), 0, gpio_table[i].addr);
        /* Connect GPIO IRQ to PIC */
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->avic),
                                            gpio_table[i].irq));
    }

    /* initialize 2 x 16 KB ROM */
    memory_region_init_rom(&s->rom[0], NULL,
                           "imx25.rom0", FSL_IMX25_ROM0_SIZE, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(get_system_memory(), FSL_IMX25_ROM0_ADDR,
                                &s->rom[0]);
    memory_region_init_rom(&s->rom[1], NULL,
                           "imx25.rom1", FSL_IMX25_ROM1_SIZE, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(get_system_memory(), FSL_IMX25_ROM1_ADDR,
                                &s->rom[1]);

    /* initialize internal RAM (128 KB) */
    memory_region_init_ram(&s->iram, NULL, "imx25.iram", FSL_IMX25_IRAM_SIZE,
                           &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(get_system_memory(), FSL_IMX25_IRAM_ADDR,
                                &s->iram);

    /* internal RAM (128 KB) is aliased over 128 MB - 128 KB */
    memory_region_init_alias(&s->iram_alias, NULL, "imx25.iram_alias",
                             &s->iram, 0, FSL_IMX25_IRAM_ALIAS_SIZE);
    memory_region_add_subregion(get_system_memory(), FSL_IMX25_IRAM_ALIAS_ADDR,
                                &s->iram_alias);
}

static void fsl_imx25_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = fsl_imx25_realize;
    dc->desc = "i.MX25 SOC";
    /*
     * Reason: uses serial_hds in realize and the imx25 board does not
     * support multiple CPUs
     */
    dc->user_creatable = false;
}

static const TypeInfo fsl_imx25_type_info = {
    .name = TYPE_FSL_IMX25,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(FslIMX25State),
    .instance_init = fsl_imx25_init,
    .class_init = fsl_imx25_class_init,
};

static void fsl_imx25_register_types(void)
{
    type_register_static(&fsl_imx25_type_info);
}

type_init(fsl_imx25_register_types)
