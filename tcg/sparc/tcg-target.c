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

#ifndef NDEBUG
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

/* Define some temporary registers.  T2 is used for constant generation.  */
#define TCG_REG_T1  TCG_REG_G1
#define TCG_REG_T2  TCG_REG_O7

#ifdef CONFIG_USE_GUEST_BASE
# define TCG_GUEST_BASE_REG TCG_REG_I5
#else
# define TCG_GUEST_BASE_REG TCG_REG_G0
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
#define ARITH_ADDX (INSN_OP(2) | INSN_OP3(0x08))
#define ARITH_SUBX (INSN_OP(2) | INSN_OP3(0x0c))
#define ARITH_UMUL (INSN_OP(2) | INSN_OP3(0x0a))
#define ARITH_UDIV (INSN_OP(2) | INSN_OP3(0x0e))
#define ARITH_SDIV (INSN_OP(2) | INSN_OP3(0x0f))
#define ARITH_MULX (INSN_OP(2) | INSN_OP3(0x09))
#define ARITH_UDIVX (INSN_OP(2) | INSN_OP3(0x0d))
#define ARITH_SDIVX (INSN_OP(2) | INSN_OP3(0x2d))
#define ARITH_MOVCC (INSN_OP(2) | INSN_OP3(0x2c))
#define ARITH_MOVR (INSN_OP(2) | INSN_OP3(0x2f))

#define SHIFT_SLL  (INSN_OP(2) | INSN_OP3(0x25))
#define SHIFT_SRL  (INSN_OP(2) | INSN_OP3(0x26))
#define SHIFT_SRA  (INSN_OP(2) | INSN_OP3(0x27))

#define SHIFT_SLLX (INSN_OP(2) | INSN_OP3(0x25) | (1 << 12))
#define SHIFT_SRLX (INSN_OP(2) | INSN_OP3(0x26) | (1 << 12))
#define SHIFT_SRAX (INSN_OP(2) | INSN_OP3(0x27) | (1 << 12))

#define RDY        (INSN_OP(2) | INSN_OP3(0x28) | INSN_RS1(0))
#define WRY        (INSN_OP(2) | INSN_OP3(0x30) | INSN_RD(0))
#define JMPL       (INSN_OP(2) | INSN_OP3(0x38))
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

static inline int check_fit_tl(tcg_target_long val, unsigned int bits)
{
    return (val << ((sizeof(tcg_target_long) * 8 - bits))
            >> (sizeof(tcg_target_long) * 8 - bits)) == val;
}

static inline int check_fit_i32(uint32_t val, unsigned int bits)
{
    return ((val << (32 - bits)) >> (32 - bits)) == val;
}

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    uint32_t insn;
    value += addend;
    switch (type) {
    case R_SPARC_32:
        if (value != (uint32_t)value) {
            tcg_abort();
        }
        *(uint32_t *)code_ptr = value;
        break;
    case R_SPARC_WDISP16:
        value -= (long)code_ptr;
        if (!check_fit_tl(value >> 2, 16)) {
            tcg_abort();
        }
        insn = *(uint32_t *)code_ptr;
        insn &= ~INSN_OFF16(-1);
        insn |= INSN_OFF16(value);
        *(uint32_t *)code_ptr = insn;
        break;
    case R_SPARC_WDISP19:
        value -= (long)code_ptr;
        if (!check_fit_tl(value >> 2, 19)) {
            tcg_abort();
        }
        insn = *(uint32_t *)code_ptr;
        insn &= ~INSN_OFF19(-1);
        insn |= INSN_OFF19(value);
        *(uint32_t *)code_ptr = insn;
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
    case 'L': /* qemu_ld/st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        // Helper args
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_O2);
        break;
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
static inline int tcg_target_const_match(tcg_target_long val,
                                         const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;

    if (ct & TCG_CT_CONST) {
        return 1;
    } else if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S11) && check_fit_tl(val, 11)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S13) && check_fit_tl(val, 13)) {
        return 1;
    } else {
        return 0;
    }
}

static inline void tcg_out_arith(TCGContext *s, int rd, int rs1, int rs2,
                                 int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1) |
              INSN_RS2(rs2));
}

static inline void tcg_out_arithi(TCGContext *s, int rd, int rs1,
                                  uint32_t offset, int op)
{
    tcg_out32(s, op | INSN_RD(rd) | INSN_RS1(rs1) |
              INSN_IMM13(offset));
}

static void tcg_out_arithc(TCGContext *s, int rd, int rs1,
			   int val2, int val2const, int op)
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

static inline void tcg_out_sethi(TCGContext *s, int ret, uint32_t arg)
{
    tcg_out32(s, SETHI | INSN_RD(ret) | ((arg & 0xfffffc00) >> 10));
}

static inline void tcg_out_movi_imm13(TCGContext *s, int ret, uint32_t arg)
{
    tcg_out_arithi(s, ret, TCG_REG_G0, arg, ARITH_OR);
}

static inline void tcg_out_movi_imm32(TCGContext *s, int ret, uint32_t arg)
{
    if (check_fit_tl(arg, 13))
        tcg_out_movi_imm13(s, ret, arg);
    else {
        tcg_out_sethi(s, ret, arg);
        if (arg & 0x3ff)
            tcg_out_arithi(s, ret, ret, arg & 0x3ff, ARITH_OR);
    }
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                TCGReg ret, tcg_target_long arg)
{
    /* All 32-bit constants, as well as 64-bit constants with
       no high bits set go through movi_imm32.  */
    if (TCG_TARGET_REG_BITS == 32
        || type == TCG_TYPE_I32
        || (arg & ~(tcg_target_long)0xffffffff) == 0) {
        tcg_out_movi_imm32(s, ret, arg);
    } else if (check_fit_tl(arg, 13)) {
        /* A 13-bit constant sign-extended to 64-bits.  */
        tcg_out_movi_imm13(s, ret, arg);
    } else if (check_fit_tl(arg, 32)) {
        /* A 32-bit constant sign-extended to 64-bits.  */
        tcg_out_sethi(s, ret, ~arg);
        tcg_out_arithi(s, ret, ret, (arg & 0x3ff) | -0x400, ARITH_XOR);
    } else {
        tcg_out_movi_imm32(s, ret, arg >> (TCG_TARGET_REG_BITS / 2));
        tcg_out_arithi(s, ret, ret, 32, SHIFT_SLLX);
        tcg_out_movi_imm32(s, TCG_REG_T2, arg);
        tcg_out_arith(s, ret, ret, TCG_REG_T2, ARITH_OR);
    }
}

