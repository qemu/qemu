/*
 * RISC-V translation routines for the RVC Compressed Instruction Set.
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

static bool trans_c_addi4spn(DisasContext *ctx, arg_c_addi4spn *a)
{
    if (a->nzuimm == 0) {
        /* Reserved in ISA */
        return false;
    }
    arg_addi arg = { .rd = a->rd, .rs1 = 2, .imm = a->nzuimm };
    return trans_addi(ctx, &arg);
}

static bool trans_c_fld(DisasContext *ctx, arg_c_fld *a)
{
    arg_fld arg = { .rd = a->rd, .rs1 = a->rs1, .imm = a->uimm };
    return trans_fld(ctx, &arg);
}

static bool trans_c_lw(DisasContext *ctx, arg_c_lw *a)
{
    arg_lw arg = { .rd = a->rd, .rs1 = a->rs1, .imm = a->uimm };
    return trans_lw(ctx, &arg);
}

static bool trans_c_flw_ld(DisasContext *ctx, arg_c_flw_ld *a)
{
#ifdef TARGET_RISCV32
    /* C.FLW ( RV32FC-only ) */
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);

    arg_c_lw tmp;
    decode_insn16_extract_cl_w(&tmp, ctx->opcode);
    arg_flw arg = { .rd = tmp.rd, .rs1 = tmp.rs1, .imm = tmp.uimm };
    return trans_flw(ctx, &arg);
#else
    /* C.LD ( RV64C/RV128C-only ) */
    arg_c_fld tmp;
    decode_insn16_extract_cl_d(&tmp, ctx->opcode);
    arg_ld arg = { .rd = tmp.rd, .rs1 = tmp.rs1, .imm = tmp.uimm };
    return trans_ld(ctx, &arg);
#endif
}

static bool trans_c_fsd(DisasContext *ctx, arg_c_fsd *a)
{
    arg_fsd arg = { .rs1 = a->rs1, .rs2 = a->rs2, .imm = a->uimm };
    return trans_fsd(ctx, &arg);
}

static bool trans_c_sw(DisasContext *ctx, arg_c_sw *a)
{
    arg_sw arg = { .rs1 = a->rs1, .rs2 = a->rs2, .imm = a->uimm };
    return trans_sw(ctx, &arg);
}

static bool trans_c_fsw_sd(DisasContext *ctx, arg_c_fsw_sd *a)
{
#ifdef TARGET_RISCV32
    /* C.FSW ( RV32FC-only ) */
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);

    arg_c_sw tmp;
    decode_insn16_extract_cs_w(&tmp, ctx->opcode);
    arg_fsw arg = { .rs1 = tmp.rs1, .rs2 = tmp.rs2, .imm = tmp.uimm };
    return trans_fsw(ctx, &arg);
#else
    /* C.SD ( RV64C/RV128C-only ) */
    arg_c_fsd tmp;
    decode_insn16_extract_cs_d(&tmp, ctx->opcode);
    arg_sd arg = { .rs1 = tmp.rs1, .rs2 = tmp.rs2, .imm = tmp.uimm };
    return trans_sd(ctx, &arg);
#endif
}

static bool trans_c_addi(DisasContext *ctx, arg_c_addi *a)
{
    if (a->imm == 0) {
        /* Hint: insn is valid but does not affect state */
        return true;
    }
    arg_addi arg = { .rd = a->rd, .rs1 = a->rd, .imm = a->imm };
    return trans_addi(ctx, &arg);
}

static bool trans_c_jal_addiw(DisasContext *ctx, arg_c_jal_addiw *a)
{
#ifdef TARGET_RISCV32
    /* C.JAL */
    arg_c_j tmp;
    decode_insn16_extract_cj(&tmp, ctx->opcode);
    arg_jal arg = { .rd = 1, .imm = tmp.imm };
    return trans_jal(ctx, &arg);
#else
    /* C.ADDIW */
    arg_addiw arg = { .rd = a->rd, .rs1 = a->rd, .imm = a->imm };
    return trans_addiw(ctx, &arg);
#endif
}

static bool trans_c_li(DisasContext *ctx, arg_c_li *a)
{
    if (a->rd == 0) {
        /* Hint: insn is valid but does not affect state */
        return true;
    }
    arg_addi arg = { .rd = a->rd, .rs1 = 0, .imm = a->imm };
    return trans_addi(ctx, &arg);
}

