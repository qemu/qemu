/*
 * ARM page table walking.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_ARM_PTW_H
#define TARGET_ARM_PTW_H

#ifndef CONFIG_USER_ONLY

bool regime_is_user(CPUARMState *env, ARMMMUIdx mmu_idx);
bool regime_translation_disabled(CPUARMState *env, ARMMMUIdx mmu_idx);
ARMCacheAttrs combine_cacheattrs(CPUARMState *env,
                                 ARMCacheAttrs s1, ARMCacheAttrs s2);

bool get_phys_addr_v5(CPUARMState *env, uint32_t address,
                      MMUAccessType access_type, ARMMMUIdx mmu_idx,
                      hwaddr *phys_ptr, int *prot,
                      target_ulong *page_size,
                      ARMMMUFaultInfo *fi);
bool get_phys_addr_pmsav5(CPUARMState *env, uint32_t address,
                          MMUAccessType access_type, ARMMMUIdx mmu_idx,
                          hwaddr *phys_ptr, int *prot,
                          ARMMMUFaultInfo *fi);
bool get_phys_addr_v6(CPUARMState *env, uint32_t address,
                      MMUAccessType access_type, ARMMMUIdx mmu_idx,
                      hwaddr *phys_ptr, MemTxAttrs *attrs, int *prot,
                      target_ulong *page_size, ARMMMUFaultInfo *fi);
bool get_phys_addr_pmsav7(CPUARMState *env, uint32_t address,
                          MMUAccessType access_type, ARMMMUIdx mmu_idx,
                          hwaddr *phys_ptr, int *prot,
                          target_ulong *page_size,
                          ARMMMUFaultInfo *fi);
bool get_phys_addr_pmsav8(CPUARMState *env, uint32_t address,
                          MMUAccessType access_type, ARMMMUIdx mmu_idx,
                          hwaddr *phys_ptr, MemTxAttrs *txattrs,
                          int *prot, target_ulong *page_size,
                          ARMMMUFaultInfo *fi);
bool get_phys_addr_lpae(CPUARMState *env, uint64_t address,
                        MMUAccessType access_type, ARMMMUIdx mmu_idx,
                        bool s1_is_el0,
                        hwaddr *phys_ptr, MemTxAttrs *txattrs, int *prot,
                        target_ulong *page_size_ptr,
                        ARMMMUFaultInfo *fi, ARMCacheAttrs *cacheattrs)
    __attribute__((nonnull));

#endif /* !CONFIG_USER_ONLY */
#endif /* TARGET_ARM_PTW_H */
