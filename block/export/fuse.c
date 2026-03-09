/*
 * Present a block device as a raw image through FUSE
 *
 * Copyright (c) 2020, 2025 Hanna Czenczek <hreitz@redhat.com>
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
#include "qemu/aio.h"
#include "block/block_int-common.h"
#include "block/export.h"
#include "block/fuse.h"
#include "block/qapi.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block.h"
#include "qemu/coroutine.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "system/block-backend.h"

#include <fuse.h>
#include <fuse_lowlevel.h>

#include "standard-headers/linux/fuse.h"

#if defined(CONFIG_FALLOCATE_ZERO_RANGE)
#include <linux/falloc.h>
#endif

#ifdef __linux__
#include <linux/fs.h>
#endif

/* Prevent overly long bounce buffer allocations */
#define FUSE_MAX_READ_BYTES (MIN(BDRV_REQUEST_MAX_BYTES, 1 * 1024 * 1024))
#define FUSE_MAX_WRITE_BYTES (64 * 1024)

/*
 * fuse_init_in structure before 7.36.  We don't need the flags2 field added
 * there, so we can work with the smaller older structure to stay compatible
 * with older kernels.
 */
struct fuse_init_in_compat {
    uint32_t major;
    uint32_t minor;
    uint32_t max_readahead;
    uint32_t flags;
};

typedef struct FuseRequestInHeader {
    struct fuse_in_header common;
    /* All supported requests */
    union {
        struct fuse_init_in_compat init;
        struct fuse_open_in open;
        struct fuse_setattr_in setattr;
        struct fuse_read_in read;
        struct fuse_write_in write;
        struct fuse_fallocate_in fallocate;
#ifdef CONFIG_FUSE_LSEEK
        struct fuse_lseek_in lseek;
#endif
    };
} FuseRequestInHeader;

typedef struct FuseRequestOutHeader {
    struct fuse_out_header common;
    /* All supported requests */
    union {
        struct fuse_init_out init;
        struct fuse_statfs_out statfs;
        struct fuse_open_out open;
        struct fuse_attr_out attr;
        struct fuse_write_out write;
#ifdef CONFIG_FUSE_LSEEK
        struct fuse_lseek_out lseek;
#endif
    };
} FuseRequestOutHeader;

typedef union FuseRequestInHeaderBuf {
    struct FuseRequestInHeader structured;
    struct {
        /*
         * Part of the request header that is filled for write requests
         * (Needed because we want the data to go into a different buffer, to
         * avoid having to use a bounce buffer)
         */
        char head[sizeof(struct fuse_in_header) +
                  sizeof(struct fuse_write_in)];
        /*
         * Rest of the request header for requests that have a longer header
         * than write requests
         */
        char tail[sizeof(FuseRequestInHeader) -
                  (sizeof(struct fuse_in_header) +
                   sizeof(struct fuse_write_in))];
    };
} FuseRequestInHeaderBuf;

QEMU_BUILD_BUG_ON(sizeof(FuseRequestInHeaderBuf) !=
                  sizeof(FuseRequestInHeader));
QEMU_BUILD_BUG_ON(sizeof(((FuseRequestInHeaderBuf *)0)->head) +
                  sizeof(((FuseRequestInHeaderBuf *)0)->tail) !=
                  sizeof(FuseRequestInHeader));

typedef struct FuseExport {
    BlockExport common;

    struct fuse_session *fuse_session;
    unsigned int in_flight; /* atomic */
    bool mounted, fd_handler_set_up;

    /*
     * Cached buffer to receive the data of WRITE requests.  Cached because:
     * To read requests, we put a FuseRequestInHeaderBuf (FRIHB) object on the
     * stack, and a (WRITE data) buffer on the heap.  We pass FRIHB.head and the
     * data buffer to readv().  This way, for WRITE requests, we get exactly
     * their data in the data buffer and can avoid bounce buffering.
     * However, for non-WRITE requests, some of the header may end up in the
     * data buffer, so we will need to copy that back into the FRIHB object, and
     * then we don't need the heap buffer anymore.  That is why we cache it, so
     * we can trivially reuse it between non-WRITE requests.
     *
     * Note that these data buffers and thus req_write_data_cached are allocated
     * via blk_blockalign() and thus need to be freed via qemu_vfree().
     */
    void *req_write_data_cached;

    /*
     * Set when there was an unrecoverable error and no requests should be read
     * from the device anymore (basically only in case of something we would
     * consider a kernel bug).  Access atomically.
     */
    bool halted;

    int fuse_fd;

    char *mountpoint;
    bool writable;
    bool growable;
    /* Whether allow_other was used as a mount option or not */
    bool allow_other;

    /* All atomic */
    mode_t st_mode;
    uid_t st_uid;
    gid_t st_gid;
} FuseExport;

/*
 * Verify that the size of FuseRequestInHeaderBuf.head plus the data
 * buffer are big enough to be accepted by the FUSE kernel driver.
 */
QEMU_BUILD_BUG_ON(sizeof(((FuseRequestInHeaderBuf *)0)->head) +
                  FUSE_MAX_WRITE_BYTES <
                  FUSE_MIN_READ_BUFFER);

static GHashTable *exports;

