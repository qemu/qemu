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

#include "../tcg-ldst.c.inc"
#include "../tcg-pool.c.inc"

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

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 1);
    return TCG_REG_A0 + slot;
}

#define TCG_CT_CONST_ZERO  0x100
#define TCG_CT_CONST_S12   0x200
#define TCG_CT_CONST_N12   0x400
#define TCG_CT_CONST_M12   0x800

#define ALL_GENERAL_REGS   MAKE_64BIT_MASK(0, 32)

#define sextreg  sextract64

/* test if a constant matches the constraint */
static bool tcg_target_const_match(int64_t val, TCGType type, int ct)
{
    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    }
    /*
     * Sign extended from 12 bits: [-0x800, 0x7ff].
     * Used for most arithmetic, as this is the isa field.
     */
    if ((ct & TCG_CT_CONST_S12) && val >= -0x800 && val <= 0x7ff) {
        return 1;
    }
    /*
     * Sign extended from 12 bits, negated: [-0x7ff, 0x800].
     * Used for subtraction, where a constant must be handled by ADDI.
     */
    if ((ct & TCG_CT_CONST_N12) && val >= -0x7ff && val <= 0x800) {
        return 1;
    }
    /*
     * Sign extended from 12 bits, +/- matching: [-0x7ff, 0x7ff].
     * Used by addsub2, which may need the negative operation,
     * and requires the modified constant to be representable.
     */
    if ((ct & TCG_CT_CONST_M12) && val >= -0x7ff && val <= 0x7ff) {
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

    OPC_FENCE = 0x0000000f,
    OPC_NOP   = OPC_ADDI,   /* nop = addi r0,r0,0 */
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
        p[i] = OPC_NOP;
    }
}

/*
 * Relocations
 */

static bool reloc_sbimm12(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;

    tcg_debug_assert((offset & 1) == 0);
    if (offset == sextreg(offset, 0, 12)) {
        *src_rw |= encode_sbimm12(offset);
        return true;
    }

    return false;
}

static bool reloc_jimm20(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;

    tcg_debug_assert((offset & 1) == 0);
    if (offset == sextreg(offset, 0, 20)) {
        *src_rw |= encode_ujimm20(offset);
        return true;
    }

    return false;
}

static bool reloc_call(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t offset = (intptr_t)target - (intptr_t)src_rx;
    int32_t lo = sextreg(offset, 0, 12);
    int32_t hi = offset - lo;

    if (offset == hi + lo) {
        src_rw[0] |= encode_uimm20(hi);
        src_rw[1] |= encode_imm12(lo);
        return true;
    }

    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);
    switch (type) {
    case R_RISCV_BRANCH:
        return reloc_sbimm12(code_ptr, (tcg_insn_unit *)value);
    case R_RISCV_JAL:
        return reloc_jimm20(code_ptr, (tcg_insn_unit *)value);
    case R_RISCV_CALL:
        return reloc_call(code_ptr, (tcg_insn_unit *)value);
    default:
        g_assert_not_reached();
    }
}

/*
 * TCG intrinsics
 */

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        tcg_out_opc_imm(s, OPC_ADDI, ret, arg, 0);
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long val)
{
    tcg_target_long lo, hi, tmp;
    int shift, ret;

    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    lo = sextreg(val, 0, 12);
    if (val == lo) {
        tcg_out_opc_imm(s, OPC_ADDI, rd, TCG_REG_ZERO, lo);
        return;
    }

    hi = val - lo;
    if (val == (int32_t)val) {
        tcg_out_opc_upper(s, OPC_LUI, rd, hi);
        if (lo != 0) {
            tcg_out_opc_imm(s, OPC_ADDIW, rd, rd, lo);
        }
        return;
    }

    tmp = tcg_pcrel_diff(s, (void *)val);
    if (tmp == (int32_t)tmp) {
        tcg_out_opc_upper(s, OPC_AUIPC, rd, 0);
        tcg_out_opc_imm(s, OPC_ADDI, rd, rd, 0);
        ret = reloc_call(s->code_ptr - 2, (const tcg_insn_unit *)val);
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

static bool tcg_out_xchg(TCGContext *s, TCGType type, TCGReg r1, TCGReg r2)
{
    return false;
}

static void tcg_out_addi_ptr(TCGContext *s, TCGReg rd, TCGReg rs,
                             tcg_target_long imm)
{
    /* This function is only used for passing structs by reference. */
    g_assert_not_reached();
}

static void tcg_out_ext8u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_ANDI, ret, arg, 0xff);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLIW, ret, arg, 16);
    tcg_out_opc_imm(s, OPC_SRLIW, ret, ret, 16);
}

