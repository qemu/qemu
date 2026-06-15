/*
 * Copyright Red Hat, Inc. 2026
 * Author(s): Igor Mammedov <imammedo@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef QEMU_HW_ACPI_WDAT_ICH9_H
#define QEMU_HW_ACPI_WDAT_ICH9_H

#include "hw/acpi/aml-build.h"

void build_ich9_wdat(GArray *table_data, BIOSLinker *linker, const char *oem_id,
                     const char *oem_table_id, uint64_t tco_base);

#endif /* QEMU_HW_ACPI_WDAT_ICH9_H */