static void fuse_export_shutdown(BlockExport *exp);
static void fuse_export_delete(BlockExport *exp);
static void fuse_export_halt(FuseExport *exp);

static void init_exports_table(void);

static int mount_fuse_export(FuseExport *exp, Error **errp);

static bool is_regular_file(const char *path, Error **errp);

static void read_from_fuse_fd(void *opaque);
static void coroutine_fn
fuse_co_process_request(FuseExport *exp, const FuseRequestInHeader *in_hdr,
                        const void *data_buffer);
static int fuse_write_err(int fd, const struct fuse_in_header *in_hdr, int err);

static void fuse_inc_in_flight(FuseExport *exp)
{
    if (qatomic_fetch_inc(&exp->in_flight) == 0) {
        /* Prevent export from being deleted */
        blk_exp_ref(&exp->common);
    }
}

static void fuse_dec_in_flight(FuseExport *exp)
{
    if (qatomic_fetch_dec(&exp->in_flight) == 1) {
        /* Wake AIO_WAIT_WHILE() */
        aio_wait_kick();

        /* Now the export can be deleted */
        blk_exp_unref(&exp->common);
    }
}

/**
 * Attach FUSE FD read handler.
 */
static void fuse_attach_handlers(FuseExport *exp)
{
    if (qatomic_read(&exp->halted)) {
        return;
    }

    aio_set_fd_handler(exp->common.ctx, exp->fuse_fd,
                       read_from_fuse_fd, NULL, NULL, NULL, exp);
    exp->fd_handler_set_up = true;
}

/**
 * Detach FUSE FD read handler.
 */
static void fuse_detach_handlers(FuseExport *exp)
{
    aio_set_fd_handler(exp->common.ctx, exp->fuse_fd,
                       NULL, NULL, NULL, NULL, NULL);
    exp->fd_handler_set_up = false;
}

static void fuse_export_drained_begin(void *opaque)
{
    fuse_detach_handlers(opaque);
}

static void fuse_export_drained_end(void *opaque)
{
    FuseExport *exp = opaque;

    /* Refresh AioContext in case it changed */
    exp->common.ctx = blk_get_aio_context(exp->common.blk);
    fuse_attach_handlers(exp);
}

static bool fuse_export_drained_poll(void *opaque)
{
    FuseExport *exp = opaque;

    return qatomic_read(&exp->in_flight) > 0;
}

static const BlockDevOps fuse_export_blk_dev_ops = {
    .drained_begin = fuse_export_drained_begin,
    .drained_end   = fuse_export_drained_end,
    .drained_poll  = fuse_export_drained_poll,
};

static int fuse_export_create(BlockExport *blk_exp,
                              BlockExportOptions *blk_exp_args,
                              AioContext *const *multithread,
                              size_t mt_count,
                              Error **errp)
{
    ERRP_GUARD(); /* ensure clean-up even with error_fatal */
    FuseExport *exp = container_of(blk_exp, FuseExport, common);
    BlockExportOptionsFuse *args = &blk_exp_args->u.fuse;
    uint32_t st_mode;
    int ret;

    assert(blk_exp_args->type == BLOCK_EXPORT_TYPE_FUSE);

    if (multithread) {
        error_setg(errp, "FUSE export does not support multi-threading");
        return -EINVAL;
    }

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

    blk_set_dev_ops(exp->common.blk, &fuse_export_blk_dev_ops, exp);

    /*
     * We handle draining ourselves using an in-flight counter and by disabling
     * the FUSE fd handler. Do not queue BlockBackend requests, they need to
     * complete so the in-flight counter reaches zero.
     */
    blk_set_disable_request_queuing(exp->common.blk, true);

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

    st_mode = S_IFREG | S_IRUSR;
    if (exp->writable) {
        st_mode |= S_IWUSR;
    }
    qatomic_set(&exp->st_mode, st_mode);
    qatomic_set(&exp->st_uid, getuid());
    qatomic_set(&exp->st_gid, getgid());

    if (args->allow_other == FUSE_EXPORT_ALLOW_OTHER_AUTO) {
        /* Try allow_other == true first, ignore errors */
        exp->allow_other = true;
        ret = mount_fuse_export(exp, NULL);
        if (ret < 0) {
            exp->allow_other = false;
            ret = mount_fuse_export(exp, errp);
        }
    } else {
        exp->allow_other = args->allow_other == FUSE_EXPORT_ALLOW_OTHER_ON;
        ret = mount_fuse_export(exp, errp);
    }
    if (ret < 0) {
        goto fail;
    }

    g_hash_table_insert(exports, g_strdup(exp->mountpoint), NULL);

    exp->fuse_fd = fuse_session_fd(exp->fuse_session);
    ret = qemu_fcntl_addfl(exp->fuse_fd, O_NONBLOCK);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to make FUSE FD non-blocking");
        goto fail;
    }

    fuse_attach_handlers(exp);
    return 0;

fail:
    fuse_export_shutdown(blk_exp);
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
 * Create exp->fuse_session and mount it.  Expects exp->mountpoint,
 * exp->writable, and exp->allow_other to be set as intended for the mount.
 */
