/*
 * Support for generating APEI tables and recording CPER for Guests:
 * stub functions.
 *
 * Copyright (c) 2021 Linaro, Ltd
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/acpi/ghes.h"

int acpi_ghes_memory_errors(AcpiGhesState *ags, uint16_t source_id,
                            uint64_t physical_address)
{
    return -1;
}

AcpiGhesState *acpi_ghes_get_state(void)
{
    return NULL;
}
