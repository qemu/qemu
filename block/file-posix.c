/*
 * Block driver for RAW files (posix)
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "trace.h"
#include "block/thread-pool.h"
#include "qemu/iov.h"
#include "block/raw-aio.h"
#include "qapi/util.h"
#include "qapi/qmp/qstring.h"

#if defined(__APPLE__) && (__MACH__)
#include <paths.h>
#include <sys/param.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOMediaBSDClient.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOCDMedia.h>
//#include <IOKit/storage/IOCDTypes.h>
#include <IOKit/storage/IODVDMedia.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef __sun__
#define _POSIX_PTHREAD_SEMANTICS 1
#include <sys/dkio.h>
#endif
#ifdef __linux__
#include <sys/ioctl.h>
#include <sys/param.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <scsi/sg.h>
#ifdef __s390__
#include <asm/dasd.h>
#endif
#ifndef FS_NOCOW_FL
#define FS_NOCOW_FL                     0x00800000 /* Do not cow file */
#endif
#endif
#if defined(CONFIG_FALLOCATE_PUNCH_HOLE) || defined(CONFIG_FALLOCATE_ZERO_RANGE)
#include <linux/falloc.h>
#endif
#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <sys/disk.h>
#include <sys/cdio.h>
#endif

#ifdef __OpenBSD__
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#endif

#ifdef __NetBSD__
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/disk.h>
#endif

#ifdef __DragonFly__
#include <sys/ioctl.h>
#include <sys/diskslice.h>
#endif

#ifdef CONFIG_XFS
#include <xfs/xfs.h>
#endif

//#define DEBUG_BLOCK

#ifdef DEBUG_BLOCK
# define DEBUG_BLOCK_PRINT 1
#else
# define DEBUG_BLOCK_PRINT 0
#endif
#define DPRINTF(fmt, ...) \
do { \
    if (DEBUG_BLOCK_PRINT) { \
        printf(fmt, ## __VA_ARGS__); \
    } \
} while (0)

/* OS X does not have O_DSYNC */
#ifndef O_DSYNC
#ifdef O_SYNC
#define O_DSYNC O_SYNC
#elif defined(O_FSYNC)
#define O_DSYNC O_FSYNC
#endif
#endif

/* Approximate O_DIRECT with O_DSYNC if O_DIRECT isn't available */
#ifndef O_DIRECT
#define O_DIRECT O_DSYNC
#endif

#define FTYPE_FILE   0
#define FTYPE_CD     1

#define MAX_BLOCKSIZE	4096

/* Posix file locking bytes. Libvirt takes byte 0, we start from higher bytes,
 * leaving a few more bytes for its future use. */
#define RAW_LOCK_PERM_BASE             100
#define RAW_LOCK_SHARED_BASE           200

typedef struct BDRVRawState {
    int fd;
    int lock_fd;
    bool use_lock;
    int type;
    int open_flags;
    size_t buf_align;

    /* The current permissions. */
    uint64_t perm;
    uint64_t shared_perm;

#ifdef CONFIG_XFS
    bool is_xfs:1;
#endif
    bool has_discard:1;
    bool has_write_zeroes:1;
    bool discard_zeroes:1;
    bool use_linux_aio:1;
    bool page_cache_inconsistent:1;
    bool has_fallocate;
    bool needs_alignment;
} BDRVRawState;

typedef struct BDRVRawReopenState {
    int fd;
    int open_flags;
} BDRVRawReopenState;

static int fd_open(BlockDriverState *bs);
static int64_t raw_getlength(BlockDriverState *bs);

typedef struct RawPosixAIOData {
    BlockDriverState *bs;
    int aio_fildes;
    union {
        struct iovec *aio_iov;
        void *aio_ioctl_buf;
    };
    int aio_niov;
    uint64_t aio_nbytes;
#define aio_ioctl_cmd   aio_nbytes /* for QEMU_AIO_IOCTL */
    off_t aio_offset;
    int aio_type;
} RawPosixAIOData;

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
static int cdrom_reopen(BlockDriverState *bs);
#endif

#if defined(__NetBSD__)
static int raw_normalize_devicepath(const char **filename)
{
    static char namebuf[PATH_MAX];
    const char *dp, *fname;
    struct stat sb;

    fname = *filename;
    dp = strrchr(fname, '/');
    if (lstat(fname, &sb) < 0) {
        fprintf(stderr, "%s: stat failed: %s\n",
            fname, strerror(errno));
        return -errno;
    }

    if (!S_ISBLK(sb.st_mode)) {
        return 0;
    }

    if (dp == NULL) {
        snprintf(namebuf, PATH_MAX, "r%s", fname);
    } else {
        snprintf(namebuf, PATH_MAX, "%.*s/r%s",
            (int)(dp - fname), fname, dp + 1);
    }
    fprintf(stderr, "%s is a block device", fname);
    *filename = namebuf;
    fprintf(stderr, ", using %s\n", *filename);

    return 0;
}
#else
static int raw_normalize_devicepath(const char **filename)
{
    return 0;
}
#endif

/*
 * Get logical block size via ioctl. On success store it in @sector_size_p.
 */
static int probe_logical_blocksize(int fd, unsigned int *sector_size_p)
{
    unsigned int sector_size;
    bool success = false;
    int i;

    errno = ENOTSUP;
    static const unsigned long ioctl_list[] = {
#ifdef BLKSSZGET
        BLKSSZGET,
#endif
#ifdef DKIOCGETBLOCKSIZE
        DKIOCGETBLOCKSIZE,
#endif
#ifdef DIOCGSECTORSIZE
        DIOCGSECTORSIZE,
#endif
    };

    /* Try a few ioctls to get the right size */
    for (i = 0; i < (int)ARRAY_SIZE(ioctl_list); i++) {
        if (ioctl(fd, ioctl_list[i], &sector_size) >= 0) {
            *sector_size_p = sector_size;
            success = true;
        }
    }

    return success ? 0 : -errno;
}

/**
 * Get physical block size of @fd.
 * On success, store it in @blk_size and return 0.
 * On failure, return -errno.
 */
static int probe_physical_blocksize(int fd, unsigned int *blk_size)
{
#ifdef BLKPBSZGET
    if (ioctl(fd, BLKPBSZGET, blk_size) < 0) {
        return -errno;
    }
    return 0;
#else
    return -ENOTSUP;
#endif
}

/* Check if read is allowed with given memory buffer and length.
 *
 * This function is used to check O_DIRECT memory buffer and request alignment.
 */
static bool raw_is_io_aligned(int fd, void *buf, size_t len)
{
    ssize_t ret = pread(fd, buf, len, 0);

    if (ret >= 0) {
        return true;
    }

#ifdef __linux__
    /* The Linux kernel returns EINVAL for misaligned O_DIRECT reads.  Ignore
     * other errors (e.g. real I/O error), which could happen on a failed
     * drive, since we only care about probing alignment.
     */
    if (errno != EINVAL) {
        return true;
    }
#endif

    return false;
}

static void raw_probe_alignment(BlockDriverState *bs, int fd, Error **errp)
{
    BDRVRawState *s = bs->opaque;
    char *buf;
    size_t max_align = MAX(MAX_BLOCKSIZE, getpagesize());

    /* For SCSI generic devices the alignment is not really used.
       With buffered I/O, we don't have any restrictions. */
    if (bdrv_is_sg(bs) || !s->needs_alignment) {
        bs->bl.request_alignment = 1;
        s->buf_align = 1;
        return;
    }

    bs->bl.request_alignment = 0;
    s->buf_align = 0;
    /* Let's try to use the logical blocksize for the alignment. */
    if (probe_logical_blocksize(fd, &bs->bl.request_alignment) < 0) {
        bs->bl.request_alignment = 0;
    }
#ifdef CONFIG_XFS
    if (s->is_xfs) {
        struct dioattr da;
        if (xfsctl(NULL, fd, XFS_IOC_DIOINFO, &da) >= 0) {
            bs->bl.request_alignment = da.d_miniosz;
            /* The kernel returns wrong information for d_mem */
            /* s->buf_align = da.d_mem; */
        }
    }
#endif

    /* If we could not get the sizes so far, we can only guess them */
    if (!s->buf_align) {
        size_t align;
        buf = qemu_memalign(max_align, 2 * max_align);
        for (align = 512; align <= max_align; align <<= 1) {
            if (raw_is_io_aligned(fd, buf + align, max_align)) {
                s->buf_align = align;
                break;
            }
        }
        qemu_vfree(buf);
    }

    if (!bs->bl.request_alignment) {
        size_t align;
        buf = qemu_memalign(s->buf_align, max_align);
        for (align = 512; align <= max_align; align <<= 1) {
            if (raw_is_io_aligned(fd, buf, align)) {
                bs->bl.request_alignment = align;
                break;
            }
        }
        qemu_vfree(buf);
    }

    if (!s->buf_align || !bs->bl.request_alignment) {
        error_setg(errp, "Could not find working O_DIRECT alignment");
        error_append_hint(errp, "Try cache.direct=off\n");
    }
}

static void raw_parse_flags(int bdrv_flags, int *open_flags)
{
    assert(open_flags != NULL);

    *open_flags |= O_BINARY;
    *open_flags &= ~O_ACCMODE;
    if (bdrv_flags & BDRV_O_RDWR) {
        *open_flags |= O_RDWR;
    } else {
        *open_flags |= O_RDONLY;
    }

    /* Use O_DSYNC for write-through caching, no flags for write-back caching,
     * and O_DIRECT for no caching. */
    if ((bdrv_flags & BDRV_O_NOCACHE)) {
        *open_flags |= O_DIRECT;
    }
}

static void raw_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    bdrv_parse_filename_strip_prefix(filename, "file:", options);
}

static QemuOptsList raw_runtime_opts = {
    .name = "raw",
    .head = QTAILQ_HEAD_INITIALIZER(raw_runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "File name of the image",
        },
        {
            .name = "aio",
            .type = QEMU_OPT_STRING,
            .help = "host AIO implementation (threads, native)",
        },
        {
            .name = "locking",
            .type = QEMU_OPT_STRING,
            .help = "file locking mode (on/off/auto, default: auto)",
        },
        { /* end of list */ }
    },
};

