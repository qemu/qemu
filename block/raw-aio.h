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

/* AIO request types */
#define QEMU_AIO_READ         0x0001
#define QEMU_AIO_WRITE        0x0002
#define QEMU_AIO_IOCTL        0x0004
#define QEMU_AIO_FLUSH        0x0008
#define QEMU_AIO_DISCARD      0x0010
#define QEMU_AIO_WRITE_ZEROES 0x0020
#define QEMU_AIO_TYPE_MASK \
        (QEMU_AIO_READ|QEMU_AIO_WRITE|QEMU_AIO_IOCTL|QEMU_AIO_FLUSH| \
         QEMU_AIO_DISCARD|QEMU_AIO_WRITE_ZEROES)

/* AIO flags */
#define QEMU_AIO_MISALIGNED   0x1000
#define QEMU_AIO_BLKDEV       0x2000


/* linux-aio.c - Linux native implementation */
#ifdef CONFIG_LINUX_AIO
void *laio_init(void);
void laio_cleanup(void *s);
BlockDriverAIOCB *laio_submit(BlockDriverState *bs, void *aio_ctx, int fd,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int type);
void laio_detach_aio_context(void *s, AioContext *old_context);
void laio_attach_aio_context(void *s, AioContext *new_context);
void laio_io_plug(BlockDriverState *bs, void *aio_ctx);
int laio_io_unplug(BlockDriverState *bs, void *aio_ctx, bool unplug);
#endif

#ifdef _WIN32
typedef struct QEMUWin32AIOState QEMUWin32AIOState;
QEMUWin32AIOState *win32_aio_init(void);
void win32_aio_cleanup(QEMUWin32AIOState *aio);
int win32_aio_attach(QEMUWin32AIOState *aio, HANDLE hfile);
BlockDriverAIOCB *win32_aio_submit(BlockDriverState *bs,
        QEMUWin32AIOState *aio, HANDLE hfile,
        int64_t sector_num, QEMUIOVector *qiov, int nb_sectors,
        BlockDriverCompletionFunc *cb, void *opaque, int type);
void win32_aio_detach_aio_context(QEMUWin32AIOState *aio,
                                  AioContext *old_context);
void win32_aio_attach_aio_context(QEMUWin32AIOState *aio,
                                  AioContext *new_context);
#endif

#endif /* QEMU_RAW_AIO_H */
