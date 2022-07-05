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
#include "qemu/memalign.h"
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

#if defined(CONFIG_FALLOCATE_ZERO_RANGE)
#include <linux/falloc.h>
#endif

#ifdef __linux__
#include <linux/fs.h>
#endif

/* Prevent overly long bounce buffer allocations */
#define FUSE_MAX_BOUNCE_BYTES (MIN(BDRV_REQUEST_MAX_BYTES, 64 * 1024 * 1024))


typedef struct FuseExport {
    BlockExport common;

    struct fuse_session *fuse_session;
    struct fuse_buf fuse_buf;
    bool mounted, fd_handler_set_up;

    char *mountpoint;
    bool writable;
    bool growable;
    /* Whether allow_other was used as a mount option or not */
    bool allow_other;

    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
} FuseExport;

static GHashTable *exports;
static const struct fuse_lowlevel_ops fuse_ops;

static void fuse_export_shutdown(BlockExport *exp);
static void fuse_export_delete(BlockExport *exp);

static void init_exports_table(void);

static int setup_fuse_export(FuseExport *exp, const char *mountpoint,
                             bool allow_other, Error **errp);
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

    /* For growable and writable exports, take the RESIZE permission */
    if (args->growable || blk_exp_args->writable) {
        uint64_t blk_perm, blk_shared_perm;

        blk_get_perm(exp->common.blk, &blk_perm, &blk_shared_perm);

        ret = blk_set_perm(exp->common.blk, blk_perm | BLK_PERM_RESIZE,
                           blk_shared_perm, errp);
        if (ret < 0) {
            return ret;
        }
    }

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
    exp->growable = args->growable;

    /* set default */
    if (!args->has_allow_other) {
        args->allow_other = FUSE_EXPORT_ALLOW_OTHER_AUTO;
    }

    exp->st_mode = S_IFREG | S_IRUSR;
    if (exp->writable) {
        exp->st_mode |= S_IWUSR;
    }
    exp->st_uid = getuid();
    exp->st_gid = getgid();

    if (args->allow_other == FUSE_EXPORT_ALLOW_OTHER_AUTO) {
        /* Ignore errors on our first attempt */
        ret = setup_fuse_export(exp, args->mountpoint, true, NULL);
        exp->allow_other = ret == 0;
        if (ret < 0) {
            ret = setup_fuse_export(exp, args->mountpoint, false, errp);
        }
    } else {
        exp->allow_other = args->allow_other == FUSE_EXPORT_ALLOW_OTHER_ON;
        ret = setup_fuse_export(exp, args->mountpoint, exp->allow_other, errp);
    }
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
                             bool allow_other, Error **errp)
{
    const char *fuse_argv[4];
    char *mount_opts;
    struct fuse_args fuse_args;
    int ret;

    /*
     * max_read needs to match what fuse_init() sets.
     * max_write need not be supplied.
     */
    mount_opts = g_strdup_printf("max_read=%zu,default_permissions%s",
                                 FUSE_MAX_BOUNCE_BYTES,
                                 allow_other ? ",allow_other" : "");

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
                       read_from_fuse_export, NULL, NULL, NULL, exp);
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
                               NULL, NULL, NULL, NULL, NULL);
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

/**
 * Let clients look up files.  Always return ENOENT because we only
 * care about the mountpoint itself.
 */
static void fuse_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    fuse_reply_err(req, ENOENT);
}

/**
 * Let clients get file attributes (i.e., stat() the file).
 */
static void fuse_getattr(fuse_req_t req, fuse_ino_t inode,
                         struct fuse_file_info *fi)
{
    struct stat statbuf;
    int64_t length, allocated_blocks;
    time_t now = time(NULL);
    FuseExport *exp = fuse_req_userdata(req);

    length = blk_getlength(exp->common.blk);
    if (length < 0) {
        fuse_reply_err(req, -length);
        return;
    }

    allocated_blocks = bdrv_get_allocated_file_size(blk_bs(exp->common.blk));
    if (allocated_blocks <= 0) {
        allocated_blocks = DIV_ROUND_UP(length, 512);
    } else {
        allocated_blocks = DIV_ROUND_UP(allocated_blocks, 512);
    }

