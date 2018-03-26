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

static int throttle_configure_tgm(BlockDriverState *bs,
                                  ThrottleGroupMember *tgm,
                                  QDict *options, Error **errp)
{
    int ret;
    const char *group_name;
    Error *local_err = NULL;
    QemuOpts *opts = qemu_opts_create(&throttle_opts, NULL, 0, &error_abort);

    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
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

    /* Register membership to group with name group_name */
    throttle_group_register_tgm(tgm, group_name, bdrv_get_aio_context(bs));
    ret = 0;
fin:
    qemu_opts_del(opts);
    return ret;
}

static int throttle_open(BlockDriverState *bs, QDict *options,
                         int flags, Error **errp)
{
    ThrottleGroupMember *tgm = bs->opaque;

    bs->file = bdrv_open_child(NULL, options, "file", bs,
                               &child_file, false, errp);
    if (!bs->file) {
        return -EINVAL;
    }
    bs->supported_write_flags = bs->file->bs->supported_write_flags;
    bs->supported_zero_flags = bs->file->bs->supported_zero_flags;

    return throttle_configure_tgm(bs, tgm, options, errp);
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
                                           uint64_t offset, uint64_t bytes,
                                           QEMUIOVector *qiov, int flags)
{

    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, false);

    return bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwritev(BlockDriverState *bs,
                                            uint64_t offset, uint64_t bytes,
                                            QEMUIOVector *qiov, int flags)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pwritev(bs->file, offset, bytes, qiov, flags);
}

static int coroutine_fn throttle_co_pwrite_zeroes(BlockDriverState *bs,
                                                  int64_t offset, int bytes,
                                                  BdrvRequestFlags flags)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn throttle_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int bytes)
{
    ThrottleGroupMember *tgm = bs->opaque;
    throttle_group_co_io_limits_intercept(tgm, bytes, true);

    return bdrv_co_pdiscard(bs->file->bs, offset, bytes);
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
    ThrottleGroupMember *tgm;

    assert(reopen_state != NULL);
    assert(reopen_state->bs != NULL);

    reopen_state->opaque = g_new0(ThrottleGroupMember, 1);
    tgm = reopen_state->opaque;

    return throttle_configure_tgm(reopen_state->bs, tgm, reopen_state->options,
            errp);
}

static void throttle_reopen_commit(BDRVReopenState *reopen_state)
{
    ThrottleGroupMember *old_tgm = reopen_state->bs->opaque;
    ThrottleGroupMember *new_tgm = reopen_state->opaque;

    throttle_group_unregister_tgm(old_tgm);
    g_free(old_tgm);
    reopen_state->bs->opaque = new_tgm;
    reopen_state->opaque = NULL;
}

static void throttle_reopen_abort(BDRVReopenState *reopen_state)
{
    ThrottleGroupMember *tgm = reopen_state->opaque;

    throttle_group_unregister_tgm(tgm);
    g_free(tgm);
    reopen_state->opaque = NULL;
}

static bool throttle_recurse_is_first_non_filter(BlockDriverState *bs,
                                                 BlockDriverState *candidate)
{
    return bdrv_recurse_is_first_non_filter(bs->file->bs, candidate);
}

static void coroutine_fn throttle_co_drain_begin(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    if (atomic_fetch_inc(&tgm->io_limits_disabled) == 0) {
        throttle_group_restart_tgm(tgm);
    }
}

static void coroutine_fn throttle_co_drain_end(BlockDriverState *bs)
{
    ThrottleGroupMember *tgm = bs->opaque;
    assert(tgm->io_limits_disabled);
    atomic_dec(&tgm->io_limits_disabled);
}

static BlockDriver bdrv_throttle = {
    .format_name                        =   "throttle",
    .instance_size                      =   sizeof(ThrottleGroupMember),

    .bdrv_open                          =   throttle_open,
    .bdrv_close                         =   throttle_close,
    .bdrv_co_flush                      =   throttle_co_flush,

    .bdrv_child_perm                    =   bdrv_filter_default_perms,

    .bdrv_getlength                     =   throttle_getlength,

    .bdrv_co_preadv                     =   throttle_co_preadv,
    .bdrv_co_pwritev                    =   throttle_co_pwritev,

    .bdrv_co_pwrite_zeroes              =   throttle_co_pwrite_zeroes,
    .bdrv_co_pdiscard                   =   throttle_co_pdiscard,

    .bdrv_recurse_is_first_non_filter   =   throttle_recurse_is_first_non_filter,

    .bdrv_attach_aio_context            =   throttle_attach_aio_context,
    .bdrv_detach_aio_context            =   throttle_detach_aio_context,

    .bdrv_reopen_prepare                =   throttle_reopen_prepare,
    .bdrv_reopen_commit                 =   throttle_reopen_commit,
    .bdrv_reopen_abort                  =   throttle_reopen_abort,
    .bdrv_co_block_status               =   bdrv_co_block_status_from_file,

    .bdrv_co_drain_begin                =   throttle_co_drain_begin,
    .bdrv_co_drain_end                  =   throttle_co_drain_end,

    .is_filter                          =   true,
};

static void bdrv_throttle_init(void)
{
    bdrv_register(&bdrv_throttle);
}

block_init(bdrv_throttle_init);