static bool trans_c_addi16sp_lui(DisasContext *ctx, arg_c_addi16sp_lui *a)
{
    if (a->rd == 2) {
        /* C.ADDI16SP */
        arg_addi arg = { .rd = 2, .rs1 = 2, .imm = a->imm_addi16sp };
        return trans_addi(ctx, &arg);
    } else if (a->imm_lui != 0) {
        /* C.LUI */
        if (a->rd == 0) {
            /* Hint: insn is valid but does not affect state */
            return true;
        }
        arg_lui arg = { .rd = a->rd, .imm = a->imm_lui };
        return trans_lui(ctx, &arg);
    }
    return false;
}

static bool trans_c_srli(DisasContext *ctx, arg_c_srli *a)
{
    int shamt = a->shamt;
    if (shamt == 0) {
        /* For RV128 a shamt of 0 means a shift by 64 */
        shamt = 64;
    }
    /* Ensure, that shamt[5] is zero for RV32 */
    if (shamt >= TARGET_LONG_BITS) {
        return false;
    }

    arg_srli arg = { .rd = a->rd, .rs1 = a->rd, .shamt = a->shamt };
    return trans_srli(ctx, &arg);
}

static bool trans_c_srai(DisasContext *ctx, arg_c_srai *a)
{
    int shamt = a->shamt;
    if (shamt == 0) {
        /* For RV128 a shamt of 0 means a shift by 64 */
        shamt = 64;
    }
    /* Ensure, that shamt[5] is zero for RV32 */
    if (shamt >= TARGET_LONG_BITS) {
        return false;
    }

    arg_srai arg = { .rd = a->rd, .rs1 = a->rd, .shamt = a->shamt };
    return trans_srai(ctx, &arg);
}

static bool trans_c_andi(DisasContext *ctx, arg_c_andi *a)
{
    arg_andi arg = { .rd = a->rd, .rs1 = a->rd, .imm = a->imm };
    return trans_andi(ctx, &arg);
}

static bool trans_c_sub(DisasContext *ctx, arg_c_sub *a)
{
    arg_sub arg = { .rd = a->rd, .rs1 = a->rd, .rs2 = a->rs2 };
    return trans_sub(ctx, &arg);
}

static bool trans_c_xor(DisasContext *ctx, arg_c_xor *a)
{
    arg_xor arg = { .rd = a->rd, .rs1 = a->rd, .rs2 = a->rs2 };
    return trans_xor(ctx, &arg);
}

static bool trans_c_or(DisasContext *ctx, arg_c_or *a)
{
    arg_or arg = { .rd = a->rd, .rs1 = a->rd, .rs2 = a->rs2 };
    return trans_or(ctx, &arg);
}

static bool trans_c_and(DisasContext *ctx, arg_c_and *a)
{
    arg_and arg = { .rd = a->rd, .rs1 = a->rd, .rs2 = a->rs2 };
    return trans_and(ctx, &arg);
}

static bool trans_c_subw(DisasContext *ctx, arg_c_subw *a)
{
#ifdef TARGET_RISCV64
    arg_subw arg = { .rd = a->rd, .rs1 = a->rd, .rs2 = a->rs2 };
    return trans_subw(ctx, &arg);
#else
    return false;
#endif
}

static bool trans_c_addw(DisasContext *ctx, arg_c_addw *a)
{
#ifdef TARGET_RISCV64
    arg_addw arg = { .rd = a->rd, .rs1 = a->rd, .rs2 = a->rs2 };
    return trans_addw(ctx, &arg);
#else
    return false;
#endif
}

static bool trans_c_j(DisasContext *ctx, arg_c_j *a)
{
    arg_jal arg = { .rd = 0, .imm = a->imm };
    return trans_jal(ctx, &arg);
}

static bool trans_c_beqz(DisasContext *ctx, arg_c_beqz *a)
{
    arg_beq arg = { .rs1 = a->rs1, .rs2 = 0, .imm = a->imm };
    return trans_beq(ctx, &arg);
}

