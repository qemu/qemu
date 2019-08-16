/*
 * Altera Nios2 CPU PIC
 *
 * Copyright (c) 2016 Marek Vasut <marek.vasut@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/irq.h"

#include "qemu/config-file.h"

#include "boot.h"

static void nios2_pic_cpu_handler(void *opaque, int irq, int level)
{
    Nios2CPU *cpu = opaque;
    CPUNios2State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    int type = irq ? CPU_INTERRUPT_NMI : CPU_INTERRUPT_HARD;

    if (type == CPU_INTERRUPT_HARD) {
        env->irq_pending = level;

        if (level && (env->regs[CR_STATUS] & CR_STATUS_PIE)) {
            env->irq_pending = 0;
            cpu_interrupt(cs, type);
        } else if (!level) {
            env->irq_pending = 0;
            cpu_reset_interrupt(cs, type);
        }
    } else {
        if (level) {
            cpu_interrupt(cs, type);
        } else {
            cpu_reset_interrupt(cs, type);
        }
    }
}

void nios2_check_interrupts(CPUNios2State *env)
{
    if (env->irq_pending) {
        env->irq_pending = 0;
        cpu_interrupt(env_cpu(env), CPU_INTERRUPT_HARD);
    }
}

qemu_irq *nios2_cpu_pic_init(Nios2CPU *cpu)
{
    return qemu_allocate_irqs(nios2_pic_cpu_handler, cpu, 2);
}