static inline void tcg_out_ldst_rr(TCGContext *s, int data, int a1,
                                   int a2, int op)
{
    tcg_out32(s, op | INSN_RD(data) | INSN_RS1(a1) | INSN_RS2(a2));
}

static inline void tcg_out_ldst(TCGContext *s, int ret, int addr,
                                int offset, int op)
{
    if (check_fit_tl(offset, 13)) {
        tcg_out32(s, op | INSN_RD(ret) | INSN_RS1(addr) |
                  INSN_IMM13(offset));
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T1, offset);
        tcg_out_ldst_rr(s, ret, addr, TCG_REG_T1, op);
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                              TCGReg arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, ret, arg1, arg2, (type == TCG_TYPE_I32 ? LDUW : LDX));
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, arg, arg1, arg2, (type == TCG_TYPE_I32 ? STW : STX));
}

static inline void tcg_out_ld_ptr(TCGContext *s, int ret,
                                  tcg_target_long arg)
{
    if (!check_fit_tl(arg, 10)) {
        tcg_out_movi(s, TCG_TYPE_PTR, ret, arg & ~0x3ff);
    }
    tcg_out_ld(s, TCG_TYPE_PTR, ret, ret, arg & 0x3ff);
}

static inline void tcg_out_sety(TCGContext *s, int rs)
{
    tcg_out32(s, WRY | INSN_RS1(TCG_REG_G0) | INSN_RS2(rs));
}

static inline void tcg_out_rdy(TCGContext *s, int rd)
{
    tcg_out32(s, RDY | INSN_RD(rd));
}

static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val != 0) {
        if (check_fit_tl(val, 13))
            tcg_out_arithi(s, reg, reg, val, ARITH_ADD);
        else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_T1, val);
            tcg_out_arith(s, reg, reg, TCG_REG_T1, ARITH_ADD);
        }
    }
}

static inline void tcg_out_andi(TCGContext *s, int rd, int rs,
                                tcg_target_long val)
{
    if (val != 0) {
        if (check_fit_tl(val, 13))
            tcg_out_arithi(s, rd, rs, val, ARITH_AND);
        else {
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_T1, val);
            tcg_out_arith(s, rd, rs, TCG_REG_T1, ARITH_AND);
        }
    }
}

static void tcg_out_div32(TCGContext *s, int rd, int rs1,
                          int val2, int val2const, int uns)
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

static void tcg_out_bpcc(TCGContext *s, int scond, int flags, int label)
{
    TCGLabel *l = &s->labels[label];
    int off19;

    if (l->has_value) {
        off19 = INSN_OFF19(l->u.value - (unsigned long)s->code_ptr);
    } else {
        /* Make sure to preserve destinations during retranslation.  */
        off19 = *(uint32_t *)s->code_ptr & INSN_OFF19(-1);
        tcg_out_reloc(s, s->code_ptr, R_SPARC_WDISP19, label, 0);
    }
    tcg_out_bpcc0(s, scond, flags, off19);
}

static void tcg_out_cmp(TCGContext *s, TCGArg c1, TCGArg c2, int c2const)
{
    tcg_out_arithc(s, TCG_REG_G0, c1, c2, c2const, ARITH_SUBCC);
}

static void tcg_out_brcond_i32(TCGContext *s, TCGCond cond, TCGArg arg1,
                               TCGArg arg2, int const_arg2, int label)
{
    tcg_out_cmp(s, arg1, arg2, const_arg2);
    tcg_out_bpcc(s, tcg_cond_to_bcond[cond], BPCC_ICC | BPCC_PT, label);
    tcg_out_nop(s);
}

static void tcg_out_movcc(TCGContext *s, TCGCond cond, int cc, TCGArg ret,
                          TCGArg v1, int v1const)
{
    tcg_out32(s, ARITH_MOVCC | cc | INSN_RD(ret)
              | INSN_RS1(tcg_cond_to_bcond[cond])
              | (v1const ? INSN_IMM11(v1) : INSN_RS2(v1)));
}

static void tcg_out_movcond_i32(TCGContext *s, TCGCond cond, TCGArg ret,
                                TCGArg c1, TCGArg c2, int c2const,
                                TCGArg v1, int v1const)
{
    tcg_out_cmp(s, c1, c2, c2const);
    tcg_out_movcc(s, cond, MOVCC_ICC, ret, v1, v1const);
}

#if TCG_TARGET_REG_BITS == 64
static void tcg_out_brcond_i64(TCGContext *s, TCGCond cond, TCGArg arg1,
                               TCGArg arg2, int const_arg2, int label)
{
    /* For 64-bit signed comparisons vs zero, we can avoid the compare.  */
    if (arg2 == 0 && !is_unsigned_cond(cond)) {
        TCGLabel *l = &s->labels[label];
        int off16;

        if (l->has_value) {
            off16 = INSN_OFF16(l->u.value - (unsigned long)s->code_ptr);
        } else {
            /* Make sure to preserve destinations during retranslation.  */
            off16 = *(uint32_t *)s->code_ptr & INSN_OFF16(-1);
            tcg_out_reloc(s, s->code_ptr, R_SPARC_WDISP16, label, 0);
        }
        tcg_out32(s, INSN_OP(0) | INSN_OP2(3) | BPR_PT | INSN_RS1(arg1)
                  | INSN_COND(tcg_cond_to_rcond[cond]) | off16);
    } else {
        tcg_out_cmp(s, arg1, arg2, const_arg2);
        tcg_out_bpcc(s, tcg_cond_to_bcond[cond], BPCC_XCC | BPCC_PT, label);
    }
    tcg_out_nop(s);
}