static int mount_fuse_export(FuseExport *exp, Error **errp)
{
    const char *fuse_argv[4];
    char *mount_opts;
    struct fuse_args fuse_args;
    int ret;
    /*
     * We just create the session for mounting/unmounting, no need to provide
     * any operations.  However, since libfuse commit 52a633a5d, we have to
     * provide some op struct and cannot just pass NULL (even though the commit
     * message ("allow passing ops as NULL") seems to imply the exact opposite,
     * as does the comment added to fuse_session_new_fn() ("To create a no-op
     * session just for mounting pass op as NULL.").
     * This is how said libfuse commit implements a no-op session internally, so
     * do it the same way.
     */
    static const struct fuse_lowlevel_ops null_ops = { 0 };

    /*
     * Note that these mount options differ from what we would pass to a direct
     * mount() call:
     * - nosuid, nodev, and noatime are not understood by the kernel; libfuse
     *   uses those options to construct the mount flags (MS_*)
     * - The FUSE kernel driver requires additional options (fd, rootmode,
     *   user_id, group_id); these will be set by libfuse.
     * Note that max_read is set here, while max_write is set via the FUSE INIT
     * operation.
     */
    mount_opts = g_strdup_printf("%s,nosuid,nodev,noatime,max_read=%zu,"
                                 "default_permissions%s",
                                 exp->writable ? "rw" : "ro",
                                 FUSE_MAX_READ_BYTES,
                                 exp->allow_other ? ",allow_other" : "");

    fuse_argv[0] = ""; /* Dummy program name */
    fuse_argv[1] = "-o";
    fuse_argv[2] = mount_opts;
    fuse_argv[3] = NULL;
    fuse_args = (struct fuse_args)FUSE_ARGS_INIT(3, (char **)fuse_argv);

    exp->fuse_session = fuse_session_new(&fuse_args, &null_ops,
                                         sizeof(null_ops), NULL);
    g_free(mount_opts);
    if (!exp->fuse_session) {
        error_setg(errp, "Failed to set up FUSE session");
        return -EIO;
    }

    ret = fuse_session_mount(exp->fuse_session, exp->mountpoint);
    if (ret < 0) {
        error_setg(errp, "Failed to mount FUSE session to export");
        ret = -EIO;
        goto fail;
    }
    exp->mounted = true;

    return 0;

fail:
    fuse_session_destroy(exp->fuse_session);
    exp->fuse_session = NULL;
    return ret;
}

/**
 * Allocate a buffer to receive WRITE data, or take the cached one.
 */
static void *get_write_data_buffer(FuseExport *exp)
{
    if (exp->req_write_data_cached) {
        void *cached = exp->req_write_data_cached;
        exp->req_write_data_cached = NULL;
        return cached;
    } else {
        return blk_blockalign(exp->common.blk, FUSE_MAX_WRITE_BYTES);
    }
}

/**
 * Release a WRITE data buffer, possibly reusing it for a subsequent request.
 */
static void release_write_data_buffer(FuseExport *exp, void **buffer)
{
    if (!*buffer) {
        return;
    }

    if (!exp->req_write_data_cached) {
        exp->req_write_data_cached = *buffer;
    } else {
        qemu_vfree(*buffer);
    }
    *buffer = NULL;
}

/**
 * Return the length of the specific operation's own in_header.
 * Return -ENOSYS if the operation is not supported.
 */
static ssize_t req_op_hdr_len(const FuseRequestInHeader *in_hdr)
{
    switch (in_hdr->common.opcode) {
    case FUSE_INIT:
        return sizeof(in_hdr->init);
    case FUSE_OPEN:
        return sizeof(in_hdr->open);
    case FUSE_SETATTR:
        return sizeof(in_hdr->setattr);
    case FUSE_READ:
        return sizeof(in_hdr->read);
    case FUSE_WRITE:
        return sizeof(in_hdr->write);
    case FUSE_FALLOCATE:
        return sizeof(in_hdr->fallocate);
#ifdef CONFIG_FUSE_LSEEK
    case FUSE_LSEEK:
        return sizeof(in_hdr->lseek);
#endif
    case FUSE_DESTROY:
    case FUSE_STATFS:
    case FUSE_RELEASE:
    case FUSE_LOOKUP:
    case FUSE_FORGET:
    case FUSE_BATCH_FORGET:
    case FUSE_GETATTR:
    case FUSE_FSYNC:
    case FUSE_FLUSH:
        /* These requests don't have their own header or we don't care */
        return 0;
    default:
        return -ENOSYS;
    }
}

/**
 * Try to read a single request from the FUSE FD.
 * Takes a FuseExport pointer in `opaque`.
 *
 * Assumes the export's in-flight counter has already been incremented.
 *
 * If a request is available, process it.
 */
