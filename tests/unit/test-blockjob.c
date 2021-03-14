/*
 * Blockjob tests
 *
 * Copyright Igalia, S.L. 2016
 *
 * Authors:
 *  Alberto Garcia   <berto@igalia.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "block/blockjob_int.h"
#include "sysemu/block-backend.h"
#include "qapi/qmp/qdict.h"

static const BlockJobDriver test_block_job_driver = {
    .job_driver = {
        .instance_size = sizeof(BlockJob),
        .free          = block_job_free,
        .user_resume   = block_job_user_resume,
    },
};

static void block_job_cb(void *opaque, int ret)
{
}

static BlockJob *mk_job(BlockBackend *blk, const char *id,
                        const BlockJobDriver *drv, bool should_succeed,
                        int flags)
{
    BlockJob *job;
    Error *err = NULL;

    job = block_job_create(id, drv, NULL, blk_bs(blk),
                           0, BLK_PERM_ALL, 0, flags, block_job_cb,
                           NULL, &err);
    if (should_succeed) {
        g_assert_null(err);
        g_assert_nonnull(job);
        if (id) {
            g_assert_cmpstr(job->job.id, ==, id);
        } else {
            g_assert_cmpstr(job->job.id, ==, blk_name(blk));
        }
    } else {
        error_free_or_abort(&err);
        g_assert_null(job);
    }

    return job;
}

static BlockJob *do_test_id(BlockBackend *blk, const char *id,
                            bool should_succeed)
{
    return mk_job(blk, id, &test_block_job_driver,
                  should_succeed, JOB_DEFAULT);
}

/* This creates a BlockBackend (optionally with a name) with a
 * BlockDriverState inserted. */
static BlockBackend *create_blk(const char *name)
{
    /* No I/O is performed on this device */
    BlockBackend *blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
    BlockDriverState *bs;

    QDict *opt = qdict_new();
    qdict_put_str(opt, "file.read-zeroes", "on");
    bs = bdrv_open("null-co://", NULL, opt, 0, &error_abort);
    g_assert_nonnull(bs);

    blk_insert_bs(blk, bs, &error_abort);
    bdrv_unref(bs);

    if (name) {
        Error *err = NULL;
        monitor_add_blk(blk, name, &err);
        g_assert_null(err);
    }

    return blk;
}

/* This destroys the backend */
static void destroy_blk(BlockBackend *blk)
{
    if (blk_name(blk)[0] != '\0') {
        monitor_remove_blk(blk);
    }

    blk_remove_bs(blk);
    blk_unref(blk);
}

static void test_job_ids(void)
{
    BlockBackend *blk[3];
    BlockJob *job[3];

    blk[0] = create_blk(NULL);
    blk[1] = create_blk("drive1");
    blk[2] = create_blk("drive2");

    /* No job ID provided and the block backend has no name */
    job[0] = do_test_id(blk[0], NULL, false);

    /* These are all invalid job IDs */
    job[0] = do_test_id(blk[0], "0id", false);
    job[0] = do_test_id(blk[0], "",    false);
    job[0] = do_test_id(blk[0], "   ", false);
    job[0] = do_test_id(blk[0], "123", false);
    job[0] = do_test_id(blk[0], "_id", false);
    job[0] = do_test_id(blk[0], "-id", false);
    job[0] = do_test_id(blk[0], ".id", false);
    job[0] = do_test_id(blk[0], "#id", false);

    /* This one is valid */
    job[0] = do_test_id(blk[0], "id0", true);

    /* We can have two jobs in the same BDS */
    job[1] = do_test_id(blk[0], "id1", true);
    job_early_fail(&job[1]->job);

    /* Duplicate job IDs are not allowed */
    job[1] = do_test_id(blk[1], "id0", false);

    /* But once job[0] finishes we can reuse its ID */
    job_early_fail(&job[0]->job);
    job[1] = do_test_id(blk[1], "id0", true);

    /* No job ID specified, defaults to the backend name ('drive1') */
    job_early_fail(&job[1]->job);
    job[1] = do_test_id(blk[1], NULL, true);

    /* Duplicate job ID */
    job[2] = do_test_id(blk[2], "drive1", false);

    /* The ID of job[2] would default to 'drive2' but it is already in use */
    job[0] = do_test_id(blk[0], "drive2", true);
    job[2] = do_test_id(blk[2], NULL, false);

    /* This one is valid */
    job[2] = do_test_id(blk[2], "id_2", true);

    job_early_fail(&job[0]->job);
    job_early_fail(&job[1]->job);
    job_early_fail(&job[2]->job);

    destroy_blk(blk[0]);
    destroy_blk(blk[1]);
    destroy_blk(blk[2]);
}

typedef struct CancelJob {
    BlockJob common;
    BlockBackend *blk;
    bool should_converge;
    bool should_complete;
} CancelJob;

static void cancel_job_complete(Job *job, Error **errp)
{
    CancelJob *s = container_of(job, CancelJob, common.job);
    s->should_complete = true;
}

static int coroutine_fn cancel_job_run(Job *job, Error **errp)
{
    CancelJob *s = container_of(job, CancelJob, common.job);

    while (!s->should_complete) {
        if (job_is_cancelled(&s->common.job)) {
            return 0;
        }

        if (!job_is_ready(&s->common.job) && s->should_converge) {
            job_transition_to_ready(&s->common.job);
        }

        job_sleep_ns(&s->common.job, 100000);
    }

    return 0;
}

