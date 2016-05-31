/*
 * Hierarchical bitmap unit-tests.
 *
 * Copyright (C) 2012 Red Hat Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/hbitmap.h"

#define LOG_BITS_PER_LONG          (BITS_PER_LONG == 32 ? 5 : 6)

#define L1                         BITS_PER_LONG
#define L2                         (BITS_PER_LONG * L1)
#define L3                         (BITS_PER_LONG * L2)

typedef struct TestHBitmapData {
    HBitmap       *hb;
    unsigned long *bits;
    size_t         size;
    size_t         old_size;
    int            granularity;
} TestHBitmapData;


/* Check that the HBitmap and the shadow bitmap contain the same data,
 * ignoring the same "first" bits.
 */
static void hbitmap_test_check(TestHBitmapData *data,
                               uint64_t first)
{
    uint64_t count = 0;
    size_t pos;
    int bit;
    HBitmapIter hbi;
    int64_t i, next;

    hbitmap_iter_init(&hbi, data->hb, first);

    i = first;
    for (;;) {
        next = hbitmap_iter_next(&hbi);
        if (next < 0) {
            next = data->size;
        }

        while (i < next) {
            pos = i >> LOG_BITS_PER_LONG;
            bit = i & (BITS_PER_LONG - 1);
            i++;
            g_assert_cmpint(data->bits[pos] & (1UL << bit), ==, 0);
        }

        if (next == data->size) {
            break;
        }

        pos = i >> LOG_BITS_PER_LONG;
        bit = i & (BITS_PER_LONG - 1);
        i++;
        count++;
        g_assert_cmpint(data->bits[pos] & (1UL << bit), !=, 0);
    }

    if (first == 0) {
        g_assert_cmpint(count << data->granularity, ==, hbitmap_count(data->hb));
    }
}

/* This is provided instead of a test setup function so that the sizes
   are kept in the test functions (and not in main()) */
static void hbitmap_test_init(TestHBitmapData *data,
                              uint64_t size, int granularity)
{
    size_t n;
    data->hb = hbitmap_alloc(size, granularity);

    n = DIV_ROUND_UP(size, BITS_PER_LONG);
    if (n == 0) {
        n = 1;
    }
    data->bits = g_new0(unsigned long, n);
    data->size = size;
    data->granularity = granularity;
    if (size) {
        hbitmap_test_check(data, 0);
    }
}

static inline size_t hbitmap_test_array_size(size_t bits)
{
    size_t n = DIV_ROUND_UP(bits, BITS_PER_LONG);
    return n ? n : 1;
}

static void hbitmap_test_truncate_impl(TestHBitmapData *data,
                                       size_t size)
{
    size_t n;
    size_t m;
    data->old_size = data->size;
    data->size = size;

    if (data->size == data->old_size) {
        return;
    }

    n = hbitmap_test_array_size(size);
    m = hbitmap_test_array_size(data->old_size);
    data->bits = g_realloc(data->bits, sizeof(unsigned long) * n);
    if (n > m) {
        memset(&data->bits[m], 0x00, sizeof(unsigned long) * (n - m));
    }

    /* If we shrink to an uneven multiple of sizeof(unsigned long),
     * scrub the leftover memory. */
    if (data->size < data->old_size) {
        m = size % (sizeof(unsigned long) * 8);
        if (m) {
            unsigned long mask = (1ULL << m) - 1;
            data->bits[n-1] &= mask;
        }
    }

    hbitmap_truncate(data->hb, size);
}

static void hbitmap_test_teardown(TestHBitmapData *data,
                                  const void *unused)
{
    if (data->hb) {
        hbitmap_free(data->hb);
        data->hb = NULL;
    }
    g_free(data->bits);
    data->bits = NULL;
}

/* Set a range in the HBitmap and in the shadow "simple" bitmap.
 * The two bitmaps are then tested against each other.
 */
static void hbitmap_test_set(TestHBitmapData *data,
                             uint64_t first, uint64_t count)
{
    hbitmap_set(data->hb, first, count);
    while (count-- != 0) {
        size_t pos = first >> LOG_BITS_PER_LONG;
        int bit = first & (BITS_PER_LONG - 1);
        first++;

        data->bits[pos] |= 1UL << bit;
    }

    if (data->granularity == 0) {
        hbitmap_test_check(data, 0);
    }
}

