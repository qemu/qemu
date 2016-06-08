/*
 * qdist.c - QEMU helpers for handling frequency distributions of data.
 *
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/qdist.h"

#include <math.h>
#ifndef NAN
#define NAN (0.0 / 0.0)
#endif

void qdist_init(struct qdist *dist)
{
    dist->entries = g_malloc(sizeof(*dist->entries));
    dist->size = 1;
    dist->n = 0;
}

void qdist_destroy(struct qdist *dist)
{
    g_free(dist->entries);
}

static inline int qdist_cmp_double(double a, double b)
{
    if (a > b) {
        return 1;
    } else if (a < b) {
        return -1;
    }
    return 0;
}

static int qdist_cmp(const void *ap, const void *bp)
{
    const struct qdist_entry *a = ap;
    const struct qdist_entry *b = bp;

    return qdist_cmp_double(a->x, b->x);
}

void qdist_add(struct qdist *dist, double x, long count)
{
    struct qdist_entry *entry = NULL;

    if (dist->n) {
        struct qdist_entry e;

        e.x = x;
        entry = bsearch(&e, dist->entries, dist->n, sizeof(e), qdist_cmp);
    }

    if (entry) {
        entry->count += count;
        return;
    }

    if (unlikely(dist->n == dist->size)) {
        dist->size *= 2;
        dist->entries = g_realloc(dist->entries,
                                  sizeof(*dist->entries) * (dist->size));
    }
    dist->n++;
    entry = &dist->entries[dist->n - 1];
    entry->x = x;
    entry->count = count;
    qsort(dist->entries, dist->n, sizeof(*entry), qdist_cmp);
}

void qdist_inc(struct qdist *dist, double x)
{
    qdist_add(dist, x, 1);
}

/*
 * Unicode for block elements. See:
 *   https://en.wikipedia.org/wiki/Block_Elements
 */
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

/*
 * Print a distribution into a string.
 *
 * This function assumes that appropriate binning has been done on the input;
 * see qdist_bin__internal() and qdist_pr_plain().
 *
 * Callers must free the returned string with g_free().
 */
static char *qdist_pr_internal(const struct qdist *dist)
{
    double min, max;
    GString *s = g_string_new("");
    size_t i;

    /* if only one entry, its printout will be either full or empty */
    if (dist->n == 1) {
        if (dist->entries[0].count) {
            g_string_append_unichar(s, qdist_blocks[QDIST_NR_BLOCK_CODES - 1]);
        } else {
            g_string_append_c(s, ' ');
        }
        goto out;
    }

    /* get min and max counts */
    min = dist->entries[0].count;
    max = min;
    for (i = 0; i < dist->n; i++) {
        struct qdist_entry *e = &dist->entries[i];

        if (e->count < min) {
            min = e->count;
        }
        if (e->count > max) {
            max = e->count;
        }
    }

    for (i = 0; i < dist->n; i++) {
        struct qdist_entry *e = &dist->entries[i];
        int index;

        /* make an exception with 0; instead of using block[0], print a space */
        if (e->count) {
            /* divide first to avoid loss of precision when e->count == max */
            index = (e->count - min) / (max - min) * (QDIST_NR_BLOCK_CODES - 1);
            g_string_append_unichar(s, qdist_blocks[index]);
        } else {
            g_string_append_c(s, ' ');
        }
    }
 out:
    return g_string_free(s, FALSE);
}

/*
 * Bin the distribution in @from into @n bins of consecutive, non-overlapping
 * intervals, copying the result to @to.
 *
 * This function is internal to qdist: only this file and test code should
 * ever call it.
 *
 * Note: calling this function on an already-binned qdist is a bug.
 *
 * If @n == 0 or @from->n == 1, use @from->n.
 */
void qdist_bin__internal(struct qdist *to, const struct qdist *from, size_t n)
{
    double xmin, xmax;
    double step;
    size_t i, j;

    qdist_init(to);

    if (from->n == 0) {
        return;
    }
    if (n == 0 || from->n == 1) {
        n = from->n;
    }

    /* set equally-sized bins between @from's left and right */
    xmin = qdist_xmin(from);
    xmax = qdist_xmax(from);
    step = (xmax - xmin) / n;

    if (n == from->n) {
        /* if @from's entries are equally spaced, no need to re-bin */
        for (i = 0; i < from->n; i++) {
            if (from->entries[i].x != xmin + i * step) {
                goto rebin;
            }
        }
        /* they're equally spaced, so copy the dist and bail out */
        to->entries = g_new(struct qdist_entry, from->n);
        to->n = from->n;
        memcpy(to->entries, from->entries, sizeof(*to->entries) * to->n);
        return;
    }

 rebin:
    j = 0;
    for (i = 0; i < n; i++) {
        double x;
        double left, right;

        left = xmin + i * step;
        right = xmin + (i + 1) * step;

        /* Add x, even if it might not get any counts later */
        x = left;
        qdist_add(to, x, 0);

        /*
         * To avoid double-counting we capture [left, right) ranges, except for
         * the righmost bin, which captures a [left, right] range.
         */
        while (j < from->n && (from->entries[j].x < right || i == n - 1)) {
            struct qdist_entry *o = &from->entries[j];

            qdist_add(to, x, o->count);
            j++;
        }
    }
}

