/*
 * RISC-V translation routines for the RVXI Base Integer Instruction Set.
 *
 * Copyright (c) 2020 Western Digital
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

#ifndef CONFIG_USER_ONLY
static bool check_access(DisasContext *ctx)
{
    if (!ctx->hlsx) {
        if (ctx->virt_enabled) {
            generate_exception(ctx, RISCV_EXCP_VIRT_INSTRUCTION_FAULT);
        } else {
            generate_exception(ctx, RISCV_EXCP_ILLEGAL_INST);
        }
        return false;
    }
    return true;
}
#endif

static bool do_hlv(DisasContext *ctx, arg_r2 *a, MemOp mop)
{
#ifdef CONFIG_USER_ONLY
    return false;
#else
    decode_save_opc(ctx);
    if (check_access(ctx)) {
        TCGv dest = dest_gpr(ctx, a->rd);
        TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
        int mem_idx = ctx->mem_idx | TB_FLAGS_PRIV_HYP_ACCESS_MASK;
        tcg_gen_qemu_ld_tl(dest, addr, mem_idx, mop);
        gen_set_gpr(ctx, a->rd, dest);
    }
    return true;
#endif
}

static bool trans_hlv_b(DisasContext *ctx, arg_hlv_b *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_SB);
}

static bool trans_hlv_h(DisasContext *ctx, arg_hlv_h *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TESW);
}

static bool trans_hlv_w(DisasContext *ctx, arg_hlv_w *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TESL);
}

static bool trans_hlv_bu(DisasContext *ctx, arg_hlv_bu *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_UB);
}

static bool trans_hlv_hu(DisasContext *ctx, arg_hlv_hu *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TEUW);
}

static bool do_hsv(DisasContext *ctx, arg_r2_s *a, MemOp mop)
{
#ifdef CONFIG_USER_ONLY
    return false;
#else
    decode_save_opc(ctx);
    if (check_access(ctx)) {
        TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
        TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);
        int mem_idx = ctx->mem_idx | TB_FLAGS_PRIV_HYP_ACCESS_MASK;
        tcg_gen_qemu_st_tl(data, addr, mem_idx, mop);
    }
    return true;
#endif
}

static bool trans_hsv_b(DisasContext *ctx, arg_hsv_b *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_SB);
}

static bool trans_hsv_h(DisasContext *ctx, arg_hsv_h *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_TESW);
}

static bool trans_hsv_w(DisasContext *ctx, arg_hsv_w *a)
{
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_TESL);
}

static bool trans_hlv_wu(DisasContext *ctx, arg_hlv_wu *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TEUL);
}

static bool trans_hlv_d(DisasContext *ctx, arg_hlv_d *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVH);
    return do_hlv(ctx, a, MO_TEUQ);
}

static bool trans_hsv_d(DisasContext *ctx, arg_hsv_d *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EXT(ctx, RVH);
    return do_hsv(ctx, a, MO_TEUQ);
}

#ifndef CONFIG_USER_ONLY
static bool do_hlvx(DisasContext *ctx, arg_r2 *a,
                    void (*func)(TCGv, TCGv_env, TCGv))
{
    decode_save_opc(ctx);
    if (check_access(ctx)) {
        TCGv dest = dest_gpr(ctx, a->rd);
        TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
        func(dest, cpu_env, addr);
        gen_set_gpr(ctx, a->rd, dest);
    }
    return true;
}
#endif

static bool trans_hlvx_hu(DisasContext *ctx, arg_hlvx_hu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    return do_hlvx(ctx, a, gen_helper_hyp_hlvx_hu);
#else
    return false;
#endif
}

static bool trans_hlvx_wu(DisasContext *ctx, arg_hlvx_wu *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    return do_hlvx(ctx, a, gen_helper_hyp_hlvx_wu);
#else
    return false;
#endif
}

static bool trans_hfence_gvma(DisasContext *ctx, arg_sfence_vma *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    decode_save_opc(ctx);
    gen_helper_hyp_gvma_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}

static bool trans_hfence_vvma(DisasContext *ctx, arg_sfence_vma *a)
{
    REQUIRE_EXT(ctx, RVH);
#ifndef CONFIG_USER_ONLY
    decode_save_opc(ctx);
    gen_helper_hyp_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}
