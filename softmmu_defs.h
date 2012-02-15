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

uint8_t __ldb_mmu(target_ulong addr, int mmu_idx);
void __stb_mmu(target_ulong addr, uint8_t val, int mmu_idx);
uint16_t __ldw_mmu(target_ulong addr, int mmu_idx);
void __stw_mmu(target_ulong addr, uint16_t val, int mmu_idx);
uint32_t __ldl_mmu(target_ulong addr, int mmu_idx);
void __stl_mmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t __ldq_mmu(target_ulong addr, int mmu_idx);
void __stq_mmu(target_ulong addr, uint64_t val, int mmu_idx);

uint8_t __ldb_cmmu(target_ulong addr, int mmu_idx);
void __stb_cmmu(target_ulong addr, uint8_t val, int mmu_idx);
uint16_t __ldw_cmmu(target_ulong addr, int mmu_idx);
void __stw_cmmu(target_ulong addr, uint16_t val, int mmu_idx);
uint32_t __ldl_cmmu(target_ulong addr, int mmu_idx);
void __stl_cmmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t __ldq_cmmu(target_ulong addr, int mmu_idx);
void __stq_cmmu(target_ulong addr, uint64_t val, int mmu_idx);

#endif