static int raw_open_common(BlockDriverState *bs, QDict *options,
                           int bdrv_flags, int open_flags, Error **errp)
{
    BDRVRawState *s = bs->opaque;
    QemuOpts *opts;
    Error *local_err = NULL;
    const char *filename = NULL;
    BlockdevAioOptions aio, aio_default;
    int fd, ret;
    struct stat st;
    OnOffAuto locking;

    opts = qemu_opts_create(&raw_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }

    filename = qemu_opt_get(opts, "filename");

    ret = raw_normalize_devicepath(&filename);
    if (ret != 0) {
        error_setg_errno(errp, -ret, "Could not normalize device path");
        goto fail;
    }

    aio_default = (bdrv_flags & BDRV_O_NATIVE_AIO)
                  ? BLOCKDEV_AIO_OPTIONS_NATIVE
                  : BLOCKDEV_AIO_OPTIONS_THREADS;
    aio = qapi_enum_parse(BlockdevAioOptions_lookup, qemu_opt_get(opts, "aio"),
                          BLOCKDEV_AIO_OPTIONS__MAX, aio_default, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }
    s->use_linux_aio = (aio == BLOCKDEV_AIO_OPTIONS_NATIVE);

    locking = qapi_enum_parse(OnOffAuto_lookup, qemu_opt_get(opts, "locking"),
                              ON_OFF_AUTO__MAX, ON_OFF_AUTO_AUTO, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        ret = -EINVAL;
        goto fail;
    }
    switch (locking) {
    case ON_OFF_AUTO_ON:
        s->use_lock = true;
        if (!qemu_has_ofd_lock()) {
            fprintf(stderr,
                    "File lock requested but OFD locking syscall is "
                    "unavailable, falling back to POSIX file locks.\n"
                    "Due to the implementation, locks can be lost "
                    "unexpectedly.\n");
        }
        break;
    case ON_OFF_AUTO_OFF:
        s->use_lock = false;
        break;
    case ON_OFF_AUTO_AUTO:
        s->use_lock = qemu_has_ofd_lock();
        break;
    default:
        abort();
    }

    s->open_flags = open_flags;
    raw_parse_flags(bdrv_flags, &s->open_flags);

    s->fd = -1;
    fd = qemu_open(filename, s->open_flags, 0644);
    if (fd < 0) {
        ret = -errno;
        error_setg_errno(errp, errno, "Could not open '%s'", filename);
        if (ret == -EROFS) {
            ret = -EACCES;
        }
        goto fail;
    }
    s->fd = fd;

    s->lock_fd = -1;
    if (s->use_lock) {
        fd = qemu_open(filename, s->open_flags);
        if (fd < 0) {
            ret = -errno;
            error_setg_errno(errp, errno, "Could not open '%s' for locking",
                             filename);
            qemu_close(s->fd);
            goto fail;
        }
        s->lock_fd = fd;
    }
    s->perm = 0;
    s->shared_perm = BLK_PERM_ALL;

#ifdef CONFIG_LINUX_AIO
     /* Currently Linux does AIO only for files opened with O_DIRECT */
    if (s->use_linux_aio && !(s->open_flags & O_DIRECT)) {
        error_setg(errp, "aio=native was specified, but it requires "
                         "cache.direct=on, which was not specified.");
        ret = -EINVAL;
        goto fail;
    }
#else
    if (s->use_linux_aio) {
        error_setg(errp, "aio=native was specified, but is not supported "
                         "in this build.");
        ret = -EINVAL;
        goto fail;
    }
#endif /* !defined(CONFIG_LINUX_AIO) */

    s->has_discard = true;
    s->has_write_zeroes = true;
    bs->supported_zero_flags = BDRV_REQ_MAY_UNMAP;
    if ((bs->open_flags & BDRV_O_NOCACHE) != 0) {
        s->needs_alignment = true;
    }

    if (fstat(s->fd, &st) < 0) {
        ret = -errno;
        error_setg_errno(errp, errno, "Could not stat file");
        goto fail;
    }
    if (S_ISREG(st.st_mode)) {
        s->discard_zeroes = true;
        s->has_fallocate = true;
    }
    if (S_ISBLK(st.st_mode)) {
#ifdef BLKDISCARDZEROES
        unsigned int arg;
        if (ioctl(s->fd, BLKDISCARDZEROES, &arg) == 0 && arg) {
            s->discard_zeroes = true;
        }
#endif
#ifdef __linux__
        /* On Linux 3.10, BLKDISCARD leaves stale data in the page cache.  Do
         * not rely on the contents of discarded blocks unless using O_DIRECT.
         * Same for BLKZEROOUT.
         */
        if (!(bs->open_flags & BDRV_O_NOCACHE)) {
            s->discard_zeroes = false;
            s->has_write_zeroes = false;
        }
#endif
    }
#ifdef __FreeBSD__
    if (S_ISCHR(st.st_mode)) {
        /*
         * The file is a char device (disk), which on FreeBSD isn't behind
         * a pager, so force all requests to be aligned. This is needed
         * so QEMU makes sure all IO operations on the device are aligned
         * to sector size, or else FreeBSD will reject them with EINVAL.
         */
        s->needs_alignment = true;
    }
#endif

#ifdef CONFIG_XFS
    if (platform_test_xfs_fd(s->fd)) {
        s->is_xfs = true;
    }
#endif

    ret = 0;
fail:
    if (filename && (bdrv_flags & BDRV_O_TEMPORARY)) {
        unlink(filename);
    }
    qemu_opts_del(opts);
    return ret;
}

static int raw_open(BlockDriverState *bs, QDict *options, int flags,
                    Error **errp)
{
    BDRVRawState *s = bs->opaque;

    s->type = FTYPE_FILE;
    return raw_open_common(bs, options, flags, 0, errp);
}

typedef enum {
    RAW_PL_PREPARE,
    RAW_PL_COMMIT,
    RAW_PL_ABORT,
} RawPermLockOp;

#define PERM_FOREACH(i) \
    for ((i) = 0; (1ULL << (i)) <= BLK_PERM_ALL; i++)

/* Lock bytes indicated by @perm_lock_bits and @shared_perm_lock_bits in the
 * file; if @unlock == true, also unlock the unneeded bytes.
 * @shared_perm_lock_bits is the mask of all permissions that are NOT shared.
 */
static int raw_apply_lock_bytes(BDRVRawState *s,
                                uint64_t perm_lock_bits,
                                uint64_t shared_perm_lock_bits,
                                bool unlock, Error **errp)
{
    int ret;
    int i;

    PERM_FOREACH(i) {
        int off = RAW_LOCK_PERM_BASE + i;
        if (perm_lock_bits & (1ULL << i)) {
            ret = qemu_lock_fd(s->lock_fd, off, 1, false);
            if (ret) {
                error_setg(errp, "Failed to lock byte %d", off);
                return ret;
            }
        } else if (unlock) {
            ret = qemu_unlock_fd(s->lock_fd, off, 1);
            if (ret) {
                error_setg(errp, "Failed to unlock byte %d", off);
                return ret;
            }
        }
    }
    PERM_FOREACH(i) {
        int off = RAW_LOCK_SHARED_BASE + i;
        if (shared_perm_lock_bits & (1ULL << i)) {
            ret = qemu_lock_fd(s->lock_fd, off, 1, false);
            if (ret) {
                error_setg(errp, "Failed to lock byte %d", off);
                return ret;
            }
        } else if (unlock) {
            ret = qemu_unlock_fd(s->lock_fd, off, 1);
            if (ret) {
                error_setg(errp, "Failed to unlock byte %d", off);
                return ret;
            }
        }
    }
    return 0;
}

/* Check "unshared" bytes implied by @perm and ~@shared_perm in the file. */
static int raw_check_lock_bytes(BDRVRawState *s,
                                uint64_t perm, uint64_t shared_perm,
                                Error **errp)
{
    int ret;
    int i;

    PERM_FOREACH(i) {
        int off = RAW_LOCK_SHARED_BASE + i;
        uint64_t p = 1ULL << i;
        if (perm & p) {
            ret = qemu_lock_fd_test(s->lock_fd, off, 1, true);
            if (ret) {
                char *perm_name = bdrv_perm_names(p);
                error_setg(errp,
                           "Failed to get \"%s\" lock",
                           perm_name);
                g_free(perm_name);
                error_append_hint(errp,
                                  "Is another process using the image?\n");
                return ret;
            }
        }
    }
    PERM_FOREACH(i) {
        int off = RAW_LOCK_PERM_BASE + i;
        uint64_t p = 1ULL << i;
        if (!(shared_perm & p)) {
            ret = qemu_lock_fd_test(s->lock_fd, off, 1, true);
            if (ret) {
                char *perm_name = bdrv_perm_names(p);
                error_setg(errp,
                           "Failed to get shared \"%s\" lock",
                           perm_name);
                g_free(perm_name);
                error_append_hint(errp,
                                  "Is another process using the image?\n");
                return ret;
            }
        }
    }
    return 0;
}

static int raw_handle_perm_lock(BlockDriverState *bs,
                                RawPermLockOp op,
                                uint64_t new_perm, uint64_t new_shared,
                                Error **errp)
{
    BDRVRawState *s = bs->opaque;
    int ret = 0;
    Error *local_err = NULL;

    if (!s->use_lock) {
        return 0;
    }

    if (bdrv_get_flags(bs) & BDRV_O_INACTIVE) {
        return 0;
    }

    assert(s->lock_fd > 0);

    switch (op) {
    case RAW_PL_PREPARE:
        ret = raw_apply_lock_bytes(s, s->perm | new_perm,
                                   ~s->shared_perm | ~new_shared,
                                   false, errp);
        if (!ret) {
            ret = raw_check_lock_bytes(s, new_perm, new_shared, errp);
            if (!ret) {
                return 0;
            }
        }
        op = RAW_PL_ABORT;
        /* fall through to unlock bytes. */
    case RAW_PL_ABORT:
        raw_apply_lock_bytes(s, s->perm, ~s->shared_perm, true, &local_err);
        if (local_err) {
            /* Theoretically the above call only unlocks bytes and it cannot
             * fail. Something weird happened, report it.
             */
            error_report_err(local_err);
        }
        break;
    case RAW_PL_COMMIT:
        raw_apply_lock_bytes(s, new_perm, ~new_shared, true, &local_err);
        if (local_err) {
            /* Theoretically the above call only unlocks bytes and it cannot
             * fail. Something weird happened, report it.
             */
            error_report_err(local_err);
        }
        break;
    }
    return ret;
}

