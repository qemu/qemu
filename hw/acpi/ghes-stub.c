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

int acpi_ghes_record_errors(uint8_t source_id, uint64_t physical_address)
{
    return -1;
}

bool acpi_ghes_present(void)
{
    return false;
}
