/*
 * Present a block device as a raw image through FUSE
 *
 * Copyright (c) 2020 Max Reitz <mreitz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 or later of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define FUSE_USE_VERSION 31

#include "qemu/osdep.h"
#include "block/aio.h"
#include "block/block.h"
#include "block/export.h"
#include "block/fuse.h"
#include "block/qapi.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block.h"
#include "sysemu/block-backend.h"

#include <fuse.h>
#include <fuse_lowlevel.h>


/* Prevent overly long bounce buffer allocations */
#define FUSE_MAX_BOUNCE_BYTES (MIN(BDRV_REQUEST_MAX_BYTES, 64 * 1024 * 1024))


typedef struct FuseExport {
    BlockExport common;

    struct fuse_session *fuse_session;
    struct fuse_buf fuse_buf;
    bool mounted, fd_handler_set_up;

    char *mountpoint;
    bool writable;
} FuseExport;

static GHashTable *exports;
static const struct fuse_lowlevel_ops fuse_ops;

static void fuse_export_shutdown(BlockExport *exp);
static void fuse_export_delete(BlockExport *exp);

static void init_exports_table(void);

static int setup_fuse_export(FuseExport *exp, const char *mountpoint,
                             Error **errp);
static void read_from_fuse_export(void *opaque);

static bool is_regular_file(const char *path, Error **errp);


static int fuse_export_create(BlockExport *blk_exp,
                              BlockExportOptions *blk_exp_args,
                              Error **errp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);
    BlockExportOptionsFuse *args = &blk_exp_args->u.fuse;
    int ret;

    assert(blk_exp_args->type == BLOCK_EXPORT_TYPE_FUSE);

    init_exports_table();

    /*
     * It is important to do this check before calling is_regular_file() --
     * that function will do a stat(), which we would have to handle if we
     * already exported something on @mountpoint.  But we cannot, because
     * we are currently caught up here.
     * (Note that ideally we would want to resolve relative paths here,
     * but bdrv_make_absolute_filename() might do the wrong thing for
     * paths that contain colons, and realpath() would resolve symlinks,
     * which we do not want: The mount point is not going to be the
     * symlink's destination, but the link itself.)
     * So this will not catch all potential clashes, but hopefully at
     * least the most common one of specifying exactly the same path
     * string twice.
     */
    if (g_hash_table_contains(exports, args->mountpoint)) {
        error_setg(errp, "There already is a FUSE export on '%s'",
                   args->mountpoint);
        ret = -EEXIST;
        goto fail;
    }

    if (!is_regular_file(args->mountpoint, errp)) {
        ret = -EINVAL;
        goto fail;
    }

    exp->mountpoint = g_strdup(args->mountpoint);
    exp->writable = blk_exp_args->writable;

    ret = setup_fuse_export(exp, args->mountpoint, errp);
    if (ret < 0) {
        goto fail;
    }

    return 0;

fail:
    fuse_export_delete(blk_exp);
    return ret;
}

/**
 * Allocates the global @exports hash table.
 */
