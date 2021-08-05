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

#include "../tcg-pool.c.inc"

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

#ifdef __arch64__
# define SPARC64 1
#else
# define SPARC64 0
#endif

#define TCG_CT_CONST_S11  0x100
#define TCG_CT_CONST_S13  0x200
#define TCG_CT_CONST_ZERO 0x400

/*
 * For softmmu, we need to avoid conflicts with the first 3
 * argument registers to perform the tlb lookup, and to call
 * the helper function.
 */
#ifdef CONFIG_SOFTMMU
#define SOFTMMU_RESERVE_REGS MAKE_64BIT_MASK(TCG_REG_O0, 3)
#else
#define SOFTMMU_RESERVE_REGS 0
#endif

/*
 * Note that sparcv8plus can only hold 64 bit quantities in %g and %o
 * registers.  These are saved manually by the kernel in full 64-bit
 * slots.  The %i and %l registers are saved by the register window
 * mechanism, which only allocates space for 32 bits.  Given that this
 * window spill/fill can happen on any signal, we must consider the
 * high bits of the %i and %l registers garbage at all times.
 */
#define ALL_GENERAL_REGS     MAKE_64BIT_MASK(0, 32)
#if SPARC64
# define ALL_GENERAL_REGS64  ALL_GENERAL_REGS
#else
# define ALL_GENERAL_REGS64  MAKE_64BIT_MASK(0, 16)
#endif
#define ALL_QLDST_REGS       (ALL_GENERAL_REGS & ~SOFTMMU_RESERVE_REGS)
#define ALL_QLDST_REGS64     (ALL_GENERAL_REGS64 & ~SOFTMMU_RESERVE_REGS)

/* Define some temporary registers.  T2 is used for constant generation.  */
#define TCG_REG_T1  TCG_REG_G1
#define TCG_REG_T2  TCG_REG_O7

#ifndef CONFIG_SOFTMMU
# define TCG_GUEST_BASE_REG TCG_REG_I5
#endif