static bool trans_c_bnez(DisasContext *ctx, arg_c_bnez *a)
{
    arg_bne arg = { .rs1 = a->rs1, .rs2 = 0, .imm = a->imm };
    return trans_bne(ctx, &arg);
}

static bool trans_c_slli(DisasContext *ctx, arg_c_slli *a)
{
    int shamt = a->shamt;
    if (shamt == 0) {
        /* For RV128 a shamt of 0 means a shift by 64 */
        shamt = 64;
    }
    /* Ensure, that shamt[5] is zero for RV32 */
    if (shamt >= TARGET_LONG_BITS) {
        return false;
    }

    arg_slli arg = { .rd = a->rd, .rs1 = a->rd, .shamt = a->shamt };
    return trans_slli(ctx, &arg);
}

static bool trans_c_fldsp(DisasContext *ctx, arg_c_fldsp *a)
{
    arg_fld arg = { .rd = a->rd, .rs1 = 2, .imm = a->uimm };
    return trans_fld(ctx, &arg);
}

static bool trans_c_lwsp(DisasContext *ctx, arg_c_lwsp *a)
{
    arg_lw arg = { .rd = a->rd, .rs1 = 2, .imm = a->uimm };
    return trans_lw(ctx, &arg);
}

static bool trans_c_flwsp_ldsp(DisasContext *ctx, arg_c_flwsp_ldsp *a)
{
#ifdef TARGET_RISCV32
    /* C.FLWSP */
    arg_flw arg_flw = { .rd = a->rd, .rs1 = 2, .imm = a->uimm_flwsp };
    return trans_flw(ctx, &arg_flw);
#else
    /* C.LDSP */
    arg_ld arg_ld = { .rd = a->rd, .rs1 = 2, .imm = a->uimm_ldsp };
    return trans_ld(ctx, &arg_ld);
#endif
    return false;
}

static bool trans_c_jr_mv(DisasContext *ctx, arg_c_jr_mv *a)
{
    if (a->rd != 0 && a->rs2 == 0) {
        /* C.JR */
        arg_jalr arg = { .rd = 0, .rs1 = a->rd, .imm = 0 };
        return trans_jalr(ctx, &arg);
    } else if (a->rd != 0 && a->rs2 != 0) {
        /* C.MV */
        arg_add arg = { .rd = a->rd, .rs1 = 0, .rs2 = a->rs2 };
        return trans_add(ctx, &arg);
    }
    return false;
}

static bool trans_c_ebreak_jalr_add(DisasContext *ctx, arg_c_ebreak_jalr_add *a)
{
    if (a->rd == 0 && a->rs2 == 0) {
        /* C.EBREAK */
        arg_ebreak arg = { };
        return trans_ebreak(ctx, &arg);
    } else if (a->rd != 0) {
        if (a->rs2 == 0) {
            /* C.JALR */
            arg_jalr arg = { .rd = 1, .rs1 = a->rd, .imm = 0 };
            return trans_jalr(ctx, &arg);
        } else {
            /* C.ADD */
            arg_add arg = { .rd = a->rd, .rs1 = a->rd, .rs2 = a->rs2 };
            return trans_add(ctx, &arg);
        }
    }
    return false;
}

static bool trans_c_fsdsp(DisasContext *ctx, arg_c_fsdsp *a)
{
    arg_fsd arg = { .rs1 = 2, .rs2 = a->rs2, .imm = a->uimm };
    return trans_fsd(ctx, &arg);
}

static bool trans_c_swsp(DisasContext *ctx, arg_c_swsp *a)
{
    arg_sw arg = { .rs1 = 2, .rs2 = a->rs2, .imm = a->uimm };
    return trans_sw(ctx, &arg);
}

static bool trans_c_fswsp_sdsp(DisasContext *ctx, arg_c_fswsp_sdsp *a)
{
#ifdef TARGET_RISCV32
    /* C.FSWSP */
    arg_fsw a_fsw = { .rs1 = 2, .rs2 = a->rs2, .imm = a->uimm_fswsp };
    return trans_fsw(ctx, &a_fsw);
#else
    /* C.SDSP */
    arg_sd a_sd = { .rs1 = 2, .rs2 = a->rs2, .imm = a->uimm_sdsp };
    return trans_sd(ctx, &a_sd);
#endif
}
