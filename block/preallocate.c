/*
 * preallocate filter driver
 *
 * The driver performs preallocate operation: it is injected above
 * some node, and before each write over EOF it does additional preallocating
 * write-zeroes request.
 *
 * Copyright (c) 2020 Virtuozzo International GmbH.
 *
 * Author:
 *  Sementsov-Ogievskiy Vladimir <vsementsov@virtuozzo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/units.h"
#include "block/block-io.h"
#include "block/block_int.h"


typedef struct PreallocateOpts {
    int64_t prealloc_size;
    int64_t prealloc_align;
} PreallocateOpts;

typedef struct BDRVPreallocateState {
    PreallocateOpts opts;

    /*
     * Track real data end, to crop preallocation on close. If < 0 the status is
     * unknown.
     *
     * @data_end is a maximum of file size on open (or when we get write/resize
     * permissions) and all write request ends after it. So it's safe to
     * truncate to data_end if it is valid.
     */
    int64_t data_end;

    /*
     * Start of trailing preallocated area which reads as zero. May be smaller
     * than data_end, if user does over-EOF write zero operation. If < 0 the
     * status is unknown.
     *
     * If both @zero_start and @file_end are valid, the region
     * [@zero_start, @file_end) is known to be preallocated zeroes. If @file_end
     * is not valid, @zero_start doesn't make much sense.
     */
    int64_t zero_start;

    /*
     * Real end of file. Actually the cache for bdrv_getlength(bs->file->bs),
     * to avoid extra lseek() calls on each write operation. If < 0 the status
     * is unknown.
     */
    int64_t file_end;

    /*
     * All three states @data_end, @zero_start and @file_end are guaranteed to
     * be invalid (< 0) when we don't have both exclusive BLK_PERM_RESIZE and
     * BLK_PERM_WRITE permissions on file child.
     */
} BDRVPreallocateState;

#define PREALLOCATE_OPT_PREALLOC_ALIGN "prealloc-align"
#define PREALLOCATE_OPT_PREALLOC_SIZE "prealloc-size"
static QemuOptsList runtime_opts = {
    .name = "preallocate",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = PREALLOCATE_OPT_PREALLOC_ALIGN,
            .type = QEMU_OPT_SIZE,
            .help = "on preallocation, align file length to this number, "
                "default 1M",
        },
        {
            .name = PREALLOCATE_OPT_PREALLOC_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "how much to preallocate, default 128M",
        },
        { /* end of list */ }
    },
};

static bool preallocate_absorb_opts(PreallocateOpts *dest, QDict *options,
                                    BlockDriverState *child_bs, Error **errp)
{
    QemuOpts *opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);

    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        return false;
    }

    dest->prealloc_align =
        qemu_opt_get_size(opts, PREALLOCATE_OPT_PREALLOC_ALIGN, 1 * MiB);
    dest->prealloc_size =
        qemu_opt_get_size(opts, PREALLOCATE_OPT_PREALLOC_SIZE, 128 * MiB);

    qemu_opts_del(opts);

    if (!QEMU_IS_ALIGNED(dest->prealloc_align, BDRV_SECTOR_SIZE)) {
        error_setg(errp, "prealloc-align parameter of preallocate filter "
                   "is not aligned to %llu", BDRV_SECTOR_SIZE);
        return false;
    }

    if (!QEMU_IS_ALIGNED(dest->prealloc_align,
                         child_bs->bl.request_alignment)) {
        error_setg(errp, "prealloc-align parameter of preallocate filter "
                   "is not aligned to underlying node request alignment "
                   "(%" PRIi32 ")", child_bs->bl.request_alignment);
        return false;
    }

    return true;
}

static int preallocate_open(BlockDriverState *bs, QDict *options, int flags,
                            Error **errp)
{
    BDRVPreallocateState *s = bs->opaque;
    int ret;

    /*
     * s->data_end and friends should be initialized on permission update.
     * For this to work, mark them invalid.
     */
    s->file_end = s->zero_start = s->data_end = -EINVAL;

    ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    if (!preallocate_absorb_opts(&s->opts, options, bs->file->bs, errp)) {
        return -EINVAL;
    }

    bs->supported_write_flags = BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);

    bs->supported_zero_flags = BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
            bs->file->bs->supported_zero_flags);

    return 0;
}

