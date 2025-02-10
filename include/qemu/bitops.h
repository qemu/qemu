/*
 * Bitops Module
 *
 * Copyright (C) 2010 Corentin Chary <corentin.chary@gmail.com>
 *
 * Mostly inspired by (stolen from) linux/bitmap.h and linux/bitops.h
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef BITOPS_H
#define BITOPS_H


#include "host-utils.h"
#include "atomic.h"

#define BITS_PER_BYTE           CHAR_BIT
#define BITS_PER_LONG           (sizeof (unsigned long) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))
#define BITS_TO_U32S(nr)        DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(uint32_t))

#define BIT(nr)                 (1UL << (nr))
#define BIT_ULL(nr)             (1ULL << (nr))

#define MAKE_64BIT_MASK(shift, length) \
    (((~0ULL) >> (64 - (length))) << (shift))

/**
 * DOC: Functions operating on arrays of bits
 *
 * We provide a set of functions which work on arbitrary-length arrays of
 * bits. These come in several flavours which vary in what the type of the
 * underlying storage for the bits is:
 *
 * - Bits stored in an array of 'unsigned long': set_bit(), clear_bit(), etc
 * - Bits stored in an array of 'uint32_t': set_bit32(), clear_bit32(), etc
 *
 * Because the 'unsigned long' type has a size which varies between
 * host systems, the versions using 'uint32_t' are often preferable.
 * This is particularly the case in a device model where there may
 * be some guest-visible register view of the bit array.
 *
 * We do not currently implement uint32_t versions of find_last_bit(),
 * find_next_bit(), find_next_zero_bit(), find_first_bit() or
 * find_first_zero_bit(), because we haven't yet needed them. If you
 * need them you should implement them similarly to the 'unsigned long'
 * versions.
 *
 * You can declare a bitmap to be used with these functions via the
 * DECLARE_BITMAP and DECLARE_BITMAP32 macros in bitmap.h.
 */

/**
 * DOC:  'unsigned long' bit array APIs
 */

#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)

/**
 * set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
static inline void set_bit(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);

    *p  |= mask;
}

/**
 * set_bit_atomic - Set a bit in memory atomically
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
static inline void set_bit_atomic(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);

    qatomic_or(p, mask);
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
static inline void clear_bit(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);

    *p &= ~mask;
}

/**
 * clear_bit_atomic - Clears a bit in memory atomically
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
static inline void clear_bit_atomic(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);

    return qatomic_and(p, ~mask);
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 */
static inline void change_bit(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);

    *p ^= mask;
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 */
static inline int test_and_set_bit(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);
    unsigned long old = *p;

    *p = old | mask;
    return (old & mask) != 0;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 */
static inline int test_and_clear_bit(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);
    unsigned long old = *p;

    *p = old & ~mask;
    return (old & mask) != 0;
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 */
static inline int test_and_change_bit(long nr, unsigned long *addr)
{
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = addr + BIT_WORD(nr);
    unsigned long old = *p;

    *p = old ^ mask;
    return (old & mask) != 0;
}

