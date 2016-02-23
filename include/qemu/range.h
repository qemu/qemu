#ifndef QEMU_RANGE_H
#define QEMU_RANGE_H

#include <qemu/typedefs.h>
#include "qemu/queue.h"

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

/* 0,1 can merge with 1,2 but don't overlap */
static inline bool ranges_can_merge(Range *range1, Range *range2)
{
    return !(range1->end < range2->begin || range2->end < range1->begin);
}

static inline int range_merge(Range *range1, Range *range2)
{
    if (ranges_can_merge(range1, range2)) {
        if (range1->end < range2->end) {
            range1->end = range2->end;
        }
        if (range1->begin > range2->begin) {
            range1->begin = range2->begin;
        }
        return 0;
    }

    return -1;
}

static inline GList *g_list_insert_sorted_merged(GList *list,
                                                 gpointer data,
                                                 GCompareFunc func)
{
    GList *l, *next = NULL;
    Range *r, *nextr;

    if (!list) {
        list = g_list_insert_sorted(list, data, func);
        return list;
    }

    nextr = data;
    l = list;
    while (l && l != next && nextr) {
        r = l->data;
        if (ranges_can_merge(r, nextr)) {
            range_merge(r, nextr);
            l = g_list_remove_link(l, next);
            next = g_list_next(l);
            if (next) {
                nextr = next->data;
            } else {
                nextr = NULL;
            }
        } else {
            l = g_list_next(l);
        }
    }

    if (!l) {
        list = g_list_insert_sorted(list, data, func);
    }

    return list;
}

static inline gint range_compare(gconstpointer a, gconstpointer b)
{
    Range *ra = (Range *)a, *rb = (Range *)b;
    if (ra->begin == rb->begin && ra->end == rb->end) {
        return 0;
    } else if (range_get_last(ra->begin, ra->end) <
               range_get_last(rb->begin, rb->end)) {
        return -1;
    } else {
        return 1;
    }
}

#endif
