/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Andrzej Zaborowski
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
    "%r0",
    "%r1",
    "%r2",
    "%r3",
    "%r4",
    "%r5",
    "%r6",
    "%r7",
    "%r8",
    "%r9",
    "%r10",
    "%r11",
    "%r12",
    "%r13",
    "%r14",
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
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
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_R0, TCG_REG_R1, TCG_REG_R2, TCG_REG_R3
};
static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_R0, TCG_REG_R1
};

static void patch_reloc(uint8_t *code_ptr, int type,
                tcg_target_long value, tcg_target_long addend)
{
    switch (type) {
    case R_ARM_ABS32:
        *(uint32_t *) code_ptr = value;
        break;

    case R_ARM_CALL:
    case R_ARM_JUMP24:
    default:
        tcg_abort();

    case R_ARM_PC24:
        *(uint32_t *) code_ptr = ((*(uint32_t *) code_ptr) & 0xff000000) |
                (((value - ((tcg_target_long) code_ptr + 8)) >> 2) & 0xffffff);
        break;
    }
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
#ifndef CONFIG_SOFTMMU
    case 'd':
    case 'D':
    case 'x':
    case 'X':
#endif
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        break;

#ifdef CONFIG_SOFTMMU
    /* qemu_ld/st inputs (unless 'X', 'd' or 'D') */
    case 'x':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R1);
        break;

    /* qemu_ld64 data_reg */
    case 'd':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        /* r1 is still needed to load data_reg2, so don't use it.  */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R1);
        break;

    /* qemu_ld/st64 data_reg2 */
    case 'D':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        /* r0, r1 and optionally r2 will be overwritten by the address
         * and the low word of data, so don't use these.  */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R1);
# if TARGET_LONG_BITS == 64
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R2);
# endif
        break;

# if TARGET_LONG_BITS == 64
    /* qemu_ld/st addr_reg2 */
    case 'X':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        /* r0 will be overwritten by the low word of base, so don't use it.  */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R1);
        break;
# endif
#endif

    case '1':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        break;

    case '2':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R1);
        break;

    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;

    return 0;
}

/* Test if a constant matches the constraint.
 * TODO: define constraints for:
 *
 * ldr/str offset:   between -0xfff and 0xfff
 * ldrh/strh offset: between -0xff and 0xff
 * mov operand2:     values represented with x << (2 * y), x < 0x100
 * add, sub, eor...: ditto
 */
static inline int tcg_target_const_match(tcg_target_long val,
                const TCGArgConstraint *arg_ct)
{
    int ct;
    ct = arg_ct->ct;
    if (ct & TCG_CT_CONST)
        return 1;
    else
        return 0;
}

enum arm_data_opc_e {
    ARITH_AND = 0x0,
    ARITH_EOR = 0x1,
    ARITH_SUB = 0x2,
    ARITH_RSB = 0x3,
    ARITH_ADD = 0x4,
    ARITH_ADC = 0x5,
    ARITH_SBC = 0x6,
    ARITH_RSC = 0x7,
    ARITH_TST = 0x8,
    ARITH_CMP = 0xa,
    ARITH_CMN = 0xb,
    ARITH_ORR = 0xc,
    ARITH_MOV = 0xd,
    ARITH_BIC = 0xe,
    ARITH_MVN = 0xf,
};

#define TO_CPSR(opc) \
  ((opc == ARITH_CMP || opc == ARITH_CMN || opc == ARITH_TST) << 20)

#define SHIFT_IMM_LSL(im)	(((im) << 7) | 0x00)
#define SHIFT_IMM_LSR(im)	(((im) << 7) | 0x20)
#define SHIFT_IMM_ASR(im)	(((im) << 7) | 0x40)
#define SHIFT_IMM_ROR(im)	(((im) << 7) | 0x60)
#define SHIFT_REG_LSL(rs)	(((rs) << 8) | 0x10)
#define SHIFT_REG_LSR(rs)	(((rs) << 8) | 0x30)
#define SHIFT_REG_ASR(rs)	(((rs) << 8) | 0x50)
#define SHIFT_REG_ROR(rs)	(((rs) << 8) | 0x70)

enum arm_cond_code_e {
    COND_EQ = 0x0,
    COND_NE = 0x1,
    COND_CS = 0x2,	/* Unsigned greater or equal */
    COND_CC = 0x3,	/* Unsigned less than */
    COND_MI = 0x4,	/* Negative */
    COND_PL = 0x5,	/* Zero or greater */
    COND_VS = 0x6,	/* Overflow */
    COND_VC = 0x7,	/* No overflow */
    COND_HI = 0x8,	/* Unsigned greater than */
    COND_LS = 0x9,	/* Unsigned less or equal */
    COND_GE = 0xa,
    COND_LT = 0xb,
    COND_GT = 0xc,
    COND_LE = 0xd,
    COND_AL = 0xe,
};

static const uint8_t tcg_cond_to_arm_cond[10] = {
    [TCG_COND_EQ] = COND_EQ,
    [TCG_COND_NE] = COND_NE,
    [TCG_COND_LT] = COND_LT,
    [TCG_COND_GE] = COND_GE,
    [TCG_COND_LE] = COND_LE,
    [TCG_COND_GT] = COND_GT,
    /* unsigned */
    [TCG_COND_LTU] = COND_CC,
    [TCG_COND_GEU] = COND_CS,
    [TCG_COND_LEU] = COND_LS,
    [TCG_COND_GTU] = COND_HI,
};

static inline void tcg_out_bx(TCGContext *s, int cond, int rn)
{
    tcg_out32(s, (cond << 28) | 0x012fff10 | rn);
}

static inline void tcg_out_b(TCGContext *s, int cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | 0x0a000000 |
                    (((offset - 8) >> 2) & 0x00ffffff));
}

static inline void tcg_out_b_noaddr(TCGContext *s, int cond)
{
#ifdef WORDS_BIGENDIAN
    tcg_out8(s, (cond << 4) | 0x0a);
    s->code_ptr += 3;
#else
    s->code_ptr += 3;
    tcg_out8(s, (cond << 4) | 0x0a);
#endif
}

