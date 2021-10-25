/*
 * RISC-V translation routines for the RV64D Standard Extension.
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

static bool trans_fld(DisasContext *ctx, arg_fld *a)
{
    TCGv addr;

    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    addr = get_gpr(ctx, a->rs1, EXT_NONE);
    if (a->imm) {
        TCGv temp = temp_new(ctx);
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }
    addr = gen_pm_adjust_address(ctx, addr);

    tcg_gen_qemu_ld_i64(cpu_fpr[a->rd], addr, ctx->mem_idx, MO_TEQ);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsd(DisasContext *ctx, arg_fsd *a)
{
    TCGv addr;

    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    addr = get_gpr(ctx, a->rs1, EXT_NONE);
    if (a->imm) {
        TCGv temp = temp_new(ctx);
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }
    addr = gen_pm_adjust_address(ctx, addr);

    tcg_gen_qemu_st_i64(cpu_fpr[a->rs2], addr, ctx->mem_idx, MO_TEQ);

    return true;
}

static bool trans_fmadd_d(DisasContext *ctx, arg_fmadd_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    gen_set_rm(ctx, a->rm);
    gen_helper_fmadd_d(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                       cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmsub_d(DisasContext *ctx, arg_fmsub_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    gen_set_rm(ctx, a->rm);
    gen_helper_fmsub_d(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                       cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmsub_d(DisasContext *ctx, arg_fnmsub_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    gen_set_rm(ctx, a->rm);
    gen_helper_fnmsub_d(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                        cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fnmadd_d(DisasContext *ctx, arg_fnmadd_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    gen_set_rm(ctx, a->rm);
    gen_helper_fnmadd_d(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1],
                        cpu_fpr[a->rs2], cpu_fpr[a->rs3]);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fadd_d(DisasContext *ctx, arg_fadd_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_set_rm(ctx, a->rm);
    gen_helper_fadd_d(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsub_d(DisasContext *ctx, arg_fsub_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsub_d(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmul_d(DisasContext *ctx, arg_fmul_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_set_rm(ctx, a->rm);
    gen_helper_fmul_d(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fdiv_d(DisasContext *ctx, arg_fdiv_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_set_rm(ctx, a->rm);
    gen_helper_fdiv_d(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsqrt_d(DisasContext *ctx, arg_fsqrt_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_set_rm(ctx, a->rm);
    gen_helper_fsqrt_d(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnj_d(DisasContext *ctx, arg_fsgnj_d *a)
{
    if (a->rs1 == a->rs2) { /* FMOV */
        tcg_gen_mov_i64(cpu_fpr[a->rd], cpu_fpr[a->rs1]);
    } else {
        tcg_gen_deposit_i64(cpu_fpr[a->rd], cpu_fpr[a->rs2],
                            cpu_fpr[a->rs1], 0, 63);
    }
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnjn_d(DisasContext *ctx, arg_fsgnjn_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    if (a->rs1 == a->rs2) { /* FNEG */
        tcg_gen_xori_i64(cpu_fpr[a->rd], cpu_fpr[a->rs1], INT64_MIN);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_not_i64(t0, cpu_fpr[a->rs2]);
        tcg_gen_deposit_i64(cpu_fpr[a->rd], t0, cpu_fpr[a->rs1], 0, 63);
        tcg_temp_free_i64(t0);
    }
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsgnjx_d(DisasContext *ctx, arg_fsgnjx_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    if (a->rs1 == a->rs2) { /* FABS */
        tcg_gen_andi_i64(cpu_fpr[a->rd], cpu_fpr[a->rs1], ~INT64_MIN);
    } else {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_andi_i64(t0, cpu_fpr[a->rs2], INT64_MIN);
        tcg_gen_xor_i64(cpu_fpr[a->rd], cpu_fpr[a->rs1], t0);
        tcg_temp_free_i64(t0);
    }
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmin_d(DisasContext *ctx, arg_fmin_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_helper_fmin_d(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmax_d(DisasContext *ctx, arg_fmax_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_helper_fmax_d(cpu_fpr[a->rd], cpu_env,
                      cpu_fpr[a->rs1], cpu_fpr[a->rs2]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_s_d(DisasContext *ctx, arg_fcvt_s_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_s_d(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_d_s(DisasContext *ctx, arg_fcvt_d_s *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_d_s(cpu_fpr[a->rd], cpu_env, cpu_fpr[a->rs1]);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_feq_d(DisasContext *ctx, arg_feq_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_helper_feq_d(dest, cpu_env, cpu_fpr[a->rs1], cpu_fpr[a->rs2]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_flt_d(DisasContext *ctx, arg_flt_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_helper_flt_d(dest, cpu_env, cpu_fpr[a->rs1], cpu_fpr[a->rs2]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fle_d(DisasContext *ctx, arg_fle_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_helper_fle_d(dest, cpu_env, cpu_fpr[a->rs1], cpu_fpr[a->rs2]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fclass_d(DisasContext *ctx, arg_fclass_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_helper_fclass_d(dest, cpu_fpr[a->rs1]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_w_d(DisasContext *ctx, arg_fcvt_w_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_w_d(dest, cpu_env, cpu_fpr[a->rs1]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_wu_d(DisasContext *ctx, arg_fcvt_wu_d *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_wu_d(dest, cpu_env, cpu_fpr[a->rs1]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_d_w(DisasContext *ctx, arg_fcvt_d_w *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv src = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_d_w(cpu_fpr[a->rd], cpu_env, src);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_d_wu(DisasContext *ctx, arg_fcvt_d_wu *a)
{
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_d_wu(cpu_fpr[a->rd], cpu_env, src);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_l_d(DisasContext *ctx, arg_fcvt_l_d *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_l_d(dest, cpu_env, cpu_fpr[a->rs1]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fcvt_lu_d(DisasContext *ctx, arg_fcvt_lu_d *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv dest = dest_gpr(ctx, a->rd);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_lu_d(dest, cpu_env, cpu_fpr[a->rs1]);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_fmv_x_d(DisasContext *ctx, arg_fmv_x_d *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

#ifdef TARGET_RISCV64
    gen_set_gpr(ctx, a->rd, cpu_fpr[a->rs1]);
    return true;
#else
    qemu_build_not_reached();
#endif
}

static bool trans_fcvt_d_l(DisasContext *ctx, arg_fcvt_d_l *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv src = get_gpr(ctx, a->rs1, EXT_SIGN);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_d_l(cpu_fpr[a->rd], cpu_env, src);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fcvt_d_lu(DisasContext *ctx, arg_fcvt_d_lu *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv src = get_gpr(ctx, a->rs1, EXT_ZERO);

    gen_set_rm(ctx, a->rm);
    gen_helper_fcvt_d_lu(cpu_fpr[a->rd], cpu_env, src);

    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fmv_d_x(DisasContext *ctx, arg_fmv_d_x *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

#ifdef TARGET_RISCV64
    tcg_gen_mov_tl(cpu_fpr[a->rd], get_gpr(ctx, a->rs1, EXT_NONE));
    mark_fs_dirty(ctx);
    return true;
#else
    qemu_build_not_reached();
#endif
}
