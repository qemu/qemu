/*
 * QEMU RISC-V Advanced Interrupt Architecture (AIA)
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/kvm.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"

#include "aia.h"

uint32_t imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}

DeviceState *riscv_create_aia(bool msimode, int aia_guests,
                             uint16_t num_sources,
                             const MemMapEntry *aplic_m,
                             const MemMapEntry *aplic_s,
                             const MemMapEntry *imsic_m,
                             const MemMapEntry *imsic_s,
                             int socket, int base_hartid, int hart_count,
                             uint32_t num_msis, uint32_t num_prio_bits)
{
    int i;
    hwaddr addr = 0;
    uint32_t guest_bits;
    DeviceState *aplic_s_dev = NULL;
    DeviceState *aplic_m_dev = NULL;

    /* The RISC-V Advanced Interrupt Architecture, Chapter 1.2. Limits */
    g_assert(num_sources <= 1023);

    if (msimode) {
        if (!kvm_enabled()) {
            /* Per-socket M-level IMSICs */
            addr = imsic_m->base + socket * (1U << IMSIC_MMIO_GROUP_MIN_SHIFT);
            for (i = 0; i < hart_count; i++) {
                riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0),
                                   base_hartid + i, true, 1,
                                   num_msis);
            }
        }

        /* Per-socket S-level IMSICs */
        guest_bits = imsic_num_bits(aia_guests + 1);
        addr = imsic_s->base + socket * (1U << IMSIC_MMIO_GROUP_MIN_SHIFT);
        for (i = 0; i < hart_count; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(guest_bits),
                               base_hartid + i, false, 1 + aia_guests,
                               num_msis);
        }
    }

    if (!kvm_enabled()) {
        /* Per-socket M-level APLIC */
        aplic_m_dev = riscv_aplic_create(aplic_m->base +
                                     socket * aplic_m->size,
                                     aplic_m->size,
                                     (msimode) ? 0 : base_hartid,
                                     (msimode) ? 0 : hart_count,
                                     num_sources,
                                     num_prio_bits,
                                     msimode, true, NULL);
    }

    /* Per-socket S-level APLIC */
    aplic_s_dev = riscv_aplic_create(aplic_s->base +
                                 socket * aplic_s->size,
                                 aplic_s->size,
                                 (msimode) ? 0 : base_hartid,
                                 (msimode) ? 0 : hart_count,
                                 num_sources,
                                 num_prio_bits,
                                 msimode, false, aplic_m_dev);

    if (kvm_enabled() && msimode) {
        riscv_aplic_set_kvm_msicfgaddr(RISCV_APLIC(aplic_s_dev), addr);
    }

    return kvm_enabled() ? aplic_s_dev : aplic_m_dev;
}