static void tcg_out_movr(TCGContext *s, TCGCond cond, TCGArg ret, TCGArg c1,
                         TCGArg v1, int v1const)
{
    tcg_out32(s, ARITH_MOVR | INSN_RD(ret) | INSN_RS1(c1)
              | (tcg_cond_to_rcond[cond] << 10)
              | (v1const ? INSN_IMM10(v1) : INSN_RS2(v1)));
}

static void tcg_out_movcond_i64(TCGContext *s, TCGCond cond, TCGArg ret,
                                TCGArg c1, TCGArg c2, int c2const,
                                TCGArg v1, int v1const)
{
    /* For 64-bit signed comparisons vs zero, we can avoid the compare.
       Note that the immediate range is one bit smaller, so we must check
       for that as well.  */
    if (c2 == 0 && !is_unsigned_cond(cond)
        && (!v1const || check_fit_tl(v1, 10))) {
        tcg_out_movr(s, cond, ret, c1, v1, v1const);
    } else {
        tcg_out_cmp(s, c1, c2, c2const);
        tcg_out_movcc(s, cond, MOVCC_XCC, ret, v1, v1const);
    }
}
#else
static void tcg_out_brcond2_i32(TCGContext *s, TCGCond cond,
                                TCGArg al, TCGArg ah,
                                TCGArg bl, int blconst,
                                TCGArg bh, int bhconst, int label_dest)
{
    int scond, label_next = gen_new_label();

    tcg_out_cmp(s, ah, bh, bhconst);

    /* Note that we fill one of the delay slots with the second compare.  */
    switch (cond) {
    case TCG_COND_EQ:
        tcg_out_bpcc(s, COND_NE, BPCC_ICC | BPCC_PT, label_next);
        tcg_out_cmp(s, al, bl, blconst);
        tcg_out_bpcc(s, COND_E, BPCC_ICC | BPCC_PT, label_dest);
        break;

    case TCG_COND_NE:
        tcg_out_bpcc(s, COND_NE, BPCC_ICC | BPCC_PT, label_dest);
        tcg_out_cmp(s, al, bl, blconst);
        tcg_out_bpcc(s, COND_NE, BPCC_ICC | BPCC_PT, label_dest);
        break;

    default:
        scond = tcg_cond_to_bcond[tcg_high_cond(cond)];
        tcg_out_bpcc(s, scond, BPCC_ICC | BPCC_PT, label_dest);
        tcg_out_nop(s);
        tcg_out_bpcc(s, COND_NE, BPCC_ICC | BPCC_PT, label_next);
        tcg_out_cmp(s, al, bl, blconst);
        scond = tcg_cond_to_bcond[tcg_unsigned_cond(cond)];
        tcg_out_bpcc(s, scond, BPCC_ICC | BPCC_PT, label_dest);
        break;
    }
    tcg_out_nop(s);

    tcg_out_label(s, label_next, s->code_ptr);
}
#endif

static void tcg_out_setcond_i32(TCGContext *s, TCGCond cond, TCGArg ret,
                                TCGArg c1, TCGArg c2, int c2const)
{
    /* For 32-bit comparisons, we can play games with ADDX/SUBX.  */
    switch (cond) {
    case TCG_COND_LTU:
    case TCG_COND_GEU:
        /* The result of the comparison is in the carry bit.  */
        break;

    case TCG_COND_EQ:
    case TCG_COND_NE:
        /* For equality, we can transform to inequality vs zero.  */
        if (c2 != 0) {
            tcg_out_arithc(s, ret, c1, c2, c2const, ARITH_XOR);
        }
        c1 = TCG_REG_G0, c2 = ret, c2const = 0;
        cond = (cond == TCG_COND_EQ ? TCG_COND_GEU : TCG_COND_LTU);
	break;

    case TCG_COND_GTU:
    case TCG_COND_LEU:
        /* If we don't need to load a constant into a register, we can
           swap the operands on GTU/LEU.  There's no benefit to loading
           the constant into a temporary register.  */
        if (!c2const || c2 == 0) {
            TCGArg t = c1;
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
        tcg_out_arithi(s, ret, TCG_REG_G0, 0, ARITH_ADDX);
    } else {
        tcg_out_arithi(s, ret, TCG_REG_G0, -1, ARITH_SUBX);
    }
}

#if TCG_TARGET_REG_BITS == 64
static void tcg_out_setcond_i64(TCGContext *s, TCGCond cond, TCGArg ret,
                                TCGArg c1, TCGArg c2, int c2const)
{
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
#else
static void tcg_out_setcond2_i32(TCGContext *s, TCGCond cond, TCGArg ret,
                                 TCGArg al, TCGArg ah,
                                 TCGArg bl, int blconst,
                                 TCGArg bh, int bhconst)
{
    int tmp = TCG_REG_T1;

    /* Note that the low parts are fully consumed before tmp is set.  */
    if (ret != ah && (bhconst || ret != bh)) {
        tmp = ret;
    }

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        if (bl == 0 && bh == 0) {
            if (cond == TCG_COND_EQ) {
                tcg_out_arith(s, TCG_REG_G0, al, ah, ARITH_ORCC);
                tcg_out_movi(s, TCG_TYPE_I32, ret, 1);
            } else {
                tcg_out_arith(s, ret, al, ah, ARITH_ORCC);
            }
        } else {
            tcg_out_setcond_i32(s, cond, tmp, al, bl, blconst);
            tcg_out_cmp(s, ah, bh, bhconst);
            tcg_out_mov(s, TCG_TYPE_I32, ret, tmp);
        }
        tcg_out_movcc(s, TCG_COND_NE, MOVCC_ICC, ret, cond == TCG_COND_NE, 1);
        break;

    default:
        /* <= : ah < bh | (ah == bh && al <= bl) */
        tcg_out_setcond_i32(s, tcg_unsigned_cond(cond), tmp, al, bl, blconst);
        tcg_out_cmp(s, ah, bh, bhconst);
        tcg_out_mov(s, TCG_TYPE_I32, ret, tmp);
        tcg_out_movcc(s, TCG_COND_NE, MOVCC_ICC, ret, 0, 1);
        tcg_out_movcc(s, tcg_high_cond(cond), MOVCC_ICC, ret, 1, 1);
        break;
    }
}
#endif

static void tcg_out_addsub2(TCGContext *s, TCGArg rl, TCGArg rh,
                            TCGArg al, TCGArg ah, TCGArg bl, int blconst,
                            TCGArg bh, int bhconst, int opl, int oph)
{
    TCGArg tmp = TCG_REG_T1;

    /* Note that the low parts are fully consumed before tmp is set.  */
    if (rl != ah && (bhconst || rl != bh)) {
        tmp = rl;
    }

    tcg_out_arithc(s, tmp, al, bl, blconst, opl);
    tcg_out_arithc(s, rh, ah, bh, bhconst, oph);
    tcg_out_mov(s, TCG_TYPE_I32, rl, tmp);
}

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

#ifdef CONFIG_USE_GUEST_BASE
    if (GUEST_BASE != 0) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out32(s, JMPL | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_I1) |
              INSN_RS2(TCG_REG_G0));
    /* delay slot */
    tcg_out_nop(s);

    /* No epilogue required.  We issue ret + restore directly in the TB.  */
}

