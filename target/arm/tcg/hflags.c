/*
 * ARM hflags
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "cpregs.h"

static inline bool fgt_svc(CPUARMState *env, int el)
{
    /*
     * Assuming fine-grained-traps are active, return true if we
     * should be trapping on SVC instructions. Only AArch64 can
     * trap on an SVC at EL1, but we don't need to special-case this
     * because if this is AArch32 EL1 then arm_fgt_active() is false.
     * We also know el is 0 or 1.
     */
    return el == 0 ?
        FIELD_EX64(env->cp15.fgt_exec[FGTREG_HFGITR], HFGITR_EL2, SVC_EL0) :
        FIELD_EX64(env->cp15.fgt_exec[FGTREG_HFGITR], HFGITR_EL2, SVC_EL1);
}

static CPUARMTBFlags rebuild_hflags_common(CPUARMState *env, int fp_el,
                                           ARMMMUIdx mmu_idx,
                                           CPUARMTBFlags flags)
{
    DP_TBFLAG_ANY(flags, FPEXC_EL, fp_el);
    DP_TBFLAG_ANY(flags, MMUIDX, arm_to_core_mmu_idx(mmu_idx));

    if (arm_singlestep_active(env)) {
        DP_TBFLAG_ANY(flags, SS_ACTIVE, 1);
    }

    return flags;
}

static CPUARMTBFlags rebuild_hflags_common_32(CPUARMState *env, int fp_el,
                                              ARMMMUIdx mmu_idx,
                                              CPUARMTBFlags flags)
{
    bool sctlr_b = arm_sctlr_b(env);

    if (sctlr_b) {
        DP_TBFLAG_A32(flags, SCTLR__B, 1);
    }
    if (arm_cpu_data_is_big_endian_a32(env, sctlr_b)) {
        DP_TBFLAG_ANY(flags, BE_DATA, 1);
    }
    DP_TBFLAG_A32(flags, NS, !access_secure_reg(env));

    return rebuild_hflags_common(env, fp_el, mmu_idx, flags);
}

static CPUARMTBFlags rebuild_hflags_m32(CPUARMState *env, int fp_el,
                                        ARMMMUIdx mmu_idx)
{
    CPUARMTBFlags flags = {};
    uint32_t ccr = env->v7m.ccr[env->v7m.secure];

    /* Without HaveMainExt, CCR.UNALIGN_TRP is RES1. */
    if (ccr & R_V7M_CCR_UNALIGN_TRP_MASK) {
        DP_TBFLAG_ANY(flags, ALIGN_MEM, 1);
    }

    if (arm_v7m_is_handler_mode(env)) {
        DP_TBFLAG_M32(flags, HANDLER, 1);
    }

    /*
     * v8M always applies stack limit checks unless CCR.STKOFHFNMIGN
     * is suppressing them because the requested execution priority
     * is less than 0.
     */
    if (arm_feature(env, ARM_FEATURE_V8) &&
        !((mmu_idx & ARM_MMU_IDX_M_NEGPRI) &&
          (ccr & R_V7M_CCR_STKOFHFNMIGN_MASK))) {
        DP_TBFLAG_M32(flags, STACKCHECK, 1);
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY) && env->v7m.secure) {
        DP_TBFLAG_M32(flags, SECURE, 1);
    }

    return rebuild_hflags_common_32(env, fp_el, mmu_idx, flags);
}

/* This corresponds to the ARM pseudocode function IsFullA64Enabled(). */
static bool sme_fa64(CPUARMState *env, int el)
{
    if (!cpu_isar_feature(aa64_sme_fa64, env_archcpu(env))) {
        return false;
    }

    if (el <= 1 && !el_is_in_host(env, el)) {
        if (!FIELD_EX64(env->vfp.smcr_el[1], SMCR, FA64)) {
            return false;
        }
    }
    if (el <= 2 && arm_is_el2_enabled(env)) {
        if (!FIELD_EX64(env->vfp.smcr_el[2], SMCR, FA64)) {
            return false;
        }
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        if (!FIELD_EX64(env->vfp.smcr_el[3], SMCR, FA64)) {
            return false;
        }
    }

    return true;
}

