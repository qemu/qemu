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
#include "posix-aio-compat.h"
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
#include <sys/cdio.h>
#endif

#ifdef __OpenBSD__
#include <sys/ioctl.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#endif

#ifdef __DragonFly__
#include <sys/ioctl.h>
#include <sys/diskslice.h>
#endif

//#define DEBUG_FLOPPY

//#define DEBUG_BLOCK
#if defined(DEBUG_BLOCK)
#define DEBUG_BLOCK_PRINT(formatCstr, args...) do { if (qemu_log_enabled())	\
    { qemu_log(formatCstr, ##args); qemu_log_flush(); } } while (0)
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

typedef struct BDRVRawState {
    int fd;
    int type;
    unsigned int lseek_err_cnt;
#if defined(__linux__)
    /* linux floppy specific */
    int fd_open_flags;
    int64_t fd_open_time;
    int64_t fd_error_time;
    int fd_got_error;
    int fd_media_changed;
#endif
#if defined(__FreeBSD__)
    int cd_open_flags;
#endif
    uint8_t* aligned_buf;
} BDRVRawState;

static int posix_aio_init(void);

static int fd_open(BlockDriverState *bs);

#if defined(__FreeBSD__)
static int cd_open(BlockDriverState *bs);
#endif

static int raw_is_inserted(BlockDriverState *bs);

static int raw_open(BlockDriverState *bs, const char *filename, int flags)
{
    BDRVRawState *s = bs->opaque;
    int fd, open_flags, ret;

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
        return -errno;

    if (offset >= 0 && lseek(s->fd, offset, SEEK_SET) == (off_t)-1) {
        ++(s->lseek_err_cnt);
        if(s->lseek_err_cnt) {
            DEBUG_BLOCK_PRINT("raw_pwrite(%d:%s, %" PRId64 ", %p, %d) [%"
                              PRId64 "] lseek failed : %d = %s\n",
                              s->fd, bs->filename, offset, buf, count,
                              bs->total_sectors, errno, strerror(errno));
        }
        return -EIO;
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

    return  (ret < 0) ? -errno : ret;
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

static int raw_read(BlockDriverState *bs, int64_t sector_num,
                    uint8_t *buf, int nb_sectors)
{
    int ret;

    ret = raw_pread(bs, sector_num * 512, buf, nb_sectors * 512);
    if (ret == (nb_sectors * 512))
        ret = 0;
    return ret;
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

static int raw_write(BlockDriverState *bs, int64_t sector_num,
                     const uint8_t *buf, int nb_sectors)
{
    int ret;
    ret = raw_pwrite(bs, sector_num * 512, buf, nb_sectors * 512);
    if (ret == (nb_sectors * 512))
        ret = 0;
    return ret;
}

#ifdef CONFIG_AIO
/***********************************************************/
/* Unix AIO using POSIX AIO */

typedef struct RawAIOCB {
    BlockDriverAIOCB common;
    struct qemu_paiocb aiocb;
    struct RawAIOCB *next;
    int ret;
} RawAIOCB;

typedef struct PosixAioState
{
    int rfd, wfd;
    RawAIOCB *first_aio;
} PosixAioState;

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
            ret = qemu_paio_error(&acb->aiocb);
            if (ret == ECANCELED) {
                /* remove the request */
                *pacb = acb->next;
                qemu_aio_release(acb);
            } else if (ret != EINPROGRESS) {
                /* end of aio */
                if (ret == 0) {
                    ret = qemu_paio_return(&acb->aiocb);
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
    struct qemu_paioinit ai;
  
    if (posix_aio_state)
        return 0;

    s = qemu_malloc(sizeof(PosixAioState));

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

    memset(&ai, 0, sizeof(ai));
    ai.aio_threads = 64;
    ai.aio_num = 64;
    qemu_paio_init(&ai);

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
    acb->aiocb.aio_fildes = s->fd;
    acb->aiocb.ev_signo = SIGUSR2;
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

static void raw_aio_remove(RawAIOCB *acb)
{
    RawAIOCB **pacb;

    /* remove the callback from the queue */
    pacb = &posix_aio_state->first_aio;
    for(;;) {
        if (*pacb == NULL) {
            fprintf(stderr, "raw_aio_remove: aio request not found!\n");
            break;
        } else if (*pacb == acb) {
            *pacb = acb->next;
            qemu_aio_release(acb);
            break;
        }
        pacb = &(*pacb)->next;
    }
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
    if (qemu_paio_read(&acb->aiocb) < 0) {
        raw_aio_remove(acb);
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
    if (qemu_paio_write(&acb->aiocb) < 0) {
        raw_aio_remove(acb);
        return NULL;
    }
    return &acb->common;
}

static void raw_aio_cancel(BlockDriverAIOCB *blockacb)
{
    int ret;
    RawAIOCB *acb = (RawAIOCB *)blockacb;

    ret = qemu_paio_cancel(acb->aiocb.aio_fildes, &acb->aiocb);
    if (ret == QEMU_PAIO_NOTCANCELED) {
        /* fail safe: if the aio could not be canceled, we wait for
           it */
        while (qemu_paio_error(&acb->aiocb) == EINPROGRESS);
    }

    raw_aio_remove(acb);
}
#else /* CONFIG_AIO */
static int posix_aio_init(void)
{
    return 0;
}
#endif /* CONFIG_AIO */


static void raw_close(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
        if (s->aligned_buf != NULL)
            qemu_free(s->aligned_buf);
    }
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
#ifdef HOST_BSD
    struct stat sb;
#ifdef __FreeBSD__
    int reopened = 0;
#endif
#endif
#ifdef __sun__
    struct dk_minfo minfo;
    int rv;
#endif
    int ret;

    ret = fd_open(bs);
    if (ret < 0)
        return ret;

#ifdef HOST_BSD
#ifdef __FreeBSD__
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
#ifdef CONFIG_COCOA
        size = LONG_LONG_MAX;
#else
        size = lseek(fd, 0LL, SEEK_END);
#endif
#ifdef __FreeBSD__
        switch(s->type) {
        case FTYPE_CD:
            /* XXX FreeBSD acd returns UINT_MAX sectors for an empty drive */
            if (size == 2048LL * (unsigned)-1)
                size = 0;
            /* XXX no disc?  maybe we need to reopen... */
            if (size <= 0 && !reopened && cd_open(bs) >= 0) {
                reopened = 1;
                goto again;
            }
        }
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

    .bdrv_read = raw_read,
    .bdrv_write = raw_write,
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
    int fd, open_flags, ret;

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
#if defined(__FreeBSD__)
    if (strstart(filename, "/dev/cd", NULL) ||
        strstart(filename, "/dev/acd", NULL)) {
        s->type = FTYPE_CD;
        s->cd_open_flags = open_flags;
    }
#endif
    s->fd = -1;
    fd = open(filename, open_flags, 0644);
    if (fd < 0) {
        ret = -errno;
        if (ret == -EROFS)
            ret = -EACCES;
        return ret;
    }
    s->fd = fd;
#if defined(__FreeBSD__)
    /* make sure the door isnt locked at this time */
    if (s->type == FTYPE_CD)
        ioctl (s->fd, CDIOCALLOW);
#endif
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

static BlockDriverAIOCB *raw_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    RawAIOCB *acb;

    acb = raw_aio_setup(bs, 0, buf, 0, cb, opaque);
    if (!acb)
        return NULL;

    acb->aiocb.aio_ioctl_cmd = req;
    if (qemu_paio_ioctl(&acb->aiocb) < 0) {
        raw_aio_remove(acb);
        return NULL;
    }

    return &acb->common;
}

#elif defined(__FreeBSD__)

static int fd_open(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    /* this is just to ensure s->fd is sane (its called by io ops) */
    if (s->fd >= 0)
        return 0;
    return -EIO;
}

static int cd_open(BlockDriverState *bs)
{
#if defined(__FreeBSD__)
    BDRVRawState *s = bs->opaque;
    int fd;

    switch(s->type) {
    case FTYPE_CD:
        /* XXX force reread of possibly changed/newly loaded disc,
         * FreeBSD seems to not notice sometimes... */
        if (s->fd >= 0)
            close (s->fd);
        fd = open(bs->filename, s->cd_open_flags, 0644);
        if (fd < 0) {
            s->fd = -1;
            return -EIO;
        }
        s->fd = fd;
        /* make sure the door isnt locked at this time */
        ioctl (s->fd, CDIOCALLOW);
    }
#endif
    return 0;
}

static int raw_is_inserted(BlockDriverState *bs)
{
    BDRVRawState *s = bs->opaque;

    switch(s->type) {
    case FTYPE_CD:
        return (raw_getlength(bs) > 0);
    case FTYPE_FD:
        /* XXX handle this */
        /* FALLTHRU */
    default:
        return 1;
    }
}

static int raw_media_changed(BlockDriverState *bs)
{
    return -ENOTSUP;
}

static int raw_eject(BlockDriverState *bs, int eject_flag)
{
    BDRVRawState *s = bs->opaque;

    switch(s->type) {
    case FTYPE_CD:
        if (s->fd < 0)
            return -ENOTSUP;
        (void) ioctl (s->fd, CDIOCALLOW);
        if (eject_flag) {
            if (ioctl (s->fd, CDIOCEJECT) < 0)
                perror("CDIOCEJECT");
        } else {
            if (ioctl (s->fd, CDIOCCLOSE) < 0)
                perror("CDIOCCLOSE");
        }
        if (cd_open(bs) < 0)
            return -ENOTSUP;
        break;
    case FTYPE_FD:
        /* XXX handle this */
        /* FALLTHRU */
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
        if (s->fd < 0)
            return -ENOTSUP;
        if (ioctl (s->fd, (locked ? CDIOCPREVENT : CDIOCALLOW)) < 0) {
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
    return -ENOTSUP;
}
#else /* !linux && !FreeBSD */

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

static BlockDriverAIOCB *raw_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    return NULL;
}
#endif /* !linux && !FreeBSD */

BlockDriver bdrv_host_device = {
    .format_name	= "host_device",
    .instance_size	= sizeof(BDRVRawState),
    .bdrv_open		= hdev_open,
    .bdrv_close		= raw_close,
    .bdrv_flush		= raw_flush,

#ifdef CONFIG_AIO
    .bdrv_aio_read	= raw_aio_read,
    .bdrv_aio_write	= raw_aio_write,
    .bdrv_aio_cancel	= raw_aio_cancel,
    .aiocb_size		= sizeof(RawAIOCB),
#endif

    .bdrv_read          = raw_read,
    .bdrv_write         = raw_write,
    .bdrv_getlength	= raw_getlength,

    /* removable device support */
    .bdrv_is_inserted	= raw_is_inserted,
    .bdrv_media_changed	= raw_media_changed,
    .bdrv_eject		= raw_eject,
    .bdrv_set_locked	= raw_set_locked,
    /* generic scsi device */
    .bdrv_ioctl		= raw_ioctl,
    .bdrv_aio_ioctl	= raw_aio_ioctl,
};
