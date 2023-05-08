/*
 * Declarations for AIO in the raw protocol
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

#ifndef QEMU_RAW_AIO_H
#define QEMU_RAW_AIO_H

#include "block/aio.h"
#include "qemu/iov.h"

/* AIO request types */
#define QEMU_AIO_READ         0x0001
#define QEMU_AIO_WRITE        0x0002
#define QEMU_AIO_IOCTL        0x0004
#define QEMU_AIO_FLUSH        0x0008
#define QEMU_AIO_DISCARD      0x0010
#define QEMU_AIO_WRITE_ZEROES 0x0020
#define QEMU_AIO_COPY_RANGE   0x0040
#define QEMU_AIO_TRUNCATE     0x0080
#define QEMU_AIO_ZONE_REPORT  0x0100
#define QEMU_AIO_ZONE_MGMT    0x0200
#define QEMU_AIO_ZONE_APPEND  0x0400
#define QEMU_AIO_TYPE_MASK \
        (QEMU_AIO_READ | \
         QEMU_AIO_WRITE | \
         QEMU_AIO_IOCTL | \
         QEMU_AIO_FLUSH | \
         QEMU_AIO_DISCARD | \
         QEMU_AIO_WRITE_ZEROES | \
         QEMU_AIO_COPY_RANGE | \
         QEMU_AIO_TRUNCATE | \
         QEMU_AIO_ZONE_REPORT | \
         QEMU_AIO_ZONE_MGMT | \
         QEMU_AIO_ZONE_APPEND)

/* AIO flags */
#define QEMU_AIO_MISALIGNED   0x1000
#define QEMU_AIO_BLKDEV       0x2000
#define QEMU_AIO_NO_FALLBACK  0x4000


/* linux-aio.c - Linux native implementation */
#ifdef CONFIG_LINUX_AIO
typedef struct LinuxAioState LinuxAioState;
LinuxAioState *laio_init(Error **errp);
void laio_cleanup(LinuxAioState *s);

/* laio_co_submit: submit I/O requests in the thread's current AioContext. */
int coroutine_fn laio_co_submit(int fd, uint64_t offset, QEMUIOVector *qiov,
                                int type, uint64_t dev_max_batch);

void laio_detach_aio_context(LinuxAioState *s, AioContext *old_context);
void laio_attach_aio_context(LinuxAioState *s, AioContext *new_context);

/*
 * laio_io_plug/unplug work in the thread's current AioContext, therefore the
 * caller must ensure that they are paired in the same IOThread.
 */
void laio_io_plug(void);
void laio_io_unplug(uint64_t dev_max_batch);
#endif
/* io_uring.c - Linux io_uring implementation */
#ifdef CONFIG_LINUX_IO_URING
typedef struct LuringState LuringState;
LuringState *luring_init(Error **errp);
void luring_cleanup(LuringState *s);

/* luring_co_submit: submit I/O requests in the thread's current AioContext. */
int coroutine_fn luring_co_submit(BlockDriverState *bs, int fd, uint64_t offset,
                                  QEMUIOVector *qiov, int type);
void luring_detach_aio_context(LuringState *s, AioContext *old_context);
void luring_attach_aio_context(LuringState *s, AioContext *new_context);

/*
 * luring_io_plug/unplug work in the thread's current AioContext, therefore the
 * caller must ensure that they are paired in the same IOThread.
 */
void luring_io_plug(void);
void luring_io_unplug(void);
#endif

#ifdef _WIN32
typedef struct QEMUWin32AIOState QEMUWin32AIOState;
QEMUWin32AIOState *win32_aio_init(void);
void win32_aio_cleanup(QEMUWin32AIOState *aio);
int win32_aio_attach(QEMUWin32AIOState *aio, HANDLE hfile);
BlockAIOCB *win32_aio_submit(BlockDriverState *bs,
        QEMUWin32AIOState *aio, HANDLE hfile,
        uint64_t offset, uint64_t bytes, QEMUIOVector *qiov,
        BlockCompletionFunc *cb, void *opaque, int type);
void win32_aio_detach_aio_context(QEMUWin32AIOState *aio,
                                  AioContext *old_context);
void win32_aio_attach_aio_context(QEMUWin32AIOState *aio,
                                  AioContext *new_context);
#endif

#endif /* QEMU_RAW_AIO_H */