static void coroutine_fn co_read_from_fuse_fd(void *opaque)
{
    FuseExport *exp = opaque;
    int fuse_fd = exp->fuse_fd;
    ssize_t ret;
    FuseRequestInHeaderBuf in_hdr_buf;
    const FuseRequestInHeader *in_hdr;
    void *data_buffer = NULL;
    struct iovec iov[2];
    ssize_t op_hdr_len;

    if (unlikely(qatomic_read(&exp->halted))) {
        goto no_request;
    }

    data_buffer = get_write_data_buffer(exp);

    /* Construct the I/O vector to hold the FUSE request */
    iov[0] = (struct iovec) { &in_hdr_buf.head, sizeof(in_hdr_buf.head) };
    iov[1] = (struct iovec) { data_buffer, FUSE_MAX_WRITE_BYTES };
    ret = RETRY_ON_EINTR(readv(fuse_fd, iov, ARRAY_SIZE(iov)));
    if (ret < 0 && errno == EAGAIN) {
        /* No request available */
        goto no_request;
    } else if (unlikely(ret < 0)) {
        error_report("Failed to read from FUSE device: %s", strerror(errno));
        goto no_request;
    }

    if (unlikely(ret < sizeof(in_hdr->common))) {
        error_report("Incomplete read from FUSE device, expected at least %zu "
                     "bytes, read %zi bytes; cannot trust subsequent "
                     "requests, halting the export",
                     sizeof(in_hdr->common), ret);
        fuse_export_halt(exp);
        goto no_request;
    }
    in_hdr = &in_hdr_buf.structured;

    if (unlikely(ret != in_hdr->common.len)) {
        error_report("Number of bytes read from FUSE device does not match "
                     "request size, expected %" PRIu32 " bytes, read %zi "
                     "bytes; cannot trust subsequent requests, halting the "
                     "export",
                     in_hdr->common.len, ret);
        fuse_export_halt(exp);
        goto no_request;
    }

    op_hdr_len = req_op_hdr_len(in_hdr);
    if (op_hdr_len < 0) {
        fuse_write_err(fuse_fd, &in_hdr->common, op_hdr_len);
        goto no_request;
    }

    if (unlikely(ret < sizeof(in_hdr->common) + op_hdr_len)) {
        error_report("FUSE request truncated, expected %zu bytes, read %zi "
                     "bytes",
                     sizeof(in_hdr->common) + op_hdr_len, ret);
        fuse_write_err(fuse_fd, &in_hdr->common, -EINVAL);
        goto no_request;
    }

    /*
     * Only WRITE uses the write data buffer, so for non-WRITE requests longer
     * than .head, we need to copy any data that spilled into data_buffer into
     * .tail.  Then we can release the write data buffer.
     */
    if (in_hdr->common.opcode != FUSE_WRITE) {
        if (ret > sizeof(in_hdr_buf.head)) {
            size_t len;
            /* Limit size to prevent overflow */
            len = MIN(ret - sizeof(in_hdr_buf.head), sizeof(in_hdr_buf.tail));
            memcpy(in_hdr_buf.tail, data_buffer, len);
        }

        release_write_data_buffer(exp, &data_buffer);
    }

    fuse_co_process_request(exp, in_hdr, data_buffer);

no_request:
    release_write_data_buffer(exp, &data_buffer);
    fuse_dec_in_flight(exp);
}

/**
 * Try to read and process a single request from the FUSE FD.
 * (To be used as a handler for when the FUSE FD becomes readable.)
 * Takes a FuseExport pointer in `opaque`.
 */
static void read_from_fuse_fd(void *opaque)
{
    FuseExport *exp = opaque;
    Coroutine *co;

    co = qemu_coroutine_create(co_read_from_fuse_fd, exp);
    /* Decremented by co_read_from_fuse_fd() */
    fuse_inc_in_flight(exp);
    qemu_coroutine_enter(co);
}

