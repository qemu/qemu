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

#include "tcg-be-null.h"

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

/* Note that sparcv8plus can only hold 64 bit quantities in %g and %o
   registers.  These are saved manually by the kernel in full 64-bit
   slots.  The %i and %l registers are saved by the register window
   mechanism, which only allocates space for 32 bits.  Given that this
   window spill/fill can happen on any signal, we must consider the
   high bits of the %i and %l registers garbage at all times.  */
#if SPARC64
# define ALL_64  0xffffffffu
#else
# define ALL_64  0xffffu
#endif

/* Define some temporary registers.  T2 is used for constant generation.  */
#define TCG_REG_T1  TCG_REG_G1
#define TCG_REG_T2  TCG_REG_O7

#ifndef CONFIG_SOFTMMU
# define TCG_GUEST_BASE_REG TCG_REG_I5
#endif

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

static inline int check_fit_i64(int64_t val, unsigned int bits)
{
    return val == sextract64(val, 0, bits);
}

static inline int check_fit_i32(int32_t val, unsigned int bits)
{
    return val == sextract32(val, 0, bits);
}

#define check_fit_tl    check_fit_i64
#if SPARC64
# define check_fit_ptr  check_fit_i64
#else
# define check_fit_ptr  check_fit_i32
#endif

static void patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    uint32_t insn;

    tcg_debug_assert(addend == 0);
    value = tcg_ptr_byte_diff((tcg_insn_unit *)value, code_ptr);

    switch (type) {
    case R_SPARC_WDISP16:
        if (!check_fit_ptr(value >> 2, 16)) {
            tcg_abort();
        }
        insn = *code_ptr;
        insn &= ~INSN_OFF16(-1);
        insn |= INSN_OFF16(value);
        *code_ptr = insn;
        break;
    case R_SPARC_WDISP19:
        if (!check_fit_ptr(value >> 2, 19)) {
            tcg_abort();
        }
        insn = *code_ptr;
        insn &= ~INSN_OFF19(-1);
        insn |= INSN_OFF19(value);
        *code_ptr = insn;
        break;
    default:
        tcg_abort();
    }
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        break;
    case 'R':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, ALL_64);
        break;
    case 'A': /* qemu_ld/st address constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0,
                         TARGET_LONG_BITS == 64 ? ALL_64 : 0xffffffff);
    reserve_helpers:
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O2);
        break;
    case 's': /* qemu_st data 32-bit constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        goto reserve_helpers;
    case 'S': /* qemu_st data 64-bit constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, ALL_64);
        goto reserve_helpers;
    case 'I':
        ct->ct |= TCG_CT_CONST_S11;
        break;
    case 'J':
        ct->ct |= TCG_CT_CONST_S13;
        break;
    case 'Z':
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;
    return 0;
}

/* test if a constant matches the constraint */
static inline int tcg_target_const_match(tcg_target_long val, TCGType type,
                                         const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;

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

static inline void tcg_out_arith(TCGContext *s, TCGReg rd, TCGReg rs1,
                                 TCGReg rs2, int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1) | INSN_RS2(rs2));
}

static inline void tcg_out_arithi(TCGContext *s, TCGReg rd, TCGReg rs1,
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

static inline void tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out_arith(s, ret, arg, TCG_REG_G0, ARITH_OR);
    }
}

static inline void tcg_out_sethi(TCGContext *s, TCGReg ret, uint32_t arg)
{
    tcg_out32(s, SETHI | INSN_RD(ret) | ((arg & 0xfffffc00) >> 10));
}

