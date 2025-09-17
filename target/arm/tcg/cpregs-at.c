/*
 * System instructions for address translation
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "cpu-features.h"
#include "internals.h"
#include "cpregs.h"


static int par_el1_shareability(GetPhysAddrResult *res)
{
    /*
     * The PAR_EL1.SH field must be 0b10 for Device or Normal-NC
     * memory -- see pseudocode PAREncodeShareability().
     */
    if (((res->cacheattrs.attrs & 0xf0) == 0) ||
        res->cacheattrs.attrs == 0x44 || res->cacheattrs.attrs == 0x40) {
        return 2;
    }
    return res->cacheattrs.shareability;
}

static uint64_t do_ats_write(CPUARMState *env, uint64_t value,
                             unsigned prot_check, ARMMMUIdx mmu_idx,
                             ARMSecuritySpace ss)
{
    uint64_t par64;
    bool format64 = false;
    ARMMMUFaultInfo fi = {};
    GetPhysAddrResult res = {};
    bool ret = get_phys_addr_for_at(env, value, prot_check,
                                    mmu_idx, ss, &res, &fi);

    /*
     * ATS operations only do S1 or S1+S2 translations, so we never
     * have to deal with the ARMCacheAttrs format for S2 only.
     */
    assert(!res.cacheattrs.is_s2_format);

    if (ret) {
        /*
         * Some kinds of translation fault must cause exceptions rather
         * than being reported in the PAR.
         */
        int current_el = arm_current_el(env);
        int target_el;
        uint32_t syn, fsr, fsc;
        bool take_exc = false;

        if (fi.s1ptw && current_el == 1
            && arm_mmu_idx_is_stage1_of_2(mmu_idx)) {
            /*
             * Synchronous stage 2 fault on an access made as part of the
             * translation table walk for AT S1E0* or AT S1E1* insn
             * executed from NS EL1. If this is a synchronous external abort
             * and SCR_EL3.EA == 1, then we take a synchronous external abort
             * to EL3. Otherwise the fault is taken as an exception to EL2,
             * and HPFAR_EL2 holds the faulting IPA.
             */
            if (fi.type == ARMFault_SyncExternalOnWalk &&
                (env->cp15.scr_el3 & SCR_EA)) {
                target_el = 3;
            } else {
                env->cp15.hpfar_el2 = extract64(fi.s2addr, 12, 47) << 4;
                if (arm_is_secure_below_el3(env) && fi.s1ns) {
                    env->cp15.hpfar_el2 |= HPFAR_NS;
                }
                target_el = 2;
            }
            take_exc = true;
        } else if (fi.type == ARMFault_SyncExternalOnWalk) {
            /*
             * Synchronous external aborts during a translation table walk
             * are taken as Data Abort exceptions.
             */
            if (fi.stage2) {
                if (current_el == 3) {
                    target_el = 3;
                } else {
                    target_el = 2;
                }
            } else {
                target_el = exception_target_el(env);
            }
            take_exc = true;
        }

        if (take_exc) {
            /* Construct FSR and FSC using same logic as arm_deliver_fault() */
            if (target_el == 2 || arm_el_is_aa64(env, target_el) ||
                arm_s1_regime_using_lpae_format(env, mmu_idx)) {
                fsr = arm_fi_to_lfsc(&fi);
                fsc = extract32(fsr, 0, 6);
            } else {
                fsr = arm_fi_to_sfsc(&fi);
                fsc = 0x3f;
            }
            /*
             * Report exception with ESR indicating a fault due to a
             * translation table walk for a cache maintenance instruction.
             */
            syn = syn_data_abort_no_iss(current_el == target_el, 0,
                                        fi.ea, 1, fi.s1ptw, 1, fsc);
            env->exception.vaddress = value;
            env->exception.fsr = fsr;
            raise_exception(env, EXCP_DATA_ABORT, syn, target_el);
        }
    }

    if (is_a64(env)) {
        format64 = true;
    } else if (arm_feature(env, ARM_FEATURE_LPAE)) {
        /*
         * ATS1Cxx:
         * * TTBCR.EAE determines whether the result is returned using the
         *   32-bit or the 64-bit PAR format
         * * Instructions executed in Hyp mode always use the 64bit format
         *
         * ATS1S2NSOxx uses the 64bit format if any of the following is true:
         * * The Non-secure TTBCR.EAE bit is set to 1
         * * The implementation includes EL2, and the value of HCR.VM is 1
         *
         * (Note that HCR.DC makes HCR.VM behave as if it is 1.)
         *
         * ATS1Hx always uses the 64bit format.
         */
        format64 = arm_s1_regime_using_lpae_format(env, mmu_idx);

        if (arm_feature(env, ARM_FEATURE_EL2)) {
            if (mmu_idx == ARMMMUIdx_E10_0 ||
                mmu_idx == ARMMMUIdx_E10_1 ||
                mmu_idx == ARMMMUIdx_E10_1_PAN) {
                format64 |= env->cp15.hcr_el2 & (HCR_VM | HCR_DC);
            } else {
                format64 |= arm_current_el(env) == 2;
            }
        }
    }

    if (format64) {
        /* Create a 64-bit PAR */
        par64 = (1 << 11); /* LPAE bit always set */
        if (!ret) {
            par64 |= res.f.phys_addr & ~0xfffULL;
            if (!res.f.attrs.secure) {
                par64 |= (1 << 9); /* NS */
            }
            par64 |= (uint64_t)res.cacheattrs.attrs << 56; /* ATTR */
            par64 |= par_el1_shareability(&res) << 7; /* SH */
        } else {
            uint32_t fsr = arm_fi_to_lfsc(&fi);

            par64 |= 1; /* F */
            par64 |= (fsr & 0x3f) << 1; /* FS */
            if (fi.stage2) {
                par64 |= (1 << 9); /* S */
            }
            if (fi.s1ptw) {
                par64 |= (1 << 8); /* PTW */
            }
        }
    } else {
        /*
         * fsr is a DFSR/IFSR value for the short descriptor
         * translation table format (with WnR always clear).
         * Convert it to a 32-bit PAR.
         */
        if (!ret) {
            /* We do not set any attribute bits in the PAR */
            if (res.f.lg_page_size == 24
                && arm_feature(env, ARM_FEATURE_V7)) {
                par64 = (res.f.phys_addr & 0xff000000) | (1 << 1);
            } else {
                par64 = res.f.phys_addr & 0xfffff000;
            }
            if (!res.f.attrs.secure) {
                par64 |= (1 << 9); /* NS */
            }
        } else {
            uint32_t fsr = arm_fi_to_sfsc(&fi);

            par64 = ((fsr & (1 << 10)) >> 5) | ((fsr & (1 << 12)) >> 6) |
                    ((fsr & 0xf) << 1) | 1;
        }
    }
    return par64;
}