static int raw_reopen_prepare(BDRVReopenState *state,
                              BlockReopenQueue *queue, Error **errp)
{
    BDRVRawState *s;
    BDRVRawReopenState *rs;
    int ret = 0;
    Error *local_err = NULL;

    assert(state != NULL);
    assert(state->bs != NULL);

    s = state->bs->opaque;

    state->opaque = g_new0(BDRVRawReopenState, 1);
    rs = state->opaque;

    if (s->type == FTYPE_CD) {
        rs->open_flags |= O_NONBLOCK;
    }

    raw_parse_flags(state->flags, &rs->open_flags);

    rs->fd = -1;

    int fcntl_flags = O_APPEND | O_NONBLOCK;
#ifdef O_NOATIME
    fcntl_flags |= O_NOATIME;
#endif

#ifdef O_ASYNC
    /* Not all operating systems have O_ASYNC, and those that don't
     * will not let us track the state into rs->open_flags (typically
     * you achieve the same effect with an ioctl, for example I_SETSIG
     * on Solaris). But we do not use O_ASYNC, so that's fine.
     */
    assert((s->open_flags & O_ASYNC) == 0);
#endif

    if ((rs->open_flags & ~fcntl_flags) == (s->open_flags & ~fcntl_flags)) {
        /* dup the original fd */
        rs->fd = qemu_dup(s->fd);
        if (rs->fd >= 0) {
            ret = fcntl_setfl(rs->fd, rs->open_flags);
            if (ret) {
                qemu_close(rs->fd);
                rs->fd = -1;
            }
        }
    }

    /* If we cannot use fcntl, or fcntl failed, fall back to qemu_open() */
    if (rs->fd == -1) {
        const char *normalized_filename = state->bs->filename;
        ret = raw_normalize_devicepath(&normalized_filename);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not normalize device path");
        } else {
            assert(!(rs->open_flags & O_CREAT));
            rs->fd = qemu_open(normalized_filename, rs->open_flags);
            if (rs->fd == -1) {
                error_setg_errno(errp, errno, "Could not reopen file");
                ret = -1;
            }
        }
    }

    /* Fail already reopen_prepare() if we can't get a working O_DIRECT
     * alignment with the new fd. */
    if (rs->fd != -1) {
        raw_probe_alignment(state->bs, rs->fd, &local_err);
        if (local_err) {
            qemu_close(rs->fd);
            rs->fd = -1;
            error_propagate(errp, local_err);
            ret = -EINVAL;
        }
    }

    return ret;
}

static void raw_reopen_commit(BDRVReopenState *state)
{
    BDRVRawReopenState *rs = state->opaque;
    BDRVRawState *s = state->bs->opaque;

    s->open_flags = rs->open_flags;

    qemu_close(s->fd);
    s->fd = rs->fd;

    g_free(state->opaque);
    state->opaque = NULL;
}


static void raw_reopen_abort(BDRVReopenState *state)
{
    BDRVRawReopenState *rs = state->opaque;

     /* nothing to do if NULL, we didn't get far enough */
    if (rs == NULL) {
        return;
    }

    if (rs->fd >= 0) {
        qemu_close(rs->fd);
        rs->fd = -1;
    }
    g_free(state->opaque);
    state->opaque = NULL;
}

static int hdev_get_max_transfer_length(BlockDriverState *bs, int fd)
{
#ifdef BLKSECTGET
    int max_bytes = 0;
    short max_sectors = 0;
    if (bs->sg && ioctl(fd, BLKSECTGET, &max_bytes) == 0) {
        return max_bytes;
    } else if (!bs->sg && ioctl(fd, BLKSECTGET, &max_sectors) == 0) {
        return max_sectors << BDRV_SECTOR_BITS;
    } else {
        return -errno;
    }
#else
    return -ENOSYS;
#endif
}

static int hdev_get_max_segments(const struct stat *st)
{
#ifdef CONFIG_LINUX
    char buf[32];
    const char *end;
    char *sysfspath;
    int ret;
    int fd = -1;
    long max_segments;

    sysfspath = g_strdup_printf("/sys/dev/block/%u:%u/queue/max_segments",
                                major(st->st_rdev), minor(st->st_rdev));
    fd = open(sysfspath, O_RDONLY);
    if (fd == -1) {
        ret = -errno;
        goto out;
    }
    do {
        ret = read(fd, buf, sizeof(buf) - 1);
    } while (ret == -1 && errno == EINTR);
    if (ret < 0) {
        ret = -errno;
        goto out;
    } else if (ret == 0) {
        ret = -EIO;
        goto out;
    }
    buf[ret] = 0;
    /* The file is ended with '\n', pass 'end' to accept that. */
    ret = qemu_strtol(buf, &end, 10, &max_segments);
    if (ret == 0 && end && *end == '\n') {
        ret = max_segments;
    }

out:
    if (fd != -1) {
        close(fd);
    }
    g_free(sysfspath);
    return ret;
#else
    return -ENOTSUP;
#endif
}

static void raw_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVRawState *s = bs->opaque;
    struct stat st;

    if (!fstat(s->fd, &st)) {
        if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
            int ret = hdev_get_max_transfer_length(bs, s->fd);
            if (ret > 0 && ret <= BDRV_REQUEST_MAX_BYTES) {
                bs->bl.max_transfer = pow2floor(ret);
            }
            ret = hdev_get_max_segments(&st);
            if (ret > 0) {
                bs->bl.max_transfer = MIN(bs->bl.max_transfer,
                                          ret * getpagesize());
            }
        }
    }

    raw_probe_alignment(bs, s->fd, errp);
    bs->bl.min_mem_alignment = s->buf_align;
    bs->bl.opt_mem_alignment = MAX(s->buf_align, getpagesize());
}

static int check_for_dasd(int fd)
{
#ifdef BIODASDINFO2
    struct dasd_information2_t info = {0};

    return ioctl(fd, BIODASDINFO2, &info);
#else
    return -1;
#endif
}

/**
 * Try to get @bs's logical and physical block size.
 * On success, store them in @bsz and return zero.
 * On failure, return negative errno.
 */
static int hdev_probe_blocksizes(BlockDriverState *bs, BlockSizes *bsz)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    /* If DASD, get blocksizes */
    if (check_for_dasd(s->fd) < 0) {
        return -ENOTSUP;
    }
    ret = probe_logical_blocksize(s->fd, &bsz->log);
    if (ret < 0) {
        return ret;
    }
    return probe_physical_blocksize(s->fd, &bsz->phys);
}

/**
 * Try to get @bs's geometry: cyls, heads, sectors.
 * On success, store them in @geo and return 0.
 * On failure return -errno.
 * (Allows block driver to assign default geometry values that guest sees)
 */
#ifdef __linux__
static int hdev_probe_geometry(BlockDriverState *bs, HDGeometry *geo)
{
    BDRVRawState *s = bs->opaque;
    struct hd_geometry ioctl_geo = {0};

    /* If DASD, get its geometry */
    if (check_for_dasd(s->fd) < 0) {
        return -ENOTSUP;
    }
    if (ioctl(s->fd, HDIO_GETGEO, &ioctl_geo) < 0) {
        return -errno;
    }
    /* HDIO_GETGEO may return success even though geo contains zeros
       (e.g. certain multipath setups) */
    if (!ioctl_geo.heads || !ioctl_geo.sectors || !ioctl_geo.cylinders) {
        return -ENOTSUP;
    }
    /* Do not return a geometry for partition */
    if (ioctl_geo.start != 0) {
        return -ENOTSUP;
    }
    geo->heads = ioctl_geo.heads;
    geo->sectors = ioctl_geo.sectors;
    geo->cylinders = ioctl_geo.cylinders;

    return 0;
}
#else /* __linux__ */
static int hdev_probe_geometry(BlockDriverState *bs, HDGeometry *geo)
{
    return -ENOTSUP;
}
#endif

static ssize_t handle_aiocb_ioctl(RawPosixAIOData *aiocb)
{
    int ret;

    ret = ioctl(aiocb->aio_fildes, aiocb->aio_ioctl_cmd, aiocb->aio_ioctl_buf);
    if (ret == -1) {
        return -errno;
    }

    return 0;
}

static ssize_t handle_aiocb_flush(RawPosixAIOData *aiocb)
{
    BDRVRawState *s = aiocb->bs->opaque;
    int ret;

    if (s->page_cache_inconsistent) {
        return -EIO;
    }

    ret = qemu_fdatasync(aiocb->aio_fildes);
    if (ret == -1) {
        /* There is no clear definition of the semantics of a failing fsync(),
         * so we may have to assume the worst. The sad truth is that this
         * assumption is correct for Linux. Some pages are now probably marked
         * clean in the page cache even though they are inconsistent with the
         * on-disk contents. The next fdatasync() call would succeed, but no
         * further writeback attempt will be made. We can't get back to a state
         * in which we know what is on disk (we would have to rewrite
         * everything that was touched since the last fdatasync() at least), so
         * make bdrv_flush() fail permanently. Given that the behaviour isn't
         * really defined, I have little hope that other OSes are doing better.
         *
         * Obviously, this doesn't affect O_DIRECT, which bypasses the page
         * cache. */
        if ((s->open_flags & O_DIRECT) == 0) {
            s->page_cache_inconsistent = true;
        }
        return -errno;
    }
    return 0;
}

#ifdef CONFIG_PREADV

static bool preadv_present = true;

static ssize_t
qemu_preadv(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return preadv(fd, iov, nr_iov, offset);
}

static ssize_t
qemu_pwritev(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return pwritev(fd, iov, nr_iov, offset);
}

#else

static bool preadv_present = false;

static ssize_t
qemu_preadv(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return -ENOSYS;
}

static ssize_t
qemu_pwritev(int fd, const struct iovec *iov, int nr_iov, off_t offset)
{
    return -ENOSYS;
}

#endif

static ssize_t handle_aiocb_rw_vector(RawPosixAIOData *aiocb)
{
    ssize_t len;

    do {
        if (aiocb->aio_type & QEMU_AIO_WRITE)
            len = qemu_pwritev(aiocb->aio_fildes,
                               aiocb->aio_iov,
                               aiocb->aio_niov,
                               aiocb->aio_offset);
         else
            len = qemu_preadv(aiocb->aio_fildes,
                              aiocb->aio_iov,
                              aiocb->aio_niov,
                              aiocb->aio_offset);
    } while (len == -1 && errno == EINTR);

    if (len == -1) {
        return -errno;
    }
    return len;
}

/*
 * Read/writes the data to/from a given linear buffer.
 *
 * Returns the number of bytes handles or -errno in case of an error. Short
 * reads are only returned if the end of the file is reached.
 */
static ssize_t handle_aiocb_rw_linear(RawPosixAIOData *aiocb, char *buf)
{
    ssize_t offset = 0;
    ssize_t len;

    while (offset < aiocb->aio_nbytes) {
        if (aiocb->aio_type & QEMU_AIO_WRITE) {
            len = pwrite(aiocb->aio_fildes,
                         (const char *)buf + offset,
                         aiocb->aio_nbytes - offset,
                         aiocb->aio_offset + offset);
        } else {
            len = pread(aiocb->aio_fildes,
                        buf + offset,
                        aiocb->aio_nbytes - offset,
                        aiocb->aio_offset + offset);
        }
        if (len == -1 && errno == EINTR) {
            continue;
        } else if (len == -1 && errno == EINVAL &&
                   (aiocb->bs->open_flags & BDRV_O_NOCACHE) &&
                   !(aiocb->aio_type & QEMU_AIO_WRITE) &&
                   offset > 0) {
            /* O_DIRECT pread() may fail with EINVAL when offset is unaligned
             * after a short read.  Assume that O_DIRECT short reads only occur
             * at EOF.  Therefore this is a short read, not an I/O error.
             */
            break;
        } else if (len == -1) {
            offset = -errno;
            break;
        } else if (len == 0) {
            break;
        }
        offset += len;
    }

    return offset;
}