static inline void tcg_out_movi_imm13(TCGContext *s, TCGReg ret, int32_t arg)
{
    tcg_out_arithi(s, ret, TCG_REG_G0, arg, ARITH_OR);
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_target_long hi, lo = (int32_t)arg;

    /* Make sure we test 32-bit constants for imm13 properly.  */
    if (type == TCG_TYPE_I32) {
        arg = lo;
    }

    /* A 13-bit constant sign-extended to 64-bits.  */
    if (check_fit_tl(arg, 13)) {
        tcg_out_movi_imm13(s, ret, arg);
        return;
    }

    /* A 32-bit constant, or 32-bit zero-extended to 64-bits.  */
    if (type == TCG_TYPE_I32 || arg == (uint32_t)arg) {
        tcg_out_sethi(s, ret, arg);
        if (arg & 0x3ff) {
            tcg_out_arithi(s, ret, ret, arg & 0x3ff, ARITH_OR);
        }
        return;
    }

    /* A 32-bit constant sign-extended to 64-bits.  */
    if (arg == lo) {
        tcg_out_sethi(s, ret, ~arg);
        tcg_out_arithi(s, ret, ret, (arg & 0x3ff) | -0x400, ARITH_XOR);
        return;
    }

    /* A 64-bit constant decomposed into 2 32-bit pieces.  */
    if (check_fit_i32(lo, 13)) {
        hi = (arg - lo) >> 32;
        tcg_out_movi(s, TCG_TYPE_I32, ret, hi);
        tcg_out_arithi(s, ret, ret, 32, SHIFT_SLLX);
        tcg_out_arithi(s, ret, ret, lo, ARITH_ADD);
    } else {
        hi = arg >> 32;
        tcg_out_movi(s, TCG_TYPE_I32, ret, hi);
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T2, lo);
        tcg_out_arithi(s, ret, ret, 32, SHIFT_SLLX);
        tcg_out_arith(s, ret, ret, TCG_REG_T2, ARITH_OR);
    }
}

static inline void tcg_out_ldst_rr(TCGContext *s, TCGReg data, TCGReg a1,
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

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, ret, arg1, arg2, (type == TCG_TYPE_I32 ? LDUW : LDX));
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_ldst(s, arg, arg1, arg2, (type == TCG_TYPE_I32 ? STW : STX));
}

static void tcg_out_ld_ptr(TCGContext *s, TCGReg ret, uintptr_t arg)
{
    tcg_out_movi(s, TCG_TYPE_PTR, ret, arg & ~0x3ff);
    tcg_out_ld(s, TCG_TYPE_PTR, ret, ret, arg & 0x3ff);
}

static inline void tcg_out_sety(TCGContext *s, TCGReg rs)
{
    tcg_out32(s, WRY | INSN_RS1(TCG_REG_G0) | INSN_RS2(rs));
}

static inline void tcg_out_rdy(TCGContext *s, TCGReg rd)
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

