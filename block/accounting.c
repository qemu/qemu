/*
 * QEMU System Emulator block accounting
 *
 * Copyright (c) 2011 Christoph Hellwig
 * Copyright (c) 2015 Igalia, S.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/accounting.h"
#include "block/block_int.h"
#include "qemu/timer.h"
#include "system/qtest.h"
#include "qapi/error.h"

static QEMUClockType clock_type = QEMU_CLOCK_REALTIME;
static const int qtest_latency_ns = NANOSECONDS_PER_SECOND / 1000;

void block_acct_init(BlockAcctStats *stats)
{
    qemu_mutex_init(&stats->lock);
    if (qtest_enabled()) {
        clock_type = QEMU_CLOCK_VIRTUAL;
    }
    stats->account_invalid = true;
    stats->account_failed = true;
}

static bool bool_from_onoffauto(OnOffAuto val, bool def)
{
    switch (val) {
    case ON_OFF_AUTO_AUTO:
        return def;
    case ON_OFF_AUTO_ON:
        return true;
    case ON_OFF_AUTO_OFF:
        return false;
    default:
        abort();
    }
}

bool block_acct_setup(BlockAcctStats *stats, enum OnOffAuto account_invalid,
                      enum OnOffAuto account_failed, uint32_t *stats_intervals,
                      uint32_t num_stats_intervals, Error **errp)
{
    stats->account_invalid = bool_from_onoffauto(account_invalid,
                                                 stats->account_invalid);
    stats->account_failed = bool_from_onoffauto(account_failed,
                                                stats->account_failed);
    if (stats_intervals) {
        for (int i = 0; i < num_stats_intervals; i++) {
            if (stats_intervals[i] <= 0) {
                error_setg(errp, "Invalid interval length: %u", stats_intervals[i]);
                return false;
            }
            block_acct_add_interval(stats, stats_intervals[i]);
        }
        g_free(stats_intervals);
    }
    return true;
}

void block_acct_cleanup(BlockAcctStats *stats)
{
    BlockAcctTimedStats *s, *next;
    QSLIST_FOREACH_SAFE(s, &stats->intervals, entries, next) {
        g_free(s);
    }
    qemu_mutex_destroy(&stats->lock);
}

void block_acct_add_interval(BlockAcctStats *stats, unsigned interval_length)
{
    BlockAcctTimedStats *s;
    unsigned i;

    s = g_new0(BlockAcctTimedStats, 1);
    s->interval_length = interval_length;
    s->stats = stats;
    qemu_mutex_lock(&stats->lock);
    QSLIST_INSERT_HEAD(&stats->intervals, s, entries);

    for (i = 0; i < BLOCK_MAX_IOTYPE; i++) {
        timed_average_init(&s->latency[i], clock_type,
                           (uint64_t) interval_length * NANOSECONDS_PER_SECOND);
    }
    qemu_mutex_unlock(&stats->lock);
}

BlockAcctTimedStats *block_acct_interval_next(BlockAcctStats *stats,
                                              BlockAcctTimedStats *s)
{
    if (s == NULL) {
        return QSLIST_FIRST(&stats->intervals);
    } else {
        return QSLIST_NEXT(s, entries);
    }
}

void block_acct_start(BlockAcctStats *stats, BlockAcctCookie *cookie,
                      int64_t bytes, enum BlockAcctType type)
{
    assert(type < BLOCK_MAX_IOTYPE);

    cookie->bytes = bytes;
    cookie->start_time_ns = qemu_clock_get_ns(clock_type);
    cookie->type = type;
}

/* block_latency_histogram_compare_func:
 * Compare @key with interval [@it[0], @it[1]).
 * Return: -1 if @key < @it[0]
 *          0 if @key in [@it[0], @it[1])
 *         +1 if @key >= @it[1]
 */
static int block_latency_histogram_compare_func(const void *key, const void *it)
{
    uint64_t k = *(uint64_t *)key;
    uint64_t a = ((uint64_t *)it)[0];
    uint64_t b = ((uint64_t *)it)[1];

    return k < a ? -1 : (k < b ? 0 : 1);
}

static void block_latency_histogram_account(BlockLatencyHistogram *hist,
                                            int64_t latency_ns)
{
    uint64_t *pos;

    if (hist->bins == NULL) {
        /* histogram disabled */
        return;
    }


    if (latency_ns < hist->boundaries[0]) {
        hist->bins[0]++;
        return;
    }

    if (latency_ns >= hist->boundaries[hist->nbins - 2]) {
        hist->bins[hist->nbins - 1]++;
        return;
    }

    pos = bsearch(&latency_ns, hist->boundaries, hist->nbins - 2,
                  sizeof(hist->boundaries[0]),
                  block_latency_histogram_compare_func);
    assert(pos != NULL);

    hist->bins[pos - hist->boundaries + 1]++;
}

