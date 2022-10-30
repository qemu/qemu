/*
 * QEMU Thread Context
 *
 * Copyright Red Hat Inc., 2022
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef SYSEMU_THREAD_CONTEXT_H
#define SYSEMU_THREAD_CONTEXT_H

#include "qapi/qapi-types-machine.h"
#include "qemu/thread.h"
#include "qom/object.h"

#define TYPE_THREAD_CONTEXT "thread-context"
OBJECT_DECLARE_TYPE(ThreadContext, ThreadContextClass,
                    THREAD_CONTEXT)

struct ThreadContextClass {
    ObjectClass parent_class;
};

struct ThreadContext {
    /* private */
    Object parent;

    /* private */
    unsigned int thread_id;
    QemuThread thread;

    /* Semaphore to wait for context thread action. */
    QemuSemaphore sem;
    /* Semaphore to wait for action in context thread. */
    QemuSemaphore sem_thread;
    /* Mutex to synchronize requests. */
    QemuMutex mutex;

    /* Commands for the thread to execute. */
    int thread_cmd;
    void *thread_cmd_data;

    /* CPU affinity bitmap used for initialization. */
    unsigned long *init_cpu_bitmap;
    int init_cpu_nbits;
};

void thread_context_create_thread(ThreadContext *tc, QemuThread *thread,
                                  const char *name,
                                  void *(*start_routine)(void *), void *arg,
                                  int mode);

#endif /* SYSEMU_THREAD_CONTEXT_H */
