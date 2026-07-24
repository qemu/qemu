/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * BlockAcctStats latency histogram locking regression test
 *
 * Copyright (c) 2026 Virtuozzo International GmbH.
 *
 * Regression test for missing stats->lock in
 * block_latency_histogram_set()/block_latency_histograms_clear(),
 * racing block_account_one_io() reading the same fields from an
 * iothread. Aborts reliably before the fix, passes after it.
 */

#include "qemu/osdep.h"
#include "block/block.h"
#include "block/accounting.h"
#include "system/block-backend.h"
#include "system/block-backend-io.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"

#define RACE_DURATION_MS 2000
#define NUM_READER_THREADS 8

static bool stop_workers;

/*
 * Different bin counts, so the writer's g_free()/g_new() churn can be
 * caught mid-update. Values are small enough (nanoseconds) that plain
 * back-to-back start/done calls exercise every bin without sleeping.
 */
static uint64List boundaries_a[] = {
    { .next = &boundaries_a[1], .value = 1000 },
    { .next = &boundaries_a[2], .value = 5000 },
    { .next = NULL,             .value = 50000 },
};

static uint64List boundaries_b[] = {
    { .next = &boundaries_b[1], .value = 800 },
    { .next = &boundaries_b[2], .value = 3000 },
    { .next = &boundaries_b[3], .value = 20000 },
    { .next = NULL,             .value = 200000 },
};

static void *writer_thread(void *opaque)
{
    BlockAcctStats *stats = opaque;

    while (!qatomic_read(&stop_workers)) {
        block_latency_histogram_set(stats, BLOCK_ACCT_READ, boundaries_a);
        block_latency_histogram_set(stats, BLOCK_ACCT_READ, boundaries_b);
        block_latency_histograms_clear(stats);
    }

    return NULL;
}

static void *reader_thread(void *opaque)
{
    BlockAcctStats *stats = opaque;

    while (!qatomic_read(&stop_workers)) {
        BlockAcctCookie cookie;

        block_acct_start(stats, &cookie, 4096, BLOCK_ACCT_READ);
        block_acct_done(stats, &cookie);
    }

    return NULL;
}

static void test_latency_histogram_race(void)
{
    BlockBackend *blk = blk_new(qemu_get_aio_context(),
                                BLK_PERM_ALL, BLK_PERM_ALL);
    BlockAcctStats *stats = blk_get_stats(blk);
    QemuThread writer, readers[NUM_READER_THREADS];
    int i;

    /* Histogram has to be enabled (bins != NULL) before racing it. */
    g_assert(block_latency_histogram_set(stats, BLOCK_ACCT_READ,
                                         boundaries_a) == 0);

    stop_workers = false;
    qemu_thread_create(&writer, "hist-writer", writer_thread, stats,
                       QEMU_THREAD_JOINABLE);
    for (i = 0; i < NUM_READER_THREADS; i++) {
        qemu_thread_create(&readers[i], "hist-reader", reader_thread, stats,
                           QEMU_THREAD_JOINABLE);
    }

    g_usleep(RACE_DURATION_MS * 1000);
    qatomic_set(&stop_workers, true);

    qemu_thread_join(&writer);
    for (i = 0; i < NUM_READER_THREADS; i++) {
        qemu_thread_join(&readers[i]);
    }

    blk_unref(blk);
}

int main(int argc, char **argv)
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/block-accounting/latency_histogram_race",
                    test_latency_histogram_race);

    return g_test_run();
}
