/*
 * Bitmap Module
 *
 * Copyright (C) 2010 Corentin Chary <corentin.chary@gmail.com>
 *
 * Mostly inspired by (stolen from) linux/bitmap.h and linux/bitops.h
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef BITMAP_H
#define BITMAP_H


#include "qemu/bitops.h"

/*
 * The available bitmap operations and their rough meaning in the
 * case that the bitmap is a single unsigned long are thus:
 *
 * Note that nbits should be always a compile time evaluable constant.
 * Otherwise many inlines will generate horrible code.
 *
 * bitmap_zero(dst, nbits)                      *dst = 0UL
 * bitmap_fill(dst, nbits)                      *dst = ~0UL
 * bitmap_copy(dst, src, nbits)                 *dst = *src
 * bitmap_and(dst, src1, src2, nbits)           *dst = *src1 & *src2
 * bitmap_or(dst, src1, src2, nbits)            *dst = *src1 | *src2
 * bitmap_xor(dst, src1, src2, nbits)           *dst = *src1 ^ *src2
 * bitmap_andnot(dst, src1, src2, nbits)        *dst = *src1 & ~(*src2)
 * bitmap_complement(dst, src, nbits)           *dst = ~(*src)
 * bitmap_equal(src1, src2, nbits)              Are *src1 and *src2 equal?
 * bitmap_intersects(src1, src2, nbits)         Do *src1 and *src2 overlap?
 * bitmap_empty(src, nbits)                     Are all bits zero in *src?
 * bitmap_full(src, nbits)                      Are all bits set in *src?
 * bitmap_set(dst, pos, nbits)                  Set specified bit area
 * bitmap_set_atomic(dst, pos, nbits)           Set specified bit area with atomic ops
 * bitmap_clear(dst, pos, nbits)                Clear specified bit area
 * bitmap_test_and_clear_atomic(dst, pos, nbits)    Test and clear area
 * bitmap_find_next_zero_area(buf, len, pos, n, mask)  Find bit free area
 * bitmap_to_le(dst, src, nbits)      Convert bitmap to little endian
 * bitmap_from_le(dst, src, nbits)    Convert bitmap from little endian
 * bitmap_copy_with_src_offset(dst, src, offset, nbits)
 *                                    *dst = *src (with an offset into src)
 * bitmap_copy_with_dst_offset(dst, src, offset, nbits)
 *                                    *dst = *src (with an offset into dst)
 */

/*
 * Also the following operations apply to bitmaps.
 *
 * set_bit(bit, addr)               *addr |= bit
 * clear_bit(bit, addr)             *addr &= ~bit
 * change_bit(bit, addr)            *addr ^= bit
 * test_bit(bit, addr)              Is bit set in *addr?
 * test_and_set_bit(bit, addr)      Set bit and return old value
 * test_and_clear_bit(bit, addr)    Clear bit and return old value
 * test_and_change_bit(bit, addr)   Change bit and return old value
 * find_first_zero_bit(addr, nbits) Position first zero bit in *addr
 * find_first_bit(addr, nbits)      Position first set bit in *addr
 * find_next_zero_bit(addr, nbits, bit) Position next zero bit in *addr >= bit
 * find_next_bit(addr, nbits, bit)  Position next set bit in *addr >= bit
 */

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

#define DECLARE_BITMAP(name,bits)                  \
        unsigned long name[BITS_TO_LONGS(bits)]

/*
 * This is for use with the bit32 versions of set_bit() etc;
 * we don't currently support the full range of bitmap operations
 * on bitmaps backed by an array of uint32_t.
 */
#define DECLARE_BITMAP32(name, bits)            \
        uint32_t name[BITS_TO_U32S(bits)]

#define small_nbits(nbits)                      \
        ((nbits) <= BITS_PER_LONG)

int slow_bitmap_empty(const unsigned long *bitmap, long bits);
int slow_bitmap_full(const unsigned long *bitmap, long bits);
int slow_bitmap_equal(const unsigned long *bitmap1,
                      const unsigned long *bitmap2, long bits);
void slow_bitmap_complement(unsigned long *dst, const unsigned long *src,
                            long bits);
int slow_bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
                    const unsigned long *bitmap2, long bits);
void slow_bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
                    const unsigned long *bitmap2, long bits);
void slow_bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
                     const unsigned long *bitmap2, long bits);
int slow_bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
                       const unsigned long *bitmap2, long bits);
int slow_bitmap_intersects(const unsigned long *bitmap1,
                           const unsigned long *bitmap2, long bits);
long slow_bitmap_count_one(const unsigned long *bitmap, long nbits);

static inline unsigned long *bitmap_try_new(long nbits)
{
    long nelem = BITS_TO_LONGS(nbits);
    return g_try_new0(unsigned long, nelem);
}

static inline unsigned long *bitmap_new(long nbits)
{
    long nelem = BITS_TO_LONGS(nbits);
    return g_new0(unsigned long, nelem);
}

static inline void bitmap_zero(unsigned long *dst, long nbits)
{
    if (small_nbits(nbits)) {
        *dst = 0UL;
    } else {
        long len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
        memset(dst, 0, len);
    }
}

static inline void bitmap_fill(unsigned long *dst, long nbits)
{
    size_t nlongs = BITS_TO_LONGS(nbits);
    if (!small_nbits(nbits)) {
        long len = (nlongs - 1) * sizeof(unsigned long);
        memset(dst, 0xff,  len);
    }
    dst[nlongs - 1] = BITMAP_LAST_WORD_MASK(nbits);
}

