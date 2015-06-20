#ifndef QEMU_RANGE_H
#define QEMU_RANGE_H

#include <inttypes.h>
#include <qemu/typedefs.h>

/*
 * Operations on 64 bit address ranges.
 * Notes:
 *   - ranges must not wrap around 0, but can include the last byte ~0x0LL.
 *   - this can not represent a full 0 to ~0x0LL range.
 */

/* A structure representing a range of addresses. */
struct Range {
    uint64_t begin; /* First byte of the range, or 0 if empty. */
    uint64_t end;   /* 1 + the last byte. 0 if range empty or ends at ~0x0LL. */
};

static inline void range_extend(Range *range, Range *extend_by)
{
    if (!extend_by->begin && !extend_by->end) {
        return;
    }
    if (!range->begin && !range->end) {
        *range = *extend_by;
        return;
    }
    if (range->begin > extend_by->begin) {
        range->begin = extend_by->begin;
    }
    /* Compare last byte in case region ends at ~0x0LL */
    if (range->end - 1 < extend_by->end - 1) {
        range->end = extend_by->end;
    }
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

#endif
