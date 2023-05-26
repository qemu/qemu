/*
 * RISC-V translation routines for the RVXI Base Integer Instruction Set.
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

static bool trans_illegal(DisasContext *ctx, arg_empty *a)
{
    gen_exception_illegal(ctx);
    return true;
}

static bool trans_c64_illegal(DisasContext *ctx, arg_empty *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return trans_illegal(ctx, a);
}

static bool trans_lui(DisasContext *ctx, arg_lui *a)
{
    gen_set_gpri(ctx, a->rd, a->imm);
    return true;
}

static bool trans_auipc(DisasContext *ctx, arg_auipc *a)
{
    TCGv target_pc = dest_gpr(ctx, a->rd);
    gen_pc_plus_diff(target_pc, ctx, a->imm);
    gen_set_gpr(ctx, a->rd, target_pc);
    return true;
}

static bool trans_jal(DisasContext *ctx, arg_jal *a)
{
    gen_jal(ctx, a->rd, a->imm);
    return true;
}

static bool trans_jalr(DisasContext *ctx, arg_jalr *a)
{
    TCGLabel *misaligned = NULL;
    TCGv target_pc = tcg_temp_new();
    TCGv succ_pc = dest_gpr(ctx, a->rd);

    tcg_gen_addi_tl(target_pc, get_gpr(ctx, a->rs1, EXT_NONE), a->imm);
    tcg_gen_andi_tl(target_pc, target_pc, (target_ulong)-2);

    if (get_xl(ctx) == MXL_RV32) {
        tcg_gen_ext32s_tl(target_pc, target_pc);
    }

    if (!has_ext(ctx, RVC) && !ctx->cfg_ptr->ext_zca) {
        TCGv t0 = tcg_temp_new();

        misaligned = gen_new_label();
        tcg_gen_andi_tl(t0, target_pc, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0x0, misaligned);
    }

    gen_pc_plus_diff(succ_pc, ctx, ctx->cur_insn_len);
    gen_set_gpr(ctx, a->rd, succ_pc);

    tcg_gen_mov_tl(cpu_pc, target_pc);
    lookup_and_goto_ptr(ctx);

    if (misaligned) {
        gen_set_label(misaligned);
        gen_exception_inst_addr_mis(ctx, target_pc);
    }
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static TCGCond gen_compare_i128(bool bz, TCGv rl,
                                TCGv al, TCGv ah, TCGv bl, TCGv bh,
                                TCGCond cond)
{
    TCGv rh = tcg_temp_new();
    bool invert = false;

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        if (bz) {
            tcg_gen_or_tl(rl, al, ah);
        } else {
            tcg_gen_xor_tl(rl, al, bl);
            tcg_gen_xor_tl(rh, ah, bh);
            tcg_gen_or_tl(rl, rl, rh);
        }
        break;

    case TCG_COND_GE:
    case TCG_COND_LT:
        if (bz) {
            tcg_gen_mov_tl(rl, ah);
        } else {
            TCGv tmp = tcg_temp_new();

            tcg_gen_sub2_tl(rl, rh, al, ah, bl, bh);
            tcg_gen_xor_tl(rl, rh, ah);
            tcg_gen_xor_tl(tmp, ah, bh);
            tcg_gen_and_tl(rl, rl, tmp);
            tcg_gen_xor_tl(rl, rh, rl);
        }
        break;

    case TCG_COND_LTU:
        invert = true;
        /* fallthrough */
    case TCG_COND_GEU:
        {
            TCGv tmp = tcg_temp_new();
            TCGv zero = tcg_constant_tl(0);
            TCGv one = tcg_constant_tl(1);

            cond = TCG_COND_NE;
            /* borrow in to second word */
            tcg_gen_setcond_tl(TCG_COND_LTU, tmp, al, bl);
            /* seed third word with 1, which will be result */
            tcg_gen_sub2_tl(tmp, rh, ah, one, tmp, zero);
            tcg_gen_sub2_tl(tmp, rl, tmp, rh, bh, zero);
        }
        break;

    default:
        g_assert_not_reached();
    }

    if (invert) {
        cond = tcg_invert_cond(cond);
    }
    return cond;
}

