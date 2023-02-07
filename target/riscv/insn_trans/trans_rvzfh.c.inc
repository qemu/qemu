/*
 * RISC-V translation routines for the RV64Zfh Standard Extension.
 *
 * Copyright (c) 2020 Chih-Min Chao, chihmin.chao@sifive.com
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

#define REQUIRE_ZFH(ctx) do { \
    if (!ctx->cfg_ptr->ext_zfh) {      \
        return false;         \
    }                         \
} while (0)

#define REQUIRE_ZHINX_OR_ZFH(ctx) do { \
    if (!ctx->cfg_ptr->ext_zhinx && !ctx->cfg_ptr->ext_zfh) { \
        return false;                  \
    }                                  \
} while (0)

#define REQUIRE_ZFH_OR_ZFHMIN(ctx) do {       \
    if (!(ctx->cfg_ptr->ext_zfh || ctx->cfg_ptr->ext_zfhmin)) { \
        return false;                         \
    }                                         \
} while (0)

#define REQUIRE_ZFH_OR_ZFHMIN_OR_ZHINX_OR_ZHINXMIN(ctx) do { \
    if (!(ctx->cfg_ptr->ext_zfh || ctx->cfg_ptr->ext_zfhmin ||          \
          ctx->cfg_ptr->ext_zhinx || ctx->cfg_ptr->ext_zhinxmin)) {     \
        return false;                                        \
    }                                                        \
} while (0)

static bool trans_flh(DisasContext *ctx, arg_flh *a)
{
    TCGv_i64 dest;
    TCGv t0;

    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN(ctx);

    decode_save_opc(ctx);
    t0 = get_gpr(ctx, a->rs1, EXT_NONE);
    if (a->imm) {
        TCGv temp = temp_new(ctx);
        tcg_gen_addi_tl(temp, t0, a->imm);
        t0 = temp;
    }

    dest = cpu_fpr[a->rd];
    tcg_gen_qemu_ld_i64(dest, t0, ctx->mem_idx, MO_TEUW);
    gen_nanbox_h(dest, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsh(DisasContext *ctx, arg_fsh *a)
{
    TCGv t0;

    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN(ctx);

    decode_save_opc(ctx);
    t0 = get_gpr(ctx, a->rs1, EXT_NONE);
    if (a->imm) {
        TCGv temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, t0, a->imm);
        t0 = temp;
    }

    tcg_gen_qemu_st_i64(cpu_fpr[a->rs2], t0, ctx->mem_idx, MO_TEUW);

    return true;
}

static bool trans_fmadd_h(DisasContext *ctx, arg_fmadd_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmadd_h(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmsub_h(DisasContext *ctx, arg_fmsub_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmsub_h(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmsub_h(DisasContext *ctx, arg_fnmsub_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fnmsub_h(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmadd_h(DisasContext *ctx, arg_fnmadd_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fnmadd_h(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fadd_h(DisasContext *ctx, arg_fadd_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fadd_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsub_h(DisasContext *ctx, arg_fsub_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsub_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmul_h(DisasContext *ctx, arg_fmul_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmul_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fdiv_h(DisasContext *ctx, arg_fdiv_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fdiv_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsqrt_h(DisasContext *ctx, arg_fsqrt_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsqrt_h(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnj_h(DisasContext *ctx, arg_fsgnj_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    if (a->rs1 == a->rs2) { /* FMOV */
        if (!ctx->cfg_ptr->ext_zfinx) {
            gen_check_nanbox_h(dest, src1);
        } else {
            tcg_gen_ext16s_i64(dest, src1);
        }
    } else {
        TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

        if (!ctx->cfg_ptr->ext_zfinx) {
            TCGv_i64 rs1 = tcg_temp_new_i64();
            TCGv_i64 rs2 = tcg_temp_new_i64();
            gen_check_nanbox_h(rs1, src1);
            gen_check_nanbox_h(rs2, src2);

            /* This formulation retains the nanboxing of rs2 in normal 'Zfh'. */
            tcg_gen_deposit_i64(dest, rs2, rs1, 0, 15);

            tcg_temp_free_i64(rs1);
            tcg_temp_free_i64(rs2);
        } else {
            tcg_gen_deposit_i64(dest, src2, src1, 0, 15);
            tcg_gen_ext16s_i64(dest, dest);
        }
    }
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnjn_h(DisasContext *ctx, arg_fsgnjn_h *a)
{
    TCGv_i64 rs1, rs2, mask;

    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    rs1 = tcg_temp_new_i64();
    if (!ctx->cfg_ptr->ext_zfinx) {
        gen_check_nanbox_h(rs1, src1);
    } else {
        tcg_gen_mov_i64(rs1, src1);
    }

    if (a->rs1 == a->rs2) { /* FNEG */
        tcg_gen_xori_i64(dest, rs1, MAKE_64BIT_MASK(15, 1));
    } else {
        TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
        rs2 = tcg_temp_new_i64();

        if (!ctx->cfg_ptr->ext_zfinx) {
            gen_check_nanbox_h(rs2, src2);
        } else {
            tcg_gen_mov_i64(rs2, src2);
        }

        /*
         * Replace bit 15 in rs1 with inverse in rs2.
         * This formulation retains the nanboxing of rs1.
         */
        mask = tcg_const_i64(~MAKE_64BIT_MASK(15, 1));
        tcg_gen_not_i64(rs2, rs2);
        tcg_gen_andc_i64(rs2, rs2, mask);
        tcg_gen_and_i64(dest, mask, rs1);
        tcg_gen_or_i64(dest, dest, rs2);

        tcg_temp_free_i64(mask);
        tcg_temp_free_i64(rs2);
    }
    /* signed-extended intead of nanboxing for result if enable zfinx */
    if (ctx->cfg_ptr->ext_zfinx) {
        tcg_gen_ext16s_i64(dest, dest);
    }
    tcg_temp_free_i64(rs1);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnjx_h(DisasContext *ctx, arg_fsgnjx_h *a)
{
    TCGv_i64 rs1, rs2;

    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    rs1 = tcg_temp_new_i64();
    if (!ctx->cfg_ptr->ext_zfinx) {
        gen_check_nanbox_h(rs1, src1);
    } else {
        tcg_gen_mov_i64(rs1, src1);
    }

    if (a->rs1 == a->rs2) { /* FABS */
        tcg_gen_andi_i64(dest, rs1, ~MAKE_64BIT_MASK(15, 1));
    } else {
        TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
        rs2 = tcg_temp_new_i64();

        if (!ctx->cfg_ptr->ext_zfinx) {
            gen_check_nanbox_h(rs2, src2);
        } else {
            tcg_gen_mov_i64(rs2, src2);
        }

        /*
         * Xor bit 15 in rs1 with that in rs2.
         * This formulation retains the nanboxing of rs1.
         */
        tcg_gen_andi_i64(dest, rs2, MAKE_64BIT_MASK(15, 1));
        tcg_gen_xor_i64(dest, rs1, dest);

        tcg_temp_free_i64(rs2);
    }
    /* signed-extended intead of nanboxing for result if enable zfinx */
    if (ctx->cfg_ptr->ext_zfinx) {
        tcg_gen_ext16s_i64(dest, dest);
    }
    tcg_temp_free_i64(rs1);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmin_h(DisasContext *ctx, arg_fmin_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmin_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmax_h(DisasContext *ctx, arg_fmax_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmax_h(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_s_h(DisasContext *ctx, arg_fcvt_s_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN_OR_ZHINX_OR_ZHINXMIN(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_s_h(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);

    return true;
}

static bool trans_fcvt_d_h(DisasContext *ctx, arg_fcvt_d_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN_OR_ZHINX_OR_ZHINXMIN(ctx);
    REQUIRE_ZDINX_OR_D(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_d_h(dest, cpu_env, src1);
    gen_set_fpr_d(ctx, a->rd, dest);

    mark_fs_dirty(ctx);

    return true;
}

static bool trans_fcvt_h_s(DisasContext *ctx, arg_fcvt_h_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN_OR_ZHINX_OR_ZHINXMIN(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_h_s(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);

    return true;
}

static bool trans_fcvt_h_d(DisasContext *ctx, arg_fcvt_h_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN_OR_ZHINX_OR_ZHINXMIN(ctx);
    REQUIRE_ZDINX_OR_D(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_h_d(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);

    return true;
}

static bool trans_feq_h(DisasContext *ctx, arg_feq_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_feq_h(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_flt_h(DisasContext *ctx, arg_flt_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_flt_h(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_fle_h(DisasContext *ctx, arg_fle_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fle_h(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fclass_h(DisasContext *ctx, arg_fclass_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_helper_fclass_h(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_w_h(DisasContext *ctx, arg_fcvt_w_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_w_h(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_wu_h(DisasContext *ctx, arg_fcvt_wu_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_wu_h(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_h_w(DisasContext *ctx, arg_fcvt_h_w *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv t0 = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_h_w(dest, cpu_env, t0);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_h_wu(DisasContext *ctx, arg_fcvt_h_wu *a)
{
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv t0 = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_h_wu(dest, cpu_env, t0);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmv_x_h(DisasContext *ctx, arg_fmv_x_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);

#if defined(TARGET_RISCV64)
    /* 16 bits -> 64 bits */
    tcg_gen_ext16s_tl(dest, cpu_fpr[a->rs1]);
#else
    /* 16 bits -> 32 bits */
    tcg_gen_extrl_i64_i32(dest, cpu_fpr[a->rs1]);
    tcg_gen_ext16s_tl(dest, dest);
#endif

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fmv_h_x(DisasContext *ctx, arg_fmv_h_x *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH_OR_ZFHMIN(ctx);

    TCGv t0 = get_gpr(ctx, a->rs1, EXT_ZERO);

    tcg_gen_extu_tl_i64(cpu_fpr[a->rd], t0);
    gen_nanbox_h(cpu_fpr[a->rd], cpu_fpr[a->rd]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_l_h(DisasContext *ctx, arg_fcvt_l_h *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_l_h(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_lu_h(DisasContext *ctx, arg_fcvt_lu_h *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_lu_h(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_h_l(DisasContext *ctx, arg_fcvt_h_l *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv t0 = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_h_l(dest, cpu_env, t0);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_h_lu(DisasContext *ctx, arg_fcvt_h_lu *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZHINX_OR_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv t0 = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_h_lu(dest, cpu_env, t0);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}
