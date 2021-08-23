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
     REQUIRE_64BIT(ctx);
     return trans_illegal(ctx, a);
}

static bool trans_lui(DisasContext *ctx, arg_lui *a)
{
    if (a->rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[a->rd], a->imm);
    }
    return true;
}

static bool trans_auipc(DisasContext *ctx, arg_auipc *a)
{
    if (a->rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[a->rd], a->imm + ctx->base.pc_next);
    }
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

    tcg_gen_addi_tl(cpu_pc, get_gpr(ctx, a->rs1, EXT_NONE), a->imm);
    tcg_gen_andi_tl(cpu_pc, cpu_pc, (target_ulong)-2);

    if (!has_ext(ctx, RVC)) {
        TCGv t0 = tcg_temp_new();

        misaligned = gen_new_label();
        tcg_gen_andi_tl(t0, cpu_pc, 0x2);
        tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0x0, misaligned);
        tcg_temp_free(t0);
    }

    if (a->rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[a->rd], ctx->pc_succ_insn);
    }

    /* No chaining with JALR. */
    lookup_and_goto_ptr(ctx);

    if (misaligned) {
        gen_set_label(misaligned);
        gen_exception_inst_addr_mis(ctx);
    }
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool gen_branch(DisasContext *ctx, arg_b *a, TCGCond cond)
{
    TCGLabel *l = gen_new_label();
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_SIGN);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_SIGN);

    tcg_gen_brcond_tl(cond, src1, src2, l);
    gen_goto_tb(ctx, 1, ctx->pc_succ_insn);

    gen_set_label(l); /* branch taken */

    if (!has_ext(ctx, RVC) && ((ctx->base.pc_next + a->imm) & 0x3)) {
        /* misaligned */
        gen_exception_inst_addr_mis(ctx);
    } else {
        gen_goto_tb(ctx, 0, ctx->base.pc_next + a->imm);
    }
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

static bool gen_load(DisasContext *ctx, arg_lb *a, MemOp memop)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);

    if (a->imm) {
        TCGv temp = temp_new(ctx);
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
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

static bool trans_lbu(DisasContext *ctx, arg_lbu *a)
{
    return gen_load(ctx, a, MO_UB);
}

static bool trans_lhu(DisasContext *ctx, arg_lhu *a)
{
    return gen_load(ctx, a, MO_TEUW);
}

static bool gen_store(DisasContext *ctx, arg_sb *a, MemOp memop)
{
    TCGv addr = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv data = get_gpr(ctx, a->rs2, EXT_NONE);

    if (a->imm) {
        TCGv temp = temp_new(ctx);
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);
    return true;
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

static bool trans_lwu(DisasContext *ctx, arg_lwu *a)
{
    REQUIRE_64BIT(ctx);
    return gen_load(ctx, a, MO_TEUL);
}

static bool trans_ld(DisasContext *ctx, arg_ld *a)
{
    REQUIRE_64BIT(ctx);
    return gen_load(ctx, a, MO_TEQ);
}

static bool trans_sd(DisasContext *ctx, arg_sd *a)
{
    REQUIRE_64BIT(ctx);
    return gen_store(ctx, a, MO_TEQ);
}

static bool trans_addi(DisasContext *ctx, arg_addi *a)
{
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl);
}

static void gen_slt(TCGv ret, TCGv s1, TCGv s2)
{
    tcg_gen_setcond_tl(TCG_COND_LT, ret, s1, s2);
}

static void gen_sltu(TCGv ret, TCGv s1, TCGv s2)
{
    tcg_gen_setcond_tl(TCG_COND_LTU, ret, s1, s2);
}

static bool trans_slti(DisasContext *ctx, arg_slti *a)
{
    return gen_arith_imm_tl(ctx, a, EXT_SIGN, gen_slt);
}

static bool trans_sltiu(DisasContext *ctx, arg_sltiu *a)
{
    return gen_arith_imm_tl(ctx, a, EXT_SIGN, gen_sltu);
}

static bool trans_xori(DisasContext *ctx, arg_xori *a)
{
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_xori_tl);
}

static bool trans_ori(DisasContext *ctx, arg_ori *a)
{
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_ori_tl);
}

static bool trans_andi(DisasContext *ctx, arg_andi *a)
{
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_andi_tl);
}

static bool trans_slli(DisasContext *ctx, arg_slli *a)
{
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl);
}

static bool trans_srli(DisasContext *ctx, arg_srli *a)
{
    return gen_shift_imm_fn(ctx, a, EXT_ZERO, tcg_gen_shri_tl);
}

static bool trans_srai(DisasContext *ctx, arg_srai *a)
{
    return gen_shift_imm_fn(ctx, a, EXT_SIGN, tcg_gen_sari_tl);
}

static bool trans_add(DisasContext *ctx, arg_add *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl);
}

static bool trans_sub(DisasContext *ctx, arg_sub *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl);
}

static bool trans_sll(DisasContext *ctx, arg_sll *a)
{
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl);
}

static bool trans_slt(DisasContext *ctx, arg_slt *a)
{
    return gen_arith(ctx, a, EXT_SIGN, gen_slt);
}

static bool trans_sltu(DisasContext *ctx, arg_sltu *a)
{
    return gen_arith(ctx, a, EXT_SIGN, gen_sltu);
}

static bool trans_xor(DisasContext *ctx, arg_xor *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_xor_tl);
}

static bool trans_srl(DisasContext *ctx, arg_srl *a)
{
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl);
}

