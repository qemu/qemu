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
#include "qemu-timer.h"
#include "qemu-char.h"
#include "block_int.h"
#include <assert.h>
#ifdef CONFIG_AIO
#include <aio.h>
#endif

#ifdef CONFIG_COCOA
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
#include <signal.h>
#include <sys/dkio.h>
#endif
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#endif
#ifdef __FreeBSD__
#include <signal.h>
#include <sys/disk.h>
#endif

#ifdef __OpenBSD__
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#endif

//#define DEBUG_FLOPPY

//#define DEBUG_BLOCK
#if defined(DEBUG_BLOCK)
#define DEBUG_BLOCK_PRINT(formatCstr, args...) do { if (loglevel != 0)	\
    { fprintf(logfile, formatCstr, ##args); fflush(logfile); } } while (0)
#else
#define DEBUG_BLOCK_PRINT(formatCstr, args...)
#endif

/* OS X does not have O_DSYNC */
#ifndef O_DSYNC
#define O_DSYNC O_SYNC
#endif

/* Approximate O_DIRECT with O_DSYNC if O_DIRECT isn't available */
#ifndef O_DIRECT
#define O_DIRECT O_DSYNC
#endif

#define FTYPE_FILE   0
#define FTYPE_CD     1
#define FTYPE_FD     2

#define ALIGNED_BUFFER_SIZE (32 * 512)

/* if the FD is not accessed during that time (in ms), we try to
   reopen it to see if the disk has been changed */
#define FD_OPEN_TIMEOUT 1000

/* posix-aio doesn't allow multiple outstanding requests to a single file
 * descriptor.  we implement a pool of dup()'d file descriptors to work
 * around this */
#define RAW_FD_POOL_SIZE	64

typedef struct BDRVRawState {
    int fd;
    int type;
    unsigned int lseek_err_cnt;
    int fd_pool[RAW_FD_POOL_SIZE];
#if defined(__linux__)
    /* linux floppy specific */
    int fd_open_flags;
    int64_t fd_open_time;
    int64_t fd_error_time;
    int fd_got_error;
    int fd_media_changed;
#endif
    uint8_t* aligned_buf;
} BDRVRawState;

static int posix_aio_init(void);

static int fd_open(BlockDriverState *bs);

static int raw_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVRawState *s = bs->opaque;
    int fd, open_flags, ret;
    int i;

    posix_aio_init();

    s->lseek_err_cnt = 0;

    open_flags = O_BINARY;
    if ((flags & BDRV_O_ACCESS) == O_RDWR) {
        open_flags |= O_RDWR;
    } else {
        open_flags |= O_RDONLY;
        bs->read_only = 1;
    }
    if (flags & BDRV_O_CREAT)
        open_flags |= O_CREAT | O_TRUNC;

    /* Use O_DSYNC for write-through caching, no flags for write-back caching,
     * and O_DIRECT for no caching. */
    if ((flags & BDRV_O_NOCACHE))
        open_flags |= O_DIRECT;
    else if (!(flags & BDRV_O_CACHE_WB))
        open_flags |= O_DSYNC;

    s->type = FTYPE_FILE;

    fd = open(filename, open_flags, 0644);
    if (fd < 0) {
        ret = -errno;
        if (ret == -EROFS)
            ret = -EACCES;
        return ret;
    }
    s->fd = fd;
    for (i = 0; i < RAW_FD_POOL_SIZE; i++)
        s->fd_pool[i] = -1;
    s->aligned_buf = NULL;
    if ((flags & BDRV_O_NOCACHE)) {
        s->aligned_buf = qemu_memalign(512, ALIGNED_BUFFER_SIZE);
        if (s->aligned_buf == NULL) {
            ret = -errno;
            close(fd);
            return ret;
        }
    }
    return 0;
}

/* XXX: use host sector size if necessary with:
#ifdef DIOCGSECTORSIZE
        {
            unsigned int sectorsize = 512;
            if (!ioctl(fd, DIOCGSECTORSIZE, &sectorsize) &&
                sectorsize > bufsize)
                bufsize = sectorsize;
        }
#endif
#ifdef CONFIG_COCOA
        u_int32_t   blockSize = 512;
        if ( !ioctl( fd, DKIOCGETBLOCKSIZE, &blockSize ) && blockSize > bufsize) {
            bufsize = blockSize;
        }
#endif
*/

/*
 * offset and count are in bytes, but must be multiples of 512 for files
 * opened with O_DIRECT. buf must be aligned to 512 bytes then.
 *
 * This function may be called without alignment if the caller ensures
 * that O_DIRECT is not in effect.
 */
static int raw_pread_aligned(BlockDriverState *bs, int64_t offset,
                     uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    ret = fd_open(bs);
    if (ret < 0)
        return ret;

    if (offset >= 0 && lseek(s->fd, offset, SEEK_SET) == (off_t)-1) {
        ++(s->lseek_err_cnt);
        if(s->lseek_err_cnt <= 10) {
            DEBUG_BLOCK_PRINT("raw_pread(%d:%s, %" PRId64 ", %p, %d) [%" PRId64
                              "] lseek failed : %d = %s\n",
                              s->fd, bs->filename, offset, buf, count,
                              bs->total_sectors, errno, strerror(errno));
        }
        return -1;
    }
    s->lseek_err_cnt=0;

    ret = read(s->fd, buf, count);
    if (ret == count)
        goto label__raw_read__success;

    DEBUG_BLOCK_PRINT("raw_pread(%d:%s, %" PRId64 ", %p, %d) [%" PRId64
                      "] read failed %d : %d = %s\n",
                      s->fd, bs->filename, offset, buf, count,
                      bs->total_sectors, ret, errno, strerror(errno));

    /* Try harder for CDrom. */
    if (bs->type == BDRV_TYPE_CDROM) {
        lseek(s->fd, offset, SEEK_SET);
        ret = read(s->fd, buf, count);
        if (ret == count)
            goto label__raw_read__success;
        lseek(s->fd, offset, SEEK_SET);
        ret = read(s->fd, buf, count);
        if (ret == count)
            goto label__raw_read__success;

        DEBUG_BLOCK_PRINT("raw_pread(%d:%s, %" PRId64 ", %p, %d) [%" PRId64
                          "] retry read failed %d : %d = %s\n",
                          s->fd, bs->filename, offset, buf, count,
                          bs->total_sectors, ret, errno, strerror(errno));
    }

label__raw_read__success:

    return ret;
}

/*
 * offset and count are in bytes, but must be multiples of 512 for files
 * opened with O_DIRECT. buf must be aligned to 512 bytes then.
 *
 * This function may be called without alignment if the caller ensures
 * that O_DIRECT is not in effect.
 */
static int raw_pwrite_aligned(BlockDriverState *bs, int64_t offset,
                      const uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    ret = fd_open(bs);
    if (ret < 0)
        return ret;

    if (offset >= 0 && lseek(s->fd, offset, SEEK_SET) == (off_t)-1) {
        ++(s->lseek_err_cnt);
        if(s->lseek_err_cnt) {
            DEBUG_BLOCK_PRINT("raw_pwrite(%d:%s, %" PRId64 ", %p, %d) [%"
                              PRId64 "] lseek failed : %d = %s\n",
                              s->fd, bs->filename, offset, buf, count,
                              bs->total_sectors, errno, strerror(errno));
        }
        return -1;
    }
    s->lseek_err_cnt = 0;

    ret = write(s->fd, buf, count);
    if (ret == count)
        goto label__raw_write__success;

    DEBUG_BLOCK_PRINT("raw_pwrite(%d:%s, %" PRId64 ", %p, %d) [%" PRId64
                      "] write failed %d : %d = %s\n",
                      s->fd, bs->filename, offset, buf, count,
                      bs->total_sectors, ret, errno, strerror(errno));

label__raw_write__success:

    return ret;
}


/*
 * offset and count are in bytes and possibly not aligned. For files opened
 * with O_DIRECT, necessary alignments are ensured before calling
 * raw_pread_aligned to do the actual read.
 */
static int raw_pread(BlockDriverState *bs, int64_t offset,
                     uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    int size, ret, shift, sum;

    sum = 0;

    if (s->aligned_buf != NULL)  {

        if (offset & 0x1ff) {
            /* align offset on a 512 bytes boundary */

            shift = offset & 0x1ff;
            size = (shift + count + 0x1ff) & ~0x1ff;
            if (size > ALIGNED_BUFFER_SIZE)
                size = ALIGNED_BUFFER_SIZE;
            ret = raw_pread_aligned(bs, offset - shift, s->aligned_buf, size);
            if (ret < 0)
                return ret;

            size = 512 - shift;
            if (size > count)
                size = count;
            memcpy(buf, s->aligned_buf + shift, size);

            buf += size;
            offset += size;
            count -= size;
            sum += size;

            if (count == 0)
                return sum;
        }
        if (count & 0x1ff || (uintptr_t) buf & 0x1ff) {

            /* read on aligned buffer */

            while (count) {

                size = (count + 0x1ff) & ~0x1ff;
                if (size > ALIGNED_BUFFER_SIZE)
                    size = ALIGNED_BUFFER_SIZE;

                ret = raw_pread_aligned(bs, offset, s->aligned_buf, size);
                if (ret < 0)
                    return ret;

                size = ret;
                if (size > count)
                    size = count;

                memcpy(buf, s->aligned_buf, size);

                buf += size;
                offset += size;
                count -= size;
                sum += size;
            }

            return sum;
        }
    }

    return raw_pread_aligned(bs, offset, buf, count) + sum;
}

/*
 * offset and count are in bytes and possibly not aligned. For files opened
 * with O_DIRECT, necessary alignments are ensured before calling
 * raw_pwrite_aligned to do the actual write.
 */
static int raw_pwrite(BlockDriverState *bs, int64_t offset,
                      const uint8_t *buf, int count)
{
    BDRVRawState *s = bs->opaque;
    int size, ret, shift, sum;

    sum = 0;

    if (s->aligned_buf != NULL) {

        if (offset & 0x1ff) {
            /* align offset on a 512 bytes boundary */
            shift = offset & 0x1ff;
            ret = raw_pread_aligned(bs, offset - shift, s->aligned_buf, 512);
            if (ret < 0)
                return ret;

            size = 512 - shift;
            if (size > count)
                size = count;
            memcpy(s->aligned_buf + shift, buf, size);

            ret = raw_pwrite_aligned(bs, offset - shift, s->aligned_buf, 512);
            if (ret < 0)
                return ret;

            buf += size;
            offset += size;
            count -= size;
            sum += size;

            if (count == 0)
                return sum;
        }
        if (count & 0x1ff || (uintptr_t) buf & 0x1ff) {

            while ((size = (count & ~0x1ff)) != 0) {

                if (size > ALIGNED_BUFFER_SIZE)
                    size = ALIGNED_BUFFER_SIZE;

                memcpy(s->aligned_buf, buf, size);

                ret = raw_pwrite_aligned(bs, offset, s->aligned_buf, size);
                if (ret < 0)
                    return ret;

                buf += ret;
                offset += ret;
                count -= ret;
                sum += ret;
            }
            /* here, count < 512 because (count & ~0x1ff) == 0 */
            if (count) {
                ret = raw_pread_aligned(bs, offset, s->aligned_buf, 512);
                if (ret < 0)
                    return ret;
                 memcpy(s->aligned_buf, buf, count);

                 ret = raw_pwrite_aligned(bs, offset, s->aligned_buf, 512);
                 if (ret < 0)
                     return ret;
                 if (count < ret)
                     ret = count;

                 sum += ret;
            }
            return sum;
        }
    }
    return raw_pwrite_aligned(bs, offset, buf, count) + sum;
}

#ifdef CONFIG_AIO
/***********************************************************/
/* Unix AIO using POSIX AIO */

typedef struct RawAIOCB {
    BlockDriverAIOCB common;
    int fd;
    struct aiocb aiocb;
    struct RawAIOCB *next;
    int ret;
} RawAIOCB;

typedef struct PosixAioState
{
    int rfd, wfd;
    RawAIOCB *first_aio;
} PosixAioState;

static int raw_fd_pool_get(BDRVRawState *s)
{
    int i;

    for (i = 0; i < RAW_FD_POOL_SIZE; i++) {
        /* already in use */
        if (s->fd_pool[i] != -1)
            continue;

        /* try to dup file descriptor */
        s->fd_pool[i] = dup(s->fd);
        if (s->fd_pool[i] != -1)
            return s->fd_pool[i];
    }

    /* we couldn't dup the file descriptor so just use the main one */
    return s->fd;
}

static void raw_fd_pool_put(RawAIOCB *acb)
{
    BDRVRawState *s = acb->common.bs->opaque;
    int i;

    for (i = 0; i < RAW_FD_POOL_SIZE; i++) {
        if (s->fd_pool[i] == acb->fd) {
            close(s->fd_pool[i]);
            s->fd_pool[i] = -1;
        }
    }
}

static void posix_aio_read(void *opaque)
{
    PosixAioState *s = opaque;
    RawAIOCB *acb, **pacb;
    int ret;
    ssize_t len;

    /* read all bytes from signal pipe */
    for (;;) {
        char bytes[16];

        len = read(s->rfd, bytes, sizeof(bytes));
        if (len == -1 && errno == EINTR)
            continue; /* try again */
        if (len == sizeof(bytes))
            continue; /* more to read */
        break;
    }

    for(;;) {
        pacb = &s->first_aio;
        for(;;) {
            acb = *pacb;
            if (!acb)
                goto the_end;
            ret = aio_error(&acb->aiocb);
            if (ret == ECANCELED) {
                /* remove the request */
                *pacb = acb->next;
                raw_fd_pool_put(acb);
                qemu_aio_release(acb);
            } else if (ret != EINPROGRESS) {
                /* end of aio */
                if (ret == 0) {
                    ret = aio_return(&acb->aiocb);
                    if (ret == acb->aiocb.aio_nbytes)
                        ret = 0;
                    else
                        ret = -EINVAL;
                } else {
                    ret = -ret;
                }
                /* remove the request */
                *pacb = acb->next;
                /* call the callback */
                acb->common.cb(acb->common.opaque, ret);
                raw_fd_pool_put(acb);
                qemu_aio_release(acb);
                break;
            } else {
                pacb = &acb->next;
            }
        }
    }
 the_end: ;
}

static int posix_aio_flush(void *opaque)
{
    PosixAioState *s = opaque;
    return !!s->first_aio;
}

static PosixAioState *posix_aio_state;

static void aio_signal_handler(int signum)
{
    if (posix_aio_state) {
        char byte = 0;

        write(posix_aio_state->wfd, &byte, sizeof(byte));
    }

    qemu_service_io();
}

static int posix_aio_init(void)
{
    struct sigaction act;
    PosixAioState *s;
    int fds[2];
  
    if (posix_aio_state)
        return 0;

    s = qemu_malloc(sizeof(PosixAioState));
    if (s == NULL)
        return -ENOMEM;

    sigfillset(&act.sa_mask);
    act.sa_flags = 0; /* do not restart syscalls to interrupt select() */
    act.sa_handler = aio_signal_handler;
    sigaction(SIGUSR2, &act, NULL);

    s->first_aio = NULL;
    if (pipe(fds) == -1) {
        fprintf(stderr, "failed to create pipe\n");
        return -errno;
    }

    s->rfd = fds[0];
    s->wfd = fds[1];

    fcntl(s->rfd, F_SETFL, O_NONBLOCK);
    fcntl(s->wfd, F_SETFL, O_NONBLOCK);

    qemu_aio_set_fd_handler(s->rfd, posix_aio_read, NULL, posix_aio_flush, s);

#if defined(__linux__)
    {
        struct aioinit ai;

        memset(&ai, 0, sizeof(ai));
#if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 4)
        ai.aio_threads = 64;
        ai.aio_num = 64;
#else
        /* XXX: aio thread exit seems to hang on RedHat 9 and this init
           seems to fix the problem. */
        ai.aio_threads = 1;
        ai.aio_num = 1;
        ai.aio_idle_time = 365 * 100000;
#endif
        aio_init(&ai);
    }
#endif
    posix_aio_state = s;

    return 0;
}

static RawAIOCB *raw_aio_setup(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    BDRVRawState *s = bs->opaque;
    RawAIOCB *acb;

    if (fd_open(bs) < 0)
        return NULL;

    acb = qemu_aio_get(bs, cb, opaque);
    if (!acb)
        return NULL;
    acb->fd = raw_fd_pool_get(s);
    acb->aiocb.aio_fildes = acb->fd;
    acb->aiocb.aio_sigevent.sigev_signo = SIGUSR2;
    acb->aiocb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    acb->aiocb.aio_buf = buf;
    if (nb_sectors < 0)
        acb->aiocb.aio_nbytes = -nb_sectors;
    else
        acb->aiocb.aio_nbytes = nb_sectors * 512;
    acb->aiocb.aio_offset = sector_num * 512;
    acb->next = posix_aio_state->first_aio;
    posix_aio_state->first_aio = acb;
    return acb;
}

static void raw_aio_em_cb(void* opaque)
{
    RawAIOCB *acb = opaque;
    acb->common.cb(acb->common.opaque, acb->ret);
    qemu_aio_release(acb);
}

static BlockDriverAIOCB *raw_aio_read(BlockDriverState *bs,
        int64_t sector_num, uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    RawAIOCB *acb;

    /*
     * If O_DIRECT is used and the buffer is not aligned fall back
     * to synchronous IO.
     */
    BDRVRawState *s = bs->opaque;

    if (unlikely(s->aligned_buf != NULL && ((uintptr_t) buf % 512))) {
        QEMUBH *bh;
        acb = qemu_aio_get(bs, cb, opaque);
        acb->ret = raw_pread(bs, 512 * sector_num, buf, 512 * nb_sectors);
        bh = qemu_bh_new(raw_aio_em_cb, acb);
        qemu_bh_schedule(bh);
        return &acb->common;
    }

    acb = raw_aio_setup(bs, sector_num, buf, nb_sectors, cb, opaque);
    if (!acb)
        return NULL;
    if (aio_read(&acb->aiocb) < 0) {
        qemu_aio_release(acb);
        return NULL;
    }
    return &acb->common;
}

static BlockDriverAIOCB *raw_aio_write(BlockDriverState *bs,
        int64_t sector_num, const uint8_t *buf, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    RawAIOCB *acb;

    /*
     * If O_DIRECT is used and the buffer is not aligned fall back
     * to synchronous IO.
     */
    BDRVRawState *s = bs->opaque;

    if (unlikely(s->aligned_buf != NULL && ((uintptr_t) buf % 512))) {
        QEMUBH *bh;
        acb = qemu_aio_get(bs, cb, opaque);
        acb->ret = raw_pwrite(bs, 512 * sector_num, buf, 512 * nb_sectors);
        bh = qemu_bh_new(raw_aio_em_cb, acb);
        qemu_bh_schedule(bh);
        return &acb->common;
    }

    acb = raw_aio_setup(bs, sector_num, (uint8_t*)buf, nb_sectors, cb, opaque);
    if (!acb)
        return NULL;
    if (aio_write(&acb->aiocb) < 0) {
        qemu_aio_release(acb);
        return NULL;
    }
    return &acb->common;
}

static void raw_aio_cancel(BlockDriverAIOCB *blockacb)
{
    int ret;
    RawAIOCB *acb = (RawAIOCB *)blockacb;
    RawAIOCB **pacb;

    ret = aio_cancel(acb->aiocb.aio_fildes, &acb->aiocb);
    if (ret == AIO_NOTCANCELED) {
        /* fail safe: if the aio could not be canceled, we wait for
           it */
        while (aio_error(&acb->aiocb) == EINPROGRESS);
    }

    /* remove the callback from the queue */
    pacb = &posix_aio_state->first_aio;
    for(;;) {
        if (*pacb == NULL) {
            break;
        } else if (*pacb == acb) {
            *pacb = acb->next;
            raw_fd_pool_put(acb);
            qemu_aio_release(acb);
            break;
        }
        pacb = &acb->next;
    }
}

#else /* CONFIG_AIO */
static int posix_aio_init(void)
{
    return 0;
}
#endif /* CONFIG_AIO */

static void raw_close_fd_pool(BDRVRawState *s)
{
    int i;

    for (i = 0; i < RAW_FD_POOL_SIZE; i++) {
        if (s->fd_pool[i] != -1) {
            close(s->fd_pool[i]);
            s->fd_pool[i] = -1;
        }
    }
}

static void raw_close(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
        if (s->aligned_buf != NULL)
            qemu_free(s->aligned_buf);
    }
    raw_close_fd_pool(s);
}

