/*
 * QEMU Arm CPU -- feature test functions
 *
 *  Copyright (c) 2023 Linaro Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_ARM_FEATURES_H
#define TARGET_ARM_FEATURES_H

#include "hw/registerfields.h"
#include "qemu/host-utils.h"

/*
 * Naming convention for isar_feature functions:
 * Functions which test 32-bit ID registers should have _aa32_ in
 * their name. Functions which test 64-bit ID registers should have
 * _aa64_ in their name. These must only be used in code where we
 * know for certain that the CPU has AArch32 or AArch64 respectively
 * or where the correct answer for a CPU which doesn't implement that
 * CPU state is "false" (eg when generating A32 or A64 code, if adding
 * system registers that are specific to that CPU state, for "should
 * we let this system register bit be set" tests where the 32-bit
 * flavour of the register doesn't have the bit, and so on).
 * Functions which simply ask "does this feature exist at all" have
 * _any_ in their name, and always return the logical OR of the _aa64_
 * and the _aa32_ function.
 */

/*
 * 32-bit feature tests via id registers.
 */
static inline bool isar_feature_aa32_thumb_div(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar0, ID_ISAR0, DIVIDE) != 0;
}

static inline bool isar_feature_aa32_arm_div(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar0, ID_ISAR0, DIVIDE) > 1;
}

static inline bool isar_feature_aa32_lob(const ARMISARegisters *id)
{
    /* (M-profile) low-overhead loops and branch future */
    return FIELD_EX32(id->id_isar0, ID_ISAR0, CMPBRANCH) >= 3;
}

static inline bool isar_feature_aa32_jazelle(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar1, ID_ISAR1, JAZELLE) != 0;
}

static inline bool isar_feature_aa32_aes(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, AES) != 0;
}

static inline bool isar_feature_aa32_pmull(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, AES) > 1;
}

static inline bool isar_feature_aa32_sha1(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, SHA1) != 0;
}

static inline bool isar_feature_aa32_sha2(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, SHA2) != 0;
}

static inline bool isar_feature_aa32_crc32(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, CRC32) != 0;
}

static inline bool isar_feature_aa32_rdm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, RDM) != 0;
}

static inline bool isar_feature_aa32_vcma(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar5, ID_ISAR5, VCMA) != 0;
}

static inline bool isar_feature_aa32_jscvt(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, JSCVT) != 0;
}

static inline bool isar_feature_aa32_dp(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, DP) != 0;
}

static inline bool isar_feature_aa32_fhm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, FHM) != 0;
}

static inline bool isar_feature_aa32_sb(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, SB) != 0;
}

static inline bool isar_feature_aa32_predinv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, SPECRES) != 0;
}

static inline bool isar_feature_aa32_bf16(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, BF16) != 0;
}

static inline bool isar_feature_aa32_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_isar6, ID_ISAR6, I8MM) != 0;
}

static inline bool isar_feature_aa32_ras(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr0, ID_PFR0, RAS) != 0;
}

static inline bool isar_feature_aa32_mprofile(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr1, ID_PFR1, MPROGMOD) != 0;
}

static inline bool isar_feature_aa32_m_sec_state(const ARMISARegisters *id)
{
    /*
     * Return true if M-profile state handling insns
     * (VSCCLRM, CLRM, FPCTX access insns) are implemented
     */
    return FIELD_EX32(id->id_pfr1, ID_PFR1, SECURITY) >= 3;
}

static inline bool isar_feature_aa32_fp16_arith(const ARMISARegisters *id)
{
    /* Sadly this is encoded differently for A-profile and M-profile */
    if (isar_feature_aa32_mprofile(id)) {
        return FIELD_EX32(id->mvfr1, MVFR1, FP16) > 0;
    } else {
        return FIELD_EX32(id->mvfr1, MVFR1, FPHP) >= 3;
    }
}

static inline bool isar_feature_aa32_mve(const ARMISARegisters *id)
{
    /*
     * Return true if MVE is supported (either integer or floating point).
     * We must check for M-profile as the MVFR1 field means something
     * else for A-profile.
     */
    return isar_feature_aa32_mprofile(id) &&
        FIELD_EX32(id->mvfr1, MVFR1, MVE) > 0;
}

static inline bool isar_feature_aa32_mve_fp(const ARMISARegisters *id)
{
    /*
     * Return true if MVE is supported (either integer or floating point).
     * We must check for M-profile as the MVFR1 field means something
     * else for A-profile.
     */
    return isar_feature_aa32_mprofile(id) &&
        FIELD_EX32(id->mvfr1, MVFR1, MVE) >= 2;
}

