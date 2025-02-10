/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
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

/* We only support generating code for 64-bit mode.  */
#ifndef __arch64__
#error "unsupported code generation mode"
#endif

/* Used for function call generation. */
#define TCG_REG_CALL_STACK              TCG_REG_O6
#define TCG_TARGET_STACK_BIAS           2047
#define TCG_TARGET_STACK_ALIGN          16
#define TCG_TARGET_CALL_STACK_OFFSET    (128 + 6 * 8 + TCG_TARGET_STACK_BIAS)
#define TCG_TARGET_CALL_ARG_I32         TCG_CALL_ARG_EXTEND
#define TCG_TARGET_CALL_ARG_I64         TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I128        TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_RET_I128        TCG_CALL_RET_NORMAL

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%g0",
    "%g1",
    "%g2",
    "%g3",
    "%g4",
    "%g5",
    "%g6",
    "%g7",
    "%o0",
    "%o1",
    "%o2",
    "%o3",
    "%o4",
    "%o5",
    "%o6",
    "%o7",
    "%l0",
    "%l1",
    "%l2",
    "%l3",
    "%l4",
    "%l5",
    "%l6",
    "%l7",
    "%i0",
    "%i1",
    "%i2",
    "%i3",
    "%i4",
    "%i5",
    "%i6",
    "%i7",
};
#endif

#define TCG_CT_CONST_S11  0x100
#define TCG_CT_CONST_S13  0x200

#define ALL_GENERAL_REGS  MAKE_64BIT_MASK(0, 32)

/* Define some temporary registers.  T3 is used for constant generation.  */
#define TCG_REG_T1  TCG_REG_G1
#define TCG_REG_T2  TCG_REG_G2
#define TCG_REG_T3  TCG_REG_O7

#ifndef CONFIG_SOFTMMU
# define TCG_GUEST_BASE_REG TCG_REG_I5
#endif

#define TCG_REG_TB  TCG_REG_I1

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_L0,
    TCG_REG_L1,
    TCG_REG_L2,
    TCG_REG_L3,
    TCG_REG_L4,
    TCG_REG_L5,
    TCG_REG_L6,
    TCG_REG_L7,

    TCG_REG_I0,
    TCG_REG_I1,
    TCG_REG_I2,
    TCG_REG_I3,
    TCG_REG_I4,
    TCG_REG_I5,

    TCG_REG_G3,
    TCG_REG_G4,
    TCG_REG_G5,

    TCG_REG_O0,
    TCG_REG_O1,
    TCG_REG_O2,
    TCG_REG_O3,
    TCG_REG_O4,
    TCG_REG_O5,
};

static const int tcg_target_call_iarg_regs[6] = {
    TCG_REG_O0,
    TCG_REG_O1,
    TCG_REG_O2,
    TCG_REG_O3,
    TCG_REG_O4,
    TCG_REG_O5,
};

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 3);
    return TCG_REG_O0 + slot;
}

#define INSN_OP(x)  ((x) << 30)
#define INSN_OP2(x) ((x) << 22)
#define INSN_OP3(x) ((x) << 19)
#define INSN_OPF(x) ((x) << 5)
#define INSN_RD(x)  ((x) << 25)
#define INSN_RS1(x) ((x) << 14)
#define INSN_RS2(x) (x)
#define INSN_ASI(x) ((x) << 5)

#define INSN_IMM10(x) ((1 << 13) | ((x) & 0x3ff))
#define INSN_IMM11(x) ((1 << 13) | ((x) & 0x7ff))
#define INSN_IMM13(x) ((1 << 13) | ((x) & 0x1fff))
#define INSN_OFF16(x) ((((x) >> 2) & 0x3fff) | ((((x) >> 16) & 3) << 20))
#define INSN_OFF19(x) (((x) >> 2) & 0x07ffff)
#define INSN_COND(x) ((x) << 25)

#define COND_N     0x0
#define COND_E     0x1
#define COND_LE    0x2
#define COND_L     0x3
#define COND_LEU   0x4
#define COND_CS    0x5
#define COND_NEG   0x6
#define COND_VS    0x7
#define COND_A     0x8
#define COND_NE    0x9
#define COND_G     0xa
#define COND_GE    0xb
#define COND_GU    0xc
#define COND_CC    0xd
#define COND_POS   0xe
#define COND_VC    0xf
#define BA         (INSN_OP(0) | INSN_COND(COND_A) | INSN_OP2(0x2))

#define RCOND_Z    1
#define RCOND_LEZ  2
#define RCOND_LZ   3
#define RCOND_NZ   5
#define RCOND_GZ   6
#define RCOND_GEZ  7

#define MOVCC_ICC  (1 << 18)
#define MOVCC_XCC  (1 << 18 | 1 << 12)

#define BPCC_ICC   0
#define BPCC_XCC   (2 << 20)
#define BPCC_PT    (1 << 19)
#define BPCC_PN    0
#define BPCC_A     (1 << 29)

#define BPR_PT     BPCC_PT

#define ARITH_ADD  (INSN_OP(2) | INSN_OP3(0x00))
#define ARITH_ADDCC (INSN_OP(2) | INSN_OP3(0x10))
#define ARITH_AND  (INSN_OP(2) | INSN_OP3(0x01))
#define ARITH_ANDCC (INSN_OP(2) | INSN_OP3(0x11))
#define ARITH_ANDN (INSN_OP(2) | INSN_OP3(0x05))
#define ARITH_OR   (INSN_OP(2) | INSN_OP3(0x02))
#define ARITH_ORCC (INSN_OP(2) | INSN_OP3(0x12))
#define ARITH_ORN  (INSN_OP(2) | INSN_OP3(0x06))
#define ARITH_XOR  (INSN_OP(2) | INSN_OP3(0x03))
#define ARITH_SUB  (INSN_OP(2) | INSN_OP3(0x04))
#define ARITH_SUBCC (INSN_OP(2) | INSN_OP3(0x14))
#define ARITH_ADDC (INSN_OP(2) | INSN_OP3(0x08))
#define ARITH_SUBC (INSN_OP(2) | INSN_OP3(0x0c))
#define ARITH_UMUL (INSN_OP(2) | INSN_OP3(0x0a))
#define ARITH_SMUL (INSN_OP(2) | INSN_OP3(0x0b))
#define ARITH_UDIV (INSN_OP(2) | INSN_OP3(0x0e))
#define ARITH_SDIV (INSN_OP(2) | INSN_OP3(0x0f))
#define ARITH_MULX (INSN_OP(2) | INSN_OP3(0x09))
#define ARITH_UDIVX (INSN_OP(2) | INSN_OP3(0x0d))
#define ARITH_SDIVX (INSN_OP(2) | INSN_OP3(0x2d))
#define ARITH_MOVCC (INSN_OP(2) | INSN_OP3(0x2c))
#define ARITH_MOVR (INSN_OP(2) | INSN_OP3(0x2f))

#define ARITH_ADDXC (INSN_OP(2) | INSN_OP3(0x36) | INSN_OPF(0x11))
#define ARITH_UMULXHI (INSN_OP(2) | INSN_OP3(0x36) | INSN_OPF(0x16))

