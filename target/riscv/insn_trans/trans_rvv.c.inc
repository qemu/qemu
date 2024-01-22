/*
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "tcg/tcg-op-gvec.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"

static inline bool is_overlapped(const int8_t astart, int8_t asize,
                                 const int8_t bstart, int8_t bsize)
{
    const int8_t aend = astart + asize;
    const int8_t bend = bstart + bsize;

    return MAX(aend, bend) - MIN(astart, bstart) < asize + bsize;
}

static bool require_rvv(DisasContext *s)
{
    return s->mstatus_vs != EXT_STATUS_DISABLED;
}

static bool require_rvf(DisasContext *s)
{
    if (s->mstatus_fs == EXT_STATUS_DISABLED) {
        return false;
    }

    switch (s->sew) {
    case MO_16:
        return s->cfg_ptr->ext_zvfh;
    case MO_32:
        return s->cfg_ptr->ext_zve32f;
    case MO_64:
        return s->cfg_ptr->ext_zve64d;
    default:
        return false;
    }
}

static bool require_scale_rvf(DisasContext *s)
{
    if (s->mstatus_fs == EXT_STATUS_DISABLED) {
        return false;
    }

    switch (s->sew) {
    case MO_8:
        return s->cfg_ptr->ext_zvfh;
    case MO_16:
        return s->cfg_ptr->ext_zve32f;
    case MO_32:
        return s->cfg_ptr->ext_zve64d;
    default:
        return false;
    }
}

static bool require_scale_rvfmin(DisasContext *s)
{
    if (s->mstatus_fs == EXT_STATUS_DISABLED) {
        return false;
    }

    switch (s->sew) {
    case MO_8:
        return s->cfg_ptr->ext_zvfhmin;
    case MO_16:
        return s->cfg_ptr->ext_zve32f;
    case MO_32:
        return s->cfg_ptr->ext_zve64d;
    default:
        return false;
    }
}

/* Destination vector register group cannot overlap source mask register. */
static bool require_vm(int vm, int vd)
{
    return (vm != 0 || vd != 0);
}

static bool require_nf(int vd, int nf, int lmul)
{
    int size = nf << MAX(lmul, 0);
    return size <= 8 && vd + size <= 32;
}

/*
 * Vector register should aligned with the passed-in LMUL (EMUL).
 * If LMUL < 0, i.e. fractional LMUL, any vector register is allowed.
 */
static bool require_align(const int8_t val, const int8_t lmul)
{
    return lmul <= 0 || extract32(val, 0, lmul) == 0;
}

/*
 * A destination vector register group can overlap a source vector
 * register group only if one of the following holds:
 *  1. The destination EEW equals the source EEW.
 *  2. The destination EEW is smaller than the source EEW and the overlap
 *     is in the lowest-numbered part of the source register group.
 *  3. The destination EEW is greater than the source EEW, the source EMUL
 *     is at least 1, and the overlap is in the highest-numbered part of
 *     the destination register group.
 * (Section 5.2)
 *
 * This function returns true if one of the following holds:
 *  * Destination vector register group does not overlap a source vector
 *    register group.
 *  * Rule 3 met.
 * For rule 1, overlap is allowed so this function doesn't need to be called.
 * For rule 2, (vd == vs). Caller has to check whether: (vd != vs) before
 * calling this function.
 */
static bool require_noover(const int8_t dst, const int8_t dst_lmul,
                           const int8_t src, const int8_t src_lmul)
{
    int8_t dst_size = dst_lmul <= 0 ? 1 : 1 << dst_lmul;
    int8_t src_size = src_lmul <= 0 ? 1 : 1 << src_lmul;

    /* Destination EEW is greater than the source EEW, check rule 3. */
    if (dst_size > src_size) {
        if (dst < src &&
            src_lmul >= 0 &&
            is_overlapped(dst, dst_size, src, src_size) &&
            !is_overlapped(dst, dst_size, src + src_size, src_size)) {
            return true;
        }
    }

    return !is_overlapped(dst, dst_size, src, src_size);
}