static void preallocate_close(BlockDriverState *bs)
{
    int ret;
    BDRVPreallocateState *s = bs->opaque;

    if (s->data_end < 0) {
        return;
    }

    if (s->file_end < 0) {
        s->file_end = bdrv_getlength(bs->file->bs);
        if (s->file_end < 0) {
            return;
        }
    }

    if (s->data_end < s->file_end) {
        ret = bdrv_truncate(bs->file, s->data_end, true, PREALLOC_MODE_OFF, 0,
                            NULL);
        s->file_end = ret < 0 ? ret : s->data_end;
    }
}


/*
 * Handle reopen.
 *
 * We must implement reopen handlers, otherwise reopen just don't work. Handle
 * new options and don't care about preallocation state, as it is handled in
 * set/check permission handlers.
 */

static int preallocate_reopen_prepare(BDRVReopenState *reopen_state,
                                      BlockReopenQueue *queue, Error **errp)
{
    PreallocateOpts *opts = g_new0(PreallocateOpts, 1);

    if (!preallocate_absorb_opts(opts, reopen_state->options,
                                 reopen_state->bs->file->bs, errp)) {
        g_free(opts);
        return -EINVAL;
    }

    reopen_state->opaque = opts;

    return 0;
}

static void preallocate_reopen_commit(BDRVReopenState *state)
{
    BDRVPreallocateState *s = state->bs->opaque;

    s->opts = *(PreallocateOpts *)state->opaque;

    g_free(state->opaque);
    state->opaque = NULL;
}

static void preallocate_reopen_abort(BDRVReopenState *state)
{
    g_free(state->opaque);
    state->opaque = NULL;
}

static coroutine_fn int preallocate_co_preadv_part(
        BlockDriverState *bs, int64_t offset, int64_t bytes,
        QEMUIOVector *qiov, size_t qiov_offset, BdrvRequestFlags flags)
{
    return bdrv_co_preadv_part(bs->file, offset, bytes, qiov, qiov_offset,
                               flags);
}