static const BlockJobDriver test_cancel_driver = {
    .job_driver = {
        .instance_size = sizeof(CancelJob),
        .free          = block_job_free,
        .user_resume   = block_job_user_resume,
        .run           = cancel_job_run,
        .complete      = cancel_job_complete,
    },
};

static CancelJob *create_common(Job **pjob)
{
    BlockBackend *blk;
    Job *job;
    BlockJob *bjob;
    CancelJob *s;

    blk = create_blk(NULL);
    bjob = mk_job(blk, "Steve", &test_cancel_driver, true,
                  JOB_MANUAL_FINALIZE | JOB_MANUAL_DISMISS);
    job = &bjob->job;
    job_ref(job);
    assert(job->status == JOB_STATUS_CREATED);
    s = container_of(bjob, CancelJob, common);
    s->blk = blk;

    *pjob = job;
    return s;
}

static void cancel_common(CancelJob *s)
{
    BlockJob *job = &s->common;
    BlockBackend *blk = s->blk;
    JobStatus sts = job->job.status;
    AioContext *ctx;

    ctx = job->job.aio_context;
    aio_context_acquire(ctx);

    job_cancel_sync(&job->job);
    if (sts != JOB_STATUS_CREATED && sts != JOB_STATUS_CONCLUDED) {
        Job *dummy = &job->job;
        job_dismiss(&dummy, &error_abort);
    }
    assert(job->job.status == JOB_STATUS_NULL);
    job_unref(&job->job);
    destroy_blk(blk);

    aio_context_release(ctx);
}

static void test_cancel_created(void)
{
    Job *job;
    CancelJob *s;

    s = create_common(&job);
    cancel_common(s);
}

static void test_cancel_running(void)
{
    Job *job;
    CancelJob *s;

    s = create_common(&job);

    job_start(job);
    assert(job->status == JOB_STATUS_RUNNING);

    cancel_common(s);
}

static void test_cancel_paused(void)
{
    Job *job;
    CancelJob *s;

    s = create_common(&job);

    job_start(job);
    assert(job->status == JOB_STATUS_RUNNING);

    job_user_pause(job, &error_abort);
    job_enter(job);
    assert(job->status == JOB_STATUS_PAUSED);

    cancel_common(s);
}

static void test_cancel_ready(void)
{
    Job *job;
    CancelJob *s;

    s = create_common(&job);

    job_start(job);
    assert(job->status == JOB_STATUS_RUNNING);

    s->should_converge = true;
    job_enter(job);
    assert(job->status == JOB_STATUS_READY);

    cancel_common(s);
}

static void test_cancel_standby(void)
{
    Job *job;
    CancelJob *s;

    s = create_common(&job);

    job_start(job);
    assert(job->status == JOB_STATUS_RUNNING);

    s->should_converge = true;
    job_enter(job);
    assert(job->status == JOB_STATUS_READY);

    job_user_pause(job, &error_abort);
    job_enter(job);
    assert(job->status == JOB_STATUS_STANDBY);

    cancel_common(s);
}

static void test_cancel_pending(void)
{
    Job *job;
    CancelJob *s;

    s = create_common(&job);

    job_start(job);
    assert(job->status == JOB_STATUS_RUNNING);

    s->should_converge = true;
    job_enter(job);
    assert(job->status == JOB_STATUS_READY);

    job_complete(job, &error_abort);
    job_enter(job);
    while (!job->deferred_to_main_loop) {
        aio_poll(qemu_get_aio_context(), true);
    }
    assert(job->status == JOB_STATUS_READY);
    aio_poll(qemu_get_aio_context(), true);
    assert(job->status == JOB_STATUS_PENDING);

    cancel_common(s);
}

static void test_cancel_concluded(void)
{
    Job *job;
    CancelJob *s;

    s = create_common(&job);

    job_start(job);
    assert(job->status == JOB_STATUS_RUNNING);

    s->should_converge = true;
    job_enter(job);
    assert(job->status == JOB_STATUS_READY);

    job_complete(job, &error_abort);
    job_enter(job);
    while (!job->deferred_to_main_loop) {
        aio_poll(qemu_get_aio_context(), true);
    }
    assert(job->status == JOB_STATUS_READY);
    aio_poll(qemu_get_aio_context(), true);
    assert(job->status == JOB_STATUS_PENDING);

    aio_context_acquire(job->aio_context);
    job_finalize(job, &error_abort);
    aio_context_release(job->aio_context);
    assert(job->status == JOB_STATUS_CONCLUDED);

    cancel_common(s);
}

int main(int argc, char **argv)
{
    qemu_init_main_loop(&error_abort);
    bdrv_init();

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/blockjob/ids", test_job_ids);
    g_test_add_func("/blockjob/cancel/created", test_cancel_created);
    g_test_add_func("/blockjob/cancel/running", test_cancel_running);
    g_test_add_func("/blockjob/cancel/paused", test_cancel_paused);
    g_test_add_func("/blockjob/cancel/ready", test_cancel_ready);
    g_test_add_func("/blockjob/cancel/standby", test_cancel_standby);
    g_test_add_func("/blockjob/cancel/pending", test_cancel_pending);
    g_test_add_func("/blockjob/cancel/concluded", test_cancel_concluded);
    return g_test_run();
}
