/* 
 * Generic ARM Programmable Interrupt Controller support.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 *
 * Arm hardware uses a wide variety of interrupt handling hardware.
 * This provides a generic framework for connecting interrupt sources and
 * inputs.
 */

#ifndef ARM_INTERRUPT_H
#define ARM_INTERRUPT_H 1

/* The CPU is also modeled as an interrupt controller.  */
#define ARM_PIC_CPU_IRQ 0
#define ARM_PIC_CPU_FIQ 1
qemu_irq *arm_pic_init_cpu(CPUState *env);

#endif /* !ARM_INTERRUPT_H */

