/*
 * RISC-V translation routines for the Zc[b,mp,mt] Standard Extensions.
 *
 * Copyright (c) 2021-2022 PLCT Lab
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

#define REQUIRE_ZCB(ctx) do {   \
    if (!ctx->cfg_ptr->ext_zcb) \
        return false;           \
} while (0)

#define REQUIRE_ZCMP(ctx) do {   \
    if (!ctx->cfg_ptr->ext_zcmp) \
        return false;            \
} while (0)

#define REQUIRE_ZCMT(ctx) do {   \
    if (!ctx->cfg_ptr->ext_zcmt) \
        return false;            \
} while (0)

static bool trans_c_zext_b(DisasContext *ctx, arg_c_zext_b *a)
{
    REQUIRE_ZCB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8u_tl);
}

static bool trans_c_zext_h(DisasContext *ctx, arg_c_zext_h *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16u_tl);
}

static bool trans_c_sext_b(DisasContext *ctx, arg_c_sext_b *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext8s_tl);
}

static bool trans_c_sext_h(DisasContext *ctx, arg_c_sext_h *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext16s_tl);
}

static bool trans_c_zext_w(DisasContext *ctx, arg_c_zext_w *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZCB(ctx);
    REQUIRE_ZBA(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_ext32u_tl);
}

static bool trans_c_not(DisasContext *ctx, arg_c_not *a)
{
    REQUIRE_ZCB(ctx);
    return gen_unary(ctx, a, EXT_NONE, tcg_gen_not_tl);
}

static bool trans_c_mul(DisasContext *ctx, arg_c_mul *a)
{
    REQUIRE_ZCB(ctx);
    REQUIRE_M_OR_ZMMUL(ctx);
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_mul_tl, NULL);
}

static bool trans_c_lbu(DisasContext *ctx, arg_c_lbu *a)
{
    REQUIRE_ZCB(ctx);
    return gen_load(ctx, a, MO_UB);
}

static bool trans_c_lhu(DisasContext *ctx, arg_c_lhu *a)
{
    REQUIRE_ZCB(ctx);
    return gen_load(ctx, a, MO_UW);
}

static bool trans_c_lh(DisasContext *ctx, arg_c_lh *a)
{
    REQUIRE_ZCB(ctx);
    return gen_load(ctx, a, MO_SW);
}

static bool trans_c_sb(DisasContext *ctx, arg_c_sb *a)
{
    REQUIRE_ZCB(ctx);
    return gen_store(ctx, a, MO_UB);
}

static bool trans_c_sh(DisasContext *ctx, arg_c_sh *a)
{
    REQUIRE_ZCB(ctx);
    return gen_store(ctx, a, MO_UW);
}

#define X_S0    8
#define X_S1    9
#define X_Sn    16

static uint32_t decode_push_pop_list(DisasContext *ctx, target_ulong rlist)
{
    uint32_t reg_bitmap = 0;

    if (has_ext(ctx, RVE) && rlist > 6) {
        return 0;
    }

    switch (rlist) {
    case 15:
        reg_bitmap |=  1 << (X_Sn + 11) ;
        reg_bitmap |=  1 << (X_Sn + 10) ;
        /* FALL THROUGH */
    case 14:
        reg_bitmap |=  1 << (X_Sn + 9) ;
        /* FALL THROUGH */
    case 13:
        reg_bitmap |=  1 << (X_Sn + 8) ;
        /* FALL THROUGH */
    case 12:
        reg_bitmap |=  1 << (X_Sn + 7) ;
        /* FALL THROUGH */
    case 11:
        reg_bitmap |=  1 << (X_Sn + 6) ;
        /* FALL THROUGH */
    case 10:
        reg_bitmap |=  1 << (X_Sn + 5) ;
        /* FALL THROUGH */
    case 9:
        reg_bitmap |=  1 << (X_Sn + 4) ;
        /* FALL THROUGH */
    case 8:
        reg_bitmap |=  1 << (X_Sn + 3) ;
        /* FALL THROUGH */
    case 7:
        reg_bitmap |=  1 << (X_Sn + 2) ;
        /* FALL THROUGH */
    case 6:
        reg_bitmap |=  1 << X_S1 ;
        /* FALL THROUGH */
    case 5:
        reg_bitmap |= 1 << X_S0;
        /* FALL THROUGH */
    case 4:
        reg_bitmap |= 1 << xRA;
        break;
    default:
        break;
    }

    return reg_bitmap;
}

