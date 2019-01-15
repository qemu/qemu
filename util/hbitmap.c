/*
 * Hierarchical Bitmap Data Type
 *
 * Copyright Red Hat, Inc., 2012
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/hbitmap.h"
#include "qemu/host-utils.h"
#include "trace.h"
#include "crypto/hash.h"

/* HBitmaps provides an array of bits.  The bits are stored as usual in an
 * array of unsigned longs, but HBitmap is also optimized to provide fast
 * iteration over set bits; going from one bit to the next is O(logB n)
 * worst case, with B = sizeof(long) * CHAR_BIT: the result is low enough
 * that the number of levels is in fact fixed.
 *
 * In order to do this, it stacks multiple bitmaps with progressively coarser
 * granularity; in all levels except the last, bit N is set iff the N-th
 * unsigned long is nonzero in the immediately next level.  When iteration
 * completes on the last level it can examine the 2nd-last level to quickly
 * skip entire words, and even do so recursively to skip blocks of 64 words or
 * powers thereof (32 on 32-bit machines).
 *
 * Given an index in the bitmap, it can be split in group of bits like
 * this (for the 64-bit case):
 *
 *   bits 0-57 => word in the last bitmap     | bits 58-63 => bit in the word
 *   bits 0-51 => word in the 2nd-last bitmap | bits 52-57 => bit in the word
 *   bits 0-45 => word in the 3rd-last bitmap | bits 46-51 => bit in the word
 *
 * So it is easy to move up simply by shifting the index right by
 * log2(BITS_PER_LONG) bits.  To move down, you shift the index left
 * similarly, and add the word index within the group.  Iteration uses
 * ffs (find first set bit) to find the next word to examine; this
 * operation can be done in constant time in most current architectures.
 *
 * Setting or clearing a range of m bits on all levels, the work to perform
 * is O(m + m/W + m/W^2 + ...), which is O(m) like on a regular bitmap.
 *
 * When iterating on a bitmap, each bit (on any level) is only visited
 * once.  Hence, The total cost of visiting a bitmap with m bits in it is
 * the number of bits that are set in all bitmaps.  Unless the bitmap is
 * extremely sparse, this is also O(m + m/W + m/W^2 + ...), so the amortized
 * cost of advancing from one bit to the next is usually constant (worst case
 * O(logB n) as in the non-amortized complexity).
 */

struct HBitmap {
    /* Size of the bitmap, as requested in hbitmap_alloc. */
    uint64_t orig_size;

    /* Number of total bits in the bottom level.  */
    uint64_t size;

    /* Number of set bits in the bottom level.  */
    uint64_t count;

    /* A scaling factor.  Given a granularity of G, each bit in the bitmap will
     * will actually represent a group of 2^G elements.  Each operation on a
     * range of bits first rounds the bits to determine which group they land
     * in, and then affect the entire page; iteration will only visit the first
     * bit of each group.  Here is an example of operations in a size-16,
     * granularity-1 HBitmap:
     *
     *    initial state            00000000
     *    set(start=0, count=9)    11111000 (iter: 0, 2, 4, 6, 8)
     *    reset(start=1, count=3)  00111000 (iter: 4, 6, 8)
     *    set(start=9, count=2)    00111100 (iter: 4, 6, 8, 10)
     *    reset(start=5, count=5)  00000000
     *
     * From an implementation point of view, when setting or resetting bits,
     * the bitmap will scale bit numbers right by this amount of bits.  When
     * iterating, the bitmap will scale bit numbers left by this amount of
     * bits.
     */
    int granularity;

    /* A meta dirty bitmap to track the dirtiness of bits in this HBitmap. */
    HBitmap *meta;

    /* A number of progressively less coarse bitmaps (i.e. level 0 is the
     * coarsest).  Each bit in level N represents a word in level N+1 that
     * has a set bit, except the last level where each bit represents the
     * actual bitmap.
     *
     * Note that all bitmaps have the same number of levels.  Even a 1-bit
     * bitmap will still allocate HBITMAP_LEVELS arrays.
     */
    unsigned long *levels[HBITMAP_LEVELS];