static ssize_t handle_aiocb_rw(RawPosixAIOData *aiocb)
{
    ssize_t nbytes;
    char *buf;

    if (!(aiocb->aio_type & QEMU_AIO_MISALIGNED)) {
        /*
         * If there is just a single buffer, and it is properly aligned
         * we can just use plain pread/pwrite without any problems.
         */
        if (aiocb->aio_niov == 1) {
             return handle_aiocb_rw_linear(aiocb, aiocb->aio_iov->iov_base);
        }
        /*
         * We have more than one iovec, and all are properly aligned.
         *
         * Try preadv/pwritev first and fall back to linearizing the
         * buffer if it's not supported.
         */
        if (preadv_present) {
            nbytes = handle_aiocb_rw_vector(aiocb);
            if (nbytes == aiocb->aio_nbytes ||
                (nbytes < 0 && nbytes != -ENOSYS)) {
                return nbytes;
            }
            preadv_present = false;
        }

        /*
         * XXX(hch): short read/write.  no easy way to handle the reminder
         * using these interfaces.  For now retry using plain
         * pread/pwrite?
         */
    }

    /*
     * Ok, we have to do it the hard way, copy all segments into
     * a single aligned buffer.
     */
    buf = qemu_try_blockalign(aiocb->bs, aiocb->aio_nbytes);
    if (buf == NULL) {
        return -ENOMEM;
    }

    if (aiocb->aio_type & QEMU_AIO_WRITE) {
        char *p = buf;
        int i;

        for (i = 0; i < aiocb->aio_niov; ++i) {
            memcpy(p, aiocb->aio_iov[i].iov_base, aiocb->aio_iov[i].iov_len);
            p += aiocb->aio_iov[i].iov_len;
        }
        assert(p - buf == aiocb->aio_nbytes);
    }

    nbytes = handle_aiocb_rw_linear(aiocb, buf);
    if (!(aiocb->aio_type & QEMU_AIO_WRITE)) {
        char *p = buf;
        size_t count = aiocb->aio_nbytes, copy;
        int i;

        for (i = 0; i < aiocb->aio_niov && count; ++i) {
            copy = count;
            if (copy > aiocb->aio_iov[i].iov_len) {
                copy = aiocb->aio_iov[i].iov_len;
            }
            memcpy(aiocb->aio_iov[i].iov_base, p, copy);
            assert(count >= copy);
            p     += copy;
            count -= copy;
        }
        assert(count == 0);
    }
    qemu_vfree(buf);

    return nbytes;
}

#ifdef CONFIG_XFS
static int xfs_write_zeroes(BDRVRawState *s, int64_t offset, uint64_t bytes)
{
    struct xfs_flock64 fl;
    int err;

    memset(&fl, 0, sizeof(fl));
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = bytes;

    if (xfsctl(NULL, s->fd, XFS_IOC_ZERO_RANGE, &fl) < 0) {
        err = errno;
        DPRINTF("cannot write zero range (%s)\n", strerror(errno));
        return -err;
    }

    return 0;
}

static int xfs_discard(BDRVRawState *s, int64_t offset, uint64_t bytes)
{
    struct xfs_flock64 fl;
    int err;

    memset(&fl, 0, sizeof(fl));
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = bytes;

    if (xfsctl(NULL, s->fd, XFS_IOC_UNRESVSP64, &fl) < 0) {
        err = errno;
        DPRINTF("cannot punch hole (%s)\n", strerror(errno));
        return -err;
    }

    return 0;
}
#endif

static int translate_err(int err)
{
    if (err == -ENODEV || err == -ENOSYS || err == -EOPNOTSUPP ||
        err == -ENOTTY) {
        err = -ENOTSUP;
    }
    return err;
}

#ifdef CONFIG_FALLOCATE
static int do_fallocate(int fd, int mode, off_t offset, off_t len)
{
    do {
        if (fallocate(fd, mode, offset, len) == 0) {
            return 0;
        }
    } while (errno == EINTR);
    return translate_err(-errno);
}
#endif

static ssize_t handle_aiocb_write_zeroes_block(RawPosixAIOData *aiocb)
{
    int ret = -ENOTSUP;
    BDRVRawState *s = aiocb->bs->opaque;

    if (!s->has_write_zeroes) {
        return -ENOTSUP;
    }

#ifdef BLKZEROOUT
    do {
        uint64_t range[2] = { aiocb->aio_offset, aiocb->aio_nbytes };
        if (ioctl(aiocb->aio_fildes, BLKZEROOUT, range) == 0) {
            return 0;
        }
    } while (errno == EINTR);

    ret = translate_err(-errno);
#endif

    if (ret == -ENOTSUP) {
        s->has_write_zeroes = false;
    }
    return ret;
}

static ssize_t handle_aiocb_write_zeroes(RawPosixAIOData *aiocb)
{
#if defined(CONFIG_FALLOCATE) || defined(CONFIG_XFS)
    BDRVRawState *s = aiocb->bs->opaque;
#endif
#ifdef CONFIG_FALLOCATE
    int64_t len;
#endif

    if (aiocb->aio_type & QEMU_AIO_BLKDEV) {
        return handle_aiocb_write_zeroes_block(aiocb);
    }

#ifdef CONFIG_XFS
    if (s->is_xfs) {
        return xfs_write_zeroes(s, aiocb->aio_offset, aiocb->aio_nbytes);
    }
#endif

#ifdef CONFIG_FALLOCATE_ZERO_RANGE
    if (s->has_write_zeroes) {
        int ret = do_fallocate(s->fd, FALLOC_FL_ZERO_RANGE,
                               aiocb->aio_offset, aiocb->aio_nbytes);
        if (ret == 0 || ret != -ENOTSUP) {
            return ret;
        }
        s->has_write_zeroes = false;
    }
#endif

#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
    if (s->has_discard && s->has_fallocate) {
        int ret = do_fallocate(s->fd,
                               FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                               aiocb->aio_offset, aiocb->aio_nbytes);
        if (ret == 0) {
            ret = do_fallocate(s->fd, 0, aiocb->aio_offset, aiocb->aio_nbytes);
            if (ret == 0 || ret != -ENOTSUP) {
                return ret;
            }
            s->has_fallocate = false;
        } else if (ret != -ENOTSUP) {
            return ret;
        } else {
            s->has_discard = false;
        }
    }
#endif

#ifdef CONFIG_FALLOCATE
    /* Last resort: we are trying to extend the file with zeroed data. This
     * can be done via fallocate(fd, 0) */
    len = bdrv_getlength(aiocb->bs);
    if (s->has_fallocate && len >= 0 && aiocb->aio_offset >= len) {
        int ret = do_fallocate(s->fd, 0, aiocb->aio_offset, aiocb->aio_nbytes);
        if (ret == 0 || ret != -ENOTSUP) {
            return ret;
        }
        s->has_fallocate = false;
    }
#endif

    return -ENOTSUP;
}

static ssize_t handle_aiocb_discard(RawPosixAIOData *aiocb)
{
    int ret = -EOPNOTSUPP;
    BDRVRawState *s = aiocb->bs->opaque;

    if (!s->has_discard) {
        return -ENOTSUP;
    }

    if (aiocb->aio_type & QEMU_AIO_BLKDEV) {
#ifdef BLKDISCARD
        do {
            uint64_t range[2] = { aiocb->aio_offset, aiocb->aio_nbytes };
            if (ioctl(aiocb->aio_fildes, BLKDISCARD, range) == 0) {
                return 0;
            }
        } while (errno == EINTR);

        ret = -errno;
#endif
    } else {
#ifdef CONFIG_XFS
        if (s->is_xfs) {
            return xfs_discard(s, aiocb->aio_offset, aiocb->aio_nbytes);
        }
#endif

#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
        ret = do_fallocate(s->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                           aiocb->aio_offset, aiocb->aio_nbytes);
#endif
    }

    ret = translate_err(ret);
    if (ret == -ENOTSUP) {
        s->has_discard = false;
    }
    return ret;
}

static int aio_worker(void *arg)
{
    RawPosixAIOData *aiocb = arg;
    ssize_t ret = 0;

    switch (aiocb->aio_type & QEMU_AIO_TYPE_MASK) {
    case QEMU_AIO_READ:
        ret = handle_aiocb_rw(aiocb);
        if (ret >= 0 && ret < aiocb->aio_nbytes) {
            iov_memset(aiocb->aio_iov, aiocb->aio_niov, ret,
                      0, aiocb->aio_nbytes - ret);

            ret = aiocb->aio_nbytes;
        }
        if (ret == aiocb->aio_nbytes) {
            ret = 0;
        } else if (ret >= 0 && ret < aiocb->aio_nbytes) {
            ret = -EINVAL;
        }
        break;
    case QEMU_AIO_WRITE:
        ret = handle_aiocb_rw(aiocb);
        if (ret == aiocb->aio_nbytes) {
            ret = 0;
        } else if (ret >= 0 && ret < aiocb->aio_nbytes) {
            ret = -EINVAL;
        }
        break;
    case QEMU_AIO_FLUSH:
        ret = handle_aiocb_flush(aiocb);
        break;
    case QEMU_AIO_IOCTL:
        ret = handle_aiocb_ioctl(aiocb);
        break;
    case QEMU_AIO_DISCARD:
        ret = handle_aiocb_discard(aiocb);
        break;
    case QEMU_AIO_WRITE_ZEROES:
        ret = handle_aiocb_write_zeroes(aiocb);
        break;
    default:
        fprintf(stderr, "invalid aio request (0x%x)\n", aiocb->aio_type);
        ret = -EINVAL;
        break;
    }

    g_free(aiocb);
    return ret;
}

static int paio_submit_co(BlockDriverState *bs, int fd,
                          int64_t offset, QEMUIOVector *qiov,
                          int bytes, int type)
{
    RawPosixAIOData *acb = g_new(RawPosixAIOData, 1);
    ThreadPool *pool;

    acb->bs = bs;
    acb->aio_type = type;
    acb->aio_fildes = fd;

    acb->aio_nbytes = bytes;
    acb->aio_offset = offset;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
        assert(qiov->size == bytes);
    }

    trace_paio_submit_co(offset, bytes, type);
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_co(pool, aio_worker, acb);
}

static BlockAIOCB *paio_submit(BlockDriverState *bs, int fd,
        int64_t offset, QEMUIOVector *qiov, int bytes,
        BlockCompletionFunc *cb, void *opaque, int type)
{
    RawPosixAIOData *acb = g_new(RawPosixAIOData, 1);
    ThreadPool *pool;

    acb->bs = bs;
    acb->aio_type = type;
    acb->aio_fildes = fd;

    acb->aio_nbytes = bytes;
    acb->aio_offset = offset;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
        assert(qiov->size == acb->aio_nbytes);
    }

    trace_paio_submit(acb, opaque, offset, bytes, type);
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_aio(pool, aio_worker, acb, cb, opaque);
}

