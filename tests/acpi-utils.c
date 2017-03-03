/*
 * ACPI Utility Functions
 *
 * Copyright (c) 2013 Red Hat Inc.
 * Copyright (c) 2017 Skyport Systems
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *  Ben Warren <ben@skyportsystems.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "qemu-common.h"
#include "hw/smbios/smbios.h"
#include "qemu/bitmap.h"
#include "acpi-utils.h"
#include "boot-sector.h"

uint8_t acpi_calc_checksum(const uint8_t *data, int len)
{
    int i;
    uint8_t sum = 0;

    for (i = 0; i < len; i++) {
        sum += data[i];
    }

    return sum;
}

uint32_t acpi_find_rsdp_address(void)
{
    uint32_t off;

    /* RSDP location can vary across a narrow range */
    for (off = 0xf0000; off < 0x100000; off += 0x10) {
        uint8_t sig[] = "RSD PTR ";
        int i;

        for (i = 0; i < sizeof sig - 1; ++i) {
            sig[i] = readb(off + i);
        }

        if (!memcmp(sig, "RSD PTR ", sizeof sig)) {
            break;
        }
    }
    return off;
}

void acpi_parse_rsdp_table(uint32_t addr, AcpiRsdpDescriptor *rsdp_table)
{
    ACPI_READ_FIELD(rsdp_table->signature, addr);
    ACPI_ASSERT_CMP64(rsdp_table->signature, "RSD PTR ");

    ACPI_READ_FIELD(rsdp_table->checksum, addr);
    ACPI_READ_ARRAY(rsdp_table->oem_id, addr);
    ACPI_READ_FIELD(rsdp_table->revision, addr);
    ACPI_READ_FIELD(rsdp_table->rsdt_physical_address, addr);
    ACPI_READ_FIELD(rsdp_table->length, addr);
}
