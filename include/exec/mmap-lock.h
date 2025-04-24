/*
 * QEMU user-only mmap lock, with stubs for system mode
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef EXEC_MMAP_LOCK_H
#define EXEC_MMAP_LOCK_H

#ifdef CONFIG_USER_ONLY

void TSA_NO_TSA mmap_lock(void);
void TSA_NO_TSA mmap_unlock(void);
bool have_mmap_lock(void);

static inline void mmap_unlock_guard(void *unused)
{
    mmap_unlock();
}

#define WITH_MMAP_LOCK_GUARD() \
    for (int _mmap_lock_iter __attribute__((cleanup(mmap_unlock_guard))) \
         = (mmap_lock(), 0); _mmap_lock_iter == 0; _mmap_lock_iter = 1)

#else

static inline void mmap_lock(void) {}
static inline void mmap_unlock(void) {}
#define WITH_MMAP_LOCK_GUARD()

#endif /* CONFIG_USER_ONLY */
#endif /* EXEC_MMAP_LOCK_H */
