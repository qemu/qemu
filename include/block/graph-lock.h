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
#ifndef GRAPH_LOCK_H
#define GRAPH_LOCK_H

#include "qemu/osdep.h"
#include "qemu/clang-tsa.h"

#include "qemu/coroutine.h"

/**
 * Graph Lock API
 * This API provides a rwlock used to protect block layer
 * graph modifications like edge (BdrvChild) and node (BlockDriverState)
 * addition and removal.
 * Currently we have 1 writer only, the Main loop, and many
 * readers, mostly coroutines running in other AioContext thus other threads.
 *
 * We distinguish between writer (main loop, under BQL) that modifies the
 * graph, and readers (all other coroutines running in various AioContext),
 * that go through the graph edges, reading
 * BlockDriverState ->parents and->children.
 *
 * The writer (main loop)  has an "exclusive" access, so it first waits for
 * current read to finish, and then prevents incoming ones from
 * entering while it has the exclusive access.
 *
 * The readers (coroutines in multiple AioContext) are free to
 * access the graph as long the writer is not modifying the graph.
 * In case it is, they go in a CoQueue and sleep until the writer
 * is done.
 *
 * If a coroutine changes AioContext, the counter in the original and new
 * AioContext are left intact, since the writer does not care where is the
 * reader, but only if there is one.
 * As a result, some AioContexts might have a negative reader count, to
 * balance the positive count of the AioContext that took the lock.
 * This also means that when an AioContext is deleted it may have a nonzero
 * reader count. In that case we transfer the count to a global shared counter
 * so that the writer is always aware of all readers.
 */
typedef struct BdrvGraphRWlock BdrvGraphRWlock;

/* Dummy lock object to use for Thread Safety Analysis (TSA) */
typedef struct TSA_CAPABILITY("mutex") BdrvGraphLock {
} BdrvGraphLock;

extern BdrvGraphLock graph_lock;

/*
 * clang doesn't check consistency in locking annotations between forward
 * declarations and the function definition. Having the annotation on the
 * definition, but not the declaration in a header file, may give the reader
 * a false sense of security because the condition actually remains unchecked
 * for callers in other source files.
 *
 * Therefore, as a convention, for public functions, GRAPH_RDLOCK and
 * GRAPH_WRLOCK annotations should be present only in the header file.
 */
#define GRAPH_WRLOCK TSA_REQUIRES(graph_lock)
#define GRAPH_RDLOCK TSA_REQUIRES_SHARED(graph_lock)

/*
 * TSA annotations are not part of function types, so checks are defeated when
 * using a function pointer. As a workaround, annotate function pointers with
 * this macro that will require that the lock is at least taken while reading
 * the pointer. In most cases this is equivalent to actually protecting the
 * function call.
 */
#define GRAPH_RDLOCK_PTR TSA_GUARDED_BY(graph_lock)
#define GRAPH_WRLOCK_PTR TSA_GUARDED_BY(graph_lock)

/*
 * register_aiocontext:
 * Add AioContext @ctx to the list of AioContext.
 * This list is used to obtain the total number of readers
 * currently running the graph.
 */
void register_aiocontext(AioContext *ctx);

/*
 * unregister_aiocontext:
 * Removes AioContext @ctx to the list of AioContext.
 */
void unregister_aiocontext(AioContext *ctx);

/*
 * bdrv_graph_wrlock:
 * Start an exclusive write operation to modify the graph. This means we are
 * adding or removing an edge or a node in the block layer graph. Nobody else
 * is allowed to access the graph.
 *
 * Must only be called from outside bdrv_graph_co_rdlock.
 *
 * The wrlock can only be taken from the main loop, with BQL held, as only the
 * main loop is allowed to modify the graph.
 *
 * This function polls. Callers must not hold the lock of any AioContext other
 * than the current one.
 */
void bdrv_graph_wrlock(void) TSA_ACQUIRE(graph_lock) TSA_NO_TSA;

/*
 * bdrv_graph_wrunlock:
 * Write finished, reset global has_writer to 0 and restart
 * all readers that are waiting.
 */
void bdrv_graph_wrunlock(void) TSA_RELEASE(graph_lock) TSA_NO_TSA;

/*
 * bdrv_graph_co_rdlock:
 * Read the bs graph. This usually means traversing all nodes in
 * the graph, therefore it can't happen while another thread is
 * modifying it.
 * Increases the reader counter of the current aiocontext,
 * and if has_writer is set, it means that the writer is modifying
 * the graph, therefore wait in a coroutine queue.
 * The writer will then wake this coroutine once it is done.
 *
 * This lock should be taken from Iothreads (IO_CODE() class of functions)
 * because it signals the writer that there are some
 * readers currently running, or waits until the current
 * write is finished before continuing.
 * Calling this function from the Main Loop with BQL held
 * is not necessary, since the Main Loop itself is the only
 * writer, thus won't be able to read and write at the same time.
 * The only exception to that is when we can't take the lock in the
 * function/coroutine itself, and need to delegate the caller (usually main
 * loop) to take it and wait that the coroutine ends, so that
 * we always signal that a reader is running.
 */
