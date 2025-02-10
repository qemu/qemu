/*
 * ARM MemTag convenience functions.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef AARCH64_MTE_USER_HELPER_H
#define AARCH64_MTE USER_HELPER_H

#include "user/abitypes.h"

#ifndef PR_MTE_TCF_SHIFT
# define PR_MTE_TCF_SHIFT       1
# define PR_MTE_TCF_NONE        (0UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_SYNC        (1UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_ASYNC       (2UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TCF_MASK        (3UL << PR_MTE_TCF_SHIFT)
# define PR_MTE_TAG_SHIFT       3
# define PR_MTE_TAG_MASK        (0xffffUL << PR_MTE_TAG_SHIFT)
#endif

/**
 * arm_set_mte_tcf0 - Set TCF0 field in SCTLR_EL1 register
 * @env: The CPU environment
 * @value: The value to be set for the Tag Check Fault in EL0 field.
 *
 * Only SYNC and ASYNC modes can be selected. If ASYMM mode is given, the SYNC
 * mode is selected instead. So, there is no way to set the ASYMM mode.
 */
void arm_set_mte_tcf0(CPUArchState *env, abi_long value);

#endif /* AARCH64_MTE_USER_HELPER_H */