static void gen_setcond_i128(TCGv rl, TCGv rh,
                             TCGv src1l, TCGv src1h,
                             TCGv src2l, TCGv src2h,
                             TCGCond cond)
{
    cond = gen_compare_i128(false, rl, src1l, src1h, src2l, src2h, cond);
    tcg_gen_setcondi_tl(cond, rl, rl, 0);
    tcg_gen_movi_tl(rh, 0);
}

static bool gen_branch(DisasContext *ctx, arg_b *a, TCGCond cond)
{
    TCGLabel *l = gen_new_label();
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_SIGN);
    target_ulong orig_pc_save = ctx->pc_save;

    if (get_xl(ctx) == MXL_RV128) {
        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv src2h = get_gprh(ctx, a->rs2);
        TCGv tmp = tcg_temp_new();

        cond = gen_compare_i128(a->rs2 == 0,
                                tmp, src1, src1h, src2, src2h, cond);
        tcg_gen_brcondi_tl(cond, tmp, 0, l);
    } else {
        tcg_gen_brcond_tl(cond, src1, src2, l);
    }
    gen_goto_tb(ctx, 1, ctx->cur_insn_len);
    ctx->pc_save = orig_pc_save;

    gen_set_label(l); /* branch taken */

    if (!has_ext(ctx, RVC) && !ctx->cfg_ptr->ext_zca &&
        (a->imm & 0x3)) {
        /* misaligned */
        TCGv target_pc = tcg_temp_new();
        gen_pc_plus_diff(target_pc, ctx, a->imm);
        gen_exception_inst_addr_mis(ctx, target_pc);
    } else {
        gen_goto_tb(ctx, 0, a->imm);
    }
    ctx->pc_save = -1;
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_beq(DisasContext *ctx, arg_beq *a)
{
    return gen_branch(ctx, a, TCG_COND_EQ);
}

static bool trans_bne(DisasContext *ctx, arg_bne *a)
{
    return gen_branch(ctx, a, TCG_COND_NE);
}

static bool trans_blt(DisasContext *ctx, arg_blt *a)
{
    return gen_branch(ctx, a, TCG_COND_LT);
}

static bool trans_bge(DisasContext *ctx, arg_bge *a)
{
    return gen_branch(ctx, a, TCG_COND_GE);
}

static bool trans_bltu(DisasContext *ctx, arg_bltu *a)
{
    return gen_branch(ctx, a, TCG_COND_LTU);
}

static bool trans_bgeu(DisasContext *ctx, arg_bgeu *a)
{
    return gen_branch(ctx, a, TCG_COND_GEU);
}

static bool gen_load_tl(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv addr = get_address(ctx, a->rs1, a->imm);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

/* Compute only 64-bit addresses to use the address translation mechanism */
static bool gen_load_i128(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    TCGv src1l = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv destl = dest_gpr(ctx, a->rd);
    TCGv desth = dest_gprh(ctx, a->rd);
    TCGv addrl = tcg_temp_new();

    tcg_gen_addi_tl(addrl, src1l, a->imm);

    if ((memop & MO_SIZE) <= MO_64) {
        tcg_gen_qemu_ld_tl(destl, addrl, ctx->mem_idx, memop);
        if (memop & MO_SIGN) {
            tcg_gen_sari_tl(desth, destl, 63);
        } else {
            tcg_gen_movi_tl(desth, 0);
        }
    } else {
        /* assume little-endian memory access for now */
        tcg_gen_qemu_ld_tl(destl, addrl, ctx->mem_idx, MO_TEUQ);
        tcg_gen_addi_tl(addrl, addrl, 8);
        tcg_gen_qemu_ld_tl(desth, addrl, ctx->mem_idx, MO_TEUQ);
    }

    gen_set_gpr128(ctx, a->rd, destl, desth);
    return true;
}

static bool gen_load(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    decode_save_opc(ctx);
    if (get_xl(ctx) == MXL_RV128) {
        return gen_load_i128(ctx, a, memop);
    } else {
        return gen_load_tl(ctx, a, memop);
    }
}

static bool trans_lb(DisasContext *ctx, arg_lb *a)
{
    return gen_load(ctx, a, MO_SB);
}

static bool trans_lh(DisasContext *ctx, arg_lh *a)
{
    return gen_load(ctx, a, MO_TESW);
}

static bool trans_lw(DisasContext *ctx, arg_lw *a)
{
    return gen_load(ctx, a, MO_TESL);
}

static bool trans_ld(DisasContext *ctx, arg_ld *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return gen_load(ctx, a, MO_TESQ);
}

static bool trans_lq(DisasContext *ctx, arg_lq *a)
{
    REQUIRE_128BIT(ctx);
    return gen_load(ctx, a, MO_TEUO);
}

static bool trans_lbu(DisasContext *ctx, arg_lbu *a)
{
    return gen_load(ctx, a, MO_UB);
}

static bool trans_lhu(DisasContext *ctx, arg_lhu *a)
{
    return gen_load(ctx, a, MO_TEUW);
}

static bool trans_lwu(DisasContext *ctx, arg_lwu *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return gen_load(ctx, a, MO_TEUL);
}

static bool trans_ldu(DisasContext *ctx, arg_ldu *a)
{
    REQUIRE_128BIT(ctx);
    return gen_load(ctx, a, MO_TEUQ);
}

static bool gen_store_tl(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    TCGv addr = get_address(ctx, a->rs1, a->imm);
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);
    return true;
}

