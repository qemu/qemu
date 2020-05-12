/*
 * Support for generating APEI tables and recording CPER for Guests
 *
 * Copyright (c) 2020 HUAWEI TECHNOLOGIES CO., LTD.
 *
 * Author: Dongjiu Geng <gengdongjiu@huawei.com>
 *
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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/acpi/ghes.h"
#include "hw/acpi/aml-build.h"
#include "qemu/error-report.h"

#define ACPI_GHES_ERRORS_FW_CFG_FILE        "etc/hardware_errors"
#define ACPI_GHES_DATA_ADDR_FW_CFG_FILE     "etc/hardware_errors_addr"

/* The max size in bytes for one error block */
#define ACPI_GHES_MAX_RAW_DATA_LENGTH   (1 * KiB)

/* Now only support ARMv8 SEA notification type error source */
#define ACPI_GHES_ERROR_SOURCE_COUNT        1

/* Generic Hardware Error Source version 2 */
#define ACPI_GHES_SOURCE_GENERIC_ERROR_V2   10

/* Address offset in Generic Address Structure(GAS) */
#define GAS_ADDR_OFFSET 4

/*
 * Hardware Error Notification
 * ACPI 4.0: 17.3.2.7 Hardware Error Notification
 * Composes dummy Hardware Error Notification descriptor of specified type
 */
static void build_ghes_hw_error_notification(GArray *table, const uint8_t type)
{
    /* Type */
    build_append_int_noprefix(table, type, 1);
    /*
     * Length:
     * Total length of the structure in bytes
     */
    build_append_int_noprefix(table, 28, 1);
    /* Configuration Write Enable */
    build_append_int_noprefix(table, 0, 2);
    /* Poll Interval */
    build_append_int_noprefix(table, 0, 4);
    /* Vector */
    build_append_int_noprefix(table, 0, 4);
    /* Switch To Polling Threshold Value */
    build_append_int_noprefix(table, 0, 4);
    /* Switch To Polling Threshold Window */
    build_append_int_noprefix(table, 0, 4);
    /* Error Threshold Value */
    build_append_int_noprefix(table, 0, 4);
    /* Error Threshold Window */
    build_append_int_noprefix(table, 0, 4);
}

/*
 * Build table for the hardware error fw_cfg blob.
 * Initialize "etc/hardware_errors" and "etc/hardware_errors_addr" fw_cfg blobs.
 * See docs/specs/acpi_hest_ghes.rst for blobs format.
 */
void build_ghes_error_table(GArray *hardware_errors, BIOSLinker *linker)
{
    int i, error_status_block_offset;

    /* Build error_block_address */
    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        build_append_int_noprefix(hardware_errors, 0, sizeof(uint64_t));
    }

    /* Build read_ack_register */
    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        /*
         * Initialize the value of read_ack_register to 1, so GHES can be
         * writeable after (re)boot.
         * ACPI 6.2: 18.3.2.8 Generic Hardware Error Source version 2
         * (GHESv2 - Type 10)
         */
        build_append_int_noprefix(hardware_errors, 1, sizeof(uint64_t));
    }

    /* Generic Error Status Block offset in the hardware error fw_cfg blob */
    error_status_block_offset = hardware_errors->len;

    /* Reserve space for Error Status Data Block */
    acpi_data_push(hardware_errors,
        ACPI_GHES_MAX_RAW_DATA_LENGTH * ACPI_GHES_ERROR_SOURCE_COUNT);

    /* Tell guest firmware to place hardware_errors blob into RAM */
    bios_linker_loader_alloc(linker, ACPI_GHES_ERRORS_FW_CFG_FILE,
                             hardware_errors, sizeof(uint64_t), false);

    for (i = 0; i < ACPI_GHES_ERROR_SOURCE_COUNT; i++) {
        /*
         * Tell firmware to patch error_block_address entries to point to
         * corresponding "Generic Error Status Block"
         */
        bios_linker_loader_add_pointer(linker,
            ACPI_GHES_ERRORS_FW_CFG_FILE, sizeof(uint64_t) * i,
            sizeof(uint64_t), ACPI_GHES_ERRORS_FW_CFG_FILE,
            error_status_block_offset + i * ACPI_GHES_MAX_RAW_DATA_LENGTH);
    }

    /*
     * tell firmware to write hardware_errors GPA into
     * hardware_errors_addr fw_cfg, once the former has been initialized.
     */
    bios_linker_loader_write_pointer(linker, ACPI_GHES_DATA_ADDR_FW_CFG_FILE,
        0, sizeof(uint64_t), ACPI_GHES_ERRORS_FW_CFG_FILE, 0);
}