#define TCG_REG_TB  TCG_REG_I1
#define USE_REG_TB  (sizeof(void *) > 4)

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

    TCG_REG_G2,
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

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_O0,
    TCG_REG_O1,
    TCG_REG_O2,
    TCG_REG_O3,
};

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
#if SPARC64
# define check_fit_ptr  check_fit_i64
#else
# define check_fit_ptr  check_fit_i32
#endif

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
static bool tcg_target_const_match(int64_t val, TCGType type, int ct)
{
    if (ct & TCG_CT_CONST) {
        return 1;
    }

    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S11) && check_fit_tl(val, 11)) {
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

static void tcg_out_movi_imm13(TCGContext *s, TCGReg ret, int32_t arg)
{
    tcg_out_arithi(s, ret, TCG_REG_G0, arg, ARITH_OR);
}

static void tcg_out_movi_imm32(TCGContext *s, TCGReg ret, int32_t arg)
{
    if (check_fit_i32(arg, 13)) {
        /* A 13-bit constant sign-extended to 64-bits.  */
        tcg_out_movi_imm13(s, ret, arg);
    } else {
        /* A 32-bit constant zero-extended to 64 bits.  */
        tcg_out_sethi(s, ret, arg);
        if (arg & 0x3ff) {
            tcg_out_arithi(s, ret, ret, arg & 0x3ff, ARITH_OR);
        }
    }
}

static void tcg_out_movi_int(TCGContext *s, TCGType type, TCGReg ret,
                             tcg_target_long arg, bool in_prologue,
                             TCGReg scratch)
{
    tcg_target_long hi, lo = (int32_t)arg;
    tcg_target_long test, lsb;

    /* A 32-bit constant, or 32-bit zero-extended to 64-bits.  */
    if (type == TCG_TYPE_I32 || arg == (uint32_t)arg) {
        tcg_out_movi_imm32(s, ret, arg);
        return;
    }

    /* A 13-bit constant sign-extended to 64-bits.  */
    if (check_fit_tl(arg, 13)) {
        tcg_out_movi_imm13(s, ret, arg);
        return;
    }

    /* A 13-bit constant relative to the TB.  */
    if (!in_prologue && USE_REG_TB) {
        test = tcg_tbrel_diff(s, (void *)arg);
        if (check_fit_ptr(test, 13)) {
            tcg_out_arithi(s, ret, TCG_REG_TB, test, ARITH_ADD);
            return;
        }
    }

    /* A 32-bit constant sign-extended to 64-bits.  */
    if (arg == lo) {
        tcg_out_sethi(s, ret, ~arg);
        tcg_out_arithi(s, ret, ret, (arg & 0x3ff) | -0x400, ARITH_XOR);
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
    if (!in_prologue && USE_REG_TB) {
        new_pool_label(s, arg, R_SPARC_13, s->code_ptr,
                       tcg_tbrel_diff(s, NULL));
        tcg_out32(s, LDX | INSN_RD(ret) | INSN_RS1(TCG_REG_TB));
        return;
    }

    /* A 64-bit constant decomposed into 2 32-bit pieces.  */
    if (check_fit_i32(lo, 13)) {
        hi = (arg - lo) >> 32;
        tcg_out_movi_imm32(s, ret, hi);
        tcg_out_arithi(s, ret, ret, 32, SHIFT_SLLX);
        tcg_out_arithi(s, ret, ret, lo, ARITH_ADD);
    } else {
        hi = arg >> 32;
        tcg_out_movi_imm32(s, ret, hi);
        tcg_out_movi_imm32(s, scratch, lo);
        tcg_out_arithi(s, ret, ret, 32, SHIFT_SLLX);
        tcg_out_arith(s, ret, ret, scratch, ARITH_OR);
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_debug_assert(ret != TCG_REG_T2);
    tcg_out_movi_int(s, type, ret, arg, false, TCG_REG_T2);
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

static void tcg_out_ld_ptr(TCGContext *s, TCGReg ret, const void *arg)
{
    intptr_t diff = tcg_tbrel_diff(s, arg);
    if (USE_REG_TB && check_fit_ptr(diff, 13)) {
        tcg_out_ld(s, TCG_TYPE_PTR, ret, TCG_REG_TB, diff);
        return;
    }
    tcg_out_movi(s, TCG_TYPE_PTR, ret, (uintptr_t)arg & ~0x3ff);
    tcg_out_ld(s, TCG_TYPE_PTR, ret, ret, (uintptr_t)arg & 0x3ff);
}

static void tcg_out_sety(TCGContext *s, TCGReg rs)
{
    tcg_out32(s, WRY | INSN_RS1(TCG_REG_G0) | INSN_RS2(rs));
}

static void tcg_out_rdy(TCGContext *s, TCGReg rd)
{
    tcg_out32(s, RDY | INSN_RD(rd));
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

static const uint8_t tcg_cond_to_bcond[] = {
    [TCG_COND_EQ] = COND_E,
    [TCG_COND_NE] = COND_NE,
    [TCG_COND_LT] = COND_L,
    [TCG_COND_GE] = COND_GE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_G,
    [TCG_COND_LTU] = COND_CS,
    [TCG_COND_GEU] = COND_CC,
    [TCG_COND_LEU] = COND_LEU,
    [TCG_COND_GTU] = COND_GU,
};

static const uint8_t tcg_cond_to_rcond[] = {
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

static void tcg_out_cmp(TCGContext *s, TCGReg c1, int32_t c2, int c2const)
{
    tcg_out_arithc(s, TCG_REG_G0, c1, c2, c2const, ARITH_SUBCC);
}

static void tcg_out_brcond_i32(TCGContext *s, TCGCond cond, TCGReg arg1,
                               int32_t arg2, int const_arg2, TCGLabel *l)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2);
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
    tcg_out_cmp(s, c1, c2, c2const);
    tcg_out_movcc(s, cond, MOVCC_ICC, ret, v1, v1const);
}

static void tcg_out_brcond_i64(TCGContext *s, TCGCond cond, TCGReg arg1,
                               int32_t arg2, int const_arg2, TCGLabel *l)
{
    /* For 64-bit signed comparisons vs zero, we can avoid the compare.  */
    if (arg2 == 0 && !is_unsigned_cond(cond)) {
        int off16 = 0;

        if (l->has_value) {
            off16 = INSN_OFF16(tcg_pcrel_diff(s, l->u.value_ptr));
        } else {
            tcg_out_reloc(s, s->code_ptr, R_SPARC_WDISP16, l, 0);
        }
        tcg_out32(s, INSN_OP(0) | INSN_OP2(3) | BPR_PT | INSN_RS1(arg1)
                  | INSN_COND(tcg_cond_to_rcond[cond]) | off16);
    } else {
        tcg_out_cmp(s, arg1, arg2, const_arg2);
        tcg_out_bpcc(s, tcg_cond_to_bcond[cond], BPCC_XCC | BPCC_PT, l);
    }
    tcg_out_nop(s);
}

static void tcg_out_movr(TCGContext *s, TCGCond cond, TCGReg ret, TCGReg c1,
                         int32_t v1, int v1const)
{
    tcg_out32(s, ARITH_MOVR | INSN_RD(ret) | INSN_RS1(c1)
              | (tcg_cond_to_rcond[cond] << 10)
              | (v1const ? INSN_IMM10(v1) : INSN_RS2(v1)));
}

static void tcg_out_movcond_i64(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg c1, int32_t c2, int c2const,
                                int32_t v1, int v1const)
{
    /* For 64-bit signed comparisons vs zero, we can avoid the compare.
       Note that the immediate range is one bit smaller, so we must check
       for that as well.  */
    if (c2 == 0 && !is_unsigned_cond(cond)
        && (!v1const || check_fit_i32(v1, 10))) {
        tcg_out_movr(s, cond, ret, c1, v1, v1const);
    } else {
        tcg_out_cmp(s, c1, c2, c2const);
        tcg_out_movcc(s, cond, MOVCC_XCC, ret, v1, v1const);
    }
}

static void tcg_out_setcond_i32(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg c1, int32_t c2, int c2const)
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
        tcg_out_cmp(s, c1, c2, c2const);
        tcg_out_movi_imm13(s, ret, 0);
        tcg_out_movcc(s, cond, MOVCC_ICC, ret, 1, 1);
        return;
    }

    tcg_out_cmp(s, c1, c2, c2const);
    if (cond == TCG_COND_LTU) {
        tcg_out_arithi(s, ret, TCG_REG_G0, 0, ARITH_ADDC);
    } else {
        tcg_out_arithi(s, ret, TCG_REG_G0, -1, ARITH_SUBC);
    }
}

static void tcg_out_setcond_i64(TCGContext *s, TCGCond cond, TCGReg ret,
                                TCGReg c1, int32_t c2, int c2const)
{
    if (use_vis3_instructions) {
        switch (cond) {
        case TCG_COND_NE:
            if (c2 != 0) {
                break;
            }
            c2 = c1, c2const = 0, c1 = TCG_REG_G0;
            /* FALLTHRU */
        case TCG_COND_LTU:
            tcg_out_cmp(s, c1, c2, c2const);
            tcg_out_arith(s, ret, TCG_REG_G0, TCG_REG_G0, ARITH_ADDXC);
            return;
        default:
            break;
        }
    }

    /* For 64-bit signed comparisons vs zero, we can avoid the compare
       if the input does not overlap the output.  */
    if (c2 == 0 && !is_unsigned_cond(cond) && c1 != ret) {
        tcg_out_movi_imm13(s, ret, 0);
        tcg_out_movr(s, cond, ret, c1, 1, 1);
    } else {
        tcg_out_cmp(s, c1, c2, c2const);
        tcg_out_movi_imm13(s, ret, 0);
        tcg_out_movcc(s, cond, MOVCC_XCC, ret, 1, 1);
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
           tcg_out_movi_imm13(s, TCG_REG_T2, bh);
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
            tcg_out_movi_imm13(s, TCG_REG_T2, bh + (is_sub ? -1 : 1));
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

    /* Be careful not to clobber %o7 for a tail call. */
    tcg_out_movi_int(s, TCG_TYPE_PTR, TCG_REG_T1,
                     desti & ~0xfff, in_prologue,
                     tail_call ? TCG_REG_G2 : TCG_REG_O7);
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

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *dest)
{
    tcg_out_call_nodelay(s, dest, false);
    tcg_out_nop(s);
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    /* Note that the TCG memory order constants mirror the Sparc MEMBAR.  */
    tcg_out32(s, MEMBAR | (a0 & TCG_MO_ALL));
}

#ifdef CONFIG_SOFTMMU
static const tcg_insn_unit *qemu_ld_trampoline[(MO_SSIZE | MO_BSWAP) + 1];
static const tcg_insn_unit *qemu_st_trampoline[(MO_SIZE | MO_BSWAP) + 1];

static void emit_extend(TCGContext *s, TCGReg r, int op)
{
    /* Emit zero extend of 8, 16 or 32 bit data as
     * required by the MO_* value op; do nothing for 64 bit.
     */
    switch (op & MO_SIZE) {
    case MO_8:
        tcg_out_arithi(s, r, r, 0xff, ARITH_AND);
        break;
    case MO_16:
        tcg_out_arithi(s, r, r, 16, SHIFT_SLL);
        tcg_out_arithi(s, r, r, 16, SHIFT_SRL);
        break;
    case MO_32:
        if (SPARC64) {
            tcg_out_arith(s, r, r, 0, SHIFT_SRL);
        }
        break;
    case MO_64:
        break;
    }
}

static void build_trampolines(TCGContext *s)
{
    static void * const qemu_ld_helpers[] = {
        [MO_UB]   = helper_ret_ldub_mmu,
        [MO_SB]   = helper_ret_ldsb_mmu,
        [MO_LEUW] = helper_le_lduw_mmu,
        [MO_LESW] = helper_le_ldsw_mmu,
        [MO_LEUL] = helper_le_ldul_mmu,
        [MO_LEUQ] = helper_le_ldq_mmu,
        [MO_BEUW] = helper_be_lduw_mmu,
        [MO_BESW] = helper_be_ldsw_mmu,
        [MO_BEUL] = helper_be_ldul_mmu,
        [MO_BEUQ] = helper_be_ldq_mmu,
    };
    static void * const qemu_st_helpers[] = {
        [MO_UB]   = helper_ret_stb_mmu,
        [MO_LEUW] = helper_le_stw_mmu,
        [MO_LEUL] = helper_le_stl_mmu,
        [MO_LEUQ] = helper_le_stq_mmu,
        [MO_BEUW] = helper_be_stw_mmu,
        [MO_BEUL] = helper_be_stl_mmu,
        [MO_BEUQ] = helper_be_stq_mmu,
    };

    int i;
    TCGReg ra;

    for (i = 0; i < ARRAY_SIZE(qemu_ld_helpers); ++i) {
        if (qemu_ld_helpers[i] == NULL) {
            continue;
        }

        /* May as well align the trampoline.  */
        while ((uintptr_t)s->code_ptr & 15) {
            tcg_out_nop(s);
        }
        qemu_ld_trampoline[i] = tcg_splitwx_to_rx(s->code_ptr);

        if (SPARC64 || TARGET_LONG_BITS == 32) {
            ra = TCG_REG_O3;
        } else {
            /* Install the high part of the address.  */
            tcg_out_arithi(s, TCG_REG_O1, TCG_REG_O2, 32, SHIFT_SRLX);
            ra = TCG_REG_O4;
        }

        /* Set the retaddr operand.  */
        tcg_out_mov(s, TCG_TYPE_PTR, ra, TCG_REG_O7);
        /* Tail call.  */
        tcg_out_jmpl_const(s, qemu_ld_helpers[i], true, true);
        /* delay slot -- set the env argument */
        tcg_out_mov_delay(s, TCG_REG_O0, TCG_AREG0);
    }

    for (i = 0; i < ARRAY_SIZE(qemu_st_helpers); ++i) {
        if (qemu_st_helpers[i] == NULL) {
            continue;
        }

        /* May as well align the trampoline.  */
        while ((uintptr_t)s->code_ptr & 15) {
            tcg_out_nop(s);
        }
        qemu_st_trampoline[i] = tcg_splitwx_to_rx(s->code_ptr);

        if (SPARC64) {
            emit_extend(s, TCG_REG_O2, i);
            ra = TCG_REG_O4;
        } else {
            ra = TCG_REG_O1;
            if (TARGET_LONG_BITS == 64) {
                /* Install the high part of the address.  */
                tcg_out_arithi(s, ra, ra + 1, 32, SHIFT_SRLX);
                ra += 2;
            } else {
                ra += 1;
            }
            if ((i & MO_SIZE) == MO_64) {
                /* Install the high part of the data.  */
                tcg_out_arithi(s, ra, ra + 1, 32, SHIFT_SRLX);
                ra += 2;
            } else {
                emit_extend(s, ra, i);
                ra += 1;
            }
            /* Skip the oi argument.  */
            ra += 1;
        }
                
        /* Set the retaddr operand.  */
        if (ra >= TCG_REG_O6) {
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_O7, TCG_REG_CALL_STACK,
                       TCG_TARGET_CALL_STACK_OFFSET);
        } else {
            tcg_out_mov(s, TCG_TYPE_PTR, ra, TCG_REG_O7);
        }

        /* Tail call.  */
        tcg_out_jmpl_const(s, qemu_st_helpers[i], true, true);
        /* delay slot -- set the env argument */
        tcg_out_mov_delay(s, TCG_REG_O0, TCG_AREG0);
    }
}
#else
static const tcg_insn_unit *qemu_unalign_ld_trampoline;
static const tcg_insn_unit *qemu_unalign_st_trampoline;

static void build_trampolines(TCGContext *s)
{
    for (int ld = 0; ld < 2; ++ld) {
        void *helper;

        while ((uintptr_t)s->code_ptr & 15) {
            tcg_out_nop(s);
        }

        if (ld) {
            helper = helper_unaligned_ld;
            qemu_unalign_ld_trampoline = tcg_splitwx_to_rx(s->code_ptr);
        } else {
            helper = helper_unaligned_st;
            qemu_unalign_st_trampoline = tcg_splitwx_to_rx(s->code_ptr);
        }

        if (!SPARC64 && TARGET_LONG_BITS == 64) {
            /* Install the high part of the address.  */
            tcg_out_arithi(s, TCG_REG_O1, TCG_REG_O2, 32, SHIFT_SRLX);
        }

        /* Tail call.  */
        tcg_out_jmpl_const(s, helper, true, true);
        /* delay slot -- set the env argument */
        tcg_out_mov_delay(s, TCG_REG_O0, TCG_AREG0);
    }
}
#endif

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
    if (USE_REG_TB) {
        QEMU_BUILD_BUG_ON(TCG_REG_TB != TCG_REG_I1);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_TB);
    }

    tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I1, 0, JMPL);
    /* delay slot */
    tcg_out_nop(s);

    /* Epilogue for goto_ptr.  */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
    /* delay slot */
    tcg_out_movi_imm13(s, TCG_REG_O0, 0);

    build_trampolines(s);
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = NOP;
    }
}

