/*
 * AArch64 gcs functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef AARCH64_GCS_INTERNAL_H
#define AARCH64_GCS_INTERNAL_H

#ifndef PR_SHADOW_STACK_ENABLE
# define PR_SHADOW_STACK_ENABLE  (1U << 0)
# define PR_SHADOW_STACK_WRITE   (1U << 1)
# define PR_SHADOW_STACK_PUSH    (1U << 2)
#endif

static inline uint64_t gcs_get_el0_mode(CPUArchState *env)
{
    uint64_t cr = env->cp15.gcscr_el[0];
    abi_ulong flags = 0;

    flags |= cr & GCSCR_PCRSEL ? PR_SHADOW_STACK_ENABLE : 0;
    flags |= cr & GCSCR_STREN ? PR_SHADOW_STACK_WRITE : 0;
    flags |= cr & GCSCR_PUSHMEN ? PR_SHADOW_STACK_PUSH : 0;

    return flags;
}

static inline void gcs_set_el0_mode(CPUArchState *env, uint64_t flags)
{
    uint64_t cr = GCSCRE0_NTR;

    cr |= flags & PR_SHADOW_STACK_ENABLE ? GCSCR_RVCHKEN | GCSCR_PCRSEL : 0;
    cr |= flags & PR_SHADOW_STACK_WRITE ? GCSCR_STREN : 0;
    cr |= flags & PR_SHADOW_STACK_PUSH ? GCSCR_PUSHMEN : 0;

    env->cp15.gcscr_el[0] = cr;
}

#endif
