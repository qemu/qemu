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
#include "qemu/bitmap.h"
#include "block/block.h"

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
    hbitmap_test_reset(data, 0, 2);
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

static void test_hbitmap_serialize_align(TestHBitmapData *data,
                                         const void *unused)
{
    int r;

    hbitmap_test_init(data, L3 * 2, 3);
    g_assert(hbitmap_is_serializable(data->hb));

    r = hbitmap_serialization_align(data->hb);
    g_assert_cmpint(r, ==, 64 << 3);
}

static void hbitmap_test_serialize_range(TestHBitmapData *data,
                                         uint8_t *buf, size_t buf_size,
                                         uint64_t pos, uint64_t count)
{
    size_t i;
    unsigned long *el = (unsigned long *)buf;

    assert(hbitmap_granularity(data->hb) == 0);
    hbitmap_reset_all(data->hb);
    memset(buf, 0, buf_size);
    if (count) {
        hbitmap_set(data->hb, pos, count);
    }

    g_assert(hbitmap_is_serializable(data->hb));
    hbitmap_serialize_part(data->hb, buf, 0, data->size);

    /* Serialized buffer is inherently LE, convert it back manually to test */
    for (i = 0; i < buf_size / sizeof(unsigned long); i++) {
        el[i] = (BITS_PER_LONG == 32 ? le32_to_cpu(el[i]) : le64_to_cpu(el[i]));
    }

    for (i = 0; i < data->size; i++) {
        int is_set = test_bit(i, (unsigned long *)buf);
        if (i >= pos && i < pos + count) {
            g_assert(is_set);
        } else {
            g_assert(!is_set);
        }
    }

    /* Re-serialize for deserialization testing */
    memset(buf, 0, buf_size);
    hbitmap_serialize_part(data->hb, buf, 0, data->size);
    hbitmap_reset_all(data->hb);

    g_assert(hbitmap_is_serializable(data->hb));
    hbitmap_deserialize_part(data->hb, buf, 0, data->size, true);

    for (i = 0; i < data->size; i++) {
        int is_set = hbitmap_get(data->hb, i);
        if (i >= pos && i < pos + count) {
            g_assert(is_set);
        } else {
            g_assert(!is_set);
        }
    }
}

static void test_hbitmap_serialize_basic(TestHBitmapData *data,
                                         const void *unused)
{
    int i, j;
    size_t buf_size;
    uint8_t *buf;
    uint64_t positions[] = { 0, 1, L1 - 1, L1, L2 - 1, L2, L2 + 1, L3 - 1 };
    int num_positions = ARRAY_SIZE(positions);

    hbitmap_test_init(data, L3, 0);
    g_assert(hbitmap_is_serializable(data->hb));
    buf_size = hbitmap_serialization_size(data->hb, 0, data->size);
    buf = g_malloc0(buf_size);

    for (i = 0; i < num_positions; i++) {
        for (j = 0; j < num_positions; j++) {
            hbitmap_test_serialize_range(data, buf, buf_size,
                                         positions[i],
                                         MIN(positions[j], L3 - positions[i]));
        }
    }

    g_free(buf);
}

static void test_hbitmap_serialize_part(TestHBitmapData *data,
                                        const void *unused)
{
    int i, j, k;
    size_t buf_size;
    uint8_t *buf;
    uint64_t positions[] = { 0, 1, L1 - 1, L1, L2 - 1, L2, L2 + 1, L3 - 1 };
    int num_positions = ARRAY_SIZE(positions);

    hbitmap_test_init(data, L3, 0);
    buf_size = L2;
    buf = g_malloc0(buf_size);

    for (i = 0; i < num_positions; i++) {
        hbitmap_set(data->hb, positions[i], 1);
    }

    g_assert(hbitmap_is_serializable(data->hb));

    for (i = 0; i < data->size; i += buf_size) {
        unsigned long *el = (unsigned long *)buf;
        hbitmap_serialize_part(data->hb, buf, i, buf_size);
        for (j = 0; j < buf_size / sizeof(unsigned long); j++) {
            el[j] = (BITS_PER_LONG == 32 ? le32_to_cpu(el[j]) : le64_to_cpu(el[j]));
        }

        for (j = 0; j < buf_size; j++) {
            bool should_set = false;
            for (k = 0; k < num_positions; k++) {
                if (positions[k] == j + i) {
                    should_set = true;
                    break;
                }
            }
            g_assert_cmpint(should_set, ==, test_bit(j, (unsigned long *)buf));
        }
    }

    g_free(buf);
}

