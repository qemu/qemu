/*
 * RISC-V translation routines for the RVV Standard Extension.
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

static bool trans_vsetvl(DisasContext *ctx, arg_vsetvl *a)
{
    TCGv s1, s2, dst;

    if (!has_ext(ctx, RVV)) {
        return false;
    }

    s2 = tcg_temp_new();
    dst = tcg_temp_new();

    /* Using x0 as the rs1 register specifier, encodes an infinite AVL */
    if (a->rs1 == 0) {
        /* As the mask is at least one bit, RV_VLEN_MAX is >= VLMAX */
        s1 = tcg_const_tl(RV_VLEN_MAX);
    } else {
        s1 = tcg_temp_new();
        gen_get_gpr(s1, a->rs1);
    }
    gen_get_gpr(s2, a->rs2);
    gen_helper_vsetvl(dst, cpu_env, s1, s2);
    gen_set_gpr(a->rd, dst);
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    lookup_and_goto_ptr(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;

    tcg_temp_free(s1);
    tcg_temp_free(s2);
    tcg_temp_free(dst);
    return true;
}

static bool trans_vsetvli(DisasContext *ctx, arg_vsetvli *a)
{
    TCGv s1, s2, dst;

    if (!has_ext(ctx, RVV)) {
        return false;
    }

    s2 = tcg_const_tl(a->zimm);
    dst = tcg_temp_new();

    /* Using x0 as the rs1 register specifier, encodes an infinite AVL */
    if (a->rs1 == 0) {
        /* As the mask is at least one bit, RV_VLEN_MAX is >= VLMAX */
        s1 = tcg_const_tl(RV_VLEN_MAX);
    } else {
        s1 = tcg_temp_new();
        gen_get_gpr(s1, a->rs1);
    }
    gen_helper_vsetvl(dst, cpu_env, s1, s2);
    gen_set_gpr(a->rd, dst);
    gen_goto_tb(ctx, 0, ctx->pc_succ_insn);
    ctx->base.is_jmp = DISAS_NORETURN;

    tcg_temp_free(s1);
    tcg_temp_free(s2);
    tcg_temp_free(dst);
    return true;
}

/* vector register offset from env */
static uint32_t vreg_ofs(DisasContext *s, int reg)
{
    return offsetof(CPURISCVState, vreg) + reg * s->vlen / 8;
}

/* check functions */

/*
 * In cpu_get_tb_cpu_state(), set VILL if RVV was not present.
 * So RVV is also be checked in this function.
 */
static bool vext_check_isa_ill(DisasContext *s)
{
    return !s->vill;
}

/*
 * There are two rules check here.
 *
 * 1. Vector register numbers are multiples of LMUL. (Section 3.2)
 *
 * 2. For all widening instructions, the destination LMUL value must also be
 *    a supported LMUL value. (Section 11.2)
 */
static bool vext_check_reg(DisasContext *s, uint32_t reg, bool widen)
{
    /*
     * The destination vector register group results are arranged as if both
     * SEW and LMUL were at twice their current settings. (Section 11.2).
     */
    int legal = widen ? 2 << s->lmul : 1 << s->lmul;

    return !((s->lmul == 0x3 && widen) || (reg % legal));
}

/*
 * There are two rules check here.
 *
 * 1. The destination vector register group for a masked vector instruction can
 *    only overlap the source mask register (v0) when LMUL=1. (Section 5.3)
 *
 * 2. In widen instructions and some other insturctions, like vslideup.vx,
 *    there is no need to check whether LMUL=1.
 */
static bool vext_check_overlap_mask(DisasContext *s, uint32_t vd, bool vm,
    bool force)
{
    return (vm != 0 || vd != 0) || (!force && (s->lmul == 0));
}

/* The LMUL setting must be such that LMUL * NFIELDS <= 8. (Section 7.8) */
static bool vext_check_nf(DisasContext *s, uint32_t nf)
{
    return (1 << s->lmul) * nf <= 8;
}

/*
 * The destination vector register group cannot overlap a source vector register
 * group of a different element width. (Section 11.2)
 */
static inline bool vext_check_overlap_group(int rd, int dlen, int rs, int slen)
{
    return ((rd >= rs + slen) || (rs >= rd + dlen));
}
/* common translation macro */
#define GEN_VEXT_TRANS(NAME, SEQ, ARGTYPE, OP, CHECK)      \
static bool trans_##NAME(DisasContext *s, arg_##ARGTYPE *a)\
{                                                          \
    if (CHECK(s, a)) {                                     \
        return OP(s, a, SEQ);                              \
    }                                                      \
    return false;                                          \
}

/*
 *** unit stride load and store
 */