/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static inline int test_bit(long nr, const unsigned long *addr)
{
    return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

/**
 * find_last_bit - find the last set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the last set bit,
 * or @size if there is no set bit in the bitmap.
 */
unsigned long find_last_bit(const unsigned long *addr,
                            unsigned long size);

/**
 * find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 *
 * Returns the bit number of the next set bit,
 * or @size if there are no further set bits in the bitmap.
 */
unsigned long find_next_bit(const unsigned long *addr,
                            unsigned long size,
                            unsigned long offset);

/**
 * find_next_zero_bit - find the next cleared bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 *
 * Returns the bit number of the next cleared bit,
 * or @size if there are no further clear bits in the bitmap.
 */

unsigned long find_next_zero_bit(const unsigned long *addr,
                                 unsigned long size,
                                 unsigned long offset);

/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the first set bit,
 * or @size if there is no set bit in the bitmap.
 */
static inline unsigned long find_first_bit(const unsigned long *addr,
                                           unsigned long size)
{
    unsigned long result, tmp;

    for (result = 0; result < size; result += BITS_PER_LONG) {
        tmp = *addr++;
        if (tmp) {
            result += ctzl(tmp);
            return result < size ? result : size;
        }
    }
    /* Not found */
    return size;
}

/**
 * find_first_zero_bit - find the first cleared bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the first cleared bit,
 * or @size if there is no clear bit in the bitmap.
 */
static inline unsigned long find_first_zero_bit(const unsigned long *addr,
                                                unsigned long size)
{
    return find_next_zero_bit(addr, size, 0);
}

/**
 * DOC:  'uint32_t' bit array APIs
 */

#define BIT32_MASK(nr)            (1UL << ((nr) % 32))
#define BIT32_WORD(nr)            ((nr) / 32)

/**
 * set_bit32 - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
static inline void set_bit32(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);

    *p  |= mask;
}

/**
 * set_bit32_atomic - Set a bit in memory atomically
 * @nr: the bit to set
 * @addr: the address to start counting from
 */
static inline void set_bit32_atomic(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);

    qatomic_or(p, mask);
}

/**
 * clear_bit32 - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
static inline void clear_bit32(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);

    *p &= ~mask;
}

/**
 * clear_bit32_atomic - Clears a bit in memory atomically
 * @nr: Bit to clear
 * @addr: Address to start counting from
 */
static inline void clear_bit32_atomic(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);

    return qatomic_and(p, ~mask);
}

/**
 * change_bit32 - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 */
static inline void change_bit32(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);

    *p ^= mask;
}

/**
 * test_and_set_bit32 - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 */
static inline int test_and_set_bit32(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);
    uint32_t old = *p;

    *p = old | mask;
    return (old & mask) != 0;
}

/**
 * test_and_clear_bit32 - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 */
static inline int test_and_clear_bit32(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);
    uint32_t old = *p;

    *p = old & ~mask;
    return (old & mask) != 0;
}

/**
 * test_and_change_bit32 - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 */
static inline int test_and_change_bit32(long nr, uint32_t *addr)
{
    uint32_t mask = BIT32_MASK(nr);
    uint32_t *p = addr + BIT32_WORD(nr);
    uint32_t old = *p;

    *p = old ^ mask;
    return (old & mask) != 0;
}

/**
 * test_bit32 - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static inline int test_bit32(long nr, const uint32_t *addr)
{
    return 1U & (addr[BIT32_WORD(nr)] >> (nr & 31));
}

/**
 * DOC: Miscellaneous bit operations on single values
 *
 * These functions are a collection of useful operations
 * (rotations, bit extract, bit deposit, etc) on single
 * integer values.
 */

/**
 * rol8 - rotate an 8-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint8_t rol8(uint8_t word, unsigned int shift)
{
    return (word << (shift & 7)) | (word >> (-shift & 7));
}

/**
 * ror8 - rotate an 8-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint8_t ror8(uint8_t word, unsigned int shift)
{
    return (word >> (shift & 7)) | (word << (-shift & 7));
}

/**
 * rol16 - rotate a 16-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint16_t rol16(uint16_t word, unsigned int shift)
{
    return (word << (shift & 15)) | (word >> (-shift & 15));
}

/**
 * ror16 - rotate a 16-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint16_t ror16(uint16_t word, unsigned int shift)
{
    return (word >> (shift & 15)) | (word << (-shift & 15));
}

/**
 * rol32 - rotate a 32-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint32_t rol32(uint32_t word, unsigned int shift)
{
    return (word << (shift & 31)) | (word >> (-shift & 31));
}

/**
 * ror32 - rotate a 32-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint32_t ror32(uint32_t word, unsigned int shift)
{
    return (word >> (shift & 31)) | (word << (-shift & 31));
}

/**
 * rol64 - rotate a 64-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint64_t rol64(uint64_t word, unsigned int shift)
{
    return (word << (shift & 63)) | (word >> (-shift & 63));
}

/**
 * ror64 - rotate a 64-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline uint64_t ror64(uint64_t word, unsigned int shift)
{
    return (word >> (shift & 63)) | (word << (-shift & 63));
}

/**
 * hswap32 - swap 16-bit halfwords within a 32-bit value
 * @h: value to swap
 */