static void tcg_out_ext32u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLI, ret, arg, 32);
    tcg_out_opc_imm(s, OPC_SRLI, ret, ret, 32);
}

static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLIW, ret, arg, 24);
    tcg_out_opc_imm(s, OPC_SRAIW, ret, ret, 24);
}

static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLIW, ret, arg, 16);
    tcg_out_opc_imm(s, OPC_SRAIW, ret, ret, 16);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_ADDIW, ret, arg, 0);
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_ext32s(s, ret, arg);
    }
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_ext32u(s, ret, arg);
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_ext32s(s, ret, arg);
}

static void tcg_out_ldst(TCGContext *s, RISCVInsn opc, TCGReg data,
                         TCGReg addr, intptr_t offset)
{
    intptr_t imm12 = sextreg(offset, 0, 12);

    if (offset != imm12) {
        intptr_t diff = tcg_pcrel_diff(s, (void *)offset);

        if (addr == TCG_REG_ZERO && diff == (int32_t)diff) {
            imm12 = sextreg(diff, 0, 12);
            tcg_out_opc_upper(s, OPC_AUIPC, TCG_REG_TMP2, diff - imm12);
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP2, offset - imm12);
            if (addr != TCG_REG_ZERO) {
                tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP2, TCG_REG_TMP2, addr);
            }
        }
        addr = TCG_REG_TMP2;
    }

    switch (opc) {
    case OPC_SB:
    case OPC_SH:
    case OPC_SW:
    case OPC_SD:
        tcg_out_opc_store(s, opc, addr, data, imm12);
        break;
    case OPC_LB:
    case OPC_LBU:
    case OPC_LH:
    case OPC_LHU:
    case OPC_LW:
    case OPC_LWU:
    case OPC_LD:
        tcg_out_opc_imm(s, opc, data, addr, imm12);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    RISCVInsn insn = type == TCG_TYPE_I32 ? OPC_LW : OPC_LD;
    tcg_out_ldst(s, insn, arg, arg1, arg2);
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    RISCVInsn insn = type == TCG_TYPE_I32 ? OPC_SW : OPC_SD;
    tcg_out_ldst(s, insn, arg, arg1, arg2);
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    if (val == 0) {
        tcg_out_st(s, type, TCG_REG_ZERO, base, ofs);
        return true;
    }
    return false;
}

static void tcg_out_addsub2(TCGContext *s,
                            TCGReg rl, TCGReg rh,
                            TCGReg al, TCGReg ah,
                            TCGArg bl, TCGArg bh,
                            bool cbl, bool cbh, bool is_sub, bool is32bit)
{
    const RISCVInsn opc_add = is32bit ? OPC_ADDW : OPC_ADD;
    const RISCVInsn opc_addi = is32bit ? OPC_ADDIW : OPC_ADDI;
    const RISCVInsn opc_sub = is32bit ? OPC_SUBW : OPC_SUB;
    TCGReg th = TCG_REG_TMP1;

    /* If we have a negative constant such that negating it would
       make the high part zero, we can (usually) eliminate one insn.  */
    if (cbl && cbh && bh == -1 && bl != 0) {
        bl = -bl;
        bh = 0;
        is_sub = !is_sub;
    }

    /* By operating on the high part first, we get to use the final
       carry operation to move back from the temporary.  */
    if (!cbh) {
        tcg_out_opc_reg(s, (is_sub ? opc_sub : opc_add), th, ah, bh);
    } else if (bh != 0 || ah == rl) {
        tcg_out_opc_imm(s, opc_addi, th, ah, (is_sub ? -bh : bh));
    } else {
        th = ah;
    }

    /* Note that tcg optimization should eliminate the bl == 0 case.  */
    if (is_sub) {
        if (cbl) {
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_REG_TMP0, al, bl);
            tcg_out_opc_imm(s, opc_addi, rl, al, -bl);
        } else {
            tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_TMP0, al, bl);
            tcg_out_opc_reg(s, opc_sub, rl, al, bl);
        }
        tcg_out_opc_reg(s, opc_sub, rh, th, TCG_REG_TMP0);
    } else {
        if (cbl) {
            tcg_out_opc_imm(s, opc_addi, rl, al, bl);
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_REG_TMP0, rl, bl);
        } else if (al == bl) {
            /*
             * If the input regs overlap, this is a simple doubling
             * and carry-out is the input msb.  This special case is
             * required when the output reg overlaps the input,
             * but we might as well use it always.
             */
            tcg_out_opc_imm(s, OPC_SLTI, TCG_REG_TMP0, al, 0);
            tcg_out_opc_reg(s, opc_add, rl, al, al);
        } else {
            tcg_out_opc_reg(s, opc_add, rl, al, bl);
            tcg_out_opc_reg(s, OPC_SLTU, TCG_REG_TMP0,
                            rl, (rl == bl ? al : bl));
        }
        tcg_out_opc_reg(s, opc_add, rh, th, TCG_REG_TMP0);
    }
}

