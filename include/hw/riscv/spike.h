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

#ifndef HW_RISCV_SPIKE_H
#define HW_RISCV_SPIKE_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define SPIKE_CPUS_MAX 8
#define SPIKE_SOCKETS_MAX 8

#define TYPE_SPIKE_MACHINE MACHINE_TYPE_NAME("spike")
typedef struct SpikeState SpikeState;
DECLARE_INSTANCE_CHECKER(SpikeState, SPIKE_MACHINE,
                         TYPE_SPIKE_MACHINE)

struct SpikeState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVHartArrayState soc[SPIKE_SOCKETS_MAX];
    void *fdt;
    int fdt_size;
};

enum {
    SPIKE_MROM,
    SPIKE_HTIF,
    SPIKE_CLINT,
    SPIKE_DRAM
};

#endif
