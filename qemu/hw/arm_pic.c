/* 
 * Generic ARM Programmable Interrupt Controller support.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL
 */

#include "vl.h"
#include "arm_pic.h"

/* Stub functions for hardware that doesn't exist.  */
void pic_set_irq(int irq, int level)
{
    cpu_abort(cpu_single_env, "pic_set_irq");
}

void pic_info(void)
{
}

void irq_info(void)
{
}


void pic_set_irq_new(void *opaque, int irq, int level)
{
    arm_pic_handler *p = (arm_pic_handler *)opaque;
    /* Call the real handler.  */
    (*p)(opaque, irq, level);
}

/* Model the IRQ/FIQ CPU interrupt lines as a two input interrupt controller.
   Input 0 is IRQ and input 1 is FIQ.  */
typedef struct
{
    arm_pic_handler handler;
    CPUState *cpu_env;
} arm_pic_cpu_state;

static void arm_pic_cpu_handler(void *opaque, int irq, int level)
{
    arm_pic_cpu_state *s = (arm_pic_cpu_state *)opaque;
    switch (irq) {
    case ARM_PIC_CPU_IRQ:
        if (level)
            cpu_interrupt(s->cpu_env, CPU_INTERRUPT_HARD);
        else
            cpu_reset_interrupt(s->cpu_env, CPU_INTERRUPT_HARD);
        break;
    case ARM_PIC_CPU_FIQ:
        if (level)
            cpu_interrupt(s->cpu_env, CPU_INTERRUPT_FIQ);
        else
            cpu_reset_interrupt(s->cpu_env, CPU_INTERRUPT_FIQ);
        break;
    default:
        cpu_abort(s->cpu_env, "arm_pic_cpu_handler: Bad interrput line %d\n",
                  irq);
    }
}

void *arm_pic_init_cpu(CPUState *env)
{
    arm_pic_cpu_state *s;
    
    s = (arm_pic_cpu_state *)malloc(sizeof(arm_pic_cpu_state));
    s->handler = arm_pic_cpu_handler;
    s->cpu_env = env;
    return s;
}