#define SHIFT_SLL  (INSN_OP(2) | INSN_OP3(0x25))
#define SHIFT_SRL  (INSN_OP(2) | INSN_OP3(0x26))
#define SHIFT_SRA  (INSN_OP(2) | INSN_OP3(0x27))

#define SHIFT_SLLX (INSN_OP(2) | INSN_OP3(0x25) | (1 << 12))
#define SHIFT_SRLX (INSN_OP(2) | INSN_OP3(0x26) | (1 << 12))
#define SHIFT_SRAX (INSN_OP(2) | INSN_OP3(0x27) | (1 << 12))

#define RDY        (INSN_OP(2) | INSN_OP3(0x28) | INSN_RS1(0))
#define WRY        (INSN_OP(2) | INSN_OP3(0x30) | INSN_RD(0))
#define JMPL       (INSN_OP(2) | INSN_OP3(0x38))
#define RETURN     (INSN_OP(2) | INSN_OP3(0x39))
#define SAVE       (INSN_OP(2) | INSN_OP3(0x3c))
#define RESTORE    (INSN_OP(2) | INSN_OP3(0x3d))
#define SETHI      (INSN_OP(0) | INSN_OP2(0x4))
#define CALL       INSN_OP(1)
#define LDUB       (INSN_OP(3) | INSN_OP3(0x01))
#define LDSB       (INSN_OP(3) | INSN_OP3(0x09))
#define LDUH       (INSN_OP(3) | INSN_OP3(0x02))
#define LDSH       (INSN_OP(3) | INSN_OP3(0x0a))
#define LDUW       (INSN_OP(3) | INSN_OP3(0x00))
#define LDSW       (INSN_OP(3) | INSN_OP3(0x08))
#define LDX        (INSN_OP(3) | INSN_OP3(0x0b))
#define STB        (INSN_OP(3) | INSN_OP3(0x05))
#define STH        (INSN_OP(3) | INSN_OP3(0x06))
#define STW        (INSN_OP(3) | INSN_OP3(0x04))
#define STX        (INSN_OP(3) | INSN_OP3(0x0e))
#define LDUBA      (INSN_OP(3) | INSN_OP3(0x11))
#define LDSBA      (INSN_OP(3) | INSN_OP3(0x19))
#define LDUHA      (INSN_OP(3) | INSN_OP3(0x12))
#define LDSHA      (INSN_OP(3) | INSN_OP3(0x1a))
#define LDUWA      (INSN_OP(3) | INSN_OP3(0x10))
#define LDSWA      (INSN_OP(3) | INSN_OP3(0x18))
#define LDXA       (INSN_OP(3) | INSN_OP3(0x1b))
#define STBA       (INSN_OP(3) | INSN_OP3(0x15))
#define STHA       (INSN_OP(3) | INSN_OP3(0x16))
#define STWA       (INSN_OP(3) | INSN_OP3(0x14))
#define STXA       (INSN_OP(3) | INSN_OP3(0x1e))

#define MEMBAR     (INSN_OP(2) | INSN_OP3(0x28) | INSN_RS1(15) | (1 << 13))

#define NOP        (SETHI | INSN_RD(TCG_REG_G0) | 0)

#ifndef ASI_PRIMARY_LITTLE
#define ASI_PRIMARY_LITTLE 0x88
#endif

#define LDUH_LE    (LDUHA | INSN_ASI(ASI_PRIMARY_LITTLE))
#define LDSH_LE    (LDSHA | INSN_ASI(ASI_PRIMARY_LITTLE))
#define LDUW_LE    (LDUWA | INSN_ASI(ASI_PRIMARY_LITTLE))
#define LDSW_LE    (LDSWA | INSN_ASI(ASI_PRIMARY_LITTLE))
#define LDX_LE     (LDXA  | INSN_ASI(ASI_PRIMARY_LITTLE))

#define STH_LE     (STHA  | INSN_ASI(ASI_PRIMARY_LITTLE))
#define STW_LE     (STWA  | INSN_ASI(ASI_PRIMARY_LITTLE))
#define STX_LE     (STXA  | INSN_ASI(ASI_PRIMARY_LITTLE))

#ifndef use_vis3_instructions
bool use_vis3_instructions;
#endif

static bool check_fit_i64(int64_t val, unsigned int bits)
{
    return val == sextract64(val, 0, bits);
}

static bool check_fit_i32(int32_t val, unsigned int bits)
{
    return val == sextract32(val, 0, bits);
}

#define check_fit_tl    check_fit_i64
#define check_fit_ptr   check_fit_i64

static bool patch_reloc(tcg_insn_unit *src_rw, int type,
                        intptr_t value, intptr_t addend)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    uint32_t insn = *src_rw;
    intptr_t pcrel;

    value += addend;
    pcrel = tcg_ptr_byte_diff((tcg_insn_unit *)value, src_rx);

    switch (type) {
    case R_SPARC_WDISP16:
        if (!check_fit_ptr(pcrel >> 2, 16)) {
            return false;
        }
        insn &= ~INSN_OFF16(-1);
        insn |= INSN_OFF16(pcrel);
        break;
    case R_SPARC_WDISP19:
        if (!check_fit_ptr(pcrel >> 2, 19)) {
            return false;
        }
        insn &= ~INSN_OFF19(-1);
        insn |= INSN_OFF19(pcrel);
        break;
    case R_SPARC_13:
        if (!check_fit_ptr(value, 13)) {
            return false;
        }
        insn &= ~INSN_IMM13(-1);
        insn |= INSN_IMM13(value);
        break;
    default:
        g_assert_not_reached();
    }

    *src_rw = insn;
    return true;
}

/* test if a constant matches the constraint */
static bool tcg_target_const_match(int64_t val, int ct,
                                   TCGType type, TCGCond cond, int vece)
{
    if (ct & TCG_CT_CONST) {
        return 1;
    }

    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    if ((ct & TCG_CT_CONST_S11) && check_fit_tl(val, 11)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S13) && check_fit_tl(val, 13)) {
        return 1;
    } else {
        return 0;
    }
}

static void tcg_out_nop(TCGContext *s)
{
    tcg_out32(s, NOP);
}

static void tcg_out_arith(TCGContext *s, TCGReg rd, TCGReg rs1,
                          TCGReg rs2, int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1) | INSN_RS2(rs2));
}

static void tcg_out_arithi(TCGContext *s, TCGReg rd, TCGReg rs1,
                           int32_t offset, int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1) | INSN_IMM13(offset));
}

static void tcg_out_arithc(TCGContext *s, TCGReg rd, TCGReg rs1,
			   int32_t val2, int val2const, int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1)
              | (val2const ? INSN_IMM13(val2) : INSN_RS2(val2)));
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_arith(s, ret, arg, TCG_REG_G0, ARITH_OR);
    }
    return true;
}

static void tcg_out_mov_delay(TCGContext *s, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_arith(s, ret, arg, TCG_REG_G0, ARITH_OR);
    } else {
        tcg_out_nop(s);
    }
}

static void tcg_out_sethi(TCGContext *s, TCGReg ret, uint32_t arg)
{
    tcg_out32(s, SETHI | INSN_RD(ret) | ((arg & 0xfffffc00) >> 10));
}