#if defined(CONFIG_SOFTMMU)

/* We expect to use a 13-bit negative offset from ENV.  */
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -(1 << 12));

/* Perform the TLB load and compare.

   Inputs:
   ADDRLO and ADDRHI contain the possible two parts of the address.

   MEM_INDEX and S_BITS are the memory context and log2 size of the load.

   WHICH is the offset into the CPUTLBEntry structure of the slot to read.
   This should be offsetof addr_read or addr_write.

   The result of the TLB comparison is in %[ix]cc.  The sanitized address
   is in the returned register, maybe %o0.  The TLB addend is in %o1.  */

static TCGReg tcg_out_tlb_load(TCGContext *s, TCGReg addr, int mem_index,
                               MemOp opc, int which)
{
    int fast_off = TLB_MASK_TABLE_OFS(mem_index);
    int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
    int table_off = fast_off + offsetof(CPUTLBDescFast, table);
    const TCGReg r0 = TCG_REG_O0;
    const TCGReg r1 = TCG_REG_O1;
    const TCGReg r2 = TCG_REG_O2;
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_bits = get_alignment_bits(opc);
    tcg_target_long compare_mask;

    /* Load tlb_mask[mmu_idx] and tlb_table[mmu_idx].  */
    tcg_out_ld(s, TCG_TYPE_PTR, r0, TCG_AREG0, mask_off);
    tcg_out_ld(s, TCG_TYPE_PTR, r1, TCG_AREG0, table_off);

    /* Extract the page index, shifted into place for tlb index.  */
    tcg_out_arithi(s, r2, addr, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS,
                   SHIFT_SRL);
    tcg_out_arith(s, r2, r2, r0, ARITH_AND);

    /* Add the tlb_table pointer, creating the CPUTLBEntry address into R2.  */
    tcg_out_arith(s, r2, r2, r1, ARITH_ADD);

    /* Load the tlb comparator and the addend.  */
    tcg_out_ld(s, TCG_TYPE_TL, r0, r2, which);
    tcg_out_ld(s, TCG_TYPE_PTR, r1, r2, offsetof(CPUTLBEntry, addend));

    /* Mask out the page offset, except for the required alignment.
       We don't support unaligned accesses.  */
    if (a_bits < s_bits) {
        a_bits = s_bits;
    }
    compare_mask = (tcg_target_ulong)TARGET_PAGE_MASK | ((1 << a_bits) - 1);
    if (check_fit_tl(compare_mask, 13)) {
        tcg_out_arithi(s, r2, addr, compare_mask, ARITH_AND);
    } else {
        tcg_out_movi(s, TCG_TYPE_TL, r2, compare_mask);
        tcg_out_arith(s, r2, addr, r2, ARITH_AND);
    }
    tcg_out_cmp(s, r0, r2, 0);

    /* If the guest address must be zero-extended, do so now.  */
    if (SPARC64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, r0, addr, 0, SHIFT_SRL);
        return r0;
    }
    return addr;
}
#endif /* CONFIG_SOFTMMU */