static void init_exports_table(void)
{
    if (exports) {
        return;
    }

    exports = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

/**
 * Create exp->fuse_session and mount it.
 */
static int setup_fuse_export(FuseExport *exp, const char *mountpoint,
                             Error **errp)
{
    const char *fuse_argv[4];
    char *mount_opts;
    struct fuse_args fuse_args;
    int ret;

    /* Needs to match what fuse_init() sets.  Only max_read must be supplied. */
    mount_opts = g_strdup_printf("max_read=%zu", FUSE_MAX_BOUNCE_BYTES);

    fuse_argv[0] = ""; /* Dummy program name */
    fuse_argv[1] = "-o";
    fuse_argv[2] = mount_opts;
    fuse_argv[3] = NULL;
    fuse_args = (struct fuse_args)FUSE_ARGS_INIT(3, (char **)fuse_argv);

    exp->fuse_session = fuse_session_new(&fuse_args, &fuse_ops,
                                         sizeof(fuse_ops), exp);
    g_free(mount_opts);
    if (!exp->fuse_session) {
        error_setg(errp, "Failed to set up FUSE session");
        ret = -EIO;
        goto fail;
    }

    ret = fuse_session_mount(exp->fuse_session, mountpoint);
    if (ret < 0) {
        error_setg(errp, "Failed to mount FUSE session to export");
        ret = -EIO;
        goto fail;
    }
    exp->mounted = true;

    g_hash_table_insert(exports, g_strdup(mountpoint), NULL);

    aio_set_fd_handler(exp->common.ctx,
                       fuse_session_fd(exp->fuse_session), true,
                       read_from_fuse_export, NULL, NULL, exp);
    exp->fd_handler_set_up = true;

    return 0;

fail:
    fuse_export_shutdown(&exp->common);
    return ret;
}

/**
 * Callback to be invoked when the FUSE session FD can be read from.
 * (This is basically the FUSE event loop.)
 */
static void read_from_fuse_export(void *opaque)
{
    FuseExport *exp = opaque;
    int ret;

    blk_exp_ref(&exp->common);

    do {
        ret = fuse_session_receive_buf(exp->fuse_session, &exp->fuse_buf);
    } while (ret == -EINTR);
    if (ret < 0) {
        goto out;
    }

    fuse_session_process_buf(exp->fuse_session, &exp->fuse_buf);

out:
    blk_exp_unref(&exp->common);
}

static void fuse_export_shutdown(BlockExport *blk_exp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);

    if (exp->fuse_session) {
        fuse_session_exit(exp->fuse_session);

        if (exp->fd_handler_set_up) {
            aio_set_fd_handler(exp->common.ctx,
                               fuse_session_fd(exp->fuse_session), true,
                               NULL, NULL, NULL, NULL);
            exp->fd_handler_set_up = false;
        }
    }

    if (exp->mountpoint) {
        /*
         * Safe to drop now, because we will not handle any requests
         * for this export anymore anyway.
         */
        g_hash_table_remove(exports, exp->mountpoint);
    }
}

static void fuse_export_delete(BlockExport *blk_exp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);

    if (exp->fuse_session) {
        if (exp->mounted) {
            fuse_session_unmount(exp->fuse_session);
        }

        fuse_session_destroy(exp->fuse_session);
    }

    free(exp->fuse_buf.mem);
    g_free(exp->mountpoint);
}

/**
 * Check whether @path points to a regular file.  If not, put an
 * appropriate message into *errp.
 */
static bool is_regular_file(const char *path, Error **errp)
{
    struct stat statbuf;
    int ret;

    ret = stat(path, &statbuf);
    if (ret < 0) {
        error_setg_errno(errp, errno, "Failed to stat '%s'", path);
        return false;
    }

    if (!S_ISREG(statbuf.st_mode)) {
        error_setg(errp, "'%s' is not a regular file", path);
        return false;
    }

    return true;
}

/**
 * A chance to set change some parameters supplied to FUSE_INIT.
 */
static void fuse_init(void *userdata, struct fuse_conn_info *conn)
{
    /*
     * MIN_NON_ZERO() would not be wrong here, but what we set here
     * must equal what has been passed to fuse_session_new().
     * Therefore, as long as max_read must be passed as a mount option
     * (which libfuse claims will be changed at some point), we have
     * to set max_read to a fixed value here.
     */
    conn->max_read = FUSE_MAX_BOUNCE_BYTES;

    conn->max_write = MIN_NON_ZERO(BDRV_REQUEST_MAX_BYTES, conn->max_write);
}

static const struct fuse_lowlevel_ops fuse_ops = {
    .init       = fuse_init,
};

const BlockExportDriver blk_exp_fuse = {
    .type               = BLOCK_EXPORT_TYPE_FUSE,
    .instance_size      = sizeof(FuseExport),
    .create             = fuse_export_create,
    .delete             = fuse_export_delete,
    .request_shutdown   = fuse_export_shutdown,
};
