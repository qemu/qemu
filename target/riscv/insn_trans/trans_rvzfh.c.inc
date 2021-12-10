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
    if (!ctx->ext_zfh) {      \
        return false;         \
    }                         \
} while (0)

static bool trans_flh(DisasContext *ctx, arg_flh *a)
{
    TCGv_i64 dest;
    TCGv t0;

    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

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
    REQUIRE_ZFH(ctx);

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
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmadd_h(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                       cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmsub_h(DisasContext *ctx, arg_fmsub_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmsub_h(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                       cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmsub_h(DisasContext *ctx, arg_fnmsub_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fnmsub_h(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                        cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmadd_h(DisasContext *ctx, arg_fnmadd_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fnmadd_h(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                        cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fadd_h(DisasContext *ctx, arg_fadd_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fadd_h(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsub_h(DisasContext *ctx, arg_fsub_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsub_h(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmul_h(DisasContext *ctx, arg_fmul_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmul_h(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fdiv_h(DisasContext *ctx, arg_fdiv_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fdiv_h(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsqrt_h(DisasContext *ctx, arg_fsqrt_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsqrt_h(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmin_h(DisasContext *ctx, arg_fmin_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_helper_fmin_h(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                      cpu_fpr[a->rs2]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmax_h(DisasContext *ctx, arg_fmax_h *a)
{
    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);

    gen_helper_fmax_h(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                      cpu_fpr[a->rs2]);
    mark_fs_dirty(ctx);
    return true;
}
