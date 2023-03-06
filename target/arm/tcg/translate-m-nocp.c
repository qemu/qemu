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
#include "tcg/tcg-op-gvec.h"
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

    s->eci_handled = true;

    /* If no fpu, NOP. */
    if (!dc_isar_feature(aa32_vfp, s)) {
        clear_eci_state(s);
        return true;
    }

    fptr = load_reg(s, a->rn);
    if (a->l) {
        gen_helper_v7m_vlldm(cpu_env, fptr);
    } else {
        gen_helper_v7m_vlstm(cpu_env, fptr);
    }

    clear_eci_state(s);

    /*
     * End the TB, because we have updated FP control bits,
     * and possibly VPR or LTPSIZE.
     */
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

    s->eci_handled = true;

    if (!dc_isar_feature(aa32_vfp_simd, s)) {
        /* NOP if we have neither FP nor MVE */
        clear_eci_state(s);
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
    tcg_gen_brcondi_i32(TCG_COND_EQ, sfpa, 0, s->condlabel.label);

    if (s->fp_excp_el != 0) {
        gen_exception_insn_el(s, 0, EXCP_NOCP,
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
    zero = tcg_constant_i64(0);
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
    if (dc_isar_feature(aa32_mve, s)) {
        store_cpu_field(tcg_constant_i32(0), v7m.vpr);
    }

    clear_eci_state(s);
    return true;
}

/*
 * M-profile provides two different sets of instructions that can
 * access floating point system registers: VMSR/VMRS (which move
 * to/from a general purpose register) and VLDR/VSTR sysreg (which
 * move directly to/from memory). In some cases there are also side
 * effects which must happen after any write to memory (which could
 * cause an exception). So we implement the common logic for the
 * sysreg access in gen_M_fp_sysreg_write() and gen_M_fp_sysreg_read(),
 * which take pointers to callback functions which will perform the
 * actual "read/write general purpose register" and "read/write
 * memory" operations.
 */

/*
 * Emit code to store the sysreg to its final destination; frees the
 * TCG temp 'value' it is passed. do_access is true to do the store,
 * and false to skip it and only perform side-effects like base
 * register writeback.
 */
typedef void fp_sysreg_storefn(DisasContext *s, void *opaque, TCGv_i32 value,
                               bool do_access);
/*
 * Emit code to load the value to be copied to the sysreg; returns
 * a new TCG temporary. do_access is true to do the store,
 * and false to skip it and only perform side-effects like base
 * register writeback.
 */
typedef TCGv_i32 fp_sysreg_loadfn(DisasContext *s, void *opaque,
                                  bool do_access);

/* Common decode/access checks for fp sysreg read/write */
typedef enum FPSysRegCheckResult {
    FPSysRegCheckFailed, /* caller should return false */
    FPSysRegCheckDone, /* caller should return true */
    FPSysRegCheckContinue, /* caller should continue generating code */
} FPSysRegCheckResult;

static FPSysRegCheckResult fp_sysreg_checks(DisasContext *s, int regno)
{
    if (!dc_isar_feature(aa32_fpsp_v2, s) && !dc_isar_feature(aa32_mve, s)) {
        return FPSysRegCheckFailed;
    }

    switch (regno) {
    case ARM_VFP_FPSCR:
    case QEMU_VFP_FPSCR_NZCV:
        break;
    case ARM_VFP_FPSCR_NZCVQC:
        if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
            return FPSysRegCheckFailed;
        }
        break;
    case ARM_VFP_FPCXT_S:
    case ARM_VFP_FPCXT_NS:
        if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
            return FPSysRegCheckFailed;
        }
        if (!s->v8m_secure) {
            return FPSysRegCheckFailed;
        }
        break;
    case ARM_VFP_VPR:
    case ARM_VFP_P0:
        if (!dc_isar_feature(aa32_mve, s)) {
            return FPSysRegCheckFailed;
        }
        break;
    default:
        return FPSysRegCheckFailed;
    }

    /*
     * FPCXT_NS is a special case: it has specific handling for
     * "current FP state is inactive", and must do the PreserveFPState()
     * but not the usual full set of actions done by ExecuteFPCheck().
     * So we don't call vfp_access_check() and the callers must handle this.
     */
    if (regno != ARM_VFP_FPCXT_NS && !vfp_access_check(s)) {
        return FPSysRegCheckDone;
    }
    return FPSysRegCheckContinue;
}