typedef void gen_helper_ldst_us(TCGv_ptr, TCGv_ptr, TCGv,
                                TCGv_env, TCGv_i32);

static bool ldst_us_trans(uint32_t vd, uint32_t rs1, uint32_t data,
                          gen_helper_ldst_us *fn, DisasContext *s)
{
    TCGv_ptr dest, mask;
    TCGv base;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    base = tcg_temp_new();

    /*
     * As simd_desc supports at most 256 bytes, and in this implementation,
     * the max vector group length is 2048 bytes. So split it into two parts.
     *
     * The first part is vlen in bytes, encoded in maxsz of simd_desc.
     * The second part is lmul, encoded in data of simd_desc.
     */
    desc = tcg_const_i32(simd_desc(0, s->vlen / 8, data));

    gen_get_gpr(base, rs1);
    tcg_gen_addi_ptr(dest, cpu_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(mask, cpu_env, vreg_ofs(s, 0));

    fn(dest, mask, base, cpu_env, desc);

    tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(mask);
    tcg_temp_free(base);
    tcg_temp_free_i32(desc);
    gen_set_label(over);
    return true;
}

static bool ld_us_op(DisasContext *s, arg_r2nfvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;
    static gen_helper_ldst_us * const fns[2][7][4] = {
        /* masked unit stride load */
        { { gen_helper_vlb_v_b_mask,  gen_helper_vlb_v_h_mask,
            gen_helper_vlb_v_w_mask,  gen_helper_vlb_v_d_mask },
          { NULL,                     gen_helper_vlh_v_h_mask,
            gen_helper_vlh_v_w_mask,  gen_helper_vlh_v_d_mask },
          { NULL,                     NULL,
            gen_helper_vlw_v_w_mask,  gen_helper_vlw_v_d_mask },
          { gen_helper_vle_v_b_mask,  gen_helper_vle_v_h_mask,
            gen_helper_vle_v_w_mask,  gen_helper_vle_v_d_mask },
          { gen_helper_vlbu_v_b_mask, gen_helper_vlbu_v_h_mask,
            gen_helper_vlbu_v_w_mask, gen_helper_vlbu_v_d_mask },
          { NULL,                     gen_helper_vlhu_v_h_mask,
            gen_helper_vlhu_v_w_mask, gen_helper_vlhu_v_d_mask },
          { NULL,                     NULL,
            gen_helper_vlwu_v_w_mask, gen_helper_vlwu_v_d_mask } },
        /* unmasked unit stride load */
        { { gen_helper_vlb_v_b,  gen_helper_vlb_v_h,
            gen_helper_vlb_v_w,  gen_helper_vlb_v_d },
          { NULL,                gen_helper_vlh_v_h,
            gen_helper_vlh_v_w,  gen_helper_vlh_v_d },
          { NULL,                NULL,
            gen_helper_vlw_v_w,  gen_helper_vlw_v_d },
          { gen_helper_vle_v_b,  gen_helper_vle_v_h,
            gen_helper_vle_v_w,  gen_helper_vle_v_d },
          { gen_helper_vlbu_v_b, gen_helper_vlbu_v_h,
            gen_helper_vlbu_v_w, gen_helper_vlbu_v_d },
          { NULL,                gen_helper_vlhu_v_h,
            gen_helper_vlhu_v_w, gen_helper_vlhu_v_d },
          { NULL,                NULL,
            gen_helper_vlwu_v_w, gen_helper_vlwu_v_d } }
    };

    fn =  fns[a->vm][seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldst_us_trans(a->rd, a->rs1, data, fn, s);
}

static bool ld_us_check(DisasContext *s, arg_r2nfvm* a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, false) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_nf(s, a->nf));
}

GEN_VEXT_TRANS(vlb_v, 0, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vlh_v, 1, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vlw_v, 2, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vle_v, 3, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vlbu_v, 4, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vlhu_v, 5, r2nfvm, ld_us_op, ld_us_check)
GEN_VEXT_TRANS(vlwu_v, 6, r2nfvm, ld_us_op, ld_us_check)