/* A 13-bit constant sign-extended to 64 bits.  */
static void tcg_out_movi_s13(TCGContext *s, TCGReg ret, int32_t arg)
{
    tcg_out_arithi(s, ret, TCG_REG_G0, arg, ARITH_OR);
}

/* A 32-bit constant sign-extended to 64 bits.  */
static void tcg_out_movi_s32(TCGContext *s, TCGReg ret, int32_t arg)
{
    tcg_out_sethi(s, ret, ~arg);
    tcg_out_arithi(s, ret, ret, (arg & 0x3ff) | -0x400, ARITH_XOR);
}

/* A 32-bit constant zero-extended to 64 bits.  */
static void tcg_out_movi_u32(TCGContext *s, TCGReg ret, uint32_t arg)
{
    tcg_out_sethi(s, ret, arg);
    if (arg & 0x3ff) {
        tcg_out_arithi(s, ret, ret, arg & 0x3ff, ARITH_OR);
    }
}

static void tcg_out_movi_int(TCGContext *s, TCGType type, TCGReg ret,
                             tcg_target_long arg, bool in_prologue,
                             TCGReg scratch)
{
    tcg_target_long hi, lo = (int32_t)arg;
    tcg_target_long test, lsb;

    /* A 13-bit constant sign-extended to 64-bits.  */
    if (check_fit_tl(arg, 13)) {
        tcg_out_movi_s13(s, ret, arg);
        return;
    }

    /* A 32-bit constant, or 32-bit zero-extended to 64-bits.  */
    if (type == TCG_TYPE_I32 || arg == (uint32_t)arg) {
        tcg_out_movi_u32(s, ret, arg);
        return;
    }

    /* A 13-bit constant relative to the TB.  */
    if (!in_prologue) {
        test = tcg_tbrel_diff(s, (void *)arg);
        if (check_fit_ptr(test, 13)) {
            tcg_out_arithi(s, ret, TCG_REG_TB, test, ARITH_ADD);
            return;
        }
    }

    /* A 32-bit constant sign-extended to 64-bits.  */
    if (arg == lo) {
        tcg_out_movi_s32(s, ret, arg);
        return;
    }

    /* A 32-bit constant, shifted.  */
    lsb = ctz64(arg);
    test = (tcg_target_long)arg >> lsb;
    if (lsb > 10 && test == extract64(test, 0, 21)) {
        tcg_out_sethi(s, ret, test << 10);
        tcg_out_arithi(s, ret, ret, lsb - 10, SHIFT_SLLX);
        return;
    } else if (test == (uint32_t)test || test == (int32_t)test) {
        tcg_out_movi_int(s, TCG_TYPE_I64, ret, test, in_prologue, scratch);
        tcg_out_arithi(s, ret, ret, lsb, SHIFT_SLLX);
        return;
    }

    /* Use the constant pool, if possible. */
    if (!in_prologue) {
        new_pool_label(s, arg, R_SPARC_13, s->code_ptr,
                       tcg_tbrel_diff(s, NULL));
        tcg_out32(s, LDX | INSN_RD(ret) | INSN_RS1(TCG_REG_TB));
        return;
    }

    /* A 64-bit constant decomposed into 2 32-bit pieces.  */
    if (check_fit_i32(lo, 13)) {
        hi = (arg - lo) >> 32;
        tcg_out_movi_u32(s, ret, hi);
        tcg_out_arithi(s, ret, ret, 32, SHIFT_SLLX);
        tcg_out_arithi(s, ret, ret, lo, ARITH_ADD);
    } else {
        hi = arg >> 32;
        tcg_out_movi_u32(s, ret, hi);
        tcg_out_movi_u32(s, scratch, lo);
        tcg_out_arithi(s, ret, ret, 32, SHIFT_SLLX);
        tcg_out_arith(s, ret, ret, scratch, ARITH_OR);
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_debug_assert(ret != TCG_REG_T3);
    tcg_out_movi_int(s, type, ret, arg, false, TCG_REG_T3);
}

static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rs)
{
    g_assert_not_reached();
}

static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg rd, TCGReg rs)
{
    g_assert_not_reached();
}

static void tcg_out_ext8u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_arithi(s, rd, rs, 0xff, ARITH_AND);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_arithi(s, rd, rs, 16, SHIFT_SLL);
    tcg_out_arithi(s, rd, rd, 16, SHIFT_SRL);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_arithi(s, rd, rs, 0, SHIFT_SRA);
}

static void tcg_out_ext32u(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_arithi(s, rd, rs, 0, SHIFT_SRL);
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_ext32s(s, rd, rs);
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_ext32u(s, rd, rs);
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg rd, TCGReg rs)
{
    tcg_out_ext32u(s, rd, rs);
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

static void tcg_out_ldst_rr(TCGContext *s, TCGReg data, TCGReg a1,
                            TCGReg a2, int op)
{
    tcg_out32(s, op | INSN_RD(data) | INSN_RS1(a1) | INSN_RS2(a2));
}

static void tcg_out_ldst(TCGContext *s, TCGReg ret, TCGReg addr,
                         intptr_t offset, int op)
{
    if (check_fit_ptr(offset, 13)) {
        tcg_out32(s, op | INSN_RD(ret) | INSN_RS1(addr) |
                  INSN_IMM13(offset));
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T1, offset);
        tcg_out_ldst_rr(s, ret, addr, TCG_REG_T1, op);
    }
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                       TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, ret, arg1, arg2, (type == TCG_TYPE_I32 ? LDUW : LDX));
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, arg, arg1, arg2, (type == TCG_TYPE_I32 ? STW : STX));
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    if (val == 0) {
        tcg_out_st(s, type, TCG_REG_G0, base, ofs);
        return true;
    }
    return false;
}

static void tcg_out_sety(TCGContext *s, TCGReg rs)
{
    tcg_out32(s, WRY | INSN_RS1(TCG_REG_G0) | INSN_RS2(rs));
}

static void tcg_out_div32(TCGContext *s, TCGReg rd, TCGReg rs1,
                          int32_t val2, int val2const, int uns)
{
    /* Load Y with the sign/zero extension of RS1 to 64-bits.  */
    if (uns) {
        tcg_out_sety(s, TCG_REG_G0);
    } else {
        tcg_out_arithi(s, TCG_REG_T1, rs1, 31, SHIFT_SRA);
        tcg_out_sety(s, TCG_REG_T1);
    }

    tcg_out_arithc(s, rd, rs1, val2, val2const,
                   uns ? ARITH_UDIV : ARITH_SDIV);
}

static const uint8_t tcg_cond_to_bcond[16] = {
    [TCG_COND_EQ] = COND_E,
    [TCG_COND_NE] = COND_NE,
    [TCG_COND_TSTEQ] = COND_E,
    [TCG_COND_TSTNE] = COND_NE,
    [TCG_COND_LT] = COND_L,
    [TCG_COND_GE] = COND_GE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_G,
    [TCG_COND_LTU] = COND_CS,
    [TCG_COND_GEU] = COND_CC,
    [TCG_COND_LEU] = COND_LEU,
    [TCG_COND_GTU] = COND_GU,
};