#if defined(CONFIG_SOFTMMU)

#include "exec/softmmu_defs.h"

/* helper signature: helper_ld_mmu(CPUState *env, target_ulong addr,
   int mmu_idx) */
static const void * const qemu_ld_helpers[4] = {
    helper_ldb_mmu,
    helper_ldw_mmu,
    helper_ldl_mmu,
    helper_ldq_mmu,
};

/* helper signature: helper_st_mmu(CPUState *env, target_ulong addr,
   uintxx_t val, int mmu_idx) */
static const void * const qemu_st_helpers[4] = {
    helper_stb_mmu,
    helper_stw_mmu,
    helper_stl_mmu,
    helper_stq_mmu,
};

/* Perform the TLB load and compare.

   Inputs:
   ADDRLO_IDX contains the index into ARGS of the low part of the
   address; the high part of the address is at ADDR_LOW_IDX+1.

   MEM_INDEX and S_BITS are the memory context and log2 size of the load.

   WHICH is the offset into the CPUTLBEntry structure of the slot to read.
   This should be offsetof addr_read or addr_write.

   The result of the TLB comparison is in %[ix]cc.  The sanitized address
   is in the returned register, maybe %o0.  The TLB addend is in %o1.  */

static int tcg_out_tlb_load(TCGContext *s, int addrlo_idx, int mem_index,
                            int s_bits, const TCGArg *args, int which)
{
    const int addrlo = args[addrlo_idx];
    const int r0 = TCG_REG_O0;
    const int r1 = TCG_REG_O1;
    const int r2 = TCG_REG_O2;
    int addr = addrlo;
    int tlb_ofs;

    if (TCG_TARGET_REG_BITS == 32 && TARGET_LONG_BITS == 64) {
        /* Assemble the 64-bit address in R0.  */
        tcg_out_arithi(s, r0, addrlo, 0, SHIFT_SRL);
        tcg_out_arithi(s, r1, args[addrlo_idx + 1], 32, SHIFT_SLLX);
        tcg_out_arith(s, r0, r0, r1, ARITH_OR);
    }

    /* Shift the page number down to tlb-entry.  */
    tcg_out_arithi(s, r1, addrlo,
                   TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS, SHIFT_SRL);

    /* Mask out the page offset, except for the required alignment.  */
    tcg_out_andi(s, r0, addr, TARGET_PAGE_MASK | ((1 << s_bits) - 1));

    /* Compute tlb index, modulo tlb size.  */
    tcg_out_andi(s, r1, r1, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);

    /* Relative to the current ENV.  */
    tcg_out_arith(s, r1, TCG_AREG0, r1, ARITH_ADD);

    /* Find a base address that can load both tlb comparator and addend.  */
    tlb_ofs = offsetof(CPUArchState, tlb_table[mem_index][0]);
    if (!check_fit_tl(tlb_ofs + sizeof(CPUTLBEntry), 13)) {
        tcg_out_addi(s, r1, tlb_ofs);
        tlb_ofs = 0;
    }

    /* Load the tlb comparator and the addend.  */
    tcg_out_ld(s, TCG_TYPE_TL, r2, r1, tlb_ofs + which);
    tcg_out_ld(s, TCG_TYPE_PTR, r1, r1, tlb_ofs+offsetof(CPUTLBEntry, addend));

    /* subcc arg0, arg2, %g0 */
    tcg_out_cmp(s, r0, r2, 0);

    /* If the guest address must be zero-extended, do so now.  */
    if (TCG_TARGET_REG_BITS == 64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, r0, addrlo, 0, SHIFT_SRL);
        return r0;
    }
    return addrlo;
}
#endif /* CONFIG_SOFTMMU */

static const int qemu_ld_opc[8] = {
#ifdef TARGET_WORDS_BIGENDIAN
    LDUB, LDUH, LDUW, LDX, LDSB, LDSH, LDSW, LDX
#else
    LDUB, LDUH_LE, LDUW_LE, LDX_LE, LDSB, LDSH_LE, LDSW_LE, LDX_LE
#endif
};

