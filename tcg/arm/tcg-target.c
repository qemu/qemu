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

#include "elf.h"
#include "tcg-be-ldst.h"

/* The __ARM_ARCH define is provided by gcc 4.8.  Construct it otherwise.  */
#ifndef __ARM_ARCH
# if defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) \
     || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) \
     || defined(__ARM_ARCH_7EM__)
#  define __ARM_ARCH 7
# elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) \
       || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__) \
       || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6T2__)
#  define __ARM_ARCH 6
# elif defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_5E__) \
       || defined(__ARM_ARCH_5T__) || defined(__ARM_ARCH_5TE__) \
       || defined(__ARM_ARCH_5TEJ__)
#  define __ARM_ARCH 5
# else
#  define __ARM_ARCH 4
# endif
#endif

static int arm_arch = __ARM_ARCH;

#if defined(__ARM_ARCH_5T__) \
    || defined(__ARM_ARCH_5TE__) || defined(__ARM_ARCH_5TEJ__)
# define use_armv5t_instructions 1
#else
# define use_armv5t_instructions use_armv6_instructions
#endif

#define use_armv6_instructions  (__ARM_ARCH >= 6 || arm_arch >= 6)
#define use_armv7_instructions  (__ARM_ARCH >= 7 || arm_arch >= 7)

#ifndef use_idiv_instructions
bool use_idiv_instructions;
#endif

/* ??? Ought to think about changing CONFIG_SOFTMMU to always defined.  */
#ifdef CONFIG_SOFTMMU
# define USING_SOFTMMU 1
#else
# define USING_SOFTMMU 0
#endif

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
    "%pc",
};
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
    TCG_REG_R13,
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R12,
    TCG_REG_R14,
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_R0, TCG_REG_R1, TCG_REG_R2, TCG_REG_R3
};
static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_R0, TCG_REG_R1
};

#define TCG_REG_TMP  TCG_REG_R12

static inline void reloc_pc24(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    ptrdiff_t offset = (tcg_ptr_byte_diff(target, code_ptr) - 8) >> 2;
    *code_ptr = (*code_ptr & ~0xffffff) | (offset & 0xffffff);
}

static void patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    assert(type == R_ARM_PC24);
    assert(addend == 0);
    reloc_pc24(code_ptr, (tcg_insn_unit *)value);
}

#define TCG_CT_CONST_ARM  0x100
#define TCG_CT_CONST_INV  0x200
#define TCG_CT_CONST_NEG  0x400
#define TCG_CT_CONST_ZERO 0x800

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'I':
        ct->ct |= TCG_CT_CONST_ARM;
        break;
    case 'K':
        ct->ct |= TCG_CT_CONST_INV;
        break;
    case 'N': /* The gcc constraint letter is L, already used here.  */
        ct->ct |= TCG_CT_CONST_NEG;
        break;
    case 'Z':
        ct->ct |= TCG_CT_CONST_ZERO;
        break;

    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        break;

    /* qemu_ld address */
    case 'l':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
#ifdef CONFIG_SOFTMMU
        /* r0-r2,lr will be overwritten when reading the tlb entry,
           so don't use these. */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R1);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R2);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R14);
#endif
        break;

    /* qemu_st address & data */
    case 's':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, (1 << TCG_TARGET_NB_REGS) - 1);
        /* r0-r2 will be overwritten when reading the tlb entry (softmmu only)
           and r0-r1 doing the byte swapping, so don't use these. */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R1);
#if defined(CONFIG_SOFTMMU)
        /* Avoid clashes with registers being used for helper args */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R2);
#if TARGET_LONG_BITS == 64
        /* Avoid clashes with registers being used for helper args */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
#endif
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R14);
#endif
        break;

    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;

    return 0;
}

static inline uint32_t rotl(uint32_t val, int n)
{
  return (val << n) | (val >> (32 - n));
}

/* ARM immediates for ALU instructions are made of an unsigned 8-bit
   right-rotated by an even amount between 0 and 30. */
static inline int encode_imm(uint32_t imm)
{
    int shift;

    /* simple case, only lower bits */
    if ((imm & ~0xff) == 0)
        return 0;
    /* then try a simple even shift */
    shift = ctz32(imm) & ~1;
    if (((imm >> shift) & ~0xff) == 0)
        return 32 - shift;
    /* now try harder with rotations */
    if ((rotl(imm, 2) & ~0xff) == 0)
        return 2;
    if ((rotl(imm, 4) & ~0xff) == 0)
        return 4;
    if ((rotl(imm, 6) & ~0xff) == 0)
        return 6;
    /* imm can't be encoded */
    return -1;
}

static inline int check_fit_imm(uint32_t imm)
{
    return encode_imm(imm) >= 0;
}

/* Test if a constant matches the constraint.
 * TODO: define constraints for:
 *
 * ldr/str offset:   between -0xfff and 0xfff
 * ldrh/strh offset: between -0xff and 0xff
 * mov operand2:     values represented with x << (2 * y), x < 0x100
 * add, sub, eor...: ditto
 */
static inline int tcg_target_const_match(tcg_target_long val, TCGType type,
                                         const TCGArgConstraint *arg_ct)
{
    int ct;
    ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    } else if ((ct & TCG_CT_CONST_ARM) && check_fit_imm(val)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_INV) && check_fit_imm(~val)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_NEG) && check_fit_imm(-val)) {
        return 1;
    } else if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    } else {
        return 0;
    }
}

#define TO_CPSR (1 << 20)

typedef enum {
    ARITH_AND = 0x0 << 21,
    ARITH_EOR = 0x1 << 21,
    ARITH_SUB = 0x2 << 21,
    ARITH_RSB = 0x3 << 21,
    ARITH_ADD = 0x4 << 21,
    ARITH_ADC = 0x5 << 21,
    ARITH_SBC = 0x6 << 21,
    ARITH_RSC = 0x7 << 21,
    ARITH_TST = 0x8 << 21 | TO_CPSR,
    ARITH_CMP = 0xa << 21 | TO_CPSR,
    ARITH_CMN = 0xb << 21 | TO_CPSR,
    ARITH_ORR = 0xc << 21,
    ARITH_MOV = 0xd << 21,
    ARITH_BIC = 0xe << 21,
    ARITH_MVN = 0xf << 21,

    INSN_LDR_IMM   = 0x04100000,
    INSN_LDR_REG   = 0x06100000,
    INSN_STR_IMM   = 0x04000000,
    INSN_STR_REG   = 0x06000000,

    INSN_LDRH_IMM  = 0x005000b0,
    INSN_LDRH_REG  = 0x001000b0,
    INSN_LDRSH_IMM = 0x005000f0,
    INSN_LDRSH_REG = 0x001000f0,
    INSN_STRH_IMM  = 0x004000b0,
    INSN_STRH_REG  = 0x000000b0,

    INSN_LDRB_IMM  = 0x04500000,
    INSN_LDRB_REG  = 0x06500000,
    INSN_LDRSB_IMM = 0x005000d0,
    INSN_LDRSB_REG = 0x001000d0,
    INSN_STRB_IMM  = 0x04400000,
    INSN_STRB_REG  = 0x06400000,

    INSN_LDRD_IMM  = 0x004000d0,
    INSN_LDRD_REG  = 0x000000d0,
    INSN_STRD_IMM  = 0x004000f0,
    INSN_STRD_REG  = 0x000000f0,
} ARMInsn;

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

static const uint8_t tcg_cond_to_arm_cond[] = {
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
    /* We pay attention here to not modify the branch target by masking
       the corresponding bytes.  This ensure that caches and memory are
       kept coherent during retranslation. */
    tcg_out32(s, deposit32(*s->code_ptr, 24, 8, (cond << 4) | 0x0a));
}

static inline void tcg_out_bl_noaddr(TCGContext *s, int cond)
{
    /* We pay attention here to not modify the branch target by masking
       the corresponding bytes.  This ensure that caches and memory are
       kept coherent during retranslation. */
    tcg_out32(s, deposit32(*s->code_ptr, 24, 8, (cond << 4) | 0x0b));
}

static inline void tcg_out_bl(TCGContext *s, int cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | 0x0b000000 |
                    (((offset - 8) >> 2) & 0x00ffffff));
}

static inline void tcg_out_blx(TCGContext *s, int cond, int rn)
{
    tcg_out32(s, (cond << 28) | 0x012fff30 | rn);
}