static int raw_truncate(BlockDriverState *bs, int64_t offset)
{
    BDRVRawState *s = bs->opaque;
    if (s->type != FTYPE_FILE)
        return -ENOTSUP;
    if (ftruncate(s->fd, offset) < 0)
        return -errno;
    return 0;
}

#ifdef __OpenBSD__
static int64_t raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int fd = s->fd;
    struct stat st;

    if (fstat(fd, &st))
        return -1;
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        struct disklabel dl;

        if (ioctl(fd, DIOCGDINFO, &dl))
            return -1;
        return (uint64_t)dl.d_secsize *
            dl.d_partitions[DISKPART(st.st_rdev)].p_size;
    } else
        return st.st_size;
}
#else /* !__OpenBSD__ */
static int64_t  raw_getlength(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int fd = s->fd;
    int64_t size;
#ifdef _BSD
    struct stat sb;
#endif
#ifdef __sun__
    struct dk_minfo minfo;
    int rv;
#endif
    int ret;

    ret = fd_open(bs);
    if (ret < 0)
        return ret;

#ifdef _BSD
    if (!fstat(fd, &sb) && (S_IFCHR & sb.st_mode)) {
#ifdef DIOCGMEDIASIZE
	if (ioctl(fd, DIOCGMEDIASIZE, (off_t *)&size))
#endif
#ifdef CONFIG_COCOA
        size = LONG_LONG_MAX;
#else
        size = lseek(fd, 0LL, SEEK_END);
#endif
    } else
#endif
#ifdef __sun__
    /*
     * use the DKIOCGMEDIAINFO ioctl to read the size.
     */
    rv = ioctl ( fd, DKIOCGMEDIAINFO, &minfo );
    if ( rv != -1 ) {
        size = minfo.dki_lbsize * minfo.dki_capacity;
    } else /* there are reports that lseek on some devices
              fails, but irc discussion said that contingency
              on contingency was overkill */
#endif
    {
        size = lseek(fd, 0, SEEK_END);
    }
    return size;
}
#endif