static const int qemu_st_opc[4] = {
#ifdef TARGET_WORDS_BIGENDIAN
    STB, STH, STW, STX
#else
    STB, STH_LE, STW_LE, STX_LE
#endif
};

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, int sizeop)
{
    int addrlo_idx = 1, datalo, datahi, addr_reg;
#if defined(CONFIG_SOFTMMU)
    int memi_idx, memi, s_bits, n;
    uint32_t *label_ptr[2];
#endif

    datahi = datalo = args[0];
    if (TCG_TARGET_REG_BITS == 32 && sizeop == 3) {
        datahi = args[1];
        addrlo_idx = 2;
    }

#if defined(CONFIG_SOFTMMU)
    memi_idx = addrlo_idx + 1 + (TARGET_LONG_BITS > TCG_TARGET_REG_BITS);
    memi = args[memi_idx];
    s_bits = sizeop & 3;

    addr_reg = tcg_out_tlb_load(s, addrlo_idx, memi, s_bits, args,
                                offsetof(CPUTLBEntry, addr_read));

    if (TCG_TARGET_REG_BITS == 32 && sizeop == 3) {
        int reg64;

        /* bne,pn %[xi]cc, label0 */
        label_ptr[0] = (uint32_t *)s->code_ptr;
        tcg_out_bpcc0(s, COND_NE, BPCC_PN
                      | (TARGET_LONG_BITS == 64 ? BPCC_XCC : BPCC_ICC), 0);

        /* TLB Hit.  */
        /* Load all 64-bits into an O/G register.  */
        reg64 = (datalo < 16 ? datalo : TCG_REG_O0);
        tcg_out_ldst_rr(s, reg64, addr_reg, TCG_REG_O1, qemu_ld_opc[sizeop]);

        /* Move the two 32-bit pieces into the destination registers.  */
        tcg_out_arithi(s, datahi, reg64, 32, SHIFT_SRLX);
        if (reg64 != datalo) {
            tcg_out_mov(s, TCG_TYPE_I32, datalo, reg64);
        }

        /* b,a,pt label1 */
        label_ptr[1] = (uint32_t *)s->code_ptr;
        tcg_out_bpcc0(s, COND_A, BPCC_A | BPCC_PT, 0);
    } else {
        /* The fast path is exactly one insn.  Thus we can perform the
           entire TLB Hit in the (annulled) delay slot of the branch
           over the TLB Miss case.  */

        /* beq,a,pt %[xi]cc, label0 */
        label_ptr[0] = NULL;
        label_ptr[1] = (uint32_t *)s->code_ptr;
        tcg_out_bpcc0(s, COND_E, BPCC_A | BPCC_PT
                      | (TARGET_LONG_BITS == 64 ? BPCC_XCC : BPCC_ICC), 0);
        /* delay slot */
        tcg_out_ldst_rr(s, datalo, addr_reg, TCG_REG_O1, qemu_ld_opc[sizeop]);
    }

    /* TLB Miss.  */

    if (label_ptr[0]) {
        *label_ptr[0] |= INSN_OFF19((unsigned long)s->code_ptr -
                                    (unsigned long)label_ptr[0]);
    }
    n = 0;
    tcg_out_mov(s, TCG_TYPE_PTR, tcg_target_call_iarg_regs[n++], TCG_AREG0);
    if (TARGET_LONG_BITS > TCG_TARGET_REG_BITS) {
        tcg_out_mov(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[n++],
                    args[addrlo_idx + 1]);
    }
    tcg_out_mov(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[n++],
                args[addrlo_idx]);

    /* qemu_ld_helper[s_bits](arg0, arg1) */
    tcg_out32(s, CALL | ((((tcg_target_ulong)qemu_ld_helpers[s_bits]
                           - (tcg_target_ulong)s->code_ptr) >> 2)
                         & 0x3fffffff));
    /* delay slot */
    tcg_out_movi(s, TCG_TYPE_I32, tcg_target_call_iarg_regs[n], memi);

    n = tcg_target_call_oarg_regs[0];
    /* datalo = sign_extend(arg0) */
    switch (sizeop) {
    case 0 | 4:
        /* Recall that SRA sign extends from bit 31 through bit 63.  */
        tcg_out_arithi(s, datalo, n, 24, SHIFT_SLL);
        tcg_out_arithi(s, datalo, datalo, 24, SHIFT_SRA);
        break;
    case 1 | 4:
        tcg_out_arithi(s, datalo, n, 16, SHIFT_SLL);
        tcg_out_arithi(s, datalo, datalo, 16, SHIFT_SRA);
        break;
    case 2 | 4:
        tcg_out_arithi(s, datalo, n, 0, SHIFT_SRA);
        break;
    case 3:
        if (TCG_TARGET_REG_BITS == 32) {
            tcg_out_mov(s, TCG_TYPE_REG, datahi, n);
            tcg_out_mov(s, TCG_TYPE_REG, datalo, n + 1);
            break;
        }
        /* FALLTHRU */
    case 0:
    case 1:
    case 2:
    default:
        /* mov */
        tcg_out_mov(s, TCG_TYPE_REG, datalo, n);
        break;
    }

    *label_ptr[1] |= INSN_OFF19((unsigned long)s->code_ptr -
                                (unsigned long)label_ptr[1]);
#else
    addr_reg = args[addrlo_idx];
    if (TCG_TARGET_REG_BITS == 64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, TCG_REG_T1, addr_reg, 0, SHIFT_SRL);
        addr_reg = TCG_REG_T1;
    }
    if (TCG_TARGET_REG_BITS == 32 && sizeop == 3) {
        int reg64 = (datalo < 16 ? datalo : TCG_REG_O0);

        tcg_out_ldst_rr(s, reg64, addr_reg,
                        (GUEST_BASE ? TCG_GUEST_BASE_REG : TCG_REG_G0),
                        qemu_ld_opc[sizeop]);

        tcg_out_arithi(s, datahi, reg64, 32, SHIFT_SRLX);
        if (reg64 != datalo) {
            tcg_out_mov(s, TCG_TYPE_I32, datalo, reg64);
        }
    } else {
        tcg_out_ldst_rr(s, datalo, addr_reg,
                        (GUEST_BASE ? TCG_GUEST_BASE_REG : TCG_REG_G0),
                        qemu_ld_opc[sizeop]);
    }
