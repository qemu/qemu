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
        cur = hbi->cur[--i];
        pos >>= BITS_PER_LEVEL;
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
 * changes from zero to non-zero.
 */
static inline bool hb_set_elem(unsigned long *elem, uint64_t start, uint64_t last)
{
    unsigned long mask;
    bool changed;

    assert((last >> BITS_PER_LEVEL) == (start >> BITS_PER_LEVEL));
    assert(start <= last);

    mask = 2UL << (last & (BITS_PER_LONG - 1));
    mask -= 1UL << (start & (BITS_PER_LONG - 1));
    changed = (*elem == 0);
    *elem |= mask;
    return changed;
}

/* The recursive workhorse (the depth is limited to HBITMAP_LEVELS)... */
static void hb_set_between(HBitmap *hb, int level, uint64_t start, uint64_t last)
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
}

void hbitmap_set(HBitmap *hb, uint64_t start, uint64_t count)
{
    /* Compute range in the last layer.  */
    uint64_t last = start + count - 1;

    trace_hbitmap_set(hb, start, count,
                      start >> hb->granularity, last >> hb->granularity);

    start >>= hb->granularity;
    last >>= hb->granularity;
    count = last - start + 1;

    hb->count += count - hb_count_between(hb, start, last);
    hb_set_between(hb, HBITMAP_LEVELS - 1, start, last);
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

/* The recursive workhorse (the depth is limited to HBITMAP_LEVELS)... */
static void hb_reset_between(HBitmap *hb, int level, uint64_t start, uint64_t last)
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
}

void hbitmap_reset(HBitmap *hb, uint64_t start, uint64_t count)
{
    /* Compute range in the last layer.  */
    uint64_t last = start + count - 1;

    trace_hbitmap_reset(hb, start, count,
                        start >> hb->granularity, last >> hb->granularity);

    start >>= hb->granularity;
    last >>= hb->granularity;

    hb->count -= hb_count_between(hb, start, last);
    hb_reset_between(hb, HBITMAP_LEVELS - 1, start, last);
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

bool hbitmap_get(const HBitmap *hb, uint64_t item)
{
    /* Compute position and bit in the last layer.  */
    uint64_t pos = item >> hb->granularity;
    unsigned long bit = 1UL << (pos & (BITS_PER_LONG - 1));

    return (hb->levels[HBITMAP_LEVELS - 1][pos >> BITS_PER_LEVEL] & bit) != 0;
}

void hbitmap_free(HBitmap *hb)
{
    unsigned i;
    for (i = HBITMAP_LEVELS; i-- > 0; ) {
        g_free(hb->levels[i]);
    }
    g_free(hb);
}

HBitmap *hbitmap_alloc(uint64_t size, int granularity)
{
    HBitmap *hb = g_new0(struct HBitmap, 1);
    unsigned i;

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
        uint64_t start = QEMU_ALIGN_UP(num_elements, 1 << hb->granularity);
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
}


/**
 * Given HBitmaps A and B, let A := A (BITOR) B.
 * Bitmap B will not be modified.
 *
 * @return true if the merge was successful,
 *         false if it was not attempted.
 */
bool hbitmap_merge(HBitmap *a, const HBitmap *b)
{
    int i;
    uint64_t j;

    if ((a->size != b->size) || (a->granularity != b->granularity)) {
        return false;
    }

    if (hbitmap_count(b) == 0) {
        return true;
    }

    /* This merge is O(size), as BITS_PER_LONG and HBITMAP_LEVELS are constant.
     * It may be possible to improve running times for sparsely populated maps
     * by using hbitmap_iter_next, but this is suboptimal for dense maps.
     */
    for (i = HBITMAP_LEVELS - 1; i >= 0; i--) {
        for (j = 0; j < a->sizes[i]; j++) {
            a->levels[i][j] |= b->levels[i][j];
        }
    }

    return true;
}
