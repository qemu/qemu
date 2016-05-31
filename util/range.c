/*
 * QEMU 64-bit address ranges
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/range.h"

/*
 * Operations on 64 bit address ranges.
 * Notes:
 *   - ranges must not wrap around 0, but can include the last byte ~0x0LL.
 *   - this can not represent a full 0 to ~0x0LL range.
 */

/* 0,1 can merge with 1,2 but don't overlap */
static bool ranges_can_merge(Range *range1, Range *range2)
{
    return !(range1->end < range2->begin || range2->end < range1->begin);
}

static void range_merge(Range *range1, Range *range2)
{
    if (range1->end < range2->end) {
        range1->end = range2->end;
    }
    if (range1->begin > range2->begin) {
        range1->begin = range2->begin;
    }
}

GList *g_list_insert_sorted_merged(GList *list, gpointer data,
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
