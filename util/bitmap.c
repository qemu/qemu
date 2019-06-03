/*
 * Bitmap Module
 *
 * Stolen from linux/src/lib/bitmap.c
 *
 * Copyright (C) 2010 Corentin Chary
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/atomic.h"

/*
 * bitmaps provide an array of bits, implemented using an
 * array of unsigned longs.  The number of valid bits in a
 * given bitmap does _not_ need to be an exact multiple of
 * BITS_PER_LONG.
 *
 * The possible unused bits in the last, partially used word
 * of a bitmap are 'don't care'.  The implementation makes
 * no particular effort to keep them zero.  It ensures that
 * their value will not affect the results of any operation.
 * The bitmap operations that return Boolean (bitmap_empty,
 * for example) or scalar (bitmap_weight, for example) results
 * carefully filter out these unused bits from impacting their
 * results.
 *
 * These operations actually hold to a slightly stronger rule:
 * if you don't input any bitmaps to these ops that have some
 * unused bits set, then they won't output any set unused bits
 * in output bitmaps.
 *
 * The byte ordering of bitmaps is more natural on little
 * endian architectures.
 */

int slow_bitmap_empty(const unsigned long *bitmap, long bits)
{
    long k, lim = bits/BITS_PER_LONG;

    for (k = 0; k < lim; ++k) {
        if (bitmap[k]) {
            return 0;
        }
    }
    if (bits % BITS_PER_LONG) {
        if (bitmap[k] & BITMAP_LAST_WORD_MASK(bits)) {
            return 0;
        }
    }

    return 1;
}

int slow_bitmap_full(const unsigned long *bitmap, long bits)
{
    long k, lim = bits/BITS_PER_LONG;

    for (k = 0; k < lim; ++k) {
        if (~bitmap[k]) {
            return 0;
        }
    }

    if (bits % BITS_PER_LONG) {
        if (~bitmap[k] & BITMAP_LAST_WORD_MASK(bits)) {
            return 0;
        }
    }

    return 1;
}

int slow_bitmap_equal(const unsigned long *bitmap1,
                      const unsigned long *bitmap2, long bits)
{
    long k, lim = bits/BITS_PER_LONG;

    for (k = 0; k < lim; ++k) {
        if (bitmap1[k] != bitmap2[k]) {
            return 0;
        }
    }

    if (bits % BITS_PER_LONG) {
        if ((bitmap1[k] ^ bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits)) {
            return 0;
        }
    }

    return 1;
}

void slow_bitmap_complement(unsigned long *dst, const unsigned long *src,
                            long bits)
{
    long k, lim = bits/BITS_PER_LONG;

    for (k = 0; k < lim; ++k) {
        dst[k] = ~src[k];
    }

    if (bits % BITS_PER_LONG) {
        dst[k] = ~src[k] & BITMAP_LAST_WORD_MASK(bits);
    }
}

int slow_bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
                    const unsigned long *bitmap2, long bits)
{
    long k;
    long nr = BITS_TO_LONGS(bits);
    unsigned long result = 0;

    for (k = 0; k < nr; k++) {
        result |= (dst[k] = bitmap1[k] & bitmap2[k]);
    }
    return result != 0;
}

void slow_bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
                    const unsigned long *bitmap2, long bits)
{
    long k;
    long nr = BITS_TO_LONGS(bits);

    for (k = 0; k < nr; k++) {
        dst[k] = bitmap1[k] | bitmap2[k];
    }
}

void slow_bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
                     const unsigned long *bitmap2, long bits)
{
    long k;
    long nr = BITS_TO_LONGS(bits);

    for (k = 0; k < nr; k++) {
        dst[k] = bitmap1[k] ^ bitmap2[k];
    }
}

int slow_bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
                       const unsigned long *bitmap2, long bits)
{
    long k;
    long nr = BITS_TO_LONGS(bits);
    unsigned long result = 0;

    for (k = 0; k < nr; k++) {
        result |= (dst[k] = bitmap1[k] & ~bitmap2[k]);
    }
    return result != 0;
}

