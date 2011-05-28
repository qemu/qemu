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
    "%r0", "%r1", "%rp", "%r3", "%r4", "%r5", "%r6", "%r7",
    "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15",
    "%r16", "%r17", "%r18", "%r19", "%r20", "%r21", "%r22", "%r23",
    "%r24", "%r25", "%r26", "%dp", "%ret0", "%ret1", "%sp", "%r31",
};
#endif

/* This is an 8 byte temp slot in the stack frame.  */
#define STACK_TEMP_OFS -16

#ifdef CONFIG_USE_GUEST_BASE
#define TCG_GUEST_BASE_REG TCG_REG_R16
#else
#define TCG_GUEST_BASE_REG TCG_REG_R0
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,

    TCG_REG_R17,
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,

    TCG_REG_R26,
    TCG_REG_R25,
    TCG_REG_R24,
    TCG_REG_R23,

    TCG_REG_RET0,
    TCG_REG_RET1,
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_R26,
    TCG_REG_R25,
    TCG_REG_R24,
    TCG_REG_R23,
};

static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_RET0,
    TCG_REG_RET1,
};

/* True iff val fits a signed field of width BITS.  */
static inline int check_fit_tl(tcg_target_long val, unsigned int bits)
{
    return (val << ((sizeof(tcg_target_long) * 8 - bits))
            >> (sizeof(tcg_target_long) * 8 - bits)) == val;
}

/* True iff depi can be used to compute (reg | MASK).
   Accept a bit pattern like:
      0....01....1
      1....10....0
      0..01..10..0
   Copied from gcc sources.  */
static inline int or_mask_p(tcg_target_ulong mask)
{
    if (mask == 0 || mask == -1) {
        return 0;
    }
    mask += mask & -mask;
    return (mask & (mask - 1)) == 0;
}

/* True iff depi or extru can be used to compute (reg & mask).
   Accept a bit pattern like these:
      0....01....1
      1....10....0
      1..10..01..1
   Copied from gcc sources.  */
static inline int and_mask_p(tcg_target_ulong mask)
{
    return or_mask_p(~mask);
}

static int low_sign_ext(int val, int len)
{
    return (((val << 1) & ~(-1u << len)) | ((val >> (len - 1)) & 1));
}

static int reassemble_12(int as12)
{
    return (((as12 & 0x800) >> 11) |
            ((as12 & 0x400) >> 8) |
            ((as12 & 0x3ff) << 3));
}

static int reassemble_17(int as17)
{
    return (((as17 & 0x10000) >> 16) |
            ((as17 & 0x0f800) << 5) |
            ((as17 & 0x00400) >> 8) |
            ((as17 & 0x003ff) << 3));
}

static int reassemble_21(int as21)
{
    return (((as21 & 0x100000) >> 20) |
            ((as21 & 0x0ffe00) >> 8) |
            ((as21 & 0x000180) << 7) |
            ((as21 & 0x00007c) << 14) |
            ((as21 & 0x000003) << 12));
}

/* ??? Bizzarely, there is no PCREL12F relocation type.  I guess all
   such relocations are simply fully handled by the assembler.  */
#define R_PARISC_PCREL12F  R_PARISC_NONE

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    uint32_t *insn_ptr = (uint32_t *)code_ptr;
    uint32_t insn = *insn_ptr;
    tcg_target_long pcrel;

    value += addend;
    pcrel = (value - ((tcg_target_long)code_ptr + 8)) >> 2;

    switch (type) {
    case R_PARISC_PCREL12F:
        assert(check_fit_tl(pcrel, 12));
        /* ??? We assume all patches are forward.  See tcg_out_brcond
           re setting the NUL bit on the branch and eliding the nop.  */
        assert(pcrel >= 0);
        insn &= ~0x1ffdu;
        insn |= reassemble_12(pcrel);
        break;
    case R_PARISC_PCREL17F:
        assert(check_fit_tl(pcrel, 17));
        insn &= ~0x1f1ffdu;
        insn |= reassemble_17(pcrel);
        break;
    default:
        tcg_abort();
    }

    *insn_ptr = insn;
}

/* maximum number of register used for input function arguments */
static inline int tcg_target_get_call_iarg_regs_count(int flags)
{
    return 4;
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
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R26);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R25);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R24);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R23);
        break;
    case 'Z':
        ct->ct |= TCG_CT_CONST_0;
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_S11;
        break;
    case 'J':
        ct->ct |= TCG_CT_CONST_S5;
	break;
    case 'K':
        ct->ct |= TCG_CT_CONST_MS11;
        break;
    case 'M':
        ct->ct |= TCG_CT_CONST_AND;
        break;
    case 'O':
        ct->ct |= TCG_CT_CONST_OR;
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;
    return 0;
}

/* test if a constant matches the constraint */
static int tcg_target_const_match(tcg_target_long val,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    } else if (ct & TCG_CT_CONST_0) {
        return val == 0;
    } else if (ct & TCG_CT_CONST_S5) {
        return check_fit_tl(val, 5);
    } else if (ct & TCG_CT_CONST_S11) {
        return check_fit_tl(val, 11);
    } else if (ct & TCG_CT_CONST_MS11) {
        return check_fit_tl(-val, 11);
    } else if (ct & TCG_CT_CONST_AND) {
        return and_mask_p(val);
    } else if (ct & TCG_CT_CONST_OR) {
        return or_mask_p(val);
    }
    return 0;
}

#define INSN_OP(x)       ((x) << 26)
#define INSN_EXT3BR(x)   ((x) << 13)
#define INSN_EXT3SH(x)   ((x) << 10)
#define INSN_EXT4(x)     ((x) << 6)
#define INSN_EXT5(x)     (x)
#define INSN_EXT6(x)     ((x) << 6)
#define INSN_EXT7(x)     ((x) << 6)
#define INSN_EXT8A(x)    ((x) << 6)
#define INSN_EXT8B(x)    ((x) << 5)
#define INSN_T(x)        (x)
#define INSN_R1(x)       ((x) << 16)
#define INSN_R2(x)       ((x) << 21)
#define INSN_DEP_LEN(x)  (32 - (x))
#define INSN_SHDEP_CP(x) ((31 - (x)) << 5)
#define INSN_SHDEP_P(x)  ((x) << 5)
#define INSN_COND(x)     ((x) << 13)
#define INSN_IM11(x)     low_sign_ext(x, 11)
#define INSN_IM14(x)     low_sign_ext(x, 14)
#define INSN_IM5(x)      (low_sign_ext(x, 5) << 16)

#define COND_NEVER   0
#define COND_EQ      1
#define COND_LT      2
#define COND_LE      3
#define COND_LTU     4
#define COND_LEU     5
#define COND_SV      6
#define COND_OD      7
#define COND_FALSE   8

