#if !defined (__MMU_HASH32_H__)
#define __MMU_HASH32_H__

#ifndef CONFIG_USER_ONLY

int pte32_is_valid(target_ulong pte0);
int get_segment32(CPUPPCState *env, mmu_ctx_t *ctx,
                  target_ulong eaddr, int rw, int type);

#endif /* CONFIG_USER_ONLY */

#endif /* __MMU_HASH32_H__ */