static bool gen_pop(DisasContext *ctx, arg_cmpp *a, bool ret, bool ret_val)
{
    REQUIRE_ZCMP(ctx);

    uint32_t reg_bitmap = decode_push_pop_list(ctx, a->urlist);
    if (reg_bitmap == 0) {
        return false;
    }

    MemOp memop = get_ol(ctx) == MXL_RV32 ? MO_UL : MO_UQ;
    int reg_size = memop_size(memop);
    target_ulong stack_adj = ROUND_UP(ctpop32(reg_bitmap) * reg_size, 16) +
                             a->spimm;
    TCGv sp = dest_gpr(ctx, xSP);
    TCGv addr = tcg_temp_new();
    int i;

    tcg_gen_addi_tl(addr, sp, stack_adj - reg_size);

    memop |= mo_endian(ctx);
    for (i = X_Sn + 11; i >= 0; i--) {
        if (reg_bitmap & (1 << i)) {
            TCGv dest = dest_gpr(ctx, i);
            tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, memop);
            gen_set_gpr(ctx, i, dest);
            tcg_gen_subi_tl(addr, addr, reg_size);
        }
    }

    tcg_gen_addi_tl(sp, sp, stack_adj);
    gen_set_gpr(ctx, xSP, sp);

    if (ret_val) {
        gen_set_gpr(ctx, xA0, ctx->zero);
    }

    if (ret) {
        TCGv ret_addr = get_gpr(ctx, xRA, EXT_SIGN);
#ifndef CONFIG_USER_ONLY
        if (ctx->cfg_ptr->ext_smctr || ctx->cfg_ptr->ext_ssctr) {
            TCGv type = tcg_constant_tl(CTRDATA_TYPE_RETURN);
            TCGv src = tcg_temp_new();
            gen_pc_plus_diff(src, ctx, 0);
            gen_helper_ctr_add_entry(tcg_env, src, ret_addr, type);
        }
#endif
        tcg_gen_mov_tl(cpu_pc, ret_addr);
        tcg_gen_lookup_and_goto_ptr();
        ctx->base.is_jmp = DISAS_NORETURN;
    }

    return true;
}

static bool trans_cm_push(DisasContext *ctx, arg_cm_push *a)
{
    REQUIRE_ZCMP(ctx);

    uint32_t reg_bitmap = decode_push_pop_list(ctx, a->urlist);
    if (reg_bitmap == 0) {
        return false;
    }

    MemOp memop = get_ol(ctx) == MXL_RV32 ? MO_UL : MO_UQ;
    int reg_size = memop_size(memop);
    target_ulong stack_adj = ROUND_UP(ctpop32(reg_bitmap) * reg_size, 16) +
                             a->spimm;
    TCGv sp = dest_gpr(ctx, xSP);
    TCGv addr = tcg_temp_new();
    int i;

    tcg_gen_subi_tl(addr, sp, reg_size);

    memop |= mo_endian(ctx);
    for (i = X_Sn + 11; i >= 0; i--) {
        if (reg_bitmap & (1 << i)) {
            TCGv val = get_gpr(ctx, i, EXT_NONE);
            tcg_gen_qemu_st_tl(val, addr, ctx->mem_idx, memop);
            tcg_gen_subi_tl(addr, addr, reg_size);
        }
    }

    tcg_gen_subi_tl(sp, sp, stack_adj);
    gen_set_gpr(ctx, xSP, sp);

    return true;
}

static bool trans_cm_pop(DisasContext *ctx, arg_cm_pop *a)
{
    return gen_pop(ctx, a, false, false);
}

static bool trans_cm_popret(DisasContext *ctx, arg_cm_popret *a)
{
    return gen_pop(ctx, a, true, false);
}

static bool trans_cm_popretz(DisasContext *ctx, arg_cm_popret *a)
{
    return gen_pop(ctx, a, true, true);
}

static bool trans_cm_mva01s(DisasContext *ctx, arg_cm_mva01s *a)
{
    REQUIRE_ZCMP(ctx);

    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    gen_set_gpr(ctx, xA0, src1);
    gen_set_gpr(ctx, xA1, src2);

    return true;
}

static bool trans_cm_mvsa01(DisasContext *ctx, arg_cm_mvsa01 *a)
{
    REQUIRE_ZCMP(ctx);

    if (a->rs1 == a->rs2) {
        return false;
    }

    TCGv a0 = get_gpr(ctx, xA0, EXT_NONE);
    TCGv a1 = get_gpr(ctx, xA1, EXT_NONE);

    gen_set_gpr(ctx, a->rs1, a0);
    gen_set_gpr(ctx, a->rs2, a1);

    return true;
}

static bool trans_cm_jalt(DisasContext *ctx, arg_cm_jalt *a)
{
    REQUIRE_ZCMT(ctx);

    TCGv addr = tcg_temp_new();

    /*
     * Update pc to current for the non-unwinding exception
     * that might come from cpu_ld*_code() in the helper.
     */
    gen_update_pc(ctx, 0);
    gen_helper_cm_jalt(addr, tcg_env, tcg_constant_i32(a->index));

    /* c.jt vs c.jalt depends on the index. */
    if (a->index >= 32) {
        TCGv succ_pc = dest_gpr(ctx, xRA);
        gen_pc_plus_diff(succ_pc, ctx, ctx->cur_insn_len);
        gen_set_gpr(ctx, xRA, succ_pc);
    }

#ifndef CONFIG_USER_ONLY
    if (ctx->cfg_ptr->ext_smctr || ctx->cfg_ptr->ext_ssctr) {
        if (a->index >= 32) {
            TCGv type = tcg_constant_tl(CTRDATA_TYPE_DIRECT_CALL);
            gen_helper_ctr_add_entry(tcg_env, cpu_pc, addr, type);
        } else {
            TCGv type = tcg_constant_tl(CTRDATA_TYPE_DIRECT_JUMP);
            gen_helper_ctr_add_entry(tcg_env, cpu_pc, addr, type);
        }
    }
#endif


    tcg_gen_mov_tl(cpu_pc, addr);

    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}