#endif /* CONFIG_SOFTMMU */
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, int sizeop)
{
    int addrlo_idx = 1, datalo, datahi, addr_reg;
#if defined(CONFIG_SOFTMMU)
    int memi_idx, memi, n, datafull;
    uint32_t *label_ptr;
#endif

    datahi = datalo = args[0];
    if (TCG_TARGET_REG_BITS == 32 && sizeop == 3) {
        datahi = args[1];
        addrlo_idx = 2;
    }

#if defined(CONFIG_SOFTMMU)
    memi_idx = addrlo_idx + 1 + (TARGET_LONG_BITS > TCG_TARGET_REG_BITS);
    memi = args[memi_idx];

    addr_reg = tcg_out_tlb_load(s, addrlo_idx, memi, sizeop, args,
                                offsetof(CPUTLBEntry, addr_write));

    datafull = datalo;
    if (TCG_TARGET_REG_BITS == 32 && sizeop == 3) {
        /* Reconstruct the full 64-bit value.  */
        tcg_out_arithi(s, TCG_REG_T1, datalo, 0, SHIFT_SRL);
        tcg_out_arithi(s, TCG_REG_O2, datahi, 32, SHIFT_SLLX);
        tcg_out_arith(s, TCG_REG_O2, TCG_REG_T1, TCG_REG_O2, ARITH_OR);
        datafull = TCG_REG_O2;
    }

    /* The fast path is exactly one insn.  Thus we can perform the entire
       TLB Hit in the (annulled) delay slot of the branch over TLB Miss.  */
    /* beq,a,pt %[xi]cc, label0 */
    label_ptr = (uint32_t *)s->code_ptr;
    tcg_out_bpcc0(s, COND_E, BPCC_A | BPCC_PT
                  | (TARGET_LONG_BITS == 64 ? BPCC_XCC : BPCC_ICC), 0);
    /* delay slot */
    tcg_out_ldst_rr(s, datafull, addr_reg, TCG_REG_O1, qemu_st_opc[sizeop]);

    /* TLB Miss.  */

    n = 0;
    tcg_out_mov(s, TCG_TYPE_PTR, tcg_target_call_iarg_regs[n++], TCG_AREG0);
    if (TARGET_LONG_BITS > TCG_TARGET_REG_BITS) {
        tcg_out_mov(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[n++],
                    args[addrlo_idx + 1]);
    }
    tcg_out_mov(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[n++],
                args[addrlo_idx]);
    if (TCG_TARGET_REG_BITS == 32 && sizeop == 3) {
        tcg_out_mov(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[n++], datahi);
    }
    tcg_out_mov(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[n++], datalo);

    /* qemu_st_helper[s_bits](arg0, arg1, arg2) */
    tcg_out32(s, CALL | ((((tcg_target_ulong)qemu_st_helpers[sizeop]
                           - (tcg_target_ulong)s->code_ptr) >> 2)
                         & 0x3fffffff));
    /* delay slot */
    tcg_out_movi(s, TCG_TYPE_REG, tcg_target_call_iarg_regs[n], memi);

    *label_ptr |= INSN_OFF19((unsigned long)s->code_ptr -
                             (unsigned long)label_ptr);
#else
    addr_reg = args[addrlo_idx];
    if (TCG_TARGET_REG_BITS == 64 && TARGET_LONG_BITS == 32) {
        tcg_out_arithi(s, TCG_REG_T1, addr_reg, 0, SHIFT_SRL);
        addr_reg = TCG_REG_T1;
    }
    if (TCG_TARGET_REG_BITS == 32 && sizeop == 3) {
        tcg_out_arithi(s, TCG_REG_T1, datalo, 0, SHIFT_SRL);
        tcg_out_arithi(s, TCG_REG_O2, datahi, 32, SHIFT_SLLX);
        tcg_out_arith(s, TCG_REG_O2, TCG_REG_T1, TCG_REG_O2, ARITH_OR);
        datalo = TCG_REG_O2;
    }
    tcg_out_ldst_rr(s, datalo, addr_reg,
                    (GUEST_BASE ? TCG_GUEST_BASE_REG : TCG_REG_G0),
                    qemu_st_opc[sizeop]);
#endif /* CONFIG_SOFTMMU */
}

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                              const int *const_args)
{
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_I0, args[0]);
        tcg_out32(s, JMPL | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_I7) |
                  INSN_IMM13(8));
        tcg_out32(s, RESTORE | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_G0) |
                      INSN_RS2(TCG_REG_G0));
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            uint32_t old_insn = *(uint32_t *)s->code_ptr;
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
            /* Make sure to preserve links during retranslation.  */
            tcg_out32(s, CALL | (old_insn & ~INSN_OP(-1)));
        } else {
            /* indirect jump method */
            tcg_out_ld_ptr(s, TCG_REG_T1,
                           (tcg_target_long)(s->tb_next + args[0]));
            tcg_out32(s, JMPL | INSN_RD(TCG_REG_G0) | INSN_RS1(TCG_REG_T1) |
                      INSN_RS2(TCG_REG_G0));
        }
        tcg_out_nop(s);
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out32(s, CALL | ((((tcg_target_ulong)args[0]
                                   - (tcg_target_ulong)s->code_ptr) >> 2)
                                 & 0x3fffffff));
        } else {
            tcg_out_ld_ptr(s, TCG_REG_T1,
                           (tcg_target_long)(s->tb_next + args[0]));
            tcg_out32(s, JMPL | INSN_RD(TCG_REG_O7) | INSN_RS1(TCG_REG_T1) |
                      INSN_RS2(TCG_REG_G0));
        }
        /* delay slot */
        tcg_out_nop(s);
        break;
    case INDEX_op_br:
        tcg_out_bpcc(s, COND_A, BPCC_PT, args[0]);
        tcg_out_nop(s);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], (uint32_t)args[1]);
        break;

#if TCG_TARGET_REG_BITS == 64
#define OP_32_64(x)                             \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)
#else
#define OP_32_64(x)                             \
        glue(glue(case INDEX_op_, x), _i32)
