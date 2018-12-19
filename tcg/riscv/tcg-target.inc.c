/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2018 SiFive, Inc
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Based on i386/tcg-target.c and mips/tcg-target.c
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "tcg-pool.inc.c"

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "zero",
    "ra",
    "sp",
    "gp",
    "tp",
    "t0",
    "t1",
    "t2",
    "s0",
    "s1",
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "s8",
    "s9",
    "s10",
    "s11",
    "t3",
    "t4",
    "t5",
    "t6"
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    /* Call saved registers */
    /* TCG_REG_S0 reservered for TCG_AREG0 */
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_S8,
    TCG_REG_S9,
    TCG_REG_S10,
    TCG_REG_S11,

    /* Call clobbered registers */
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,

    /* Argument registers */
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_A4,
    TCG_REG_A5,
    TCG_REG_A6,
    TCG_REG_A7,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_A4,
    TCG_REG_A5,
    TCG_REG_A6,
    TCG_REG_A7,
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
};

#define TCG_CT_CONST_ZERO  0x100
#define TCG_CT_CONST_S12   0x200
#define TCG_CT_CONST_N12   0x400
#define TCG_CT_CONST_M12   0x800

static inline tcg_target_long sextreg(tcg_target_long val, int pos, int len)
{
    if (TCG_TARGET_REG_BITS == 32) {
        return sextract32(val, pos, len);
    } else {
        return sextract64(val, pos, len);
    }
}

/* parse target specific constraints */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch (*ct_str++) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        break;
    case 'L':
        /* qemu_ld/qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        /* qemu_ld/qemu_st uses TCG_REG_TMP0 */
#if defined(CONFIG_SOFTMMU)
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[0]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[1]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[2]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[3]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[4]);
#endif
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_S12;
        break;
    case 'N':
        ct->ct |= TCG_CT_CONST_N12;
        break;
    case 'M':
        ct->ct |= TCG_CT_CONST_M12;
        break;
    case 'Z':
        /* we can use a zero immediate as a zero register argument. */
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return NULL;
    }
    return ct_str;
}

/* test if a constant matches the constraint */
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_S12) && val == sextreg(val, 0, 12)) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_N12) && -val == sextreg(-val, 0, 12)) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_M12) && val >= -0xfff && val <= 0xfff) {
        return 1;
    }
    return 0;
}

/*
 * RISC-V Base ISA opcodes (IM)
 */

typedef enum {
    OPC_ADD = 0x33,
    OPC_ADDI = 0x13,
    OPC_AND = 0x7033,
    OPC_ANDI = 0x7013,
    OPC_AUIPC = 0x17,
    OPC_BEQ = 0x63,
    OPC_BGE = 0x5063,
    OPC_BGEU = 0x7063,
    OPC_BLT = 0x4063,
    OPC_BLTU = 0x6063,
    OPC_BNE = 0x1063,
    OPC_DIV = 0x2004033,
    OPC_DIVU = 0x2005033,
    OPC_JAL = 0x6f,
    OPC_JALR = 0x67,
    OPC_LB = 0x3,
    OPC_LBU = 0x4003,
    OPC_LD = 0x3003,
    OPC_LH = 0x1003,
    OPC_LHU = 0x5003,
    OPC_LUI = 0x37,
    OPC_LW = 0x2003,
    OPC_LWU = 0x6003,
    OPC_MUL = 0x2000033,
    OPC_MULH = 0x2001033,
    OPC_MULHSU = 0x2002033,
    OPC_MULHU = 0x2003033,
    OPC_OR = 0x6033,
    OPC_ORI = 0x6013,
    OPC_REM = 0x2006033,
    OPC_REMU = 0x2007033,
    OPC_SB = 0x23,
    OPC_SD = 0x3023,
    OPC_SH = 0x1023,
    OPC_SLL = 0x1033,
    OPC_SLLI = 0x1013,
    OPC_SLT = 0x2033,
    OPC_SLTI = 0x2013,
    OPC_SLTIU = 0x3013,
    OPC_SLTU = 0x3033,
    OPC_SRA = 0x40005033,
    OPC_SRAI = 0x40005013,
    OPC_SRL = 0x5033,
    OPC_SRLI = 0x5013,
    OPC_SUB = 0x40000033,
    OPC_SW = 0x2023,
    OPC_XOR = 0x4033,
    OPC_XORI = 0x4013,

#if TCG_TARGET_REG_BITS == 64
    OPC_ADDIW = 0x1b,
    OPC_ADDW = 0x3b,
    OPC_DIVUW = 0x200503b,
    OPC_DIVW = 0x200403b,
    OPC_MULW = 0x200003b,
    OPC_REMUW = 0x200703b,
    OPC_REMW = 0x200603b,
    OPC_SLLIW = 0x101b,
    OPC_SLLW = 0x103b,
    OPC_SRAIW = 0x4000501b,
    OPC_SRAW = 0x4000503b,
    OPC_SRLIW = 0x501b,
    OPC_SRLW = 0x503b,
    OPC_SUBW = 0x4000003b,
#else
    /* Simplify code throughout by defining aliases for RV32.  */
    OPC_ADDIW = OPC_ADDI,
    OPC_ADDW = OPC_ADD,
    OPC_DIVUW = OPC_DIVU,
    OPC_DIVW = OPC_DIV,
    OPC_MULW = OPC_MUL,
    OPC_REMUW = OPC_REMU,
    OPC_REMW = OPC_REM,
    OPC_SLLIW = OPC_SLLI,
    OPC_SLLW = OPC_SLL,
    OPC_SRAIW = OPC_SRAI,
    OPC_SRAW = OPC_SRA,
    OPC_SRLIW = OPC_SRLI,
    OPC_SRLW = OPC_SRL,
    OPC_SUBW = OPC_SUB,
#endif

    OPC_FENCE = 0x0000000f,
} RISCVInsn;