static inline void tcg_out_bl(TCGContext *s, int cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | 0x0b000000 |
                    (((offset - 8) >> 2) & 0x00ffffff));
}

static inline void tcg_out_dat_reg(TCGContext *s,
                int cond, int opc, int rd, int rn, int rm, int shift)
{
    tcg_out32(s, (cond << 28) | (0 << 25) | (opc << 21) | TO_CPSR(opc) |
                    (rn << 16) | (rd << 12) | shift | rm);
}

static inline void tcg_out_dat_reg2(TCGContext *s,
                int cond, int opc0, int opc1, int rd0, int rd1,
                int rn0, int rn1, int rm0, int rm1, int shift)
{
    tcg_out32(s, (cond << 28) | (0 << 25) | (opc0 << 21) | (1 << 20) |
                    (rn0 << 16) | (rd0 << 12) | shift | rm0);
    tcg_out32(s, (cond << 28) | (0 << 25) | (opc1 << 21) |
                    (rn1 << 16) | (rd1 << 12) | shift | rm1);
}

static inline void tcg_out_dat_imm(TCGContext *s,
                int cond, int opc, int rd, int rn, int im)
{
    tcg_out32(s, (cond << 28) | (1 << 25) | (opc << 21) | TO_CPSR(opc) |
                    (rn << 16) | (rd << 12) | im);
}

static inline void tcg_out_movi32(TCGContext *s,
                int cond, int rd, int32_t arg)
{
    int offset = (uint32_t) arg - ((uint32_t) s->code_ptr + 8);

    /* TODO: This is very suboptimal, we can easily have a constant
     * pool somewhere after all the instructions.  */

    if (arg < 0 && arg > -0x100)
        return tcg_out_dat_imm(s, cond, ARITH_MVN, rd, 0, (~arg) & 0xff);

    if (offset < 0x100 && offset > -0x100)
        return offset >= 0 ?
                tcg_out_dat_imm(s, cond, ARITH_ADD, rd, 15, offset) :
                tcg_out_dat_imm(s, cond, ARITH_SUB, rd, 15, -offset);

    tcg_out_dat_imm(s, cond, ARITH_MOV, rd, 0, arg & 0xff);
    if (arg & 0x0000ff00)
        tcg_out_dat_imm(s, cond, ARITH_ORR, rd, rd,
                        ((arg >>  8) & 0xff) | 0xc00);
    if (arg & 0x00ff0000)
        tcg_out_dat_imm(s, cond, ARITH_ORR, rd, rd,
                        ((arg >> 16) & 0xff) | 0x800);
    if (arg & 0xff000000)
        tcg_out_dat_imm(s, cond, ARITH_ORR, rd, rd,
                        ((arg >> 24) & 0xff) | 0x400);
}

static inline void tcg_out_mul32(TCGContext *s,
                int cond, int rd, int rs, int rm)
{
    if (rd != rm)
        tcg_out32(s, (cond << 28) | (rd << 16) | (0 << 12) |
                        (rs << 8) | 0x90 | rm);
    else if (rd != rs)
        tcg_out32(s, (cond << 28) | (rd << 16) | (0 << 12) |
                        (rm << 8) | 0x90 | rs);
    else {
        tcg_out32(s, (cond << 28) | ( 8 << 16) | (0 << 12) |
                        (rs << 8) | 0x90 | rm);
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, 8, SHIFT_IMM_LSL(0));
    }
}

static inline void tcg_out_umull32(TCGContext *s,
                int cond, int rd0, int rd1, int rs, int rm)
{
    if (rd0 != rm && rd1 != rm)
        tcg_out32(s, (cond << 28) | 0x800090 |
                        (rd1 << 16) | (rd0 << 12) | (rs << 8) | rm);
    else if (rd0 != rs && rd1 != rs)
        tcg_out32(s, (cond << 28) | 0x800090 |
                        (rd1 << 16) | (rd0 << 12) | (rm << 8) | rs);
    else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        TCG_REG_R8, 0, rm, SHIFT_IMM_LSL(0));
        tcg_out32(s, (cond << 28) | 0x800098 |
                        (rd1 << 16) | (rd0 << 12) | (rs << 8));
    }
}

static inline void tcg_out_smull32(TCGContext *s,
                int cond, int rd0, int rd1, int rs, int rm)
{
    if (rd0 != rm && rd1 != rm)
        tcg_out32(s, (cond << 28) | 0xc00090 |
                        (rd1 << 16) | (rd0 << 12) | (rs << 8) | rm);
    else if (rd0 != rs && rd1 != rs)
        tcg_out32(s, (cond << 28) | 0xc00090 |
                        (rd1 << 16) | (rd0 << 12) | (rm << 8) | rs);
    else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        TCG_REG_R8, 0, rm, SHIFT_IMM_LSL(0));
        tcg_out32(s, (cond << 28) | 0xc00098 |
                        (rd1 << 16) | (rd0 << 12) | (rs << 8));
    }
}

static inline void tcg_out_ld32_12(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x05900000 |
                        (rn << 16) | (rd << 12) | (im & 0xfff));
    else
        tcg_out32(s, (cond << 28) | 0x05100000 |
                        (rn << 16) | (rd << 12) | ((-im) & 0xfff));
}

static inline void tcg_out_st32_12(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x05800000 |
                        (rn << 16) | (rd << 12) | (im & 0xfff));
    else
        tcg_out32(s, (cond << 28) | 0x05000000 |
                        (rn << 16) | (rd << 12) | ((-im) & 0xfff));
}

static inline void tcg_out_ld32_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x07900000 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_st32_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x07800000 |
                    (rn << 16) | (rd << 12) | rm);
}

/* Register pre-increment with base writeback.  */
static inline void tcg_out_ld32_rwb(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x07b00000 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_st32_rwb(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x07a00000 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_ld16u_8(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x01d000b0 |
                        (rn << 16) | (rd << 12) |
                        ((im & 0xf0) << 4) | (im & 0xf));
    else
        tcg_out32(s, (cond << 28) | 0x015000b0 |
                        (rn << 16) | (rd << 12) |
                        (((-im) & 0xf0) << 4) | ((-im) & 0xf));
}

static inline void tcg_out_st16u_8(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x01c000b0 |
                        (rn << 16) | (rd << 12) |
                        ((im & 0xf0) << 4) | (im & 0xf));
    else
        tcg_out32(s, (cond << 28) | 0x014000b0 |
                        (rn << 16) | (rd << 12) |
                        (((-im) & 0xf0) << 4) | ((-im) & 0xf));
}

