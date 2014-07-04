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
#include "qemu-common.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "block/block_int.h"
#include "qemu/module.h"
#include "trace.h"
#include "block/thread-pool.h"
#include "qemu/iov.h"
#include "raw-aio.h"

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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <linux/fs.h>
#ifndef FS_NOCOW_FL
#define FS_NOCOW_FL                     0x00800000 /* Do not cow file */
#endif
#endif
#ifdef CONFIG_FIEMAP
#include <linux/fiemap.h>
#endif
#ifdef CONFIG_FALLOCATE_PUNCH_HOLE
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

//#define DEBUG_FLOPPY

//#define DEBUG_BLOCK
#if defined(DEBUG_BLOCK)
#define DEBUG_BLOCK_PRINT(formatCstr, ...) do { if (qemu_log_enabled()) \
    { qemu_log(formatCstr, ## __VA_ARGS__); qemu_log_flush(); } } while (0)
#else
#define DEBUG_BLOCK_PRINT(formatCstr, ...)
#endif

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
#define FTYPE_FD     2

/* if the FD is not accessed during that time (in ns), we try to
   reopen it to see if the disk has been changed */
#define FD_OPEN_TIMEOUT (1000000000)

#define MAX_BLOCKSIZE	4096

typedef struct BDRVRawState {
    int fd;
    int type;
    int open_flags;
    size_t buf_align;

#if defined(__linux__)
    /* linux floppy specific */
    int64_t fd_open_time;
    int64_t fd_error_time;
    int fd_got_error;
    int fd_media_changed;
#endif
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
#ifdef CONFIG_FIEMAP
    bool skip_fiemap;
#endif
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

static void raw_probe_alignment(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    char *buf;
    unsigned int sector_size;

    /* For /dev/sg devices the alignment is not really used.
       With buffered I/O, we don't have any restrictions. */
    if (bs->sg || !(s->open_flags & O_DIRECT)) {
        bs->request_alignment = 1;
        s->buf_align = 1;
        return;
    }

    /* Try a few ioctls to get the right size */
    bs->request_alignment = 0;
    s->buf_align = 0;

#ifdef BLKSSZGET
    if (ioctl(s->fd, BLKSSZGET, &sector_size) >= 0) {
        bs->request_alignment = sector_size;
    }
#endif
#ifdef DKIOCGETBLOCKSIZE
    if (ioctl(s->fd, DKIOCGETBLOCKSIZE, &sector_size) >= 0) {
        bs->request_alignment = sector_size;
    }
#endif
#ifdef DIOCGSECTORSIZE
    if (ioctl(s->fd, DIOCGSECTORSIZE, &sector_size) >= 0) {
        bs->request_alignment = sector_size;
    }
#endif
#ifdef CONFIG_XFS
    if (s->is_xfs) {
        struct dioattr da;
        if (xfsctl(NULL, s->fd, XFS_IOC_DIOINFO, &da) >= 0) {
            bs->request_alignment = da.d_miniosz;
            /* The kernel returns wrong information for d_mem */
            /* s->buf_align = da.d_mem; */
        }
    }
#endif

    /* If we could not get the sizes so far, we can only guess them */
    if (!s->buf_align) {
        size_t align;
        buf = qemu_memalign(MAX_BLOCKSIZE, 2 * MAX_BLOCKSIZE);
        for (align = 512; align <= MAX_BLOCKSIZE; align <<= 1) {
            if (pread(s->fd, buf + align, MAX_BLOCKSIZE, 0) >= 0) {
                s->buf_align = align;
                break;
            }
        }
        qemu_vfree(buf);
    }

    if (!bs->request_alignment) {
        size_t align;
        buf = qemu_memalign(s->buf_align, MAX_BLOCKSIZE);
        for (align = 512; align <= MAX_BLOCKSIZE; align <<= 1) {
            if (pread(s->fd, buf, align, 0) >= 0) {
                bs->request_alignment = align;
                break;
            }
        }
        qemu_vfree(buf);
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
#endif

    s->has_discard = true;
    s->has_write_zeroes = true;

    if (fstat(s->fd, &st) < 0) {
        error_setg_errno(errp, errno, "Could not stat file");
        goto fail;
    }
    if (S_ISREG(st.st_mode)) {
        s->discard_zeroes = true;
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

    assert(state != NULL);
    assert(state->bs != NULL);

    s = state->bs->opaque;

    state->opaque = g_malloc0(sizeof(BDRVRawReopenState));
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

    if (s->type == FTYPE_FD || s->type == FTYPE_CD) {
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
        assert(!(raw_s->open_flags & O_CREAT));
        raw_s->fd = qemu_open(state->bs->filename, raw_s->open_flags);
        if (raw_s->fd == -1) {
            error_setg_errno(errp, errno, "Could not reopen file");
            ret = -1;
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

static int raw_refresh_limits(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    raw_probe_alignment(bs);
    bs->bl.opt_mem_alignment = s->buf_align;

    return 0;
}

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
    buf = qemu_blockalign(aiocb->bs, aiocb->aio_nbytes);
    if (aiocb->aio_type & QEMU_AIO_WRITE) {
        char *p = buf;
        int i;

        for (i = 0; i < aiocb->aio_niov; ++i) {
            memcpy(p, aiocb->aio_iov[i].iov_base, aiocb->aio_iov[i].iov_len);
            p += aiocb->aio_iov[i].iov_len;
        }
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
            p     += copy;
            count -= copy;
        }
    }
    qemu_vfree(buf);

    return nbytes;
}

#ifdef CONFIG_XFS
static int xfs_write_zeroes(BDRVRawState *s, int64_t offset, uint64_t bytes)
{
    struct xfs_flock64 fl;

    memset(&fl, 0, sizeof(fl));
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = bytes;

    if (xfsctl(NULL, s->fd, XFS_IOC_ZERO_RANGE, &fl) < 0) {
        DEBUG_BLOCK_PRINT("cannot write zero range (%s)\n", strerror(errno));
        return -errno;
    }

    return 0;
}

static int xfs_discard(BDRVRawState *s, int64_t offset, uint64_t bytes)
{
    struct xfs_flock64 fl;

    memset(&fl, 0, sizeof(fl));
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = bytes;

    if (xfsctl(NULL, s->fd, XFS_IOC_UNRESVSP64, &fl) < 0) {
        DEBUG_BLOCK_PRINT("cannot punch hole (%s)\n", strerror(errno));
        return -errno;
    }

    return 0;
}
#endif

static ssize_t handle_aiocb_write_zeroes(RawPosixAIOData *aiocb)
{
    int ret = -EOPNOTSUPP;
    BDRVRawState *s = aiocb->bs->opaque;

    if (s->has_write_zeroes == 0) {
        return -ENOTSUP;
    }

    if (aiocb->aio_type & QEMU_AIO_BLKDEV) {
#ifdef BLKZEROOUT
        do {
            uint64_t range[2] = { aiocb->aio_offset, aiocb->aio_nbytes };
            if (ioctl(aiocb->aio_fildes, BLKZEROOUT, range) == 0) {
                return 0;
            }
        } while (errno == EINTR);

        ret = -errno;
#endif
    } else {
#ifdef CONFIG_XFS
        if (s->is_xfs) {
            return xfs_write_zeroes(s, aiocb->aio_offset, aiocb->aio_nbytes);
        }
#endif
    }

    if (ret == -ENODEV || ret == -ENOSYS || ret == -EOPNOTSUPP ||
        ret == -ENOTTY) {
        s->has_write_zeroes = false;
        ret = -ENOTSUP;
    }
    return ret;
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
        do {
            if (fallocate(s->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                          aiocb->aio_offset, aiocb->aio_nbytes) == 0) {
                return 0;
            }
        } while (errno == EINTR);

        ret = -errno;
#endif
    }

    if (ret == -ENODEV || ret == -ENOSYS || ret == -EOPNOTSUPP ||
        ret == -ENOTTY) {
        s->has_discard = false;
        ret = -ENOTSUP;
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
        if (ret >= 0 && ret < aiocb->aio_nbytes && aiocb->bs->growable) {
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

    g_slice_free(RawPosixAIOData, aiocb);
    return ret;
}

static int paio_submit_co(BlockDriverState *bs, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        int type)
{
    RawPosixAIOData *acb = g_slice_new(RawPosixAIOData);
    ThreadPool *pool;

    acb->bs = bs;
    acb->aio_type = type;
    acb->aio_fildes = fd;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
    }
    acb->aio_nbytes = nb_sectors * 512;
    acb->aio_offset = sector_num * 512;

    trace_paio_submit_co(sector_num, nb_sectors, type);
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_co(pool, aio_worker, acb);
}

static BlockDriverAIOCB *paio_submit(BlockDriverState *bs, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int type)
{
    RawPosixAIOData *acb = g_slice_new(RawPosixAIOData);
    ThreadPool *pool;

    acb->bs = bs;
    acb->aio_type = type;
    acb->aio_fildes = fd;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
    }
    acb->aio_nbytes = nb_sectors * 512;
    acb->aio_offset = sector_num * 512;

    trace_paio_submit(acb, opaque, sector_num, nb_sectors, type);
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_aio(pool, aio_worker, acb, cb, opaque);
}

static BlockDriverAIOCB *raw_aio_submit(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int type)
{
    BDRVRawState *s = bs->opaque;

    if (fd_open(bs) < 0)
        return NULL;

    /*
     * If O_DIRECT is used the buffer needs to be aligned on a sector
     * boundary.  Check if this is the case or tell the low-level
     * driver that it needs to copy the buffer.
     */
    if ((bs->open_flags & BDRV_O_NOCACHE)) {
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

static BlockDriverAIOCB *raw_aio_readv(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return raw_aio_submit(bs, sector_num, qiov, nb_sectors,
                          cb, opaque, QEMU_AIO_READ);
}

static BlockDriverAIOCB *raw_aio_writev(BlockDriverState *bs,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return raw_aio_submit(bs, sector_num, qiov, nb_sectors,
                          cb, opaque, QEMU_AIO_WRITE);
}

static BlockDriverAIOCB *raw_aio_flush(BlockDriverState *bs,
        BlockDriverCompletionFunc *cb, void *opaque)
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
        size = LLONG_MAX;
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

    strstart(filename, "file:", &filename);

    /* Read out options */
    total_size =
        qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0) / BDRV_SECTOR_SIZE;
    nocow = qemu_opt_get_bool(opts, BLOCK_OPT_NOCOW, false);

    fd = qemu_open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
                   0644);
    if (fd < 0) {
        result = -errno;
        error_setg_errno(errp, -result, "Could not create file");
    } else {
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

        if (ftruncate(fd, total_size * BDRV_SECTOR_SIZE) != 0) {
            result = -errno;
            error_setg_errno(errp, -result, "Could not resize file");
        }
        if (qemu_close(fd) != 0) {
            result = -errno;
            error_setg_errno(errp, -result, "Could not close the new file");
        }
    }
    return result;
}

static int64_t try_fiemap(BlockDriverState *bs, off_t start, off_t *data,
                          off_t *hole, int nb_sectors, int *pnum)
{
#ifdef CONFIG_FIEMAP
    BDRVRawState *s = bs->opaque;
    int64_t ret = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID | start;
    struct {
        struct fiemap fm;
        struct fiemap_extent fe;
    } f;

    if (s->skip_fiemap) {
        return -ENOTSUP;
    }

    f.fm.fm_start = start;
    f.fm.fm_length = (int64_t)nb_sectors * BDRV_SECTOR_SIZE;
    f.fm.fm_flags = 0;
    f.fm.fm_extent_count = 1;
    f.fm.fm_reserved = 0;
    if (ioctl(s->fd, FS_IOC_FIEMAP, &f) == -1) {
        s->skip_fiemap = true;
        return -errno;
    }

    if (f.fm.fm_mapped_extents == 0) {
        /* No extents found, data is beyond f.fm.fm_start + f.fm.fm_length.
         * f.fm.fm_start + f.fm.fm_length must be clamped to the file size!
         */
        off_t length = lseek(s->fd, 0, SEEK_END);
        *hole = f.fm.fm_start;
        *data = MIN(f.fm.fm_start + f.fm.fm_length, length);
    } else {
        *data = f.fe.fe_logical;
        *hole = f.fe.fe_logical + f.fe.fe_length;
        if (f.fe.fe_flags & FIEMAP_EXTENT_UNWRITTEN) {
            ret |= BDRV_BLOCK_ZERO;
        }
    }

    return ret;
#else
    return -ENOTSUP;
#endif
}

static int64_t try_seek_hole(BlockDriverState *bs, off_t start, off_t *data,
                             off_t *hole, int *pnum)
{
#if defined SEEK_HOLE && defined SEEK_DATA
    BDRVRawState *s = bs->opaque;

    *hole = lseek(s->fd, start, SEEK_HOLE);
    if (*hole == -1) {
        /* -ENXIO indicates that sector_num was past the end of the file.
         * There is a virtual hole there.  */
        assert(errno != -ENXIO);

        return -errno;
    }

    if (*hole > start) {
        *data = start;
    } else {
        /* On a hole.  We need another syscall to find its end.  */
        *data = lseek(s->fd, start, SEEK_DATA);
        if (*data == -1) {
            *data = lseek(s->fd, 0, SEEK_END);
        }
    }

    return BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID | start;
#else
    return -ENOTSUP;
#endif
}

/*
 * Returns true iff the specified sector is present in the disk image. Drivers
 * not implementing the functionality are assumed to not support backing files,
 * hence all their sectors are reported as allocated.
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
                                                    int nb_sectors, int *pnum)
{
    off_t start, data = 0, hole = 0;
    int64_t ret;

    ret = fd_open(bs);
    if (ret < 0) {
        return ret;
    }

    start = sector_num * BDRV_SECTOR_SIZE;

    ret = try_fiemap(bs, start, &data, &hole, nb_sectors, pnum);
    if (ret < 0) {
        ret = try_seek_hole(bs, start, &data, &hole, pnum);
        if (ret < 0) {
            /* Assume everything is allocated. */
            data = 0;
            hole = start + nb_sectors * BDRV_SECTOR_SIZE;
            ret = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID | start;
        }
    }

    if (data <= start) {
        /* On a data extent, compute sectors to the end of the extent.  */
        *pnum = MIN(nb_sectors, (hole - start) / BDRV_SECTOR_SIZE);
    } else {
        /* On a hole, compute sectors to the beginning of the next extent.  */
        *pnum = MIN(nb_sectors, (data - start) / BDRV_SECTOR_SIZE);
        ret &= ~BDRV_BLOCK_DATA;
        ret |= BDRV_BLOCK_ZERO;
    }

    return ret;
}

static coroutine_fn BlockDriverAIOCB *raw_aio_discard(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
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
        { /* end of list */ }
    }
};