#define INSN_ADD	(INSN_OP(0x02) | INSN_EXT6(0x18))
#define INSN_ADDC	(INSN_OP(0x02) | INSN_EXT6(0x1c))
#define INSN_ADDI	(INSN_OP(0x2d))
#define INSN_ADDIL	(INSN_OP(0x0a))
#define INSN_ADDL	(INSN_OP(0x02) | INSN_EXT6(0x28))
#define INSN_AND	(INSN_OP(0x02) | INSN_EXT6(0x08))
#define INSN_ANDCM	(INSN_OP(0x02) | INSN_EXT6(0x00))
#define INSN_COMCLR	(INSN_OP(0x02) | INSN_EXT6(0x22))
#define INSN_COMICLR	(INSN_OP(0x24))
#define INSN_DEP	(INSN_OP(0x35) | INSN_EXT3SH(3))
#define INSN_DEPI	(INSN_OP(0x35) | INSN_EXT3SH(7))
#define INSN_EXTRS	(INSN_OP(0x34) | INSN_EXT3SH(7))
#define INSN_EXTRU	(INSN_OP(0x34) | INSN_EXT3SH(6))
#define INSN_LDIL	(INSN_OP(0x08))
#define INSN_LDO	(INSN_OP(0x0d))
#define INSN_MTCTL	(INSN_OP(0x00) | INSN_EXT8B(0xc2))
#define INSN_OR		(INSN_OP(0x02) | INSN_EXT6(0x09))
#define INSN_SHD	(INSN_OP(0x34) | INSN_EXT3SH(2))
#define INSN_SUB	(INSN_OP(0x02) | INSN_EXT6(0x10))
#define INSN_SUBB	(INSN_OP(0x02) | INSN_EXT6(0x14))
#define INSN_SUBI	(INSN_OP(0x25))
#define INSN_VEXTRS	(INSN_OP(0x34) | INSN_EXT3SH(5))
#define INSN_VEXTRU	(INSN_OP(0x34) | INSN_EXT3SH(4))
#define INSN_VSHD	(INSN_OP(0x34) | INSN_EXT3SH(0))
#define INSN_XOR	(INSN_OP(0x02) | INSN_EXT6(0x0a))
#define INSN_ZDEP	(INSN_OP(0x35) | INSN_EXT3SH(2))
#define INSN_ZVDEP	(INSN_OP(0x35) | INSN_EXT3SH(0))

#define INSN_BL         (INSN_OP(0x3a) | INSN_EXT3BR(0))
#define INSN_BL_N       (INSN_OP(0x3a) | INSN_EXT3BR(0) | 2)
#define INSN_BLR        (INSN_OP(0x3a) | INSN_EXT3BR(2))
#define INSN_BV         (INSN_OP(0x3a) | INSN_EXT3BR(6))
#define INSN_BV_N       (INSN_OP(0x3a) | INSN_EXT3BR(6) | 2)
#define INSN_BLE_SR4    (INSN_OP(0x39) | (1 << 13))

#define INSN_LDB        (INSN_OP(0x10))
#define INSN_LDH        (INSN_OP(0x11))
#define INSN_LDW        (INSN_OP(0x12))
#define INSN_LDWM       (INSN_OP(0x13))
#define INSN_FLDDS      (INSN_OP(0x0b) | INSN_EXT4(0) | (1 << 12))

#define INSN_LDBX	(INSN_OP(0x03) | INSN_EXT4(0))
#define INSN_LDHX	(INSN_OP(0x03) | INSN_EXT4(1))
#define INSN_LDWX       (INSN_OP(0x03) | INSN_EXT4(2))

#define INSN_STB        (INSN_OP(0x18))
#define INSN_STH        (INSN_OP(0x19))
#define INSN_STW        (INSN_OP(0x1a))
#define INSN_STWM       (INSN_OP(0x1b))
#define INSN_FSTDS      (INSN_OP(0x0b) | INSN_EXT4(8) | (1 << 12))

#define INSN_COMBT      (INSN_OP(0x20))
#define INSN_COMBF      (INSN_OP(0x22))
#define INSN_COMIBT     (INSN_OP(0x21))
#define INSN_COMIBF     (INSN_OP(0x23))

/* supplied by libgcc */
extern void *__canonicalize_funcptr_for_compare(void *);

static void tcg_out_mov(TCGContext *s, TCGType type, int ret, int arg)
{
    /* PA1.1 defines COPY as OR r,0,t; PA2.0 defines COPY as LDO 0(r),t
       but hppa-dis.c is unaware of this definition */
    if (ret != arg) {
        tcg_out32(s, INSN_OR | INSN_T(ret) | INSN_R1(arg)
                  | INSN_R2(TCG_REG_R0));
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         int ret, tcg_target_long arg)
{
    if (check_fit_tl(arg, 14)) {
        tcg_out32(s, INSN_LDO | INSN_R1(ret)
                  | INSN_R2(TCG_REG_R0) | INSN_IM14(arg));
    } else {
        uint32_t hi, lo;
        hi = arg >> 11;
        lo = arg & 0x7ff;

        tcg_out32(s, INSN_LDIL | INSN_R2(ret) | reassemble_21(hi));
        if (lo) {
            tcg_out32(s, INSN_LDO | INSN_R1(ret)
                      | INSN_R2(ret) | INSN_IM14(lo));
        }
    }
}

static void tcg_out_ldst(TCGContext *s, int ret, int addr,
                         tcg_target_long offset, int op)
{
    if (!check_fit_tl(offset, 14)) {
        uint32_t hi, lo, op;

        hi = offset >> 11;
        lo = offset & 0x7ff;

        if (addr == TCG_REG_R0) {
            op = INSN_LDIL | INSN_R2(TCG_REG_R1);
        } else {
            op = INSN_ADDIL | INSN_R2(addr);
        }
        tcg_out32(s, op | reassemble_21(hi));

        addr = TCG_REG_R1;
	offset = lo;
    }

    if (ret != addr || offset != 0 || op != INSN_LDO) {
        tcg_out32(s, op | INSN_R1(ret) | INSN_R2(addr) | INSN_IM14(offset));
    }
}

/* This function is required by tcg.c.  */
static inline void tcg_out_ld(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, ret, arg1, arg2, INSN_LDW);
}

/* This function is required by tcg.c.  */
static inline void tcg_out_st(TCGContext *s, TCGType type, int ret,
                              int arg1, tcg_target_long arg2)
{
    tcg_out_ldst(s, ret, arg1, arg2, INSN_STW);
}

static void tcg_out_ldst_index(TCGContext *s, int data,
                               int base, int index, int op)
{
    tcg_out32(s, op | INSN_T(data) | INSN_R1(index) | INSN_R2(base));
}

static inline void tcg_out_addi2(TCGContext *s, int ret, int arg1,
                                 tcg_target_long val)
{
    tcg_out_ldst(s, ret, arg1, val, INSN_LDO);
}

/* This function is required by tcg.c.  */
static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    tcg_out_addi2(s, reg, reg, val);
}

static inline void tcg_out_arith(TCGContext *s, int t, int r1, int r2, int op)
{
    tcg_out32(s, op | INSN_T(t) | INSN_R1(r1) | INSN_R2(r2));
}

static inline void tcg_out_arithi(TCGContext *s, int t, int r1,
                                  tcg_target_long val, int op)
{
    assert(check_fit_tl(val, 11));
    tcg_out32(s, op | INSN_R1(t) | INSN_R2(r1) | INSN_IM11(val));
}

static inline void tcg_out_nop(TCGContext *s)
{
    tcg_out_arith(s, TCG_REG_R0, TCG_REG_R0, TCG_REG_R0, INSN_OR);
}

static inline void tcg_out_mtctl_sar(TCGContext *s, int arg)
{
    tcg_out32(s, INSN_MTCTL | INSN_R2(11) | INSN_R1(arg));
}

/* Extract LEN bits at position OFS from ARG and place in RET.
   Note that here the bit ordering is reversed from the PA-RISC
   standard, such that the right-most bit is 0.  */
