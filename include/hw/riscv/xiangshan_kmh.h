/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * QEMU RISC-V Board Compatible with the Xiangshan Kunminghu
 * FPGA prototype platform
 *
 * Copyright (c) 2025 Beijing Institute of Open Source Chip (BOSC)
 *
 */

#ifndef HW_XIANGSHAN_KMH_H
#define HW_XIANGSHAN_KMH_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"

#define XIANGSHAN_KMH_MAX_CPUS 16

typedef struct XiangshanKmhSoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *irqchip;
    MemoryRegion rom;
} XiangshanKmhSoCState;

#define TYPE_XIANGSHAN_KMH_SOC "xiangshan.kunminghu.soc"
DECLARE_INSTANCE_CHECKER(XiangshanKmhSoCState, XIANGSHAN_KMH_SOC,
                         TYPE_XIANGSHAN_KMH_SOC)

typedef struct XiangshanKmhState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    XiangshanKmhSoCState soc;
} XiangshanKmhState;

#define TYPE_XIANGSHAN_KMH_MACHINE MACHINE_TYPE_NAME("xiangshan-kunminghu")
DECLARE_INSTANCE_CHECKER(XiangshanKmhState, XIANGSHAN_KMH_MACHINE,
                         TYPE_XIANGSHAN_KMH_MACHINE)

enum {
    XIANGSHAN_KMH_ROM,
    XIANGSHAN_KMH_UART0,
    XIANGSHAN_KMH_CLINT,
    XIANGSHAN_KMH_APLIC_M,
    XIANGSHAN_KMH_APLIC_S,
    XIANGSHAN_KMH_IMSIC_M,
    XIANGSHAN_KMH_IMSIC_S,
    XIANGSHAN_KMH_DRAM,
};

enum {
    XIANGSHAN_KMH_UART0_IRQ = 10,
};

/* Indicating Timebase-freq (1MHZ) */
#define XIANGSHAN_KMH_CLINT_TIMEBASE_FREQ 1000000

#define XIANGSHAN_KMH_IMSIC_NUM_IDS 255
#define XIANGSHAN_KMH_IMSIC_NUM_GUESTS 7
#define XIANGSHAN_KMH_IMSIC_GUEST_BITS 3

#define XIANGSHAN_KMH_APLIC_NUM_SOURCES 96

#endif
