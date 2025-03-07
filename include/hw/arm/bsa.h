/*
 * Common definitions for Arm Base System Architecture (BSA) platforms.
 *
 * Copyright (c) 2015 Linaro Limited
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef QEMU_ARM_BSA_H
#define QEMU_ARM_BSA_H

/* These are architectural INTID values */
#define ARCH_TIMER_S_EL2_VIRT_IRQ  19
#define ARCH_TIMER_S_EL2_IRQ       20
#define VIRTUAL_PMU_IRQ            23
#define ARCH_GIC_MAINT_IRQ         25
#define ARCH_TIMER_NS_EL2_IRQ      26
#define ARCH_TIMER_VIRT_IRQ        27
#define ARCH_TIMER_NS_EL2_VIRT_IRQ 28
#define ARCH_TIMER_S_EL1_IRQ       29
#define ARCH_TIMER_NS_EL1_IRQ      30

#define INTID_TO_PPI(irq) ((irq) - 16)

#endif /* QEMU_ARM_BSA_H */