static inline bool isar_feature_aa32_vfp_simd(const ARMISARegisters *id)
{
    /*
     * Return true if either VFP or SIMD is implemented.
     * In this case, a minimum of VFP w/ D0-D15.
     */
    return FIELD_EX32(id->mvfr0, MVFR0, SIMDREG) > 0;
}

static inline bool isar_feature_aa32_simd_r32(const ARMISARegisters *id)
{
    /* Return true if D16-D31 are implemented */
    return FIELD_EX32(id->mvfr0, MVFR0, SIMDREG) >= 2;
}

static inline bool isar_feature_aa32_fpshvec(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr0, MVFR0, FPSHVEC) > 0;
}

static inline bool isar_feature_aa32_fpsp_v2(const ARMISARegisters *id)
{
    /* Return true if CPU supports single precision floating point, VFPv2 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPSP) > 0;
}

static inline bool isar_feature_aa32_fpsp_v3(const ARMISARegisters *id)
{
    /* Return true if CPU supports single precision floating point, VFPv3 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPSP) >= 2;
}

static inline bool isar_feature_aa32_fpdp_v2(const ARMISARegisters *id)
{
    /* Return true if CPU supports double precision floating point, VFPv2 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPDP) > 0;
}

static inline bool isar_feature_aa32_fpdp_v3(const ARMISARegisters *id)
{
    /* Return true if CPU supports double precision floating point, VFPv3 */
    return FIELD_EX32(id->mvfr0, MVFR0, FPDP) >= 2;
}

static inline bool isar_feature_aa32_vfp(const ARMISARegisters *id)
{
    return isar_feature_aa32_fpsp_v2(id) || isar_feature_aa32_fpdp_v2(id);
}

/*
 * We always set the FP and SIMD FP16 fields to indicate identical
 * levels of support (assuming SIMD is implemented at all), so
 * we only need one set of accessors.
 */
static inline bool isar_feature_aa32_fp16_spconv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, FPHP) > 0;
}

static inline bool isar_feature_aa32_fp16_dpconv(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, FPHP) > 1;
}

/*
 * Note that this ID register field covers both VFP and Neon FMAC,
 * so should usually be tested in combination with some other
 * check that confirms the presence of whichever of VFP or Neon is
 * relevant, to avoid accidentally enabling a Neon feature on
 * a VFP-no-Neon core or vice-versa.
 */
static inline bool isar_feature_aa32_simdfmac(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr1, MVFR1, SIMDFMAC) != 0;
}

static inline bool isar_feature_aa32_vsel(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 1;
}

static inline bool isar_feature_aa32_vcvt_dr(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 2;
}

static inline bool isar_feature_aa32_vrint(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 3;
}

static inline bool isar_feature_aa32_vminmaxnm(const ARMISARegisters *id)
{
    return FIELD_EX32(id->mvfr2, MVFR2, FPMISC) >= 4;
}

static inline bool isar_feature_aa32_pxn(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr0, ID_MMFR0, VMSA) >= 4;
}

static inline bool isar_feature_aa32_pan(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr3, ID_MMFR3, PAN) != 0;
}

static inline bool isar_feature_aa32_ats1e1(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr3, ID_MMFR3, PAN) >= 2;
}

static inline bool isar_feature_aa32_pmuv3p1(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) >= 4 &&
        FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_pmuv3p4(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) >= 5 &&
        FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_pmuv3p5(const ARMISARegisters *id)
{
    /* 0xf means "non-standard IMPDEF PMU" */
    return FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) >= 6 &&
        FIELD_EX32(id->id_dfr0, ID_DFR0, PERFMON) != 0xf;
}

static inline bool isar_feature_aa32_hpd(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, HPDS) != 0;
}

static inline bool isar_feature_aa32_ac2(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, AC2) != 0;
}

static inline bool isar_feature_aa32_ccidx(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, CCIDX) != 0;
}

static inline bool isar_feature_aa32_tts2uxn(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, XNX) != 0;
}

static inline bool isar_feature_aa32_half_evt(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, EVT) >= 1;
}

static inline bool isar_feature_aa32_evt(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_mmfr4, ID_MMFR4, EVT) >= 2;
}

static inline bool isar_feature_aa32_dit(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr0, ID_PFR0, DIT) != 0;
}

