/*
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/qdist.h"

#include <math.h>

struct entry_desc {
    double x;
    unsigned long count;

    /* 0 prints a space, 1-8 prints from qdist_blocks[] */
    int fill_code;
};

/* See: https://en.wikipedia.org/wiki/Block_Elements */
static const gunichar qdist_blocks[] = {
    0x2581,
    0x2582,
    0x2583,
    0x2584,
    0x2585,
    0x2586,
    0x2587,
    0x2588
};

#define QDIST_NR_BLOCK_CODES ARRAY_SIZE(qdist_blocks)

static char *pr_hist(const struct entry_desc *darr, size_t n)
{
    GString *s = g_string_new("");
    size_t i;

    for (i = 0; i < n; i++) {
        int fill = darr[i].fill_code;

        if (fill) {
            assert(fill <= QDIST_NR_BLOCK_CODES);
            g_string_append_unichar(s, qdist_blocks[fill - 1]);
        } else {
            g_string_append_c(s, ' ');
        }
    }
    return g_string_free(s, FALSE);
}

static void
histogram_check(const struct qdist *dist, const struct entry_desc *darr,
                size_t n, size_t n_bins)
{
    char *pr = qdist_pr_plain(dist, n_bins);
    char *str = pr_hist(darr, n);

    g_assert_cmpstr(pr, ==, str);
    g_free(pr);
    g_free(str);
}

static void histogram_check_single_full(const struct qdist *dist, size_t n_bins)
{
    struct entry_desc desc = { .fill_code = 8 };

    histogram_check(dist, &desc, 1, n_bins);
}

static void
entries_check(const struct qdist *dist, const struct entry_desc *darr, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        struct qdist_entry *e = &dist->entries[i];

        g_assert_cmpuint(e->count, ==, darr[i].count);
    }
}

static void
entries_insert(struct qdist *dist, const struct entry_desc *darr, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        qdist_add(dist, darr[i].x, darr[i].count);
    }
}

static void do_test_bin(const struct entry_desc *a, size_t n_a,
                        const struct entry_desc *b, size_t n_b)
{
    struct qdist qda;
    struct qdist qdb;

    qdist_init(&qda);

    entries_insert(&qda, a, n_a);
    qdist_inc(&qda, a[0].x);
    qdist_add(&qda, a[0].x, -1);

    g_assert_cmpuint(qdist_unique_entries(&qda), ==, n_a);
    g_assert_cmpfloat(qdist_xmin(&qda), ==, a[0].x);
    g_assert_cmpfloat(qdist_xmax(&qda), ==, a[n_a - 1].x);
    histogram_check(&qda, a, n_a, 0);
    histogram_check(&qda, a, n_a, n_a);

    qdist_bin__internal(&qdb, &qda, n_b);
    g_assert_cmpuint(qdb.n, ==, n_b);
    entries_check(&qdb, b, n_b);
    g_assert_cmpuint(qdist_sample_count(&qda), ==, qdist_sample_count(&qdb));
    /*
     * No histogram_check() for $qdb, since we'd rebin it and that is a bug.
     * Instead, regenerate it from $qda.
     */
    histogram_check(&qda, b, n_b, n_b);

    qdist_destroy(&qdb);
    qdist_destroy(&qda);
}

