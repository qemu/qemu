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

static bool is_equal(const void *obj, const void *userp)
{
    const int32_t *a = obj;
    const int32_t *b = userp;

    return *a == *b;
}

static void insert(int a, int b)
{
    int i;

    for (i = a; i < b; i++) {
        uint32_t hash;

        arr[i] = i;
        hash = i;

        qht_insert(&ht, &arr[i], hash);
    }
}

static void rm(int init, int end)
{
    int i;

    for (i = init; i < end; i++) {
        uint32_t hash;

        hash = arr[i];
        g_assert_true(qht_remove(&ht, &arr[i], hash));
    }
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
        p = qht_lookup(&ht, is_equal, &val, hash);
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

static void count_func(struct qht *ht, void *p, uint32_t hash, void *userp)
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

static void qht_do_test(unsigned int mode, size_t init_entries)
{
    /* under KVM we might fetch stats from an uninitialized qht */
    check_n(0);

    qht_init(&ht, 0, mode);

    check_n(0);
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

    rm(1, 2);
    check_n(N - 1);
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