static int coroutine_fn raw_co_prw(BlockDriverState *bs, uint64_t offset,
                                   uint64_t bytes, QEMUIOVector *qiov, int type)
{
    BDRVRawState *s = bs->opaque;

    if (fd_open(bs) < 0)
        return -EIO;

    /*
     * Check if the underlying device requires requests to be aligned,
     * and if the request we are trying to submit is aligned or not.
     * If this is the case tell the low-level driver that it needs
     * to copy the buffer.
     */
    if (s->needs_alignment) {
        if (!bdrv_qiov_is_aligned(bs, qiov)) {
            type |= QEMU_AIO_MISALIGNED;
#ifdef CONFIG_LINUX_AIO
        } else if (s->use_linux_aio) {
            LinuxAioState *aio = aio_get_linux_aio(bdrv_get_aio_context(bs));
            assert(qiov->size == bytes);
            return laio_co_submit(bs, aio, s->fd, offset, qiov, type);
#endif
        }
    }

    return paio_submit_co(bs, s->fd, offset, qiov, bytes, type);
}

static int coroutine_fn raw_co_preadv(BlockDriverState *bs, uint64_t offset,
                                      uint64_t bytes, QEMUIOVector *qiov,
                                      int flags)
{
    return raw_co_prw(bs, offset, bytes, qiov, QEMU_AIO_READ);
}

static int coroutine_fn raw_co_pwritev(BlockDriverState *bs, uint64_t offset,
                                       uint64_t bytes, QEMUIOVector *qiov,
                                       int flags)
{
    assert(flags == 0);
    return raw_co_prw(bs, offset, bytes, qiov, QEMU_AIO_WRITE);
}

static void raw_aio_plug(BlockDriverState *bs)
{
#ifdef CONFIG_LINUX_AIO
    BDRVRawState *s = bs->opaque;
    if (s->use_linux_aio) {
        LinuxAioState *aio = aio_get_linux_aio(bdrv_get_aio_context(bs));
        laio_io_plug(bs, aio);
    }
#endif
}

static void raw_aio_unplug(BlockDriverState *bs)
{
#ifdef CONFIG_LINUX_AIO
    BDRVRawState *s = bs->opaque;
    if (s->use_linux_aio) {
        LinuxAioState *aio = aio_get_linux_aio(bdrv_get_aio_context(bs));
        laio_io_unplug(bs, aio);
    }
#endif
}

static BlockAIOCB *raw_aio_flush(BlockDriverState *bs,
        BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;

    if (fd_open(bs) < 0)
        return NULL;

    return paio_submit(bs, s->fd, 0, NULL, 0, cb, opaque, QEMU_AIO_FLUSH);
}

static void raw_close(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    if (s->fd >= 0) {
        qemu_close(s->fd);
        s->fd = -1;
    }
    if (s->lock_fd >= 0) {
        qemu_close(s->lock_fd);
        s->lock_fd = -1;
    }
}

/**
 * Truncates the given regular file @fd to @offset and, when growing, fills the
 * new space according to @prealloc.
 *
 * Returns: 0 on success, -errno on failure.
 */
static int raw_regular_truncate(int fd, int64_t offset, PreallocMode prealloc,
                                Error **errp)
{
    int result = 0;
    int64_t current_length = 0;
    char *buf = NULL;
    struct stat st;

    if (fstat(fd, &st) < 0) {
        result = -errno;
        error_setg_errno(errp, -result, "Could not stat file");
        return result;
    }

    current_length = st.st_size;
    if (current_length > offset && prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Cannot use preallocation for shrinking files");
        return -ENOTSUP;
    }

    switch (prealloc) {
#ifdef CONFIG_POSIX_FALLOCATE
    case PREALLOC_MODE_FALLOC:
        /*
         * Truncating before posix_fallocate() makes it about twice slower on
         * file systems that do not support fallocate(), trying to check if a
         * block is allocated before allocating it, so don't do that here.
         */
        result = -posix_fallocate(fd, current_length, offset - current_length);
        if (result != 0) {
            /* posix_fallocate() doesn't set errno. */
            error_setg_errno(errp, -result,
                             "Could not preallocate new data");
        }
        goto out;
#endif
    case PREALLOC_MODE_FULL:
    {
        int64_t num = 0, left = offset - current_length;

        /*
         * Knowing the final size from the beginning could allow the file
         * system driver to do less allocations and possibly avoid
         * fragmentation of the file.
         */
        if (ftruncate(fd, offset) != 0) {
            result = -errno;
            error_setg_errno(errp, -result, "Could not resize file");
            goto out;
        }

        buf = g_malloc0(65536);

        result = lseek(fd, current_length, SEEK_SET);
        if (result < 0) {
            result = -errno;
            error_setg_errno(errp, -result,
                             "Failed to seek to the old end of file");
            goto out;
        }

        while (left > 0) {
            num = MIN(left, 65536);
            result = write(fd, buf, num);
            if (result < 0) {
                result = -errno;
                error_setg_errno(errp, -result,
                                 "Could not write zeros for preallocation");
                goto out;
            }
            left -= result;
        }
        if (result >= 0) {
            result = fsync(fd);
            if (result < 0) {
                result = -errno;
                error_setg_errno(errp, -result,
                                 "Could not flush file to disk");
                goto out;
            }
        }
        goto out;
    }
    case PREALLOC_MODE_OFF:
        if (ftruncate(fd, offset) != 0) {
            result = -errno;
            error_setg_errno(errp, -result, "Could not resize file");
        }
        return result;
    default:
        result = -ENOTSUP;
        error_setg(errp, "Unsupported preallocation mode: %s",
                   PreallocMode_lookup[prealloc]);
        return result;
    }

out:
    if (result < 0) {
        if (ftruncate(fd, current_length) < 0) {
            error_report("Failed to restore old file length: %s",
                         strerror(errno));
        }
    }

    g_free(buf);
    return result;
}

static int raw_truncate(BlockDriverState *bs, int64_t offset,
                        PreallocMode prealloc, Error **errp)
{
    BDRVRawState *s = bs->opaque;
    struct stat st;
    int ret;

    if (fstat(s->fd, &st)) {
        ret = -errno;
        error_setg_errno(errp, -ret, "Failed to fstat() the file");
        return ret;
    }

    if (S_ISREG(st.st_mode)) {
        return raw_regular_truncate(s->fd, offset, prealloc, errp);
    }

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Preallocation mode '%s' unsupported for this "
                   "non-regular file", PreallocMode_lookup[prealloc]);
        return -ENOTSUP;
    }

    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        if (offset > raw_getlength(bs)) {
            error_setg(errp, "Cannot grow device files");
            return -EINVAL;
        }
    } else {
        error_setg(errp, "Resizing this file is not supported");
        return -ENOTSUP;
    }

    return 0;
}

#ifdef __OpenBSD__
static int64_t raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int fd = s->fd;
    struct stat st;

    if (fstat(fd, &st))
        return -errno;
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        struct disklabel dl;

        if (ioctl(fd, DIOCGDINFO, &dl))
            return -errno;
        return (uint64_t)dl.d_secsize *
            dl.d_partitions[DISKPART(st.st_rdev)].p_size;
    } else
        return st.st_size;
}
#elif defined(__NetBSD__)
static int64_t raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int fd = s->fd;
    struct stat st;

    if (fstat(fd, &st))
        return -errno;
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        struct dkwedge_info dkw;

        if (ioctl(fd, DIOCGWEDGEINFO, &dkw) != -1) {
            return dkw.dkw_size * 512;
        } else {
            struct disklabel dl;

            if (ioctl(fd, DIOCGDINFO, &dl))
                return -errno;
            return (uint64_t)dl.d_secsize *
                dl.d_partitions[DISKPART(st.st_rdev)].p_size;
        }
    } else
        return st.st_size;
}
#elif defined(__sun__)
static int64_t raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    struct dk_minfo minfo;
    int ret;
    int64_t size;

    ret = fd_open(bs);
    if (ret < 0) {
        return ret;
    }

    /*
     * Use the DKIOCGMEDIAINFO ioctl to read the size.
     */
    ret = ioctl(s->fd, DKIOCGMEDIAINFO, &minfo);
    if (ret != -1) {
        return minfo.dki_lbsize * minfo.dki_capacity;
    }

    /*
     * There are reports that lseek on some devices fails, but
     * irc discussion said that contingency on contingency was overkill.
     */
    size = lseek(s->fd, 0, SEEK_END);
    if (size < 0) {
        return -errno;
    }
    return size;
}
#elif defined(CONFIG_BSD)
static int64_t raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int fd = s->fd;
    int64_t size;
    struct stat sb;
#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__)
    int reopened = 0;
#endif
    int ret;

    ret = fd_open(bs);
    if (ret < 0)
        return ret;

#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__)
again:
#endif
    if (!fstat(fd, &sb) && (S_IFCHR & sb.st_mode)) {
#ifdef DIOCGMEDIASIZE
	if (ioctl(fd, DIOCGMEDIASIZE, (off_t *)&size))
#elif defined(DIOCGPART)
        {
                struct partinfo pi;
                if (ioctl(fd, DIOCGPART, &pi) == 0)
                        size = pi.media_size;
                else
                        size = 0;
        }
        if (size == 0)
#endif
#if defined(__APPLE__) && defined(__MACH__)
        {
            uint64_t sectors = 0;
            uint32_t sector_size = 0;

            if (ioctl(fd, DKIOCGETBLOCKCOUNT, &sectors) == 0
               && ioctl(fd, DKIOCGETBLOCKSIZE, &sector_size) == 0) {
                size = sectors * sector_size;
            } else {
                size = lseek(fd, 0LL, SEEK_END);
                if (size < 0) {
                    return -errno;
                }
            }
        }
#else
        size = lseek(fd, 0LL, SEEK_END);
        if (size < 0) {
            return -errno;
        }
#endif
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        switch(s->type) {
        case FTYPE_CD:
            /* XXX FreeBSD acd returns UINT_MAX sectors for an empty drive */
            if (size == 2048LL * (unsigned)-1)
                size = 0;
            /* XXX no disc?  maybe we need to reopen... */
            if (size <= 0 && !reopened && cdrom_reopen(bs) >= 0) {
                reopened = 1;
                goto again;
            }
        }
#endif
    } else {
        size = lseek(fd, 0, SEEK_END);
        if (size < 0) {
            return -errno;
        }
    }
    return size;
}
#else
static int64_t raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int ret;
    int64_t size;

    ret = fd_open(bs);
    if (ret < 0) {
        return ret;
    }

    size = lseek(s->fd, 0, SEEK_END);
    if (size < 0) {
        return -errno;
    }
    return size;
}
#endif

static int64_t raw_get_allocated_file_size(BlockDriverState *bs)
{
    struct stat st;
    BDRVRawState *s = bs->opaque;

    if (fstat(s->fd, &st) < 0) {
        return -errno;
    }
    return (int64_t)st.st_blocks * 512;
}