static void fuse_export_shutdown(BlockExport *blk_exp)
{
    FuseExport *exp = container_of(blk_exp, FuseExport, common);

    if (exp->fd_handler_set_up) {
        fuse_detach_handlers(exp);
    }

    if (exp->mountpoint) {
        /*
         * Safe to drop now, because we will not handle any requests for this
         * export anymore anyway (at least not from the main thread).
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

    qemu_vfree(exp->req_write_data_cached);
    g_free(exp->mountpoint);
}

/**
 * Halt the export: Detach FD handlers, and set exp->halted to true, preventing
 * fuse_attach_handlers() from re-attaching them, therefore stopping all further
 * request processing.
 *
 * Call this function when an unrecoverable error happens that makes processing
 * all future requests unreliable.
 */
static void fuse_export_halt(FuseExport *exp)
{
    qatomic_set(&exp->halted, true);
    fuse_detach_handlers(exp);
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
 * Process FUSE INIT.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_init(FuseExport *exp, struct fuse_init_out *out,
             const struct fuse_init_in_compat *in)
{
    const uint32_t supported_flags = FUSE_ASYNC_READ | FUSE_ASYNC_DIO;

    if (in->major != 7) {
        error_report("FUSE major version mismatch: We have 7, but kernel has %"
                     PRIu32, in->major);
        return -EINVAL;
    }

    /* 2007's 7.9 added fuse_attr.blksize; working around that would be hard */
    if (in->minor < 9) {
        error_report("FUSE minor version too old: 9 required, but kernel has %"
                     PRIu32, in->minor);
        return -EINVAL;
    }

    *out = (struct fuse_init_out) {
        .major = 7,
        .minor = MIN(FUSE_KERNEL_MINOR_VERSION, in->minor),
        .max_readahead = in->max_readahead,
        .max_write = FUSE_MAX_WRITE_BYTES,
        .flags = in->flags & supported_flags,
        .flags2 = 0,

        /* libfuse maximum: 2^16 - 1 */
        .max_background = UINT16_MAX,

        /* libfuse default: max_background * 3 / 4 */
        .congestion_threshold = (int)UINT16_MAX * 3 / 4,

        /* libfuse default: 1 */
        .time_gran = 1,

        /*
         * probably unneeded without FUSE_MAX_PAGES, but this would be the
         * libfuse default
         */
        .max_pages = DIV_ROUND_UP(FUSE_MAX_WRITE_BYTES,
                                  qemu_real_host_page_size()),

        /* Only needed for mappings (i.e. DAX) */
        .map_alignment = 0,
    };

    /*
     * Before 7.23, fuse_init_out is shorter.
     * Drop the tail (time_gran, max_pages, map_alignment).
     */
    return out->minor >= 23 ? sizeof(*out) : FUSE_COMPAT_22_INIT_OUT_SIZE;
}

/**
 * Return some filesystem information, just to not break e.g. `df`.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_statfs(FuseExport *exp, struct fuse_statfs_out *out)
{
    BlockDriverState *root_bs;
    uint32_t opt_transfer = 512;

    root_bs = blk_bs(exp->common.blk);
    if (root_bs) {
        opt_transfer = root_bs->bl.opt_transfer;
        if (!opt_transfer) {
            opt_transfer = root_bs->bl.request_alignment;
        }
        opt_transfer = MAX(opt_transfer, 512);
    }

    *out = (struct fuse_statfs_out) {
        /* These are the fields libfuse sets by default */
        .st = {
            .namelen = 255,
            .bsize = opt_transfer,
        },
    };
    return sizeof(*out);
}

/**
 * Let clients get file attributes (i.e., stat() the file).
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_getattr(FuseExport *exp, struct fuse_attr_out *out)
{
    int64_t length, allocated_blocks;
    time_t now = time(NULL);

    length = blk_co_getlength(exp->common.blk);
    if (length < 0) {
        return length;
    }

    allocated_blocks = bdrv_co_get_allocated_file_size(blk_bs(exp->common.blk));
    if (allocated_blocks <= 0) {
        allocated_blocks = DIV_ROUND_UP(length, 512);
    } else {
        allocated_blocks = DIV_ROUND_UP(allocated_blocks, 512);
    }

    *out = (struct fuse_attr_out) {
        .attr_valid = 1,
        .attr = {
            .ino        = 1,
            .mode       = qatomic_read(&exp->st_mode),
            .nlink      = 1,
            .uid        = qatomic_read(&exp->st_uid),
            .gid        = qatomic_read(&exp->st_gid),
            .size       = length,
            .blksize    = blk_bs(exp->common.blk)->bl.request_alignment,
            .blocks     = allocated_blocks,
            .atime      = now,
            .mtime      = now,
            .ctime      = now,
        },
    };

    return sizeof(*out);
}

static int coroutine_fn GRAPH_RDLOCK
fuse_co_do_truncate(const FuseExport *exp, int64_t size, bool req_zero_write,
                    PreallocMode prealloc)
{
    BdrvRequestFlags truncate_flags = 0;

    if (req_zero_write) {
        truncate_flags |= BDRV_REQ_ZERO_WRITE;
    }

    return blk_co_truncate(exp->common.blk, size, true, prealloc,
                           truncate_flags, NULL);
}

/**
 * Let clients set file attributes.  Only resizing and changing
 * permissions (st_mode, st_uid, st_gid) is allowed.
 * Changing permissions is only allowed as far as it will actually
 * permit access: Read-only exports cannot be given +w, and exports
 * without allow_other cannot be given a different UID or GID, and
 * they cannot be given non-owner access.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_setattr(FuseExport *exp, struct fuse_attr_out *out, uint32_t to_set,
                uint64_t size, uint32_t mode, uint32_t uid, uint32_t gid)
{
    int supported_attrs;
    int ret;

    /* SIZE and MODE are actually supported, the others can be safely ignored */
    supported_attrs = FATTR_SIZE | FATTR_MODE |
        FATTR_FH | FATTR_LOCKOWNER | FATTR_KILL_SUIDGID;
    if (exp->allow_other) {
        supported_attrs |= FATTR_UID | FATTR_GID;
    }

    if (to_set & ~supported_attrs) {
        return -ENOTSUP;
    }

    /* Do some argument checks first before committing to anything */
    if (to_set & FATTR_MODE) {
        /*
         * Without allow_other, non-owners can never access the export, so do
         * not allow setting permissions for them
         */
        if (!exp->allow_other && (mode & (S_IRWXG | S_IRWXO)) != 0) {
            return -EPERM;
        }

        /* +w for read-only exports makes no sense, disallow it */
        if (!exp->writable && (mode & (S_IWUSR | S_IWGRP | S_IWOTH)) != 0) {
            return -EROFS;
        }
    }

    if (to_set & FATTR_SIZE) {
        if (!exp->writable) {
            return -EACCES;
        }

        ret = fuse_co_do_truncate(exp, size, true, PREALLOC_MODE_OFF);
        if (ret < 0) {
            return ret;
        }
    }

    if (to_set & FATTR_MODE) {
        /* Ignore FUSE-supplied file type, only change the mode */
        qatomic_set(&exp->st_mode, (mode & 07777) | S_IFREG);
    }

    if (to_set & FATTR_UID) {
        qatomic_set(&exp->st_uid, uid);
    }

    if (to_set & FATTR_GID) {
        qatomic_set(&exp->st_gid, gid);
    }

    return fuse_co_getattr(exp, out);
}

