/*
 * RISC-V translation routines for the Svinval Standard Instruction Set.
 *
 * Copyright (c) 2020-2022 PLCT lab
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

#define REQUIRE_SVINVAL(ctx) do {          \
    if (!ctx->cfg_ptr->ext_svinval) {      \
        return false;                      \
    }                                      \
} while (0)

static bool trans_sinval_vma(DisasContext *ctx, arg_sinval_vma *a)
{
    REQUIRE_SVINVAL(ctx);
    /* Do the same as sfence.vma currently */
    REQUIRE_EXT(ctx, RVS);
#ifndef CONFIG_USER_ONLY
    decode_save_opc(ctx);
    gen_helper_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}

static bool trans_sfence_w_inval(DisasContext *ctx, arg_sfence_w_inval *a)
{
    REQUIRE_SVINVAL(ctx);
    REQUIRE_EXT(ctx, RVS);
    /* Do nothing currently */
    return true;
}

static bool trans_sfence_inval_ir(DisasContext *ctx, arg_sfence_inval_ir *a)
{
    REQUIRE_SVINVAL(ctx);
    REQUIRE_EXT(ctx, RVS);
    /* Do nothing currently */
    return true;
}

static bool trans_hinval_vvma(DisasContext *ctx, arg_hinval_vvma *a)
{
    REQUIRE_SVINVAL(ctx);
    /* Do the same as hfence.vvma currently */
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    decode_save_opc(ctx);
    gen_helper_hyp_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}

static bool trans_hinval_gvma(DisasContext *ctx, arg_hinval_gvma *a)
{
    REQUIRE_SVINVAL(ctx);
    /* Do the same as hfence.gvma currently */
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    decode_save_opc(ctx);
    gen_helper_hyp_gvma_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}