static inline void tcg_out_nop(TCGContext *s)
{
    tcg_out_sethi(s, TCG_REG_G0, 0);
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
    int off19;

    if (l->has_value) {
        off19 = INSN_OFF19(tcg_pcrel_diff(s, l->u.value_ptr));
    } else {
        /* Make sure to preserve destinations during retranslation.  */
        off19 = *s->code_ptr & INSN_OFF19(-1);
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
        int off16;

        if (l->has_value) {
            off16 = INSN_OFF16(tcg_pcrel_diff(s, l->u.value_ptr));
        } else {
            /* Make sure to preserve destinations during retranslation.  */
            off16 = *s->code_ptr & INSN_OFF16(-1);
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
           tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_T2, bh);
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
        /* Otherwise adjust BH as if there is carry into T2 ... */
        if (bhconst) {
            tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_T2, bh + (is_sub ? -1 : 1));
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

static void tcg_out_call_nodelay(TCGContext *s, tcg_insn_unit *dest)
{
    ptrdiff_t disp = tcg_pcrel_diff(s, dest);

    if (disp == (int32_t)disp) {
        tcg_out32(s, CALL | (uint32_t)disp >> 2);
    } else {
        uintptr_t desti = (uintptr_t)dest;
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T1, desti & ~0xfff);
        tcg_out_arithi(s, TCG_REG_O7, TCG_REG_T1, desti & 0xfff, JMPL);
    }
}

static void tcg_out_call(TCGContext *s, tcg_insn_unit *dest)
{
    tcg_out_call_nodelay(s, dest);
    tcg_out_nop(s);
}

#ifdef CONFIG_SOFTMMU
static tcg_insn_unit *qemu_ld_trampoline[16];
static tcg_insn_unit *qemu_st_trampoline[16];

static void build_trampolines(TCGContext *s)
{
    static void * const qemu_ld_helpers[16] = {
        [MO_UB]   = helper_ret_ldub_mmu,
        [MO_SB]   = helper_ret_ldsb_mmu,
        [MO_LEUW] = helper_le_lduw_mmu,
        [MO_LESW] = helper_le_ldsw_mmu,
        [MO_LEUL] = helper_le_ldul_mmu,
        [MO_LEQ]  = helper_le_ldq_mmu,
        [MO_BEUW] = helper_be_lduw_mmu,
        [MO_BESW] = helper_be_ldsw_mmu,
        [MO_BEUL] = helper_be_ldul_mmu,
        [MO_BEQ]  = helper_be_ldq_mmu,
    };
    static void * const qemu_st_helpers[16] = {
        [MO_UB]   = helper_ret_stb_mmu,
        [MO_LEUW] = helper_le_stw_mmu,
        [MO_LEUL] = helper_le_stl_mmu,
        [MO_LEQ]  = helper_le_stq_mmu,
        [MO_BEUW] = helper_be_stw_mmu,
        [MO_BEUL] = helper_be_stl_mmu,
        [MO_BEQ]  = helper_be_stq_mmu,
    };

    int i;
    TCGReg ra;

    for (i = 0; i < 16; ++i) {
        if (qemu_ld_helpers[i] == NULL) {
            continue;
        }

        /* May as well align the trampoline.  */
        while ((uintptr_t)s->code_ptr & 15) {
            tcg_out_nop(s);
        }
        qemu_ld_trampoline[i] = s->code_ptr;

        if (SPARC64 || TARGET_LONG_BITS == 32) {
            ra = TCG_REG_O3;
        } else {
            /* Install the high part of the address.  */
            tcg_out_arithi(s, TCG_REG_O1, TCG_REG_O2, 32, SHIFT_SRLX);
            ra = TCG_REG_O4;
        }

        /* Set the retaddr operand.  */
        tcg_out_mov(s, TCG_TYPE_PTR, ra, TCG_REG_O7);
        /* Set the env operand.  */
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_O0, TCG_AREG0);
        /* Tail call.  */
        tcg_out_call_nodelay(s, qemu_ld_helpers[i]);
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_O7, ra);
    }

    for (i = 0; i < 16; ++i) {
        if (qemu_st_helpers[i] == NULL) {
            continue;
        }

        /* May as well align the trampoline.  */
        while ((uintptr_t)s->code_ptr & 15) {
            tcg_out_nop(s);
        }
        qemu_st_trampoline[i] = s->code_ptr;

        if (SPARC64) {
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
                ra += 1;
            }
            /* Skip the oi argument.  */
            ra += 1;
        }
                
        /* Set the retaddr operand.  */
        if (ra >= TCG_REG_O6) {
            tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_O7, TCG_REG_CALL_STACK,
                       TCG_TARGET_CALL_STACK_OFFSET);
            ra = TCG_REG_G1;
        }
        tcg_out_mov(s, TCG_TYPE_PTR, ra, TCG_REG_O7);
        /* Set the env operand.  */
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_O0, TCG_AREG0);
        /* Tail call.  */
        tcg_out_call_nodelay(s, qemu_st_helpers[i]);
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_O7, ra);
    }
}
#endif

/* Generate global QEMU prologue and epilogue code */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    int tmp_buf_size, frame_size;

    /* The TCG temp buffer is at the top of the frame, immediately
       below the frame pointer.  */
    tmp_buf_size = CPU_TEMP_BUF_NLONGS * (int)sizeof(long);
    tcg_set_frame(s, TCG_REG_I6, TCG_TARGET_STACK_BIAS - tmp_buf_size,
                  tmp_buf_size);

    /* TCG_TARGET_CALL_STACK_OFFSET includes the stack bias, but is
       otherwise the minimal frame usable by callees.  */
    frame_size = TCG_TARGET_CALL_STACK_OFFSET - TCG_TARGET_STACK_BIAS;
    frame_size += TCG_STATIC_CALL_ARGS_SIZE + tmp_buf_size;
    frame_size += TCG_TARGET_STACK_ALIGN - 1;
    frame_size &= -TCG_TARGET_STACK_ALIGN;
    tcg_out32(s, SAVE | INSN_RD(TCG_REG_O6) | INSN_RS1(TCG_REG_O6) |
              INSN_IMM13(-frame_size));

#ifndef CONFIG_SOFTMMU
    if (guest_base != 0) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I1, 0, JMPL);
    /* delay slot */
    tcg_out_nop(s);

    /* No epilogue required.  We issue ret + restore directly in the TB.  */

#ifdef CONFIG_SOFTMMU
    build_trampolines(s);
#endif
}

#if defined(CONFIG_SOFTMMU)
/* Perform the TLB load and compare.

   Inputs:
   ADDRLO and ADDRHI contain the possible two parts of the address.

   MEM_INDEX and S_BITS are the memory context and log2 size of the load.

   WHICH is the offset into the CPUTLBEntry structure of the slot to read.
   This should be offsetof addr_read or addr_write.

   The result of the TLB comparison is in %[ix]cc.  The sanitized address
   is in the returned register, maybe %o0.  The TLB addend is in %o1.  */

