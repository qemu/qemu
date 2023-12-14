/*
 * ACPI Error Record Serialization Table, ERST, Implementation
 *
 * ACPI ERST introduced in ACPI 4.0, June 16, 2009.
 * ACPI Platform Error Interfaces : Error Serialization
 *
 * Copyright (c) 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_ACPI_ERST_H
#define HW_ACPI_ERST_H

#include "hw/acpi/bios-linker-loader.h"
#include "qom/object.h"

void build_erst(GArray *table_data, BIOSLinker *linker, Object *erst_dev,
                const char *oem_id, const char *oem_table_id);

#define TYPE_ACPI_ERST "acpi-erst"

/* returns NULL unless there is exactly one device */
static inline Object *find_erst_dev(void)
{
    return object_resolve_path_type("", TYPE_ACPI_ERST, NULL);
}
#endif
