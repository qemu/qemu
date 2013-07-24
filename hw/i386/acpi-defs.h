/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef QEMU_ACPI_DEFS_H
#define QEMU_ACPI_DEFS_H

enum {
    ACPI_FADT_F_WBINVD,
    ACPI_FADT_F_WBINVD_FLUSH,
    ACPI_FADT_F_PROC_C1,
    ACPI_FADT_F_P_LVL2_UP,
    ACPI_FADT_F_PWR_BUTTON,
    ACPI_FADT_F_SLP_BUTTON,
    ACPI_FADT_F_FIX_RTC,
    ACPI_FADT_F_RTC_S4,
    ACPI_FADT_F_TMR_VAL_EXT,
    ACPI_FADT_F_DCK_CAP,
    ACPI_FADT_F_RESET_REG_SUP,
    ACPI_FADT_F_SEALED_CASE,
    ACPI_FADT_F_HEADLESS,
    ACPI_FADT_F_CPU_SW_SLP,
    ACPI_FADT_F_PCI_EXP_WAK,
    ACPI_FADT_F_USE_PLATFORM_CLOCK,
    ACPI_FADT_F_S4_RTC_STS_VALID,
    ACPI_FADT_F_REMOTE_POWER_ON_CAPABLE,
    ACPI_FADT_F_FORCE_APIC_CLUSTER_MODEL,
    ACPI_FADT_F_FORCE_APIC_PHYSICAL_DESTINATION_MODE,
    ACPI_FADT_F_HW_REDUCED_ACPI,
    ACPI_FADT_F_LOW_POWER_S0_IDLE_CAPABLE,
};

/*
 * ACPI 2.0 Generic Address Space definition.
 */
struct Acpi20GenericAddress {
    uint8_t  address_space_id;
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  reserved;
    uint64_t address;
} QEMU_PACKED;
typedef struct Acpi20GenericAddress Acpi20GenericAddress;

#define ACPI_RSDP_SIGNATURE 0x2052545020445352LL // "RSD PTR "

struct AcpiRsdpDescriptor {        /* Root System Descriptor Pointer */
    uint64_t signature;              /* ACPI signature, contains "RSD PTR " */
    uint8_t  checksum;               /* To make sum of struct == 0 */
    uint8_t  oem_id [6];             /* OEM identification */
    uint8_t  revision;               /* Must be 0 for 1.0, 2 for 2.0 */
    uint32_t rsdt_physical_address;  /* 32-bit physical address of RSDT */
    uint32_t length;                 /* XSDT Length in bytes including hdr */
    uint64_t xsdt_physical_address;  /* 64-bit physical address of XSDT */
    uint8_t  extended_checksum;      /* Checksum of entire table */
    uint8_t  reserved [3];           /* Reserved field must be 0 */
} QEMU_PACKED;
typedef struct AcpiRsdpDescriptor AcpiRsdpDescriptor;

/* Table structure from Linux kernel (the ACPI tables are under the
   BSD license) */


#define ACPI_TABLE_HEADER_DEF   /* ACPI common table header */ \
    uint32_t signature;          /* ACPI signature (4 ASCII characters) */ \
    uint32_t length;                 /* Length of table, in bytes, including header */ \
    uint8_t  revision;               /* ACPI Specification minor version # */ \
    uint8_t  checksum;               /* To make sum of entire table == 0 */ \
    uint8_t  oem_id [6];             /* OEM identification */ \
    uint8_t  oem_table_id [8];       /* OEM table identification */ \
    uint32_t oem_revision;           /* OEM revision number */ \
    uint8_t  asl_compiler_id [4];    /* ASL compiler vendor ID */ \
    uint32_t asl_compiler_revision;  /* ASL compiler revision number */


struct AcpiTableHeader         /* ACPI common table header */
{
    ACPI_TABLE_HEADER_DEF
} QEMU_PACKED;
typedef struct AcpiTableHeader AcpiTableHeader;

/*
 * ACPI 1.0 Fixed ACPI Description Table (FADT)
 */
