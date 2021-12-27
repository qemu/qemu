/*
 * Generic prctl unalign functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef GENERIC_TARGET_PRCTL_UNALIGN_H
#define GENERIC_TARGET_PRCTL_UNALIGN_H

static abi_long do_prctl_get_unalign(CPUArchState *env, target_long arg2)
{
    CPUState *cs = env_cpu(env);
    uint32_t res = PR_UNALIGN_NOPRINT;
    if (cs->prctl_unalign_sigbus) {
        res |= PR_UNALIGN_SIGBUS;
    }
    return put_user_u32(res, arg2);
}
#define do_prctl_get_unalign do_prctl_get_unalign

static abi_long do_prctl_set_unalign(CPUArchState *env, target_long arg2)
{
    env_cpu(env)->prctl_unalign_sigbus = arg2 & PR_UNALIGN_SIGBUS;
    return 0;
}
#define do_prctl_set_unalign do_prctl_set_unalign

#endif /* GENERIC_TARGET_PRCTL_UNALIGN_H */
