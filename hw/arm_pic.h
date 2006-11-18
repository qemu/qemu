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

/* The first element of an individual PIC state structures should
   be a pointer to the handler routine.  */
typedef void (*arm_pic_handler)(void *opaque, int irq, int level);

/* The CPU is also modeled as an interrupt controller.  */
#define ARM_PIC_CPU_IRQ 0
#define ARM_PIC_CPU_FIQ 1
void *arm_pic_init_cpu(CPUState *env);

#endif /* !ARM_INTERRUPT_H */

