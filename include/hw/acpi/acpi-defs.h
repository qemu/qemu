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

typedef struct AcpiRsdpData {
    char *oem_id;                     /* OEM identification */
    uint8_t revision;                 /* Must be 0 for 1.0, 2 for 2.0 */

    unsigned *rsdt_tbl_offset;
    unsigned *xsdt_tbl_offset;
} AcpiRsdpData;

/* Table structure from Linux kernel (the ACPI tables are under the
   BSD license) */


#define ACPI_TABLE_HEADER_DEF   /* ACPI common table header */ \
    uint32_t signature;          /* ACPI signature (4 ASCII characters) */ \
    uint32_t length;                 /* Length of table, in bytes, including header */ \
    uint8_t  revision;               /* ACPI Specification minor version # */ \
    uint8_t  checksum;               /* To make sum of entire table == 0 */ \
    uint8_t  oem_id[6] \
                 QEMU_NONSTRING;     /* OEM identification */ \
    uint8_t  oem_table_id[8] \
                 QEMU_NONSTRING;     /* OEM table identification */ \
    uint32_t oem_revision;           /* OEM revision number */ \
    uint8_t  asl_compiler_id[4] \
                 QEMU_NONSTRING;     /* ASL compiler vendor ID */ \
    uint32_t asl_compiler_revision;  /* ASL compiler revision number */


/* ACPI common table header */
struct AcpiTableHeader {
    ACPI_TABLE_HEADER_DEF
} QEMU_PACKED;
typedef struct AcpiTableHeader AcpiTableHeader;

struct AcpiGenericAddress {
    uint8_t space_id;        /* Address space where struct or register exists */
    uint8_t bit_width;       /* Size in bits of given register */
    uint8_t bit_offset;      /* Bit offset within the register */
    uint8_t access_width;    /* ACPI 3.0: Minimum Access size (ACPI 3.0),
                                ACPI 2.0: Reserved, Table 5-1 */
    uint64_t address;        /* 64-bit address of struct or register */
} QEMU_PACKED;

typedef struct AcpiFadtData {
    struct AcpiGenericAddress pm1a_cnt;   /* PM1a_CNT_BLK */
    struct AcpiGenericAddress pm1a_evt;   /* PM1a_EVT_BLK */
    struct AcpiGenericAddress pm_tmr;    /* PM_TMR_BLK */
    struct AcpiGenericAddress gpe0_blk;  /* GPE0_BLK */
    struct AcpiGenericAddress reset_reg; /* RESET_REG */
    struct AcpiGenericAddress sleep_ctl; /* SLEEP_CONTROL_REG */
    struct AcpiGenericAddress sleep_sts; /* SLEEP_STATUS_REG */
    uint8_t reset_val;         /* RESET_VALUE */
    uint8_t  rev;              /* Revision */
    uint32_t flags;            /* Flags */
    uint32_t smi_cmd;          /* SMI_CMD */
    uint16_t sci_int;          /* SCI_INT */
    uint8_t  int_model;        /* INT_MODEL */
    uint8_t  acpi_enable_cmd;  /* ACPI_ENABLE */
    uint8_t  acpi_disable_cmd; /* ACPI_DISABLE */
    uint8_t  rtc_century;      /* CENTURY */
    uint16_t plvl2_lat;        /* P_LVL2_LAT */
    uint16_t plvl3_lat;        /* P_LVL3_LAT */
    uint16_t arm_boot_arch;    /* ARM_BOOT_ARCH */
    uint8_t minor_ver;         /* FADT Minor Version */

    /*
     * respective tables offsets within ACPI_BUILD_TABLE_FILE,
     * NULL if table doesn't exist (in that case field's value
     * won't be patched by linker and will be kept set to 0)
     */
    unsigned *facs_tbl_offset; /* FACS offset in */
    unsigned *dsdt_tbl_offset;
    unsigned *xdsdt_tbl_offset;
} AcpiFadtData;

#define ACPI_FADT_ARM_PSCI_COMPLIANT  (1 << 0)
#define ACPI_FADT_ARM_PSCI_USE_HVC    (1 << 1)

/*
 * Serial Port Console Redirection Table (SPCR), Rev. 1.02
 *
 * For .interface_type see Debug Port Table 2 (DBG2) serial port
 * subtypes in Table 3, Rev. May 22, 2012
 */
