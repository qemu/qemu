/*
 *  Software MMU support
 *
 * Declare helpers used by TCG for qemu_ld/st ops.
 *
 * Used by softmmu_exec.h, TCG targets and exec-all.h.
 *
 */
#ifndef SOFTMMU_DEFS_H
#define SOFTMMU_DEFS_H

uint8_t helper_ret_ldb_mmu(CPUArchState *env, target_ulong addr,
                           int mmu_idx, uintptr_t retaddr);
uint16_t helper_ret_ldw_mmu(CPUArchState *env, target_ulong addr,
                            int mmu_idx, uintptr_t retaddr);
uint32_t helper_ret_ldl_mmu(CPUArchState *env, target_ulong addr,
                            int mmu_idx, uintptr_t retaddr);
uint64_t helper_ret_ldq_mmu(CPUArchState *env, target_ulong addr,
                            int mmu_idx, uintptr_t retaddr);

void helper_ret_stb_mmu(CPUArchState *env, target_ulong addr, uint8_t val,
                        int mmu_idx, uintptr_t retaddr);
void helper_ret_stw_mmu(CPUArchState *env, target_ulong addr, uint16_t val,
                        int mmu_idx, uintptr_t retaddr);
void helper_ret_stl_mmu(CPUArchState *env, target_ulong addr, uint32_t val,
                        int mmu_idx, uintptr_t retaddr);
void helper_ret_stq_mmu(CPUArchState *env, target_ulong addr, uint64_t val,
                        int mmu_idx, uintptr_t retaddr);

uint8_t helper_ldb_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint16_t helper_ldw_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint32_t helper_ldl_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint64_t helper_ldq_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);

void helper_stb_mmu(CPUArchState *env, target_ulong addr,
                    uint8_t val, int mmu_idx);
void helper_stw_mmu(CPUArchState *env, target_ulong addr,
                    uint16_t val, int mmu_idx);
void helper_stl_mmu(CPUArchState *env, target_ulong addr,
                    uint32_t val, int mmu_idx);
void helper_stq_mmu(CPUArchState *env, target_ulong addr,
                    uint64_t val, int mmu_idx);

uint8_t helper_ldb_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint16_t helper_ldw_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint32_t helper_ldl_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint64_t helper_ldq_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);

#endif /* SOFTMMU_DEFS_H */