static BlockDriver bdrv_file = {
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
static kern_return_t GetBSDPath( io_iterator_t mediaIterator, char *bsdPath, CFIndex maxPathSize );

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

kern_return_t GetBSDPath( io_iterator_t mediaIterator, char *bsdPath, CFIndex maxPathSize )
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
            strcat( bsdPath, "r" );
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

static int hdev_open(BlockDriverState *bs, QDict *options, int flags,
                     Error **errp)
{
    BDRVRawState *s = bs->opaque;
    Error *local_err = NULL;
    int ret;
    const char *filename = qdict_get_str(options, "filename");

#if defined(__APPLE__) && defined(__MACH__)
    if (strstart(filename, "/dev/cdrom", NULL)) {
        kern_return_t kernResult;
        io_iterator_t mediaIterator;
        char bsdPath[ MAXPATHLEN ];
        int fd;

        kernResult = FindEjectableCDMedia( &mediaIterator );
        kernResult = GetBSDPath( mediaIterator, bsdPath, sizeof( bsdPath ) );

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
#if defined(__linux__)
    {
        char resolved_path[ MAXPATHLEN ], *temp;

        temp = realpath(filename, resolved_path);
        if (temp && strstart(temp, "/dev/sg", NULL)) {
            bs->sg = 1;
        }
    }
#endif

    ret = raw_open_common(bs, options, flags, 0, &local_err);
    if (ret < 0) {
        if (local_err) {
            error_propagate(errp, local_err);
        }
        return ret;
    }

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
/* Note: we do not have a reliable method to detect if the floppy is
   present. The current method is to try to open the floppy at every
   I/O and to keep it opened during a few hundreds of ms. */
static int fd_open(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int last_media_present;

    if (s->type != FTYPE_FD)
        return 0;
    last_media_present = (s->fd >= 0);
    if (s->fd >= 0 &&
        (get_clock() - s->fd_open_time) >= FD_OPEN_TIMEOUT) {
        qemu_close(s->fd);
        s->fd = -1;
#ifdef DEBUG_FLOPPY
        printf("Floppy closed\n");
#endif
    }
    if (s->fd < 0) {
        if (s->fd_got_error &&
            (get_clock() - s->fd_error_time) < FD_OPEN_TIMEOUT) {
#ifdef DEBUG_FLOPPY
            printf("No floppy (open delayed)\n");
#endif
            return -EIO;
        }
        s->fd = qemu_open(bs->filename, s->open_flags & ~O_NONBLOCK);
        if (s->fd < 0) {
            s->fd_error_time = get_clock();
            s->fd_got_error = 1;
            if (last_media_present)
                s->fd_media_changed = 1;
#ifdef DEBUG_FLOPPY
            printf("No floppy\n");
#endif
            return -EIO;
        }
#ifdef DEBUG_FLOPPY
        printf("Floppy opened\n");
#endif
    }
    if (!last_media_present)
        s->fd_media_changed = 1;
    s->fd_open_time = get_clock();
    s->fd_got_error = 0;
    return 0;
}

static int hdev_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    BDRVRawState *s = bs->opaque;

    return ioctl(s->fd, req, buf);
}

static BlockDriverAIOCB *hdev_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    RawPosixAIOData *acb;
    ThreadPool *pool;

    if (fd_open(bs) < 0)
        return NULL;

    acb = g_slice_new(RawPosixAIOData);
    acb->bs = bs;
    acb->aio_type = QEMU_AIO_IOCTL;
    acb->aio_fildes = s->fd;
    acb->aio_offset = 0;
    acb->aio_ioctl_buf = buf;
    acb->aio_ioctl_cmd = req;
    pool = aio_get_thread_pool(bdrv_get_aio_context(bs));
    return thread_pool_submit_aio(pool, aio_worker, acb, cb, opaque);
}

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
static int fd_open(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    /* this is just to ensure s->fd is sane (its called by io ops) */
    if (s->fd >= 0)
        return 0;
    return -EIO;
}
#else /* !linux && !FreeBSD */

static int fd_open(BlockDriverState *bs)
{
    return 0;
}

#endif /* !linux && !FreeBSD */

static coroutine_fn BlockDriverAIOCB *hdev_aio_discard(BlockDriverState *bs,
    int64_t sector_num, int nb_sectors,
    BlockDriverCompletionFunc *cb, void *opaque)
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

    /* This function is used by all three protocol block drivers and therefore
     * any of these three prefixes may be given.
     * The return value has to be stored somewhere, otherwise this is an error
     * due to -Werror=unused-value. */
    has_prefix =
        strstart(filename, "host_device:", &filename) ||
        strstart(filename, "host_cdrom:" , &filename) ||
        strstart(filename, "host_floppy:", &filename);

    (void)has_prefix;

    /* Read out options */
    total_size =
        qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0) / BDRV_SECTOR_SIZE;

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
    } else if (lseek(fd, 0, SEEK_END) < total_size * BDRV_SECTOR_SIZE) {
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

    .bdrv_detach_aio_context = raw_detach_aio_context,
    .bdrv_attach_aio_context = raw_attach_aio_context,

    /* generic scsi device */
#ifdef __linux__
    .bdrv_ioctl         = hdev_ioctl,
    .bdrv_aio_ioctl     = hdev_aio_ioctl,
#endif
};