static int coroutine_fn preallocate_co_pdiscard(BlockDriverState *bs,
                                               int64_t offset, int64_t bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static bool can_write_resize(uint64_t perm)
{
    return (perm & BLK_PERM_WRITE) && (perm & BLK_PERM_RESIZE);
}

static bool has_prealloc_perms(BlockDriverState *bs)
{
    BDRVPreallocateState *s = bs->opaque;

    if (can_write_resize(bs->file->perm)) {
        assert(!(bs->file->shared_perm & BLK_PERM_WRITE));
        assert(!(bs->file->shared_perm & BLK_PERM_RESIZE));
        return true;
    }

    assert(s->data_end < 0);
    assert(s->zero_start < 0);
    assert(s->file_end < 0);
    return false;
}

/*
 * Call on each write. Returns true if @want_merge_zero is true and the region
 * [offset, offset + bytes) is zeroed (as a result of this call or earlier
 * preallocation).
 *
 * want_merge_zero is used to merge write-zero request with preallocation in
 * one bdrv_co_pwrite_zeroes() call.
 */
static bool coroutine_fn handle_write(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes, bool want_merge_zero)
{
    BDRVPreallocateState *s = bs->opaque;
    int64_t end = offset + bytes;
    int64_t prealloc_start, prealloc_end;
    int ret;
    uint32_t file_align = bs->file->bs->bl.request_alignment;
    uint32_t prealloc_align = MAX(s->opts.prealloc_align, file_align);

    assert(QEMU_IS_ALIGNED(prealloc_align, file_align));

    if (!has_prealloc_perms(bs)) {
        /* We don't have state neither should try to recover it */
        return false;
    }

    if (s->data_end < 0) {
        s->data_end = bdrv_co_getlength(bs->file->bs);
        if (s->data_end < 0) {
            return false;
        }

        if (s->file_end < 0) {
            s->file_end = s->data_end;
        }
    }

    if (end <= s->data_end) {
        return false;
    }

    /* We have valid s->data_end, and request writes beyond it. */

    s->data_end = end;
    if (s->zero_start < 0 || !want_merge_zero) {
        s->zero_start = end;
    }

    if (s->file_end < 0) {
        s->file_end = bdrv_co_getlength(bs->file->bs);
        if (s->file_end < 0) {
            return false;
        }
    }

    /* Now s->data_end, s->zero_start and s->file_end are valid. */

    if (end <= s->file_end) {
        /* No preallocation needed. */
        return want_merge_zero && offset >= s->zero_start;
    }

    /* Now we want new preallocation, as request writes beyond s->file_end. */

    prealloc_start = QEMU_ALIGN_UP(
            want_merge_zero ? MIN(offset, s->file_end) : s->file_end,
            file_align);
    prealloc_end = QEMU_ALIGN_UP(
            MAX(prealloc_start, end) + s->opts.prealloc_size,
            prealloc_align);

    want_merge_zero = want_merge_zero && (prealloc_start <= offset);

    ret = bdrv_co_pwrite_zeroes(
            bs->file, prealloc_start, prealloc_end - prealloc_start,
            BDRV_REQ_NO_FALLBACK | BDRV_REQ_SERIALISING | BDRV_REQ_NO_WAIT);
    if (ret < 0) {
        s->file_end = ret;
        return false;
    }

    s->file_end = prealloc_end;
    return want_merge_zero;
}

static int coroutine_fn preallocate_co_pwrite_zeroes(BlockDriverState *bs,
        int64_t offset, int64_t bytes, BdrvRequestFlags flags)
{
    bool want_merge_zero =
        !(flags & ~(BDRV_REQ_ZERO_WRITE | BDRV_REQ_NO_FALLBACK));
    if (handle_write(bs, offset, bytes, want_merge_zero)) {
        return 0;
    }

    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static coroutine_fn int preallocate_co_pwritev_part(BlockDriverState *bs,
                                                    int64_t offset,
                                                    int64_t bytes,
                                                    QEMUIOVector *qiov,
                                                    size_t qiov_offset,
                                                    BdrvRequestFlags flags)
{
    handle_write(bs, offset, bytes, false);

    return bdrv_co_pwritev_part(bs->file, offset, bytes, qiov, qiov_offset,
                                flags);
}

static int coroutine_fn
preallocate_co_truncate(BlockDriverState *bs, int64_t offset,
                        bool exact, PreallocMode prealloc,
                        BdrvRequestFlags flags, Error **errp)
{
    ERRP_GUARD();
    BDRVPreallocateState *s = bs->opaque;
    int ret;

    if (s->data_end >= 0 && offset > s->data_end) {
        if (s->file_end < 0) {
            s->file_end = bdrv_co_getlength(bs->file->bs);
            if (s->file_end < 0) {
                error_setg(errp, "failed to get file length");
                return s->file_end;
            }
        }

        if (prealloc == PREALLOC_MODE_FALLOC) {
            /*
             * If offset <= s->file_end, the task is already done, just
             * update s->data_end, to move part of "filter preallocation"
             * to "preallocation requested by user".
             * Otherwise just proceed to preallocate missing part.
             */
            if (offset <= s->file_end) {
                s->data_end = offset;
                return 0;
            }
        } else {
            /*
             * We have to drop our preallocation, to
             * - avoid "Cannot use preallocation for shrinking files" in
             *   case of offset < file_end
             * - give PREALLOC_MODE_OFF a chance to keep small disk
             *   usage
             * - give PREALLOC_MODE_FULL a chance to actually write the
             *   whole region as user expects
             */
            if (s->file_end > s->data_end) {
                ret = bdrv_co_truncate(bs->file, s->data_end, true,
                                       PREALLOC_MODE_OFF, 0, errp);
                if (ret < 0) {
                    s->file_end = ret;
                    error_prepend(errp, "preallocate-filter: failed to drop "
                                  "write-zero preallocation: ");
                    return ret;
                }
                s->file_end = s->data_end;
            }
        }

        s->data_end = offset;
    }

    ret = bdrv_co_truncate(bs->file, offset, exact, prealloc, flags, errp);
    if (ret < 0) {
        s->file_end = s->zero_start = s->data_end = ret;
        return ret;
    }

    if (has_prealloc_perms(bs)) {
        s->file_end = s->zero_start = s->data_end = offset;
    }
    return 0;
}

static int coroutine_fn preallocate_co_flush(BlockDriverState *bs)
{
    return bdrv_co_flush(bs->file->bs);
}

static int64_t coroutine_fn preallocate_co_getlength(BlockDriverState *bs)
{
    int64_t ret;
    BDRVPreallocateState *s = bs->opaque;

    if (s->data_end >= 0) {
        return s->data_end;
    }

    ret = bdrv_co_getlength(bs->file->bs);

    if (has_prealloc_perms(bs)) {
        s->file_end = s->zero_start = s->data_end = ret;
    }

    return ret;
}

static int preallocate_check_perm(BlockDriverState *bs,
                                  uint64_t perm, uint64_t shared, Error **errp)
{
    BDRVPreallocateState *s = bs->opaque;

    if (s->data_end >= 0 && !can_write_resize(perm)) {
        /*
         * Lose permissions.
         * We should truncate in check_perm, as in set_perm bs->file->perm will
         * be already changed, and we should not violate it.
         */
        if (s->file_end < 0) {
            s->file_end = bdrv_getlength(bs->file->bs);
            if (s->file_end < 0) {
                error_setg(errp, "Failed to get file length");
                return s->file_end;
            }
        }

        if (s->data_end < s->file_end) {
            int ret = bdrv_truncate(bs->file, s->data_end, true,
                                    PREALLOC_MODE_OFF, 0, NULL);
            if (ret < 0) {
                error_setg(errp, "Failed to drop preallocation");
                s->file_end = ret;
                return ret;
            }
            s->file_end = s->data_end;
        }
    }

    return 0;
}

static void preallocate_set_perm(BlockDriverState *bs,
                                 uint64_t perm, uint64_t shared)
{
    BDRVPreallocateState *s = bs->opaque;

    if (can_write_resize(perm)) {
        if (s->data_end < 0) {
            s->data_end = s->file_end = s->zero_start =
                bdrv_getlength(bs->file->bs);
        }
    } else {
        /*
         * We drop our permissions, as well as allow shared
         * permissions (see preallocate_child_perm), anyone will be able to
         * change the child, so mark all states invalid. We'll regain control if
         * get good permissions back.
         */
        s->data_end = s->file_end = s->zero_start = -EINVAL;
    }
}

static void preallocate_child_perm(BlockDriverState *bs, BdrvChild *c,
    BdrvChildRole role, BlockReopenQueue *reopen_queue,
    uint64_t perm, uint64_t shared, uint64_t *nperm, uint64_t *nshared)
{
    bdrv_default_perms(bs, c, role, reopen_queue, perm, shared, nperm, nshared);

    if (can_write_resize(perm)) {
        /* This should come by default, but let's enforce: */
        *nperm |= BLK_PERM_WRITE | BLK_PERM_RESIZE;

        /*
         * Don't share, to keep our states s->file_end, s->data_end and
         * s->zero_start valid.
         */
        *nshared &= ~(BLK_PERM_WRITE | BLK_PERM_RESIZE);
    }
}

BlockDriver bdrv_preallocate_filter = {
    .format_name = "preallocate",
    .instance_size = sizeof(BDRVPreallocateState),

    .bdrv_co_getlength    = preallocate_co_getlength,
    .bdrv_open            = preallocate_open,
    .bdrv_close           = preallocate_close,

    .bdrv_reopen_prepare  = preallocate_reopen_prepare,
    .bdrv_reopen_commit   = preallocate_reopen_commit,
    .bdrv_reopen_abort    = preallocate_reopen_abort,

    .bdrv_co_preadv_part = preallocate_co_preadv_part,
    .bdrv_co_pwritev_part = preallocate_co_pwritev_part,
    .bdrv_co_pwrite_zeroes = preallocate_co_pwrite_zeroes,
    .bdrv_co_pdiscard = preallocate_co_pdiscard,
    .bdrv_co_flush = preallocate_co_flush,
    .bdrv_co_truncate = preallocate_co_truncate,

    .bdrv_check_perm = preallocate_check_perm,
    .bdrv_set_perm = preallocate_set_perm,
    .bdrv_child_perm = preallocate_child_perm,

    .has_variable_length = true,
    .is_filter = true,
};

static void bdrv_preallocate_init(void)
{
    bdrv_register(&bdrv_preallocate_filter);
}

block_init(bdrv_preallocate_init);