static inline void tcg_out_extr(TCGContext *s, int ret, int arg,
                                unsigned ofs, unsigned len, int sign)
{
    assert(ofs < 32 && len <= 32 - ofs);
    tcg_out32(s, (sign ? INSN_EXTRS : INSN_EXTRU)
              | INSN_R1(ret) | INSN_R2(arg)
              | INSN_SHDEP_P(31 - ofs) | INSN_DEP_LEN(len));
}

/* Likewise with OFS interpreted little-endian.  */
static inline void tcg_out_dep(TCGContext *s, int ret, int arg,
                               unsigned ofs, unsigned len)
{
    assert(ofs < 32 && len <= 32 - ofs);
    tcg_out32(s, INSN_DEP | INSN_R2(ret) | INSN_R1(arg)
              | INSN_SHDEP_CP(31 - ofs) | INSN_DEP_LEN(len));
}

static inline void tcg_out_shd(TCGContext *s, int ret, int hi, int lo,
                               unsigned count)
{
    assert(count < 32);
    tcg_out32(s, INSN_SHD | INSN_R1(hi) | INSN_R2(lo) | INSN_T(ret)
              | INSN_SHDEP_CP(count));
}

static void tcg_out_vshd(TCGContext *s, int ret, int hi, int lo, int creg)
{
    tcg_out_mtctl_sar(s, creg);
    tcg_out32(s, INSN_VSHD | INSN_T(ret) | INSN_R1(hi) | INSN_R2(lo));
}

static void tcg_out_ori(TCGContext *s, int ret, int arg, tcg_target_ulong m)
{
    int bs0, bs1;

    /* Note that the argument is constrained to match or_mask_p.  */
    for (bs0 = 0; bs0 < 32; bs0++) {
        if ((m & (1u << bs0)) != 0) {
            break;
        }
    }
    for (bs1 = bs0; bs1 < 32; bs1++) {
        if ((m & (1u << bs1)) == 0) {
            break;
        }
    }
    assert(bs1 == 32 || (1ul << bs1) > m);

    tcg_out_mov(s, TCG_TYPE_I32, ret, arg);
    tcg_out32(s, INSN_DEPI | INSN_R2(ret) | INSN_IM5(-1)
              | INSN_SHDEP_CP(31 - bs0) | INSN_DEP_LEN(bs1 - bs0));
}

static void tcg_out_andi(TCGContext *s, int ret, int arg, tcg_target_ulong m)
{
    int ls0, ls1, ms0;

    /* Note that the argument is constrained to match and_mask_p.  */
    for (ls0 = 0; ls0 < 32; ls0++) {
        if ((m & (1u << ls0)) == 0) {
            break;
        }
    }
    for (ls1 = ls0; ls1 < 32; ls1++) {
        if ((m & (1u << ls1)) != 0) {
            break;
        }
    }
    for (ms0 = ls1; ms0 < 32; ms0++) {
        if ((m & (1u << ms0)) == 0) {
            break;
        }
    }
    assert (ms0 == 32);

    if (ls1 == 32) {
        tcg_out_extr(s, ret, arg, 0, ls0, 0);
    } else {
        tcg_out_mov(s, TCG_TYPE_I32, ret, arg);
        tcg_out32(s, INSN_DEPI | INSN_R2(ret) | INSN_IM5(0)
                  | INSN_SHDEP_CP(31 - ls0) | INSN_DEP_LEN(ls1 - ls0));
    }
}

static inline void tcg_out_ext8s(TCGContext *s, int ret, int arg)
{
    tcg_out_extr(s, ret, arg, 0, 8, 1);
}

static inline void tcg_out_ext16s(TCGContext *s, int ret, int arg)
{
    tcg_out_extr(s, ret, arg, 0, 16, 1);
}

static void tcg_out_shli(TCGContext *s, int ret, int arg, int count)
{
    count &= 31;
    tcg_out32(s, INSN_ZDEP | INSN_R2(ret) | INSN_R1(arg)
              | INSN_SHDEP_CP(31 - count) | INSN_DEP_LEN(32 - count));
}

static void tcg_out_shl(TCGContext *s, int ret, int arg, int creg)
{
    tcg_out_arithi(s, TCG_REG_R20, creg, 31, INSN_SUBI);
    tcg_out_mtctl_sar(s, TCG_REG_R20);
    tcg_out32(s, INSN_ZVDEP | INSN_R2(ret) | INSN_R1(arg) | INSN_DEP_LEN(32));
}

static void tcg_out_shri(TCGContext *s, int ret, int arg, int count)
{
    count &= 31;
    tcg_out_extr(s, ret, arg, count, 32 - count, 0);
}

static void tcg_out_shr(TCGContext *s, int ret, int arg, int creg)
{
    tcg_out_vshd(s, ret, TCG_REG_R0, arg, creg);
}

static void tcg_out_sari(TCGContext *s, int ret, int arg, int count)
{
    count &= 31;
    tcg_out_extr(s, ret, arg, count, 32 - count, 1);
}

static void tcg_out_sar(TCGContext *s, int ret, int arg, int creg)
{
    tcg_out_arithi(s, TCG_REG_R20, creg, 31, INSN_SUBI);
    tcg_out_mtctl_sar(s, TCG_REG_R20);
    tcg_out32(s, INSN_VEXTRS | INSN_R1(ret) | INSN_R2(arg) | INSN_DEP_LEN(32));
}

static void tcg_out_rotli(TCGContext *s, int ret, int arg, int count)
{
    count &= 31;
    tcg_out_shd(s, ret, arg, arg, 32 - count);
}

static void tcg_out_rotl(TCGContext *s, int ret, int arg, int creg)
{
    tcg_out_arithi(s, TCG_REG_R20, creg, 32, INSN_SUBI);
    tcg_out_vshd(s, ret, arg, arg, TCG_REG_R20);
}

static void tcg_out_rotri(TCGContext *s, int ret, int arg, int count)
{
    count &= 31;
    tcg_out_shd(s, ret, arg, arg, count);
}

static void tcg_out_rotr(TCGContext *s, int ret, int arg, int creg)
{
    tcg_out_vshd(s, ret, arg, arg, creg);
}

static void tcg_out_bswap16(TCGContext *s, int ret, int arg, int sign)
{
    if (ret != arg) {
        tcg_out_mov(s, TCG_TYPE_I32, ret, arg); /* arg =  xxAB */
    }
    tcg_out_dep(s, ret, ret, 16, 8);          /* ret =  xBAB */
    tcg_out_extr(s, ret, ret, 8, 16, sign);   /* ret =  ..BA */
}

static void tcg_out_bswap32(TCGContext *s, int ret, int arg, int temp)
{
                                          /* arg =  ABCD */
    tcg_out_rotri(s, temp, arg, 16);      /* temp = CDAB */
    tcg_out_dep(s, temp, temp, 16, 8);    /* temp = CBAB */
    tcg_out_shd(s, ret, arg, temp, 8);    /* ret =  DCBA */
}

