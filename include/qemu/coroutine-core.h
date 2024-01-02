/*
 * QEMU coroutine implementation
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *  Kevin Wolf         <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_COROUTINE_CORE_H
#define QEMU_COROUTINE_CORE_H

/**
 * Coroutines are a mechanism for stack switching and can be used for
 * cooperative userspace threading.  These functions provide a simple but
 * useful flavor of coroutines that is suitable for writing sequential code,
 * rather than callbacks, for operations that need to give up control while
 * waiting for events to complete.
 *
 * These functions are re-entrant and may be used outside the BQL.
 *
 * Functions that execute in coroutine context cannot be called
 * directly from normal functions.  Use @coroutine_fn to mark such
 * functions.  For example:
 *
 *   static void coroutine_fn foo(void) {
 *       ....
 *   }
 *
 * In the future it would be nice to have the compiler or a static
 * checker catch misuse of such functions.  This annotation might make
 * it possible and in the meantime it serves as documentation.
 */

/**
 * Mark a function that executes in coroutine context
 *
 *
 * Functions that execute in coroutine context cannot be called
 * directly from normal functions.  Use @coroutine_fn to mark such
 * functions.  For example:
 *
 *   static void coroutine_fn foo(void) {
 *       ....
 *   }
 *
 * In the future it would be nice to have the compiler or a static
 * checker catch misuse of such functions.  This annotation might make
 * it possible and in the meantime it serves as documentation.
 */

typedef struct Coroutine Coroutine;
typedef struct CoMutex CoMutex;

/**
 * Coroutine entry point
 *
 * When the coroutine is entered for the first time, opaque is passed in as an
 * argument.
 *
 * When this function returns, the coroutine is destroyed automatically and
 * execution continues in the caller who last entered the coroutine.
 */
typedef void coroutine_fn CoroutineEntry(void *opaque);

/**
 * Create a new coroutine
 *
 * Use qemu_coroutine_enter() to actually transfer control to the coroutine.
 * The opaque argument is passed as the argument to the entry point.
 */
Coroutine *qemu_coroutine_create(CoroutineEntry *entry, void *opaque);

/**
 * Transfer control to a coroutine
 */
void qemu_coroutine_enter(Coroutine *coroutine);

/**
 * Transfer control to a coroutine if it's not active (i.e. part of the call
 * stack of the running coroutine). Otherwise, do nothing.
 */
void qemu_coroutine_enter_if_inactive(Coroutine *co);

/**
 * Transfer control to a coroutine and associate it with ctx
 */
void qemu_aio_coroutine_enter(AioContext *ctx, Coroutine *co);

/**
 * Transfer control back to a coroutine's caller
 *
 * This function does not return until the coroutine is re-entered using
 * qemu_coroutine_enter().
 */
void coroutine_fn qemu_coroutine_yield(void);

/**
 * Get the AioContext of the given coroutine
 */
AioContext *qemu_coroutine_get_aio_context(Coroutine *co);

/**
 * Get the currently executing coroutine
 */
Coroutine *qemu_coroutine_self(void);

/**
 * Return whether or not currently inside a coroutine
 *
 * This can be used to write functions that work both when in coroutine context
 * and when not in coroutine context.  Note that such functions cannot use the
 * coroutine_fn annotation since they work outside coroutine context.
 */
bool qemu_in_coroutine(void);

/**
 * Return true if the coroutine is currently entered
 *
 * A coroutine is "entered" if it has not yielded from the current
 * qemu_coroutine_enter() call used to run it.  This does not mean that the
 * coroutine is currently executing code since it may have transferred control
 * to another coroutine using qemu_coroutine_enter().
 *
 * When several coroutines enter each other there may be no way to know which
 * ones have already been entered.  In such situations this function can be
 * used to avoid recursively entering coroutines.
 */
bool qemu_coroutine_entered(Coroutine *co);

/**
 * Initialises a CoMutex. This must be called before any other operation is used
 * on the CoMutex.
 */
void qemu_co_mutex_init(CoMutex *mutex);

/**
 * Locks the mutex. If the lock cannot be taken immediately, control is
 * transferred to the caller of the current coroutine.
 */
void coroutine_fn qemu_co_mutex_lock(CoMutex *mutex);

/**
 * Unlocks the mutex and schedules the next coroutine that was waiting for this
 * lock to be run.
 */
void coroutine_fn qemu_co_mutex_unlock(CoMutex *mutex);

#endif