static bool do_vsetvl(DisasContext *s, int rd, int rs1, TCGv s2)
{
    TCGv s1, dst;

    if (!require_rvv(s) || !s->cfg_ptr->ext_zve32f) {
        return false;
    }

    dst = dest_gpr(s, rd);

    if (rd == 0 && rs1 == 0) {
        s1 = tcg_temp_new();
        tcg_gen_mov_tl(s1, cpu_vl);
    } else if (rs1 == 0) {
        /* As the mask is at least one bit, RV_VLEN_MAX is >= VLMAX */
        s1 = tcg_constant_tl(RV_VLEN_MAX);
    } else {
        s1 = get_gpr(s, rs1, EXT_ZERO);
    }

    gen_helper_vsetvl(dst, tcg_env, s1, s2);
    gen_set_gpr(s, rd, dst);
    mark_vs_dirty(s);

    gen_update_pc(s, s->cur_insn_len);
    lookup_and_goto_ptr(s);
    s->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_vsetivli(DisasContext *s, int rd, TCGv s1, TCGv s2)
{
    TCGv dst;

    if (!require_rvv(s) || !s->cfg_ptr->ext_zve32f) {
        return false;
    }

    dst = dest_gpr(s, rd);

    gen_helper_vsetvl(dst, tcg_env, s1, s2);
    gen_set_gpr(s, rd, dst);
    mark_vs_dirty(s);
    gen_update_pc(s, s->cur_insn_len);
    lookup_and_goto_ptr(s);
    s->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_vsetvl(DisasContext *s, arg_vsetvl *a)
{
    TCGv s2 = get_gpr(s, a->rs2, EXT_ZERO);
    return do_vsetvl(s, a->rd, a->rs1, s2);
}

static bool trans_vsetvli(DisasContext *s, arg_vsetvli *a)
{
    TCGv s2 = tcg_constant_tl(a->zimm);
    return do_vsetvl(s, a->rd, a->rs1, s2);
}

static bool trans_vsetivli(DisasContext *s, arg_vsetivli *a)
{
    TCGv s1 = tcg_constant_tl(a->rs1);
    TCGv s2 = tcg_constant_tl(a->zimm);
    return do_vsetivli(s, a->rd, s1, s2);
}

/* vector register offset from env */
static uint32_t vreg_ofs(DisasContext *s, int reg)
{
    return offsetof(CPURISCVState, vreg) + reg * s->cfg_ptr->vlenb;
}

/* check functions */

/*
 * Vector unit-stride, strided, unit-stride segment, strided segment
 * store check function.
 *
 * Rules to be checked here:
 *   1. EMUL must within the range: 1/8 <= EMUL <= 8. (Section 7.3)
 *   2. Destination vector register number is multiples of EMUL.
 *      (Section 3.4.2, 7.3)
 *   3. The EMUL setting must be such that EMUL * NFIELDS ≤ 8. (Section 7.8)
 *   4. Vector register numbers accessed by the segment load or store
 *      cannot increment past 31. (Section 7.8)
 */
static bool vext_check_store(DisasContext *s, int vd, int nf, uint8_t eew)
{
    int8_t emul = eew - s->sew + s->lmul;
    return (emul >= -3 && emul <= 3) &&
           require_align(vd, emul) &&
           require_nf(vd, nf, emul);
}

/*
 * Vector unit-stride, strided, unit-stride segment, strided segment
 * load check function.
 *
 * Rules to be checked here:
 *   1. All rules applies to store instructions are applies
 *      to load instructions.
 *   2. Destination vector register group for a masked vector
 *      instruction cannot overlap the source mask register (v0).
 *      (Section 5.3)
 */
static bool vext_check_load(DisasContext *s, int vd, int nf, int vm,
                            uint8_t eew)
{
    return vext_check_store(s, vd, nf, eew) && require_vm(vm, vd);
}

/*
 * Vector indexed, indexed segment store check function.
 *
 * Rules to be checked here:
 *   1. EMUL must within the range: 1/8 <= EMUL <= 8. (Section 7.3)
 *   2. Index vector register number is multiples of EMUL.
 *      (Section 3.4.2, 7.3)
 *   3. Destination vector register number is multiples of LMUL.
 *      (Section 3.4.2, 7.3)
 *   4. The EMUL setting must be such that EMUL * NFIELDS ≤ 8. (Section 7.8)
 *   5. Vector register numbers accessed by the segment load or store
 *      cannot increment past 31. (Section 7.8)
 */
static bool vext_check_st_index(DisasContext *s, int vd, int vs2, int nf,
                                uint8_t eew)
{
    int8_t emul = eew - s->sew + s->lmul;
    bool ret = (emul >= -3 && emul <= 3) &&
               require_align(vs2, emul) &&
               require_align(vd, s->lmul) &&
               require_nf(vd, nf, s->lmul);

    /*
     * V extension supports all vector load and store instructions,
     * except V extension does not support EEW=64 for index values
     * when XLEN=32. (Section 18.3)
     */
    if (get_xl(s) == MXL_RV32) {
        ret &= (eew != MO_64);
    }

    return ret;
}

/*
 * Vector indexed, indexed segment load check function.
 *
 * Rules to be checked here:
 *   1. All rules applies to store instructions are applies
 *      to load instructions.
 *   2. Destination vector register group for a masked vector
 *      instruction cannot overlap the source mask register (v0).
 *      (Section 5.3)
 *   3. Destination vector register cannot overlap a source vector
 *      register (vs2) group.
 *      (Section 5.2)
 *   4. Destination vector register groups cannot overlap
 *      the source vector register (vs2) group for
 *      indexed segment load instructions. (Section 7.8.3)
 */
static bool vext_check_ld_index(DisasContext *s, int vd, int vs2,
                                int nf, int vm, uint8_t eew)
{
    int8_t seg_vd;
    int8_t emul = eew - s->sew + s->lmul;
    bool ret = vext_check_st_index(s, vd, vs2, nf, eew) &&
               require_vm(vm, vd);

    /* Each segment register group has to follow overlap rules. */
    for (int i = 0; i < nf; ++i) {
        seg_vd = vd + (1 << MAX(s->lmul, 0)) * i;

        if (eew > s->sew) {
            if (seg_vd != vs2) {
                ret &= require_noover(seg_vd, s->lmul, vs2, emul);
            }
        } else if (eew < s->sew) {
            ret &= require_noover(seg_vd, s->lmul, vs2, emul);
        }

        /*
         * Destination vector register groups cannot overlap
         * the source vector register (vs2) group for
         * indexed segment load instructions.
         */
        if (nf > 1) {
            ret &= !is_overlapped(seg_vd, 1 << MAX(s->lmul, 0),
                                  vs2, 1 << MAX(emul, 0));
        }
    }
    return ret;
}

static bool vext_check_ss(DisasContext *s, int vd, int vs, int vm)
{
    return require_vm(vm, vd) &&
           require_align(vd, s->lmul) &&
           require_align(vs, s->lmul);
}

/*
 * Check function for vector instruction with format:
 * single-width result and single-width sources (SEW = SEW op SEW)
 *
 * Rules to be checked here:
 *   1. Destination vector register group for a masked vector
 *      instruction cannot overlap the source mask register (v0).
 *      (Section 5.3)
 *   2. Destination vector register number is multiples of LMUL.
 *      (Section 3.4.2)
 *   3. Source (vs2, vs1) vector register number are multiples of LMUL.
 *      (Section 3.4.2)
 */
static bool vext_check_sss(DisasContext *s, int vd, int vs1, int vs2, int vm)
{
    return vext_check_ss(s, vd, vs2, vm) &&
           require_align(vs1, s->lmul);
}

static bool vext_check_ms(DisasContext *s, int vd, int vs)
{
    bool ret = require_align(vs, s->lmul);
    if (vd != vs) {
        ret &= require_noover(vd, 0, vs, s->lmul);
    }
    return ret;
}

/*
 * Check function for maskable vector instruction with format:
 * single-width result and single-width sources (SEW = SEW op SEW)
 *
 * Rules to be checked here:
 *   1. Source (vs2, vs1) vector register number are multiples of LMUL.
 *      (Section 3.4.2)
 *   2. Destination vector register cannot overlap a source vector
 *      register (vs2, vs1) group.
 *      (Section 5.2)
 *   3. The destination vector register group for a masked vector
 *      instruction cannot overlap the source mask register (v0),
 *      unless the destination vector register is being written
 *      with a mask value (e.g., comparisons) or the scalar result
 *      of a reduction. (Section 5.3)
 */
static bool vext_check_mss(DisasContext *s, int vd, int vs1, int vs2)
{
    bool ret = vext_check_ms(s, vd, vs2) &&
               require_align(vs1, s->lmul);
    if (vd != vs1) {
        ret &= require_noover(vd, 0, vs1, s->lmul);
    }
    return ret;
}

/*
 * Common check function for vector widening instructions
 * of double-width result (2*SEW).
 *
 * Rules to be checked here:
 *   1. The largest vector register group used by an instruction
 *      can not be greater than 8 vector registers (Section 5.2):
 *      => LMUL < 8.
 *      => SEW < 64.
 *   2. Double-width SEW cannot greater than ELEN.
 *   3. Destination vector register number is multiples of 2 * LMUL.
 *      (Section 3.4.2)
 *   4. Destination vector register group for a masked vector
 *      instruction cannot overlap the source mask register (v0).
 *      (Section 5.3)
 */
static bool vext_wide_check_common(DisasContext *s, int vd, int vm)
{
    return (s->lmul <= 2) &&
           (s->sew < MO_64) &&
           ((s->sew + 1) <= (s->cfg_ptr->elen >> 4)) &&
           require_align(vd, s->lmul + 1) &&
           require_vm(vm, vd);
}

/*
 * Common check function for vector narrowing instructions
 * of single-width result (SEW) and double-width source (2*SEW).
 *
 * Rules to be checked here:
 *   1. The largest vector register group used by an instruction
 *      can not be greater than 8 vector registers (Section 5.2):
 *      => LMUL < 8.
 *      => SEW < 64.
 *   2. Double-width SEW cannot greater than ELEN.
 *   3. Source vector register number is multiples of 2 * LMUL.
 *      (Section 3.4.2)
 *   4. Destination vector register number is multiples of LMUL.
 *      (Section 3.4.2)
 *   5. Destination vector register group for a masked vector
 *      instruction cannot overlap the source mask register (v0).
 *      (Section 5.3)
 */
static bool vext_narrow_check_common(DisasContext *s, int vd, int vs2,
                                     int vm)
{
    return (s->lmul <= 2) &&
           (s->sew < MO_64) &&
           ((s->sew + 1) <= (s->cfg_ptr->elen >> 4)) &&
           require_align(vs2, s->lmul + 1) &&
           require_align(vd, s->lmul) &&
           require_vm(vm, vd);
}

static bool vext_check_ds(DisasContext *s, int vd, int vs, int vm)
{
    return vext_wide_check_common(s, vd, vm) &&
           require_align(vs, s->lmul) &&
           require_noover(vd, s->lmul + 1, vs, s->lmul);
}

static bool vext_check_dd(DisasContext *s, int vd, int vs, int vm)
{
    return vext_wide_check_common(s, vd, vm) &&
           require_align(vs, s->lmul + 1);
}

/*
 * Check function for vector instruction with format:
 * double-width result and single-width sources (2*SEW = SEW op SEW)
 *
 * Rules to be checked here:
 *   1. All rules in defined in widen common rules are applied.
 *   2. Source (vs2, vs1) vector register number are multiples of LMUL.
 *      (Section 3.4.2)
 *   3. Destination vector register cannot overlap a source vector
 *      register (vs2, vs1) group.
 *      (Section 5.2)
 */
static bool vext_check_dss(DisasContext *s, int vd, int vs1, int vs2, int vm)
{
    return vext_check_ds(s, vd, vs2, vm) &&
           require_align(vs1, s->lmul) &&
           require_noover(vd, s->lmul + 1, vs1, s->lmul);
}

/*
 * Check function for vector instruction with format:
 * double-width result and double-width source1 and single-width
 * source2 (2*SEW = 2*SEW op SEW)
 *
 * Rules to be checked here:
 *   1. All rules in defined in widen common rules are applied.
 *   2. Source 1 (vs2) vector register number is multiples of 2 * LMUL.
 *      (Section 3.4.2)
 *   3. Source 2 (vs1) vector register number is multiples of LMUL.
 *      (Section 3.4.2)
 *   4. Destination vector register cannot overlap a source vector
 *      register (vs1) group.
 *      (Section 5.2)
 */
static bool vext_check_dds(DisasContext *s, int vd, int vs1, int vs2, int vm)
{
    return vext_check_ds(s, vd, vs1, vm) &&
           require_align(vs2, s->lmul + 1);
}

static bool vext_check_sd(DisasContext *s, int vd, int vs, int vm)
{
    bool ret = vext_narrow_check_common(s, vd, vs, vm);
    if (vd != vs) {
        ret &= require_noover(vd, s->lmul, vs, s->lmul + 1);
    }
    return ret;
}

/*
 * Check function for vector instruction with format:
 * single-width result and double-width source 1 and single-width
 * source 2 (SEW = 2*SEW op SEW)
 *
 * Rules to be checked here:
 *   1. All rules in defined in narrow common rules are applied.
 *   2. Destination vector register cannot overlap a source vector
 *      register (vs2) group.
 *      (Section 5.2)
 *   3. Source 2 (vs1) vector register number is multiples of LMUL.
 *      (Section 3.4.2)
 */
static bool vext_check_sds(DisasContext *s, int vd, int vs1, int vs2, int vm)
{
    return vext_check_sd(s, vd, vs2, vm) &&
           require_align(vs1, s->lmul);
}

/*
 * Check function for vector reduction instructions.
 *
 * Rules to be checked here:
 *   1. Source 1 (vs2) vector register number is multiples of LMUL.
 *      (Section 3.4.2)
 */
static bool vext_check_reduction(DisasContext *s, int vs2)
{
    return require_align(vs2, s->lmul) && s->vstart_eq_zero;
}

/*
 * Check function for vector slide instructions.
 *
 * Rules to be checked here:
 *   1. Source 1 (vs2) vector register number is multiples of LMUL.
 *      (Section 3.4.2)
 *   2. Destination vector register number is multiples of LMUL.
 *      (Section 3.4.2)
 *   3. Destination vector register group for a masked vector
 *      instruction cannot overlap the source mask register (v0).
 *      (Section 5.3)
 *   4. The destination vector register group for vslideup, vslide1up,
 *      vfslide1up, cannot overlap the source vector register (vs2) group.
 *      (Section 5.2, 16.3.1, 16.3.3)
 */
static bool vext_check_slide(DisasContext *s, int vd, int vs2,
                             int vm, bool is_over)
{
    bool ret = require_align(vs2, s->lmul) &&
               require_align(vd, s->lmul) &&
               require_vm(vm, vd);
    if (is_over) {
        ret &= (vd != vs2);
    }
    return ret;
}

/*
 * In cpu_get_tb_cpu_state(), set VILL if RVV was not present.
 * So RVV is also be checked in this function.
 */
static bool vext_check_isa_ill(DisasContext *s)
{
    return !s->vill;
}

/* common translation macro */
#define GEN_VEXT_TRANS(NAME, EEW, ARGTYPE, OP, CHECK)        \
static bool trans_##NAME(DisasContext *s, arg_##ARGTYPE * a) \
{                                                            \
    if (CHECK(s, a, EEW)) {                                  \
        return OP(s, a, EEW);                                \
    }                                                        \
    return false;                                            \
}

static uint8_t vext_get_emul(DisasContext *s, uint8_t eew)
{
    int8_t emul = eew - s->sew + s->lmul;
    return emul < 0 ? 0 : emul;
}

/*
 *** unit stride load and store
 */
typedef void gen_helper_ldst_us(TCGv_ptr, TCGv_ptr, TCGv,
                                TCGv_env, TCGv_i32);

static bool ldst_us_trans(uint32_t vd, uint32_t rs1, uint32_t data,
                          gen_helper_ldst_us *fn, DisasContext *s,
                          bool is_store)
{
    TCGv_ptr dest, mask;
    TCGv base;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    base = get_gpr(s, rs1, EXT_NONE);

    /*
     * As simd_desc supports at most 2048 bytes, and in this implementation,
     * the max vector group length is 4096 bytes. So split it into two parts.
     *
     * The first part is vlen in bytes (vlenb), encoded in maxsz of simd_desc.
     * The second part is lmul, encoded in data of simd_desc.
     */
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

    fn(dest, mask, base, tcg_env, desc);

    if (!is_store) {
        mark_vs_dirty(s);
    }

    gen_set_label(over);
    return true;
}

static bool ld_us_op(DisasContext *s, arg_r2nfvm *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;
    static gen_helper_ldst_us * const fns[2][4] = {
        /* masked unit stride load */
        { gen_helper_vle8_v_mask, gen_helper_vle16_v_mask,
          gen_helper_vle32_v_mask, gen_helper_vle64_v_mask },
        /* unmasked unit stride load */
        { gen_helper_vle8_v, gen_helper_vle16_v,
          gen_helper_vle32_v, gen_helper_vle64_v }
    };

    fn =  fns[a->vm][eew];
    if (fn == NULL) {
        return false;
    }

    /*
     * Vector load/store instructions have the EEW encoded
     * directly in the instructions. The maximum vector size is
     * calculated with EMUL rather than LMUL.
     */
    uint8_t emul = vext_get_emul(s, eew);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    return ldst_us_trans(a->rd, a->rs1, data, fn, s, false);
}

static bool ld_us_check(DisasContext *s, arg_r2nfvm* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_load(s, a->rd, a->nf, a->vm, eew);
}

GEN_VEXT_TRANS(vle8_v,  MO_8,  r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vle16_v, MO_16, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vle32_v, MO_32, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vle64_v, MO_64, r2nfvm, ld_us_op, ld_us_check)

static bool st_us_op(DisasContext *s, arg_r2nfvm *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;
    static gen_helper_ldst_us * const fns[2][4] = {
        /* masked unit stride store */
        { gen_helper_vse8_v_mask, gen_helper_vse16_v_mask,
          gen_helper_vse32_v_mask, gen_helper_vse64_v_mask },
        /* unmasked unit stride store */
        { gen_helper_vse8_v, gen_helper_vse16_v,
          gen_helper_vse32_v, gen_helper_vse64_v }
    };

    fn =  fns[a->vm][eew];
    if (fn == NULL) {
        return false;
    }

    uint8_t emul = vext_get_emul(s, eew);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldst_us_trans(a->rd, a->rs1, data, fn, s, true);
}

static bool st_us_check(DisasContext *s, arg_r2nfvm* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_store(s, a->rd, a->nf, eew);
}

GEN_VEXT_TRANS(vse8_v,  MO_8,  r2nfvm, st_us_op, st_us_check)
GEN_VEXT_TRANS(vse16_v, MO_16, r2nfvm, st_us_op, st_us_check)
GEN_VEXT_TRANS(vse32_v, MO_32, r2nfvm, st_us_op, st_us_check)
GEN_VEXT_TRANS(vse64_v, MO_64, r2nfvm, st_us_op, st_us_check)

/*
 *** unit stride mask load and store
 */
static bool ld_us_mask_op(DisasContext *s, arg_vlm_v *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn = gen_helper_vlm_v;

    /* EMUL = 1, NFIELDS = 1 */
    data = FIELD_DP32(data, VDATA, LMUL, 0);
    data = FIELD_DP32(data, VDATA, NF, 1);
    /* Mask destination register are always tail-agnostic */
    data = FIELD_DP32(data, VDATA, VTA, s->cfg_vta_all_1s);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    return ldst_us_trans(a->rd, a->rs1, data, fn, s, false);
}

static bool ld_us_mask_check(DisasContext *s, arg_vlm_v *a, uint8_t eew)
{
    /* EMUL = 1, NFIELDS = 1 */
    return require_rvv(s) && vext_check_isa_ill(s);
}

static bool st_us_mask_op(DisasContext *s, arg_vsm_v *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn = gen_helper_vsm_v;

    /* EMUL = 1, NFIELDS = 1 */
    data = FIELD_DP32(data, VDATA, LMUL, 0);
    data = FIELD_DP32(data, VDATA, NF, 1);
    return ldst_us_trans(a->rd, a->rs1, data, fn, s, true);
}

static bool st_us_mask_check(DisasContext *s, arg_vsm_v *a, uint8_t eew)
{
    /* EMUL = 1, NFIELDS = 1 */
    return require_rvv(s) && vext_check_isa_ill(s);
}

GEN_VEXT_TRANS(vlm_v, MO_8, vlm_v, ld_us_mask_op, ld_us_mask_check)
GEN_VEXT_TRANS(vsm_v, MO_8, vsm_v, st_us_mask_op, st_us_mask_check)

/*
 *** stride load and store
 */
typedef void gen_helper_ldst_stride(TCGv_ptr, TCGv_ptr, TCGv,
                                    TCGv, TCGv_env, TCGv_i32);

static bool ldst_stride_trans(uint32_t vd, uint32_t rs1, uint32_t rs2,
                              uint32_t data, gen_helper_ldst_stride *fn,
                              DisasContext *s, bool is_store)
{
    TCGv_ptr dest, mask;
    TCGv base, stride;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    base = get_gpr(s, rs1, EXT_NONE);
    stride = get_gpr(s, rs2, EXT_NONE);
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

    fn(dest, mask, base, stride, tcg_env, desc);

    if (!is_store) {
        mark_vs_dirty(s);
    }

    gen_set_label(over);
    return true;
}

static bool ld_stride_op(DisasContext *s, arg_rnfvm *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_stride *fn;
    static gen_helper_ldst_stride * const fns[4] = {
        gen_helper_vlse8_v, gen_helper_vlse16_v,
        gen_helper_vlse32_v, gen_helper_vlse64_v
    };

    fn = fns[eew];
    if (fn == NULL) {
        return false;
    }

    uint8_t emul = vext_get_emul(s, eew);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    return ldst_stride_trans(a->rd, a->rs1, a->rs2, data, fn, s, false);
}

static bool ld_stride_check(DisasContext *s, arg_rnfvm* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_load(s, a->rd, a->nf, a->vm, eew);
}

GEN_VEXT_TRANS(vlse8_v,  MO_8,  rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlse16_v, MO_16, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlse32_v, MO_32, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlse64_v, MO_64, rnfvm, ld_stride_op, ld_stride_check)

static bool st_stride_op(DisasContext *s, arg_rnfvm *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_stride *fn;
    static gen_helper_ldst_stride * const fns[4] = {
        /* masked stride store */
        gen_helper_vsse8_v,  gen_helper_vsse16_v,
        gen_helper_vsse32_v,  gen_helper_vsse64_v
    };

    uint8_t emul = vext_get_emul(s, eew);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    fn = fns[eew];
    if (fn == NULL) {
        return false;
    }

    return ldst_stride_trans(a->rd, a->rs1, a->rs2, data, fn, s, true);
}

static bool st_stride_check(DisasContext *s, arg_rnfvm* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_store(s, a->rd, a->nf, eew);
}

GEN_VEXT_TRANS(vsse8_v,  MO_8,  rnfvm, st_stride_op, st_stride_check)
GEN_VEXT_TRANS(vsse16_v, MO_16, rnfvm, st_stride_op, st_stride_check)
GEN_VEXT_TRANS(vsse32_v, MO_32, rnfvm, st_stride_op, st_stride_check)
GEN_VEXT_TRANS(vsse64_v, MO_64, rnfvm, st_stride_op, st_stride_check)

/*
 *** index load and store
 */
typedef void gen_helper_ldst_index(TCGv_ptr, TCGv_ptr, TCGv,
                                   TCGv_ptr, TCGv_env, TCGv_i32);

static bool ldst_index_trans(uint32_t vd, uint32_t rs1, uint32_t vs2,
                             uint32_t data, gen_helper_ldst_index *fn,
                             DisasContext *s, bool is_store)
{
    TCGv_ptr dest, mask, index;
    TCGv base;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    index = tcg_temp_new_ptr();
    base = get_gpr(s, rs1, EXT_NONE);
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(index, tcg_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

    fn(dest, mask, base, index, tcg_env, desc);

    if (!is_store) {
        mark_vs_dirty(s);
    }

    gen_set_label(over);
    return true;
}

static bool ld_index_op(DisasContext *s, arg_rnfvm *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_index *fn;
    static gen_helper_ldst_index * const fns[4][4] = {
        /*
         * offset vector register group EEW = 8,
         * data vector register group EEW = SEW
         */
        { gen_helper_vlxei8_8_v,  gen_helper_vlxei8_16_v,
          gen_helper_vlxei8_32_v, gen_helper_vlxei8_64_v },
        /*
         * offset vector register group EEW = 16,
         * data vector register group EEW = SEW
         */
        { gen_helper_vlxei16_8_v, gen_helper_vlxei16_16_v,
          gen_helper_vlxei16_32_v, gen_helper_vlxei16_64_v },
        /*
         * offset vector register group EEW = 32,
         * data vector register group EEW = SEW
         */
        { gen_helper_vlxei32_8_v, gen_helper_vlxei32_16_v,
          gen_helper_vlxei32_32_v, gen_helper_vlxei32_64_v },
        /*
         * offset vector register group EEW = 64,
         * data vector register group EEW = SEW
         */
        { gen_helper_vlxei64_8_v, gen_helper_vlxei64_16_v,
          gen_helper_vlxei64_32_v, gen_helper_vlxei64_64_v }
    };

    fn = fns[eew][s->sew];

    uint8_t emul = vext_get_emul(s, s->sew);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    return ldst_index_trans(a->rd, a->rs1, a->rs2, data, fn, s, false);
}

static bool ld_index_check(DisasContext *s, arg_rnfvm* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ld_index(s, a->rd, a->rs2, a->nf, a->vm, eew);
}

GEN_VEXT_TRANS(vlxei8_v,  MO_8,  rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxei16_v, MO_16, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxei32_v, MO_32, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxei64_v, MO_64, rnfvm, ld_index_op, ld_index_check)

static bool st_index_op(DisasContext *s, arg_rnfvm *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_index *fn;
    static gen_helper_ldst_index * const fns[4][4] = {
        /*
         * offset vector register group EEW = 8,
         * data vector register group EEW = SEW
         */
        { gen_helper_vsxei8_8_v,  gen_helper_vsxei8_16_v,
          gen_helper_vsxei8_32_v, gen_helper_vsxei8_64_v },
        /*
         * offset vector register group EEW = 16,
         * data vector register group EEW = SEW
         */
        { gen_helper_vsxei16_8_v, gen_helper_vsxei16_16_v,
          gen_helper_vsxei16_32_v, gen_helper_vsxei16_64_v },
        /*
         * offset vector register group EEW = 32,
         * data vector register group EEW = SEW
         */
        { gen_helper_vsxei32_8_v, gen_helper_vsxei32_16_v,
          gen_helper_vsxei32_32_v, gen_helper_vsxei32_64_v },
        /*
         * offset vector register group EEW = 64,
         * data vector register group EEW = SEW
         */
        { gen_helper_vsxei64_8_v, gen_helper_vsxei64_16_v,
          gen_helper_vsxei64_32_v, gen_helper_vsxei64_64_v }
    };

    fn = fns[eew][s->sew];

    uint8_t emul = vext_get_emul(s, s->sew);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldst_index_trans(a->rd, a->rs1, a->rs2, data, fn, s, true);
}

static bool st_index_check(DisasContext *s, arg_rnfvm* a, uint8_t eew)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_st_index(s, a->rd, a->rs2, a->nf, eew);
}

GEN_VEXT_TRANS(vsxei8_v,  MO_8,  rnfvm, st_index_op, st_index_check)
GEN_VEXT_TRANS(vsxei16_v, MO_16, rnfvm, st_index_op, st_index_check)
GEN_VEXT_TRANS(vsxei32_v, MO_32, rnfvm, st_index_op, st_index_check)
GEN_VEXT_TRANS(vsxei64_v, MO_64, rnfvm, st_index_op, st_index_check)

/*
 *** unit stride fault-only-first load
 */
static bool ldff_trans(uint32_t vd, uint32_t rs1, uint32_t data,
                       gen_helper_ldst_us *fn, DisasContext *s)
{
    TCGv_ptr dest, mask;
    TCGv base;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    base = get_gpr(s, rs1, EXT_NONE);
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

    fn(dest, mask, base, tcg_env, desc);

    mark_vs_dirty(s);
    gen_set_label(over);
    return true;
}

static bool ldff_op(DisasContext *s, arg_r2nfvm *a, uint8_t eew)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;
    static gen_helper_ldst_us * const fns[4] = {
        gen_helper_vle8ff_v, gen_helper_vle16ff_v,
        gen_helper_vle32ff_v, gen_helper_vle64ff_v
    };

    fn = fns[eew];
    if (fn == NULL) {
        return false;
    }

    uint8_t emul = vext_get_emul(s, eew);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, emul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    return ldff_trans(a->rd, a->rs1, data, fn, s);
}

GEN_VEXT_TRANS(vle8ff_v,  MO_8,  r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vle16ff_v, MO_16, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vle32ff_v, MO_32, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vle64ff_v, MO_64, r2nfvm, ldff_op, ld_us_check)

/*
 * load and store whole register instructions
 */
typedef void gen_helper_ldst_whole(TCGv_ptr, TCGv, TCGv_env, TCGv_i32);

static bool ldst_whole_trans(uint32_t vd, uint32_t rs1, uint32_t nf,
                             uint32_t width, gen_helper_ldst_whole *fn,
                             DisasContext *s, bool is_store)
{
    uint32_t evl = s->cfg_ptr->vlenb * nf / width;
    TCGLabel *over = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_GEU, cpu_vstart, evl, over);

    TCGv_ptr dest;
    TCGv base;
    TCGv_i32 desc;

    uint32_t data = FIELD_DP32(0, VDATA, NF, nf);
    dest = tcg_temp_new_ptr();
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    base = get_gpr(s, rs1, EXT_NONE);
    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));

    fn(dest, base, tcg_env, desc);

    if (!is_store) {
        mark_vs_dirty(s);
    }
    gen_set_label(over);

    return true;
}

