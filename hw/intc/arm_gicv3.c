/*
 * ARM Generic Interrupt Controller v3
 *
 * Copyright (c) 2015 Huawei.
 * Copyright (c) 2016 Linaro Limited
 * Written by Shlomo Pongratz, Peter Maydell
 *
 * This code is licensed under the GPL, version 2 or (at your option)
 * any later version.
 */

/* This file contains implementation code for an interrupt controller
 * which implements the GICv3 architecture. Specifically this is where
 * the device class itself and the functions for handling interrupts
 * coming in and going out live.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/intc/arm_gicv3.h"
#include "gicv3_internal.h"

/* Process a change in an external IRQ input. */
static void gicv3_set_irq(void *opaque, int irq, int level)
{
    /* Meaning of the 'irq' parameter:
     *  [0..N-1] : external interrupts
     *  [N..N+31] : PPI (internal) interrupts for CPU 0
     *  [N+32..N+63] : PPI (internal interrupts for CPU 1
     *  ...
     */
    /* Do nothing for now */
}

static void arm_gic_realize(DeviceState *dev, Error **errp)
{
    /* Device instance realize function for the GIC sysbus device */
    GICv3State *s = ARM_GICV3(dev);
    ARMGICv3Class *agc = ARM_GICV3_GET_CLASS(s);
    Error *local_err = NULL;

    agc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    gicv3_init_irqs_and_mmio(s, gicv3_set_irq, NULL);
}

static void arm_gicv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMGICv3Class *agc = ARM_GICV3_CLASS(klass);

    agc->parent_realize = dc->realize;
    dc->realize = arm_gic_realize;
}

static const TypeInfo arm_gicv3_info = {
    .name = TYPE_ARM_GICV3,
    .parent = TYPE_ARM_GICV3_COMMON,
    .instance_size = sizeof(GICv3State),
    .class_init = arm_gicv3_class_init,
    .class_size = sizeof(ARMGICv3Class),
};

static void arm_gicv3_register_types(void)
{
    type_register_static(&arm_gicv3_info);
}

type_init(arm_gicv3_register_types)