static inline uint32_t hswap32(uint32_t h)
{
    return rol32(h, 16);
}

/**
 * hswap64 - swap 16-bit halfwords within a 64-bit value
 * @h: value to swap
 */
static inline uint64_t hswap64(uint64_t h)
{
    uint64_t m = 0x0000ffff0000ffffull;
    h = rol64(h, 32);
    return ((h & m) << 16) | ((h >> 16) & m);
}

/**
 * wswap64 - swap 32-bit words within a 64-bit value
 * @h: value to swap
 */
static inline uint64_t wswap64(uint64_t h)
{
    return rol64(h, 32);
}

/**
 * extract32:
 * @value: the value to extract the bit field from
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 *
 * Extract from the 32 bit input @value the bit field specified by the
 * @start and @length parameters, and return it. The bit field must
 * lie entirely within the 32 bit word. It is valid to request that
 * all 32 bits are returned (ie @length 32 and @start 0).
 *
 * Returns: the value of the bit field extracted from the input value.
 */
static inline uint32_t extract32(uint32_t value, int start, int length)
{
    assert(start >= 0 && length > 0 && length <= 32 - start);
    return (value >> start) & (~0U >> (32 - length));
}

/**
 * extract8:
 * @value: the value to extract the bit field from
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 *
 * Extract from the 8 bit input @value the bit field specified by the
 * @start and @length parameters, and return it. The bit field must
 * lie entirely within the 8 bit word. It is valid to request that
 * all 8 bits are returned (ie @length 8 and @start 0).
 *
 * Returns: the value of the bit field extracted from the input value.
 */
static inline uint8_t extract8(uint8_t value, int start, int length)
{
    assert(start >= 0 && length > 0 && length <= 8 - start);
    return extract32(value, start, length);
}

/**
 * extract16:
 * @value: the value to extract the bit field from
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 *
 * Extract from the 16 bit input @value the bit field specified by the
 * @start and @length parameters, and return it. The bit field must
 * lie entirely within the 16 bit word. It is valid to request that
 * all 16 bits are returned (ie @length 16 and @start 0).
 *
 * Returns: the value of the bit field extracted from the input value.
 */
static inline uint16_t extract16(uint16_t value, int start, int length)
{
    assert(start >= 0 && length > 0 && length <= 16 - start);
    return extract32(value, start, length);
}

/**
 * extract64:
 * @value: the value to extract the bit field from
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 *
 * Extract from the 64 bit input @value the bit field specified by the
 * @start and @length parameters, and return it. The bit field must
 * lie entirely within the 64 bit word. It is valid to request that
 * all 64 bits are returned (ie @length 64 and @start 0).
 *
 * Returns: the value of the bit field extracted from the input value.
 */
static inline uint64_t extract64(uint64_t value, int start, int length)
{
    assert(start >= 0 && length > 0 && length <= 64 - start);
    return (value >> start) & (~0ULL >> (64 - length));
}

/**
 * sextract32:
 * @value: the value to extract the bit field from
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 *
 * Extract from the 32 bit input @value the bit field specified by the
 * @start and @length parameters, and return it, sign extended to
 * an int32_t (ie with the most significant bit of the field propagated
 * to all the upper bits of the return value). The bit field must lie
 * entirely within the 32 bit word. It is valid to request that
 * all 32 bits are returned (ie @length 32 and @start 0).
 *
 * Returns: the sign extended value of the bit field extracted from the
 * input value.
 */