    statbuf = (struct stat) {
        .st_ino     = inode,
        .st_mode    = exp->st_mode,
        .st_nlink   = 1,
        .st_uid     = exp->st_uid,
        .st_gid     = exp->st_gid,
        .st_size    = length,
        .st_blksize = blk_bs(exp->common.blk)->bl.request_alignment,
        .st_blocks  = allocated_blocks,
        .st_atime   = now,
        .st_mtime   = now,
        .st_ctime   = now,
    };

    fuse_reply_attr(req, &statbuf, 1.);
}

static int fuse_do_truncate(const FuseExport *exp, int64_t size,
                            bool req_zero_write, PreallocMode prealloc)
{
    uint64_t blk_perm, blk_shared_perm;
    BdrvRequestFlags truncate_flags = 0;
    bool add_resize_perm;
    int ret, ret_check;

    /* Growable and writable exports have a permanent RESIZE permission */
    add_resize_perm = !exp->growable && !exp->writable;

    if (req_zero_write) {
        truncate_flags |= BDRV_REQ_ZERO_WRITE;
    }

    if (add_resize_perm) {

        if (!qemu_in_main_thread()) {
            /* Changing permissions like below only works in the main thread */
            return -EPERM;
        }

        blk_get_perm(exp->common.blk, &blk_perm, &blk_shared_perm);

        ret = blk_set_perm(exp->common.blk, blk_perm | BLK_PERM_RESIZE,
                           blk_shared_perm, NULL);
        if (ret < 0) {
            return ret;
        }
    }

    ret = blk_truncate(exp->common.blk, size, true, prealloc,
                       truncate_flags, NULL);

    if (add_resize_perm) {
        /* Must succeed, because we are only giving up the RESIZE permission */
        ret_check = blk_set_perm(exp->common.blk, blk_perm,
                                 blk_shared_perm, &error_abort);
        assert(ret_check == 0);
    }

    return ret;
}

/**
 * Let clients set file attributes.  Only resizing and changing
 * permissions (st_mode, st_uid, st_gid) is allowed.
 * Changing permissions is only allowed as far as it will actually
 * permit access: Read-only exports cannot be given +w, and exports
 * without allow_other cannot be given a different UID or GID, and
 * they cannot be given non-owner access.
 */