static inline void bitmap_copy(unsigned long *dst, const unsigned long *src,
                               long nbits)
{
    if (small_nbits(nbits)) {
        *dst = *src;
    } else {
        long len = BITS_TO_LONGS(nbits) * sizeof(unsigned long);
        memcpy(dst, src, len);
    }
}

static inline int bitmap_and(unsigned long *dst, const unsigned long *src1,
                             const unsigned long *src2, long nbits)
{
    if (small_nbits(nbits)) {
        return (*dst = *src1 & *src2) != 0;
    }
    return slow_bitmap_and(dst, src1, src2, nbits);
}

static inline void bitmap_or(unsigned long *dst, const unsigned long *src1,
                             const unsigned long *src2, long nbits)
{
    if (small_nbits(nbits)) {
        *dst = *src1 | *src2;
    } else {
        slow_bitmap_or(dst, src1, src2, nbits);
    }
}

static inline void bitmap_xor(unsigned long *dst, const unsigned long *src1,
                              const unsigned long *src2, long nbits)
{
    if (small_nbits(nbits)) {
        *dst = *src1 ^ *src2;
    } else {
        slow_bitmap_xor(dst, src1, src2, nbits);
    }
}

static inline int bitmap_andnot(unsigned long *dst, const unsigned long *src1,
                                const unsigned long *src2, long nbits)
{
    if (small_nbits(nbits)) {
        return (*dst = *src1 & ~(*src2)) != 0;
    }
    return slow_bitmap_andnot(dst, src1, src2, nbits);
}

static inline void bitmap_complement(unsigned long *dst,
                                     const unsigned long *src,
                                     long nbits)
{
    if (small_nbits(nbits)) {
        *dst = ~(*src) & BITMAP_LAST_WORD_MASK(nbits);
    } else {
        slow_bitmap_complement(dst, src, nbits);
    }
}

static inline int bitmap_equal(const unsigned long *src1,
                               const unsigned long *src2, long nbits)
{
    if (small_nbits(nbits)) {
        return ! ((*src1 ^ *src2) & BITMAP_LAST_WORD_MASK(nbits));
    } else {
        return slow_bitmap_equal(src1, src2, nbits);
    }
}

static inline int bitmap_empty(const unsigned long *src, long nbits)
{
    if (small_nbits(nbits)) {
        return ! (*src & BITMAP_LAST_WORD_MASK(nbits));
    } else {
        return slow_bitmap_empty(src, nbits);
    }
}

static inline int bitmap_full(const unsigned long *src, long nbits)
{
    if (small_nbits(nbits)) {
        return ! (~(*src) & BITMAP_LAST_WORD_MASK(nbits));
    } else {
        return slow_bitmap_full(src, nbits);
    }
}

static inline int bitmap_intersects(const unsigned long *src1,
                                    const unsigned long *src2, long nbits)
{
    if (small_nbits(nbits)) {
        return ((*src1 & *src2) & BITMAP_LAST_WORD_MASK(nbits)) != 0;
    } else {
        return slow_bitmap_intersects(src1, src2, nbits);
    }
}

static inline long bitmap_count_one(const unsigned long *bitmap, long nbits)
{
    if (unlikely(!nbits)) {
        return 0;
    }

    if (small_nbits(nbits)) {
        return ctpopl(*bitmap & BITMAP_LAST_WORD_MASK(nbits));
    } else {
        return slow_bitmap_count_one(bitmap, nbits);
    }
}

static inline long bitmap_count_one_with_offset(const unsigned long *bitmap,
                                                long offset, long nbits)
{
    long aligned_offset = QEMU_ALIGN_DOWN(offset, BITS_PER_LONG);
    long redundant_bits = offset - aligned_offset;
    long bits_to_count = nbits + redundant_bits;
    const unsigned long *bitmap_start = bitmap +
                                        aligned_offset / BITS_PER_LONG;

    return bitmap_count_one(bitmap_start, bits_to_count) -
           bitmap_count_one(bitmap_start, redundant_bits);
}

void bitmap_set(unsigned long *map, long i, long len);
void bitmap_set_atomic(unsigned long *map, long i, long len);
void bitmap_clear(unsigned long *map, long start, long nr);
bool bitmap_test_and_clear_atomic(unsigned long *map, long start, long nr);
bool bitmap_test_and_clear(unsigned long *map, long start, long nr);
void bitmap_copy_and_clear_atomic(unsigned long *dst, unsigned long *src,
                                  long nr);
unsigned long bitmap_find_next_zero_area(unsigned long *map,
                                         unsigned long size,
                                         unsigned long start,
                                         unsigned long nr,
                                         unsigned long align_mask);

static inline unsigned long *bitmap_zero_extend(unsigned long *old,
                                                long old_nbits, long new_nbits)
{
    long new_nelem = BITS_TO_LONGS(new_nbits);
    unsigned long *ptr = g_renew(unsigned long, old, new_nelem);
    bitmap_clear(ptr, old_nbits, new_nbits - old_nbits);
    return ptr;
}

void bitmap_to_le(unsigned long *dst, const unsigned long *src,
                  long nbits);
void bitmap_from_le(unsigned long *dst, const unsigned long *src,
                    long nbits);

void bitmap_copy_with_src_offset(unsigned long *dst, const unsigned long *src,
                                 unsigned long offset, unsigned long nbits);
void bitmap_copy_with_dst_offset(unsigned long *dst, const unsigned long *src,
                                 unsigned long shift, unsigned long nbits);

#endif /* BITMAP_H */