/* Reset a range in the HBitmap and in the shadow "simple" bitmap.
 */
static void hbitmap_test_reset(TestHBitmapData *data,
                               uint64_t first, uint64_t count)
{
    hbitmap_reset(data->hb, first, count);
    while (count-- != 0) {
        size_t pos = first >> LOG_BITS_PER_LONG;
        int bit = first & (BITS_PER_LONG - 1);
        first++;

        data->bits[pos] &= ~(1UL << bit);
    }

    if (data->granularity == 0) {
        hbitmap_test_check(data, 0);
    }
}

static void hbitmap_test_reset_all(TestHBitmapData *data)
{
    size_t n;

    hbitmap_reset_all(data->hb);

    n = DIV_ROUND_UP(data->size, BITS_PER_LONG);
    if (n == 0) {
        n = 1;
    }
    memset(data->bits, 0, n * sizeof(unsigned long));

    if (data->granularity == 0) {
        hbitmap_test_check(data, 0);
    }
}

static void hbitmap_test_check_get(TestHBitmapData *data)
{
    uint64_t count = 0;
    uint64_t i;

    for (i = 0; i < data->size; i++) {
        size_t pos = i >> LOG_BITS_PER_LONG;
        int bit = i & (BITS_PER_LONG - 1);
        unsigned long val = data->bits[pos] & (1UL << bit);
        count += hbitmap_get(data->hb, i);
        g_assert_cmpint(hbitmap_get(data->hb, i), ==, val != 0);
    }
    g_assert_cmpint(count, ==, hbitmap_count(data->hb));
}

static void test_hbitmap_zero(TestHBitmapData *data,
                               const void *unused)
{
    hbitmap_test_init(data, 0, 0);
}

static void test_hbitmap_unaligned(TestHBitmapData *data,
                                   const void *unused)
{
    hbitmap_test_init(data, L3 + 23, 0);
    hbitmap_test_set(data, 0, 1);
    hbitmap_test_set(data, L3 + 22, 1);
}

static void test_hbitmap_iter_empty(TestHBitmapData *data,
                                    const void *unused)
{
    hbitmap_test_init(data, L1, 0);
}

static void test_hbitmap_iter_partial(TestHBitmapData *data,
                                      const void *unused)
{
    hbitmap_test_init(data, L3, 0);
    hbitmap_test_set(data, 0, L3);
    hbitmap_test_check(data, 1);
    hbitmap_test_check(data, L1 - 1);
    hbitmap_test_check(data, L1);
    hbitmap_test_check(data, L1 * 2 - 1);
    hbitmap_test_check(data, L2 - 1);
    hbitmap_test_check(data, L2);
    hbitmap_test_check(data, L2 + 1);
    hbitmap_test_check(data, L2 + L1);
    hbitmap_test_check(data, L2 + L1 * 2 - 1);
    hbitmap_test_check(data, L2 * 2 - 1);
    hbitmap_test_check(data, L2 * 2);
    hbitmap_test_check(data, L2 * 2 + 1);
    hbitmap_test_check(data, L2 * 2 + L1);
    hbitmap_test_check(data, L2 * 2 + L1 * 2 - 1);
    hbitmap_test_check(data, L3 / 2);
}

static void test_hbitmap_set_all(TestHBitmapData *data,
                                 const void *unused)
{
    hbitmap_test_init(data, L3, 0);
    hbitmap_test_set(data, 0, L3);
}

static void test_hbitmap_get_all(TestHBitmapData *data,
                                 const void *unused)
{
    hbitmap_test_init(data, L3, 0);
    hbitmap_test_set(data, 0, L3);
    hbitmap_test_check_get(data);
}

static void test_hbitmap_get_some(TestHBitmapData *data,
                                  const void *unused)
{
    hbitmap_test_init(data, 2 * L2, 0);
    hbitmap_test_set(data, 10, 1);
    hbitmap_test_check_get(data);
    hbitmap_test_set(data, L1 - 1, 1);
    hbitmap_test_check_get(data);
    hbitmap_test_set(data, L1, 1);
    hbitmap_test_check_get(data);
    hbitmap_test_set(data, L2 - 1, 1);
    hbitmap_test_check_get(data);
    hbitmap_test_set(data, L2, 1);
    hbitmap_test_check_get(data);
}

