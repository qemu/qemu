/*
 * Blockjob transactions tests
 *
 * Copyright Red Hat, Inc. 2015
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib.h>
#include "qemu/main-loop.h"
#include "block/blockjob.h"

typedef struct {
    BlockJob common;
    unsigned int iterations;
    bool use_timer;
    int rc;
    int *result;
} TestBlockJob;

static const BlockJobDriver test_block_job_driver = {
    .instance_size = sizeof(TestBlockJob),
};

static void test_block_job_complete(BlockJob *job, void *opaque)
{
    BlockDriverState *bs = job->bs;
    int rc = (intptr_t)opaque;

    if (block_job_is_cancelled(job)) {
        rc = -ECANCELED;
    }

    block_job_completed(job, rc);
    bdrv_unref(bs);
}

static void coroutine_fn test_block_job_run(void *opaque)
{
    TestBlockJob *s = opaque;
    BlockJob *job = &s->common;

    while (s->iterations--) {
        if (s->use_timer) {
            block_job_sleep_ns(job, QEMU_CLOCK_REALTIME, 0);
        } else {
            block_job_yield(job);
        }

        if (block_job_is_cancelled(job)) {
            break;
        }
    }

    block_job_defer_to_main_loop(job, test_block_job_complete,
                                 (void *)(intptr_t)s->rc);
}

typedef struct {
    TestBlockJob *job;
    int *result;
} TestBlockJobCBData;

static void test_block_job_cb(void *opaque, int ret)
{
    TestBlockJobCBData *data = opaque;
    if (!ret && block_job_is_cancelled(&data->job->common)) {
        ret = -ECANCELED;
    }
    *data->result = ret;
    g_free(data);
}

/* Create a block job that completes with a given return code after a given
 * number of event loop iterations.  The return code is stored in the given
 * result pointer.
 *
 * The event loop iterations can either be handled automatically with a 0 delay
 * timer, or they can be stepped manually by entering the coroutine.
 */
static BlockJob *test_block_job_start(unsigned int iterations,
                                      bool use_timer,
                                      int rc, int *result)
{
    BlockDriverState *bs;
    TestBlockJob *s;
    TestBlockJobCBData *data;

    data = g_new0(TestBlockJobCBData, 1);
    bs = bdrv_new();
    s = block_job_create(&test_block_job_driver, bs, 0, test_block_job_cb,
                         data, &error_abort);
    s->iterations = iterations;
    s->use_timer = use_timer;
    s->rc = rc;
    s->result = result;
    s->common.co = qemu_coroutine_create(test_block_job_run);
    data->job = s;
    data->result = result;
    qemu_coroutine_enter(s->common.co, s);
    return &s->common;
}

static void test_single_job(int expected)
{
    BlockJob *job;
    BlockJobTxn *txn;
    int result = -EINPROGRESS;

    txn = block_job_txn_new();
    job = test_block_job_start(1, true, expected, &result);
    block_job_txn_add_job(txn, job);

    if (expected == -ECANCELED) {
        block_job_cancel(job);
    }

    while (result == -EINPROGRESS) {
        aio_poll(qemu_get_aio_context(), true);
    }
    g_assert_cmpint(result, ==, expected);

    block_job_txn_unref(txn);
}

static void test_single_job_success(void)
{
    test_single_job(0);
}

static void test_single_job_failure(void)
{
    test_single_job(-EIO);
}

static void test_single_job_cancel(void)
{
    test_single_job(-ECANCELED);
}

static void test_pair_jobs(int expected1, int expected2)
{
    BlockJob *job1;
    BlockJob *job2;
    BlockJobTxn *txn;
    int result1 = -EINPROGRESS;
    int result2 = -EINPROGRESS;

    txn = block_job_txn_new();
    job1 = test_block_job_start(1, true, expected1, &result1);
    block_job_txn_add_job(txn, job1);
    job2 = test_block_job_start(2, true, expected2, &result2);
    block_job_txn_add_job(txn, job2);

    if (expected1 == -ECANCELED) {
        block_job_cancel(job1);
    }
    if (expected2 == -ECANCELED) {
        block_job_cancel(job2);
    }

    while (result1 == -EINPROGRESS || result2 == -EINPROGRESS) {
        aio_poll(qemu_get_aio_context(), true);
    }

    /* Failure or cancellation of one job cancels the other job */
    if (expected1 != 0) {
        expected2 = -ECANCELED;
    } else if (expected2 != 0) {
        expected1 = -ECANCELED;
    }

    g_assert_cmpint(result1, ==, expected1);
    g_assert_cmpint(result2, ==, expected2);

    block_job_txn_unref(txn);
}

static void test_pair_jobs_success(void)
{
    test_pair_jobs(0, 0);
}

static void test_pair_jobs_failure(void)
{
    /* Test both orderings.  The two jobs run for a different number of
     * iterations so the code path is different depending on which job fails
     * first.
     */
    test_pair_jobs(-EIO, 0);
    test_pair_jobs(0, -EIO);
}

static void test_pair_jobs_cancel(void)
{
    test_pair_jobs(-ECANCELED, 0);
    test_pair_jobs(0, -ECANCELED);
}

static void test_pair_jobs_fail_cancel_race(void)
{
    BlockJob *job1;
    BlockJob *job2;
    BlockJobTxn *txn;
    int result1 = -EINPROGRESS;
    int result2 = -EINPROGRESS;

    txn = block_job_txn_new();
    job1 = test_block_job_start(1, true, -ECANCELED, &result1);
    block_job_txn_add_job(txn, job1);
    job2 = test_block_job_start(2, false, 0, &result2);
    block_job_txn_add_job(txn, job2);

    block_job_cancel(job1);

    /* Now make job2 finish before the main loop kicks jobs.  This simulates
     * the race between a pending kick and another job completing.
     */
    block_job_enter(job2);
    block_job_enter(job2);

    while (result1 == -EINPROGRESS || result2 == -EINPROGRESS) {
        aio_poll(qemu_get_aio_context(), true);
    }

    g_assert_cmpint(result1, ==, -ECANCELED);
    g_assert_cmpint(result2, ==, -ECANCELED);

    block_job_txn_unref(txn);
}

int main(int argc, char **argv)
{
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/single/success", test_single_job_success);
    g_test_add_func("/single/failure", test_single_job_failure);
    g_test_add_func("/single/cancel", test_single_job_cancel);
    g_test_add_func("/pair/success", test_pair_jobs_success);
    g_test_add_func("/pair/failure", test_pair_jobs_failure);
    g_test_add_func("/pair/cancel", test_pair_jobs_cancel);
    g_test_add_func("/pair/fail-cancel-race", test_pair_jobs_fail_cancel_race);
    return g_test_run();
}