static TCGReg tcg_out_tlb_load(TCGContext *s, TCGReg addr, int mem_index,
                               TCGMemOp s_bits, int which)
{
    const TCGReg r0 = TCG_REG_O0;
    const TCGReg r1 = TCG_REG_O1;
    const TCGReg r2 = TCG_REG_O2;
    int tlb_ofs;

    /* Shift the page number down.  */
    tcg_out_arithi(s, r1, addr, TARGET_PAGE_BITS, SHIFT_SRL);

    /* Mask out the page offset, except for the required alignment.  */
    tcg_out_movi(s, TCG_TYPE_TL, TCG_REG_T1,
                 TARGET_PAGE_MASK | ((1 << s_bits) - 1));

    /* Mask the tlb index.  */
    tcg_out_arithi(s, r1, r1, CPU_TLB_SIZE - 1, ARITH_AND);
    
    /* Mask page, part 2.  */
    tcg_out_arith(s, r0, addr, TCG_REG_T1, ARITH_AND);

    /* Shift the tlb index into place.  */
    tcg_out_arithi(s, r1, r1, CPU_TLB_ENTRY_BITS, SHIFT_SLL);

    /* Relative to the current ENV.  */
    tcg_out_arith(s, r1, TCG_AREG0, r1, ARITH_ADD);

    /* Find a base address that can load both tlb comparator and addend.  */
    tlb_ofs = offsetof(CPUArchState, tlb_table[mem_index][0]);
    if (!check_fit_ptr(tlb_ofs + sizeof(CPUTLBEntry), 13)) {
        if (tlb_ofs & ~0x3ff) {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T1, tlb_ofs & ~0x3ff);
            tcg_out_arith(s, r1, r1, TCG_REG_T1, ARITH_ADD);
        }
        tlb_ofs &= 0x3ff;
    }

    /* Load the tlb comparator and the addend.  */
    tcg_out_ld(s, TCG_TYPE_TL, r2, r1, tlb_ofs + which);
    tcg_out_ld(s, TCG_TYPE_PTR, r1, r1, tlb_ofs+offsetof(CPUTLBEntry, addend));

    /* subcc arg0, arg2, %g0 */
    tcg_out_cmp(s, r0, r2, 0);

    /* If the guest address must be zero-extended, do so now.  */
    if (SPARC64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, r0, addr, 0, SHIFT_SRL);
        return r0;
    }
    return addr;
}
#endif /* CONFIG_SOFTMMU */

static const int qemu_ld_opc[16] = {
    [MO_UB]   = LDUB,
    [MO_SB]   = LDSB,

    [MO_BEUW] = LDUH,
    [MO_BESW] = LDSH,
    [MO_BEUL] = LDUW,
    [MO_BESL] = LDSW,
    [MO_BEQ]  = LDX,

    [MO_LEUW] = LDUH_LE,
    [MO_LESW] = LDSH_LE,
    [MO_LEUL] = LDUW_LE,
    [MO_LESL] = LDSW_LE,
    [MO_LEQ]  = LDX_LE,
};

static const int qemu_st_opc[16] = {
    [MO_UB]   = STB,

    [MO_BEUW] = STH,
    [MO_BEUL] = STW,
    [MO_BEQ]  = STX,

    [MO_LEUW] = STH_LE,
    [MO_LEUL] = STW_LE,
    [MO_LEQ]  = STX_LE,
};