static const struct {
    RISCVInsn op;
    bool swap;
} tcg_brcond_to_riscv[] = {
    [TCG_COND_EQ] =  { OPC_BEQ,  false },
    [TCG_COND_NE] =  { OPC_BNE,  false },
    [TCG_COND_LT] =  { OPC_BLT,  false },
    [TCG_COND_GE] =  { OPC_BGE,  false },
    [TCG_COND_LE] =  { OPC_BGE,  true  },
    [TCG_COND_GT] =  { OPC_BLT,  true  },
    [TCG_COND_LTU] = { OPC_BLTU, false },
    [TCG_COND_GEU] = { OPC_BGEU, false },
    [TCG_COND_LEU] = { OPC_BGEU, true  },
    [TCG_COND_GTU] = { OPC_BLTU, true  }
};

static void tcg_out_brcond(TCGContext *s, TCGCond cond, TCGReg arg1,
                           TCGReg arg2, TCGLabel *l)
{
    RISCVInsn op = tcg_brcond_to_riscv[cond].op;

    tcg_debug_assert(op != 0);

    if (tcg_brcond_to_riscv[cond].swap) {
        TCGReg t = arg1;
        arg1 = arg2;
        arg2 = t;
    }

    tcg_out_reloc(s, s->code_ptr, R_RISCV_BRANCH, l, 0);
    tcg_out_opc_branch(s, op, arg1, arg2, 0);
}

static void tcg_out_setcond(TCGContext *s, TCGCond cond, TCGReg ret,
                            TCGReg arg1, TCGReg arg2)
{
    switch (cond) {
    case TCG_COND_EQ:
        tcg_out_opc_reg(s, OPC_SUB, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_SLTIU, ret, ret, 1);
        break;
    case TCG_COND_NE:
        tcg_out_opc_reg(s, OPC_SUB, ret, arg1, arg2);
        tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, ret);
        break;
    case TCG_COND_LT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        break;
    case TCG_COND_GE:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_LE:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg2, arg1);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_GT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg2, arg1);
        break;
    case TCG_COND_LTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        break;
    case TCG_COND_GEU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_LEU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg2, arg1);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_GTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg2, arg1);
        break;
    default:
         g_assert_not_reached();
         break;
     }
}

static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *arg, bool tail)
{
    TCGReg link = tail ? TCG_REG_ZERO : TCG_REG_RA;
    ptrdiff_t offset = tcg_pcrel_diff(s, arg);
    int ret;

    tcg_debug_assert((offset & 1) == 0);
    if (offset == sextreg(offset, 0, 20)) {
        /* short jump: -2097150 to 2097152 */
        tcg_out_opc_jump(s, OPC_JAL, link, offset);
    } else if (offset == (int32_t)offset) {
        /* long jump: -2147483646 to 2147483648 */
        tcg_out_opc_upper(s, OPC_AUIPC, TCG_REG_TMP0, 0);
        tcg_out_opc_imm(s, OPC_JALR, link, TCG_REG_TMP0, 0);
        ret = reloc_call(s->code_ptr - 2, arg);
        tcg_debug_assert(ret == true);
    } else {
        /* far jump: 64-bit */
        tcg_target_long imm = sextreg((tcg_target_long)arg, 0, 12);
        tcg_target_long base = (tcg_target_long)arg - imm;
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP0, base);
        tcg_out_opc_imm(s, OPC_JALR, link, TCG_REG_TMP0, imm);
    }
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *arg,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, arg, false);
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    tcg_insn_unit insn = OPC_FENCE;

    if (a0 & TCG_MO_LD_LD) {
        insn |= 0x02200000;
    }
    if (a0 & TCG_MO_ST_LD) {
        insn |= 0x01200000;
    }
    if (a0 & TCG_MO_LD_ST) {
        insn |= 0x02100000;
    }
    if (a0 & TCG_MO_ST_ST) {
        insn |= 0x02200000;
    }
    tcg_out32(s, insn);
}

