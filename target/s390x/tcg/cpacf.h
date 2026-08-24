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

/* from cpacf_aes.c */
int cpacf_aes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod);
int cpacf_aes_cbc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod);
int cpacf_aes_ctr(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint64_t *ctr_ptr_reg, uint32_t type,
                  uint8_t fc, uint8_t mod);
int cpacf_aes_pcc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint8_t fc);
int cpacf_aes_xts(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                  uint64_t param_addr, uint64_t *dst_ptr_reg,
                  uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                  uint32_t type, uint8_t fc, uint8_t mod);
int cpacf_aes_pckmo(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                    uint64_t param_addr, uint8_t fc);
int cpacf_paes_ecb(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint32_t type, uint8_t fc, uint8_t mod);
int cpacf_paes_cbc(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint32_t type, uint8_t fc, uint8_t mod);
int cpacf_paes_ctr(CPUS390XState *env, const int mmu_idx, uintptr_t ra,
                   uint64_t param_addr, uint64_t *dst_ptr_reg,
                   uint64_t *src_ptr_reg, uint64_t *src_len_reg,
                   uint64_t *ctr_ptr_reg, uint32_t type,
                   uint8_t fc, uint8_t mod);

#endif /* S390X_CPACF_H */