static void do_test_pr(uint32_t opt)
{
    static const struct entry_desc desc[] = {
        [0] = { 1, 900, 8 },
        [1] = { 2, 1, 1 },
        [2] = { 3, 2, 1 }
    };
    static const char border[] = "|";
    const char *llabel = NULL;
    const char *rlabel = NULL;
    struct qdist dist;
    GString *s;
    char *str;
    char *pr;
    size_t n;

    n = ARRAY_SIZE(desc);
    qdist_init(&dist);

    entries_insert(&dist, desc, n);
    histogram_check(&dist, desc, n, 0);

    s = g_string_new("");

    if (opt & QDIST_PR_LABELS) {
        unsigned int lopts = opt & (QDIST_PR_NODECIMAL |
                                    QDIST_PR_PERCENT |
                                    QDIST_PR_100X |
                                    QDIST_PR_NOBINRANGE);

        if (lopts == 0) {
            llabel = "[1.0,1.7)";
            rlabel = "[2.3,3.0]";
        } else if (lopts == QDIST_PR_NODECIMAL) {
            llabel = "[1,2)";
            rlabel = "[2,3]";
        } else if (lopts == (QDIST_PR_PERCENT | QDIST_PR_NODECIMAL)) {
            llabel = "[1,2)%";
            rlabel = "[2,3]%";
        } else if (lopts == QDIST_PR_100X) {
            llabel = "[100.0,166.7)";
            rlabel = "[233.3,300.0]";
        } else if (lopts == (QDIST_PR_NOBINRANGE | QDIST_PR_NODECIMAL)) {
            llabel = "1";
            rlabel = "3";
        } else {
            g_assert_cmpstr("BUG", ==, "This is not meant to be exhaustive");
        }
    }

    if (llabel) {
        g_string_append(s, llabel);
    }
    if (opt & QDIST_PR_BORDER) {
        g_string_append(s, border);
    }

    str = pr_hist(desc, n);
    g_string_append(s, str);
    g_free(str);

    if (opt & QDIST_PR_BORDER) {
        g_string_append(s, border);
    }
    if (rlabel) {
        g_string_append(s, rlabel);
    }

    str = g_string_free(s, FALSE);
    pr = qdist_pr(&dist, n, opt);
    g_assert_cmpstr(pr, ==, str);
    g_free(pr);
    g_free(str);

    qdist_destroy(&dist);
}

static inline void do_test_pr_label(uint32_t opt)
{
    opt |= QDIST_PR_LABELS;
    do_test_pr(opt);
}

static void test_pr(void)
{
    do_test_pr(0);

    do_test_pr(QDIST_PR_BORDER);

    /* 100X should be ignored because we're not setting LABELS */
    do_test_pr(QDIST_PR_100X);

    do_test_pr_label(0);
    do_test_pr_label(QDIST_PR_NODECIMAL);
    do_test_pr_label(QDIST_PR_PERCENT | QDIST_PR_NODECIMAL);
    do_test_pr_label(QDIST_PR_100X);
    do_test_pr_label(QDIST_PR_NOBINRANGE | QDIST_PR_NODECIMAL);
}

static void test_bin_shrink(void)
{
    static const struct entry_desc a[] = {
        [0] = { 0.0,   42922, 7 },
        [1] = { 0.25,  47834, 8 },
        [2] = { 0.50,  26628, 0 },
        [3] = { 0.625, 597,   4 },
        [4] = { 0.75,  10298, 1 },
        [5] = { 0.875, 22,    2 },
        [6] = { 1.0,   2771,  1 }
    };
    static const struct entry_desc b[] = {
        [0] = { 0.0, 42922, 7 },
        [1] = { 0.25, 47834, 8 },
        [2] = { 0.50, 27225, 3 },
        [3] = { 0.75, 13091, 1 }
    };

    return do_test_bin(a, ARRAY_SIZE(a), b, ARRAY_SIZE(b));
}

static void test_bin_expand(void)
{
    static const struct entry_desc a[] = {
        [0] = { 0.0,   11713, 5 },
        [1] = { 0.25,  20294, 0 },
        [2] = { 0.50,  17266, 8 },
        [3] = { 0.625, 1506,  0 },
        [4] = { 0.75,  10355, 6 },
        [5] = { 0.833, 2,     1 },
        [6] = { 0.875, 99,    4 },
        [7] = { 1.0,   4301,  2 }
    };
    static const struct entry_desc b[] = {
        [0] = { 0.0, 11713, 5 },
        [1] = { 0.0, 0,     0 },
        [2] = { 0.0, 20294, 8 },
        [3] = { 0.0, 0,     0 },
        [4] = { 0.0, 0,     0 },
        [5] = { 0.0, 17266, 6 },
        [6] = { 0.0, 1506,  1 },
        [7] = { 0.0, 10355, 4 },
        [8] = { 0.0, 101,   1 },
        [9] = { 0.0, 4301,  2 }
    };

    return do_test_bin(a, ARRAY_SIZE(a), b, ARRAY_SIZE(b));
}