static const uint8_t tcg_cond_to_rcond[16] = {
    [TCG_COND_EQ] = RCOND_Z,
    [TCG_COND_NE] = RCOND_NZ,
    [TCG_COND_LT] = RCOND_LZ,
    [TCG_COND_GT] = RCOND_GZ,
    [TCG_COND_LE] = RCOND_LEZ,
    [TCG_COND_GE] = RCOND_GEZ
};

static void tcg_out_bpcc0(TCGContext *s, int scond, int flags, int off19)
{
    tcg_out32(s, INSN_OP(0) | INSN_OP2(1) | INSN_COND(scond) | flags | off19);
}

static void tcg_out_bpcc(TCGContext *s, int scond, int flags, TCGLabel *l)
{
    int off19 = 0;

    if (l->has_value) {
        off19 = INSN_OFF19(tcg_pcrel_diff(s, l->u.value_ptr));
    } else {
        tcg_out_reloc(s, s->code_ptr, R_SPARC_WDISP19, l, 0);
    }
    tcg_out_bpcc0(s, scond, flags, off19);
}

static void tcg_out_cmp(TCGContext *s, TCGCond cond,
                        TCGReg c1, int32_t c2, int c2const)
{
    tcg_out_arithc(s, TCG_REG_G0, c1, c2, c2const,
                   is_tst_cond(cond) ? ARITH_ANDCC : ARITH_SUBCC);
}

static void tcg_out_brcond_i32(TCGContext *s, TCGCond cond, TCGReg arg1,
                               int32_t arg2, int const_arg2, TCGLabel *l)
{
    tcg_out_cmp(s, cond, arg1, arg2, const_arg2);
    tcg_out_bpcc(s, tcg_cond_to_bcond[cond], BPCC_ICC | BPCC_PT, l);
    tcg_out_nop(s);
}

static void tcg_out_movcc(TCGContext *s, TCGCond cond, int cc, TCGReg ret,
                          int32_t v1, int v1const)
{
    tcg_out32(s, ARITH_MOVCC | cc | INSN_RD(ret)
              | INSN_RS1(tcg_cond_to_bcond[cond])
              | (v1const ? INSN_IMM11(v1) : INSN_RS2(v1)));
}

static void tcg_out_movcond_i32(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg c1, int32_t c2, int c2const,
                                int32_t v1, int v1const)
{
    tcg_out_cmp(s, cond, c1, c2, c2const);
    tcg_out_movcc(s, cond, MOVCC_ICC, ret, v1, v1const);
}

static void tcg_out_brcond_i64(TCGContext *s, TCGCond cond, TCGReg arg1,
                               int32_t arg2, int const_arg2, TCGLabel *l)
{
    /* For 64-bit signed comparisons vs zero, we can avoid the compare.  */
    int rcond = tcg_cond_to_rcond[cond];
    if (arg2 == 0 && rcond) {
        int off16 = 0;

        if (l->has_value) {
            off16 = INSN_OFF16(tcg_pcrel_diff(s, l->u.value_ptr));
        } else {
            tcg_out_reloc(s, s->code_ptr, R_SPARC_WDISP16, l, 0);
        }
        tcg_out32(s, INSN_OP(0) | INSN_OP2(3) | BPR_PT | INSN_RS1(arg1)
                  | INSN_COND(rcond) | off16);
    } else {
        tcg_out_cmp(s, cond, arg1, arg2, const_arg2);
        tcg_out_bpcc(s, tcg_cond_to_bcond[cond], BPCC_XCC | BPCC_PT, l);
    }
    tcg_out_nop(s);
}

static void tcg_out_movr(TCGContext *s, int rcond, TCGReg ret, TCGReg c1,
                         int32_t v1, int v1const)
{
    tcg_out32(s, ARITH_MOVR | INSN_RD(ret) | INSN_RS1(c1) | (rcond << 10)
              | (v1const ? INSN_IMM10(v1) : INSN_RS2(v1)));
}

static void tcg_out_movcond_i64(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg c1, int32_t c2, int c2const,
                                int32_t v1, int v1const)
{
    /* For 64-bit signed comparisons vs zero, we can avoid the compare.
       Note that the immediate range is one bit smaller, so we must check
       for that as well.  */
    int rcond = tcg_cond_to_rcond[cond];
    if (c2 == 0 && rcond && (!v1const || check_fit_i32(v1, 10))) {
        tcg_out_movr(s, rcond, ret, c1, v1, v1const);
    } else {
        tcg_out_cmp(s, cond, c1, c2, c2const);
        tcg_out_movcc(s, cond, MOVCC_XCC, ret, v1, v1const);
    }
}

static void tcg_out_setcond_i32(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg c1, int32_t c2, int c2const, bool neg)
{
    /* For 32-bit comparisons, we can play games with ADDC/SUBC.  */
    switch (cond) {
    case TCG_COND_LTU:
    case TCG_COND_GEU:
        /* The result of the comparison is in the carry bit.  */
        break;

    case TCG_COND_EQ:
    case TCG_COND_NE:
        /* For equality, we can transform to inequality vs zero.  */
        if (c2 != 0) {
            tcg_out_arithc(s, TCG_REG_T1, c1, c2, c2const, ARITH_XOR);
            c2 = TCG_REG_T1;
        } else {
            c2 = c1;
        }
        c1 = TCG_REG_G0, c2const = 0;
        cond = (cond == TCG_COND_EQ ? TCG_COND_GEU : TCG_COND_LTU);
	break;

    case TCG_COND_TSTEQ:
    case TCG_COND_TSTNE:
        /* Transform to inequality vs zero.  */
        tcg_out_arithc(s, TCG_REG_T1, c1, c2, c2const, ARITH_AND);
        c1 = TCG_REG_G0;
        c2 = TCG_REG_T1, c2const = 0;
        cond = (cond == TCG_COND_TSTEQ ? TCG_COND_GEU : TCG_COND_LTU);
	break;

    case TCG_COND_GTU:
    case TCG_COND_LEU:
        /* If we don't need to load a constant into a register, we can
           swap the operands on GTU/LEU.  There's no benefit to loading
           the constant into a temporary register.  */
        if (!c2const || c2 == 0) {
            TCGReg t = c1;
            c1 = c2;
            c2 = t;
            c2const = 0;
            cond = tcg_swap_cond(cond);
            break;
        }
        /* FALLTHRU */

    default:
        tcg_out_cmp(s, cond, c1, c2, c2const);
        tcg_out_movi_s13(s, ret, 0);
        tcg_out_movcc(s, cond, MOVCC_ICC, ret, neg ? -1 : 1, 1);
        return;
    }

    tcg_out_cmp(s, cond, c1, c2, c2const);
    if (cond == TCG_COND_LTU) {
        if (neg) {
            /* 0 - 0 - C = -C = (C ? -1 : 0) */
            tcg_out_arithi(s, ret, TCG_REG_G0, 0, ARITH_SUBC);
        } else {
            /* 0 + 0 + C =  C = (C ? 1 : 0) */
            tcg_out_arithi(s, ret, TCG_REG_G0, 0, ARITH_ADDC);
        }
    } else {
        if (neg) {
            /* 0 + -1 + C = C - 1 = (C ? 0 : -1) */
            tcg_out_arithi(s, ret, TCG_REG_G0, -1, ARITH_ADDC);
        } else {
            /* 0 - -1 - C = 1 - C = (C ? 0 : 1) */
            tcg_out_arithi(s, ret, TCG_REG_G0, -1, ARITH_SUBC);
        }
    }
}