/*
 * load and store whole register instructions ignore vtype and vl setting.
 * Thus, we don't need to check vill bit. (Section 7.9)
 */
#define GEN_LDST_WHOLE_TRANS(NAME, ARG_NF, WIDTH, IS_STORE)               \
static bool trans_##NAME(DisasContext *s, arg_##NAME * a)                 \
{                                                                         \
    if (require_rvv(s) &&                                                 \
        QEMU_IS_ALIGNED(a->rd, ARG_NF)) {                                 \
        return ldst_whole_trans(a->rd, a->rs1, ARG_NF, WIDTH,             \
                                gen_helper_##NAME, s, IS_STORE);          \
    }                                                                     \
    return false;                                                         \
}

GEN_LDST_WHOLE_TRANS(vl1re8_v,  1, 1, false)
GEN_LDST_WHOLE_TRANS(vl1re16_v, 1, 2, false)
GEN_LDST_WHOLE_TRANS(vl1re32_v, 1, 4, false)
GEN_LDST_WHOLE_TRANS(vl1re64_v, 1, 8, false)
GEN_LDST_WHOLE_TRANS(vl2re8_v,  2, 1, false)
GEN_LDST_WHOLE_TRANS(vl2re16_v, 2, 2, false)
GEN_LDST_WHOLE_TRANS(vl2re32_v, 2, 4, false)
GEN_LDST_WHOLE_TRANS(vl2re64_v, 2, 8, false)
GEN_LDST_WHOLE_TRANS(vl4re8_v,  4, 1, false)
GEN_LDST_WHOLE_TRANS(vl4re16_v, 4, 2, false)
GEN_LDST_WHOLE_TRANS(vl4re32_v, 4, 4, false)
GEN_LDST_WHOLE_TRANS(vl4re64_v, 4, 8, false)
GEN_LDST_WHOLE_TRANS(vl8re8_v,  8, 1, false)
GEN_LDST_WHOLE_TRANS(vl8re16_v, 8, 2, false)
GEN_LDST_WHOLE_TRANS(vl8re32_v, 8, 4, false)
GEN_LDST_WHOLE_TRANS(vl8re64_v, 8, 8, false)

/*
 * The vector whole register store instructions are encoded similar to
 * unmasked unit-stride store of elements with EEW=8.
 */
GEN_LDST_WHOLE_TRANS(vs1r_v, 1, 1, true)
GEN_LDST_WHOLE_TRANS(vs2r_v, 2, 1, true)
GEN_LDST_WHOLE_TRANS(vs4r_v, 4, 1, true)
GEN_LDST_WHOLE_TRANS(vs8r_v, 8, 1, true)

/*
 *** Vector Integer Arithmetic Instructions
 */

/*
 * MAXSZ returns the maximum vector size can be operated in bytes,
 * which is used in GVEC IR when vl_eq_vlmax flag is set to true
 * to accelerate vector operation.
 */
static inline uint32_t MAXSZ(DisasContext *s)
{
    int max_sz = s->cfg_ptr->vlenb * 8;
    return max_sz >> (3 - s->lmul);
}

static bool opivv_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_sss(s, a->rd, a->rs1, a->rs2, a->vm);
}

typedef void GVecGen3Fn(unsigned, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t);

static inline bool
do_opivv_gvec(DisasContext *s, arg_rmrr *a, GVecGen3Fn *gvec_fn,
              gen_helper_gvec_4_ptr *fn)
{
    TCGLabel *over = gen_new_label();

    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    if (a->vm && s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
        gvec_fn(s->sew, vreg_ofs(s, a->rd),
                vreg_ofs(s, a->rs2), vreg_ofs(s, a->rs1),
                MAXSZ(s), MAXSZ(s));
    } else {
        uint32_t data = 0;

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        data = FIELD_DP32(data, VDATA, VTA, s->vta);
        data = FIELD_DP32(data, VDATA, VMA, s->vma);
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs1), vreg_ofs(s, a->rs2),
                           tcg_env, s->cfg_ptr->vlenb,
                           s->cfg_ptr->vlenb, data, fn);
    }
    mark_vs_dirty(s);
    gen_set_label(over);
    return true;
}

/* OPIVV with GVEC IR */
#define GEN_OPIVV_GVEC_TRANS(NAME, SUF) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    static gen_helper_gvec_4_ptr * const fns[4] = {                \
        gen_helper_##NAME##_b, gen_helper_##NAME##_h,              \
        gen_helper_##NAME##_w, gen_helper_##NAME##_d,              \
    };                                                             \
    if (!opivv_check(s, a)) {                                      \
        return false;                                              \
    }                                                              \
    return do_opivv_gvec(s, a, tcg_gen_gvec_##SUF, fns[s->sew]);   \
}

GEN_OPIVV_GVEC_TRANS(vadd_vv, add)
GEN_OPIVV_GVEC_TRANS(vsub_vv, sub)

typedef void gen_helper_opivx(TCGv_ptr, TCGv_ptr, TCGv, TCGv_ptr,
                              TCGv_env, TCGv_i32);

static bool opivx_trans(uint32_t vd, uint32_t rs1, uint32_t vs2, uint32_t vm,
                        gen_helper_opivx *fn, DisasContext *s)
{
    TCGv_ptr dest, src2, mask;
    TCGv src1;
    TCGv_i32 desc;
    uint32_t data = 0;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();
    src1 = get_gpr(s, rs1, EXT_SIGN);

    data = FIELD_DP32(data, VDATA, VM, vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(src2, tcg_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

    fn(dest, mask, src1, src2, tcg_env, desc);

    mark_vs_dirty(s);
    gen_set_label(over);
    return true;
}

static bool opivx_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ss(s, a->rd, a->rs2, a->vm);
}

typedef void GVecGen2sFn(unsigned, uint32_t, uint32_t, TCGv_i64,
                         uint32_t, uint32_t);

static inline bool
do_opivx_gvec(DisasContext *s, arg_rmrr *a, GVecGen2sFn *gvec_fn,
              gen_helper_opivx *fn)
{
    if (a->vm && s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
        TCGv_i64 src1 = tcg_temp_new_i64();

        tcg_gen_ext_tl_i64(src1, get_gpr(s, a->rs1, EXT_SIGN));
        gvec_fn(s->sew, vreg_ofs(s, a->rd), vreg_ofs(s, a->rs2),
                src1, MAXSZ(s), MAXSZ(s));

        mark_vs_dirty(s);
        return true;
    }
    return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fn, s);
}

/* OPIVX with GVEC IR */
#define GEN_OPIVX_GVEC_TRANS(NAME, SUF) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    static gen_helper_opivx * const fns[4] = {                     \
        gen_helper_##NAME##_b, gen_helper_##NAME##_h,              \
        gen_helper_##NAME##_w, gen_helper_##NAME##_d,              \
    };                                                             \
    if (!opivx_check(s, a)) {                                      \
        return false;                                              \
    }                                                              \
    return do_opivx_gvec(s, a, tcg_gen_gvec_##SUF, fns[s->sew]);   \
}

GEN_OPIVX_GVEC_TRANS(vadd_vx, adds)
GEN_OPIVX_GVEC_TRANS(vsub_vx, subs)

static void gen_vec_rsub8_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_vec_sub8_i64(d, b, a);
}

static void gen_vec_rsub16_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_vec_sub16_i64(d, b, a);
}

static void gen_rsub_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_sub_i32(ret, arg2, arg1);
}

static void gen_rsub_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_sub_i64(ret, arg2, arg1);
}

static void gen_rsub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_sub_vec(vece, r, b, a);
}

static void tcg_gen_gvec_rsubs(unsigned vece, uint32_t dofs, uint32_t aofs,
                               TCGv_i64 c, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_sub_vec, 0 };
    static const GVecGen2s rsub_op[4] = {
        { .fni8 = gen_vec_rsub8_i64,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs8,
          .opt_opc = vecop_list,
          .vece = MO_8 },
        { .fni8 = gen_vec_rsub16_i64,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs16,
          .opt_opc = vecop_list,
          .vece = MO_16 },
        { .fni4 = gen_rsub_i32,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs32,
          .opt_opc = vecop_list,
          .vece = MO_32 },
        { .fni8 = gen_rsub_i64,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs64,
          .opt_opc = vecop_list,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_2s(dofs, aofs, oprsz, maxsz, c, &rsub_op[vece]);
}

GEN_OPIVX_GVEC_TRANS(vrsub_vx, rsubs)

typedef enum {
    IMM_ZX,         /* Zero-extended */
    IMM_SX,         /* Sign-extended */
    IMM_TRUNC_SEW,  /* Truncate to log(SEW) bits */
    IMM_TRUNC_2SEW, /* Truncate to log(2*SEW) bits */
} imm_mode_t;

static int64_t extract_imm(DisasContext *s, uint32_t imm, imm_mode_t imm_mode)
{
    switch (imm_mode) {
    case IMM_ZX:
        return extract64(imm, 0, 5);
    case IMM_SX:
        return sextract64(imm, 0, 5);
    case IMM_TRUNC_SEW:
        return extract64(imm, 0, s->sew + 3);
    case IMM_TRUNC_2SEW:
        return extract64(imm, 0, s->sew + 4);
    default:
        g_assert_not_reached();
    }
}

static bool opivi_trans(uint32_t vd, uint32_t imm, uint32_t vs2, uint32_t vm,
                        gen_helper_opivx *fn, DisasContext *s,
                        imm_mode_t imm_mode)
{
    TCGv_ptr dest, src2, mask;
    TCGv src1;
    TCGv_i32 desc;
    uint32_t data = 0;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();
    src1 = tcg_constant_tl(extract_imm(s, imm, imm_mode));

    data = FIELD_DP32(data, VDATA, VM, vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(src2, tcg_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

    fn(dest, mask, src1, src2, tcg_env, desc);

    mark_vs_dirty(s);
    gen_set_label(over);
    return true;
}

typedef void GVecGen2iFn(unsigned, uint32_t, uint32_t, int64_t,
                         uint32_t, uint32_t);

static inline bool
do_opivi_gvec(DisasContext *s, arg_rmrr *a, GVecGen2iFn *gvec_fn,
              gen_helper_opivx *fn, imm_mode_t imm_mode)
{
    if (a->vm && s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
        gvec_fn(s->sew, vreg_ofs(s, a->rd), vreg_ofs(s, a->rs2),
                extract_imm(s, a->rs1, imm_mode), MAXSZ(s), MAXSZ(s));
        mark_vs_dirty(s);
        return true;
    }
    return opivi_trans(a->rd, a->rs1, a->rs2, a->vm, fn, s, imm_mode);
}

/* OPIVI with GVEC IR */
#define GEN_OPIVI_GVEC_TRANS(NAME, IMM_MODE, OPIVX, SUF) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    static gen_helper_opivx * const fns[4] = {                     \
        gen_helper_##OPIVX##_b, gen_helper_##OPIVX##_h,            \
        gen_helper_##OPIVX##_w, gen_helper_##OPIVX##_d,            \
    };                                                             \
    if (!opivx_check(s, a)) {                                      \
        return false;                                              \
    }                                                              \
    return do_opivi_gvec(s, a, tcg_gen_gvec_##SUF,                 \
                         fns[s->sew], IMM_MODE);                   \
}

GEN_OPIVI_GVEC_TRANS(vadd_vi, IMM_SX, vadd_vx, addi)

static void tcg_gen_gvec_rsubi(unsigned vece, uint32_t dofs, uint32_t aofs,
                               int64_t c, uint32_t oprsz, uint32_t maxsz)
{
    TCGv_i64 tmp = tcg_constant_i64(c);
    tcg_gen_gvec_rsubs(vece, dofs, aofs, tmp, oprsz, maxsz);
}

GEN_OPIVI_GVEC_TRANS(vrsub_vi, IMM_SX, vrsub_vx, rsubi)

/* Vector Widening Integer Add/Subtract */

/* OPIVV with WIDEN */
static bool opivv_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_dss(s, a->rd, a->rs1, a->rs2, a->vm);
}

static bool do_opivv_widen(DisasContext *s, arg_rmrr *a,
                           gen_helper_gvec_4_ptr *fn,
                           bool (*checkfn)(DisasContext *, arg_rmrr *))
{
    if (checkfn(s, a)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        data = FIELD_DP32(data, VDATA, VTA, s->vta);
        data = FIELD_DP32(data, VDATA, VMA, s->vma);
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs1),
                           vreg_ofs(s, a->rs2),
                           tcg_env, s->cfg_ptr->vlenb,
                           s->cfg_ptr->vlenb,
                           data, fn);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

#define GEN_OPIVV_WIDEN_TRANS(NAME, CHECK) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)       \
{                                                            \
    static gen_helper_gvec_4_ptr * const fns[3] = {          \
        gen_helper_##NAME##_b,                               \
        gen_helper_##NAME##_h,                               \
        gen_helper_##NAME##_w                                \
    };                                                       \
    return do_opivv_widen(s, a, fns[s->sew], CHECK);         \
}

GEN_OPIVV_WIDEN_TRANS(vwaddu_vv, opivv_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwadd_vv, opivv_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwsubu_vv, opivv_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwsub_vv, opivv_widen_check)

/* OPIVX with WIDEN */
static bool opivx_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ds(s, a->rd, a->rs2, a->vm);
}

#define GEN_OPIVX_WIDEN_TRANS(NAME, CHECK) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                    \
{                                                                         \
    if (CHECK(s, a)) {                                                    \
        static gen_helper_opivx * const fns[3] = {                        \
            gen_helper_##NAME##_b,                                        \
            gen_helper_##NAME##_h,                                        \
            gen_helper_##NAME##_w                                         \
        };                                                                \
        return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fns[s->sew], s); \
    }                                                                     \
    return false;                                                         \
}

GEN_OPIVX_WIDEN_TRANS(vwaddu_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwadd_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwsubu_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwsub_vx, opivx_widen_check)

/* WIDEN OPIVV with WIDEN */
static bool opiwv_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_dds(s, a->rd, a->rs1, a->rs2, a->vm);
}