void coroutine_fn TSA_ACQUIRE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_co_rdlock(void);

/*
 * bdrv_graph_rdunlock:
 * Read terminated, decrease the count of readers in the current aiocontext.
 * If the writer is waiting for reads to finish (has_writer == 1), signal
 * the writer that we are done via aio_wait_kick() to let it continue.
 */
void coroutine_fn TSA_RELEASE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_co_rdunlock(void);

/*
 * bdrv_graph_rd{un}lock_main_loop:
 * Just a placeholder to mark where the graph rdlock should be taken
 * in the main loop. It is just asserting that we are not
 * in a coroutine and in GLOBAL_STATE_CODE.
 */
void TSA_ACQUIRE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_rdlock_main_loop(void);

void TSA_RELEASE_SHARED(graph_lock) TSA_NO_TSA
bdrv_graph_rdunlock_main_loop(void);

/*
 * assert_bdrv_graph_readable:
 * Make sure that the reader is either the main loop,
 * or there is at least a reader helding the rdlock.
 * In this way an incoming writer is aware of the read and waits.
 */
void GRAPH_RDLOCK assert_bdrv_graph_readable(void);

/*
 * assert_bdrv_graph_writable:
 * Make sure that the writer is the main loop and has set @has_writer,
 * so that incoming readers will pause.
 */
void GRAPH_WRLOCK assert_bdrv_graph_writable(void);

/*
 * Calling this function tells TSA that we know that the lock is effectively
 * taken even though we cannot prove it (yet) with GRAPH_RDLOCK. This can be
 * useful in intermediate stages of a conversion to using the GRAPH_RDLOCK
 * macro.
 */
static inline void TSA_ASSERT_SHARED(graph_lock) TSA_NO_TSA
assume_graph_lock(void)
{
}

typedef struct GraphLockable { } GraphLockable;

/*
 * In C, compound literals have the lifetime of an automatic variable.
 * In C++ it would be different, but then C++ wouldn't need QemuLockable
 * either...
 */
#define GML_OBJ_() (&(GraphLockable) { })

/*
 * This is not marked as TSA_ACQUIRE() because TSA doesn't understand the
 * cleanup attribute and would therefore complain that the graph is never
 * unlocked. TSA_ASSERT() makes sure that the following calls know that we
 * hold the lock while unlocking is left unchecked.
 */
static inline GraphLockable * TSA_ASSERT(graph_lock) TSA_NO_TSA
graph_lockable_auto_lock(GraphLockable *x)
{
    bdrv_graph_co_rdlock();
    return x;
}

static inline void TSA_NO_TSA
graph_lockable_auto_unlock(GraphLockable *x)
{
    bdrv_graph_co_rdunlock();
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GraphLockable, graph_lockable_auto_unlock)

#define WITH_GRAPH_RDLOCK_GUARD_(var)                                         \
    for (g_autoptr(GraphLockable) var = graph_lockable_auto_lock(GML_OBJ_()); \
         var;                                                                 \
         graph_lockable_auto_unlock(var), var = NULL)

#define WITH_GRAPH_RDLOCK_GUARD() \
    WITH_GRAPH_RDLOCK_GUARD_(glue(graph_lockable_auto, __COUNTER__))

#define GRAPH_RDLOCK_GUARD(x)                                       \
    g_autoptr(GraphLockable)                                        \
    glue(graph_lockable_auto, __COUNTER__) G_GNUC_UNUSED =          \
            graph_lockable_auto_lock(GML_OBJ_())


typedef struct GraphLockableMainloop { } GraphLockableMainloop;

/*
 * In C, compound literals have the lifetime of an automatic variable.
 * In C++ it would be different, but then C++ wouldn't need QemuLockable
 * either...
 */
#define GMLML_OBJ_() (&(GraphLockableMainloop) { })

/*
 * This is not marked as TSA_ACQUIRE() because TSA doesn't understand the
 * cleanup attribute and would therefore complain that the graph is never
 * unlocked. TSA_ASSERT() makes sure that the following calls know that we
 * hold the lock while unlocking is left unchecked.
 */
static inline GraphLockableMainloop * TSA_ASSERT(graph_lock) TSA_NO_TSA
graph_lockable_auto_lock_mainloop(GraphLockableMainloop *x)
{
    bdrv_graph_rdlock_main_loop();
    return x;
}

static inline void TSA_NO_TSA
graph_lockable_auto_unlock_mainloop(GraphLockableMainloop *x)
{
    bdrv_graph_rdunlock_main_loop();
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GraphLockableMainloop,
                              graph_lockable_auto_unlock_mainloop)

#define GRAPH_RDLOCK_GUARD_MAINLOOP(x)                              \
    g_autoptr(GraphLockableMainloop)                                \
    glue(graph_lockable_auto, __COUNTER__) G_GNUC_UNUSED =          \
            graph_lockable_auto_lock_mainloop(GMLML_OBJ_())

#endif /* GRAPH_LOCK_H */

