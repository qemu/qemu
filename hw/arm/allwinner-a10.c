/*
 * Allwinner A10 SoC emulation
 *
 * Copyright (C) 2013 Li Guang
 * Written by Li Guang <lig.fnst@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/allwinner-a10.h"
#include "hw/misc/unimp.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/usb/hcd-ohci.h"

#define AW_A10_MMC0_BASE        0x01c0f000
#define AW_A10_PIC_REG_BASE     0x01c20400
#define AW_A10_PIT_REG_BASE     0x01c20c00
#define AW_A10_UART0_REG_BASE   0x01c28000
#define AW_A10_EMAC_BASE        0x01c0b000
#define AW_A10_EHCI_BASE        0x01c14000
#define AW_A10_OHCI_BASE        0x01c14400
#define AW_A10_SATA_BASE        0x01c18000
#define AW_A10_RTC_BASE         0x01c20d00

static void aw_a10_init(Object *obj)
{
    AwA10State *s = AW_A10(obj);

    object_initialize_child(obj, "cpu", &s->cpu,
                            ARM_CPU_TYPE_NAME("cortex-a8"));

    object_initialize_child(obj, "intc", &s->intc, TYPE_AW_A10_PIC);

    object_initialize_child(obj, "timer", &s->timer, TYPE_AW_A10_PIT);

    object_initialize_child(obj, "emac", &s->emac, TYPE_AW_EMAC);

    object_initialize_child(obj, "sata", &s->sata, TYPE_ALLWINNER_AHCI);

    if (machine_usb(current_machine)) {
        int i;

        for (i = 0; i < AW_A10_NUM_USB; i++) {
            object_initialize_child(obj, "ehci[*]", &s->ehci[i],
                                    TYPE_PLATFORM_EHCI);
            object_initialize_child(obj, "ohci[*]", &s->ohci[i],
                                    TYPE_SYSBUS_OHCI);
        }
    }

    object_initialize_child(obj, "mmc0", &s->mmc0, TYPE_AW_SDHOST_SUN4I);

    object_initialize_child(obj, "rtc", &s->rtc, TYPE_AW_RTC_SUN4I);
}

static void aw_a10_realize(DeviceState *dev, Error **errp)
{
    AwA10State *s = AW_A10(dev);
    SysBusDevice *sysbusdev;

    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->intc), errp)) {
        return;
    }
    sysbusdev = SYS_BUS_DEVICE(&s->intc);
    sysbus_mmio_map(sysbusdev, 0, AW_A10_PIC_REG_BASE);
    sysbus_connect_irq(sysbusdev, 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(sysbusdev, 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));
    qdev_pass_gpios(DEVICE(&s->intc), dev, NULL);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer), errp)) {
        return;
    }
    sysbusdev = SYS_BUS_DEVICE(&s->timer);
    sysbus_mmio_map(sysbusdev, 0, AW_A10_PIT_REG_BASE);
    sysbus_connect_irq(sysbusdev, 0, qdev_get_gpio_in(dev, 22));
    sysbus_connect_irq(sysbusdev, 1, qdev_get_gpio_in(dev, 23));
    sysbus_connect_irq(sysbusdev, 2, qdev_get_gpio_in(dev, 24));
    sysbus_connect_irq(sysbusdev, 3, qdev_get_gpio_in(dev, 25));
    sysbus_connect_irq(sysbusdev, 4, qdev_get_gpio_in(dev, 67));
    sysbus_connect_irq(sysbusdev, 5, qdev_get_gpio_in(dev, 68));

    memory_region_init_ram(&s->sram_a, OBJECT(dev), "sram A", 48 * KiB,
                           &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0x00000000, &s->sram_a);
    create_unimplemented_device("a10-sram-ctrl", 0x01c00000, 4 * KiB);

    /* FIXME use qdev NIC properties instead of nd_table[] */
    if (nd_table[0].used) {
        qemu_check_nic_model(&nd_table[0], TYPE_AW_EMAC);
        qdev_set_nic_properties(DEVICE(&s->emac), &nd_table[0]);
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->emac), errp)) {
        return;
    }
    sysbusdev = SYS_BUS_DEVICE(&s->emac);
    sysbus_mmio_map(sysbusdev, 0, AW_A10_EMAC_BASE);
    sysbus_connect_irq(sysbusdev, 0, qdev_get_gpio_in(dev, 55));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sata), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sata), 0, AW_A10_SATA_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sata), 0, qdev_get_gpio_in(dev, 56));

    /* FIXME use a qdev chardev prop instead of serial_hd() */
    serial_mm_init(get_system_memory(), AW_A10_UART0_REG_BASE, 2,
                   qdev_get_gpio_in(dev, 1),
                   115200, serial_hd(0), DEVICE_NATIVE_ENDIAN);

    if (machine_usb(current_machine)) {
        int i;

        for (i = 0; i < AW_A10_NUM_USB; i++) {
            char bus[16];

            sprintf(bus, "usb-bus.%d", i);

            object_property_set_bool(OBJECT(&s->ehci[i]), "companion-enable",
                                     true, &error_fatal);
            sysbus_realize(SYS_BUS_DEVICE(&s->ehci[i]), &error_fatal);
            sysbus_mmio_map(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                            AW_A10_EHCI_BASE + i * 0x8000);
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->ehci[i]), 0,
                               qdev_get_gpio_in(dev, 39 + i));

            object_property_set_str(OBJECT(&s->ohci[i]), "masterbus", bus,
                                    &error_fatal);
            sysbus_realize(SYS_BUS_DEVICE(&s->ohci[i]), &error_fatal);
            sysbus_mmio_map(SYS_BUS_DEVICE(&s->ohci[i]), 0,
                            AW_A10_OHCI_BASE + i * 0x8000);
            sysbus_connect_irq(SYS_BUS_DEVICE(&s->ohci[i]), 0,
                               qdev_get_gpio_in(dev, 64 + i));
        }
    }

    /* SD/MMC */
    object_property_set_link(OBJECT(&s->mmc0), "dma-memory",
                             OBJECT(get_system_memory()), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->mmc0), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->mmc0), 0, AW_A10_MMC0_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mmc0), 0, qdev_get_gpio_in(dev, 32));
    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->mmc0),
                              "sd-bus");

    /* RTC */
    sysbus_realize(SYS_BUS_DEVICE(&s->rtc), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->rtc), 0, AW_A10_RTC_BASE, 10);
}

static void aw_a10_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aw_a10_realize;
    /* Reason: Uses serial_hds and nd_table in realize function */
    dc->user_creatable = false;
}

static const TypeInfo aw_a10_type_info = {
    .name = TYPE_AW_A10,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AwA10State),
    .instance_init = aw_a10_init,
    .class_init = aw_a10_class_init,
};

static void aw_a10_register_types(void)
{
    type_register_static(&aw_a10_type_info);
}

type_init(aw_a10_register_types)