#endif
    OP_32_64(ld8u):
        tcg_out_ldst(s, args[0], args[1], args[2], LDUB);
        break;
    OP_32_64(ld8s):
        tcg_out_ldst(s, args[0], args[1], args[2], LDSB);
        break;
    OP_32_64(ld16u):
        tcg_out_ldst(s, args[0], args[1], args[2], LDUH);
        break;
    OP_32_64(ld16s):
        tcg_out_ldst(s, args[0], args[1], args[2], LDSH);
        break;
    case INDEX_op_ld_i32:
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_ld32u_i64:
#endif
        tcg_out_ldst(s, args[0], args[1], args[2], LDUW);
        break;
    OP_32_64(st8):
        tcg_out_ldst(s, args[0], args[1], args[2], STB);
        break;
    OP_32_64(st16):
        tcg_out_ldst(s, args[0], args[1], args[2], STH);
        break;
    case INDEX_op_st_i32:
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_st32_i64:
#endif
        tcg_out_ldst(s, args[0], args[1], args[2], STW);
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
        tcg_out_arithc(s, args[0], args[1], args[2] & 31, const_args[2], c);
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
        tcg_out_div32(s, args[0], args[1], args[2], const_args[2], 0);
        break;
    case INDEX_op_divu_i32:
        tcg_out_div32(s, args[0], args[1], args[2], const_args[2], 1);
        break;

    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
        tcg_out_div32(s, TCG_REG_T1, args[1], args[2], const_args[2],
                      opc == INDEX_op_remu_i32);
        tcg_out_arithc(s, TCG_REG_T1, TCG_REG_T1, args[2], const_args[2],
                       ARITH_UMUL);
        tcg_out_arith(s, args[0], args[1], TCG_REG_T1, ARITH_SUB);
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond_i32(s, args[2], args[0], args[1], const_args[1],
                           args[3]);
        break;
    case INDEX_op_setcond_i32:
        tcg_out_setcond_i32(s, args[3], args[0], args[1],
                            args[2], const_args[2]);
        break;
    case INDEX_op_movcond_i32:
        tcg_out_movcond_i32(s, args[5], args[0], args[1],
                            args[2], const_args[2], args[3], const_args[3]);
        break;

#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2_i32(s, args[4], args[0], args[1],
                            args[2], const_args[2],
                            args[3], const_args[3], args[5]);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2_i32(s, args[5], args[0], args[1], args[2],
                             args[3], const_args[3],
                             args[4], const_args[4]);
        break;
#endif

    case INDEX_op_add2_i32:
        tcg_out_addsub2(s, args[0], args[1], args[2], args[3],
                        args[4], const_args[4], args[5], const_args[5],
                        ARITH_ADDCC, ARITH_ADDX);
        break;
    case INDEX_op_sub2_i32:
        tcg_out_addsub2(s, args[0], args[1], args[2], args[3],
                        args[4], const_args[4], args[5], const_args[5],
                        ARITH_SUBCC, ARITH_SUBX);
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_arithc(s, args[0], args[2], args[3], const_args[3],
                       ARITH_UMUL);
        tcg_out_rdy(s, args[1]);
        break;

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld(s, args, 0);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld(s, args, 0 | 4);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld(s, args, 1);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld(s, args, 1 | 4);
        break;
    case INDEX_op_qemu_ld32:
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_qemu_ld32u:
#endif
        tcg_out_qemu_ld(s, args, 2);
        break;
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_qemu_ld32s:
        tcg_out_qemu_ld(s, args, 2 | 4);
        break;
#endif
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, args, 3);
        break;
    case INDEX_op_qemu_st8:
        tcg_out_qemu_st(s, args, 0);
        break;
    case INDEX_op_qemu_st16:
        tcg_out_qemu_st(s, args, 1);
        break;
    case INDEX_op_qemu_st32:
        tcg_out_qemu_st(s, args, 2);
        break;
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st(s, args, 3);
        break;

#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_movi_i64:
        tcg_out_movi(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_ldst(s, args[0], args[1], args[2], LDSW);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldst(s, args[0], args[1], args[2], LDX);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldst(s, args[0], args[1], args[2], STX);
        break;
    case INDEX_op_shl_i64:
        c = SHIFT_SLLX;
    do_shift64:
        /* Limit immediate shift count lest we create an illegal insn.  */
        tcg_out_arithc(s, args[0], args[1], args[2] & 63, const_args[2], c);
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
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
        tcg_out_arithc(s, TCG_REG_T1, args[1], args[2], const_args[2],
                       opc == INDEX_op_rem_i64 ? ARITH_SDIVX : ARITH_UDIVX);
        tcg_out_arithc(s, TCG_REG_T1, TCG_REG_T1, args[2], const_args[2],
                       ARITH_MULX);
        tcg_out_arith(s, args[0], args[1], TCG_REG_T1, ARITH_SUB);
        break;
    case INDEX_op_ext32s_i64:
        if (const_args[1]) {
            tcg_out_movi(s, TCG_TYPE_I64, args[0], (int32_t)args[1]);
        } else {
            tcg_out_arithi(s, args[0], args[1], 0, SHIFT_SRA);
        }
        break;
    case INDEX_op_ext32u_i64:
        if (const_args[1]) {
            tcg_out_movi_imm32(s, args[0], args[1]);
        } else {
            tcg_out_arithi(s, args[0], args[1], 0, SHIFT_SRL);
        }
        break;

    case INDEX_op_brcond_i64:
        tcg_out_brcond_i64(s, args[2], args[0], args[1], const_args[1],
                           args[3]);
        break;
    case INDEX_op_setcond_i64:
        tcg_out_setcond_i64(s, args[3], args[0], args[1],
                            args[2], const_args[2]);
        break;
    case INDEX_op_movcond_i64:
        tcg_out_movcond_i64(s, args[5], args[0], args[1],
                            args[2], const_args[2], args[3], const_args[3]);
        break;
#endif
    gen_arith:
        tcg_out_arithc(s, args[0], args[1], args[2], const_args[2], c);
        break;

    gen_arith1:
	tcg_out_arithc(s, args[0], TCG_REG_G0, args[1], const_args[1], c);
	break;

    default:
        fprintf(stderr, "unknown opcode 0x%x\n", opc);
        tcg_abort();
    }
}

