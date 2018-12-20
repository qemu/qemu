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

uint32_t acpi_find_rsdp_address(QTestState *qts)
{
    uint32_t off;

    /* RSDP location can vary across a narrow range */
    for (off = 0xf0000; off < 0x100000; off += 0x10) {
        uint8_t sig[] = "RSD PTR ";
        int i;

        for (i = 0; i < sizeof sig - 1; ++i) {
            sig[i] = qtest_readb(qts, off + i);
        }

        if (!memcmp(sig, "RSD PTR ", sizeof sig)) {
            break;
        }
    }
    return off;
}

uint32_t acpi_get_rsdt_address(uint8_t *rsdp_table)
{
    uint32_t rsdt_physical_address;

    memcpy(&rsdt_physical_address, &rsdp_table[16 /* RsdtAddress offset */], 4);
    return le32_to_cpu(rsdt_physical_address);
}

uint64_t acpi_get_xsdt_address(uint8_t *rsdp_table)
{
    uint64_t xsdt_physical_address;
    uint8_t revision = rsdp_table[15 /* Revision offset */];

    /* We must have revision 2 if we're looking for an XSDT pointer */
    g_assert(revision == 2);

    memcpy(&xsdt_physical_address, &rsdp_table[24 /* XsdtAddress offset */], 8);
    return le64_to_cpu(xsdt_physical_address);
}

void acpi_parse_rsdp_table(QTestState *qts, uint32_t addr, uint8_t *rsdp_table)
{
    uint8_t revision;

    /* Read mandatory revision 0 table data (20 bytes) first */
    qtest_memread(qts, addr, rsdp_table, 20);
    revision = rsdp_table[15 /* Revision offset */];

    switch (revision) {
    case 0: /* ACPI 1.0 RSDP */
        break;
    case 2: /* ACPI 2.0+ RSDP */
        /* Read the rest of the RSDP table */
        qtest_memread(qts, addr + 20, rsdp_table + 20, 16);
        break;
    default:
        g_assert_not_reached();
    }

    ACPI_ASSERT_CMP64(*((uint64_t *)(rsdp_table)), "RSD PTR ");
}
