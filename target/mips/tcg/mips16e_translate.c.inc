/*
 *  MIPS16 extension (Code Compaction) ASE translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* MIPS16 major opcodes */
enum {
  M16_OPC_ADDIUSP = 0x00,
  M16_OPC_ADDIUPC = 0x01,
  M16_OPC_B = 0x02,
  M16_OPC_JAL = 0x03,
  M16_OPC_BEQZ = 0x04,
  M16_OPC_BNEQZ = 0x05,
  M16_OPC_SHIFT = 0x06,
  M16_OPC_LD = 0x07,
  M16_OPC_RRIA = 0x08,
  M16_OPC_ADDIU8 = 0x09,
  M16_OPC_SLTI = 0x0a,
  M16_OPC_SLTIU = 0x0b,
  M16_OPC_I8 = 0x0c,
  M16_OPC_LI = 0x0d,
  M16_OPC_CMPI = 0x0e,
  M16_OPC_SD = 0x0f,
  M16_OPC_LB = 0x10,
  M16_OPC_LH = 0x11,
  M16_OPC_LWSP = 0x12,
  M16_OPC_LW = 0x13,
  M16_OPC_LBU = 0x14,
  M16_OPC_LHU = 0x15,
  M16_OPC_LWPC = 0x16,
  M16_OPC_LWU = 0x17,
  M16_OPC_SB = 0x18,
  M16_OPC_SH = 0x19,
  M16_OPC_SWSP = 0x1a,
  M16_OPC_SW = 0x1b,
  M16_OPC_RRR = 0x1c,
  M16_OPC_RR = 0x1d,
  M16_OPC_EXTEND = 0x1e,
  M16_OPC_I64 = 0x1f
};

/* I8 funct field */
enum {
  I8_BTEQZ = 0x0,
  I8_BTNEZ = 0x1,
  I8_SWRASP = 0x2,
  I8_ADJSP = 0x3,
  I8_SVRS = 0x4,
  I8_MOV32R = 0x5,
  I8_MOVR32 = 0x7
};

/* RRR f field */
enum {
  RRR_DADDU = 0x0,
  RRR_ADDU = 0x1,
  RRR_DSUBU = 0x2,
  RRR_SUBU = 0x3
};

/* RR funct field */
enum {
  RR_JR = 0x00,
  RR_SDBBP = 0x01,
  RR_SLT = 0x02,
  RR_SLTU = 0x03,
  RR_SLLV = 0x04,
  RR_BREAK = 0x05,
  RR_SRLV = 0x06,
  RR_SRAV = 0x07,
  RR_DSRL = 0x08,
  RR_CMP = 0x0a,
  RR_NEG = 0x0b,
  RR_AND = 0x0c,
  RR_OR = 0x0d,
  RR_XOR = 0x0e,
  RR_NOT = 0x0f,
  RR_MFHI = 0x10,
  RR_CNVT = 0x11,
  RR_MFLO = 0x12,
  RR_DSRA = 0x13,
  RR_DSLLV = 0x14,
  RR_DSRLV = 0x16,
  RR_DSRAV = 0x17,
  RR_MULT = 0x18,
  RR_MULTU = 0x19,
  RR_DIV = 0x1a,
  RR_DIVU = 0x1b,
  RR_DMULT = 0x1c,
  RR_DMULTU = 0x1d,
  RR_DDIV = 0x1e,
  RR_DDIVU = 0x1f
};

/* I64 funct field */
enum {
  I64_LDSP = 0x0,
  I64_SDSP = 0x1,
  I64_SDRASP = 0x2,
  I64_DADJSP = 0x3,
  I64_LDPC = 0x4,
  I64_DADDIU5 = 0x5,
  I64_DADDIUPC = 0x6,
  I64_DADDIUSP = 0x7
};

/* RR ry field for CNVT */
enum {
  RR_RY_CNVT_ZEB = 0x0,
  RR_RY_CNVT_ZEH = 0x1,
  RR_RY_CNVT_ZEW = 0x2,
  RR_RY_CNVT_SEB = 0x4,
  RR_RY_CNVT_SEH = 0x5,
  RR_RY_CNVT_SEW = 0x6,
};