static void tcg_out_qemu_ld(TCGContext *s, TCGReg data, TCGReg addr,
                            TCGMemOpIdx oi, bool is_64)
{
    TCGMemOp memop = get_memop(oi);
#ifdef CONFIG_SOFTMMU
    unsigned memi = get_mmuidx(oi);
    TCGReg addrz, param;
    tcg_insn_unit *func;
    tcg_insn_unit *label_ptr;

    addrz = tcg_out_tlb_load(s, addr, memi, memop & MO_SIZE,
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
    tcg_out_mov(s, TCG_TYPE_REG, param++, addr);

    /* We use the helpers to extend SB and SW data, leaving the case
       of SL needing explicit extending below.  */
    if ((memop & MO_SSIZE) == MO_SL) {
        func = qemu_ld_trampoline[memop & (MO_BSWAP | MO_SIZE)];
    } else {
        func = qemu_ld_trampoline[memop & (MO_BSWAP | MO_SSIZE)];
    }
    tcg_debug_assert(func != NULL);
    tcg_out_call_nodelay(s, func);
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
    if (SPARC64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, TCG_REG_T1, addr, 0, SHIFT_SRL);
        addr = TCG_REG_T1;
    }
    tcg_out_ldst_rr(s, data, addr,
                    (guest_base ? TCG_GUEST_BASE_REG : TCG_REG_G0),
                    qemu_ld_opc[memop & (MO_BSWAP | MO_SSIZE)]);
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_qemu_st(TCGContext *s, TCGReg data, TCGReg addr,
                            TCGMemOpIdx oi)
{
    TCGMemOp memop = get_memop(oi);
#ifdef CONFIG_SOFTMMU
    unsigned memi = get_mmuidx(oi);
    TCGReg addrz, param;
    tcg_insn_unit *func;
    tcg_insn_unit *label_ptr;

    addrz = tcg_out_tlb_load(s, addr, memi, memop & MO_SIZE,
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
    tcg_out_mov(s, TCG_TYPE_REG, param++, addr);
    if (!SPARC64 && (memop & MO_SIZE) == MO_64) {
        /* Skip the high-part; we'll perform the extract in the trampoline.  */
        param++;
    }
    tcg_out_mov(s, TCG_TYPE_REG, param++, data);

    func = qemu_st_trampoline[memop & (MO_BSWAP | MO_SIZE)];
    tcg_debug_assert(func != NULL);
    tcg_out_call_nodelay(s, func);
    /* delay slot */
    tcg_out_movi(s, TCG_TYPE_I32, param, oi);

    *label_ptr |= INSN_OFF19(tcg_ptr_byte_diff(s->code_ptr, label_ptr));
#else
    if (SPARC64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, TCG_REG_T1, addr, 0, SHIFT_SRL);
        addr = TCG_REG_T1;
    }
    tcg_out_ldst_rr(s, data, addr,
                    (guest_base ? TCG_GUEST_BASE_REG : TCG_REG_G0),
                    qemu_st_opc[memop & (MO_BSWAP | MO_SIZE)]);
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
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I0, a0 & ~0x3ff);
            tcg_out_arithi(s, TCG_REG_G0, TCG_REG_I7, 8, RETURN);
            tcg_out_arithi(s, TCG_REG_O0, TCG_REG_O0, a0 & 0x3ff, ARITH_OR);
        }
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            s->tb_jmp_offset[a0] = tcg_current_code_size(s);
            /* Make sure to preserve links during retranslation.  */
            tcg_out32(s, CALL | (*s->code_ptr & ~INSN_OP(-1)));
        } else {
            /* indirect jump method */
            tcg_out_ld_ptr(s, TCG_REG_T1, (uintptr_t)(s->tb_next + a0));
            tcg_out_arithi(s, TCG_REG_G0, TCG_REG_T1, 0, JMPL);
        }
        tcg_out_nop(s);
        s->tb_next_offset[a0] = tcg_current_code_size(s);
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

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_movi_i32: /* Always emitted via tcg_out_movi.  */
    case INDEX_op_movi_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    default:
        tcg_abort();
    }
}