#ifdef __linux__
static void floppy_parse_filename(const char *filename, QDict *options,
                                  Error **errp)
{
    /* The prefix is optional, just as for "file". */
    strstart(filename, "host_floppy:", &filename);

    qdict_put_obj(options, "filename", QOBJECT(qstring_from_str(filename)));
}

static int floppy_open(BlockDriverState *bs, QDict *options, int flags,
                       Error **errp)
{
    BDRVRawState *s = bs->opaque;
    Error *local_err = NULL;
    int ret;

    s->type = FTYPE_FD;

    /* open will not fail even if no floppy is inserted, so add O_NONBLOCK */
    ret = raw_open_common(bs, options, flags, O_NONBLOCK, &local_err);
    if (ret) {
        if (local_err) {
            error_propagate(errp, local_err);
        }
        return ret;
    }

    /* close fd so that we can reopen it as needed */
    qemu_close(s->fd);
    s->fd = -1;
    s->fd_media_changed = 1;

    return 0;
}

static int floppy_probe_device(const char *filename)
{
    int fd, ret;
    int prio = 0;
    struct floppy_struct fdparam;
    struct stat st;

    if (strstart(filename, "/dev/fd", NULL) &&
        !strstart(filename, "/dev/fdset/", NULL)) {
        prio = 50;
    }

    fd = qemu_open(filename, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        goto out;
    }
    ret = fstat(fd, &st);
    if (ret == -1 || !S_ISBLK(st.st_mode)) {
        goto outc;
    }

    /* Attempt to detect via a floppy specific ioctl */
    ret = ioctl(fd, FDGETPRM, &fdparam);
    if (ret >= 0)
        prio = 100;

outc:
    qemu_close(fd);
out:
    return prio;
}