static int xlat(int r)
{
  static const int map[] = { 16, 17, 2, 3, 4, 5, 6, 7 };

  return map[r];
}

static void decr_and_store(DisasContext *ctx, unsigned regidx, TCGv t0)
{
    TCGv t1 = tcg_temp_new();

    gen_op_addr_addi(ctx, t0, t0, -4);
    gen_load_gpr(t1, regidx);
    tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, mo_endian(ctx) | MO_UL |
                       ctx->default_tcg_memop_mask);
}

static void gen_mips16_save(DisasContext *ctx,
                            int xsregs, int aregs,
                            int do_ra, int do_s0, int do_s1,
                            int framesize)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int args, astatic;

    switch (aregs) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 11:
        args = 0;
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        args = 1;
        break;
    case 8:
    case 9:
    case 10:
        args = 2;
        break;
    case 12:
    case 13:
        args = 3;
        break;
    case 14:
        args = 4;
        break;
    default:
        gen_reserved_instruction(ctx);
        return;
    }

    switch (args) {
    case 4:
        gen_base_offset_addr(ctx, t0, 29, 12);
        gen_load_gpr(t1, 7);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, mo_endian(ctx) | MO_UL |
                           ctx->default_tcg_memop_mask);
        /* Fall through */
    case 3:
        gen_base_offset_addr(ctx, t0, 29, 8);
        gen_load_gpr(t1, 6);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, mo_endian(ctx) | MO_UL |
                           ctx->default_tcg_memop_mask);
        /* Fall through */
    case 2:
        gen_base_offset_addr(ctx, t0, 29, 4);
        gen_load_gpr(t1, 5);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, mo_endian(ctx) | MO_UL |
                           ctx->default_tcg_memop_mask);
        /* Fall through */
    case 1:
        gen_base_offset_addr(ctx, t0, 29, 0);
        gen_load_gpr(t1, 4);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, mo_endian(ctx) | MO_UL |
                           ctx->default_tcg_memop_mask);
    }

    gen_load_gpr(t0, 29);

    if (do_ra) {
        decr_and_store(ctx, 31, t0);
    }

    switch (xsregs) {
    case 7:
        decr_and_store(ctx, 30, t0);
        /* Fall through */
    case 6:
        decr_and_store(ctx, 23, t0);
        /* Fall through */
    case 5:
        decr_and_store(ctx, 22, t0);
        /* Fall through */
    case 4:
        decr_and_store(ctx, 21, t0);
        /* Fall through */
    case 3:
        decr_and_store(ctx, 20, t0);
        /* Fall through */
    case 2:
        decr_and_store(ctx, 19, t0);
        /* Fall through */
    case 1:
        decr_and_store(ctx, 18, t0);
    }

    if (do_s1) {
        decr_and_store(ctx, 17, t0);
    }
    if (do_s0) {
        decr_and_store(ctx, 16, t0);
    }

    switch (aregs) {
    case 0:
    case 4:
    case 8:
    case 12:
    case 14:
        astatic = 0;
        break;
    case 1:
    case 5:
    case 9:
    case 13:
        astatic = 1;
        break;
    case 2:
    case 6:
    case 10:
        astatic = 2;
        break;
    case 3:
    case 7:
        astatic = 3;
        break;
    case 11:
        astatic = 4;
        break;
    default:
        gen_reserved_instruction(ctx);
        return;
    }

    if (astatic > 0) {
        decr_and_store(ctx, 7, t0);
        if (astatic > 1) {
            decr_and_store(ctx, 6, t0);
            if (astatic > 2) {
                decr_and_store(ctx, 5, t0);
                if (astatic > 3) {
                    decr_and_store(ctx, 4, t0);
                }
            }
        }
    }

    gen_op_addr_addi(ctx, cpu_gpr[29], cpu_gpr[29], -framesize);
}

static void decr_and_load(DisasContext *ctx, unsigned regidx, TCGv t0)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    tcg_gen_movi_tl(t2, -4);
    gen_op_addr_add(ctx, t0, t0, t2);
    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TE | MO_SL |
                       ctx->default_tcg_memop_mask);
    gen_store_gpr(t1, regidx);
}