static void gen_branch_fpInactive(DisasContext *s, TCGCond cond,
                                  TCGLabel *label)
{
    /*
     * FPCXT_NS is a special case: it has specific handling for
     * "current FP state is inactive", and must do the PreserveFPState()
     * but not the usual full set of actions done by ExecuteFPCheck().
     * We don't have a TB flag that matches the fpInactive check, so we
     * do it at runtime as we don't expect FPCXT_NS accesses to be frequent.
     *
     * Emit code that checks fpInactive and does a conditional
     * branch to label based on it:
     *  if cond is TCG_COND_NE then branch if fpInactive != 0 (ie if inactive)
     *  if cond is TCG_COND_EQ then branch if fpInactive == 0 (ie if active)
     */
    assert(cond == TCG_COND_EQ || cond == TCG_COND_NE);

    /* fpInactive = FPCCR_NS.ASPEN == 1 && CONTROL.FPCA == 0 */
    TCGv_i32 aspen, fpca;
    aspen = load_cpu_field(v7m.fpccr[M_REG_NS]);
    fpca = load_cpu_field(v7m.control[M_REG_S]);
    tcg_gen_andi_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_xori_i32(aspen, aspen, R_V7M_FPCCR_ASPEN_MASK);
    tcg_gen_andi_i32(fpca, fpca, R_V7M_CONTROL_FPCA_MASK);
    tcg_gen_or_i32(fpca, fpca, aspen);
    tcg_gen_brcondi_i32(tcg_invert_cond(cond), fpca, 0, label);
}

static bool gen_M_fp_sysreg_write(DisasContext *s, int regno,
                                  fp_sysreg_loadfn *loadfn,
                                  void *opaque)
{
    /* Do a write to an M-profile floating point system register */
    TCGv_i32 tmp;
    TCGLabel *lab_end = NULL;

    switch (fp_sysreg_checks(s, regno)) {
    case FPSysRegCheckFailed:
        return false;
    case FPSysRegCheckDone:
        return true;
    case FPSysRegCheckContinue:
        break;
    }

    switch (regno) {
    case ARM_VFP_FPSCR:
        tmp = loadfn(s, opaque, true);
        gen_helper_vfp_set_fpscr(cpu_env, tmp);
        gen_lookup_tb(s);
        break;
    case ARM_VFP_FPSCR_NZCVQC:
    {
        TCGv_i32 fpscr;
        tmp = loadfn(s, opaque, true);
        if (dc_isar_feature(aa32_mve, s)) {
            /* QC is only present for MVE; otherwise RES0 */
            TCGv_i32 qc = tcg_temp_new_i32();
            tcg_gen_andi_i32(qc, tmp, FPCR_QC);
            /*
             * The 4 vfp.qc[] fields need only be "zero" vs "non-zero";
             * here writing the same value into all elements is simplest.
             */
            tcg_gen_gvec_dup_i32(MO_32, offsetof(CPUARMState, vfp.qc),
                                 16, 16, qc);
        }
        tcg_gen_andi_i32(tmp, tmp, FPCR_NZCV_MASK);
        fpscr = load_cpu_field(vfp.xregs[ARM_VFP_FPSCR]);
        tcg_gen_andi_i32(fpscr, fpscr, ~FPCR_NZCV_MASK);
        tcg_gen_or_i32(fpscr, fpscr, tmp);
        store_cpu_field(fpscr, vfp.xregs[ARM_VFP_FPSCR]);
        break;
    }
    case ARM_VFP_FPCXT_NS:
    {
        TCGLabel *lab_active = gen_new_label();

        lab_end = gen_new_label();
        gen_branch_fpInactive(s, TCG_COND_EQ, lab_active);
        /*
         * fpInactive case: write is a NOP, so only do side effects
         * like register writeback before we branch to end
         */
        loadfn(s, opaque, false);
        tcg_gen_br(lab_end);

        gen_set_label(lab_active);
        /*
         * !fpInactive: if FPU disabled, take NOCP exception;
         * otherwise PreserveFPState(), and then FPCXT_NS writes
         * behave the same as FPCXT_S writes.
         */
        if (!vfp_access_check_m(s, true)) {
            /*
             * This was only a conditional exception, so override
             * gen_exception_insn_el()'s default to DISAS_NORETURN
             */
            s->base.is_jmp = DISAS_NEXT;
            break;
        }
    }
    /* fall through */
    case ARM_VFP_FPCXT_S:
    {
        TCGv_i32 sfpa, control;
        /*
         * Set FPSCR and CONTROL.SFPA from value; the new FPSCR takes
         * bits [27:0] from value and zeroes bits [31:28].
         */
        tmp = loadfn(s, opaque, true);
        sfpa = tcg_temp_new_i32();
        tcg_gen_shri_i32(sfpa, tmp, 31);
        control = load_cpu_field(v7m.control[M_REG_S]);
        tcg_gen_deposit_i32(control, control, sfpa,
                            R_V7M_CONTROL_SFPA_SHIFT, 1);
        store_cpu_field(control, v7m.control[M_REG_S]);
        tcg_gen_andi_i32(tmp, tmp, ~FPCR_NZCV_MASK);
        gen_helper_vfp_set_fpscr(cpu_env, tmp);
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        break;
    }
    case ARM_VFP_VPR:
        /* Behaves as NOP if not privileged */
        if (IS_USER(s)) {
            loadfn(s, opaque, false);
            break;
        }
        tmp = loadfn(s, opaque, true);
        store_cpu_field(tmp, v7m.vpr);
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        break;
    case ARM_VFP_P0:
    {
        TCGv_i32 vpr;
        tmp = loadfn(s, opaque, true);
        vpr = load_cpu_field(v7m.vpr);
        tcg_gen_deposit_i32(vpr, vpr, tmp,
                            R_V7M_VPR_P0_SHIFT, R_V7M_VPR_P0_LENGTH);
        store_cpu_field(vpr, v7m.vpr);
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        break;
    }
    default:
        g_assert_not_reached();
    }
    if (lab_end) {
        gen_set_label(lab_end);
    }
    return true;
}