/**
 * Open an inode.  We only have a single inode in our exported filesystem, so we
 * just acknowledge the request.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_open(FuseExport *exp, struct fuse_open_out *out)
{
    *out = (struct fuse_open_out) {
        .open_flags = FOPEN_DIRECT_IO | FOPEN_PARALLEL_DIRECT_WRITES,
    };
    return sizeof(*out);
}

/**
 * Handle client reads from the exported image.  Allocates *bufptr and reads
 * data from the block device into that buffer.
 * Returns the buffer (read) size on success, and -errno on error.
 * Note: If the returned size is 0, *bufptr will be set to NULL.
 * After use, *bufptr must be freed via qemu_vfree().
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_read(FuseExport *exp, void **bufptr, uint64_t offset, uint32_t size)
{
    int64_t blk_len;
    void *buf;
    int ret;

    /* Limited by max_read, should not happen */
    if (size > FUSE_MAX_READ_BYTES) {
        return -EINVAL;
    }

    /**
     * Clients will expect short reads at EOF, so we have to limit
     * offset+size to the image length.
     */
    blk_len = blk_co_getlength(exp->common.blk);
    if (blk_len < 0) {
        return blk_len;
    }

    if (offset >= blk_len) {
        /* Explicitly set to NULL because we return success here */
        *bufptr = NULL;
        return 0;
    }

    if (offset + size > blk_len) {
        size = blk_len - offset;
    }

    buf = qemu_try_blockalign(blk_bs(exp->common.blk), size);
    if (!buf) {
        return -ENOMEM;
    }

    ret = blk_co_pread(exp->common.blk, offset, size, buf, 0);
    if (ret < 0) {
        qemu_vfree(buf);
        return ret;
    }

    *bufptr = buf;
    return size;
}

/**
 * Handle client writes to the exported image.  @buf has the data to be written.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_write(FuseExport *exp, struct fuse_write_out *out,
              uint64_t offset, uint32_t size, const void *buf)
{
    int64_t blk_len;
    int ret;

    QEMU_BUILD_BUG_ON(FUSE_MAX_WRITE_BYTES > BDRV_REQUEST_MAX_BYTES);
    /* Limited by max_write, should not happen */
    if (size > FUSE_MAX_WRITE_BYTES) {
        return -EINVAL;
    }

    if (!exp->writable) {
        return -EACCES;
    }

    /**
     * Clients will expect short writes at EOF, so we have to limit
     * offset+size to the image length.
     */
    blk_len = blk_co_getlength(exp->common.blk);
    if (blk_len < 0) {
        return blk_len;
    }

    if (offset >= blk_len && !exp->growable) {
        *out = (struct fuse_write_out) {
            .size = 0,
        };
        return sizeof(*out);
    }

    if (offset + size < offset) {
        return -EINVAL;
    } else if (offset + size > blk_len) {
        if (exp->growable) {
            ret = fuse_co_do_truncate(exp, offset + size, true,
                                      PREALLOC_MODE_OFF);
            if (ret < 0) {
                return ret;
            }
        } else {
            size = blk_len - offset;
        }
    }

    ret = blk_co_pwrite(exp->common.blk, offset, size, buf, 0);
    if (ret < 0) {
        return ret;
    }

    *out = (struct fuse_write_out) {
        .size = size,
    };
    return sizeof(*out);
}

/**
 * Let clients perform various fallocate() operations.
 * Return 0 on success (no 'out' object), and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_fallocate(FuseExport *exp,
                  uint64_t offset, uint64_t length, uint32_t mode)
{
    int64_t blk_len;
    int ret;

    if (!exp->writable) {
        return -EACCES;
    }

    blk_len = blk_co_getlength(exp->common.blk);
    if (blk_len < 0) {
        return blk_len;
    }

#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    if (mode & FALLOC_FL_KEEP_SIZE) {
        length = MIN(length, blk_len - offset);
    }
#endif /* CONFIG_FALLOCATE_PUNCH_HOLE */

    if (!mode) {
        /* We can only fallocate at the EOF with a truncate */
        if (offset < blk_len) {
            return -EOPNOTSUPP;
        }

        if (offset > blk_len) {
            /* No preallocation needed here */
            ret = fuse_co_do_truncate(exp, offset, true, PREALLOC_MODE_OFF);
            if (ret < 0) {
                return ret;
            }
        }

        ret = fuse_co_do_truncate(exp, offset + length, true,
                                  PREALLOC_MODE_FALLOC);
    }
#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    else if (mode & FALLOC_FL_PUNCH_HOLE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE)) {
            return -EINVAL;
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_co_pwrite_zeroes(exp->common.blk, offset, size,
                                       BDRV_REQ_MAY_UNMAP |
                                       BDRV_REQ_NO_FALLBACK);
            if (ret == -ENOTSUP) {
                /*
                 * fallocate() specifies to return EOPNOTSUPP for unsupported
                 * operations
                 */
                ret = -EOPNOTSUPP;
            }

            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif /* CONFIG_FALLOCATE_PUNCH_HOLE */