static CPUARMTBFlags rebuild_hflags_a32(CPUARMState *env, int fp_el,
                                        ARMMMUIdx mmu_idx)
{
    CPUARMTBFlags flags = {};
    int el = arm_current_el(env);

    if (arm_sctlr(env, el) & SCTLR_A) {
        DP_TBFLAG_ANY(flags, ALIGN_MEM, 1);
    }

    if (arm_el_is_aa64(env, 1)) {
        DP_TBFLAG_A32(flags, VFPEN, 1);
    }

    if (el < 2 && env->cp15.hstr_el2 && arm_is_el2_enabled(env) &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        DP_TBFLAG_A32(flags, HSTR_ACTIVE, 1);
    }

    if (arm_fgt_active(env, el)) {
        DP_TBFLAG_ANY(flags, FGT_ACTIVE, 1);
        if (fgt_svc(env, el)) {
            DP_TBFLAG_ANY(flags, FGT_SVC, 1);
        }
    }

    if (env->uncached_cpsr & CPSR_IL) {
        DP_TBFLAG_ANY(flags, PSTATE__IL, 1);
    }

    /*
     * The SME exception we are testing for is raised via
     * AArch64.CheckFPAdvSIMDEnabled(), as called from
     * AArch32.CheckAdvSIMDOrFPEnabled().
     */
    if (el == 0
        && FIELD_EX64(env->svcr, SVCR, SM)
        && (!arm_is_el2_enabled(env)
            || (arm_el_is_aa64(env, 2) && !(env->cp15.hcr_el2 & HCR_TGE)))
        && arm_el_is_aa64(env, 1)
        && !sme_fa64(env, el)) {
        DP_TBFLAG_A32(flags, SME_TRAP_NONSTREAMING, 1);
    }

    return rebuild_hflags_common_32(env, fp_el, mmu_idx, flags);
}

static CPUARMTBFlags rebuild_hflags_a64(CPUARMState *env, int el, int fp_el,
                                        ARMMMUIdx mmu_idx)
{
    CPUARMTBFlags flags = {};
    ARMMMUIdx stage1 = stage_1_mmu_idx(mmu_idx);
    uint64_t tcr = regime_tcr(env, mmu_idx);
    uint64_t sctlr;
    int tbii, tbid;

    DP_TBFLAG_ANY(flags, AARCH64_STATE, 1);

    /* Get control bits for tagged addresses.  */
    tbid = aa64_va_parameter_tbi(tcr, mmu_idx);
    tbii = tbid & ~aa64_va_parameter_tbid(tcr, mmu_idx);

    DP_TBFLAG_A64(flags, TBII, tbii);
    DP_TBFLAG_A64(flags, TBID, tbid);

    if (cpu_isar_feature(aa64_sve, env_archcpu(env))) {
        int sve_el = sve_exception_el(env, el);

        /*
         * If either FP or SVE are disabled, translator does not need len.
         * If SVE EL > FP EL, FP exception has precedence, and translator
         * does not need SVE EL.  Save potential re-translations by forcing
         * the unneeded data to zero.
         */
        if (fp_el != 0) {
            if (sve_el > fp_el) {
                sve_el = 0;
            }
        } else if (sve_el == 0) {
            DP_TBFLAG_A64(flags, VL, sve_vqm1_for_el(env, el));
        }
        DP_TBFLAG_A64(flags, SVEEXC_EL, sve_el);
    }
    if (cpu_isar_feature(aa64_sme, env_archcpu(env))) {
        int sme_el = sme_exception_el(env, el);
        bool sm = FIELD_EX64(env->svcr, SVCR, SM);

        DP_TBFLAG_A64(flags, SMEEXC_EL, sme_el);
        if (sme_el == 0) {
            /* Similarly, do not compute SVL if SME is disabled. */
            int svl = sve_vqm1_for_el_sm(env, el, true);
            DP_TBFLAG_A64(flags, SVL, svl);
            if (sm) {
                /* If SVE is disabled, we will not have set VL above. */
                DP_TBFLAG_A64(flags, VL, svl);
            }
        }
        if (sm) {
            DP_TBFLAG_A64(flags, PSTATE_SM, 1);
            DP_TBFLAG_A64(flags, SME_TRAP_NONSTREAMING, !sme_fa64(env, el));
        }
        DP_TBFLAG_A64(flags, PSTATE_ZA, FIELD_EX64(env->svcr, SVCR, ZA));
    }

    sctlr = regime_sctlr(env, stage1);

    if (sctlr & SCTLR_A) {
        DP_TBFLAG_ANY(flags, ALIGN_MEM, 1);
    }

    if (arm_cpu_data_is_big_endian_a64(el, sctlr)) {
        DP_TBFLAG_ANY(flags, BE_DATA, 1);
    }

    if (cpu_isar_feature(aa64_pauth, env_archcpu(env))) {
        /*
         * In order to save space in flags, we record only whether
         * pauth is "inactive", meaning all insns are implemented as
         * a nop, or "active" when some action must be performed.
         * The decision of which action to take is left to a helper.
         */
        if (sctlr & (SCTLR_EnIA | SCTLR_EnIB | SCTLR_EnDA | SCTLR_EnDB)) {
            DP_TBFLAG_A64(flags, PAUTH_ACTIVE, 1);
        }
    }

    if (cpu_isar_feature(aa64_bti, env_archcpu(env))) {
        /* Note that SCTLR_EL[23].BT == SCTLR_BT1.  */
        if (sctlr & (el == 0 ? SCTLR_BT0 : SCTLR_BT1)) {
            DP_TBFLAG_A64(flags, BT, 1);
        }
    }

    if (cpu_isar_feature(aa64_lse2, env_archcpu(env))) {
        if (sctlr & SCTLR_nAA) {
            DP_TBFLAG_A64(flags, NAA, 1);
        }
    }

    /* Compute the condition for using AccType_UNPRIV for LDTR et al. */
    if (!(env->pstate & PSTATE_UAO)) {
        switch (mmu_idx) {
        case ARMMMUIdx_E10_1:
        case ARMMMUIdx_E10_1_PAN:
            /* TODO: ARMv8.3-NV */
            DP_TBFLAG_A64(flags, UNPRIV, 1);
            break;
        case ARMMMUIdx_E20_2:
        case ARMMMUIdx_E20_2_PAN:
            /*
             * Note that EL20_2 is gated by HCR_EL2.E2H == 1, but EL20_0 is
             * gated by HCR_EL2.<E2H,TGE> == '11', and so is LDTR.
             */
            if (env->cp15.hcr_el2 & HCR_TGE) {
                DP_TBFLAG_A64(flags, UNPRIV, 1);
            }
            break;
        default:
            break;
        }
    }

    if (env->pstate & PSTATE_IL) {
        DP_TBFLAG_ANY(flags, PSTATE__IL, 1);
    }

    if (arm_fgt_active(env, el)) {
        DP_TBFLAG_ANY(flags, FGT_ACTIVE, 1);
        if (FIELD_EX64(env->cp15.fgt_exec[FGTREG_HFGITR], HFGITR_EL2, ERET)) {
            DP_TBFLAG_A64(flags, FGT_ERET, 1);
        }
        if (fgt_svc(env, el)) {
            DP_TBFLAG_ANY(flags, FGT_SVC, 1);
        }
    }

    if (cpu_isar_feature(aa64_mte, env_archcpu(env))) {
        /*
         * Set MTE_ACTIVE if any access may be Checked, and leave clear
         * if all accesses must be Unchecked:
         * 1) If no TBI, then there are no tags in the address to check,
         * 2) If Tag Check Override, then all accesses are Unchecked,
         * 3) If Tag Check Fail == 0, then Checked access have no effect,
         * 4) If no Allocation Tag Access, then all accesses are Unchecked.
         */
        if (allocation_tag_access_enabled(env, el, sctlr)) {
            DP_TBFLAG_A64(flags, ATA, 1);
            if (tbid
                && !(env->pstate & PSTATE_TCO)
                && (sctlr & (el == 0 ? SCTLR_TCF0 : SCTLR_TCF))) {
                DP_TBFLAG_A64(flags, MTE_ACTIVE, 1);
            }
        }
        /* And again for unprivileged accesses, if required.  */
        if (EX_TBFLAG_A64(flags, UNPRIV)
            && tbid
            && !(env->pstate & PSTATE_TCO)
            && (sctlr & SCTLR_TCF0)
            && allocation_tag_access_enabled(env, 0, sctlr)) {
            DP_TBFLAG_A64(flags, MTE0_ACTIVE, 1);
        }
        /* Cache TCMA as well as TBI. */
        DP_TBFLAG_A64(flags, TCMA, aa64_va_parameter_tcma(tcr, mmu_idx));
    }

    return rebuild_hflags_common(env, fp_el, mmu_idx, flags);
}