static const int qemu_ld_opc[(MO_SSIZE | MO_BSWAP) + 1] = {
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

static const int qemu_st_opc[(MO_SIZE | MO_BSWAP) + 1] = {
    [MO_UB]   = STB,

    [MO_BEUW] = STH,
    [MO_BEUL] = STW,
    [MO_BEUQ] = STX,

    [MO_LEUW] = STH_LE,
    [MO_LEUL] = STW_LE,
    [MO_LEUQ] = STX_LE,
};

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data, TCGReg addr,
                            MemOpIdx oi, bool is_64)
{
    MemOp memop = get_memop(oi);
    tcg_insn_unit *label_ptr;

#ifdef CONFIG_SOFTMMU
    unsigned memi = get_mmuidx(oi);
    TCGReg addrz, param;
    const tcg_insn_unit *func;

    addrz = tcg_out_tlb_load(s, addr, memi, memop,
                             offsetof(CPUTLBEntry, addr_read));

    /* The fast path is exactly one insn.  Thus we can perform the
       entire TLB Hit in the (annulled) delay slot of the branch
       over the TLB Miss case.  */

    /* beq,a,pt %[xi]cc, label0 */
    label_ptr = s->code_ptr;
    tcg_out_bpcc0(s, COND_E, BPCC_A | BPCC_PT
                  | (TARGET_LONG_BITS == 64 ? BPCC_XCC : BPCC_ICC), 0);
    /* delay slot */
    tcg_out_ldst_rr(s, data, addrz, TCG_REG_O1,
                    qemu_ld_opc[memop & (MO_BSWAP | MO_SSIZE)]);

    /* TLB Miss.  */

    param = TCG_REG_O1;
    if (!SPARC64 && TARGET_LONG_BITS == 64) {
        /* Skip the high-part; we'll perform the extract in the trampoline.  */
        param++;
    }
    tcg_out_mov(s, TCG_TYPE_REG, param++, addrz);

    /* We use the helpers to extend SB and SW data, leaving the case
       of SL needing explicit extending below.  */
    if ((memop & MO_SSIZE) == MO_SL) {
        func = qemu_ld_trampoline[memop & (MO_BSWAP | MO_SIZE)];
    } else {
        func = qemu_ld_trampoline[memop & (MO_BSWAP | MO_SSIZE)];
    }
    tcg_debug_assert(func != NULL);
    tcg_out_call_nodelay(s, func, false);
    /* delay slot */
    tcg_out_movi(s, TCG_TYPE_I32, param, oi);

    /* Recall that all of the helpers return 64-bit results.
       Which complicates things for sparcv8plus.  */
    if (SPARC64) {
        /* We let the helper sign-extend SB and SW, but leave SL for here.  */
        if (is_64 && (memop & MO_SSIZE) == MO_SL) {
            tcg_out_arithi(s, data, TCG_REG_O0, 0, SHIFT_SRA);
        } else {
            tcg_out_mov(s, TCG_TYPE_REG, data, TCG_REG_O0);
        }
    } else {
        if ((memop & MO_SIZE) == MO_64) {
            tcg_out_arithi(s, TCG_REG_O0, TCG_REG_O0, 32, SHIFT_SLLX);
            tcg_out_arithi(s, TCG_REG_O1, TCG_REG_O1, 0, SHIFT_SRL);
            tcg_out_arith(s, data, TCG_REG_O0, TCG_REG_O1, ARITH_OR);
        } else if (is_64) {
            /* Re-extend from 32-bit rather than reassembling when we
               know the high register must be an extension.  */
            tcg_out_arithi(s, data, TCG_REG_O1, 0,
                           memop & MO_SIGN ? SHIFT_SRA : SHIFT_SRL);
        } else {
            tcg_out_mov(s, TCG_TYPE_I32, data, TCG_REG_O1);
        }
    }

    *label_ptr |= INSN_OFF19(tcg_ptr_byte_diff(s->code_ptr, label_ptr));
#else
    TCGReg index = (guest_base ? TCG_GUEST_BASE_REG : TCG_REG_G0);
    unsigned a_bits = get_alignment_bits(memop);
    unsigned s_bits = memop & MO_SIZE;
    unsigned t_bits;

    if (SPARC64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, TCG_REG_T1, addr, 0, SHIFT_SRL);
        addr = TCG_REG_T1;
    }

    /*
     * Normal case: alignment equal to access size.
     */
    if (a_bits == s_bits) {
        tcg_out_ldst_rr(s, data, addr, index,
                        qemu_ld_opc[memop & (MO_BSWAP | MO_SSIZE)]);
        return;
    }

    /*
     * Test for at least natural alignment, and assume most accesses
     * will be aligned -- perform a straight load in the delay slot.
     * This is required to preserve atomicity for aligned accesses.
     */
    t_bits = MAX(a_bits, s_bits);
    tcg_debug_assert(t_bits < 13);
    tcg_out_arithi(s, TCG_REG_G0, addr, (1u << t_bits) - 1, ARITH_ANDCC);

    /* beq,a,pt %icc, label */
    label_ptr = s->code_ptr;
    tcg_out_bpcc0(s, COND_E, BPCC_A | BPCC_PT | BPCC_ICC, 0);
    /* delay slot */
    tcg_out_ldst_rr(s, data, addr, index,
                    qemu_ld_opc[memop & (MO_BSWAP | MO_SSIZE)]);

    if (a_bits >= s_bits) {
        /*
         * Overalignment: A successful alignment test will perform the memory
         * operation in the delay slot, and failure need only invoke the
         * handler for SIGBUS.
         */
        TCGReg arg_low = TCG_REG_O1 + (!SPARC64 && TARGET_LONG_BITS == 64);
        tcg_out_call_nodelay(s, qemu_unalign_ld_trampoline, false);
        /* delay slot -- move to low part of argument reg */
        tcg_out_mov_delay(s, arg_low, addr);
    } else {
        /* Underalignment: load by pieces of minimum alignment. */
        int ld_opc, a_size, s_size, i;

        /*
         * Force full address into T1 early; avoids problems with
         * overlap between @addr and @data.
         */
        tcg_out_arith(s, TCG_REG_T1, addr, index, ARITH_ADD);

        a_size = 1 << a_bits;
        s_size = 1 << s_bits;
        if ((memop & MO_BSWAP) == MO_BE) {
            ld_opc = qemu_ld_opc[a_bits | MO_BE | (memop & MO_SIGN)];
            tcg_out_ldst(s, data, TCG_REG_T1, 0, ld_opc);
            ld_opc = qemu_ld_opc[a_bits | MO_BE];
            for (i = a_size; i < s_size; i += a_size) {
                tcg_out_ldst(s, TCG_REG_T2, TCG_REG_T1, i, ld_opc);
                tcg_out_arithi(s, data, data, a_size, SHIFT_SLLX);
                tcg_out_arith(s, data, data, TCG_REG_T2, ARITH_OR);
            }
        } else if (a_bits == 0) {
            ld_opc = LDUB;
            tcg_out_ldst(s, data, TCG_REG_T1, 0, ld_opc);
            for (i = a_size; i < s_size; i += a_size) {
                if ((memop & MO_SIGN) && i == s_size - a_size) {
                    ld_opc = LDSB;
                }
                tcg_out_ldst(s, TCG_REG_T2, TCG_REG_T1, i, ld_opc);
                tcg_out_arithi(s, TCG_REG_T2, TCG_REG_T2, i * 8, SHIFT_SLLX);
                tcg_out_arith(s, data, data, TCG_REG_T2, ARITH_OR);
            }
        } else {
            ld_opc = qemu_ld_opc[a_bits | MO_LE];
            tcg_out_ldst_rr(s, data, TCG_REG_T1, TCG_REG_G0, ld_opc);
            for (i = a_size; i < s_size; i += a_size) {
                tcg_out_arithi(s, TCG_REG_T1, TCG_REG_T1, a_size, ARITH_ADD);
                if ((memop & MO_SIGN) && i == s_size - a_size) {
                    ld_opc = qemu_ld_opc[a_bits | MO_LE | MO_SIGN];
                }
                tcg_out_ldst_rr(s, TCG_REG_T2, TCG_REG_T1, TCG_REG_G0, ld_opc);
                tcg_out_arithi(s, TCG_REG_T2, TCG_REG_T2, i * 8, SHIFT_SLLX);
                tcg_out_arith(s, data, data, TCG_REG_T2, ARITH_OR);
            }
        }
    }

    *label_ptr |= INSN_OFF19(tcg_ptr_byte_diff(s->code_ptr, label_ptr));
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_qemu_st(TCGContext *s, TCGReg data, TCGReg addr,
                            MemOpIdx oi)
{
    MemOp memop = get_memop(oi);
    tcg_insn_unit *label_ptr;

#ifdef CONFIG_SOFTMMU
    unsigned memi = get_mmuidx(oi);
    TCGReg addrz, param;
    const tcg_insn_unit *func;

    addrz = tcg_out_tlb_load(s, addr, memi, memop,
                             offsetof(CPUTLBEntry, addr_write));

    /* The fast path is exactly one insn.  Thus we can perform the entire
       TLB Hit in the (annulled) delay slot of the branch over TLB Miss.  */
    /* beq,a,pt %[xi]cc, label0 */
    label_ptr = s->code_ptr;
    tcg_out_bpcc0(s, COND_E, BPCC_A | BPCC_PT
                  | (TARGET_LONG_BITS == 64 ? BPCC_XCC : BPCC_ICC), 0);
    /* delay slot */
    tcg_out_ldst_rr(s, data, addrz, TCG_REG_O1,
                    qemu_st_opc[memop & (MO_BSWAP | MO_SIZE)]);

    /* TLB Miss.  */

    param = TCG_REG_O1;
    if (!SPARC64 && TARGET_LONG_BITS == 64) {
        /* Skip the high-part; we'll perform the extract in the trampoline.  */
        param++;
    }
    tcg_out_mov(s, TCG_TYPE_REG, param++, addrz);
    if (!SPARC64 && (memop & MO_SIZE) == MO_64) {
        /* Skip the high-part; we'll perform the extract in the trampoline.  */
        param++;
    }
    tcg_out_mov(s, TCG_TYPE_REG, param++, data);

    func = qemu_st_trampoline[memop & (MO_BSWAP | MO_SIZE)];
    tcg_debug_assert(func != NULL);
    tcg_out_call_nodelay(s, func, false);
    /* delay slot */
    tcg_out_movi(s, TCG_TYPE_I32, param, oi);

    *label_ptr |= INSN_OFF19(tcg_ptr_byte_diff(s->code_ptr, label_ptr));
#else
    TCGReg index = (guest_base ? TCG_GUEST_BASE_REG : TCG_REG_G0);
    unsigned a_bits = get_alignment_bits(memop);
    unsigned s_bits = memop & MO_SIZE;
    unsigned t_bits;

    if (SPARC64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, TCG_REG_T1, addr, 0, SHIFT_SRL);
        addr = TCG_REG_T1;
    }

    /*
     * Normal case: alignment equal to access size.
     */
    if (a_bits == s_bits) {
        tcg_out_ldst_rr(s, data, addr, index,
                        qemu_st_opc[memop & (MO_BSWAP | MO_SIZE)]);
        return;
    }

    /*
     * Test for at least natural alignment, and assume most accesses
     * will be aligned -- perform a straight store in the delay slot.
     * This is required to preserve atomicity for aligned accesses.
     */
    t_bits = MAX(a_bits, s_bits);
    tcg_debug_assert(t_bits < 13);
    tcg_out_arithi(s, TCG_REG_G0, addr, (1u << t_bits) - 1, ARITH_ANDCC);

    /* beq,a,pt %icc, label */
    label_ptr = s->code_ptr;
    tcg_out_bpcc0(s, COND_E, BPCC_A | BPCC_PT | BPCC_ICC, 0);
    /* delay slot */
    tcg_out_ldst_rr(s, data, addr, index,
                    qemu_st_opc[memop & (MO_BSWAP | MO_SIZE)]);

    if (a_bits >= s_bits) {
        /*
         * Overalignment: A successful alignment test will perform the memory
         * operation in the delay slot, and failure need only invoke the
         * handler for SIGBUS.
         */
        TCGReg arg_low = TCG_REG_O1 + (!SPARC64 && TARGET_LONG_BITS == 64);
        tcg_out_call_nodelay(s, qemu_unalign_st_trampoline, false);
        /* delay slot -- move to low part of argument reg */
        tcg_out_mov_delay(s, arg_low, addr);
    } else {
        /* Underalignment: store by pieces of minimum alignment. */
        int st_opc, a_size, s_size, i;

        /*
         * Force full address into T1 early; avoids problems with
         * overlap between @addr and @data.
         */
        tcg_out_arith(s, TCG_REG_T1, addr, index, ARITH_ADD);

        a_size = 1 << a_bits;
        s_size = 1 << s_bits;
        if ((memop & MO_BSWAP) == MO_BE) {
            st_opc = qemu_st_opc[a_bits | MO_BE];
            for (i = 0; i < s_size; i += a_size) {
                TCGReg d = data;
                int shift = (s_size - a_size - i) * 8;
                if (shift) {
                    d = TCG_REG_T2;
                    tcg_out_arithi(s, d, data, shift, SHIFT_SRLX);
                }
                tcg_out_ldst(s, d, TCG_REG_T1, i, st_opc);
            }
        } else if (a_bits == 0) {
            tcg_out_ldst(s, data, TCG_REG_T1, 0, STB);
            for (i = 1; i < s_size; i++) {
                tcg_out_arithi(s, TCG_REG_T2, data, i * 8, SHIFT_SRLX);
                tcg_out_ldst(s, TCG_REG_T2, TCG_REG_T1, i, STB);
            }
        } else {
            /* Note that ST*A with immediate asi must use indexed address. */
            st_opc = qemu_st_opc[a_bits + MO_LE];
            tcg_out_ldst_rr(s, data, TCG_REG_T1, TCG_REG_G0, st_opc);
            for (i = a_size; i < s_size; i += a_size) {
                tcg_out_arithi(s, TCG_REG_T2, data, i * 8, SHIFT_SRLX);
                tcg_out_arithi(s, TCG_REG_T1, TCG_REG_T1, a_size, ARITH_ADD);
                tcg_out_ldst_rr(s, TCG_REG_T2, TCG_REG_T1, TCG_REG_G0, st_opc);
            }
        }
    }

    *label_ptr |= INSN_OFF19(tcg_ptr_byte_diff(s->code_ptr, label_ptr));
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
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
    case INDEX_op_exit_tb:
        if (check_fit_ptr(a0, 13)) {
            tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
            tcg_out_movi_imm13(s, TCG_REG_O0, a0);
            break;
        } else if (USE_REG_TB) {
            intptr_t tb_diff = tcg_tbrel_diff(s, (void *)a0);
            if (check_fit_ptr(tb_diff, 13)) {
                tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
                /* Note that TCG_REG_TB has been unwound to O1.  */
                tcg_out_arithi(s, TCG_REG_O0, TCG_REG_O1, tb_diff, ARITH_ADD);
                break;
            }
        }
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I0, a0 & ~0x3ff);
        tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
        tcg_out_arithi(s, TCG_REG_O0, TCG_REG_O0, a0 & 0x3ff, ARITH_OR);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_insn_offset) {
            /* direct jump method */
            if (USE_REG_TB) {
                /* make sure the patch is 8-byte aligned.  */
                if ((intptr_t)s->code_ptr & 4) {
                    tcg_out_nop(s);
                }
                s->tb_jmp_insn_offset[a0] = tcg_current_code_size(s);
                tcg_out_sethi(s, TCG_REG_T1, 0);
                tcg_out_arithi(s, TCG_REG_T1, TCG_REG_T1, 0, ARITH_OR);
                tcg_out_arith(s, TCG_REG_G0, TCG_REG_TB, TCG_REG_T1, JMPL);
                tcg_out_arith(s, TCG_REG_TB, TCG_REG_TB, TCG_REG_T1, ARITH_ADD);
            } else {
                s->tb_jmp_insn_offset[a0] = tcg_current_code_size(s);
                tcg_out32(s, CALL);
                tcg_out_nop(s);
            }
        } else {
            /* indirect jump method */
            tcg_out_ld_ptr(s, TCG_REG_TB, s->tb_jmp_target_addr + a0);
            tcg_out_arithi(s, TCG_REG_G0, TCG_REG_TB, 0, JMPL);
            tcg_out_nop(s);
        }
        set_jmp_reset_offset(s, a0);

        /* For the unlinked path of goto_tb, we need to reset
           TCG_REG_TB to the beginning of this TB.  */
        if (USE_REG_TB) {
            c = -tcg_current_code_size(s);
            if (check_fit_i32(c, 13)) {
                tcg_out_arithi(s, TCG_REG_TB, TCG_REG_TB, c, ARITH_ADD);
            } else {
                tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T1, c);
                tcg_out_arith(s, TCG_REG_TB, TCG_REG_TB,
                              TCG_REG_T1, ARITH_ADD);
            }
        }
        break;
    case INDEX_op_goto_ptr:
        tcg_out_arithi(s, TCG_REG_G0, a0, 0, JMPL);
        if (USE_REG_TB) {
            tcg_out_mov_delay(s, TCG_REG_TB, a0);
        } else {
            tcg_out_nop(s);
        }
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
        tcg_out_setcond_i32(s, args[3], a0, a1, a2, c2);
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
        /* The 32-bit multiply insns produce a full 64-bit result.  If the
           destination register can hold it, we can avoid the slower RDY.  */
        tcg_out_arithc(s, a0, a2, args[3], const_args[3], c);
        if (SPARC64 || a0 <= TCG_REG_O7) {
            tcg_out_arithi(s, a1, a0, 32, SHIFT_SRLX);
        } else {
            tcg_out_rdy(s, a1);
        }
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, a0, a1, a2, false);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, a0, a1, a2, true);
        break;
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, a0, a1, a2);
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
    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        tcg_out_arithi(s, a0, a1, 0, SHIFT_SRA);
        break;
    case INDEX_op_extu_i32_i64:
    case INDEX_op_ext32u_i64:
        tcg_out_arithi(s, a0, a1, 0, SHIFT_SRL);
        break;
    case INDEX_op_extrl_i64_i32:
        tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
        break;
    case INDEX_op_extrh_i64_i32:
        tcg_out_arithi(s, a0, a1, 32, SHIFT_SRLX);
        break;

    case INDEX_op_brcond_i64:
        tcg_out_brcond_i64(s, a2, a0, a1, const_args[1], arg_label(args[3]));
        break;
    case INDEX_op_setcond_i64:
        tcg_out_setcond_i64(s, args[3], a0, a1, a2, c2);
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

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    default:
        tcg_abort();
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
    case INDEX_op_neg_i32:
    case INDEX_op_not_i32:
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
        return C_O0_I2(rZ, r);

    case INDEX_op_add_i32:
    case INDEX_op_mul_i32:
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_sub_i32:
    case INDEX_op_and_i32:
    case INDEX_op_andc_i32:
    case INDEX_op_or_i32:
    case INDEX_op_orc_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_setcond_i32:
        return C_O1_I2(r, rZ, rJ);

    case INDEX_op_brcond_i32:
        return C_O0_I2(rZ, rJ);
    case INDEX_op_movcond_i32:
        return C_O1_I4(r, rZ, rJ, rI, 0);
    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        return C_O2_I4(r, r, rZ, rZ, rJ, rJ);
    case INDEX_op_mulu2_i32:
    case INDEX_op_muls2_i32:
        return C_O2_I2(r, r, rZ, rJ);

    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
        return C_O1_I1(R, r);

    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return C_O0_I2(RZ, r);

    case INDEX_op_add_i64:
    case INDEX_op_mul_i64:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
    case INDEX_op_sub_i64:
    case INDEX_op_and_i64:
    case INDEX_op_andc_i64:
    case INDEX_op_or_i64:
    case INDEX_op_orc_i64:
    case INDEX_op_xor_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_setcond_i64:
        return C_O1_I2(R, RZ, RJ);

    case INDEX_op_neg_i64:
    case INDEX_op_not_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
        return C_O1_I1(R, R);

    case INDEX_op_extrl_i64_i32:
    case INDEX_op_extrh_i64_i32:
        return C_O1_I1(r, R);

    case INDEX_op_brcond_i64:
        return C_O0_I2(RZ, RJ);
    case INDEX_op_movcond_i64:
        return C_O1_I4(R, RZ, RJ, RI, 0);
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i64:
        return C_O2_I4(R, R, RZ, RZ, RJ, RI);
    case INDEX_op_muluh_i64:
        return C_O1_I2(R, R, R);

    case INDEX_op_qemu_ld_i32:
        return C_O1_I1(r, A);
    case INDEX_op_qemu_ld_i64:
        return C_O1_I1(R, A);
    case INDEX_op_qemu_st_i32:
        return C_O0_I2(sZ, A);
    case INDEX_op_qemu_st_i64:
        return C_O0_I2(SZ, A);

    default:
        g_assert_not_reached();
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
    tcg_target_available_regs[TCG_TYPE_I64] = ALL_GENERAL_REGS64;

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
}