static void tcg_out_call(TCGContext *s, void *func)
{
    tcg_target_long val, hi, lo, disp;

    val = (uint32_t)__canonicalize_funcptr_for_compare(func);
    disp = (val - ((tcg_target_long)s->code_ptr + 8)) >> 2;

    if (check_fit_tl(disp, 17)) {
        tcg_out32(s, INSN_BL_N | INSN_R2(TCG_REG_RP) | reassemble_17(disp));
    } else {
        hi = val >> 11;
        lo = val & 0x7ff;

        tcg_out32(s, INSN_LDIL | INSN_R2(TCG_REG_R20) | reassemble_21(hi));
        tcg_out32(s, INSN_BLE_SR4 | INSN_R2(TCG_REG_R20)
                  | reassemble_17(lo >> 2));
        tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_RP, TCG_REG_R31);
    }
}

static void tcg_out_xmpyu(TCGContext *s, int retl, int reth,
                          int arg1, int arg2)
{
    /* Store both words into the stack for copy to the FPU.  */
    tcg_out_ldst(s, arg1, TCG_REG_CALL_STACK, STACK_TEMP_OFS, INSN_STW);
    tcg_out_ldst(s, arg2, TCG_REG_CALL_STACK, STACK_TEMP_OFS + 4, INSN_STW);

    /* Load both words into the FPU at the same time.  We get away
       with this because we can address the left and right half of the
       FPU registers individually once loaded.  */
    /* fldds stack_temp(sp),fr22 */
    tcg_out32(s, INSN_FLDDS | INSN_R2(TCG_REG_CALL_STACK)
              | INSN_IM5(STACK_TEMP_OFS) | INSN_T(22));

    /* xmpyu fr22r,fr22,fr22 */
    tcg_out32(s, 0x3ad64796);

    /* Store the 64-bit result back into the stack.  */
    /* fstds stack_temp(sp),fr22 */
    tcg_out32(s, INSN_FSTDS | INSN_R2(TCG_REG_CALL_STACK)
              | INSN_IM5(STACK_TEMP_OFS) | INSN_T(22));

    /* Load the pieces of the result that the caller requested.  */
    if (reth) {
        tcg_out_ldst(s, reth, TCG_REG_CALL_STACK, STACK_TEMP_OFS, INSN_LDW);
    }
    if (retl) {
        tcg_out_ldst(s, retl, TCG_REG_CALL_STACK, STACK_TEMP_OFS + 4,
                     INSN_LDW);
    }
}

static void tcg_out_add2(TCGContext *s, int destl, int desth,
                         int al, int ah, int bl, int bh, int blconst)
{
    int tmp = (destl == ah || destl == bh ? TCG_REG_R20 : destl);

    if (blconst) {
        tcg_out_arithi(s, tmp, al, bl, INSN_ADDI);
    } else {
        tcg_out_arith(s, tmp, al, bl, INSN_ADD);
    }
    tcg_out_arith(s, desth, ah, bh, INSN_ADDC);

    tcg_out_mov(s, TCG_TYPE_I32, destl, tmp);
}

static void tcg_out_sub2(TCGContext *s, int destl, int desth, int al, int ah,
                         int bl, int bh, int alconst, int blconst)
{
    int tmp = (destl == ah || destl == bh ? TCG_REG_R20 : destl);

    if (alconst) {
        if (blconst) {
            tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R20, bl);
            bl = TCG_REG_R20;
        }
        tcg_out_arithi(s, tmp, bl, al, INSN_SUBI);
    } else if (blconst) {
        tcg_out_arithi(s, tmp, al, -bl, INSN_ADDI);
    } else {
        tcg_out_arith(s, tmp, al, bl, INSN_SUB);
    }
    tcg_out_arith(s, desth, ah, bh, INSN_SUBB);

    tcg_out_mov(s, TCG_TYPE_I32, destl, tmp);
}

static void tcg_out_branch(TCGContext *s, int label_index, int nul)
{
    TCGLabel *l = &s->labels[label_index];
    uint32_t op = nul ? INSN_BL_N : INSN_BL;

    if (l->has_value) {
        tcg_target_long val = l->u.value;

        val -= (tcg_target_long)s->code_ptr + 8;
        val >>= 2;
        assert(check_fit_tl(val, 17));

        tcg_out32(s, op | reassemble_17(val));
    } else {
        /* We need to keep the offset unchanged for retranslation.  */
        uint32_t old_insn = *(uint32_t *)s->code_ptr;

        tcg_out_reloc(s, s->code_ptr, R_PARISC_PCREL17F, label_index, 0);
        tcg_out32(s, op | (old_insn & 0x1f1ffdu));
    }
}

static const uint8_t tcg_cond_to_cmp_cond[10] =
{
    [TCG_COND_EQ] = COND_EQ,
    [TCG_COND_NE] = COND_EQ | COND_FALSE,
    [TCG_COND_LT] = COND_LT,
    [TCG_COND_GE] = COND_LT | COND_FALSE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_LE | COND_FALSE,
    [TCG_COND_LTU] = COND_LTU,
    [TCG_COND_GEU] = COND_LTU | COND_FALSE,
    [TCG_COND_LEU] = COND_LEU,
    [TCG_COND_GTU] = COND_LEU | COND_FALSE,
};

static void tcg_out_brcond(TCGContext *s, int cond, TCGArg c1,
                           TCGArg c2, int c2const, int label_index)
{
    TCGLabel *l = &s->labels[label_index];
    int op, pacond;

    /* Note that COMIB operates as if the immediate is the first
       operand.  We model brcond with the immediate in the second
       to better match what targets are likely to give us.  For
       consistency, model COMB with reversed operands as well.  */
    pacond = tcg_cond_to_cmp_cond[tcg_swap_cond(cond)];

    if (c2const) {
        op = (pacond & COND_FALSE ? INSN_COMIBF : INSN_COMIBT);
        op |= INSN_IM5(c2);
    } else {
        op = (pacond & COND_FALSE ? INSN_COMBF : INSN_COMBT);
        op |= INSN_R1(c2);
    }
    op |= INSN_R2(c1);
    op |= INSN_COND(pacond & 7);

    if (l->has_value) {
        tcg_target_long val = l->u.value;

        val -= (tcg_target_long)s->code_ptr + 8;
        val >>= 2;
        assert(check_fit_tl(val, 12));

        /* ??? Assume that all branches to defined labels are backward.
           Which means that if the nul bit is set, the delay slot is
           executed if the branch is taken, and not executed in fallthru.  */
        tcg_out32(s, op | reassemble_12(val));
        tcg_out_nop(s);
    } else {
        /* We need to keep the offset unchanged for retranslation.  */
        uint32_t old_insn = *(uint32_t *)s->code_ptr;

        tcg_out_reloc(s, s->code_ptr, R_PARISC_PCREL12F, label_index, 0);
        /* ??? Assume that all branches to undefined labels are forward.
           Which means that if the nul bit is set, the delay slot is
           not executed if the branch is taken, which is what we want.  */
        tcg_out32(s, op | 2 | (old_insn & 0x1ffdu));
    }
}

static void tcg_out_comclr(TCGContext *s, int cond, TCGArg ret,
                           TCGArg c1, TCGArg c2, int c2const)
{
    int op, pacond;

    /* Note that COMICLR operates as if the immediate is the first
       operand.  We model setcond with the immediate in the second
       to better match what targets are likely to give us.  For
       consistency, model COMCLR with reversed operands as well.  */
    pacond = tcg_cond_to_cmp_cond[tcg_swap_cond(cond)];

    if (c2const) {
        op = INSN_COMICLR | INSN_R2(c1) | INSN_R1(ret) | INSN_IM11(c2);
    } else {
        op = INSN_COMCLR | INSN_R2(c1) | INSN_R1(c2) | INSN_T(ret);
    }
    op |= INSN_COND(pacond & 7);
    op |= pacond & COND_FALSE ? 1 << 12 : 0;

    tcg_out32(s, op);
}

