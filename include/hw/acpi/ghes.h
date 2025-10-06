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

#ifndef ACPI_GHES_H
#define ACPI_GHES_H

#include "hw/acpi/bios-linker-loader.h"
#include "qapi/error.h"
#include "qemu/notify.h"

extern NotifierList acpi_generic_error_notifiers;

/*
 * Values for Hardware Error Notification Type field
 */
enum AcpiGhesNotifyType {
    /* Polled */
    ACPI_GHES_NOTIFY_POLLED = 0,
    /* External Interrupt */
    ACPI_GHES_NOTIFY_EXTERNAL = 1,
    /* Local Interrupt */
    ACPI_GHES_NOTIFY_LOCAL = 2,
    /* SCI */
    ACPI_GHES_NOTIFY_SCI = 3,
    /* NMI */
    ACPI_GHES_NOTIFY_NMI = 4,
    /* CMCI, ACPI 5.0: 18.3.2.7, Table 18-290 */
    ACPI_GHES_NOTIFY_CMCI = 5,
    /* MCE, ACPI 5.0: 18.3.2.7, Table 18-290 */
    ACPI_GHES_NOTIFY_MCE = 6,
    /* GPIO-Signal, ACPI 6.0: 18.3.2.7, Table 18-332 */
    ACPI_GHES_NOTIFY_GPIO = 7,
    /* ARMv8 SEA, ACPI 6.1: 18.3.2.9, Table 18-345 */
    ACPI_GHES_NOTIFY_SEA = 8,
    /* ARMv8 SEI, ACPI 6.1: 18.3.2.9, Table 18-345 */
    ACPI_GHES_NOTIFY_SEI = 9,
    /* External Interrupt - GSIV, ACPI 6.1: 18.3.2.9, Table 18-345 */
    ACPI_GHES_NOTIFY_GSIV = 10,
    /* Software Delegated Exception, ACPI 6.2: 18.3.2.9, Table 18-383 */
    ACPI_GHES_NOTIFY_SDEI = 11,
    /* 12 and greater are reserved */
    ACPI_GHES_NOTIFY_RESERVED = 12
};

/*
 * ID numbers used to fill HEST source ID field
 */
enum AcpiGhesSourceID {
    ACPI_HEST_SRC_ID_SYNC,
    ACPI_HEST_SRC_ID_QMP,       /* Use it only for QMP injected errors */
};

typedef struct AcpiNotificationSourceId {
    enum AcpiGhesSourceID source_id;
    enum AcpiGhesNotifyType notify;
} AcpiNotificationSourceId;

/*
 * AcpiGhesState stores GPA values that will be used to fill HEST entries.
 *
 * When use_hest_addr is false, the GPA of the etc/hardware_errors firmware
 * is stored at hw_error_le. This is the default on QEMU 9.x.
 *
 * When use_hest_addr is true, the GPA of the HEST table is stored at
 * hest_addr_le. This is the default for QEMU 10.x and above.
 *
 * Whe both GPA values are equal to zero means that GHES is not present.
 */
typedef struct AcpiGhesState {
    uint64_t hest_addr_le;
    uint64_t hw_error_le;
    bool use_hest_addr; /* True if HEST address is present */
} AcpiGhesState;

void acpi_build_hest(AcpiGhesState *ags, GArray *table_data,
                     GArray *hardware_errors,
                     BIOSLinker *linker,
                     const AcpiNotificationSourceId * const notif_source,
                     int num_sources,
                     const char *oem_id, const char *oem_table_id);
void acpi_ghes_add_fw_cfg(AcpiGhesState *vms, FWCfgState *s,
                          GArray *hardware_errors);
int acpi_ghes_memory_errors(AcpiGhesState *ags, uint16_t source_id,
                            uint64_t error_physical_addr);
void ghes_record_cper_errors(AcpiGhesState *ags, const void *cper, size_t len,
                             uint16_t source_id, Error **errp);

/**
 * acpi_ghes_get_state: Get a pointer for ACPI ghes state
 *
 * Returns: a pointer to ghes state if the system has an ACPI GHES table,
 * NULL, otherwise.
 */
AcpiGhesState *acpi_ghes_get_state(void);
#endif
