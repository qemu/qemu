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
#define HBITMAP_H

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
 * hbitmap_truncate:
 * @hb: The bitmap to change the size of.
 * @size: The number of elements to change the bitmap to accommodate.
 *
 * truncate or grow an existing bitmap to accommodate a new number of elements.
 * This may invalidate existing HBitmapIterators.
 */
void hbitmap_truncate(HBitmap *hb, uint64_t size);

/**
 * hbitmap_merge:
 *
 * Store result of merging @a and @b into @result.
 * @result is allowed to be equal to @a or @b.
 *
 * Return true if the merge was successful,
 *        false if it was not attempted.
 */
bool hbitmap_merge(const HBitmap *a, const HBitmap *b, HBitmap *result);

/**
 * hbitmap_can_merge:
 *
 * hbitmap_can_merge(a, b) && hbitmap_can_merge(a, result) is sufficient and
 * necessary for hbitmap_merge will not fail.
 *
 */
bool hbitmap_can_merge(const HBitmap *a, const HBitmap *b);

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
 * hbitmap_reset_all:
 * @hb: HBitmap to operate on.
 *
 * Reset all bits in an HBitmap.
 */
void hbitmap_reset_all(HBitmap *hb);

/**
 * hbitmap_get:
 * @hb: HBitmap to operate on.
 * @item: Bit to query (0-based).
 *
 * Return whether the @item-th bit in an HBitmap is set.
 */
bool hbitmap_get(const HBitmap *hb, uint64_t item);

/**
 * hbitmap_is_serializable:
 * @hb: HBitmap which should be (de-)serialized.
 *
 * Returns whether the bitmap can actually be (de-)serialized. Other
 * (de-)serialization functions may only be invoked if this function returns
 * true.
 *
 * Calling (de-)serialization functions does not affect a bitmap's
 * (de-)serializability.
 */
bool hbitmap_is_serializable(const HBitmap *hb);

/**
 * hbitmap_serialization_align:
 * @hb: HBitmap to operate on.
 *
 * Required alignment of serialization chunks, used by other serialization
 * functions. For every chunk:
 * 1. Chunk start should be aligned to this granularity.
 * 2. Chunk size should be aligned too, except for last chunk (for which
 *      start + count == hb->size)
 */
uint64_t hbitmap_serialization_align(const HBitmap *hb);

/**
 * hbitmap_serialization_size:
 * @hb: HBitmap to operate on.
 * @start: Starting bit
 * @count: Number of bits
 *
 * Return number of bytes hbitmap_(de)serialize_part needs
 */
uint64_t hbitmap_serialization_size(const HBitmap *hb,
                                    uint64_t start, uint64_t count);

/**
 * hbitmap_serialize_part
 * @hb: HBitmap to operate on.
 * @buf: Buffer to store serialized bitmap.
 * @start: First bit to store.
 * @count: Number of bits to store.
 *
 * Stores HBitmap data corresponding to given region. The format of saved data
 * is linear sequence of bits, so it can be used by hbitmap_deserialize_part
 * independently of endianness and size of HBitmap level array elements
 */
void hbitmap_serialize_part(const HBitmap *hb, uint8_t *buf,
                            uint64_t start, uint64_t count);

/**
 * hbitmap_deserialize_part
 * @hb: HBitmap to operate on.
 * @buf: Buffer to restore bitmap data from.
 * @start: First bit to restore.
 * @count: Number of bits to restore.
 * @finish: Whether to call hbitmap_deserialize_finish automatically.
 *
 * Restores HBitmap data corresponding to given region. The format is the same
 * as for hbitmap_serialize_part.
 *
 * If @finish is false, caller must call hbitmap_serialize_finish before using
 * the bitmap.
 */
void hbitmap_deserialize_part(HBitmap *hb, uint8_t *buf,
                              uint64_t start, uint64_t count,
                              bool finish);

