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

struct BCM283XClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *name;
    const char *cpu_type;
    unsigned core_count;
    hwaddr peri_base; /* Peripheral base address seen by the CPU */
    hwaddr ctrl_base; /* Interrupt controller and mailboxes etc. */
    int clusterid;
};

static Property bcm2836_enabled_cores_property =
    DEFINE_PROP_UINT32("enabled-cpus", BCM283XState, enabled_cpus, 0);

static void bcm2836_init(Object *obj)
{
    BCM283XState *s = BCM283X(obj);
    BCM283XClass *bc = BCM283X_GET_CLASS(obj);
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

    object_initialize_child(obj, "peripherals", &s->peripherals,
                            TYPE_BCM2835_PERIPHERALS);
    object_property_add_alias(obj, "board-rev", OBJECT(&s->peripherals),
                              "board-rev");
    object_property_add_alias(obj, "command-line", OBJECT(&s->peripherals),
                              "command-line");
    object_property_add_alias(obj, "vcram-size", OBJECT(&s->peripherals),
                              "vcram-size");
}

static bool bcm283x_common_realize(DeviceState *dev, Error **errp)
{
    BCM283XState *s = BCM283X(dev);
    BCM283XClass *bc = BCM283X_GET_CLASS(dev);
    Object *obj;

    /* common peripherals from bcm2835 */

    obj = object_property_get_link(OBJECT(dev), "ram", &error_abort);

    object_property_add_const_link(OBJECT(&s->peripherals), "ram", obj);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->peripherals), errp)) {
        return false;
    }

    object_property_add_alias(OBJECT(s), "sd-bus", OBJECT(&s->peripherals),
                              "sd-bus");

    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(&s->peripherals), 0,
                            bc->peri_base, 1);
    return true;
}

static void bcm2835_realize(DeviceState *dev, Error **errp)
{
    BCM283XState *s = BCM283X(dev);

    if (!bcm283x_common_realize(dev, errp)) {
        return;
    }

    if (!qdev_realize(DEVICE(&s->cpu[0].core), NULL, errp)) {
        return;
    }

    /* Connect irq/fiq outputs from the interrupt controller. */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 0,
            qdev_get_gpio_in(DEVICE(&s->cpu[0].core), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 1,
            qdev_get_gpio_in(DEVICE(&s->cpu[0].core), ARM_CPU_FIQ));
}

static void bcm2836_realize(DeviceState *dev, Error **errp)
{
    BCM283XState *s = BCM283X(dev);
    BCM283XClass *bc = BCM283X_GET_CLASS(dev);
    int n;

    if (!bcm283x_common_realize(dev, errp)) {
        return;
    }

    /* bcm2836 interrupt controller (and mailboxes, etc.) */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->control), errp)) {
        return;
    }

    sysbus_mmio_map(SYS_BUS_DEVICE(&s->control), 0, bc->ctrl_base);

    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 0,
        qdev_get_gpio_in_named(DEVICE(&s->control), "gpu-irq", 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->peripherals), 1,
        qdev_get_gpio_in_named(DEVICE(&s->control), "gpu-fiq", 0));

    for (n = 0; n < BCM283X_NCPUS; n++) {
        /* TODO: this should be converted to a property of ARM_CPU */
        s->cpu[n].core.mp_affinity = (bc->clusterid << 8) | n;

        /* set periphbase/CBAR value for CPU-local registers */
        if (!object_property_set_int(OBJECT(&s->cpu[n].core), "reset-cbar",
                                     bc->peri_base, errp)) {
            return;
        }

        /* start powered off if not enabled */
        if (!object_property_set_bool(OBJECT(&s->cpu[n].core),
                                      "start-powered-off",
                                      n >= s->enabled_cpus,
                                      errp)) {
            return;
        }

        if (!qdev_realize(DEVICE(&s->cpu[n].core), NULL, errp)) {
            return;
        }

        /* Connect irq/fiq outputs from the interrupt controller. */
        qdev_connect_gpio_out_named(DEVICE(&s->control), "irq", n,
                qdev_get_gpio_in(DEVICE(&s->cpu[n].core), ARM_CPU_IRQ));
        qdev_connect_gpio_out_named(DEVICE(&s->control), "fiq", n,
                qdev_get_gpio_in(DEVICE(&s->cpu[n].core), ARM_CPU_FIQ));

        /* Connect timers from the CPU to the interrupt controller */
        qdev_connect_gpio_out(DEVICE(&s->cpu[n].core), GTIMER_PHYS,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cntpnsirq", n));
        qdev_connect_gpio_out(DEVICE(&s->cpu[n].core), GTIMER_VIRT,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cntvirq", n));
        qdev_connect_gpio_out(DEVICE(&s->cpu[n].core), GTIMER_HYP,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cnthpirq", n));
        qdev_connect_gpio_out(DEVICE(&s->cpu[n].core), GTIMER_SEC,
                qdev_get_gpio_in_named(DEVICE(&s->control), "cntpsirq", n));
    }
}

static void bcm283x_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    /* Reason: Must be wired up in code (see raspi_init() function) */
    dc->user_creatable = false;
}

static void bcm2835_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XClass *bc = BCM283X_CLASS(oc);

    bc->cpu_type = ARM_CPU_TYPE_NAME("arm1176");
    bc->core_count = 1;
    bc->peri_base = 0x20000000;
    dc->realize = bcm2835_realize;
};

static void bcm2836_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    BCM283XClass *bc = BCM283X_CLASS(oc);

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
    BCM283XClass *bc = BCM283X_CLASS(oc);

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
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(BCM283XState),
        .instance_init  = bcm2836_init,
        .class_size     = sizeof(BCM283XClass),
        .class_init     = bcm283x_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(bcm283x_types)
