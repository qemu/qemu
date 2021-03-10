/*
 * Event loop thread implementation for unit tests
 *
 * Copyright Red Hat Inc., 2013, 2016
 *
 * Authors:
 *  Stefan Hajnoczi   <stefanha@redhat.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef TEST_IOTHREAD_H
#define TEST_IOTHREAD_H

#include "block/aio.h"
#include "qemu/thread.h"

typedef struct IOThread IOThread;

IOThread *iothread_new(void);
void iothread_join(IOThread *iothread);
AioContext *iothread_get_aio_context(IOThread *iothread);

#endif