static void tcg_out_setcond_i64(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg c1, int32_t c2, int c2const, bool neg)
{
    int rcond;

    if (use_vis3_instructions && !neg) {
        switch (cond) {
        case TCG_COND_NE:
            if (c2 != 0) {
                break;
            }
            c2 = c1, c2const = 0, c1 = TCG_REG_G0;
            /* FALLTHRU */
        case TCG_COND_LTU:
            tcg_out_cmp(s, cond, c1, c2, c2const);
            tcg_out_arith(s, ret, TCG_REG_G0, TCG_REG_G0, ARITH_ADDXC);
            return;
        default:
            break;
        }
    }

    /* For 64-bit signed comparisons vs zero, we can avoid the compare
       if the input does not overlap the output.  */
    rcond = tcg_cond_to_rcond[cond];
    if (c2 == 0 && rcond && c1 != ret) {
        tcg_out_movi_s13(s, ret, 0);
        tcg_out_movr(s, rcond, ret, c1, neg ? -1 : 1, 1);
    } else {
        tcg_out_cmp(s, cond, c1, c2, c2const);
        tcg_out_movi_s13(s, ret, 0);
        tcg_out_movcc(s, cond, MOVCC_XCC, ret, neg ? -1 : 1, 1);
    }
}

static void tcg_out_addsub2_i32(TCGContext *s, TCGReg rl, TCGReg rh,
                                TCGReg al, TCGReg ah, int32_t bl, int blconst,
                                int32_t bh, int bhconst, int opl, int oph)
{
    TCGReg tmp = TCG_REG_T1;

    /* Note that the low parts are fully consumed before tmp is set.  */
    if (rl != ah && (bhconst || rl != bh)) {
        tmp = rl;
    }

    tcg_out_arithc(s, tmp, al, bl, blconst, opl);
    tcg_out_arithc(s, rh, ah, bh, bhconst, oph);
    tcg_out_mov(s, TCG_TYPE_I32, rl, tmp);
}

static void tcg_out_addsub2_i64(TCGContext *s, TCGReg rl, TCGReg rh,
                                TCGReg al, TCGReg ah, int32_t bl, int blconst,
                                int32_t bh, int bhconst, bool is_sub)
{
    TCGReg tmp = TCG_REG_T1;

    /* Note that the low parts are fully consumed before tmp is set.  */
    if (rl != ah && (bhconst || rl != bh)) {
        tmp = rl;
    }

    tcg_out_arithc(s, tmp, al, bl, blconst, is_sub ? ARITH_SUBCC : ARITH_ADDCC);

    if (use_vis3_instructions && !is_sub) {
        /* Note that ADDXC doesn't accept immediates.  */
        if (bhconst && bh != 0) {
           tcg_out_movi_s13(s, TCG_REG_T2, bh);
           bh = TCG_REG_T2;
        }
        tcg_out_arith(s, rh, ah, bh, ARITH_ADDXC);
    } else if (bh == TCG_REG_G0) {
	/* If we have a zero, we can perform the operation in two insns,
           with the arithmetic first, and a conditional move into place.  */
	if (rh == ah) {
            tcg_out_arithi(s, TCG_REG_T2, ah, 1,
			   is_sub ? ARITH_SUB : ARITH_ADD);
            tcg_out_movcc(s, TCG_COND_LTU, MOVCC_XCC, rh, TCG_REG_T2, 0);
	} else {
            tcg_out_arithi(s, rh, ah, 1, is_sub ? ARITH_SUB : ARITH_ADD);
	    tcg_out_movcc(s, TCG_COND_GEU, MOVCC_XCC, rh, ah, 0);
	}
    } else {
        /*
         * Otherwise adjust BH as if there is carry into T2.
         * Note that constant BH is constrained to 11 bits for the MOVCC,
         * so the adjustment fits 12 bits.
         */
        if (bhconst) {
            tcg_out_movi_s13(s, TCG_REG_T2, bh + (is_sub ? -1 : 1));
        } else {
            tcg_out_arithi(s, TCG_REG_T2, bh, 1,
                           is_sub ? ARITH_SUB : ARITH_ADD);
        }
        /* ... smoosh T2 back to original BH if carry is clear ... */
        tcg_out_movcc(s, TCG_COND_GEU, MOVCC_XCC, TCG_REG_T2, bh, bhconst);
	/* ... and finally perform the arithmetic with the new operand.  */
        tcg_out_arith(s, rh, ah, TCG_REG_T2, is_sub ? ARITH_SUB : ARITH_ADD);
    }

    tcg_out_mov(s, TCG_TYPE_I64, rl, tmp);
}

static void tcg_out_jmpl_const(TCGContext *s, const tcg_insn_unit *dest,
                               bool in_prologue, bool tail_call)
{
    uintptr_t desti = (uintptr_t)dest;

    tcg_out_movi_int(s, TCG_TYPE_PTR, TCG_REG_T1,
                     desti & ~0xfff, in_prologue, TCG_REG_T2);
    tcg_out_arithi(s, tail_call ? TCG_REG_G0 : TCG_REG_O7,
                   TCG_REG_T1, desti & 0xfff, JMPL);
}

static void tcg_out_call_nodelay(TCGContext *s, const tcg_insn_unit *dest,
                                 bool in_prologue)
{
    ptrdiff_t disp = tcg_pcrel_diff(s, dest);

    if (disp == (int32_t)disp) {
        tcg_out32(s, CALL | (uint32_t)disp >> 2);
    } else {
        tcg_out_jmpl_const(s, dest, in_prologue, false);
    }
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *dest,
                         const TCGHelperInfo *info)
{
    tcg_out_call_nodelay(s, dest, false);
    tcg_out_nop(s);
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    /* Note that the TCG memory order constants mirror the Sparc MEMBAR.  */
    tcg_out32(s, MEMBAR | (a0 & TCG_MO_ALL));
}

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int tmp_buf_size, frame_size;

    /*
     * The TCG temp buffer is at the top of the frame, immediately
     * below the frame pointer.  Use the logical (aligned) offset here;
     * the stack bias is applied in temp_allocate_frame().
     */
    tmp_buf_size = CPU_TEMP_BUF_NLONGS * (int)sizeof(long);
    tcg_set_frame(s, TCG_REG_I6, -tmp_buf_size, tmp_buf_size);

    /*
     * TCG_TARGET_CALL_STACK_OFFSET includes the stack bias, but is
     * otherwise the minimal frame usable by callees.
     */
    frame_size = TCG_TARGET_CALL_STACK_OFFSET - TCG_TARGET_STACK_BIAS;
    frame_size += TCG_STATIC_CALL_ARGS_SIZE + tmp_buf_size;
    frame_size += TCG_TARGET_STACK_ALIGN - 1;
    frame_size &= -TCG_TARGET_STACK_ALIGN;
    tcg_out32(s, SAVE | INSN_RD(TCG_REG_O6) | INSN_RS1(TCG_REG_O6) |
              INSN_IMM13(-frame_size));