static inline int32_t sextract32(uint32_t value, int start, int length)
{
    assert(start >= 0 && length > 0 && length <= 32 - start);
    /* Note that this implementation relies on right shift of signed
     * integers being an arithmetic shift.
     */
    return ((int32_t)(value << (32 - length - start))) >> (32 - length);
}

/**
 * sextract64:
 * @value: the value to extract the bit field from
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 *
 * Extract from the 64 bit input @value the bit field specified by the
 * @start and @length parameters, and return it, sign extended to
 * an int64_t (ie with the most significant bit of the field propagated
 * to all the upper bits of the return value). The bit field must lie
 * entirely within the 64 bit word. It is valid to request that
 * all 64 bits are returned (ie @length 64 and @start 0).
 *
 * Returns: the sign extended value of the bit field extracted from the
 * input value.
 */
static inline int64_t sextract64(uint64_t value, int start, int length)
{
    assert(start >= 0 && length > 0 && length <= 64 - start);
    /* Note that this implementation relies on right shift of signed
     * integers being an arithmetic shift.
     */
    return ((int64_t)(value << (64 - length - start))) >> (64 - length);
}

/**
 * deposit32:
 * @value: initial value to insert bit field into
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 * @fieldval: the value to insert into the bit field
 *
 * Deposit @fieldval into the 32 bit @value at the bit field specified
 * by the @start and @length parameters, and return the modified
 * @value. Bits of @value outside the bit field are not modified.
 * Bits of @fieldval above the least significant @length bits are
 * ignored. The bit field must lie entirely within the 32 bit word.
 * It is valid to request that all 32 bits are modified (ie @length
 * 32 and @start 0).
 *
 * Returns: the modified @value.
 */
static inline uint32_t deposit32(uint32_t value, int start, int length,
                                 uint32_t fieldval)
{
    uint32_t mask;
    assert(start >= 0 && length > 0 && length <= 32 - start);
    mask = (~0U >> (32 - length)) << start;
    return (value & ~mask) | ((fieldval << start) & mask);
}

/**
 * deposit64:
 * @value: initial value to insert bit field into
 * @start: the lowest bit in the bit field (numbered from 0)
 * @length: the length of the bit field
 * @fieldval: the value to insert into the bit field
 *
 * Deposit @fieldval into the 64 bit @value at the bit field specified
 * by the @start and @length parameters, and return the modified
 * @value. Bits of @value outside the bit field are not modified.
 * Bits of @fieldval above the least significant @length bits are
 * ignored. The bit field must lie entirely within the 64 bit word.
 * It is valid to request that all 64 bits are modified (ie @length
 * 64 and @start 0).
 *
 * Returns: the modified @value.
 */
static inline uint64_t deposit64(uint64_t value, int start, int length,
                                 uint64_t fieldval)
{
    uint64_t mask;
    assert(start >= 0 && length > 0 && length <= 64 - start);
    mask = (~0ULL >> (64 - length)) << start;
    return (value & ~mask) | ((fieldval << start) & mask);
}

/**
 * half_shuffle32:
 * @x: 32-bit value (of which only the bottom 16 bits are of interest)
 *
 * Given an input value::
 *
 *   xxxx xxxx xxxx xxxx ABCD EFGH IJKL MNOP
 *
 * return the value where the bottom 16 bits are spread out into
 * the odd bits in the word, and the even bits are zeroed::
 *
 *   0A0B 0C0D 0E0F 0G0H 0I0J 0K0L 0M0N 0O0P
 *
 * Any bits set in the top half of the input are ignored.
 *
 * Returns: the shuffled bits.
 */
static inline uint32_t half_shuffle32(uint32_t x)
{
    /* This algorithm is from _Hacker's Delight_ section 7-2 "Shuffling Bits".
     * It ignores any bits set in the top half of the input.
     */
    x = ((x & 0xFF00) << 8) | (x & 0x00FF);
    x = ((x << 4) | x) & 0x0F0F0F0F;
    x = ((x << 2) | x) & 0x33333333;
    x = ((x << 1) | x) & 0x55555555;
    return x;
}