/*
 * Load/store and TLB
 */

static void tcg_out_goto(TCGContext *s, const tcg_insn_unit *target)
{
    tcg_out_opc_jump(s, OPC_JAL, TCG_REG_ZERO, 0);
    bool ok = reloc_jimm20(s->code_ptr - 1, target);
    tcg_debug_assert(ok);
}

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return false;
}

/* We have three temps, we might as well expose them. */
static const TCGLdstHelperParam ldst_helper_param = {
    .ntmp = 3, .tmp = { TCG_REG_TMP0, TCG_REG_TMP1, TCG_REG_TMP2 }
};

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    MemOp opc = get_memop(l->oi);

    /* resolve label address */
    if (!reloc_sbimm12(l->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    /* call load helper */
    tcg_out_ld_helper_args(s, l, &ldst_helper_param);
    tcg_out_call_int(s, qemu_ld_helpers[opc & MO_SSIZE], false);
    tcg_out_ld_helper_ret(s, l, true, &ldst_helper_param);

    tcg_out_goto(s, l->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    MemOp opc = get_memop(l->oi);

    /* resolve label address */
    if (!reloc_sbimm12(l->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    /* call store helper */
    tcg_out_st_helper_args(s, l, &ldst_helper_param);
    tcg_out_call_int(s, qemu_st_helpers[opc & MO_SIZE], false);

    tcg_out_goto(s, l->raddr);
    return true;
}

/*
 * For softmmu, perform the TLB load and compare.
 * For useronly, perform any required alignment tests.
 * In both cases, return a TCGLabelQemuLdst structure if the slow path
 * is required and fill in @h with the host address for the fast path.
 */
static TCGLabelQemuLdst *prepare_host_addr(TCGContext *s, TCGReg *pbase,
                                           TCGReg addr_reg, MemOpIdx oi,
                                           bool is_ld)
{
    TCGLabelQemuLdst *ldst = NULL;
    MemOp opc = get_memop(oi);
    TCGAtomAlign aa;
    unsigned a_mask;

    aa = atom_and_align_for_opc(s, opc, MO_ATOM_IFALIGN, false);
    a_mask = (1u << aa.align) - 1;

#ifdef CONFIG_SOFTMMU
    unsigned s_bits = opc & MO_SIZE;
    unsigned s_mask = (1u << s_bits) - 1;
    int mem_index = get_mmuidx(oi);
    int fast_ofs = TLB_MASK_TABLE_OFS(mem_index);
    int mask_ofs = fast_ofs + offsetof(CPUTLBDescFast, mask);
    int table_ofs = fast_ofs + offsetof(CPUTLBDescFast, table);
    int compare_mask;
    TCGReg addr_adj;

    ldst = new_ldst_label(s);
    ldst->is_ld = is_ld;
    ldst->oi = oi;
    ldst->addrlo_reg = addr_reg;

    QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
    QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -(1 << 11));
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP0, TCG_AREG0, mask_ofs);
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP1, TCG_AREG0, table_ofs);

    tcg_out_opc_imm(s, OPC_SRLI, TCG_REG_TMP2, addr_reg,
                    TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    tcg_out_opc_reg(s, OPC_AND, TCG_REG_TMP2, TCG_REG_TMP2, TCG_REG_TMP0);
    tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP2, TCG_REG_TMP2, TCG_REG_TMP1);

    /*
     * For aligned accesses, we check the first byte and include the alignment
     * bits within the address.  For unaligned access, we check that we don't
     * cross pages using the address of the last byte of the access.
     */
    addr_adj = addr_reg;
    if (a_mask < s_mask) {
        addr_adj = TCG_REG_TMP0;
        tcg_out_opc_imm(s, TARGET_LONG_BITS == 32 ? OPC_ADDIW : OPC_ADDI,
                        addr_adj, addr_reg, s_mask - a_mask);
    }
    compare_mask = TARGET_PAGE_MASK | a_mask;
    if (compare_mask == sextreg(compare_mask, 0, 12)) {
        tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_TMP1, addr_adj, compare_mask);
    } else {
        tcg_out_movi(s, TCG_TYPE_TL, TCG_REG_TMP1, compare_mask);
        tcg_out_opc_reg(s, OPC_AND, TCG_REG_TMP1, TCG_REG_TMP1, addr_adj);
    }

    /* Load the tlb comparator and the addend.  */
    tcg_out_ld(s, TCG_TYPE_TL, TCG_REG_TMP0, TCG_REG_TMP2,
               is_ld ? offsetof(CPUTLBEntry, addr_read)
                     : offsetof(CPUTLBEntry, addr_write));
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP2, TCG_REG_TMP2,
               offsetof(CPUTLBEntry, addend));

    /* Compare masked address with the TLB entry. */
    ldst->label_ptr[0] = s->code_ptr;
    tcg_out_opc_branch(s, OPC_BNE, TCG_REG_TMP0, TCG_REG_TMP1, 0);

    /* TLB Hit - translate address using addend.  */
    addr_adj = addr_reg;
    if (TARGET_LONG_BITS == 32) {
        addr_adj = TCG_REG_TMP0;
        tcg_out_ext32u(s, addr_adj, addr_reg);
    }
    tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP0, TCG_REG_TMP2, addr_adj);
    *pbase = TCG_REG_TMP0;