static inline void tcg_out_blx_imm(TCGContext *s, int32_t offset)
{
    tcg_out32(s, 0xfa000000 | ((offset & 2) << 23) |
                (((offset - 8) >> 2) & 0x00ffffff));
}

static inline void tcg_out_dat_reg(TCGContext *s,
                int cond, int opc, int rd, int rn, int rm, int shift)
{
    tcg_out32(s, (cond << 28) | (0 << 25) | opc |
                    (rn << 16) | (rd << 12) | shift | rm);
}

static inline void tcg_out_nop(TCGContext *s)
{
    if (use_armv7_instructions) {
        /* Architected nop introduced in v6k.  */
        /* ??? This is an MSR (imm) 0,0,0 insn.  Anyone know if this
           also Just So Happened to do nothing on pre-v6k so that we
           don't need to conditionalize it?  */
        tcg_out32(s, 0xe320f000);
    } else {
        /* Prior to that the assembler uses mov r0, r0.  */
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV, 0, 0, 0, SHIFT_IMM_LSL(0));
    }
}

static inline void tcg_out_mov_reg(TCGContext *s, int cond, int rd, int rm)
{
    /* Simple reg-reg move, optimising out the 'do nothing' case */
    if (rd != rm) {
        tcg_out_dat_reg(s, cond, ARITH_MOV, rd, 0, rm, SHIFT_IMM_LSL(0));
    }
}

static inline void tcg_out_dat_imm(TCGContext *s,
                int cond, int opc, int rd, int rn, int im)
{
    tcg_out32(s, (cond << 28) | (1 << 25) | opc |
                    (rn << 16) | (rd << 12) | im);
}

static void tcg_out_movi32(TCGContext *s, int cond, int rd, uint32_t arg)
{
    int rot, opc, rn;

    /* For armv7, make sure not to use movw+movt when mov/mvn would do.
       Speed things up by only checking when movt would be required.
       Prior to armv7, have one go at fully rotated immediates before
       doing the decomposition thing below.  */
    if (!use_armv7_instructions || (arg & 0xffff0000)) {
        rot = encode_imm(arg);
        if (rot >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_MOV, rd, 0,
                            rotl(arg, rot) | (rot << 7));
            return;
        }
        rot = encode_imm(~arg);
        if (rot >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_MVN, rd, 0,
                            rotl(~arg, rot) | (rot << 7));
            return;
        }
    }

    /* Use movw + movt.  */
    if (use_armv7_instructions) {
        /* movw */
        tcg_out32(s, (cond << 28) | 0x03000000 | (rd << 12)
                  | ((arg << 4) & 0x000f0000) | (arg & 0xfff));
        if (arg & 0xffff0000) {
            /* movt */
            tcg_out32(s, (cond << 28) | 0x03400000 | (rd << 12)
                      | ((arg >> 12) & 0x000f0000) | ((arg >> 16) & 0xfff));
        }
        return;
    }

    /* TODO: This is very suboptimal, we can easily have a constant
       pool somewhere after all the instructions.  */
    opc = ARITH_MOV;
    rn = 0;
    /* If we have lots of leading 1's, we can shorten the sequence by
       beginning with mvn and then clearing higher bits with eor.  */
    if (clz32(~arg) > clz32(arg)) {
        opc = ARITH_MVN, arg = ~arg;
    }
    do {
        int i = ctz32(arg) & ~1;
        rot = ((32 - i) << 7) & 0xf00;
        tcg_out_dat_imm(s, cond, opc, rd, rn, ((arg >> i) & 0xff) | rot);
        arg &= ~(0xff << i);

        opc = ARITH_EOR;
        rn = rd;
    } while (arg);
}

static inline void tcg_out_dat_rI(TCGContext *s, int cond, int opc, TCGArg dst,
                                  TCGArg lhs, TCGArg rhs, int rhs_is_const)
{
    /* Emit either the reg,imm or reg,reg form of a data-processing insn.
     * rhs must satisfy the "rI" constraint.
     */
    if (rhs_is_const) {
        int rot = encode_imm(rhs);
        assert(rot >= 0);
        tcg_out_dat_imm(s, cond, opc, dst, lhs, rotl(rhs, rot) | (rot << 7));
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_dat_rIK(TCGContext *s, int cond, int opc, int opinv,
                            TCGReg dst, TCGReg lhs, TCGArg rhs,
                            bool rhs_is_const)
{
    /* Emit either the reg,imm or reg,reg form of a data-processing insn.
     * rhs must satisfy the "rIK" constraint.
     */
    if (rhs_is_const) {
        int rot = encode_imm(rhs);
        if (rot < 0) {
            rhs = ~rhs;
            rot = encode_imm(rhs);
            assert(rot >= 0);
            opc = opinv;
        }
        tcg_out_dat_imm(s, cond, opc, dst, lhs, rotl(rhs, rot) | (rot << 7));
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_dat_rIN(TCGContext *s, int cond, int opc, int opneg,
                            TCGArg dst, TCGArg lhs, TCGArg rhs,
                            bool rhs_is_const)
{
    /* Emit either the reg,imm or reg,reg form of a data-processing insn.
     * rhs must satisfy the "rIN" constraint.
     */
    if (rhs_is_const) {
        int rot = encode_imm(rhs);
        if (rot < 0) {
            rhs = -rhs;
            rot = encode_imm(rhs);
            assert(rot >= 0);
            opc = opneg;
        }
        tcg_out_dat_imm(s, cond, opc, dst, lhs, rotl(rhs, rot) | (rot << 7));
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static inline void tcg_out_mul32(TCGContext *s, int cond, TCGReg rd,
                                 TCGReg rn, TCGReg rm)
{
    /* if ArchVersion() < 6 && d == n then UNPREDICTABLE;  */
    if (!use_armv6_instructions && rd == rn) {
        if (rd == rm) {
            /* rd == rn == rm; copy an input to tmp first.  */
            tcg_out_mov_reg(s, cond, TCG_REG_TMP, rn);
            rm = rn = TCG_REG_TMP;
        } else {
            rn = rm;
            rm = rd;
        }
    }
    /* mul */
    tcg_out32(s, (cond << 28) | 0x90 | (rd << 16) | (rm << 8) | rn);
}

static inline void tcg_out_umull32(TCGContext *s, int cond, TCGReg rd0,
                                   TCGReg rd1, TCGReg rn, TCGReg rm)
{
    /* if ArchVersion() < 6 && (dHi == n || dLo == n) then UNPREDICTABLE;  */
    if (!use_armv6_instructions && (rd0 == rn || rd1 == rn)) {
        if (rd0 == rm || rd1 == rm) {
            tcg_out_mov_reg(s, cond, TCG_REG_TMP, rn);
            rn = TCG_REG_TMP;
        } else {
            TCGReg t = rn;
            rn = rm;
            rm = t;
        }
    }
    /* umull */
    tcg_out32(s, (cond << 28) | 0x00800090 |
              (rd1 << 16) | (rd0 << 12) | (rm << 8) | rn);
}

static inline void tcg_out_smull32(TCGContext *s, int cond, TCGReg rd0,
                                   TCGReg rd1, TCGReg rn, TCGReg rm)
{
    /* if ArchVersion() < 6 && (dHi == n || dLo == n) then UNPREDICTABLE;  */
    if (!use_armv6_instructions && (rd0 == rn || rd1 == rn)) {
        if (rd0 == rm || rd1 == rm) {
            tcg_out_mov_reg(s, cond, TCG_REG_TMP, rn);
            rn = TCG_REG_TMP;
        } else {
            TCGReg t = rn;
            rn = rm;
            rm = t;
        }
    }
    /* smull */
    tcg_out32(s, (cond << 28) | 0x00c00090 |
              (rd1 << 16) | (rd0 << 12) | (rm << 8) | rn);
}

static inline void tcg_out_sdiv(TCGContext *s, int cond, int rd, int rn, int rm)
{
    tcg_out32(s, 0x0710f010 | (cond << 28) | (rd << 16) | rn | (rm << 8));
}

static inline void tcg_out_udiv(TCGContext *s, int cond, int rd, int rn, int rm)
{
    tcg_out32(s, 0x0730f010 | (cond << 28) | (rd << 16) | rn | (rm << 8));
}

static inline void tcg_out_ext8s(TCGContext *s, int cond,
                                 int rd, int rn)
{
    if (use_armv6_instructions) {
        /* sxtb */
        tcg_out32(s, 0x06af0070 | (cond << 28) | (rd << 12) | rn);
    } else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, rn, SHIFT_IMM_LSL(24));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, rd, SHIFT_IMM_ASR(24));
    }
}

static inline void tcg_out_ext8u(TCGContext *s, int cond,
                                 int rd, int rn)
{
    tcg_out_dat_imm(s, cond, ARITH_AND, rd, rn, 0xff);
}

static inline void tcg_out_ext16s(TCGContext *s, int cond,
                                  int rd, int rn)
{
    if (use_armv6_instructions) {
        /* sxth */
        tcg_out32(s, 0x06bf0070 | (cond << 28) | (rd << 12) | rn);
    } else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, rn, SHIFT_IMM_LSL(16));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, rd, SHIFT_IMM_ASR(16));
    }
}