static bool st_us_op(DisasContext *s, arg_r2nfvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;
    static gen_helper_ldst_us * const fns[2][4][4] = {
        /* masked unit stride load and store */
        { { gen_helper_vsb_v_b_mask,  gen_helper_vsb_v_h_mask,
            gen_helper_vsb_v_w_mask,  gen_helper_vsb_v_d_mask },
          { NULL,                     gen_helper_vsh_v_h_mask,
            gen_helper_vsh_v_w_mask,  gen_helper_vsh_v_d_mask },
          { NULL,                     NULL,
            gen_helper_vsw_v_w_mask,  gen_helper_vsw_v_d_mask },
          { gen_helper_vse_v_b_mask,  gen_helper_vse_v_h_mask,
            gen_helper_vse_v_w_mask,  gen_helper_vse_v_d_mask } },
        /* unmasked unit stride store */
        { { gen_helper_vsb_v_b,  gen_helper_vsb_v_h,
            gen_helper_vsb_v_w,  gen_helper_vsb_v_d },
          { NULL,                gen_helper_vsh_v_h,
            gen_helper_vsh_v_w,  gen_helper_vsh_v_d },
          { NULL,                NULL,
            gen_helper_vsw_v_w,  gen_helper_vsw_v_d },
          { gen_helper_vse_v_b,  gen_helper_vse_v_h,
            gen_helper_vse_v_w,  gen_helper_vse_v_d } }
    };

    fn =  fns[a->vm][seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldst_us_trans(a->rd, a->rs1, data, fn, s);
}

static bool st_us_check(DisasContext *s, arg_r2nfvm* a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_nf(s, a->nf));
}

GEN_VEXT_TRANS(vsb_v, 0, r2nfvm, st_us_op, st_us_check)
GEN_VEXT_TRANS(vsh_v, 1, r2nfvm, st_us_op, st_us_check)
GEN_VEXT_TRANS(vsw_v, 2, r2nfvm, st_us_op, st_us_check)
GEN_VEXT_TRANS(vse_v, 3, r2nfvm, st_us_op, st_us_check)

/*
 *** stride load and store
 */
typedef void gen_helper_ldst_stride(TCGv_ptr, TCGv_ptr, TCGv,
                                    TCGv, TCGv_env, TCGv_i32);

static bool ldst_stride_trans(uint32_t vd, uint32_t rs1, uint32_t rs2,
                              uint32_t data, gen_helper_ldst_stride *fn,
                              DisasContext *s)
{
    TCGv_ptr dest, mask;
    TCGv base, stride;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    base = tcg_temp_new();
    stride = tcg_temp_new();
    desc = tcg_const_i32(simd_desc(0, s->vlen / 8, data));

    gen_get_gpr(base, rs1);
    gen_get_gpr(stride, rs2);
    tcg_gen_addi_ptr(dest, cpu_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(mask, cpu_env, vreg_ofs(s, 0));

    fn(dest, mask, base, stride, cpu_env, desc);

    tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(mask);
    tcg_temp_free(base);
    tcg_temp_free(stride);
    tcg_temp_free_i32(desc);
    gen_set_label(over);
    return true;
}

static bool ld_stride_op(DisasContext *s, arg_rnfvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_ldst_stride *fn;
    static gen_helper_ldst_stride * const fns[7][4] = {
        { gen_helper_vlsb_v_b,  gen_helper_vlsb_v_h,
          gen_helper_vlsb_v_w,  gen_helper_vlsb_v_d },
        { NULL,                 gen_helper_vlsh_v_h,
          gen_helper_vlsh_v_w,  gen_helper_vlsh_v_d },
        { NULL,                 NULL,
          gen_helper_vlsw_v_w,  gen_helper_vlsw_v_d },
        { gen_helper_vlse_v_b,  gen_helper_vlse_v_h,
          gen_helper_vlse_v_w,  gen_helper_vlse_v_d },
        { gen_helper_vlsbu_v_b, gen_helper_vlsbu_v_h,
          gen_helper_vlsbu_v_w, gen_helper_vlsbu_v_d },
        { NULL,                 gen_helper_vlshu_v_h,
          gen_helper_vlshu_v_w, gen_helper_vlshu_v_d },
        { NULL,                 NULL,
          gen_helper_vlswu_v_w, gen_helper_vlswu_v_d },
    };

    fn =  fns[seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldst_stride_trans(a->rd, a->rs1, a->rs2, data, fn, s);
}

static bool ld_stride_check(DisasContext *s, arg_rnfvm* a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, false) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_nf(s, a->nf));
}

GEN_VEXT_TRANS(vlsb_v, 0, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlsh_v, 1, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlsw_v, 2, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlse_v, 3, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlsbu_v, 4, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlshu_v, 5, rnfvm, ld_stride_op, ld_stride_check)
GEN_VEXT_TRANS(vlswu_v, 6, rnfvm, ld_stride_op, ld_stride_check)

