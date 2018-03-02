/*
 * Spike machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
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

#ifndef HW_SPIKE_H
#define HW_SPIKE_H

#define TYPE_RISCV_SPIKE_V1_09_1_BOARD "riscv.spike_v1_9_1"
#define TYPE_RISCV_SPIKE_V1_10_0_BOARD "riscv.spike_v1_10"

#define SPIKE(obj) \
    OBJECT_CHECK(SpikeState, (obj), TYPE_RISCV_SPIKE_BOARD)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState soc;
    void *fdt;
    int fdt_size;
} SpikeState;


enum {
    SPIKE_MROM,
    SPIKE_CLINT,
    SPIKE_DRAM
};

#if defined(TARGET_RISCV32)
#define SPIKE_V1_09_1_CPU TYPE_RISCV_CPU_RV32GCSU_V1_09_1
#define SPIKE_V1_10_0_CPU TYPE_RISCV_CPU_RV32GCSU_V1_10_0
#elif defined(TARGET_RISCV64)
#define SPIKE_V1_09_1_CPU TYPE_RISCV_CPU_RV64GCSU_V1_09_1
#define SPIKE_V1_10_0_CPU TYPE_RISCV_CPU_RV64GCSU_V1_10_0
#endif

#endif
