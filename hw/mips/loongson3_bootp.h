/*
 * LEFI (a UEFI-like interface for BIOS-Kernel boot parameters) data structures
 * defined at arch/mips/include/asm/mach-loongson64/boot_param.h in Linux kernel
 *
 * Copyright (c) 2017-2020 Huacai Chen (chenhc@lemote.com)
 * Copyright (c) 2017-2020 Jiaxun Yang <jiaxun.yang@flygoat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HW_MIPS_LOONGSON3_BOOTP_H
#define HW_MIPS_LOONGSON3_BOOTP_H

struct efi_memory_map_loongson {
    uint16_t vers;               /* version of efi_memory_map */
    uint32_t nr_map;             /* number of memory_maps */
    uint32_t mem_freq;           /* memory frequence */
    struct mem_map {
        uint32_t node_id;        /* node_id which memory attached to */
        uint32_t mem_type;       /* system memory, pci memory, pci io, etc. */
        uint64_t mem_start;      /* memory map start address */
        uint32_t mem_size;       /* each memory_map size, not the total size */
    } map[128];
} QEMU_PACKED;

enum loongson_cpu_type {
    Legacy_2E = 0x0,
    Legacy_2F = 0x1,
    Legacy_3A = 0x2,
    Legacy_3B = 0x3,
    Legacy_1A = 0x4,
    Legacy_1B = 0x5,
    Legacy_2G = 0x6,
    Legacy_2H = 0x7,
    Loongson_1A = 0x100,
    Loongson_1B = 0x101,
    Loongson_2E = 0x200,
    Loongson_2F = 0x201,
    Loongson_2G = 0x202,
    Loongson_2H = 0x203,
    Loongson_3A = 0x300,
    Loongson_3B = 0x301
};

/*
 * Capability and feature descriptor structure for MIPS CPU
 */
struct efi_cpuinfo_loongson {
    uint16_t vers;               /* version of efi_cpuinfo_loongson */
    uint32_t processor_id;       /* PRID, e.g. 6305, 6306 */
    uint32_t cputype;            /* Loongson_3A/3B, etc. */
    uint32_t total_node;         /* num of total numa nodes */
    uint16_t cpu_startup_core_id;   /* Boot core id */
    uint16_t reserved_cores_mask;
    uint32_t cpu_clock_freq;     /* cpu_clock */
    uint32_t nr_cpus;
    char cpuname[64];
} QEMU_PACKED;

#define MAX_UARTS 64
struct uart_device {
    uint32_t iotype;
    uint32_t uartclk;
    uint32_t int_offset;
    uint64_t uart_base;
} QEMU_PACKED;

#define MAX_SENSORS 64
#define SENSOR_TEMPER  0x00000001
#define SENSOR_VOLTAGE 0x00000002
#define SENSOR_FAN     0x00000004
struct sensor_device {
    char name[32];  /* a formal name */
    char label[64]; /* a flexible description */
    uint32_t type;       /* SENSOR_* */
    uint32_t id;         /* instance id of a sensor-class */
    uint32_t fan_policy; /* step speed or constant speed */
    uint32_t fan_percent;/* only for constant speed policy */
    uint64_t base_addr;  /* base address of device registers */
} QEMU_PACKED;

struct system_loongson {
    uint16_t vers;               /* version of system_loongson */
    uint32_t ccnuma_smp;         /* 0: no numa; 1: has numa */
    uint32_t sing_double_channel;/* 1: single; 2: double */
    uint32_t nr_uarts;
    struct uart_device uarts[MAX_UARTS];
    uint32_t nr_sensors;
    struct sensor_device sensors[MAX_SENSORS];
    char has_ec;
    char ec_name[32];
    uint64_t ec_base_addr;
    char has_tcm;
    char tcm_name[32];
    uint64_t tcm_base_addr;
    uint64_t workarounds;
    uint64_t of_dtb_addr; /* NULL if not support */
} QEMU_PACKED;

