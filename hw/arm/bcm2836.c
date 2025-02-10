/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/arm/bcm2836.h"
#include "hw/arm/raspi_platform.h"
#include "hw/sysbus.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

static const Property bcm2836_enabled_cores_property =
    DEFINE_PROP_UINT32("enabled-cpus", BCM283XBaseState, enabled_cpus, 0);

static void bcm283x_base_init(Object *obj)
{
    BCM283XBaseState *s = BCM283X_BASE(obj);
    BCM283XBaseClass *bc = BCM283X_BASE_GET_CLASS(obj);
    int n;

    for (n = 0; n < bc->core_count; n++) {
        object_initialize_child(obj, "cpu[*]", &s->cpu[n].core,
                                bc->cpu_type);
    }
    if (bc->core_count > 1) {
        qdev_property_add_static(DEVICE(obj), &bcm2836_enabled_cores_property);
        qdev_prop_set_uint32(DEVICE(obj), "enabled-cpus", bc->core_count);
    }

    if (bc->ctrl_base) {
        object_initialize_child(obj, "control", &s->control,
                                TYPE_BCM2836_CONTROL);
    }
}

static void bcm283x_init(Object *obj)
{
    BCM283XState *s = BCM283X(obj);

    object_initialize_child(obj, "peripherals", &s->peripherals,
                            TYPE_BCM2835_PERIPHERALS);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->peripherals),
                              "board-rev");
    object_property_add_alias(obj, "command-line", OBJECT(&s->peripherals),
                              "command-line");
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->peripherals),
                              "vcram-size");
    object_property_add_alias(obj, "vcram-base", OBJECT(&s->peripherals),
                              "vcram-base");
}

bool bcm283x_common_realize(DeviceState *dev, BCMSocPeripheralBaseState *ps,
                            Error **errp)
{
    BCM283XBaseState *s = BCM283X_BASE(dev);
    BCM283XBaseClass *bc = BCM283X_BASE_GET_CLASS(dev);
    Object *obj;

    /* common peripherals from bcm2835 */

    obj = object_property_get_link(OBJECT(dev), "ram", &error_abort);

    object_property_add_const_link(OBJECT(ps), "ram", obj);

    if (!sysbus_realize(SYS_BUS_DEVICE(ps), errp)) {
        return false;
    }

    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(ps), "sd-bus");

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(ps), 0, bc->peri_base, 1);
    return true;
}

static void bcm2835_realize(DeviceState *dev, Error **errp)
{
    BCM283XState *s = BCM283X(dev);
    BCM283XBaseState *s_base = BCM283X_BASE(dev);
    BCMSocPeripheralBaseState *ps_base
        = BCM_SOC_PERIPHERALS_BASE(&s->peripherals);

    if (!bcm283x_common_realize(dev, ps_base, errp)) {
        return;
    }

    if (!qdev_realize(DEVICE(&s_base->cpu[0].core), NULL, errp)) {
        return;
    }

    /* Connect irq/fiq outputs from the interrupt controller. */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 0,
            qdev_get_gpio_in(DEVICE(&s_base->cpu[0].core), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 1,
            qdev_get_gpio_in(DEVICE(&s_base->cpu[0].core), ARM_CPU_FIQ));
}