    /* The length of each levels[] array. */
    uint64_t sizes[HBITMAP_LEVELS];
};

/* Advance hbi to the next nonzero word and return it.  hbi->pos
 * is updated.  Returns zero if we reach the end of the bitmap.
 */
unsigned long hbitmap_iter_skip_words(HBitmapIter *hbi)
{
    size_t pos = hbi->pos;
    const HBitmap *hb = hbi->hb;
    unsigned i = HBITMAP_LEVELS - 1;

    unsigned long cur;
    do {
        i--;
        pos >>= BITS_PER_LEVEL;
        cur = hbi->cur[i] & hb->levels[i][pos];
    } while (cur == 0);

    /* Check for end of iteration.  We always use fewer than BITS_PER_LONG
     * bits in the level 0 bitmap; thus we can repurpose the most significant
     * bit as a sentinel.  The sentinel is set in hbitmap_alloc and ensures
     * that the above loop ends even without an explicit check on i.
     */

    if (i == 0 && cur == (1UL << (BITS_PER_LONG - 1))) {
        return 0;
    }
    for (; i < HBITMAP_LEVELS - 1; i++) {
        /* Shift back pos to the left, matching the right shifts above.
         * The index of this word's least significant set bit provides
         * the low-order bits.
         */
        assert(cur);
        pos = (pos << BITS_PER_LEVEL) + ctzl(cur);
        hbi->cur[i] = cur & (cur - 1);

        /* Set up next level for iteration.  */
        cur = hb->levels[i + 1][pos];
    }

    hbi->pos = pos;
    trace_hbitmap_iter_skip_words(hbi->hb, hbi, pos, cur);

    assert(cur);
    return cur;
}

int64_t hbitmap_iter_next(HBitmapIter *hbi)
{
    unsigned long cur = hbi->cur[HBITMAP_LEVELS - 1] &
            hbi->hb->levels[HBITMAP_LEVELS - 1][hbi->pos];
    int64_t item;

    if (cur == 0) {
        cur = hbitmap_iter_skip_words(hbi);
        if (cur == 0) {
            return -1;
        }
    }

    /* The next call will resume work from the next bit.  */
    hbi->cur[HBITMAP_LEVELS - 1] = cur & (cur - 1);
    item = ((uint64_t)hbi->pos << BITS_PER_LEVEL) + ctzl(cur);

    return item << hbi->granularity;
}

void hbitmap_iter_init(HBitmapIter *hbi, const HBitmap *hb, uint64_t first)
{
    unsigned i, bit;
    uint64_t pos;

    hbi->hb = hb;
    pos = first >> hb->granularity;
    assert(pos < hb->size);
    hbi->pos = pos >> BITS_PER_LEVEL;
    hbi->granularity = hb->granularity;

    for (i = HBITMAP_LEVELS; i-- > 0; ) {
        bit = pos & (BITS_PER_LONG - 1);
        pos >>= BITS_PER_LEVEL;

        /* Drop bits representing items before first.  */
        hbi->cur[i] = hb->levels[i][pos] & ~((1UL << bit) - 1);

        /* We have already added level i+1, so the lowest set bit has
         * been processed.  Clear it.
         */
        if (i != HBITMAP_LEVELS - 1) {
            hbi->cur[i] &= ~(1UL << bit);
        }
    }
}

