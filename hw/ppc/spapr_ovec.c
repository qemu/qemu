/*
 * QEMU SPAPR Architecture Option Vector Helper Functions
 *
 * Copyright IBM Corp. 2016
 *
 * Authors:
 *  Bharata B Rao     <bharata@linux.vnet.ibm.com>
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/ppc/spapr_ovec.h"
#include "qemu/bitmap.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "trace.h"
#include <libfdt.h>

#define OV_MAXBYTES 256 /* not including length byte */
#define OV_MAXBITS (OV_MAXBYTES * BITS_PER_BYTE)

/* we *could* work with bitmaps directly, but handling the bitmap privately
 * allows us to more safely make assumptions about the bitmap size and
 * simplify the calling code somewhat
 */
struct sPAPROptionVector {
    unsigned long *bitmap;
    int32_t bitmap_size; /* only used for migration */
};

const VMStateDescription vmstate_spapr_ovec = {
    .name = "spapr_option_vector",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BITMAP(bitmap, sPAPROptionVector, 1, bitmap_size),
        VMSTATE_END_OF_LIST()
    }
};

sPAPROptionVector *spapr_ovec_new(void)
{
    sPAPROptionVector *ov;

    ov = g_new0(sPAPROptionVector, 1);
    ov->bitmap = bitmap_new(OV_MAXBITS);
    ov->bitmap_size = OV_MAXBITS;

    return ov;
}

sPAPROptionVector *spapr_ovec_clone(sPAPROptionVector *ov_orig)
{
    sPAPROptionVector *ov;

    g_assert(ov_orig);

    ov = spapr_ovec_new();
    bitmap_copy(ov->bitmap, ov_orig->bitmap, OV_MAXBITS);

    return ov;
}

void spapr_ovec_intersect(sPAPROptionVector *ov,
                          sPAPROptionVector *ov1,
                          sPAPROptionVector *ov2)
{
    g_assert(ov);
    g_assert(ov1);
    g_assert(ov2);

    bitmap_and(ov->bitmap, ov1->bitmap, ov2->bitmap, OV_MAXBITS);
}

/* returns true if options bits were removed, false otherwise */
bool spapr_ovec_diff(sPAPROptionVector *ov,
                     sPAPROptionVector *ov_old,
                     sPAPROptionVector *ov_new)
{
    unsigned long *change_mask = bitmap_new(OV_MAXBITS);
    unsigned long *removed_bits = bitmap_new(OV_MAXBITS);
    bool bits_were_removed = false;

    g_assert(ov);
    g_assert(ov_old);
    g_assert(ov_new);

    bitmap_xor(change_mask, ov_old->bitmap, ov_new->bitmap, OV_MAXBITS);
    bitmap_and(ov->bitmap, ov_new->bitmap, change_mask, OV_MAXBITS);
    bitmap_and(removed_bits, ov_old->bitmap, change_mask, OV_MAXBITS);

    if (!bitmap_empty(removed_bits, OV_MAXBITS)) {
        bits_were_removed = true;
    }

    g_free(change_mask);
    g_free(removed_bits);

    return bits_were_removed;
}

void spapr_ovec_cleanup(sPAPROptionVector *ov)
{
    if (ov) {
        g_free(ov->bitmap);
        g_free(ov);
    }
}

void spapr_ovec_set(sPAPROptionVector *ov, long bitnr)
{
    g_assert(ov);
    g_assert_cmpint(bitnr, <, OV_MAXBITS);

    set_bit(bitnr, ov->bitmap);
}

void spapr_ovec_clear(sPAPROptionVector *ov, long bitnr)
{
    g_assert(ov);
    g_assert_cmpint(bitnr, <, OV_MAXBITS);

    clear_bit(bitnr, ov->bitmap);
}

bool spapr_ovec_test(sPAPROptionVector *ov, long bitnr)
{
    g_assert(ov);
    g_assert_cmpint(bitnr, <, OV_MAXBITS);

    return test_bit(bitnr, ov->bitmap) ? true : false;
}

static void guest_byte_to_bitmap(uint8_t entry, unsigned long *bitmap,
                                 long bitmap_offset)
{
    int i;

    for (i = 0; i < BITS_PER_BYTE; i++) {
        if (entry & (1 << (BITS_PER_BYTE - 1 - i))) {
            bitmap_set(bitmap, bitmap_offset + i, 1);
        }
    }
}

static uint8_t guest_byte_from_bitmap(unsigned long *bitmap, long bitmap_offset)
{
    uint8_t entry = 0;
    int i;

    for (i = 0; i < BITS_PER_BYTE; i++) {
        if (test_bit(bitmap_offset + i, bitmap)) {
            entry |= (1 << (BITS_PER_BYTE - 1 - i));
        }
    }

    return entry;
}

static target_ulong vector_addr(target_ulong table_addr, int vector)
{
    uint16_t vector_count, vector_len;
    int i;

    vector_count = ldub_phys(&address_space_memory, table_addr) + 1;
    if (vector > vector_count) {
        return 0;
    }
    table_addr++; /* skip nr option vectors */

    for (i = 0; i < vector - 1; i++) {
        vector_len = ldub_phys(&address_space_memory, table_addr) + 1;
        table_addr += vector_len + 1; /* bit-vector + length byte */
    }
    return table_addr;
}

sPAPROptionVector *spapr_ovec_parse_vector(target_ulong table_addr, int vector)
{
    sPAPROptionVector *ov;
    target_ulong addr;
    uint16_t vector_len;
    int i;

    g_assert(table_addr);
    g_assert_cmpint(vector, >=, 1); /* vector numbering starts at 1 */

    addr = vector_addr(table_addr, vector);
    if (!addr) {
        /* specified vector isn't present */
        return NULL;
    }

    vector_len = ldub_phys(&address_space_memory, addr++) + 1;
    g_assert_cmpint(vector_len, <=, OV_MAXBYTES);
    ov = spapr_ovec_new();

    for (i = 0; i < vector_len; i++) {
        uint8_t entry = ldub_phys(&address_space_memory, addr + i);
        if (entry) {
            trace_spapr_ovec_parse_vector(vector, i + 1, vector_len, entry);
            guest_byte_to_bitmap(entry, ov->bitmap, i * BITS_PER_BYTE);
        }
    }

    return ov;
}

int spapr_ovec_populate_dt(void *fdt, int fdt_offset,
                           sPAPROptionVector *ov, const char *name)
{
    uint8_t vec[OV_MAXBYTES + 1];
    uint16_t vec_len;
    unsigned long lastbit;
    int i;

    g_assert(ov);

    lastbit = find_last_bit(ov->bitmap, OV_MAXBITS);
    /* if no bits are set, include at least 1 byte of the vector so we can
     * still encoded this in the device tree while abiding by the same
     * encoding/sizing expected in ibm,client-architecture-support
     */
    vec_len = (lastbit == OV_MAXBITS) ? 1 : lastbit / BITS_PER_BYTE + 1;
    g_assert_cmpint(vec_len, <=, OV_MAXBYTES);
    /* guest expects vector len encoded as vec_len - 1, since the length byte
     * is assumed and not included, and the first byte of the vector
     * is assumed as well
     */
    vec[0] = vec_len - 1;

    for (i = 1; i < vec_len + 1; i++) {
        vec[i] = guest_byte_from_bitmap(ov->bitmap, (i - 1) * BITS_PER_BYTE);
        if (vec[i]) {
            trace_spapr_ovec_populate_dt(i, vec_len, vec[i]);
        }
    }

    return fdt_setprop(fdt, fdt_offset, name, vec, vec_len + 1);
}