static bool do_opiwv_widen(DisasContext *s, arg_rmrr *a,
                           gen_helper_gvec_4_ptr *fn)
{
    if (opiwv_widen_check(s, a)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        data = FIELD_DP32(data, VDATA, VTA, s->vta);
        data = FIELD_DP32(data, VDATA, VMA, s->vma);
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs1),
                           vreg_ofs(s, a->rs2),
                           tcg_env, s->cfg_ptr->vlenb,
                           s->cfg_ptr->vlenb, data, fn);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

#define GEN_OPIWV_WIDEN_TRANS(NAME) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)       \
{                                                            \
    static gen_helper_gvec_4_ptr * const fns[3] = {          \
        gen_helper_##NAME##_b,                               \
        gen_helper_##NAME##_h,                               \
        gen_helper_##NAME##_w                                \
    };                                                       \
    return do_opiwv_widen(s, a, fns[s->sew]);                \
}

GEN_OPIWV_WIDEN_TRANS(vwaddu_wv)
GEN_OPIWV_WIDEN_TRANS(vwadd_wv)
GEN_OPIWV_WIDEN_TRANS(vwsubu_wv)
GEN_OPIWV_WIDEN_TRANS(vwsub_wv)

/* WIDEN OPIVX with WIDEN */
static bool opiwx_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_dd(s, a->rd, a->rs2, a->vm);
}

static bool do_opiwx_widen(DisasContext *s, arg_rmrr *a,
                           gen_helper_opivx *fn)
{
    if (opiwx_widen_check(s, a)) {
        return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fn, s);
    }
    return false;
}

#define GEN_OPIWX_WIDEN_TRANS(NAME) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)       \
{                                                            \
    static gen_helper_opivx * const fns[3] = {               \
        gen_helper_##NAME##_b,                               \
        gen_helper_##NAME##_h,                               \
        gen_helper_##NAME##_w                                \
    };                                                       \
    return do_opiwx_widen(s, a, fns[s->sew]);                \
}

GEN_OPIWX_WIDEN_TRANS(vwaddu_wx)
GEN_OPIWX_WIDEN_TRANS(vwadd_wx)
GEN_OPIWX_WIDEN_TRANS(vwsubu_wx)
GEN_OPIWX_WIDEN_TRANS(vwsub_wx)

static bool opivv_trans(uint32_t vd, uint32_t vs1, uint32_t vs2, uint32_t vm,
                        gen_helper_gvec_4_ptr *fn, DisasContext *s)
{
    uint32_t data = 0;
    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    data = FIELD_DP32(data, VDATA, VM, vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);
    tcg_gen_gvec_4_ptr(vreg_ofs(s, vd), vreg_ofs(s, 0), vreg_ofs(s, vs1),
                       vreg_ofs(s, vs2), tcg_env, s->cfg_ptr->vlenb,
                       s->cfg_ptr->vlenb, data, fn);
    mark_vs_dirty(s);
    gen_set_label(over);
    return true;
}

/* Vector Integer Add-with-Carry / Subtract-with-Borrow Instructions */
/* OPIVV without GVEC IR */
#define GEN_OPIVV_TRANS(NAME, CHECK)                                     \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                   \
{                                                                        \
    if (CHECK(s, a)) {                                                   \
        static gen_helper_gvec_4_ptr * const fns[4] = {                  \
            gen_helper_##NAME##_b, gen_helper_##NAME##_h,                \
            gen_helper_##NAME##_w, gen_helper_##NAME##_d,                \
        };                                                               \
        return opivv_trans(a->rd, a->rs1, a->rs2, a->vm, fns[s->sew], s);\
    }                                                                    \
    return false;                                                        \
}

/*
 * For vadc and vsbc, an illegal instruction exception is raised if the
 * destination vector register is v0 and LMUL > 1. (Section 11.4)
 */
static bool opivv_vadc_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           (a->rd != 0) &&
           vext_check_sss(s, a->rd, a->rs1, a->rs2, a->vm);
}

GEN_OPIVV_TRANS(vadc_vvm, opivv_vadc_check)
GEN_OPIVV_TRANS(vsbc_vvm, opivv_vadc_check)

/*
 * For vmadc and vmsbc, an illegal instruction exception is raised if the
 * destination vector register overlaps a source vector register group.
 */
static bool opivv_vmadc_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_mss(s, a->rd, a->rs1, a->rs2);
}

GEN_OPIVV_TRANS(vmadc_vvm, opivv_vmadc_check)
GEN_OPIVV_TRANS(vmsbc_vvm, opivv_vmadc_check)

static bool opivx_vadc_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           (a->rd != 0) &&
           vext_check_ss(s, a->rd, a->rs2, a->vm);
}

/* OPIVX without GVEC IR */
#define GEN_OPIVX_TRANS(NAME, CHECK)                                     \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                   \
{                                                                        \
    if (CHECK(s, a)) {                                                   \
        static gen_helper_opivx * const fns[4] = {                       \
            gen_helper_##NAME##_b, gen_helper_##NAME##_h,                \
            gen_helper_##NAME##_w, gen_helper_##NAME##_d,                \
        };                                                               \
                                                                         \
        return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fns[s->sew], s);\
    }                                                                    \
    return false;                                                        \
}

GEN_OPIVX_TRANS(vadc_vxm, opivx_vadc_check)
GEN_OPIVX_TRANS(vsbc_vxm, opivx_vadc_check)

static bool opivx_vmadc_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ms(s, a->rd, a->rs2);
}

GEN_OPIVX_TRANS(vmadc_vxm, opivx_vmadc_check)
GEN_OPIVX_TRANS(vmsbc_vxm, opivx_vmadc_check)

/* OPIVI without GVEC IR */
#define GEN_OPIVI_TRANS(NAME, IMM_MODE, OPIVX, CHECK)                    \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                   \
{                                                                        \
    if (CHECK(s, a)) {                                                   \
        static gen_helper_opivx * const fns[4] = {                       \
            gen_helper_##OPIVX##_b, gen_helper_##OPIVX##_h,              \
            gen_helper_##OPIVX##_w, gen_helper_##OPIVX##_d,              \
        };                                                               \
        return opivi_trans(a->rd, a->rs1, a->rs2, a->vm,                 \
                           fns[s->sew], s, IMM_MODE);                    \
    }                                                                    \
    return false;                                                        \
}

GEN_OPIVI_TRANS(vadc_vim, IMM_SX, vadc_vxm, opivx_vadc_check)
GEN_OPIVI_TRANS(vmadc_vim, IMM_SX, vmadc_vxm, opivx_vmadc_check)

/* Vector Bitwise Logical Instructions */
GEN_OPIVV_GVEC_TRANS(vand_vv, and)
GEN_OPIVV_GVEC_TRANS(vor_vv,  or)
GEN_OPIVV_GVEC_TRANS(vxor_vv, xor)
GEN_OPIVX_GVEC_TRANS(vand_vx, ands)
GEN_OPIVX_GVEC_TRANS(vor_vx,  ors)
GEN_OPIVX_GVEC_TRANS(vxor_vx, xors)
GEN_OPIVI_GVEC_TRANS(vand_vi, IMM_SX, vand_vx, andi)
GEN_OPIVI_GVEC_TRANS(vor_vi, IMM_SX, vor_vx,  ori)
GEN_OPIVI_GVEC_TRANS(vxor_vi, IMM_SX, vxor_vx, xori)

/* Vector Single-Width Bit Shift Instructions */
GEN_OPIVV_GVEC_TRANS(vsll_vv,  shlv)
GEN_OPIVV_GVEC_TRANS(vsrl_vv,  shrv)
GEN_OPIVV_GVEC_TRANS(vsra_vv,  sarv)

typedef void GVecGen2sFn32(unsigned, uint32_t, uint32_t, TCGv_i32,
                           uint32_t, uint32_t);

static inline bool
do_opivx_gvec_shift(DisasContext *s, arg_rmrr *a, GVecGen2sFn32 *gvec_fn,
                    gen_helper_opivx *fn)
{
    if (a->vm && s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
        TCGv_i32 src1 = tcg_temp_new_i32();

        tcg_gen_trunc_tl_i32(src1, get_gpr(s, a->rs1, EXT_NONE));
        tcg_gen_extract_i32(src1, src1, 0, s->sew + 3);
        gvec_fn(s->sew, vreg_ofs(s, a->rd), vreg_ofs(s, a->rs2),
                src1, MAXSZ(s), MAXSZ(s));

        mark_vs_dirty(s);
        return true;
    }
    return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fn, s);
}

#define GEN_OPIVX_GVEC_SHIFT_TRANS(NAME, SUF) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                    \
{                                                                         \
    static gen_helper_opivx * const fns[4] = {                            \
        gen_helper_##NAME##_b, gen_helper_##NAME##_h,                     \
        gen_helper_##NAME##_w, gen_helper_##NAME##_d,                     \
    };                                                                    \
    if (!opivx_check(s, a)) {                                             \
        return false;                                                     \
    }                                                                     \
    return do_opivx_gvec_shift(s, a, tcg_gen_gvec_##SUF, fns[s->sew]);    \
}

GEN_OPIVX_GVEC_SHIFT_TRANS(vsll_vx,  shls)
GEN_OPIVX_GVEC_SHIFT_TRANS(vsrl_vx,  shrs)
GEN_OPIVX_GVEC_SHIFT_TRANS(vsra_vx,  sars)

GEN_OPIVI_GVEC_TRANS(vsll_vi, IMM_TRUNC_SEW, vsll_vx, shli)
GEN_OPIVI_GVEC_TRANS(vsrl_vi, IMM_TRUNC_SEW, vsrl_vx, shri)
GEN_OPIVI_GVEC_TRANS(vsra_vi, IMM_TRUNC_SEW, vsra_vx, sari)

/* Vector Narrowing Integer Right Shift Instructions */
static bool opiwv_narrow_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_sds(s, a->rd, a->rs1, a->rs2, a->vm);
}

/* OPIVV with NARROW */
#define GEN_OPIWV_NARROW_TRANS(NAME)                               \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    if (opiwv_narrow_check(s, a)) {                                \
        uint32_t data = 0;                                         \
        static gen_helper_gvec_4_ptr * const fns[3] = {            \
            gen_helper_##NAME##_b,                                 \
            gen_helper_##NAME##_h,                                 \
            gen_helper_##NAME##_w,                                 \
        };                                                         \
        TCGLabel *over = gen_new_label();                          \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);               \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs1),                    \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data,                \
                           fns[s->sew]);                           \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}
GEN_OPIWV_NARROW_TRANS(vnsra_wv)
GEN_OPIWV_NARROW_TRANS(vnsrl_wv)

static bool opiwx_narrow_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_sd(s, a->rd, a->rs2, a->vm);
}

/* OPIVX with NARROW */
#define GEN_OPIWX_NARROW_TRANS(NAME)                                     \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                   \
{                                                                        \
    if (opiwx_narrow_check(s, a)) {                                      \
        static gen_helper_opivx * const fns[3] = {                       \
            gen_helper_##NAME##_b,                                       \
            gen_helper_##NAME##_h,                                       \
            gen_helper_##NAME##_w,                                       \
        };                                                               \
        return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fns[s->sew], s);\
    }                                                                    \
    return false;                                                        \
}

GEN_OPIWX_NARROW_TRANS(vnsra_wx)
GEN_OPIWX_NARROW_TRANS(vnsrl_wx)

/* OPIWI with NARROW */
#define GEN_OPIWI_NARROW_TRANS(NAME, IMM_MODE, OPIVX)                    \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)                   \
{                                                                        \
    if (opiwx_narrow_check(s, a)) {                                      \
        static gen_helper_opivx * const fns[3] = {                       \
            gen_helper_##OPIVX##_b,                                      \
            gen_helper_##OPIVX##_h,                                      \
            gen_helper_##OPIVX##_w,                                      \
        };                                                               \
        return opivi_trans(a->rd, a->rs1, a->rs2, a->vm,                 \
                           fns[s->sew], s, IMM_MODE);                    \
    }                                                                    \
    return false;                                                        \
}

GEN_OPIWI_NARROW_TRANS(vnsra_wi, IMM_ZX, vnsra_wx)
GEN_OPIWI_NARROW_TRANS(vnsrl_wi, IMM_ZX, vnsrl_wx)

/* Vector Integer Comparison Instructions */
/*
 * For all comparison instructions, an illegal instruction exception is raised
 * if the destination vector register overlaps a source vector register group
 * and LMUL > 1.
 */
static bool opivv_cmp_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_mss(s, a->rd, a->rs1, a->rs2);
}

GEN_OPIVV_TRANS(vmseq_vv, opivv_cmp_check)
GEN_OPIVV_TRANS(vmsne_vv, opivv_cmp_check)
GEN_OPIVV_TRANS(vmsltu_vv, opivv_cmp_check)
GEN_OPIVV_TRANS(vmslt_vv, opivv_cmp_check)
GEN_OPIVV_TRANS(vmsleu_vv, opivv_cmp_check)
GEN_OPIVV_TRANS(vmsle_vv, opivv_cmp_check)

static bool opivx_cmp_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ms(s, a->rd, a->rs2);
}

GEN_OPIVX_TRANS(vmseq_vx, opivx_cmp_check)
GEN_OPIVX_TRANS(vmsne_vx, opivx_cmp_check)
GEN_OPIVX_TRANS(vmsltu_vx, opivx_cmp_check)
GEN_OPIVX_TRANS(vmslt_vx, opivx_cmp_check)
GEN_OPIVX_TRANS(vmsleu_vx, opivx_cmp_check)
GEN_OPIVX_TRANS(vmsle_vx, opivx_cmp_check)
GEN_OPIVX_TRANS(vmsgtu_vx, opivx_cmp_check)
GEN_OPIVX_TRANS(vmsgt_vx, opivx_cmp_check)

GEN_OPIVI_TRANS(vmseq_vi, IMM_SX, vmseq_vx, opivx_cmp_check)
GEN_OPIVI_TRANS(vmsne_vi, IMM_SX, vmsne_vx, opivx_cmp_check)
GEN_OPIVI_TRANS(vmsleu_vi, IMM_SX, vmsleu_vx, opivx_cmp_check)
GEN_OPIVI_TRANS(vmsle_vi, IMM_SX, vmsle_vx, opivx_cmp_check)
GEN_OPIVI_TRANS(vmsgtu_vi, IMM_SX, vmsgtu_vx, opivx_cmp_check)
GEN_OPIVI_TRANS(vmsgt_vi, IMM_SX, vmsgt_vx, opivx_cmp_check)

/* Vector Integer Min/Max Instructions */
GEN_OPIVV_GVEC_TRANS(vminu_vv, umin)
GEN_OPIVV_GVEC_TRANS(vmin_vv,  smin)
GEN_OPIVV_GVEC_TRANS(vmaxu_vv, umax)
GEN_OPIVV_GVEC_TRANS(vmax_vv,  smax)
GEN_OPIVX_TRANS(vminu_vx, opivx_check)
GEN_OPIVX_TRANS(vmin_vx,  opivx_check)
GEN_OPIVX_TRANS(vmaxu_vx, opivx_check)
GEN_OPIVX_TRANS(vmax_vx,  opivx_check)

/* Vector Single-Width Integer Multiply Instructions */

static bool vmulh_vv_check(DisasContext *s, arg_rmrr *a)
{
    /*
     * All Zve* extensions support all vector integer instructions,
     * except that the vmulh integer multiply variants
     * that return the high word of the product
     * (vmulh.vv, vmulh.vx, vmulhu.vv, vmulhu.vx, vmulhsu.vv, vmulhsu.vx)
     * are not included for EEW=64 in Zve64*. (Section 18.2)
     */
    return opivv_check(s, a) &&
           (!has_ext(s, RVV) ? s->sew != MO_64 : true);
}

static bool vmulh_vx_check(DisasContext *s, arg_rmrr *a)
{
    /*
     * All Zve* extensions support all vector integer instructions,
     * except that the vmulh integer multiply variants
     * that return the high word of the product
     * (vmulh.vv, vmulh.vx, vmulhu.vv, vmulhu.vx, vmulhsu.vv, vmulhsu.vx)
     * are not included for EEW=64 in Zve64*. (Section 18.2)
     */
    return opivx_check(s, a) &&
           (!has_ext(s, RVV) ? s->sew != MO_64 : true);
}