static bool gen_store_i128(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    TCGv src1l = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2l = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv src2h = get_gprh(ctx, a->rs2);
    TCGv addrl = tcg_temp_new();

    tcg_gen_addi_tl(addrl, src1l, a->imm);

    if ((memop & MO_SIZE) <= MO_64) {
        tcg_gen_qemu_st_tl(src2l, addrl, ctx->mem_idx, memop);
    } else {
        /* little-endian memory access assumed for now */
        tcg_gen_qemu_st_tl(src2l, addrl, ctx->mem_idx, MO_TEUQ);
        tcg_gen_addi_tl(addrl, addrl, 8);
        tcg_gen_qemu_st_tl(src2h, addrl, ctx->mem_idx, MO_TEUQ);
    }
    return true;
}

static bool gen_store(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    decode_save_opc(ctx);
    if (get_xl(ctx) == MXL_RV128) {
        return gen_store_i128(ctx, a, memop);
    } else {
        return gen_store_tl(ctx, a, memop);
    }
}

static bool trans_sb(DisasContext *ctx, arg_sb *a)
{
    return gen_store(ctx, a, MO_SB);
}

static bool trans_sh(DisasContext *ctx, arg_sh *a)
{
    return gen_store(ctx, a, MO_TESW);
}

static bool trans_sw(DisasContext *ctx, arg_sw *a)
{
    return gen_store(ctx, a, MO_TESL);
}

static bool trans_sd(DisasContext *ctx, arg_sd *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    return gen_store(ctx, a, MO_TEUQ);
}

static bool trans_sq(DisasContext *ctx, arg_sq *a)
{
    REQUIRE_128BIT(ctx);
    return gen_store(ctx, a, MO_TEUO);
}

static bool trans_addd(DisasContext *ctx, arg_addd *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl, NULL);
}

static bool trans_addid(DisasContext *ctx, arg_addid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl, NULL);
}

static bool trans_subd(DisasContext *ctx, arg_subd *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl, NULL);
}

static void gen_addi2_i128(TCGv retl, TCGv reth,
                           TCGv srcl, TCGv srch, target_long imm)
{
    TCGv imml  = tcg_constant_tl(imm);
    TCGv immh  = tcg_constant_tl(-(imm < 0));
    tcg_gen_add2_tl(retl, reth, srcl, srch, imml, immh);
}

static bool trans_addi(DisasContext *ctx, arg_addi *a)
{
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl, gen_addi2_i128);
}

static void gen_slt(TCGv ret, TCGv s1, TCGv s2)
{
    tcg_gen_setcond_tl(TCG_COND_LT, ret, s1, s2);
}

static void gen_slt_i128(TCGv retl, TCGv reth,
                         TCGv s1l, TCGv s1h, TCGv s2l, TCGv s2h)
{
    gen_setcond_i128(retl, reth, s1l, s1h, s2l, s2h, TCG_COND_LT);
}

static void gen_sltu(TCGv ret, TCGv s1, TCGv s2)
{
    tcg_gen_setcond_tl(TCG_COND_LTU, ret, s1, s2);
}

