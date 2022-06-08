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

uint32_t arm_ldl_ptw(CPUState *cs, hwaddr addr, bool is_secure,
                     ARMMMUIdx mmu_idx, ARMMMUFaultInfo *fi);
uint64_t arm_ldq_ptw(CPUState *cs, hwaddr addr, bool is_secure,
                     ARMMMUIdx mmu_idx, ARMMMUFaultInfo *fi);

bool regime_is_user(CPUARMState *env, ARMMMUIdx mmu_idx);
bool regime_translation_disabled(CPUARMState *env, ARMMMUIdx mmu_idx);
ARMCacheAttrs combine_cacheattrs(CPUARMState *env,
                                 ARMCacheAttrs s1, ARMCacheAttrs s2);

bool get_level1_table_address(CPUARMState *env, ARMMMUIdx mmu_idx,
                              uint32_t *table, uint32_t address);
int ap_to_rw_prot(CPUARMState *env, ARMMMUIdx mmu_idx,
                  int ap, int domain_prot);
int simple_ap_to_rw_prot_is_user(int ap, bool is_user);

static inline int
simple_ap_to_rw_prot(CPUARMState *env, ARMMMUIdx mmu_idx, int ap)
{
    return simple_ap_to_rw_prot_is_user(ap, regime_is_user(env, mmu_idx));
}

void get_phys_addr_pmsav7_default(CPUARMState *env,
                                  ARMMMUIdx mmu_idx,
                                  int32_t address, int *prot);
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