int64_t hbitmap_next_zero(const HBitmap *hb, uint64_t start, uint64_t count)
{
    size_t pos = (start >> hb->granularity) >> BITS_PER_LEVEL;
    unsigned long *last_lev = hb->levels[HBITMAP_LEVELS - 1];
    unsigned long cur = last_lev[pos];
    unsigned start_bit_offset;
    uint64_t end_bit, sz;
    int64_t res;

    if (start >= hb->orig_size || count == 0) {
        return -1;
    }

    end_bit = count > hb->orig_size - start ?
                hb->size :
                ((start + count - 1) >> hb->granularity) + 1;
    sz = (end_bit + BITS_PER_LONG - 1) >> BITS_PER_LEVEL;

    /* There may be some zero bits in @cur before @start. We are not interested
     * in them, let's set them.
     */
    start_bit_offset = (start >> hb->granularity) & (BITS_PER_LONG - 1);
    cur |= (1UL << start_bit_offset) - 1;
    assert((start >> hb->granularity) < hb->size);

    if (cur == (unsigned long)-1) {
        do {
            pos++;
        } while (pos < sz && last_lev[pos] == (unsigned long)-1);

        if (pos >= sz) {
            return -1;
        }

        cur = last_lev[pos];
    }

    res = (pos << BITS_PER_LEVEL) + ctol(cur);
    if (res >= end_bit) {
        return -1;
    }

    res = res << hb->granularity;
    if (res < start) {
        assert(((start - res) >> hb->granularity) == 0);
        return start;
    }

    return res;
}

bool hbitmap_next_dirty_area(const HBitmap *hb, uint64_t *start,
                             uint64_t *count)
{
    HBitmapIter hbi;
    int64_t firt_dirty_off, area_end;
    uint32_t granularity = 1UL << hb->granularity;
    uint64_t end;

    if (*start >= hb->orig_size || *count == 0) {
        return false;
    }

    end = *count > hb->orig_size - *start ? hb->orig_size : *start + *count;

    hbitmap_iter_init(&hbi, hb, *start);
    firt_dirty_off = hbitmap_iter_next(&hbi);

    if (firt_dirty_off < 0 || firt_dirty_off >= end) {
        return false;
    }

    if (firt_dirty_off + granularity >= end) {
        area_end = end;
    } else {
        area_end = hbitmap_next_zero(hb, firt_dirty_off + granularity,
                                     end - firt_dirty_off - granularity);
        if (area_end < 0) {
            area_end = end;
        }
    }

    if (firt_dirty_off > *start) {
        *start = firt_dirty_off;
    }
    *count = area_end - *start;

    return true;
}

bool hbitmap_empty(const HBitmap *hb)
{
    return hb->count == 0;
}

int hbitmap_granularity(const HBitmap *hb)
{
    return hb->granularity;
}

uint64_t hbitmap_count(const HBitmap *hb)
{
    return hb->count << hb->granularity;
}

/* Count the number of set bits between start and end, not accounting for
 * the granularity.  Also an example of how to use hbitmap_iter_next_word.
 */
static uint64_t hb_count_between(HBitmap *hb, uint64_t start, uint64_t last)
{
    HBitmapIter hbi;
    uint64_t count = 0;
    uint64_t end = last + 1;
    unsigned long cur;
    size_t pos;

    hbitmap_iter_init(&hbi, hb, start << hb->granularity);
    for (;;) {
        pos = hbitmap_iter_next_word(&hbi, &cur);
        if (pos >= (end >> BITS_PER_LEVEL)) {
            break;
        }
        count += ctpopl(cur);
    }

    if (pos == (end >> BITS_PER_LEVEL)) {
        /* Drop bits representing the END-th and subsequent items.  */
        int bit = end & (BITS_PER_LONG - 1);
        cur &= (1UL << bit) - 1;
        count += ctpopl(cur);
    }

    return count;
}

/* Setting starts at the last layer and propagates up if an element
 * changes.
 */
static inline bool hb_set_elem(unsigned long *elem, uint64_t start, uint64_t last)
{
    unsigned long mask;
    unsigned long old;

    assert((last >> BITS_PER_LEVEL) == (start >> BITS_PER_LEVEL));
    assert(start <= last);

    mask = 2UL << (last & (BITS_PER_LONG - 1));
    mask -= 1UL << (start & (BITS_PER_LONG - 1));
    old = *elem;
    *elem |= mask;
    return old != *elem;
}

/* The recursive workhorse (the depth is limited to HBITMAP_LEVELS)...
 * Returns true if at least one bit is changed. */
