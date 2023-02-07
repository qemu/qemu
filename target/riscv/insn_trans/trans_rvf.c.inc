/*
 * RISC-V translation routines for the RV64F Standard Extension.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
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

#define REQUIRE_FPU do {\
    if (ctx->mstatus_fs == 0) \
        if (!ctx->cfg_ptr->ext_zfinx) \
            return false; \
} while (0)

#define REQUIRE_ZFINX_OR_F(ctx) do {\
    if (!ctx->cfg_ptr->ext_zfinx) { \
        REQUIRE_EXT(ctx, RVF); \
    } \
} while (0)

static bool trans_flw(DisasContext *ctx, arg_flw *a)
{
    TCGv_i64 dest;
    TCGv addr;

    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);

    decode_save_opc(ctx);
    addr = get_address(ctx, a->rs1, a->imm);
    dest = cpu_fpr[a->rd];
    tcg_gen_qemu_ld_i64(dest, addr, ctx->mem_idx, MO_TEUL);
    gen_nanbox_s(dest, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsw(DisasContext *ctx, arg_fsw *a)
{
    TCGv addr;

    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);

    decode_save_opc(ctx);
    addr = get_address(ctx, a->rs1, a->imm);
    tcg_gen_qemu_st_i64(cpu_fpr[a->rs2], addr, ctx->mem_idx, MO_TEUL);
    return true;
}

static bool trans_fmadd_s(DisasContext *ctx, arg_fmadd_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmadd_s(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmsub_s(DisasContext *ctx, arg_fmsub_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmsub_s(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmsub_s(DisasContext *ctx, arg_fnmsub_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fnmsub_s(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmadd_s(DisasContext *ctx, arg_fnmadd_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
    TCGv_i64 src3 = get_fpr_hs(ctx, a->rs3);

    gen_set_rm(ctx, a->rm);
    gen_helper_fnmadd_s(dest, cpu_env, src1, src2, src3);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fadd_s(DisasContext *ctx, arg_fadd_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fadd_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsub_s(DisasContext *ctx, arg_fsub_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsub_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmul_s(DisasContext *ctx, arg_fmul_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmul_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fdiv_s(DisasContext *ctx, arg_fdiv_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_set_rm(ctx, a->rm);
    gen_helper_fdiv_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsqrt_s(DisasContext *ctx, arg_fsqrt_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsqrt_s(dest, cpu_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnj_s(DisasContext *ctx, arg_fsgnj_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    if (a->rs1 == a->rs2) { /* FMOV */
        if (!ctx->cfg_ptr->ext_zfinx) {
            gen_check_nanbox_s(dest, src1);
        } else {
            tcg_gen_ext32s_i64(dest, src1);
        }
    } else { /* FSGNJ */
        TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

        if (!ctx->cfg_ptr->ext_zfinx) {
            TCGv_i64 rs1 = tcg_temp_new_i64();
            TCGv_i64 rs2 = tcg_temp_new_i64();
            gen_check_nanbox_s(rs1, src1);
            gen_check_nanbox_s(rs2, src2);

            /* This formulation retains the nanboxing of rs2 in normal 'F'. */
            tcg_gen_deposit_i64(dest, rs2, rs1, 0, 31);

            tcg_temp_free_i64(rs1);
            tcg_temp_free_i64(rs2);
        } else {
            tcg_gen_deposit_i64(dest, src2, src1, 0, 31);
            tcg_gen_ext32s_i64(dest, dest);
        }
    }
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnjn_s(DisasContext *ctx, arg_fsgnjn_s *a)
{
    TCGv_i64 rs1, rs2, mask;

    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    rs1 = tcg_temp_new_i64();
    if (!ctx->cfg_ptr->ext_zfinx) {
        gen_check_nanbox_s(rs1, src1);
    } else {
        tcg_gen_mov_i64(rs1, src1);
    }
    if (a->rs1 == a->rs2) { /* FNEG */
        tcg_gen_xori_i64(dest, rs1, MAKE_64BIT_MASK(31, 1));
    } else {
        TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
        rs2 = tcg_temp_new_i64();
        if (!ctx->cfg_ptr->ext_zfinx) {
            gen_check_nanbox_s(rs2, src2);
        } else {
            tcg_gen_mov_i64(rs2, src2);
        }

        /*
         * Replace bit 31 in rs1 with inverse in rs2.
         * This formulation retains the nanboxing of rs1.
         */
        mask = tcg_constant_i64(~MAKE_64BIT_MASK(31, 1));
        tcg_gen_nor_i64(rs2, rs2, mask);
        tcg_gen_and_i64(dest, mask, rs1);
        tcg_gen_or_i64(dest, dest, rs2);

        tcg_temp_free_i64(rs2);
    }
    /* signed-extended intead of nanboxing for result if enable zfinx */
    if (ctx->cfg_ptr->ext_zfinx) {
        tcg_gen_ext32s_i64(dest, dest);
    }
    gen_set_fpr_hs(ctx, a->rd, dest);
    tcg_temp_free_i64(rs1);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnjx_s(DisasContext *ctx, arg_fsgnjx_s *a)
{
    TCGv_i64 rs1, rs2;

    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    rs1 = tcg_temp_new_i64();

    if (!ctx->cfg_ptr->ext_zfinx) {
        gen_check_nanbox_s(rs1, src1);
    } else {
        tcg_gen_mov_i64(rs1, src1);
    }

    if (a->rs1 == a->rs2) { /* FABS */
        tcg_gen_andi_i64(dest, rs1, ~MAKE_64BIT_MASK(31, 1));
    } else {
        TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);
        rs2 = tcg_temp_new_i64();

        if (!ctx->cfg_ptr->ext_zfinx) {
            gen_check_nanbox_s(rs2, src2);
        } else {
            tcg_gen_mov_i64(rs2, src2);
        }

        /*
         * Xor bit 31 in rs1 with that in rs2.
         * This formulation retains the nanboxing of rs1.
         */
        tcg_gen_andi_i64(dest, rs2, MAKE_64BIT_MASK(31, 1));
        tcg_gen_xor_i64(dest, rs1, dest);

        tcg_temp_free_i64(rs2);
    }
    /* signed-extended intead of nanboxing for result if enable zfinx */
    if (ctx->cfg_ptr->ext_zfinx) {
        tcg_gen_ext32s_i64(dest, dest);
    }
    tcg_temp_free_i64(rs1);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmin_s(DisasContext *ctx, arg_fmin_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmin_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmax_s(DisasContext *ctx, arg_fmax_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmax_s(dest, cpu_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_w_s(DisasContext *ctx, arg_fcvt_w_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_w_s(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_wu_s(DisasContext *ctx, arg_fcvt_wu_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_wu_s(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fmv_x_w(DisasContext *ctx, arg_fmv_x_w *a)
{
    /* NOTE: This was FMV.X.S in an earlier version of the ISA spec! */
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
#if defined(TARGET_RISCV64)
    tcg_gen_ext32s_tl(dest, src1);
#else
    tcg_gen_extrl_i64_i32(dest, src1);
#endif

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_feq_s(DisasContext *ctx, arg_feq_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_feq_s(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_flt_s(DisasContext *ctx, arg_flt_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_flt_s(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fle_s(DisasContext *ctx, arg_fle_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fle_s(dest, cpu_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fclass_s(DisasContext *ctx, arg_fclass_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_helper_fclass_s(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_s_w(DisasContext *ctx, arg_fcvt_s_w *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv src = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_s_w(dest, cpu_env, src);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_s_wu(DisasContext *ctx, arg_fcvt_s_wu *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_s_wu(dest, cpu_env, src);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmv_w_x(DisasContext *ctx, arg_fmv_w_x *a)
{
    /* NOTE: This was FMV.S.X in an earlier version of the ISA spec! */
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);

    tcg_gen_extu_tl_i64(dest, src);
    gen_nanbox_s(dest, dest);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_l_s(DisasContext *ctx, arg_fcvt_l_s *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_l_s(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_lu_s(DisasContext *ctx, arg_fcvt_lu_s *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_lu_s(dest, cpu_env, src1);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_s_l(DisasContext *ctx, arg_fcvt_s_l *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv src = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_s_l(dest, cpu_env, src);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_s_lu(DisasContext *ctx, arg_fcvt_s_lu *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_ZFINX_OR_F(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_s_lu(dest, cpu_env, src);
    gen_set_fpr_hs(ctx, a->rd, dest);
    mark_fs_dirty(ctx);
    return true;
}
