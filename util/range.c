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

#include "qemu/osdep.h"
#include "qemu/range.h"

int range_compare(Range *a, Range *b)
{
    assert(!range_is_empty(a) && !range_is_empty(b));

    /* Careful, avoid wraparound */
    if (b->lob && b->lob - 1 > a->upb) {
        return -1;
    }
    if (a->lob && a->lob - 1 > b->upb) {
        return 1;
    }
    return 0;
}

/* Insert @data into @list of ranges; caller no longer owns @data */
GList *range_list_insert(GList *list, Range *data)
{
    GList *l;

    assert(!range_is_empty(data));

    /* Skip all list elements strictly less than data */
    for (l = list; l && range_compare(l->data, data) < 0; l = l->next) {
    }

    if (!l || range_compare(l->data, data) > 0) {
        /* Rest of the list (if any) is strictly greater than @data */
        return g_list_insert_before(list, l, data);
    }

    /* Current list element overlaps @data, merge the two */
    range_extend(l->data, data);
    g_free(data);

    /* Merge any subsequent list elements that now also overlap */
    while (l->next && range_compare(l->data, l->next->data) == 0) {
        GList *new_l;

        range_extend(l->data, l->next->data);
        g_free(l->next->data);
        new_l = g_list_delete_link(list, l->next);
        assert(new_l == list);
    }

    return list;
}

static inline
GList *append_new_range(GList *list, uint64_t lob, uint64_t upb)
{
    Range *new = g_new0(Range, 1);

    range_set_bounds(new, lob, upb);
    return g_list_append(list, new);
}


void range_inverse_array(GList *in, GList **rev,
                         uint64_t low, uint64_t high)
{
    Range *r, *rn;
    GList *l = in, *out = *rev;

    for (l = in; l && range_upb(l->data) < low; l = l->next) {
        continue;
    }

    if (!l) {
        out = append_new_range(out, low, high);
        goto exit;
    }
    r = (Range *)l->data;

    /* first range lob is greater than min, insert a first range */
    if (range_lob(r) > low) {
        out = append_new_range(out, low, MIN(range_lob(r) - 1, high));
    }

    /* insert a range in between each original range until we reach high */
    for (; l->next; l = l->next) {
        r = (Range *)l->data;
        rn = (Range *)l->next->data;
        if (range_lob(r) >= high) {
            goto exit;
        }
        if (range_compare(r, rn)) {
            out = append_new_range(out, range_upb(r) + 1,
                                   MIN(range_lob(rn) - 1, high));
        }
    }

    /* last range */
    r = (Range *)l->data;

    /* last range upb is less than max, insert a last range */
    if (range_upb(r) <  high) {
        out = append_new_range(out, range_upb(r) + 1, high);
    }
exit:
    *rev = out;
}
