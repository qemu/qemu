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

#ifndef __KVM_MIPS_H__
#define __KVM_MIPS_H__

/**
 * kvm_mips_reset_vcpu:
 * @cpu: MIPSCPU
 *
 * Called at reset time to set kernel registers to their initial values.
 */
void kvm_mips_reset_vcpu(MIPSCPU *cpu);

int kvm_mips_set_interrupt(MIPSCPU *cpu, int irq, int level);
int kvm_mips_set_ipi_interrupt(MIPSCPU *cpu, int irq, int level);

#endif /* __KVM_MIPS_H__ */