static void gen_sltu_i128(TCGv retl, TCGv reth,
                          TCGv s1l, TCGv s1h, TCGv s2l, TCGv s2h)
{
    gen_setcond_i128(retl, reth, s1l, s1h, s2l, s2h, TCG_COND_LTU);
}

static bool trans_slti(DisasContext *ctx, arg_slti *a)
{
    return gen_arith_imm_tl(ctx, a, EXT_SIGN, gen_slt, gen_slt_i128);
}

static bool trans_sltiu(DisasContext *ctx, arg_sltiu *a)
{
    return gen_arith_imm_tl(ctx, a, EXT_SIGN, gen_sltu, gen_sltu_i128);
}

static bool trans_xori(DisasContext *ctx, arg_xori *a)
{
    return gen_logic_imm_fn(ctx, a, tcg_gen_xori_tl);
}

static bool trans_ori(DisasContext *ctx, arg_ori *a)
{
    return gen_logic_imm_fn(ctx, a, tcg_gen_ori_tl);
}

static bool trans_andi(DisasContext *ctx, arg_andi *a)
{
    return gen_logic_imm_fn(ctx, a, tcg_gen_andi_tl);
}

static void gen_slli_i128(TCGv retl, TCGv reth,
                          TCGv src1l, TCGv src1h,
                          target_long shamt)
{
    if (shamt >= 64) {
        tcg_gen_shli_tl(reth, src1l, shamt - 64);
        tcg_gen_movi_tl(retl, 0);
    } else {
        tcg_gen_extract2_tl(reth, src1l, src1h, 64 - shamt);
        tcg_gen_shli_tl(retl, src1l, shamt);
    }
}

static bool trans_slli(DisasContext *ctx, arg_slli *a)
{
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl, gen_slli_i128);
}

static void gen_srliw(TCGv dst, TCGv src, target_long shamt)
{
    tcg_gen_extract_tl(dst, src, shamt, 32 - shamt);
}

static void gen_srli_i128(TCGv retl, TCGv reth,
                          TCGv src1l, TCGv src1h,
                          target_long shamt)
{
    if (shamt >= 64) {
        tcg_gen_shri_tl(retl, src1h, shamt - 64);
        tcg_gen_movi_tl(reth, 0);
    } else {
        tcg_gen_extract2_tl(retl, src1l, src1h, shamt);
        tcg_gen_shri_tl(reth, src1h, shamt);
    }
}

static bool trans_srli(DisasContext *ctx, arg_srli *a)
{
    return gen_shift_imm_fn_per_ol(ctx, a, EXT_NONE,
                                   tcg_gen_shri_tl, gen_srliw, gen_srli_i128);
}

static void gen_sraiw(TCGv dst, TCGv src, target_long shamt)
{
    tcg_gen_sextract_tl(dst, src, shamt, 32 - shamt);
}

static void gen_srai_i128(TCGv retl, TCGv reth,
                          TCGv src1l, TCGv src1h,
                          target_long shamt)
{
    if (shamt >= 64) {
        tcg_gen_sari_tl(retl, src1h, shamt - 64);
        tcg_gen_sari_tl(reth, src1h, 63);
    } else {
        tcg_gen_extract2_tl(retl, src1l, src1h, shamt);
        tcg_gen_sari_tl(reth, src1h, shamt);
    }
}

static bool trans_srai(DisasContext *ctx, arg_srai *a)
{
    return gen_shift_imm_fn_per_ol(ctx, a, EXT_NONE,
                                   tcg_gen_sari_tl, gen_sraiw, gen_srai_i128);
}

static bool trans_add(DisasContext *ctx, arg_add *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl, tcg_gen_add2_tl);
}

static bool trans_sub(DisasContext *ctx, arg_sub *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl, tcg_gen_sub2_tl);
}