/*
 * Print @dist into a string, after re-binning it into @n bins of consecutive,
 * non-overlapping intervals.
 *
 * If @n == 0, use @orig->n.
 *
 * Callers must free the returned string with g_free().
 */
char *qdist_pr_plain(const struct qdist *dist, size_t n)
{
    struct qdist binned;
    char *ret;

    if (dist->n == 0) {
        return NULL;
    }
    qdist_bin__internal(&binned, dist, n);
    ret = qdist_pr_internal(&binned);
    qdist_destroy(&binned);
    return ret;
}

static char *qdist_pr_label(const struct qdist *dist, size_t n_bins,
                            uint32_t opt, bool is_left)
{
    const char *percent;
    const char *lparen;
    const char *rparen;
    GString *s;
    double x1, x2, step;
    double x;
    double n;
    int dec;

    s = g_string_new("");
    if (!(opt & QDIST_PR_LABELS)) {
        goto out;
    }

    dec = opt & QDIST_PR_NODECIMAL ? 0 : 1;
    percent = opt & QDIST_PR_PERCENT ? "%" : "";

    n = n_bins ? n_bins : dist->n;
    x = is_left ? qdist_xmin(dist) : qdist_xmax(dist);
    step = (qdist_xmax(dist) - qdist_xmin(dist)) / n;

    if (opt & QDIST_PR_100X) {
        x *= 100.0;
        step *= 100.0;
    }
    if (opt & QDIST_PR_NOBINRANGE) {
        lparen = rparen = "";
        x1 = x;
        x2 = x; /* unnecessary, but a dumb compiler might not figure it out */
    } else {
        lparen = "[";
        rparen = is_left ? ")" : "]";
        if (is_left) {
            x1 = x;
            x2 = x + step;
        } else {
            x1 = x - step;
            x2 = x;
        }
    }
    g_string_append_printf(s, "%s%.*f", lparen, dec, x1);
    if (!(opt & QDIST_PR_NOBINRANGE)) {
        g_string_append_printf(s, ",%.*f%s", dec, x2, rparen);
    }
    g_string_append(s, percent);
 out:
    return g_string_free(s, FALSE);
}

/*
 * Print the distribution's histogram into a string.
 *
 * See also: qdist_pr_plain().
 *
 * Callers must free the returned string with g_free().
 */
char *qdist_pr(const struct qdist *dist, size_t n_bins, uint32_t opt)
{
    const char *border = opt & QDIST_PR_BORDER ? "|" : "";
    char *llabel, *rlabel;
    char *hgram;
    GString *s;

    if (dist->n == 0) {
        return NULL;
    }

    s = g_string_new("");

    llabel = qdist_pr_label(dist, n_bins, opt, true);
    rlabel = qdist_pr_label(dist, n_bins, opt, false);
    hgram = qdist_pr_plain(dist, n_bins);
    g_string_append_printf(s, "%s%s%s%s%s",
                           llabel, border, hgram, border, rlabel);
    g_free(llabel);
    g_free(rlabel);
    g_free(hgram);

    return g_string_free(s, FALSE);
}

static inline double qdist_x(const struct qdist *dist, int index)
{
    if (dist->n == 0) {
        return NAN;
    }
    return dist->entries[index].x;
}

double qdist_xmin(const struct qdist *dist)
{
    return qdist_x(dist, 0);
}

double qdist_xmax(const struct qdist *dist)
{
    return qdist_x(dist, dist->n - 1);
}

size_t qdist_unique_entries(const struct qdist *dist)
{
    return dist->n;
}

unsigned long qdist_sample_count(const struct qdist *dist)
{
    unsigned long count = 0;
    size_t i;

    for (i = 0; i < dist->n; i++) {
        struct qdist_entry *e = &dist->entries[i];

        count += e->count;
    }
    return count;
}

static double qdist_pairwise_avg(const struct qdist *dist, size_t index,
                                 size_t n, unsigned long count)
{
    /* amortize the recursion by using a base case > 2 */
    if (n <= 8) {
        size_t i;
        double ret = 0;

        for (i = 0; i < n; i++) {
            struct qdist_entry *e = &dist->entries[index + i];

            ret += e->x * e->count / count;
        }
        return ret;
    } else {
        size_t n2 = n / 2;

        return qdist_pairwise_avg(dist, index, n2, count) +
               qdist_pairwise_avg(dist, index + n2, n - n2, count);
    }
}

double qdist_avg(const struct qdist *dist)
{
    unsigned long count;

    count = qdist_sample_count(dist);
    if (!count) {
        return NAN;
    }
    return qdist_pairwise_avg(dist, 0, dist->n, count);
}
