/*
 * Generic ARM Programmable Interrupt Controller support.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL
 */

#include "hw/hw.h"
#include "hw/arm/arm.h"

/* Backwards compatibility shim; this can disappear when all
 * board models have been updated to get IRQ and FIQ lines directly
 * from the ARMCPU object rather than by calling this function.
 */
qemu_irq *arm_pic_init_cpu(ARMCPU *cpu)
{
    DeviceState *dev = DEVICE(cpu);
    qemu_irq *irqs = g_new(qemu_irq, 2);

    irqs[0] = qdev_get_gpio_in(dev, ARM_CPU_IRQ);
    irqs[1] = qdev_get_gpio_in(dev, ARM_CPU_FIQ);
    return irqs;
}