#else
    if (a_mask) {
        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addrlo_reg = addr_reg;

        /* We are expecting alignment max 7, so we can always use andi. */
        tcg_debug_assert(a_mask == sextreg(a_mask, 0, 12));
        tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_TMP1, addr_reg, a_mask);

        ldst->label_ptr[0] = s->code_ptr;
        tcg_out_opc_branch(s, OPC_BNE, TCG_REG_TMP1, TCG_REG_ZERO, 0);
    }

    TCGReg base = addr_reg;
    if (TARGET_LONG_BITS == 32) {
        tcg_out_ext32u(s, TCG_REG_TMP0, base);
        base = TCG_REG_TMP0;
    }
    if (guest_base != 0) {
        tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP0, TCG_GUEST_BASE_REG, base);
        base = TCG_REG_TMP0;
    }
    *pbase = base;
#endif

    return ldst;
}

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGReg val,
                                   TCGReg base, MemOp opc, TCGType type)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & (MO_SSIZE)) {
    case MO_UB:
        tcg_out_opc_imm(s, OPC_LBU, val, base, 0);
        break;
    case MO_SB:
        tcg_out_opc_imm(s, OPC_LB, val, base, 0);
        break;
    case MO_UW:
        tcg_out_opc_imm(s, OPC_LHU, val, base, 0);
        break;
    case MO_SW:
        tcg_out_opc_imm(s, OPC_LH, val, base, 0);
        break;
    case MO_UL:
        if (type == TCG_TYPE_I64) {
            tcg_out_opc_imm(s, OPC_LWU, val, base, 0);
            break;
        }
        /* FALLTHRU */
    case MO_SL:
        tcg_out_opc_imm(s, OPC_LW, val, base, 0);
        break;
    case MO_UQ:
        tcg_out_opc_imm(s, OPC_LD, val, base, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
{
    TCGLabelQemuLdst *ldst;
    TCGReg base;

    ldst = prepare_host_addr(s, &base, addr_reg, oi, true);
    tcg_out_qemu_ld_direct(s, data_reg, base, get_memop(oi), data_type);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, TCGReg val,
                                   TCGReg base, MemOp opc)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & (MO_SSIZE)) {
    case MO_8:
        tcg_out_opc_store(s, OPC_SB, base, val, 0);
        break;
    case MO_16:
        tcg_out_opc_store(s, OPC_SH, base, val, 0);
        break;
    case MO_32:
        tcg_out_opc_store(s, OPC_SW, base, val, 0);
        break;
    case MO_64:
        tcg_out_opc_store(s, OPC_SD, base, val, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_st(TCGContext *s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
{
    TCGLabelQemuLdst *ldst;
    TCGReg base;

    ldst = prepare_host_addr(s, &base, addr_reg, oi, false);
    tcg_out_qemu_st_direct(s, data_reg, base, get_memop(oi));

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static const tcg_insn_unit *tb_ret_addr;

static void tcg_out_exit_tb(TCGContext *s, uintptr_t a0)
{
    /* Reuse the zeroing that exists for goto_ptr.  */
    if (a0 == 0) {
        tcg_out_call_int(s, tcg_code_gen_epilogue, true);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_A0, a0);
        tcg_out_call_int(s, tb_ret_addr, true);
    }
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    /* Direct branch will be patched by tb_target_set_jmp_target. */
    set_jmp_insn_offset(s, which);
    tcg_out32(s, OPC_JAL);

    /* When branch is out of range, fall through to indirect. */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP0, TCG_REG_ZERO,
               get_jmp_target_addr(s, which));
    tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, TCG_REG_TMP0, 0);
    set_jmp_reset_offset(s, which);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    uintptr_t addr = tb->jmp_target_addr[n];
    ptrdiff_t offset = addr - jmp_rx;
    tcg_insn_unit insn;

    /* Either directly branch, or fall through to indirect branch. */
    if (offset == sextreg(offset, 0, 20)) {
        insn = encode_uj(OPC_JAL, TCG_REG_ZERO, offset);
    } else {
        insn = OPC_NOP;
    }
    qatomic_set((uint32_t *)jmp_rw, insn);
    flush_idcache_range(jmp_rx, jmp_rw, 4);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    TCGArg a0 = args[0];
    TCGArg a1 = args[1];
    TCGArg a2 = args[2];
    int c2 = const_args[2];

    switch (opc) {
    case INDEX_op_goto_ptr:
        tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, a0, 0);
        break;

    case INDEX_op_br:
        tcg_out_reloc(s, s->code_ptr, R_RISCV_JAL, arg_label(a0), 0);
        tcg_out_opc_jump(s, OPC_JAL, TCG_REG_ZERO, 0);
        break;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        tcg_out_ldst(s, OPC_LBU, a0, a1, a2);
        break;
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
        tcg_out_ldst(s, OPC_LB, a0, a1, a2);
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        tcg_out_ldst(s, OPC_LHU, a0, a1, a2);
        break;
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
        tcg_out_ldst(s, OPC_LH, a0, a1, a2);
        break;
    case INDEX_op_ld32u_i64:
        tcg_out_ldst(s, OPC_LWU, a0, a1, a2);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32s_i64:
        tcg_out_ldst(s, OPC_LW, a0, a1, a2);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldst(s, OPC_LD, a0, a1, a2);
        break;

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        tcg_out_ldst(s, OPC_SB, a0, a1, a2);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        tcg_out_ldst(s, OPC_SH, a0, a1, a2);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_ldst(s, OPC_SW, a0, a1, a2);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldst(s, OPC_SD, a0, a1, a2);
        break;

    case INDEX_op_add_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDIW, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_ADDW, a0, a1, a2);
        }
        break;
    case INDEX_op_add_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_ADD, a0, a1, a2);
        }
        break;

    case INDEX_op_sub_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDIW, a0, a1, -a2);
        } else {
            tcg_out_opc_reg(s, OPC_SUBW, a0, a1, a2);
        }
        break;
    case INDEX_op_sub_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ADDI, a0, a1, -a2);
        } else {
            tcg_out_opc_reg(s, OPC_SUB, a0, a1, a2);
        }
        break;

    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ANDI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_AND, a0, a1, a2);
        }
        break;

    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_ORI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_OR, a0, a1, a2);
        }
        break;

    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_XORI, a0, a1, a2);
        } else {
            tcg_out_opc_reg(s, OPC_XOR, a0, a1, a2);
        }
        break;

    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
        tcg_out_opc_imm(s, OPC_XORI, a0, a1, -1);
        break;

    case INDEX_op_neg_i32:
        tcg_out_opc_reg(s, OPC_SUBW, a0, TCG_REG_ZERO, a1);
        break;
    case INDEX_op_neg_i64:
        tcg_out_opc_reg(s, OPC_SUB, a0, TCG_REG_ZERO, a1);
        break;

    case INDEX_op_mul_i32:
        tcg_out_opc_reg(s, OPC_MULW, a0, a1, a2);
        break;
    case INDEX_op_mul_i64:
        tcg_out_opc_reg(s, OPC_MUL, a0, a1, a2);
        break;

    case INDEX_op_div_i32:
        tcg_out_opc_reg(s, OPC_DIVW, a0, a1, a2);
        break;
    case INDEX_op_div_i64:
        tcg_out_opc_reg(s, OPC_DIV, a0, a1, a2);
        break;

    case INDEX_op_divu_i32:
        tcg_out_opc_reg(s, OPC_DIVUW, a0, a1, a2);
        break;
    case INDEX_op_divu_i64:
        tcg_out_opc_reg(s, OPC_DIVU, a0, a1, a2);
        break;

    case INDEX_op_rem_i32:
        tcg_out_opc_reg(s, OPC_REMW, a0, a1, a2);
        break;
    case INDEX_op_rem_i64:
        tcg_out_opc_reg(s, OPC_REM, a0, a1, a2);
        break;

    case INDEX_op_remu_i32:
        tcg_out_opc_reg(s, OPC_REMUW, a0, a1, a2);
        break;
    case INDEX_op_remu_i64:
        tcg_out_opc_reg(s, OPC_REMU, a0, a1, a2);
        break;

    case INDEX_op_shl_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SLLIW, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_SLLW, a0, a1, a2);
        }
        break;
    case INDEX_op_shl_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SLLI, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_SLL, a0, a1, a2);
        }
        break;

    case INDEX_op_shr_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRLIW, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_SRLW, a0, a1, a2);
        }
        break;
    case INDEX_op_shr_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRLI, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_SRL, a0, a1, a2);
        }
        break;

    case INDEX_op_sar_i32:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRAIW, a0, a1, a2 & 0x1f);
        } else {
            tcg_out_opc_reg(s, OPC_SRAW, a0, a1, a2);
        }
        break;
    case INDEX_op_sar_i64:
        if (c2) {
            tcg_out_opc_imm(s, OPC_SRAI, a0, a1, a2 & 0x3f);
        } else {
            tcg_out_opc_reg(s, OPC_SRA, a0, a1, a2);
        }
        break;

    case INDEX_op_add2_i32:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], false, true);
        break;
    case INDEX_op_add2_i64:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], false, false);
        break;
    case INDEX_op_sub2_i32:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], true, true);
        break;
    case INDEX_op_sub2_i64:
        tcg_out_addsub2(s, a0, a1, a2, args[3], args[4], args[5],
                        const_args[4], const_args[5], true, false);
        break;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, a2, a0, a1, arg_label(args[3]));
        break;

    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
        tcg_out_setcond(s, args[3], a0, a1, a2);
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, a0, a1, a2, TCG_TYPE_I32);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, a0, a1, a2, TCG_TYPE_I64);
        break;
    case INDEX_op_qemu_st_i32:
        tcg_out_qemu_st(s, a0, a1, a2, TCG_TYPE_I32);
        break;
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, a0, a1, a2, TCG_TYPE_I64);
        break;

    case INDEX_op_extrh_i64_i32:
        tcg_out_opc_imm(s, OPC_SRAI, a0, a1, 32);
        break;

    case INDEX_op_mulsh_i32:
    case INDEX_op_mulsh_i64:
        tcg_out_opc_reg(s, OPC_MULH, a0, a1, a2);
        break;

    case INDEX_op_muluh_i32:
    case INDEX_op_muluh_i64:
        tcg_out_opc_reg(s, OPC_MULHU, a0, a1, a2);
        break;

    case INDEX_op_mb:
        tcg_out_mb(s, a0);
        break;

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    case INDEX_op_exit_tb:  /* Always emitted via tcg_out_exit_tb.  */
    case INDEX_op_goto_tb:  /* Always emitted via tcg_out_goto_tb.  */
    case INDEX_op_ext8s_i32:  /* Always emitted via tcg_reg_alloc_op.  */
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extrl_i64_i32:
    default:
        g_assert_not_reached();
    }
}

