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

struct AcpiGenericAddress {
    uint8_t space_id;        /* Address space where struct or register exists */
    uint8_t bit_width;       /* Size in bits of given register */
    uint8_t bit_offset;      /* Bit offset within the register */
    uint8_t access_width;    /* ACPI 3.0: Minimum Access size (ACPI 3.0),
                                ACPI 2.0: Reserved, Table 5-1 */
    uint64_t address;        /* 64-bit address of struct or register */
};

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
    uint16_t iapc_boot_arch;   /* IAPC_BOOT_ARCH */
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

typedef struct AcpiGas {
    uint8_t id;                /* Address space ID */
    uint8_t width;             /* Register bit width */
    uint8_t offset;            /* Register bit offset */
    uint8_t size;              /* Access size */
    uint64_t addr;             /* Address */
} AcpiGas;

/* SPCR (Serial Port Console Redirection table) */
typedef struct AcpiSpcrData {
    uint8_t interface_type;
    uint8_t reserved[3];
    struct AcpiGas base_addr;
    uint8_t interrupt_type;
    uint8_t pc_interrupt;
    uint32_t interrupt;        /* Global system interrupt */
    uint8_t baud_rate;
    uint8_t parity;
    uint8_t stop_bits;
    uint8_t flow_control;
    uint8_t terminal_type;
    uint8_t language;
    uint8_t reserved1;
    uint16_t pci_device_id;    /* Must be 0xffff if not PCI device */
    uint16_t pci_vendor_id;    /* Must be 0xffff if not PCI device */
    uint8_t pci_bus;
    uint8_t pci_device;
    uint8_t pci_function;
    uint32_t pci_flags;
    uint8_t pci_segment;
    uint32_t reserved2;
} AcpiSpcrData;

#define ACPI_FADT_ARM_PSCI_COMPLIANT  (1 << 0)
#define ACPI_FADT_ARM_PSCI_USE_HVC    (1 << 1)

#endif