#ifdef CONFIG_FALLOCATE_ZERO_RANGE
    else if (mode & FALLOC_FL_ZERO_RANGE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE) && offset + length > blk_len) {
            /* No need for zeroes, we are going to write them ourselves */
            ret = fuse_co_do_truncate(exp, offset + length, false,
                                      PREALLOC_MODE_OFF);
            if (ret < 0) {
                return ret;
            }
        }

        do {
            int size = MIN(length, BDRV_REQUEST_MAX_BYTES);

            ret = blk_co_pwrite_zeroes(exp->common.blk,
                                       offset, size, 0);
            offset += size;
            length -= size;
        } while (ret == 0 && length > 0);
    }
#endif /* CONFIG_FALLOCATE_ZERO_RANGE */
    else {
        ret = -EOPNOTSUPP;
    }

    return ret < 0 ? ret : 0;
}

/**
 * Let clients fsync the exported image.
 * Return 0 on success (no 'out' object), and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK fuse_co_fsync(FuseExport *exp)
{
    return blk_co_flush(exp->common.blk);
}

/**
 * Called before an FD to the exported image is closed.  (libfuse
 * notes this to be a way to return last-minute errors.)
 * Return 0 on success (no 'out' object), and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK fuse_co_flush(FuseExport *exp)
{
    return blk_co_flush(exp->common.blk);
}

#ifdef CONFIG_FUSE_LSEEK
/**
 * Let clients inquire allocation status.
 * Return the number of bytes written to *out on success, and -errno on error.
 */
static ssize_t coroutine_fn GRAPH_RDLOCK
fuse_co_lseek(FuseExport *exp, struct fuse_lseek_out *out,
              uint64_t offset, uint32_t whence)
{
    if (whence != SEEK_HOLE && whence != SEEK_DATA) {
        return -EINVAL;
    }

    while (true) {
        int64_t pnum;
        int ret;

        ret = bdrv_co_block_status_above(blk_bs(exp->common.blk), NULL,
                                         offset, INT64_MAX, &pnum, NULL, NULL);
        if (ret < 0) {
            return ret;
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

            blk_len = blk_co_getlength(exp->common.blk);
            if (blk_len < 0) {
                return blk_len;
            }

            if (offset > blk_len || whence == SEEK_DATA) {
                return -ENXIO;
            }

            *out = (struct fuse_lseek_out) {
                .offset = offset,
            };
            return sizeof(*out);
        }

        if (ret & BDRV_BLOCK_DATA) {
            if (whence == SEEK_DATA) {
                *out = (struct fuse_lseek_out) {
                    .offset = offset,
                };
                return sizeof(*out);
            }
        } else {
            if (whence == SEEK_HOLE) {
                *out = (struct fuse_lseek_out) {
                    .offset = offset,
                };
                return sizeof(*out);
            }
        }

        /* Safety check against infinite loops */
        if (!pnum) {
            return -ENXIO;
        }

        offset += pnum;
    }
}
#endif

/**
 * Write a FUSE response to the given @fd.
 *
 * Effectively, writes out_hdr->common.len bytes of the buffer that is *out_hdr.
 *
 * @fd: FUSE file descriptor
 * @out_hdr: Request response header and request-specific response data
 */
static int fuse_write_response(int fd, FuseRequestOutHeader *out_hdr)
{
    size_t to_write = out_hdr->common.len;
    ssize_t ret;

    /* Must at least write fuse_out_header */
    assert(to_write >= sizeof(out_hdr->common));

    ret = RETRY_ON_EINTR(write(fd, out_hdr, to_write));
    if (ret < 0) {
        ret = -errno;
        error_report("Failed to write to FUSE device: %s", strerror(-ret));
        return ret;
    }

    /* Short writes are unexpected, treat them as errors */
    if (ret != to_write) {
        error_report("Short write to FUSE device, wrote %zi of %zu bytes",
                     ret, to_write);
        return -EIO;
    }

    return 0;
}

/**
 * Write a FUSE error response to @fd.
 *
 * @fd: FUSE file descriptor
 * @in_hdr: Incoming request header to which to respond
 * @err: Error code (-errno, must be negative!)
 */
static int fuse_write_err(int fd, const struct fuse_in_header *in_hdr, int err)
{
    FuseRequestOutHeader out_hdr = {
        .common = {
            .len = sizeof(out_hdr.common),
            /* FUSE expects negative error values */
            .error = err,
            .unique = in_hdr->unique,
        },
    };

    return fuse_write_response(fd, &out_hdr);
}

/**
 * Write a FUSE response to the given @fd, using separate buffers for the
 * response header and data.
 *
 * In contrast to fuse_write_response(), this function cannot return a full
 * FuseRequestOutHeader (i.e. including request-specific response structs),
 * but only FuseRequestOutHeader.common.  The remaining data must be in
 * *buf.
 *
 * (Total length must be set in out_hdr->len.)
 *
 * @fd: FUSE file descriptor
 * @out_hdr: Request response header
 * @buf: Pointer to response data
 */
