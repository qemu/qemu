/*
 * BCM2838 peripherals emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/raspi_platform.h"
#include "hw/arm/bcm2838_peripherals.h"

/* Lower peripheral base address on the VC (GPU) system bus */
#define BCM2838_VC_PERI_LOW_BASE 0x7c000000

static void bcm2838_peripherals_init(Object *obj)
{
    BCM2838PeripheralState *s = BCM2838_PERIPHERALS(obj);
    BCM2838PeripheralClass *bc = BCM2838_PERIPHERALS_GET_CLASS(obj);

    /* Lower memory region for peripheral devices (exported to the Soc) */
    memory_region_init(&s->peri_low_mr, obj, "bcm2838-peripherals",
                       bc->peri_low_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->peri_low_mr);

}

static void bcm2838_peripherals_realize(DeviceState *dev, Error **errp)
{
    BCM2838PeripheralState *s = BCM2838_PERIPHERALS(dev);
    BCMSocPeripheralBaseState *s_base = BCM_SOC_PERIPHERALS_BASE(dev);

    bcm_soc_peripherals_common_realize(dev, errp);

    /* Map lower peripherals into the GPU address space */
    memory_region_init_alias(&s->peri_low_mr_alias, OBJECT(s),
                             "bcm2838-peripherals", &s->peri_low_mr, 0,
                             memory_region_size(&s->peri_low_mr));
    memory_region_add_subregion_overlap(&s_base->gpu_bus_mr,
                                        BCM2838_VC_PERI_LOW_BASE,
                                        &s->peri_low_mr_alias, 1);

}

static void bcm2838_peripherals_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM2838PeripheralClass *bc = BCM2838_PERIPHERALS_CLASS(oc);
    BCMSocPeripheralBaseClass *bc_base = BCM_SOC_PERIPHERALS_BASE_CLASS(oc);

    bc->peri_low_size = 0x2000000;
    bc_base->peri_size = 0x1800000;
    dc->realize = bcm2838_peripherals_realize;
}

static const TypeInfo bcm2838_peripherals_type_info = {
    .name = TYPE_BCM2838_PERIPHERALS,
    .parent = TYPE_BCM_SOC_PERIPHERALS_BASE,
    .instance_size = sizeof(BCM2838PeripheralState),
    .instance_init = bcm2838_peripherals_init,
    .class_size = sizeof(BCM2838PeripheralClass),
    .class_init = bcm2838_peripherals_class_init,
};

static void bcm2838_peripherals_register_types(void)
{
    type_register_static(&bcm2838_peripherals_type_info);
}

type_init(bcm2838_peripherals_register_types)