static void tcg_out_brcond2(TCGContext *s, int cond, TCGArg al, TCGArg ah,
                            TCGArg bl, int blconst, TCGArg bh, int bhconst,
                            int label_index)
{
    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        tcg_out_comclr(s, tcg_invert_cond(cond), TCG_REG_R0, al, bl, blconst);
        tcg_out_brcond(s, cond, ah, bh, bhconst, label_index);
        break;

    default:
        tcg_out_brcond(s, cond, ah, bh, bhconst, label_index);
        tcg_out_comclr(s, TCG_COND_NE, TCG_REG_R0, ah, bh, bhconst);
        tcg_out_brcond(s, tcg_unsigned_cond(cond),
                       al, bl, blconst, label_index);
        break;
    }
}

static void tcg_out_setcond(TCGContext *s, int cond, TCGArg ret,
                            TCGArg c1, TCGArg c2, int c2const)
{
    tcg_out_comclr(s, tcg_invert_cond(cond), ret, c1, c2, c2const);
    tcg_out_movi(s, TCG_TYPE_I32, ret, 1);
}

static void tcg_out_setcond2(TCGContext *s, int cond, TCGArg ret,
                             TCGArg al, TCGArg ah, TCGArg bl, int blconst,
                             TCGArg bh, int bhconst)
{
    int scratch = TCG_REG_R20;

    if (ret != al && ret != ah
        && (blconst || ret != bl)
        && (bhconst || ret != bh)) {
        scratch = ret;
    }

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        tcg_out_setcond(s, cond, scratch, al, bl, blconst);
        tcg_out_comclr(s, TCG_COND_EQ, TCG_REG_R0, ah, bh, bhconst);
        tcg_out_movi(s, TCG_TYPE_I32, scratch, cond == TCG_COND_NE);
        break;

    default:
        tcg_out_setcond(s, tcg_unsigned_cond(cond), scratch, al, bl, blconst);
        tcg_out_comclr(s, TCG_COND_EQ, TCG_REG_R0, ah, bh, bhconst);
        tcg_out_movi(s, TCG_TYPE_I32, scratch, 0);
        tcg_out_comclr(s, cond, TCG_REG_R0, ah, bh, bhconst);
        tcg_out_movi(s, TCG_TYPE_I32, scratch, 1);
        break;
    }

    tcg_out_mov(s, TCG_TYPE_I32, ret, scratch);
}

#if defined(CONFIG_SOFTMMU)
#include "../../softmmu_defs.h"

static void *qemu_ld_helpers[4] = {
    __ldb_mmu,
    __ldw_mmu,
    __ldl_mmu,
    __ldq_mmu,
};

static void *qemu_st_helpers[4] = {
    __stb_mmu,
    __stw_mmu,
    __stl_mmu,
    __stq_mmu,
};

/* Load and compare a TLB entry, and branch if TLB miss.  OFFSET is set to
   the offset of the first ADDR_READ or ADDR_WRITE member of the appropriate
   TLB for the memory index.  The return value is the offset from ENV
   contained in R1 afterward (to be used when loading ADDEND); if the
   return value is 0, R1 is not used.  */

static int tcg_out_tlb_read(TCGContext *s, int r0, int r1, int addrlo,
                            int addrhi, int s_bits, int lab_miss, int offset)
{
    int ret;

    /* Extracting the index into the TLB.  The "normal C operation" is
          r1 = addr_reg >> TARGET_PAGE_BITS;
          r1 &= CPU_TLB_SIZE - 1;
          r1 <<= CPU_TLB_ENTRY_BITS;
       What this does is extract CPU_TLB_BITS beginning at TARGET_PAGE_BITS
       and place them at CPU_TLB_ENTRY_BITS.  We can combine the first two
       operations with an EXTRU.  Unfortunately, the current value of
       CPU_TLB_ENTRY_BITS is > 3, so we can't merge that shift with the
       add that follows.  */
    tcg_out_extr(s, r1, addrlo, TARGET_PAGE_BITS, CPU_TLB_BITS, 0);
    tcg_out_shli(s, r1, r1, CPU_TLB_ENTRY_BITS);
    tcg_out_arith(s, r1, r1, TCG_AREG0, INSN_ADDL);

    /* Make sure that both the addr_{read,write} and addend can be
       read with a 14-bit offset from the same base register.  */
    if (check_fit_tl(offset + CPU_TLB_SIZE, 14)) {
        ret = 0;
    } else {
        ret = (offset + 0x400) & ~0x7ff;
        offset = ret - offset;
        tcg_out_addi2(s, TCG_REG_R1, r1, ret);
        r1 = TCG_REG_R1;
    }

    /* Load the entry from the computed slot.  */
    if (TARGET_LONG_BITS == 64) {
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R23, r1, offset);
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R20, r1, offset + 4);
    } else {
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R20, r1, offset);
    }

    /* Compute the value that ought to appear in the TLB for a hit, namely, the page
       of the address.  We include the low N bits of the address to catch unaligned
       accesses and force them onto the slow path.  Do this computation after having
       issued the load from the TLB slot to give the load time to complete.  */
    tcg_out_andi(s, r0, addrlo, TARGET_PAGE_MASK | ((1 << s_bits) - 1));

    /* If not equal, jump to lab_miss. */
    if (TARGET_LONG_BITS == 64) {
        tcg_out_brcond2(s, TCG_COND_NE, TCG_REG_R20, TCG_REG_R23,
                        r0, 0, addrhi, 0, lab_miss);
    } else {
        tcg_out_brcond(s, TCG_COND_NE, TCG_REG_R20, r0, 0, lab_miss);
    }

    return ret;
}
#endif