static TCGConstraintSetIndex tcg_target_op_def(TCGOpcode op)
{
    switch (op) {
    case INDEX_op_goto_ptr:
        return C_O0_I1(r);

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_not_i32:
    case INDEX_op_neg_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_not_i64:
    case INDEX_op_neg_i64:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_extrl_i64_i32:
    case INDEX_op_extrh_i64_i32:
    case INDEX_op_ext_i32_i64:
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return C_O0_I2(rZ, r);

    case INDEX_op_add_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_add_i64:
    case INDEX_op_and_i64:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
        return C_O1_I2(r, r, rI);

    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
        return C_O1_I2(r, rZ, rN);

    case INDEX_op_mul_i32:
    case INDEX_op_mulsh_i32:
    case INDEX_op_muluh_i32:
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
    case INDEX_op_setcond_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_mulsh_i64:
    case INDEX_op_muluh_i64:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
    case INDEX_op_setcond_i64:
        return C_O1_I2(r, rZ, rZ);

    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
        return C_O1_I2(r, r, ri);

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return C_O0_I2(rZ, rZ);

    case INDEX_op_add2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i32:
    case INDEX_op_sub2_i64:
        return C_O2_I4(r, r, rZ, rZ, rM, rM);

    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        return C_O1_I1(r, r);
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        return C_O0_I2(rZ, r);

    default:
        g_assert_not_reached();
    }
}