/**
 * hbitmap_deserialize_zeroes
 * @hb: HBitmap to operate on.
 * @start: First bit to restore.
 * @count: Number of bits to restore.
 * @finish: Whether to call hbitmap_deserialize_finish automatically.
 *
 * Fills the bitmap with zeroes.
 *
 * If @finish is false, caller must call hbitmap_serialize_finish before using
 * the bitmap.
 */
void hbitmap_deserialize_zeroes(HBitmap *hb, uint64_t start, uint64_t count,
                                bool finish);

/**
 * hbitmap_deserialize_ones
 * @hb: HBitmap to operate on.
 * @start: First bit to restore.
 * @count: Number of bits to restore.
 * @finish: Whether to call hbitmap_deserialize_finish automatically.
 *
 * Fills the bitmap with ones.
 *
 * If @finish is false, caller must call hbitmap_serialize_finish before using
 * the bitmap.
 */
void hbitmap_deserialize_ones(HBitmap *hb, uint64_t start, uint64_t count,
                              bool finish);

/**
 * hbitmap_deserialize_finish
 * @hb: HBitmap to operate on.
 *
 * Repair HBitmap after calling hbitmap_deserialize_data. Actually, all HBitmap
 * layers are restored here.
 */
void hbitmap_deserialize_finish(HBitmap *hb);

/**
 * hbitmap_sha256:
 * @bitmap: HBitmap to operate on.
 *
 * Returns SHA256 hash of the last level.
 */
char *hbitmap_sha256(const HBitmap *bitmap, Error **errp);

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
 * iteration to miss some of those bits.
 *
 * The concurrent resetting of bits is OK.
 */
void hbitmap_iter_init(HBitmapIter *hbi, const HBitmap *hb, uint64_t first);

/* hbitmap_iter_skip_words:
 * @hbi: HBitmapIter to operate on.
 *
 * Internal function used by hbitmap_iter_next and hbitmap_iter_next_word.
 */
unsigned long hbitmap_iter_skip_words(HBitmapIter *hbi);

/* hbitmap_next_zero:
 *
 * Find next not dirty bit within selected range. If not found, return -1.
 *
 * @hb: The HBitmap to operate on
 * @start: The bit to start from.
 * @count: Number of bits to proceed. If @start+@count > bitmap size, the whole
 * bitmap is looked through. You can use UINT64_MAX as @count to search up to
 * the bitmap end.
 */
int64_t hbitmap_next_zero(const HBitmap *hb, uint64_t start, uint64_t count);

/* hbitmap_next_dirty_area:
 * @hb: The HBitmap to operate on
 * @start: in-out parameter.
 *         in: the offset to start from
 *         out: (if area found) start of found area
 * @count: in-out parameter.
 *         in: length of requested region
 *         out: length of found area
 *
 * If dirty area found within [@start, @start + @count), returns true and sets
 * @offset and @bytes appropriately. Otherwise returns false and leaves @offset
 * and @bytes unchanged.
 */
bool hbitmap_next_dirty_area(const HBitmap *hb, uint64_t *start,
                             uint64_t *count);

/* hbitmap_create_meta:
 * Create a "meta" hbitmap to track dirtiness of the bits in this HBitmap.
 * The caller owns the created bitmap and must call hbitmap_free_meta(hb) to
 * free it.
 *
 * Currently, we only guarantee that if a bit in the hbitmap is changed it
 * will be reflected in the meta bitmap, but we do not yet guarantee the
 * opposite.
 *
 * @hb: The HBitmap to operate on.
 * @chunk_size: How many bits in @hb does one bit in the meta track.
 */
HBitmap *hbitmap_create_meta(HBitmap *hb, int chunk_size);

/* hbitmap_free_meta:
 * Free the meta bitmap of @hb.
 *
 * @hb: The HBitmap whose meta bitmap should be freed.
 */
void hbitmap_free_meta(HBitmap *hb);

/**
 * hbitmap_iter_next:
 * @hbi: HBitmapIter to operate on.
 *
 * Return the next bit that is set in @hbi's associated HBitmap,
 * or -1 if all remaining bits are zero.
 */
int64_t hbitmap_iter_next(HBitmapIter *hbi);

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