#ifndef CONFIG_SOFTMMU
    if (guest_base != 0) {
        tcg_out_movi_int(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG,
                         guest_base, true, TCG_REG_T1);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    /* We choose TCG_REG_TB such that no move is required.  */
    QEMU_BUILD_BUG_ON(TCG_REG_TB != TCG_REG_I1);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TB);

    tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I1, 0, JMPL);
    /* delay slot */
    tcg_out_nop(s);

    /* Epilogue for goto_ptr.  */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
    /* delay slot */
    tcg_out_movi_s13(s, TCG_REG_O0, 0);
}

static void tcg_out_tb_start(TCGContext *s)
{
    /* nothing to do */
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = NOP;
    }
}

static const TCGLdstHelperParam ldst_helper_param = {
    .ntmp = 1, .tmp = { TCG_REG_T1 }
};

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);
    MemOp sgn;

    if (!patch_reloc(lb->label_ptr[0], R_SPARC_WDISP19,
                     (intptr_t)tcg_splitwx_to_rx(s->code_ptr), 0)) {
        return false;
    }

    /* Use inline tcg_out_ext32s; otherwise let the helper sign-extend. */
    sgn = (opc & MO_SIZE) < MO_32 ? MO_SIGN : 0;

    tcg_out_ld_helper_args(s, lb, &ldst_helper_param);
    tcg_out_call(s, qemu_ld_helpers[opc & (MO_SIZE | sgn)], NULL);
    tcg_out_ld_helper_ret(s, lb, sgn, &ldst_helper_param);

    tcg_out_bpcc0(s, COND_A, BPCC_A | BPCC_PT, 0);
    return patch_reloc(s->code_ptr - 1, R_SPARC_WDISP19,
                       (intptr_t)lb->raddr, 0);
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);

    if (!patch_reloc(lb->label_ptr[0], R_SPARC_WDISP19,
                     (intptr_t)tcg_splitwx_to_rx(s->code_ptr), 0)) {
        return false;
    }

    tcg_out_st_helper_args(s, lb, &ldst_helper_param);
    tcg_out_call(s, qemu_st_helpers[opc & MO_SIZE], NULL);

    tcg_out_bpcc0(s, COND_A, BPCC_A | BPCC_PT, 0);
    return patch_reloc(s->code_ptr - 1, R_SPARC_WDISP19,
                       (intptr_t)lb->raddr, 0);
}

typedef struct {
    TCGReg base;
    TCGReg index;
    TCGAtomAlign aa;
} HostAddress;

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return true;
}

/* We expect to use a 13-bit negative offset from ENV.  */
#define MIN_TLB_MASK_TABLE_OFS  -(1 << 12)

/*
 * For system-mode, perform the TLB load and compare.
 * For user-mode, perform any required alignment tests.
 * In both cases, return a TCGLabelQemuLdst structure if the slow path
 * is required and fill in @h with the host address for the fast path.
 */
static TCGLabelQemuLdst *prepare_host_addr(TCGContext *s, HostAddress *h,
                                           TCGReg addr_reg, MemOpIdx oi,
                                           bool is_ld)
{
    TCGType addr_type = s->addr_type;
    TCGLabelQemuLdst *ldst = NULL;
    MemOp opc = get_memop(oi);
    MemOp s_bits = opc & MO_SIZE;
    unsigned a_mask;

    /* We don't support unaligned accesses. */
    h->aa = atom_and_align_for_opc(s, opc, MO_ATOM_IFALIGN, false);
    h->aa.align = MAX(h->aa.align, s_bits);
    a_mask = (1u << h->aa.align) - 1;

#ifdef CONFIG_SOFTMMU
    int mem_index = get_mmuidx(oi);
    int fast_off = tlb_mask_table_ofs(s, mem_index);
    int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
    int table_off = fast_off + offsetof(CPUTLBDescFast, table);
    int cmp_off = is_ld ? offsetof(CPUTLBEntry, addr_read)
                        : offsetof(CPUTLBEntry, addr_write);
    int add_off = offsetof(CPUTLBEntry, addend);
    int compare_mask;
    int cc;

    /* Load tlb_mask[mmu_idx] and tlb_table[mmu_idx].  */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_T2, TCG_AREG0, mask_off);
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_T3, TCG_AREG0, table_off);

    /* Extract the page index, shifted into place for tlb index.  */
    tcg_out_arithi(s, TCG_REG_T1, addr_reg,
                   s->page_bits - CPU_TLB_ENTRY_BITS, SHIFT_SRL);
    tcg_out_arith(s, TCG_REG_T1, TCG_REG_T1, TCG_REG_T2, ARITH_AND);

    /* Add the tlb_table pointer, creating the CPUTLBEntry address into R2.  */
    tcg_out_arith(s, TCG_REG_T1, TCG_REG_T1, TCG_REG_T3, ARITH_ADD);

    /*
     * Load the tlb comparator and the addend.
     * Always load the entire 64-bit comparator for simplicity.
     * We will ignore the high bits via BPCC_ICC below.
     */
    tcg_out_ld(s, TCG_TYPE_I64, TCG_REG_T2, TCG_REG_T1, cmp_off);
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_T1, TCG_REG_T1, add_off);
    h->base = TCG_REG_T1;

    /* Mask out the page offset, except for the required alignment. */
    compare_mask = s->page_mask | a_mask;
    if (check_fit_tl(compare_mask, 13)) {
        tcg_out_arithi(s, TCG_REG_T3, addr_reg, compare_mask, ARITH_AND);
    } else {
        tcg_out_movi_s32(s, TCG_REG_T3, compare_mask);
        tcg_out_arith(s, TCG_REG_T3, addr_reg, TCG_REG_T3, ARITH_AND);
    }
    tcg_out_cmp(s, TCG_COND_NE, TCG_REG_T2, TCG_REG_T3, 0);

    ldst = new_ldst_label(s);
    ldst->is_ld = is_ld;
    ldst->oi = oi;
    ldst->addr_reg = addr_reg;
    ldst->label_ptr[0] = s->code_ptr;

    /* bne,pn %[xi]cc, label0 */
    cc = addr_type == TCG_TYPE_I32 ? BPCC_ICC : BPCC_XCC;
    tcg_out_bpcc0(s, COND_NE, BPCC_PN | cc, 0);
#else
    /*
     * If the size equals the required alignment, we can skip the test
     * and allow host SIGBUS to deliver SIGBUS to the guest.
     * Otherwise, test for at least natural alignment and defer
     * everything else to the helper functions.
     */
    if (s_bits != memop_alignment_bits(opc)) {
        tcg_debug_assert(check_fit_tl(a_mask, 13));
        tcg_out_arithi(s, TCG_REG_G0, addr_reg, a_mask, ARITH_ANDCC);

        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addr_reg = addr_reg;
        ldst->label_ptr[0] = s->code_ptr;

        /* bne,pn %icc, label0 */
        tcg_out_bpcc0(s, COND_NE, BPCC_PN | BPCC_ICC, 0);
    }
    h->base = guest_base ? TCG_GUEST_BASE_REG : TCG_REG_G0;
