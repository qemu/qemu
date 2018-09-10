/*
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/qht.h"
#include "qemu/rcu.h"

#define N 5000

static struct qht ht;
static int32_t arr[N * 2];

static bool is_equal(const void *ap, const void *bp)
{
    const int32_t *a = ap;
    const int32_t *b = bp;

    return *a == *b;
}

static void insert(int a, int b)
{
    int i;

    for (i = a; i < b; i++) {
        uint32_t hash;
        void *existing;
        bool inserted;

        arr[i] = i;
        hash = i;

        inserted = qht_insert(&ht, &arr[i], hash, NULL);
        g_assert_true(inserted);
        inserted = qht_insert(&ht, &arr[i], hash, &existing);
        g_assert_false(inserted);
        g_assert_true(existing == &arr[i]);
    }
}

static void do_rm(int init, int end, bool exist)
{
    int i;

    for (i = init; i < end; i++) {
        uint32_t hash;

        hash = arr[i];
        if (exist) {
            g_assert_true(qht_remove(&ht, &arr[i], hash));
        } else {
            g_assert_false(qht_remove(&ht, &arr[i], hash));
        }
    }
}

static void rm(int init, int end)
{
    do_rm(init, end, true);
}

static void rm_nonexist(int init, int end)
{
    do_rm(init, end, false);
}

static void check(int a, int b, bool expected)
{
    struct qht_stats stats;
    int i;

    rcu_read_lock();
    for (i = a; i < b; i++) {
        void *p;
        uint32_t hash;
        int32_t val;

        val = i;
        hash = i;
        /* test both lookup variants; results should be the same */
        if (i % 2) {
            p = qht_lookup(&ht, &val, hash);
        } else {
            p = qht_lookup_custom(&ht, &val, hash, is_equal);
        }
        g_assert_true(!!p == expected);
    }
    rcu_read_unlock();

    qht_statistics_init(&ht, &stats);
    if (stats.used_head_buckets) {
        g_assert_cmpfloat(qdist_avg(&stats.chain), >=, 1.0);
    }
    g_assert_cmpuint(stats.head_buckets, >, 0);
    qht_statistics_destroy(&stats);
}

static void count_func(void *p, uint32_t hash, void *userp)
{
    unsigned int *curr = userp;

    (*curr)++;
}

static void check_n(size_t expected)
{
    struct qht_stats stats;

    qht_statistics_init(&ht, &stats);
    g_assert_cmpuint(stats.entries, ==, expected);
    qht_statistics_destroy(&stats);
}

static void iter_check(unsigned int count)
{
    unsigned int curr = 0;

    qht_iter(&ht, count_func, &curr);
    g_assert_cmpuint(curr, ==, count);
}

static void sum_func(void *p, uint32_t hash, void *userp)
{
    uint32_t *sum = userp;
    uint32_t a = *(uint32_t *)p;

    *sum += a;
}

static void iter_sum_check(unsigned int expected)
{
    unsigned int sum = 0;

    qht_iter(&ht, sum_func, &sum);
    g_assert_cmpuint(sum, ==, expected);
}

static bool rm_mod_func(void *p, uint32_t hash, void *userp)
{
    uint32_t a = *(uint32_t *)p;
    unsigned int mod = *(unsigned int *)userp;

    return a % mod == 0;
}

static void iter_rm_mod(unsigned int mod)
{
    qht_iter_remove(&ht, rm_mod_func, &mod);
}

static void iter_rm_mod_check(unsigned int mod)
{
    unsigned int expected = 0;
    unsigned int i;

    for (i = 0; i < N; i++) {
        if (i % mod == 0) {
            continue;
        }
        expected += i;
    }
    iter_sum_check(expected);
}

static void qht_do_test(unsigned int mode, size_t init_entries)
{
    /* under KVM we might fetch stats from an uninitialized qht */
    check_n(0);

    qht_init(&ht, is_equal, 0, mode);
    rm_nonexist(0, 4);
    /*
     * Test that we successfully delete the last element in a bucket.
     * This is a hard-to-reach code path when resizing is on, but without
     * resizing we can easily hit it if init_entries <= 1.
     * Given that the number of elements per bucket can be 4 or 6 depending on
     * the host's pointer size, test the removal of the 4th and 6th elements.
     */
    insert(0, 4);
    rm_nonexist(5, 6);
    rm(3, 4);
    check_n(3);
    insert(3, 6);
    rm(5, 6);
    check_n(5);
    rm_nonexist(7, 8);
    iter_rm_mod(1);

    if (!(mode & QHT_MODE_AUTO_RESIZE)) {
        qht_resize(&ht, init_entries * 4 + 4);
    }

    check_n(0);
    rm_nonexist(0, 10);
    insert(0, N);
    check(0, N, true);
    check_n(N);
    check(-N, -1, false);
    iter_check(N);

    rm(101, 102);
    check_n(N - 1);
    insert(N, N * 2);
    check_n(N + N - 1);
    rm(N, N * 2);
    check_n(N - 1);
    insert(101, 102);
    check_n(N);

    rm(10, 200);
    check_n(N - 190);
    insert(150, 200);
    check_n(N - 190 + 50);
    insert(10, 150);
    check_n(N);

    qht_reset(&ht);
    insert(0, N);
    rm_nonexist(N, N + 32);
    iter_rm_mod(10);
    iter_rm_mod_check(10);
    check_n(N * 9 / 10);
    qht_reset_size(&ht, 0);
    check_n(0);
    check(0, N, false);

    qht_destroy(&ht);
}

static void qht_test(unsigned int mode)
{
    qht_do_test(mode, 0);
    qht_do_test(mode, 1);
    qht_do_test(mode, 2);
    qht_do_test(mode, 8);
    qht_do_test(mode, 16);
    qht_do_test(mode, 8192);
    qht_do_test(mode, 16384);
}

static void test_default(void)
{
    qht_test(0);
}

static void test_resize(void)
{
    qht_test(QHT_MODE_AUTO_RESIZE);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qht/mode/default", test_default);
    g_test_add_func("/qht/mode/resize", test_resize);
    return g_test_run();
}
