/*
 * RISC-V translation routines for the BF16 Standard Extensions.
 *
 * Copyright (c) 2020-2023 PLCT Lab
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

#define REQUIRE_ZFBFMIN(ctx) do { \
    if (!ctx->cfg_ptr->ext_zfbfmin) { \
        return false; \
    } \
} while (0)

#define REQUIRE_ZVFBFMIN(ctx) do { \
    if (!ctx->cfg_ptr->ext_zvfbfmin) { \
        return false; \
    } \
} while (0)

#define REQUIRE_ZVFBFWMA(ctx) do { \
    if (!ctx->cfg_ptr->ext_zvfbfwma) { \
        return false; \
    } \
} while (0)

static bool trans_fcvt_bf16_s(DisasContext *ctx, arg_fcvt_bf16_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFBFMIN(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_bf16_s(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_s_bf16(DisasContext *ctx, arg_fcvt_s_bf16 *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFBFMIN(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_s_bf16(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_vfncvtbf16_f_f_w(DisasContext *ctx, arg_vfncvtbf16_f_f_w *a)
{
    REQUIRE_FPU;
    REQUIRE_ZVFBFMIN(ctx);

    if (opfv_narrow_check(ctx, a) && (ctx->sew == MO_16)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();

        gen_set_rm_chkfrm(ctx, RISCV_FRM_DYN);
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, ctx->lmul);
        data = FIELD_DP32(data, VDATA, VTA, ctx->vta);
        data = FIELD_DP32(data, VDATA, VMA, ctx->vma);
        tcg_gen_gvec_3_ptr(vreg_ofs(ctx, a->rd), vreg_ofs(ctx, 0),
                           vreg_ofs(ctx, a->rs2), cpu_env,
                           ctx->cfg_ptr->vlen / 8,
                           ctx->cfg_ptr->vlen / 8, data,
                           gen_helper_vfncvtbf16_f_f_w);
        mark_vs_dirty(ctx);
        gen_set_label(over);
        return true;
    }
    return false;
}

static bool trans_vfwcvtbf16_f_f_v(DisasContext *ctx, arg_vfwcvtbf16_f_f_v *a)
{
    REQUIRE_FPU;
    REQUIRE_ZVFBFMIN(ctx);

    if (opfv_widen_check(ctx, a) && (ctx->sew == MO_16)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();

        gen_set_rm_chkfrm(ctx, RISCV_FRM_DYN);
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, ctx->lmul);
        data = FIELD_DP32(data, VDATA, VTA, ctx->vta);
        data = FIELD_DP32(data, VDATA, VMA, ctx->vma);
        tcg_gen_gvec_3_ptr(vreg_ofs(ctx, a->rd), vreg_ofs(ctx, 0),
                           vreg_ofs(ctx, a->rs2), cpu_env,
                           ctx->cfg_ptr->vlen / 8,
                           ctx->cfg_ptr->vlen / 8, data,
                           gen_helper_vfwcvtbf16_f_f_v);
        mark_vs_dirty(ctx);
        gen_set_label(over);
        return true;
    }
    return false;
}

static bool trans_vfwmaccbf16_vv(DisasContext *ctx, arg_vfwmaccbf16_vv *a)
{
    REQUIRE_FPU;
    REQUIRE_ZVFBFWMA(ctx);

    if (require_rvv(ctx) && vext_check_isa_ill(ctx) && (ctx->sew == MO_16) &&
        vext_check_dss(ctx, a->rd, a->rs1, a->rs2, a->vm)) {
        uint32_t data = 0;
        TCGLabel *over = gen_new_label();

        gen_set_rm_chkfrm(ctx, RISCV_FRM_DYN);
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_vl, 0, over);
        tcg_gen_brcond_tl(TCG_COND_GEU, cpu_vstart, cpu_vl, over);

        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, ctx->lmul);
        data = FIELD_DP32(data, VDATA, VTA, ctx->vta);
        data = FIELD_DP32(data, VDATA, VMA, ctx->vma);
        tcg_gen_gvec_4_ptr(vreg_ofs(ctx, a->rd), vreg_ofs(ctx, 0),
                           vreg_ofs(ctx, a->rs1),
                           vreg_ofs(ctx, a->rs2), cpu_env,
                           ctx->cfg_ptr->vlen / 8,
                           ctx->cfg_ptr->vlen / 8, data,
                           gen_helper_vfwmaccbf16_vv);
        mark_vs_dirty(ctx);
        gen_set_label(over);
        return true;
    }
    return false;
}

static bool trans_vfwmaccbf16_vf(DisasContext *ctx, arg_vfwmaccbf16_vf *a)
{
    REQUIRE_FPU;
    REQUIRE_ZVFBFWMA(ctx);

    if (require_rvv(ctx) && (ctx->sew == MO_16) && vext_check_isa_ill(ctx) &&
        vext_check_ds(ctx, a->rd, a->rs2, a->vm)) {
        uint32_t data = 0;

        gen_set_rm(ctx, RISCV_FRM_DYN);
        data = FIELD_DP32(data, VDATA, VM, a->vm);
        data = FIELD_DP32(data, VDATA, LMUL, ctx->lmul);
        data = FIELD_DP32(data, VDATA, VTA, ctx->vta);
        data = FIELD_DP32(data, VDATA, VMA, ctx->vma);
        return opfvf_trans(a->rd, a->rs1, a->rs2, data,
                           gen_helper_vfwmaccbf16_vf, ctx);
    }

    return false;
}
