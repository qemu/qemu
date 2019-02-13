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
    gen_jalr(ctx, OPC_RISC_JALR, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_beq(DisasContext *ctx, arg_beq *a)
{
    gen_branch(ctx, OPC_RISC_BEQ, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_bne(DisasContext *ctx, arg_bne *a)
{
    gen_branch(ctx, OPC_RISC_BNE, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_blt(DisasContext *ctx, arg_blt *a)
{
    gen_branch(ctx, OPC_RISC_BLT, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_bge(DisasContext *ctx, arg_bge *a)
{
    gen_branch(ctx, OPC_RISC_BGE, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_bltu(DisasContext *ctx, arg_bltu *a)
{
    gen_branch(ctx, OPC_RISC_BLTU, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_bgeu(DisasContext *ctx, arg_bgeu *a)
{

    gen_branch(ctx, OPC_RISC_BGEU, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_lb(DisasContext *ctx, arg_lb *a)
{
    gen_load(ctx, OPC_RISC_LB, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_lh(DisasContext *ctx, arg_lh *a)
{
    gen_load(ctx, OPC_RISC_LH, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_lw(DisasContext *ctx, arg_lw *a)
{
    gen_load(ctx, OPC_RISC_LW, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_lbu(DisasContext *ctx, arg_lbu *a)
{
    gen_load(ctx, OPC_RISC_LBU, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_lhu(DisasContext *ctx, arg_lhu *a)
{
    gen_load(ctx, OPC_RISC_LHU, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_sb(DisasContext *ctx, arg_sb *a)
{
    gen_store(ctx, OPC_RISC_SB, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_sh(DisasContext *ctx, arg_sh *a)
{
    gen_store(ctx, OPC_RISC_SH, a->rs1, a->rs2, a->imm);
    return true;
}

static bool trans_sw(DisasContext *ctx, arg_sw *a)
{
    gen_store(ctx, OPC_RISC_SW, a->rs1, a->rs2, a->imm);
    return true;
}

#ifdef TARGET_RISCV64
static bool trans_lwu(DisasContext *ctx, arg_lwu *a)
{
    gen_load(ctx, OPC_RISC_LWU, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_ld(DisasContext *ctx, arg_ld *a)
{
    gen_load(ctx, OPC_RISC_LD, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_sd(DisasContext *ctx, arg_sd *a)
{
    gen_store(ctx, OPC_RISC_SD, a->rs1, a->rs2, a->imm);
    return true;
}
#endif

static bool trans_addi(DisasContext *ctx, arg_addi *a)
{
    gen_arith_imm(ctx, OPC_RISC_ADDI, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_slti(DisasContext *ctx, arg_slti *a)
{
    gen_arith_imm(ctx, OPC_RISC_SLTI, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_sltiu(DisasContext *ctx, arg_sltiu *a)
{
    gen_arith_imm(ctx, OPC_RISC_SLTIU, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_xori(DisasContext *ctx, arg_xori *a)
{
    gen_arith_imm(ctx, OPC_RISC_XORI, a->rd, a->rs1, a->imm);
    return true;
}
static bool trans_ori(DisasContext *ctx, arg_ori *a)
{
    gen_arith_imm(ctx, OPC_RISC_ORI, a->rd, a->rs1, a->imm);
    return true;
}
static bool trans_andi(DisasContext *ctx, arg_andi *a)
{
    gen_arith_imm(ctx, OPC_RISC_ANDI, a->rd, a->rs1, a->imm);
    return true;
}
static bool trans_slli(DisasContext *ctx, arg_slli *a)
{
    gen_arith_imm(ctx, OPC_RISC_SLLI, a->rd, a->rs1, a->shamt);
    return true;
}

static bool trans_srli(DisasContext *ctx, arg_srli *a)
{
    gen_arith_imm(ctx, OPC_RISC_SHIFT_RIGHT_I, a->rd, a->rs1, a->shamt);
    return true;
}

static bool trans_srai(DisasContext *ctx, arg_srai *a)
{
    gen_arith_imm(ctx, OPC_RISC_SHIFT_RIGHT_I, a->rd, a->rs1, a->shamt | 0x400);
    return true;
}

static bool trans_add(DisasContext *ctx, arg_add *a)
{
    gen_arith(ctx, OPC_RISC_ADD, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_sub(DisasContext *ctx, arg_sub *a)
{
    gen_arith(ctx, OPC_RISC_SUB, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_sll(DisasContext *ctx, arg_sll *a)
{
    gen_arith(ctx, OPC_RISC_SLL, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_slt(DisasContext *ctx, arg_slt *a)
{
    gen_arith(ctx, OPC_RISC_SLT, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_sltu(DisasContext *ctx, arg_sltu *a)
{
    gen_arith(ctx, OPC_RISC_SLTU, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_xor(DisasContext *ctx, arg_xor *a)
{
    gen_arith(ctx, OPC_RISC_XOR, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_srl(DisasContext *ctx, arg_srl *a)
{
    gen_arith(ctx, OPC_RISC_SRL, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_sra(DisasContext *ctx, arg_sra *a)
{
    gen_arith(ctx, OPC_RISC_SRA, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_or(DisasContext *ctx, arg_or *a)
{
    gen_arith(ctx, OPC_RISC_OR, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_and(DisasContext *ctx, arg_and *a)
{
    gen_arith(ctx, OPC_RISC_AND, a->rd, a->rs1, a->rs2);
    return true;
}

#ifdef TARGET_RISCV64
static bool trans_addiw(DisasContext *ctx, arg_addiw *a)
{
    gen_arith_imm(ctx, OPC_RISC_ADDIW, a->rd, a->rs1, a->imm);
    return true;
}

static bool trans_slliw(DisasContext *ctx, arg_slliw *a)
{
    gen_arith_imm(ctx, OPC_RISC_SLLIW, a->rd, a->rs1, a->shamt);
    return true;
}

static bool trans_srliw(DisasContext *ctx, arg_srliw *a)
{
    gen_arith_imm(ctx, OPC_RISC_SHIFT_RIGHT_IW, a->rd, a->rs1, a->shamt);
    return true;
}

static bool trans_sraiw(DisasContext *ctx, arg_sraiw *a)
{
    gen_arith_imm(ctx, OPC_RISC_SHIFT_RIGHT_IW , a->rd, a->rs1,
                  a->shamt | 0x400);
    return true;
}

static bool trans_addw(DisasContext *ctx, arg_addw *a)
{
    gen_arith(ctx, OPC_RISC_ADDW, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_subw(DisasContext *ctx, arg_subw *a)
{
    gen_arith(ctx, OPC_RISC_SUBW, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_sllw(DisasContext *ctx, arg_sllw *a)
{
    gen_arith(ctx, OPC_RISC_SLLW, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_srlw(DisasContext *ctx, arg_srlw *a)
{
    gen_arith(ctx, OPC_RISC_SRLW, a->rd, a->rs1, a->rs2);
    return true;
}

static bool trans_sraw(DisasContext *ctx, arg_sraw *a)
{
    gen_arith(ctx, OPC_RISC_SRAW, a->rd, a->rs1, a->rs2);
    return true;
}
#endif