static const TCGTargetOpDef sparc_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },
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
    { INDEX_op_rem_i32, { "r", "rZ", "rJ" } },
    { INDEX_op_remu_i32, { "r", "rZ", "rJ" } },
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

#if TCG_TARGET_REG_BITS == 32
    { INDEX_op_brcond2_i32, { "rZ", "rZ", "rJ", "rJ" } },
    { INDEX_op_setcond2_i32, { "r", "rZ", "rZ", "rJ", "rJ" } },
#endif

    { INDEX_op_add2_i32, { "r", "r", "rZ", "rZ", "rJ", "rJ" } },
    { INDEX_op_sub2_i32, { "r", "r", "rZ", "rZ", "rJ", "rJ" } },
    { INDEX_op_mulu2_i32, { "r", "r", "rZ", "rJ" } },

#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_mov_i64, { "r", "r" } },
    { INDEX_op_movi_i64, { "r" } },
    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },
    { INDEX_op_st8_i64, { "rZ", "r" } },
    { INDEX_op_st16_i64, { "rZ", "r" } },
    { INDEX_op_st32_i64, { "rZ", "r" } },
    { INDEX_op_st_i64, { "rZ", "r" } },

    { INDEX_op_add_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_mul_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_div_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_divu_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_rem_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_remu_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_sub_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_and_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_andc_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_or_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_orc_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_xor_i64, { "r", "rZ", "rJ" } },

    { INDEX_op_shl_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_shr_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_sar_i64, { "r", "rZ", "rJ" } },

    { INDEX_op_neg_i64, { "r", "rJ" } },
    { INDEX_op_not_i64, { "r", "rJ" } },

    { INDEX_op_ext32s_i64, { "r", "ri" } },
    { INDEX_op_ext32u_i64, { "r", "ri" } },

    { INDEX_op_brcond_i64, { "rZ", "rJ" } },
    { INDEX_op_setcond_i64, { "r", "rZ", "rJ" } },
    { INDEX_op_movcond_i64, { "r", "rZ", "rJ", "rI", "0" } },
#endif

#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L" } },
    { INDEX_op_qemu_ld32s, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "L" } },

    { INDEX_op_qemu_st8, { "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L" } },
#elif TARGET_LONG_BITS <= TCG_TARGET_REG_BITS
    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L" } },

    { INDEX_op_qemu_st8, { "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L", "L" } },
#else
    { INDEX_op_qemu_ld8u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L", "L" } },
    { INDEX_op_qemu_ld64, { "L", "L", "L", "L" } },

    { INDEX_op_qemu_st8, { "L", "L", "L" } },
    { INDEX_op_qemu_st16, { "L", "L", "L" } },
    { INDEX_op_qemu_st32, { "L", "L", "L" } },
    { INDEX_op_qemu_st64, { "L", "L", "L", "L" } },
#endif

    { -1 },
};

static void tcg_target_init(TCGContext *s)
{
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
#if TCG_TARGET_REG_BITS == 64
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffffffff);
#endif
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

#if TCG_TARGET_REG_BITS == 64
# define ELF_HOST_MACHINE  EM_SPARCV9
#else
# define ELF_HOST_MACHINE  EM_SPARC32PLUS
# define ELF_HOST_FLAGS    EF_SPARC_32PLUS
#endif

typedef struct {
    uint32_t len __attribute__((aligned((sizeof(void *)))));
    uint32_t id;
    uint8_t version;
    char augmentation[1];
    uint8_t code_align;
    uint8_t data_align;
    uint8_t return_column;
} DebugFrameCIE;

typedef struct {
    uint32_t len __attribute__((aligned((sizeof(void *)))));
    uint32_t cie_offset;
    tcg_target_long func_start __attribute__((packed));
    tcg_target_long func_len __attribute__((packed));
    uint8_t def_cfa[TCG_TARGET_REG_BITS == 64 ? 4 : 2];
    uint8_t win_save;
    uint8_t ret_save[3];
} DebugFrameFDE;

typedef struct {
    DebugFrameCIE cie;
    DebugFrameFDE fde;
} DebugFrame;

static DebugFrame debug_frame = {
    .cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .cie.id = -1,
    .cie.version = 1,
    .cie.code_align = 1,
    .cie.data_align = -sizeof(void *) & 0x7f,
    .cie.return_column = 15,            /* o7 */

    .fde.len = sizeof(DebugFrameFDE)-4, /* length after .len member */
    .fde.def_cfa = {
#if TCG_TARGET_REG_BITS == 64
        12, 30,                         /* DW_CFA_def_cfa i6, 2047 */
        (2047 & 0x7f) | 0x80, (2047 >> 7)
#else
        13, 30                          /* DW_CFA_def_cfa_register i6 */
#endif
    },
    .fde.win_save = 0x2d,               /* DW_CFA_GNU_window_save */
    .fde.ret_save = { 9, 15, 31 },      /* DW_CFA_register o7, i7 */
};

void tcg_register_jit(void *buf, size_t buf_size)
{
    debug_frame.fde.func_start = (tcg_target_long) buf;
    debug_frame.fde.func_len = buf_size;

    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}

void tb_set_jmp_target1(uintptr_t jmp_addr, uintptr_t addr)
{
    uint32_t *ptr = (uint32_t *)jmp_addr;
    tcg_target_long disp = (tcg_target_long)(addr - jmp_addr) >> 2;

    /* We can reach the entire address space for 32-bit.  For 64-bit
       the code_gen_buffer can't be larger than 2GB.  */
    if (TCG_TARGET_REG_BITS == 64 && !check_fit_tl(disp, 30)) {
        tcg_abort();
    }

    *ptr = CALL | (disp & 0x3fffffff);
    flush_icache_range(jmp_addr, jmp_addr + 4);
}
