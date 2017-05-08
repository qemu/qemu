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

static const BlockJobDriver test_block_job_driver = {
    .instance_size = sizeof(BlockJob),
};

static void block_job_cb(void *opaque, int ret)
{
}

static BlockJob *do_test_id(BlockBackend *blk, const char *id,
                            bool should_succeed)
{
    BlockJob *job;
    Error *errp = NULL;

    job = block_job_create(id, &test_block_job_driver, blk_bs(blk),
                           0, BLK_PERM_ALL, 0, BLOCK_JOB_DEFAULT, block_job_cb,
                           NULL, &errp);
    if (should_succeed) {
        g_assert_null(errp);
        g_assert_nonnull(job);
        if (id) {
            g_assert_cmpstr(job->id, ==, id);
        } else {
            g_assert_cmpstr(job->id, ==, blk_name(blk));
        }
    } else {
        g_assert_nonnull(errp);
        g_assert_null(job);
        error_free(errp);
    }

    return job;
}

/* This creates a BlockBackend (optionally with a name) with a
 * BlockDriverState inserted. */
static BlockBackend *create_blk(const char *name)
{
    /* No I/O is performed on this device */
    BlockBackend *blk = blk_new(0, BLK_PERM_ALL);
    BlockDriverState *bs;

    bs = bdrv_open("null-co://", NULL, NULL, 0, &error_abort);
    g_assert_nonnull(bs);

    blk_insert_bs(blk, bs, &error_abort);
    bdrv_unref(bs);

    if (name) {
        Error *errp = NULL;
        monitor_add_blk(blk, name, &errp);
        g_assert_null(errp);
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

    /* We cannot have two jobs in the same BDS */
    do_test_id(blk[0], "id1", false);

    /* Duplicate job IDs are not allowed */
    job[1] = do_test_id(blk[1], "id0", false);

    /* But once job[0] finishes we can reuse its ID */
    block_job_early_fail(job[0]);
    job[1] = do_test_id(blk[1], "id0", true);

    /* No job ID specified, defaults to the backend name ('drive1') */
    block_job_early_fail(job[1]);
    job[1] = do_test_id(blk[1], NULL, true);

    /* Duplicate job ID */
    job[2] = do_test_id(blk[2], "drive1", false);

    /* The ID of job[2] would default to 'drive2' but it is already in use */
    job[0] = do_test_id(blk[0], "drive2", true);
    job[2] = do_test_id(blk[2], NULL, false);

    /* This one is valid */
    job[2] = do_test_id(blk[2], "id_2", true);

    block_job_early_fail(job[0]);
    block_job_early_fail(job[1]);
    block_job_early_fail(job[2]);

    destroy_blk(blk[0]);
    destroy_blk(blk[1]);
    destroy_blk(blk[2]);
}

int main(int argc, char **argv)
{
    qemu_init_main_loop(&error_abort);
    bdrv_init();

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/blockjob/ids", test_job_ids);
    return g_test_run();
}
