/*
 * Common qemu-thread implementation header file.
 *
 * Copyright Red Hat, Inc. 2018
 *
 * Authors:
 *  Peter Xu <peterx@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_THREAD_COMMON_H
#define QEMU_THREAD_COMMON_H

#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include "trace.h"

static inline void qemu_mutex_post_init(QemuMutex *mutex)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = NULL;
    mutex->line = 0;
#endif
    mutex->initialized = true;
}

static inline void qemu_mutex_pre_lock(QemuMutex *mutex,
                                       const char *file, int line)
{
    trace_qemu_mutex_lock(mutex, file, line);
}

static inline void qemu_mutex_post_lock(QemuMutex *mutex,
                                        const char *file, int line)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = file;
    mutex->line = line;
#endif
    trace_qemu_mutex_locked(mutex, file, line);
    if (mutex_is_bql(mutex)) {
        bql_update_status(true);
    }
}

static inline void qemu_mutex_pre_unlock(QemuMutex *mutex,
                                         const char *file, int line)
{
#ifdef CONFIG_DEBUG_MUTEX
    mutex->file = NULL;
    mutex->line = 0;
#endif
    trace_qemu_mutex_unlock(mutex, file, line);
    if (mutex_is_bql(mutex)) {
        bql_update_status(false);
    }
}

#endif
