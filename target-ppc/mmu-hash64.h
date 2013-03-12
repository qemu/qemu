#if !defined (__MMU_HASH64_H__)
#define __MMU_HASH64_H__

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64
void dump_slb(FILE *f, fprintf_function cpu_fprintf, CPUPPCState *env);
int ppc_store_slb (CPUPPCState *env, target_ulong rb, target_ulong rs);
int get_segment64(CPUPPCState *env, mmu_ctx_t *ctx,
                  target_ulong eaddr, int rw, int type);
#endif

#endif /* CONFIG_USER_ONLY */

#endif /* !defined (__MMU_HASH64_H__) */
