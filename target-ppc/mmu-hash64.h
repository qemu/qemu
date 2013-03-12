#if !defined (__MMU_HASH64_H__)
#define __MMU_HASH64_H__

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64
void dump_slb(FILE *f, fprintf_function cpu_fprintf, CPUPPCState *env);
int ppc_store_slb (CPUPPCState *env, target_ulong rb, target_ulong rs);
hwaddr ppc_hash64_get_phys_page_debug(CPUPPCState *env, target_ulong addr);
int ppc_hash64_handle_mmu_fault(CPUPPCState *env, target_ulong address, int rw,
                                int mmu_idx);
#endif

#endif /* CONFIG_USER_ONLY */

#endif /* !defined (__MMU_HASH64_H__) */