static bool st_stride_op(DisasContext *s, arg_rnfvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_ldst_stride *fn;
    static gen_helper_ldst_stride * const fns[4][4] = {
        /* masked stride store */
        { gen_helper_vssb_v_b,  gen_helper_vssb_v_h,
          gen_helper_vssb_v_w,  gen_helper_vssb_v_d },
        { NULL,                 gen_helper_vssh_v_h,
          gen_helper_vssh_v_w,  gen_helper_vssh_v_d },
        { NULL,                 NULL,
          gen_helper_vssw_v_w,  gen_helper_vssw_v_d },
        { gen_helper_vsse_v_b,  gen_helper_vsse_v_h,
          gen_helper_vsse_v_w,  gen_helper_vsse_v_d }
    };

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    fn =  fns[seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    return ldst_stride_trans(a->rd, a->rs1, a->rs2, data, fn, s);
}

static bool st_stride_check(DisasContext *s, arg_rnfvm* a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_nf(s, a->nf));
}

GEN_VEXT_TRANS(vssb_v, 0, rnfvm, st_stride_op, st_stride_check)
GEN_VEXT_TRANS(vssh_v, 1, rnfvm, st_stride_op, st_stride_check)
GEN_VEXT_TRANS(vssw_v, 2, rnfvm, st_stride_op, st_stride_check)
GEN_VEXT_TRANS(vsse_v, 3, rnfvm, st_stride_op, st_stride_check)

/*
 *** index load and store
 */
typedef void gen_helper_ldst_index(TCGv_ptr, TCGv_ptr, TCGv,
                                   TCGv_ptr, TCGv_env, TCGv_i32);

static bool ldst_index_trans(uint32_t vd, uint32_t rs1, uint32_t vs2,
                             uint32_t data, gen_helper_ldst_index *fn,
                             DisasContext *s)
{
    TCGv_ptr dest, mask, index;
    TCGv base;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    index = tcg_temp_new_ptr();
    base = tcg_temp_new();
    desc = tcg_const_i32(simd_desc(0, s->vlen / 8, data));

    gen_get_gpr(base, rs1);
    tcg_gen_addi_ptr(dest, cpu_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(index, cpu_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, cpu_env, vreg_ofs(s, 0));

    fn(dest, mask, base, index, cpu_env, desc);

    tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(mask);
    tcg_temp_free_ptr(index);
    tcg_temp_free(base);
    tcg_temp_free_i32(desc);
    gen_set_label(over);
    return true;
}

static bool ld_index_op(DisasContext *s, arg_rnfvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_ldst_index *fn;
    static gen_helper_ldst_index * const fns[7][4] = {
        { gen_helper_vlxb_v_b,  gen_helper_vlxb_v_h,
          gen_helper_vlxb_v_w,  gen_helper_vlxb_v_d },
        { NULL,                 gen_helper_vlxh_v_h,
          gen_helper_vlxh_v_w,  gen_helper_vlxh_v_d },
        { NULL,                 NULL,
          gen_helper_vlxw_v_w,  gen_helper_vlxw_v_d },
        { gen_helper_vlxe_v_b,  gen_helper_vlxe_v_h,
          gen_helper_vlxe_v_w,  gen_helper_vlxe_v_d },
        { gen_helper_vlxbu_v_b, gen_helper_vlxbu_v_h,
          gen_helper_vlxbu_v_w, gen_helper_vlxbu_v_d },
        { NULL,                 gen_helper_vlxhu_v_h,
          gen_helper_vlxhu_v_w, gen_helper_vlxhu_v_d },
        { NULL,                 NULL,
          gen_helper_vlxwu_v_w, gen_helper_vlxwu_v_d },
    };

    fn =  fns[seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldst_index_trans(a->rd, a->rs1, a->rs2, data, fn, s);
}

static bool ld_index_check(DisasContext *s, arg_rnfvm* a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, false) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_reg(s, a->rs2, false) &&
            vext_check_nf(s, a->nf));
}

GEN_VEXT_TRANS(vlxb_v, 0, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxh_v, 1, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxw_v, 2, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxe_v, 3, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxbu_v, 4, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxhu_v, 5, rnfvm, ld_index_op, ld_index_check)
GEN_VEXT_TRANS(vlxwu_v, 6, rnfvm, ld_index_op, ld_index_check)

static bool st_index_op(DisasContext *s, arg_rnfvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_ldst_index *fn;
    static gen_helper_ldst_index * const fns[4][4] = {
        { gen_helper_vsxb_v_b,  gen_helper_vsxb_v_h,
          gen_helper_vsxb_v_w,  gen_helper_vsxb_v_d },
        { NULL,                 gen_helper_vsxh_v_h,
          gen_helper_vsxh_v_w,  gen_helper_vsxh_v_d },
        { NULL,                 NULL,
          gen_helper_vsxw_v_w,  gen_helper_vsxw_v_d },
        { gen_helper_vsxe_v_b,  gen_helper_vsxe_v_h,
          gen_helper_vsxe_v_w,  gen_helper_vsxe_v_d }
    };

    fn =  fns[seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldst_index_trans(a->rd, a->rs1, a->rs2, data, fn, s);
}

static bool st_index_check(DisasContext *s, arg_rnfvm* a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_reg(s, a->rs2, false) &&
            vext_check_nf(s, a->nf));
}

