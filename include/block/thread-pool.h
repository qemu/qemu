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

/* ------------------------------------------- */
/* Generic thread pool types and methods below */
typedef struct ThreadPool ThreadPool;

/* Create a new thread pool. Never returns NULL. */
ThreadPool *thread_pool_new(void);

/*
 * Free the thread pool.
 * Waits for all the previously submitted work to complete before performing
 * the actual freeing operation.
 */
void thread_pool_free(ThreadPool *pool);

/*
 * Submit a new work (task) for the pool.
 *
 * @opaque_destroy is an optional GDestroyNotify for the @opaque argument
 * to the work function at @func.
 */
void thread_pool_submit(ThreadPool *pool, ThreadPoolFunc *func,
                        void *opaque, GDestroyNotify opaque_destroy);

/*
 * Submit a new work (task) for the pool, making sure it starts getting
 * processed immediately, launching a new thread for it if necessary.
 *
 * @opaque_destroy is an optional GDestroyNotify for the @opaque argument
 * to the work function at @func.
 */
void thread_pool_submit_immediate(ThreadPool *pool, ThreadPoolFunc *func,
                                  void *opaque, GDestroyNotify opaque_destroy);

/*
 * Wait for all previously submitted work to complete before returning.
 *
 * Can be used as a barrier between two sets of tasks executed on a thread
 * pool without destroying it or in a performance sensitive path where the
 * caller just wants to wait for all tasks to complete while deferring the
 * pool free operation for later, less performance sensitive time.
 */
void thread_pool_wait(ThreadPool *pool);

/* Set the maximum number of threads in the pool. */
bool thread_pool_set_max_threads(ThreadPool *pool, int max_threads);

/*
 * Adjust the maximum number of threads in the pool to give each task its
 * own thread (exactly one thread per task).
 */
bool thread_pool_adjust_max_threads_to_work(ThreadPool *pool);

#endif