static void test_hbitmap_set_one(TestHBitmapData *data,
                                 const void *unused)
{
    hbitmap_test_init(data, 2 * L2, 0);
    hbitmap_test_set(data, 10, 1);
    hbitmap_test_set(data, L1 - 1, 1);
    hbitmap_test_set(data, L1, 1);
    hbitmap_test_set(data, L2 - 1, 1);
    hbitmap_test_set(data, L2, 1);
}

static void test_hbitmap_set_two_elem(TestHBitmapData *data,
                                      const void *unused)
{
    hbitmap_test_init(data, 2 * L2, 0);
    hbitmap_test_set(data, L1 - 1, 2);
    hbitmap_test_set(data, L1 * 2 - 1, 4);
    hbitmap_test_set(data, L1 * 4, L1 + 1);
    hbitmap_test_set(data, L1 * 8 - 1, L1 + 1);
    hbitmap_test_set(data, L2 - 1, 2);
    hbitmap_test_set(data, L2 + L1 - 1, 8);
    hbitmap_test_set(data, L2 + L1 * 4, L1 + 1);
    hbitmap_test_set(data, L2 + L1 * 8 - 1, L1 + 1);
}

static void test_hbitmap_set(TestHBitmapData *data,
                             const void *unused)
{
    hbitmap_test_init(data, L3 * 2, 0);
    hbitmap_test_set(data, L1 - 1, L1 + 2);
    hbitmap_test_set(data, L1 * 3 - 1, L1 + 2);
    hbitmap_test_set(data, L1 * 5, L1 * 2 + 1);
    hbitmap_test_set(data, L1 * 8 - 1, L1 * 2 + 1);
    hbitmap_test_set(data, L2 - 1, L1 + 2);
    hbitmap_test_set(data, L2 + L1 * 2 - 1, L1 + 2);
    hbitmap_test_set(data, L2 + L1 * 4, L1 * 2 + 1);
    hbitmap_test_set(data, L2 + L1 * 7 - 1, L1 * 2 + 1);
    hbitmap_test_set(data, L2 * 2 - 1, L3 * 2 - L2 * 2);
}

static void test_hbitmap_set_twice(TestHBitmapData *data,
                                   const void *unused)
{
    hbitmap_test_init(data, L1 * 3, 0);
    hbitmap_test_set(data, 0, L1 * 3);
    hbitmap_test_set(data, L1, 1);
}

static void test_hbitmap_set_overlap(TestHBitmapData *data,
                                     const void *unused)
{
    hbitmap_test_init(data, L3 * 2, 0);
    hbitmap_test_set(data, L1 - 1, L1 + 2);
    hbitmap_test_set(data, L1 * 2 - 1, L1 * 2 + 2);
    hbitmap_test_set(data, 0, L1 * 3);
    hbitmap_test_set(data, L1 * 8 - 1, L2);
    hbitmap_test_set(data, L2, L1);
    hbitmap_test_set(data, L2 - L1 - 1, L1 * 8 + 2);
    hbitmap_test_set(data, L2, L3 - L2 + 1);
    hbitmap_test_set(data, L3 - L1, L1 * 3);
    hbitmap_test_set(data, L3 - 1, 3);
    hbitmap_test_set(data, L3 - 1, L2);
}

static void test_hbitmap_reset_empty(TestHBitmapData *data,
                                     const void *unused)
{
    hbitmap_test_init(data, L3, 0);
    hbitmap_test_reset(data, 0, L3);
}

static void test_hbitmap_reset(TestHBitmapData *data,
                               const void *unused)
{
    hbitmap_test_init(data, L3 * 2, 0);
    hbitmap_test_set(data, L1 - 1, L1 + 2);
    hbitmap_test_reset(data, L1 * 2 - 1, L1 * 2 + 2);
    hbitmap_test_set(data, 0, L1 * 3);
    hbitmap_test_reset(data, L1 * 8 - 1, L2);
    hbitmap_test_set(data, L2, L1);
    hbitmap_test_reset(data, L2 - L1 - 1, L1 * 8 + 2);
    hbitmap_test_set(data, L2, L3 - L2 + 1);
    hbitmap_test_reset(data, L3 - L1, L1 * 3);
    hbitmap_test_set(data, L3 - 1, 3);
    hbitmap_test_reset(data, L3 - 1, L2);
    hbitmap_test_set(data, 0, L3 * 2);
    hbitmap_test_reset(data, 0, L1);
    hbitmap_test_reset(data, 0, L2);
    hbitmap_test_reset(data, L3, L3);
    hbitmap_test_set(data, L3 / 2, L3);
}