static bool hb_set_between(HBitmap *hb, int level, uint64_t start,
                           uint64_t last)
{
    size_t pos = start >> BITS_PER_LEVEL;
    size_t lastpos = last >> BITS_PER_LEVEL;
    bool changed = false;
    size_t i;

    i = pos;
    if (i < lastpos) {
        uint64_t next = (start | (BITS_PER_LONG - 1)) + 1;
        changed |= hb_set_elem(&hb->levels[level][i], start, next - 1);
        for (;;) {
            start = next;
            next += BITS_PER_LONG;
            if (++i == lastpos) {
                break;
            }
            changed |= (hb->levels[level][i] == 0);
            hb->levels[level][i] = ~0UL;
        }
    }
    changed |= hb_set_elem(&hb->levels[level][i], start, last);

    /* If there was any change in this layer, we may have to update
     * the one above.
     */
    if (level > 0 && changed) {
        hb_set_between(hb, level - 1, pos, lastpos);
    }
    return changed;
}

void hbitmap_set(HBitmap *hb, uint64_t start, uint64_t count)
{
    /* Compute range in the last layer.  */
    uint64_t first, n;
    uint64_t last = start + count - 1;

    trace_hbitmap_set(hb, start, count,
                      start >> hb->granularity, last >> hb->granularity);

    first = start >> hb->granularity;
    last >>= hb->granularity;
    assert(last < hb->size);
    n = last - first + 1;

    hb->count += n - hb_count_between(hb, first, last);
    if (hb_set_between(hb, HBITMAP_LEVELS - 1, first, last) &&
        hb->meta) {
        hbitmap_set(hb->meta, start, count);
    }
}

/* Resetting works the other way round: propagate up if the new
 * value is zero.
 */
static inline bool hb_reset_elem(unsigned long *elem, uint64_t start, uint64_t last)
{
    unsigned long mask;
    bool blanked;

    assert((last >> BITS_PER_LEVEL) == (start >> BITS_PER_LEVEL));
    assert(start <= last);

    mask = 2UL << (last & (BITS_PER_LONG - 1));
    mask -= 1UL << (start & (BITS_PER_LONG - 1));
    blanked = *elem != 0 && ((*elem & ~mask) == 0);
    *elem &= ~mask;
    return blanked;
}

/* The recursive workhorse (the depth is limited to HBITMAP_LEVELS)...
 * Returns true if at least one bit is changed. */
static bool hb_reset_between(HBitmap *hb, int level, uint64_t start,
                             uint64_t last)
{
    size_t pos = start >> BITS_PER_LEVEL;
    size_t lastpos = last >> BITS_PER_LEVEL;
    bool changed = false;
    size_t i;

    i = pos;
    if (i < lastpos) {
        uint64_t next = (start | (BITS_PER_LONG - 1)) + 1;

        /* Here we need a more complex test than when setting bits.  Even if
         * something was changed, we must not blank bits in the upper level
         * unless the lower-level word became entirely zero.  So, remove pos
         * from the upper-level range if bits remain set.
         */
        if (hb_reset_elem(&hb->levels[level][i], start, next - 1)) {
            changed = true;
        } else {
            pos++;
        }

        for (;;) {
            start = next;
            next += BITS_PER_LONG;
            if (++i == lastpos) {
                break;
            }
            changed |= (hb->levels[level][i] != 0);
            hb->levels[level][i] = 0UL;
        }
    }

    /* Same as above, this time for lastpos.  */
    if (hb_reset_elem(&hb->levels[level][i], start, last)) {
        changed = true;
    } else {
        lastpos--;
    }

    if (level > 0 && changed) {
        hb_reset_between(hb, level - 1, pos, lastpos);
    }

    return changed;

}

void hbitmap_reset(HBitmap *hb, uint64_t start, uint64_t count)
{
    /* Compute range in the last layer.  */
    uint64_t first;
    uint64_t last = start + count - 1;

    trace_hbitmap_reset(hb, start, count,
                        start >> hb->granularity, last >> hb->granularity);

    first = start >> hb->granularity;
    last >>= hb->granularity;
    assert(last < hb->size);

    hb->count -= hb_count_between(hb, first, last);
    if (hb_reset_between(hb, HBITMAP_LEVELS - 1, first, last) &&
        hb->meta) {
        hbitmap_set(hb->meta, start, count);
    }
}

