/*
 * Event loop thread
 *
 * Copyright Red Hat Inc., 2013
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef IOTHREAD_H
#define IOTHREAD_H

#include "block/aio.h"
#include "qemu/thread.h"
#include "qom/object.h"

#define TYPE_IOTHREAD "iothread"

struct IOThread {
    Object parent_obj;

    QemuThread thread;
    AioContext *ctx;
    bool run_gcontext;          /* whether we should run gcontext */
    GMainContext *worker_context;
    GMainLoop *main_loop;
    QemuSemaphore init_done_sem; /* is thread init done? */
    bool stopping;              /* has iothread_stop() been called? */
    bool running;               /* should iothread_run() continue? */
    int thread_id;

    /* AioContext poll parameters */
    int64_t poll_max_ns;
    int64_t poll_grow;
    int64_t poll_shrink;
};
typedef struct IOThread IOThread;

DECLARE_INSTANCE_CHECKER(IOThread, IOTHREAD,
                         TYPE_IOTHREAD)

char *iothread_get_id(IOThread *iothread);
IOThread *iothread_by_id(const char *id);
AioContext *iothread_get_aio_context(IOThread *iothread);
GMainContext *iothread_get_g_main_context(IOThread *iothread);

/*
 * Helpers used to allocate iothreads for internal use.  These
 * iothreads will not be seen by monitor clients when query using
 * "query-iothreads".
 */
IOThread *iothread_create(const char *id, Error **errp);
void iothread_stop(IOThread *iothread);
void iothread_destroy(IOThread *iothread);

/*
 * Returns true if executing withing IOThread context,
 * false otherwise.
 */
bool qemu_in_iothread(void);

#endif /* IOTHREAD_H */