static inline bool isar_feature_aa32_ssbs(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_pfr2, ID_PFR2, SSBS) != 0;
}

static inline bool isar_feature_aa32_debugv7p1(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_dfr0, ID_DFR0, COPDBG) >= 5;
}

static inline bool isar_feature_aa32_debugv8p2(const ARMISARegisters *id)
{
    return FIELD_EX32(id->id_dfr0, ID_DFR0, COPDBG) >= 8;
}

static inline bool isar_feature_aa32_doublelock(const ARMISARegisters *id)
{
    return FIELD_EX32(id->dbgdevid, DBGDEVID, DOUBLELOCK) > 0;
}

/*
 * 64-bit feature tests via id registers.
 */
static inline bool isar_feature_aa64_aes(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, AES) != 0;
}

static inline bool isar_feature_aa64_pmull(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, AES) > 1;
}

static inline bool isar_feature_aa64_sha1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA1) != 0;
}

static inline bool isar_feature_aa64_sha256(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA2) != 0;
}

static inline bool isar_feature_aa64_sha512(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA2) > 1;
}

static inline bool isar_feature_aa64_crc32(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, CRC32) != 0;
}

static inline bool isar_feature_aa64_atomics(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, ATOMIC) != 0;
}

static inline bool isar_feature_aa64_rdm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, RDM) != 0;
}

static inline bool isar_feature_aa64_sha3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SHA3) != 0;
}

static inline bool isar_feature_aa64_sm3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SM3) != 0;
}

static inline bool isar_feature_aa64_sm4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, SM4) != 0;
}

static inline bool isar_feature_aa64_dp(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, DP) != 0;
}

static inline bool isar_feature_aa64_fhm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, FHM) != 0;
}

static inline bool isar_feature_aa64_condm_4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TS) != 0;
}

static inline bool isar_feature_aa64_condm_5(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TS) >= 2;
}

static inline bool isar_feature_aa64_rndr(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, RNDR) != 0;
}

static inline bool isar_feature_aa64_tlbirange(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TLB) == 2;
}

static inline bool isar_feature_aa64_tlbios(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar0, ID_AA64ISAR0, TLB) != 0;
}

static inline bool isar_feature_aa64_jscvt(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, JSCVT) != 0;
}

static inline bool isar_feature_aa64_fcma(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, FCMA) != 0;
}

static inline bool isar_feature_aa64_xs(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, XS) != 0;
}

/*
 * These are the values from APA/API/APA3.
 * In general these must be compared '>=', per the normal Arm ARM
 * treatment of fields in ID registers.
 */
typedef enum {
    PauthFeat_None         = 0,
    PauthFeat_1            = 1,
    PauthFeat_EPAC         = 2,
    PauthFeat_2            = 3,
    PauthFeat_FPAC         = 4,
    PauthFeat_FPACCOMBINED = 5,
} ARMPauthFeature;

static inline ARMPauthFeature
isar_feature_pauth_feature(const ARMISARegisters *id)
{
    /*
     * Architecturally, only one of {APA,API,APA3} may be active (non-zero)
     * and the other two must be zero.  Thus we may avoid conditionals.
     */
    return (FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, APA) |
            FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, API) |
            FIELD_EX64(id->id_aa64isar2, ID_AA64ISAR2, APA3));
}

static inline bool isar_feature_aa64_pauth(const ARMISARegisters *id)
{
    /*
     * Return true if any form of pauth is enabled, as this
     * predicate controls migration of the 128-bit keys.
     */
    return isar_feature_pauth_feature(id) != PauthFeat_None;
}

static inline bool isar_feature_aa64_pauth_qarma5(const ARMISARegisters *id)
{
    /*
     * Return true if pauth is enabled with the architected QARMA5 algorithm.
     * QEMU will always enable or disable both APA and GPA.
     */
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, APA) != 0;
}

static inline bool isar_feature_aa64_pauth_qarma3(const ARMISARegisters *id)
{
    /*
     * Return true if pauth is enabled with the architected QARMA3 algorithm.
     * QEMU will always enable or disable both APA3 and GPA3.
     */
    return FIELD_EX64(id->id_aa64isar2, ID_AA64ISAR2, APA3) != 0;
}

static inline bool isar_feature_aa64_sb(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, SB) != 0;
}

static inline bool isar_feature_aa64_predinv(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, SPECRES) != 0;
}

static inline bool isar_feature_aa64_frint(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, FRINTTS) != 0;
}

static inline bool isar_feature_aa64_dcpop(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, DPB) != 0;
}