static const int tcg_target_callee_save_regs[] = {
    TCG_REG_S0,       /* used for the global env (TCG_AREG0) */
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
    TCG_REG_RA,       /* should be last for ABI compliance */
};

/* Stack frame parameters.  */
#define REG_SIZE   (TCG_TARGET_REG_BITS / 8)
#define SAVE_SIZE  ((int)ARRAY_SIZE(tcg_target_callee_save_regs) * REG_SIZE)
#define TEMP_SIZE  (CPU_TEMP_BUF_NLONGS * (int)sizeof(long))
#define FRAME_SIZE ((TCG_STATIC_CALL_ARGS_SIZE + TEMP_SIZE + SAVE_SIZE \
                     + TCG_TARGET_STACK_ALIGN - 1) \
                    & -TCG_TARGET_STACK_ALIGN)
#define SAVE_OFS   (TCG_STATIC_CALL_ARGS_SIZE + TEMP_SIZE)

/* We're expecting to be able to use an immediate for frame allocation.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE > 0x7ff);

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int i;

    tcg_set_frame(s, TCG_REG_SP, TCG_STATIC_CALL_ARGS_SIZE, TEMP_SIZE);

    /* TB prologue */
    tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_SP, TCG_REG_SP, -FRAME_SIZE);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_st(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

#if !defined(CONFIG_SOFTMMU)
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base);
    tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