static void gen_sll_i128(TCGv destl, TCGv desth,
                         TCGv src1l, TCGv src1h, TCGv shamt)
{
    TCGv ls = tcg_temp_new();
    TCGv rs = tcg_temp_new();
    TCGv hs = tcg_temp_new();
    TCGv ll = tcg_temp_new();
    TCGv lr = tcg_temp_new();
    TCGv h0 = tcg_temp_new();
    TCGv h1 = tcg_temp_new();
    TCGv zero = tcg_constant_tl(0);

    tcg_gen_andi_tl(hs, shamt, 64);
    tcg_gen_andi_tl(ls, shamt, 63);
    tcg_gen_neg_tl(shamt, shamt);
    tcg_gen_andi_tl(rs, shamt, 63);

    tcg_gen_shl_tl(ll, src1l, ls);
    tcg_gen_shl_tl(h0, src1h, ls);
    tcg_gen_shr_tl(lr, src1l, rs);
    tcg_gen_movcond_tl(TCG_COND_NE, lr, shamt, zero, lr, zero);
    tcg_gen_or_tl(h1, h0, lr);

    tcg_gen_movcond_tl(TCG_COND_NE, destl, hs, zero, zero, ll);
    tcg_gen_movcond_tl(TCG_COND_NE, desth, hs, zero, ll, h1);
}

static bool trans_sll(DisasContext *ctx, arg_sll *a)
{
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl, gen_sll_i128);
}

static bool trans_slt(DisasContext *ctx, arg_slt *a)
{
    return gen_arith(ctx, a, EXT_SIGN, gen_slt, gen_slt_i128);
}

static bool trans_sltu(DisasContext *ctx, arg_sltu *a)
{
    return gen_arith(ctx, a, EXT_SIGN, gen_sltu, gen_sltu_i128);
}

static void gen_srl_i128(TCGv destl, TCGv desth,
                         TCGv src1l, TCGv src1h, TCGv shamt)
{
    TCGv ls = tcg_temp_new();
    TCGv rs = tcg_temp_new();
    TCGv hs = tcg_temp_new();
    TCGv ll = tcg_temp_new();
    TCGv lr = tcg_temp_new();
    TCGv h0 = tcg_temp_new();
    TCGv h1 = tcg_temp_new();
    TCGv zero = tcg_constant_tl(0);

    tcg_gen_andi_tl(hs, shamt, 64);
    tcg_gen_andi_tl(rs, shamt, 63);
    tcg_gen_neg_tl(shamt, shamt);
    tcg_gen_andi_tl(ls, shamt, 63);

    tcg_gen_shr_tl(lr, src1l, rs);
    tcg_gen_shr_tl(h1, src1h, rs);
    tcg_gen_shl_tl(ll, src1h, ls);
    tcg_gen_movcond_tl(TCG_COND_NE, ll, shamt, zero, ll, zero);
    tcg_gen_or_tl(h0, ll, lr);

    tcg_gen_movcond_tl(TCG_COND_NE, destl, hs, zero, h1, h0);
    tcg_gen_movcond_tl(TCG_COND_NE, desth, hs, zero, zero, h1);
}

static bool trans_srl(DisasContext *ctx, arg_srl *a)
{
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl, gen_srl_i128);
}

static void gen_sra_i128(TCGv destl, TCGv desth,
                         TCGv src1l, TCGv src1h, TCGv shamt)
{
    TCGv ls = tcg_temp_new();
    TCGv rs = tcg_temp_new();
    TCGv hs = tcg_temp_new();
    TCGv ll = tcg_temp_new();
    TCGv lr = tcg_temp_new();
    TCGv h0 = tcg_temp_new();
    TCGv h1 = tcg_temp_new();
    TCGv zero = tcg_constant_tl(0);

    tcg_gen_andi_tl(hs, shamt, 64);
    tcg_gen_andi_tl(rs, shamt, 63);
    tcg_gen_neg_tl(shamt, shamt);
    tcg_gen_andi_tl(ls, shamt, 63);

    tcg_gen_shr_tl(lr, src1l, rs);
    tcg_gen_sar_tl(h1, src1h, rs);
    tcg_gen_shl_tl(ll, src1h, ls);
    tcg_gen_movcond_tl(TCG_COND_NE, ll, shamt, zero, ll, zero);
    tcg_gen_or_tl(h0, ll, lr);
    tcg_gen_sari_tl(lr, src1h, 63);

    tcg_gen_movcond_tl(TCG_COND_NE, destl, hs, zero, h1, h0);
    tcg_gen_movcond_tl(TCG_COND_NE, desth, hs, zero, lr, h1);
}

static bool trans_sra(DisasContext *ctx, arg_sra *a)
{
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl, gen_sra_i128);
}

static bool trans_xor(DisasContext *ctx, arg_xor *a)
{
    return gen_logic(ctx, a, tcg_gen_xor_tl);
}

