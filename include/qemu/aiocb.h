/*
 * Data structures representing asynchronous I/O operations
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_AIOCB_H
#define QEMU_AIOCB_H

typedef struct BlockAIOCB BlockAIOCB;
typedef void BlockCompletionFunc(void *opaque, int ret);

typedef struct AIOCBInfo {
    void (*cancel_async)(BlockAIOCB *acb);
    size_t aiocb_size;
} AIOCBInfo;

struct BlockAIOCB {
    const AIOCBInfo *aiocb_info;
    BlockDriverState *bs;
    BlockCompletionFunc *cb;
    void *opaque;
    int refcnt;
};

void *qemu_aio_get(const AIOCBInfo *aiocb_info, BlockDriverState *bs,
                   BlockCompletionFunc *cb, void *opaque);
void qemu_aio_unref(void *p);
void qemu_aio_ref(void *p);

#endif