void hbitmap_reset_all(HBitmap *hb)
{
    unsigned int i;

    /* Same as hbitmap_alloc() except for memset() instead of malloc() */
    for (i = HBITMAP_LEVELS; --i >= 1; ) {
        memset(hb->levels[i], 0, hb->sizes[i] * sizeof(unsigned long));
    }

    hb->levels[0][0] = 1UL << (BITS_PER_LONG - 1);
    hb->count = 0;
}

bool hbitmap_is_serializable(const HBitmap *hb)
{
    /* Every serialized chunk must be aligned to 64 bits so that endianness
     * requirements can be fulfilled on both 64 bit and 32 bit hosts.
     * We have hbitmap_serialization_align() which converts this
     * alignment requirement from bitmap bits to items covered (e.g. sectors).
     * That value is:
     *    64 << hb->granularity
     * Since this value must not exceed UINT64_MAX, hb->granularity must be
     * less than 58 (== 64 - 6, where 6 is ld(64), i.e. 1 << 6 == 64).
     *
     * In order for hbitmap_serialization_align() to always return a
     * meaningful value, bitmaps that are to be serialized must have a
     * granularity of less than 58. */

    return hb->granularity < 58;
}

bool hbitmap_get(const HBitmap *hb, uint64_t item)
{
    /* Compute position and bit in the last layer.  */
    uint64_t pos = item >> hb->granularity;
    unsigned long bit = 1UL << (pos & (BITS_PER_LONG - 1));
    assert(pos < hb->size);

    return (hb->levels[HBITMAP_LEVELS - 1][pos >> BITS_PER_LEVEL] & bit) != 0;
}

uint64_t hbitmap_serialization_align(const HBitmap *hb)
{
    assert(hbitmap_is_serializable(hb));

    /* Require at least 64 bit granularity to be safe on both 64 bit and 32 bit
     * hosts. */
    return UINT64_C(64) << hb->granularity;
}

/* Start should be aligned to serialization granularity, chunk size should be
 * aligned to serialization granularity too, except for last chunk.
 */
static void serialization_chunk(const HBitmap *hb,
                                uint64_t start, uint64_t count,
                                unsigned long **first_el, uint64_t *el_count)
{
    uint64_t last = start + count - 1;
    uint64_t gran = hbitmap_serialization_align(hb);

    assert((start & (gran - 1)) == 0);
    assert((last >> hb->granularity) < hb->size);
    if ((last >> hb->granularity) != hb->size - 1) {
        assert((count & (gran - 1)) == 0);
    }

    start = (start >> hb->granularity) >> BITS_PER_LEVEL;
    last = (last >> hb->granularity) >> BITS_PER_LEVEL;

    *first_el = &hb->levels[HBITMAP_LEVELS - 1][start];
    *el_count = last - start + 1;
}

uint64_t hbitmap_serialization_size(const HBitmap *hb,
                                    uint64_t start, uint64_t count)
{
    uint64_t el_count;
    unsigned long *cur;

    if (!count) {
        return 0;
    }
    serialization_chunk(hb, start, count, &cur, &el_count);

    return el_count * sizeof(unsigned long);
}

void hbitmap_serialize_part(const HBitmap *hb, uint8_t *buf,
                            uint64_t start, uint64_t count)
{
    uint64_t el_count;
    unsigned long *cur, *end;

    if (!count) {
        return;
    }
    serialization_chunk(hb, start, count, &cur, &el_count);
    end = cur + el_count;

    while (cur != end) {
        unsigned long el =
            (BITS_PER_LONG == 32 ? cpu_to_le32(*cur) : cpu_to_le64(*cur));

        memcpy(buf, &el, sizeof(el));
        buf += sizeof(el);
        cur++;
    }
}