static inline void tcg_out_ext16u(TCGContext *s, int cond,
                                  int rd, int rn)
{
    if (use_armv6_instructions) {
        /* uxth */
        tcg_out32(s, 0x06ff0070 | (cond << 28) | (rd << 12) | rn);
    } else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, rn, SHIFT_IMM_LSL(16));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, rd, SHIFT_IMM_LSR(16));
    }
}

static inline void tcg_out_bswap16s(TCGContext *s, int cond, int rd, int rn)
{
    if (use_armv6_instructions) {
        /* revsh */
        tcg_out32(s, 0x06ff0fb0 | (cond << 28) | (rd << 12) | rn);
    } else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        TCG_REG_TMP, 0, rn, SHIFT_IMM_LSL(24));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        TCG_REG_TMP, 0, TCG_REG_TMP, SHIFT_IMM_ASR(16));
        tcg_out_dat_reg(s, cond, ARITH_ORR,
                        rd, TCG_REG_TMP, rn, SHIFT_IMM_LSR(8));
    }
}

static inline void tcg_out_bswap16(TCGContext *s, int cond, int rd, int rn)
{
    if (use_armv6_instructions) {
        /* rev16 */
        tcg_out32(s, 0x06bf0fb0 | (cond << 28) | (rd << 12) | rn);
    } else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        TCG_REG_TMP, 0, rn, SHIFT_IMM_LSL(24));
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        TCG_REG_TMP, 0, TCG_REG_TMP, SHIFT_IMM_LSR(16));
        tcg_out_dat_reg(s, cond, ARITH_ORR,
                        rd, TCG_REG_TMP, rn, SHIFT_IMM_LSR(8));
    }
}

/* swap the two low bytes assuming that the two high input bytes and the
   two high output bit can hold any value. */
static inline void tcg_out_bswap16st(TCGContext *s, int cond, int rd, int rn)
{
    if (use_armv6_instructions) {
        /* rev16 */
        tcg_out32(s, 0x06bf0fb0 | (cond << 28) | (rd << 12) | rn);
    } else {
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        TCG_REG_TMP, 0, rn, SHIFT_IMM_LSR(8));
        tcg_out_dat_imm(s, cond, ARITH_AND, TCG_REG_TMP, TCG_REG_TMP, 0xff);
        tcg_out_dat_reg(s, cond, ARITH_ORR,
                        rd, TCG_REG_TMP, rn, SHIFT_IMM_LSL(8));
    }
}

static inline void tcg_out_bswap32(TCGContext *s, int cond, int rd, int rn)
{
    if (use_armv6_instructions) {
        /* rev */
        tcg_out32(s, 0x06bf0f30 | (cond << 28) | (rd << 12) | rn);
    } else {
        tcg_out_dat_reg(s, cond, ARITH_EOR,
                        TCG_REG_TMP, rn, rn, SHIFT_IMM_ROR(16));
        tcg_out_dat_imm(s, cond, ARITH_BIC,
                        TCG_REG_TMP, TCG_REG_TMP, 0xff | 0x800);
        tcg_out_dat_reg(s, cond, ARITH_MOV,
                        rd, 0, rn, SHIFT_IMM_ROR(8));
        tcg_out_dat_reg(s, cond, ARITH_EOR,
                        rd, rd, TCG_REG_TMP, SHIFT_IMM_LSR(8));
    }
}

bool tcg_target_deposit_valid(int ofs, int len)
{
    /* ??? Without bfi, we could improve over generic code by combining
       the right-shift from a non-zero ofs with the orr.  We do run into
       problems when rd == rs, and the mask generated from ofs+len doesn't
       fit into an immediate.  We would have to be careful not to pessimize
       wrt the optimizations performed on the expanded code.  */
    return use_armv7_instructions;
}

static inline void tcg_out_deposit(TCGContext *s, int cond, TCGReg rd,
                                   TCGArg a1, int ofs, int len, bool const_a1)
{
    if (const_a1) {
        /* bfi becomes bfc with rn == 15.  */
        a1 = 15;
    }
    /* bfi/bfc */
    tcg_out32(s, 0x07c00010 | (cond << 28) | (rd << 12) | a1
              | (ofs << 7) | ((ofs + len - 1) << 16));
}

/* Note that this routine is used for both LDR and LDRH formats, so we do
   not wish to include an immediate shift at this point.  */
static void tcg_out_memop_r(TCGContext *s, int cond, ARMInsn opc, TCGReg rt,
                            TCGReg rn, TCGReg rm, bool u, bool p, bool w)
{
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24)
              | (w << 21) | (rn << 16) | (rt << 12) | rm);
}

static void tcg_out_memop_8(TCGContext *s, int cond, ARMInsn opc, TCGReg rt,
                            TCGReg rn, int imm8, bool p, bool w)
{
    bool u = 1;
    if (imm8 < 0) {
        imm8 = -imm8;
        u = 0;
    }
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24) | (w << 21) |
              (rn << 16) | (rt << 12) | ((imm8 & 0xf0) << 4) | (imm8 & 0xf));
}

static void tcg_out_memop_12(TCGContext *s, int cond, ARMInsn opc, TCGReg rt,
                             TCGReg rn, int imm12, bool p, bool w)
{
    bool u = 1;
    if (imm12 < 0) {
        imm12 = -imm12;
        u = 0;
    }
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24) | (w << 21) |
              (rn << 16) | (rt << 12) | imm12);
}

static inline void tcg_out_ld32_12(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_LDR_IMM, rt, rn, imm12, 1, 0);
}

static inline void tcg_out_st32_12(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_STR_IMM, rt, rn, imm12, 1, 0);
}

static inline void tcg_out_ld32_r(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDR_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_st32_r(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STR_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_ldrd_8(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRD_IMM, rt, rn, imm8, 1, 0);
}

static inline void tcg_out_ldrd_r(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRD_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_strd_8(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_STRD_IMM, rt, rn, imm8, 1, 0);
}

static inline void tcg_out_strd_r(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRD_REG, rt, rn, rm, 1, 1, 0);
}

/* Register pre-increment with base writeback.  */
static inline void tcg_out_ld32_rwb(TCGContext *s, int cond, TCGReg rt,
                                    TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDR_REG, rt, rn, rm, 1, 1, 1);
}

static inline void tcg_out_st32_rwb(TCGContext *s, int cond, TCGReg rt,
                                    TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STR_REG, rt, rn, rm, 1, 1, 1);
}

static inline void tcg_out_ld16u_8(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRH_IMM, rt, rn, imm8, 1, 0);
}

static inline void tcg_out_st16_8(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_STRH_IMM, rt, rn, imm8, 1, 0);
}

static inline void tcg_out_ld16u_r(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRH_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_st16_r(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRH_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_ld16s_8(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRSH_IMM, rt, rn, imm8, 1, 0);
}

static inline void tcg_out_ld16s_r(TCGContext *s, int cond, TCGReg rt,
                                   TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRSH_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_ld8_12(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_LDRB_IMM, rt, rn, imm12, 1, 0);
}

static inline void tcg_out_st8_12(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_STRB_IMM, rt, rn, imm12, 1, 0);
}

static inline void tcg_out_ld8_r(TCGContext *s, int cond, TCGReg rt,
                                 TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRB_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_st8_r(TCGContext *s, int cond, TCGReg rt,
                                 TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRB_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_ld8s_8(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRSB_IMM, rt, rn, imm8, 1, 0);
}

static inline void tcg_out_ld8s_r(TCGContext *s, int cond, TCGReg rt,
                                  TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRSB_REG, rt, rn, rm, 1, 1, 0);
}

static inline void tcg_out_ld32u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld32_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld32_12(s, cond, rd, rn, offset);
}

