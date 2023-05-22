/*
 * Graph lock: rwlock to protect block layer graph manipulations (add/remove
 * edges and nodes)
 *
 *  Copyright (c) 2022 Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "block/graph-lock.h"
#include "block/block.h"
#include "block/block_int.h"

/* Dummy lock object to use for Thread Safety Analysis (TSA) */
BdrvGraphLock graph_lock;

/* Protects the list of aiocontext and orphaned_reader_count */
static QemuMutex aio_context_list_lock;

#if 0
/* Written and read with atomic operations. */
static int has_writer;
#endif

/*
 * A reader coroutine could move from an AioContext to another.
 * If this happens, there is no problem from the point of view of
 * counters. The problem is that the total count becomes
 * unbalanced if one of the two AioContexts gets deleted.
 * The count of readers must remain correct, so the AioContext's
 * balance is transferred to this glboal variable.
 * Protected by aio_context_list_lock.
 */
static uint32_t orphaned_reader_count;

/* Queue of readers waiting for the writer to finish */
static CoQueue reader_queue;

struct BdrvGraphRWlock {
    /* How many readers are currently reading the graph. */
    uint32_t reader_count;

    /*
     * List of BdrvGraphRWlock kept in graph-lock.c
     * Protected by aio_context_list_lock
     */
    QTAILQ_ENTRY(BdrvGraphRWlock) next_aio;
};

/*
 * List of BdrvGraphRWlock. This list ensures that each BdrvGraphRWlock
 * can safely modify only its own counter, avoid reading/writing
 * others and thus improving performances by avoiding cacheline bounces.
 */
static QTAILQ_HEAD(, BdrvGraphRWlock) aio_context_list =
    QTAILQ_HEAD_INITIALIZER(aio_context_list);

static void __attribute__((__constructor__)) bdrv_init_graph_lock(void)
{
    qemu_mutex_init(&aio_context_list_lock);
    qemu_co_queue_init(&reader_queue);
}

void register_aiocontext(AioContext *ctx)
{
    ctx->bdrv_graph = g_new0(BdrvGraphRWlock, 1);
    QEMU_LOCK_GUARD(&aio_context_list_lock);
    assert(ctx->bdrv_graph->reader_count == 0);
    QTAILQ_INSERT_TAIL(&aio_context_list, ctx->bdrv_graph, next_aio);
}

void unregister_aiocontext(AioContext *ctx)
{
    QEMU_LOCK_GUARD(&aio_context_list_lock);
    orphaned_reader_count += ctx->bdrv_graph->reader_count;
    QTAILQ_REMOVE(&aio_context_list, ctx->bdrv_graph, next_aio);
    g_free(ctx->bdrv_graph);
}

#if 0
static uint32_t reader_count(void)
{
    BdrvGraphRWlock *brdv_graph;
    uint32_t rd;

    QEMU_LOCK_GUARD(&aio_context_list_lock);

    /* rd can temporarly be negative, but the total will *always* be >= 0 */
    rd = orphaned_reader_count;
    QTAILQ_FOREACH(brdv_graph, &aio_context_list, next_aio) {
        rd += qatomic_read(&brdv_graph->reader_count);
    }

    /* shouldn't overflow unless there are 2^31 readers */
    assert((int32_t)rd >= 0);
    return rd;
}
#endif

void bdrv_graph_wrlock(void)
{
    GLOBAL_STATE_CODE();
    /*
     * TODO Some callers hold an AioContext lock when this is called, which
     * causes deadlocks. Reenable once the AioContext locking is cleaned up (or
     * AioContext locks are gone).
     */
#if 0
    assert(!qatomic_read(&has_writer));

    /* Make sure that constantly arriving new I/O doesn't cause starvation */
    bdrv_drain_all_begin_nopoll();

    /*
     * reader_count == 0: this means writer will read has_reader as 1
     * reader_count >= 1: we don't know if writer read has_writer == 0 or 1,
     *                    but we need to wait.
     * Wait by allowing other coroutine (and possible readers) to continue.
     */
    do {
        /*
         * has_writer must be 0 while polling, otherwise we get a deadlock if
         * any callback involved during AIO_WAIT_WHILE() tries to acquire the
         * reader lock.
         */
        qatomic_set(&has_writer, 0);
        AIO_WAIT_WHILE_UNLOCKED(NULL, reader_count() >= 1);
        qatomic_set(&has_writer, 1);

        /*
         * We want to only check reader_count() after has_writer = 1 is visible
         * to other threads. That way no more readers can sneak in after we've
         * determined reader_count() == 0.
         */
        smp_mb();
    } while (reader_count() >= 1);

    bdrv_drain_all_end();
#endif
}

