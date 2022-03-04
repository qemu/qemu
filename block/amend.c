/*
 * Block layer code related to image options amend
 *
 * Copyright (c) 2018 Kevin Wolf <kwolf@redhat.com>
 * Copyright (c) 2020 Red Hat. Inc
 *
 * Heavily based on create.c
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
#include "block/block_int.h"
#include "qemu/job.h"
#include "qemu/main-loop.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/clone-visitor.h"
#include "qapi/error.h"

typedef struct BlockdevAmendJob {
    Job common;
    BlockdevAmendOptions *opts;
    BlockDriverState *bs;
    bool force;
} BlockdevAmendJob;

static int coroutine_fn blockdev_amend_run(Job *job, Error **errp)
{
    BlockdevAmendJob *s = container_of(job, BlockdevAmendJob, common);
    int ret;

    job_progress_set_remaining(&s->common, 1);
    ret = s->bs->drv->bdrv_co_amend(s->bs, s->opts, s->force, errp);
    job_progress_update(&s->common, 1);
    qapi_free_BlockdevAmendOptions(s->opts);
    return ret;
}

static int blockdev_amend_pre_run(BlockdevAmendJob *s, Error **errp)
{
    if (s->bs->drv->bdrv_amend_pre_run) {
        return s->bs->drv->bdrv_amend_pre_run(s->bs, errp);
    }

    return 0;
}

static void blockdev_amend_free(Job *job)
{
    BlockdevAmendJob *s = container_of(job, BlockdevAmendJob, common);

    if (s->bs->drv->bdrv_amend_clean) {
        s->bs->drv->bdrv_amend_clean(s->bs);
    }

    bdrv_unref(s->bs);
}

static const JobDriver blockdev_amend_job_driver = {
    .instance_size = sizeof(BlockdevAmendJob),
    .job_type      = JOB_TYPE_AMEND,
    .run           = blockdev_amend_run,
    .free          = blockdev_amend_free,
};

void qmp_x_blockdev_amend(const char *job_id,
                          const char *node_name,
                          BlockdevAmendOptions *options,
                          bool has_force,
                          bool force,
                          Error **errp)
{
    BlockdevAmendJob *s;
    const char *fmt = BlockdevDriver_str(options->driver);
    BlockDriver *drv = bdrv_find_format(fmt);
    BlockDriverState *bs;

    bs = bdrv_lookup_bs(NULL, node_name, errp);
    if (!bs) {
        return;
    }

    if (!drv) {
        error_setg(errp, "Block driver '%s' not found or not supported", fmt);
        return;
    }

    /*
     * If the driver is in the schema, we know that it exists. But it may not
     * be whitelisted.
     */
    if (bdrv_uses_whitelist() && !bdrv_is_whitelisted(drv, false)) {
        error_setg(errp, "Driver is not whitelisted");
        return;
    }

    if (bs->drv != drv) {
        error_setg(errp,
                   "x-blockdev-amend doesn't support changing the block driver");
        return;
    }

    /* Error out if the driver doesn't support .bdrv_co_amend */
    if (!drv->bdrv_co_amend) {
        error_setg(errp, "Driver does not support x-blockdev-amend");
        return;
    }

    /* Create the block job */
    s = job_create(job_id, &blockdev_amend_job_driver, NULL,
                   bdrv_get_aio_context(bs), JOB_DEFAULT | JOB_MANUAL_DISMISS,
                   NULL, NULL, errp);
    if (!s) {
        return;
    }

    bdrv_ref(bs);
    s->bs = bs,
    s->opts = QAPI_CLONE(BlockdevAmendOptions, options),
    s->force = has_force ? force : false;

    if (blockdev_amend_pre_run(s, errp)) {
        job_early_fail(&s->common);
        return;
    }

    job_start(&s->common);
}