static inline void tcg_out_st32(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_st32_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_st32_12(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld16u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld16u_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld16u_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld16s(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld16s_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld16s_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_st16(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_st16_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_st16_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld8u(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld8_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld8_12(s, cond, rd, rn, offset);
}

static inline void tcg_out_ld8s(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld8s_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld8s_8(s, cond, rd, rn, offset);
}

static inline void tcg_out_st8(TCGContext *s, int cond,
                int rd, int rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_st8_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_st8_12(s, cond, rd, rn, offset);
}

/* The _goto case is normally between TBs within the same code buffer, and
 * with the code buffer limited to 16MB we wouldn't need the long case.
 * But we also use it for the tail-call to the qemu_ld/st helpers, which does.
 */
static inline void tcg_out_goto(TCGContext *s, int cond, tcg_insn_unit *addr)
{
    intptr_t addri = (intptr_t)addr;
    ptrdiff_t disp = tcg_pcrel_diff(s, addr);

    if ((addri & 1) == 0 && disp - 8 < 0x01fffffd && disp - 8 > -0x01fffffd) {
        tcg_out_b(s, cond, disp);
        return;
    }

    tcg_out_movi32(s, cond, TCG_REG_TMP, addri);
    if (use_armv5t_instructions) {
        tcg_out_bx(s, cond, TCG_REG_TMP);
    } else {
        if (addri & 1) {
            tcg_abort();
        }
        tcg_out_mov_reg(s, cond, TCG_REG_PC, TCG_REG_TMP);
    }
}

/* The call case is mostly used for helpers - so it's not unreasonable
 * for them to be beyond branch range */
static void tcg_out_call(TCGContext *s, tcg_insn_unit *addr)
{
    intptr_t addri = (intptr_t)addr;
    ptrdiff_t disp = tcg_pcrel_diff(s, addr);

    if (disp - 8 < 0x02000000 && disp - 8 >= -0x02000000) {
        if (addri & 1) {
            /* Use BLX if the target is in Thumb mode */
            if (!use_armv5t_instructions) {
                tcg_abort();
            }
            tcg_out_blx_imm(s, disp);
        } else {
            tcg_out_bl(s, COND_AL, disp);
        }
    } else if (use_armv7_instructions) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, addri);
        tcg_out_blx(s, COND_AL, TCG_REG_TMP);
    } else {
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, TCG_REG_R14, TCG_REG_PC, 4);
        tcg_out_ld32_12(s, COND_AL, TCG_REG_PC, TCG_REG_PC, -4);
        tcg_out32(s, addri);
    }
}

static inline void tcg_out_goto_label(TCGContext *s, int cond, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (l->has_value) {
        tcg_out_goto(s, cond, l->u.value_ptr);
    } else {
        tcg_out_reloc(s, s->code_ptr, R_ARM_PC24, label_index, 0);
        tcg_out_b_noaddr(s, cond);
    }
}

#ifdef CONFIG_SOFTMMU
/* helper signature: helper_ret_ld_mmu(CPUState *env, target_ulong addr,
 *                                     int mmu_idx, uintptr_t ra)
 */
static void * const qemu_ld_helpers[16] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_SB]   = helper_ret_ldsb_mmu,

    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
    [MO_LEQ]  = helper_le_ldq_mmu,
    [MO_LESW] = helper_le_ldsw_mmu,
    [MO_LESL] = helper_le_ldul_mmu,

    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
    [MO_BEQ]  = helper_be_ldq_mmu,
    [MO_BESW] = helper_be_ldsw_mmu,
    [MO_BESL] = helper_be_ldul_mmu,
};

/* helper signature: helper_ret_st_mmu(CPUState *env, target_ulong addr,
 *                                     uintxx_t val, int mmu_idx, uintptr_t ra)
 */
static void * const qemu_st_helpers[16] = {
    [MO_UB]   = helper_ret_stb_mmu,
    [MO_LEUW] = helper_le_stw_mmu,
    [MO_LEUL] = helper_le_stl_mmu,
    [MO_LEQ]  = helper_le_stq_mmu,
    [MO_BEUW] = helper_be_stw_mmu,
    [MO_BEUL] = helper_be_stl_mmu,
    [MO_BEQ]  = helper_be_stq_mmu,
};

/* Helper routines for marshalling helper function arguments into
 * the correct registers and stack.
 * argreg is where we want to put this argument, arg is the argument itself.
 * Return value is the updated argreg ready for the next call.
 * Note that argreg 0..3 is real registers, 4+ on stack.
 *
 * We provide routines for arguments which are: immediate, 32 bit
 * value in register, 16 and 8 bit values in register (which must be zero
 * extended before use) and 64 bit value in a lo:hi register pair.
 */
#define DEFINE_TCG_OUT_ARG(NAME, ARGTYPE, MOV_ARG, EXT_ARG)                \
static TCGReg NAME(TCGContext *s, TCGReg argreg, ARGTYPE arg)              \
{                                                                          \
    if (argreg < 4) {                                                      \
        MOV_ARG(s, COND_AL, argreg, arg);                                  \
    } else {                                                               \
        int ofs = (argreg - 4) * 4;                                        \
        EXT_ARG;                                                           \
        assert(ofs + 4 <= TCG_STATIC_CALL_ARGS_SIZE);                      \
        tcg_out_st32_12(s, COND_AL, arg, TCG_REG_CALL_STACK, ofs);         \
    }                                                                      \
    return argreg + 1;                                                     \
}

DEFINE_TCG_OUT_ARG(tcg_out_arg_imm32, uint32_t, tcg_out_movi32,
    (tcg_out_movi32(s, COND_AL, TCG_REG_TMP, arg), arg = TCG_REG_TMP))
DEFINE_TCG_OUT_ARG(tcg_out_arg_reg8, TCGReg, tcg_out_ext8u,
    (tcg_out_ext8u(s, COND_AL, TCG_REG_TMP, arg), arg = TCG_REG_TMP))
DEFINE_TCG_OUT_ARG(tcg_out_arg_reg16, TCGReg, tcg_out_ext16u,
    (tcg_out_ext16u(s, COND_AL, TCG_REG_TMP, arg), arg = TCG_REG_TMP))
DEFINE_TCG_OUT_ARG(tcg_out_arg_reg32, TCGReg, tcg_out_mov_reg, )

static TCGReg tcg_out_arg_reg64(TCGContext *s, TCGReg argreg,
                                TCGReg arglo, TCGReg arghi)
{
    /* 64 bit arguments must go in even/odd register pairs
     * and in 8-aligned stack slots.
     */
    if (argreg & 1) {
        argreg++;
    }
    if (use_armv6_instructions && argreg >= 4
        && (arglo & 1) == 0 && arghi == arglo + 1) {
        tcg_out_strd_8(s, COND_AL, arglo,
                       TCG_REG_CALL_STACK, (argreg - 4) * 4);
        return argreg + 2;
    } else {
        argreg = tcg_out_arg_reg32(s, argreg, arglo);
        argreg = tcg_out_arg_reg32(s, argreg, arghi);
        return argreg;
    }
}

#define TLB_SHIFT	(CPU_TLB_ENTRY_BITS + CPU_TLB_BITS)

/* We're expecting to use an 8-bit immediate and to mask.  */
QEMU_BUILD_BUG_ON(CPU_TLB_BITS > 8);

/* We're expecting to use an 8-bit immediate add + 8-bit ldrd offset.
   Using the offset of the second entry in the last tlb table ensures
   that we can index all of the elements of the first entry.  */
QEMU_BUILD_BUG_ON(offsetof(CPUArchState, tlb_table[NB_MMU_MODES - 1][1])
                  > 0xffff);

/* Load and compare a TLB entry, leaving the flags set.  Returns the register
   containing the addend of the tlb entry.  Clobbers R0, R1, R2, TMP.  */