void bitmap_set(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

    assert(start >= 0 && nr >= 0);

    while (nr - bits_to_set >= 0) {
        *p |= mask_to_set;
        nr -= bits_to_set;
        bits_to_set = BITS_PER_LONG;
        mask_to_set = ~0UL;
        p++;
    }
    if (nr) {
        mask_to_set &= BITMAP_LAST_WORD_MASK(size);
        *p |= mask_to_set;
    }
}

void bitmap_set_atomic(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

    assert(start >= 0 && nr >= 0);

    /* First word */
    if (nr - bits_to_set > 0) {
        atomic_or(p, mask_to_set);
        nr -= bits_to_set;
        bits_to_set = BITS_PER_LONG;
        mask_to_set = ~0UL;
        p++;
    }

    /* Full words */
    if (bits_to_set == BITS_PER_LONG) {
        while (nr >= BITS_PER_LONG) {
            *p = ~0UL;
            nr -= BITS_PER_LONG;
            p++;
        }
    }

    /* Last word */
    if (nr) {
        mask_to_set &= BITMAP_LAST_WORD_MASK(size);
        atomic_or(p, mask_to_set);
    } else {
        /* If we avoided the full barrier in atomic_or(), issue a
         * barrier to account for the assignments in the while loop.
         */
        smp_mb();
    }
}

void bitmap_clear(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

    assert(start >= 0 && nr >= 0);

    while (nr - bits_to_clear >= 0) {
        *p &= ~mask_to_clear;
        nr -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        mask_to_clear = ~0UL;
        p++;
    }
    if (nr) {
        mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
        *p &= ~mask_to_clear;
    }
}

bool bitmap_test_and_clear_atomic(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);
    unsigned long dirty = 0;
    unsigned long old_bits;

    assert(start >= 0 && nr >= 0);

    /* First word */
    if (nr - bits_to_clear > 0) {
        old_bits = atomic_fetch_and(p, ~mask_to_clear);
        dirty |= old_bits & mask_to_clear;
        nr -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        mask_to_clear = ~0UL;
        p++;
    }

    /* Full words */
    if (bits_to_clear == BITS_PER_LONG) {
        while (nr >= BITS_PER_LONG) {
            if (*p) {
                old_bits = atomic_xchg(p, 0);
                dirty |= old_bits;
            }
            nr -= BITS_PER_LONG;
            p++;
        }
    }

    /* Last word */
    if (nr) {
        mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
        old_bits = atomic_fetch_and(p, ~mask_to_clear);
        dirty |= old_bits & mask_to_clear;
    } else {
        if (!dirty) {
            smp_mb();
        }
    }

    return dirty != 0;
}

void bitmap_copy_and_clear_atomic(unsigned long *dst, unsigned long *src,
                                  long nr)
{
    while (nr > 0) {
        *dst = atomic_xchg(src, 0);
        dst++;
        src++;
        nr -= BITS_PER_LONG;
    }
}

#define ALIGN_MASK(x,mask)      (((x)+(mask))&~(mask))

/**
 * bitmap_find_next_zero_area - find a contiguous aligned zero area
 * @map: The address to base the search on
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @align_mask: Alignment mask for zero area
 *
 * The @align_mask should be one less than a power of 2; the effect is that
 * the bit offset of all zero areas this function finds is multiples of that
 * power of 2. A @align_mask of 0 means no alignment is required.
 */
unsigned long bitmap_find_next_zero_area(unsigned long *map,
                                         unsigned long size,
                                         unsigned long start,
                                         unsigned long nr,
                                         unsigned long align_mask)
{
    unsigned long index, end, i;
again:
    index = find_next_zero_bit(map, size, start);

    /* Align allocation */
    index = ALIGN_MASK(index, align_mask);

    end = index + nr;
    if (end > size) {
        return end;
    }
    i = find_next_bit(map, end, index);
    if (i < end) {
        start = i + 1;
        goto again;
    }
    return index;
}

int slow_bitmap_intersects(const unsigned long *bitmap1,
                           const unsigned long *bitmap2, long bits)
{
    long k, lim = bits/BITS_PER_LONG;

    for (k = 0; k < lim; ++k) {
        if (bitmap1[k] & bitmap2[k]) {
            return 1;
        }
    }

    if (bits % BITS_PER_LONG) {
        if ((bitmap1[k] & bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits)) {
            return 1;
        }
    }
    return 0;
}

