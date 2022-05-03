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
#include "libqtest.h"

#define LOW(x) ((x) & 0xff)
#define HIGH(x) ((x) >> 8)

#define SIGNATURE 0xdead
#define SIGNATURE_OFFSET 0x10
#define BOOT_SECTOR_ADDRESS 0x7c00
#define SIGNATURE_ADDR (BOOT_SECTOR_ADDRESS + SIGNATURE_OFFSET)

/* x86 boot sector code: write SIGNATURE into memory,
 * then halt.
 */
static uint8_t x86_boot_sector[512] = {
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
    [0x09] = LOW(SIGNATURE_ADDR),
    [0x0a] = HIGH(SIGNATURE_ADDR),

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

/* For s390x, use a mini "kernel" with the appropriate signature */
static const uint8_t s390x_psw_and_magic[] = {
    0x00, 0x08, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00,  /* Program status word  */
    0x02, 0x00, 0x00, 0x18, 0x60, 0x00, 0x00, 0x50,  /* Magic:               */
    0x02, 0x00, 0x00, 0x68, 0x60, 0x00, 0x00, 0x50,  /* see linux_s390_magic */
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40   /* in the s390-ccw bios */
};
static const uint8_t s390x_code[] = {
    0xa7, 0xf4, 0x00, 0x08,                                /* j 0x10010 */
    0x00, 0x00, 0x00, 0x00,
    'S', '3', '9', '0',
    'E', 'P', 0x00, 0x01,
    0xa7, 0x39, HIGH(SIGNATURE_ADDR), LOW(SIGNATURE_ADDR), /* lghi r3,0x7c10 */
    0xa7, 0x48, LOW(SIGNATURE), HIGH(SIGNATURE),           /* lhi r4,0xadde */
    0x40, 0x40, 0x30, 0x00,                                /* sth r4,0(r3) */
    0xa7, 0xf4, 0xff, 0xfa                                 /* j 0x10010 */
};

/* Create boot disk file.  */
int boot_sector_init(char *fname)
{
    int fd, ret;
    size_t len;
    char *boot_code;
    const char *arch = qtest_get_arch();

    fd = mkstemp(fname);
    if (fd < 0) {
        fprintf(stderr, "Couldn't open \"%s\": %s", fname, strerror(errno));
        return 1;
    }

    if (g_str_equal(arch, "i386") || g_str_equal(arch, "x86_64")) {
        /* Q35 requires a minimum 0x7e000 bytes disk (bug or feature?) */
        len = MAX(0x7e000, sizeof(x86_boot_sector));
        boot_code = g_malloc0(len);
        memcpy(boot_code, x86_boot_sector, sizeof(x86_boot_sector));
    } else if (g_str_equal(arch, "ppc64")) {
        /* For Open Firmware based system, use a Forth script */
        boot_code = g_strdup_printf("\\ Bootscript\n%x %x c! %x %x c!\n",
                                    LOW(SIGNATURE), SIGNATURE_ADDR,
                                    HIGH(SIGNATURE), SIGNATURE_ADDR + 1);
        len = strlen(boot_code);
    } else if (g_str_equal(arch, "s390x")) {
        len = 0x10000 + sizeof(s390x_code);
        boot_code = g_malloc0(len);
        memcpy(boot_code, s390x_psw_and_magic, sizeof(s390x_psw_and_magic));
        memcpy(&boot_code[0x10000], s390x_code, sizeof(s390x_code));
    } else {
        g_assert_not_reached();
    }

    ret = write(fd, boot_code, len);
    close(fd);

    g_free(boot_code);

    if (ret != len) {
        fprintf(stderr, "Could not write \"%s\"", fname);
        return 1;
    }

    return 0;
}

/* Loop until signature in memory is OK.  */
void boot_sector_test(QTestState *qts)
{
    uint8_t signature_low;
    uint8_t signature_high;
    uint16_t signature;
    QDict *qrsp, *qret;
    int i;

    /* Wait at most 600 seconds (test is slow with TCI and --enable-debug) */
#define TEST_DELAY (1 * G_USEC_PER_SEC / 10)
#define TEST_CYCLES MAX((600 * G_USEC_PER_SEC / TEST_DELAY), 1)

    /* Poll until code has run and modified memory.  Once it has we know BIOS
     * initialization is done.  TODO: check that IP reached the halt
     * instruction.
     */
    for (i = 0; i < TEST_CYCLES; ++i) {
        signature_low = qtest_readb(qts, SIGNATURE_ADDR);
        signature_high = qtest_readb(qts, SIGNATURE_ADDR + 1);
        signature = (signature_high << 8) | signature_low;
        if (signature == SIGNATURE) {
            break;
        }

        /* check that guest is still in "running" state and did not panic */
        qrsp = qtest_qmp(qts, "{ 'execute': 'query-status' }");
        qret = qdict_get_qdict(qrsp, "return");
        g_assert_nonnull(qret);
        g_assert_cmpstr(qdict_get_try_str(qret, "status"), ==, "running");
        qobject_unref(qrsp);

        g_usleep(TEST_DELAY);
    }

    g_assert_cmphex(signature, ==, SIGNATURE);
}

/* unlink boot disk file.  */
void boot_sector_cleanup(const char *fname)
{
    unlink(fname);
}
