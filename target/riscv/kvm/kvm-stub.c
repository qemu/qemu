/*
 * QEMU RISCV specific KVM stubs
 *
 *  Copyright (c) rev.ng Labs Srl.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "target/riscv/kvm/kvm_riscv.h"

void kvm_riscv_aia_create(MachineState *machine, uint64_t group_shift,
                          uint64_t aia_irq_num, uint64_t aia_msi_num,
                          uint64_t aplic_base, uint64_t imsic_base,
                          uint64_t guest_num)
{
    g_assert_not_reached();
}

uint64_t kvm_riscv_get_timebase_frequency(RISCVCPU *cpu)
{
    g_assert_not_reached();
}
