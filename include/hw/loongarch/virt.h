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

/* IOCSR region */
#define VERSION_REG             0x0
#define FEATURE_REG             0x8
#define  IOCSRF_TEMP             0
#define  IOCSRF_NODECNT          1
#define  IOCSRF_MSI              2
#define  IOCSRF_EXTIOI           3
#define  IOCSRF_CSRIPI           4
#define  IOCSRF_FREQCSR          5
#define  IOCSRF_FREQSCALE        6
#define  IOCSRF_DVFSV1           7
#define  IOCSRF_GMOD             9
#define  IOCSRF_VM               11
#define  IOCSRF_DMSI             15
#define VENDOR_REG              0x10
#define CPUNAME_REG             0x20
#define MISC_FUNC_REG           0x420
#define  IOCSRM_EXTIOI_EN         48
#define  IOCSRM_EXTIOI_INT_ENCODE 49
#define  IOCSRM_DMSI_EN           51

#define LOONGARCH_MAX_CPUS      256

/* MMIO memory region */
#define VIRT_PCH_REG_BASE       0x10000000UL
#define VIRT_PCH_REG_SIZE       0x400
#define VIRT_RTC_REG_BASE       0x100d0100UL
#define VIRT_RTC_LEN            0x100
#define VIRT_PLATFORM_BUS_BASEADDRESS   0x16000000UL
#define VIRT_PLATFORM_BUS_SIZE          0x02000000
#define VIRT_PCI_IO_BASE        0x18004000UL
#define VIRT_PCI_IO_OFFSET      0x4000
#define VIRT_PCI_IO_SIZE        0xC000
#define VIRT_BIOS_BASE          0x1c000000UL
#define VIRT_BIOS_SIZE          0x01000000UL
#define VIRT_FLASH_SECTOR_SIZE  (256 * KiB)
#define VIRT_FLASH0_BASE        VIRT_BIOS_BASE
#define VIRT_FLASH0_SIZE        VIRT_BIOS_SIZE
#define VIRT_FLASH1_BASE        0x1d000000UL
#define VIRT_FLASH1_SIZE        0x01000000UL
#define VIRT_FWCFG_BASE         0x1e020000UL
#define VIRT_UART_BASE          0x1fe001e0UL
#define VIRT_UART_SIZE          0x100
#define VIRT_PCI_CFG_BASE       0x20000000UL
#define VIRT_PCI_CFG_SIZE       0x08000000UL
#define VIRT_DINTC_BASE         0x2FE00000UL
#define VIRT_DINTC_SIZE         0x00100000UL
#define VIRT_PCH_MSI_ADDR_LOW   0x2FF00000UL
#define VIRT_PCH_MSI_SIZE       0x8
#define VIRT_PCI_MEM_BASE       0x40000000UL
#define VIRT_PCI_MEM_SIZE       0x40000000UL

#define VIRT_LOWMEM_BASE        0
#define VIRT_LOWMEM_SIZE        0x10000000
#define FDT_BASE                0x100000
#define VIRT_HIGHMEM_BASE       0x80000000
#define VIRT_GED_EVT_ADDR       0x100e0000
#define VIRT_GED_MEM_ADDR       QEMU_ALIGN_UP(VIRT_GED_EVT_ADDR + ACPI_GED_EVT_SEL_LEN, 4)
#define VIRT_GED_REG_ADDR       QEMU_ALIGN_UP(VIRT_GED_MEM_ADDR + MEMORY_HOTPLUG_IO_LEN, 4)
#define VIRT_GED_CPUHP_ADDR     QEMU_ALIGN_UP(VIRT_GED_REG_ADDR + ACPI_GED_REG_COUNT, 4)

/*
 * GSI_BASE is hard-coded with 64 in linux kernel, else kernel fails to boot
 * 0  - 15  GSI for ISA devices even if there is no ISA devices
 * 16 - 63  GSI for CPU devices such as timers/perf monitor etc
 * 64 -     GSI for external devices
 */
#define VIRT_PCH_PIC_IRQ_NUM     32
#define VIRT_GSI_BASE            64
#define VIRT_DEVICE_IRQS         16
#define VIRT_UART_IRQ            (VIRT_GSI_BASE + 2)
#define VIRT_UART_COUNT          4
#define VIRT_RTC_IRQ             (VIRT_GSI_BASE + 6)
#define VIRT_SCI_IRQ             (VIRT_GSI_BASE + 7)
#define VIRT_PLATFORM_BUS_IRQ    (VIRT_GSI_BASE + 8)
#define VIRT_PLATFORM_BUS_NUM_IRQS      2

#define COMMAND_LINE_SIZE       512

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
    OnOffAuto    dmsi;
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
    struct memmap_entry *memmap_table;
    unsigned int memmap_entries;
    uint64_t misc_feature;
    uint64_t misc_status;
    DeviceState *dintc;
};

#define TYPE_LOONGARCH_VIRT_MACHINE  MACHINE_TYPE_NAME("virt")
OBJECT_DECLARE_SIMPLE_TYPE(LoongArchVirtMachineState, LOONGARCH_VIRT_MACHINE)
void virt_acpi_setup(LoongArchVirtMachineState *lvms);
void virt_fdt_setup(LoongArchVirtMachineState *lvms);

static inline bool virt_has_dmsi(LoongArchVirtMachineState *lvms)
{
    if (!(lvms->misc_feature & BIT(IOCSRF_DMSI))) {
        return false;
    }

    return true;
}

static inline bool virt_is_veiointc_enabled(LoongArchVirtMachineState *lvms)
{
    if (lvms->veiointc == ON_OFF_AUTO_OFF) {
        return false;
    }
    return true;
}

#endif
