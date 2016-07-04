/*
 * 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Harsh Prateek Bora <harsh@linux.vnet.ibm.com>
 *  Venkateswararao Jujjuri(JV) <jvrao@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "block/thread-pool.h"
#include "qemu/coroutine.h"
#include "coth.h"

/* Called from QEMU I/O thread.  */
static void coroutine_enter_cb(void *opaque, int ret)
{
    Coroutine *co = opaque;
    qemu_coroutine_enter(co);
}

/* Called from worker thread.  */
static int coroutine_enter_func(void *arg)
{
    Coroutine *co = arg;
    qemu_coroutine_enter(co);
    return 0;
}

void co_run_in_worker_bh(void *opaque)
{
    Coroutine *co = opaque;
    thread_pool_submit_aio(aio_get_thread_pool(qemu_get_aio_context()),
                           coroutine_enter_func, co, coroutine_enter_cb, co);
}