static int floppy_is_inserted(BlockDriverState *bs)
{
    return fd_open(bs) >= 0;
}

static int floppy_media_changed(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    /*
     * XXX: we do not have a true media changed indication.
     * It does not work if the floppy is changed without trying to read it.
     */
    fd_open(bs);
    ret = s->fd_media_changed;
    s->fd_media_changed = 0;
#ifdef DEBUG_FLOPPY
    printf("Floppy changed=%d\n", ret);
#endif
    return ret;
}

static void floppy_eject(BlockDriverState *bs, bool eject_flag)
{
    BDRVRawState *s = bs->opaque;
    int fd;

    if (s->fd >= 0) {
        qemu_close(s->fd);
        s->fd = -1;
    }
    fd = qemu_open(bs->filename, s->open_flags | O_NONBLOCK);
    if (fd >= 0) {
        if (ioctl(fd, FDEJECT, 0) < 0)
            perror("FDEJECT");
        qemu_close(fd);
    }
}

static BlockDriver bdrv_host_floppy = {
    .format_name        = "host_floppy",
    .protocol_name      = "host_floppy",
    .instance_size      = sizeof(BDRVRawState),
    .bdrv_needs_filename = true,
    .bdrv_probe_device	= floppy_probe_device,
    .bdrv_parse_filename = floppy_parse_filename,
    .bdrv_file_open     = floppy_open,
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
    .bdrv_is_inserted   = floppy_is_inserted,
    .bdrv_media_changed = floppy_media_changed,
    .bdrv_eject         = floppy_eject,
};
#endif

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

static int cdrom_is_inserted(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    ret = ioctl(s->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
    if (ret == CDS_DISC_OK)
        return 1;
    return 0;
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
    .bdrv_ioctl         = hdev_ioctl,
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

static int cdrom_is_inserted(BlockDriverState *bs)
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
    bdrv_register(&bdrv_host_floppy);
    bdrv_register(&bdrv_host_cdrom);
#endif
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
    bdrv_register(&bdrv_host_cdrom);
#endif
}

block_init(bdrv_file_init);
