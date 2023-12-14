/*
 * RISC-V translation routines for the Zfa Standard Extension.
 *
 * Copyright (c) 2023 Christoph Müllner, christoph.muellner@vrull.eu
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

#define REQUIRE_ZFA(ctx) do {     \
    if (!ctx->cfg_ptr->ext_zfa) { \
        return false;             \
    }                             \
} while (0)

#define REQUIRE_ZFH(ctx) do {     \
    if (!ctx->cfg_ptr->ext_zfh) { \
        return false;             \
    }                             \
} while (0)

static bool trans_fli_s(DisasContext *ctx, arg_fli_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    /* Values below are NaN-boxed to avoid a gen_nanbox_s(). */
    static const uint64_t fli_s_table[] = {
        0xffffffffbf800000,  /* -1.0 */
        0xffffffff00800000,  /* minimum positive normal */
        0xffffffff37800000,  /* 1.0 * 2^-16 */
        0xffffffff38000000,  /* 1.0 * 2^-15 */
        0xffffffff3b800000,  /* 1.0 * 2^-8  */
        0xffffffff3c000000,  /* 1.0 * 2^-7  */
        0xffffffff3d800000,  /* 1.0 * 2^-4  */
        0xffffffff3e000000,  /* 1.0 * 2^-3  */
        0xffffffff3e800000,  /* 0.25 */
        0xffffffff3ea00000,  /* 0.3125 */
        0xffffffff3ec00000,  /* 0.375 */
        0xffffffff3ee00000,  /* 0.4375 */
        0xffffffff3f000000,  /* 0.5 */
        0xffffffff3f200000,  /* 0.625 */
        0xffffffff3f400000,  /* 0.75 */
        0xffffffff3f600000,  /* 0.875 */
        0xffffffff3f800000,  /* 1.0 */
        0xffffffff3fa00000,  /* 1.25 */
        0xffffffff3fc00000,  /* 1.5 */
        0xffffffff3fe00000,  /* 1.75 */
        0xffffffff40000000,  /* 2.0 */
        0xffffffff40200000,  /* 2.5 */
        0xffffffff40400000,  /* 3 */
        0xffffffff40800000,  /* 4 */
        0xffffffff41000000,  /* 8 */
        0xffffffff41800000,  /* 16 */
        0xffffffff43000000,  /* 2^7 */
        0xffffffff43800000,  /* 2^8 */
        0xffffffff47000000,  /* 2^15 */
        0xffffffff47800000,  /* 2^16 */
        0xffffffff7f800000,  /* +inf */
        0xffffffff7fc00000,  /* Canonical NaN */
    };

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    tcg_gen_movi_i64(dest, fli_s_table[a->rs1]);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fli_d(DisasContext *ctx, arg_fli_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    static const uint64_t fli_d_table[] = {
        0xbff0000000000000,  /* -1.0 */
        0x0010000000000000,  /* minimum positive normal */
        0x3ef0000000000000,  /* 1.0 * 2^-16 */
        0x3f00000000000000,  /* 1.0 * 2^-15 */
        0x3f70000000000000,  /* 1.0 * 2^-8  */
        0x3f80000000000000,  /* 1.0 * 2^-7  */
        0x3fb0000000000000,  /* 1.0 * 2^-4  */
        0x3fc0000000000000,  /* 1.0 * 2^-3  */
        0x3fd0000000000000,  /* 0.25 */
        0x3fd4000000000000,  /* 0.3125 */
        0x3fd8000000000000,  /* 0.375 */
        0x3fdc000000000000,  /* 0.4375 */
        0x3fe0000000000000,  /* 0.5 */
        0x3fe4000000000000,  /* 0.625 */
        0x3fe8000000000000,  /* 0.75 */
        0x3fec000000000000,  /* 0.875 */
        0x3ff0000000000000,  /* 1.0 */
        0x3ff4000000000000,  /* 1.25 */
        0x3ff8000000000000,  /* 1.5 */
        0x3ffc000000000000,  /* 1.75 */
        0x4000000000000000,  /* 2.0 */
        0x4004000000000000,  /* 2.5 */
        0x4008000000000000,  /* 3 */
        0x4010000000000000,  /* 4 */
        0x4020000000000000,  /* 8 */
        0x4030000000000000,  /* 16 */
        0x4060000000000000,  /* 2^7 */
        0x4070000000000000,  /* 2^8 */
        0x40e0000000000000,  /* 2^15 */
        0x40f0000000000000,  /* 2^16 */
        0x7ff0000000000000,  /* +inf */
        0x7ff8000000000000,  /* Canonical NaN */
    };

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    tcg_gen_movi_i64(dest, fli_d_table[a->rs1]);
    gen_set_fpr_d(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fli_h(DisasContext *ctx, arg_fli_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    /* Values below are NaN-boxed to avoid a gen_nanbox_h(). */
    static const uint64_t fli_h_table[] = {
        0xffffffffffffbc00,  /* -1.0 */
        0xffffffffffff0400,  /* minimum positive normal */
        0xffffffffffff0100,  /* 1.0 * 2^-16 */
        0xffffffffffff0200,  /* 1.0 * 2^-15 */
        0xffffffffffff1c00,  /* 1.0 * 2^-8  */
        0xffffffffffff2000,  /* 1.0 * 2^-7  */
        0xffffffffffff2c00,  /* 1.0 * 2^-4  */
        0xffffffffffff3000,  /* 1.0 * 2^-3  */
        0xffffffffffff3400,  /* 0.25 */
        0xffffffffffff3500,  /* 0.3125 */
        0xffffffffffff3600,  /* 0.375 */
        0xffffffffffff3700,  /* 0.4375 */
        0xffffffffffff3800,  /* 0.5 */
        0xffffffffffff3900,  /* 0.625 */
        0xffffffffffff3a00,  /* 0.75 */
        0xffffffffffff3b00,  /* 0.875 */
        0xffffffffffff3c00,  /* 1.0 */
        0xffffffffffff3d00,  /* 1.25 */
        0xffffffffffff3e00,  /* 1.5 */
        0xffffffffffff3f00,  /* 1.75 */
        0xffffffffffff4000,  /* 2.0 */
        0xffffffffffff4100,  /* 2.5 */
        0xffffffffffff4200,  /* 3 */
        0xffffffffffff4400,  /* 4 */
        0xffffffffffff4800,  /* 8 */
        0xffffffffffff4c00,  /* 16 */
        0xffffffffffff5800,  /* 2^7 */
        0xffffffffffff5c00,  /* 2^8 */
        0xffffffffffff7800,  /* 2^15 */
        0xffffffffffff7c00,  /* 2^16 */
        0xffffffffffff7c00,  /* +inf */
        0xffffffffffff7e00,  /* Canonical NaN */
    };

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    tcg_gen_movi_i64(dest, fli_h_table[a->rs1]);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fminm_s(DisasContext *ctx, arg_fminm_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fminm_s(dest, tcg_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmaxm_s(DisasContext *ctx, arg_fmaxm_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmaxm_s(dest, tcg_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fminm_d(DisasContext *ctx, arg_fminm_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_d(ctx, a->rs2);

    gen_helper_fminm_d(dest, tcg_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmaxm_d(DisasContext *ctx, arg_fmaxm_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_d(ctx, a->rs2);

    gen_helper_fmaxm_d(dest, tcg_env, src1, src2);
    gen_set_fpr_d(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fminm_h(DisasContext *ctx, arg_fminm_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fminm_h(dest, tcg_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmaxm_h(DisasContext *ctx, arg_fmaxm_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fmaxm_h(dest, tcg_env, src1, src2);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fround_s(DisasContext *ctx, arg_fround_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fround_s(dest, tcg_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_froundnx_s(DisasContext *ctx, arg_froundnx_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_froundnx_s(dest, tcg_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fround_d(DisasContext *ctx, arg_fround_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fround_d(dest, tcg_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_froundnx_d(DisasContext *ctx, arg_froundnx_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_froundnx_d(dest, tcg_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fround_h(DisasContext *ctx, arg_fround_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_fround_h(dest, tcg_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_froundnx_h(DisasContext *ctx, arg_froundnx_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv_i64 dest = dest_fpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);

    gen_set_rm(ctx, a->rm);
    gen_helper_froundnx_h(dest, tcg_env, src1);
    gen_set_fpr_hs(ctx, a->rd, dest);

    mark_fs_dirty(ctx);
    return true;
}

bool trans_fcvtmod_w_d(DisasContext *ctx, arg_fcvtmod_w_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv dst = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_d(ctx, a->rs1);
    TCGv_i64 t1 = tcg_temp_new_i64();

    /* Rounding mode is RTZ. */
    gen_set_rm(ctx, RISCV_FRM_RTZ);
    gen_helper_fcvtmod_w_d(t1, tcg_env, src1);
    tcg_gen_trunc_i64_tl(dst, t1);
    gen_set_gpr(ctx, a->rd, dst);

    return true;
}

bool trans_fmvh_x_d(DisasContext *ctx, arg_fmvh_x_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);
    REQUIRE_32BIT(ctx);

    TCGv dst = dest_gpr(ctx, a->rd);
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_sari_i64(t1, cpu_fpr[a->rs1], 32);
    tcg_gen_trunc_i64_tl(dst, t1);
    gen_set_gpr(ctx, a->rd, dst);
    return true;
}

bool trans_fmvp_d_x(DisasContext *ctx, arg_fmvp_d_x *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);
    REQUIRE_32BIT(ctx);

    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    tcg_gen_concat_tl_i64(cpu_fpr[a->rd], src1, src2);

    mark_fs_dirty(ctx);
    return true;
}

bool trans_fleq_s(DisasContext *ctx, arg_fleq_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fleq_s(dest, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

bool trans_fltq_s(DisasContext *ctx, arg_fltq_s *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVF);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fltq_s(dest, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

bool trans_fleq_d(DisasContext *ctx, arg_fleq_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fleq_d(dest, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

bool trans_fltq_d(DisasContext *ctx, arg_fltq_d *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fltq_d(dest, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

bool trans_fleq_h(DisasContext *ctx, arg_fleq_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fleq_h(dest, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

bool trans_fltq_h(DisasContext *ctx, arg_fltq_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFA(ctx);
    REQUIRE_ZFH(ctx);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv_i64 src1 = get_fpr_hs(ctx, a->rs1);
    TCGv_i64 src2 = get_fpr_hs(ctx, a->rs2);

    gen_helper_fltq_h(dest, tcg_env, src1, src2);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}