int block_latency_histogram_set(BlockAcctStats *stats, enum BlockAcctType type,
                                uint64List *boundaries)
{
    BlockLatencyHistogram *hist = &stats->latency_histogram[type];
    uint64List *entry;
    uint64_t *ptr;
    uint64_t prev = 0;
    int new_nbins = 1;

    for (entry = boundaries; entry; entry = entry->next) {
        if (entry->value <= prev) {
            return -EINVAL;
        }
        new_nbins++;
        prev = entry->value;
    }

    hist->nbins = new_nbins;
    g_free(hist->boundaries);
    hist->boundaries = g_new(uint64_t, hist->nbins - 1);
    for (entry = boundaries, ptr = hist->boundaries; entry;
         entry = entry->next, ptr++)
    {
        *ptr = entry->value;
    }

    g_free(hist->bins);
    hist->bins = g_new0(uint64_t, hist->nbins);

    return 0;
}

void block_latency_histograms_clear(BlockAcctStats *stats)
{
    int i;

    for (i = 0; i < BLOCK_MAX_IOTYPE; i++) {
        BlockLatencyHistogram *hist = &stats->latency_histogram[i];
        g_free(hist->bins);
        g_free(hist->boundaries);
        memset(hist, 0, sizeof(*hist));
    }
}

static void block_account_one_io(BlockAcctStats *stats, BlockAcctCookie *cookie,
                                 bool failed)
{
    BlockAcctTimedStats *s;
    int64_t time_ns = qemu_clock_get_ns(clock_type);
    int64_t latency_ns = time_ns - cookie->start_time_ns;

    if (qtest_enabled()) {
        latency_ns = qtest_latency_ns;
    }

    assert(cookie->type < BLOCK_MAX_IOTYPE);

    if (cookie->type == BLOCK_ACCT_NONE) {
        return;
    }

    WITH_QEMU_LOCK_GUARD(&stats->lock) {
        if (failed) {
            stats->failed_ops[cookie->type]++;
        } else {
            stats->nr_bytes[cookie->type] += cookie->bytes;
            stats->nr_ops[cookie->type]++;
        }

        block_latency_histogram_account(&stats->latency_histogram[cookie->type],
                                        latency_ns);

        if (!failed || stats->account_failed) {
            stats->total_time_ns[cookie->type] += latency_ns;
            stats->last_access_time_ns = time_ns;

            QSLIST_FOREACH(s, &stats->intervals, entries) {
                timed_average_account(&s->latency[cookie->type], latency_ns);
            }
        }
    }

    cookie->type = BLOCK_ACCT_NONE;
}

void block_acct_done(BlockAcctStats *stats, BlockAcctCookie *cookie)
{
    block_account_one_io(stats, cookie, false);
}

void block_acct_failed(BlockAcctStats *stats, BlockAcctCookie *cookie)
{
    block_account_one_io(stats, cookie, true);
}

void block_acct_invalid(BlockAcctStats *stats, enum BlockAcctType type)
{
    assert(type < BLOCK_MAX_IOTYPE);

    /* block_account_one_io() updates total_time_ns[], but this one does
     * not.  The reason is that invalid requests are accounted during their
     * submission, therefore there's no actual I/O involved.
     */
    qemu_mutex_lock(&stats->lock);
    stats->invalid_ops[type]++;

    if (stats->account_invalid) {
        stats->last_access_time_ns = qemu_clock_get_ns(clock_type);
    }
    qemu_mutex_unlock(&stats->lock);
}

void block_acct_merge_done(BlockAcctStats *stats, enum BlockAcctType type,
                      int num_requests)
{
    assert(type < BLOCK_MAX_IOTYPE);

    qemu_mutex_lock(&stats->lock);
    stats->merged[type] += num_requests;
    qemu_mutex_unlock(&stats->lock);
}

int64_t block_acct_idle_time_ns(BlockAcctStats *stats)
{
    return qemu_clock_get_ns(clock_type) - stats->last_access_time_ns;
}

double block_acct_queue_depth(BlockAcctTimedStats *stats,
                              enum BlockAcctType type)
{
    uint64_t sum, elapsed;

    assert(type < BLOCK_MAX_IOTYPE);

    qemu_mutex_lock(&stats->stats->lock);
    sum = timed_average_sum(&stats->latency[type], &elapsed);
    qemu_mutex_unlock(&stats->stats->lock);

    return (double) sum / elapsed;
}