static TCGReg tcg_out_tlb_read(TCGContext *s, TCGReg addrlo, TCGReg addrhi,
                               TCGMemOp s_bits, int mem_index, bool is_load)
{
    TCGReg base = TCG_AREG0;
    int cmp_off =
        (is_load
         ? offsetof(CPUArchState, tlb_table[mem_index][0].addr_read)
         : offsetof(CPUArchState, tlb_table[mem_index][0].addr_write));
    int add_off = offsetof(CPUArchState, tlb_table[mem_index][0].addend);

    /* Should generate something like the following:
     *   shr    tmp, addrlo, #TARGET_PAGE_BITS                    (1)
     *   add    r2, env, #high
     *   and    r0, tmp, #(CPU_TLB_SIZE - 1)                      (2)
     *   add    r2, r2, r0, lsl #CPU_TLB_ENTRY_BITS               (3)
     *   ldr    r0, [r2, #cmp]                                    (4)
     *   tst    addrlo, #s_mask
     *   ldr    r2, [r2, #add]                                    (5)
     *   cmpeq  r0, tmp, lsl #TARGET_PAGE_BITS
     */
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, TCG_REG_TMP,
                    0, addrlo, SHIFT_IMM_LSR(TARGET_PAGE_BITS));

    /* We checked that the offset is contained within 16 bits above.  */
    if (add_off > 0xfff || (use_armv6_instructions && cmp_off > 0xff)) {
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, TCG_REG_R2, base,
                        (24 << 7) | (cmp_off >> 8));
        base = TCG_REG_R2;
        add_off -= cmp_off & 0xff00;
        cmp_off &= 0xff;
    }

    tcg_out_dat_imm(s, COND_AL, ARITH_AND,
                    TCG_REG_R0, TCG_REG_TMP, CPU_TLB_SIZE - 1);
    tcg_out_dat_reg(s, COND_AL, ARITH_ADD, TCG_REG_R2, base,
                    TCG_REG_R0, SHIFT_IMM_LSL(CPU_TLB_ENTRY_BITS));

    /* Load the tlb comparator.  Use ldrd if needed and available,
       but due to how the pointer needs setting up, ldm isn't useful.
       Base arm5 doesn't have ldrd, but armv5te does.  */
    if (use_armv6_instructions && TARGET_LONG_BITS == 64) {
        tcg_out_ldrd_8(s, COND_AL, TCG_REG_R0, TCG_REG_R2, cmp_off);
    } else {
        tcg_out_ld32_12(s, COND_AL, TCG_REG_R0, TCG_REG_R2, cmp_off);
        if (TARGET_LONG_BITS == 64) {
            tcg_out_ld32_12(s, COND_AL, TCG_REG_R1, TCG_REG_R2, cmp_off + 4);
        }
    }

    /* Check alignment.  */
    if (s_bits) {
        tcg_out_dat_imm(s, COND_AL, ARITH_TST,
                        0, addrlo, (1 << s_bits) - 1);
    }

    /* Load the tlb addend.  */
    tcg_out_ld32_12(s, COND_AL, TCG_REG_R2, TCG_REG_R2, add_off);

    tcg_out_dat_reg(s, (s_bits ? COND_EQ : COND_AL), ARITH_CMP, 0,
                    TCG_REG_R0, TCG_REG_TMP, SHIFT_IMM_LSL(TARGET_PAGE_BITS));

    if (TARGET_LONG_BITS == 64) {
        tcg_out_dat_reg(s, COND_EQ, ARITH_CMP, 0,
                        TCG_REG_R1, addrhi, SHIFT_IMM_LSL(0));
    }

    return TCG_REG_R2;
}

/* Record the context of a call to the out of line helper code for the slow
   path for a load or store, so that we can later generate the correct
   helper code.  */
static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOp opc,
                                TCGReg datalo, TCGReg datahi, TCGReg addrlo,
                                TCGReg addrhi, int mem_index,
                                tcg_insn_unit *raddr, tcg_insn_unit *label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->opc = opc;
    label->datalo_reg = datalo;
    label->datahi_reg = datahi;
    label->addrlo_reg = addrlo;
    label->addrhi_reg = addrhi;
    label->mem_index = mem_index;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr;
}

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg argreg, datalo, datahi;
    TCGMemOp opc = lb->opc;
    void *func;

    reloc_pc24(lb->label_ptr[0], s->code_ptr);

    argreg = tcg_out_arg_reg32(s, TCG_REG_R0, TCG_AREG0);
    if (TARGET_LONG_BITS == 64) {
        argreg = tcg_out_arg_reg64(s, argreg, lb->addrlo_reg, lb->addrhi_reg);
    } else {
        argreg = tcg_out_arg_reg32(s, argreg, lb->addrlo_reg);
    }
    argreg = tcg_out_arg_imm32(s, argreg, lb->mem_index);
    argreg = tcg_out_arg_reg32(s, argreg, TCG_REG_R14);

    /* For armv6 we can use the canonical unsigned helpers and minimize
       icache usage.  For pre-armv6, use the signed helpers since we do
       not have a single insn sign-extend.  */
    if (use_armv6_instructions) {
        func = qemu_ld_helpers[opc & ~MO_SIGN];
    } else {
        func = qemu_ld_helpers[opc];
        if (opc & MO_SIGN) {
            opc = MO_UL;
        }
    }
    tcg_out_call(s, func);

    datalo = lb->datalo_reg;
    datahi = lb->datahi_reg;
    switch (opc & MO_SSIZE) {
    case MO_SB:
        tcg_out_ext8s(s, COND_AL, datalo, TCG_REG_R0);
        break;
    case MO_SW:
        tcg_out_ext16s(s, COND_AL, datalo, TCG_REG_R0);
        break;
    default:
        tcg_out_mov_reg(s, COND_AL, datalo, TCG_REG_R0);
        break;
    case MO_Q:
        if (datalo != TCG_REG_R1) {
            tcg_out_mov_reg(s, COND_AL, datalo, TCG_REG_R0);
            tcg_out_mov_reg(s, COND_AL, datahi, TCG_REG_R1);
        } else if (datahi != TCG_REG_R0) {
            tcg_out_mov_reg(s, COND_AL, datahi, TCG_REG_R1);
            tcg_out_mov_reg(s, COND_AL, datalo, TCG_REG_R0);
        } else {
            tcg_out_mov_reg(s, COND_AL, TCG_REG_TMP, TCG_REG_R0);
            tcg_out_mov_reg(s, COND_AL, datahi, TCG_REG_R1);
            tcg_out_mov_reg(s, COND_AL, datalo, TCG_REG_TMP);
        }
        break;
    }

    tcg_out_goto(s, COND_AL, lb->raddr);
}

static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg argreg, datalo, datahi;
    TCGMemOp opc = lb->opc;

    reloc_pc24(lb->label_ptr[0], s->code_ptr);

    argreg = TCG_REG_R0;
    argreg = tcg_out_arg_reg32(s, argreg, TCG_AREG0);
    if (TARGET_LONG_BITS == 64) {
        argreg = tcg_out_arg_reg64(s, argreg, lb->addrlo_reg, lb->addrhi_reg);
    } else {
        argreg = tcg_out_arg_reg32(s, argreg, lb->addrlo_reg);
    }

    datalo = lb->datalo_reg;
    datahi = lb->datahi_reg;
    switch (opc & MO_SIZE) {
    case MO_8:
        argreg = tcg_out_arg_reg8(s, argreg, datalo);
        break;
    case MO_16:
        argreg = tcg_out_arg_reg16(s, argreg, datalo);
        break;
    case MO_32:
    default:
        argreg = tcg_out_arg_reg32(s, argreg, datalo);
        break;
    case MO_64:
        argreg = tcg_out_arg_reg64(s, argreg, datalo, datahi);
        break;
    }

    argreg = tcg_out_arg_imm32(s, argreg, lb->mem_index);
    argreg = tcg_out_arg_reg32(s, argreg, TCG_REG_R14);

    /* Tail-call to the helper, which will return to the fast path.  */
    tcg_out_goto(s, COND_AL, qemu_st_helpers[opc]);
}
#endif /* SOFTMMU */