static bool gen_M_fp_sysreg_read(DisasContext *s, int regno,
                                 fp_sysreg_storefn *storefn,
                                 void *opaque)
{
    /* Do a read from an M-profile floating point system register */
    TCGv_i32 tmp;
    TCGLabel *lab_end = NULL;
    bool lookup_tb = false;

    switch (fp_sysreg_checks(s, regno)) {
    case FPSysRegCheckFailed:
        return false;
    case FPSysRegCheckDone:
        return true;
    case FPSysRegCheckContinue:
        break;
    }

    if (regno == ARM_VFP_FPSCR_NZCVQC && !dc_isar_feature(aa32_mve, s)) {
        /* QC is RES0 without MVE, so NZCVQC simplifies to NZCV */
        regno = QEMU_VFP_FPSCR_NZCV;
    }

    switch (regno) {
    case ARM_VFP_FPSCR:
        tmp = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(tmp, cpu_env);
        storefn(s, opaque, tmp, true);
        break;
    case ARM_VFP_FPSCR_NZCVQC:
        tmp = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(tmp, cpu_env);
        tcg_gen_andi_i32(tmp, tmp, FPCR_NZCVQC_MASK);
        storefn(s, opaque, tmp, true);
        break;
    case QEMU_VFP_FPSCR_NZCV:
        /*
         * Read just NZCV; this is a special case to avoid the
         * helper call for the "VMRS to CPSR.NZCV" insn.
         */
        tmp = load_cpu_field(vfp.xregs[ARM_VFP_FPSCR]);
        tcg_gen_andi_i32(tmp, tmp, FPCR_NZCV_MASK);
        storefn(s, opaque, tmp, true);
        break;
    case ARM_VFP_FPCXT_S:
    {
        TCGv_i32 control, sfpa, fpscr;
        /* Bits [27:0] from FPSCR, bit [31] from CONTROL.SFPA */
        tmp = tcg_temp_new_i32();
        sfpa = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(tmp, cpu_env);
        tcg_gen_andi_i32(tmp, tmp, ~FPCR_NZCV_MASK);
        control = load_cpu_field(v7m.control[M_REG_S]);
        tcg_gen_andi_i32(sfpa, control, R_V7M_CONTROL_SFPA_MASK);
        tcg_gen_shli_i32(sfpa, sfpa, 31 - R_V7M_CONTROL_SFPA_SHIFT);
        tcg_gen_or_i32(tmp, tmp, sfpa);
        /*
         * Store result before updating FPSCR etc, in case
         * it is a memory write which causes an exception.
         */
        storefn(s, opaque, tmp, true);
        /*
         * Now we must reset FPSCR from FPDSCR_NS, and clear
         * CONTROL.SFPA; so we'll end the TB here.
         */
        tcg_gen_andi_i32(control, control, ~R_V7M_CONTROL_SFPA_MASK);
        store_cpu_field(control, v7m.control[M_REG_S]);
        fpscr = load_cpu_field(v7m.fpdscr[M_REG_NS]);
        gen_helper_vfp_set_fpscr(cpu_env, fpscr);
        lookup_tb = true;
        break;
    }
    case ARM_VFP_FPCXT_NS:
    {
        TCGv_i32 control, sfpa, fpscr, fpdscr;
        TCGLabel *lab_active = gen_new_label();

        lookup_tb = true;

        gen_branch_fpInactive(s, TCG_COND_EQ, lab_active);
        /* fpInactive case: reads as FPDSCR_NS */
        TCGv_i32 tmp = load_cpu_field(v7m.fpdscr[M_REG_NS]);
        storefn(s, opaque, tmp, true);
        lab_end = gen_new_label();
        tcg_gen_br(lab_end);

        gen_set_label(lab_active);
        /*
         * !fpInactive: if FPU disabled, take NOCP exception;
         * otherwise PreserveFPState(), and then FPCXT_NS
         * reads the same as FPCXT_S.
         */
        if (!vfp_access_check_m(s, true)) {
            /*
             * This was only a conditional exception, so override
             * gen_exception_insn_el()'s default to DISAS_NORETURN
             */
            s->base.is_jmp = DISAS_NEXT;
            break;
        }
        tmp = tcg_temp_new_i32();
        sfpa = tcg_temp_new_i32();
        fpscr = tcg_temp_new_i32();
        gen_helper_vfp_get_fpscr(fpscr, cpu_env);
        tcg_gen_andi_i32(tmp, fpscr, ~FPCR_NZCV_MASK);
        control = load_cpu_field(v7m.control[M_REG_S]);
        tcg_gen_andi_i32(sfpa, control, R_V7M_CONTROL_SFPA_MASK);
        tcg_gen_shli_i32(sfpa, sfpa, 31 - R_V7M_CONTROL_SFPA_SHIFT);
        tcg_gen_or_i32(tmp, tmp, sfpa);
        /* Store result before updating FPSCR, in case it faults */
        storefn(s, opaque, tmp, true);
        /* If SFPA is zero then set FPSCR from FPDSCR_NS */
        fpdscr = load_cpu_field(v7m.fpdscr[M_REG_NS]);
        tcg_gen_movcond_i32(TCG_COND_EQ, fpscr, sfpa, tcg_constant_i32(0),
                            fpdscr, fpscr);
        gen_helper_vfp_set_fpscr(cpu_env, fpscr);
        break;
    }
    case ARM_VFP_VPR:
        /* Behaves as NOP if not privileged */
        if (IS_USER(s)) {
            storefn(s, opaque, NULL, false);
            break;
        }
        tmp = load_cpu_field(v7m.vpr);
        storefn(s, opaque, tmp, true);
        break;
    case ARM_VFP_P0:
        tmp = load_cpu_field(v7m.vpr);
        tcg_gen_extract_i32(tmp, tmp, R_V7M_VPR_P0_SHIFT, R_V7M_VPR_P0_LENGTH);
        storefn(s, opaque, tmp, true);
        break;
    default:
        g_assert_not_reached();
    }

    if (lab_end) {
        gen_set_label(lab_end);
    }
    if (lookup_tb) {
        gen_lookup_tb(s);
    }
    return true;
}

