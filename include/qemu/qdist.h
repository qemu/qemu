/*
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef QEMU_QDIST_H
#define QEMU_QDIST_H

#include "qemu/bitops.h"

/*
 * Samples with the same 'x value' end up in the same qdist_entry,
 * e.g. inc(0.1) and inc(0.1) end up as {x=0.1, count=2}.
 *
 * Binning happens only at print time, so that we retain the flexibility to
 * choose the binning. This might not be ideal for workloads that do not care
 * much about precision and insert many samples all with different x values;
 * in that case, pre-binning (e.g. entering both 0.115 and 0.097 as 0.1)
 * should be considered.
 */
struct qdist_entry {
    double x;
    unsigned long count;
};

struct qdist {
    struct qdist_entry *entries;
    size_t n;
    size_t size;
};

#define QDIST_PR_BORDER     BIT(0)
#define QDIST_PR_LABELS     BIT(1)
/* the remaining options only work if PR_LABELS is set */
#define QDIST_PR_NODECIMAL  BIT(2)
#define QDIST_PR_PERCENT    BIT(3)
#define QDIST_PR_100X       BIT(4)
#define QDIST_PR_NOBINRANGE BIT(5)

void qdist_init(struct qdist *dist);
void qdist_destroy(struct qdist *dist);

void qdist_add(struct qdist *dist, double x, long count);
void qdist_inc(struct qdist *dist, double x);
double qdist_xmin(const struct qdist *dist);
double qdist_xmax(const struct qdist *dist);
double qdist_avg(const struct qdist *dist);
unsigned long qdist_sample_count(const struct qdist *dist);
size_t qdist_unique_entries(const struct qdist *dist);

/* callers must free the returned string with g_free() */
char *qdist_pr_plain(const struct qdist *dist, size_t n_groups);

/* callers must free the returned string with g_free() */
char *qdist_pr(const struct qdist *dist, size_t n_groups, uint32_t opt);

/* Only qdist code and test code should ever call this function */
void qdist_bin__internal(struct qdist *to, const struct qdist *from, size_t n);

#endif /* QEMU_QDIST_H */