static void test_hbitmap_serialize_zeroes(TestHBitmapData *data,
                                          const void *unused)
{
    int i;
    HBitmapIter iter;
    int64_t next;
    uint64_t min_l1 = MAX(L1, 64);
    uint64_t positions[] = { 0, min_l1, L2, L3 - min_l1};
    int num_positions = ARRAY_SIZE(positions);

    hbitmap_test_init(data, L3, 0);

    for (i = 0; i < num_positions; i++) {
        hbitmap_set(data->hb, positions[i], L1);
    }

    g_assert(hbitmap_is_serializable(data->hb));

    for (i = 0; i < num_positions; i++) {
        hbitmap_deserialize_zeroes(data->hb, positions[i], min_l1, true);
        hbitmap_iter_init(&iter, data->hb, 0);
        next = hbitmap_iter_next(&iter);
        if (i == num_positions - 1) {
            g_assert_cmpint(next, ==, -1);
        } else {
            g_assert_cmpint(next, ==, positions[i + 1]);
        }
    }
}

static void hbitmap_test_add(const char *testpath,
                                   void (*test_func)(TestHBitmapData *data, const void *user_data))
{
    g_test_add(testpath, TestHBitmapData, NULL, NULL, test_func,
               hbitmap_test_teardown);
}

static void test_hbitmap_iter_and_reset(TestHBitmapData *data,
                                        const void *unused)
{
    HBitmapIter hbi;

    hbitmap_test_init(data, L1 * 2, 0);
    hbitmap_set(data->hb, 0, data->size);

    hbitmap_iter_init(&hbi, data->hb, BITS_PER_LONG - 1);

    hbitmap_iter_next(&hbi);

    hbitmap_reset_all(data->hb);
    hbitmap_iter_next(&hbi);
}

static void test_hbitmap_next_x_check_range(TestHBitmapData *data,
                                            int64_t start,
                                            int64_t count)
{
    int64_t next_zero = hbitmap_next_zero(data->hb, start, count);
    int64_t next_dirty = hbitmap_next_dirty(data->hb, start, count);
    int64_t next;
    int64_t end = start >= data->size || data->size - start < count ?
                data->size : start + count;
    bool first_bit = hbitmap_get(data->hb, start);

    for (next = start;
         next < end && hbitmap_get(data->hb, next) == first_bit;
         next++)
    {
        ;
    }

    if (next == end) {
        next = -1;
    }

    g_assert_cmpint(next_dirty, ==, first_bit ? start : next);
    g_assert_cmpint(next_zero, ==, first_bit ? next : start);
}

static void test_hbitmap_next_x_check(TestHBitmapData *data, int64_t start)
{
    test_hbitmap_next_x_check_range(data, start, INT64_MAX);
}

static void test_hbitmap_next_x_do(TestHBitmapData *data, int granularity)
{
    hbitmap_test_init(data, L3, granularity);
    test_hbitmap_next_x_check(data, 0);
    test_hbitmap_next_x_check(data, L3 - 1);
    test_hbitmap_next_x_check_range(data, 0, 1);
    test_hbitmap_next_x_check_range(data, L3 - 1, 1);

    hbitmap_set(data->hb, L2, 1);
    test_hbitmap_next_x_check(data, 0);
    test_hbitmap_next_x_check(data, L2 - 1);
    test_hbitmap_next_x_check(data, L2);
    test_hbitmap_next_x_check(data, L2 + 1);
    test_hbitmap_next_x_check_range(data, 0, 1);
    test_hbitmap_next_x_check_range(data, 0, L2);
    test_hbitmap_next_x_check_range(data, L2 - 1, 1);
    test_hbitmap_next_x_check_range(data, L2 - 1, 2);
    test_hbitmap_next_x_check_range(data, L2, 1);
    test_hbitmap_next_x_check_range(data, L2 + 1, 1);

    hbitmap_set(data->hb, L2 + 5, L1);
    test_hbitmap_next_x_check(data, 0);
    test_hbitmap_next_x_check(data, L2 - L1);
    test_hbitmap_next_x_check(data, L2 + 1);
    test_hbitmap_next_x_check(data, L2 + 2);
    test_hbitmap_next_x_check(data, L2 + 5);
    test_hbitmap_next_x_check(data, L2 + L1 - 1);
    test_hbitmap_next_x_check(data, L2 + L1);
    test_hbitmap_next_x_check(data, L2 + L1 + 1);
    test_hbitmap_next_x_check_range(data, L2 - 2, L1);
    test_hbitmap_next_x_check_range(data, L2, 4);
    test_hbitmap_next_x_check_range(data, L2, 6);
    test_hbitmap_next_x_check_range(data, L2 + 1, 3);
    test_hbitmap_next_x_check_range(data, L2 + 4, L1);
    test_hbitmap_next_x_check_range(data, L2 + 5, L1);
    test_hbitmap_next_x_check_range(data, L2 + 5 + L1 - 1, 1);
    test_hbitmap_next_x_check_range(data, L2 + 5 + L1, 1);
    test_hbitmap_next_x_check_range(data, L2 + 5 + L1 + 1, 1);

    hbitmap_set(data->hb, L2 * 2, L3 - L2 * 2);
    test_hbitmap_next_x_check(data, L2 * 2 - L1);
    test_hbitmap_next_x_check(data, L2 * 2 - 2);
    test_hbitmap_next_x_check(data, L2 * 2 - 1);
    test_hbitmap_next_x_check(data, L2 * 2);
    test_hbitmap_next_x_check(data, L2 * 2 + 1);
    test_hbitmap_next_x_check(data, L2 * 2 + L1);
    test_hbitmap_next_x_check(data, L3 - 1);
    test_hbitmap_next_x_check_range(data, L2 * 2 - L1, L1 + 1);
    test_hbitmap_next_x_check_range(data, L2 * 2, L2);

    hbitmap_set(data->hb, 0, L3);
    test_hbitmap_next_x_check(data, 0);
}