#define ACPI_FACP_SIGNATURE 0x50434146 // FACP
struct AcpiFadtDescriptorRev1
{
    ACPI_TABLE_HEADER_DEF     /* ACPI common table header */
    uint32_t firmware_ctrl;          /* Physical address of FACS */
    uint32_t dsdt;                   /* Physical address of DSDT */
    uint8_t  model;                  /* System Interrupt Model */
    uint8_t  reserved1;              /* Reserved */
    uint16_t sci_int;                /* System vector of SCI interrupt */
    uint32_t smi_cmd;                /* Port address of SMI command port */
    uint8_t  acpi_enable;            /* Value to write to smi_cmd to enable ACPI */
    uint8_t  acpi_disable;           /* Value to write to smi_cmd to disable ACPI */
    uint8_t  S4bios_req;             /* Value to write to SMI CMD to enter S4BIOS state */
    uint8_t  reserved2;              /* Reserved - must be zero */
    uint32_t pm1a_evt_blk;           /* Port address of Power Mgt 1a acpi_event Reg Blk */
    uint32_t pm1b_evt_blk;           /* Port address of Power Mgt 1b acpi_event Reg Blk */
    uint32_t pm1a_cnt_blk;           /* Port address of Power Mgt 1a Control Reg Blk */
    uint32_t pm1b_cnt_blk;           /* Port address of Power Mgt 1b Control Reg Blk */
    uint32_t pm2_cnt_blk;            /* Port address of Power Mgt 2 Control Reg Blk */
    uint32_t pm_tmr_blk;             /* Port address of Power Mgt Timer Ctrl Reg Blk */
    uint32_t gpe0_blk;               /* Port addr of General Purpose acpi_event 0 Reg Blk */
    uint32_t gpe1_blk;               /* Port addr of General Purpose acpi_event 1 Reg Blk */
    uint8_t  pm1_evt_len;            /* Byte length of ports at pm1_x_evt_blk */
    uint8_t  pm1_cnt_len;            /* Byte length of ports at pm1_x_cnt_blk */
    uint8_t  pm2_cnt_len;            /* Byte Length of ports at pm2_cnt_blk */
    uint8_t  pm_tmr_len;             /* Byte Length of ports at pm_tm_blk */
    uint8_t  gpe0_blk_len;           /* Byte Length of ports at gpe0_blk */
    uint8_t  gpe1_blk_len;           /* Byte Length of ports at gpe1_blk */
    uint8_t  gpe1_base;              /* Offset in gpe model where gpe1 events start */
    uint8_t  reserved3;              /* Reserved */
    uint16_t plvl2_lat;              /* Worst case HW latency to enter/exit C2 state */
    uint16_t plvl3_lat;              /* Worst case HW latency to enter/exit C3 state */
    uint16_t flush_size;             /* Size of area read to flush caches */
    uint16_t flush_stride;           /* Stride used in flushing caches */
    uint8_t  duty_offset;            /* Bit location of duty cycle field in p_cnt reg */
    uint8_t  duty_width;             /* Bit width of duty cycle field in p_cnt reg */
    uint8_t  day_alrm;               /* Index to day-of-month alarm in RTC CMOS RAM */
    uint8_t  mon_alrm;               /* Index to month-of-year alarm in RTC CMOS RAM */
    uint8_t  century;                /* Index to century in RTC CMOS RAM */
    uint8_t  reserved4;              /* Reserved */
    uint8_t  reserved4a;             /* Reserved */
    uint8_t  reserved4b;             /* Reserved */
    uint32_t flags;
} QEMU_PACKED;
typedef struct AcpiFadtDescriptorRev1 AcpiFadtDescriptorRev1;

/*
 * ACPI 1.0 Root System Description Table (RSDT)
 */
#define ACPI_RSDT_SIGNATURE 0x54445352 // RSDT
struct AcpiRsdtDescriptorRev1
{
    ACPI_TABLE_HEADER_DEF       /* ACPI common table header */
    uint32_t table_offset_entry[0];  /* Array of pointers to other */
    /* ACPI tables */
} QEMU_PACKED;
typedef struct AcpiRsdtDescriptorRev1 AcpiRsdtDescriptorRev1;

/*
 * ACPI 1.0 Firmware ACPI Control Structure (FACS)
 */
#define ACPI_FACS_SIGNATURE 0x53434146 // FACS
struct AcpiFacsDescriptorRev1
{
    uint32_t signature;           /* ACPI Signature */
    uint32_t length;                 /* Length of structure, in bytes */
    uint32_t hardware_signature;     /* Hardware configuration signature */
    uint32_t firmware_waking_vector; /* ACPI OS waking vector */
    uint32_t global_lock;            /* Global Lock */
    uint32_t flags;
    uint8_t  resverved3 [40];        /* Reserved - must be zero */
} QEMU_PACKED;
typedef struct AcpiFacsDescriptorRev1 AcpiFacsDescriptorRev1;

/*
 * Differentiated System Description Table (DSDT)
 */
#define ACPI_DSDT_SIGNATURE 0x54445344 // DSDT

/*
 * MADT values and structures
 */

/* Values for MADT PCATCompat */

#define ACPI_DUAL_PIC                0
#define ACPI_MULTIPLE_APIC           1

/* Master MADT */

#define ACPI_APIC_SIGNATURE 0x43495041 // APIC
struct AcpiMultipleApicTable
{
    ACPI_TABLE_HEADER_DEF     /* ACPI common table header */
    uint32_t local_apic_address;     /* Physical address of local APIC */
    uint32_t flags;
} QEMU_PACKED;
typedef struct AcpiMultipleApicTable AcpiMultipleApicTable;

/* Values for Type in APIC sub-headers */

#define ACPI_APIC_PROCESSOR          0
#define ACPI_APIC_IO                 1
#define ACPI_APIC_XRUPT_OVERRIDE     2
#define ACPI_APIC_NMI                3
#define ACPI_APIC_LOCAL_NMI          4
#define ACPI_APIC_ADDRESS_OVERRIDE   5
#define ACPI_APIC_IO_SAPIC           6
#define ACPI_APIC_LOCAL_SAPIC        7
#define ACPI_APIC_XRUPT_SOURCE       8
#define ACPI_APIC_RESERVED           9           /* 9 and greater are reserved */

