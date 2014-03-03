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

#define TYPE_IOTHREAD "iothread"

typedef struct IOThread IOThread;

#define IOTHREAD(obj) \
   OBJECT_CHECK(IOThread, obj, TYPE_IOTHREAD)

IOThread *iothread_find(const char *id);
char *iothread_get_id(IOThread *iothread);
AioContext *iothread_get_aio_context(IOThread *iothread);

#endif /* IOTHREAD_H */
