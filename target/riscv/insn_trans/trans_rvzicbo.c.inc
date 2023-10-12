/*
 * RISC-V translation routines for the RISC-V CBO Extension.
 *
 * Copyright (c) 2021 Philipp Tomsich, philipp.tomsich@vrull.eu
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

#define REQUIRE_ZICBOM(ctx) do {     \
    if (!ctx->cfg_ptr->ext_zicbom) { \
        return false;                \
    }                                \
} while (0)

#define REQUIRE_ZICBOZ(ctx) do {     \
    if (!ctx->cfg_ptr->ext_zicboz) { \
        return false;                \
    }                                \
} while (0)

static bool trans_cbo_clean(DisasContext *ctx, arg_cbo_clean *a)
{
    REQUIRE_ZICBOM(ctx);
    gen_helper_cbo_clean_flush(tcg_env, cpu_gpr[a->rs1]);
    return true;
}

static bool trans_cbo_flush(DisasContext *ctx, arg_cbo_flush *a)
{
    REQUIRE_ZICBOM(ctx);
    gen_helper_cbo_clean_flush(tcg_env, cpu_gpr[a->rs1]);
    return true;
}

static bool trans_cbo_inval(DisasContext *ctx, arg_cbo_inval *a)
{
    REQUIRE_ZICBOM(ctx);
    gen_helper_cbo_inval(tcg_env, cpu_gpr[a->rs1]);
    return true;
}

static bool trans_cbo_zero(DisasContext *ctx, arg_cbo_zero *a)
{
    REQUIRE_ZICBOZ(ctx);
    gen_helper_cbo_zero(tcg_env, cpu_gpr[a->rs1]);
    return true;
}
