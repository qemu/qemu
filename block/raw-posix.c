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
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "trace.h"
#include "block/thread-pool.h"
#include "qemu/iov.h"
#include "raw-aio.h"
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

typedef struct BDRVRawState {
    int fd;
    int type;
    int open_flags;
    size_t buf_align;

#ifdef CONFIG_LINUX_AIO
    int use_aio;
    void *aio_ctx;
#endif
#ifdef CONFIG_XFS
    bool is_xfs:1;
#endif
    bool has_discard:1;
    bool has_write_zeroes:1;
    bool discard_zeroes:1;
    bool has_fallocate;
    bool needs_alignment;
} BDRVRawState;

typedef struct BDRVRawReopenState {
    int fd;
    int open_flags;
#ifdef CONFIG_LINUX_AIO
    int use_aio;
#endif
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

    errno = ENOTSUP;

    /* Try a few ioctls to get the right size */
#ifdef BLKSSZGET
    if (ioctl(fd, BLKSSZGET, &sector_size) >= 0) {
        *sector_size_p = sector_size;
        success = true;
    }
#endif
#ifdef DKIOCGETBLOCKSIZE
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &sector_size) >= 0) {
        *sector_size_p = sector_size;
        success = true;
    }
#endif
#ifdef DIOCGSECTORSIZE
    if (ioctl(fd, DIOCGSECTORSIZE, &sector_size) >= 0) {
        *sector_size_p = sector_size;
        success = true;
    }
#endif

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
        bs->request_alignment = 1;
        s->buf_align = 1;
        return;
    }

    bs->request_alignment = 0;
    s->buf_align = 0;
    /* Let's try to use the logical blocksize for the alignment. */
    if (probe_logical_blocksize(fd, &bs->request_alignment) < 0) {
        bs->request_alignment = 0;
    }
