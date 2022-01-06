/*
 * MIPS specific prctl functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef MIPS_TARGET_PRCTL_H
#define MIPS_TARGET_PRCTL_H

static abi_long do_prctl_get_fp_mode(CPUArchState *env)
{
    abi_long ret = 0;

    if (env->CP0_Status & (1 << CP0St_FR)) {
        ret |= PR_FP_MODE_FR;
    }
    if (env->CP0_Config5 & (1 << CP0C5_FRE)) {
        ret |= PR_FP_MODE_FRE;
    }
    return ret;
}
#define do_prctl_get_fp_mode do_prctl_get_fp_mode

static abi_long do_prctl_set_fp_mode(CPUArchState *env, abi_long arg2)
{
    bool old_fr = env->CP0_Status & (1 << CP0St_FR);
    bool old_fre = env->CP0_Config5 & (1 << CP0C5_FRE);
    bool new_fr = arg2 & PR_FP_MODE_FR;
    bool new_fre = arg2 & PR_FP_MODE_FRE;
    const unsigned int known_bits = PR_FP_MODE_FR | PR_FP_MODE_FRE;

    /* If nothing to change, return right away, successfully.  */
    if (old_fr == new_fr && old_fre == new_fre) {
        return 0;
    }
    /* Check the value is valid */
    if (arg2 & ~known_bits) {
        return -TARGET_EOPNOTSUPP;
    }
    /* Setting FRE without FR is not supported.  */
    if (new_fre && !new_fr) {
        return -TARGET_EOPNOTSUPP;
    }
    if (new_fr && !(env->active_fpu.fcr0 & (1 << FCR0_F64))) {
        /* FR1 is not supported */
        return -TARGET_EOPNOTSUPP;
    }
    if (!new_fr && (env->active_fpu.fcr0 & (1 << FCR0_F64))
        && !(env->CP0_Status_rw_bitmask & (1 << CP0St_FR))) {
        /* cannot set FR=0 */
        return -TARGET_EOPNOTSUPP;
    }
    if (new_fre && !(env->active_fpu.fcr0 & (1 << FCR0_FREP))) {
        /* Cannot set FRE=1 */
        return -TARGET_EOPNOTSUPP;
    }

    int i;
    fpr_t *fpr = env->active_fpu.fpr;
    for (i = 0; i < 32 ; i += 2) {
        if (!old_fr && new_fr) {
            fpr[i].w[!FP_ENDIAN_IDX] = fpr[i + 1].w[FP_ENDIAN_IDX];
        } else if (old_fr && !new_fr) {
            fpr[i + 1].w[FP_ENDIAN_IDX] = fpr[i].w[!FP_ENDIAN_IDX];
        }
    }

    if (new_fr) {
        env->CP0_Status |= (1 << CP0St_FR);
        env->hflags |= MIPS_HFLAG_F64;
    } else {
        env->CP0_Status &= ~(1 << CP0St_FR);
        env->hflags &= ~MIPS_HFLAG_F64;
    }
    if (new_fre) {
        env->CP0_Config5 |= (1 << CP0C5_FRE);
        if (env->active_fpu.fcr0 & (1 << FCR0_FREP)) {
            env->hflags |= MIPS_HFLAG_FRE;
        }
    } else {
        env->CP0_Config5 &= ~(1 << CP0C5_FRE);
        env->hflags &= ~MIPS_HFLAG_FRE;
    }

    return 0;
}
#define do_prctl_set_fp_mode do_prctl_set_fp_mode

#endif /* MIPS_TARGET_PRCTL_H */
