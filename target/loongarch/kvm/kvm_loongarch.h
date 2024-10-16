/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch kvm interface
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "cpu.h"

#ifndef QEMU_KVM_LOONGARCH_H
#define QEMU_KVM_LOONGARCH_H

int  kvm_loongarch_set_interrupt(LoongArchCPU *cpu, int irq, int level);
void kvm_arch_reset_vcpu(CPUState *cs);

#endif