#ifdef CONFIG_XFS
    if (s->is_xfs) {
        struct dioattr da;
        if (xfsctl(NULL, fd, XFS_IOC_DIOINFO, &da) >= 0) {
            bs->request_alignment = da.d_miniosz;
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

    if (!bs->request_alignment) {
        size_t align;
        buf = qemu_memalign(s->buf_align, max_align);
        for (align = 512; align <= max_align; align <<= 1) {
            if (raw_is_io_aligned(fd, buf, align)) {
                bs->request_alignment = align;
                break;
            }
        }
        qemu_vfree(buf);
    }

    if (!s->buf_align || !bs->request_alignment) {
        error_setg(errp, "Could not find working O_DIRECT alignment. "
                         "Try cache.direct=off.");
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

static void raw_detach_aio_context(BlockDriverState *bs)
{
#ifdef CONFIG_LINUX_AIO
    BDRVRawState *s = bs->opaque;

    if (s->use_aio) {
        laio_detach_aio_context(s->aio_ctx, bdrv_get_aio_context(bs));
    }
#endif
}

static void raw_attach_aio_context(BlockDriverState *bs,
                                   AioContext *new_context)
{
#ifdef CONFIG_LINUX_AIO
    BDRVRawState *s = bs->opaque;

    if (s->use_aio) {
        laio_attach_aio_context(s->aio_ctx, new_context);
    }
#endif
}

#ifdef CONFIG_LINUX_AIO
static int raw_set_aio(void **aio_ctx, int *use_aio, int bdrv_flags)
{
    int ret = -1;
    assert(aio_ctx != NULL);
    assert(use_aio != NULL);
    /*
     * Currently Linux do AIO only for files opened with O_DIRECT
     * specified so check NOCACHE flag too
     */
    if ((bdrv_flags & (BDRV_O_NOCACHE|BDRV_O_NATIVE_AIO)) ==
                      (BDRV_O_NOCACHE|BDRV_O_NATIVE_AIO)) {

        /* if non-NULL, laio_init() has already been run */
        if (*aio_ctx == NULL) {
            *aio_ctx = laio_init();
            if (!*aio_ctx) {
                goto error;
            }
        }
        *use_aio = 1;
    } else {
        *use_aio = 0;
    }

    ret = 0;

error:
    return ret;
}
#endif

static void raw_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    /* The filename does not have to be prefixed by the protocol name, since
     * "file" is the default protocol; therefore, the return value of this
     * function call can be ignored. */
    strstart(filename, "file:", &filename);

    qdict_put_obj(options, "filename", QOBJECT(qstring_from_str(filename)));
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
    int fd, ret;
    struct stat st;

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

    s->open_flags = open_flags;
    raw_parse_flags(bdrv_flags, &s->open_flags);

    s->fd = -1;
    fd = qemu_open(filename, s->open_flags, 0644);
    if (fd < 0) {
        ret = -errno;
        if (ret == -EROFS) {
            ret = -EACCES;
        }
        goto fail;
    }
    s->fd = fd;

#ifdef CONFIG_LINUX_AIO
    if (raw_set_aio(&s->aio_ctx, &s->use_aio, bdrv_flags)) {
        qemu_close(fd);
        ret = -errno;
        error_setg_errno(errp, -ret, "Could not set AIO state");
        goto fail;
    }
    if (!s->use_aio && (bdrv_flags & BDRV_O_NATIVE_AIO)) {
        error_setg(errp, "aio=native was specified, but it requires "
                         "cache.direct=on, which was not specified.");
        ret = -EINVAL;
        goto fail;
    }
#else
    if (bdrv_flags & BDRV_O_NATIVE_AIO) {
        error_setg(errp, "aio=native was specified, but is not supported "
                         "in this build.");
        ret = -EINVAL;
        goto fail;
    }
#endif /* !defined(CONFIG_LINUX_AIO) */

    s->has_discard = true;
    s->has_write_zeroes = true;
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

    raw_attach_aio_context(bs, bdrv_get_aio_context(bs));

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
    Error *local_err = NULL;
    int ret;

    s->type = FTYPE_FILE;
    ret = raw_open_common(bs, options, flags, 0, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;
}

static int raw_reopen_prepare(BDRVReopenState *state,
                              BlockReopenQueue *queue, Error **errp)
{
    BDRVRawState *s;
    BDRVRawReopenState *raw_s;
    int ret = 0;
    Error *local_err = NULL;

    assert(state != NULL);
    assert(state->bs != NULL);

    s = state->bs->opaque;

    state->opaque = g_new0(BDRVRawReopenState, 1);
    raw_s = state->opaque;

#ifdef CONFIG_LINUX_AIO
    raw_s->use_aio = s->use_aio;

    /* we can use s->aio_ctx instead of a copy, because the use_aio flag is
     * valid in the 'false' condition even if aio_ctx is set, and raw_set_aio()
     * won't override aio_ctx if aio_ctx is non-NULL */
    if (raw_set_aio(&s->aio_ctx, &raw_s->use_aio, state->flags)) {
        error_setg(errp, "Could not set AIO state");
        return -1;
    }
#endif

    if (s->type == FTYPE_CD) {
        raw_s->open_flags |= O_NONBLOCK;
    }

    raw_parse_flags(state->flags, &raw_s->open_flags);

    raw_s->fd = -1;

    int fcntl_flags = O_APPEND | O_NONBLOCK;
#ifdef O_NOATIME
    fcntl_flags |= O_NOATIME;
#endif

#ifdef O_ASYNC
    /* Not all operating systems have O_ASYNC, and those that don't
     * will not let us track the state into raw_s->open_flags (typically
     * you achieve the same effect with an ioctl, for example I_SETSIG
     * on Solaris). But we do not use O_ASYNC, so that's fine.
     */
    assert((s->open_flags & O_ASYNC) == 0);
#endif

    if ((raw_s->open_flags & ~fcntl_flags) == (s->open_flags & ~fcntl_flags)) {
        /* dup the original fd */
        /* TODO: use qemu fcntl wrapper */
#ifdef F_DUPFD_CLOEXEC
        raw_s->fd = fcntl(s->fd, F_DUPFD_CLOEXEC, 0);
#else
        raw_s->fd = dup(s->fd);
        if (raw_s->fd != -1) {
            qemu_set_cloexec(raw_s->fd);
        }
#endif
        if (raw_s->fd >= 0) {
            ret = fcntl_setfl(raw_s->fd, raw_s->open_flags);
            if (ret) {
                qemu_close(raw_s->fd);
                raw_s->fd = -1;
            }
        }
    }

    /* If we cannot use fcntl, or fcntl failed, fall back to qemu_open() */
    if (raw_s->fd == -1) {
        const char *normalized_filename = state->bs->filename;
        ret = raw_normalize_devicepath(&normalized_filename);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Could not normalize device path");
        } else {
            assert(!(raw_s->open_flags & O_CREAT));
            raw_s->fd = qemu_open(normalized_filename, raw_s->open_flags);
            if (raw_s->fd == -1) {
                error_setg_errno(errp, errno, "Could not reopen file");
                ret = -1;
            }
        }
    }

    /* Fail already reopen_prepare() if we can't get a working O_DIRECT
     * alignment with the new fd. */
    if (raw_s->fd != -1) {
        raw_probe_alignment(state->bs, raw_s->fd, &local_err);
        if (local_err) {
            qemu_close(raw_s->fd);
            raw_s->fd = -1;
            error_propagate(errp, local_err);
            ret = -EINVAL;
        }
    }

    return ret;
}

static void raw_reopen_commit(BDRVReopenState *state)
{
    BDRVRawReopenState *raw_s = state->opaque;
    BDRVRawState *s = state->bs->opaque;

    s->open_flags = raw_s->open_flags;

    qemu_close(s->fd);
    s->fd = raw_s->fd;
#ifdef CONFIG_LINUX_AIO
    s->use_aio = raw_s->use_aio;
#endif

    g_free(state->opaque);
    state->opaque = NULL;
}


static void raw_reopen_abort(BDRVReopenState *state)
{
    BDRVRawReopenState *raw_s = state->opaque;

     /* nothing to do if NULL, we didn't get far enough */
    if (raw_s == NULL) {
        return;
    }

    if (raw_s->fd >= 0) {
        qemu_close(raw_s->fd);
        raw_s->fd = -1;
    }
    g_free(state->opaque);
    state->opaque = NULL;
}

static void raw_refresh_limits(BlockDriverState *bs, Error **errp)
{
    BDRVRawState *s = bs->opaque;

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
    int ret;

    ret = qemu_fdatasync(aiocb->aio_fildes);
    if (ret == -1) {
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
    if (s->has_fallocate && aiocb->aio_offset >= bdrv_getlength(aiocb->bs)) {
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
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        int type)
{
    RawPosixAIOData *acb = g_new(RawPosixAIOData, 1);
    ThreadPool *pool;

    acb->bs = bs;
    acb->aio_type = type;
    acb->aio_fildes = fd;

    acb->aio_nbytes = nb_sectors * BDRV_SECTOR_SIZE;
    acb->aio_offset = sector_num * BDRV_SECTOR_SIZE;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
        assert(qiov->size == acb->aio_nbytes);
    }

    trace_paio_submit_co(sector_num, nb_sectors, type);
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_co(pool, aio_worker, acb);
}

static BlockAIOCB *paio_submit(BlockDriverState *bs, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque, int type)
{
    RawPosixAIOData *acb = g_new(RawPosixAIOData, 1);
    ThreadPool *pool;

    acb->bs = bs;
    acb->aio_type = type;
    acb->aio_fildes = fd;

    acb->aio_nbytes = nb_sectors * BDRV_SECTOR_SIZE;
    acb->aio_offset = sector_num * BDRV_SECTOR_SIZE;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
        assert(qiov->size == acb->aio_nbytes);
    }

    trace_paio_submit(acb, opaque, sector_num, nb_sectors, type);
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_aio(pool, aio_worker, acb, cb, opaque);
}

static BlockAIOCB *raw_aio_submit(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque, int type)
{
    BDRVRawState *s = bs->opaque;

    if (fd_open(bs) < 0)
        return NULL;

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
        } else if (s->use_aio) {
            return laio_submit(bs, s->aio_ctx, s->fd, sector_num, qiov,
                               nb_sectors, cb, opaque, type);
#endif
        }
    }

    return paio_submit(bs, s->fd, sector_num, qiov, nb_sectors,
                       cb, opaque, type);
}

