#if !defined (__MMU_HASH64_H__)
#define __MMU_HASH64_H__

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64
ppc_slb_t *slb_lookup(CPUPPCState *env, target_ulong eaddr);
void dump_slb(FILE *f, fprintf_function cpu_fprintf, CPUPPCState *env);
int ppc_store_slb (CPUPPCState *env, target_ulong rb, target_ulong rs);
int pte64_check(mmu_ctx_t *ctx, target_ulong pte0,
                target_ulong pte1, int h, int rw, int type);
#endif

#endif /* CONFIG_USER_ONLY */

#endif /* !defined (__MMU_HASH64_H__) */