/*
 * RISC-V immediate and instruction encoders (excludes 16-bit RVC)
 */

/* Type-R */

static int32_t encode_r(RISCVInsn opc, TCGReg rd, TCGReg rs1, TCGReg rs2)
{
    return opc | (rd & 0x1f) << 7 | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20;
}

/* Type-I */

static int32_t encode_imm12(uint32_t imm)
{
    return (imm & 0xfff) << 20;
}

static int32_t encode_i(RISCVInsn opc, TCGReg rd, TCGReg rs1, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | (rs1 & 0x1f) << 15 | encode_imm12(imm);
}

/* Type-S */

static int32_t encode_simm12(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0xFE0) << 20;
    ret |= (imm & 0x1F) << 7;

    return ret;
}

static int32_t encode_s(RISCVInsn opc, TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    return opc | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20 | encode_simm12(imm);
}

/* Type-SB */

static int32_t encode_sbimm12(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0x1000) << 19;
    ret |= (imm & 0x7e0) << 20;
    ret |= (imm & 0x1e) << 7;
    ret |= (imm & 0x800) >> 4;

    return ret;
}

static int32_t encode_sb(RISCVInsn opc, TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    return opc | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20 | encode_sbimm12(imm);
}

/* Type-U */

static int32_t encode_uimm20(uint32_t imm)
{
    return imm & 0xfffff000;
}

static int32_t encode_u(RISCVInsn opc, TCGReg rd, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | encode_uimm20(imm);
}

/* Type-UJ */

static int32_t encode_ujimm20(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0x0007fe) << (21 - 1);
    ret |= (imm & 0x000800) << (20 - 11);
    ret |= (imm & 0x0ff000) << (12 - 12);
    ret |= (imm & 0x100000) << (31 - 20);

    return ret;
}

static int32_t encode_uj(RISCVInsn opc, TCGReg rd, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | encode_ujimm20(imm);
}

/*
 * RISC-V instruction emitters
 */

static void tcg_out_opc_reg(TCGContext *s, RISCVInsn opc,
                            TCGReg rd, TCGReg rs1, TCGReg rs2)
{
    tcg_out32(s, encode_r(opc, rd, rs1, rs2));
}

static void tcg_out_opc_imm(TCGContext *s, RISCVInsn opc,
                            TCGReg rd, TCGReg rs1, TCGArg imm)
{
    tcg_out32(s, encode_i(opc, rd, rs1, imm));
}

static void tcg_out_opc_store(TCGContext *s, RISCVInsn opc,
                              TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    tcg_out32(s, encode_s(opc, rs1, rs2, imm));
}

static void tcg_out_opc_branch(TCGContext *s, RISCVInsn opc,
                               TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    tcg_out32(s, encode_sb(opc, rs1, rs2, imm));
}

static void tcg_out_opc_upper(TCGContext *s, RISCVInsn opc,
                              TCGReg rd, uint32_t imm)
{
    tcg_out32(s, encode_u(opc, rd, imm));
}

static void tcg_out_opc_jump(TCGContext *s, RISCVInsn opc,
                             TCGReg rd, uint32_t imm)
{
    tcg_out32(s, encode_uj(opc, rd, imm));
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = encode_i(OPC_ADDI, TCG_REG_ZERO, TCG_REG_ZERO, 0);
    }
}

/*
 * Relocations
 */

static bool reloc_sbimm12(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    intptr_t offset = (intptr_t)target - (intptr_t)code_ptr;

    if (offset == sextreg(offset, 1, 12) << 1) {
        code_ptr[0] |= encode_sbimm12(offset);
        return true;
    }

    return false;
}