long slow_bitmap_count_one(const unsigned long *bitmap, long nbits)
{
    long k, lim = nbits / BITS_PER_LONG, result = 0;

    for (k = 0; k < lim; k++) {
        result += ctpopl(bitmap[k]);
    }

    if (nbits % BITS_PER_LONG) {
        result += ctpopl(bitmap[k] & BITMAP_LAST_WORD_MASK(nbits));
    }

    return result;
}

static void bitmap_to_from_le(unsigned long *dst,
                              const unsigned long *src, long nbits)
{
    long len = BITS_TO_LONGS(nbits);

#ifdef HOST_WORDS_BIGENDIAN
    long index;

    for (index = 0; index < len; index++) {
# if HOST_LONG_BITS == 64
        dst[index] = bswap64(src[index]);
# else
        dst[index] = bswap32(src[index]);
# endif
    }
#else
    memcpy(dst, src, len * sizeof(unsigned long));
#endif
}

void bitmap_from_le(unsigned long *dst, const unsigned long *src,
                    long nbits)
{
    bitmap_to_from_le(dst, src, nbits);
}

void bitmap_to_le(unsigned long *dst, const unsigned long *src,
                  long nbits)
{
    bitmap_to_from_le(dst, src, nbits);
}

/*
 * Copy "src" bitmap with a positive offset and put it into the "dst"
 * bitmap.  The caller needs to make sure the bitmap size of "src"
 * is bigger than (shift + nbits).
 */
void bitmap_copy_with_src_offset(unsigned long *dst, const unsigned long *src,
                                 unsigned long shift, unsigned long nbits)
{
    unsigned long left_mask, right_mask, last_mask;

    /* Proper shift src pointer to the first word to copy from */
    src += BIT_WORD(shift);
    shift %= BITS_PER_LONG;

    if (!shift) {
        /* Fast path */
        bitmap_copy(dst, src, nbits);
        return;
    }

    right_mask = (1ul << shift) - 1;
    left_mask = ~right_mask;

    while (nbits >= BITS_PER_LONG) {
        *dst = (*src & left_mask) >> shift;
        *dst |= (src[1] & right_mask) << (BITS_PER_LONG - shift);
        dst++;
        src++;
        nbits -= BITS_PER_LONG;
    }

    if (nbits > BITS_PER_LONG - shift) {
        *dst = (*src & left_mask) >> shift;
        nbits -= BITS_PER_LONG - shift;
        last_mask = (1ul << nbits) - 1;
        *dst |= (src[1] & last_mask) << (BITS_PER_LONG - shift);
    } else if (nbits) {
        last_mask = (1ul << nbits) - 1;
        *dst = (*src >> shift) & last_mask;
    }
}

/*
 * Copy "src" bitmap into the "dst" bitmap with an offset in the
 * "dst".  The caller needs to make sure the bitmap size of "dst" is
 * bigger than (shift + nbits).
 */
void bitmap_copy_with_dst_offset(unsigned long *dst, const unsigned long *src,
                                 unsigned long shift, unsigned long nbits)
{
    unsigned long left_mask, right_mask, last_mask;

    /* Proper shift dst pointer to the first word to copy from */
    dst += BIT_WORD(shift);
    shift %= BITS_PER_LONG;

    if (!shift) {
        /* Fast path */
        bitmap_copy(dst, src, nbits);
        return;
    }

    right_mask = (1ul << (BITS_PER_LONG - shift)) - 1;
    left_mask = ~right_mask;

    *dst &= (1ul << shift) - 1;
    while (nbits >= BITS_PER_LONG) {
        *dst |= (*src & right_mask) << shift;
        dst[1] = (*src & left_mask) >> (BITS_PER_LONG - shift);
        dst++;
        src++;
        nbits -= BITS_PER_LONG;
    }

    if (nbits > BITS_PER_LONG - shift) {
        *dst |= (*src & right_mask) << shift;
        nbits -= BITS_PER_LONG - shift;
        last_mask = ((1ul << nbits) - 1) << (BITS_PER_LONG - shift);
        dst[1] = (*src & last_mask) >> (BITS_PER_LONG - shift);
    } else if (nbits) {
        last_mask = (1ul << nbits) - 1;
        *dst |= (*src & last_mask) << shift;
    }
}
