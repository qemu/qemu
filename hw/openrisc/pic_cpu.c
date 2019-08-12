/*
 * OpenRISC Programmable Interrupt Controller support.
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "cpu.h"

/* OpenRISC pic handler */
static void openrisc_pic_cpu_handler(void *opaque, int irq, int level)
{
    OpenRISCCPU *cpu = (OpenRISCCPU *)opaque;
    CPUState *cs = CPU(cpu);
    uint32_t irq_bit;

    if (irq > 31 || irq < 0) {
        return;
    }

    irq_bit = 1U << irq;

    if (level) {
        cpu->env.picsr |= irq_bit;
    } else {
        cpu->env.picsr &= ~irq_bit;
    }

    if (cpu->env.picsr & cpu->env.picmr) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        cpu->env.picsr = 0;
    }
}

void cpu_openrisc_pic_init(OpenRISCCPU *cpu)
{
    int i;
    qemu_irq *qi;
    qi = qemu_allocate_irqs(openrisc_pic_cpu_handler, cpu, NR_IRQS);

    for (i = 0; i < NR_IRQS; i++) {
        cpu->env.irq[i] = qi[i];
    }
}
