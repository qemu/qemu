/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * s390x cpacf
 *
 */

#ifndef S390X_CPACF_H
#define S390X_CPACF_H

/* from cpacf_sha256.c */
int cpacf_sha256(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                 uint64_t param_addr, uint64_t *message_reg, uint64_t *len_reg,
                 uint32_t type);

/* from cpacf_sha512.c */
int cpacf_sha512(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                 uint64_t param_addr, uint64_t *message_reg, uint64_t *len_reg,
                 uint32_t type);

#endif /* S390X_CPACF_H */
