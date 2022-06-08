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

bool regime_translation_disabled(CPUARMState *env, ARMMMUIdx mmu_idx);

#endif /* !CONFIG_USER_ONLY */
#endif /* TARGET_ARM_PTW_H */