/* Build Generic Hardware Error Source version 2 (GHESv2) */
static void build_ghes_v2(GArray *table_data, int source_id, BIOSLinker *linker)
{
    uint64_t address_offset;
    /*
     * Type:
     * Generic Hardware Error Source version 2(GHESv2 - Type 10)
     */
    build_append_int_noprefix(table_data, ACPI_GHES_SOURCE_GENERIC_ERROR_V2, 2);
    /* Source Id */
    build_append_int_noprefix(table_data, source_id, 2);
    /* Related Source Id */
    build_append_int_noprefix(table_data, 0xffff, 2);
    /* Flags */
    build_append_int_noprefix(table_data, 0, 1);
    /* Enabled */
    build_append_int_noprefix(table_data, 1, 1);

    /* Number of Records To Pre-allocate */
    build_append_int_noprefix(table_data, 1, 4);
    /* Max Sections Per Record */
    build_append_int_noprefix(table_data, 1, 4);
    /* Max Raw Data Length */
    build_append_int_noprefix(table_data, ACPI_GHES_MAX_RAW_DATA_LENGTH, 4);

    address_offset = table_data->len;
    /* Error Status Address */
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
                     4 /* QWord access */, 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
        address_offset + GAS_ADDR_OFFSET, sizeof(uint64_t),
        ACPI_GHES_ERRORS_FW_CFG_FILE, source_id * sizeof(uint64_t));

    switch (source_id) {
    case ACPI_HEST_SRC_ID_SEA:
        /*
         * Notification Structure
         * Now only enable ARMv8 SEA notification type
         */
        build_ghes_hw_error_notification(table_data, ACPI_GHES_NOTIFY_SEA);
        break;
    default:
        error_report("Not support this error source");
        abort();
    }

    /* Error Status Block Length */
    build_append_int_noprefix(table_data, ACPI_GHES_MAX_RAW_DATA_LENGTH, 4);

    /*
     * Read Ack Register
     * ACPI 6.1: 18.3.2.8 Generic Hardware Error Source
     * version 2 (GHESv2 - Type 10)
     */
    address_offset = table_data->len;
    build_append_gas(table_data, AML_AS_SYSTEM_MEMORY, 0x40, 0,
                     4 /* QWord access */, 0);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
        address_offset + GAS_ADDR_OFFSET,
        sizeof(uint64_t), ACPI_GHES_ERRORS_FW_CFG_FILE,
        (ACPI_GHES_ERROR_SOURCE_COUNT + source_id) * sizeof(uint64_t));

    /*
     * Read Ack Preserve field
     * We only provide the first bit in Read Ack Register to OSPM to write
     * while the other bits are preserved.
     */
    build_append_int_noprefix(table_data, ~0x1ULL, 8);
    /* Read Ack Write */
    build_append_int_noprefix(table_data, 0x1, 8);
}

/* Build Hardware Error Source Table */
void acpi_build_hest(GArray *table_data, BIOSLinker *linker)
{
    uint64_t hest_start = table_data->len;

    /* Hardware Error Source Table header*/
    acpi_data_push(table_data, sizeof(AcpiTableHeader));

    /* Error Source Count */
    build_append_int_noprefix(table_data, ACPI_GHES_ERROR_SOURCE_COUNT, 4);

    build_ghes_v2(table_data, ACPI_HEST_SRC_ID_SEA, linker);

    build_header(linker, table_data, (void *)(table_data->data + hest_start),
        "HEST", table_data->len - hest_start, 1, NULL, NULL);
}