static void gen_mips16_restore(DisasContext *ctx,
                               int xsregs, int aregs,
                               int do_ra, int do_s0, int do_s1,
                               int framesize)
{
    int astatic;
    TCGv t0 = tcg_temp_new();

    gen_op_addr_addi(ctx, t0, cpu_gpr[29], framesize);

    if (do_ra) {
        decr_and_load(ctx, 31, t0);
    }

    switch (xsregs) {
    case 7:
        decr_and_load(ctx, 30, t0);
        /* Fall through */
    case 6:
        decr_and_load(ctx, 23, t0);
        /* Fall through */
    case 5:
        decr_and_load(ctx, 22, t0);
        /* Fall through */
    case 4:
        decr_and_load(ctx, 21, t0);
        /* Fall through */
    case 3:
        decr_and_load(ctx, 20, t0);
        /* Fall through */
    case 2:
        decr_and_load(ctx, 19, t0);
        /* Fall through */
    case 1:
        decr_and_load(ctx, 18, t0);
    }

    if (do_s1) {
        decr_and_load(ctx, 17, t0);
    }
    if (do_s0) {
        decr_and_load(ctx, 16, t0);
    }

    switch (aregs) {
    case 0:
    case 4:
    case 8:
    case 12:
    case 14:
        astatic = 0;
        break;
    case 1:
    case 5:
    case 9:
    case 13:
        astatic = 1;
        break;
    case 2:
    case 6:
    case 10:
        astatic = 2;
        break;
    case 3:
    case 7:
        astatic = 3;
        break;
    case 11:
        astatic = 4;
        break;
    default:
        gen_reserved_instruction(ctx);
        return;
    }

    if (astatic > 0) {
        decr_and_load(ctx, 7, t0);
        if (astatic > 1) {
            decr_and_load(ctx, 6, t0);
            if (astatic > 2) {
                decr_and_load(ctx, 5, t0);
                if (astatic > 3) {
                    decr_and_load(ctx, 4, t0);
                }
            }
        }
    }

    gen_op_addr_addi(ctx, cpu_gpr[29], cpu_gpr[29], framesize);
}

#if defined(TARGET_MIPS64)
static void decode_i64_mips16(DisasContext *ctx,
                              int ry, int funct, int16_t offset,
                              int extended)
{
    switch (funct) {
    case I64_LDSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 3;
        gen_ld(ctx, OPC_LD, ry, 29, offset);
        break;
    case I64_SDSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 3;
        gen_st(ctx, OPC_SD, ry, 29, offset);
        break;
    case I64_SDRASP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : (ctx->opcode & 0xff) << 3;
        gen_st(ctx, OPC_SD, 31, 29, offset);
        break;
    case I64_DADJSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : ((int8_t)ctx->opcode) << 3;
        gen_arith_imm(ctx, OPC_DADDIU, 29, 29, offset);
        break;
    case I64_LDPC:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        if (extended && (ctx->hflags & MIPS_HFLAG_BMASK)) {
            gen_reserved_instruction(ctx);
        } else {
            offset = extended ? offset : offset << 3;
            gen_ld(ctx, OPC_LDPC, ry, 0, offset);
        }
        break;
    case I64_DADDIU5:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : ((int8_t)(offset << 3)) >> 3;
        gen_arith_imm(ctx, OPC_DADDIU, ry, ry, offset);
        break;
    case I64_DADDIUPC:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 2;
        gen_addiupc(ctx, ry, offset, 1, extended);
        break;
    case I64_DADDIUSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 2;
        gen_arith_imm(ctx, OPC_DADDIU, ry, 29, offset);
        break;
    }
}
#endif