static void test_hbitmap_reset_all(TestHBitmapData *data,
                                   const void *unused)
{
    hbitmap_test_init(data, L3 * 2, 0);
    hbitmap_test_set(data, L1 - 1, L1 + 2);
    hbitmap_test_reset_all(data);
    hbitmap_test_set(data, 0, L1 * 3);
    hbitmap_test_reset_all(data);
    hbitmap_test_set(data, L2, L1);
    hbitmap_test_reset_all(data);
    hbitmap_test_set(data, L2, L3 - L2 + 1);
    hbitmap_test_reset_all(data);
    hbitmap_test_set(data, L3 - 1, 3);
    hbitmap_test_reset_all(data);
    hbitmap_test_set(data, 0, L3 * 2);
    hbitmap_test_reset_all(data);
    hbitmap_test_set(data, L3 / 2, L3);
    hbitmap_test_reset_all(data);
}

static void test_hbitmap_granularity(TestHBitmapData *data,
                                     const void *unused)
{
    /* Note that hbitmap_test_check has to be invoked manually in this test.  */
    hbitmap_test_init(data, L1, 1);
    hbitmap_test_set(data, 0, 1);
    g_assert_cmpint(hbitmap_count(data->hb), ==, 2);
    hbitmap_test_check(data, 0);
    hbitmap_test_set(data, 2, 1);
    g_assert_cmpint(hbitmap_count(data->hb), ==, 4);
    hbitmap_test_check(data, 0);
    hbitmap_test_set(data, 0, 3);
    g_assert_cmpint(hbitmap_count(data->hb), ==, 4);
    hbitmap_test_reset(data, 0, 1);
    g_assert_cmpint(hbitmap_count(data->hb), ==, 2);
}

static void test_hbitmap_iter_granularity(TestHBitmapData *data,
                                          const void *unused)
{
    HBitmapIter hbi;

    /* Note that hbitmap_test_check has to be invoked manually in this test.  */
    hbitmap_test_init(data, 131072 << 7, 7);
    hbitmap_iter_init(&hbi, data->hb, 0);
    g_assert_cmpint(hbitmap_iter_next(&hbi), <, 0);

    hbitmap_test_set(data, ((L2 + L1 + 1) << 7) + 8, 8);
    hbitmap_iter_init(&hbi, data->hb, 0);
    g_assert_cmpint(hbitmap_iter_next(&hbi), ==, (L2 + L1 + 1) << 7);
    g_assert_cmpint(hbitmap_iter_next(&hbi), <, 0);

    hbitmap_iter_init(&hbi, data->hb, (L2 + L1 + 2) << 7);
    g_assert_cmpint(hbitmap_iter_next(&hbi), <, 0);

    hbitmap_test_set(data, (131072 << 7) - 8, 8);
    hbitmap_iter_init(&hbi, data->hb, 0);
    g_assert_cmpint(hbitmap_iter_next(&hbi), ==, (L2 + L1 + 1) << 7);
    g_assert_cmpint(hbitmap_iter_next(&hbi), ==, 131071 << 7);
    g_assert_cmpint(hbitmap_iter_next(&hbi), <, 0);

    hbitmap_iter_init(&hbi, data->hb, (L2 + L1 + 2) << 7);
    g_assert_cmpint(hbitmap_iter_next(&hbi), ==, 131071 << 7);
    g_assert_cmpint(hbitmap_iter_next(&hbi), <, 0);
}

static void hbitmap_test_set_boundary_bits(TestHBitmapData *data, ssize_t diff)
{
    size_t size = data->size;

    /* First bit */
    hbitmap_test_set(data, 0, 1);
    if (diff < 0) {
        /* Last bit in new, shortened map */
        hbitmap_test_set(data, size + diff - 1, 1);

        /* First bit to be truncated away */
        hbitmap_test_set(data, size + diff, 1);
    }
    /* Last bit */
    hbitmap_test_set(data, size - 1, 1);
    if (data->granularity == 0) {
        hbitmap_test_check_get(data);
    }
}