struct AcpiSerialPortConsoleRedirection {
    ACPI_TABLE_HEADER_DEF
    uint8_t  interface_type;
    uint8_t  reserved1[3];
    struct AcpiGenericAddress base_address;
    uint8_t  interrupt_types;
    uint8_t  irq;
    uint32_t gsi;
    uint8_t  baud;
    uint8_t  parity;
    uint8_t  stopbits;
    uint8_t  flowctrl;
    uint8_t  term_type;
    uint8_t  reserved2;
    uint16_t pci_device_id;
    uint16_t pci_vendor_id;
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint32_t pci_flags;
    uint8_t  pci_seg;
    uint32_t reserved3;
} QEMU_PACKED;
typedef struct AcpiSerialPortConsoleRedirection
               AcpiSerialPortConsoleRedirection;

/*
 * ACPI 1.0 Firmware ACPI Control Structure (FACS)
 */
struct AcpiFacsDescriptorRev1 {
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
 * Generic Timer Description Table (GTDT)
 */
#define ACPI_GTDT_INTERRUPT_MODE_LEVEL    (0 << 0)
#define ACPI_GTDT_INTERRUPT_MODE_EDGE     (1 << 0)
#define ACPI_GTDT_CAP_ALWAYS_ON           (1 << 2)

struct AcpiGenericTimerTable {
    ACPI_TABLE_HEADER_DEF
    uint64_t counter_block_addresss;
    uint32_t reserved;
    uint32_t secure_el1_interrupt;
    uint32_t secure_el1_flags;
    uint32_t non_secure_el1_interrupt;
    uint32_t non_secure_el1_flags;
    uint32_t virtual_timer_interrupt;
    uint32_t virtual_timer_flags;
    uint32_t non_secure_el2_interrupt;
    uint32_t non_secure_el2_flags;
    uint64_t counter_read_block_address;
    uint32_t platform_timer_count;
    uint32_t platform_timer_offset;
} QEMU_PACKED;
typedef struct AcpiGenericTimerTable AcpiGenericTimerTable;

/*
 * IORT node types
 */

#define ACPI_IORT_NODE_HEADER_DEF   /* Node format common fields */ \
    uint8_t  type;          \
    uint16_t length;        \
    uint8_t  revision;      \
    uint32_t reserved;      \
    uint32_t mapping_count; \
    uint32_t mapping_offset;

/* Values for node Type above */
enum {
        ACPI_IORT_NODE_ITS_GROUP = 0x00,
        ACPI_IORT_NODE_NAMED_COMPONENT = 0x01,
        ACPI_IORT_NODE_PCI_ROOT_COMPLEX = 0x02,
        ACPI_IORT_NODE_SMMU = 0x03,
        ACPI_IORT_NODE_SMMU_V3 = 0x04
};

struct AcpiIortIdMapping {
    uint32_t input_base;
    uint32_t id_count;
    uint32_t output_base;
    uint32_t output_reference;
    uint32_t flags;
} QEMU_PACKED;
typedef struct AcpiIortIdMapping AcpiIortIdMapping;

struct AcpiIortMemoryAccess {
    uint32_t cache_coherency;
    uint8_t  hints;
    uint16_t reserved;
    uint8_t  memory_flags;
} QEMU_PACKED;
typedef struct AcpiIortMemoryAccess AcpiIortMemoryAccess;

struct AcpiIortItsGroup {
    ACPI_IORT_NODE_HEADER_DEF
    uint32_t its_count;
    uint32_t identifiers[];
} QEMU_PACKED;
typedef struct AcpiIortItsGroup AcpiIortItsGroup;

#define ACPI_IORT_SMMU_V3_COHACC_OVERRIDE 1

struct AcpiIortSmmu3 {
    ACPI_IORT_NODE_HEADER_DEF
    uint64_t base_address;
    uint32_t flags;
    uint32_t reserved2;
    uint64_t vatos_address;
    uint32_t model;
    uint32_t event_gsiv;
    uint32_t pri_gsiv;
    uint32_t gerr_gsiv;
    uint32_t sync_gsiv;
    AcpiIortIdMapping id_mapping_array[];
} QEMU_PACKED;
typedef struct AcpiIortSmmu3 AcpiIortSmmu3;

struct AcpiIortRC {
    ACPI_IORT_NODE_HEADER_DEF
    AcpiIortMemoryAccess memory_properties;
    uint32_t ats_attribute;
    uint32_t pci_segment_number;
    AcpiIortIdMapping id_mapping_array[];
} QEMU_PACKED;
typedef struct AcpiIortRC AcpiIortRC;

#endif
