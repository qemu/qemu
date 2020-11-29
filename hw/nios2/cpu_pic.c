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

void nios2_check_interrupts(CPUNios2State *env)
{
    if (env->irq_pending &&
        (env->regs[CR_STATUS] & CR_STATUS_PIE)) {
        env->irq_pending = 0;
        cpu_interrupt(env_cpu(env), CPU_INTERRUPT_HARD);
    }
}
