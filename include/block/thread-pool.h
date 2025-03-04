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
#define QEMU_THREAD_POOL_H

#include "block/aio.h"

#define THREAD_POOL_MAX_THREADS_DEFAULT         64

typedef int ThreadPoolFunc(void *opaque);

typedef struct ThreadPoolAio ThreadPoolAio;

ThreadPoolAio *thread_pool_new_aio(struct AioContext *ctx);
void thread_pool_free_aio(ThreadPoolAio *pool);

/*
 * thread_pool_submit_{aio,co} API: submit I/O requests in the thread's
 * current AioContext.
 */
BlockAIOCB *thread_pool_submit_aio(ThreadPoolFunc *func, void *arg,
                                   BlockCompletionFunc *cb, void *opaque);
int coroutine_fn thread_pool_submit_co(ThreadPoolFunc *func, void *arg);
void thread_pool_update_params(ThreadPoolAio *pool, struct AioContext *ctx);


#endif
