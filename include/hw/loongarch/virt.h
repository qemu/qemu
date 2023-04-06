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
#include "hw/block/flash.h"

#define LOONGARCH_MAX_CPUS      256

#define VIRT_ISA_IO_BASE        0x18000000UL
#define VIRT_ISA_IO_SIZE        0x0004000
#define VIRT_FWCFG_BASE         0x1e020000UL
#define VIRT_BIOS_BASE          0x1c000000UL
#define VIRT_BIOS_SIZE          (4 * MiB)
#define VIRT_FLASH_SECTOR_SIZE  (128 * KiB)
#define VIRT_FLASH_BASE         0x1d000000UL
#define VIRT_FLASH_SIZE         (16 * MiB)

#define VIRT_LOWMEM_BASE        0
#define VIRT_LOWMEM_SIZE        0x10000000
#define VIRT_HIGHMEM_BASE       0x90000000
#define VIRT_GED_EVT_ADDR       0x100e0000
#define VIRT_GED_MEM_ADDR       (VIRT_GED_EVT_ADDR + ACPI_GED_EVT_SEL_LEN)
#define VIRT_GED_REG_ADDR       (VIRT_GED_MEM_ADDR + MEMORY_HOTPLUG_IO_LEN)

struct LoongArchMachineState {
    /*< private >*/
    MachineState parent_obj;

    MemoryRegion lowmem;
    MemoryRegion highmem;
    MemoryRegion isa_io;
    MemoryRegion bios;
    bool         bios_loaded;
    /* State for other subsystems/APIs: */
    FWCfgState  *fw_cfg;
    Notifier     machine_done;
    Notifier     powerdown_notifier;
    OnOffAuto    acpi;
    char         *oem_id;
    char         *oem_table_id;
    DeviceState  *acpi_ged;
    int          fdt_size;
    DeviceState *platform_bus_dev;
    PCIBus       *pci_bus;
    PFlashCFI01  *flash;
};

#define TYPE_LOONGARCH_MACHINE  MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchMachineState, LOONGARCH_MACHINE)
bool loongarch_is_acpi_enabled(LoongArchMachineState *lams);
void loongarch_acpi_setup(LoongArchMachineState *lams);
#endif
