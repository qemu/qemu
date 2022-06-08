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
uint64_t regime_ttbr(CPUARMState *env, ARMMMUIdx mmu_idx, int ttbrn);

int ap_to_rw_prot(CPUARMState *env, ARMMMUIdx mmu_idx,
                  int ap, int domain_prot);
int simple_ap_to_rw_prot_is_user(int ap, bool is_user);

static inline int
simple_ap_to_rw_prot(CPUARMState *env, ARMMMUIdx mmu_idx, int ap)
{
    return simple_ap_to_rw_prot_is_user(ap, regime_is_user(env, mmu_idx));
}

ARMVAParameters aa32_va_parameters(CPUARMState *env, uint32_t va,
                                   ARMMMUIdx mmu_idx);

#endif /* !CONFIG_USER_ONLY */
#endif /* TARGET_ARM_PTW_H */