GEN_VEXT_TRANS(vsxb_v, 0, rnfvm, st_index_op, st_index_check)
GEN_VEXT_TRANS(vsxh_v, 1, rnfvm, st_index_op, st_index_check)
GEN_VEXT_TRANS(vsxw_v, 2, rnfvm, st_index_op, st_index_check)
GEN_VEXT_TRANS(vsxe_v, 3, rnfvm, st_index_op, st_index_check)

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
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    base = tcg_temp_new();
    desc = tcg_const_i32(simd_desc(0, s->vlen / 8, data));

    gen_get_gpr(base, rs1);
    tcg_gen_addi_ptr(dest, cpu_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(mask, cpu_env, vreg_ofs(s, 0));

    fn(dest, mask, base, cpu_env, desc);

    tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(mask);
    tcg_temp_free(base);
    tcg_temp_free_i32(desc);
    gen_set_label(over);
    return true;
}

static bool ldff_op(DisasContext *s, arg_r2nfvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_ldst_us *fn;
    static gen_helper_ldst_us * const fns[7][4] = {
        { gen_helper_vlbff_v_b,  gen_helper_vlbff_v_h,
          gen_helper_vlbff_v_w,  gen_helper_vlbff_v_d },
        { NULL,                  gen_helper_vlhff_v_h,
          gen_helper_vlhff_v_w,  gen_helper_vlhff_v_d },
        { NULL,                  NULL,
          gen_helper_vlwff_v_w,  gen_helper_vlwff_v_d },
        { gen_helper_vleff_v_b,  gen_helper_vleff_v_h,
          gen_helper_vleff_v_w,  gen_helper_vleff_v_d },
        { gen_helper_vlbuff_v_b, gen_helper_vlbuff_v_h,
          gen_helper_vlbuff_v_w, gen_helper_vlbuff_v_d },
        { NULL,                  gen_helper_vlhuff_v_h,
          gen_helper_vlhuff_v_w, gen_helper_vlhuff_v_d },
        { NULL,                  NULL,
          gen_helper_vlwuff_v_w, gen_helper_vlwuff_v_d }
    };

    fn =  fns[seq][s->sew];
    if (fn == NULL) {
        return false;
    }

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, NF, a->nf);
    return ldff_trans(a->rd, a->rs1, data, fn, s);
}

GEN_VEXT_TRANS(vlbff_v, 0, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vlhff_v, 1, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vlwff_v, 2, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vleff_v, 3, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vlbuff_v, 4, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vlhuff_v, 5, r2nfvm, ldff_op, ld_us_check)
GEN_VEXT_TRANS(vlwuff_v, 6, r2nfvm, ldff_op, ld_us_check)

/*
 *** vector atomic operation
 */
typedef void gen_helper_amo(TCGv_ptr, TCGv_ptr, TCGv, TCGv_ptr,
                            TCGv_env, TCGv_i32);

