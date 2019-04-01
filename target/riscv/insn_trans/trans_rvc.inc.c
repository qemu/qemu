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

static bool trans_c_flw_ld(DisasContext *ctx, arg_c_flw_ld *a)
{
#ifdef TARGET_RISCV32
    /* C.FLW ( RV32FC-only ) */
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);

    arg_i arg;
    decode_insn16_extract_cl_w(ctx, &arg, ctx->opcode);
    return trans_flw(ctx, &arg);
#else
    /* C.LD ( RV64C/RV128C-only ) */
    arg_i arg;
    decode_insn16_extract_cl_d(ctx, &arg, ctx->opcode);
    return trans_ld(ctx, &arg);
#endif
}

static bool trans_c_fsw_sd(DisasContext *ctx, arg_c_fsw_sd *a)
{
#ifdef TARGET_RISCV32
    /* C.FSW ( RV32FC-only ) */
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);

    arg_s arg;
    decode_insn16_extract_cs_w(ctx, &arg, ctx->opcode);
    return trans_fsw(ctx, &arg);
#else
    /* C.SD ( RV64C/RV128C-only ) */
    arg_s arg;
    decode_insn16_extract_cs_d(ctx, &arg, ctx->opcode);
    return trans_sd(ctx, &arg);
#endif
}

static bool trans_c_jal_addiw(DisasContext *ctx, arg_c_jal_addiw *a)
{
#ifdef TARGET_RISCV32
    /* C.JAL */
    arg_j tmp;
    decode_insn16_extract_cj(ctx, &tmp, ctx->opcode);
    arg_jal arg = { .rd = 1, .imm = tmp.imm };
    return trans_jal(ctx, &arg);
#else
    /* C.ADDIW */
    arg_addiw arg = { .rd = a->rd, .rs1 = a->rd, .imm = a->imm };
    return trans_addiw(ctx, &arg);
#endif
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


static bool trans_c_subw(DisasContext *ctx, arg_c_subw *a)
{
#ifdef TARGET_RISCV64
    return trans_subw(ctx, a);
#else
    return false;
#endif
}

static bool trans_c_addw(DisasContext *ctx, arg_c_addw *a)
{
#ifdef TARGET_RISCV64
    return trans_addw(ctx, a);
#else
    return false;
#endif
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
