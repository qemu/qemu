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
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/devices.h"
#include "hw/arm/allwinner-a10.h"

static void aw_a10_init(Object *obj)
{
    AwA10State *s = AW_A10(obj);

    object_initialize(&s->cpu, sizeof(s->cpu), "cortex-a8-" TYPE_ARM_CPU);
    object_property_add_child(obj, "cpu", OBJECT(&s->cpu), NULL);

    object_initialize(&s->intc, sizeof(s->intc), TYPE_AW_A10_PIC);
    qdev_set_parent_bus(DEVICE(&s->intc), sysbus_get_default());

    object_initialize(&s->timer, sizeof(s->timer), TYPE_AW_A10_PIT);
    qdev_set_parent_bus(DEVICE(&s->timer), sysbus_get_default());

    object_initialize(&s->emac, sizeof(s->emac), TYPE_AW_EMAC);
    qdev_set_parent_bus(DEVICE(&s->emac), sysbus_get_default());
    /* FIXME use qdev NIC properties instead of nd_table[] */
    if (nd_table[0].used) {
        qemu_check_nic_model(&nd_table[0], TYPE_AW_EMAC);
        qdev_set_nic_properties(DEVICE(&s->emac), &nd_table[0]);
    }

    object_initialize(&s->sata, sizeof(s->sata), TYPE_ALLWINNER_AHCI);
    qdev_set_parent_bus(DEVICE(&s->sata), sysbus_get_default());
}

static void aw_a10_realize(DeviceState *dev, Error **errp)
{
    AwA10State *s = AW_A10(dev);
    SysBusDevice *sysbusdev;
    uint8_t i;
    qemu_irq fiq, irq;
    Error *err = NULL;

    object_property_set_bool(OBJECT(&s->cpu), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    irq = qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ);
    fiq = qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ);

    object_property_set_bool(OBJECT(&s->intc), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    sysbusdev = SYS_BUS_DEVICE(&s->intc);
    sysbus_mmio_map(sysbusdev, 0, AW_A10_PIC_REG_BASE);
    sysbus_connect_irq(sysbusdev, 0, irq);
    sysbus_connect_irq(sysbusdev, 1, fiq);
    for (i = 0; i < AW_A10_PIC_INT_NR; i++) {
        s->irq[i] = qdev_get_gpio_in(DEVICE(&s->intc), i);
    }

    object_property_set_bool(OBJECT(&s->timer), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    sysbusdev = SYS_BUS_DEVICE(&s->timer);
    sysbus_mmio_map(sysbusdev, 0, AW_A10_PIT_REG_BASE);
    sysbus_connect_irq(sysbusdev, 0, s->irq[22]);
    sysbus_connect_irq(sysbusdev, 1, s->irq[23]);
    sysbus_connect_irq(sysbusdev, 2, s->irq[24]);
    sysbus_connect_irq(sysbusdev, 3, s->irq[25]);
    sysbus_connect_irq(sysbusdev, 4, s->irq[67]);
    sysbus_connect_irq(sysbusdev, 5, s->irq[68]);

    object_property_set_bool(OBJECT(&s->emac), true, "realized", &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    sysbusdev = SYS_BUS_DEVICE(&s->emac);
    sysbus_mmio_map(sysbusdev, 0, AW_A10_EMAC_BASE);
    sysbus_connect_irq(sysbusdev, 0, s->irq[55]);

    object_property_set_bool(OBJECT(&s->sata), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sata), 0, AW_A10_SATA_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sata), 0, s->irq[56]);

    /* FIXME use a qdev chardev prop instead of serial_hds[] */
    serial_mm_init(get_system_memory(), AW_A10_UART0_REG_BASE, 2, s->irq[1],
                   115200, serial_hds[0], DEVICE_NATIVE_ENDIAN);
}

static void aw_a10_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = aw_a10_realize;
    /* Reason: Uses serial_hds in realize and nd_table in instance_init */
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