static inline void tcg_out_ld16u_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x019000b0 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_st16u_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x018000b0 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_ld16s_8(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x01d000f0 |
                        (rn << 16) | (rd << 12) |
                        ((im & 0xf0) << 4) | (im & 0xf));
    else
        tcg_out32(s, (cond << 28) | 0x015000f0 |
                        (rn << 16) | (rd << 12) |
                        (((-im) & 0xf0) << 4) | ((-im) & 0xf));
}

static inline void tcg_out_st16s_8(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x01c000f0 |
                        (rn << 16) | (rd << 12) |
                        ((im & 0xf0) << 4) | (im & 0xf));
    else
        tcg_out32(s, (cond << 28) | 0x014000f0 |
                        (rn << 16) | (rd << 12) |
                        (((-im) & 0xf0) << 4) | ((-im) & 0xf));
}

static inline void tcg_out_ld16s_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x019000f0 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_st16s_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x018000f0 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_ld8_12(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x05d00000 |
                        (rn << 16) | (rd << 12) | (im & 0xfff));
    else
        tcg_out32(s, (cond << 28) | 0x05500000 |
                        (rn << 16) | (rd << 12) | ((-im) & 0xfff));
}

static inline void tcg_out_st8_12(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x05c00000 |
                        (rn << 16) | (rd << 12) | (im & 0xfff));
    else
        tcg_out32(s, (cond << 28) | 0x05400000 |
                        (rn << 16) | (rd << 12) | ((-im) & 0xfff));
}

static inline void tcg_out_ld8_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x07d00000 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_st8_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x07c00000 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_ld8s_8(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x01d000d0 |
                        (rn << 16) | (rd << 12) |
                        ((im & 0xf0) << 4) | (im & 0xf));
    else
        tcg_out32(s, (cond << 28) | 0x015000d0 |
                        (rn << 16) | (rd << 12) |
                        (((-im) & 0xf0) << 4) | ((-im) & 0xf));
}

static inline void tcg_out_st8s_8(TCGContext *s, int cond,
                int rd, int rn, tcg_target_long im)
{
    if (im >= 0)
        tcg_out32(s, (cond << 28) | 0x01c000d0 |
                        (rn << 16) | (rd << 12) |
                        ((im & 0xf0) << 4) | (im & 0xf));
    else
        tcg_out32(s, (cond << 28) | 0x014000d0 |
                        (rn << 16) | (rd << 12) |
                        (((-im) & 0xf0) << 4) | ((-im) & 0xf));
}

static inline void tcg_out_ld8s_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x019000d0 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_st8s_r(TCGContext *s, int cond,
                int rd, int rn, int rm)
{
    tcg_out32(s, (cond << 28) | 0x018000d0 |
                    (rn << 16) | (rd << 12) | rm);
}

static inline void tcg_out_ld32u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_ld32_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_ld32_12(s, cond, rd, rn, offset);
}