static void test_hbitmap_next_x_0(TestHBitmapData *data, const void *unused)
{
    test_hbitmap_next_x_do(data, 0);
}

static void test_hbitmap_next_x_4(TestHBitmapData *data, const void *unused)
{
    test_hbitmap_next_x_do(data, 4);
}

static void test_hbitmap_next_x_after_truncate(TestHBitmapData *data,
                                               const void *unused)
{
    hbitmap_test_init(data, L1, 0);
    hbitmap_test_truncate_impl(data, L1 * 2);
    hbitmap_set(data->hb, 0, L1);
    test_hbitmap_next_x_check(data, 0);
}

static void test_hbitmap_next_dirty_area_check_limited(TestHBitmapData *data,
                                                       int64_t offset,
                                                       int64_t count,
                                                       int64_t max_dirty)
{
    int64_t off1, off2;
    int64_t len1 = 0, len2;
    bool ret1, ret2;
    int64_t end;

    ret1 = hbitmap_next_dirty_area(data->hb,
            offset, count == INT64_MAX ? INT64_MAX : offset + count, max_dirty,
            &off1, &len1);

    end = offset > data->size || data->size - offset < count ? data->size :
                                                               offset + count;

    for (off2 = offset; off2 < end && !hbitmap_get(data->hb, off2); off2++) {
        ;
    }

    for (len2 = 1; (off2 + len2 < end && len2 < max_dirty &&
                    hbitmap_get(data->hb, off2 + len2)); len2++)
    {
        ;
    }

    ret2 = off2 < end;
    g_assert_cmpint(ret1, ==, ret2);

    if (ret2) {
        g_assert_cmpint(off1, ==, off2);
        g_assert_cmpint(len1, ==, len2);
    }
}

static void test_hbitmap_next_dirty_area_check(TestHBitmapData *data,
                                               int64_t offset, int64_t count)
{
    test_hbitmap_next_dirty_area_check_limited(data, offset, count, INT64_MAX);
}