static inline bool isar_feature_aa64_dcpodp(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, DPB) >= 2;
}

static inline bool isar_feature_aa64_bf16(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, BF16) != 0;
}

static inline bool isar_feature_aa64_ebf16(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, BF16) > 1;
}

static inline bool isar_feature_aa64_rcpc_8_3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, LRCPC) != 0;
}

static inline bool isar_feature_aa64_rcpc_8_4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, LRCPC) >= 2;
}

static inline bool isar_feature_aa64_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar1, ID_AA64ISAR1, I8MM) != 0;
}

static inline bool isar_feature_aa64_wfxt(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar2, ID_AA64ISAR2, WFXT) >= 2;
}

static inline bool isar_feature_aa64_hbc(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar2, ID_AA64ISAR2, BC) != 0;
}

static inline bool isar_feature_aa64_mops(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar2, ID_AA64ISAR2, MOPS);
}

static inline bool isar_feature_aa64_rpres(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64isar2, ID_AA64ISAR2, RPRES);
}

static inline bool isar_feature_aa64_fp_simd(const ARMISARegisters *id)
{
    /* We always set the AdvSIMD and FP fields identically.  */
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, FP) != 0xf;
}

static inline bool isar_feature_aa64_fp16(const ARMISARegisters *id)
{
    /* We always set the AdvSIMD and FP fields identically wrt FP16.  */
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, FP) == 1;
}

static inline bool isar_feature_aa64_aa32(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, EL0) >= 2;
}

static inline bool isar_feature_aa64_aa32_el1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, EL1) >= 2;
}

static inline bool isar_feature_aa64_aa32_el2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, EL2) >= 2;
}

static inline bool isar_feature_aa64_ras(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, RAS) != 0;
}

static inline bool isar_feature_aa64_doublefault(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, RAS) >= 2;
}

static inline bool isar_feature_aa64_sve(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, SVE) != 0;
}

static inline bool isar_feature_aa64_sel2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, SEL2) != 0;
}

static inline bool isar_feature_aa64_rme(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, RME) != 0;
}

static inline bool isar_feature_aa64_dit(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, DIT) != 0;
}

static inline bool isar_feature_aa64_scxtnum(const ARMISARegisters *id)
{
    int key = FIELD_EX64(id->id_aa64pfr0, ID_AA64PFR0, CSV2);
    if (key >= 2) {
        return true;      /* FEAT_CSV2_2 */
    }
    if (key == 1) {
        key = FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, CSV2_FRAC);
        return key >= 2;  /* FEAT_CSV2_1p2 */
    }
    return false;
}

static inline bool isar_feature_aa64_ssbs(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, SSBS) != 0;
}

static inline bool isar_feature_aa64_bti(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, BT) != 0;
}

static inline bool isar_feature_aa64_mte_insn_reg(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, MTE) != 0;
}

static inline bool isar_feature_aa64_mte(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, MTE) >= 2;
}

static inline bool isar_feature_aa64_mte3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, MTE) >= 3;
}

static inline bool isar_feature_aa64_sme(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, SME) != 0;
}

static inline bool isar_feature_aa64_nmi(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64pfr1, ID_AA64PFR1, NMI) != 0;
}

static inline bool isar_feature_aa64_tgran4_lpa2(const ARMISARegisters *id)
{
    return FIELD_SEX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN4) >= 1;
}

static inline bool isar_feature_aa64_tgran4_2_lpa2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN4_2);
    return t >= 3 || (t == 0 && isar_feature_aa64_tgran4_lpa2(id));
}

static inline bool isar_feature_aa64_tgran16_lpa2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN16) >= 2;
}

static inline bool isar_feature_aa64_tgran16_2_lpa2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN16_2);
    return t >= 3 || (t == 0 && isar_feature_aa64_tgran16_lpa2(id));
}

static inline bool isar_feature_aa64_tgran4(const ARMISARegisters *id)
{
    return FIELD_SEX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN4) >= 0;
}

static inline bool isar_feature_aa64_tgran16(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN16) >= 1;
}

static inline bool isar_feature_aa64_tgran64(const ARMISARegisters *id)
{
    return FIELD_SEX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN64) >= 0;
}

static inline bool isar_feature_aa64_tgran4_2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN4_2);
    return t >= 2 || (t == 0 && isar_feature_aa64_tgran4(id));
}

static inline bool isar_feature_aa64_tgran16_2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN16_2);
    return t >= 2 || (t == 0 && isar_feature_aa64_tgran16(id));
}

