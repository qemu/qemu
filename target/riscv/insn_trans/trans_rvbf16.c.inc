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