#endif

    /* If the guest address must be zero-extended, do in the delay slot.  */
    if (addr_type == TCG_TYPE_I32) {
        tcg_out_ext32u(s, TCG_REG_T2, addr_reg);
        h->index = TCG_REG_T2;
    } else {
        if (ldst) {
            tcg_out_nop(s);
        }
        h->index = addr_reg;
    }
    return ldst;
}

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data, TCGReg addr,
                            MemOpIdx oi, TCGType data_type)
{
    static const int ld_opc[(MO_SSIZE | MO_BSWAP) + 1] = {
        [MO_UB]   = LDUB,
        [MO_SB]   = LDSB,
        [MO_UB | MO_LE] = LDUB,
        [MO_SB | MO_LE] = LDSB,

        [MO_BEUW] = LDUH,
        [MO_BESW] = LDSH,
        [MO_BEUL] = LDUW,
        [MO_BESL] = LDSW,
        [MO_BEUQ] = LDX,
        [MO_BESQ] = LDX,

        [MO_LEUW] = LDUH_LE,
        [MO_LESW] = LDSH_LE,
        [MO_LEUL] = LDUW_LE,
        [MO_LESL] = LDSW_LE,
        [MO_LEUQ] = LDX_LE,
        [MO_LESQ] = LDX_LE,
    };

    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, true);

    tcg_out_ldst_rr(s, data, h.base, h.index,
                    ld_opc[get_memop(oi) & (MO_BSWAP | MO_SSIZE)]);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static void tcg_out_qemu_st(TCGContext *s, TCGReg data, TCGReg addr,
                            MemOpIdx oi, TCGType data_type)
{
    static const int st_opc[(MO_SIZE | MO_BSWAP) + 1] = {
        [MO_UB]   = STB,

        [MO_BEUW] = STH,
        [MO_BEUL] = STW,
        [MO_BEUQ] = STX,

        [MO_LEUW] = STH_LE,
        [MO_LEUL] = STW_LE,
        [MO_LEUQ] = STX_LE,
    };

    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr, oi, false);

    tcg_out_ldst_rr(s, data, h.base, h.index,
                    st_opc[get_memop(oi) & (MO_BSWAP | MO_SIZE)]);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static void tcg_out_exit_tb(TCGContext *s, uintptr_t a0)
{
    if (check_fit_ptr(a0, 13)) {
        tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
        tcg_out_movi_s13(s, TCG_REG_O0, a0);
        return;
    } else {
        intptr_t tb_diff = tcg_tbrel_diff(s, (void *)a0);
        if (check_fit_ptr(tb_diff, 13)) {
            tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
            /* Note that TCG_REG_TB has been unwound to O1.  */
            tcg_out_arithi(s, TCG_REG_O0, TCG_REG_O1, tb_diff, ARITH_ADD);
            return;
        }
    }
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I0, a0 & ~0x3ff);
    tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
    tcg_out_arithi(s, TCG_REG_O0, TCG_REG_O0, a0 & 0x3ff, ARITH_OR);
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    ptrdiff_t off = tcg_tbrel_diff(s, (void *)get_jmp_target_addr(s, which));

    /* Load link and indirect branch. */
    set_jmp_insn_offset(s, which);
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TB, TCG_REG_TB, off);
    tcg_out_arithi(s, TCG_REG_G0, TCG_REG_TB, 0, JMPL);
    /* delay slot */
    tcg_out_nop(s);
    set_jmp_reset_offset(s, which);

    /*
     * For the unlinked path of goto_tb, we need to reset TCG_REG_TB
     * to the beginning of this TB.
     */
    off = -tcg_current_code_size(s);
    if (check_fit_i32(off, 13)) {
        tcg_out_arithi(s, TCG_REG_TB, TCG_REG_TB, off, ARITH_ADD);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T1, off);
        tcg_out_arith(s, TCG_REG_TB, TCG_REG_TB, TCG_REG_T1, ARITH_ADD);
    }
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc, TCGType type,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    TCGArg a0, a1, a2;
    int c, c2;

    /* Hoist the loads of the most common arguments.  */
    a0 = args[0];
    a1 = args[1];
    a2 = args[2];
    c2 = const_args[2];

    switch (opc) {
    case INDEX_op_goto_ptr:
        tcg_out_arithi(s, TCG_REG_G0, a0, 0, JMPL);
        tcg_out_mov_delay(s, TCG_REG_TB, a0);
        break;
    case INDEX_op_br:
        tcg_out_bpcc(s, COND_A, BPCC_PT, arg_label(a0));
        tcg_out_nop(s);
        break;

#define OP_32_64(x)                             \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)

    OP_32_64(ld8u):
        tcg_out_ldst(s, a0, a1, a2, LDUB);
        break;
    OP_32_64(ld8s):
        tcg_out_ldst(s, a0, a1, a2, LDSB);
        break;
    OP_32_64(ld16u):
        tcg_out_ldst(s, a0, a1, a2, LDUH);
        break;
    OP_32_64(ld16s):
        tcg_out_ldst(s, a0, a1, a2, LDSH);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
        tcg_out_ldst(s, a0, a1, a2, LDUW);
        break;
    OP_32_64(st8):
        tcg_out_ldst(s, a0, a1, a2, STB);
        break;
    OP_32_64(st16):
        tcg_out_ldst(s, a0, a1, a2, STH);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_ldst(s, a0, a1, a2, STW);
        break;
    OP_32_64(add):
        c = ARITH_ADD;
        goto gen_arith;
    OP_32_64(sub):
        c = ARITH_SUB;
        goto gen_arith;
    OP_32_64(and):
        c = ARITH_AND;
        goto gen_arith;
    OP_32_64(andc):
        c = ARITH_ANDN;
        goto gen_arith;
    OP_32_64(or):
        c = ARITH_OR;
        goto gen_arith;
    OP_32_64(orc):
        c = ARITH_ORN;
        goto gen_arith;
    OP_32_64(xor):
        c = ARITH_XOR;
        goto gen_arith;
    case INDEX_op_shl_i32:
        c = SHIFT_SLL;
    do_shift32:
        /* Limit immediate shift count lest we create an illegal insn.  */
        tcg_out_arithc(s, a0, a1, a2 & 31, c2, c);
        break;
    case INDEX_op_shr_i32:
        c = SHIFT_SRL;
        goto do_shift32;
    case INDEX_op_sar_i32:
        c = SHIFT_SRA;
        goto do_shift32;
    case INDEX_op_mul_i32:
        c = ARITH_UMUL;
        goto gen_arith;

    OP_32_64(neg):
	c = ARITH_SUB;
	goto gen_arith1;
    OP_32_64(not):
	c = ARITH_ORN;
	goto gen_arith1;

    case INDEX_op_div_i32:
        tcg_out_div32(s, a0, a1, a2, c2, 0);
        break;
    case INDEX_op_divu_i32:
        tcg_out_div32(s, a0, a1, a2, c2, 1);
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond_i32(s, a2, a0, a1, const_args[1], arg_label(args[3]));
        break;
    case INDEX_op_setcond_i32:
        tcg_out_setcond_i32(s, args[3], a0, a1, a2, c2, false);
        break;
    case INDEX_op_negsetcond_i32:
        tcg_out_setcond_i32(s, args[3], a0, a1, a2, c2, true);
        break;
    case INDEX_op_movcond_i32:
        tcg_out_movcond_i32(s, args[5], a0, a1, a2, c2, args[3], const_args[3]);
        break;

    case INDEX_op_add2_i32:
        tcg_out_addsub2_i32(s, args[0], args[1], args[2], args[3],
                            args[4], const_args[4], args[5], const_args[5],
                            ARITH_ADDCC, ARITH_ADDC);
        break;
    case INDEX_op_sub2_i32:
        tcg_out_addsub2_i32(s, args[0], args[1], args[2], args[3],
                            args[4], const_args[4], args[5], const_args[5],
                            ARITH_SUBCC, ARITH_SUBC);
        break;
    case INDEX_op_mulu2_i32:
        c = ARITH_UMUL;
        goto do_mul2;
    case INDEX_op_muls2_i32:
        c = ARITH_SMUL;
    do_mul2:
        /* The 32-bit multiply insns produce a full 64-bit result. */
        tcg_out_arithc(s, a0, a2, args[3], const_args[3], c);
        tcg_out_arithi(s, a1, a0, 32, SHIFT_SRLX);
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

    case INDEX_op_ld32s_i64:
        tcg_out_ldst(s, a0, a1, a2, LDSW);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldst(s, a0, a1, a2, LDX);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldst(s, a0, a1, a2, STX);
        break;
    case INDEX_op_shl_i64:
        c = SHIFT_SLLX;
    do_shift64:
        /* Limit immediate shift count lest we create an illegal insn.  */
        tcg_out_arithc(s, a0, a1, a2 & 63, c2, c);
        break;
    case INDEX_op_shr_i64:
        c = SHIFT_SRLX;
        goto do_shift64;
    case INDEX_op_sar_i64:
        c = SHIFT_SRAX;
        goto do_shift64;
    case INDEX_op_mul_i64:
        c = ARITH_MULX;
        goto gen_arith;
    case INDEX_op_div_i64:
        c = ARITH_SDIVX;
        goto gen_arith;
    case INDEX_op_divu_i64:
        c = ARITH_UDIVX;
        goto gen_arith;

    case INDEX_op_brcond_i64:
        tcg_out_brcond_i64(s, a2, a0, a1, const_args[1], arg_label(args[3]));
        break;
    case INDEX_op_setcond_i64:
        tcg_out_setcond_i64(s, args[3], a0, a1, a2, c2, false);
        break;
    case INDEX_op_negsetcond_i64:
        tcg_out_setcond_i64(s, args[3], a0, a1, a2, c2, true);
        break;
    case INDEX_op_movcond_i64:
        tcg_out_movcond_i64(s, args[5], a0, a1, a2, c2, args[3], const_args[3]);
        break;
    case INDEX_op_add2_i64:
        tcg_out_addsub2_i64(s, args[0], args[1], args[2], args[3], args[4],
                            const_args[4], args[5], const_args[5], false);
        break;
    case INDEX_op_sub2_i64:
        tcg_out_addsub2_i64(s, args[0], args[1], args[2], args[3], args[4],
                            const_args[4], args[5], const_args[5], true);
        break;
    case INDEX_op_muluh_i64:
        tcg_out_arith(s, args[0], args[1], args[2], ARITH_UMULXHI);
        break;

    gen_arith:
        tcg_out_arithc(s, a0, a1, a2, c2, c);
        break;

    gen_arith1:
	tcg_out_arithc(s, a0, TCG_REG_G0, a1, const_args[1], c);
	break;

    case INDEX_op_mb:
        tcg_out_mb(s, a0);
        break;

    case INDEX_op_extract_i64:
        tcg_debug_assert(a2 + args[3] == 32);
        tcg_out_arithi(s, a0, a1, a2, SHIFT_SRL);
        break;
    case INDEX_op_sextract_i64:
        tcg_debug_assert(a2 + args[3] == 32);
        tcg_out_arithi(s, a0, a1, a2, SHIFT_SRA);
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
    default:
        g_assert_not_reached();
    }
}

