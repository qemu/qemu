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

uint8_t REGPARM __ldb_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stb_mmu(target_ulong addr, uint8_t val, int mmu_idx);
uint16_t REGPARM __ldw_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stw_mmu(target_ulong addr, uint16_t val, int mmu_idx);
uint32_t REGPARM __ldl_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stl_mmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t REGPARM __ldq_mmu(target_ulong addr, int mmu_idx);
void REGPARM __stq_mmu(target_ulong addr, uint64_t val, int mmu_idx);

uint8_t REGPARM __ldb_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stb_cmmu(target_ulong addr, uint8_t val, int mmu_idx);
uint16_t REGPARM __ldw_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stw_cmmu(target_ulong addr, uint16_t val, int mmu_idx);
uint32_t REGPARM __ldl_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stl_cmmu(target_ulong addr, uint32_t val, int mmu_idx);
uint64_t REGPARM __ldq_cmmu(target_ulong addr, int mmu_idx);
void REGPARM __stq_cmmu(target_ulong addr, uint64_t val, int mmu_idx);

#endif
