/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "cpu.h"
#include "hw/arm/bcm2836.h"
#include "hw/arm/raspi_platform.h"
#include "hw/sysbus.h"

/* Peripheral base address seen by the CPU */
#define BCM2836_PERI_BASE       0x3F000000

/* "QA7" (Pi2) interrupt controller and mailboxes etc. */
#define BCM2836_CONTROL_BASE    0x40000000

struct BCM283XInfo {
    const char *name;
    const char *cpu_type;
    int clusterid;
};

static const BCM283XInfo bcm283x_socs[] = {
    {
        .name = TYPE_BCM2836,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-a7"),
        .clusterid = 0xf,
    },
#ifdef TARGET_AARCH64
    {
        .name = TYPE_BCM2837,
        .cpu_type = ARM_CPU_TYPE_NAME("cortex-a53"),
        .clusterid = 0x0,
    },
#endif
};

static void bcm2836_init(Object *obj)
{
    BCM283XState *s = BCM283X(obj);
    BCM283XClass *bc = BCM283X_GET_CLASS(obj);
    const BCM283XInfo *info = bc->info;
    int n;

    for (n = 0; n < BCM283X_NCPUS; n++) {
        object_initialize_child(obj, "cpu[*]", &s->cpus[n], sizeof(s->cpus[n]),
                                info->cpu_type, &error_abort, NULL);
    }

    sysbus_init_child_obj(obj, "control", &s->control, sizeof(s->control),
                          TYPE_BCM2836_CONTROL);

    sysbus_init_child_obj(obj, "peripherals", &s->peripherals,
                          sizeof(s->peripherals), TYPE_BCM2835_PERIPHERALS);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->peripherals),
                              "board-rev", &error_abort);
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->peripherals),
                              "vcram-size", &error_abort);
}

static void bcm2836_realize(DeviceState *dev, Error **errp)
{
    BCM283XState *s = BCM283X(dev);
    BCM283XClass *bc = BCM283X_GET_CLASS(dev);
    const BCM283XInfo *info = bc->info;
    Object *obj;
    Error *err = NULL;
    int n;

    /* common peripherals from bcm2835 */

    obj = object_property_get_link(OBJECT(dev), "ram", &err);
    if (obj == NULL) {
        error_setg(errp, "%s: required ram link not found: %s",
                   __func__, error_get_pretty(err));
        return;
    }

    object_property_add_const_link(OBJECT(&s->peripherals), "ram", obj, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_bool(OBJECT(&s->peripherals), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->peripherals),
                              "sd-bus", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->peripherals), 0,
                            BCM2836_PERI_BASE, 1);

    /* bcm2836 interrupt controller (and mailboxes, etc.) */
    object_property_set_bool(OBJECT(&s->control), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->control), 0, BCM2836_CONTROL_BASE);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 0,
        qdev_get_gpio_in_named(DEVICE(&s->control), "gpu-irq", 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 1,
        qdev_get_gpio_in_named(DEVICE(&s->control), "gpu-fiq", 0));

    for (n = 0; n < BCM283X_NCPUS; n++) {
        /* TODO: this should be converted to a property of ARM_CPU */
        s->cpus[n].mp_affinity = (info->clusterid << 8) | n;

        /* set periphbase/CBAR value for CPU-local registers */
        object_property_set_int(OBJECT(&s->cpus[n]),
                                BCM2836_PERI_BASE + MCORE_OFFSET,
                                "reset-cbar", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        /* start powered off if not enabled */
        object_property_set_bool(OBJECT(&s->cpus[n]), n >= s->enabled_cpus,
                                 "start-powered-off", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        object_property_set_bool(OBJECT(&s->cpus[n]), true, "realized", &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }

        /* Connect irq/fiq outputs from the interrupt controller. */
        qdev_connect_gpio_out_named(DEVICE(&s->control), "irq", n,
                qdev_get_gpio_in(DEVICE(&s->cpus[n]), ARM_CPU_IRQ));
        qdev_connect_gpio_out_named(DEVICE(&s->control), "fiq", n,
                qdev_get_gpio_in(DEVICE(&s->cpus[n]), ARM_CPU_FIQ));

        /* Connect timers from the CPU to the interrupt controller */
        qdev_connect_gpio_out(DEVICE(&s->cpus[n]), GTIMER_PHYS,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cntpnsirq", n));
        qdev_connect_gpio_out(DEVICE(&s->cpus[n]), GTIMER_VIRT,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cntvirq", n));
        qdev_connect_gpio_out(DEVICE(&s->cpus[n]), GTIMER_HYP,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cnthpirq", n));
        qdev_connect_gpio_out(DEVICE(&s->cpus[n]), GTIMER_SEC,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cntpsirq", n));
    }
}

static Property bcm2836_props[] = {
    DEFINE_PROP_UINT32("enabled-cpus", BCM283XState, enabled_cpus,
                       BCM283X_NCPUS),
    DEFINE_PROP_END_OF_LIST()
};

static void bcm283x_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XClass *bc = BCM283X_CLASS(oc);

    bc->info = data;
    dc->realize = bcm2836_realize;
    dc->props = bcm2836_props;
    /* Reason: Must be wired up in code (see raspi_init() function) */
    dc->user_creatable = false;
}

static const TypeInfo bcm283x_type_info = {
    .name = TYPE_BCM283X,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(BCM283XState),
    .instance_init = bcm2836_init,
    .class_size = sizeof(BCM283XClass),
    .abstract = true,
};

static void bcm2836_register_types(void)
{
    int i;

    type_register_static(&bcm283x_type_info);
    for (i = 0; i < ARRAY_SIZE(bcm283x_socs); i++) {
        TypeInfo ti = {
            .name = bcm283x_socs[i].name,
            .parent = TYPE_BCM283X,
            .class_init = bcm283x_class_init,
            .class_data = (void *) &bcm283x_socs[i],
        };
        type_register(&ti);
    }
}

type_init(bcm2836_register_types)