static const TCGTargetOpDef sparc_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_br, { } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "rZ", "r" } },
    { INDEX_op_st16_i32, { "rZ", "r" } },
    { INDEX_op_st_i32, { "rZ", "r" } },

    { INDEX_op_add_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_mul_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_div_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_divu_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_sub_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_and_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_andc_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_or_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_orc_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_xor_i32, { "r", "rZ", "rJ" } },

    { INDEX_op_shl_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_shr_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_sar_i32, { "r", "rZ", "rJ" } },

    { INDEX_op_neg_i32, { "r", "rJ" } },
    { INDEX_op_not_i32, { "r", "rJ" } },

    { INDEX_op_brcond_i32, { "rZ", "rJ" } },
    { INDEX_op_setcond_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_movcond_i32, { "r", "rZ", "rJ", "rI", "0" } },

    { INDEX_op_add2_i32, { "r", "r", "rZ", "rZ", "rJ", "rJ" } },
    { INDEX_op_sub2_i32, { "r", "r", "rZ", "rZ", "rJ", "rJ" } },
    { INDEX_op_mulu2_i32, { "r", "r", "rZ", "rJ" } },
    { INDEX_op_muls2_i32, { "r", "r", "rZ", "rJ" } },

    { INDEX_op_ld8u_i64, { "R", "r" } },
    { INDEX_op_ld8s_i64, { "R", "r" } },
    { INDEX_op_ld16u_i64, { "R", "r" } },
    { INDEX_op_ld16s_i64, { "R", "r" } },
    { INDEX_op_ld32u_i64, { "R", "r" } },
    { INDEX_op_ld32s_i64, { "R", "r" } },
    { INDEX_op_ld_i64, { "R", "r" } },
    { INDEX_op_st8_i64, { "RZ", "r" } },
    { INDEX_op_st16_i64, { "RZ", "r" } },
    { INDEX_op_st32_i64, { "RZ", "r" } },
    { INDEX_op_st_i64, { "RZ", "r" } },

    { INDEX_op_add_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_mul_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_div_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_divu_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_sub_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_and_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_andc_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_or_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_orc_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_xor_i64, { "R", "RZ", "RJ" } },

    { INDEX_op_shl_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_shr_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_sar_i64, { "R", "RZ", "RJ" } },

    { INDEX_op_neg_i64, { "R", "RJ" } },
    { INDEX_op_not_i64, { "R", "RJ" } },

    { INDEX_op_ext32s_i64, { "R", "R" } },
    { INDEX_op_ext32u_i64, { "R", "R" } },
    { INDEX_op_ext_i32_i64, { "R", "r" } },
    { INDEX_op_extu_i32_i64, { "R", "r" } },
    { INDEX_op_extrl_i64_i32,  { "r", "R" } },
    { INDEX_op_extrh_i64_i32,  { "r", "R" } },

    { INDEX_op_brcond_i64, { "RZ", "RJ" } },
    { INDEX_op_setcond_i64, { "R", "RZ", "RJ" } },
    { INDEX_op_movcond_i64, { "R", "RZ", "RJ", "RI", "0" } },

    { INDEX_op_add2_i64, { "R", "R", "RZ", "RZ", "RJ", "RI" } },
    { INDEX_op_sub2_i64, { "R", "R", "RZ", "RZ", "RJ", "RI" } },
    { INDEX_op_muluh_i64, { "R", "RZ", "RZ" } },

    { INDEX_op_qemu_ld_i32, { "r", "A" } },
    { INDEX_op_qemu_ld_i64, { "R", "A" } },
    { INDEX_op_qemu_st_i32, { "sZ", "A" } },
    { INDEX_op_qemu_st_i64, { "SZ", "A" } },

    { -1 },
};

static void tcg_target_init(TCGContext *s)
{
    /* Only probe for the platform and capabilities if we havn't already
       determined maximum values at compile time.  */
#ifndef use_vis3_instructions
    {
        unsigned long hwcap = qemu_getauxval(AT_HWCAP);
        use_vis3_instructions = (hwcap & HWCAP_SPARC_VIS3) != 0;
    }
#endif

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, ALL_64);

    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_G1) |
                     (1 << TCG_REG_G2) |
                     (1 << TCG_REG_G3) |
                     (1 << TCG_REG_G4) |
                     (1 << TCG_REG_G5) |
                     (1 << TCG_REG_G6) |
                     (1 << TCG_REG_G7) |
                     (1 << TCG_REG_O0) |
                     (1 << TCG_REG_O1) |
                     (1 << TCG_REG_O2) |
                     (1 << TCG_REG_O3) |
                     (1 << TCG_REG_O4) |
                     (1 << TCG_REG_O5) |
                     (1 << TCG_REG_O7));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_G0); /* zero */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_G6); /* reserved for os */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_G7); /* thread pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I6); /* frame pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_I7); /* return address */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_O6); /* stack pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_T1); /* for internal use */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_T2); /* for internal use */

    tcg_add_target_add_op_defs(sparc_op_defs);
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

void tcg_register_jit(void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
    uint32_t *ptr = (uint32_t *)jmp_addr;
    uintptr_t disp = addr - jmp_addr;

    /* We can reach the entire address space for 32-bit.  For 64-bit
       the code_gen_buffer can't be larger than 2GB.  */
    tcg_debug_assert(disp == (int32_t)disp);

    atomic_set(ptr, deposit32(CALL, 0, 30, disp >> 2));
    flush_icache_range(jmp_addr, jmp_addr + 4);
}
