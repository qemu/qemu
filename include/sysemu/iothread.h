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

#define TYPE_IOTHREAD "iothread"

typedef struct {
    Object parent_obj;

    QemuThread thread;
    AioContext *ctx;
    QemuMutex init_done_lock;
    QemuCond init_done_cond;    /* is thread initialization done? */
    bool stopping;
    int thread_id;
} IOThread;

#define IOTHREAD(obj) \
   OBJECT_CHECK(IOThread, obj, TYPE_IOTHREAD)

IOThread *iothread_find(const char *id);
char *iothread_get_id(IOThread *iothread);
AioContext *iothread_get_aio_context(IOThread *iothread);

#endif /* IOTHREAD_H */