static void test_hbitmap_next_dirty_area_do(TestHBitmapData *data,
                                            int granularity)
{
    hbitmap_test_init(data, L3, granularity);
    test_hbitmap_next_dirty_area_check(data, 0, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, 0, 1);
    test_hbitmap_next_dirty_area_check(data, L3 - 1, 1);
    test_hbitmap_next_dirty_area_check_limited(data, 0, INT64_MAX, 1);

    hbitmap_set(data->hb, L2, 1);
    test_hbitmap_next_dirty_area_check(data, 0, 1);
    test_hbitmap_next_dirty_area_check(data, 0, L2);
    test_hbitmap_next_dirty_area_check(data, 0, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2 - 1, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2 - 1, 1);
    test_hbitmap_next_dirty_area_check(data, L2 - 1, 2);
    test_hbitmap_next_dirty_area_check(data, L2 - 1, 3);
    test_hbitmap_next_dirty_area_check(data, L2, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2, 1);
    test_hbitmap_next_dirty_area_check(data, L2 + 1, 1);
    test_hbitmap_next_dirty_area_check_limited(data, 0, INT64_MAX, 1);
    test_hbitmap_next_dirty_area_check_limited(data, L2 - 1, 2, 1);

    hbitmap_set(data->hb, L2 + 5, L1);
    test_hbitmap_next_dirty_area_check(data, 0, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2 - 2, 8);
    test_hbitmap_next_dirty_area_check(data, L2 + 1, 5);
    test_hbitmap_next_dirty_area_check(data, L2 + 1, 3);
    test_hbitmap_next_dirty_area_check(data, L2 + 4, L1);
    test_hbitmap_next_dirty_area_check(data, L2 + 5, L1);
    test_hbitmap_next_dirty_area_check(data, L2 + 7, L1);
    test_hbitmap_next_dirty_area_check(data, L2 + L1, L1);
    test_hbitmap_next_dirty_area_check(data, L2, 0);
    test_hbitmap_next_dirty_area_check(data, L2 + 1, 0);
    test_hbitmap_next_dirty_area_check_limited(data, L2 + 3, INT64_MAX, 3);
    test_hbitmap_next_dirty_area_check_limited(data, L2 + 3, 7, 10);

    hbitmap_set(data->hb, L2 * 2, L3 - L2 * 2);
    test_hbitmap_next_dirty_area_check(data, 0, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2 + 1, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2 + 5 + L1 - 1, INT64_MAX);
    test_hbitmap_next_dirty_area_check(data, L2 + 5 + L1, 5);
    test_hbitmap_next_dirty_area_check(data, L2 * 2 - L1, L1 + 1);
    test_hbitmap_next_dirty_area_check(data, L2 * 2, L2);
    test_hbitmap_next_dirty_area_check_limited(data, L2 * 2 + 1, INT64_MAX, 5);
    test_hbitmap_next_dirty_area_check_limited(data, L2 * 2 + 1, 10, 5);
    test_hbitmap_next_dirty_area_check_limited(data, L2 * 2 + 1, 2, 5);

    hbitmap_set(data->hb, 0, L3);
    test_hbitmap_next_dirty_area_check(data, 0, INT64_MAX);
}

static void test_hbitmap_next_dirty_area_0(TestHBitmapData *data,
                                           const void *unused)
{
    test_hbitmap_next_dirty_area_do(data, 0);
}

static void test_hbitmap_next_dirty_area_1(TestHBitmapData *data,
                                           const void *unused)
{
    test_hbitmap_next_dirty_area_do(data, 1);
}

static void test_hbitmap_next_dirty_area_4(TestHBitmapData *data,
                                           const void *unused)
{
    test_hbitmap_next_dirty_area_do(data, 4);
}

static void test_hbitmap_next_dirty_area_after_truncate(TestHBitmapData *data,
                                                        const void *unused)
{
    hbitmap_test_init(data, L1, 0);
    hbitmap_test_truncate_impl(data, L1 * 2);
    hbitmap_set(data->hb, L1 + 1, 1);
    test_hbitmap_next_dirty_area_check(data, 0, INT64_MAX);
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

    hbitmap_test_add("/hbitmap/serialize/align",
                     test_hbitmap_serialize_align);
    hbitmap_test_add("/hbitmap/serialize/basic",
                     test_hbitmap_serialize_basic);
    hbitmap_test_add("/hbitmap/serialize/part",
                     test_hbitmap_serialize_part);
    hbitmap_test_add("/hbitmap/serialize/zeroes",
                     test_hbitmap_serialize_zeroes);

    hbitmap_test_add("/hbitmap/iter/iter_and_reset",
                     test_hbitmap_iter_and_reset);

    hbitmap_test_add("/hbitmap/next_zero/next_x_0",
                     test_hbitmap_next_x_0);
    hbitmap_test_add("/hbitmap/next_zero/next_x_4",
                     test_hbitmap_next_x_4);
    hbitmap_test_add("/hbitmap/next_zero/next_x_after_truncate",
                     test_hbitmap_next_x_after_truncate);

    hbitmap_test_add("/hbitmap/next_dirty_area/next_dirty_area_0",
                     test_hbitmap_next_dirty_area_0);
    hbitmap_test_add("/hbitmap/next_dirty_area/next_dirty_area_1",
                     test_hbitmap_next_dirty_area_1);
    hbitmap_test_add("/hbitmap/next_dirty_area/next_dirty_area_4",
                     test_hbitmap_next_dirty_area_4);
    hbitmap_test_add("/hbitmap/next_dirty_area/next_dirty_area_after_truncate",
                     test_hbitmap_next_dirty_area_after_truncate);

    g_test_run();

    return 0;
}
