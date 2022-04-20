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

void acpi_fetch_rsdp_table(QTestState *qts, uint64_t addr, uint8_t *rsdp_table)
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

/** acpi_fetch_table
 *  load ACPI table at @addr_ptr offset pointer into buffer and return it in
 *  @aml, its length in @aml_len and check that signature/checksum matches
 *  actual one.
 */
void acpi_fetch_table(QTestState *qts, uint8_t **aml, uint32_t *aml_len,
                      const uint8_t *addr_ptr, int addr_size, const char *sig,
                      bool verify_checksum)
{
    uint32_t len;
    uint64_t addr = 0;

    g_assert(addr_size == 4 || addr_size == 8);
    memcpy(&addr, addr_ptr , addr_size);
    addr = le64_to_cpu(addr);
    qtest_memread(qts, addr + 4, &len, 4); /* Length of ACPI table */
    *aml_len = le32_to_cpu(len);
    *aml = g_malloc0(*aml_len);
    /* get whole table */
    qtest_memread(qts, addr, *aml, *aml_len);

    if (sig) {
        ACPI_ASSERT_CMP(**aml, sig);
    }
    if (verify_checksum) {
        if (acpi_calc_checksum(*aml, *aml_len)) {
            gint fd, ret;
            char *fname = NULL;
            GError *error = NULL;

            fprintf(stderr, "Invalid '%.4s'(%d)\n", *aml, *aml_len);
            fd = g_file_open_tmp("malformed-XXXXXX.dat", &fname, &error);
            g_assert_no_error(error);
            fprintf(stderr, "Dumping invalid table into '%s'\n", fname);
            ret = qemu_write_full(fd, *aml, *aml_len);
            g_assert(ret == *aml_len);
            close(fd);
            g_free(fname);
        }
        g_assert(!acpi_calc_checksum(*aml, *aml_len));
    }
}

#define GUID_SIZE 16
static const uint8_t AcpiTestSupportGuid[GUID_SIZE] = {
       0xb1, 0xa6, 0x87, 0xab,
       0x34, 0x20,
       0xa0, 0xbd,
       0x71, 0xbd, 0x37, 0x50, 0x07, 0x75, 0x77, 0x85 };

typedef struct {
    uint8_t signature_guid[GUID_SIZE];
    uint64_t rsdp10;
    uint64_t rsdp20;
} __attribute__((packed)) UefiTestSupport;

/* Wait at most 600 seconds (test is slow with TCG and --enable-debug) */
#define TEST_DELAY (1 * G_USEC_PER_SEC / 10)
#define TEST_CYCLES MAX((600 * G_USEC_PER_SEC / TEST_DELAY), 1)
#define MB 0x100000ULL
uint64_t acpi_find_rsdp_address_uefi(QTestState *qts, uint64_t start,
                                     uint64_t size)
{
    int i, j;
    uint8_t data[GUID_SIZE];

    for (i = 0; i < TEST_CYCLES; ++i) {
        for (j = 0; j < size / MB; j++) {
            /* look for GUID at every 1Mb block */
            uint64_t addr = start + j * MB;

            qtest_memread(qts, addr, data, sizeof(data));
            if (!memcmp(AcpiTestSupportGuid, data, sizeof(data))) {
                UefiTestSupport ret;

                qtest_memread(qts, addr, &ret, sizeof(ret));
                ret.rsdp10 = le64_to_cpu(ret.rsdp10);
                ret.rsdp20 = le64_to_cpu(ret.rsdp20);
                return ret.rsdp20 ? ret.rsdp20 : ret.rsdp10;
            }
        }
        g_usleep(TEST_DELAY);
    }
    g_assert_not_reached();
    return 0;
}
