/*
 * BCM2838 SoC emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/raspi_platform.h"
#include "hw/sysbus.h"
#include "hw/arm/bcm2838.h"
#include "trace.h"

#define VIRTUAL_PMU_IRQ 7

static void bcm2838_init(Object *obj)
{
    BCM2838State *s = BCM2838(obj);

    object_initialize_child(obj, "peripherals", &s->peripherals,
                            TYPE_BCM2838_PERIPHERALS);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->peripherals),
                              "board-rev");
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->peripherals),
                              "vcram-size");
    object_property_add_alias(obj, "command-line", OBJECT(&s->peripherals),
                              "command-line");
}

static void bcm2838_realize(DeviceState *dev, Error **errp)
{
    int n;
    BCM2838State *s = BCM2838(dev);
    BCM283XBaseState *s_base = BCM283X_BASE(dev);
    BCM283XBaseClass *bc_base = BCM283X_BASE_GET_CLASS(dev);
    BCM2838PeripheralState *ps = BCM2838_PERIPHERALS(&s->peripherals);
    BCMSocPeripheralBaseState *ps_base =
        BCM_SOC_PERIPHERALS_BASE(&s->peripherals);

    if (!bcm283x_common_realize(dev, ps_base, errp)) {
        return;
    }
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(ps), 1, BCM2838_PERI_LOW_BASE, 1);

    /* bcm2836 interrupt controller (and mailboxes, etc.) */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s_base->control), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s_base->control), 0, bc_base->ctrl_base);

    /* Create cores */
    for (n = 0; n < bc_base->core_count; n++) {

        object_property_set_int(OBJECT(&s_base->cpu[n].core), "mp-affinity",
                                (bc_base->clusterid << 8) | n, &error_abort);

        /* start powered off if not enabled */
        object_property_set_bool(OBJECT(&s_base->cpu[n].core),
                                 "start-powered-off",
                                 n >= s_base->enabled_cpus, &error_abort);

        if (!qdev_realize(DEVICE(&s_base->cpu[n].core), NULL, errp)) {
            return;
        }
    }
}

static void bcm2838_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XBaseClass *bc_base = BCM283X_BASE_CLASS(oc);

    bc_base->cpu_type = ARM_CPU_TYPE_NAME("cortex-a72");
    bc_base->core_count = BCM283X_NCPUS;
    bc_base->peri_base = 0xfe000000;
    bc_base->ctrl_base = 0xff800000;
    bc_base->clusterid = 0x0;
    dc->realize = bcm2838_realize;
}

static const TypeInfo bcm2838_type = {
    .name           = TYPE_BCM2838,
    .parent         = TYPE_BCM283X_BASE,
    .instance_size  = sizeof(BCM2838State),
    .instance_init  = bcm2838_init,
    .class_size     = sizeof(BCM283XBaseClass),
    .class_init     = bcm2838_class_init,
};

static void bcm2838_register_types(void)
{
    type_register_static(&bcm2838_type);
}

type_init(bcm2838_register_types);
