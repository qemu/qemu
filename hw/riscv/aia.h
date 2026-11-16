/*
 * QEMU RISC-V Advanced Interrupt Architecture (AIA)
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RISCV_AIA_H
#define HW_RISCV_AIA_H

#include "exec/hwaddr.h"
#include "qemu/bitops.h"

typedef struct RISCVAIASocket {
    uint32_t hartid_base;
    uint32_t num_harts;
} RISCVAIASocket;

static inline uint32_t imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}

DeviceState *riscv_create_aia(MemoryRegion *container,
                             bool msimode, int aia_guests,
                             uint32_t m_imsic_stride,
                             uint32_t s_imsic_stride,
                             uint16_t num_sources,
                             const MemMapEntry *aplic_m,
                             const MemMapEntry *aplic_s,
                             const MemMapEntry *imsic_m,
                             const MemMapEntry *imsic_s,
                             int socket, int base_hartid, int hart_count,
                             uint32_t num_msis, uint32_t num_prio_bits);

#endif