static inline bool isar_feature_aa64_tgran64_2(const ARMISARegisters *id)
{
    unsigned t = FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, TGRAN64_2);
    return t >= 2 || (t == 0 && isar_feature_aa64_tgran64(id));
}

static inline bool isar_feature_aa64_fgt(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, FGT) != 0;
}

static inline bool isar_feature_aa64_ecv_traps(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, ECV) > 0;
}

static inline bool isar_feature_aa64_ecv(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr0, ID_AA64MMFR0, ECV) > 1;
}

static inline bool isar_feature_aa64_vh(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, VH) != 0;
}

static inline bool isar_feature_aa64_lor(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, LO) != 0;
}

static inline bool isar_feature_aa64_pan(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, PAN) != 0;
}

static inline bool isar_feature_aa64_ats1e1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, PAN) >= 2;
}

static inline bool isar_feature_aa64_pan3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, PAN) >= 3;
}

static inline bool isar_feature_aa64_hcx(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, HCX) != 0;
}

static inline bool isar_feature_aa64_afp(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, AFP) != 0;
}

static inline bool isar_feature_aa64_tidcp1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, TIDCP1) != 0;
}

static inline bool isar_feature_aa64_cmow(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, CMOW) != 0;
}

static inline bool isar_feature_aa64_hafs(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, HAFDBS) != 0;
}

static inline bool isar_feature_aa64_hdbs(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, HAFDBS) >= 2;
}

static inline bool isar_feature_aa64_tts2uxn(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr1, ID_AA64MMFR1, XNX) != 0;
}

static inline bool isar_feature_aa64_uao(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, UAO) != 0;
}

static inline bool isar_feature_aa64_st(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, ST) != 0;
}

static inline bool isar_feature_aa64_lse2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, AT) != 0;
}

static inline bool isar_feature_aa64_fwb(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, FWB) != 0;
}

static inline bool isar_feature_aa64_ids(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, IDS) != 0;
}

static inline bool isar_feature_aa64_half_evt(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, EVT) >= 1;
}

static inline bool isar_feature_aa64_evt(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, EVT) >= 2;
}

static inline bool isar_feature_aa64_ccidx(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, CCIDX) != 0;
}

static inline bool isar_feature_aa64_lva(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, VARANGE) != 0;
}

static inline bool isar_feature_aa64_e0pd(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, E0PD) != 0;
}

static inline bool isar_feature_aa64_nv(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, NV) != 0;
}

static inline bool isar_feature_aa64_nv2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64mmfr2, ID_AA64MMFR2, NV) >= 2;
}

static inline bool isar_feature_aa64_pmuv3p1(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) >= 4 &&
        FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_pmuv3p4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) >= 5 &&
        FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_pmuv3p5(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) >= 6 &&
        FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, PMUVER) != 0xf;
}

static inline bool isar_feature_aa64_debugv8p2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64dfr0, ID_AA64DFR0, DEBUGVER) >= 8;
}

static inline bool isar_feature_aa64_doublelock(const ARMISARegisters *id)
{
    return FIELD_SEX64(id->id_aa64dfr0, ID_AA64DFR0, DOUBLELOCK) >= 0;
}

static inline bool isar_feature_aa64_sve2(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, SVEVER) != 0;
}

static inline bool isar_feature_aa64_sve2_aes(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, AES) != 0;
}

static inline bool isar_feature_aa64_sve2_pmull128(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, AES) >= 2;
}

static inline bool isar_feature_aa64_sve2_bitperm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, BITPERM) != 0;
}

static inline bool isar_feature_aa64_sve_bf16(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, BFLOAT16) != 0;
}

static inline bool isar_feature_aa64_sve2_sha3(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, SHA3) != 0;
}

static inline bool isar_feature_aa64_sve2_sm4(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, SM4) != 0;
}

static inline bool isar_feature_aa64_sve_i8mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, I8MM) != 0;
}

static inline bool isar_feature_aa64_sve_f32mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, F32MM) != 0;
}

static inline bool isar_feature_aa64_sve_f64mm(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64zfr0, ID_AA64ZFR0, F64MM) != 0;
}

static inline bool isar_feature_aa64_sme_f64f64(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64smfr0, ID_AA64SMFR0, F64F64);
}

static inline bool isar_feature_aa64_sme_i16i64(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64smfr0, ID_AA64SMFR0, I16I64) == 0xf;
}