#endif

    /* Call generated code */
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, tcg_target_call_iarg_regs[1], 0);

    /* Return path for goto_ptr. Set return value to 0 */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_mov(s, TCG_TYPE_REG, TCG_REG_A0, TCG_REG_ZERO);

    /* TB epilogue */
    tb_ret_addr = tcg_splitwx_to_rx(s->code_ptr);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_ld(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_SP, SAVE_OFS + i * REG_SIZE);
    }

    tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_SP, TCG_REG_SP, FRAME_SIZE);
    tcg_out_opc_imm(s, OPC_JALR, TCG_REG_ZERO, TCG_REG_RA, 0);
}

static void tcg_target_init(TCGContext *s)
{
    tcg_target_available_regs[TCG_TYPE_I32] = 0xffffffff;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffffffff;

    tcg_target_call_clobber_regs = -1u;
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S0);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S1);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S2);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S3);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S4);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S5);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S6);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S7);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S8);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S9);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S10);
    tcg_regset_reset_reg(tcg_target_call_clobber_regs, TCG_REG_S11);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_ZERO);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP1);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP2);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_GP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TP);
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[ARRAY_SIZE(tcg_target_callee_save_regs) * 2];
} DebugFrame;

#define ELF_HOST_MACHINE EM_RISCV

static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE) - 4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = -(TCG_TARGET_REG_BITS / 8) & 0x7f, /* sleb128 */
    .h.cie.return_column = TCG_REG_RA,

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, TCG_REG_SP,                 /* DW_CFA_def_cfa sp, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        0x80 + 9,  12,                  /* DW_CFA_offset, s1,  -96 */
        0x80 + 18, 11,                  /* DW_CFA_offset, s2,  -88 */
        0x80 + 19, 10,                  /* DW_CFA_offset, s3,  -80 */
        0x80 + 20, 9,                   /* DW_CFA_offset, s4,  -72 */
        0x80 + 21, 8,                   /* DW_CFA_offset, s5,  -64 */
        0x80 + 22, 7,                   /* DW_CFA_offset, s6,  -56 */
        0x80 + 23, 6,                   /* DW_CFA_offset, s7,  -48 */
        0x80 + 24, 5,                   /* DW_CFA_offset, s8,  -40 */
        0x80 + 25, 4,                   /* DW_CFA_offset, s9,  -32 */
        0x80 + 26, 3,                   /* DW_CFA_offset, s10, -24 */
        0x80 + 27, 2,                   /* DW_CFA_offset, s11, -16 */
        0x80 + 1 , 1,                   /* DW_CFA_offset, ra,  -8 */
    }
};

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