static void test_bin_precision(void)
{
    static const struct entry_desc a[] = {
        [0] = { 0, 213549, 8 },
        [1] = { 1, 70, 1 },
    };
    static const struct entry_desc b[] = {
        [0] = { 0, 213549, 8 },
        [1] = { 0, 70, 1 },
    };

    return do_test_bin(a, ARRAY_SIZE(a), b, ARRAY_SIZE(b));
}

static void test_bin_simple(void)
{
    static const struct entry_desc a[] = {
        [0] = { 10, 101, 8 },
        [1] = { 11, 0, 0 },
        [2] = { 12, 2, 1 }
    };
    static const struct entry_desc b[] = {
        [0] = { 0, 101, 8 },
        [1] = { 0, 0, 0 },
        [2] = { 0, 0, 0 },
        [3] = { 0, 0, 0 },
        [4] = { 0, 2, 1 }
    };

    return do_test_bin(a, ARRAY_SIZE(a), b, ARRAY_SIZE(b));
}

static void test_single_full(void)
{
    struct qdist dist;

    qdist_init(&dist);

    qdist_add(&dist, 3, 102);
    g_assert_cmpfloat(qdist_avg(&dist), ==, 3);
    g_assert_cmpfloat(qdist_xmin(&dist), ==, 3);
    g_assert_cmpfloat(qdist_xmax(&dist), ==, 3);

    histogram_check_single_full(&dist, 0);
    histogram_check_single_full(&dist, 1);
    histogram_check_single_full(&dist, 10);

    qdist_destroy(&dist);
}

static void test_single_empty(void)
{
    struct qdist dist;
    char *pr;

    qdist_init(&dist);

    qdist_add(&dist, 3, 0);
    g_assert_cmpuint(qdist_sample_count(&dist), ==, 0);
    g_assert(isnan(qdist_avg(&dist)));
    g_assert_cmpfloat(qdist_xmin(&dist), ==, 3);
    g_assert_cmpfloat(qdist_xmax(&dist), ==, 3);

    pr = qdist_pr_plain(&dist, 0);
    g_assert_cmpstr(pr, ==, " ");
    g_free(pr);

    pr = qdist_pr_plain(&dist, 1);
    g_assert_cmpstr(pr, ==, " ");
    g_free(pr);

    pr = qdist_pr_plain(&dist, 2);
    g_assert_cmpstr(pr, ==, " ");
    g_free(pr);

    qdist_destroy(&dist);
}

static void test_none(void)
{
    struct qdist dist;
    char *pr;

    qdist_init(&dist);

    g_assert(isnan(qdist_avg(&dist)));
    g_assert(isnan(qdist_xmin(&dist)));
    g_assert(isnan(qdist_xmax(&dist)));

    pr = qdist_pr_plain(&dist, 0);
    g_assert(pr == NULL);

    pr = qdist_pr_plain(&dist, 2);
    g_assert(pr == NULL);

    qdist_destroy(&dist);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qdist/none", test_none);
    g_test_add_func("/qdist/single/empty", test_single_empty);
    g_test_add_func("/qdist/single/full", test_single_full);
    g_test_add_func("/qdist/binning/simple", test_bin_simple);
    g_test_add_func("/qdist/binning/precision", test_bin_precision);
    g_test_add_func("/qdist/binning/expand", test_bin_expand);
    g_test_add_func("/qdist/binning/shrink", test_bin_shrink);
    g_test_add_func("/qdist/pr", test_pr);
    return g_test_run();
}