/*
 * MADT sub-structures (Follow MULTIPLE_APIC_DESCRIPTION_TABLE)
 */
#define ACPI_SUB_HEADER_DEF   /* Common ACPI sub-structure header */\
    uint8_t  type;                               \
    uint8_t  length;

/* Sub-structures for MADT */

struct AcpiMadtProcessorApic
{
    ACPI_SUB_HEADER_DEF
    uint8_t  processor_id;           /* ACPI processor id */
    uint8_t  local_apic_id;          /* Processor's local APIC id */
    uint32_t flags;
} QEMU_PACKED;
typedef struct AcpiMadtProcessorApic AcpiMadtProcessorApic;

struct AcpiMadtIoApic
{
    ACPI_SUB_HEADER_DEF
    uint8_t  io_apic_id;             /* I/O APIC ID */
    uint8_t  reserved;               /* Reserved - must be zero */
    uint32_t address;                /* APIC physical address */
    uint32_t interrupt;              /* Global system interrupt where INTI
                                 * lines start */
} QEMU_PACKED;
typedef struct AcpiMadtIoApic AcpiMadtIoApic;

struct AcpiMadtIntsrcovr {
    ACPI_SUB_HEADER_DEF
    uint8_t  bus;
    uint8_t  source;
    uint32_t gsi;
    uint16_t flags;
} QEMU_PACKED;
typedef struct AcpiMadtIntsrcovr AcpiMadtIntsrcovr;

struct AcpiMadtLocalNmi {
    ACPI_SUB_HEADER_DEF
    uint8_t  processor_id;           /* ACPI processor id */
    uint16_t flags;                  /* MPS INTI flags */
    uint8_t  lint;                   /* Local APIC LINT# */
} QEMU_PACKED;
typedef struct AcpiMadtLocalNmi AcpiMadtLocalNmi;

/*
 * HPET Description Table
 */
#define ACPI_HPET_SIGNATURE 0x54455048 // HPET
struct Acpi20Hpet {
    ACPI_TABLE_HEADER_DEF                    /* ACPI common table header */
    uint32_t           timer_block_id;
    Acpi20GenericAddress addr;
    uint8_t            hpet_number;
    uint16_t           min_tick;
    uint8_t            page_protect;
} QEMU_PACKED;
typedef struct Acpi20Hpet Acpi20Hpet;

/*
 * SRAT (NUMA topology description) table
 */

#define ACPI_SRAT_SIGNATURE 0x54415253 // SRAT
struct AcpiSystemResourceAffinityTable
{
    ACPI_TABLE_HEADER_DEF
    uint32_t    reserved1;
    uint32_t    reserved2[2];
} QEMU_PACKED;
typedef struct AcpiSystemResourceAffinityTable AcpiSystemResourceAffinityTable;

#define ACPI_SRAT_PROCESSOR          0
#define ACPI_SRAT_MEMORY             1

struct AcpiSratProcessorAffinity
{
    ACPI_SUB_HEADER_DEF
    uint8_t     proximity_lo;
    uint8_t     local_apic_id;
    uint32_t    flags;
    uint8_t     local_sapic_eid;
    uint8_t     proximity_hi[3];
    uint32_t    reserved;
} QEMU_PACKED;
typedef struct AcpiSratProcessorAffinity AcpiSratProcessorAffinity;

struct AcpiSratMemoryAffinity
{
    ACPI_SUB_HEADER_DEF
    uint8_t     proximity[4];
    uint16_t    reserved1;
    uint64_t    base_addr;
    uint64_t    range_length;
    uint32_t    reserved2;
    uint32_t    flags;
    uint32_t    reserved3[2];
} QEMU_PACKED;
typedef struct AcpiSratMemoryAffinity AcpiSratMemoryAffinity;

/* PCI fw r3.0 MCFG table. */
/* Subtable */
struct AcpiMcfgAllocation {
    uint64_t address;                /* Base address, processor-relative */
    uint16_t pci_segment;            /* PCI segment group number */
    uint8_t start_bus_number;       /* Starting PCI Bus number */
    uint8_t end_bus_number;         /* Final PCI Bus number */
    uint32_t reserved;
} QEMU_PACKED;
typedef struct AcpiMcfgAllocation AcpiMcfgAllocation;

#define ACPI_MCFG_SIGNATURE 0x4746434d       // MCFG

/* Reserved signature: ignored by OSPM */
#define ACPI_RSRV_SIGNATURE 0x554d4551       // QEMU

struct AcpiTableMcfg {
    ACPI_TABLE_HEADER_DEF;
    uint8_t reserved[8];
    AcpiMcfgAllocation allocation[0];
} QEMU_PACKED;
typedef struct AcpiTableMcfg AcpiTableMcfg;

#endif
