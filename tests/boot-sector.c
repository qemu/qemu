/*
 * QEMU boot sector testing helpers.
 *
 * Copyright (c) 2016 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *  Victor Kaplansky <victork@redhat.com>    
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "boot-sector.h"
#include "qemu-common.h"
#include "libqtest.h"

#define LOW(x) ((x) & 0xff)
#define HIGH(x) ((x) >> 8)

#define SIGNATURE 0xdead
#define SIGNATURE_OFFSET 0x10
#define BOOT_SECTOR_ADDRESS 0x7c00

/* Boot sector code: write SIGNATURE into memory,
 * then halt.
 * Q35 machine requires a minimum 0x7e000 bytes disk.
 * (bug or feature?)
 */
static uint8_t boot_sector[0x7e000] = {
    /* The first sector will be placed at RAM address 00007C00, and
     * the BIOS transfers control to 00007C00
     */

    /* Data Segment register should be initialized, since pxe
     * boot loader can leave it dirty.
     */

    /* 7c00: move $0000,%ax */
    [0x00] = 0xb8,
    [0x01] = 0x00,
    [0x02] = 0x00,
    /* 7c03: move %ax,%ds */
    [0x03] = 0x8e,
    [0x04] = 0xd8,

    /* 7c05: mov $0xdead,%ax */
    [0x05] = 0xb8,
    [0x06] = LOW(SIGNATURE),
    [0x07] = HIGH(SIGNATURE),
    /* 7c08:  mov %ax,0x7c10 */
    [0x08] = 0xa3,
    [0x09] = LOW(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET),
    [0x0a] = HIGH(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET),

    /* 7c0b cli */
    [0x0b] = 0xfa,
    /* 7c0c: hlt */
    [0x0c] = 0xf4,
    /* 7c0e: jmp 0x7c07=0x7c0f-3 */
    [0x0d] = 0xeb,
    [0x0e] = LOW(-3),
    /* We mov 0xdead here: set value to make debugging easier */
    [SIGNATURE_OFFSET] = LOW(0xface),
    [SIGNATURE_OFFSET + 1] = HIGH(0xface),
    /* End of boot sector marker */
    [0x1FE] = 0x55,
    [0x1FF] = 0xAA,
};

/* Create boot disk file.  */
int boot_sector_init(const char *fname)
{
    FILE *f = fopen(fname, "w");

    if (!f) {
        fprintf(stderr, "Couldn't open \"%s\": %s", fname, strerror(errno));
        return 1;
    }
    fwrite(boot_sector, 1, sizeof boot_sector, f);
    fclose(f);
    return 0;
}

/* Loop until signature in memory is OK.  */
void boot_sector_test(void)
{
    uint8_t signature_low;
    uint8_t signature_high;
    uint16_t signature;
    int i;

   /* Wait at most 1 minute */
#define TEST_DELAY (1 * G_USEC_PER_SEC / 10)
#define TEST_CYCLES MAX((60 * G_USEC_PER_SEC / TEST_DELAY), 1)

    /* Poll until code has run and modified memory.  Once it has we know BIOS
     * initialization is done.  TODO: check that IP reached the halt
     * instruction.
     */
    for (i = 0; i < TEST_CYCLES; ++i) {
        signature_low = readb(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET);
        signature_high = readb(BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET + 1);
        signature = (signature_high << 8) | signature_low;
        if (signature == SIGNATURE) {
            break;
        }
        g_usleep(TEST_DELAY);
    }

    g_assert_cmphex(signature, ==, SIGNATURE);
}

/* unlink boot disk file.  */
void boot_sector_cleanup(const char *fname)
{
    unlink(fname);
}
