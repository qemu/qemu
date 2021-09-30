/*
 * QEMU block throttling filter driver infrastructure
 *
 * Copyright (c) 2017 Manos Pitsidianakis
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "block/throttle-groups.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/throttle-options.h"
#include "qapi/error.h"

static QemuOptsList throttle_opts = {
    .name = "throttle",
    .head = QTAILQ_HEAD_INITIALIZER(throttle_opts.head),
    .desc = {
        {
            .name = QEMU_OPT_THROTTLE_GROUP_NAME,
            .type = QEMU_OPT_STRING,
            .help = "Name of the throttle group",
        },
        { /* end of list */ }
    },
};

/*
 * If this function succeeds then the throttle group name is stored in
 * @group and must be freed by the caller.
 * If there's an error then @group remains unmodified.
 */
static int throttle_parse_options(QDict *options, char **group, Error **errp)
{
    int ret;
    const char *group_name;
    QemuOpts *opts = qemu_opts_create(&throttle_opts, NULL, 0, &error_abort);

    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto fin;
    }

    group_name = qemu_opt_get(opts, QEMU_OPT_THROTTLE_GROUP_NAME);
    if (!group_name) {
        error_setg(errp, "Please specify a throttle group");
        ret = -EINVAL;
        goto fin;
    } else if (!throttle_group_exists(group_name)) {
        error_setg(errp, "Throttle group '%s' does not exist", group_name);
        ret = -EINVAL;
        goto fin;
    }

    *group = g_strdup(group_name);
    ret = 0;
fin:
    qemu_opts_del(opts);
    return ret;
}

static int throttle_open(BlockDriverState *bs, QDict *options,
                         int flags, Error **errp)
{
    ThrottleGroupMember *tgm = bs->opaque;
    char *group;
    int ret;

    bs->file = bdrv_open_child(NULL, options, "file", bs, &child_of_bds,
                               BDRV_CHILD_FILTERED | BDRV_CHILD_PRIMARY,
                               false, errp);
    if (!bs->file) {
        return -EINVAL;
    }
    bs->supported_write_flags = bs->file->bs->supported_write_flags |
                                BDRV_REQ_WRITE_UNCHANGED;
    bs->supported_zero_flags = bs->file->bs->supported_zero_flags |
                               BDRV_REQ_WRITE_UNCHANGED;

    ret = throttle_parse_options(options, &group, errp);
    if (ret == 0) {
        /* Register membership to group with name group_name */
        throttle_group_register_tgm(tgm, group, bdrv_get_aio_context(bs));
        g_free(group);
    }

    return ret;
}

static void throttle_close(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_unregister_tgm(tgm);
}


static int64_t throttle_getlength(BlockDriverState *bs)
{
    return bdrv_getlength(bs->file->bs);
}

static int coroutine_fn throttle_co_preadv(BlockDriverState *bs,
                                           int64_t offset, int64_t bytes,
                                           QEMUIOVector *qiov,
                                           BdrvRequestFlags flags)
{

    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, false);

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwritev(BlockDriverState *bs,
                                            int64_t offset, int64_t bytes,
                                            QEMUIOVector *qiov,
                                            BdrvRequestFlags flags)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwrite_zeroes(BlockDriverState *bs,
                                                  int64_t offset, int64_t bytes,
                                                  BdrvRequestFlags flags)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn throttle_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int64_t bytes)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static int coroutine_fn throttle_co_pwritev_compressed(BlockDriverState *bs,
                                                       int64_t offset,
                                                       int64_t bytes,
                                                       QEMUIOVector *qiov)
{
    return throttle_co_pwritev(bs, offset, bytes, qiov,
                               BDRV_REQ_WRITE_COMPRESSED);
}

static int throttle_co_flush(BlockDriverState *bs)
{
    return bdrv_co_flush(bs->file->bs);
}

static void throttle_detach_aio_context(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_detach_aio_context(tgm);
}

static void throttle_attach_aio_context(BlockDriverState *bs,
                                        AioContext *new_context)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_attach_aio_context(tgm, new_context);
}

static int throttle_reopen_prepare(BDRVReopenState *reopen_state,
                                   BlockReopenQueue *queue, Error **errp)
{
    int ret;
    char *group = NULL;

    assert(reopen_state != NULL);
    assert(reopen_state->bs != NULL);

    ret = throttle_parse_options(reopen_state->options, &group, errp);
    reopen_state->opaque = group;
    return ret;
}

static void throttle_reopen_commit(BDRVReopenState *reopen_state)
{
    BlockDriverState *bs = reopen_state->bs;
    ThrottleGroupMember *tgm = bs->opaque;
    char *group = reopen_state->opaque;

    assert(group);

    if (strcmp(group, throttle_group_get_name(tgm))) {
        throttle_group_unregister_tgm(tgm);
        throttle_group_register_tgm(tgm, group, bdrv_get_aio_context(bs));
    }
    g_free(reopen_state->opaque);
    reopen_state->opaque = NULL;
}

static void throttle_reopen_abort(BDRVReopenState *reopen_state)
{
    g_free(reopen_state->opaque);
    reopen_state->opaque = NULL;
}

static void coroutine_fn throttle_co_drain_begin(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    if (qatomic_fetch_inc(&tgm->io_limits_disabled) == 0) {
        throttle_group_restart_tgm(tgm);
    }
}

static void coroutine_fn throttle_co_drain_end(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    assert(tgm->io_limits_disabled);
    qatomic_dec(&tgm->io_limits_disabled);
}

static const char *const throttle_strong_runtime_opts[] = {
    QEMU_OPT_THROTTLE_GROUP_NAME,

    NULL
};

static BlockDriver bdrv_throttle = {
    .format_name                        =   "throttle",
    .instance_size                      =   sizeof(ThrottleGroupMember),

    .bdrv_open                          =   throttle_open,
    .bdrv_close                         =   throttle_close,
    .bdrv_co_flush                      =   throttle_co_flush,

    .bdrv_child_perm                    =   bdrv_default_perms,

    .bdrv_getlength                     =   throttle_getlength,

    .bdrv_co_preadv                     =   throttle_co_preadv,
    .bdrv_co_pwritev                    =   throttle_co_pwritev,

    .bdrv_co_pwrite_zeroes              =   throttle_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   =   throttle_co_pdiscard,
    .bdrv_co_pwritev_compressed         =   throttle_co_pwritev_compressed,

    .bdrv_attach_aio_context            =   throttle_attach_aio_context,
    .bdrv_detach_aio_context            =   throttle_detach_aio_context,

    .bdrv_reopen_prepare                =   throttle_reopen_prepare,
    .bdrv_reopen_commit                 =   throttle_reopen_commit,
    .bdrv_reopen_abort                  =   throttle_reopen_abort,

    .bdrv_co_drain_begin                =   throttle_co_drain_begin,
    .bdrv_co_drain_end                  =   throttle_co_drain_end,

    .is_filter                          =   true,
    .strong_runtime_opts                =   throttle_strong_runtime_opts,
};

static void bdrv_throttle_init(void)
{
    bdrv_register(&bdrv_throttle);
}

block_init(bdrv_throttle_init);