GEN_OPIVV_GVEC_TRANS(vmul_vv,  mul)
GEN_OPIVV_TRANS(vmulh_vv, vmulh_vv_check)
GEN_OPIVV_TRANS(vmulhu_vv, vmulh_vv_check)
GEN_OPIVV_TRANS(vmulhsu_vv, vmulh_vv_check)
GEN_OPIVX_GVEC_TRANS(vmul_vx,  muls)
GEN_OPIVX_TRANS(vmulh_vx, vmulh_vx_check)
GEN_OPIVX_TRANS(vmulhu_vx, vmulh_vx_check)
GEN_OPIVX_TRANS(vmulhsu_vx, vmulh_vx_check)

/* Vector Integer Divide Instructions */
GEN_OPIVV_TRANS(vdivu_vv, opivv_check)
GEN_OPIVV_TRANS(vdiv_vv, opivv_check)
GEN_OPIVV_TRANS(vremu_vv, opivv_check)
GEN_OPIVV_TRANS(vrem_vv, opivv_check)
GEN_OPIVX_TRANS(vdivu_vx, opivx_check)
GEN_OPIVX_TRANS(vdiv_vx, opivx_check)
GEN_OPIVX_TRANS(vremu_vx, opivx_check)
GEN_OPIVX_TRANS(vrem_vx, opivx_check)

/* Vector Widening Integer Multiply Instructions */
GEN_OPIVV_WIDEN_TRANS(vwmul_vv, opivv_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwmulu_vv, opivv_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwmulsu_vv, opivv_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwmul_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwmulu_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwmulsu_vx, opivx_widen_check)

/* Vector Single-Width Integer Multiply-Add Instructions */
GEN_OPIVV_TRANS(vmacc_vv, opivv_check)
GEN_OPIVV_TRANS(vnmsac_vv, opivv_check)
GEN_OPIVV_TRANS(vmadd_vv, opivv_check)
GEN_OPIVV_TRANS(vnmsub_vv, opivv_check)
GEN_OPIVX_TRANS(vmacc_vx, opivx_check)
GEN_OPIVX_TRANS(vnmsac_vx, opivx_check)
GEN_OPIVX_TRANS(vmadd_vx, opivx_check)
GEN_OPIVX_TRANS(vnmsub_vx, opivx_check)

/* Vector Widening Integer Multiply-Add Instructions */
GEN_OPIVV_WIDEN_TRANS(vwmaccu_vv, opivv_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwmacc_vv, opivv_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwmaccsu_vv, opivv_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwmaccu_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwmacc_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwmaccsu_vx, opivx_widen_check)
GEN_OPIVX_WIDEN_TRANS(vwmaccus_vx, opivx_widen_check)

/* Vector Integer Merge and Move Instructions */
static bool trans_vmv_v_v(DisasContext *s, arg_vmv_v_v *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s) &&
        /* vmv.v.v has rs2 = 0 and vm = 1 */
        vext_check_sss(s, a->rd, a->rs1, 0, 1)) {
        if (s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
            tcg_gen_gvec_mov(s->sew, vreg_ofs(s, a->rd),
                             vreg_ofs(s, a->rs1),
                             MAXSZ(s), MAXSZ(s));
        } else {
            uint32_t data = FIELD_DP32(0, VDATA, LMUL, s->lmul);
            data = FIELD_DP32(data, VDATA, VTA, s->vta);
            static gen_helper_gvec_2_ptr * const fns[4] = {
                gen_helper_vmv_v_v_b, gen_helper_vmv_v_v_h,
                gen_helper_vmv_v_v_w, gen_helper_vmv_v_v_d,
            };
            TCGLabel *over = gen_new_label();
            tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

            tcg_gen_gvec_2_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, a->rs1),
                               tcg_env, s->cfg_ptr->vlenb,
                               s->cfg_ptr->vlenb, data,
                               fns[s->sew]);
            gen_set_label(over);
        }
        mark_vs_dirty(s);
        return true;
    }
    return false;
}

typedef void gen_helper_vmv_vx(TCGv_ptr, TCGv_i64, TCGv_env, TCGv_i32);
static bool trans_vmv_v_x(DisasContext *s, arg_vmv_v_x *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s) &&
        /* vmv.v.x has rs2 = 0 and vm = 1 */
        vext_check_ss(s, a->rd, 0, 1)) {
        TCGv s1;
        TCGLabel *over = gen_new_label();
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        s1 = get_gpr(s, a->rs1, EXT_SIGN);

        if (s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
            if (get_xl(s) == MXL_RV32 && s->sew == MO_64) {
                TCGv_i64 s1_i64 = tcg_temp_new_i64();
                tcg_gen_ext_tl_i64(s1_i64, s1);
                tcg_gen_gvec_dup_i64(s->sew, vreg_ofs(s, a->rd),
                                     MAXSZ(s), MAXSZ(s), s1_i64);
            } else {
                tcg_gen_gvec_dup_tl(s->sew, vreg_ofs(s, a->rd),
                                    MAXSZ(s), MAXSZ(s), s1);
            }
        } else {
            TCGv_i32 desc;
            TCGv_i64 s1_i64 = tcg_temp_new_i64();
            TCGv_ptr dest = tcg_temp_new_ptr();
            uint32_t data = FIELD_DP32(0, VDATA, LMUL, s->lmul);
            data = FIELD_DP32(data, VDATA, VTA, s->vta);
            static gen_helper_vmv_vx * const fns[4] = {
                gen_helper_vmv_v_x_b, gen_helper_vmv_v_x_h,
                gen_helper_vmv_v_x_w, gen_helper_vmv_v_x_d,
            };

            tcg_gen_ext_tl_i64(s1_i64, s1);
            desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                              s->cfg_ptr->vlenb, data));
            tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, a->rd));
            fns[s->sew](dest, s1_i64, tcg_env, desc);
        }

        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

static bool trans_vmv_v_i(DisasContext *s, arg_vmv_v_i *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s) &&
        /* vmv.v.i has rs2 = 0 and vm = 1 */
        vext_check_ss(s, a->rd, 0, 1)) {
        int64_t simm = sextract64(a->rs1, 0, 5);
        if (s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
            tcg_gen_gvec_dup_imm(s->sew, vreg_ofs(s, a->rd),
                                 MAXSZ(s), MAXSZ(s), simm);
            mark_vs_dirty(s);
        } else {
            TCGv_i32 desc;
            TCGv_i64 s1;
            TCGv_ptr dest;
            uint32_t data = FIELD_DP32(0, VDATA, LMUL, s->lmul);
            data = FIELD_DP32(data, VDATA, VTA, s->vta);
            static gen_helper_vmv_vx * const fns[4] = {
                gen_helper_vmv_v_x_b, gen_helper_vmv_v_x_h,
                gen_helper_vmv_v_x_w, gen_helper_vmv_v_x_d,
            };
            TCGLabel *over = gen_new_label();
            tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

            s1 = tcg_constant_i64(simm);
            dest = tcg_temp_new_ptr();
            desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                              s->cfg_ptr->vlenb, data));
            tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, a->rd));
            fns[s->sew](dest, s1, tcg_env, desc);

            mark_vs_dirty(s);
            gen_set_label(over);
        }
        return true;
    }
    return false;
}

GEN_OPIVV_TRANS(vmerge_vvm, opivv_vadc_check)
GEN_OPIVX_TRANS(vmerge_vxm, opivx_vadc_check)
GEN_OPIVI_TRANS(vmerge_vim, IMM_SX, vmerge_vxm, opivx_vadc_check)

/*
 *** Vector Fixed-Point Arithmetic Instructions
 */

/* Vector Single-Width Saturating Add and Subtract */
GEN_OPIVV_TRANS(vsaddu_vv, opivv_check)
GEN_OPIVV_TRANS(vsadd_vv,  opivv_check)
GEN_OPIVV_TRANS(vssubu_vv, opivv_check)
GEN_OPIVV_TRANS(vssub_vv,  opivv_check)
GEN_OPIVX_TRANS(vsaddu_vx,  opivx_check)
GEN_OPIVX_TRANS(vsadd_vx,  opivx_check)
GEN_OPIVX_TRANS(vssubu_vx,  opivx_check)
GEN_OPIVX_TRANS(vssub_vx,  opivx_check)
GEN_OPIVI_TRANS(vsaddu_vi, IMM_SX, vsaddu_vx, opivx_check)
GEN_OPIVI_TRANS(vsadd_vi, IMM_SX, vsadd_vx, opivx_check)

/* Vector Single-Width Averaging Add and Subtract */
GEN_OPIVV_TRANS(vaadd_vv, opivv_check)
GEN_OPIVV_TRANS(vaaddu_vv, opivv_check)
GEN_OPIVV_TRANS(vasub_vv, opivv_check)
GEN_OPIVV_TRANS(vasubu_vv, opivv_check)
GEN_OPIVX_TRANS(vaadd_vx,  opivx_check)
GEN_OPIVX_TRANS(vaaddu_vx,  opivx_check)
GEN_OPIVX_TRANS(vasub_vx,  opivx_check)
GEN_OPIVX_TRANS(vasubu_vx,  opivx_check)

/* Vector Single-Width Fractional Multiply with Rounding and Saturation */

static bool vsmul_vv_check(DisasContext *s, arg_rmrr *a)
{
    /*
     * All Zve* extensions support all vector fixed-point arithmetic
     * instructions, except that vsmul.vv and vsmul.vx are not supported
     * for EEW=64 in Zve64*. (Section 18.2)
     */
    return opivv_check(s, a) &&
           (!has_ext(s, RVV) ? s->sew != MO_64 : true);
}

static bool vsmul_vx_check(DisasContext *s, arg_rmrr *a)
{
    /*
     * All Zve* extensions support all vector fixed-point arithmetic
     * instructions, except that vsmul.vv and vsmul.vx are not supported
     * for EEW=64 in Zve64*. (Section 18.2)
     */
    return opivx_check(s, a) &&
           (!has_ext(s, RVV) ? s->sew != MO_64 : true);
}

GEN_OPIVV_TRANS(vsmul_vv, vsmul_vv_check)
GEN_OPIVX_TRANS(vsmul_vx,  vsmul_vx_check)

/* Vector Single-Width Scaling Shift Instructions */
GEN_OPIVV_TRANS(vssrl_vv, opivv_check)
GEN_OPIVV_TRANS(vssra_vv, opivv_check)
GEN_OPIVX_TRANS(vssrl_vx,  opivx_check)
GEN_OPIVX_TRANS(vssra_vx,  opivx_check)
GEN_OPIVI_TRANS(vssrl_vi, IMM_TRUNC_SEW, vssrl_vx, opivx_check)
GEN_OPIVI_TRANS(vssra_vi, IMM_TRUNC_SEW, vssra_vx, opivx_check)

/* Vector Narrowing Fixed-Point Clip Instructions */
GEN_OPIWV_NARROW_TRANS(vnclipu_wv)
GEN_OPIWV_NARROW_TRANS(vnclip_wv)
GEN_OPIWX_NARROW_TRANS(vnclipu_wx)
GEN_OPIWX_NARROW_TRANS(vnclip_wx)
GEN_OPIWI_NARROW_TRANS(vnclipu_wi, IMM_ZX, vnclipu_wx)
GEN_OPIWI_NARROW_TRANS(vnclip_wi, IMM_ZX, vnclip_wx)

/*
 *** Vector Float Point Arithmetic Instructions
 */

/*
 * As RVF-only cpus always have values NaN-boxed to 64-bits,
 * RVF and RVD can be treated equally.
 * We don't have to deal with the cases of: SEW > FLEN.
 *
 * If SEW < FLEN, check whether input fp register is a valid
 * NaN-boxed value, in which case the least-significant SEW bits
 * of the f register are used, else the canonical NaN value is used.
 */
static void do_nanbox(DisasContext *s, TCGv_i64 out, TCGv_i64 in)
{
    switch (s->sew) {
    case 1:
        gen_check_nanbox_h(out, in);
        break;
    case 2:
        gen_check_nanbox_s(out, in);
        break;
    case 3:
        tcg_gen_mov_i64(out, in);
        break;
    default:
        g_assert_not_reached();
    }
}

/* Vector Single-Width Floating-Point Add/Subtract Instructions */

/*
 * If the current SEW does not correspond to a supported IEEE floating-point
 * type, an illegal instruction exception is raised.
 */
static bool opfvv_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_rvf(s) &&
           vext_check_isa_ill(s) &&
           vext_check_sss(s, a->rd, a->rs1, a->rs2, a->vm);
}

/* OPFVV without GVEC IR */
#define GEN_OPFVV_TRANS(NAME, CHECK)                               \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    if (CHECK(s, a)) {                                             \
        uint32_t data = 0;                                         \
        static gen_helper_gvec_4_ptr * const fns[3] = {            \
            gen_helper_##NAME##_h,                                 \
            gen_helper_##NAME##_w,                                 \
            gen_helper_##NAME##_d,                                 \
        };                                                         \
        TCGLabel *over = gen_new_label();                          \
        gen_set_rm(s, RISCV_FRM_DYN);                              \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);               \
        data =                                                     \
            FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);\
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs1),                    \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data,                \
                           fns[s->sew - 1]);                       \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}
GEN_OPFVV_TRANS(vfadd_vv, opfvv_check)
GEN_OPFVV_TRANS(vfsub_vv, opfvv_check)

typedef void gen_helper_opfvf(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv_ptr,
                              TCGv_env, TCGv_i32);

static bool opfvf_trans(uint32_t vd, uint32_t rs1, uint32_t vs2,
                        uint32_t data, gen_helper_opfvf *fn, DisasContext *s)
{
    TCGv_ptr dest, src2, mask;
    TCGv_i32 desc;
    TCGv_i64 t1;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();
    desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                      s->cfg_ptr->vlenb, data));

    tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(src2, tcg_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

    /* NaN-box f[rs1] */
    t1 = tcg_temp_new_i64();
    do_nanbox(s, t1, cpu_fpr[rs1]);

    fn(dest, mask, t1, src2, tcg_env, desc);

    mark_vs_dirty(s);
    gen_set_label(over);
    return true;
}

/*
 * If the current SEW does not correspond to a supported IEEE floating-point
 * type, an illegal instruction exception is raised
 */
static bool opfvf_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_rvf(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ss(s, a->rd, a->rs2, a->vm);
}

/* OPFVF without GVEC IR */
#define GEN_OPFVF_TRANS(NAME, CHECK)                              \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)            \
{                                                                 \
    if (CHECK(s, a)) {                                            \
        uint32_t data = 0;                                        \
        static gen_helper_opfvf *const fns[3] = {                 \
            gen_helper_##NAME##_h,                                \
            gen_helper_##NAME##_w,                                \
            gen_helper_##NAME##_d,                                \
        };                                                        \
        gen_set_rm(s, RISCV_FRM_DYN);                             \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);            \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);              \
        data = FIELD_DP32(data, VDATA, VTA_ALL_1S,                \
                          s->cfg_vta_all_1s);                     \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);              \
        return opfvf_trans(a->rd, a->rs1, a->rs2, data,           \
                           fns[s->sew - 1], s);                   \
    }                                                             \
    return false;                                                 \
}

GEN_OPFVF_TRANS(vfadd_vf,  opfvf_check)
GEN_OPFVF_TRANS(vfsub_vf,  opfvf_check)
GEN_OPFVF_TRANS(vfrsub_vf,  opfvf_check)

/* Vector Widening Floating-Point Add/Subtract Instructions */
static bool opfvv_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_scale_rvf(s) &&
           (s->sew != MO_8) &&
           vext_check_isa_ill(s) &&
           vext_check_dss(s, a->rd, a->rs1, a->rs2, a->vm);
}