static int decode_extended_mips16_opc(CPUMIPSState *env, DisasContext *ctx)
{
    int extend = translator_lduw(env, &ctx->base, ctx->base.pc_next + 2);
    int op, rx, ry, funct, sa;
    int16_t imm, offset;

    ctx->opcode = (ctx->opcode << 16) | extend;
    op = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 22) & 0x1f;
    funct = (ctx->opcode >> 8) & 0x7;
    rx = xlat((ctx->opcode >> 8) & 0x7);
    ry = xlat((ctx->opcode >> 5) & 0x7);
    offset = imm = (int16_t) (((ctx->opcode >> 16) & 0x1f) << 11
                              | ((ctx->opcode >> 21) & 0x3f) << 5
                              | (ctx->opcode & 0x1f));

    /*
     * The extended opcodes cleverly reuse the opcodes from their 16-bit
     * counterparts.
     */
    switch (op) {
    case M16_OPC_ADDIUSP:
        gen_arith_imm(ctx, OPC_ADDIU, rx, 29, imm);
        break;
    case M16_OPC_ADDIUPC:
        gen_addiupc(ctx, rx, imm, 0, 1);
        break;
    case M16_OPC_B:
        gen_compute_branch(ctx, OPC_BEQ, 4, 0, 0, offset << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_BEQZ:
        gen_compute_branch(ctx, OPC_BEQ, 4, rx, 0, offset << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_BNEQZ:
        gen_compute_branch(ctx, OPC_BNE, 4, rx, 0, offset << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_SHIFT:
        switch (ctx->opcode & 0x3) {
        case 0x0:
            gen_shift_imm(ctx, OPC_SLL, rx, ry, sa);
            break;
        case 0x1:
#if defined(TARGET_MIPS64)
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSLL, rx, ry, sa);
#else
            gen_reserved_instruction(ctx);
#endif
            break;
        case 0x2:
            gen_shift_imm(ctx, OPC_SRL, rx, ry, sa);
            break;
        case 0x3:
            gen_shift_imm(ctx, OPC_SRA, rx, ry, sa);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_LD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LD, ry, rx, offset);
        break;
#endif
    case M16_OPC_RRIA:
        imm = ctx->opcode & 0xf;
        imm = imm | ((ctx->opcode >> 20) & 0x7f) << 4;
        imm = imm | ((ctx->opcode >> 16) & 0xf) << 11;
        imm = (int16_t) (imm << 1) >> 1;
        if ((ctx->opcode >> 4) & 0x1) {
#if defined(TARGET_MIPS64)
            check_mips_64(ctx);
            gen_arith_imm(ctx, OPC_DADDIU, ry, rx, imm);
#else
            gen_reserved_instruction(ctx);
#endif
        } else {
            gen_arith_imm(ctx, OPC_ADDIU, ry, rx, imm);
        }
        break;
    case M16_OPC_ADDIU8:
        gen_arith_imm(ctx, OPC_ADDIU, rx, rx, imm);
        break;
    case M16_OPC_SLTI:
        gen_slt_imm(ctx, OPC_SLTI, 24, rx, imm);
        break;
    case M16_OPC_SLTIU:
        gen_slt_imm(ctx, OPC_SLTIU, 24, rx, imm);
        break;
    case M16_OPC_I8:
        switch (funct) {
        case I8_BTEQZ:
            gen_compute_branch(ctx, OPC_BEQ, 4, 24, 0, offset << 1, 0);
            break;
        case I8_BTNEZ:
            gen_compute_branch(ctx, OPC_BNE, 4, 24, 0, offset << 1, 0);
            break;
        case I8_SWRASP:
            gen_st(ctx, OPC_SW, 31, 29, imm);
            break;
        case I8_ADJSP:
            gen_arith_imm(ctx, OPC_ADDIU, 29, 29, imm);
            break;
        case I8_SVRS:
            check_insn(ctx, ISA_MIPS_R1);
            {
                int xsregs = (ctx->opcode >> 24) & 0x7;
                int aregs = (ctx->opcode >> 16) & 0xf;
                int do_ra = (ctx->opcode >> 6) & 0x1;
                int do_s0 = (ctx->opcode >> 5) & 0x1;
                int do_s1 = (ctx->opcode >> 4) & 0x1;
                int framesize = (((ctx->opcode >> 20) & 0xf) << 4
                                 | (ctx->opcode & 0xf)) << 3;

                if (ctx->opcode & (1 << 7)) {
                    gen_mips16_save(ctx, xsregs, aregs,
                                    do_ra, do_s0, do_s1,
                                    framesize);
                } else {
                    gen_mips16_restore(ctx, xsregs, aregs,
                                       do_ra, do_s0, do_s1,
                                       framesize);
                }
            }
            break;
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case M16_OPC_LI:
        tcg_gen_movi_tl(cpu_gpr[rx], (uint16_t) imm);
        break;
    case M16_OPC_CMPI:
        tcg_gen_xori_tl(cpu_gpr[24], cpu_gpr[rx], (uint16_t) imm);
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_SD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_st(ctx, OPC_SD, ry, rx, offset);
        break;
#endif
    case M16_OPC_LB:
        gen_ld(ctx, OPC_LB, ry, rx, offset);
        break;
    case M16_OPC_LH:
        gen_ld(ctx, OPC_LH, ry, rx, offset);
        break;
    case M16_OPC_LWSP:
        gen_ld(ctx, OPC_LW, rx, 29, offset);
        break;
    case M16_OPC_LW:
        gen_ld(ctx, OPC_LW, ry, rx, offset);
        break;
    case M16_OPC_LBU:
        gen_ld(ctx, OPC_LBU, ry, rx, offset);
        break;
    case M16_OPC_LHU:
        gen_ld(ctx, OPC_LHU, ry, rx, offset);
        break;
    case M16_OPC_LWPC:
        gen_ld(ctx, OPC_LWPC, rx, 0, offset);
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_LWU:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LWU, ry, rx, offset);
        break;
#endif
    case M16_OPC_SB:
        gen_st(ctx, OPC_SB, ry, rx, offset);
        break;
    case M16_OPC_SH:
        gen_st(ctx, OPC_SH, ry, rx, offset);
        break;
    case M16_OPC_SWSP:
        gen_st(ctx, OPC_SW, rx, 29, offset);
        break;
    case M16_OPC_SW:
        gen_st(ctx, OPC_SW, ry, rx, offset);
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_I64:
        decode_i64_mips16(ctx, ry, funct, offset, 1);
        break;
#endif
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    return 4;
}

static int decode_ase_mips16e(CPUMIPSState *env, DisasContext *ctx)
{
    int rx, ry;
    int sa;
    int op, cnvt_op, op1, offset;
    int funct;
    int n_bytes;

    op = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 2) & 0x7;
    sa = sa == 0 ? 8 : sa;
    rx = xlat((ctx->opcode >> 8) & 0x7);
    cnvt_op = (ctx->opcode >> 5) & 0x7;
    ry = xlat((ctx->opcode >> 5) & 0x7);
    op1 = offset = ctx->opcode & 0x1f;

    n_bytes = 2;

    switch (op) {
    case M16_OPC_ADDIUSP:
        {
            int16_t imm = ((uint8_t) ctx->opcode) << 2;

            gen_arith_imm(ctx, OPC_ADDIU, rx, 29, imm);
        }
        break;
    case M16_OPC_ADDIUPC:
        gen_addiupc(ctx, rx, ((uint8_t) ctx->opcode) << 2, 0, 0);
        break;
    case M16_OPC_B:
        offset = (ctx->opcode & 0x7ff) << 1;
        offset = (int16_t)(offset << 4) >> 4;
        gen_compute_branch(ctx, OPC_BEQ, 2, 0, 0, offset, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_JAL:
        offset = translator_lduw(env, &ctx->base, ctx->base.pc_next + 2);
        offset = (((ctx->opcode & 0x1f) << 21)
                  | ((ctx->opcode >> 5) & 0x1f) << 16
                  | offset) << 2;
        op = ((ctx->opcode >> 10) & 0x1) ? OPC_JALX : OPC_JAL;
        gen_compute_branch(ctx, op, 4, rx, ry, offset, 2);
        n_bytes = 4;
        break;
    case M16_OPC_BEQZ:
        gen_compute_branch(ctx, OPC_BEQ, 2, rx, 0,
                           ((int8_t)ctx->opcode) << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_BNEQZ:
        gen_compute_branch(ctx, OPC_BNE, 2, rx, 0,
                           ((int8_t)ctx->opcode) << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_SHIFT:
        switch (ctx->opcode & 0x3) {
        case 0x0:
            gen_shift_imm(ctx, OPC_SLL, rx, ry, sa);
            break;
        case 0x1:
#if defined(TARGET_MIPS64)
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSLL, rx, ry, sa);
#else
            gen_reserved_instruction(ctx);
#endif
            break;
        case 0x2:
            gen_shift_imm(ctx, OPC_SRL, rx, ry, sa);
            break;
        case 0x3:
            gen_shift_imm(ctx, OPC_SRA, rx, ry, sa);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_LD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LD, ry, rx, offset << 3);
        break;
#endif
    case M16_OPC_RRIA:
        {
            int16_t imm = (int8_t)((ctx->opcode & 0xf) << 4) >> 4;

            if ((ctx->opcode >> 4) & 1) {
#if defined(TARGET_MIPS64)
                check_insn(ctx, ISA_MIPS3);
                check_mips_64(ctx);
                gen_arith_imm(ctx, OPC_DADDIU, ry, rx, imm);
#else
                gen_reserved_instruction(ctx);
#endif
            } else {
                gen_arith_imm(ctx, OPC_ADDIU, ry, rx, imm);
            }
        }
        break;
    case M16_OPC_ADDIU8:
        {
            int16_t imm = (int8_t) ctx->opcode;

            gen_arith_imm(ctx, OPC_ADDIU, rx, rx, imm);
        }
        break;
    case M16_OPC_SLTI:
        {
            int16_t imm = (uint8_t) ctx->opcode;
            gen_slt_imm(ctx, OPC_SLTI, 24, rx, imm);
        }
        break;
    case M16_OPC_SLTIU:
        {
            int16_t imm = (uint8_t) ctx->opcode;
            gen_slt_imm(ctx, OPC_SLTIU, 24, rx, imm);
        }
        break;
    case M16_OPC_I8:
        {
            int reg32;

            funct = (ctx->opcode >> 8) & 0x7;
            switch (funct) {
            case I8_BTEQZ:
                gen_compute_branch(ctx, OPC_BEQ, 2, 24, 0,
                                   ((int8_t)ctx->opcode) << 1, 0);
                break;
            case I8_BTNEZ:
                gen_compute_branch(ctx, OPC_BNE, 2, 24, 0,
                                   ((int8_t)ctx->opcode) << 1, 0);
                break;
            case I8_SWRASP:
                gen_st(ctx, OPC_SW, 31, 29, (ctx->opcode & 0xff) << 2);
                break;
            case I8_ADJSP:
                gen_arith_imm(ctx, OPC_ADDIU, 29, 29,
                              ((int8_t)ctx->opcode) << 3);
                break;
            case I8_SVRS:
                check_insn(ctx, ISA_MIPS_R1);
                {
                    int do_ra = ctx->opcode & (1 << 6);
                    int do_s0 = ctx->opcode & (1 << 5);
                    int do_s1 = ctx->opcode & (1 << 4);
                    int framesize = ctx->opcode & 0xf;

                    if (framesize == 0) {
                        framesize = 128;
                    } else {
                        framesize = framesize << 3;
                    }

                    if (ctx->opcode & (1 << 7)) {
                        gen_mips16_save(ctx, 0, 0,
                                        do_ra, do_s0, do_s1, framesize);
                    } else {
                        gen_mips16_restore(ctx, 0, 0,
                                           do_ra, do_s0, do_s1, framesize);
                    }
                }
                break;
            case I8_MOV32R:
                {
                    int rz = xlat(ctx->opcode & 0x7);

                    reg32 = (((ctx->opcode >> 3) & 0x3) << 3) |
                        ((ctx->opcode >> 5) & 0x7);
                    gen_arith(ctx, OPC_ADDU, reg32, rz, 0);
                }
                break;
            case I8_MOVR32:
                reg32 = ctx->opcode & 0x1f;
                gen_arith(ctx, OPC_ADDU, ry, reg32, 0);
                break;
            default:
                gen_reserved_instruction(ctx);
                break;
            }
        }
        break;
    case M16_OPC_LI:
        {
            int16_t imm = (uint8_t) ctx->opcode;

            gen_arith_imm(ctx, OPC_ADDIU, rx, 0, imm);
        }
        break;
    case M16_OPC_CMPI:
        {
            int16_t imm = (uint8_t) ctx->opcode;
            gen_logic_imm(ctx, OPC_XORI, 24, rx, imm);
        }
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_SD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_st(ctx, OPC_SD, ry, rx, offset << 3);
        break;
#endif
    case M16_OPC_LB:
        gen_ld(ctx, OPC_LB, ry, rx, offset);
        break;
    case M16_OPC_LH:
        gen_ld(ctx, OPC_LH, ry, rx, offset << 1);
        break;
    case M16_OPC_LWSP:
        gen_ld(ctx, OPC_LW, rx, 29, ((uint8_t)ctx->opcode) << 2);
        break;
    case M16_OPC_LW:
        gen_ld(ctx, OPC_LW, ry, rx, offset << 2);
        break;
    case M16_OPC_LBU:
        gen_ld(ctx, OPC_LBU, ry, rx, offset);
        break;
    case M16_OPC_LHU:
        gen_ld(ctx, OPC_LHU, ry, rx, offset << 1);
        break;
    case M16_OPC_LWPC:
        gen_ld(ctx, OPC_LWPC, rx, 0, ((uint8_t)ctx->opcode) << 2);
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_LWU:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LWU, ry, rx, offset << 2);
        break;
#endif
    case M16_OPC_SB:
        gen_st(ctx, OPC_SB, ry, rx, offset);
        break;
    case M16_OPC_SH:
        gen_st(ctx, OPC_SH, ry, rx, offset << 1);
        break;
    case M16_OPC_SWSP:
        gen_st(ctx, OPC_SW, rx, 29, ((uint8_t)ctx->opcode) << 2);
        break;
    case M16_OPC_SW:
        gen_st(ctx, OPC_SW, ry, rx, offset << 2);
        break;
    case M16_OPC_RRR:
        {
            int rz = xlat((ctx->opcode >> 2) & 0x7);
            int mips32_op;

            switch (ctx->opcode & 0x3) {
            case RRR_ADDU:
                mips32_op = OPC_ADDU;
                break;
            case RRR_SUBU:
                mips32_op = OPC_SUBU;
                break;
#if defined(TARGET_MIPS64)
            case RRR_DADDU:
                mips32_op = OPC_DADDU;
                check_insn(ctx, ISA_MIPS3);
                check_mips_64(ctx);
                break;
            case RRR_DSUBU:
                mips32_op = OPC_DSUBU;
                check_insn(ctx, ISA_MIPS3);
                check_mips_64(ctx);
                break;
#endif
            default:
                gen_reserved_instruction(ctx);
                goto done;
            }

            gen_arith(ctx, mips32_op, rz, rx, ry);
        done:
            ;
        }
        break;
    case M16_OPC_RR:
        switch (op1) {
        case RR_JR:
            {
                int nd = (ctx->opcode >> 7) & 0x1;
                int link = (ctx->opcode >> 6) & 0x1;
                int ra = (ctx->opcode >> 5) & 0x1;

                if (nd) {
                    check_insn(ctx, ISA_MIPS_R1);
                }

                if (link) {
                    op = OPC_JALR;
                } else {
                    op = OPC_JR;
                }

                gen_compute_branch(ctx, op, 2, ra ? 31 : rx, 31, 0,
                                   (nd ? 0 : 2));
            }
            break;
        case RR_SDBBP:
            if (is_uhi(ctx, extract32(ctx->opcode, 5, 6))) {
                ctx->base.is_jmp = DISAS_SEMIHOST;
            } else {
                /*
                 * XXX: not clear which exception should be raised
                 *      when in debug mode...
                 */
                check_insn(ctx, ISA_MIPS_R1);
                generate_exception_end(ctx, EXCP_DBp);
            }
            break;
        case RR_SLT:
            gen_slt(ctx, OPC_SLT, 24, rx, ry);
            break;
        case RR_SLTU:
            gen_slt(ctx, OPC_SLTU, 24, rx, ry);
            break;
        case RR_BREAK:
            generate_exception_break(ctx, extract32(ctx->opcode, 5, 6));
            break;
        case RR_SLLV:
            gen_shift(ctx, OPC_SLLV, ry, rx, ry);
            break;
        case RR_SRLV:
            gen_shift(ctx, OPC_SRLV, ry, rx, ry);
            break;
        case RR_SRAV:
            gen_shift(ctx, OPC_SRAV, ry, rx, ry);
            break;
#if defined(TARGET_MIPS64)
        case RR_DSRL:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSRL, ry, ry, sa);
            break;
#endif
        case RR_CMP:
            gen_logic(ctx, OPC_XOR, 24, rx, ry);
            break;
        case RR_NEG:
            gen_arith(ctx, OPC_SUBU, rx, 0, ry);
            break;
        case RR_AND:
            gen_logic(ctx, OPC_AND, rx, rx, ry);
            break;
        case RR_OR:
            gen_logic(ctx, OPC_OR, rx, rx, ry);
            break;
        case RR_XOR:
            gen_logic(ctx, OPC_XOR, rx, rx, ry);
            break;
        case RR_NOT:
            gen_logic(ctx, OPC_NOR, rx, ry, 0);
            break;
        case RR_MFHI:
            gen_HILO(ctx, OPC_MFHI, 0, rx);
            break;
        case RR_CNVT:
            check_insn(ctx, ISA_MIPS_R1);
            switch (cnvt_op) {
            case RR_RY_CNVT_ZEB:
                tcg_gen_ext8u_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_ZEH:
                tcg_gen_ext16u_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_SEB:
                tcg_gen_ext8s_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_SEH:
                tcg_gen_ext16s_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
#if defined(TARGET_MIPS64)
            case RR_RY_CNVT_ZEW:
                check_insn(ctx, ISA_MIPS_R1);
                check_mips_64(ctx);
                tcg_gen_ext32u_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_SEW:
                check_insn(ctx, ISA_MIPS_R1);
                check_mips_64(ctx);
                tcg_gen_ext32s_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
#endif
            default:
                gen_reserved_instruction(ctx);
                break;
            }
            break;
        case RR_MFLO:
            gen_HILO(ctx, OPC_MFLO, 0, rx);
            break;
#if defined(TARGET_MIPS64)
        case RR_DSRA:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSRA, ry, ry, sa);
            break;
        case RR_DSLLV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift(ctx, OPC_DSLLV, ry, rx, ry);
            break;
        case RR_DSRLV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift(ctx, OPC_DSRLV, ry, rx, ry);
            break;
        case RR_DSRAV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift(ctx, OPC_DSRAV, ry, rx, ry);
            break;
#endif
        case RR_MULT:
            gen_muldiv(ctx, OPC_MULT, 0, rx, ry);
            break;
        case RR_MULTU:
            gen_muldiv(ctx, OPC_MULTU, 0, rx, ry);
            break;
        case RR_DIV:
            gen_muldiv(ctx, OPC_DIV, 0, rx, ry);
            break;
        case RR_DIVU:
            gen_muldiv(ctx, OPC_DIVU, 0, rx, ry);
            break;
#if defined(TARGET_MIPS64)
        case RR_DMULT:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DMULT, 0, rx, ry);
            break;
        case RR_DMULTU:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DMULTU, 0, rx, ry);
            break;
        case RR_DDIV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DDIV, 0, rx, ry);
            break;
        case RR_DDIVU:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DDIVU, 0, rx, ry);
            break;
#endif
        default:
            gen_reserved_instruction(ctx);
            break;
        }
        break;
    case M16_OPC_EXTEND:
        decode_extended_mips16_opc(env, ctx);
        n_bytes = 4;
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_I64:
        funct = (ctx->opcode >> 8) & 0x7;
        decode_i64_mips16(ctx, ry, funct, offset, 0);
        break;
#endif
    default:
        gen_reserved_instruction(ctx);
        break;
    }

    return n_bytes;
}