static bool trans_or(DisasContext *ctx, arg_or *a)
{
    return gen_logic(ctx, a, tcg_gen_or_tl);
}

static bool trans_and(DisasContext *ctx, arg_and *a)
{
    return gen_logic(ctx, a, tcg_gen_and_tl);
}

static bool trans_addiw(DisasContext *ctx, arg_addiw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl, NULL);
}

static bool trans_slliw(DisasContext *ctx, arg_slliw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl, NULL);
}

static bool trans_srliw(DisasContext *ctx, arg_srliw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_srliw, NULL);
}

static bool trans_sraiw(DisasContext *ctx, arg_sraiw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_sraiw, NULL);
}

static bool trans_sllid(DisasContext *ctx, arg_sllid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl, NULL);
}

static bool trans_srlid(DisasContext *ctx, arg_srlid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shri_tl, NULL);
}

static bool trans_sraid(DisasContext *ctx, arg_sraid *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_sari_tl,  NULL);
}

static bool trans_addw(DisasContext *ctx, arg_addw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl, NULL);
}

static bool trans_subw(DisasContext *ctx, arg_subw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl, NULL);
}

static bool trans_sllw(DisasContext *ctx, arg_sllw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl, NULL);
}

static bool trans_srlw(DisasContext *ctx, arg_srlw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl, NULL);
}

static bool trans_sraw(DisasContext *ctx, arg_sraw *a)
{
    REQUIRE_64_OR_128BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl, NULL);
}

static bool trans_slld(DisasContext *ctx, arg_slld *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl, NULL);
}

static bool trans_srld(DisasContext *ctx, arg_srld *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl, NULL);
}

static bool trans_srad(DisasContext *ctx, arg_srad *a)
{
    REQUIRE_128BIT(ctx);
    ctx->ol = MXL_RV64;
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl, NULL);
}