/* OPFVV with WIDEN */
#define GEN_OPFVV_WIDEN_TRANS(NAME, CHECK)                       \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)           \
{                                                                \
    if (CHECK(s, a)) {                                           \
        uint32_t data = 0;                                       \
        static gen_helper_gvec_4_ptr * const fns[2] = {          \
            gen_helper_##NAME##_h, gen_helper_##NAME##_w,        \
        };                                                       \
        TCGLabel *over = gen_new_label();                        \
        gen_set_rm(s, RISCV_FRM_DYN);                            \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);\
                                                                 \
        data = FIELD_DP32(data, VDATA, VM, a->vm);               \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);           \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);             \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);             \
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),   \
                           vreg_ofs(s, a->rs1),                  \
                           vreg_ofs(s, a->rs2), tcg_env,         \
                           s->cfg_ptr->vlenb,                    \
                           s->cfg_ptr->vlenb, data,              \
                           fns[s->sew - 1]);                     \
        mark_vs_dirty(s);                                        \
        gen_set_label(over);                                     \
        return true;                                             \
    }                                                            \
    return false;                                                \
}

GEN_OPFVV_WIDEN_TRANS(vfwadd_vv, opfvv_widen_check)
GEN_OPFVV_WIDEN_TRANS(vfwsub_vv, opfvv_widen_check)

static bool opfvf_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_scale_rvf(s) &&
           (s->sew != MO_8) &&
           vext_check_isa_ill(s) &&
           vext_check_ds(s, a->rd, a->rs2, a->vm);
}

/* OPFVF with WIDEN */
#define GEN_OPFVF_WIDEN_TRANS(NAME)                              \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)           \
{                                                                \
    if (opfvf_widen_check(s, a)) {                               \
        uint32_t data = 0;                                       \
        static gen_helper_opfvf *const fns[2] = {                \
            gen_helper_##NAME##_h, gen_helper_##NAME##_w,        \
        };                                                       \
        gen_set_rm(s, RISCV_FRM_DYN);                            \
        data = FIELD_DP32(data, VDATA, VM, a->vm);               \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);           \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);             \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);             \
        return opfvf_trans(a->rd, a->rs1, a->rs2, data,          \
                           fns[s->sew - 1], s);                  \
    }                                                            \
    return false;                                                \
}

GEN_OPFVF_WIDEN_TRANS(vfwadd_vf)
GEN_OPFVF_WIDEN_TRANS(vfwsub_vf)

static bool opfwv_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_scale_rvf(s) &&
           (s->sew != MO_8) &&
           vext_check_isa_ill(s) &&
           vext_check_dds(s, a->rd, a->rs1, a->rs2, a->vm);
}

/* WIDEN OPFVV with WIDEN */
#define GEN_OPFWV_WIDEN_TRANS(NAME)                                \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    if (opfwv_widen_check(s, a)) {                                 \
        uint32_t data = 0;                                         \
        static gen_helper_gvec_4_ptr * const fns[2] = {            \
            gen_helper_##NAME##_h, gen_helper_##NAME##_w,          \
        };                                                         \
        TCGLabel *over = gen_new_label();                          \
        gen_set_rm(s, RISCV_FRM_DYN);                              \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);               \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs1),                    \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data,                \
                           fns[s->sew - 1]);                       \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_OPFWV_WIDEN_TRANS(vfwadd_wv)
GEN_OPFWV_WIDEN_TRANS(vfwsub_wv)

static bool opfwf_widen_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_scale_rvf(s) &&
           (s->sew != MO_8) &&
           vext_check_isa_ill(s) &&
           vext_check_dd(s, a->rd, a->rs2, a->vm);
}

/* WIDEN OPFVF with WIDEN */
#define GEN_OPFWF_WIDEN_TRANS(NAME)                              \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)           \
{                                                                \
    if (opfwf_widen_check(s, a)) {                               \
        uint32_t data = 0;                                       \
        static gen_helper_opfvf *const fns[2] = {                \
            gen_helper_##NAME##_h, gen_helper_##NAME##_w,        \
        };                                                       \
        gen_set_rm(s, RISCV_FRM_DYN);                            \
        data = FIELD_DP32(data, VDATA, VM, a->vm);               \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);           \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);             \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);             \
        return opfvf_trans(a->rd, a->rs1, a->rs2, data,          \
                           fns[s->sew - 1], s);                  \
    }                                                            \
    return false;                                                \
}

GEN_OPFWF_WIDEN_TRANS(vfwadd_wf)
GEN_OPFWF_WIDEN_TRANS(vfwsub_wf)

/* Vector Single-Width Floating-Point Multiply/Divide Instructions */
GEN_OPFVV_TRANS(vfmul_vv, opfvv_check)
GEN_OPFVV_TRANS(vfdiv_vv, opfvv_check)
GEN_OPFVF_TRANS(vfmul_vf,  opfvf_check)
GEN_OPFVF_TRANS(vfdiv_vf,  opfvf_check)
GEN_OPFVF_TRANS(vfrdiv_vf,  opfvf_check)

/* Vector Widening Floating-Point Multiply */
GEN_OPFVV_WIDEN_TRANS(vfwmul_vv, opfvv_widen_check)
GEN_OPFVF_WIDEN_TRANS(vfwmul_vf)

/* Vector Single-Width Floating-Point Fused Multiply-Add Instructions */
GEN_OPFVV_TRANS(vfmacc_vv, opfvv_check)
GEN_OPFVV_TRANS(vfnmacc_vv, opfvv_check)
GEN_OPFVV_TRANS(vfmsac_vv, opfvv_check)
GEN_OPFVV_TRANS(vfnmsac_vv, opfvv_check)
GEN_OPFVV_TRANS(vfmadd_vv, opfvv_check)
GEN_OPFVV_TRANS(vfnmadd_vv, opfvv_check)
GEN_OPFVV_TRANS(vfmsub_vv, opfvv_check)
GEN_OPFVV_TRANS(vfnmsub_vv, opfvv_check)
GEN_OPFVF_TRANS(vfmacc_vf, opfvf_check)
GEN_OPFVF_TRANS(vfnmacc_vf, opfvf_check)
GEN_OPFVF_TRANS(vfmsac_vf, opfvf_check)
GEN_OPFVF_TRANS(vfnmsac_vf, opfvf_check)
GEN_OPFVF_TRANS(vfmadd_vf, opfvf_check)
GEN_OPFVF_TRANS(vfnmadd_vf, opfvf_check)
GEN_OPFVF_TRANS(vfmsub_vf, opfvf_check)
GEN_OPFVF_TRANS(vfnmsub_vf, opfvf_check)

/* Vector Widening Floating-Point Fused Multiply-Add Instructions */
GEN_OPFVV_WIDEN_TRANS(vfwmacc_vv, opfvv_widen_check)
GEN_OPFVV_WIDEN_TRANS(vfwnmacc_vv, opfvv_widen_check)
GEN_OPFVV_WIDEN_TRANS(vfwmsac_vv, opfvv_widen_check)
GEN_OPFVV_WIDEN_TRANS(vfwnmsac_vv, opfvv_widen_check)
GEN_OPFVF_WIDEN_TRANS(vfwmacc_vf)
GEN_OPFVF_WIDEN_TRANS(vfwnmacc_vf)
GEN_OPFVF_WIDEN_TRANS(vfwmsac_vf)
GEN_OPFVF_WIDEN_TRANS(vfwnmsac_vf)

/* Vector Floating-Point Square-Root Instruction */

/*
 * If the current SEW does not correspond to a supported IEEE floating-point
 * type, an illegal instruction exception is raised
 */
static bool opfv_check(DisasContext *s, arg_rmr *a)
{
    return require_rvv(s) &&
           require_rvf(s) &&
           vext_check_isa_ill(s) &&
           /* OPFV instructions ignore vs1 check */
           vext_check_ss(s, a->rd, a->rs2, a->vm);
}

static bool do_opfv(DisasContext *s, arg_rmr *a,
                    gen_helper_gvec_3_ptr *fn,
                    bool (*checkfn)(DisasContext *, arg_rmr *),
                    int rm)
{
    if (checkfn(s, a)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();
        gen_set_rm_chkfrm(s, rm);
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        data = FIELD_DP32(data, VDATA, VTA, s->vta);
        data = FIELD_DP32(data, VDATA, VMA, s->vma);
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs2), tcg_env,
                           s->cfg_ptr->vlenb,
                           s->cfg_ptr->vlenb, data, fn);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

#define GEN_OPFV_TRANS(NAME, CHECK, FRM)               \
static bool trans_##NAME(DisasContext *s, arg_rmr *a)  \
{                                                      \
    static gen_helper_gvec_3_ptr * const fns[3] = {    \
        gen_helper_##NAME##_h,                         \
        gen_helper_##NAME##_w,                         \
        gen_helper_##NAME##_d                          \
    };                                                 \
    return do_opfv(s, a, fns[s->sew - 1], CHECK, FRM); \
}

GEN_OPFV_TRANS(vfsqrt_v, opfv_check, RISCV_FRM_DYN)
GEN_OPFV_TRANS(vfrsqrt7_v, opfv_check, RISCV_FRM_DYN)
GEN_OPFV_TRANS(vfrec7_v, opfv_check, RISCV_FRM_DYN)

/* Vector Floating-Point MIN/MAX Instructions */
GEN_OPFVV_TRANS(vfmin_vv, opfvv_check)
GEN_OPFVV_TRANS(vfmax_vv, opfvv_check)
GEN_OPFVF_TRANS(vfmin_vf, opfvf_check)
GEN_OPFVF_TRANS(vfmax_vf, opfvf_check)

/* Vector Floating-Point Sign-Injection Instructions */
GEN_OPFVV_TRANS(vfsgnj_vv, opfvv_check)
GEN_OPFVV_TRANS(vfsgnjn_vv, opfvv_check)
GEN_OPFVV_TRANS(vfsgnjx_vv, opfvv_check)
GEN_OPFVF_TRANS(vfsgnj_vf, opfvf_check)
GEN_OPFVF_TRANS(vfsgnjn_vf, opfvf_check)
GEN_OPFVF_TRANS(vfsgnjx_vf, opfvf_check)

/* Vector Floating-Point Compare Instructions */
static bool opfvv_cmp_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_rvf(s) &&
           vext_check_isa_ill(s) &&
           vext_check_mss(s, a->rd, a->rs1, a->rs2);
}

GEN_OPFVV_TRANS(vmfeq_vv, opfvv_cmp_check)
GEN_OPFVV_TRANS(vmfne_vv, opfvv_cmp_check)
GEN_OPFVV_TRANS(vmflt_vv, opfvv_cmp_check)
GEN_OPFVV_TRANS(vmfle_vv, opfvv_cmp_check)

static bool opfvf_cmp_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           require_rvf(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ms(s, a->rd, a->rs2);
}

GEN_OPFVF_TRANS(vmfeq_vf, opfvf_cmp_check)
GEN_OPFVF_TRANS(vmfne_vf, opfvf_cmp_check)
GEN_OPFVF_TRANS(vmflt_vf, opfvf_cmp_check)
GEN_OPFVF_TRANS(vmfle_vf, opfvf_cmp_check)
GEN_OPFVF_TRANS(vmfgt_vf, opfvf_cmp_check)
GEN_OPFVF_TRANS(vmfge_vf, opfvf_cmp_check)

/* Vector Floating-Point Classify Instruction */
GEN_OPFV_TRANS(vfclass_v, opfv_check, RISCV_FRM_DYN)

/* Vector Floating-Point Merge Instruction */
GEN_OPFVF_TRANS(vfmerge_vfm,  opfvf_check)

static bool trans_vfmv_v_f(DisasContext *s, arg_vfmv_v_f *a)
{
    if (require_rvv(s) &&
        require_rvf(s) &&
        vext_check_isa_ill(s) &&
        require_align(a->rd, s->lmul)) {
        gen_set_rm(s, RISCV_FRM_DYN);

        TCGv_i64 t1;

        if (s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
            t1 = tcg_temp_new_i64();
            /* NaN-box f[rs1] */
            do_nanbox(s, t1, cpu_fpr[a->rs1]);

            tcg_gen_gvec_dup_i64(s->sew, vreg_ofs(s, a->rd),
                                 MAXSZ(s), MAXSZ(s), t1);
            mark_vs_dirty(s);
        } else {
            TCGv_ptr dest;
            TCGv_i32 desc;
            uint32_t data = FIELD_DP32(0, VDATA, LMUL, s->lmul);
            data = FIELD_DP32(data, VDATA, VTA, s->vta);
            data = FIELD_DP32(data, VDATA, VMA, s->vma);
            static gen_helper_vmv_vx * const fns[3] = {
                gen_helper_vmv_v_x_h,
                gen_helper_vmv_v_x_w,
                gen_helper_vmv_v_x_d,
            };
            TCGLabel *over = gen_new_label();
            tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

            t1 = tcg_temp_new_i64();
            /* NaN-box f[rs1] */
            do_nanbox(s, t1, cpu_fpr[a->rs1]);

            dest = tcg_temp_new_ptr();
            desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                              s->cfg_ptr->vlenb, data));
            tcg_gen_addi_ptr(dest, tcg_env, vreg_ofs(s, a->rd));

            fns[s->sew - 1](dest, t1, tcg_env, desc);

            mark_vs_dirty(s);
            gen_set_label(over);
        }
        return true;
    }
    return false;
}

/* Single-Width Floating-Point/Integer Type-Convert Instructions */
#define GEN_OPFV_CVT_TRANS(NAME, HELPER, FRM)               \
static bool trans_##NAME(DisasContext *s, arg_rmr *a)       \
{                                                           \
    static gen_helper_gvec_3_ptr * const fns[3] = {         \
        gen_helper_##HELPER##_h,                            \
        gen_helper_##HELPER##_w,                            \
        gen_helper_##HELPER##_d                             \
    };                                                      \
    return do_opfv(s, a, fns[s->sew - 1], opfv_check, FRM); \
}

GEN_OPFV_CVT_TRANS(vfcvt_xu_f_v, vfcvt_xu_f_v, RISCV_FRM_DYN)
GEN_OPFV_CVT_TRANS(vfcvt_x_f_v, vfcvt_x_f_v, RISCV_FRM_DYN)
GEN_OPFV_CVT_TRANS(vfcvt_f_xu_v, vfcvt_f_xu_v, RISCV_FRM_DYN)
GEN_OPFV_CVT_TRANS(vfcvt_f_x_v, vfcvt_f_x_v, RISCV_FRM_DYN)
/* Reuse the helper functions from vfcvt.xu.f.v and vfcvt.x.f.v */
GEN_OPFV_CVT_TRANS(vfcvt_rtz_xu_f_v, vfcvt_xu_f_v, RISCV_FRM_RTZ)
GEN_OPFV_CVT_TRANS(vfcvt_rtz_x_f_v, vfcvt_x_f_v, RISCV_FRM_RTZ)

/* Widening Floating-Point/Integer Type-Convert Instructions */

/*
 * If the current SEW does not correspond to a supported IEEE floating-point
 * type, an illegal instruction exception is raised
 */
static bool opfv_widen_check(DisasContext *s, arg_rmr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_ds(s, a->rd, a->rs2, a->vm);
}

static bool opxfv_widen_check(DisasContext *s, arg_rmr *a)
{
    return opfv_widen_check(s, a) &&
           require_rvf(s);
}

static bool opffv_widen_check(DisasContext *s, arg_rmr *a)
{
    return opfv_widen_check(s, a) &&
           require_scale_rvfmin(s) &&
           (s->sew != MO_8);
}

#define GEN_OPFV_WIDEN_TRANS(NAME, CHECK, HELPER, FRM)             \
static bool trans_##NAME(DisasContext *s, arg_rmr *a)              \
{                                                                  \
    if (CHECK(s, a)) {                                             \
        uint32_t data = 0;                                         \
        static gen_helper_gvec_3_ptr * const fns[2] = {            \
            gen_helper_##HELPER##_h,                               \
            gen_helper_##HELPER##_w,                               \
        };                                                         \
        TCGLabel *over = gen_new_label();                          \
        gen_set_rm_chkfrm(s, FRM);                                 \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);               \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data,                \
                           fns[s->sew - 1]);                       \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_OPFV_WIDEN_TRANS(vfwcvt_xu_f_v, opxfv_widen_check, vfwcvt_xu_f_v,
                     RISCV_FRM_DYN)
GEN_OPFV_WIDEN_TRANS(vfwcvt_x_f_v, opxfv_widen_check, vfwcvt_x_f_v,
                     RISCV_FRM_DYN)
GEN_OPFV_WIDEN_TRANS(vfwcvt_f_f_v, opffv_widen_check, vfwcvt_f_f_v,
                     RISCV_FRM_DYN)