static inline void tcg_out_st32(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_st32_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_st32_12(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld16u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_ld16u_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_ld16u_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld16s(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_ld16s_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_ld16s_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_st16u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_st16u_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_st16u_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld8u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_ld8_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_ld8_12(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld8s(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_ld8s_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_ld8s_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_st8u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_R8, offset);
        tcg_out_st8_r(s, cond, rd, rn, TCG_REG_R8);
    } else
        tcg_out_st8_12(s, cond, rd, rn, offset);
}

static inline void tcg_out_goto(TCGContext *s, int cond, uint32_t addr)
{
    int32_t val;

    val = addr - (tcg_target_long) s->code_ptr;
    if (val - 8 < 0x01fffffd && val - 8 > -0x01fffffd)
        tcg_out_b(s, cond, val);
    else {
#if 1
        tcg_abort();
#else
        if (cond == COND_AL) {
            tcg_out_ld32_12(s, COND_AL, 15, 15, -4);
            tcg_out32(s, addr); /* XXX: This is l->u.value, can we use it? */
        } else {
            tcg_out_movi32(s, cond, TCG_REG_R8, val - 8);
            tcg_out_dat_reg(s, cond, ARITH_ADD,
                            15, 15, TCG_REG_R8, SHIFT_IMM_LSL(0));
        }
#endif
    }
}

static inline void tcg_out_call(TCGContext *s, int cond, uint32_t addr)
{
    int32_t val;

#ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, TCG_REG_R8, 0, 14, SHIFT_IMM_LSL(0));
#endif

    val = addr - (tcg_target_long) s->code_ptr;
    if (val < 0x01fffffd && val > -0x01fffffd)
        tcg_out_bl(s, cond, val);
    else {
#if 1
        tcg_abort();
#else
        if (cond == COND_AL) {
            tcg_out_dat_imm(s, cond, ARITH_ADD, 14, 15, 4);
            tcg_out_ld32_12(s, COND_AL, 15, 15, -4);
            tcg_out32(s, addr); /* XXX: This is l->u.value, can we use it? */
        } else {
            tcg_out_movi32(s, cond, TCG_REG_R9, addr);
            tcg_out_dat_imm(s, cond, ARITH_MOV, 14, 0, 15);
            tcg_out_bx(s, cond, TCG_REG_R9);
        }
#endif
    }

#ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, 14, 0, TCG_REG_R8, SHIFT_IMM_LSL(0));
#endif
}

static inline void tcg_out_callr(TCGContext *s, int cond, int arg)
{
#ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, TCG_REG_R8, 0, 14, SHIFT_IMM_LSL(0));
#endif
    /* TODO: on ARMv5 and ARMv6 replace with tcg_out_blx(s, cond, arg);  */
    tcg_out_dat_reg(s, cond, ARITH_MOV, 14, 0, 15, SHIFT_IMM_LSL(0));
    tcg_out_bx(s, cond, arg);
#ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, 14, 0, TCG_REG_R8, SHIFT_IMM_LSL(0));
#endif
}

static inline void tcg_out_goto_label(TCGContext *s, int cond, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (l->has_value)
        tcg_out_goto(s, cond, l->u.value);
    else if (cond == COND_AL) {
        tcg_out_ld32_12(s, COND_AL, 15, 15, -4);
        tcg_out_reloc(s, s->code_ptr, R_ARM_ABS32, label_index, 31337);
        s->code_ptr += 4;
    } else {
        /* Probably this should be preferred even for COND_AL... */
        tcg_out_reloc(s, s->code_ptr, R_ARM_PC24, label_index, 31337);
        tcg_out_b_noaddr(s, cond);
    }
}

static void tcg_out_div_helper(TCGContext *s, int cond, const TCGArg *args,
                void *helper_div, void *helper_rem, int shift)
{
    int div_reg = args[0];
    int rem_reg = args[1];

    /* stmdb sp!, { r0 - r3, ip, lr } */
    /* (Note that we need an even number of registers as per EABI) */
    tcg_out32(s, (cond << 28) | 0x092d500f);

    tcg_out_dat_reg(s, cond, ARITH_MOV, 0, 0, args[2], SHIFT_IMM_LSL(0));
    tcg_out_dat_reg(s, cond, ARITH_MOV, 1, 0, args[3], SHIFT_IMM_LSL(0));
    tcg_out_dat_reg(s, cond, ARITH_MOV, 2, 0, args[4], SHIFT_IMM_LSL(0));
    tcg_out_dat_reg(s, cond, ARITH_MOV, 3, 0, 2, shift);

    tcg_out_call(s, cond, (uint32_t) helper_div);
    tcg_out_dat_reg(s, cond, ARITH_MOV, 8, 0, 0, SHIFT_IMM_LSL(0));

    /* ldmia sp, { r0 - r3, fp, lr } */
    tcg_out32(s, (cond << 28) | 0x089d500f);

    tcg_out_dat_reg(s, cond, ARITH_MOV, 0, 0, args[2], SHIFT_IMM_LSL(0));
    tcg_out_dat_reg(s, cond, ARITH_MOV, 1, 0, args[3], SHIFT_IMM_LSL(0));
    tcg_out_dat_reg(s, cond, ARITH_MOV, 2, 0, args[4], SHIFT_IMM_LSL(0));
    tcg_out_dat_reg(s, cond, ARITH_MOV, 3, 0, 2, shift);

    tcg_out_call(s, cond, (uint32_t) helper_rem);

    tcg_out_dat_reg(s, cond, ARITH_MOV, rem_reg, 0, 0, SHIFT_IMM_LSL(0));
    tcg_out_dat_reg(s, cond, ARITH_MOV, div_reg, 0, 8, SHIFT_IMM_LSL(0));

    /* ldr r0, [sp], #4 */
    if (rem_reg != 0 && div_reg != 0)
        tcg_out32(s, (cond << 28) | 0x04bd0004);
    /* ldr r1, [sp], #4 */
    if (rem_reg != 1 && div_reg != 1)
        tcg_out32(s, (cond << 28) | 0x04bd1004);
    /* ldr r2, [sp], #4 */
    if (rem_reg != 2 && div_reg != 2)
        tcg_out32(s, (cond << 28) | 0x04bd2004);
    /* ldr r3, [sp], #4 */
    if (rem_reg != 3 && div_reg != 3)
        tcg_out32(s, (cond << 28) | 0x04bd3004);
    /* ldr ip, [sp], #4 */
    if (rem_reg != 12 && div_reg != 12)
        tcg_out32(s, (cond << 28) | 0x04bdc004);
    /* ldr lr, [sp], #4 */
    if (rem_reg != 14 && div_reg != 14)
        tcg_out32(s, (cond << 28) | 0x04bde004);
}

#ifdef CONFIG_SOFTMMU

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
#endif

#define TLB_SHIFT	(CPU_TLB_ENTRY_BITS + CPU_TLB_BITS)

static inline void tcg_out_qemu_ld(TCGContext *s, int cond,
                const TCGArg *args, int opc)
{
    int addr_reg, data_reg, data_reg2;
#ifdef CONFIG_SOFTMMU
    int mem_index, s_bits;
# if TARGET_LONG_BITS == 64
    int addr_reg2;
# endif
    uint32_t *label_ptr;
#endif

    data_reg = *args++;
    if (opc == 3)
        data_reg2 = *args++;
    else
        data_reg2 = 0; /* surpress warning */
    addr_reg = *args++;
#if TARGET_LONG_BITS == 64
    addr_reg2 = *args++;
#endif
#ifdef CONFIG_SOFTMMU
    mem_index = *args;
    s_bits = opc & 3;

    /* Should generate something like the following:
     *  shr r8, addr_reg, #TARGET_PAGE_BITS
     *  and r0, r8, #(CPU_TLB_SIZE - 1)   @ Assumption: CPU_TLB_BITS <= 8
     *  add r0, env, r0 lsl #CPU_TLB_ENTRY_BITS
     */
#  if CPU_TLB_BITS > 8
#   error
#  endif
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV,
                    8, 0, addr_reg, SHIFT_IMM_LSR(TARGET_PAGE_BITS));
    tcg_out_dat_imm(s, COND_AL, ARITH_AND,
                    0, 8, CPU_TLB_SIZE - 1);
    tcg_out_dat_reg(s, COND_AL, ARITH_ADD,
                    0, TCG_AREG0, 0, SHIFT_IMM_LSL(CPU_TLB_ENTRY_BITS));
    /* In the
     *  ldr r1 [r0, #(offsetof(CPUState, tlb_table[mem_index][0].addr_read))]
     * below, the offset is likely to exceed 12 bits if mem_index != 0 and
     * not exceed otherwise, so use an
     *  add r0, r0, #(mem_index * sizeof *CPUState.tlb_table)
     * before.
     */
    if (mem_index)
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, 0, 0,
                        (mem_index << (TLB_SHIFT & 1)) |
                        ((16 - (TLB_SHIFT >> 1)) << 8));
    tcg_out_ld32_12(s, COND_AL, 1, 0,
                    offsetof(CPUState, tlb_table[0][0].addr_read));
    tcg_out_dat_reg(s, COND_AL, ARITH_CMP,
                    0, 1, 8, SHIFT_IMM_LSL(TARGET_PAGE_BITS));
    /* Check alignment.  */
    if (s_bits)
        tcg_out_dat_imm(s, COND_EQ, ARITH_TST,
                        0, addr_reg, (1 << s_bits) - 1);
#  if TARGET_LONG_BITS == 64
    /* XXX: possibly we could use a block data load or writeback in
     * the first access.  */
    tcg_out_ld32_12(s, COND_EQ, 1, 0,
                    offsetof(CPUState, tlb_table[0][0].addr_read) + 4);
    tcg_out_dat_reg(s, COND_EQ, ARITH_CMP,
                    0, 1, addr_reg2, SHIFT_IMM_LSL(0));
#  endif
    tcg_out_ld32_12(s, COND_EQ, 1, 0,
                    offsetof(CPUState, tlb_table[0][0].addend));

    switch (opc) {
    case 0:
        tcg_out_ld8_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 0 | 4:
        tcg_out_ld8s_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 1:
        tcg_out_ld16u_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 1 | 4:
        tcg_out_ld16s_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 2:
    default:
        tcg_out_ld32_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 3:
        tcg_out_ld32_rwb(s, COND_EQ, data_reg, 1, addr_reg);
        tcg_out_ld32_12(s, COND_EQ, data_reg2, 1, 4);
        break;
    }

    label_ptr = (void *) s->code_ptr;
    tcg_out_b(s, COND_EQ, 8);

# ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, 8, 0, 14, SHIFT_IMM_LSL(0));
# endif

    /* TODO: move this code to where the constants pool will be */
    if (addr_reg)
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        0, 0, addr_reg, SHIFT_IMM_LSL(0));
# if TARGET_LONG_BITS == 32
    tcg_out_dat_imm(s, cond, ARITH_MOV, 1, 0, mem_index);