#if SPARC64
# define ELF_HOST_MACHINE  EM_SPARCV9
#else
# define ELF_HOST_MACHINE  EM_SPARC32PLUS
# define ELF_HOST_FLAGS    EF_SPARC_32PLUS
#endif

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[SPARC64 ? 4 : 2];
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
#if SPARC64
        12, 30,                         /* DW_CFA_def_cfa i6, 2047 */
        (2047 & 0x7f) | 0x80, (2047 >> 7)
#else
        13, 30                          /* DW_CFA_def_cfa_register i6 */
#endif
    },
    .fde_win_save = 0x2d,               /* DW_CFA_GNU_window_save */
    .fde_ret_save = { 9, 15, 31 },      /* DW_CFA_register o7, i7 */
};

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

void tb_target_set_jmp_target(uintptr_t tc_ptr, uintptr_t jmp_rx,
                              uintptr_t jmp_rw, uintptr_t addr)
{
    intptr_t tb_disp = addr - tc_ptr;
    intptr_t br_disp = addr - jmp_rx;
    tcg_insn_unit i1, i2;

    /* We can reach the entire address space for ILP32.
       For LP64, the code_gen_buffer can't be larger than 2GB.  */
    tcg_debug_assert(tb_disp == (int32_t)tb_disp);
    tcg_debug_assert(br_disp == (int32_t)br_disp);

    if (!USE_REG_TB) {
        qatomic_set((uint32_t *)jmp_rw,
		    deposit32(CALL, 0, 30, br_disp >> 2));
        flush_idcache_range(jmp_rx, jmp_rw, 4);
        return;
    }

    /* This does not exercise the range of the branch, but we do
       still need to be able to load the new value of TCG_REG_TB.
       But this does still happen quite often.  */
    if (check_fit_ptr(tb_disp, 13)) {
        /* ba,pt %icc, addr */
        i1 = (INSN_OP(0) | INSN_OP2(1) | INSN_COND(COND_A)
              | BPCC_ICC | BPCC_PT | INSN_OFF19(br_disp));
        i2 = (ARITH_ADD | INSN_RD(TCG_REG_TB) | INSN_RS1(TCG_REG_TB)
              | INSN_IMM13(tb_disp));
    } else if (tb_disp >= 0) {
        i1 = SETHI | INSN_RD(TCG_REG_T1) | ((tb_disp & 0xfffffc00) >> 10);
        i2 = (ARITH_OR | INSN_RD(TCG_REG_T1) | INSN_RS1(TCG_REG_T1)
              | INSN_IMM13(tb_disp & 0x3ff));
    } else {
        i1 = SETHI | INSN_RD(TCG_REG_T1) | ((~tb_disp & 0xfffffc00) >> 10);
        i2 = (ARITH_XOR | INSN_RD(TCG_REG_T1) | INSN_RS1(TCG_REG_T1)
              | INSN_IMM13((tb_disp & 0x3ff) | -0x400));
    }

    qatomic_set((uint64_t *)jmp_rw, deposit64(i2, 32, 32, i1));
    flush_idcache_range(jmp_rx, jmp_rw, 8);
}