static void fp_sysreg_to_gpr(DisasContext *s, void *opaque, TCGv_i32 value,
                             bool do_access)
{
    arg_VMSR_VMRS *a = opaque;

    if (!do_access) {
        return;
    }

    if (a->rt == 15) {
        /* Set the 4 flag bits in the CPSR */
        gen_set_nzcv(value);
    } else {
        store_reg(s, a->rt, value);
    }
}

static TCGv_i32 gpr_to_fp_sysreg(DisasContext *s, void *opaque, bool do_access)
{
    arg_VMSR_VMRS *a = opaque;

    if (!do_access) {
        return NULL;
    }
    return load_reg(s, a->rt);
}

static bool trans_VMSR_VMRS(DisasContext *s, arg_VMSR_VMRS *a)
{
    /*
     * Accesses to R15 are UNPREDICTABLE; we choose to undef.
     * FPSCR -> r15 is a special case which writes to the PSR flags;
     * set a->reg to a special value to tell gen_M_fp_sysreg_read()
     * we only care about the top 4 bits of FPSCR there.
     */
    if (a->rt == 15) {
        if (a->l && a->reg == ARM_VFP_FPSCR) {
            a->reg = QEMU_VFP_FPSCR_NZCV;
        } else {
            return false;
        }
    }

    if (a->l) {
        /* VMRS, move FP system register to gp register */
        return gen_M_fp_sysreg_read(s, a->reg, fp_sysreg_to_gpr, a);
    } else {
        /* VMSR, move gp register to FP system register */
        return gen_M_fp_sysreg_write(s, a->reg, gpr_to_fp_sysreg, a);
    }
}