static inline void tcg_out_qemu_ld_index(TCGContext *s, TCGMemOp opc,
                                         TCGReg datalo, TCGReg datahi,
                                         TCGReg addrlo, TCGReg addend)
{
    TCGMemOp bswap = opc & MO_BSWAP;

    switch (opc & MO_SSIZE) {
    case MO_UB:
        tcg_out_ld8_r(s, COND_AL, datalo, addrlo, addend);
        break;
    case MO_SB:
        tcg_out_ld8s_r(s, COND_AL, datalo, addrlo, addend);
        break;
    case MO_UW:
        tcg_out_ld16u_r(s, COND_AL, datalo, addrlo, addend);
        if (bswap) {
            tcg_out_bswap16(s, COND_AL, datalo, datalo);
        }
        break;
    case MO_SW:
        if (bswap) {
            tcg_out_ld16u_r(s, COND_AL, datalo, addrlo, addend);
            tcg_out_bswap16s(s, COND_AL, datalo, datalo);
        } else {
            tcg_out_ld16s_r(s, COND_AL, datalo, addrlo, addend);
        }
        break;
    case MO_UL:
    default:
        tcg_out_ld32_r(s, COND_AL, datalo, addrlo, addend);
        if (bswap) {
            tcg_out_bswap32(s, COND_AL, datalo, datalo);
        }
        break;
    case MO_Q:
        {
            TCGReg dl = (bswap ? datahi : datalo);
            TCGReg dh = (bswap ? datalo : datahi);

            /* Avoid ldrd for user-only emulation, to handle unaligned.  */
            if (USING_SOFTMMU && use_armv6_instructions
                && (dl & 1) == 0 && dh == dl + 1) {
                tcg_out_ldrd_r(s, COND_AL, dl, addrlo, addend);
            } else if (dl != addend) {
                tcg_out_ld32_rwb(s, COND_AL, dl, addend, addrlo);
                tcg_out_ld32_12(s, COND_AL, dh, addend, 4);
            } else {
                tcg_out_dat_reg(s, COND_AL, ARITH_ADD, TCG_REG_TMP,
                                addend, addrlo, SHIFT_IMM_LSL(0));
                tcg_out_ld32_12(s, COND_AL, dl, TCG_REG_TMP, 0);
                tcg_out_ld32_12(s, COND_AL, dh, TCG_REG_TMP, 4);
            }
            if (bswap) {
                tcg_out_bswap32(s, COND_AL, dl, dl);
                tcg_out_bswap32(s, COND_AL, dh, dh);
            }
        }
        break;
    }
}

static inline void tcg_out_qemu_ld_direct(TCGContext *s, TCGMemOp opc,
                                          TCGReg datalo, TCGReg datahi,
                                          TCGReg addrlo)
{
    TCGMemOp bswap = opc & MO_BSWAP;

    switch (opc & MO_SSIZE) {
    case MO_UB:
        tcg_out_ld8_12(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_SB:
        tcg_out_ld8s_8(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_UW:
        tcg_out_ld16u_8(s, COND_AL, datalo, addrlo, 0);
        if (bswap) {
            tcg_out_bswap16(s, COND_AL, datalo, datalo);
        }
        break;
    case MO_SW:
        if (bswap) {
            tcg_out_ld16u_8(s, COND_AL, datalo, addrlo, 0);
            tcg_out_bswap16s(s, COND_AL, datalo, datalo);
        } else {
            tcg_out_ld16s_8(s, COND_AL, datalo, addrlo, 0);
        }
        break;
    case MO_UL:
    default:
        tcg_out_ld32_12(s, COND_AL, datalo, addrlo, 0);
        if (bswap) {
            tcg_out_bswap32(s, COND_AL, datalo, datalo);
        }
        break;
    case MO_Q:
        {
            TCGReg dl = (bswap ? datahi : datalo);
            TCGReg dh = (bswap ? datalo : datahi);

            /* Avoid ldrd for user-only emulation, to handle unaligned.  */
            if (USING_SOFTMMU && use_armv6_instructions
                && (dl & 1) == 0 && dh == dl + 1) {
                tcg_out_ldrd_8(s, COND_AL, dl, addrlo, 0);
            } else if (dl == addrlo) {
                tcg_out_ld32_12(s, COND_AL, dh, addrlo, bswap ? 0 : 4);
                tcg_out_ld32_12(s, COND_AL, dl, addrlo, bswap ? 4 : 0);
            } else {
                tcg_out_ld32_12(s, COND_AL, dl, addrlo, bswap ? 4 : 0);
                tcg_out_ld32_12(s, COND_AL, dh, addrlo, bswap ? 0 : 4);
            }
            if (bswap) {
                tcg_out_bswap32(s, COND_AL, dl, dl);
                tcg_out_bswap32(s, COND_AL, dh, dh);
            }
        }
        break;
    }
}

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg addrlo, datalo, datahi, addrhi __attribute__((unused));
    TCGMemOp opc;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    TCGReg addend;
    tcg_insn_unit *label_ptr;
#endif

    datalo = *args++;
    datahi = (is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS == 64 ? *args++ : 0);
    opc = *args++;

#ifdef CONFIG_SOFTMMU
    mem_index = *args;
    addend = tcg_out_tlb_read(s, addrlo, addrhi, opc & MO_SIZE, mem_index, 1);

    /* This a conditional BL only to load a pointer within this opcode into LR
       for the slow path.  We will not be using the value for a tail call.  */
    label_ptr = s->code_ptr;
    tcg_out_bl_noaddr(s, COND_NE);

    tcg_out_qemu_ld_index(s, opc, datalo, datahi, addrlo, addend);

    add_qemu_ldst_label(s, true, opc, datalo, datahi, addrlo, addrhi,
                        mem_index, s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    if (GUEST_BASE) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP, GUEST_BASE);
        tcg_out_qemu_ld_index(s, opc, datalo, datahi, addrlo, TCG_REG_TMP);
    } else {
        tcg_out_qemu_ld_direct(s, opc, datalo, datahi, addrlo);
    }
#endif
}

static inline void tcg_out_qemu_st_index(TCGContext *s, int cond, TCGMemOp opc,
                                         TCGReg datalo, TCGReg datahi,
                                         TCGReg addrlo, TCGReg addend)
{
    TCGMemOp bswap = opc & MO_BSWAP;

    switch (opc & MO_SIZE) {
    case MO_8:
        tcg_out_st8_r(s, cond, datalo, addrlo, addend);
        break;
    case MO_16:
        if (bswap) {
            tcg_out_bswap16st(s, cond, TCG_REG_R0, datalo);
            tcg_out_st16_r(s, cond, TCG_REG_R0, addrlo, addend);
        } else {
            tcg_out_st16_r(s, cond, datalo, addrlo, addend);
        }
        break;
    case MO_32:
    default:
        if (bswap) {
            tcg_out_bswap32(s, cond, TCG_REG_R0, datalo);
            tcg_out_st32_r(s, cond, TCG_REG_R0, addrlo, addend);
        } else {
            tcg_out_st32_r(s, cond, datalo, addrlo, addend);
        }
        break;
    case MO_64:
        /* Avoid strd for user-only emulation, to handle unaligned.  */
        if (bswap) {
            tcg_out_bswap32(s, cond, TCG_REG_R0, datahi);
            tcg_out_st32_rwb(s, cond, TCG_REG_R0, addend, addrlo);
            tcg_out_bswap32(s, cond, TCG_REG_R0, datalo);
            tcg_out_st32_12(s, cond, TCG_REG_R0, addend, 4);
        } else if (USING_SOFTMMU && use_armv6_instructions
                   && (datalo & 1) == 0 && datahi == datalo + 1) {
            tcg_out_strd_r(s, cond, datalo, addrlo, addend);
        } else {
            tcg_out_st32_rwb(s, cond, datalo, addend, addrlo);
            tcg_out_st32_12(s, cond, datahi, addend, 4);
        }
        break;
    }
}

static inline void tcg_out_qemu_st_direct(TCGContext *s, TCGMemOp opc,
                                          TCGReg datalo, TCGReg datahi,
                                          TCGReg addrlo)
{
    TCGMemOp bswap = opc & MO_BSWAP;

    switch (opc & MO_SIZE) {
    case MO_8:
        tcg_out_st8_12(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_16:
        if (bswap) {
            tcg_out_bswap16st(s, COND_AL, TCG_REG_R0, datalo);
            tcg_out_st16_8(s, COND_AL, TCG_REG_R0, addrlo, 0);
        } else {
            tcg_out_st16_8(s, COND_AL, datalo, addrlo, 0);
        }
        break;
    case MO_32:
    default:
        if (bswap) {
            tcg_out_bswap32(s, COND_AL, TCG_REG_R0, datalo);
            tcg_out_st32_12(s, COND_AL, TCG_REG_R0, addrlo, 0);
        } else {
            tcg_out_st32_12(s, COND_AL, datalo, addrlo, 0);
        }
        break;
    case MO_64:
        /* Avoid strd for user-only emulation, to handle unaligned.  */
        if (bswap) {
            tcg_out_bswap32(s, COND_AL, TCG_REG_R0, datahi);
            tcg_out_st32_12(s, COND_AL, TCG_REG_R0, addrlo, 0);
            tcg_out_bswap32(s, COND_AL, TCG_REG_R0, datalo);
            tcg_out_st32_12(s, COND_AL, TCG_REG_R0, addrlo, 4);
        } else if (USING_SOFTMMU && use_armv6_instructions
                   && (datalo & 1) == 0 && datahi == datalo + 1) {
            tcg_out_strd_8(s, COND_AL, datalo, addrlo, 0);
        } else {
            tcg_out_st32_12(s, COND_AL, datalo, addrlo, 0);
            tcg_out_st32_12(s, COND_AL, datahi, addrlo, 4);
        }
        break;
    }
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg addrlo, datalo, datahi, addrhi __attribute__((unused));
    TCGMemOp opc;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    TCGReg addend;
    tcg_insn_unit *label_ptr;
#endif

    datalo = *args++;
    datahi = (is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS == 64 ? *args++ : 0);
    opc = *args++;

#ifdef CONFIG_SOFTMMU
    mem_index = *args;
    addend = tcg_out_tlb_read(s, addrlo, addrhi, opc & MO_SIZE, mem_index, 0);

    tcg_out_qemu_st_index(s, COND_EQ, opc, datalo, datahi, addrlo, addend);

    /* The conditional call must come last, as we're going to return here.  */
    label_ptr = s->code_ptr;
    tcg_out_bl_noaddr(s, COND_NE);

    add_qemu_ldst_label(s, false, opc, datalo, datahi, addrlo, addrhi,
                        mem_index, s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    if (GUEST_BASE) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP, GUEST_BASE);
        tcg_out_qemu_st_index(s, COND_AL, opc, datalo,
                              datahi, addrlo, TCG_REG_TMP);
    } else {
        tcg_out_qemu_st_direct(s, opc, datalo, datahi, addrlo);
    }
#endif
}