static CPUARMTBFlags rebuild_hflags_internal(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    if (is_a64(env)) {
        return rebuild_hflags_a64(env, el, fp_el, mmu_idx);
    } else if (arm_feature(env, ARM_FEATURE_M)) {
        return rebuild_hflags_m32(env, fp_el, mmu_idx);
    } else {
        return rebuild_hflags_a32(env, fp_el, mmu_idx);
    }
}

void arm_rebuild_hflags(CPUARMState *env)
{
    env->hflags = rebuild_hflags_internal(env);
}

/*
 * If we have triggered a EL state change we can't rely on the
 * translator having passed it to us, we need to recompute.
 */
void HELPER(rebuild_hflags_m32_newel)(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_m32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_m32)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_m32(env, fp_el, mmu_idx);
}

/*
 * If we have triggered a EL state change we can't rely on the
 * translator having passed it to us, we need to recompute.
 */
void HELPER(rebuild_hflags_a32_newel)(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);
    env->hflags = rebuild_hflags_a32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_a32)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_a32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_a64)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_a64(env, el, fp_el, mmu_idx);
}

void assert_hflags_rebuild_correctly(CPUARMState *env)
{
#ifdef CONFIG_DEBUG_TCG
    CPUARMTBFlags c = env->hflags;
    CPUARMTBFlags r = rebuild_hflags_internal(env);

    if (unlikely(c.flags != r.flags || c.flags2 != r.flags2)) {
        fprintf(stderr, "TCG hflags mismatch "
                        "(current:(0x%08x,0x" TARGET_FMT_lx ")"
                        " rebuilt:(0x%08x,0x" TARGET_FMT_lx ")\n",
                c.flags, c.flags2, r.flags, r.flags2);
        abort();
    }
#endif
}
