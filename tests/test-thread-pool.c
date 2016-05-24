#include "qemu/osdep.h"
#include "qemu-common.h"
#include "block/aio.h"
#include "block/thread-pool.h"
#include "block/block.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"

static AioContext *ctx;
static ThreadPool *pool;
static int active;

typedef struct {
    BlockAIOCB *aiocb;
    int n;
    int ret;
} WorkerTestData;

static int worker_cb(void *opaque)
{
    WorkerTestData *data = opaque;
    return atomic_fetch_inc(&data->n);
}

static int long_cb(void *opaque)
{
    WorkerTestData *data = opaque;
    atomic_inc(&data->n);
    g_usleep(2000000);
    atomic_inc(&data->n);
    return 0;
}

static void done_cb(void *opaque, int ret)
{
    WorkerTestData *data = opaque;
    g_assert(data->ret == -EINPROGRESS || data->ret == -ECANCELED);
    data->ret = ret;
    data->aiocb = NULL;

    /* Callbacks are serialized, so no need to use atomic ops.  */
    active--;
}

static void test_submit(void)
{
    WorkerTestData data = { .n = 0 };
    thread_pool_submit(pool, worker_cb, &data);
    while (data.n == 0) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(data.n, ==, 1);
}

static void test_submit_aio(void)
{
    WorkerTestData data = { .n = 0, .ret = -EINPROGRESS };
    data.aiocb = thread_pool_submit_aio(pool, worker_cb, &data,
                                        done_cb, &data);

    /* The callbacks are not called until after the first wait.  */
    active = 1;
    g_assert_cmpint(data.ret, ==, -EINPROGRESS);
    while (data.ret == -EINPROGRESS) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(active, ==, 0);
    g_assert_cmpint(data.n, ==, 1);
    g_assert_cmpint(data.ret, ==, 0);
}

static void co_test_cb(void *opaque)
{
    WorkerTestData *data = opaque;

    active = 1;
    data->n = 0;
    data->ret = -EINPROGRESS;
    thread_pool_submit_co(pool, worker_cb, data);

    /* The test continues in test_submit_co, after qemu_coroutine_enter... */

    g_assert_cmpint(data->n, ==, 1);
    data->ret = 0;
    active--;

    /* The test continues in test_submit_co, after aio_poll... */
}

static void test_submit_co(void)
{
    WorkerTestData data;
    Coroutine *co = qemu_coroutine_create(co_test_cb);

    qemu_coroutine_enter(co, &data);

    /* Back here once the worker has started.  */

    g_assert_cmpint(active, ==, 1);
    g_assert_cmpint(data.ret, ==, -EINPROGRESS);

    /* aio_poll will execute the rest of the coroutine.  */

    while (data.ret == -EINPROGRESS) {
        aio_poll(ctx, true);
    }

    /* Back here after the coroutine has finished.  */

    g_assert_cmpint(active, ==, 0);
    g_assert_cmpint(data.ret, ==, 0);
}

static void test_submit_many(void)
{
    WorkerTestData data[100];
    int i;

    /* Start more work items than there will be threads.  */
    for (i = 0; i < 100; i++) {
        data[i].n = 0;
        data[i].ret = -EINPROGRESS;
        thread_pool_submit_aio(pool, worker_cb, &data[i], done_cb, &data[i]);
    }

    active = 100;
    while (active > 0) {
        aio_poll(ctx, true);
    }
    for (i = 0; i < 100; i++) {
        g_assert_cmpint(data[i].n, ==, 1);
        g_assert_cmpint(data[i].ret, ==, 0);
    }
}

static void do_test_cancel(bool sync)
{
    WorkerTestData data[100];
    int num_canceled;
    int i;

    /* Start more work items than there will be threads, to ensure
     * the pool is full.
     */
    test_submit_many();

    /* Start long running jobs, to ensure we can cancel some.  */
    for (i = 0; i < 100; i++) {
        data[i].n = 0;
        data[i].ret = -EINPROGRESS;
        data[i].aiocb = thread_pool_submit_aio(pool, long_cb, &data[i],
                                               done_cb, &data[i]);
    }

    /* Starting the threads may be left to a bottom half.  Let it
     * run, but do not waste too much time...
     */
    active = 100;
    aio_notify(ctx);
    aio_poll(ctx, false);

    /* Wait some time for the threads to start, with some sanity
     * testing on the behavior of the scheduler...
     */
    g_assert_cmpint(active, ==, 100);
    g_usleep(1000000);
    g_assert_cmpint(active, >, 50);

    /* Cancel the jobs that haven't been started yet.  */
    num_canceled = 0;
    for (i = 0; i < 100; i++) {
        if (atomic_cmpxchg(&data[i].n, 0, 3) == 0) {
            data[i].ret = -ECANCELED;
            if (sync) {
                bdrv_aio_cancel(data[i].aiocb);
            } else {
                bdrv_aio_cancel_async(data[i].aiocb);
            }
            num_canceled++;
        }
    }
    g_assert_cmpint(active, >, 0);
    g_assert_cmpint(num_canceled, <, 100);

    for (i = 0; i < 100; i++) {
        if (data[i].aiocb && data[i].n != 3) {
            if (sync) {
                /* Canceling the others will be a blocking operation.  */
                bdrv_aio_cancel(data[i].aiocb);
            } else {
                bdrv_aio_cancel_async(data[i].aiocb);
            }
        }
    }

    /* Finish execution and execute any remaining callbacks.  */
    while (active > 0) {
        aio_poll(ctx, true);
    }
    g_assert_cmpint(active, ==, 0);
    for (i = 0; i < 100; i++) {
        if (data[i].n == 3) {
            g_assert_cmpint(data[i].ret, ==, -ECANCELED);
            g_assert(data[i].aiocb == NULL);
        } else {
            g_assert_cmpint(data[i].n, ==, 2);
            g_assert(data[i].ret == 0 || data[i].ret == -ECANCELED);
            g_assert(data[i].aiocb == NULL);
        }
    }
}

static void test_cancel(void)
{
    do_test_cancel(true);
}

static void test_cancel_async(void)
{
    do_test_cancel(false);
}

int main(int argc, char **argv)
{
    int ret;
    Error *local_error = NULL;

    init_clocks();

    ctx = aio_context_new(&local_error);
    if (!ctx) {
        error_reportf_err(local_error, "Failed to create AIO Context: ");
        exit(1);
    }
    pool = aio_get_thread_pool(ctx);

    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/thread-pool/submit", test_submit);
    g_test_add_func("/thread-pool/submit-aio", test_submit_aio);
    g_test_add_func("/thread-pool/submit-co", test_submit_co);
    g_test_add_func("/thread-pool/submit-many", test_submit_many);
    g_test_add_func("/thread-pool/cancel", test_cancel);
    g_test_add_func("/thread-pool/cancel-async", test_cancel_async);

    ret = g_test_run();

    aio_context_unref(ctx);
    return ret;
}