static tcg_insn_unit *tb_ret_addr;

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                const TCGArg *args, const int *const_args)
{
    TCGArg a0, a1, a2, a3, a4, a5;
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi32(s, COND_AL, TCG_REG_R0, args[0]);
        tcg_out_goto(s, COND_AL, tb_ret_addr);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* Direct jump method */
            s->tb_jmp_offset[args[0]] = tcg_current_code_size(s);
            tcg_out_b_noaddr(s, COND_AL);
        } else {
            /* Indirect jump method */
            intptr_t ptr = (intptr_t)(s->tb_next + args[0]);
            tcg_out_movi32(s, COND_AL, TCG_REG_R0, ptr & ~0xfff);
            tcg_out_ld32_12(s, COND_AL, TCG_REG_PC, TCG_REG_R0, ptr & 0xfff);
        }
        s->tb_next_offset[args[0]] = tcg_current_code_size(s);
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
        tcg_out_st8(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
        tcg_out_st16(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
        tcg_out_st32(s, COND_AL, args[0], args[1], args[2]);
        break;

    case INDEX_op_movcond_i32:
        /* Constraints mean that v2 is always in the same register as dest,
         * so we only need to do "if condition passed, move v1 to dest".
         */
        tcg_out_dat_rIN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0,
                        args[1], args[2], const_args[2]);
        tcg_out_dat_rIK(s, tcg_cond_to_arm_cond[args[5]], ARITH_MOV,
                        ARITH_MVN, args[0], 0, args[3], const_args[3]);
        break;
    case INDEX_op_add_i32:
        tcg_out_dat_rIN(s, COND_AL, ARITH_ADD, ARITH_SUB,
                        args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_sub_i32:
        if (const_args[1]) {
            if (const_args[2]) {
                tcg_out_movi32(s, COND_AL, args[0], args[1] - args[2]);
            } else {
                tcg_out_dat_rI(s, COND_AL, ARITH_RSB,
                               args[0], args[2], args[1], 1);
            }
        } else {
            tcg_out_dat_rIN(s, COND_AL, ARITH_SUB, ARITH_ADD,
                            args[0], args[1], args[2], const_args[2]);
        }
        break;
    case INDEX_op_and_i32:
        tcg_out_dat_rIK(s, COND_AL, ARITH_AND, ARITH_BIC,
                        args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_andc_i32:
        tcg_out_dat_rIK(s, COND_AL, ARITH_BIC, ARITH_AND,
                        args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_or_i32:
        c = ARITH_ORR;
        goto gen_arith;
    case INDEX_op_xor_i32:
        c = ARITH_EOR;
        /* Fall through.  */
    gen_arith:
        tcg_out_dat_rI(s, COND_AL, c, args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_add2_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        a3 = args[3], a4 = args[4], a5 = args[5];
        if (a0 == a3 || (a0 == a5 && !const_args[5])) {
            a0 = TCG_REG_TMP;
        }
        tcg_out_dat_rIN(s, COND_AL, ARITH_ADD | TO_CPSR, ARITH_SUB | TO_CPSR,
                        a0, a2, a4, const_args[4]);
        tcg_out_dat_rIK(s, COND_AL, ARITH_ADC, ARITH_SBC,
                        a1, a3, a5, const_args[5]);
        tcg_out_mov_reg(s, COND_AL, args[0], a0);
        break;
    case INDEX_op_sub2_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        a3 = args[3], a4 = args[4], a5 = args[5];
        if ((a0 == a3 && !const_args[3]) || (a0 == a5 && !const_args[5])) {
            a0 = TCG_REG_TMP;
        }
        if (const_args[2]) {
            if (const_args[4]) {
                tcg_out_movi32(s, COND_AL, a0, a4);
                a4 = a0;
            }
            tcg_out_dat_rI(s, COND_AL, ARITH_RSB | TO_CPSR, a0, a4, a2, 1);
        } else {
            tcg_out_dat_rIN(s, COND_AL, ARITH_SUB | TO_CPSR,
                            ARITH_ADD | TO_CPSR, a0, a2, a4, const_args[4]);
        }
        if (const_args[3]) {
            if (const_args[5]) {
                tcg_out_movi32(s, COND_AL, a1, a5);
                a5 = a1;
            }
            tcg_out_dat_rI(s, COND_AL, ARITH_RSC, a1, a5, a3, 1);
        } else {
            tcg_out_dat_rIK(s, COND_AL, ARITH_SBC, ARITH_ADC,
                            a1, a3, a5, const_args[5]);
        }
        tcg_out_mov_reg(s, COND_AL, args[0], a0);
        break;
    case INDEX_op_neg_i32:
        tcg_out_dat_imm(s, COND_AL, ARITH_RSB, args[0], args[1], 0);
        break;
    case INDEX_op_not_i32:
        tcg_out_dat_reg(s, COND_AL,
                        ARITH_MVN, args[0], 0, args[1], SHIFT_IMM_LSL(0));
        break;
    case INDEX_op_mul_i32:
        tcg_out_mul32(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_umull32(s, COND_AL, args[0], args[1], args[2], args[3]);
        break;
    case INDEX_op_muls2_i32:
        tcg_out_smull32(s, COND_AL, args[0], args[1], args[2], args[3]);
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
        goto gen_shift32;
    case INDEX_op_rotr_i32:
        c = const_args[2] ? (args[2] & 0x1f) ? SHIFT_IMM_ROR(args[2] & 0x1f) :
                SHIFT_IMM_LSL(0) : SHIFT_REG_ROR(args[2]);
        /* Fall through.  */
    gen_shift32:
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV, args[0], 0, args[1], c);
        break;

    case INDEX_op_rotl_i32:
        if (const_args[2]) {
            tcg_out_dat_reg(s, COND_AL, ARITH_MOV, args[0], 0, args[1],
                            ((0x20 - args[2]) & 0x1f) ?
                            SHIFT_IMM_ROR((0x20 - args[2]) & 0x1f) :
                            SHIFT_IMM_LSL(0));
        } else {
            tcg_out_dat_imm(s, COND_AL, ARITH_RSB, TCG_REG_TMP, args[2], 0x20);
            tcg_out_dat_reg(s, COND_AL, ARITH_MOV, args[0], 0, args[1],
                            SHIFT_REG_ROR(TCG_REG_TMP));
        }
        break;

    case INDEX_op_brcond_i32:
        tcg_out_dat_rIN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0,
                       args[0], args[1], const_args[1]);
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
        tcg_out_dat_rIN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0,
                        args[1], args[3], const_args[3]);
        tcg_out_dat_rIN(s, COND_EQ, ARITH_CMP, ARITH_CMN, 0,
                        args[0], args[2], const_args[2]);
        tcg_out_goto_label(s, tcg_cond_to_arm_cond[args[4]], args[5]);
        break;
    case INDEX_op_setcond_i32:
        tcg_out_dat_rIN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0,
                        args[1], args[2], const_args[2]);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[args[3]],
                        ARITH_MOV, args[0], 0, 1);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[tcg_invert_cond(args[3])],
                        ARITH_MOV, args[0], 0, 0);
        break;
    case INDEX_op_setcond2_i32:
        /* See brcond2_i32 comment */
        tcg_out_dat_rIN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0,
                        args[2], args[4], const_args[4]);
        tcg_out_dat_rIN(s, COND_EQ, ARITH_CMP, ARITH_CMN, 0,
                        args[1], args[3], const_args[3]);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[args[5]],
                        ARITH_MOV, args[0], 0, 1);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[tcg_invert_cond(args[5])],
                        ARITH_MOV, args[0], 0, 0);
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, args, 0);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, args, 1);
        break;
    case INDEX_op_qemu_st_i32:
        tcg_out_qemu_st(s, args, 0);
        break;
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, args, 1);
        break;

    case INDEX_op_bswap16_i32:
        tcg_out_bswap16(s, COND_AL, args[0], args[1]);
        break;
    case INDEX_op_bswap32_i32:
        tcg_out_bswap32(s, COND_AL, args[0], args[1]);
        break;

    case INDEX_op_ext8s_i32:
        tcg_out_ext8s(s, COND_AL, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tcg_out_ext16s(s, COND_AL, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i32:
        tcg_out_ext16u(s, COND_AL, args[0], args[1]);
        break;

    case INDEX_op_deposit_i32:
        tcg_out_deposit(s, COND_AL, args[0], args[2],
                        args[3], args[4], const_args[2]);
        break;

    case INDEX_op_div_i32:
        tcg_out_sdiv(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_divu_i32:
        tcg_out_udiv(s, COND_AL, args[0], args[1], args[2]);
        break;

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_movi_i32: /* Always emitted via tcg_out_movi.  */
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    default:
        tcg_abort();
    }
}

static const TCGTargetOpDef arm_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_br, { } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },

    /* TODO: "r", "r", "ri" */
    { INDEX_op_add_i32, { "r", "r", "rIN" } },
    { INDEX_op_sub_i32, { "r", "rI", "rIN" } },
    { INDEX_op_mul_i32, { "r", "r", "r" } },
    { INDEX_op_mulu2_i32, { "r", "r", "r", "r" } },
    { INDEX_op_muls2_i32, { "r", "r", "r", "r" } },
    { INDEX_op_and_i32, { "r", "r", "rIK" } },
    { INDEX_op_andc_i32, { "r", "r", "rIK" } },
    { INDEX_op_or_i32, { "r", "r", "rI" } },
    { INDEX_op_xor_i32, { "r", "r", "rI" } },
    { INDEX_op_neg_i32, { "r", "r" } },
    { INDEX_op_not_i32, { "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "ri" } },
    { INDEX_op_shr_i32, { "r", "r", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },
    { INDEX_op_rotl_i32, { "r", "r", "ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "ri" } },

    { INDEX_op_brcond_i32, { "r", "rIN" } },
    { INDEX_op_setcond_i32, { "r", "r", "rIN" } },
    { INDEX_op_movcond_i32, { "r", "r", "rIN", "rIK", "0" } },

    { INDEX_op_add2_i32, { "r", "r", "r", "r", "rIN", "rIK" } },
    { INDEX_op_sub2_i32, { "r", "r", "rI", "rI", "rIN", "rIK" } },
    { INDEX_op_brcond2_i32, { "r", "r", "rIN", "rIN" } },
    { INDEX_op_setcond2_i32, { "r", "r", "r", "rIN", "rIN" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld_i32, { "r", "l" } },
    { INDEX_op_qemu_ld_i64, { "r", "r", "l" } },
    { INDEX_op_qemu_st_i32, { "s", "s" } },
    { INDEX_op_qemu_st_i64, { "s", "s", "s" } },
#else
    { INDEX_op_qemu_ld_i32, { "r", "l", "l" } },
    { INDEX_op_qemu_ld_i64, { "r", "r", "l", "l" } },
    { INDEX_op_qemu_st_i32, { "s", "s", "s" } },
    { INDEX_op_qemu_st_i64, { "s", "s", "s", "s" } },
#endif

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext16u_i32, { "r", "r" } },

    { INDEX_op_deposit_i32, { "r", "0", "rZ" } },

    { INDEX_op_div_i32, { "r", "r", "r" } },
    { INDEX_op_divu_i32, { "r", "r", "r" } },

    { -1 },
};

static void tcg_target_init(TCGContext *s)
{
    /* Only probe for the platform and capabilities if we havn't already
       determined maximum values at compile time.  */
#ifndef use_idiv_instructions
    {
        unsigned long hwcap = qemu_getauxval(AT_HWCAP);
        use_idiv_instructions = (hwcap & HWCAP_ARM_IDIVA) != 0;
    }
#endif
    if (__ARM_ARCH < 7) {
        const char *pl = (const char *)qemu_getauxval(AT_PLATFORM);
        if (pl != NULL && pl[0] == 'v' && pl[1] >= '4' && pl[1] <= '9') {
            arm_arch = pl[1] - '0';
        }
    }

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffff);
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_R0) |
                     (1 << TCG_REG_R1) |
                     (1 << TCG_REG_R2) |
                     (1 << TCG_REG_R3) |
                     (1 << TCG_REG_R12) |
                     (1 << TCG_REG_R14));

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_PC);

    tcg_add_target_add_op_defs(arm_op_defs);
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_ld32u(s, COND_AL, arg, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    tcg_out_st32(s, COND_AL, arg, arg1, arg2);
}

static inline void tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg arg)
{
    tcg_out_dat_reg(s, COND_AL, ARITH_MOV, ret, 0, arg, SHIFT_IMM_LSL(0));
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                TCGReg ret, tcg_target_long arg)
{
    tcg_out_movi32(s, COND_AL, ret, arg);
}

