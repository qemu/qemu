/*
 * RISC-V IMSIC (Incoming Message Signal Interrupt Controller) interface
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
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

#ifndef HW_RISCV_IMSIC_H
#define HW_RISCV_IMSIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RISCV_IMSIC "riscv.imsic"

typedef struct RISCVIMSICState RISCVIMSICState;
DECLARE_INSTANCE_CHECKER(RISCVIMSICState, RISCV_IMSIC, TYPE_RISCV_IMSIC)

#define IMSIC_MMIO_PAGE_SHIFT          12
#define IMSIC_MMIO_PAGE_SZ             (1UL << IMSIC_MMIO_PAGE_SHIFT)
#define IMSIC_MMIO_SIZE(__num_pages)   ((__num_pages) * IMSIC_MMIO_PAGE_SZ)

#define IMSIC_MMIO_HART_GUEST_MAX_BTIS 6
#define IMSIC_MMIO_GROUP_MIN_SHIFT     24

#define IMSIC_HART_NUM_GUESTS(__guest_bits)           \
    (1U << (__guest_bits))
#define IMSIC_HART_SIZE(__guest_bits)                 \
    (IMSIC_HART_NUM_GUESTS(__guest_bits) * IMSIC_MMIO_PAGE_SZ)
#define IMSIC_GROUP_NUM_HARTS(__hart_bits)            \
    (1U << (__hart_bits))
#define IMSIC_GROUP_SIZE(__hart_bits, __guest_bits)   \
    (IMSIC_GROUP_NUM_HARTS(__hart_bits) * IMSIC_HART_SIZE(__guest_bits))

struct RISCVIMSICState {
    /*< private >*/
    SysBusDevice parent_obj;
    qemu_irq *external_irqs;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t num_eistate;
    uint32_t *eidelivery;
    uint32_t *eithreshold;
    uint32_t *eistate;

    /* config */
    bool mmode;
    uint32_t hartid;
    uint32_t num_pages;
    uint32_t num_irqs;
};

DeviceState *riscv_imsic_create(hwaddr addr, uint32_t hartid, bool mmode,
                                uint32_t num_pages, uint32_t num_ids);

#endif