# else
    if (addr_reg2 != 1)
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        1, 0, addr_reg2, SHIFT_IMM_LSL(0));
    tcg_out_dat_imm(s, cond, ARITH_MOV, 2, 0, mem_index);
# endif
    tcg_out_bl(s, cond, (tcg_target_long) qemu_ld_helpers[s_bits] -
                    (tcg_target_long) s->code_ptr);

    switch (opc) {
    case 0 | 4:
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        0, 0, 0, SHIFT_IMM_LSL(24));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        data_reg, 0, 0, SHIFT_IMM_ASR(24));
        break;
    case 1 | 4:
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        0, 0, 0, SHIFT_IMM_LSL(16));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        data_reg, 0, 0, SHIFT_IMM_ASR(16));
        break;
    case 0:
    case 1:
    case 2:
    default:
        if (data_reg)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            data_reg, 0, 0, SHIFT_IMM_LSL(0));
        break;
    case 3:
        if (data_reg != 0)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            data_reg, 0, 0, SHIFT_IMM_LSL(0));
        if (data_reg2 != 1)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            data_reg2, 0, 1, SHIFT_IMM_LSL(0));
        break;
    }

# ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, 14, 0, 8, SHIFT_IMM_LSL(0));
# endif

    *label_ptr += ((void *) s->code_ptr - (void *) label_ptr - 8) >> 2;
