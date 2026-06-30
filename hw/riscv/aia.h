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

#define VIRT_IRQCHIP_NUM_SOURCES 96

uint32_t imsic_num_bits(uint32_t count);

DeviceState *riscv_create_aia(bool msimode, int aia_guests,
                             const MemMapEntry *aplic_m,
                             const MemMapEntry *aplic_s,
                             const MemMapEntry *imsic_m,
                             const MemMapEntry *imsic_s,
                             int socket, int base_hartid, int hart_count,
                             uint32_t num_msis, uint32_t num_prio_bits);

#endif