static int raw_create(const char *filename, QemuOpts *opts, Error **errp)
{
    int fd;
    int result = 0;
    int64_t total_size = 0;
    bool nocow = false;
    PreallocMode prealloc;
    char *buf = NULL;
    Error *local_err = NULL;

    strstart(filename, "file:", &filename);

    /* Read out options */
    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);
    nocow = qemu_opt_get_bool(opts, BLOCK_OPT_NOCOW, false);
    buf = qemu_opt_get_del(opts, BLOCK_OPT_PREALLOC);
    prealloc = qapi_enum_parse(PreallocMode_lookup, buf,
                               PREALLOC_MODE__MAX, PREALLOC_MODE_OFF,
                               &local_err);
    g_free(buf);
    if (local_err) {
        error_propagate(errp, local_err);
        result = -EINVAL;
        goto out;
    }

    fd = qemu_open(filename, O_RDWR | O_CREAT | O_TRUNC | O_BINARY,
                   0644);
    if (fd < 0) {
        result = -errno;
        error_setg_errno(errp, -result, "Could not create file");
        goto out;
    }

    if (nocow) {
#ifdef __linux__
        /* Set NOCOW flag to solve performance issue on fs like btrfs.
         * This is an optimisation. The FS_IOC_SETFLAGS ioctl return value
         * will be ignored since any failure of this operation should not
         * block the left work.
         */
        int attr;
        if (ioctl(fd, FS_IOC_GETFLAGS, &attr) == 0) {
            attr |= FS_NOCOW_FL;
            ioctl(fd, FS_IOC_SETFLAGS, &attr);
        }
#endif
    }

    result = raw_regular_truncate(fd, total_size, prealloc, errp);
    if (result < 0) {
        goto out_close;
    }

out_close:
    if (qemu_close(fd) != 0 && result == 0) {
        result = -errno;
        error_setg_errno(errp, -result, "Could not close the new file");
    }
out:
    return result;
}

/*
 * Find allocation range in @bs around offset @start.
 * May change underlying file descriptor's file offset.
 * If @start is not in a hole, store @start in @data, and the
 * beginning of the next hole in @hole, and return 0.
 * If @start is in a non-trailing hole, store @start in @hole and the
 * beginning of the next non-hole in @data, and return 0.
 * If @start is in a trailing hole or beyond EOF, return -ENXIO.
 * If we can't find out, return a negative errno other than -ENXIO.
 */
static int find_allocation(BlockDriverState *bs, off_t start,
                           off_t *data, off_t *hole)
{
#if defined SEEK_HOLE && defined SEEK_DATA
    BDRVRawState *s = bs->opaque;
    off_t offs;

    /*
     * SEEK_DATA cases:
     * D1. offs == start: start is in data
     * D2. offs > start: start is in a hole, next data at offs
     * D3. offs < 0, errno = ENXIO: either start is in a trailing hole
     *                              or start is beyond EOF
     *     If the latter happens, the file has been truncated behind
     *     our back since we opened it.  All bets are off then.
     *     Treating like a trailing hole is simplest.
     * D4. offs < 0, errno != ENXIO: we learned nothing
     */
    offs = lseek(s->fd, start, SEEK_DATA);
    if (offs < 0) {
        return -errno;          /* D3 or D4 */
    }
    assert(offs >= start);

    if (offs > start) {
        /* D2: in hole, next data at offs */
        *hole = start;
        *data = offs;
        return 0;
    }

    /* D1: in data, end not yet known */

    /*
     * SEEK_HOLE cases:
     * H1. offs == start: start is in a hole
     *     If this happens here, a hole has been dug behind our back
     *     since the previous lseek().
     * H2. offs > start: either start is in data, next hole at offs,
     *                   or start is in trailing hole, EOF at offs
     *     Linux treats trailing holes like any other hole: offs ==
     *     start.  Solaris seeks to EOF instead: offs > start (blech).
     *     If that happens here, a hole has been dug behind our back
     *     since the previous lseek().
     * H3. offs < 0, errno = ENXIO: start is beyond EOF
     *     If this happens, the file has been truncated behind our
     *     back since we opened it.  Treat it like a trailing hole.
     * H4. offs < 0, errno != ENXIO: we learned nothing
     *     Pretend we know nothing at all, i.e. "forget" about D1.
     */
    offs = lseek(s->fd, start, SEEK_HOLE);
    if (offs < 0) {
        return -errno;          /* D1 and (H3 or H4) */
    }
    assert(offs >= start);

    if (offs > start) {
        /*
         * D1 and H2: either in data, next hole at offs, or it was in
         * data but is now in a trailing hole.  In the latter case,
         * all bets are off.  Treating it as if it there was data all
         * the way to EOF is safe, so simply do that.
         */
        *data = start;
        *hole = offs;
        return 0;
    }

    /* D1 and H1 */
    return -EBUSY;
#else
    return -ENOTSUP;
#endif
}

/*
 * Returns the allocation status of the specified sectors.
 *
 * If 'sector_num' is beyond the end of the disk image the return value is 0
 * and 'pnum' is set to 0.
 *
 * 'pnum' is set to the number of sectors (including and immediately following
 * the specified sector) that are known to be in the same
 * allocated/unallocated state.
 *
 * 'nb_sectors' is the max value 'pnum' should be set to.  If nb_sectors goes
 * beyond the end of the disk image it will be clamped.
 */
static int64_t coroutine_fn raw_co_get_block_status(BlockDriverState *bs,
                                                    int64_t sector_num,
                                                    int nb_sectors, int *pnum,
                                                    BlockDriverState **file)
{
    off_t start, data = 0, hole = 0;
    int64_t total_size;
    int ret;

    ret = fd_open(bs);
    if (ret < 0) {
        return ret;
    }

    start = sector_num * BDRV_SECTOR_SIZE;
    total_size = bdrv_getlength(bs);
    if (total_size < 0) {
        return total_size;
    } else if (start >= total_size) {
        *pnum = 0;
        return 0;
    } else if (start + nb_sectors * BDRV_SECTOR_SIZE > total_size) {
        nb_sectors = DIV_ROUND_UP(total_size - start, BDRV_SECTOR_SIZE);
    }

    ret = find_allocation(bs, start, &data, &hole);
    if (ret == -ENXIO) {
        /* Trailing hole */
        *pnum = nb_sectors;
        ret = BDRV_BLOCK_ZERO;
    } else if (ret < 0) {
        /* No info available, so pretend there are no holes */
        *pnum = nb_sectors;
        ret = BDRV_BLOCK_DATA;
    } else if (data == start) {
        /* On a data extent, compute sectors to the end of the extent,
         * possibly including a partial sector at EOF. */
        *pnum = MIN(nb_sectors, DIV_ROUND_UP(hole - start, BDRV_SECTOR_SIZE));
        ret = BDRV_BLOCK_DATA;
    } else {
        /* On a hole, compute sectors to the beginning of the next extent.  */
        assert(hole == start);
        *pnum = MIN(nb_sectors, (data - start) / BDRV_SECTOR_SIZE);
        ret = BDRV_BLOCK_ZERO;
    }
    *file = bs;
    return ret | BDRV_BLOCK_OFFSET_VALID | start;
}

static coroutine_fn BlockAIOCB *raw_aio_pdiscard(BlockDriverState *bs,
    int64_t offset, int bytes,
    BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;

    return paio_submit(bs, s->fd, offset, NULL, bytes,
                       cb, opaque, QEMU_AIO_DISCARD);
}

static int coroutine_fn raw_co_pwrite_zeroes(
    BlockDriverState *bs, int64_t offset,
    int bytes, BdrvRequestFlags flags)
{
    BDRVRawState *s = bs->opaque;

    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        return paio_submit_co(bs, s->fd, offset, NULL, bytes,
                              QEMU_AIO_WRITE_ZEROES);
    } else if (s->discard_zeroes) {
        return paio_submit_co(bs, s->fd, offset, NULL, bytes,
                              QEMU_AIO_DISCARD);
    }
    return -ENOTSUP;
}

static int raw_get_info(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVRawState *s = bs->opaque;

    bdi->unallocated_blocks_are_zero = s->discard_zeroes;
    bdi->can_write_zeroes_with_unmap = s->discard_zeroes;
    return 0;
}

static QemuOptsList raw_create_opts = {
    .name = "raw-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(raw_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_NOCOW,
            .type = QEMU_OPT_BOOL,
            .help = "Turn off copy-on-write (valid only on btrfs)"
        },
        {
            .name = BLOCK_OPT_PREALLOC,
            .type = QEMU_OPT_STRING,
            .help = "Preallocation mode (allowed values: off, falloc, full)"
        },
        { /* end of list */ }
    }
};

static int raw_check_perm(BlockDriverState *bs, uint64_t perm, uint64_t shared,
                          Error **errp)
{
    return raw_handle_perm_lock(bs, RAW_PL_PREPARE, perm, shared, errp);
}

static void raw_set_perm(BlockDriverState *bs, uint64_t perm, uint64_t shared)
{
    BDRVRawState *s = bs->opaque;
    raw_handle_perm_lock(bs, RAW_PL_COMMIT, perm, shared, NULL);
    s->perm = perm;
    s->shared_perm = shared;
}

static void raw_abort_perm_update(BlockDriverState *bs)
{
    raw_handle_perm_lock(bs, RAW_PL_ABORT, 0, 0, NULL);
}

BlockDriver bdrv_file = {
    .format_name = "file",
    .protocol_name = "file",
    .instance_size = sizeof(BDRVRawState),
    .bdrv_needs_filename = true,
    .bdrv_probe = NULL, /* no probe for protocols */
    .bdrv_parse_filename = raw_parse_filename,
    .bdrv_file_open = raw_open,
    .bdrv_reopen_prepare = raw_reopen_prepare,
    .bdrv_reopen_commit = raw_reopen_commit,
    .bdrv_reopen_abort = raw_reopen_abort,
    .bdrv_close = raw_close,
    .bdrv_create = raw_create,
    .bdrv_has_zero_init = bdrv_has_zero_init_1,
    .bdrv_co_get_block_status = raw_co_get_block_status,
    .bdrv_co_pwrite_zeroes = raw_co_pwrite_zeroes,

    .bdrv_co_preadv         = raw_co_preadv,
    .bdrv_co_pwritev        = raw_co_pwritev,
    .bdrv_aio_flush = raw_aio_flush,
    .bdrv_aio_pdiscard = raw_aio_pdiscard,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,

    .bdrv_truncate = raw_truncate,
    .bdrv_getlength = raw_getlength,
    .bdrv_get_info = raw_get_info,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,
    .bdrv_check_perm = raw_check_perm,
    .bdrv_set_perm   = raw_set_perm,
    .bdrv_abort_perm_update = raw_abort_perm_update,
    .create_opts = &raw_create_opts,
};

