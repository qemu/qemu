/*
 * QEMU RISC-V Hart Array interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Holds the state of a heterogeneous array of RISC-V harts
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

#ifndef HW_RISCV_HART_H
#define HW_RISCV_HART_H

#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "qom/object.h"

#define TYPE_RISCV_HART_ARRAY "riscv.hart_array"

OBJECT_DECLARE_SIMPLE_TYPE(RISCVHartArrayState, RISCV_HART_ARRAY)

struct RISCVHartArrayState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    uint32_t num_harts;
    uint32_t hartid_base;
    char *cpu_type;
    uint64_t resetvec;
    uint32_t num_rnmi_irqvec;
    uint64_t *rnmi_irqvec;
    uint32_t num_rnmi_excpvec;
    uint64_t *rnmi_excpvec;
    RISCVCPU *harts;
};

#endif