static void raw_aio_plug(BlockDriverState *bs)
{
#ifdef CONFIG_LINUX_AIO
    BDRVRawState *s = bs->opaque;
    if (s->use_aio) {
        laio_io_plug(bs, s->aio_ctx);
    }
#endif
}

static void raw_aio_unplug(BlockDriverState *bs)
{
#ifdef CONFIG_LINUX_AIO
    BDRVRawState *s = bs->opaque;
    if (s->use_aio) {
        laio_io_unplug(bs, s->aio_ctx, true);
    }
#endif
}

static void raw_aio_flush_io_queue(BlockDriverState *bs)
{
#ifdef CONFIG_LINUX_AIO
    BDRVRawState *s = bs->opaque;
    if (s->use_aio) {
        laio_io_unplug(bs, s->aio_ctx, false);
    }
#endif
}

static BlockAIOCB *raw_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque)
{
    return raw_aio_submit(bs, sector_num, qiov, nb_sectors,
                          cb, opaque, QEMU_AIO_READ);
}

static BlockAIOCB *raw_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockCompletionFunc *cb, void *opaque)
{
    return raw_aio_submit(bs, sector_num, qiov, nb_sectors,
                          cb, opaque, QEMU_AIO_WRITE);
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

    raw_detach_aio_context(bs);

#ifdef CONFIG_LINUX_AIO
    if (s->use_aio) {
        laio_cleanup(s->aio_ctx);
    }
#endif
    if (s->fd >= 0) {
        qemu_close(s->fd);
        s->fd = -1;
    }
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVRawState *s = bs->opaque;
    struct stat st;

    if (fstat(s->fd, &st)) {
        return -errno;
    }

    if (S_ISREG(st.st_mode)) {
        if (ftruncate(s->fd, offset) < 0) {
            return -errno;
        }
    } else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
       if (offset > raw_getlength(bs)) {
           return -EINVAL;
       }
    } else {
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

    if (ftruncate(fd, total_size) != 0) {
        result = -errno;
        error_setg_errno(errp, -result, "Could not resize file");
        goto out_close;
    }

    switch (prealloc) {
#ifdef CONFIG_POSIX_FALLOCATE
    case PREALLOC_MODE_FALLOC:
        /* posix_fallocate() doesn't set errno. */
        result = -posix_fallocate(fd, 0, total_size);
        if (result != 0) {
            error_setg_errno(errp, -result,
                             "Could not preallocate data for the new file");
        }
        break;
#endif
    case PREALLOC_MODE_FULL:
    {
        int64_t num = 0, left = total_size;
        buf = g_malloc0(65536);

        while (left > 0) {
            num = MIN(left, 65536);
            result = write(fd, buf, num);
            if (result < 0) {
                result = -errno;
                error_setg_errno(errp, -result,
                                 "Could not write to the new file");
                break;
            }
            left -= result;
        }
        if (result >= 0) {
            result = fsync(fd);
            if (result < 0) {
                result = -errno;
                error_setg_errno(errp, -result,
                                 "Could not flush new file to disk");
            }
        }
        g_free(buf);
        break;
    }
    case PREALLOC_MODE_OFF:
        break;
    default:
        result = -EINVAL;
        error_setg(errp, "Unsupported preallocation mode: %s",
                   PreallocMode_lookup[prealloc]);
        break;
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

static coroutine_fn BlockAIOCB *raw_aio_discard(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors,
    BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;

    return paio_submit(bs, s->fd, sector_num, NULL, nb_sectors,
                       cb, opaque, QEMU_AIO_DISCARD);
}

static int coroutine_fn raw_co_write_zeroes(
    BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, BdrvRequestFlags flags)
{
    BDRVRawState *s = bs->opaque;

    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        return paio_submit_co(bs, s->fd, sector_num, NULL, nb_sectors,
                              QEMU_AIO_WRITE_ZEROES);
    } else if (s->discard_zeroes) {
        return paio_submit_co(bs, s->fd, sector_num, NULL, nb_sectors,
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
    .bdrv_co_write_zeroes = raw_co_write_zeroes,

    .bdrv_aio_readv = raw_aio_readv,
    .bdrv_aio_writev = raw_aio_writev,
    .bdrv_aio_flush = raw_aio_flush,
    .bdrv_aio_discard = raw_aio_discard,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,
    .bdrv_flush_io_queue = raw_aio_flush_io_queue,

    .bdrv_truncate = raw_truncate,
    .bdrv_getlength = raw_getlength,
    .bdrv_get_info = raw_get_info,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,

    .bdrv_detach_aio_context = raw_detach_aio_context,
    .bdrv_attach_aio_context = raw_attach_aio_context,

    .create_opts = &raw_create_opts,
};

/***********************************************/
/* host device */

#if defined(__APPLE__) && defined(__MACH__)
static kern_return_t FindEjectableCDMedia( io_iterator_t *mediaIterator );
static kern_return_t GetBSDPath(io_iterator_t mediaIterator, char *bsdPath,
                                CFIndex maxPathSize, int flags);
kern_return_t FindEjectableCDMedia( io_iterator_t *mediaIterator )
{
    kern_return_t       kernResult;
    mach_port_t     masterPort;
    CFMutableDictionaryRef  classesToMatch;

    kernResult = IOMasterPort( MACH_PORT_NULL, &masterPort );
    if ( KERN_SUCCESS != kernResult ) {
        printf( "IOMasterPort returned %d\n", kernResult );
    }

    classesToMatch = IOServiceMatching( kIOCDMediaClass );
    if ( classesToMatch == NULL ) {
        printf( "IOServiceMatching returned a NULL dictionary.\n" );
    } else {
    CFDictionarySetValue( classesToMatch, CFSTR( kIOMediaEjectableKey ), kCFBooleanTrue );
    }
    kernResult = IOServiceGetMatchingServices( masterPort, classesToMatch, mediaIterator );
    if ( KERN_SUCCESS != kernResult )
    {
        printf( "IOServiceGetMatchingServices returned %d\n", kernResult );
    }

    return kernResult;
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

#endif

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
    /* The prefix is optional, just as for "file". */
    strstart(filename, "host_device:", &filename);

    qdict_put_obj(options, "filename", QOBJECT(qstring_from_str(filename)));
}

static bool hdev_is_sg(BlockDriverState *bs)
{

#if defined(__linux__)

    struct stat st;
    struct sg_scsi_id scsiid;
    int sg_version;

    if (stat(bs->filename, &st) >= 0 && S_ISCHR(st.st_mode) &&
        !bdrv_ioctl(bs, SG_GET_VERSION_NUM, &sg_version) &&
        !bdrv_ioctl(bs, SG_GET_SCSI_ID, &scsiid)) {
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
    const char *filename = qdict_get_str(options, "filename");

    if (strstart(filename, "/dev/cdrom", NULL)) {
        kern_return_t kernResult;
        io_iterator_t mediaIterator;
        char bsdPath[ MAXPATHLEN ];
        int fd;

        kernResult = FindEjectableCDMedia( &mediaIterator );
        kernResult = GetBSDPath(mediaIterator, bsdPath, sizeof(bsdPath),
                                flags);
        if ( bsdPath[ 0 ] != '\0' ) {
            strcat(bsdPath,"s0");
            /* some CDs don't have a partition 0 */
            fd = qemu_open(bsdPath, O_RDONLY | O_BINARY | O_LARGEFILE);
            if (fd < 0) {
                bsdPath[strlen(bsdPath)-1] = '1';
            } else {
                qemu_close(fd);
            }
            filename = bsdPath;
            qdict_put(options, "filename", qstring_from_str(filename));
        }

        if ( mediaIterator )
            IOObjectRelease( mediaIterator );
    }
#endif

    s->type = FTYPE_FILE;

    ret = raw_open_common(bs, options, flags, 0, &local_err);
    if (ret < 0) {
        if (local_err) {
            error_propagate(errp, local_err);
        }
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

static coroutine_fn BlockAIOCB *hdev_aio_discard(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors,
    BlockCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;

    if (fd_open(bs) < 0) {
        return NULL;
    }
    return paio_submit(bs, s->fd, sector_num, NULL, nb_sectors,
                       cb, opaque, QEMU_AIO_DISCARD|QEMU_AIO_BLKDEV);
}

static coroutine_fn int hdev_co_write_zeroes(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors, BdrvRequestFlags flags)
{
    BDRVRawState *s = bs->opaque;
    int rc;

    rc = fd_open(bs);
    if (rc < 0) {
        return rc;
    }
    if (!(flags & BDRV_REQ_MAY_UNMAP)) {
        return paio_submit_co(bs, s->fd, sector_num, NULL, nb_sectors,
                              QEMU_AIO_WRITE_ZEROES|QEMU_AIO_BLKDEV);
    } else if (s->discard_zeroes) {
        return paio_submit_co(bs, s->fd, sector_num, NULL, nb_sectors,
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
    .bdrv_co_write_zeroes = hdev_co_write_zeroes,

    .bdrv_aio_readv	= raw_aio_readv,
    .bdrv_aio_writev	= raw_aio_writev,
    .bdrv_aio_flush	= raw_aio_flush,
    .bdrv_aio_discard   = hdev_aio_discard,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,
    .bdrv_flush_io_queue = raw_aio_flush_io_queue,

    .bdrv_truncate      = raw_truncate,
    .bdrv_getlength	= raw_getlength,
    .bdrv_get_info = raw_get_info,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,
    .bdrv_probe_blocksizes = hdev_probe_blocksizes,
    .bdrv_probe_geometry = hdev_probe_geometry,

    .bdrv_detach_aio_context = raw_detach_aio_context,
    .bdrv_attach_aio_context = raw_attach_aio_context,

    /* generic scsi device */
#ifdef __linux__
    .bdrv_aio_ioctl     = hdev_aio_ioctl,
#endif
};

#if defined(__linux__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
static void cdrom_parse_filename(const char *filename, QDict *options,
                                 Error **errp)
{
    /* The prefix is optional, just as for "file". */
    strstart(filename, "host_cdrom:", &filename);

    qdict_put_obj(options, "filename", QOBJECT(qstring_from_str(filename)));
}
#endif

#ifdef __linux__
static int cdrom_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVRawState *s = bs->opaque;
    Error *local_err = NULL;
    int ret;

    s->type = FTYPE_CD;

    /* open will not fail even if no CD is inserted, so add O_NONBLOCK */
    ret = raw_open_common(bs, options, flags, O_NONBLOCK, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
    }
    return ret;
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

    .bdrv_aio_readv     = raw_aio_readv,
    .bdrv_aio_writev    = raw_aio_writev,
    .bdrv_aio_flush	= raw_aio_flush,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,
    .bdrv_flush_io_queue = raw_aio_flush_io_queue,

    .bdrv_truncate      = raw_truncate,
    .bdrv_getlength      = raw_getlength,
    .has_variable_length = true,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,

    .bdrv_detach_aio_context = raw_detach_aio_context,
    .bdrv_attach_aio_context = raw_attach_aio_context,

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
        if (local_err) {
            error_propagate(errp, local_err);
        }
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

    .bdrv_aio_readv     = raw_aio_readv,
    .bdrv_aio_writev    = raw_aio_writev,
    .bdrv_aio_flush	= raw_aio_flush,
    .bdrv_refresh_limits = raw_refresh_limits,
    .bdrv_io_plug = raw_aio_plug,
    .bdrv_io_unplug = raw_aio_unplug,
    .bdrv_flush_io_queue = raw_aio_flush_io_queue,

    .bdrv_truncate      = raw_truncate,
    .bdrv_getlength      = raw_getlength,
    .has_variable_length = true,
    .bdrv_get_allocated_file_size
                        = raw_get_allocated_file_size,

    .bdrv_detach_aio_context = raw_detach_aio_context,
    .bdrv_attach_aio_context = raw_attach_aio_context,

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
