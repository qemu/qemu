/*
 * QEMU posix-aio emulation
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "qemu-queue.h"
#include "osdep.h"
#include "sysemu.h"
#include "qemu-common.h"
#include "trace.h"
#include "thread-pool.h"
#include "block_int.h"
#include "iov.h"

#include "block/raw-posix-aio.h"

struct qemu_paiocb {
    BlockDriverAIOCB common;
    int aio_fildes;
    union {
        struct iovec *aio_iov;
        void *aio_ioctl_buf;
    };
    int aio_niov;
    size_t aio_nbytes;
#define aio_ioctl_cmd   aio_nbytes /* for QEMU_AIO_IOCTL */
    off_t aio_offset;
    int aio_type;
};

#ifdef CONFIG_PREADV
static int preadv_present = 1;
#else
static int preadv_present = 0;
#endif

static ssize_t handle_aiocb_ioctl(struct qemu_paiocb *aiocb)
{
    int ret;

    ret = ioctl(aiocb->aio_fildes, aiocb->aio_ioctl_cmd, aiocb->aio_ioctl_buf);
    if (ret == -1)
        return -errno;

    /*
     * This looks weird, but the aio code only considers a request
     * successful if it has written the full number of bytes.
     *
     * Now we overload aio_nbytes as aio_ioctl_cmd for the ioctl command,
     * so in fact we return the ioctl command here to make posix_aio_read()
     * happy..
     */
    return aiocb->aio_nbytes;
}

static ssize_t handle_aiocb_flush(struct qemu_paiocb *aiocb)
{
    int ret;

    ret = qemu_fdatasync(aiocb->aio_fildes);
    if (ret == -1)
        return -errno;
    return 0;
}

#ifdef CONFIG_PREADV

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

static ssize_t handle_aiocb_rw_vector(struct qemu_paiocb *aiocb)
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

    if (len == -1)
        return -errno;
    return len;
}

/*
 * Read/writes the data to/from a given linear buffer.
 *
 * Returns the number of bytes handles or -errno in case of an error. Short
 * reads are only returned if the end of the file is reached.
 */
static ssize_t handle_aiocb_rw_linear(struct qemu_paiocb *aiocb, char *buf)
{
    ssize_t offset = 0;
    ssize_t len;

    while (offset < aiocb->aio_nbytes) {
         if (aiocb->aio_type & QEMU_AIO_WRITE)
             len = pwrite(aiocb->aio_fildes,
                          (const char *)buf + offset,
                          aiocb->aio_nbytes - offset,
                          aiocb->aio_offset + offset);
         else
             len = pread(aiocb->aio_fildes,
                         buf + offset,
                         aiocb->aio_nbytes - offset,
                         aiocb->aio_offset + offset);

         if (len == -1 && errno == EINTR)
             continue;
         else if (len == -1) {
             offset = -errno;
             break;
         } else if (len == 0)
             break;

         offset += len;
    }

    return offset;
}

static ssize_t handle_aiocb_rw(struct qemu_paiocb *aiocb)
{
    ssize_t nbytes;
    char *buf;

    if (!(aiocb->aio_type & QEMU_AIO_MISALIGNED)) {
        /*
         * If there is just a single buffer, and it is properly aligned
         * we can just use plain pread/pwrite without any problems.
         */
        if (aiocb->aio_niov == 1)
             return handle_aiocb_rw_linear(aiocb, aiocb->aio_iov->iov_base);

        /*
         * We have more than one iovec, and all are properly aligned.
         *
         * Try preadv/pwritev first and fall back to linearizing the
         * buffer if it's not supported.
         */
        if (preadv_present) {
            nbytes = handle_aiocb_rw_vector(aiocb);
            if (nbytes == aiocb->aio_nbytes)
                return nbytes;
            if (nbytes < 0 && nbytes != -ENOSYS)
                return nbytes;
            preadv_present = 0;
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
    buf = qemu_blockalign(aiocb->common.bs, aiocb->aio_nbytes);
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
            if (copy > aiocb->aio_iov[i].iov_len)
                copy = aiocb->aio_iov[i].iov_len;
            memcpy(aiocb->aio_iov[i].iov_base, p, copy);
            p     += copy;
            count -= copy;
        }
    }
    qemu_vfree(buf);

    return nbytes;
}

static int aio_worker(void *arg)
{
    struct qemu_paiocb *aiocb = arg;
    ssize_t ret = 0;

    switch (aiocb->aio_type & QEMU_AIO_TYPE_MASK) {
    case QEMU_AIO_READ:
        ret = handle_aiocb_rw(aiocb);
        if (ret >= 0 && ret < aiocb->aio_nbytes && aiocb->common.bs->growable) {
            /* A short read means that we have reached EOF. Pad the buffer
             * with zeros for bytes after EOF. */
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
    default:
        fprintf(stderr, "invalid aio request (0x%x)\n", aiocb->aio_type);
        ret = -EINVAL;
        break;
    }

    qemu_aio_release(aiocb);
    return ret;
}

static AIOPool raw_aio_pool = {
    .aiocb_size         = sizeof(struct qemu_paiocb),
};

BlockDriverAIOCB *paio_submit(BlockDriverState *bs, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int type)
{
    struct qemu_paiocb *acb;

    acb = qemu_aio_get(&raw_aio_pool, bs, cb, opaque);
    acb->aio_type = type;
    acb->aio_fildes = fd;

    if (qiov) {
        acb->aio_iov = qiov->iov;
        acb->aio_niov = qiov->niov;
    }
    acb->aio_nbytes = nb_sectors * 512;
    acb->aio_offset = sector_num * 512;

    trace_paio_submit(acb, opaque, sector_num, nb_sectors, type);
    return thread_pool_submit_aio(aio_worker, acb, cb, opaque);
}

BlockDriverAIOCB *paio_ioctl(BlockDriverState *bs, int fd,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque)
{
    struct qemu_paiocb *acb;

    acb = qemu_aio_get(&raw_aio_pool, bs, cb, opaque);
    acb->aio_type = QEMU_AIO_IOCTL;
    acb->aio_fildes = fd;
    acb->aio_offset = 0;
    acb->aio_ioctl_buf = buf;
    acb->aio_ioctl_cmd = req;

    return thread_pool_submit_aio(aio_worker, acb, cb, opaque);
}