static void hbitmap_test_check_boundary_bits(TestHBitmapData *data)
{
    size_t size = MIN(data->size, data->old_size);

    if (data->granularity == 0) {
        hbitmap_test_check_get(data);
        hbitmap_test_check(data, 0);
    } else {
        /* If a granularity was set, note that every distinct
         * (bit >> granularity) value that was set will increase
         * the bit pop count by 2^granularity, not just 1.
         *
         * The hbitmap_test_check facility does not currently tolerate
         * non-zero granularities, so test the boundaries and the population
         * count manually.
         */
        g_assert(hbitmap_get(data->hb, 0));
        g_assert(hbitmap_get(data->hb, size - 1));
        g_assert_cmpint(2 << data->granularity, ==, hbitmap_count(data->hb));
    }
}

/* Generic truncate test. */
static void hbitmap_test_truncate(TestHBitmapData *data,
                                  size_t size,
                                  ssize_t diff,
                                  int granularity)
{
    hbitmap_test_init(data, size, granularity);
    hbitmap_test_set_boundary_bits(data, diff);
    hbitmap_test_truncate_impl(data, size + diff);
    hbitmap_test_check_boundary_bits(data);
}

static void test_hbitmap_truncate_nop(TestHBitmapData *data,
                                      const void *unused)
{
    hbitmap_test_truncate(data, L2, 0, 0);
}

/**
 * Grow by an amount smaller than the granularity, without crossing
 * a granularity alignment boundary. Effectively a NOP.
 */
static void test_hbitmap_truncate_grow_negligible(TestHBitmapData *data,
                                                  const void *unused)
{
    size_t size = L2 - 1;
    size_t diff = 1;
    int granularity = 1;

    hbitmap_test_truncate(data, size, diff, granularity);
}

/**
 * Shrink by an amount smaller than the granularity, without crossing
 * a granularity alignment boundary. Effectively a NOP.
 */
static void test_hbitmap_truncate_shrink_negligible(TestHBitmapData *data,
                                                    const void *unused)
{
    size_t size = L2;
    ssize_t diff = -1;
    int granularity = 1;

    hbitmap_test_truncate(data, size, diff, granularity);
}

/**
 * Grow by an amount smaller than the granularity, but crossing over
 * a granularity alignment boundary.
 */
static void test_hbitmap_truncate_grow_tiny(TestHBitmapData *data,
                                            const void *unused)
{
    size_t size = L2 - 2;
    ssize_t diff = 1;
    int granularity = 1;

    hbitmap_test_truncate(data, size, diff, granularity);
}

/**
 * Shrink by an amount smaller than the granularity, but crossing over
 * a granularity alignment boundary.
 */
static void test_hbitmap_truncate_shrink_tiny(TestHBitmapData *data,
                                              const void *unused)
{
    size_t size = L2 - 1;
    ssize_t diff = -1;
    int granularity = 1;

    hbitmap_test_truncate(data, size, diff, granularity);
}

/**
 * Grow by an amount smaller than sizeof(long), and not crossing over
 * a sizeof(long) alignment boundary.
 */
static void test_hbitmap_truncate_grow_small(TestHBitmapData *data,
                                             const void *unused)
{
    size_t size = L2 + 1;
    size_t diff = sizeof(long) / 2;

    hbitmap_test_truncate(data, size, diff, 0);
}

/**
 * Shrink by an amount smaller than sizeof(long), and not crossing over
 * a sizeof(long) alignment boundary.
 */
static void test_hbitmap_truncate_shrink_small(TestHBitmapData *data,
                                               const void *unused)
{
    size_t size = L2;
    size_t diff = sizeof(long) / 2;

    hbitmap_test_truncate(data, size, -diff, 0);
}

/**
 * Grow by an amount smaller than sizeof(long), while crossing over
 * a sizeof(long) alignment boundary.
 */
static void test_hbitmap_truncate_grow_medium(TestHBitmapData *data,
                                              const void *unused)
{
    size_t size = L2 - 1;
    size_t diff = sizeof(long) / 2;

    hbitmap_test_truncate(data, size, diff, 0);
}

