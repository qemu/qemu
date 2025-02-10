/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Definitions for loongarch board emulation.
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_H
#define HW_LOONGARCH_H

#include "hw/boards.h"
#include "qemu/queue.h"
#include "hw/block/flash.h"
#include "hw/loongarch/boot.h"

#define LOONGARCH_MAX_CPUS      256

#define VIRT_FWCFG_BASE         0x1e020000UL
#define VIRT_BIOS_BASE          0x1c000000UL
#define VIRT_BIOS_SIZE          (16 * MiB)
#define VIRT_FLASH_SECTOR_SIZE  (256 * KiB)
#define VIRT_FLASH0_BASE        VIRT_BIOS_BASE
#define VIRT_FLASH0_SIZE        VIRT_BIOS_SIZE
#define VIRT_FLASH1_BASE        0x1d000000UL
#define VIRT_FLASH1_SIZE        (16 * MiB)

#define VIRT_LOWMEM_BASE        0
#define VIRT_LOWMEM_SIZE        0x10000000
#define VIRT_HIGHMEM_BASE       0x80000000
#define VIRT_GED_EVT_ADDR       0x100e0000
#define VIRT_GED_MEM_ADDR       (VIRT_GED_EVT_ADDR + ACPI_GED_EVT_SEL_LEN)
#define VIRT_GED_REG_ADDR       (VIRT_GED_MEM_ADDR + MEMORY_HOTPLUG_IO_LEN)
#define VIRT_GED_CPUHP_ADDR     (VIRT_GED_REG_ADDR + ACPI_GED_REG_COUNT)

#define COMMAND_LINE_SIZE       512

#define FDT_BASE                0x100000

struct LoongArchVirtMachineState {
    /*< private >*/
    MachineState parent_obj;

    MemoryRegion lowmem;
    MemoryRegion highmem;
    MemoryRegion bios;
    bool         bios_loaded;
    /* State for other subsystems/APIs: */
    FWCfgState  *fw_cfg;
    Notifier     machine_done;
    Notifier     powerdown_notifier;
    OnOffAuto    acpi;
    OnOffAuto    veiointc;
    char         *oem_id;
    char         *oem_table_id;
    DeviceState  *acpi_ged;
    int          fdt_size;
    DeviceState *platform_bus_dev;
    PCIBus       *pci_bus;
    PFlashCFI01  *flash[2];
    MemoryRegion system_iocsr;
    MemoryRegion iocsr_mem;
    AddressSpace as_iocsr;
    struct loongarch_boot_info bootinfo;
    DeviceState *ipi;
    DeviceState *extioi;
};

#define TYPE_LOONGARCH_VIRT_MACHINE  MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchVirtMachineState, LOONGARCH_VIRT_MACHINE)
void virt_acpi_setup(LoongArchVirtMachineState *lvms);
void virt_fdt_setup(LoongArchVirtMachineState *lvms);

static inline bool virt_is_veiointc_enabled(LoongArchVirtMachineState *lvms)
{
    if (lvms->veiointc == ON_OFF_AUTO_OFF) {
        return false;
    }
    return true;
}

#endif
