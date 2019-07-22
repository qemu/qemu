/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bitmap.c unit-tests.
 *
 * Copyright (C) 2019, Red Hat, Inc.
 *
 * Author: Peter Xu <peterx@redhat.com>
 */

#include <stdlib.h>
#include "qemu/osdep.h"
#include "qemu/bitmap.h"

#define BMAP_SIZE  1024

static void check_bitmap_copy_with_offset(void)
{
    unsigned long *bmap1, *bmap2, *bmap3, total;

    bmap1 = bitmap_new(BMAP_SIZE);
    bmap2 = bitmap_new(BMAP_SIZE);
    bmap3 = bitmap_new(BMAP_SIZE);

    bmap1[0] = random();
    bmap1[1] = random();
    bmap1[2] = random();
    bmap1[3] = random();
    total = BITS_PER_LONG * 4;

    /* Shift 115 bits into bmap2 */
    bitmap_copy_with_dst_offset(bmap2, bmap1, 115, total);
    /* Shift another 85 bits into bmap3 */
    bitmap_copy_with_dst_offset(bmap3, bmap2, 85, total + 115);
    /* Shift back 200 bits back */
    bitmap_copy_with_src_offset(bmap2, bmap3, 200, total);

    g_assert_cmpmem(bmap1, total / BITS_PER_LONG,
                    bmap2, total / BITS_PER_LONG);

    bitmap_clear(bmap1, 0, BMAP_SIZE);
    /* Set bits in bmap1 are 100-245 */
    bitmap_set(bmap1, 100, 145);

    /* Set bits in bmap2 are 60-205 */
    bitmap_copy_with_src_offset(bmap2, bmap1, 40, 250);
    g_assert_cmpint(find_first_bit(bmap2, 60), ==, 60);
    g_assert_cmpint(find_next_zero_bit(bmap2, 205, 60), ==, 205);
    g_assert(test_bit(205, bmap2) == 0);

    /* Set bits in bmap3 are 135-280 */
    bitmap_copy_with_dst_offset(bmap3, bmap1, 35, 250);
    g_assert_cmpint(find_first_bit(bmap3, 135), ==, 135);
    g_assert_cmpint(find_next_zero_bit(bmap3, 280, 135), ==, 280);
    g_assert(test_bit(280, bmap3) == 0);

    g_free(bmap1);
    g_free(bmap2);
    g_free(bmap3);
}

typedef void (*bmap_set_func)(unsigned long *map, long i, long len);
static void bitmap_set_case(bmap_set_func set_func)
{
    unsigned long *bmap;
    int offset;

    bmap = bitmap_new(BMAP_SIZE);

    /* Both Aligned, set bits [BITS_PER_LONG, 3*BITS_PER_LONG] */
    set_func(bmap, BITS_PER_LONG, 2 * BITS_PER_LONG);
    g_assert_cmpuint(bmap[1], ==, -1ul);
    g_assert_cmpuint(bmap[2], ==, -1ul);
    g_assert_cmpint(find_first_bit(bmap, BITS_PER_LONG), ==, BITS_PER_LONG);
    g_assert_cmpint(find_next_zero_bit(bmap, 3 * BITS_PER_LONG, BITS_PER_LONG),
                    ==, 3 * BITS_PER_LONG);

    for (offset = 0; offset <= BITS_PER_LONG; offset++) {
        bitmap_clear(bmap, 0, BMAP_SIZE);
        /* End Aligned, set bits [BITS_PER_LONG - offset, 3*BITS_PER_LONG] */
        set_func(bmap, BITS_PER_LONG - offset, 2 * BITS_PER_LONG + offset);
        g_assert_cmpuint(bmap[1], ==, -1ul);
        g_assert_cmpuint(bmap[2], ==, -1ul);
        g_assert_cmpint(find_first_bit(bmap, BITS_PER_LONG),
                        ==, BITS_PER_LONG - offset);
        g_assert_cmpint(find_next_zero_bit(bmap,
                                           3 * BITS_PER_LONG,
                                           BITS_PER_LONG - offset),
                        ==, 3 * BITS_PER_LONG);
    }

    for (offset = 0; offset <= BITS_PER_LONG; offset++) {
        bitmap_clear(bmap, 0, BMAP_SIZE);
        /* Start Aligned, set bits [BITS_PER_LONG, 3*BITS_PER_LONG + offset] */
        set_func(bmap, BITS_PER_LONG, 2 * BITS_PER_LONG + offset);
        g_assert_cmpuint(bmap[1], ==, -1ul);
        g_assert_cmpuint(bmap[2], ==, -1ul);
        g_assert_cmpint(find_first_bit(bmap, BITS_PER_LONG),
                        ==, BITS_PER_LONG);
        g_assert_cmpint(find_next_zero_bit(bmap,
                                           3 * BITS_PER_LONG + offset,
                                           BITS_PER_LONG),
                        ==, 3 * BITS_PER_LONG + offset);
    }

    g_free(bmap);
}

static void check_bitmap_set(void)
{
    bitmap_set_case(bitmap_set);
    bitmap_set_case(bitmap_set_atomic);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/bitmap/bitmap_copy_with_offset",
                    check_bitmap_copy_with_offset);
    g_test_add_func("/bitmap/bitmap_set",
                    check_bitmap_set);

    g_test_run();

    return 0;
}
