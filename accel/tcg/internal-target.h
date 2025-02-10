/*
 * Internal execution defines for qemu (target specific)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_INTERNAL_TARGET_H
#define ACCEL_TCG_INTERNAL_TARGET_H

#include "exec/exec-all.h"
#include "exec/translation-block.h"
#include "tb-internal.h"
#include "tcg-target-mo.h"

/*
 * Access to the various translations structures need to be serialised
 * via locks for consistency.  In user-mode emulation access to the
 * memory related structures are protected with mmap_lock.
 * In !user-mode we use per-page locks.
 */
#ifdef CONFIG_USER_ONLY
#define assert_memory_lock() tcg_debug_assert(have_mmap_lock())
#else
#define assert_memory_lock()
#endif

#if defined(CONFIG_SOFTMMU) && defined(CONFIG_DEBUG_TCG)
void assert_no_pages_locked(void);
#else
static inline void assert_no_pages_locked(void) { }
#endif

#ifdef CONFIG_USER_ONLY
static inline void page_table_config_init(void) { }
#else
void page_table_config_init(void);
#endif

#ifndef CONFIG_USER_ONLY
G_NORETURN void cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
#endif /* CONFIG_USER_ONLY */

/**
 * tcg_req_mo:
 * @type: TCGBar
 *
 * Filter @type to the barrier that is required for the guest
 * memory ordering vs the host memory ordering.  A non-zero
 * result indicates that some barrier is required.
 *
 * If TCG_GUEST_DEFAULT_MO is not defined, assume that the
 * guest requires strict ordering.
 *
 * This is a macro so that it's constant even without optimization.
 */
#ifdef TCG_GUEST_DEFAULT_MO
# define tcg_req_mo(type) \
    ((type) & TCG_GUEST_DEFAULT_MO & ~TCG_TARGET_DEFAULT_MO)
#else
# define tcg_req_mo(type) ((type) & ~TCG_TARGET_DEFAULT_MO)
#endif

/**
 * cpu_req_mo:
 * @type: TCGBar
 *
 * If tcg_req_mo indicates a barrier for @type is required
 * for the guest memory model, issue a host memory barrier.
 */
#define cpu_req_mo(type)          \
    do {                          \
        if (tcg_req_mo(type)) {   \
            smp_mb();             \
        }                         \
    } while (0)

#endif /* ACCEL_TCG_INTERNAL_H */
