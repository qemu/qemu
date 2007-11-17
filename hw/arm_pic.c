/*
 * Generic ARM Programmable Interrupt Controller support.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL
 */

#include "hw.h"
#include "arm-misc.h"

/* Stub functions for hardware that doesn't exist.  */
void pic_info(void)
{
}

void irq_info(void)
{
}


/* Input 0 is IRQ and input 1 is FIQ.  */
static void arm_pic_cpu_handler(void *opaque, int irq, int level)
{
    CPUState *env = (CPUState *)opaque;
    switch (irq) {
    case ARM_PIC_CPU_IRQ:
        if (level)
            cpu_interrupt(env, CPU_INTERRUPT_HARD);
        else
            cpu_reset_interrupt(env, CPU_INTERRUPT_HARD);
        break;
    case ARM_PIC_CPU_FIQ:
        if (level)
            cpu_interrupt(env, CPU_INTERRUPT_FIQ);
        else
            cpu_reset_interrupt(env, CPU_INTERRUPT_FIQ);
        break;
    default:
        cpu_abort(env, "arm_pic_cpu_handler: Bad interrput line %d\n", irq);
    }
}

qemu_irq *arm_pic_init_cpu(CPUState *env)
{
    return qemu_allocate_irqs(arm_pic_cpu_handler, env, 2);
}
