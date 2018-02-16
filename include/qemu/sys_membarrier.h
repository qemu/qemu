/*
 * Process-global memory barriers
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 */

#ifndef QEMU_SYS_MEMBARRIER_H
#define QEMU_SYS_MEMBARRIER_H 1

/* Keep it simple, execute a real memory barrier on both sides.  */
static inline void smp_mb_global_init(void) {}
#define smp_mb_global()            smp_mb()
#define smp_mb_placeholder()       smp_mb()

#endif