static void ats_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    unsigned access_perm = ri->opc2 & 1 ? PAGE_WRITE : PAGE_READ;
    uint64_t par64;
    ARMMMUIdx mmu_idx;
    int el = arm_current_el(env);
    ARMSecuritySpace ss = arm_security_space(env);

    switch (ri->opc2 & 6) {
    case 0:
        /* stage 1 current state PL1: ATS1CPR, ATS1CPW, ATS1CPRP, ATS1CPWP */
        switch (el) {
        case 3:
            if (ri->crm == 9 && arm_pan_enabled(env)) {
                mmu_idx = ARMMMUIdx_E30_3_PAN;
            } else {
                mmu_idx = ARMMMUIdx_E3;
            }
            break;
        case 2:
            g_assert(ss != ARMSS_Secure);  /* ARMv8.4-SecEL2 is 64-bit only */
            /* fall through */
        case 1:
            if (ri->crm == 9 && arm_pan_enabled(env)) {
                mmu_idx = ARMMMUIdx_Stage1_E1_PAN;
            } else {
                mmu_idx = ARMMMUIdx_Stage1_E1;
            }
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 2:
        /* stage 1 current state PL0: ATS1CUR, ATS1CUW */
        switch (el) {
        case 3:
            mmu_idx = ARMMMUIdx_E30_0;
            break;
        case 2:
            g_assert(ss != ARMSS_Secure);  /* ARMv8.4-SecEL2 is 64-bit only */
            mmu_idx = ARMMMUIdx_Stage1_E0;
            break;
        case 1:
            mmu_idx = ARMMMUIdx_Stage1_E0;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 4:
        /* stage 1+2 NonSecure PL1: ATS12NSOPR, ATS12NSOPW */
        mmu_idx = ARMMMUIdx_E10_1;
        ss = ARMSS_NonSecure;
        break;
    case 6:
        /* stage 1+2 NonSecure PL0: ATS12NSOUR, ATS12NSOUW */
        mmu_idx = ARMMMUIdx_E10_0;
        ss = ARMSS_NonSecure;
        break;
    default:
        g_assert_not_reached();
    }

    par64 = do_ats_write(env, value, access_perm, mmu_idx, ss);

    A32_BANKED_CURRENT_REG_SET(env, par, par64);
}

static void ats1h_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    unsigned access_perm = ri->opc2 & 1 ? PAGE_WRITE : PAGE_READ;
    uint64_t par64;

    /* There is no SecureEL2 for AArch32. */
    par64 = do_ats_write(env, value, access_perm, ARMMMUIdx_E2,
                         ARMSS_NonSecure);

    A32_BANKED_CURRENT_REG_SET(env, par, par64);
}