static bool amo_trans(uint32_t vd, uint32_t rs1, uint32_t vs2,
                      uint32_t data, gen_helper_amo *fn, DisasContext *s)
{
    TCGv_ptr dest, mask, index;
    TCGv base;
    TCGv_i32 desc;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    index = tcg_temp_new_ptr();
    base = tcg_temp_new();
    desc = tcg_const_i32(simd_desc(0, s->vlen / 8, data));

    gen_get_gpr(base, rs1);
    tcg_gen_addi_ptr(dest, cpu_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(index, cpu_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, cpu_env, vreg_ofs(s, 0));

    fn(dest, mask, base, index, cpu_env, desc);

    tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(mask);
    tcg_temp_free_ptr(index);
    tcg_temp_free(base);
    tcg_temp_free_i32(desc);
    gen_set_label(over);
    return true;
}

static bool amo_op(DisasContext *s, arg_rwdvm *a, uint8_t seq)
{
    uint32_t data = 0;
    gen_helper_amo *fn;
    static gen_helper_amo *const fnsw[9] = {
        /* no atomic operation */
        gen_helper_vamoswapw_v_w,
        gen_helper_vamoaddw_v_w,
        gen_helper_vamoxorw_v_w,
        gen_helper_vamoandw_v_w,
        gen_helper_vamoorw_v_w,
        gen_helper_vamominw_v_w,
        gen_helper_vamomaxw_v_w,
        gen_helper_vamominuw_v_w,
        gen_helper_vamomaxuw_v_w
    };
#ifdef TARGET_RISCV64
    static gen_helper_amo *const fnsd[18] = {
        gen_helper_vamoswapw_v_d,
        gen_helper_vamoaddw_v_d,
        gen_helper_vamoxorw_v_d,
        gen_helper_vamoandw_v_d,
        gen_helper_vamoorw_v_d,
        gen_helper_vamominw_v_d,
        gen_helper_vamomaxw_v_d,
        gen_helper_vamominuw_v_d,
        gen_helper_vamomaxuw_v_d,
        gen_helper_vamoswapd_v_d,
        gen_helper_vamoaddd_v_d,
        gen_helper_vamoxord_v_d,
        gen_helper_vamoandd_v_d,
        gen_helper_vamoord_v_d,
        gen_helper_vamomind_v_d,
        gen_helper_vamomaxd_v_d,
        gen_helper_vamominud_v_d,
        gen_helper_vamomaxud_v_d
    };
#endif

    if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        gen_helper_exit_atomic(cpu_env);
        s->base.is_jmp = DISAS_NORETURN;
        return true;
    } else {
        if (s->sew == 3) {
#ifdef TARGET_RISCV64
            fn = fnsd[seq];
#else
            /* Check done in amo_check(). */
            g_assert_not_reached();
#endif
        } else {
            fn = fnsw[seq];
        }
    }

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, a->vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    data = FIELD_DP32(data, VDATA, WD, a->wd);
    return amo_trans(a->rd, a->rs1, a->rs2, data, fn, s);
}
/*
 * There are two rules check here.
 *
 * 1. SEW must be at least as wide as the AMO memory element size.
 *
 * 2. If SEW is greater than XLEN, an illegal instruction exception is raised.
 */
static bool amo_check(DisasContext *s, arg_rwdvm* a)
{
    return (!s->vill && has_ext(s, RVA) &&
            (!a->wd || vext_check_overlap_mask(s, a->rd, a->vm, false)) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_reg(s, a->rs2, false) &&
            ((1 << s->sew) <= sizeof(target_ulong)) &&
            ((1 << s->sew) >= 4));
}

GEN_VEXT_TRANS(vamoswapw_v, 0, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoaddw_v, 1, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoxorw_v, 2, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoandw_v, 3, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoorw_v, 4, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamominw_v, 5, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamomaxw_v, 6, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamominuw_v, 7, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamomaxuw_v, 8, rwdvm, amo_op, amo_check)
#ifdef TARGET_RISCV64
GEN_VEXT_TRANS(vamoswapd_v, 9, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoaddd_v, 10, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoxord_v, 11, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoandd_v, 12, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamoord_v, 13, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamomind_v, 14, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamomaxd_v, 15, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamominud_v, 16, rwdvm, amo_op, amo_check)
GEN_VEXT_TRANS(vamomaxud_v, 17, rwdvm, amo_op, amo_check)
#endif

/*
 *** Vector Integer Arithmetic Instructions
 */
#define MAXSZ(s) (s->vlen >> (3 - s->lmul))

static bool opivv_check(DisasContext *s, arg_rmrr *a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, false) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_reg(s, a->rs2, false) &&
            vext_check_reg(s, a->rs1, false));
}

typedef void GVecGen3Fn(unsigned, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t);

static inline bool
do_opivv_gvec(DisasContext *s, arg_rmrr *a, GVecGen3Fn *gvec_fn,
              gen_helper_gvec_4_ptr *fn)
{
    TCGLabel *over = gen_new_label();
    if (!opivv_check(s, a)) {
        return false;
    }

    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    if (a->vm && s->vl_eq_vlmax) {
        gvec_fn(s->sew, vreg_ofs(s, a->rd),
                vreg_ofs(s, a->rs2), vreg_ofs(s, a->rs1),
                MAXSZ(s), MAXSZ(s));
    } else {
        uint32_t data = 0;

        data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs1), vreg_ofs(s, a->rs2),
                           cpu_env, 0, s->vlen / 8, data, fn);
    }
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
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();
    src1 = tcg_temp_new();
    gen_get_gpr(src1, rs1);

    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    desc = tcg_const_i32(simd_desc(0, s->vlen / 8, data));

    tcg_gen_addi_ptr(dest, cpu_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(src2, cpu_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, cpu_env, vreg_ofs(s, 0));

    fn(dest, mask, src1, src2, cpu_env, desc);

    tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(mask);
    tcg_temp_free_ptr(src2);
    tcg_temp_free(src1);
    tcg_temp_free_i32(desc);
    gen_set_label(over);
    return true;
}