/**
 * half_shuffle64:
 * @x: 64-bit value (of which only the bottom 32 bits are of interest)
 *
 * Given an input value::
 *
 *   xxxx xxxx xxxx .... xxxx xxxx ABCD EFGH IJKL MNOP QRST UVWX YZab cdef
 *
 * return the value where the bottom 32 bits are spread out into
 * the odd bits in the word, and the even bits are zeroed::
 *
 *   0A0B 0C0D 0E0F 0G0H 0I0J 0K0L 0M0N .... 0U0V 0W0X 0Y0Z 0a0b 0c0d 0e0f
 *
 * Any bits set in the top half of the input are ignored.
 *
 * Returns: the shuffled bits.
 */
static inline uint64_t half_shuffle64(uint64_t x)
{
    /* This algorithm is from _Hacker's Delight_ section 7-2 "Shuffling Bits".
     * It ignores any bits set in the top half of the input.
     */
    x = ((x & 0xFFFF0000ULL) << 16) | (x & 0xFFFF);
    x = ((x << 8) | x) & 0x00FF00FF00FF00FFULL;
    x = ((x << 4) | x) & 0x0F0F0F0F0F0F0F0FULL;
    x = ((x << 2) | x) & 0x3333333333333333ULL;
    x = ((x << 1) | x) & 0x5555555555555555ULL;
    return x;
}

/**
 * half_unshuffle32:
 * @x: 32-bit value (of which only the odd bits are of interest)
 *
 * Given an input value::
 *
 *   xAxB xCxD xExF xGxH xIxJ xKxL xMxN xOxP
 *
 * return the value where all the odd bits are compressed down
 * into the low half of the word, and the high half is zeroed::
 *
 *   0000 0000 0000 0000 ABCD EFGH IJKL MNOP
 *
 * Any even bits set in the input are ignored.
 *
 * Returns: the unshuffled bits.
 */
static inline uint32_t half_unshuffle32(uint32_t x)
{
    /* This algorithm is from _Hacker's Delight_ section 7-2 "Shuffling Bits".
     * where it is called an inverse half shuffle.
     */
    x &= 0x55555555;
    x = ((x >> 1) | x) & 0x33333333;
    x = ((x >> 2) | x) & 0x0F0F0F0F;
    x = ((x >> 4) | x) & 0x00FF00FF;
    x = ((x >> 8) | x) & 0x0000FFFF;
    return x;
}

/**
 * half_unshuffle64:
 * @x: 64-bit value (of which only the odd bits are of interest)
 *
 * Given an input value::
 *
 *   xAxB xCxD xExF xGxH xIxJ xKxL xMxN .... xUxV xWxX xYxZ xaxb xcxd xexf
 *
 * return the value where all the odd bits are compressed down
 * into the low half of the word, and the high half is zeroed::
 *
 *   0000 0000 0000 .... 0000 0000 ABCD EFGH IJKL MNOP QRST UVWX YZab cdef
 *
 * Any even bits set in the input are ignored.
 *
 * Returns: the unshuffled bits.
 */
static inline uint64_t half_unshuffle64(uint64_t x)
{
    /* This algorithm is from _Hacker's Delight_ section 7-2 "Shuffling Bits".
     * where it is called an inverse half shuffle.
     */
    x &= 0x5555555555555555ULL;
    x = ((x >> 1) | x) & 0x3333333333333333ULL;
    x = ((x >> 2) | x) & 0x0F0F0F0F0F0F0F0FULL;
    x = ((x >> 4) | x) & 0x00FF00FF00FF00FFULL;
    x = ((x >> 8) | x) & 0x0000FFFF0000FFFFULL;
    x = ((x >> 16) | x) & 0x00000000FFFFFFFFULL;
    return x;
}

#endif