#else
    switch (opc) {
    case 0:
        tcg_out_ld8_12(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 0 | 4:
        tcg_out_ld8s_8(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 1:
        tcg_out_ld16u_8(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 1 | 4:
        tcg_out_ld16s_8(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 2:
    default:
        tcg_out_ld32_12(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 3:
        /* TODO: use block load -
         * check that data_reg2 > data_reg or the other way */
        tcg_out_ld32_12(s, COND_AL, data_reg, addr_reg, 0);
        tcg_out_ld32_12(s, COND_AL, data_reg2, addr_reg, 4);
        break;
    }
#endif
}

static inline void tcg_out_qemu_st(TCGContext *s, int cond,
                const TCGArg *args, int opc)
{
    int addr_reg, data_reg, data_reg2;
#ifdef CONFIG_SOFTMMU
    int mem_index, s_bits;
# if TARGET_LONG_BITS == 64
    int addr_reg2;
# endif
    uint32_t *label_ptr;
#endif

    data_reg = *args++;
    if (opc == 3)
        data_reg2 = *args++;
    else
        data_reg2 = 0; /* surpress warning */
    addr_reg = *args++;
#if TARGET_LONG_BITS == 64
    addr_reg2 = *args++;
#endif
#ifdef CONFIG_SOFTMMU
    mem_index = *args;
    s_bits = opc & 3;

    /* Should generate something like the following:
     *  shr r8, addr_reg, #TARGET_PAGE_BITS
     *  and r0, r8, #(CPU_TLB_SIZE - 1)   @ Assumption: CPU_TLB_BITS <= 8
     *  add r0, env, r0 lsl #CPU_TLB_ENTRY_BITS
     */
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV,
                    8, 0, addr_reg, SHIFT_IMM_LSR(TARGET_PAGE_BITS));
    tcg_out_dat_imm(s, COND_AL, ARITH_AND,
                    0, 8, CPU_TLB_SIZE - 1);
    tcg_out_dat_reg(s, COND_AL, ARITH_ADD,
                    0, TCG_AREG0, 0, SHIFT_IMM_LSL(CPU_TLB_ENTRY_BITS));
    /* In the
     *  ldr r1 [r0, #(offsetof(CPUState, tlb_table[mem_index][0].addr_write))]
     * below, the offset is likely to exceed 12 bits if mem_index != 0 and
     * not exceed otherwise, so use an
     *  add r0, r0, #(mem_index * sizeof *CPUState.tlb_table)
     * before.
     */
    if (mem_index)
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, 0, 0,
                        (mem_index << (TLB_SHIFT & 1)) |
                        ((16 - (TLB_SHIFT >> 1)) << 8));
    tcg_out_ld32_12(s, COND_AL, 1, 0,
                    offsetof(CPUState, tlb_table[0][0].addr_write));
    tcg_out_dat_reg(s, COND_AL, ARITH_CMP,
                    0, 1, 8, SHIFT_IMM_LSL(TARGET_PAGE_BITS));
    /* Check alignment.  */
    if (s_bits)
        tcg_out_dat_imm(s, COND_EQ, ARITH_TST,
                        0, addr_reg, (1 << s_bits) - 1);
#  if TARGET_LONG_BITS == 64
    /* XXX: possibly we could use a block data load or writeback in
     * the first access.  */
    tcg_out_ld32_12(s, COND_EQ, 1, 0,
                    offsetof(CPUState, tlb_table[0][0].addr_write)
                    + 4);
    tcg_out_dat_reg(s, COND_EQ, ARITH_CMP,
                    0, 1, addr_reg2, SHIFT_IMM_LSL(0));
#  endif
    tcg_out_ld32_12(s, COND_EQ, 1, 0,
                    offsetof(CPUState, tlb_table[0][0].addend));

    switch (opc) {
    case 0:
        tcg_out_st8_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 0 | 4:
        tcg_out_st8s_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 1:
        tcg_out_st16u_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 1 | 4:
        tcg_out_st16s_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 2:
    default:
        tcg_out_st32_r(s, COND_EQ, data_reg, addr_reg, 1);
        break;
    case 3:
        tcg_out_st32_rwb(s, COND_EQ, data_reg, 1, addr_reg);
        tcg_out_st32_12(s, COND_EQ, data_reg2, 1, 4);
        break;
    }

    label_ptr = (void *) s->code_ptr;
    tcg_out_b(s, COND_EQ, 8);

    /* TODO: move this code to where the constants pool will be */
    if (addr_reg)
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        0, 0, addr_reg, SHIFT_IMM_LSL(0));
# if TARGET_LONG_BITS == 32
    switch (opc) {
    case 0:
        tcg_out_dat_imm(s, cond, ARITH_AND, 1, data_reg, 0xff);
        tcg_out_dat_imm(s, cond, ARITH_MOV, 2, 0, mem_index);
        break;
    case 1:
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        1, 0, data_reg, SHIFT_IMM_LSL(16));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        1, 0, 1, SHIFT_IMM_LSR(16));
        tcg_out_dat_imm(s, cond, ARITH_MOV, 2, 0, mem_index);
        break;
    case 2:
        if (data_reg != 1)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            1, 0, data_reg, SHIFT_IMM_LSL(0));
        tcg_out_dat_imm(s, cond, ARITH_MOV, 2, 0, mem_index);
        break;
    case 3:
        if (data_reg != 1)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            1, 0, data_reg, SHIFT_IMM_LSL(0));
        if (data_reg2 != 2)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            2, 0, data_reg2, SHIFT_IMM_LSL(0));
        tcg_out_dat_imm(s, cond, ARITH_MOV, 3, 0, mem_index);
        break;
    }
# else
    if (addr_reg2 != 1)
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        1, 0, addr_reg2, SHIFT_IMM_LSL(0));
    switch (opc) {
    case 0:
        tcg_out_dat_imm(s, cond, ARITH_AND, 2, data_reg, 0xff);
        tcg_out_dat_imm(s, cond, ARITH_MOV, 3, 0, mem_index);
        break;
    case 1:
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        2, 0, data_reg, SHIFT_IMM_LSL(16));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        2, 0, 2, SHIFT_IMM_LSR(16));
        tcg_out_dat_imm(s, cond, ARITH_MOV, 3, 0, mem_index);
        break;
    case 2:
        if (data_reg != 2)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            2, 0, data_reg, SHIFT_IMM_LSL(0));
        tcg_out_dat_imm(s, cond, ARITH_MOV, 3, 0, mem_index);
        break;
    case 3:
        tcg_out_dat_imm(s, cond, ARITH_MOV, 8, 0, mem_index);
        tcg_out32(s, (cond << 28) | 0x052d8010); /* str r8, [sp, #-0x10]! */
        if (data_reg != 2)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            2, 0, data_reg, SHIFT_IMM_LSL(0));
        if (data_reg2 != 3)
            tcg_out_dat_reg(s, cond, ARITH_MOV,
                            3, 0, data_reg2, SHIFT_IMM_LSL(0));
        break;
    }
# endif

# ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, 8, 0, 14, SHIFT_IMM_LSL(0));
# endif

    tcg_out_bl(s, cond, (tcg_target_long) qemu_st_helpers[s_bits] -
                    (tcg_target_long) s->code_ptr);
# if TARGET_LONG_BITS == 64
    if (opc == 3)
        tcg_out_dat_imm(s, cond, ARITH_ADD, 13, 13, 0x10);
# endif

# ifdef SAVE_LR
    tcg_out_dat_reg(s, cond, ARITH_MOV, 14, 0, 8, SHIFT_IMM_LSL(0));
# endif

    *label_ptr += ((void *) s->code_ptr - (void *) label_ptr - 8) >> 2;