static void tcg_out_qemu_ld_direct(TCGContext *s, int datalo_reg, int datahi_reg,
                                   int addr_reg, int addend_reg, int opc)
{
#ifdef TARGET_WORDS_BIGENDIAN
    const int bswap = 0;
#else
    const int bswap = 1;
#endif

    switch (opc) {
    case 0:
        tcg_out_ldst_index(s, datalo_reg, addr_reg, addend_reg, INSN_LDBX);
        break;
    case 0 | 4:
        tcg_out_ldst_index(s, datalo_reg, addr_reg, addend_reg, INSN_LDBX);
        tcg_out_ext8s(s, datalo_reg, datalo_reg);
        break;
    case 1:
        tcg_out_ldst_index(s, datalo_reg, addr_reg, addend_reg, INSN_LDHX);
        if (bswap) {
            tcg_out_bswap16(s, datalo_reg, datalo_reg, 0);
        }
        break;
    case 1 | 4:
        tcg_out_ldst_index(s, datalo_reg, addr_reg, addend_reg, INSN_LDHX);
        if (bswap) {
            tcg_out_bswap16(s, datalo_reg, datalo_reg, 1);
        } else {
            tcg_out_ext16s(s, datalo_reg, datalo_reg);
        }
        break;
    case 2:
        tcg_out_ldst_index(s, datalo_reg, addr_reg, addend_reg, INSN_LDWX);
        if (bswap) {
            tcg_out_bswap32(s, datalo_reg, datalo_reg, TCG_REG_R20);
        }
        break;
    case 3:
        if (bswap) {
            int t = datahi_reg;
            datahi_reg = datalo_reg;
            datalo_reg = t;
        }
        /* We can't access the low-part with a reg+reg addressing mode,
           so perform the addition now and use reg_ofs addressing mode.  */
        if (addend_reg != TCG_REG_R0) {
            tcg_out_arith(s, TCG_REG_R20, addr_reg, addend_reg, INSN_ADD);
            addr_reg = TCG_REG_R20;
	}
        /* Make sure not to clobber the base register.  */
        if (datahi_reg == addr_reg) {
            tcg_out_ldst(s, datalo_reg, addr_reg, 4, INSN_LDW);
            tcg_out_ldst(s, datahi_reg, addr_reg, 0, INSN_LDW);
        } else {
            tcg_out_ldst(s, datahi_reg, addr_reg, 0, INSN_LDW);
            tcg_out_ldst(s, datalo_reg, addr_reg, 4, INSN_LDW);
        }
        if (bswap) {
            tcg_out_bswap32(s, datalo_reg, datalo_reg, TCG_REG_R20);
            tcg_out_bswap32(s, datahi_reg, datahi_reg, TCG_REG_R20);
        }
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, int opc)
{
    int datalo_reg = *args++;
    /* Note that datahi_reg is only used for 64-bit loads.  */
    int datahi_reg = (opc == 3 ? *args++ : TCG_REG_R0);
    int addrlo_reg = *args++;

#if defined(CONFIG_SOFTMMU)
    /* Note that addrhi_reg is only used for 64-bit guests.  */
    int addrhi_reg = (TARGET_LONG_BITS == 64 ? *args++ : TCG_REG_R0);
    int mem_index = *args;
    int lab1, lab2, argreg, offset;

    lab1 = gen_new_label();
    lab2 = gen_new_label();

    offset = offsetof(CPUState, tlb_table[mem_index][0].addr_read);
    offset = tcg_out_tlb_read(s, TCG_REG_R26, TCG_REG_R25, addrlo_reg, addrhi_reg,
                              opc & 3, lab1, offset);

    /* TLB Hit.  */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R20, (offset ? TCG_REG_R1 : TCG_REG_R25),
               offsetof(CPUState, tlb_table[mem_index][0].addend) - offset);
    tcg_out_qemu_ld_direct(s, datalo_reg, datahi_reg, addrlo_reg, TCG_REG_R20, opc);
    tcg_out_branch(s, lab2, 1);

    /* TLB Miss.  */
    /* label1: */
    tcg_out_label(s, lab1, (tcg_target_long)s->code_ptr);

    argreg = TCG_REG_R26;
    tcg_out_mov(s, TCG_TYPE_I32, argreg--, addrlo_reg);
    if (TARGET_LONG_BITS == 64) {
        tcg_out_mov(s, TCG_TYPE_I32, argreg--, addrhi_reg);
    }
    tcg_out_movi(s, TCG_TYPE_I32, argreg, mem_index);

    tcg_out_call(s, qemu_ld_helpers[opc & 3]);

    switch (opc) {
    case 0:
        tcg_out_andi(s, datalo_reg, TCG_REG_RET0, 0xff);
        break;
    case 0 | 4:
        tcg_out_ext8s(s, datalo_reg, TCG_REG_RET0);
        break;
    case 1:
        tcg_out_andi(s, datalo_reg, TCG_REG_RET0, 0xffff);
        break;
    case 1 | 4:
        tcg_out_ext16s(s, datalo_reg, TCG_REG_RET0);
        break;
    case 2:
    case 2 | 4:
        tcg_out_mov(s, TCG_TYPE_I32, datalo_reg, TCG_REG_RET0);
        break;
    case 3:
        tcg_out_mov(s, TCG_TYPE_I32, datahi_reg, TCG_REG_RET0);
        tcg_out_mov(s, TCG_TYPE_I32, datalo_reg, TCG_REG_RET1);
        break;
    default:
        tcg_abort();
    }

    /* label2: */
    tcg_out_label(s, lab2, (tcg_target_long)s->code_ptr);
#else
    tcg_out_qemu_ld_direct(s, datalo_reg, datahi_reg, addrlo_reg,
                           (GUEST_BASE ? TCG_GUEST_BASE_REG : TCG_REG_R0), opc);
#endif
}

static void tcg_out_qemu_st_direct(TCGContext *s, int datalo_reg, int datahi_reg,
                                   int addr_reg, int opc)
{
#ifdef TARGET_WORDS_BIGENDIAN
    const int bswap = 0;
#else
    const int bswap = 1;
#endif

    switch (opc) {
    case 0:
        tcg_out_ldst(s, datalo_reg, addr_reg, 0, INSN_STB);
        break;
    case 1:
        if (bswap) {
            tcg_out_bswap16(s, TCG_REG_R20, datalo_reg, 0);
            datalo_reg = TCG_REG_R20;
        }
        tcg_out_ldst(s, datalo_reg, addr_reg, 0, INSN_STH);
        break;
    case 2:
        if (bswap) {
            tcg_out_bswap32(s, TCG_REG_R20, datalo_reg, TCG_REG_R20);
            datalo_reg = TCG_REG_R20;
        }
        tcg_out_ldst(s, datalo_reg, addr_reg, 0, INSN_STW);
        break;
    case 3:
        if (bswap) {
            tcg_out_bswap32(s, TCG_REG_R20, datalo_reg, TCG_REG_R20);
            tcg_out_bswap32(s, TCG_REG_R23, datahi_reg, TCG_REG_R23);
            datahi_reg = TCG_REG_R20;
            datalo_reg = TCG_REG_R23;
        }
        tcg_out_ldst(s, datahi_reg, addr_reg, 0, INSN_STW);
        tcg_out_ldst(s, datalo_reg, addr_reg, 4, INSN_STW);
        break;
    default:
        tcg_abort();
    }

}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, int opc)
{
    int datalo_reg = *args++;
    /* Note that datahi_reg is only used for 64-bit loads.  */
    int datahi_reg = (opc == 3 ? *args++ : TCG_REG_R0);
    int addrlo_reg = *args++;

#if defined(CONFIG_SOFTMMU)
    /* Note that addrhi_reg is only used for 64-bit guests.  */
    int addrhi_reg = (TARGET_LONG_BITS == 64 ? *args++ : TCG_REG_R0);
    int mem_index = *args;
    int lab1, lab2, argreg, offset;

    lab1 = gen_new_label();
    lab2 = gen_new_label();

    offset = offsetof(CPUState, tlb_table[mem_index][0].addr_write);
    offset = tcg_out_tlb_read(s, TCG_REG_R26, TCG_REG_R25, addrlo_reg, addrhi_reg,
                              opc, lab1, offset);

    /* TLB Hit.  */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R20, (offset ? TCG_REG_R1 : TCG_REG_R25),
               offsetof(CPUState, tlb_table[mem_index][0].addend) - offset);

    /* There are no indexed stores, so we must do this addition explitly.
       Careful to avoid R20, which is used for the bswaps to follow.  */
    tcg_out_arith(s, TCG_REG_R31, addrlo_reg, TCG_REG_R20, INSN_ADDL);
    tcg_out_qemu_st_direct(s, datalo_reg, datahi_reg, TCG_REG_R31, opc);
    tcg_out_branch(s, lab2, 1);

    /* TLB Miss.  */
    /* label1: */
    tcg_out_label(s, lab1, (tcg_target_long)s->code_ptr);

    argreg = TCG_REG_R26;
    tcg_out_mov(s, TCG_TYPE_I32, argreg--, addrlo_reg);
    if (TARGET_LONG_BITS == 64) {
        tcg_out_mov(s, TCG_TYPE_I32, argreg--, addrhi_reg);
    }

    switch(opc) {
    case 0:
        tcg_out_andi(s, argreg--, datalo_reg, 0xff);
        tcg_out_movi(s, TCG_TYPE_I32, argreg, mem_index);
        break;
    case 1:
        tcg_out_andi(s, argreg--, datalo_reg, 0xffff);
        tcg_out_movi(s, TCG_TYPE_I32, argreg, mem_index);
        break;
    case 2:
        tcg_out_mov(s, TCG_TYPE_I32, argreg--, datalo_reg);
        tcg_out_movi(s, TCG_TYPE_I32, argreg, mem_index);
        break;
    case 3:
        /* Because of the alignment required by the 64-bit data argument,
           we will always use R23/R24.  Also, we will always run out of
           argument registers for storing mem_index, so that will have
           to go on the stack.  */
        if (mem_index == 0) {
            argreg = TCG_REG_R0;
        } else {
            argreg = TCG_REG_R20;
            tcg_out_movi(s, TCG_TYPE_I32, argreg, mem_index);
        }
        tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R23, datahi_reg);
        tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R24, datalo_reg);
        tcg_out_st(s, TCG_TYPE_I32, argreg, TCG_REG_CALL_STACK,
                   TCG_TARGET_CALL_STACK_OFFSET - 4);
        break;
    default:
        tcg_abort();
    }

    tcg_out_call(s, qemu_st_helpers[opc]);

    /* label2: */
    tcg_out_label(s, lab2, (tcg_target_long)s->code_ptr);
