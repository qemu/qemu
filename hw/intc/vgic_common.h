/*
 * ARM KVM vGIC utility functions
 *
 * Copyright (c) 2015 Samsung Electronics
 * Written by Pavel Fedin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_ARM_VGIC_COMMON_H
#define QEMU_ARM_VGIC_COMMON_H

/**
 * kvm_arm_gic_set_irq - Send an IRQ to the in-kernel vGIC
 * @num_irq: Total number of IRQs configured for the GIC instance
 * @irq: qemu internal IRQ line number:
 *  [0..N-1] : external interrupts
 *  [N..N+31] : PPI (internal) interrupts for CPU 0
 *  [N+32..N+63] : PPI (internal interrupts for CPU 1
 * @level: level of the IRQ line.
 */
void kvm_arm_gic_set_irq(uint32_t num_irq, int irq, int level);

#endif
