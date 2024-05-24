#ifndef PPC_MMU_BOOKE_H
#define PPC_MMU_BOOKE_H

#include "cpu.h"

int ppcemb_tlb_search(CPUPPCState *env, target_ulong address, uint32_t pid);
int mmu40x_get_physical_address(CPUPPCState *env, hwaddr *raddr, int *prot,
                                target_ulong address,
                                MMUAccessType access_type);
hwaddr booke206_tlb_to_page_size(CPUPPCState *env, ppcmas_tlb_t *tlb);
int ppcmas_tlb_check(CPUPPCState *env, ppcmas_tlb_t *tlb, hwaddr *raddrp,
                     target_ulong address, uint32_t pid);
bool ppc_booke_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                     hwaddr *raddrp, int *psizep, int *protp, int mmu_idx,
                     bool guest_visible);

#endif
