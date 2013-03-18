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

#ifndef HBITMAP_H
#define HBITMAP_H 1

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include "bitops.h"
#include "host-utils.h"

typedef struct HBitmap HBitmap;
typedef struct HBitmapIter HBitmapIter;

#define BITS_PER_LEVEL         (BITS_PER_LONG == 32 ? 5 : 6)

/* For 32-bit, the largest that fits in a 4 GiB address space.
 * For 64-bit, the number of sectors in 1 PiB.  Good luck, in
 * either case... :)
 */
#define HBITMAP_LOG_MAX_SIZE   (BITS_PER_LONG == 32 ? 34 : 41)

/* We need to place a sentinel in level 0 to speed up iteration.  Thus,
 * we do this instead of HBITMAP_LOG_MAX_SIZE / BITS_PER_LEVEL.  The
 * difference is that it allocates an extra level when HBITMAP_LOG_MAX_SIZE
 * is an exact multiple of BITS_PER_LEVEL.
 */
#define HBITMAP_LEVELS         ((HBITMAP_LOG_MAX_SIZE / BITS_PER_LEVEL) + 1)

struct HBitmapIter {
    const HBitmap *hb;

    /* Copied from hb for access in the inline functions (hb is opaque).  */
    int granularity;

    /* Entry offset into the last-level array of longs.  */
    size_t pos;

    /* The currently-active path in the tree.  Each item of cur[i] stores
     * the bits (i.e. the subtrees) yet to be processed under that node.
     */
    unsigned long cur[HBITMAP_LEVELS];
};

/**
 * hbitmap_alloc:
 * @size: Number of bits in the bitmap.
 * @granularity: Granularity of the bitmap.  Aligned groups of 2^@granularity
 * bits will be represented by a single bit.  Each operation on a
 * range of bits first rounds the bits to determine which group they land
 * in, and then affect the entire set; iteration will only visit the first
 * bit of each group.
 *
 * Allocate a new HBitmap.
 */
HBitmap *hbitmap_alloc(uint64_t size, int granularity);

/**
 * hbitmap_empty:
 * @hb: HBitmap to operate on.
 *
 * Return whether the bitmap is empty.
 */
bool hbitmap_empty(const HBitmap *hb);

/**
 * hbitmap_granularity:
 * @hb: HBitmap to operate on.
 *
 * Return the granularity of the HBitmap.
 */
int hbitmap_granularity(const HBitmap *hb);

/**
 * hbitmap_count:
 * @hb: HBitmap to operate on.
 *
 * Return the number of bits set in the HBitmap.
 */
uint64_t hbitmap_count(const HBitmap *hb);

/**
 * hbitmap_set:
 * @hb: HBitmap to operate on.
 * @start: First bit to set (0-based).
 * @count: Number of bits to set.
 *
 * Set a consecutive range of bits in an HBitmap.
 */
void hbitmap_set(HBitmap *hb, uint64_t start, uint64_t count);

/**
 * hbitmap_reset:
 * @hb: HBitmap to operate on.
 * @start: First bit to reset (0-based).
 * @count: Number of bits to reset.
 *
 * Reset a consecutive range of bits in an HBitmap.
 */
void hbitmap_reset(HBitmap *hb, uint64_t start, uint64_t count);

/**
 * hbitmap_get:
 * @hb: HBitmap to operate on.
 * @item: Bit to query (0-based).
 *
 * Return whether the @item-th bit in an HBitmap is set.
 */
bool hbitmap_get(const HBitmap *hb, uint64_t item);

/**
 * hbitmap_free:
 * @hb: HBitmap to operate on.
 *
 * Free an HBitmap and all of its associated memory.
 */
void hbitmap_free(HBitmap *hb);

/**
 * hbitmap_iter_init:
 * @hbi: HBitmapIter to initialize.
 * @hb: HBitmap to iterate on.
 * @first: First bit to visit (0-based, must be strictly less than the
 * size of the bitmap).
 *
 * Set up @hbi to iterate on the HBitmap @hb.  hbitmap_iter_next will return
 * the lowest-numbered bit that is set in @hb, starting at @first.
 *
 * Concurrent setting of bits is acceptable, and will at worst cause the
 * iteration to miss some of those bits.  Resetting bits before the current
 * position of the iterator is also okay.  However, concurrent resetting of
 * bits can lead to unexpected behavior if the iterator has not yet reached
 * those bits.
 */
void hbitmap_iter_init(HBitmapIter *hbi, const HBitmap *hb, uint64_t first);

/* hbitmap_iter_skip_words:
 * @hbi: HBitmapIter to operate on.
 *
 * Internal function used by hbitmap_iter_next and hbitmap_iter_next_word.
 */
unsigned long hbitmap_iter_skip_words(HBitmapIter *hbi);

/**
 * hbitmap_iter_next:
 * @hbi: HBitmapIter to operate on.
 *
 * Return the next bit that is set in @hbi's associated HBitmap,
 * or -1 if all remaining bits are zero.
 */
static inline int64_t hbitmap_iter_next(HBitmapIter *hbi)
{
    unsigned long cur = hbi->cur[HBITMAP_LEVELS - 1];
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

/**
 * hbitmap_iter_next_word:
 * @hbi: HBitmapIter to operate on.
 * @p_cur: Location where to store the next non-zero word.
 *
 * Return the index of the next nonzero word that is set in @hbi's
 * associated HBitmap, and set *p_cur to the content of that word
 * (bits before the index that was passed to hbitmap_iter_init are
 * trimmed on the first call).  Return -1, and set *p_cur to zero,
 * if all remaining words are zero.
 */
static inline size_t hbitmap_iter_next_word(HBitmapIter *hbi, unsigned long *p_cur)
{
    unsigned long cur = hbi->cur[HBITMAP_LEVELS - 1];

    if (cur == 0) {
        cur = hbitmap_iter_skip_words(hbi);
        if (cur == 0) {
            *p_cur = 0;
            return -1;
        }
    }

    /* The next call will resume work from the next word.  */
    hbi->cur[HBITMAP_LEVELS - 1] = 0;
    *p_cur = cur;
    return hbi->pos;
}


#endif