static void bcm2836_realize(DeviceState *dev, Error **errp)
{
    int n;
    BCM283XState *s = BCM283X(dev);
    BCM283XBaseState *s_base = BCM283X_BASE(dev);
    BCM283XBaseClass *bc = BCM283X_BASE_GET_CLASS(dev);
    BCMSocPeripheralBaseState *ps_base
        = BCM_SOC_PERIPHERALS_BASE(&s->peripherals);

    if (!bcm283x_common_realize(dev, ps_base, errp)) {
        return;
    }

    /* bcm2836 interrupt controller (and mailboxes, etc.) */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s_base->control), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s_base->control), 0, bc->ctrl_base);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 0,
        qdev_get_gpio_in_named(DEVICE(&s_base->control), "gpu-irq", 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 1,
        qdev_get_gpio_in_named(DEVICE(&s_base->control), "gpu-fiq", 0));

    for (n = 0; n < BCM283X_NCPUS; n++) {
        object_property_set_int(OBJECT(&s_base->cpu[n].core), "mp-affinity",
                                (bc->clusterid << 8) | n, &error_abort);

        /* set periphbase/CBAR value for CPU-local registers */
        object_property_set_int(OBJECT(&s_base->cpu[n].core), "reset-cbar",
                                bc->peri_base, &error_abort);

        /* start powered off if not enabled */
        object_property_set_bool(OBJECT(&s_base->cpu[n].core),
                                 "start-powered-off",
                                 n >= s_base->enabled_cpus, &error_abort);

        if (!qdev_realize(DEVICE(&s_base->cpu[n].core), NULL, errp)) {
            return;
        }

        /* Connect irq/fiq outputs from the interrupt controller. */
        qdev_connect_gpio_out_named(DEVICE(&s_base->control), "irq", n,
            qdev_get_gpio_in(DEVICE(&s_base->cpu[n].core), ARM_CPU_IRQ));
        qdev_connect_gpio_out_named(DEVICE(&s_base->control), "fiq", n,
            qdev_get_gpio_in(DEVICE(&s_base->cpu[n].core), ARM_CPU_FIQ));

        /* Connect timers from the CPU to the interrupt controller */
        qdev_connect_gpio_out(DEVICE(&s_base->cpu[n].core), GTIMER_PHYS,
            qdev_get_gpio_in_named(DEVICE(&s_base->control), "cntpnsirq", n));
        qdev_connect_gpio_out(DEVICE(&s_base->cpu[n].core), GTIMER_VIRT,
            qdev_get_gpio_in_named(DEVICE(&s_base->control), "cntvirq", n));
        qdev_connect_gpio_out(DEVICE(&s_base->cpu[n].core), GTIMER_HYP,
            qdev_get_gpio_in_named(DEVICE(&s_base->control), "cnthpirq", n));
        qdev_connect_gpio_out(DEVICE(&s_base->cpu[n].core), GTIMER_SEC,
            qdev_get_gpio_in_named(DEVICE(&s_base->control), "cntpsirq", n));
    }
}

static void bcm283x_base_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    /* Reason: Must be wired up in code (see raspi_init() function) */
    dc->user_creatable = false;
}

static void bcm2835_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XBaseClass *bc = BCM283X_BASE_CLASS(oc);

    bc->cpu_type = ARM_CPU_TYPE_NAME("arm1176");
    bc->core_count = 1;
    bc->peri_base = 0x20000000;
    dc->realize = bcm2835_realize;
};

static void bcm2836_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XBaseClass *bc = BCM283X_BASE_CLASS(oc);

    bc->cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    bc->core_count = BCM283X_NCPUS;
    bc->peri_base = 0x3f000000;
    bc->ctrl_base = 0x40000000;
    bc->clusterid = 0xf;
    dc->realize = bcm2836_realize;
};

#ifdef TARGET_AARCH64
static void bcm2837_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XBaseClass *bc = BCM283X_BASE_CLASS(oc);

    bc->cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    bc->core_count = BCM283X_NCPUS;
    bc->peri_base = 0x3f000000;
    bc->ctrl_base = 0x40000000;
    bc->clusterid = 0x0;
    dc->realize = bcm2836_realize;
};
#endif

static const TypeInfo bcm283x_types[] = {
    {
        .name           = TYPE_BCM2835,
        .parent         = TYPE_BCM283X,
        .class_init     = bcm2835_class_init,
    }, {
        .name           = TYPE_BCM2836,
        .parent         = TYPE_BCM283X,
        .class_init     = bcm2836_class_init,
#ifdef TARGET_AARCH64
    }, {
        .name           = TYPE_BCM2837,
        .parent         = TYPE_BCM283X,
        .class_init     = bcm2837_class_init,
#endif
    }, {
        .name           = TYPE_BCM283X,
        .parent         = TYPE_BCM283X_BASE,
        .instance_size  = sizeof(BCM283XState),
        .instance_init  = bcm283x_init,
        .abstract       = true,
    }, {
        .name           = TYPE_BCM283X_BASE,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(BCM283XBaseState),
        .instance_init  = bcm283x_base_init,
        .class_size     = sizeof(BCM283XBaseClass),
        .class_init     = bcm283x_base_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(bcm283x_types)