static bool trans_pause(DisasContext *ctx, arg_pause *a)
{
    if (!ctx->cfg_ptr->ext_zihintpause) {
        return false;
    }

    /*
     * PAUSE is a no-op in QEMU,
     * end the TB and return to main loop
     */
    gen_update_pc(ctx, ctx->cur_insn_len);
    exit_tb(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_fence(DisasContext *ctx, arg_fence *a)
{
    /* FENCE is a full memory barrier. */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    return true;
}

static bool trans_fence_i(DisasContext *ctx, arg_fence_i *a)
{
    if (!ctx->cfg_ptr->ext_ifencei) {
        return false;
    }

    /*
     * FENCE_I is a no-op in QEMU,
     * however we need to end the translation block
     */
    gen_update_pc(ctx, ctx->cur_insn_len);
    exit_tb(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_csr_post(DisasContext *ctx)
{
    /* The helper may raise ILLEGAL_INSN -- record binv for unwind. */
    decode_save_opc(ctx);
    /* We may have changed important cpu state -- exit to main loop. */
    gen_update_pc(ctx, ctx->cur_insn_len);
    exit_tb(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_csrr(DisasContext *ctx, int rd, int rc)
{
    TCGv dest = dest_gpr(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    translator_io_start(&ctx->base);
    gen_helper_csrr(dest, cpu_env, csr);
    gen_set_gpr(ctx, rd, dest);
    return do_csr_post(ctx);
}

static bool do_csrw(DisasContext *ctx, int rc, TCGv src)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    translator_io_start(&ctx->base);
    gen_helper_csrw(cpu_env, csr, src);
    return do_csr_post(ctx);
}

static bool do_csrrw(DisasContext *ctx, int rd, int rc, TCGv src, TCGv mask)
{
    TCGv dest = dest_gpr(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    translator_io_start(&ctx->base);
    gen_helper_csrrw(dest, cpu_env, csr, src, mask);
    gen_set_gpr(ctx, rd, dest);
    return do_csr_post(ctx);
}

static bool do_csrr_i128(DisasContext *ctx, int rd, int rc)
{
    TCGv destl = dest_gpr(ctx, rd);
    TCGv desth = dest_gprh(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    translator_io_start(&ctx->base);
    gen_helper_csrr_i128(destl, cpu_env, csr);
    tcg_gen_ld_tl(desth, cpu_env, offsetof(CPURISCVState, retxh));
    gen_set_gpr128(ctx, rd, destl, desth);
    return do_csr_post(ctx);
}

static bool do_csrw_i128(DisasContext *ctx, int rc, TCGv srcl, TCGv srch)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    translator_io_start(&ctx->base);
    gen_helper_csrw_i128(cpu_env, csr, srcl, srch);
    return do_csr_post(ctx);
}

static bool do_csrrw_i128(DisasContext *ctx, int rd, int rc,
                          TCGv srcl, TCGv srch, TCGv maskl, TCGv maskh)
{
    TCGv destl = dest_gpr(ctx, rd);
    TCGv desth = dest_gprh(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    translator_io_start(&ctx->base);
    gen_helper_csrrw_i128(destl, cpu_env, csr, srcl, srch, maskl, maskh);
    tcg_gen_ld_tl(desth, cpu_env, offsetof(CPURISCVState, retxh));
    gen_set_gpr128(ctx, rd, destl, desth);
    return do_csr_post(ctx);
}

static bool trans_csrrw(DisasContext *ctx, arg_csrrw *a)
{
    RISCVMXL xl = get_xl(ctx);
    if (xl < MXL_RV128) {
        TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw(ctx, a->csr, src);
        }

        TCGv mask = tcg_constant_tl(xl == MXL_RV32 ? UINT32_MAX :
                                                     (target_ulong)-1);
        return do_csrrw(ctx, a->rd, a->csr, src, mask);
    } else {
        TCGv srcl = get_gpr(ctx, a->rs1, EXT_NONE);
        TCGv srch = get_gprh(ctx, a->rs1);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw_i128(ctx, a->csr, srcl, srch);
        }

        TCGv mask = tcg_constant_tl(-1);
        return do_csrrw_i128(ctx, a->rd, a->csr, srcl, srch, mask, mask);
    }
}

static bool trans_csrrs(DisasContext *ctx, arg_csrrs *a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
        return do_csrrw(ctx, a->rd, a->csr, ones, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv maskl = get_gpr(ctx, a->rs1, EXT_ZERO);
        TCGv maskh = get_gprh(ctx, a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr, ones, ones, maskl, maskh);
    }
}

static bool trans_csrrc(DisasContext *ctx, arg_csrrc *a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
        return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv maskl = get_gpr(ctx, a->rs1, EXT_ZERO);
        TCGv maskh = get_gprh(ctx, a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr,
                             ctx->zero, ctx->zero, maskl, maskh);
    }
}

static bool trans_csrrwi(DisasContext *ctx, arg_csrrwi *a)
{
    RISCVMXL xl = get_xl(ctx);
    if (xl < MXL_RV128) {
        TCGv src = tcg_constant_tl(a->rs1);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw(ctx, a->csr, src);
        }

        TCGv mask = tcg_constant_tl(xl == MXL_RV32 ? UINT32_MAX :
                                                     (target_ulong)-1);
        return do_csrrw(ctx, a->rd, a->csr, src, mask);
    } else {
        TCGv src = tcg_constant_tl(a->rs1);

        /*
         * If rd == 0, the insn shall not read the csr, nor cause any of the
         * side effects that might occur on a csr read.
         */
        if (a->rd == 0) {
            return do_csrw_i128(ctx, a->csr, src, ctx->zero);
        }

        TCGv mask = tcg_constant_tl(-1);
        return do_csrrw_i128(ctx, a->rd, a->csr, src, ctx->zero, mask, mask);
    }
}

static bool trans_csrrsi(DisasContext *ctx, arg_csrrsi *a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw(ctx, a->rd, a->csr, ones, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv ones = tcg_constant_tl(-1);
        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr, ones, ones, mask, ctx->zero);
    }
}

static bool trans_csrrci(DisasContext *ctx, arg_csrrci * a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (get_xl(ctx) < MXL_RV128) {
        if (a->rs1 == 0) {
            return do_csrr(ctx, a->rd, a->csr);
        }

        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
    } else {
        if (a->rs1 == 0) {
            return do_csrr_i128(ctx, a->rd, a->csr);
        }

        TCGv mask = tcg_constant_tl(a->rs1);
        return do_csrrw_i128(ctx, a->rd, a->csr,
                             ctx->zero, ctx->zero, mask, ctx->zero);
    }
}