static inline bool isar_feature_aa64_sme_fa64(const ARMISARegisters *id)
{
    return FIELD_EX64(id->id_aa64smfr0, ID_AA64SMFR0, FA64);
}

/*
 * Feature tests for "does this exist in either 32-bit or 64-bit?"
 */
static inline bool isar_feature_any_fp16(const ARMISARegisters *id)
{
    return isar_feature_aa64_fp16(id) || isar_feature_aa32_fp16_arith(id);
}

static inline bool isar_feature_any_predinv(const ARMISARegisters *id)
{
    return isar_feature_aa64_predinv(id) || isar_feature_aa32_predinv(id);
}

static inline bool isar_feature_any_pmuv3p1(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmuv3p1(id) || isar_feature_aa32_pmuv3p1(id);
}

static inline bool isar_feature_any_pmuv3p4(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmuv3p4(id) || isar_feature_aa32_pmuv3p4(id);
}

static inline bool isar_feature_any_pmuv3p5(const ARMISARegisters *id)
{
    return isar_feature_aa64_pmuv3p5(id) || isar_feature_aa32_pmuv3p5(id);
}

static inline bool isar_feature_any_ccidx(const ARMISARegisters *id)
{
    return isar_feature_aa64_ccidx(id) || isar_feature_aa32_ccidx(id);
}

static inline bool isar_feature_any_tts2uxn(const ARMISARegisters *id)
{
    return isar_feature_aa64_tts2uxn(id) || isar_feature_aa32_tts2uxn(id);
}

static inline bool isar_feature_any_debugv8p2(const ARMISARegisters *id)
{
    return isar_feature_aa64_debugv8p2(id) || isar_feature_aa32_debugv8p2(id);
}

static inline bool isar_feature_any_ras(const ARMISARegisters *id)
{
    return isar_feature_aa64_ras(id) || isar_feature_aa32_ras(id);
}

static inline bool isar_feature_any_half_evt(const ARMISARegisters *id)
{
    return isar_feature_aa64_half_evt(id) || isar_feature_aa32_half_evt(id);
}

static inline bool isar_feature_any_evt(const ARMISARegisters *id)
{
    return isar_feature_aa64_evt(id) || isar_feature_aa32_evt(id);
}

typedef enum {
    CCSIDR_FORMAT_LEGACY,
    CCSIDR_FORMAT_CCIDX,
} CCSIDRFormat;

static inline uint64_t make_ccsidr(CCSIDRFormat format, unsigned assoc,
                                   unsigned linesize, unsigned cachesize,
                                   uint8_t flags)
{
    unsigned lg_linesize = ctz32(linesize);
    unsigned sets;
    uint64_t ccsidr = 0;

    assert(assoc != 0);
    assert(is_power_of_2(linesize));
    assert(lg_linesize >= 4 && lg_linesize <= 7 + 4);

    /* sets * associativity * linesize == cachesize. */
    sets = cachesize / (assoc * linesize);
    assert(cachesize % (assoc * linesize) == 0);

    if (format == CCSIDR_FORMAT_LEGACY) {
        /*
         * The 32-bit CCSIDR format is:
         *   [27:13] number of sets - 1
         *   [12:3]  associativity - 1
         *   [2:0]   log2(linesize) - 4
         *           so 0 == 16 bytes, 1 == 32 bytes, 2 == 64 bytes, etc
         */
        ccsidr = deposit32(ccsidr, 28,  4, flags);
        ccsidr = deposit32(ccsidr, 13, 15, sets - 1);
        ccsidr = deposit32(ccsidr,  3, 10, assoc - 1);
        ccsidr = deposit32(ccsidr,  0,  3, lg_linesize - 4);
    } else {
        /*
         * The 64-bit CCSIDR_EL1 format is:
         *   [55:32] number of sets - 1
         *   [23:3]  associativity - 1
         *   [2:0]   log2(linesize) - 4
         *           so 0 == 16 bytes, 1 == 32 bytes, 2 == 64 bytes, etc
         */
        ccsidr = deposit64(ccsidr, 32, 24, sets - 1);
        ccsidr = deposit64(ccsidr,  3, 21, assoc - 1);
        ccsidr = deposit64(ccsidr,  0,  3, lg_linesize - 4);
    }

    return ccsidr;
}

/*
 * Forward to the above feature tests given an ARMCPU pointer.
 */
#define cpu_isar_feature(name, cpu) \
    ({ ARMCPU *cpu_ = (cpu); isar_feature_##name(&cpu_->isar); })

#endif