/**
 * Shrink by an amount smaller than sizeof(long), while crossing over
 * a sizeof(long) alignment boundary.
 */
static void test_hbitmap_truncate_shrink_medium(TestHBitmapData *data,
                                                const void *unused)
{
    size_t size = L2 + 1;
    size_t diff = sizeof(long) / 2;

    hbitmap_test_truncate(data, size, -diff, 0);
}

/**
 * Grow by an amount larger than sizeof(long).
 */
static void test_hbitmap_truncate_grow_large(TestHBitmapData *data,
                                             const void *unused)
{
    size_t size = L2;
    size_t diff = 8 * sizeof(long);

    hbitmap_test_truncate(data, size, diff, 0);
}

/**
 * Shrink by an amount larger than sizeof(long).
 */
static void test_hbitmap_truncate_shrink_large(TestHBitmapData *data,
                                               const void *unused)
{
    size_t size = L2;
    size_t diff = 8 * sizeof(long);

    hbitmap_test_truncate(data, size, -diff, 0);
}

static void hbitmap_test_add(const char *testpath,
                                   void (*test_func)(TestHBitmapData *data, const void *user_data))
{
    g_test_add(testpath, TestHBitmapData, NULL, NULL, test_func,
               hbitmap_test_teardown);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    hbitmap_test_add("/hbitmap/size/0", test_hbitmap_zero);
    hbitmap_test_add("/hbitmap/size/unaligned", test_hbitmap_unaligned);
    hbitmap_test_add("/hbitmap/iter/empty", test_hbitmap_iter_empty);
    hbitmap_test_add("/hbitmap/iter/partial", test_hbitmap_iter_partial);
    hbitmap_test_add("/hbitmap/iter/granularity", test_hbitmap_iter_granularity);
    hbitmap_test_add("/hbitmap/get/all", test_hbitmap_get_all);
    hbitmap_test_add("/hbitmap/get/some", test_hbitmap_get_some);
    hbitmap_test_add("/hbitmap/set/all", test_hbitmap_set_all);
    hbitmap_test_add("/hbitmap/set/one", test_hbitmap_set_one);
    hbitmap_test_add("/hbitmap/set/two-elem", test_hbitmap_set_two_elem);
    hbitmap_test_add("/hbitmap/set/general", test_hbitmap_set);
    hbitmap_test_add("/hbitmap/set/twice", test_hbitmap_set_twice);
    hbitmap_test_add("/hbitmap/set/overlap", test_hbitmap_set_overlap);
    hbitmap_test_add("/hbitmap/reset/empty", test_hbitmap_reset_empty);
    hbitmap_test_add("/hbitmap/reset/general", test_hbitmap_reset);
    hbitmap_test_add("/hbitmap/reset/all", test_hbitmap_reset_all);
    hbitmap_test_add("/hbitmap/granularity", test_hbitmap_granularity);

    hbitmap_test_add("/hbitmap/truncate/nop", test_hbitmap_truncate_nop);
    hbitmap_test_add("/hbitmap/truncate/grow/negligible",
                     test_hbitmap_truncate_grow_negligible);
    hbitmap_test_add("/hbitmap/truncate/shrink/negligible",
                     test_hbitmap_truncate_shrink_negligible);
    hbitmap_test_add("/hbitmap/truncate/grow/tiny",
                     test_hbitmap_truncate_grow_tiny);
    hbitmap_test_add("/hbitmap/truncate/shrink/tiny",
                     test_hbitmap_truncate_shrink_tiny);
    hbitmap_test_add("/hbitmap/truncate/grow/small",
                     test_hbitmap_truncate_grow_small);
    hbitmap_test_add("/hbitmap/truncate/shrink/small",
                     test_hbitmap_truncate_shrink_small);
    hbitmap_test_add("/hbitmap/truncate/grow/medium",
                     test_hbitmap_truncate_grow_medium);
    hbitmap_test_add("/hbitmap/truncate/shrink/medium",
                     test_hbitmap_truncate_shrink_medium);
    hbitmap_test_add("/hbitmap/truncate/grow/large",
                     test_hbitmap_truncate_grow_large);
    hbitmap_test_add("/hbitmap/truncate/shrink/large",
                     test_hbitmap_truncate_shrink_large);
    g_test_run();

    return 0;
}
