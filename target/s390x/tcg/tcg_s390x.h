/*
 * QEMU TCG support -- s390x specific functions.
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TCG_S390X_H
#define TCG_S390X_H

void tcg_s390_tod_updated(CPUState *cs, run_on_cpu_data opaque);
G_NORETURN void tcg_s390_program_interrupt(CPUS390XState *env,
                                           uint32_t code, uintptr_t ra);
G_NORETURN void tcg_s390_data_exception(CPUS390XState *env, uint32_t dxc,
                                        uintptr_t ra);
G_NORETURN void tcg_s390_vector_exception(CPUS390XState *env, uint32_t vxc,
                                          uintptr_t ra);

#ifndef CONFIG_USER_ONLY
void s390_cpu_recompute_watchpoints(CPUState *cs);
void s390x_cpu_debug_excp_handler(CPUState *cs);
#endif

#endif /* TCG_S390X_H */