void hbitmap_deserialize_part(HBitmap *hb, uint8_t *buf,
                              uint64_t start, uint64_t count,
                              bool finish)
{
    uint64_t el_count;
    unsigned long *cur, *end;

    if (!count) {
        return;
    }
    serialization_chunk(hb, start, count, &cur, &el_count);
    end = cur + el_count;

    while (cur != end) {
        memcpy(cur, buf, sizeof(*cur));

        if (BITS_PER_LONG == 32) {
            le32_to_cpus((uint32_t *)cur);
        } else {
            le64_to_cpus((uint64_t *)cur);
        }

        buf += sizeof(unsigned long);
        cur++;
    }
    if (finish) {
        hbitmap_deserialize_finish(hb);
    }
}

void hbitmap_deserialize_zeroes(HBitmap *hb, uint64_t start, uint64_t count,
                                bool finish)
{
    uint64_t el_count;
    unsigned long *first;

    if (!count) {
        return;
    }
    serialization_chunk(hb, start, count, &first, &el_count);

    memset(first, 0, el_count * sizeof(unsigned long));
    if (finish) {
        hbitmap_deserialize_finish(hb);
    }
}

void hbitmap_deserialize_ones(HBitmap *hb, uint64_t start, uint64_t count,
                              bool finish)
{
    uint64_t el_count;
    unsigned long *first;

    if (!count) {
        return;
    }
    serialization_chunk(hb, start, count, &first, &el_count);

    memset(first, 0xff, el_count * sizeof(unsigned long));
    if (finish) {
        hbitmap_deserialize_finish(hb);
    }
}

void hbitmap_deserialize_finish(HBitmap *bitmap)
{
    int64_t i, size, prev_size;
    int lev;

    /* restore levels starting from penultimate to zero level, assuming
     * that the last level is ok */
    size = MAX((bitmap->size + BITS_PER_LONG - 1) >> BITS_PER_LEVEL, 1);
    for (lev = HBITMAP_LEVELS - 1; lev-- > 0; ) {
        prev_size = size;
        size = MAX((size + BITS_PER_LONG - 1) >> BITS_PER_LEVEL, 1);
        memset(bitmap->levels[lev], 0, size * sizeof(unsigned long));

        for (i = 0; i < prev_size; ++i) {
            if (bitmap->levels[lev + 1][i]) {
                bitmap->levels[lev][i >> BITS_PER_LEVEL] |=
                    1UL << (i & (BITS_PER_LONG - 1));
            }
        }
    }

    bitmap->levels[0][0] |= 1UL << (BITS_PER_LONG - 1);
    bitmap->count = hb_count_between(bitmap, 0, bitmap->size - 1);
}

void hbitmap_free(HBitmap *hb)
{
    unsigned i;
    assert(!hb->meta);
    for (i = HBITMAP_LEVELS; i-- > 0; ) {
        g_free(hb->levels[i]);
    }
    g_free(hb);
}

HBitmap *hbitmap_alloc(uint64_t size, int granularity)
{
    HBitmap *hb = g_new0(struct HBitmap, 1);
    unsigned i;

    hb->orig_size = size;

    assert(granularity >= 0 && granularity < 64);
    size = (size + (1ULL << granularity) - 1) >> granularity;
    assert(size <= ((uint64_t)1 << HBITMAP_LOG_MAX_SIZE));

    hb->size = size;
    hb->granularity = granularity;
    for (i = HBITMAP_LEVELS; i-- > 0; ) {
        size = MAX((size + BITS_PER_LONG - 1) >> BITS_PER_LEVEL, 1);
        hb->sizes[i] = size;
        hb->levels[i] = g_new0(unsigned long, size);
    }

    /* We necessarily have free bits in level 0 due to the definition
     * of HBITMAP_LEVELS, so use one for a sentinel.  This speeds up
     * hbitmap_iter_skip_words.
     */
    assert(size == 1);
    hb->levels[0][0] |= 1UL << (BITS_PER_LONG - 1);
    return hb;
}