static TCGConstraintSetIndex
tcg_target_op_def(TCGOpcode op, TCGType type, unsigned flags)
{
    switch (op) {
    case INDEX_op_goto_ptr:
        return C_O0_I1(r);

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extract_i64:
    case INDEX_op_sextract_i64:
    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        return C_O0_I2(rz, r);

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_div_i32:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i32:
    case INDEX_op_divu_i64:
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
    case INDEX_op_shl_i32:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i32:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i32:
    case INDEX_op_sar_i64:
    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
    case INDEX_op_negsetcond_i32:
    case INDEX_op_negsetcond_i64:
        return C_O1_I2(r, rz, rJ);

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return C_O0_I2(rz, rJ);
    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        return C_O1_I4(r, rz, rJ, rI, 0);
    case INDEX_op_add2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i32:
    case INDEX_op_sub2_i64:
        return C_O2_I4(r, r, rz, rz, rJ, rJ);
    case INDEX_op_mulu2_i32:
    case INDEX_op_muls2_i32:
        return C_O2_I2(r, r, rz, rJ);
    case INDEX_op_muluh_i64:
        return C_O1_I2(r, r, r);

    default:
        return C_NotImplemented;
    }
}

static void tcg_target_init(TCGContext *s)
{
    /*
     * Only probe for the platform and capabilities if we haven't already
     * determined maximum values at compile time.
     */
#ifndef use_vis3_instructions
    {
        unsigned long hwcap = qemu_getauxval(AT_HWCAP);
        use_vis3_instructions = (hwcap & HWCAP_SPARC_VIS3) != 0;
    }
#endif

    tcg_target_available_regs[TCG_TYPE_I32] = ALL_GENERAL_REGS;
    tcg_target_available_regs[TCG_TYPE_I64] = ALL_GENERAL_REGS;

    tcg_target_call_clobber_regs = 0;
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_G1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_G2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_G3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_G4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_G5);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_G6);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_G7);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O5);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O6);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_O7);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_G0); /* zero */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_G6); /* reserved for os */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_G7); /* thread pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I6); /* frame pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I7); /* return address */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_O6); /* stack pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_T1); /* for internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_T2); /* for internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_T3); /* for internal use */
}

#define ELF_HOST_MACHINE  EM_SPARCV9

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_win_save;
    uint8_t fde_ret_save[3];
} DebugFrame;

static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = -sizeof(void *) & 0x7f,
    .h.cie.return_column = 15,            /* o7 */

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, 30,                         /* DW_CFA_def_cfa i6, 2047 */
        (2047 & 0x7f) | 0x80, (2047 >> 7)
    },
    .fde_win_save = 0x2d,               /* DW_CFA_GNU_window_save */
    .fde_ret_save = { 9, 15, 31 },      /* DW_CFA_register o7, i7 */
};

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