#else
    switch (opc) {
    case 0:
        tcg_out_st8_12(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 0 | 4:
        tcg_out_st8s_8(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 1:
        tcg_out_st16u_8(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 1 | 4:
        tcg_out_st16s_8(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 2:
    default:
        tcg_out_st32_12(s, COND_AL, data_reg, addr_reg, 0);
        break;
    case 3:
        /* TODO: use block store -
         * check that data_reg2 > data_reg or the other way */
        tcg_out_st32_12(s, COND_AL, data_reg, addr_reg, 0);
        tcg_out_st32_12(s, COND_AL, data_reg2, addr_reg, 4);
        break;
    }
#endif
}

static uint8_t *tb_ret_addr;

static inline void tcg_out_op(TCGContext *s, int opc,
                const TCGArg *args, const int *const_args)
{
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
#ifdef SAVE_LR
        if (args[0] >> 8)
            tcg_out_ld32_12(s, COND_AL, TCG_REG_R0, 15, 0);
        else
            tcg_out_dat_imm(s, COND_AL, ARITH_MOV, TCG_REG_R0, 0, args[0]);
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV, 15, 0, 14, SHIFT_IMM_LSL(0));
        if (args[0] >> 8)
            tcg_out32(s, args[0]);
#else
        if (args[0] >> 8)
            tcg_out_ld32_12(s, COND_AL, 0, 15, 0);
        else
            tcg_out_dat_imm(s, COND_AL, ARITH_MOV, 0, 0, args[0]);
        tcg_out_goto(s, COND_AL, (tcg_target_ulong) tb_ret_addr);
        if (args[0] >> 8)
            tcg_out32(s, args[0]);
#endif
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* Direct jump method */
#if 1
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
            tcg_out_b(s, COND_AL, 8);
#else
            tcg_out_ld32_12(s, COND_AL, 15, 15, -4);
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
            tcg_out32(s, 0);
#endif
        } else {
            /* Indirect jump method */
#if 1
            c = (int) (s->tb_next + args[0]) - ((int) s->code_ptr + 8);
            if (c > 0xfff || c < -0xfff) {
                tcg_out_movi32(s, COND_AL, TCG_REG_R0,
                                (tcg_target_long) (s->tb_next + args[0]));
                tcg_out_ld32_12(s, COND_AL, 15, TCG_REG_R0, 0);
            } else
                tcg_out_ld32_12(s, COND_AL, 15, 15, c);
#else
            tcg_out_ld32_12(s, COND_AL, TCG_REG_R0, 15, 0);
            tcg_out_ld32_12(s, COND_AL, 15, TCG_REG_R0, 0);
            tcg_out32(s, (tcg_target_long) (s->tb_next + args[0]));
#endif
        }
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        if (const_args[0])
            tcg_out_call(s, COND_AL, args[0]);
        else
            tcg_out_callr(s, COND_AL, args[0]);
        break;
    case INDEX_op_jmp:
        if (const_args[0])
            tcg_out_goto(s, COND_AL, args[0]);
        else
            tcg_out_bx(s, COND_AL, args[0]);
        break;
    case INDEX_op_br:
        tcg_out_goto_label(s, COND_AL, args[0]);
        break;

    case INDEX_op_ld8u_i32:
        tcg_out_ld8u(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
        tcg_out_ld8s(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16u_i32:
        tcg_out_ld16u(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
        tcg_out_ld16s(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i32:
        tcg_out_ld32u(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
        tcg_out_st8u(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
        tcg_out_st16u(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
        tcg_out_st32(s, COND_AL, args[0], args[1], args[2]);
        break;

    case INDEX_op_mov_i32:
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV,
                        args[0], 0, args[1], SHIFT_IMM_LSL(0));
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi32(s, COND_AL, args[0], args[1]);
        break;
    case INDEX_op_add_i32:
        c = ARITH_ADD;
        goto gen_arith;
    case INDEX_op_sub_i32:
        c = ARITH_SUB;
        goto gen_arith;
    case INDEX_op_and_i32:
        c = ARITH_AND;
        goto gen_arith;
    case INDEX_op_or_i32:
        c = ARITH_ORR;
        goto gen_arith;
    case INDEX_op_xor_i32:
        c = ARITH_EOR;
        /* Fall through.  */
    gen_arith:
        tcg_out_dat_reg(s, COND_AL, c,
                        args[0], args[1], args[2], SHIFT_IMM_LSL(0));
        break;
    case INDEX_op_add2_i32:
        tcg_out_dat_reg2(s, COND_AL, ARITH_ADD, ARITH_ADC,
                        args[0], args[1], args[2], args[3],
                        args[4], args[5], SHIFT_IMM_LSL(0));
        break;
    case INDEX_op_sub2_i32:
        tcg_out_dat_reg2(s, COND_AL, ARITH_SUB, ARITH_SBC,
                        args[0], args[1], args[2], args[3],
                        args[4], args[5], SHIFT_IMM_LSL(0));
        break;
    case INDEX_op_neg_i32:
        tcg_out_dat_imm(s, COND_AL, ARITH_RSB, args[0], args[1], 0);
        break;
    case INDEX_op_mul_i32:
        tcg_out_mul32(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_umull32(s, COND_AL, args[0], args[1], args[2], args[3]);
        break;
    case INDEX_op_div2_i32:
        tcg_out_div_helper(s, COND_AL, args,
                        tcg_helper_div_i64, tcg_helper_rem_i64,
                        SHIFT_IMM_ASR(31));
        break;
    case INDEX_op_divu2_i32:
        tcg_out_div_helper(s, COND_AL, args,
                        tcg_helper_divu_i64, tcg_helper_remu_i64,
                        SHIFT_IMM_LSR(31));
        break;
    /* XXX: Perhaps args[2] & 0x1f is wrong */
    case INDEX_op_shl_i32:
        c = const_args[2] ?
                SHIFT_IMM_LSL(args[2] & 0x1f) : SHIFT_REG_LSL(args[2]);
        goto gen_shift32;
    case INDEX_op_shr_i32:
        c = const_args[2] ? (args[2] & 0x1f) ? SHIFT_IMM_LSR(args[2] & 0x1f) :
                SHIFT_IMM_LSL(0) : SHIFT_REG_LSR(args[2]);
        goto gen_shift32;
    case INDEX_op_sar_i32:
        c = const_args[2] ? (args[2] & 0x1f) ? SHIFT_IMM_ASR(args[2] & 0x1f) :
                SHIFT_IMM_LSL(0) : SHIFT_REG_ASR(args[2]);
        /* Fall through.  */
    gen_shift32:
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV, args[0], 0, args[1], c);
        break;

    case INDEX_op_brcond_i32:
        tcg_out_dat_reg(s, COND_AL, ARITH_CMP, 0,
                        args[0], args[1], SHIFT_IMM_LSL(0));
        tcg_out_goto_label(s, tcg_cond_to_arm_cond[args[2]], args[3]);
        break;
    case INDEX_op_brcond2_i32:
        /* The resulting conditions are:
         * TCG_COND_EQ    -->  a0 == a2 && a1 == a3,
         * TCG_COND_NE    --> (a0 != a2 && a1 == a3) ||  a1 != a3,
         * TCG_COND_LT(U) --> (a0 <  a2 && a1 == a3) ||  a1 <  a3,
         * TCG_COND_GE(U) --> (a0 >= a2 && a1 == a3) || (a1 >= a3 && a1 != a3),
         * TCG_COND_LE(U) --> (a0 <= a2 && a1 == a3) || (a1 <= a3 && a1 != a3),
         * TCG_COND_GT(U) --> (a0 >  a2 && a1 == a3) ||  a1 >  a3,
         */
        tcg_out_dat_reg(s, COND_AL, ARITH_CMP, 0,
                        args[1], args[3], SHIFT_IMM_LSL(0));
        tcg_out_dat_reg(s, COND_EQ, ARITH_CMP, 0,
                        args[0], args[2], SHIFT_IMM_LSL(0));
        tcg_out_goto_label(s, tcg_cond_to_arm_cond[args[4]], args[5]);
        break;

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld(s, COND_AL, args, 0);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld(s, COND_AL, args, 0 | 4);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld(s, COND_AL, args, 1);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld(s, COND_AL, args, 1 | 4);
        break;
    case INDEX_op_qemu_ld32u:
        tcg_out_qemu_ld(s, COND_AL, args, 2);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld(s, COND_AL, args, 3);
        break;

    case INDEX_op_qemu_st8:
        tcg_out_qemu_st(s, COND_AL, args, 0);
        break;
    case INDEX_op_qemu_st16:
        tcg_out_qemu_st(s, COND_AL, args, 1);
        break;
    case INDEX_op_qemu_st32:
        tcg_out_qemu_st(s, COND_AL, args, 2);
        break;
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st(s, COND_AL, args, 3);
        break;

    case INDEX_op_ext8s_i32:
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV,
                        args[0], 0, args[1], SHIFT_IMM_LSL(24));
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV,
                        args[0], 0, args[0], SHIFT_IMM_ASR(24));
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV,
                        args[0], 0, args[1], SHIFT_IMM_LSL(16));
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV,
                        args[0], 0, args[0], SHIFT_IMM_ASR(16));
        break;

    default:
        tcg_abort();
    }
}

static const TCGTargetOpDef arm_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_jmp, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },

    /* TODO: "r", "r", "ri" */
    { INDEX_op_add_i32, { "r", "r", "r" } },
    { INDEX_op_sub_i32, { "r", "r", "r" } },
    { INDEX_op_mul_i32, { "r", "r", "r" } },
    { INDEX_op_mulu2_i32, { "r", "r", "r", "r" } },
    { INDEX_op_div2_i32, { "r", "r", "r", "1", "2" } },
    { INDEX_op_divu2_i32, { "r", "r", "r", "1", "2" } },
    { INDEX_op_and_i32, { "r", "r", "r" } },
    { INDEX_op_or_i32, { "r", "r", "r" } },
    { INDEX_op_xor_i32, { "r", "r", "r" } },
    { INDEX_op_neg_i32, { "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "ri" } },
    { INDEX_op_shr_i32, { "r", "r", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },

    { INDEX_op_brcond_i32, { "r", "r" } },

    /* TODO: "r", "r", "r", "r", "ri", "ri" */
    { INDEX_op_add2_i32, { "r", "r", "r", "r", "r", "r" } },
    { INDEX_op_sub2_i32, { "r", "r", "r", "r", "r", "r" } },
    { INDEX_op_brcond2_i32, { "r", "r", "r", "r" } },

    { INDEX_op_qemu_ld8u, { "r", "x", "X" } },
    { INDEX_op_qemu_ld8s, { "r", "x", "X" } },
    { INDEX_op_qemu_ld16u, { "r", "x", "X" } },
    { INDEX_op_qemu_ld16s, { "r", "x", "X" } },
    { INDEX_op_qemu_ld32u, { "r", "x", "X" } },
    { INDEX_op_qemu_ld64, { "d", "r", "x", "X" } },

    { INDEX_op_qemu_st8, { "x", "x", "X" } },
    { INDEX_op_qemu_st16, { "x", "x", "X" } },
    { INDEX_op_qemu_st32, { "x", "x", "X" } },
    { INDEX_op_qemu_st64, { "x", "D", "x", "X" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },

    { -1 },
};

void tcg_target_init(TCGContext *s)
{
    /* fail safe */
    if ((1 << CPU_TLB_ENTRY_BITS) != sizeof(CPUTLBEntry))
        tcg_abort();

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0,
                    ((2 << TCG_REG_R14) - 1) & ~(1 << TCG_REG_R8));
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                    ((2 << TCG_REG_R3) - 1) |
                    (1 << TCG_REG_R12) | (1 << TCG_REG_R14));

    tcg_regset_clear(s->reserved_regs);
#ifdef SAVE_LR
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R14);
#endif
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R8);

    tcg_add_target_add_op_defs(arm_op_defs);
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, int arg,
                int arg1, tcg_target_long arg2)
{
    tcg_out_ld32u(s, COND_AL, arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                int arg1, tcg_target_long arg2)
{
    tcg_out_st32(s, COND_AL, arg, arg1, arg2);
}

void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    if (val > 0)
        if (val < 0x100)
            tcg_out_dat_imm(s, COND_AL, ARITH_ADD, reg, reg, val);
        else
            tcg_abort();
    else if (val < 0) {
        if (val > -0x100)
            tcg_out_dat_imm(s, COND_AL, ARITH_SUB, reg, reg, -val);
        else
            tcg_abort();
    }
}

static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, ret, 0, arg, SHIFT_IMM_LSL(0));
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                int ret, tcg_target_long arg)
{
    tcg_out_movi32(s, COND_AL, ret, arg);
}

void tcg_target_qemu_prologue(TCGContext *s)
{
    /* stmdb sp!, { r9 - r11, lr } */
    tcg_out32(s, (COND_AL << 28) | 0x092d4e00);

    tcg_out_bx(s, COND_AL, TCG_REG_R0);
    tb_ret_addr = s->code_ptr;

    /* ldmia sp!, { r9 - r11, pc } */
    tcg_out32(s, (COND_AL << 28) | 0x08bd8e00);
}
