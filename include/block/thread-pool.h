/*
 * QEMU block layer thread pool
 *
 * Copyright IBM, Corp. 2008
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_THREAD_POOL_H
#define QEMU_THREAD_POOL_H 1

#include "qemu-common.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "block/coroutine.h"
#include "block/block_int.h"

typedef int ThreadPoolFunc(void *opaque);

typedef struct ThreadPool ThreadPool;

ThreadPool *thread_pool_new(struct AioContext *ctx);
void thread_pool_free(ThreadPool *pool);

BlockDriverAIOCB *thread_pool_submit_aio(ThreadPool *pool,
        ThreadPoolFunc *func, void *arg,
        BlockDriverCompletionFunc *cb, void *opaque);
int coroutine_fn thread_pool_submit_co(ThreadPool *pool,
        ThreadPoolFunc *func, void *arg);
void thread_pool_submit(ThreadPool *pool, ThreadPoolFunc *func, void *arg);

#endif
