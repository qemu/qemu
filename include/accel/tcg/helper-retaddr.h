/*
 * Get user helper pc for memory unwinding.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_HELPER_RETADDR_H
#define ACCEL_TCG_HELPER_RETADDR_H

/*
 * For user-only, helpers that use guest to host address translation
 * must protect the actual host memory access by recording 'retaddr'
 * for the signal handler.  This is required for a race condition in
 * which another thread unmaps the page between a probe and the
 * actual access.
 */
#ifdef CONFIG_USER_ONLY
extern __thread uintptr_t helper_retaddr;

static inline void set_helper_retaddr(uintptr_t ra)
{
    helper_retaddr = ra;
    /*
     * Ensure that this write is visible to the SIGSEGV handler that
     * may be invoked due to a subsequent invalid memory operation.
     */
    signal_barrier();
}

static inline void clear_helper_retaddr(void)
{
    /*
     * Ensure that previous memory operations have succeeded before
     * removing the data visible to the signal handler.
     */
    signal_barrier();
    helper_retaddr = 0;
}
#else
#define set_helper_retaddr(ra)   do { } while (0)
#define clear_helper_retaddr()   do { } while (0)
#endif

#endif /* ACCEL_TCG_HELPER_RETADDR_H */