static int fuse_write_buf_response(int fd,
                                   const struct fuse_out_header *out_hdr,
                                   const void *buf)
{
    size_t to_write = out_hdr->len;
    struct iovec iov[2] = {
        { (void *)out_hdr, sizeof(*out_hdr) },
        { (void *)buf, to_write - sizeof(*out_hdr) },
    };
    ssize_t ret;

    /* *buf length must not be negative */
    assert(to_write >= sizeof(*out_hdr));

    ret = RETRY_ON_EINTR(writev(fd, iov, ARRAY_SIZE(iov)));
    if (ret < 0) {
        ret = -errno;
        error_report("Failed to write to FUSE device: %s", strerror(-ret));
        return ret;
    }

    /* Short writes are unexpected, treat them as errors */
    if (ret != to_write) {
        error_report("Short write to FUSE device, wrote %zi of %zu bytes",
                     ret, to_write);
        return -EIO;
    }

    return 0;
}

/**
 * Process a FUSE request, incl. writing the response.
 */
static void coroutine_fn
fuse_co_process_request(FuseExport *exp, const FuseRequestInHeader *in_hdr,
                        const void *data_buffer)
{
    FuseRequestOutHeader out_hdr;
    /* For read requests: Data to be returned */
    void *out_data_buffer = NULL;
    ssize_t ret;

    GRAPH_RDLOCK_GUARD();

    switch (in_hdr->common.opcode) {
    case FUSE_INIT:
        ret = fuse_co_init(exp, &out_hdr.init, &in_hdr->init);
        break;

    case FUSE_DESTROY:
        ret = 0;
        break;

    case FUSE_STATFS:
        ret = fuse_co_statfs(exp, &out_hdr.statfs);
        break;

    case FUSE_OPEN:
        ret = fuse_co_open(exp, &out_hdr.open);
        break;

    case FUSE_RELEASE:
        ret = 0;
        break;

    case FUSE_LOOKUP:
        ret = -ENOENT; /* There is no node but the root node */
        break;

    case FUSE_FORGET:
    case FUSE_BATCH_FORGET:
        /* These have no response, and there is nothing we need to do */
        return;

    case FUSE_GETATTR:
        ret = fuse_co_getattr(exp, &out_hdr.attr);
        break;

    case FUSE_SETATTR: {
        const struct fuse_setattr_in *in = &in_hdr->setattr;
        ret = fuse_co_setattr(exp, &out_hdr.attr,
                              in->valid, in->size, in->mode, in->uid, in->gid);
        break;
    }

    case FUSE_READ: {
        const struct fuse_read_in *in = &in_hdr->read;
        ret = fuse_co_read(exp, &out_data_buffer, in->offset, in->size);
        break;
    }

    case FUSE_WRITE: {
        const struct fuse_write_in *in = &in_hdr->write;
        uint32_t req_len = in_hdr->common.len;

        if (unlikely(req_len < sizeof(in_hdr->common) + sizeof(*in) +
                               in->size)) {
            warn_report("FUSE WRITE truncated; received %zu bytes of %" PRIu32,
                        req_len - sizeof(in_hdr->common) - sizeof(*in),
                        in->size);
            ret = -EINVAL;
            break;
        }

        /*
         * co_read_from_fuse_fd() has checked that in_hdr->len matches the
         * number of bytes read, which cannot exceed the max_write value we set
         * (FUSE_MAX_WRITE_BYTES).  So we know that FUSE_MAX_WRITE_BYTES >=
         * in_hdr->len >= in->size + X, so this assertion must hold.
         */
        assert(in->size <= FUSE_MAX_WRITE_BYTES);

        ret = fuse_co_write(exp, &out_hdr.write,
                            in->offset, in->size, data_buffer);
        break;
    }

    case FUSE_FALLOCATE: {
        const struct fuse_fallocate_in *in = &in_hdr->fallocate;
        ret = fuse_co_fallocate(exp, in->offset, in->length, in->mode);
        break;
    }

    case FUSE_FSYNC:
        ret = fuse_co_fsync(exp);
        break;

    case FUSE_FLUSH:
        ret = fuse_co_flush(exp);
        break;

#ifdef CONFIG_FUSE_LSEEK
    case FUSE_LSEEK: {
        const struct fuse_lseek_in *in = &in_hdr->lseek;
        ret = fuse_co_lseek(exp, &out_hdr.lseek, in->offset, in->whence);
        break;
    }
#endif

    default:
        ret = -ENOSYS;
    }

    if (ret >= 0) {
        out_hdr.common = (struct fuse_out_header) {
            .len = sizeof(out_hdr.common) + ret,
            .unique = in_hdr->common.unique,
        };
    } else {
        /* fuse_read() must not return a buffer in case of error */
        assert(out_data_buffer == NULL);

        out_hdr.common = (struct fuse_out_header) {
            .len = sizeof(out_hdr.common),
            /* FUSE expects negative errno values */
            .error = ret,
            .unique = in_hdr->common.unique,
        };
    }

    if (out_data_buffer) {
        fuse_write_buf_response(exp->fuse_fd, &out_hdr.common, out_data_buffer);
        qemu_vfree(out_data_buffer);
    } else {
        fuse_write_response(exp->fuse_fd, &out_hdr);
    }
}

const BlockExportDriver blk_exp_fuse = {
    .type               = BLOCK_EXPORT_TYPE_FUSE,
    .instance_size      = sizeof(FuseExport),
    .create             = fuse_export_create,
    .delete             = fuse_export_delete,
    .request_shutdown   = fuse_export_shutdown,
};