static bool opivx_check(DisasContext *s, arg_rmrr *a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, false) &&
            vext_check_reg(s, a->rd, false) &&
            vext_check_reg(s, a->rs2, false));
}

typedef void GVecGen2sFn(unsigned, uint32_t, uint32_t, TCGv_i64,
                         uint32_t, uint32_t);

static inline bool
do_opivx_gvec(DisasContext *s, arg_rmrr *a, GVecGen2sFn *gvec_fn,
              gen_helper_opivx *fn)
{
    if (!opivx_check(s, a)) {
        return false;
    }

    if (a->vm && s->vl_eq_vlmax) {
        TCGv_i64 src1 = tcg_temp_new_i64();
        TCGv tmp = tcg_temp_new();

        gen_get_gpr(tmp, a->rs1);
        tcg_gen_ext_tl_i64(src1, tmp);
        gvec_fn(s->sew, vreg_ofs(s, a->rd), vreg_ofs(s, a->rs2),
                src1, MAXSZ(s), MAXSZ(s));

        tcg_temp_free_i64(src1);
        tcg_temp_free(tmp);
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
    tcg_gen_vec_sub8_i64(d, b, a);
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
    static const GVecGen2s rsub_op[4] = {
        { .fni8 = gen_vec_rsub8_i64,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs8,
          .vece = MO_8 },
        { .fni8 = gen_vec_rsub16_i64,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs16,
          .vece = MO_16 },
        { .fni4 = gen_rsub_i32,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs32,
          .vece = MO_32 },
        { .fni8 = gen_rsub_i64,
          .fniv = gen_rsub_vec,
          .fno = gen_helper_vec_rsubs64,
          .prefer_i64 = TCG_TARGET_REG_BITS == 64,
          .vece = MO_64 },
    };

    tcg_debug_assert(vece <= MO_64);
    tcg_gen_gvec_2s(dofs, aofs, oprsz, maxsz, c, &rsub_op[vece]);
}

GEN_OPIVX_GVEC_TRANS(vrsub_vx, rsubs)

static bool opivi_trans(uint32_t vd, uint32_t imm, uint32_t vs2, uint32_t vm,
                        gen_helper_opivx *fn, DisasContext *s, int zx)
{
    TCGv_ptr dest, src2, mask;
    TCGv src1;
    TCGv_i32 desc;
    uint32_t data = 0;

    TCGLabel *over = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

    dest = tcg_temp_new_ptr();
    mask = tcg_temp_new_ptr();
    src2 = tcg_temp_new_ptr();
    if (zx) {
        src1 = tcg_const_tl(imm);
    } else {
        src1 = tcg_const_tl(sextract64(imm, 0, 5));
    }
    data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
    data = FIELD_DP32(data, VDATA, VM, vm);
    data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
    desc = tcg_const_i32(simd_desc(0, s->vlen / 8, data));

    tcg_gen_addi_ptr(dest, cpu_env, vreg_ofs(s, vd));
    tcg_gen_addi_ptr(src2, cpu_env, vreg_ofs(s, vs2));
    tcg_gen_addi_ptr(mask, cpu_env, vreg_ofs(s, 0));

    fn(dest, mask, src1, src2, cpu_env, desc);

    tcg_temp_free_ptr(dest);
    tcg_temp_free_ptr(mask);
    tcg_temp_free_ptr(src2);
    tcg_temp_free(src1);
    tcg_temp_free_i32(desc);
    gen_set_label(over);
    return true;
}

typedef void GVecGen2iFn(unsigned, uint32_t, uint32_t, int64_t,
                         uint32_t, uint32_t);

static inline bool
do_opivi_gvec(DisasContext *s, arg_rmrr *a, GVecGen2iFn *gvec_fn,
              gen_helper_opivx *fn, int zx)
{
    if (!opivx_check(s, a)) {
        return false;
    }

    if (a->vm && s->vl_eq_vlmax) {
        if (zx) {
            gvec_fn(s->sew, vreg_ofs(s, a->rd), vreg_ofs(s, a->rs2),
                    extract64(a->rs1, 0, 5), MAXSZ(s), MAXSZ(s));
        } else {
            gvec_fn(s->sew, vreg_ofs(s, a->rd), vreg_ofs(s, a->rs2),
                    sextract64(a->rs1, 0, 5), MAXSZ(s), MAXSZ(s));
        }
    } else {
        return opivi_trans(a->rd, a->rs1, a->rs2, a->vm, fn, s, zx);
    }
    return true;
}

/* OPIVI with GVEC IR */
#define GEN_OPIVI_GVEC_TRANS(NAME, ZX, OPIVX, SUF) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)             \
{                                                                  \
    static gen_helper_opivx * const fns[4] = {                     \
        gen_helper_##OPIVX##_b, gen_helper_##OPIVX##_h,            \
        gen_helper_##OPIVX##_w, gen_helper_##OPIVX##_d,            \
    };                                                             \
    return do_opivi_gvec(s, a, tcg_gen_gvec_##SUF,                 \
                         fns[s->sew], ZX);                         \
}

GEN_OPIVI_GVEC_TRANS(vadd_vi, 0, vadd_vx, addi)

static void tcg_gen_gvec_rsubi(unsigned vece, uint32_t dofs, uint32_t aofs,
                               int64_t c, uint32_t oprsz, uint32_t maxsz)
{
    TCGv_i64 tmp = tcg_const_i64(c);
    tcg_gen_gvec_rsubs(vece, dofs, aofs, tmp, oprsz, maxsz);
    tcg_temp_free_i64(tmp);
}

GEN_OPIVI_GVEC_TRANS(vrsub_vi, 0, vrsub_vx, rsubi)

/* Vector Widening Integer Add/Subtract */

/* OPIVV with WIDEN */
static bool opivv_widen_check(DisasContext *s, arg_rmrr *a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, true) &&
            vext_check_reg(s, a->rd, true) &&
            vext_check_reg(s, a->rs2, false) &&
            vext_check_reg(s, a->rs1, false) &&
            vext_check_overlap_group(a->rd, 2 << s->lmul, a->rs2,
                                     1 << s->lmul) &&
            vext_check_overlap_group(a->rd, 2 << s->lmul, a->rs1,
                                     1 << s->lmul) &&
            (s->lmul < 0x3) && (s->sew < 0x3));
}

