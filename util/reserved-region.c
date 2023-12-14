/*
 * QEMU ReservedRegion helpers
 *
 * Copyright (c) 2023 Red Hat, Inc.
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
#include "qemu/reserved-region.h"

GList *resv_region_list_insert(GList *list, ReservedRegion *reg)
{
    ReservedRegion *resv_iter, *new_reg;
    Range *r = &reg->range;
    Range *range_iter;
    GList *l;

    for (l = list; l ; ) {
        resv_iter = (ReservedRegion *)l->data;
        range_iter = &resv_iter->range;

        /* Skip all list elements strictly less than range to add */
        if (range_compare(range_iter, r) < 0) {
            l = l->next;
        } else if (range_compare(range_iter, r) > 0) {
            return g_list_insert_before(list, l, reg);
        } else { /* there is an overlap */
            if (range_contains_range(r, range_iter)) {
                /* new range contains current item, simply remove this latter */
                GList *prev = l->prev;
                g_free(l->data);
                list = g_list_delete_link(list, l);
                if (prev) {
                    l = prev->next;
                } else {
                    l = list;
                }
            } else if (range_contains_range(range_iter, r)) {
                /* new region is included in the current region */
                if (range_lob(range_iter) == range_lob(r)) {
                    /* adjacent on the left side, derives into 2 regions */
                    range_set_bounds(range_iter, range_upb(r) + 1,
                                     range_upb(range_iter));
                    return g_list_insert_before(list, l, reg);
                } else if (range_upb(range_iter) == range_upb(r)) {
                    /* adjacent on the right side, derives into 2 regions */
                    range_set_bounds(range_iter, range_lob(range_iter),
                                     range_lob(r) - 1);
                    l = l->next;
                } else {
                    uint64_t lob = range_lob(range_iter);
                    /*
                     * the new range is in the middle of an existing one,
                     * split this latter into 3 regs instead
                     */
                    range_set_bounds(range_iter, range_upb(r) + 1,
                                     range_upb(range_iter));
                    new_reg = g_new0(ReservedRegion, 1);
                    new_reg->type = resv_iter->type;
                    range_set_bounds(&new_reg->range,
                                     lob, range_lob(r) - 1);
                    list = g_list_insert_before(list, l, new_reg);
                    return g_list_insert_before(list, l, reg);
                }
            } else if (range_lob(r) < range_lob(range_iter)) {
                range_set_bounds(range_iter, range_upb(r) + 1,
                                 range_upb(range_iter));
                return g_list_insert_before(list, l, reg);
            } else { /* intersection on the upper range */
                range_set_bounds(range_iter, range_lob(range_iter),
                                 range_lob(r) - 1);
                l = l->next;
            }
        } /* overlap */
    }
    return g_list_append(list, reg);
}