static void fuse_setattr(fuse_req_t req, fuse_ino_t inode, struct stat *statbuf,
                         int to_set, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int supported_attrs;
    int ret;

    supported_attrs = FUSE_SET_ATTR_SIZE | FUSE_SET_ATTR_MODE;
    if (exp->allow_other) {
        supported_attrs |= FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID;
    }

    if (to_set & ~supported_attrs) {
        fuse_reply_err(req, ENOTSUP);
        return;
    }

    /* Do some argument checks first before committing to anything */
    if (to_set & FUSE_SET_ATTR_MODE) {
        /*
         * Without allow_other, non-owners can never access the export, so do
         * not allow setting permissions for them
         */
        if (!exp->allow_other &&
            (statbuf->st_mode & (S_IRWXG | S_IRWXO)) != 0)
        {
            fuse_reply_err(req, EPERM);
            return;
        }

        /* +w for read-only exports makes no sense, disallow it */
        if (!exp->writable &&
            (statbuf->st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0)
        {
            fuse_reply_err(req, EROFS);
            return;
        }
    }

    if (to_set & FUSE_SET_ATTR_SIZE) {
        if (!exp->writable) {
            fuse_reply_err(req, EACCES);
            return;
        }

        ret = fuse_do_truncate(exp, statbuf->st_size, true, PREALLOC_MODE_OFF);
        if (ret < 0) {
            fuse_reply_err(req, -ret);
            return;
        }
    }

    if (to_set & FUSE_SET_ATTR_MODE) {
        /* Ignore FUSE-supplied file type, only change the mode */
        exp->st_mode = (statbuf->st_mode & 07777) | S_IFREG;
    }

    if (to_set & FUSE_SET_ATTR_UID) {
        exp->st_uid = statbuf->st_uid;
    }

    if (to_set & FUSE_SET_ATTR_GID) {
        exp->st_gid = statbuf->st_gid;
    }

    fuse_getattr(req, inode, fi);
}

/**
 * Let clients open a file (i.e., the exported image).
 */
static void fuse_open(fuse_req_t req, fuse_ino_t inode,
                      struct fuse_file_info *fi)
{
    fuse_reply_open(req, fi);
}

/**
 * Handle client reads from the exported image.
 */
static void fuse_read(fuse_req_t req, fuse_ino_t inode,
                      size_t size, off_t offset, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int64_t length;
    void *buf;
    int ret;

    /* Limited by max_read, should not happen */
    if (size > FUSE_MAX_BOUNCE_BYTES) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    /**
     * Clients will expect short reads at EOF, so we have to limit
     * offset+size to the image length.
     */
    length = blk_getlength(exp->common.blk);
    if (length < 0) {
        fuse_reply_err(req, -length);
        return;
    }

    if (offset + size > length) {
        size = length - offset;
    }

    buf = qemu_try_blockalign(blk_bs(exp->common.blk), size);
    if (!buf) {
        fuse_reply_err(req, ENOMEM);
        return;
    }

    ret = blk_pread(exp->common.blk, offset, size, buf, 0);
    if (ret >= 0) {
        fuse_reply_buf(req, buf, size);
    } else {
        fuse_reply_err(req, -ret);
    }

    qemu_vfree(buf);
}

/**
 * Handle client writes to the exported image.
 */
static void fuse_write(fuse_req_t req, fuse_ino_t inode, const char *buf,
                       size_t size, off_t offset, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int64_t length;
    int ret;

    /* Limited by max_write, should not happen */
    if (size > BDRV_REQUEST_MAX_BYTES) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    if (!exp->writable) {
        fuse_reply_err(req, EACCES);
        return;
    }

    /**
     * Clients will expect short writes at EOF, so we have to limit
     * offset+size to the image length.
     */
    length = blk_getlength(exp->common.blk);
    if (length < 0) {
        fuse_reply_err(req, -length);
        return;
    }

    if (offset + size > length) {
        if (exp->growable) {
            ret = fuse_do_truncate(exp, offset + size, true, PREALLOC_MODE_OFF);
            if (ret < 0) {
                fuse_reply_err(req, -ret);
                return;
            }
        } else {
            size = length - offset;
        }
    }

    ret = blk_pwrite(exp->common.blk, offset, size, buf, 0);
    if (ret >= 0) {
        fuse_reply_write(req, size);
    } else {
        fuse_reply_err(req, -ret);
    }
}

/**
 * Let clients perform various fallocate() operations.
 */
static void fuse_fallocate(fuse_req_t req, fuse_ino_t inode, int mode,
                           off_t offset, off_t length,
                           struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int64_t blk_len;
    int ret;

    if (!exp->writable) {
        fuse_reply_err(req, EACCES);
        return;
    }

    blk_len = blk_getlength(exp->common.blk);
    if (blk_len < 0) {
        fuse_reply_err(req, -blk_len);
        return;
    }

#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    if (mode & FALLOC_FL_KEEP_SIZE) {
        length = MIN(length, blk_len - offset);
    }
#endif /* CONFIG_FALLOCATE_PUNCH_HOLE */

    if (!mode) {
        /* We can only fallocate at the EOF with a truncate */
        if (offset < blk_len) {
            fuse_reply_err(req, EOPNOTSUPP);
            return;
        }

        if (offset > blk_len) {
            /* No preallocation needed here */
            ret = fuse_do_truncate(exp, offset, true, PREALLOC_MODE_OFF);
            if (ret < 0) {
                fuse_reply_err(req, -ret);
                return;
            }
        }

        ret = fuse_do_truncate(exp, offset + length, true,
                               PREALLOC_MODE_FALLOC);
    }
#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    else if (mode & FALLOC_FL_PUNCH_HOLE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE)) {
            fuse_reply_err(req, EINVAL);
            return;
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_pdiscard(exp->common.blk, offset, size);
            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif /* CONFIG_FALLOCATE_PUNCH_HOLE */
#ifdef CONFIG_FALLOCATE_ZERO_RANGE
    else if (mode & FALLOC_FL_ZERO_RANGE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + length > blk_len) {
            /* No need for zeroes, we are going to write them ourselves */
            ret = fuse_do_truncate(exp, offset + length, false,
                                   PREALLOC_MODE_OFF);
            if (ret < 0) {
                fuse_reply_err(req, -ret);
                return;
            }
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_pwrite_zeroes(exp->common.blk,
                                    offset, size, 0);
            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif /* CONFIG_FALLOCATE_ZERO_RANGE */
    else {
        ret = -EOPNOTSUPP;
    }

    fuse_reply_err(req, ret < 0 ? -ret : 0);
}

/**
 * Let clients fsync the exported image.
 */
static void fuse_fsync(fuse_req_t req, fuse_ino_t inode, int datasync,
                       struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);
    int ret;

    ret = blk_flush(exp->common.blk);
    fuse_reply_err(req, ret < 0 ? -ret : 0);
}