/* Reuse the helper functions from vfwcvt.xu.f.v and vfwcvt.x.f.v */
GEN_OPFV_WIDEN_TRANS(vfwcvt_rtz_xu_f_v, opxfv_widen_check, vfwcvt_xu_f_v,
                     RISCV_FRM_RTZ)
GEN_OPFV_WIDEN_TRANS(vfwcvt_rtz_x_f_v, opxfv_widen_check, vfwcvt_x_f_v,
                     RISCV_FRM_RTZ)

static bool opfxv_widen_check(DisasContext *s, arg_rmr *a)
{
    return require_rvv(s) &&
           require_scale_rvf(s) &&
           vext_check_isa_ill(s) &&
           /* OPFV widening instructions ignore vs1 check */
           vext_check_ds(s, a->rd, a->rs2, a->vm);
}

#define GEN_OPFXV_WIDEN_TRANS(NAME)                                \
static bool trans_##NAME(DisasContext *s, arg_rmr *a)              \
{                                                                  \
    if (opfxv_widen_check(s, a)) {                                 \
        uint32_t data = 0;                                         \
        static gen_helper_gvec_3_ptr * const fns[3] = {            \
            gen_helper_##NAME##_b,                                 \
            gen_helper_##NAME##_h,                                 \
            gen_helper_##NAME##_w,                                 \
        };                                                         \
        TCGLabel *over = gen_new_label();                          \
        gen_set_rm(s, RISCV_FRM_DYN);                              \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);               \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data,                \
                           fns[s->sew]);                           \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_OPFXV_WIDEN_TRANS(vfwcvt_f_xu_v)
GEN_OPFXV_WIDEN_TRANS(vfwcvt_f_x_v)

/* Narrowing Floating-Point/Integer Type-Convert Instructions */

/*
 * If the current SEW does not correspond to a supported IEEE floating-point
 * type, an illegal instruction exception is raised
 */
static bool opfv_narrow_check(DisasContext *s, arg_rmr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           /* OPFV narrowing instructions ignore vs1 check */
           vext_check_sd(s, a->rd, a->rs2, a->vm);
}

static bool opfxv_narrow_check(DisasContext *s, arg_rmr *a)
{
    return opfv_narrow_check(s, a) &&
           require_rvf(s) &&
           (s->sew != MO_64);
}

static bool opffv_narrow_check(DisasContext *s, arg_rmr *a)
{
    return opfv_narrow_check(s, a) &&
           require_scale_rvfmin(s) &&
           (s->sew != MO_8);
}

static bool opffv_rod_narrow_check(DisasContext *s, arg_rmr *a)
{
    return opfv_narrow_check(s, a) &&
           require_scale_rvf(s) &&
           (s->sew != MO_8);
}

#define GEN_OPFV_NARROW_TRANS(NAME, CHECK, HELPER, FRM)            \
static bool trans_##NAME(DisasContext *s, arg_rmr *a)              \
{                                                                  \
    if (CHECK(s, a)) {                                             \
        uint32_t data = 0;                                         \
        static gen_helper_gvec_3_ptr * const fns[2] = {            \
            gen_helper_##HELPER##_h,                               \
            gen_helper_##HELPER##_w,                               \
        };                                                         \
        TCGLabel *over = gen_new_label();                          \
        gen_set_rm_chkfrm(s, FRM);                                 \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);               \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data,                \
                           fns[s->sew - 1]);                       \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_OPFV_NARROW_TRANS(vfncvt_f_xu_w, opfxv_narrow_check, vfncvt_f_xu_w,
                      RISCV_FRM_DYN)
GEN_OPFV_NARROW_TRANS(vfncvt_f_x_w, opfxv_narrow_check, vfncvt_f_x_w,
                      RISCV_FRM_DYN)
GEN_OPFV_NARROW_TRANS(vfncvt_f_f_w, opffv_narrow_check, vfncvt_f_f_w,
                      RISCV_FRM_DYN)
/* Reuse the helper function from vfncvt.f.f.w */
GEN_OPFV_NARROW_TRANS(vfncvt_rod_f_f_w, opffv_rod_narrow_check, vfncvt_f_f_w,
                      RISCV_FRM_ROD)

static bool opxfv_narrow_check(DisasContext *s, arg_rmr *a)
{
    return require_rvv(s) &&
           require_scale_rvf(s) &&
           vext_check_isa_ill(s) &&
           /* OPFV narrowing instructions ignore vs1 check */
           vext_check_sd(s, a->rd, a->rs2, a->vm);
}

#define GEN_OPXFV_NARROW_TRANS(NAME, HELPER, FRM)                  \
static bool trans_##NAME(DisasContext *s, arg_rmr *a)              \
{                                                                  \
    if (opxfv_narrow_check(s, a)) {                                \
        uint32_t data = 0;                                         \
        static gen_helper_gvec_3_ptr * const fns[3] = {            \
            gen_helper_##HELPER##_b,                               \
            gen_helper_##HELPER##_h,                               \
            gen_helper_##HELPER##_w,                               \
        };                                                         \
        TCGLabel *over = gen_new_label();                          \
        gen_set_rm_chkfrm(s, FRM);                                 \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data = FIELD_DP32(data, VDATA, VTA, s->vta);               \
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data,                \
                           fns[s->sew]);                           \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_OPXFV_NARROW_TRANS(vfncvt_xu_f_w, vfncvt_xu_f_w, RISCV_FRM_DYN)
GEN_OPXFV_NARROW_TRANS(vfncvt_x_f_w, vfncvt_x_f_w, RISCV_FRM_DYN)
/* Reuse the helper functions from vfncvt.xu.f.w and vfncvt.x.f.w */
GEN_OPXFV_NARROW_TRANS(vfncvt_rtz_xu_f_w, vfncvt_xu_f_w, RISCV_FRM_RTZ)
GEN_OPXFV_NARROW_TRANS(vfncvt_rtz_x_f_w, vfncvt_x_f_w, RISCV_FRM_RTZ)

/*
 *** Vector Reduction Operations
 */
/* Vector Single-Width Integer Reduction Instructions */
static bool reduction_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_reduction(s, a->rs2);
}

GEN_OPIVV_TRANS(vredsum_vs, reduction_check)
GEN_OPIVV_TRANS(vredmaxu_vs, reduction_check)
GEN_OPIVV_TRANS(vredmax_vs, reduction_check)
GEN_OPIVV_TRANS(vredminu_vs, reduction_check)
GEN_OPIVV_TRANS(vredmin_vs, reduction_check)
GEN_OPIVV_TRANS(vredand_vs, reduction_check)
GEN_OPIVV_TRANS(vredor_vs, reduction_check)
GEN_OPIVV_TRANS(vredxor_vs, reduction_check)

/* Vector Widening Integer Reduction Instructions */
static bool reduction_widen_check(DisasContext *s, arg_rmrr *a)
{
    return reduction_check(s, a) && (s->sew < MO_64) &&
           ((s->sew + 1) <= (s->cfg_ptr->elen >> 4));
}

GEN_OPIVV_WIDEN_TRANS(vwredsum_vs, reduction_widen_check)
GEN_OPIVV_WIDEN_TRANS(vwredsumu_vs, reduction_widen_check)

/* Vector Single-Width Floating-Point Reduction Instructions */
static bool freduction_check(DisasContext *s, arg_rmrr *a)
{
    return reduction_check(s, a) &&
           require_rvf(s);
}

GEN_OPFVV_TRANS(vfredusum_vs, freduction_check)
GEN_OPFVV_TRANS(vfredosum_vs, freduction_check)
GEN_OPFVV_TRANS(vfredmax_vs, freduction_check)
GEN_OPFVV_TRANS(vfredmin_vs, freduction_check)

/* Vector Widening Floating-Point Reduction Instructions */
static bool freduction_widen_check(DisasContext *s, arg_rmrr *a)
{
    return reduction_widen_check(s, a) &&
           require_scale_rvf(s) &&
           (s->sew != MO_8);
}

GEN_OPFVV_WIDEN_TRANS(vfwredusum_vs, freduction_widen_check)
GEN_OPFVV_WIDEN_TRANS(vfwredosum_vs, freduction_widen_check)

/*
 *** Vector Mask Operations
 */

/* Vector Mask-Register Logical Instructions */
#define GEN_MM_TRANS(NAME)                                         \
static bool trans_##NAME(DisasContext *s, arg_r *a)                \
{                                                                  \
    if (require_rvv(s) &&                                          \
        vext_check_isa_ill(s)) {                                   \
        uint32_t data = 0;                                         \
        gen_helper_gvec_4_ptr *fn = gen_helper_##NAME;             \
        TCGLabel *over = gen_new_label();                          \
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over); \
                                                                   \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data =                                                     \
            FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);\
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),     \
                           vreg_ofs(s, a->rs1),                    \
                           vreg_ofs(s, a->rs2), tcg_env,           \
                           s->cfg_ptr->vlenb,                      \
                           s->cfg_ptr->vlenb, data, fn);           \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_MM_TRANS(vmand_mm)
GEN_MM_TRANS(vmnand_mm)
GEN_MM_TRANS(vmandn_mm)
GEN_MM_TRANS(vmxor_mm)
GEN_MM_TRANS(vmor_mm)
GEN_MM_TRANS(vmnor_mm)
GEN_MM_TRANS(vmorn_mm)
GEN_MM_TRANS(vmxnor_mm)

/* Vector count population in mask vcpop */
static bool trans_vcpop_m(DisasContext *s, arg_rmr *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s) &&
        s->vstart_eq_zero) {
        TCGv_ptr src2, mask;
        TCGv dst;
        TCGv_i32 desc;
        uint32_t data = 0;
        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);

        mask = tcg_temp_new_ptr();
        src2 = tcg_temp_new_ptr();
        dst = dest_gpr(s, a->rd);
        desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                          s->cfg_ptr->vlenb, data));

        tcg_gen_addi_ptr(src2, tcg_env, vreg_ofs(s, a->rs2));
        tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

        gen_helper_vcpop_m(dst, mask, src2, tcg_env, desc);
        gen_set_gpr(s, a->rd, dst);
        return true;
    }
    return false;
}

/* vmfirst find-first-set mask bit */
static bool trans_vfirst_m(DisasContext *s, arg_rmr *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s) &&
        s->vstart_eq_zero) {
        TCGv_ptr src2, mask;
        TCGv dst;
        TCGv_i32 desc;
        uint32_t data = 0;
        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);

        mask = tcg_temp_new_ptr();
        src2 = tcg_temp_new_ptr();
        dst = dest_gpr(s, a->rd);
        desc = tcg_constant_i32(simd_desc(s->cfg_ptr->vlenb,
                                          s->cfg_ptr->vlenb, data));

        tcg_gen_addi_ptr(src2, tcg_env, vreg_ofs(s, a->rs2));
        tcg_gen_addi_ptr(mask, tcg_env, vreg_ofs(s, 0));

        gen_helper_vfirst_m(dst, mask, src2, tcg_env, desc);
        gen_set_gpr(s, a->rd, dst);
        return true;
    }
    return false;
}

/*
 * vmsbf.m set-before-first mask bit
 * vmsif.m set-including-first mask bit
 * vmsof.m set-only-first mask bit
 */
#define GEN_M_TRANS(NAME)                                          \
static bool trans_##NAME(DisasContext *s, arg_rmr *a)              \
{                                                                  \
    if (require_rvv(s) &&                                          \
        vext_check_isa_ill(s) &&                                   \
        require_vm(a->vm, a->rd) &&                                \
        (a->rd != a->rs2) &&                                       \
        s->vstart_eq_zero) {                                       \
        uint32_t data = 0;                                         \
        gen_helper_gvec_3_ptr *fn = gen_helper_##NAME;             \
        TCGLabel *over = gen_new_label();                          \
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);          \
                                                                   \
        data = FIELD_DP32(data, VDATA, VM, a->vm);                 \
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);             \
        data =                                                     \
            FIELD_DP32(data, VDATA, VTA_ALL_1S, s->cfg_vta_all_1s);\
        data = FIELD_DP32(data, VDATA, VMA, s->vma);               \
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd),                     \
                           vreg_ofs(s, 0), vreg_ofs(s, a->rs2),    \
                           tcg_env, s->cfg_ptr->vlenb,             \
                           s->cfg_ptr->vlenb,                      \
                           data, fn);                              \
        mark_vs_dirty(s);                                          \
        gen_set_label(over);                                       \
        return true;                                               \
    }                                                              \
    return false;                                                  \
}

GEN_M_TRANS(vmsbf_m)
GEN_M_TRANS(vmsif_m)
GEN_M_TRANS(vmsof_m)

/*
 * Vector Iota Instruction
 *
 * 1. The destination register cannot overlap the source register.
 * 2. If masked, cannot overlap the mask register ('v0').
 * 3. An illegal instruction exception is raised if vstart is non-zero.
 */
static bool trans_viota_m(DisasContext *s, arg_viota_m *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s) &&
        !is_overlapped(a->rd, 1 << MAX(s->lmul, 0), a->rs2, 1) &&
        require_vm(a->vm, a->rd) &&
        require_align(a->rd, s->lmul) &&
        s->vstart_eq_zero) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        data = FIELD_DP32(data, VDATA, VTA, s->vta);
        data = FIELD_DP32(data, VDATA, VMA, s->vma);
        static gen_helper_gvec_3_ptr * const fns[4] = {
            gen_helper_viota_m_b, gen_helper_viota_m_h,
            gen_helper_viota_m_w, gen_helper_viota_m_d,
        };
        tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs2), tcg_env,
                           s->cfg_ptr->vlenb,
                           s->cfg_ptr->vlenb, data, fns[s->sew]);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

/* Vector Element Index Instruction */
static bool trans_vid_v(DisasContext *s, arg_vid_v *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s) &&
        require_align(a->rd, s->lmul) &&
        require_vm(a->vm, a->rd)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        data = FIELD_DP32(data, VDATA, VTA, s->vta);
        data = FIELD_DP32(data, VDATA, VMA, s->vma);
        static gen_helper_gvec_2_ptr * const fns[4] = {
            gen_helper_vid_v_b, gen_helper_vid_v_h,
            gen_helper_vid_v_w, gen_helper_vid_v_d,
        };
        tcg_gen_gvec_2_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           tcg_env, s->cfg_ptr->vlenb,
                           s->cfg_ptr->vlenb,
                           data, fns[s->sew]);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

/*
 *** Vector Permutation Instructions
 */

