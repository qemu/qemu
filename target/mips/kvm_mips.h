/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: MIPS specific KVM APIs
 *
 * Copyright (C) 2012-2014 Imagination Technologies Ltd.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#ifndef KVM_MIPS_H
#define KVM_MIPS_H

#include "cpu.h"

/**
 * kvm_mips_reset_vcpu:
 * @cpu: MIPSCPU
 *
 * Called at reset time to set kernel registers to their initial values.
 */
void kvm_mips_reset_vcpu(MIPSCPU *cpu);

int kvm_mips_set_interrupt(MIPSCPU *cpu, int irq, int level);
int kvm_mips_set_ipi_interrupt(MIPSCPU *cpu, int irq, int level);

#ifdef CONFIG_KVM
int mips_kvm_type(MachineState *machine, const char *vm_type);
#else
static inline int mips_kvm_type(MachineState *machine, const char *vm_type)
{
    return 0;
}
#endif

#endif /* KVM_MIPS_H */