static int raw_create(const char *filename, int64_t total_size,
                      const char *backing_file, int flags)
{
    int fd;

    if (flags || backing_file)
        return -ENOTSUP;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
              0644);
    if (fd < 0)
        return -EIO;
    ftruncate(fd, total_size * 512);
    close(fd);
    return 0;
}

static void raw_flush(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    fsync(s->fd);
}

BlockDriver bdrv_raw = {
    "raw",
    sizeof(BDRVRawState),
    NULL, /* no probe for protocols */
    raw_open,
    NULL,
    NULL,
    raw_close,
    raw_create,
    raw_flush,

#ifdef CONFIG_AIO
    .bdrv_aio_read = raw_aio_read,
    .bdrv_aio_write = raw_aio_write,
    .bdrv_aio_cancel = raw_aio_cancel,
    .aiocb_size = sizeof(RawAIOCB),
#endif
    .bdrv_pread = raw_pread,
    .bdrv_pwrite = raw_pwrite,
    .bdrv_truncate = raw_truncate,
    .bdrv_getlength = raw_getlength,
};

/***********************************************/
/* host device */

#ifdef CONFIG_COCOA
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

static int hdev_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVRawState *s = bs->opaque;
    int fd, open_flags, ret, i;

    posix_aio_init();

#ifdef CONFIG_COCOA
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
            fd = open(bsdPath, O_RDONLY | O_BINARY | O_LARGEFILE);
            if (fd < 0) {
                bsdPath[strlen(bsdPath)-1] = '1';
            } else {
                close(fd);
            }
            filename = bsdPath;
        }

        if ( mediaIterator )
            IOObjectRelease( mediaIterator );
    }
