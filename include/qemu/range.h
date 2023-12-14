/*
 * QEMU 64-bit address ranges
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_RANGE_H
#define QEMU_RANGE_H

/*
 * Operations on 64 bit address ranges.
 * Notes:
 * - Ranges must not wrap around 0, but can include UINT64_MAX.
 */

struct Range {
    /*
     * Do not access members directly, use the functions!
     * A non-empty range has @lob <= @upb.
     * An empty range has @lob == @upb + 1.
     */
    uint64_t lob;        /* inclusive lower bound */
    uint64_t upb;        /* inclusive upper bound */
};

static inline void range_invariant(const Range *range)
{
    assert(range->lob <= range->upb || range->lob == range->upb + 1);
}

/* Compound literal encoding the empty range */
#define range_empty ((Range){ .lob = 1, .upb = 0 })

/* Is @range empty? */
static inline bool range_is_empty(const Range *range)
{
    range_invariant(range);
    return range->lob > range->upb;
}

/* Does @range contain @val? */
static inline bool range_contains(const Range *range, uint64_t val)
{
    return val >= range->lob && val <= range->upb;
}

/* Initialize @range to the empty range */
static inline void range_make_empty(Range *range)
{
    *range = range_empty;
    assert(range_is_empty(range));
}

/*
 * Initialize @range to span the interval [@lob,@upb].
 * Both bounds are inclusive.
 * The interval must not be empty, i.e. @lob must be less than or
 * equal @upb.
 */
static inline void range_set_bounds(Range *range, uint64_t lob, uint64_t upb)
{
    range->lob = lob;
    range->upb = upb;
    assert(!range_is_empty(range));
}

/*
 * Initialize @range to span the interval [@lob,@upb_plus1).
 * The lower bound is inclusive, the upper bound is exclusive.
 * Zero @upb_plus1 is special: if @lob is also zero, set @range to the
 * empty range.  Else, set @range to [@lob,UINT64_MAX].
 */
static inline void range_set_bounds1(Range *range,
                                     uint64_t lob, uint64_t upb_plus1)
{
    if (!lob && !upb_plus1) {
        *range = range_empty;
    } else {
        range->lob = lob;
        range->upb = upb_plus1 - 1;
    }
    range_invariant(range);
}

/* Return @range's lower bound.  @range must not be empty. */
static inline uint64_t range_lob(Range *range)
{
    assert(!range_is_empty(range));
    return range->lob;
}

/* Return @range's upper bound.  @range must not be empty. */
static inline uint64_t range_upb(Range *range)
{
    assert(!range_is_empty(range));
    return range->upb;
}

/*
 * Initialize @range to span the interval [@lob,@lob + @size - 1].
 * @size may be 0. If the range would overflow, returns -ERANGE, otherwise
 * 0.
 */
G_GNUC_WARN_UNUSED_RESULT
static inline int range_init(Range *range, uint64_t lob, uint64_t size)
{
    if (lob + size < lob) {
        return -ERANGE;
    }
    range->lob = lob;
    range->upb = lob + size - 1;
    range_invariant(range);
    return 0;
}

/*
 * Initialize @range to span the interval [@lob,@lob + @size - 1].
 * @size may be 0. Range must not overflow.
 */
static inline void range_init_nofail(Range *range, uint64_t lob, uint64_t size)
{
    range->lob = lob;
    range->upb = lob + size - 1;
    range_invariant(range);
}

/*
 * Get the size of @range.
 */
static inline uint64_t range_size(const Range *range)
{
    return range->upb - range->lob + 1;
}

/*
 * Check if @range1 overlaps with @range2. If one of the ranges is empty,
 * the result is always "false".
 */
static inline bool range_overlaps_range(const Range *range1,
                                        const Range *range2)
{
    if (range_is_empty(range1) || range_is_empty(range2)) {
        return false;
    }
    return !(range2->upb < range1->lob || range1->upb < range2->lob);
}

/*
 * Check if @range1 contains @range2. If one of the ranges is empty,
 * the result is always "false".
 */
static inline bool range_contains_range(const Range *range1,
                                        const Range *range2)
{
    if (range_is_empty(range1) || range_is_empty(range2)) {
        return false;
    }
    return range1->lob <= range2->lob && range1->upb >= range2->upb;
}

/*
 * Extend @range to the smallest interval that includes @extend_by, too.
 */
static inline void range_extend(Range *range, Range *extend_by)
{
    if (range_is_empty(extend_by)) {
        return;
    }
    if (range_is_empty(range)) {
        *range = *extend_by;
        return;
    }
    if (range->lob > extend_by->lob) {
        range->lob = extend_by->lob;
    }
    if (range->upb < extend_by->upb) {
        range->upb = extend_by->upb;
    }
    range_invariant(range);
}

/* Get last byte of a range from offset + length.
 * Undefined for ranges that wrap around 0. */
static inline uint64_t range_get_last(uint64_t offset, uint64_t len)
{
    return offset + len - 1;
}

/* Check whether a given range covers a given byte. */
static inline int range_covers_byte(uint64_t offset, uint64_t len,
                                    uint64_t byte)
{
    return offset <= byte && byte <= range_get_last(offset, len);
}

/* Check whether 2 given ranges overlap.
 * Undefined if ranges that wrap around 0. */
static inline int ranges_overlap(uint64_t first1, uint64_t len1,
                                 uint64_t first2, uint64_t len2)
{
    uint64_t last1 = range_get_last(first1, len1);
    uint64_t last2 = range_get_last(first2, len2);

    return !(last2 < first1 || last1 < first2);
}

/*
 * Return -1 if @a < @b, 1 @a > @b, and 0 if they touch or overlap.
 * Both @a and @b must not be empty.
 */
int range_compare(Range *a, Range *b);

GList *range_list_insert(GList *list, Range *data);

/*
 * Inverse an array of sorted ranges over the [low, high] span, ie.
 * original ranges becomes holes in the newly allocated inv_ranges
 */
void range_inverse_array(GList *in_ranges,
                         GList **out_ranges,
                         uint64_t low, uint64_t high);

#endif