static void fp_sysreg_to_memory(DisasContext *s, void *opaque, TCGv_i32 value,
                                bool do_access)
{
    arg_vldr_sysreg *a = opaque;
    uint32_t offset = a->imm;
    TCGv_i32 addr;

    if (!a->a) {
        offset = -offset;
    }

    if (!do_access && !a->w) {
        return;
    }

    addr = load_reg(s, a->rn);
    if (a->p) {
        tcg_gen_addi_i32(addr, addr, offset);
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        gen_helper_v8m_stackcheck(cpu_env, addr);
    }

    if (do_access) {
        gen_aa32_st_i32(s, value, addr, get_mem_index(s),
                        MO_UL | MO_ALIGN | s->be_data);
    }

    if (a->w) {
        /* writeback */
        if (!a->p) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    }
}

static TCGv_i32 memory_to_fp_sysreg(DisasContext *s, void *opaque,
                                    bool do_access)
{
    arg_vldr_sysreg *a = opaque;
    uint32_t offset = a->imm;
    TCGv_i32 addr;
    TCGv_i32 value = NULL;

    if (!a->a) {
        offset = -offset;
    }

    if (!do_access && !a->w) {
        return NULL;
    }

    addr = load_reg(s, a->rn);
    if (a->p) {
        tcg_gen_addi_i32(addr, addr, offset);
    }

    if (s->v8m_stackcheck && a->rn == 13 && a->w) {
        gen_helper_v8m_stackcheck(cpu_env, addr);
    }

    if (do_access) {
        value = tcg_temp_new_i32();
        gen_aa32_ld_i32(s, value, addr, get_mem_index(s),
                        MO_UL | MO_ALIGN | s->be_data);
    }

    if (a->w) {
        /* writeback */
        if (!a->p) {
            tcg_gen_addi_i32(addr, addr, offset);
        }
        store_reg(s, a->rn, addr);
    }
    return value;
}

static bool trans_VLDR_sysreg(DisasContext *s, arg_vldr_sysreg *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        return false;
    }
    if (a->rn == 15) {
        return false;
    }
    return gen_M_fp_sysreg_write(s, a->reg, memory_to_fp_sysreg, a);
}

static bool trans_VSTR_sysreg(DisasContext *s, arg_vldr_sysreg *a)
{
    if (!arm_dc_feature(s, ARM_FEATURE_V8_1M)) {
        return false;
    }
    if (a->rn == 15) {
        return false;
    }
    return gen_M_fp_sysreg_read(s, a->reg, fp_sysreg_to_memory, a);
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
        gen_exception_insn(s, 0, EXCP_NOCP, syn_uncategorized());
        return true;
    }

    if (s->fp_excp_el != 0) {
        gen_exception_insn_el(s, 0, EXCP_NOCP,
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