struct irq_source_routing_table {
    uint16_t vers;
    uint16_t size;
    uint16_t rtr_bus;
    uint16_t rtr_devfn;
    uint32_t vendor;
    uint32_t device;
    uint32_t PIC_type;           /* conform use HT or PCI to route to CPU-PIC */
    uint64_t ht_int_bit;         /* 3A: 1<<24; 3B: 1<<16 */
    uint64_t ht_enable;          /* irqs used in this PIC */
    uint32_t node_id;            /* node id: 0x0-0; 0x1-1; 0x10-2; 0x11-3 */
    uint64_t pci_mem_start_addr;
    uint64_t pci_mem_end_addr;
    uint64_t pci_io_start_addr;
    uint64_t pci_io_end_addr;
    uint64_t pci_config_addr;
    uint16_t dma_mask_bits;
    uint16_t dma_noncoherent;
} QEMU_PACKED;

struct interface_info {
    uint16_t vers;               /* version of the specificition */
    uint16_t size;
    uint8_t  flag;
    char description[64];
} QEMU_PACKED;

#define MAX_RESOURCE_NUMBER 128
struct resource_loongson {
    uint64_t start;              /* resource start address */
    uint64_t end;                /* resource end address */
    char name[64];
    uint32_t flags;
};

struct archdev_data {};          /* arch specific additions */

struct board_devices {
    char name[64];               /* hold the device name */
    uint32_t num_resources;      /* number of device_resource */
    /* for each device's resource */
    struct resource_loongson resource[MAX_RESOURCE_NUMBER];
    /* arch specific additions */
    struct archdev_data archdata;
};

struct loongson_special_attribute {
    uint16_t vers;               /* version of this special */
    char special_name[64];       /* special_atribute_name */
    uint32_t loongson_special_type; /* type of special device */
    /* for each device's resource */
    struct resource_loongson resource[MAX_RESOURCE_NUMBER];
};

struct loongson_params {
    uint64_t memory_offset;      /* efi_memory_map_loongson struct offset */
    uint64_t cpu_offset;         /* efi_cpuinfo_loongson struct offset */
    uint64_t system_offset;      /* system_loongson struct offset */
    uint64_t irq_offset;         /* irq_source_routing_table struct offset */
    uint64_t interface_offset;   /* interface_info struct offset */
    uint64_t special_offset;     /* loongson_special_attribute struct offset */
    uint64_t boarddev_table_offset;  /* board_devices offset */
};

struct smbios_tables {
    uint16_t vers;               /* version of smbios */
    uint64_t vga_bios;           /* vga_bios address */
    struct loongson_params lp;
};

struct efi_reset_system_t {
    uint64_t ResetCold;
    uint64_t ResetWarm;
    uint64_t ResetType;
    uint64_t Shutdown;
    uint64_t DoSuspend; /* NULL if not support */
};

struct efi_loongson {
    uint64_t mps;                /* MPS table */
    uint64_t acpi;               /* ACPI table (IA64 ext 0.71) */
    uint64_t acpi20;             /* ACPI table (ACPI 2.0) */
    struct smbios_tables smbios; /* SM BIOS table */
    uint64_t sal_systab;         /* SAL system table */
    uint64_t boot_info;          /* boot info table */
};

struct boot_params {
    struct efi_loongson efi;
    struct efi_reset_system_t reset_system;
};

/* Overall MMIO & Memory layout */
enum {
    VIRT_LOWMEM,
    VIRT_PM,
    VIRT_FW_CFG,
    VIRT_RTC,
    VIRT_PCIE_PIO,
    VIRT_PCIE_ECAM,
    VIRT_BIOS_ROM,
    VIRT_UART,
    VIRT_LIOINTC,
    VIRT_PCIE_MMIO,
    VIRT_HIGHMEM
};

/* Low MEM layout for QEMU kernel loader */
enum {
    LOADER_KERNEL,
    LOADER_INITRD,
    LOADER_CMDLINE
};

/* BIOS ROM layout for QEMU kernel loader */
enum {
    LOADER_BOOTROM,
    LOADER_PARAM,
};

extern const MemMapEntry virt_memmap[];
void init_loongson_params(struct loongson_params *lp, void *p,
                          uint64_t cpu_freq, uint64_t ram_size);
void init_reset_system(struct efi_reset_system_t *reset);

#endif