/**
 * Called before an FD to the exported image is closed.  (libfuse
 * notes this to be a way to return last-minute errors.)
 */
static void fuse_flush(fuse_req_t req, fuse_ino_t inode,
                        struct fuse_file_info *fi)
{
    fuse_fsync(req, inode, 1, fi);
}

#ifdef CONFIG_FUSE_LSEEK
/**
 * Let clients inquire allocation status.
 */
static void fuse_lseek(fuse_req_t req, fuse_ino_t inode, off_t offset,
                       int whence, struct fuse_file_info *fi)
{
    FuseExport *exp = fuse_req_userdata(req);

    if (whence != SEEK_HOLE && whence != SEEK_DATA) {
        fuse_reply_err(req, EINVAL);
        return;
    }

    while (true) {
        int64_t pnum;
        int ret;

        ret = bdrv_block_status_above(blk_bs(exp->common.blk), NULL,
                                      offset, INT64_MAX, &pnum, NULL, NULL);
        if (ret < 0) {
            fuse_reply_err(req, -ret);
            return;
        }

        if (!pnum && (ret & BDRV_BLOCK_EOF)) {
            int64_t blk_len;

            /*
             * If blk_getlength() rounds (e.g. by sectors), then the
             * export length will be rounded, too.  However,
             * bdrv_block_status_above() may return EOF at unaligned
             * offsets.  We must not let this become visible and thus
             * always simulate a hole between @offset (the real EOF)
             * and @blk_len (the client-visible EOF).
             */

            blk_len = blk_getlength(exp->common.blk);
            if (blk_len < 0) {
                fuse_reply_err(req, -blk_len);
                return;
            }

            if (offset > blk_len || whence == SEEK_DATA) {
                fuse_reply_err(req, ENXIO);
            } else {
                fuse_reply_lseek(req, offset);
            }
            return;
        }

        if (ret & BDRV_BLOCK_DATA) {
            if (whence == SEEK_DATA) {
                fuse_reply_lseek(req, offset);
                return;
            }
        } else {
            if (whence == SEEK_HOLE) {
                fuse_reply_lseek(req, offset);
                return;
            }
        }

        /* Safety check against infinite loops */
        if (!pnum) {
            fuse_reply_err(req, ENXIO);
            return;
        }

        offset += pnum;
    }
}
#endif

static const struct fuse_lowlevel_ops fuse_ops = {
    .init       = fuse_init,
    .lookup     = fuse_lookup,
    .getattr    = fuse_getattr,
    .setattr    = fuse_setattr,
    .open       = fuse_open,
    .read       = fuse_read,
    .write      = fuse_write,
    .fallocate  = fuse_fallocate,
    .flush      = fuse_flush,
    .fsync      = fuse_fsync,
#ifdef CONFIG_FUSE_LSEEK
    .lseek      = fuse_lseek,
#endif
};

const BlockExportDriver blk_exp_fuse = {
    .type               = BLOCK_EXPORT_TYPE_FUSE,
    .instance_size      = sizeof(FuseExport),
    .create             = fuse_export_create,
    .delete             = fuse_export_delete,
    .request_shutdown   = fuse_export_shutdown,
};