/***********************************************/
/* host device */

#if defined(__APPLE__) && defined(__MACH__)
static kern_return_t GetBSDPath(io_iterator_t mediaIterator, char *bsdPath,
                                CFIndex maxPathSize, int flags);
static char *FindEjectableOpticalMedia(io_iterator_t *mediaIterator)
{
    kern_return_t kernResult = KERN_FAILURE;
    mach_port_t     masterPort;
    CFMutableDictionaryRef  classesToMatch;
    const char *matching_array[] = {kIODVDMediaClass, kIOCDMediaClass};
    char *mediaType = NULL;

    kernResult = IOMasterPort( MACH_PORT_NULL, &masterPort );
    if ( KERN_SUCCESS != kernResult ) {
        printf( "IOMasterPort returned %d\n", kernResult );
    }

    int index;
    for (index = 0; index < ARRAY_SIZE(matching_array); index++) {
        classesToMatch = IOServiceMatching(matching_array[index]);
        if (classesToMatch == NULL) {
            error_report("IOServiceMatching returned NULL for %s",
                         matching_array[index]);
            continue;
        }
        CFDictionarySetValue(classesToMatch, CFSTR(kIOMediaEjectableKey),
                             kCFBooleanTrue);
        kernResult = IOServiceGetMatchingServices(masterPort, classesToMatch,
                                                  mediaIterator);
        if (kernResult != KERN_SUCCESS) {
            error_report("Note: IOServiceGetMatchingServices returned %d",
                         kernResult);
            continue;
        }

        /* If a match was found, leave the loop */
        if (*mediaIterator != 0) {
            DPRINTF("Matching using %s\n", matching_array[index]);
            mediaType = g_strdup(matching_array[index]);
            break;
        }
    }
    return mediaType;
}

kern_return_t GetBSDPath(io_iterator_t mediaIterator, char *bsdPath,
                         CFIndex maxPathSize, int flags)
{
    io_object_t     nextMedia;
    kern_return_t   kernResult = KERN_FAILURE;
    *bsdPath = '\0';
    nextMedia = IOIteratorNext( mediaIterator );
    if ( nextMedia )
    {
        CFTypeRef   bsdPathAsCFString;
    bsdPathAsCFString = IORegistryEntryCreateCFProperty( nextMedia, CFSTR( kIOBSDNameKey ), kCFAllocatorDefault, 0 );
        if ( bsdPathAsCFString ) {
            size_t devPathLength;
            strcpy( bsdPath, _PATH_DEV );
            if (flags & BDRV_O_NOCACHE) {
                strcat(bsdPath, "r");
            }
            devPathLength = strlen( bsdPath );
            if ( CFStringGetCString( bsdPathAsCFString, bsdPath + devPathLength, maxPathSize - devPathLength, kCFStringEncodingASCII ) ) {
                kernResult = KERN_SUCCESS;
            }
            CFRelease( bsdPathAsCFString );
        }
        IOObjectRelease( nextMedia );
    }

    return kernResult;
}

/* Sets up a real cdrom for use in QEMU */
static bool setup_cdrom(char *bsd_path, Error **errp)
{
    int index, num_of_test_partitions = 2, fd;
    char test_partition[MAXPATHLEN];
    bool partition_found = false;

    /* look for a working partition */
    for (index = 0; index < num_of_test_partitions; index++) {
        snprintf(test_partition, sizeof(test_partition), "%ss%d", bsd_path,
                 index);
        fd = qemu_open(test_partition, O_RDONLY | O_BINARY | O_LARGEFILE);
        if (fd >= 0) {
            partition_found = true;
            qemu_close(fd);
            break;
        }
    }

    /* if a working partition on the device was not found */
    if (partition_found == false) {
        error_setg(errp, "Failed to find a working partition on disc");
    } else {
        DPRINTF("Using %s as optical disc\n", test_partition);
        pstrcpy(bsd_path, MAXPATHLEN, test_partition);
    }
    return partition_found;
}

/* Prints directions on mounting and unmounting a device */
static void print_unmounting_directions(const char *file_name)
{
    error_report("If device %s is mounted on the desktop, unmount"
                 " it first before using it in QEMU", file_name);
    error_report("Command to unmount device: diskutil unmountDisk %s",
                 file_name);
    error_report("Command to mount device: diskutil mountDisk %s", file_name);
}

#endif /* defined(__APPLE__) && defined(__MACH__) */

static int hdev_probe_device(const char *filename)
{
    struct stat st;

    /* allow a dedicated CD-ROM driver to match with a higher priority */
    if (strstart(filename, "/dev/cdrom", NULL))
        return 50;

    if (stat(filename, &st) >= 0 &&
            (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))) {
        return 100;
    }

    return 0;
}

static int check_hdev_writable(BDRVRawState *s)
{
#if defined(BLKROGET)
    /* Linux block devices can be configured "read-only" using blockdev(8).
     * This is independent of device node permissions and therefore open(2)
     * with O_RDWR succeeds.  Actual writes fail with EPERM.
     *
     * bdrv_open() is supposed to fail if the disk is read-only.  Explicitly
     * check for read-only block devices so that Linux block devices behave
     * properly.
     */
    struct stat st;
    int readonly = 0;

    if (fstat(s->fd, &st)) {
        return -errno;
    }

    if (!S_ISBLK(st.st_mode)) {
        return 0;
    }

    if (ioctl(s->fd, BLKROGET, &readonly) < 0) {
        return -errno;
    }

    if (readonly) {
        return -EACCES;
    }
#endif /* defined(BLKROGET) */
    return 0;
}

static void hdev_parse_filename(const char *filename, QDict *options,
                                Error **errp)
{
    bdrv_parse_filename_strip_prefix(filename, "host_device:", options);
}

static bool hdev_is_sg(BlockDriverState *bs)
{

#if defined(__linux__)

    BDRVRawState *s = bs->opaque;
    struct stat st;
    struct sg_scsi_id scsiid;
    int sg_version;
    int ret;

    if (stat(bs->filename, &st) < 0 || !S_ISCHR(st.st_mode)) {
        return false;
    }

    ret = ioctl(s->fd, SG_GET_VERSION_NUM, &sg_version);
    if (ret < 0) {
        return false;
    }

    ret = ioctl(s->fd, SG_GET_SCSI_ID, &scsiid);
    if (ret >= 0) {
        DPRINTF("SG device found: type=%d, version=%d\n",
            scsiid.scsi_type, sg_version);
        return true;
    }

#endif

    return false;
}

static int hdev_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    BDRVRawState *s = bs->opaque;
    Error *local_err = NULL;
    int ret;

#if defined(__APPLE__) && defined(__MACH__)
    /*
     * Caution: while qdict_get_str() is fine, getting non-string types
     * would require more care.  When @options come from -blockdev or
     * blockdev_add, its members are typed according to the QAPI
     * schema, but when they come from -drive, they're all QString.
     */
    const char *filename = qdict_get_str(options, "filename");
    char bsd_path[MAXPATHLEN] = "";
    bool error_occurred = false;

    /* If using a real cdrom */
    if (strcmp(filename, "/dev/cdrom") == 0) {
        char *mediaType = NULL;
        kern_return_t ret_val;
        io_iterator_t mediaIterator = 0;

        mediaType = FindEjectableOpticalMedia(&mediaIterator);
        if (mediaType == NULL) {
            error_setg(errp, "Please make sure your CD/DVD is in the optical"
                       " drive");
            error_occurred = true;
            goto hdev_open_Mac_error;
        }

        ret_val = GetBSDPath(mediaIterator, bsd_path, sizeof(bsd_path), flags);
        if (ret_val != KERN_SUCCESS) {
            error_setg(errp, "Could not get BSD path for optical drive");
            error_occurred = true;
            goto hdev_open_Mac_error;
        }

        /* If a real optical drive was not found */
        if (bsd_path[0] == '\0') {
            error_setg(errp, "Failed to obtain bsd path for optical drive");
            error_occurred = true;
            goto hdev_open_Mac_error;
        }

        /* If using a cdrom disc and finding a partition on the disc failed */
        if (strncmp(mediaType, kIOCDMediaClass, 9) == 0 &&
            setup_cdrom(bsd_path, errp) == false) {
            print_unmounting_directions(bsd_path);
            error_occurred = true;
            goto hdev_open_Mac_error;
        }

        qdict_put_str(options, "filename", bsd_path);

hdev_open_Mac_error:
        g_free(mediaType);
        if (mediaIterator) {
            IOObjectRelease(mediaIterator);
        }
        if (error_occurred) {
            return -ENOENT;
        }
    }
#endif /* defined(__APPLE__) && defined(__MACH__) */

    s->type = FTYPE_FILE;

    ret = raw_open_common(bs, options, flags, 0, &local_err);
    if (ret < 0) {
        error_propagate(errp, local_err);
#if defined(__APPLE__) && defined(__MACH__)
        if (*bsd_path) {
            filename = bsd_path;
        }
        /* if a physical device experienced an error while being opened */
        if (strncmp(filename, "/dev/", 5) == 0) {
            print_unmounting_directions(filename);
        }
#endif /* defined(__APPLE__) && defined(__MACH__) */
        return ret;
    }

    /* Since this does ioctl the device must be already opened */
    bs->sg = hdev_is_sg(bs);

    if (flags & BDRV_O_RDWR) {
        ret = check_hdev_writable(s);
        if (ret < 0) {
            raw_close(bs);
            error_setg_errno(errp, -ret, "The device is not writable");
            return ret;
        }
    }

    return ret;
}

#if defined(__linux__)

static BlockAIOCB *hdev_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    RawPosixAIOData *acb;
    ThreadPool *pool;

    if (fd_open(bs) < 0)
        return NULL;

    acb = g_new(RawPosixAIOData, 1);
    acb->bs = bs;
    acb->aio_type = QEMU_AIO_IOCTL;
    acb->aio_fildes = s->fd;
    acb->aio_offset = 0;
    acb->aio_ioctl_buf = buf;
    acb->aio_ioctl_cmd = req;
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_aio(pool, aio_worker, acb, cb, opaque);
}
#endif /* linux */

static int fd_open(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    /* this is just to ensure s->fd is sane (its called by io ops) */
    if (s->fd >= 0)
        return 0;
    return -EIO;
}

static coroutine_fn BlockAIOCB *hdev_aio_pdiscard(BlockDriverState *bs,
    int64_t offset, int bytes,
    BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;

    if (fd_open(bs) < 0) {
        return NULL;
    }
    return paio_submit(bs, s->fd, offset, NULL, bytes,
                       cb, opaque, QEMU_AIO_DISCARD|QEMU_AIO_BLKDEV);
}

