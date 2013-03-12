#if !defined (__MMU_HASH32_H__)
#define __MMU_HASH32_H__

#ifndef CONFIG_USER_ONLY

int pte32_is_valid(target_ulong pte0);
int find_pte32(CPUPPCState *env, mmu_ctx_t *ctx, int h,
               int rw, int type, int target_page_bits);

#endif /* CONFIG_USER_ONLY */

#endif /* __MMU_HASH32_H__ */
