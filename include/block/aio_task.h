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

#ifndef BLOCK_AIO_TASK_H
#define BLOCK_AIO_TASK_H

typedef struct AioTaskPool AioTaskPool;
typedef struct AioTask AioTask;
typedef int coroutine_fn (*AioTaskFunc)(AioTask *task);
struct AioTask {
    AioTaskPool *pool;
    AioTaskFunc func;
    int ret;
};

AioTaskPool *coroutine_fn aio_task_pool_new(int max_busy_tasks);
void aio_task_pool_free(AioTaskPool *);

/* error code of failed task or 0 if all is OK */
int aio_task_pool_status(AioTaskPool *pool);

bool aio_task_pool_empty(AioTaskPool *pool);

/* User provides filled @task, however task->pool will be set automatically */
void coroutine_fn aio_task_pool_start_task(AioTaskPool *pool, AioTask *task);

void coroutine_fn aio_task_pool_wait_slot(AioTaskPool *pool);
void coroutine_fn aio_task_pool_wait_one(AioTaskPool *pool);
void coroutine_fn aio_task_pool_wait_all(AioTaskPool *pool);

#endif /* BLOCK_AIO_TASK_H */
