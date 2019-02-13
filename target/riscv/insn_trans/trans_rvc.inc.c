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
    return false;
#else
    /* C.LD ( RV64C/RV128C-only ) */
    return false;
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
    return false;
#else
    /* C.SD ( RV64C/RV128C-only ) */
    return false;
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
    arg_jal arg = { .rd = 1, .imm = a->imm };
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
