/*
 * QEMU KVM support -- RISC-V specific functions.
 *
 * Copyright (c) 2020 Huawei Technologies Co., Ltd
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
 */

#ifndef QEMU_KVM_RISCV_H
#define QEMU_KVM_RISCV_H

#include "target/riscv/cpu-qom.h"

void kvm_riscv_reset_vcpu(RISCVCPU *cpu);
void kvm_riscv_set_irq(RISCVCPU *cpu, int irq, int level);
void kvm_riscv_aia_create(MachineState *machine, uint64_t group_shift,
                          uint64_t aia_irq_num, uint64_t aia_msi_num,
                          uint64_t aplic_base, uint64_t imsic_base,
                          uint64_t guest_num);
void riscv_kvm_aplic_request(void *opaque, int irq, int level);
int kvm_riscv_sync_mpstate_to_kvm(RISCVCPU *cpu, int state);
void riscv_kvm_cpu_finalize_features(RISCVCPU *cpu, Error **errp);
uint64_t kvm_riscv_get_timebase_frequency(RISCVCPU *cpu);

#endif
