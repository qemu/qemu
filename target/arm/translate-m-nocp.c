/*
 *  ARM translation: M-profile NOCP special-case instructions
 *
 *  Copyright (c) 2020 Linaro, Ltd.
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

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "translate.h"
#include "translate-a32.h"

#include "decode-m-nocp.c.inc"

/*
 * Decode VLLDM and VLSTM are nonstandard because:
 *  * if there is no FPU then these insns must NOP in
 *    Secure state and UNDEF in Nonsecure state
 *  * if there is an FPU then these insns do not have
 *    the usual behaviour that vfp_access_check() provides of
 *    being controlled by CPACR/NSACR enable bits or the
 *    lazy-stacking logic.
 */
static bool trans_VLLDM_VLSTM(DisasContext *s, arg_VLLDM_VLSTM *a)
{
    TCGv_i32 fptr;

    if (!arm_dc_feature(s, ARM_FEATURE_M) ||
        !arm_dc_feature(s, ARM_FEATURE_V8)) {
        return false;
    }

    if (a->op) {
        /*
         * T2 encoding ({D0-D31} reglist): v8.1M and up. We choose not
         * to take the IMPDEF option to make memory accesses to the stack
         * slots that correspond to the D16-D31 registers (discarding
         * read data and writing UNKNOWN values), so for us the T2
         * encoding behaves identically to the T1 encoding.
         */
        if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
            return false;
        }
    } else {
        /*
         * T1 encoding ({D0-D15} reglist); undef if we have 32 Dregs.
         * This is currently architecturally impossible, but we add the
         * check to stay in line with the pseudocode. Note that we must
         * emit code for the UNDEF so it takes precedence over the NOCP.
         */
        if (dc_isar_feature(aa32_simd_r32, s)) {
            unallocated_encoding(s);
            return true;
        }
    }

    /*
     * If not secure, UNDEF. We must emit code for this
     * rather than returning false so that this takes
     * precedence over the m-nocp.decode NOCP fallback.
     */
    if (!s->v8m_secure) {
        unallocated_encoding(s);
        return true;
    }
    /* If no fpu, NOP. */
    if (!dc_isar_feature(aa32_vfp, s)) {
        return true;
    }

    fptr = load_reg(s, a->rn);
    if (a->l) {
        gen_helper_v7m_vlldm(cpu_env, fptr);
    } else {
        gen_helper_v7m_vlstm(cpu_env, fptr);
    }
    tcg_temp_free_i32(fptr);

    /* End the TB, because we have updated FP control bits */
    s->base.is_jmp = DISAS_UPDATE_EXIT;
    return true;
}

static bool trans_VSCCLRM(DisasContext *s, arg_VSCCLRM *a)
{
    int btmreg, topreg;
    TCGv_i64 zero;
    TCGv_i32 aspen, sfpa;

    if (!dc_isar_feature(aa32_m_sec_state, s)) {
        /* Before v8.1M, fall through in decode to NOCP check */
        return false;
    }

    /* Explicitly UNDEF because this takes precedence over NOCP */
    if (!arm_dc_feature(s, ARM_FEATURE_M_MAIN) || !s->v8m_secure) {
        unallocated_encoding(s);
        return true;
    }

    if (!dc_isar_feature(aa32_vfp_simd, s)) {
        /* NOP if we have neither FP nor MVE */
        return true;
    }

    /*
     * If FPCCR.ASPEN != 0 && CONTROL_S.SFPA == 0 then there is no
     * active floating point context so we must NOP (without doing
     * any lazy state preservation or the NOCP check).
     */
    aspen = load_cpu_field(v7m.fpccr[M_REG_S]);
    sfpa = load_cpu_field(v7m.control[M_REG_S]);
    tcg_gen_andi_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_xori_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_andi_i32(sfpa, sfpa, R_V7M_CONTROL_SFPA_MASK);
    tcg_gen_or_i32(sfpa, sfpa, aspen);
    arm_gen_condlabel(s);
    tcg_gen_brcondi_i32(TCG_COND_EQ, sfpa, 0, s->condlabel);

    if (s->fp_excp_el != 0) {
        gen_exception_insn(s, s->pc_curr, EXCP_NOCP,
                           syn_uncategorized(), s->fp_excp_el);
        return true;
    }

    topreg = a->vd + a->imm - 1;
    btmreg = a->vd;

    /* Convert to Sreg numbers if the insn specified in Dregs */
    if (a->size == 3) {
        topreg = topreg * 2 + 1;
        btmreg *= 2;
    }

    if (topreg > 63 || (topreg > 31 && !(topreg & 1))) {
        /* UNPREDICTABLE: we choose to undef */
        unallocated_encoding(s);
        return true;
    }

    /* Silently ignore requests to clear D16-D31 if they don't exist */
    if (topreg > 31 && !dc_isar_feature(aa32_simd_r32, s)) {
        topreg = 31;
    }

    if (!vfp_access_check(s)) {
        return true;
    }

    /* Zero the Sregs from btmreg to topreg inclusive. */
    zero = tcg_const_i64(0);
    if (btmreg & 1) {
        write_neon_element64(zero, btmreg >> 1, 1, MO_32);
        btmreg++;
    }
    for (; btmreg + 1 <= topreg; btmreg += 2) {
        write_neon_element64(zero, btmreg >> 1, 0, MO_64);
    }
    if (btmreg == topreg) {
        write_neon_element64(zero, btmreg >> 1, 0, MO_32);
        btmreg++;
    }
    assert(btmreg == topreg + 1);
    /* TODO: when MVE is implemented, zero VPR here */
    return true;
}

static bool trans_NOCP(DisasContext *s, arg_nocp *a)
{
    /*
     * Handle M-profile early check for disabled coprocessor:
     * all we need to do here is emit the NOCP exception if
     * the coprocessor is disabled. Otherwise we return false
     * and the real VFP/etc decode will handle the insn.
     */
    assert(arm_dc_feature(s, ARM_FEATURE_M));

    if (a->cp == 11) {
        a->cp = 10;
    }
    if (arm_dc_feature(s, ARM_FEATURE_V8_1M) &&
        (a->cp == 8 || a->cp == 9 || a->cp == 14 || a->cp == 15)) {
        /* in v8.1M cp 8, 9, 14, 15 also are governed by the cp10 enable */
        a->cp = 10;
    }

    if (a->cp != 10) {
        gen_exception_insn(s, s->pc_curr, EXCP_NOCP,
                           syn_uncategorized(), default_exception_el(s));
        return true;
    }

    if (s->fp_excp_el != 0) {
        gen_exception_insn(s, s->pc_curr, EXCP_NOCP,
                           syn_uncategorized(), s->fp_excp_el);
        return true;
    }

    return false;
}

static bool trans_NOCP_8_1(DisasContext *s, arg_nocp *a)
{
    /* This range needs a coprocessor check for v8.1M and later only */
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        return false;
    }
    return trans_NOCP(s, a);
}