static bool trans_sra(DisasContext *ctx, arg_sra *a)
{
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl);
}

static bool trans_or(DisasContext *ctx, arg_or *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_or_tl);
}

static bool trans_and(DisasContext *ctx, arg_and *a)
{
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_and_tl);
}

static bool trans_addiw(DisasContext *ctx, arg_addiw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_arith_imm_fn(ctx, a, EXT_NONE, tcg_gen_addi_tl);
}

static bool trans_slliw(DisasContext *ctx, arg_slliw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, tcg_gen_shli_tl);
}

static void gen_srliw(TCGv dst, TCGv src, target_long shamt)
{
    tcg_gen_extract_tl(dst, src, shamt, 32 - shamt);
}

static bool trans_srliw(DisasContext *ctx, arg_srliw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_srliw);
}

static void gen_sraiw(TCGv dst, TCGv src, target_long shamt)
{
    tcg_gen_sextract_tl(dst, src, shamt, 32 - shamt);
}

static bool trans_sraiw(DisasContext *ctx, arg_sraiw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_sraiw);
}

static bool trans_addw(DisasContext *ctx, arg_addw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_add_tl);
}

static bool trans_subw(DisasContext *ctx, arg_subw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_arith(ctx, a, EXT_NONE, tcg_gen_sub_tl);
}

static bool trans_sllw(DisasContext *ctx, arg_sllw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_NONE, tcg_gen_shl_tl);
}

static bool trans_srlw(DisasContext *ctx, arg_srlw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_ZERO, tcg_gen_shr_tl);
}

static bool trans_sraw(DisasContext *ctx, arg_sraw *a)
{
    REQUIRE_64BIT(ctx);
    ctx->w = true;
    return gen_shift(ctx, a, EXT_SIGN, tcg_gen_sar_tl);
}

static bool trans_fence(DisasContext *ctx, arg_fence *a)
{
    /* FENCE is a full memory barrier. */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    return true;
}

static bool trans_fence_i(DisasContext *ctx, arg_fence_i *a)
{
    if (!ctx->ext_ifencei) {
        return false;
    }

    /*
     * FENCE_I is a no-op in QEMU,
     * however we need to end the translation block
     */
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    exit_tb(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_csr_post(DisasContext *ctx)
{
    /* We may have changed important cpu state -- exit to main loop. */
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    exit_tb(ctx);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_csrr(DisasContext *ctx, int rd, int rc)
{
    TCGv dest = dest_gpr(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrr(dest, cpu_env, csr);
    gen_set_gpr(ctx, rd, dest);
    return do_csr_post(ctx);
}

static bool do_csrw(DisasContext *ctx, int rc, TCGv src)
{
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrw(cpu_env, csr, src);
    return do_csr_post(ctx);
}

static bool do_csrrw(DisasContext *ctx, int rd, int rc, TCGv src, TCGv mask)
{
    TCGv dest = dest_gpr(ctx, rd);
    TCGv_i32 csr = tcg_constant_i32(rc);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_csrrw(dest, cpu_env, csr, src, mask);
    gen_set_gpr(ctx, rd, dest);
    return do_csr_post(ctx);
}

static bool trans_csrrw(DisasContext *ctx, arg_csrrw *a)
{
    TCGv src = get_gpr(ctx, a->rs1, EXT_NONE);

    /*
     * If rd == 0, the insn shall not read the csr, nor cause any of the
     * side effects that might occur on a csr read.
     */
    if (a->rd == 0) {
        return do_csrw(ctx, a->csr, src);
    }

    TCGv mask = tcg_constant_tl(-1);
    return do_csrrw(ctx, a->rd, a->csr, src, mask);
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
    if (a->rs1 == 0) {
        return do_csrr(ctx, a->rd, a->csr);
    }

    TCGv ones = tcg_constant_tl(-1);
    TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
    return do_csrrw(ctx, a->rd, a->csr, ones, mask);
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
    if (a->rs1 == 0) {
        return do_csrr(ctx, a->rd, a->csr);
    }

    TCGv mask = get_gpr(ctx, a->rs1, EXT_ZERO);
    return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
}

static bool trans_csrrwi(DisasContext *ctx, arg_csrrwi *a)
{
    TCGv src = tcg_constant_tl(a->rs1);

    /*
     * If rd == 0, the insn shall not read the csr, nor cause any of the
     * side effects that might occur on a csr read.
     */
    if (a->rd == 0) {
        return do_csrw(ctx, a->csr, src);
    }

    TCGv mask = tcg_constant_tl(-1);
    return do_csrrw(ctx, a->rd, a->csr, src, mask);
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
    if (a->rs1 == 0) {
        return do_csrr(ctx, a->rd, a->csr);
    }

    TCGv ones = tcg_constant_tl(-1);
    TCGv mask = tcg_constant_tl(a->rs1);
    return do_csrrw(ctx, a->rd, a->csr, ones, mask);
}

static bool trans_csrrci(DisasContext *ctx, arg_csrrci *a)
{
    /*
     * If rs1 == 0, the insn shall not write to the csr at all, nor
     * cause any of the side effects that might occur on a csr write.
     * Note that if rs1 specifies a register other than x0, holding
     * a zero value, the instruction will still attempt to write the
     * unmodified value back to the csr and will cause side effects.
     */
    if (a->rs1 == 0) {
        return do_csrr(ctx, a->rd, a->csr);
    }

    TCGv mask = tcg_constant_tl(a->rs1);
    return do_csrrw(ctx, a->rd, a->csr, ctx->zero, mask);
}