static bool do_opivv_widen(DisasContext *s, arg_rmrr *a,
                           gen_helper_gvec_4_ptr *fn,
                           bool (*checkfn)(DisasContext *, arg_rmrr *))
{
    if (checkfn(s, a)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

        data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs1),
                           vreg_ofs(s, a->rs2),
                           cpu_env, 0, s->vlen / 8,
                           data, fn);
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
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, true) &&
            vext_check_reg(s, a->rd, true) &&
            vext_check_reg(s, a->rs2, false) &&
            vext_check_overlap_group(a->rd, 2 << s->lmul, a->rs2,
                                     1 << s->lmul) &&
            (s->lmul < 0x3) && (s->sew < 0x3));
}

static bool do_opivx_widen(DisasContext *s, arg_rmrr *a,
                           gen_helper_opivx *fn)
{
    if (opivx_widen_check(s, a)) {
        return opivx_trans(a->rd, a->rs1, a->rs2, a->vm, fn, s);
    }
    return true;
}

#define GEN_OPIVX_WIDEN_TRANS(NAME) \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)       \
{                                                            \
    static gen_helper_opivx * const fns[3] = {               \
        gen_helper_##NAME##_b,                               \
        gen_helper_##NAME##_h,                               \
        gen_helper_##NAME##_w                                \
    };                                                       \
    return do_opivx_widen(s, a, fns[s->sew]);                \
}

GEN_OPIVX_WIDEN_TRANS(vwaddu_vx)
GEN_OPIVX_WIDEN_TRANS(vwadd_vx)
GEN_OPIVX_WIDEN_TRANS(vwsubu_vx)
GEN_OPIVX_WIDEN_TRANS(vwsub_vx)

/* WIDEN OPIVV with WIDEN */
static bool opiwv_widen_check(DisasContext *s, arg_rmrr *a)
{
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, true) &&
            vext_check_reg(s, a->rd, true) &&
            vext_check_reg(s, a->rs2, true) &&
            vext_check_reg(s, a->rs1, false) &&
            vext_check_overlap_group(a->rd, 2 << s->lmul, a->rs1,
                                     1 << s->lmul) &&
            (s->lmul < 0x3) && (s->sew < 0x3));
}

static bool do_opiwv_widen(DisasContext *s, arg_rmrr *a,
                           gen_helper_gvec_4_ptr *fn)
{
    if (opiwv_widen_check(s, a)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);

        data = FIELD_DP32(data, VDATA, MLEN, s->mlen);
        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, s->lmul);
        tcg_gen_gvec_4_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, 0),
                           vreg_ofs(s, a->rs1),
                           vreg_ofs(s, a->rs2),
                           cpu_env, 0, s->vlen / 8, data, fn);
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
    return (vext_check_isa_ill(s) &&
            vext_check_overlap_mask(s, a->rd, a->vm, true) &&
            vext_check_reg(s, a->rd, true) &&
            vext_check_reg(s, a->rs2, true) &&
            (s->lmul < 0x3) && (s->sew < 0x3));
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