/* Compute frame size via macros, to share between tcg_target_qemu_prologue
   and tcg_register_jit.  */

#define PUSH_SIZE  ((11 - 4 + 1 + 1) * sizeof(tcg_target_long))

#define FRAME_SIZE \
    ((PUSH_SIZE \
      + TCG_STATIC_CALL_ARGS_SIZE \
      + CPU_TEMP_BUF_NLONGS * sizeof(long) \
      + TCG_TARGET_STACK_ALIGN - 1) \
     & -TCG_TARGET_STACK_ALIGN)

static void tcg_target_qemu_prologue(TCGContext *s)
{
    int stack_addend;

    /* Calling convention requires us to save r4-r11 and lr.  */
    /* stmdb sp!, { r4 - r11, lr } */
    tcg_out32(s, (COND_AL << 28) | 0x092d4ff0);

    /* Reserve callee argument and tcg temp space.  */
    stack_addend = FRAME_SIZE - PUSH_SIZE;

    tcg_out_dat_rI(s, COND_AL, ARITH_SUB, TCG_REG_CALL_STACK,
                   TCG_REG_CALL_STACK, stack_addend, 1);
    tcg_set_frame(s, TCG_REG_CALL_STACK, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

    tcg_out_bx(s, COND_AL, tcg_target_call_iarg_regs[1]);
    tb_ret_addr = s->code_ptr;

    /* Epilogue.  We branch here via tb_ret_addr.  */
    tcg_out_dat_rI(s, COND_AL, ARITH_ADD, TCG_REG_CALL_STACK,
                   TCG_REG_CALL_STACK, stack_addend, 1);

    /* ldmia sp!, { r4 - r11, pc } */
    tcg_out32(s, (COND_AL << 28) | 0x08bd8ff0);
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[18];
} DebugFrame;

#define ELF_HOST_MACHINE EM_ARM

/* We're expecting a 2 byte uleb128 encoded value.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE >= (1 << 14));

static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = 0x7c,             /* sleb128 -4 */
    .h.cie.return_column = 14,

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, 13,                         /* DW_CFA_def_cfa sp, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        /* The following must match the stmdb in the prologue.  */
        0x8e, 1,                        /* DW_CFA_offset, lr, -4 */
        0x8b, 2,                        /* DW_CFA_offset, r11, -8 */
        0x8a, 3,                        /* DW_CFA_offset, r10, -12 */
        0x89, 4,                        /* DW_CFA_offset, r9, -16 */
        0x88, 5,                        /* DW_CFA_offset, r8, -20 */
        0x87, 6,                        /* DW_CFA_offset, r7, -24 */
        0x86, 7,                        /* DW_CFA_offset, r6, -28 */
        0x85, 8,                        /* DW_CFA_offset, r5, -32 */
        0x84, 9,                        /* DW_CFA_offset, r4, -36 */
    }
};

void tcg_register_jit(void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