static CPAccessResult at_e012_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    /*
     * R_NYXTL: instruction is UNDEFINED if it applies to an Exception level
     * lower than EL3 and the combination SCR_EL3.{NSE,NS} is reserved. This can
     * only happen when executing at EL3 because that combination also causes an
     * illegal exception return. We don't need to check FEAT_RME either, because
     * scr_write() ensures that the NSE bit is not set otherwise.
     */
    if ((env->cp15.scr_el3 & (SCR_NSE | SCR_NS)) == SCR_NSE) {
        return CP_ACCESS_UNDEFINED;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult at_s1e2_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if (arm_current_el(env) == 3 &&
        !(env->cp15.scr_el3 & (SCR_NS | SCR_EEL2))) {
        return CP_ACCESS_UNDEFINED;
    }
    return at_e012_access(env, ri, isread);
}

static CPAccessResult at_s1e01_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_AT)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return at_e012_access(env, ri, isread);
}

static void ats_write64(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    unsigned access_perm = ri->opc2 & 1 ? PAGE_WRITE : PAGE_READ;
    ARMMMUIdx mmu_idx;
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    bool regime_e20 = (hcr_el2 & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE);
    bool for_el3 = false;
    ARMSecuritySpace ss;

    switch (ri->opc2 & 6) {
    case 0:
        switch (ri->opc1) {
        case 0: /* AT S1E1R, AT S1E1W, AT S1E1RP, AT S1E1WP */
            if (ri->crm == 9 && arm_pan_enabled(env)) {
                mmu_idx = regime_e20 ?
                          ARMMMUIdx_E20_2_PAN : ARMMMUIdx_Stage1_E1_PAN;
            } else {
                mmu_idx = regime_e20 ? ARMMMUIdx_E20_2 : ARMMMUIdx_Stage1_E1;
            }
            break;
        case 4: /* AT S1E2R, AT S1E2W */
            mmu_idx = hcr_el2 & HCR_E2H ? ARMMMUIdx_E20_2 : ARMMMUIdx_E2;
            break;
        case 6: /* AT S1E3R, AT S1E3W */
            mmu_idx = ARMMMUIdx_E3;
            for_el3 = true;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 2: /* AT S1E0R, AT S1E0W */
        mmu_idx = regime_e20 ? ARMMMUIdx_E20_0 : ARMMMUIdx_Stage1_E0;
        break;
    case 4: /* AT S12E1R, AT S12E1W */
        mmu_idx = regime_e20 ? ARMMMUIdx_E20_2 : ARMMMUIdx_E10_1;
        break;
    case 6: /* AT S12E0R, AT S12E0W */
        mmu_idx = regime_e20 ? ARMMMUIdx_E20_0 : ARMMMUIdx_E10_0;
        break;
    default:
        g_assert_not_reached();
    }

    ss = for_el3 ? arm_security_space(env) : arm_security_space_below_el3(env);
    env->cp15.par_el[1] = do_ats_write(env, value, access_perm, mmu_idx, ss);
}

static CPAccessResult ats_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (ri->opc2 & 4) {
        /*
         * The ATS12NSO* operations must trap to EL3 or EL2 if executed in
         * Secure EL1 (which can only happen if EL3 is AArch64).
         * They are simply UNDEF if executed from NS EL1.
         * They function normally from EL2 or EL3.
         */
        if (arm_current_el(env) == 1) {
            if (arm_is_secure_below_el3(env)) {
                if (env->cp15.scr_el3 & SCR_EEL2) {
                    return CP_ACCESS_TRAP_EL2;
                }
                return CP_ACCESS_TRAP_EL3;
            }
            return CP_ACCESS_UNDEFINED;
        }
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo vapa_ats_reginfo[] = {
    /* This underdecoding is safe because the reginfo is NO_RAW. */
    { .name = "ATS", .cp = 15, .crn = 7, .crm = 8, .opc1 = 0, .opc2 = CP_ANY,
      .access = PL1_W, .accessfn = ats_access,
      .writefn = ats_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
};

static const ARMCPRegInfo v8_ats_reginfo[] = {
    /* 64 bit address translation operations */
    { .name = "AT_S1E1R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1R,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E1W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1W,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E0R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 2,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E0R,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E0W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 3,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E0W,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S12E1R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    { .name = "AT_S12E1W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    { .name = "AT_S12E0R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    { .name = "AT_S12E0W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 7,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_e012_access, .writefn = ats_write64 },
    /* AT S1E2* are elsewhere as they UNDEF from EL3 if EL2 is not present */
    { .name = "AT_S1E3R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S1E3W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
};

static const ARMCPRegInfo el2_ats_reginfo[] = {
    /*
     * Unlike the other EL2-related AT operations, these must
     * UNDEF from EL3 if EL2 is not implemented, which is why we
     * define them here rather than with the rest of the AT ops.
     */
    { .name = "AT_S1E2R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL2_W, .accessfn = at_s1e2_access,
      .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = ats_write64 },
    { .name = "AT_S1E2W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL2_W, .accessfn = at_s1e2_access,
      .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC | ARM_CP_EL3_NO_EL2_UNDEF,
      .writefn = ats_write64 },
    /*
     * The AArch32 ATS1H* operations are CONSTRAINED UNPREDICTABLE
     * if EL2 is not implemented; we choose to UNDEF. Behaviour at EL3
     * with SCR.NS == 0 outside Monitor mode is UNPREDICTABLE; we choose
     * to behave as if SCR.NS was 1.
     */
    { .name = "ATS1HR", .cp = 15, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL2_W,
      .writefn = ats1h_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
    { .name = "ATS1HW", .cp = 15, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL2_W,
      .writefn = ats1h_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
};

static const ARMCPRegInfo ats1e1_reginfo[] = {
    { .name = "AT_S1E1RP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1RP,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
    { .name = "AT_S1E1WP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1WP,
      .accessfn = at_s1e01_access, .writefn = ats_write64 },
};

static const ARMCPRegInfo ats1cp_reginfo[] = {
    { .name = "ATS1CPRP",
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write },
    { .name = "ATS1CPWP",
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write },
};

static void ats_s1e1a(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    bool regime_e20 = (hcr_el2 & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE);
    ARMMMUIdx mmu_idx = regime_e20 ? ARMMMUIdx_E20_2 : ARMMMUIdx_Stage1_E1;
    ARMSecuritySpace ss = arm_security_space_below_el3(env);

    env->cp15.par_el[1] = do_ats_write(env, value, 0, mmu_idx, ss);
}

static void ats_s1e2a(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    ARMMMUIdx mmu_idx = hcr_el2 & HCR_E2H ? ARMMMUIdx_E20_2 : ARMMMUIdx_E2;
    ARMSecuritySpace ss = arm_security_space_below_el3(env);

    env->cp15.par_el[1] = do_ats_write(env, value, 0, mmu_idx, ss);
}

static void ats_s1e3a(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    env->cp15.par_el[1] = do_ats_write(env, value, 0, ARMMMUIdx_E3,
                                       arm_security_space(env));
}

static const ARMCPRegInfo ats1a_reginfo[] = {
    { .name = "AT_S1E1A", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 2,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .fgt = FGT_ATS1E1A,
      .accessfn = at_s1e01_access, .writefn = ats_s1e1a },
    { .name = "AT_S1E2A", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 9, .opc2 = 2,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .accessfn = at_s1e2_access, .writefn = ats_s1e2a },
    { .name = "AT_S1E3A", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 9, .opc2 = 2,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_s1e3a },
};

void define_at_insn_regs(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_VAPA)) {
        define_arm_cp_regs(cpu, vapa_ats_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V8)) {
        define_arm_cp_regs(cpu, v8_ats_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_EL2)
        || (arm_feature(env, ARM_FEATURE_EL3)
            && arm_feature(env, ARM_FEATURE_V8))) {
        define_arm_cp_regs(cpu, el2_ats_reginfo);
    }
    if (cpu_isar_feature(aa64_ats1e1, cpu)) {
        define_arm_cp_regs(cpu, ats1e1_reginfo);
    }
    if (cpu_isar_feature(aa32_ats1e1, cpu)) {
        define_arm_cp_regs(cpu, ats1cp_reginfo);
    }
    if (cpu_isar_feature(aa64_ats1a, cpu)) {
        define_arm_cp_regs(cpu, ats1a_reginfo);
    }
}