static bool reloc_jimm20(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    intptr_t offset = (intptr_t)target - (intptr_t)code_ptr;

    if (offset == sextreg(offset, 1, 20) << 1) {
        code_ptr[0] |= encode_ujimm20(offset);
        return true;
    }

    return false;
}

static bool reloc_call(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    intptr_t offset = (intptr_t)target - (intptr_t)code_ptr;
    int32_t lo = sextreg(offset, 0, 12);
    int32_t hi = offset - lo;

    if (offset == hi + lo) {
        code_ptr[0] |= encode_uimm20(hi);
        code_ptr[1] |= encode_imm12(lo);
        return true;
    }

    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    uint32_t insn = *code_ptr;
    intptr_t diff;
    bool short_jmp;

    tcg_debug_assert(addend == 0);

    switch (type) {
    case R_RISCV_BRANCH:
        diff = value - (uintptr_t)code_ptr;
        short_jmp = diff == sextreg(diff, 0, 12);
        if (short_jmp) {
            return reloc_sbimm12(code_ptr, (tcg_insn_unit *)value);
        } else {
            /* Invert the condition */
            insn = insn ^ (1 << 12);
            /* Clear the offset */
            insn &= 0x01fff07f;
            /* Set the offset to the PC + 8 */
            insn |= encode_sbimm12(8);

            /* Move forward */
            code_ptr[0] = insn;

            /* Overwrite the NOP with jal x0,value */
            diff = value - (uintptr_t)(code_ptr + 1);
            insn = encode_uj(OPC_JAL, TCG_REG_ZERO, diff);
            code_ptr[1] = insn;

            return true;
        }
        break;
    case R_RISCV_JAL:
        return reloc_jimm20(code_ptr, (tcg_insn_unit *)value);
        break;
    case R_RISCV_CALL:
        return reloc_call(code_ptr, (tcg_insn_unit *)value);
        break;
    default:
        tcg_abort();
    }
}

/*
 * TCG intrinsics
 */

static void tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return;
    }
    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        tcg_out_opc_imm(s, OPC_ADDI, ret, arg, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long val)
{
    tcg_target_long lo, hi, tmp;
    int shift, ret;

    if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    lo = sextreg(val, 0, 12);
    if (val == lo) {
        tcg_out_opc_imm(s, OPC_ADDI, rd, TCG_REG_ZERO, lo);
        return;
    }

    hi = val - lo;
    if (TCG_TARGET_REG_BITS == 32 || val == (int32_t)val) {
        tcg_out_opc_upper(s, OPC_LUI, rd, hi);
        if (lo != 0) {
            tcg_out_opc_imm(s, OPC_ADDIW, rd, rd, lo);
        }
        return;
    }

    /* We can only be here if TCG_TARGET_REG_BITS != 32 */
    tmp = tcg_pcrel_diff(s, (void *)val);
    if (tmp == (int32_t)tmp) {
        tcg_out_opc_upper(s, OPC_AUIPC, rd, 0);
        tcg_out_opc_imm(s, OPC_ADDI, rd, rd, 0);
        ret = reloc_call(s->code_ptr - 2, (tcg_insn_unit *)val);
        tcg_debug_assert(ret == true);
        return;
    }

    /* Look for a single 20-bit section.  */
    shift = ctz64(val);
    tmp = val >> shift;
    if (tmp == sextreg(tmp, 0, 20)) {
        tcg_out_opc_upper(s, OPC_LUI, rd, tmp << 12);
        if (shift > 12) {
            tcg_out_opc_imm(s, OPC_SLLI, rd, rd, shift - 12);
        } else {
            tcg_out_opc_imm(s, OPC_SRAI, rd, rd, 12 - shift);
        }
        return;
    }

    /* Look for a few high zero bits, with lots of bits set in the middle.  */
    shift = clz64(val);
    tmp = val << shift;
    if (tmp == sextreg(tmp, 12, 20) << 12) {
        tcg_out_opc_upper(s, OPC_LUI, rd, tmp);
        tcg_out_opc_imm(s, OPC_SRLI, rd, rd, shift);
        return;
    } else if (tmp == sextreg(tmp, 0, 12)) {
        tcg_out_opc_imm(s, OPC_ADDI, rd, TCG_REG_ZERO, tmp);
        tcg_out_opc_imm(s, OPC_SRLI, rd, rd, shift);
        return;
    }

    /* Drop into the constant pool.  */
    new_pool_label(s, val, R_RISCV_CALL, s->code_ptr, 0);
    tcg_out_opc_upper(s, OPC_AUIPC, rd, 0);
    tcg_out_opc_imm(s, OPC_LD, rd, rd, 0);
}