#endif
    open_flags = O_BINARY;
    if ((flags & BDRV_O_ACCESS) == O_RDWR) {
        open_flags |= O_RDWR;
    } else {
        open_flags |= O_RDONLY;
        bs->read_only = 1;
    }
    /* Use O_DSYNC for write-through caching, no flags for write-back caching,
     * and O_DIRECT for no caching. */
    if ((flags & BDRV_O_NOCACHE))
        open_flags |= O_DIRECT;
    else if (!(flags & BDRV_O_CACHE_WB))
        open_flags |= O_DSYNC;

    s->type = FTYPE_FILE;
#if defined(__linux__)
    if (strstart(filename, "/dev/cd", NULL)) {
        /* open will not fail even if no CD is inserted */
        open_flags |= O_NONBLOCK;
        s->type = FTYPE_CD;
    } else if (strstart(filename, "/dev/fd", NULL)) {
        s->type = FTYPE_FD;
        s->fd_open_flags = open_flags;
        /* open will not fail even if no floppy is inserted */
        open_flags |= O_NONBLOCK;
    } else if (strstart(filename, "/dev/sg", NULL)) {
        bs->sg = 1;
    }
#endif
    fd = open(filename, open_flags, 0644);
    if (fd < 0) {
        ret = -errno;
        if (ret == -EROFS)
            ret = -EACCES;
        return ret;
    }
    s->fd = fd;
    for (i = 0; i < RAW_FD_POOL_SIZE; i++)
        s->fd_pool[i] = -1;