static coroutine_fn int hdev_co_pwrite_zeroes(BlockDriverState *bs,
    int64_t offset, int bytes, BdrvRequestFlags flags)
{
    BDRVRawState *s = bs->opaque;
    int rc;

    rc = fd_open(bs);
    if (rc < 0) {
        return rc;
    }
    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        return paio_submit_co(bs, s->fd, offset, NULL, bytes,
                              QEMU_AIO_WRITE_ZEROES|QEMU_AIO_BLKDEV);
    } else if (s->discard_zeroes) {
        return paio_submit_co(bs, s->fd, offset, NULL, bytes,
                              QEMU_AIO_DISCARD|QEMU_AIO_BLKDEV);
    }
    return -ENOTSUP;
}

static int hdev_create(const char *filename, QemuOpts *opts,
                       Error **errp)
{
    int fd;
    int ret = 0;
    struct stat stat_buf;
    int64_t total_size = 0;
    bool has_prefix;

    /* This function is used by both protocol block drivers and therefore either
     * of these prefixes may be given.
     * The return value has to be stored somewhere, otherwise this is an error
     * due to -Werror=unused-value. */
    has_prefix =
        strstart(filename, "host_device:", &filename) ||
        strstart(filename, "host_cdrom:" , &filename);

    (void)has_prefix;

    ret = raw_normalize_devicepath(&filename);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Could not normalize device path");
        return ret;
    }

    /* Read out options */
    total_size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                          BDRV_SECTOR_SIZE);

    fd = qemu_open(filename, O_WRONLY | O_BINARY);
    if (fd < 0) {
        ret = -errno;
        error_setg_errno(errp, -ret, "Could not open device");
        return ret;
    }

    if (fstat(fd, &stat_buf) < 0) {
        ret = -errno;
        error_setg_errno(errp, -ret, "Could not stat device");
    } else if (!S_ISBLK(stat_buf.st_mode) && !S_ISCHR(stat_buf.st_mode)) {
        error_setg(errp,
                   "The given file is neither a block nor a character device");
        ret = -ENODEV;
    } else if (lseek(fd, 0, SEEK_END) < total_size) {
        error_setg(errp, "Device is too small");
        ret = -ENOSPC;
    }

    qemu_close(fd);
    return ret;
}

static BlockDriver bdrv_host_device = {
    .format_name        = "host_device",
    .protocol_name        = "host_device",
    .instance_size      = sizeof(BDRVRawState),
    .bdrv_needs_filename = true,
    .bdrv_probe_device  = hdev_probe_device,
    .bdrv_parse_filename = hdev_parse_filename,
    .bdrv_file_open     = hdev_open,
    .bdrv_close         = raw_close,
    .bdrv_reopen_prepare = raw_reopen_prepare,
    .bdrv_reopen_commit  = raw_reopen_commit,
    .bdrv_reopen_abort   = raw_reopen_abort,
    .bdrv_create         = hdev_create,
    .create_opts         = &raw_create_opts,
    .bdrv_co_pwrite_zeroes = hdev_co_pwrite_zeroes,

    .bdrv_co_preadv         = raw_co_preadv,
    .bdrv_co_pwritev        = raw_co_pwritev,
    .bdrv_aio_flush	= raw_aio_flush,
    .bdrv_aio_pdiscard   = hdev_aio_pdiscard,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,

    .bdrv_truncate      = raw_truncate,
    .bdrv_getlength	= raw_getlength,
    .bdrv_get_info = raw_get_info,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,
    .bdrv_check_perm = raw_check_perm,
    .bdrv_set_perm   = raw_set_perm,
    .bdrv_abort_perm_update = raw_abort_perm_update,
    .bdrv_probe_blocksizes = hdev_probe_blocksizes,
    .bdrv_probe_geometry = hdev_probe_geometry,

    /* generic scsi device */
#ifdef __linux__
    .bdrv_aio_ioctl     = hdev_aio_ioctl,
#endif
};

#if defined(__linux__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
static void cdrom_parse_filename(const char *filename, QDict *options,
                                 Error **errp)
{
    bdrv_parse_filename_strip_prefix(filename, "host_cdrom:", options);
}
#endif

#ifdef __linux__
static int cdrom_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVRawState *s = bs->opaque;

    s->type = FTYPE_CD;

    /* open will not fail even if no CD is inserted, so add O_NONBLOCK */
    return raw_open_common(bs, options, flags, O_NONBLOCK, errp);
}

static int cdrom_probe_device(const char *filename)
{
    int fd, ret;
    int prio = 0;
    struct stat st;

    fd = qemu_open(filename, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        goto out;
    }
    ret = fstat(fd, &st);
    if (ret == -1 || !S_ISBLK(st.st_mode)) {
        goto outc;
    }

    /* Attempt to detect via a CDROM specific ioctl */
    ret = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
    if (ret >= 0)
        prio = 100;

outc:
    qemu_close(fd);
out:
    return prio;
}

static bool cdrom_is_inserted(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    ret = ioctl(s->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
    return ret == CDS_DISC_OK;
}

static void cdrom_eject(BlockDriverState *bs, bool eject_flag)
{
    BDRVRawState *s = bs->opaque;

    if (eject_flag) {
        if (ioctl(s->fd, CDROMEJECT, NULL) < 0)
            perror("CDROMEJECT");
    } else {
        if (ioctl(s->fd, CDROMCLOSETRAY, NULL) < 0)
            perror("CDROMEJECT");
    }
}

static void cdrom_lock_medium(BlockDriverState *bs, bool locked)
{
    BDRVRawState *s = bs->opaque;

    if (ioctl(s->fd, CDROM_LOCKDOOR, locked) < 0) {
        /*
         * Note: an error can happen if the distribution automatically
         * mounts the CD-ROM
         */
        /* perror("CDROM_LOCKDOOR"); */
    }
}

static BlockDriver bdrv_host_cdrom = {
    .format_name        = "host_cdrom",
    .protocol_name      = "host_cdrom",
    .instance_size      = sizeof(BDRVRawState),
    .bdrv_needs_filename = true,
    .bdrv_probe_device	= cdrom_probe_device,
    .bdrv_parse_filename = cdrom_parse_filename,
    .bdrv_file_open     = cdrom_open,
    .bdrv_close         = raw_close,
    .bdrv_reopen_prepare = raw_reopen_prepare,
    .bdrv_reopen_commit  = raw_reopen_commit,
    .bdrv_reopen_abort   = raw_reopen_abort,
    .bdrv_create         = hdev_create,
    .create_opts         = &raw_create_opts,


    .bdrv_co_preadv         = raw_co_preadv,
    .bdrv_co_pwritev        = raw_co_pwritev,
    .bdrv_aio_flush	= raw_aio_flush,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,

    .bdrv_truncate      = raw_truncate,
    .bdrv_getlength      = raw_getlength,
    .has_variable_length = true,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,

    /* removable device support */
    .bdrv_is_inserted   = cdrom_is_inserted,
    .bdrv_eject         = cdrom_eject,
    .bdrv_lock_medium   = cdrom_lock_medium,

    /* generic scsi device */
    .bdrv_aio_ioctl     = hdev_aio_ioctl,
};
#endif /* __linux__ */

#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__)
static int cdrom_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVRawState *s = bs->opaque;
    Error *local_err = NULL;
    int ret;

    s->type = FTYPE_CD;

    ret = raw_open_common(bs, options, flags, 0, &local_err);
    if (ret) {
        error_propagate(errp, local_err);
        return ret;
    }

    /* make sure the door isn't locked at this time */
    ioctl(s->fd, CDIOCALLOW);
    return 0;
}

static int cdrom_probe_device(const char *filename)
{
    if (strstart(filename, "/dev/cd", NULL) ||
            strstart(filename, "/dev/acd", NULL))
        return 100;
    return 0;
}

static int cdrom_reopen(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int fd;

    /*
     * Force reread of possibly changed/newly loaded disc,
     * FreeBSD seems to not notice sometimes...
     */
    if (s->fd >= 0)
        qemu_close(s->fd);
    fd = qemu_open(bs->filename, s->open_flags, 0644);
    if (fd < 0) {
        s->fd = -1;
        return -EIO;
    }
    s->fd = fd;

    /* make sure the door isn't locked at this time */
    ioctl(s->fd, CDIOCALLOW);
    return 0;
}

static bool cdrom_is_inserted(BlockDriverState *bs)
{
    return raw_getlength(bs) > 0;
}

static void cdrom_eject(BlockDriverState *bs, bool eject_flag)
{
    BDRVRawState *s = bs->opaque;

    if (s->fd < 0)
        return;

    (void) ioctl(s->fd, CDIOCALLOW);

    if (eject_flag) {
        if (ioctl(s->fd, CDIOCEJECT) < 0)
            perror("CDIOCEJECT");
    } else {
        if (ioctl(s->fd, CDIOCCLOSE) < 0)
            perror("CDIOCCLOSE");
    }

    cdrom_reopen(bs);
}

static void cdrom_lock_medium(BlockDriverState *bs, bool locked)
{
    BDRVRawState *s = bs->opaque;

    if (s->fd < 0)
        return;
    if (ioctl(s->fd, (locked ? CDIOCPREVENT : CDIOCALLOW)) < 0) {
        /*
         * Note: an error can happen if the distribution automatically
         * mounts the CD-ROM
         */
        /* perror("CDROM_LOCKDOOR"); */
    }
}

static BlockDriver bdrv_host_cdrom = {
    .format_name        = "host_cdrom",
    .protocol_name      = "host_cdrom",
    .instance_size      = sizeof(BDRVRawState),
    .bdrv_needs_filename = true,
    .bdrv_probe_device	= cdrom_probe_device,
    .bdrv_parse_filename = cdrom_parse_filename,
    .bdrv_file_open     = cdrom_open,
    .bdrv_close         = raw_close,
    .bdrv_reopen_prepare = raw_reopen_prepare,
    .bdrv_reopen_commit  = raw_reopen_commit,
    .bdrv_reopen_abort   = raw_reopen_abort,
    .bdrv_create        = hdev_create,
    .create_opts        = &raw_create_opts,

    .bdrv_co_preadv         = raw_co_preadv,
    .bdrv_co_pwritev        = raw_co_pwritev,
    .bdrv_aio_flush	= raw_aio_flush,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,

    .bdrv_truncate      = raw_truncate,
    .bdrv_getlength      = raw_getlength,
    .has_variable_length = true,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,

    /* removable device support */
    .bdrv_is_inserted   = cdrom_is_inserted,
    .bdrv_eject         = cdrom_eject,
    .bdrv_lock_medium   = cdrom_lock_medium,
};
#endif /* __FreeBSD__ */

static void bdrv_file_init(void)
{
    /*
     * Register all the drivers.  Note that order is important, the driver
     * registered last will get probed first.
     */
    bdrv_register(&bdrv_file);
    bdrv_register(&bdrv_host_device);
#ifdef __linux__
    bdrv_register(&bdrv_host_cdrom);
#endif
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
    bdrv_register(&bdrv_host_cdrom);
#endif
}

block_init(bdrv_file_init);