void bdrv_graph_wrunlock(void)
{
    GLOBAL_STATE_CODE();
#if 0
    QEMU_LOCK_GUARD(&aio_context_list_lock);
    assert(qatomic_read(&has_writer));

    /*
     * No need for memory barriers, this works in pair with
     * the slow path of rdlock() and both take the lock.
     */
    qatomic_store_release(&has_writer, 0);

    /* Wake up all coroutine that are waiting to read the graph */
    qemu_co_enter_all(&reader_queue, &aio_context_list_lock);
#endif
}

void coroutine_fn bdrv_graph_co_rdlock(void)
{
    /* TODO Reenable when wrlock is reenabled */
#if 0
    BdrvGraphRWlock *bdrv_graph;
    bdrv_graph = qemu_get_current_aio_context()->bdrv_graph;

    for (;;) {
        qatomic_set(&bdrv_graph->reader_count,
                    bdrv_graph->reader_count + 1);
        /* make sure writer sees reader_count before we check has_writer */
        smp_mb();

        /*
         * has_writer == 0: this means writer will read reader_count as >= 1
         * has_writer == 1: we don't know if writer read reader_count == 0
         *                  or > 0, but we need to wait anyways because
         *                  it will write.
         */
        if (!qatomic_read(&has_writer)) {
            break;
        }

        /*
         * Synchronize access with reader_count() in bdrv_graph_wrlock().
         * Case 1:
         * If this critical section gets executed first, reader_count will
         * decrease and the reader will go to sleep.
         * Then the writer will read reader_count that does not take into
         * account this reader, and if there's no other reader it will
         * enter the write section.
         * Case 2:
         * If reader_count() critical section gets executed first,
         * then writer will read reader_count >= 1.
         * It will wait in AIO_WAIT_WHILE(), but once it releases the lock
         * we will enter this critical section and call aio_wait_kick().
         */
        WITH_QEMU_LOCK_GUARD(&aio_context_list_lock) {
            /*
             * Additional check when we use the above lock to synchronize
             * with bdrv_graph_wrunlock().
             * Case 1:
             * If this gets executed first, has_writer is still 1, so we reduce
             * reader_count and go to sleep.
             * Then the writer will set has_writer to 0 and wake up all readers,
             * us included.
             * Case 2:
             * If bdrv_graph_wrunlock() critical section gets executed first,
             * then it will set has_writer to 0 and wake up all other readers.
             * Then we execute this critical section, and therefore must check
             * again for has_writer, otherwise we sleep without any writer
             * actually running.
             */
            if (!qatomic_read(&has_writer)) {
                return;
            }

            /* slow path where reader sleeps */
            bdrv_graph->reader_count--;
            aio_wait_kick();
            qemu_co_queue_wait(&reader_queue, &aio_context_list_lock);
        }
    }
#endif
}

void coroutine_fn bdrv_graph_co_rdunlock(void)
{
#if 0
    BdrvGraphRWlock *bdrv_graph;
    bdrv_graph = qemu_get_current_aio_context()->bdrv_graph;

    qatomic_store_release(&bdrv_graph->reader_count,
                          bdrv_graph->reader_count - 1);
    /* make sure writer sees reader_count before we check has_writer */
    smp_mb();

    /*
     * has_writer == 0: this means reader will read reader_count decreased
     * has_writer == 1: we don't know if writer read reader_count old or
     *                  new. Therefore, kick again so on next iteration
     *                  writer will for sure read the updated value.
     */
    if (qatomic_read(&has_writer)) {
        aio_wait_kick();
    }
#endif
}

void bdrv_graph_rdlock_main_loop(void)
{
    GLOBAL_STATE_CODE();
    assert(!qemu_in_coroutine());
}

void bdrv_graph_rdunlock_main_loop(void)
{
    GLOBAL_STATE_CODE();
    assert(!qemu_in_coroutine());
}

void assert_bdrv_graph_readable(void)
{
    /* reader_count() is slow due to aio_context_list_lock lock contention */
    /* TODO Reenable when wrlock is reenabled */
#if 0
#ifdef CONFIG_DEBUG_GRAPH_LOCK
    assert(qemu_in_main_thread() || reader_count());
#endif
#endif
}

void assert_bdrv_graph_writable(void)
{
    assert(qemu_in_main_thread());
    /* TODO Reenable when wrlock is reenabled */
#if 0
    assert(qatomic_read(&has_writer));
#endif
}