#if defined(__linux__)
    /* close fd so that we can reopen it as needed */
    if (s->type == FTYPE_FD) {
        close(s->fd);
        s->fd = -1;
        s->fd_media_changed = 1;
    }
#endif
    return 0;
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
        (qemu_get_clock(rt_clock) - s->fd_open_time) >= FD_OPEN_TIMEOUT) {
        close(s->fd);
        s->fd = -1;
        raw_close_fd_pool(s);
#ifdef DEBUG_FLOPPY
        printf("Floppy closed\n");
#endif
    }
    if (s->fd < 0) {
        if (s->fd_got_error &&
            (qemu_get_clock(rt_clock) - s->fd_error_time) < FD_OPEN_TIMEOUT) {
#ifdef DEBUG_FLOPPY
            printf("No floppy (open delayed)\n");
#endif
            return -EIO;
        }
        s->fd = open(bs->filename, s->fd_open_flags);
        if (s->fd < 0) {
            s->fd_error_time = qemu_get_clock(rt_clock);
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
    s->fd_open_time = qemu_get_clock(rt_clock);
    s->fd_got_error = 0;
    return 0;
}

static int raw_is_inserted(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    int ret;

    switch(s->type) {
    case FTYPE_CD:
        ret = ioctl(s->fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
        if (ret == CDS_DISC_OK)
            return 1;
        else
            return 0;
        break;
    case FTYPE_FD:
        ret = fd_open(bs);
        return (ret >= 0);
    default:
        return 1;
    }
}

/* currently only used by fdc.c, but a CD version would be good too */
static int raw_media_changed(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    switch(s->type) {
    case FTYPE_FD:
        {
            int ret;
            /* XXX: we do not have a true media changed indication. It
               does not work if the floppy is changed without trying
               to read it */
            fd_open(bs);
            ret = s->fd_media_changed;
            s->fd_media_changed = 0;
#ifdef DEBUG_FLOPPY
            printf("Floppy changed=%d\n", ret);
#endif
            return ret;
        }
    default:
        return -ENOTSUP;
    }
}

static int raw_eject(BlockDriverState *bs, int eject_flag)
{
    BDRVRawState *s = bs->opaque;

    switch(s->type) {
    case FTYPE_CD:
        if (eject_flag) {
            if (ioctl (s->fd, CDROMEJECT, NULL) < 0)
                perror("CDROMEJECT");
        } else {
            if (ioctl (s->fd, CDROMCLOSETRAY, NULL) < 0)
                perror("CDROMEJECT");
        }
        break;
    case FTYPE_FD:
        {
            int fd;
            if (s->fd >= 0) {
                close(s->fd);
                s->fd = -1;
                raw_close_fd_pool(s);
            }
            fd = open(bs->filename, s->fd_open_flags | O_NONBLOCK);
            if (fd >= 0) {
                if (ioctl(fd, FDEJECT, 0) < 0)
                    perror("FDEJECT");
                close(fd);
            }
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static int raw_set_locked(BlockDriverState *bs, int locked)
{
    BDRVRawState *s = bs->opaque;

    switch(s->type) {
    case FTYPE_CD:
        if (ioctl (s->fd, CDROM_LOCKDOOR, locked) < 0) {
            /* Note: an error can happen if the distribution automatically
               mounts the CD-ROM */
            //        perror("CDROM_LOCKDOOR");
        }
        break;
    default:
        return -ENOTSUP;
    }
    return 0;
}

static int raw_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    BDRVRawState *s = bs->opaque;

    return ioctl(s->fd, req, buf);
}
#else

static int fd_open(BlockDriverState *bs)
{
    return 0;
}

static int raw_is_inserted(BlockDriverState *bs)
{
    return 1;
}

static int raw_media_changed(BlockDriverState *bs)
{
    return -ENOTSUP;
}

static int raw_eject(BlockDriverState *bs, int eject_flag)
{
    return -ENOTSUP;
}

static int raw_set_locked(BlockDriverState *bs, int locked)
{
    return -ENOTSUP;
}

static int raw_ioctl(BlockDriverState *bs, unsigned long int req, void *buf)
{
    return -ENOTSUP;
}
#endif /* !linux */

BlockDriver bdrv_host_device = {
    "host_device",
    sizeof(BDRVRawState),
    NULL, /* no probe for protocols */
    hdev_open,
    NULL,
    NULL,
    raw_close,
    NULL,
    raw_flush,

#ifdef CONFIG_AIO
    .bdrv_aio_read = raw_aio_read,
    .bdrv_aio_write = raw_aio_write,
    .bdrv_aio_cancel = raw_aio_cancel,
    .aiocb_size = sizeof(RawAIOCB),
#endif
    .bdrv_pread = raw_pread,
    .bdrv_pwrite = raw_pwrite,
    .bdrv_getlength = raw_getlength,

    /* removable device support */
    .bdrv_is_inserted = raw_is_inserted,
    .bdrv_media_changed = raw_media_changed,
    .bdrv_eject = raw_eject,
    .bdrv_set_locked = raw_set_locked,
    /* generic scsi device */
    .bdrv_ioctl = raw_ioctl,
};