#else
    /* There are no indexed stores, so if GUEST_BASE is set we must do the add
       explicitly.  Careful to avoid R20, which is used for the bswaps to follow.  */
    if (GUEST_BASE != 0) {
        tcg_out_arith(s, TCG_REG_R31, addrlo_reg, TCG_GUEST_BASE_REG, INSN_ADDL);
        addrlo_reg = TCG_REG_R31;
    }
    tcg_out_qemu_st_direct(s, datalo_reg, datahi_reg, addrlo_reg, opc);
#endif
}

static void tcg_out_exit_tb(TCGContext *s, TCGArg arg)
{
    if (!check_fit_tl(arg, 14)) {
        uint32_t hi, lo;
        hi = arg & ~0x7ff;
        lo = arg & 0x7ff;
        if (lo) {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RET0, hi);
            tcg_out32(s, INSN_BV | INSN_R2(TCG_REG_R18));
            tcg_out_addi(s, TCG_REG_RET0, lo);
            return;
        }
        arg = hi;
    }
    tcg_out32(s, INSN_BV | INSN_R2(TCG_REG_R18));
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_RET0, arg);
}

static void tcg_out_goto_tb(TCGContext *s, TCGArg arg)
{
    if (s->tb_jmp_offset) {
        /* direct jump method */
        fprintf(stderr, "goto_tb direct\n");
        tcg_abort();
    } else {
        /* indirect jump method */
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R20, TCG_REG_R0,
                   (tcg_target_long)(s->tb_next + arg));
        tcg_out32(s, INSN_BV_N | INSN_R2(TCG_REG_R20));
    }
    s->tb_next_offset[arg] = s->code_ptr - s->code_buf;
}

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                              const int *const_args)
{
    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_exit_tb(s, args[0]);
        break;
    case INDEX_op_goto_tb:
        tcg_out_goto_tb(s, args[0]);
        break;

    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out_call(s, (void *)args[0]);
        } else {
            /* ??? FIXME: the value in the register in args[0] is almost
               certainly a procedure descriptor, not a code address.  We
               probably need to use the millicode $$dyncall routine.  */
            tcg_abort();
        }
        break;

    case INDEX_op_jmp:
        fprintf(stderr, "unimplemented jmp\n");
        tcg_abort();
        break;

    case INDEX_op_br:
        tcg_out_branch(s, args[0], 1);
        break;

    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], (uint32_t)args[1]);
        break;

    case INDEX_op_ld8u_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_LDB);
        break;
    case INDEX_op_ld8s_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_LDB);
        tcg_out_ext8s(s, args[0], args[0]);
        break;
    case INDEX_op_ld16u_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_LDH);
        break;
    case INDEX_op_ld16s_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_LDH);
        tcg_out_ext16s(s, args[0], args[0]);
        break;
    case INDEX_op_ld_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_LDW);
        break;

    case INDEX_op_st8_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_STB);
        break;
    case INDEX_op_st16_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_STH);
        break;
    case INDEX_op_st_i32:
        tcg_out_ldst(s, args[0], args[1], args[2], INSN_STW);
        break;

    case INDEX_op_add_i32:
        if (const_args[2]) {
            tcg_out_addi2(s, args[0], args[1], args[2]);
        } else {
            tcg_out_arith(s, args[0], args[1], args[2], INSN_ADDL);
        }
        break;

    case INDEX_op_sub_i32:
        if (const_args[1]) {
            if (const_args[2]) {
                tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1] - args[2]);
            } else {
                /* Recall that SUBI is a reversed subtract.  */
                tcg_out_arithi(s, args[0], args[2], args[1], INSN_SUBI);
            }
        } else if (const_args[2]) {
            tcg_out_addi2(s, args[0], args[1], -args[2]);
        } else {
            tcg_out_arith(s, args[0], args[1], args[2], INSN_SUB);
        }
        break;

    case INDEX_op_and_i32:
        if (const_args[2]) {
            tcg_out_andi(s, args[0], args[1], args[2]);
        } else {
            tcg_out_arith(s, args[0], args[1], args[2], INSN_AND);
        }
        break;

    case INDEX_op_or_i32:
        if (const_args[2]) {
            tcg_out_ori(s, args[0], args[1], args[2]);
        } else {
            tcg_out_arith(s, args[0], args[1], args[2], INSN_OR);
        }
        break;

    case INDEX_op_xor_i32:
        tcg_out_arith(s, args[0], args[1], args[2], INSN_XOR);
        break;

    case INDEX_op_andc_i32:
        if (const_args[2]) {
            tcg_out_andi(s, args[0], args[1], ~args[2]);
        } else {
            tcg_out_arith(s, args[0], args[1], args[2], INSN_ANDCM);
        }
        break;

    case INDEX_op_shl_i32:
        if (const_args[2]) {
            tcg_out_shli(s, args[0], args[1], args[2]);
        } else {
            tcg_out_shl(s, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_shr_i32:
        if (const_args[2]) {
            tcg_out_shri(s, args[0], args[1], args[2]);
        } else {
            tcg_out_shr(s, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_sar_i32:
        if (const_args[2]) {
            tcg_out_sari(s, args[0], args[1], args[2]);
        } else {
            tcg_out_sar(s, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_rotl_i32:
        if (const_args[2]) {
            tcg_out_rotli(s, args[0], args[1], args[2]);
        } else {
            tcg_out_rotl(s, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_rotr_i32:
        if (const_args[2]) {
            tcg_out_rotri(s, args[0], args[1], args[2]);
        } else {
            tcg_out_rotr(s, args[0], args[1], args[2]);
        }
        break;

    case INDEX_op_mul_i32:
        tcg_out_xmpyu(s, args[0], TCG_REG_R0, args[1], args[2]);
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_xmpyu(s, args[0], args[1], args[2], args[3]);
        break;

    case INDEX_op_bswap16_i32:
        tcg_out_bswap16(s, args[0], args[1], 0);
        break;
    case INDEX_op_bswap32_i32:
        tcg_out_bswap32(s, args[0], args[1], TCG_REG_R20);
        break;

    case INDEX_op_not_i32:
        tcg_out_arithi(s, args[0], args[1], -1, INSN_SUBI);
        break;
    case INDEX_op_ext8s_i32:
        tcg_out_ext8s(s, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_ext16s(s, args[0], args[1]);
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1], args[3]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args[4], args[0], args[1],
                        args[2], const_args[2],
                        args[3], const_args[3], args[5]);
        break;

    case INDEX_op_setcond_i32:
        tcg_out_setcond(s, args[3], args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2(s, args[5], args[0], args[1], args[2],
                         args[3], const_args[3], args[4], const_args[4]);
        break;

    case INDEX_op_add2_i32:
        tcg_out_add2(s, args[0], args[1], args[2], args[3],
                     args[4], args[5], const_args[4]);
        break;

    case INDEX_op_sub2_i32:
        tcg_out_sub2(s, args[0], args[1], args[2], args[3],
                     args[4], args[5], const_args[2], const_args[4]);
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
        tcg_out_qemu_ld(s, args, 2);
        break;
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

    default:
        fprintf(stderr, "unknown opcode 0x%x\n", opc);
        tcg_abort();
    }
}

static const TCGTargetOpDef hppa_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },

    { INDEX_op_call, { "ri" } },
    { INDEX_op_jmp, { "r" } },
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

    { INDEX_op_add_i32, { "r", "rZ", "ri" } },
    { INDEX_op_sub_i32, { "r", "rI", "ri" } },
    { INDEX_op_and_i32, { "r", "rZ", "rM" } },
    { INDEX_op_or_i32, { "r", "rZ", "rO" } },
    { INDEX_op_xor_i32, { "r", "rZ", "rZ" } },
    /* Note that the second argument will be inverted, which means
       we want a constant whose inversion matches M, and that O = ~M.
       See the implementation of and_mask_p.  */
    { INDEX_op_andc_i32, { "r", "rZ", "rO" } },

    { INDEX_op_mul_i32, { "r", "r", "r" } },
    { INDEX_op_mulu2_i32, { "r", "r", "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "ri" } },
    { INDEX_op_shr_i32, { "r", "r", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },
    { INDEX_op_rotl_i32, { "r", "r", "ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "ri" } },

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },
    { INDEX_op_not_i32, { "r", "r" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },

    { INDEX_op_brcond_i32, { "rZ", "rJ" } },
    { INDEX_op_brcond2_i32,  { "rZ", "rZ", "rJ", "rJ" } },

    { INDEX_op_setcond_i32, { "r", "rZ", "rI" } },
    { INDEX_op_setcond2_i32, { "r", "rZ", "rZ", "rI", "rI" } },

    { INDEX_op_add2_i32, { "r", "r", "rZ", "rZ", "rI", "rZ" } },
    { INDEX_op_sub2_i32, { "r", "r", "rI", "rZ", "rK", "rZ" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L" } },

    { INDEX_op_qemu_st8, { "LZ", "L" } },
    { INDEX_op_qemu_st16, { "LZ", "L" } },
    { INDEX_op_qemu_st32, { "LZ", "L" } },
    { INDEX_op_qemu_st64, { "LZ", "LZ", "L" } },
#else
    { INDEX_op_qemu_ld8u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L", "L" } },

    { INDEX_op_qemu_st8, { "LZ", "L", "L" } },
    { INDEX_op_qemu_st16, { "LZ", "L", "L" } },
    { INDEX_op_qemu_st32, { "LZ", "L", "L" } },
    { INDEX_op_qemu_st64, { "LZ", "LZ", "L", "L" } },
#endif
    { -1 },
};

static int tcg_target_callee_save_regs[] = {
    /* R2, the return address register, is saved specially
       in the caller's frame.  */
    /* R3, the frame pointer, is not currently modified.  */
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,
    TCG_REG_R17, /* R17 is the global env.  */
    TCG_REG_R18
};

static void tcg_target_qemu_prologue(TCGContext *s)
{
    int frame_size, i;

    /* Allocate space for the fixed frame marker.  */
    frame_size = -TCG_TARGET_CALL_STACK_OFFSET;
    frame_size += TCG_TARGET_STATIC_CALL_ARGS_SIZE;

    /* Allocate space for the saved registers.  */
    frame_size += ARRAY_SIZE(tcg_target_callee_save_regs) * 4;

    /* Align the allocated space.  */
    frame_size = ((frame_size + TCG_TARGET_STACK_ALIGN - 1)
                  & -TCG_TARGET_STACK_ALIGN);

    /* The return address is stored in the caller's frame.  */
    tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_RP, TCG_REG_CALL_STACK, -20);

    /* Allocate stack frame, saving the first register at the same time.  */
    tcg_out_ldst(s, tcg_target_callee_save_regs[0],
                 TCG_REG_CALL_STACK, frame_size, INSN_STWM);

    /* Save all callee saved registers.  */
    for (i = 1; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_st(s, TCG_TYPE_PTR, tcg_target_callee_save_regs[i],
                   TCG_REG_CALL_STACK, -frame_size + i * 4);
    }

#ifdef CONFIG_USE_GUEST_BASE
    if (GUEST_BASE != 0) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

    /* Jump to TB, and adjust R18 to be the return address.  */
    tcg_out32(s, INSN_BLE_SR4 | INSN_R2(tcg_target_call_iarg_regs[1]));
    tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R18, TCG_REG_R31);

    /* Restore callee saved registers.  */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_RP, TCG_REG_CALL_STACK,
               -frame_size - 20);
    for (i = 1; i < ARRAY_SIZE(tcg_target_callee_save_regs); i++) {
        tcg_out_ld(s, TCG_TYPE_PTR, tcg_target_callee_save_regs[i],
                   TCG_REG_CALL_STACK, -frame_size + i * 4);
    }

    /* Deallocate stack frame and return.  */
    tcg_out32(s, INSN_BV | INSN_R2(TCG_REG_RP));
    tcg_out_ldst(s, tcg_target_callee_save_regs[0],
                 TCG_REG_CALL_STACK, -frame_size, INSN_LDWM);
}

static void tcg_target_init(TCGContext *s)
{
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);

    tcg_regset_clear(tcg_target_call_clobber_regs);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R20);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R21);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R22);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R23);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R24);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R25);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R26);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_RET0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_RET1);

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);  /* hardwired to zero */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R1);  /* addil target */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_RP);  /* link register */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R3);  /* frame pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R18); /* return pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R19); /* clobbered w/o pic */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R20); /* reserved */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_DP);  /* data pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);  /* stack pointer */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R31); /* ble link reg */

    tcg_add_target_add_op_defs(hppa_op_defs);
    tcg_set_frame(s, TCG_AREG0, offsetof(CPUState, temp_buf),
                  CPU_TEMP_BUF_NLONGS * sizeof(long));
}
