/*
 * Aio tasks loops
 *
 * Copyright (c) 2019 Virtuozzo International GmbH.
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

#include "qemu/osdep.h"
#include "block/aio.h"
#include "block/aio_task.h"

struct AioTaskPool {
    Coroutine *main_co;
    int status;
    int max_busy_tasks;
    int busy_tasks;
    bool waiting;
};

static void coroutine_fn aio_task_co(void *opaque)
{
    AioTask *task = opaque;
    AioTaskPool *pool = task->pool;

    assert(pool->busy_tasks < pool->max_busy_tasks);
    pool->busy_tasks++;

    task->ret = task->func(task);

    pool->busy_tasks--;

    if (task->ret < 0 && pool->status == 0) {
        pool->status = task->ret;
    }

    g_free(task);

    if (pool->waiting) {
        pool->waiting = false;
        aio_co_wake(pool->main_co);
    }
}

void coroutine_fn aio_task_pool_wait_one(AioTaskPool *pool)
{
    assert(pool->busy_tasks > 0);
    assert(qemu_coroutine_self() == pool->main_co);

    pool->waiting = true;
    qemu_coroutine_yield();

    assert(!pool->waiting);
    assert(pool->busy_tasks < pool->max_busy_tasks);
}

void coroutine_fn aio_task_pool_wait_slot(AioTaskPool *pool)
{
    if (pool->busy_tasks < pool->max_busy_tasks) {
        return;
    }

    aio_task_pool_wait_one(pool);
}

void coroutine_fn aio_task_pool_wait_all(AioTaskPool *pool)
{
    while (pool->busy_tasks > 0) {
        aio_task_pool_wait_one(pool);
    }
}

void coroutine_fn aio_task_pool_start_task(AioTaskPool *pool, AioTask *task)
{
    aio_task_pool_wait_slot(pool);

    task->pool = pool;
    qemu_coroutine_enter(qemu_coroutine_create(aio_task_co, task));
}

AioTaskPool *coroutine_fn aio_task_pool_new(int max_busy_tasks)
{
    AioTaskPool *pool = g_new0(AioTaskPool, 1);

    assert(max_busy_tasks > 0);

    pool->main_co = qemu_coroutine_self();
    pool->max_busy_tasks = max_busy_tasks;

    return pool;
}

void aio_task_pool_free(AioTaskPool *pool)
{
    g_free(pool);
}

int aio_task_pool_status(AioTaskPool *pool)
{
    if (!pool) {
        return 0; /* Sugar for lazy allocation of aio pool */
    }

    return pool->status;
}