static void load_element(TCGv_i64 dest, TCGv_ptr base,
                         int ofs, int sew, bool sign)
{
    switch (sew) {
    case MO_8:
        if (!sign) {
            tcg_gen_ld8u_i64(dest, base, ofs);
        } else {
            tcg_gen_ld8s_i64(dest, base, ofs);
        }
        break;
    case MO_16:
        if (!sign) {
            tcg_gen_ld16u_i64(dest, base, ofs);
        } else {
            tcg_gen_ld16s_i64(dest, base, ofs);
        }
        break;
    case MO_32:
        if (!sign) {
            tcg_gen_ld32u_i64(dest, base, ofs);
        } else {
            tcg_gen_ld32s_i64(dest, base, ofs);
        }
        break;
    case MO_64:
        tcg_gen_ld_i64(dest, base, ofs);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

/* offset of the idx element with base register r */
static uint32_t endian_ofs(DisasContext *s, int r, int idx)
{
#if HOST_BIG_ENDIAN
    return vreg_ofs(s, r) + ((idx ^ (7 >> s->sew)) << s->sew);
#else
    return vreg_ofs(s, r) + (idx << s->sew);
#endif
}

/* adjust the index according to the endian */
static void endian_adjust(TCGv_i32 ofs, int sew)
{
#if HOST_BIG_ENDIAN
    tcg_gen_xori_i32(ofs, ofs, 7 >> sew);
#endif
}

/* Load idx >= VLMAX ? 0 : vreg[idx] */
static void vec_element_loadx(DisasContext *s, TCGv_i64 dest,
                              int vreg, TCGv idx, int vlmax)
{
    TCGv_i32 ofs = tcg_temp_new_i32();
    TCGv_ptr base = tcg_temp_new_ptr();
    TCGv_i64 t_idx = tcg_temp_new_i64();
    TCGv_i64 t_vlmax, t_zero;

    /*
     * Mask the index to the length so that we do
     * not produce an out-of-range load.
     */
    tcg_gen_trunc_tl_i32(ofs, idx);
    tcg_gen_andi_i32(ofs, ofs, vlmax - 1);

    /* Convert the index to an offset. */
    endian_adjust(ofs, s->sew);
    tcg_gen_shli_i32(ofs, ofs, s->sew);

    /* Convert the index to a pointer. */
    tcg_gen_ext_i32_ptr(base, ofs);
    tcg_gen_add_ptr(base, base, tcg_env);

    /* Perform the load. */
    load_element(dest, base,
                 vreg_ofs(s, vreg), s->sew, false);

    /* Flush out-of-range indexing to zero.  */
    t_vlmax = tcg_constant_i64(vlmax);
    t_zero = tcg_constant_i64(0);
    tcg_gen_extu_tl_i64(t_idx, idx);

    tcg_gen_movcond_i64(TCG_COND_LTU, dest, t_idx,
                        t_vlmax, dest, t_zero);
}

static void vec_element_loadi(DisasContext *s, TCGv_i64 dest,
                              int vreg, int idx, bool sign)
{
    load_element(dest, tcg_env, endian_ofs(s, vreg, idx), s->sew, sign);
}

/* Integer Scalar Move Instruction */

static void store_element(TCGv_i64 val, TCGv_ptr base,
                          int ofs, int sew)
{
    switch (sew) {
    case MO_8:
        tcg_gen_st8_i64(val, base, ofs);
        break;
    case MO_16:
        tcg_gen_st16_i64(val, base, ofs);
        break;
    case MO_32:
        tcg_gen_st32_i64(val, base, ofs);
        break;
    case MO_64:
        tcg_gen_st_i64(val, base, ofs);
        break;
    default:
        g_assert_not_reached();
        break;
    }
}

/*
 * Store vreg[idx] = val.
 * The index must be in range of VLMAX.
 */
static void vec_element_storei(DisasContext *s, int vreg,
                               int idx, TCGv_i64 val)
{
    store_element(val, tcg_env, endian_ofs(s, vreg, idx), s->sew);
}

/* vmv.x.s rd, vs2 # x[rd] = vs2[0] */
static bool trans_vmv_x_s(DisasContext *s, arg_vmv_x_s *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s)) {
        TCGv_i64 t1;
        TCGv dest;

        t1 = tcg_temp_new_i64();
        dest = tcg_temp_new();
        /*
         * load vreg and sign-extend to 64 bits,
         * then truncate to XLEN bits before storing to gpr.
         */
        vec_element_loadi(s, t1, a->rs2, 0, true);
        tcg_gen_trunc_i64_tl(dest, t1);
        gen_set_gpr(s, a->rd, dest);
        return true;
    }
    return false;
}

/* vmv.s.x vd, rs1 # vd[0] = rs1 */
static bool trans_vmv_s_x(DisasContext *s, arg_vmv_s_x *a)
{
    if (require_rvv(s) &&
        vext_check_isa_ill(s)) {
        /* This instruction ignores LMUL and vector register groups */
        TCGv_i64 t1;
        TCGv s1;
        TCGLabel *over = gen_new_label();

        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        t1 = tcg_temp_new_i64();

        /*
         * load gpr and sign-extend to 64 bits,
         * then truncate to SEW bits when storing to vreg.
         */
        s1 = get_gpr(s, a->rs1, EXT_NONE);
        tcg_gen_ext_tl_i64(t1, s1);
        vec_element_storei(s, a->rd, 0, t1);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

/* Floating-Point Scalar Move Instructions */
static bool trans_vfmv_f_s(DisasContext *s, arg_vfmv_f_s *a)
{
    if (require_rvv(s) &&
        require_rvf(s) &&
        vext_check_isa_ill(s)) {
        gen_set_rm(s, RISCV_FRM_DYN);

        unsigned int ofs = (8 << s->sew);
        unsigned int len = 64 - ofs;
        TCGv_i64 t_nan;

        vec_element_loadi(s, cpu_fpr[a->rd], a->rs2, 0, false);
        /* NaN-box f[rd] as necessary for SEW */
        if (len) {
            t_nan = tcg_constant_i64(UINT64_MAX);
            tcg_gen_deposit_i64(cpu_fpr[a->rd], cpu_fpr[a->rd],
                                t_nan, ofs, len);
        }

        mark_fs_dirty(s);
        return true;
    }
    return false;
}

/* vfmv.s.f vd, rs1 # vd[0] = rs1 (vs2=0) */
static bool trans_vfmv_s_f(DisasContext *s, arg_vfmv_s_f *a)
{
    if (require_rvv(s) &&
        require_rvf(s) &&
        vext_check_isa_ill(s)) {
        gen_set_rm(s, RISCV_FRM_DYN);

        /* The instructions ignore LMUL and vector register group. */
        TCGv_i64 t1;
        TCGLabel *over = gen_new_label();

        /* if vstart >= vl, skip vector register write back */
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        /* NaN-box f[rs1] */
        t1 = tcg_temp_new_i64();
        do_nanbox(s, t1, cpu_fpr[a->rs1]);

        vec_element_storei(s, a->rd, 0, t1);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

/* Vector Slide Instructions */
static bool slideup_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_slide(s, a->rd, a->rs2, a->vm, true);
}

GEN_OPIVX_TRANS(vslideup_vx, slideup_check)
GEN_OPIVX_TRANS(vslide1up_vx, slideup_check)
GEN_OPIVI_TRANS(vslideup_vi, IMM_ZX, vslideup_vx, slideup_check)

static bool slidedown_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           vext_check_slide(s, a->rd, a->rs2, a->vm, false);
}

GEN_OPIVX_TRANS(vslidedown_vx, slidedown_check)
GEN_OPIVX_TRANS(vslide1down_vx, slidedown_check)
GEN_OPIVI_TRANS(vslidedown_vi, IMM_ZX, vslidedown_vx, slidedown_check)

/* Vector Floating-Point Slide Instructions */
static bool fslideup_check(DisasContext *s, arg_rmrr *a)
{
    return slideup_check(s, a) &&
           require_rvf(s);
}

static bool fslidedown_check(DisasContext *s, arg_rmrr *a)
{
    return slidedown_check(s, a) &&
           require_rvf(s);
}

GEN_OPFVF_TRANS(vfslide1up_vf, fslideup_check)
GEN_OPFVF_TRANS(vfslide1down_vf, fslidedown_check)

/* Vector Register Gather Instruction */
static bool vrgather_vv_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           require_align(a->rd, s->lmul) &&
           require_align(a->rs1, s->lmul) &&
           require_align(a->rs2, s->lmul) &&
           (a->rd != a->rs2 && a->rd != a->rs1) &&
           require_vm(a->vm, a->rd);
}

static bool vrgatherei16_vv_check(DisasContext *s, arg_rmrr *a)
{
    int8_t emul = MO_16 - s->sew + s->lmul;
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           (emul >= -3 && emul <= 3) &&
           require_align(a->rd, s->lmul) &&
           require_align(a->rs1, emul) &&
           require_align(a->rs2, s->lmul) &&
           (a->rd != a->rs2 && a->rd != a->rs1) &&
           !is_overlapped(a->rd, 1 << MAX(s->lmul, 0),
                          a->rs1, 1 << MAX(emul, 0)) &&
           !is_overlapped(a->rd, 1 << MAX(s->lmul, 0),
                          a->rs2, 1 << MAX(s->lmul, 0)) &&
           require_vm(a->vm, a->rd);
}

GEN_OPIVV_TRANS(vrgather_vv, vrgather_vv_check)
GEN_OPIVV_TRANS(vrgatherei16_vv, vrgatherei16_vv_check)

static bool vrgather_vx_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           require_align(a->rd, s->lmul) &&
           require_align(a->rs2, s->lmul) &&
           (a->rd != a->rs2) &&
           require_vm(a->vm, a->rd);
}

/* vrgather.vx vd, vs2, rs1, vm # vd[i] = (x[rs1] >= VLMAX) ? 0 : vs2[rs1] */
static bool trans_vrgather_vx(DisasContext *s, arg_rmrr *a)
{
    if (!vrgather_vx_check(s, a)) {
        return false;
    }

    if (a->vm && s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
        int vlmax = vext_get_vlmax(s->cfg_ptr->vlenb, s->sew, s->lmul);
        TCGv_i64 dest = tcg_temp_new_i64();

        if (a->rs1 == 0) {
            vec_element_loadi(s, dest, a->rs2, 0, false);
        } else {
            vec_element_loadx(s, dest, a->rs2, cpu_gpr[a->rs1], vlmax);
        }

        tcg_gen_gvec_dup_i64(s->sew, vreg_ofs(s, a->rd),
                             MAXSZ(s), MAXSZ(s), dest);
        mark_vs_dirty(s);
    } else {
        static gen_helper_opivx * const fns[4] = {
            gen_helper_vrgather_vx_b, gen_helper_vrgather_vx_h,
            gen_helper_vrgather_vx_w, gen_helper_vrgather_vx_d
        };
        return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fns[s->sew], s);
    }
    return true;
}

/* vrgather.vi vd, vs2, imm, vm # vd[i] = (imm >= VLMAX) ? 0 : vs2[imm] */
static bool trans_vrgather_vi(DisasContext *s, arg_rmrr *a)
{
    if (!vrgather_vx_check(s, a)) {
        return false;
    }

    if (a->vm && s->vl_eq_vlmax && !(s->vta && s->lmul < 0)) {
        int vlmax = vext_get_vlmax(s->cfg_ptr->vlenb, s->sew, s->lmul);
        if (a->rs1 >= vlmax) {
            tcg_gen_gvec_dup_imm(MO_64, vreg_ofs(s, a->rd),
                                 MAXSZ(s), MAXSZ(s), 0);
        } else {
            tcg_gen_gvec_dup_mem(s->sew, vreg_ofs(s, a->rd),
                                 endian_ofs(s, a->rs2, a->rs1),
                                 MAXSZ(s), MAXSZ(s));
        }
        mark_vs_dirty(s);
    } else {
        static gen_helper_opivx * const fns[4] = {
            gen_helper_vrgather_vx_b, gen_helper_vrgather_vx_h,
            gen_helper_vrgather_vx_w, gen_helper_vrgather_vx_d
        };
        return opivi_trans(a->rd, a->rs1, a->rs2, a->vm, fns[s->sew],
                           s, IMM_ZX);
    }
    return true;
}

/*
 * Vector Compress Instruction
 *
 * The destination vector register group cannot overlap the
 * source vector register group or the source mask register.
 */
static bool vcompress_vm_check(DisasContext *s, arg_r *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           require_align(a->rd, s->lmul) &&
           require_align(a->rs2, s->lmul) &&
           (a->rd != a->rs2) &&
           !is_overlapped(a->rd, 1 << MAX(s->lmul, 0), a->rs1, 1) &&
           s->vstart_eq_zero;
}

static bool trans_vcompress_vm(DisasContext *s, arg_r *a)
{
    if (vcompress_vm_check(s, a)) {
        uint32_t data = 0;
        static gen_helper_gvec_4_ptr * const fns[4] = {
            gen_helper_vcompress_vm_b, gen_helper_vcompress_vm_h,
            gen_helper_vcompress_vm_w, gen_helper_vcompress_vm_d,
        };
        TCGLabel *over = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        data = FIELD_DP32(data, VDATA, VTA, s->vta);
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs1), vreg_ofs(s, a->rs2),
                           tcg_env, s->cfg_ptr->vlenb,
                           s->cfg_ptr->vlenb, data,
                           fns[s->sew]);
        mark_vs_dirty(s);
        gen_set_label(over);
        return true;
    }
    return false;
}

/*
 * Whole Vector Register Move Instructions depend on vtype register(vsew).
 * Thus, we need to check vill bit. (Section 16.6)
 */
#define GEN_VMV_WHOLE_TRANS(NAME, LEN)                             \
static bool trans_##NAME(DisasContext *s, arg_##NAME * a)               \
{                                                                       \
    if (require_rvv(s) &&                                               \
        vext_check_isa_ill(s) &&                                        \
        QEMU_IS_ALIGNED(a->rd, LEN) &&                                  \
        QEMU_IS_ALIGNED(a->rs2, LEN)) {                                 \
        uint32_t maxsz = s->cfg_ptr->vlenb * LEN;                       \
        if (s->vstart_eq_zero) {                                        \
            tcg_gen_gvec_mov(s->sew, vreg_ofs(s, a->rd),                \
                             vreg_ofs(s, a->rs2), maxsz, maxsz);        \
            mark_vs_dirty(s);                                           \
        } else {                                                        \
            TCGLabel *over = gen_new_label();                           \
            tcg_gen_brcondi_tl(TCG_COND_GEU, cpu_vstart, maxsz, over);  \
            tcg_gen_gvec_2_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, a->rs2), \
                               tcg_env, maxsz, maxsz, 0, gen_helper_vmvr_v); \
            mark_vs_dirty(s);                                           \
            gen_set_label(over);                                        \
        }                                                               \
        return true;                                                    \
    }                                                                   \
    return false;                                                       \
}

GEN_VMV_WHOLE_TRANS(vmv1r_v, 1)
GEN_VMV_WHOLE_TRANS(vmv2r_v, 2)
GEN_VMV_WHOLE_TRANS(vmv4r_v, 4)
GEN_VMV_WHOLE_TRANS(vmv8r_v, 8)

static bool int_ext_check(DisasContext *s, arg_rmr *a, uint8_t div)
{
    uint8_t from = (s->sew + 3) - div;
    bool ret = require_rvv(s) &&
        (from >= 3 && from <= 8) &&
        (a->rd != a->rs2) &&
        require_align(a->rd, s->lmul) &&
        require_align(a->rs2, s->lmul - div) &&
        require_vm(a->vm, a->rd) &&
        require_noover(a->rd, s->lmul, a->rs2, s->lmul - div);
    return ret;
}

static bool int_ext_op(DisasContext *s, arg_rmr *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_gvec_3_ptr *fn;
    TCGLabel *over = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

    static gen_helper_gvec_3_ptr * const fns[6][4] = {
        {
            NULL, gen_helper_vzext_vf2_h,
            gen_helper_vzext_vf2_w, gen_helper_vzext_vf2_d
        },
        {
            NULL, NULL,
            gen_helper_vzext_vf4_w, gen_helper_vzext_vf4_d,
        },
        {
            NULL, NULL,
            NULL, gen_helper_vzext_vf8_d
        },
        {
            NULL, gen_helper_vsext_vf2_h,
            gen_helper_vsext_vf2_w, gen_helper_vsext_vf2_d
        },
        {
            NULL, NULL,
            gen_helper_vsext_vf4_w, gen_helper_vsext_vf4_d,
        },
        {
            NULL, NULL,
            NULL, gen_helper_vsext_vf8_d
        }
    };

    fn = fns[seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, VTA, s->vta);
    data = FIELD_DP32(data, VDATA, VMA, s->vma);

    tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                       vreg_ofs(s, a->rs2), tcg_env,
                       s->cfg_ptr->vlenb,
                       s->cfg_ptr->vlenb, data, fn);

    mark_vs_dirty(s);
    gen_set_label(over);
    return true;
}

/* Vector Integer Extension */
#define GEN_INT_EXT_TRANS(NAME, DIV, SEQ)             \
static bool trans_##NAME(DisasContext *s, arg_rmr *a) \
{                                                     \
    if (int_ext_check(s, a, DIV)) {                   \
        return int_ext_op(s, a, SEQ);                 \
    }                                                 \
    return false;                                     \
}

GEN_INT_EXT_TRANS(vzext_vf2, 1, 0)
GEN_INT_EXT_TRANS(vzext_vf4, 2, 1)
GEN_INT_EXT_TRANS(vzext_vf8, 3, 2)
GEN_INT_EXT_TRANS(vsext_vf2, 1, 3)
GEN_INT_EXT_TRANS(vsext_vf4, 2, 4)
GEN_INT_EXT_TRANS(vsext_vf8, 3, 5)
