/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for loongarch board emulation.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_H
#define HW_LOONGARCH_H

#include "target/loongarch/cpu.h"
#include "hw/boards.h"
#include "qemu/queue.h"
#include "hw/intc/loongarch_ipi.h"

#define LOONGARCH_MAX_VCPUS     4

#define LOONGARCH_ISA_IO_BASE   0x18000000UL
#define LOONGARCH_ISA_IO_SIZE   0x0004000
#define VIRT_FWCFG_BASE         0x1e020000UL
#define VIRT_BIOS_BASE          0x1c000000UL
#define VIRT_BIOS_SIZE          (4 * MiB)

struct LoongArchMachineState {
    /*< private >*/
    MachineState parent_obj;

    IPICore ipi_core[MAX_IPI_CORE_NUM];
    MemoryRegion lowmem;
    MemoryRegion highmem;
    MemoryRegion isa_io;
    MemoryRegion bios;
    bool         bios_loaded;
    /* State for other subsystems/APIs: */
    FWCfgState  *fw_cfg;
    Notifier     machine_done;
};

#define TYPE_LOONGARCH_MACHINE  MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchMachineState, LOONGARCH_MACHINE)
#endif