void hbitmap_truncate(HBitmap *hb, uint64_t size)
{
    bool shrink;
    unsigned i;
    uint64_t num_elements = size;
    uint64_t old;

    /* Size comes in as logical elements, adjust for granularity. */
    size = (size + (1ULL << hb->granularity) - 1) >> hb->granularity;
    assert(size <= ((uint64_t)1 << HBITMAP_LOG_MAX_SIZE));
    shrink = size < hb->size;

    /* bit sizes are identical; nothing to do. */
    if (size == hb->size) {
        return;
    }

    /* If we're losing bits, let's clear those bits before we invalidate all of
     * our invariants. This helps keep the bitcount consistent, and will prevent
     * us from carrying around garbage bits beyond the end of the map.
     */
    if (shrink) {
        /* Don't clear partial granularity groups;
         * start at the first full one. */
        uint64_t start = ROUND_UP(num_elements, UINT64_C(1) << hb->granularity);
        uint64_t fix_count = (hb->size << hb->granularity) - start;

        assert(fix_count);
        hbitmap_reset(hb, start, fix_count);
    }

    hb->size = size;
    for (i = HBITMAP_LEVELS; i-- > 0; ) {
        size = MAX(BITS_TO_LONGS(size), 1);
        if (hb->sizes[i] == size) {
            break;
        }
        old = hb->sizes[i];
        hb->sizes[i] = size;
        hb->levels[i] = g_realloc(hb->levels[i], size * sizeof(unsigned long));
        if (!shrink) {
            memset(&hb->levels[i][old], 0x00,
                   (size - old) * sizeof(*hb->levels[i]));
        }
    }
    if (hb->meta) {
        hbitmap_truncate(hb->meta, hb->size << hb->granularity);
    }
}

bool hbitmap_can_merge(const HBitmap *a, const HBitmap *b)
{
    return (a->size == b->size) && (a->granularity == b->granularity);
}

/**
 * Given HBitmaps A and B, let A := A (BITOR) B.
 * Bitmap B will not be modified.
 *
 * @return true if the merge was successful,
 *         false if it was not attempted.
 */
bool hbitmap_merge(const HBitmap *a, const HBitmap *b, HBitmap *result)
{
    int i;
    uint64_t j;

    if (!hbitmap_can_merge(a, b) || !hbitmap_can_merge(a, result)) {
        return false;
    }
    assert(hbitmap_can_merge(b, result));

    if (hbitmap_count(b) == 0) {
        return true;
    }

    /* This merge is O(size), as BITS_PER_LONG and HBITMAP_LEVELS are constant.
     * It may be possible to improve running times for sparsely populated maps
     * by using hbitmap_iter_next, but this is suboptimal for dense maps.
     */
    for (i = HBITMAP_LEVELS - 1; i >= 0; i--) {
        for (j = 0; j < a->sizes[i]; j++) {
            result->levels[i][j] = a->levels[i][j] | b->levels[i][j];
        }
    }

    /* Recompute the dirty count */
    result->count = hb_count_between(result, 0, result->size - 1);

    return true;
}

HBitmap *hbitmap_create_meta(HBitmap *hb, int chunk_size)
{
    assert(!(chunk_size & (chunk_size - 1)));
    assert(!hb->meta);
    hb->meta = hbitmap_alloc(hb->size << hb->granularity,
                             hb->granularity + ctz32(chunk_size));
    return hb->meta;
}

void hbitmap_free_meta(HBitmap *hb)
{
    assert(hb->meta);
    hbitmap_free(hb->meta);
    hb->meta = NULL;
}

char *hbitmap_sha256(const HBitmap *bitmap, Error **errp)
{
    size_t size = bitmap->sizes[HBITMAP_LEVELS - 1] * sizeof(unsigned long);
    char *data = (char *)bitmap->levels[HBITMAP_LEVELS - 1];
    char *hash = NULL;
    qcrypto_hash_digest(QCRYPTO_HASH_ALG_SHA256, data, size, &hash, errp);

    return hash;
}
