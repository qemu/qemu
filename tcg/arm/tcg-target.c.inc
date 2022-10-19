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
#include "../tcg-ldst.c.inc"
#include "../tcg-pool.c.inc"

int arm_arch = __ARM_ARCH;

#ifndef use_idiv_instructions
bool use_idiv_instructions;
#endif
#ifndef use_neon_instructions
bool use_neon_instructions;
#endif

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%r0",  "%r1",  "%r2",  "%r3",  "%r4",  "%r5",  "%r6",  "%r7",
    "%r8",  "%r9",  "%r10", "%r11", "%r12", "%sp",  "%r14", "%pc",
    "%q0",  "%q1",  "%q2",  "%q3",  "%q4",  "%q5",  "%q6",  "%q7",
    "%q8",  "%q9",  "%q10", "%q11", "%q12", "%q13", "%q14", "%q15",
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

    TCG_REG_Q0,
    TCG_REG_Q1,
    TCG_REG_Q2,
    TCG_REG_Q3,
    /* Q4 - Q7 are call-saved, and skipped. */
    TCG_REG_Q8,
    TCG_REG_Q9,
    TCG_REG_Q10,
    TCG_REG_Q11,
    TCG_REG_Q12,
    TCG_REG_Q13,
    TCG_REG_Q14,
    TCG_REG_Q15,
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_R0, TCG_REG_R1, TCG_REG_R2, TCG_REG_R3
};

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 3);
    return TCG_REG_R0 + slot;
}

#define TCG_REG_TMP  TCG_REG_R12
#define TCG_VEC_TMP  TCG_REG_Q15
#ifndef CONFIG_SOFTMMU
#define TCG_REG_GUEST_BASE  TCG_REG_R11
#endif

typedef enum {
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
} ARMCond;

#define TO_CPSR (1 << 20)

#define SHIFT_IMM_LSL(im)	(((im) << 7) | 0x00)
#define SHIFT_IMM_LSR(im)	(((im) << 7) | 0x20)
#define SHIFT_IMM_ASR(im)	(((im) << 7) | 0x40)
#define SHIFT_IMM_ROR(im)	(((im) << 7) | 0x60)
#define SHIFT_REG_LSL(rs)	(((rs) << 8) | 0x10)
#define SHIFT_REG_LSR(rs)	(((rs) << 8) | 0x30)
#define SHIFT_REG_ASR(rs)	(((rs) << 8) | 0x50)
#define SHIFT_REG_ROR(rs)	(((rs) << 8) | 0x70)

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

    INSN_B         = 0x0a000000,

    INSN_CLZ       = 0x016f0f10,
    INSN_RBIT      = 0x06ff0f30,

    INSN_LDMIA     = 0x08b00000,
    INSN_STMDB     = 0x09200000,

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

    INSN_DMB_ISH   = 0xf57ff05b,
    INSN_DMB_MCR   = 0xee070fba,

    /* Architected nop introduced in v6k.  */
    /* ??? This is an MSR (imm) 0,0,0 insn.  Anyone know if this
       also Just So Happened to do nothing on pre-v6k so that we
       don't need to conditionalize it?  */
    INSN_NOP_v6k   = 0xe320f000,
    /* Otherwise the assembler uses mov r0,r0 */
    INSN_NOP_v4    = (COND_AL << 28) | ARITH_MOV,

    INSN_VADD      = 0xf2000800,
    INSN_VAND      = 0xf2000110,
    INSN_VBIC      = 0xf2100110,
    INSN_VEOR      = 0xf3000110,
    INSN_VORN      = 0xf2300110,
    INSN_VORR      = 0xf2200110,
    INSN_VSUB      = 0xf3000800,
    INSN_VMUL      = 0xf2000910,
    INSN_VQADD     = 0xf2000010,
    INSN_VQADD_U   = 0xf3000010,
    INSN_VQSUB     = 0xf2000210,
    INSN_VQSUB_U   = 0xf3000210,
    INSN_VMAX      = 0xf2000600,
    INSN_VMAX_U    = 0xf3000600,
    INSN_VMIN      = 0xf2000610,
    INSN_VMIN_U    = 0xf3000610,

    INSN_VABS      = 0xf3b10300,
    INSN_VMVN      = 0xf3b00580,
    INSN_VNEG      = 0xf3b10380,

    INSN_VCEQ0     = 0xf3b10100,
    INSN_VCGT0     = 0xf3b10000,
    INSN_VCGE0     = 0xf3b10080,
    INSN_VCLE0     = 0xf3b10180,
    INSN_VCLT0     = 0xf3b10200,

    INSN_VCEQ      = 0xf3000810,
    INSN_VCGE      = 0xf2000310,
    INSN_VCGT      = 0xf2000300,
    INSN_VCGE_U    = 0xf3000310,
    INSN_VCGT_U    = 0xf3000300,

    INSN_VSHLI     = 0xf2800510,  /* VSHL (immediate) */
    INSN_VSARI     = 0xf2800010,  /* VSHR.S */
    INSN_VSHRI     = 0xf3800010,  /* VSHR.U */
    INSN_VSLI      = 0xf3800510,
    INSN_VSHL_S    = 0xf2000400,  /* VSHL.S (register) */
    INSN_VSHL_U    = 0xf3000400,  /* VSHL.U (register) */

    INSN_VBSL      = 0xf3100110,
    INSN_VBIT      = 0xf3200110,
    INSN_VBIF      = 0xf3300110,

    INSN_VTST      = 0xf2000810,

    INSN_VDUP_G    = 0xee800b10,  /* VDUP (ARM core register) */
    INSN_VDUP_S    = 0xf3b00c00,  /* VDUP (scalar) */
    INSN_VLDR_D    = 0xed100b00,  /* VLDR.64 */
    INSN_VLD1      = 0xf4200000,  /* VLD1 (multiple single elements) */
    INSN_VLD1R     = 0xf4a00c00,  /* VLD1 (single element to all lanes) */
    INSN_VST1      = 0xf4000000,  /* VST1 (multiple single elements) */
    INSN_VMOVI     = 0xf2800010,  /* VMOV (immediate) */
} ARMInsn;

#define INSN_NOP   (use_armv7_instructions ? INSN_NOP_v6k : INSN_NOP_v4)

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

static int encode_imm(uint32_t imm);

/* TCG private relocation type: add with pc+imm8 */
#define R_ARM_PC8  11

/* TCG private relocation type: vldr with imm8 << 2 */
#define R_ARM_PC11 12

static bool reloc_pc24(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = (tcg_ptr_byte_diff(target, src_rx) - 8) >> 2;

    if (offset == sextract32(offset, 0, 24)) {
        *src_rw = deposit32(*src_rw, 0, 24, offset);
        return true;
    }
    return false;
}

static bool reloc_pc13(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = tcg_ptr_byte_diff(target, src_rx) - 8;

    if (offset >= -0xfff && offset <= 0xfff) {
        tcg_insn_unit insn = *src_rw;
        bool u = (offset >= 0);
        if (!u) {
            offset = -offset;
        }
        insn = deposit32(insn, 23, 1, u);
        insn = deposit32(insn, 0, 12, offset);
        *src_rw = insn;
        return true;
    }
    return false;
}

static bool reloc_pc11(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = (tcg_ptr_byte_diff(target, src_rx) - 8) / 4;

    if (offset >= -0xff && offset <= 0xff) {
        tcg_insn_unit insn = *src_rw;
        bool u = (offset >= 0);
        if (!u) {
            offset = -offset;
        }
        insn = deposit32(insn, 23, 1, u);
        insn = deposit32(insn, 0, 8, offset);
        *src_rw = insn;
        return true;
    }
    return false;
}

static bool reloc_pc8(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t offset = tcg_ptr_byte_diff(target, src_rx) - 8;
    int imm12 = encode_imm(offset);

    if (imm12 >= 0) {
        *src_rw = deposit32(*src_rw, 0, 12, imm12);
        return true;
    }
    return false;
}

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);
    switch (type) {
    case R_ARM_PC24:
        return reloc_pc24(code_ptr, (const tcg_insn_unit *)value);
    case R_ARM_PC13:
        return reloc_pc13(code_ptr, (const tcg_insn_unit *)value);
    case R_ARM_PC11:
        return reloc_pc11(code_ptr, (const tcg_insn_unit *)value);
    case R_ARM_PC8:
        return reloc_pc8(code_ptr, (const tcg_insn_unit *)value);
    default:
        g_assert_not_reached();
    }
}

#define TCG_CT_CONST_ARM  0x100
#define TCG_CT_CONST_INV  0x200
#define TCG_CT_CONST_NEG  0x400
#define TCG_CT_CONST_ZERO 0x800
#define TCG_CT_CONST_ORRI 0x1000
#define TCG_CT_CONST_ANDI 0x2000

#define ALL_GENERAL_REGS  0xffffu
#define ALL_VECTOR_REGS   0xffff0000u

/*
 * r0-r2 will be overwritten when reading the tlb entry (softmmu only)
 * and r0-r1 doing the byte swapping, so don't use these.
 * r3 is removed for softmmu to avoid clashes with helper arguments.
 */
#ifdef CONFIG_SOFTMMU
#define ALL_QLOAD_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R0) | (1 << TCG_REG_R1) | \
                          (1 << TCG_REG_R2) | (1 << TCG_REG_R3) | \
                          (1 << TCG_REG_R14)))
#define ALL_QSTORE_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R0) | (1 << TCG_REG_R1) | \
                          (1 << TCG_REG_R2) | (1 << TCG_REG_R14) | \
                          ((TARGET_LONG_BITS == 64) << TCG_REG_R3)))
#else
#define ALL_QLOAD_REGS   ALL_GENERAL_REGS
#define ALL_QSTORE_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R0) | (1 << TCG_REG_R1)))
#endif

/*
 * ARM immediates for ALU instructions are made of an unsigned 8-bit
 * right-rotated by an even amount between 0 and 30.
 *
 * Return < 0 if @imm cannot be encoded, else the entire imm12 field.
 */
static int encode_imm(uint32_t imm)
{
    uint32_t rot, imm8;

    /* Simple case, no rotation required. */
    if ((imm & ~0xff) == 0) {
        return imm;
    }

    /* Next, try a simple even shift.  */
    rot = ctz32(imm) & ~1;
    imm8 = imm >> rot;
    rot = 32 - rot;
    if ((imm8 & ~0xff) == 0) {
        goto found;
    }

    /*
     * Finally, try harder with rotations.
     * The ctz test above will have taken care of rotates >= 8.
     */
    for (rot = 2; rot < 8; rot += 2) {
        imm8 = rol32(imm, rot);
        if ((imm8 & ~0xff) == 0) {
            goto found;
        }
    }
    /* Fail: imm cannot be encoded. */
    return -1;

 found:
    /* Note that rot is even, and we discard bit 0 by shifting by 7. */
    return rot << 7 | imm8;
}

static int encode_imm_nofail(uint32_t imm)
{
    int ret = encode_imm(imm);
    tcg_debug_assert(ret >= 0);
    return ret;
}

static bool check_fit_imm(uint32_t imm)
{
    return encode_imm(imm) >= 0;
}

/* Return true if v16 is a valid 16-bit shifted immediate.  */
static bool is_shimm16(uint16_t v16, int *cmode, int *imm8)
{
    if (v16 == (v16 & 0xff)) {
        *cmode = 0x8;
        *imm8 = v16 & 0xff;
        return true;
    } else if (v16 == (v16 & 0xff00)) {
        *cmode = 0xa;
        *imm8 = v16 >> 8;
        return true;
    }
    return false;
}

/* Return true if v32 is a valid 32-bit shifted immediate.  */
static bool is_shimm32(uint32_t v32, int *cmode, int *imm8)
{
    if (v32 == (v32 & 0xff)) {
        *cmode = 0x0;
        *imm8 = v32 & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff00)) {
        *cmode = 0x2;
        *imm8 = (v32 >> 8) & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff0000)) {
        *cmode = 0x4;
        *imm8 = (v32 >> 16) & 0xff;
        return true;
    } else if (v32 == (v32 & 0xff000000)) {
        *cmode = 0x6;
        *imm8 = v32 >> 24;
        return true;
    }
    return false;
}

/* Return true if v32 is a valid 32-bit shifting ones immediate.  */
static bool is_soimm32(uint32_t v32, int *cmode, int *imm8)
{
    if ((v32 & 0xffff00ff) == 0xff) {
        *cmode = 0xc;
        *imm8 = (v32 >> 8) & 0xff;
        return true;
    } else if ((v32 & 0xff00ffff) == 0xffff) {
        *cmode = 0xd;
        *imm8 = (v32 >> 16) & 0xff;
        return true;
    }
    return false;
}

/*
 * Return non-zero if v32 can be formed by MOVI+ORR.
 * Place the parameters for MOVI in (cmode, imm8).
 * Return the cmode for ORR; the imm8 can be had via extraction from v32.
 */
static int is_shimm32_pair(uint32_t v32, int *cmode, int *imm8)
{
    int i;

    for (i = 6; i > 0; i -= 2) {
        /* Mask out one byte we can add with ORR.  */
        uint32_t tmp = v32 & ~(0xffu << (i * 4));
        if (is_shimm32(tmp, cmode, imm8) ||
            is_soimm32(tmp, cmode, imm8)) {
            break;
        }
    }
    return i;
}

/* Return true if V is a valid 16-bit or 32-bit shifted immediate.  */
static bool is_shimm1632(uint32_t v32, int *cmode, int *imm8)
{
    if (v32 == deposit32(v32, 16, 16, v32)) {
        return is_shimm16(v32, cmode, imm8);
    } else {
        return is_shimm32(v32, cmode, imm8);
    }
}

/* Test if a constant matches the constraint.
 * TODO: define constraints for:
 *
 * ldr/str offset:   between -0xfff and 0xfff
 * ldrh/strh offset: between -0xff and 0xff
 * mov operand2:     values represented with x << (2 * y), x < 0x100
 * add, sub, eor...: ditto
 */
static bool tcg_target_const_match(int64_t val, TCGType type, int ct)
{
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
    }

    switch (ct & (TCG_CT_CONST_ORRI | TCG_CT_CONST_ANDI)) {
    case 0:
        break;
    case TCG_CT_CONST_ANDI:
        val = ~val;
        /* fallthru */
    case TCG_CT_CONST_ORRI:
        if (val == deposit64(val, 32, 32, val)) {
            int cmode, imm8;
            return is_shimm1632(val, &cmode, &imm8);
        }
        break;
    default:
        /* Both bits should not be set for the same insn.  */
        g_assert_not_reached();
    }

    return 0;
}

static void tcg_out_b_imm(TCGContext *s, ARMCond cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | INSN_B |
                    (((offset - 8) >> 2) & 0x00ffffff));
}

static void tcg_out_bl_imm(TCGContext *s, ARMCond cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | 0x0b000000 |
                    (((offset - 8) >> 2) & 0x00ffffff));
}

static void tcg_out_blx_reg(TCGContext *s, ARMCond cond, TCGReg rn)
{
    tcg_out32(s, (cond << 28) | 0x012fff30 | rn);
}

static void tcg_out_blx_imm(TCGContext *s, int32_t offset)
{
    tcg_out32(s, 0xfa000000 | ((offset & 2) << 23) |
                (((offset - 8) >> 2) & 0x00ffffff));
}

static void tcg_out_dat_reg(TCGContext *s, ARMCond cond, ARMInsn opc,
                            TCGReg rd, TCGReg rn, TCGReg rm, int shift)
{
    tcg_out32(s, (cond << 28) | (0 << 25) | opc |
                    (rn << 16) | (rd << 12) | shift | rm);
}

static void tcg_out_mov_reg(TCGContext *s, ARMCond cond, TCGReg rd, TCGReg rm)
{
    /* Simple reg-reg move, optimising out the 'do nothing' case */
    if (rd != rm) {
        tcg_out_dat_reg(s, cond, ARITH_MOV, rd, 0, rm, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_bx_reg(TCGContext *s, ARMCond cond, TCGReg rn)
{
    tcg_out32(s, (cond << 28) | 0x012fff10 | rn);
}

static void tcg_out_b_reg(TCGContext *s, ARMCond cond, TCGReg rn)
{
    /*
     * Unless the C portion of QEMU is compiled as thumb, we don't need
     * true BX semantics; merely a branch to an address held in a register.
     */
    tcg_out_bx_reg(s, cond, rn);
}

static void tcg_out_dat_imm(TCGContext *s, ARMCond cond, ARMInsn opc,
                            TCGReg rd, TCGReg rn, int im)
{
    tcg_out32(s, (cond << 28) | (1 << 25) | opc |
                    (rn << 16) | (rd << 12) | im);
}

static void tcg_out_ldstm(TCGContext *s, ARMCond cond, ARMInsn opc,
                          TCGReg rn, uint16_t mask)
{
    tcg_out32(s, (cond << 28) | opc | (rn << 16) | mask);
}

/* Note that this routine is used for both LDR and LDRH formats, so we do
   not wish to include an immediate shift at this point.  */
static void tcg_out_memop_r(TCGContext *s, ARMCond cond, ARMInsn opc, TCGReg rt,
                            TCGReg rn, TCGReg rm, bool u, bool p, bool w)
{
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24)
              | (w << 21) | (rn << 16) | (rt << 12) | rm);
}

static void tcg_out_memop_8(TCGContext *s, ARMCond cond, ARMInsn opc, TCGReg rt,
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

static void tcg_out_memop_12(TCGContext *s, ARMCond cond, ARMInsn opc,
                             TCGReg rt, TCGReg rn, int imm12, bool p, bool w)
{
    bool u = 1;
    if (imm12 < 0) {
        imm12 = -imm12;
        u = 0;
    }
    tcg_out32(s, (cond << 28) | opc | (u << 23) | (p << 24) | (w << 21) |
              (rn << 16) | (rt << 12) | imm12);
}

static void tcg_out_ld32_12(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_LDR_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_st32_12(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_STR_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_ld32_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDR_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_st32_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STR_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ldrd_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRD_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ldrd_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRD_REG, rt, rn, rm, 1, 1, 0);
}

static void __attribute__((unused))
tcg_out_ldrd_rwb(TCGContext *s, ARMCond cond, TCGReg rt, TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRD_REG, rt, rn, rm, 1, 1, 1);
}

static void tcg_out_strd_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_STRD_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_strd_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRD_REG, rt, rn, rm, 1, 1, 0);
}

/* Register pre-increment with base writeback.  */
static void tcg_out_ld32_rwb(TCGContext *s, ARMCond cond, TCGReg rt,
                             TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDR_REG, rt, rn, rm, 1, 1, 1);
}

static void tcg_out_st32_rwb(TCGContext *s, ARMCond cond, TCGReg rt,
                             TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STR_REG, rt, rn, rm, 1, 1, 1);
}

static void tcg_out_ld16u_8(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRH_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_st16_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_STRH_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ld16u_r(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRH_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_st16_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRH_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ld16s_8(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRSH_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ld16s_r(TCGContext *s, ARMCond cond, TCGReg rt,
                            TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRSH_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ld8_12(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_LDRB_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_st8_12(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm12)
{
    tcg_out_memop_12(s, cond, INSN_STRB_IMM, rt, rn, imm12, 1, 0);
}

static void tcg_out_ld8_r(TCGContext *s, ARMCond cond, TCGReg rt,
                          TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRB_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_st8_r(TCGContext *s, ARMCond cond, TCGReg rt,
                          TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_STRB_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_ld8s_8(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, int imm8)
{
    tcg_out_memop_8(s, cond, INSN_LDRSB_IMM, rt, rn, imm8, 1, 0);
}

static void tcg_out_ld8s_r(TCGContext *s, ARMCond cond, TCGReg rt,
                           TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRSB_REG, rt, rn, rm, 1, 1, 0);
}

static void tcg_out_movi_pool(TCGContext *s, ARMCond cond,
                              TCGReg rd, uint32_t arg)
{
    new_pool_label(s, arg, R_ARM_PC13, s->code_ptr, 0);
    tcg_out_ld32_12(s, cond, rd, TCG_REG_PC, 0);
}

static void tcg_out_movi32(TCGContext *s, ARMCond cond,
                           TCGReg rd, uint32_t arg)
{
    int imm12, diff, opc, sh1, sh2;
    uint32_t tt0, tt1, tt2;

    /* Check a single MOV/MVN before anything else.  */
    imm12 = encode_imm(arg);
    if (imm12 >= 0) {
        tcg_out_dat_imm(s, cond, ARITH_MOV, rd, 0, imm12);
        return;
    }
    imm12 = encode_imm(~arg);
    if (imm12 >= 0) {
        tcg_out_dat_imm(s, cond, ARITH_MVN, rd, 0, imm12);
        return;
    }

    /* Check for a pc-relative address.  This will usually be the TB,
       or within the TB, which is immediately before the code block.  */
    diff = tcg_pcrel_diff(s, (void *)arg) - 8;
    if (diff >= 0) {
        imm12 = encode_imm(diff);
        if (imm12 >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_ADD, rd, TCG_REG_PC, imm12);
            return;
        }
    } else {
        imm12 = encode_imm(-diff);
        if (imm12 >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_SUB, rd, TCG_REG_PC, imm12);
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

    /* Look for sequences of two insns.  If we have lots of 1's, we can
       shorten the sequence by beginning with mvn and then clearing
       higher bits with eor.  */
    tt0 = arg;
    opc = ARITH_MOV;
    if (ctpop32(arg) > 16) {
        tt0 = ~arg;
        opc = ARITH_MVN;
    }
    sh1 = ctz32(tt0) & ~1;
    tt1 = tt0 & ~(0xff << sh1);
    sh2 = ctz32(tt1) & ~1;
    tt2 = tt1 & ~(0xff << sh2);
    if (tt2 == 0) {
        int rot;

        rot = ((32 - sh1) << 7) & 0xf00;
        tcg_out_dat_imm(s, cond, opc, rd,  0, ((tt0 >> sh1) & 0xff) | rot);
        rot = ((32 - sh2) << 7) & 0xf00;
        tcg_out_dat_imm(s, cond, ARITH_EOR, rd, rd,
                        ((tt0 >> sh2) & 0xff) | rot);
        return;
    }

    /* Otherwise, drop it into the constant pool.  */
    tcg_out_movi_pool(s, cond, rd, arg);
}

/*
 * Emit either the reg,imm or reg,reg form of a data-processing insn.
 * rhs must satisfy the "rI" constraint.
 */
static void tcg_out_dat_rI(TCGContext *s, ARMCond cond, ARMInsn opc,
                           TCGReg dst, TCGReg lhs, TCGArg rhs, int rhs_is_const)
{
    if (rhs_is_const) {
        tcg_out_dat_imm(s, cond, opc, dst, lhs, encode_imm_nofail(rhs));
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

/*
 * Emit either the reg,imm or reg,reg form of a data-processing insn.
 * rhs must satisfy the "rIK" constraint.
 */
static void tcg_out_dat_rIK(TCGContext *s, ARMCond cond, ARMInsn opc,
                            ARMInsn opinv, TCGReg dst, TCGReg lhs, TCGArg rhs,
                            bool rhs_is_const)
{
    if (rhs_is_const) {
        int imm12 = encode_imm(rhs);
        if (imm12 < 0) {
            imm12 = encode_imm_nofail(~rhs);
            opc = opinv;
        }
        tcg_out_dat_imm(s, cond, opc, dst, lhs, imm12);
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_dat_rIN(TCGContext *s, ARMCond cond, ARMInsn opc,
                            ARMInsn opneg, TCGReg dst, TCGReg lhs, TCGArg rhs,
                            bool rhs_is_const)
{
    /* Emit either the reg,imm or reg,reg form of a data-processing insn.
     * rhs must satisfy the "rIN" constraint.
     */
    if (rhs_is_const) {
        int imm12 = encode_imm(rhs);
        if (imm12 < 0) {
            imm12 = encode_imm_nofail(-rhs);
            opc = opneg;
        }
        tcg_out_dat_imm(s, cond, opc, dst, lhs, imm12);
    } else {
        tcg_out_dat_reg(s, cond, opc, dst, lhs, rhs, SHIFT_IMM_LSL(0));
    }
}

static void tcg_out_mul32(TCGContext *s, ARMCond cond, TCGReg rd,
                          TCGReg rn, TCGReg rm)
{
    /* mul */
    tcg_out32(s, (cond << 28) | 0x90 | (rd << 16) | (rm << 8) | rn);
}

static void tcg_out_umull32(TCGContext *s, ARMCond cond, TCGReg rd0,
                            TCGReg rd1, TCGReg rn, TCGReg rm)
{
    /* umull */
    tcg_out32(s, (cond << 28) | 0x00800090 |
              (rd1 << 16) | (rd0 << 12) | (rm << 8) | rn);
}

static void tcg_out_smull32(TCGContext *s, ARMCond cond, TCGReg rd0,
                            TCGReg rd1, TCGReg rn, TCGReg rm)
{
    /* smull */
    tcg_out32(s, (cond << 28) | 0x00c00090 |
              (rd1 << 16) | (rd0 << 12) | (rm << 8) | rn);
}

static void tcg_out_sdiv(TCGContext *s, ARMCond cond,
                         TCGReg rd, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, 0x0710f010 | (cond << 28) | (rd << 16) | rn | (rm << 8));
}

static void tcg_out_udiv(TCGContext *s, ARMCond cond,
                         TCGReg rd, TCGReg rn, TCGReg rm)
{
    tcg_out32(s, 0x0730f010 | (cond << 28) | (rd << 16) | rn | (rm << 8));
}

static void tcg_out_ext8s(TCGContext *s, ARMCond cond, TCGReg rd, TCGReg rn)
{
    /* sxtb */
    tcg_out32(s, 0x06af0070 | (cond << 28) | (rd << 12) | rn);
}

static void __attribute__((unused))
tcg_out_ext8u(TCGContext *s, ARMCond cond, TCGReg rd, TCGReg rn)
{
    tcg_out_dat_imm(s, cond, ARITH_AND, rd, rn, 0xff);
}

static void tcg_out_ext16s(TCGContext *s, ARMCond cond, TCGReg rd, TCGReg rn)
{
    /* sxth */
    tcg_out32(s, 0x06bf0070 | (cond << 28) | (rd << 12) | rn);
}

static void tcg_out_ext16u(TCGContext *s, ARMCond cond, TCGReg rd, TCGReg rn)
{
    /* uxth */
    tcg_out32(s, 0x06ff0070 | (cond << 28) | (rd << 12) | rn);
}

static void tcg_out_bswap16(TCGContext *s, ARMCond cond,
                            TCGReg rd, TCGReg rn, int flags)
{
    if (flags & TCG_BSWAP_OS) {
        /* revsh */
        tcg_out32(s, 0x06ff0fb0 | (cond << 28) | (rd << 12) | rn);
        return;
    }

    /* rev16 */
    tcg_out32(s, 0x06bf0fb0 | (cond << 28) | (rd << 12) | rn);
    if ((flags & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
        /* uxth */
        tcg_out32(s, 0x06ff0070 | (cond << 28) | (rd << 12) | rd);
    }
}

static void tcg_out_bswap32(TCGContext *s, ARMCond cond, TCGReg rd, TCGReg rn)
{
    /* rev */
    tcg_out32(s, 0x06bf0f30 | (cond << 28) | (rd << 12) | rn);
}

static void tcg_out_deposit(TCGContext *s, ARMCond cond, TCGReg rd,
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

static void tcg_out_extract(TCGContext *s, ARMCond cond, TCGReg rd,
                            TCGReg rn, int ofs, int len)
{
    /* ubfx */
    tcg_out32(s, 0x07e00050 | (cond << 28) | (rd << 12) | rn
              | (ofs << 7) | ((len - 1) << 16));
}

static void tcg_out_sextract(TCGContext *s, ARMCond cond, TCGReg rd,
                             TCGReg rn, int ofs, int len)
{
    /* sbfx */
    tcg_out32(s, 0x07a00050 | (cond << 28) | (rd << 12) | rn
              | (ofs << 7) | ((len - 1) << 16));
}

static void tcg_out_ld32u(TCGContext *s, ARMCond cond,
                          TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld32_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld32_12(s, cond, rd, rn, offset);
}

static void tcg_out_st32(TCGContext *s, ARMCond cond,
                         TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_st32_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_st32_12(s, cond, rd, rn, offset);
}

static void tcg_out_ld16u(TCGContext *s, ARMCond cond,
                          TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld16u_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld16u_8(s, cond, rd, rn, offset);
}

static void tcg_out_ld16s(TCGContext *s, ARMCond cond,
                          TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld16s_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld16s_8(s, cond, rd, rn, offset);
}

static void tcg_out_st16(TCGContext *s, ARMCond cond,
                         TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_st16_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_st16_8(s, cond, rd, rn, offset);
}

static void tcg_out_ld8u(TCGContext *s, ARMCond cond,
                         TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld8_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld8_12(s, cond, rd, rn, offset);
}

static void tcg_out_ld8s(TCGContext *s, ARMCond cond,
                         TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xff || offset < -0xff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_ld8s_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_ld8s_8(s, cond, rd, rn, offset);
}

static void tcg_out_st8(TCGContext *s, ARMCond cond,
                        TCGReg rd, TCGReg rn, int32_t offset)
{
    if (offset > 0xfff || offset < -0xfff) {
        tcg_out_movi32(s, cond, TCG_REG_TMP, offset);
        tcg_out_st8_r(s, cond, rd, rn, TCG_REG_TMP);
    } else
        tcg_out_st8_12(s, cond, rd, rn, offset);
}

/*
 * The _goto case is normally between TBs within the same code buffer, and
 * with the code buffer limited to 16MB we wouldn't need the long case.
 * But we also use it for the tail-call to the qemu_ld/st helpers, which does.
 */
static void tcg_out_goto(TCGContext *s, ARMCond cond, const tcg_insn_unit *addr)
{
    intptr_t addri = (intptr_t)addr;
    ptrdiff_t disp = tcg_pcrel_diff(s, addr);
    bool arm_mode = !(addri & 1);

    if (arm_mode && disp - 8 < 0x01fffffd && disp - 8 > -0x01fffffd) {
        tcg_out_b_imm(s, cond, disp);
        return;
    }

    /* LDR is interworking from v5t. */
    tcg_out_movi_pool(s, cond, TCG_REG_PC, addri);
}

/*
 * The call case is mostly used for helpers - so it's not unreasonable
 * for them to be beyond branch range.
 */
static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *addr)
{
    intptr_t addri = (intptr_t)addr;
    ptrdiff_t disp = tcg_pcrel_diff(s, addr);
    bool arm_mode = !(addri & 1);

    if (disp - 8 < 0x02000000 && disp - 8 >= -0x02000000) {
        if (arm_mode) {
            tcg_out_bl_imm(s, COND_AL, disp);
        } else {
            tcg_out_blx_imm(s, disp);
        }
        return;
    }

    tcg_out_movi32(s, COND_AL, TCG_REG_TMP, addri);
    tcg_out_blx_reg(s, COND_AL, TCG_REG_TMP);
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *addr,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, addr);
}

static void tcg_out_goto_label(TCGContext *s, ARMCond cond, TCGLabel *l)
{
    if (l->has_value) {
        tcg_out_goto(s, cond, l->u.value_ptr);
    } else {
        tcg_out_reloc(s, s->code_ptr, R_ARM_PC24, l, 0);
        tcg_out_b_imm(s, cond, 0);
    }
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    if (use_armv7_instructions) {
        tcg_out32(s, INSN_DMB_ISH);
    } else {
        tcg_out32(s, INSN_DMB_MCR);
    }
}

static TCGCond tcg_out_cmp2(TCGContext *s, const TCGArg *args,
                            const int *const_args)
{
    TCGReg al = args[0];
    TCGReg ah = args[1];
    TCGArg bl = args[2];
    TCGArg bh = args[3];
    TCGCond cond = args[4];
    int const_bl = const_args[2];
    int const_bh = const_args[3];

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
    case TCG_COND_LTU:
    case TCG_COND_LEU:
    case TCG_COND_GTU:
    case TCG_COND_GEU:
        /* We perform a conditional comparision.  If the high half is
           equal, then overwrite the flags with the comparison of the
           low half.  The resulting flags cover the whole.  */
        tcg_out_dat_rI(s, COND_AL, ARITH_CMP, 0, ah, bh, const_bh);
        tcg_out_dat_rI(s, COND_EQ, ARITH_CMP, 0, al, bl, const_bl);
        return cond;

    case TCG_COND_LT:
    case TCG_COND_GE:
        /* We perform a double-word subtraction and examine the result.
           We do not actually need the result of the subtract, so the
           low part "subtract" is a compare.  For the high half we have
           no choice but to compute into a temporary.  */
        tcg_out_dat_rI(s, COND_AL, ARITH_CMP, 0, al, bl, const_bl);
        tcg_out_dat_rI(s, COND_AL, ARITH_SBC | TO_CPSR,
                       TCG_REG_TMP, ah, bh, const_bh);
        return cond;

    case TCG_COND_LE:
    case TCG_COND_GT:
        /* Similar, but with swapped arguments, via reversed subtract.  */
        tcg_out_dat_rI(s, COND_AL, ARITH_RSB | TO_CPSR,
                       TCG_REG_TMP, al, bl, const_bl);
        tcg_out_dat_rI(s, COND_AL, ARITH_RSC | TO_CPSR,
                       TCG_REG_TMP, ah, bh, const_bh);
        return tcg_swap_cond(cond);

    default:
        g_assert_not_reached();
    }
}

/*
 * Note that TCGReg references Q-registers.
 * Q-regno = 2 * D-regno, so shift left by 1 whlie inserting.
 */
static uint32_t encode_vd(TCGReg rd)
{
    tcg_debug_assert(rd >= TCG_REG_Q0);
    return (extract32(rd, 3, 1) << 22) | (extract32(rd, 0, 3) << 13);
}

static uint32_t encode_vn(TCGReg rn)
{
    tcg_debug_assert(rn >= TCG_REG_Q0);
    return (extract32(rn, 3, 1) << 7) | (extract32(rn, 0, 3) << 17);
}

static uint32_t encode_vm(TCGReg rm)
{
    tcg_debug_assert(rm >= TCG_REG_Q0);
    return (extract32(rm, 3, 1) << 5) | (extract32(rm, 0, 3) << 1);
}

static void tcg_out_vreg2(TCGContext *s, ARMInsn insn, int q, int vece,
                          TCGReg d, TCGReg m)
{
    tcg_out32(s, insn | (vece << 18) | (q << 6) |
              encode_vd(d) | encode_vm(m));
}

static void tcg_out_vreg3(TCGContext *s, ARMInsn insn, int q, int vece,
                          TCGReg d, TCGReg n, TCGReg m)
{
    tcg_out32(s, insn | (vece << 20) | (q << 6) |
              encode_vd(d) | encode_vn(n) | encode_vm(m));
}

static void tcg_out_vmovi(TCGContext *s, TCGReg rd,
                          int q, int op, int cmode, uint8_t imm8)
{
    tcg_out32(s, INSN_VMOVI | encode_vd(rd) | (q << 6) | (op << 5)
              | (cmode << 8) | extract32(imm8, 0, 4)
              | (extract32(imm8, 4, 3) << 16)
              | (extract32(imm8, 7, 1) << 24));
}

static void tcg_out_vshifti(TCGContext *s, ARMInsn insn, int q,
                            TCGReg rd, TCGReg rm, int l_imm6)
{
    tcg_out32(s, insn | (q << 6) | encode_vd(rd) | encode_vm(rm) |
              (extract32(l_imm6, 6, 1) << 7) |
              (extract32(l_imm6, 0, 6) << 16));
}

static void tcg_out_vldst(TCGContext *s, ARMInsn insn,
                          TCGReg rd, TCGReg rn, int offset)
{
    if (offset != 0) {
        if (check_fit_imm(offset) || check_fit_imm(-offset)) {
            tcg_out_dat_rIN(s, COND_AL, ARITH_ADD, ARITH_SUB,
                            TCG_REG_TMP, rn, offset, true);
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP, offset);
            tcg_out_dat_reg(s, COND_AL, ARITH_ADD,
                            TCG_REG_TMP, TCG_REG_TMP, rn, 0);
        }
        rn = TCG_REG_TMP;
    }
    tcg_out32(s, insn | (rn << 16) | encode_vd(rd) | 0xf);
}

#ifdef CONFIG_SOFTMMU
/* helper signature: helper_ret_ld_mmu(CPUState *env, target_ulong addr,
 *                                     int mmu_idx, uintptr_t ra)
 */
static void * const qemu_ld_helpers[MO_SSIZE + 1] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_SB]   = helper_ret_ldsb_mmu,
#if HOST_BIG_ENDIAN
    [MO_UW] = helper_be_lduw_mmu,
    [MO_UL] = helper_be_ldul_mmu,
    [MO_UQ] = helper_be_ldq_mmu,
    [MO_SW] = helper_be_ldsw_mmu,
    [MO_SL] = helper_be_ldul_mmu,
#else
    [MO_UW] = helper_le_lduw_mmu,
    [MO_UL] = helper_le_ldul_mmu,
    [MO_UQ] = helper_le_ldq_mmu,
    [MO_SW] = helper_le_ldsw_mmu,
    [MO_SL] = helper_le_ldul_mmu,
#endif
};

/* helper signature: helper_ret_st_mmu(CPUState *env, target_ulong addr,
 *                                     uintxx_t val, int mmu_idx, uintptr_t ra)
 */
static void * const qemu_st_helpers[MO_SIZE + 1] = {
    [MO_8]   = helper_ret_stb_mmu,
#if HOST_BIG_ENDIAN
    [MO_16] = helper_be_stw_mmu,
    [MO_32] = helper_be_stl_mmu,
    [MO_64] = helper_be_stq_mmu,
#else
    [MO_16] = helper_le_stw_mmu,
    [MO_32] = helper_le_stl_mmu,
    [MO_64] = helper_le_stq_mmu,
#endif
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
        tcg_debug_assert(ofs + 4 <= TCG_STATIC_CALL_ARGS_SIZE);            \
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
    if (argreg >= 4 && (arglo & 1) == 0 && arghi == arglo + 1) {
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

/* We expect to use an 9-bit sign-magnitude negative offset from ENV.  */
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -256);

/* These offsets are built into the LDRD below.  */
QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, mask) != 0);
QEMU_BUILD_BUG_ON(offsetof(CPUTLBDescFast, table) != 4);

/* Load and compare a TLB entry, leaving the flags set.  Returns the register
   containing the addend of the tlb entry.  Clobbers R0, R1, R2, TMP.  */

static TCGReg tcg_out_tlb_read(TCGContext *s, TCGReg addrlo, TCGReg addrhi,
                               MemOp opc, int mem_index, bool is_load)
{
    int cmp_off = (is_load ? offsetof(CPUTLBEntry, addr_read)
                   : offsetof(CPUTLBEntry, addr_write));
    int fast_off = TLB_MASK_TABLE_OFS(mem_index);
    unsigned s_mask = (1 << (opc & MO_SIZE)) - 1;
    unsigned a_mask = (1 << get_alignment_bits(opc)) - 1;
    TCGReg t_addr;

    /* Load env_tlb(env)->f[mmu_idx].{mask,table} into {r0,r1}.  */
    tcg_out_ldrd_8(s, COND_AL, TCG_REG_R0, TCG_AREG0, fast_off);

    /* Extract the tlb index from the address into R0.  */
    tcg_out_dat_reg(s, COND_AL, ARITH_AND, TCG_REG_R0, TCG_REG_R0, addrlo,
                    SHIFT_IMM_LSR(TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS));

    /*
     * Add the tlb_table pointer, creating the CPUTLBEntry address in R1.
     * Load the tlb comparator into R2/R3 and the fast path addend into R1.
     */
    if (cmp_off == 0) {
        if (TARGET_LONG_BITS == 64) {
            tcg_out_ldrd_rwb(s, COND_AL, TCG_REG_R2, TCG_REG_R1, TCG_REG_R0);
        } else {
            tcg_out_ld32_rwb(s, COND_AL, TCG_REG_R2, TCG_REG_R1, TCG_REG_R0);
        }
    } else {
        tcg_out_dat_reg(s, COND_AL, ARITH_ADD,
                        TCG_REG_R1, TCG_REG_R1, TCG_REG_R0, 0);
        if (TARGET_LONG_BITS == 64) {
            tcg_out_ldrd_8(s, COND_AL, TCG_REG_R2, TCG_REG_R1, cmp_off);
        } else {
            tcg_out_ld32_12(s, COND_AL, TCG_REG_R2, TCG_REG_R1, cmp_off);
        }
    }

    /* Load the tlb addend.  */
    tcg_out_ld32_12(s, COND_AL, TCG_REG_R1, TCG_REG_R1,
                    offsetof(CPUTLBEntry, addend));

    /*
     * Check alignment, check comparators.
     * Do this in 2-4 insns.  Use MOVW for v7, if possible,
     * to reduce the number of sequential conditional instructions.
     * Almost all guests have at least 4k pages, which means that we need
     * to clear at least 9 bits even for an 8-byte memory, which means it
     * isn't worth checking for an immediate operand for BIC.
     *
     * For unaligned accesses, test the page of the last unit of alignment.
     * This leaves the least significant alignment bits unchanged, and of
     * course must be zero.
     */
    t_addr = addrlo;
    if (a_mask < s_mask) {
        t_addr = TCG_REG_R0;
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, t_addr,
                        addrlo, s_mask - a_mask);
    }
    if (use_armv7_instructions && TARGET_PAGE_BITS <= 16) {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, ~(TARGET_PAGE_MASK | a_mask));
        tcg_out_dat_reg(s, COND_AL, ARITH_BIC, TCG_REG_TMP,
                        t_addr, TCG_REG_TMP, 0);
        tcg_out_dat_reg(s, COND_AL, ARITH_CMP, 0, TCG_REG_R2, TCG_REG_TMP, 0);
    } else {
        if (a_mask) {
            tcg_debug_assert(a_mask <= 0xff);
            tcg_out_dat_imm(s, COND_AL, ARITH_TST, 0, addrlo, a_mask);
        }
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV, TCG_REG_TMP, 0, t_addr,
                        SHIFT_IMM_LSR(TARGET_PAGE_BITS));
        tcg_out_dat_reg(s, (a_mask ? COND_EQ : COND_AL), ARITH_CMP,
                        0, TCG_REG_R2, TCG_REG_TMP,
                        SHIFT_IMM_LSL(TARGET_PAGE_BITS));
    }

    if (TARGET_LONG_BITS == 64) {
        tcg_out_dat_reg(s, COND_EQ, ARITH_CMP, 0, TCG_REG_R3, addrhi, 0);
    }

    return TCG_REG_R1;
}

/* Record the context of a call to the out of line helper code for the slow
   path for a load or store, so that we can later generate the correct
   helper code.  */
static void add_qemu_ldst_label(TCGContext *s, bool is_ld, MemOpIdx oi,
                                TCGReg datalo, TCGReg datahi, TCGReg addrlo,
                                TCGReg addrhi, tcg_insn_unit *raddr,
                                tcg_insn_unit *label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->oi = oi;
    label->datalo_reg = datalo;
    label->datahi_reg = datahi;
    label->addrlo_reg = addrlo;
    label->addrhi_reg = addrhi;
    label->raddr = tcg_splitwx_to_rx(raddr);
    label->label_ptr[0] = label_ptr;
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg argreg, datalo, datahi;
    MemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);

    if (!reloc_pc24(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    argreg = tcg_out_arg_reg32(s, TCG_REG_R0, TCG_AREG0);
    if (TARGET_LONG_BITS == 64) {
        argreg = tcg_out_arg_reg64(s, argreg, lb->addrlo_reg, lb->addrhi_reg);
    } else {
        argreg = tcg_out_arg_reg32(s, argreg, lb->addrlo_reg);
    }
    argreg = tcg_out_arg_imm32(s, argreg, oi);
    argreg = tcg_out_arg_reg32(s, argreg, TCG_REG_R14);

    /* Use the canonical unsigned helpers and minimize icache usage. */
    tcg_out_call_int(s, qemu_ld_helpers[opc & MO_SIZE]);

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
    case MO_UQ:
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
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg argreg, datalo, datahi;
    MemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);

    if (!reloc_pc24(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

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

    argreg = tcg_out_arg_imm32(s, argreg, oi);
    argreg = tcg_out_arg_reg32(s, argreg, TCG_REG_R14);

    /* Tail-call to the helper, which will return to the fast path.  */
    tcg_out_goto(s, COND_AL, qemu_st_helpers[opc & MO_SIZE]);
    return true;
}
#else

static void tcg_out_test_alignment(TCGContext *s, bool is_ld, TCGReg addrlo,
                                   TCGReg addrhi, unsigned a_bits)
{
    unsigned a_mask = (1 << a_bits) - 1;
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->addrlo_reg = addrlo;
    label->addrhi_reg = addrhi;

    /* We are expecting a_bits to max out at 7, and can easily support 8. */
    tcg_debug_assert(a_mask <= 0xff);
    /* tst addr, #mask */
    tcg_out_dat_imm(s, COND_AL, ARITH_TST, 0, addrlo, a_mask);

    /* blne slow_path */
    label->label_ptr[0] = s->code_ptr;
    tcg_out_bl_imm(s, COND_NE, 0);

    label->raddr = tcg_splitwx_to_rx(s->code_ptr);
}

static bool tcg_out_fail_alignment(TCGContext *s, TCGLabelQemuLdst *l)
{
    if (!reloc_pc24(l->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    if (TARGET_LONG_BITS == 64) {
        /* 64-bit target address is aligned into R2:R3. */
        if (l->addrhi_reg != TCG_REG_R2) {
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R2, l->addrlo_reg);
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R3, l->addrhi_reg);
        } else if (l->addrlo_reg != TCG_REG_R3) {
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R3, l->addrhi_reg);
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R2, l->addrlo_reg);
        } else {
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R1, TCG_REG_R2);
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R2, TCG_REG_R3);
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R3, TCG_REG_R1);
        }
    } else {
        tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R1, l->addrlo_reg);
    }
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R0, TCG_AREG0);

    /*
     * Tail call to the helper, with the return address back inline,
     * just for the clarity of the debugging traceback -- the helper
     * cannot return.  We have used BLNE to arrive here, so LR is
     * already set.
     */
    tcg_out_goto(s, COND_AL, (const void *)
                 (l->is_ld ? helper_unaligned_ld : helper_unaligned_st));
    return true;
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    return tcg_out_fail_alignment(s, l);
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    return tcg_out_fail_alignment(s, l);
}
#endif /* SOFTMMU */

static void tcg_out_qemu_ld_index(TCGContext *s, MemOp opc,
                                  TCGReg datalo, TCGReg datahi,
                                  TCGReg addrlo, TCGReg addend,
                                  bool scratch_addend)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & MO_SSIZE) {
    case MO_UB:
        tcg_out_ld8_r(s, COND_AL, datalo, addrlo, addend);
        break;
    case MO_SB:
        tcg_out_ld8s_r(s, COND_AL, datalo, addrlo, addend);
        break;
    case MO_UW:
        tcg_out_ld16u_r(s, COND_AL, datalo, addrlo, addend);
        break;
    case MO_SW:
        tcg_out_ld16s_r(s, COND_AL, datalo, addrlo, addend);
        break;
    case MO_UL:
        tcg_out_ld32_r(s, COND_AL, datalo, addrlo, addend);
        break;
    case MO_UQ:
        /* We used pair allocation for datalo, so already should be aligned. */
        tcg_debug_assert((datalo & 1) == 0);
        tcg_debug_assert(datahi == datalo + 1);
        /* LDRD requires alignment; double-check that. */
        if (get_alignment_bits(opc) >= MO_64) {
            /*
             * Rm (the second address op) must not overlap Rt or Rt + 1.
             * Since datalo is aligned, we can simplify the test via alignment.
             * Flip the two address arguments if that works.
             */
            if ((addend & ~1) != datalo) {
                tcg_out_ldrd_r(s, COND_AL, datalo, addrlo, addend);
                break;
            }
            if ((addrlo & ~1) != datalo) {
                tcg_out_ldrd_r(s, COND_AL, datalo, addend, addrlo);
                break;
            }
        }
        if (scratch_addend) {
            tcg_out_ld32_rwb(s, COND_AL, datalo, addend, addrlo);
            tcg_out_ld32_12(s, COND_AL, datahi, addend, 4);
        } else {
            tcg_out_dat_reg(s, COND_AL, ARITH_ADD, TCG_REG_TMP,
                            addend, addrlo, SHIFT_IMM_LSL(0));
            tcg_out_ld32_12(s, COND_AL, datalo, TCG_REG_TMP, 0);
            tcg_out_ld32_12(s, COND_AL, datahi, TCG_REG_TMP, 4);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

#ifndef CONFIG_SOFTMMU
static void tcg_out_qemu_ld_direct(TCGContext *s, MemOp opc, TCGReg datalo,
                                   TCGReg datahi, TCGReg addrlo)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & MO_SSIZE) {
    case MO_UB:
        tcg_out_ld8_12(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_SB:
        tcg_out_ld8s_8(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_UW:
        tcg_out_ld16u_8(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_SW:
        tcg_out_ld16s_8(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_UL:
        tcg_out_ld32_12(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_UQ:
        /* We used pair allocation for datalo, so already should be aligned. */
        tcg_debug_assert((datalo & 1) == 0);
        tcg_debug_assert(datahi == datalo + 1);
        /* LDRD requires alignment; double-check that. */
        if (get_alignment_bits(opc) >= MO_64) {
            tcg_out_ldrd_8(s, COND_AL, datalo, addrlo, 0);
        } else if (datalo == addrlo) {
            tcg_out_ld32_12(s, COND_AL, datahi, addrlo, 4);
            tcg_out_ld32_12(s, COND_AL, datalo, addrlo, 0);
        } else {
            tcg_out_ld32_12(s, COND_AL, datalo, addrlo, 0);
            tcg_out_ld32_12(s, COND_AL, datahi, addrlo, 4);
        }
        break;
    default:
        g_assert_not_reached();
    }
}
#endif

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg addrlo, datalo, datahi, addrhi __attribute__((unused));
    MemOpIdx oi;
    MemOp opc;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    TCGReg addend;
    tcg_insn_unit *label_ptr;
#else
    unsigned a_bits;
#endif

    datalo = *args++;
    datahi = (is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS == 64 ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#ifdef CONFIG_SOFTMMU
    mem_index = get_mmuidx(oi);
    addend = tcg_out_tlb_read(s, addrlo, addrhi, opc, mem_index, 1);

    /* This a conditional BL only to load a pointer within this opcode into LR
       for the slow path.  We will not be using the value for a tail call.  */
    label_ptr = s->code_ptr;
    tcg_out_bl_imm(s, COND_NE, 0);

    tcg_out_qemu_ld_index(s, opc, datalo, datahi, addrlo, addend, true);

    add_qemu_ldst_label(s, true, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    a_bits = get_alignment_bits(opc);
    if (a_bits) {
        tcg_out_test_alignment(s, true, addrlo, addrhi, a_bits);
    }
    if (guest_base) {
        tcg_out_qemu_ld_index(s, opc, datalo, datahi,
                              addrlo, TCG_REG_GUEST_BASE, false);
    } else {
        tcg_out_qemu_ld_direct(s, opc, datalo, datahi, addrlo);
    }
#endif
}

static void tcg_out_qemu_st_index(TCGContext *s, ARMCond cond, MemOp opc,
                                  TCGReg datalo, TCGReg datahi,
                                  TCGReg addrlo, TCGReg addend,
                                  bool scratch_addend)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & MO_SIZE) {
    case MO_8:
        tcg_out_st8_r(s, cond, datalo, addrlo, addend);
        break;
    case MO_16:
        tcg_out_st16_r(s, cond, datalo, addrlo, addend);
        break;
    case MO_32:
        tcg_out_st32_r(s, cond, datalo, addrlo, addend);
        break;
    case MO_64:
        /* We used pair allocation for datalo, so already should be aligned. */
        tcg_debug_assert((datalo & 1) == 0);
        tcg_debug_assert(datahi == datalo + 1);
        /* STRD requires alignment; double-check that. */
        if (get_alignment_bits(opc) >= MO_64) {
            tcg_out_strd_r(s, cond, datalo, addrlo, addend);
        } else if (scratch_addend) {
            tcg_out_st32_rwb(s, cond, datalo, addend, addrlo);
            tcg_out_st32_12(s, cond, datahi, addend, 4);
        } else {
            tcg_out_dat_reg(s, cond, ARITH_ADD, TCG_REG_TMP,
                            addend, addrlo, SHIFT_IMM_LSL(0));
            tcg_out_st32_12(s, cond, datalo, TCG_REG_TMP, 0);
            tcg_out_st32_12(s, cond, datahi, TCG_REG_TMP, 4);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

#ifndef CONFIG_SOFTMMU
static void tcg_out_qemu_st_direct(TCGContext *s, MemOp opc, TCGReg datalo,
                                   TCGReg datahi, TCGReg addrlo)
{
    /* Byte swapping is left to middle-end expansion. */
    tcg_debug_assert((opc & MO_BSWAP) == 0);

    switch (opc & MO_SIZE) {
    case MO_8:
        tcg_out_st8_12(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_16:
        tcg_out_st16_8(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_32:
        tcg_out_st32_12(s, COND_AL, datalo, addrlo, 0);
        break;
    case MO_64:
        /* We used pair allocation for datalo, so already should be aligned. */
        tcg_debug_assert((datalo & 1) == 0);
        tcg_debug_assert(datahi == datalo + 1);
        /* STRD requires alignment; double-check that. */
        if (get_alignment_bits(opc) >= MO_64) {
            tcg_out_strd_8(s, COND_AL, datalo, addrlo, 0);
        } else {
            tcg_out_st32_12(s, COND_AL, datalo, addrlo, 0);
            tcg_out_st32_12(s, COND_AL, datahi, addrlo, 4);
        }
        break;
    default:
        g_assert_not_reached();
    }
}
#endif

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg addrlo, datalo, datahi, addrhi __attribute__((unused));
    MemOpIdx oi;
    MemOp opc;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    TCGReg addend;
    tcg_insn_unit *label_ptr;
#else
    unsigned a_bits;
#endif

    datalo = *args++;
    datahi = (is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS == 64 ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#ifdef CONFIG_SOFTMMU
    mem_index = get_mmuidx(oi);
    addend = tcg_out_tlb_read(s, addrlo, addrhi, opc, mem_index, 0);

    tcg_out_qemu_st_index(s, COND_EQ, opc, datalo, datahi,
                          addrlo, addend, true);

    /* The conditional call must come last, as we're going to return here.  */
    label_ptr = s->code_ptr;
    tcg_out_bl_imm(s, COND_NE, 0);

    add_qemu_ldst_label(s, false, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    a_bits = get_alignment_bits(opc);
    if (a_bits) {
        tcg_out_test_alignment(s, false, addrlo, addrhi, a_bits);
    }
    if (guest_base) {
        tcg_out_qemu_st_index(s, COND_AL, opc, datalo, datahi,
                              addrlo, TCG_REG_GUEST_BASE, false);
    } else {
        tcg_out_qemu_st_direct(s, opc, datalo, datahi, addrlo);
    }
#endif
}

static void tcg_out_epilogue(TCGContext *s);

static void tcg_out_exit_tb(TCGContext *s, uintptr_t arg)
{
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R0, arg);
    tcg_out_epilogue(s);
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    uintptr_t i_addr;
    intptr_t i_disp;

    /* Direct branch will be patched by tb_target_set_jmp_target. */
    set_jmp_insn_offset(s, which);
    tcg_out32(s, INSN_NOP);

    /* When branch is out of range, fall through to indirect. */
    i_addr = get_jmp_target_addr(s, which);
    i_disp = tcg_pcrel_diff(s, (void *)i_addr) - 8;
    tcg_debug_assert(i_disp < 0);
    if (i_disp >= -0xfff) {
        tcg_out_ld32_12(s, COND_AL, TCG_REG_PC, TCG_REG_PC, i_disp);
    } else {
        /*
         * The TB is close, but outside the 12 bits addressable by
         * the load.  We can extend this to 20 bits with a sub of a
         * shifted immediate from pc.
         */
        int h = -i_disp;
        int l = h & 0xfff;

        h = encode_imm_nofail(h - l);
        tcg_out_dat_imm(s, COND_AL, ARITH_SUB, TCG_REG_R0, TCG_REG_PC, h);
        tcg_out_ld32_12(s, COND_AL, TCG_REG_PC, TCG_REG_R0, l);
    }
    set_jmp_reset_offset(s, which);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    uintptr_t addr = tb->jmp_target_addr[n];
    ptrdiff_t offset = addr - (jmp_rx + 8);
    tcg_insn_unit insn;

    /* Either directly branch, or fall through to indirect branch. */
    if (offset == sextract64(offset, 0, 26)) {
        /* B <addr> */
        insn = deposit32((COND_AL << 28) | INSN_B, 0, 24, offset >> 2);
    } else {
        insn = INSN_NOP;
    }

    qatomic_set((uint32_t *)jmp_rw, insn);
    flush_idcache_range(jmp_rx, jmp_rw, 4);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    TCGArg a0, a1, a2, a3, a4, a5;
    int c;

    switch (opc) {
    case INDEX_op_goto_ptr:
        tcg_out_b_reg(s, COND_AL, args[0]);
        break;
    case INDEX_op_br:
        tcg_out_goto_label(s, COND_AL, arg_label(args[0]));
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

    case INDEX_op_ctz_i32:
        tcg_out_dat_reg(s, COND_AL, INSN_RBIT, TCG_REG_TMP, 0, args[1], 0);
        a1 = TCG_REG_TMP;
        goto do_clz;

    case INDEX_op_clz_i32:
        a1 = args[1];
    do_clz:
        a0 = args[0];
        a2 = args[2];
        c = const_args[2];
        if (c && a2 == 32) {
            tcg_out_dat_reg(s, COND_AL, INSN_CLZ, a0, 0, a1, 0);
            break;
        }
        tcg_out_dat_imm(s, COND_AL, ARITH_CMP, 0, a1, 0);
        tcg_out_dat_reg(s, COND_NE, INSN_CLZ, a0, 0, a1, 0);
        if (c || a0 != a2) {
            tcg_out_dat_rIK(s, COND_EQ, ARITH_MOV, ARITH_MVN, a0, 0, a2, c);
        }
        break;

    case INDEX_op_brcond_i32:
        tcg_out_dat_rIN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0,
                       args[0], args[1], const_args[1]);
        tcg_out_goto_label(s, tcg_cond_to_arm_cond[args[2]],
                           arg_label(args[3]));
        break;
    case INDEX_op_setcond_i32:
        tcg_out_dat_rIN(s, COND_AL, ARITH_CMP, ARITH_CMN, 0,
                        args[1], args[2], const_args[2]);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[args[3]],
                        ARITH_MOV, args[0], 0, 1);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[tcg_invert_cond(args[3])],
                        ARITH_MOV, args[0], 0, 0);
        break;

    case INDEX_op_brcond2_i32:
        c = tcg_out_cmp2(s, args, const_args);
        tcg_out_goto_label(s, tcg_cond_to_arm_cond[c], arg_label(args[5]));
        break;
    case INDEX_op_setcond2_i32:
        c = tcg_out_cmp2(s, args + 1, const_args + 1);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[c], ARITH_MOV, args[0], 0, 1);
        tcg_out_dat_imm(s, tcg_cond_to_arm_cond[tcg_invert_cond(c)],
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
        tcg_out_bswap16(s, COND_AL, args[0], args[1], args[2]);
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
    case INDEX_op_extract_i32:
        tcg_out_extract(s, COND_AL, args[0], args[1], args[2], args[3]);
        break;
    case INDEX_op_sextract_i32:
        tcg_out_sextract(s, COND_AL, args[0], args[1], args[2], args[3]);
        break;
    case INDEX_op_extract2_i32:
        /* ??? These optimization vs zero should be generic.  */
        /* ??? But we can't substitute 2 for 1 in the opcode stream yet.  */
        if (const_args[1]) {
            if (const_args[2]) {
                tcg_out_movi(s, TCG_TYPE_REG, args[0], 0);
            } else {
                tcg_out_dat_reg(s, COND_AL, ARITH_MOV, args[0], 0,
                                args[2], SHIFT_IMM_LSL(32 - args[3]));
            }
        } else if (const_args[2]) {
            tcg_out_dat_reg(s, COND_AL, ARITH_MOV, args[0], 0,
                            args[1], SHIFT_IMM_LSR(args[3]));
        } else {
            /* We can do extract2 in 2 insns, vs the 3 required otherwise.  */
            tcg_out_dat_reg(s, COND_AL, ARITH_MOV, TCG_REG_TMP, 0,
                            args[2], SHIFT_IMM_LSL(32 - args[3]));
            tcg_out_dat_reg(s, COND_AL, ARITH_ORR, args[0], TCG_REG_TMP,
                            args[1], SHIFT_IMM_LSR(args[3]));
        }
        break;

    case INDEX_op_div_i32:
        tcg_out_sdiv(s, COND_AL, args[0], args[1], args[2]);
        break;
    case INDEX_op_divu_i32:
        tcg_out_udiv(s, COND_AL, args[0], args[1], args[2]);
        break;

    case INDEX_op_mb:
        tcg_out_mb(s, args[0]);
        break;

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    case INDEX_op_exit_tb:  /* Always emitted via tcg_out_exit_tb.  */
    case INDEX_op_goto_tb:  /* Always emitted via tcg_out_goto_tb.  */
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
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap32_i32:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16u_i32:
    case INDEX_op_extract_i32:
    case INDEX_op_sextract_i32:
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
        return C_O0_I2(r, r);

    case INDEX_op_add_i32:
    case INDEX_op_sub_i32:
    case INDEX_op_setcond_i32:
        return C_O1_I2(r, r, rIN);

    case INDEX_op_and_i32:
    case INDEX_op_andc_i32:
    case INDEX_op_clz_i32:
    case INDEX_op_ctz_i32:
        return C_O1_I2(r, r, rIK);

    case INDEX_op_mul_i32:
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
        return C_O1_I2(r, r, r);

    case INDEX_op_mulu2_i32:
    case INDEX_op_muls2_i32:
        return C_O2_I2(r, r, r, r);

    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
        return C_O1_I2(r, r, rI);

    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_rotl_i32:
    case INDEX_op_rotr_i32:
        return C_O1_I2(r, r, ri);

    case INDEX_op_brcond_i32:
        return C_O0_I2(r, rIN);
    case INDEX_op_deposit_i32:
        return C_O1_I2(r, 0, rZ);
    case INDEX_op_extract2_i32:
        return C_O1_I2(r, rZ, rZ);
    case INDEX_op_movcond_i32:
        return C_O1_I4(r, r, rIN, rIK, 0);
    case INDEX_op_add2_i32:
        return C_O2_I4(r, r, r, r, rIN, rIK);
    case INDEX_op_sub2_i32:
        return C_O2_I4(r, r, rI, rI, rIN, rIK);
    case INDEX_op_brcond2_i32:
        return C_O0_I4(r, r, rI, rI);
    case INDEX_op_setcond2_i32:
        return C_O1_I4(r, r, r, rI, rI);

    case INDEX_op_qemu_ld_i32:
        return TARGET_LONG_BITS == 32 ? C_O1_I1(r, l) : C_O1_I2(r, l, l);
    case INDEX_op_qemu_ld_i64:
        return TARGET_LONG_BITS == 32 ? C_O2_I1(e, p, l) : C_O2_I2(e, p, l, l);
    case INDEX_op_qemu_st_i32:
        return TARGET_LONG_BITS == 32 ? C_O0_I2(s, s) : C_O0_I3(s, s, s);
    case INDEX_op_qemu_st_i64:
        return TARGET_LONG_BITS == 32 ? C_O0_I3(S, p, s) : C_O0_I4(S, p, s, s);

    case INDEX_op_st_vec:
        return C_O0_I2(w, r);
    case INDEX_op_ld_vec:
    case INDEX_op_dupm_vec:
        return C_O1_I1(w, r);
    case INDEX_op_dup_vec:
        return C_O1_I1(w, wr);
    case INDEX_op_abs_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_not_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
        return C_O1_I1(w, w);
    case INDEX_op_dup2_vec:
    case INDEX_op_add_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_arm_sshl_vec:
    case INDEX_op_arm_ushl_vec:
        return C_O1_I2(w, w, w);
    case INDEX_op_arm_sli_vec:
        return C_O1_I2(w, 0, w);
    case INDEX_op_or_vec:
    case INDEX_op_andc_vec:
        return C_O1_I2(w, w, wO);
    case INDEX_op_and_vec:
    case INDEX_op_orc_vec:
        return C_O1_I2(w, w, wV);
    case INDEX_op_cmp_vec:
        return C_O1_I2(w, w, wZ);
    case INDEX_op_bitsel_vec:
        return C_O1_I3(w, w, w, w);
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
#if !defined(use_idiv_instructions) || !defined(use_neon_instructions)
    {
        unsigned long hwcap = qemu_getauxval(AT_HWCAP);
#ifndef use_idiv_instructions
        use_idiv_instructions = (hwcap & HWCAP_ARM_IDIVA) != 0;
#endif
#ifndef use_neon_instructions
        use_neon_instructions = (hwcap & HWCAP_ARM_NEON) != 0;
#endif
    }
#endif

    if (__ARM_ARCH < 7) {
        const char *pl = (const char *)qemu_getauxval(AT_PLATFORM);
        if (pl != NULL && pl[0] == 'v' && pl[1] >= '4' && pl[1] <= '9') {
            arm_arch = pl[1] - '0';
        }

        if (arm_arch < 6) {
            error_report("TCG: ARMv%d is unsupported; exiting", arm_arch);
            exit(EXIT_FAILURE);
        }
    }

    tcg_target_available_regs[TCG_TYPE_I32] = ALL_GENERAL_REGS;

    tcg_target_call_clobber_regs = 0;
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R12);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R14);

    if (use_neon_instructions) {
        tcg_target_available_regs[TCG_TYPE_V64]  = ALL_VECTOR_REGS;
        tcg_target_available_regs[TCG_TYPE_V128] = ALL_VECTOR_REGS;

        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q0);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q1);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q2);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q3);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q8);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q9);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q10);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q11);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q12);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q13);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q14);
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_Q15);
    }

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_PC);
    tcg_regset_set_reg(s->reserved_regs, TCG_VEC_TMP);
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_ld32u(s, COND_AL, arg, arg1, arg2);
        return;
    case TCG_TYPE_V64:
        /* regs 1; size 8; align 8 */
        tcg_out_vldst(s, INSN_VLD1 | 0x7d0, arg, arg1, arg2);
        return;
    case TCG_TYPE_V128:
        /*
         * We have only 8-byte alignment for the stack per the ABI.
         * Rather than dynamically re-align the stack, it's easier
         * to simply not request alignment beyond that.  So:
         * regs 2; size 8; align 8
         */
        tcg_out_vldst(s, INSN_VLD1 | 0xad0, arg, arg1, arg2);
        return;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_st32(s, COND_AL, arg, arg1, arg2);
        return;
    case TCG_TYPE_V64:
        /* regs 1; size 8; align 8 */
        tcg_out_vldst(s, INSN_VST1 | 0x7d0, arg, arg1, arg2);
        return;
    case TCG_TYPE_V128:
        /* See tcg_out_ld re alignment: regs 2; size 8; align 8 */
        tcg_out_vldst(s, INSN_VST1 | 0xad0, arg, arg1, arg2);
        return;
    default:
        g_assert_not_reached();
    }
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    return false;
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I32:
        if (ret < TCG_REG_Q0 && arg < TCG_REG_Q0) {
            tcg_out_mov_reg(s, COND_AL, ret, arg);
            return true;
        }
        return false;

    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
        /* "VMOV D,N" is an alias for "VORR D,N,N". */
        tcg_out_vreg3(s, INSN_VORR, type - TCG_TYPE_V64, 0, ret, arg, arg);
        return true;

    default:
        g_assert_not_reached();
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    tcg_debug_assert(type == TCG_TYPE_I32);
    tcg_debug_assert(ret < TCG_REG_Q0);
    tcg_out_movi32(s, COND_AL, ret, arg);
}

static void tcg_out_addi_ptr(TCGContext *s, TCGReg rd, TCGReg rs,
                             tcg_target_long imm)
{
    int enc, opc = ARITH_ADD;

    /* All of the easiest immediates to encode are positive. */
    if (imm < 0) {
        imm = -imm;
        opc = ARITH_SUB;
    }
    enc = encode_imm(imm);
    if (enc >= 0) {
        tcg_out_dat_imm(s, COND_AL, opc, rd, rs, enc);
    } else {
        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, imm);
        tcg_out_dat_reg(s, COND_AL, opc, rd, rs,
                        TCG_REG_TMP, SHIFT_IMM_LSL(0));
    }
}

/* Type is always V128, with I64 elements.  */
static void tcg_out_dup2_vec(TCGContext *s, TCGReg rd, TCGReg rl, TCGReg rh)
{
    /* Move high element into place first. */
    /* VMOV Dd+1, Ds */
    tcg_out_vreg3(s, INSN_VORR | (1 << 12), 0, 0, rd, rh, rh);
    /* Move low element into place; tcg_out_mov will check for nop. */
    tcg_out_mov(s, TCG_TYPE_V64, rd, rl);
}

static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg rd, TCGReg rs)
{
    int q = type - TCG_TYPE_V64;

    if (vece == MO_64) {
        if (type == TCG_TYPE_V128) {
            tcg_out_dup2_vec(s, rd, rs, rs);
        } else {
            tcg_out_mov(s, TCG_TYPE_V64, rd, rs);
        }
    } else if (rs < TCG_REG_Q0) {
        int b = (vece == MO_8);
        int e = (vece == MO_16);
        tcg_out32(s, INSN_VDUP_G | (b << 22) | (q << 21) | (e << 5) |
                  encode_vn(rd) | (rs << 12));
    } else {
        int imm4 = 1 << vece;
        tcg_out32(s, INSN_VDUP_S | (imm4 << 16) | (q << 6) |
                  encode_vd(rd) | encode_vm(rs));
    }
    return true;
}

static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg rd, TCGReg base, intptr_t offset)
{
    if (vece == MO_64) {
        tcg_out_ld(s, TCG_TYPE_V64, rd, base, offset);
        if (type == TCG_TYPE_V128) {
            tcg_out_dup2_vec(s, rd, rd, rd);
        }
    } else {
        int q = type - TCG_TYPE_V64;
        tcg_out_vldst(s, INSN_VLD1R | (vece << 6) | (q << 5),
                      rd, base, offset);
    }
    return true;
}

static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg rd, int64_t v64)
{
    int q = type - TCG_TYPE_V64;
    int cmode, imm8, i;

    /* Test all bytes equal first.  */
    if (vece == MO_8) {
        tcg_out_vmovi(s, rd, q, 0, 0xe, v64);
        return;
    }

    /*
     * Test all bytes 0x00 or 0xff second.  This can match cases that
     * might otherwise take 2 or 3 insns for MO_16 or MO_32 below.
     */
    for (i = imm8 = 0; i < 8; i++) {
        uint8_t byte = v64 >> (i * 8);
        if (byte == 0xff) {
            imm8 |= 1 << i;
        } else if (byte != 0) {
            goto fail_bytes;
        }
    }
    tcg_out_vmovi(s, rd, q, 1, 0xe, imm8);
    return;
 fail_bytes:

    /*
     * Tests for various replications.  For each element width, if we
     * cannot find an expansion there's no point checking a larger
     * width because we already know by replication it cannot match.
     */
    if (vece == MO_16) {
        uint16_t v16 = v64;

        if (is_shimm16(v16, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 0, cmode, imm8);
            return;
        }
        if (is_shimm16(~v16, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 1, cmode, imm8);
            return;
        }

        /*
         * Otherwise, all remaining constants can be loaded in two insns:
         * rd = v16 & 0xff, rd |= v16 & 0xff00.
         */
        tcg_out_vmovi(s, rd, q, 0, 0x8, v16 & 0xff);
        tcg_out_vmovi(s, rd, q, 0, 0xb, v16 >> 8);   /* VORRI */
        return;
    }

    if (vece == MO_32) {
        uint32_t v32 = v64;

        if (is_shimm32(v32, &cmode, &imm8) ||
            is_soimm32(v32, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 0, cmode, imm8);
            return;
        }
        if (is_shimm32(~v32, &cmode, &imm8) ||
            is_soimm32(~v32, &cmode, &imm8)) {
            tcg_out_vmovi(s, rd, q, 1, cmode, imm8);
            return;
        }

        /*
         * Restrict the set of constants to those we can load with
         * two instructions.  Others we load from the pool.
         */
        i = is_shimm32_pair(v32, &cmode, &imm8);
        if (i) {
            tcg_out_vmovi(s, rd, q, 0, cmode, imm8);
            tcg_out_vmovi(s, rd, q, 0, i | 1, extract32(v32, i * 4, 8));
            return;
        }
        i = is_shimm32_pair(~v32, &cmode, &imm8);
        if (i) {
            tcg_out_vmovi(s, rd, q, 1, cmode, imm8);
            tcg_out_vmovi(s, rd, q, 1, i | 1, extract32(~v32, i * 4, 8));
            return;
        }
    }

    /*
     * As a last resort, load from the constant pool.
     */
    if (!q || vece == MO_64) {
        new_pool_l2(s, R_ARM_PC11, s->code_ptr, 0, v64, v64 >> 32);
        /* VLDR Dd, [pc + offset] */
        tcg_out32(s, INSN_VLDR_D | encode_vd(rd) | (0xf << 16));
        if (q) {
            tcg_out_dup2_vec(s, rd, rd, rd);
        }
    } else {
        new_pool_label(s, (uint32_t)v64, R_ARM_PC8, s->code_ptr, 0);
        /* add tmp, pc, offset */
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, TCG_REG_TMP, TCG_REG_PC, 0);
        tcg_out_dupm_vec(s, type, MO_32, rd, TCG_REG_TMP, 0);
    }
}

static const ARMInsn vec_cmp_insn[16] = {
    [TCG_COND_EQ] = INSN_VCEQ,
    [TCG_COND_GT] = INSN_VCGT,
    [TCG_COND_GE] = INSN_VCGE,
    [TCG_COND_GTU] = INSN_VCGT_U,
    [TCG_COND_GEU] = INSN_VCGE_U,
};

static const ARMInsn vec_cmp0_insn[16] = {
    [TCG_COND_EQ] = INSN_VCEQ0,
    [TCG_COND_GT] = INSN_VCGT0,
    [TCG_COND_GE] = INSN_VCGE0,
    [TCG_COND_LT] = INSN_VCLT0,
    [TCG_COND_LE] = INSN_VCLE0,
};

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS])
{
    TCGType type = vecl + TCG_TYPE_V64;
    unsigned q = vecl;
    TCGArg a0, a1, a2, a3;
    int cmode, imm8;

    a0 = args[0];
    a1 = args[1];
    a2 = args[2];

    switch (opc) {
    case INDEX_op_ld_vec:
        tcg_out_ld(s, type, a0, a1, a2);
        return;
    case INDEX_op_st_vec:
        tcg_out_st(s, type, a0, a1, a2);
        return;
    case INDEX_op_dupm_vec:
        tcg_out_dupm_vec(s, type, vece, a0, a1, a2);
        return;
    case INDEX_op_dup2_vec:
        tcg_out_dup2_vec(s, a0, a1, a2);
        return;
    case INDEX_op_abs_vec:
        tcg_out_vreg2(s, INSN_VABS, q, vece, a0, a1);
        return;
    case INDEX_op_neg_vec:
        tcg_out_vreg2(s, INSN_VNEG, q, vece, a0, a1);
        return;
    case INDEX_op_not_vec:
        tcg_out_vreg2(s, INSN_VMVN, q, 0, a0, a1);
        return;
    case INDEX_op_add_vec:
        tcg_out_vreg3(s, INSN_VADD, q, vece, a0, a1, a2);
        return;
    case INDEX_op_mul_vec:
        tcg_out_vreg3(s, INSN_VMUL, q, vece, a0, a1, a2);
        return;
    case INDEX_op_smax_vec:
        tcg_out_vreg3(s, INSN_VMAX, q, vece, a0, a1, a2);
        return;
    case INDEX_op_smin_vec:
        tcg_out_vreg3(s, INSN_VMIN, q, vece, a0, a1, a2);
        return;
    case INDEX_op_sub_vec:
        tcg_out_vreg3(s, INSN_VSUB, q, vece, a0, a1, a2);
        return;
    case INDEX_op_ssadd_vec:
        tcg_out_vreg3(s, INSN_VQADD, q, vece, a0, a1, a2);
        return;
    case INDEX_op_sssub_vec:
        tcg_out_vreg3(s, INSN_VQSUB, q, vece, a0, a1, a2);
        return;
    case INDEX_op_umax_vec:
        tcg_out_vreg3(s, INSN_VMAX_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_umin_vec:
        tcg_out_vreg3(s, INSN_VMIN_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_usadd_vec:
        tcg_out_vreg3(s, INSN_VQADD_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_ussub_vec:
        tcg_out_vreg3(s, INSN_VQSUB_U, q, vece, a0, a1, a2);
        return;
    case INDEX_op_xor_vec:
        tcg_out_vreg3(s, INSN_VEOR, q, 0, a0, a1, a2);
        return;
    case INDEX_op_arm_sshl_vec:
        /*
         * Note that Vm is the data and Vn is the shift count,
         * therefore the arguments appear reversed.
         */
        tcg_out_vreg3(s, INSN_VSHL_S, q, vece, a0, a2, a1);
        return;
    case INDEX_op_arm_ushl_vec:
        /* See above. */
        tcg_out_vreg3(s, INSN_VSHL_U, q, vece, a0, a2, a1);
        return;
    case INDEX_op_shli_vec:
        tcg_out_vshifti(s, INSN_VSHLI, q, a0, a1, a2 + (8 << vece));
        return;
    case INDEX_op_shri_vec:
        tcg_out_vshifti(s, INSN_VSHRI, q, a0, a1, (16 << vece) - a2);
        return;
    case INDEX_op_sari_vec:
        tcg_out_vshifti(s, INSN_VSARI, q, a0, a1, (16 << vece) - a2);
        return;
    case INDEX_op_arm_sli_vec:
        tcg_out_vshifti(s, INSN_VSLI, q, a0, a2, args[3] + (8 << vece));
        return;

    case INDEX_op_andc_vec:
        if (!const_args[2]) {
            tcg_out_vreg3(s, INSN_VBIC, q, 0, a0, a1, a2);
            return;
        }
        a2 = ~a2;
        /* fall through */
    case INDEX_op_and_vec:
        if (const_args[2]) {
            is_shimm1632(~a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_vmovi(s, a0, q, 1, cmode | 1, imm8); /* VBICI */
                return;
            }
            tcg_out_vmovi(s, a0, q, 1, cmode, imm8); /* VMVNI */
            a2 = a0;
        }
        tcg_out_vreg3(s, INSN_VAND, q, 0, a0, a1, a2);
        return;

    case INDEX_op_orc_vec:
        if (!const_args[2]) {
            tcg_out_vreg3(s, INSN_VORN, q, 0, a0, a1, a2);
            return;
        }
        a2 = ~a2;
        /* fall through */
    case INDEX_op_or_vec:
        if (const_args[2]) {
            is_shimm1632(a2, &cmode, &imm8);
            if (a0 == a1) {
                tcg_out_vmovi(s, a0, q, 0, cmode | 1, imm8); /* VORRI */
                return;
            }
            tcg_out_vmovi(s, a0, q, 0, cmode, imm8); /* VMOVI */
            a2 = a0;
        }
        tcg_out_vreg3(s, INSN_VORR, q, 0, a0, a1, a2);
        return;

    case INDEX_op_cmp_vec:
        {
            TCGCond cond = args[3];

            if (cond == TCG_COND_NE) {
                if (const_args[2]) {
                    tcg_out_vreg3(s, INSN_VTST, q, vece, a0, a1, a1);
                } else {
                    tcg_out_vreg3(s, INSN_VCEQ, q, vece, a0, a1, a2);
                    tcg_out_vreg2(s, INSN_VMVN, q, 0, a0, a0);
                }
            } else {
                ARMInsn insn;

                if (const_args[2]) {
                    insn = vec_cmp0_insn[cond];
                    if (insn) {
                        tcg_out_vreg2(s, insn, q, vece, a0, a1);
                        return;
                    }
                    tcg_out_dupi_vec(s, type, MO_8, TCG_VEC_TMP, 0);
                    a2 = TCG_VEC_TMP;
                }
                insn = vec_cmp_insn[cond];
                if (insn == 0) {
                    TCGArg t;
                    t = a1, a1 = a2, a2 = t;
                    cond = tcg_swap_cond(cond);
                    insn = vec_cmp_insn[cond];
                    tcg_debug_assert(insn != 0);
                }
                tcg_out_vreg3(s, insn, q, vece, a0, a1, a2);
            }
        }
        return;

    case INDEX_op_bitsel_vec:
        a3 = args[3];
        if (a0 == a3) {
            tcg_out_vreg3(s, INSN_VBIT, q, 0, a0, a2, a1);
        } else if (a0 == a2) {
            tcg_out_vreg3(s, INSN_VBIF, q, 0, a0, a3, a1);
        } else {
            tcg_out_mov(s, type, a0, a1);
            tcg_out_vreg3(s, INSN_VBSL, q, 0, a0, a2, a3);
        }
        return;

    case INDEX_op_mov_vec:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_dup_vec:  /* Always emitted via tcg_out_dup_vec.  */
    default:
        g_assert_not_reached();
    }
}

int tcg_can_emit_vec_op(TCGOpcode opc, TCGType type, unsigned vece)
{
    switch (opc) {
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_and_vec:
    case INDEX_op_andc_vec:
    case INDEX_op_or_vec:
    case INDEX_op_orc_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_not_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_bitsel_vec:
        return 1;
    case INDEX_op_abs_vec:
    case INDEX_op_cmp_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
        return vece < MO_64;
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_rotli_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
        return -1;
    default:
        return 0;
    }
}

void tcg_expand_vec_op(TCGOpcode opc, TCGType type, unsigned vece,
                       TCGArg a0, ...)
{
    va_list va;
    TCGv_vec v0, v1, v2, t1, t2, c1;
    TCGArg a2;

    va_start(va, a0);
    v0 = temp_tcgv_vec(arg_temp(a0));
    v1 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
    a2 = va_arg(va, TCGArg);
    va_end(va);

    switch (opc) {
    case INDEX_op_shlv_vec:
        /*
         * Merely propagate shlv_vec to arm_ushl_vec.
         * In this way we don't set TCG_TARGET_HAS_shv_vec
         * because everything is done via expansion.
         */
        v2 = temp_tcgv_vec(arg_temp(a2));
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        break;

    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
        /* Right shifts are negative left shifts for NEON.  */
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        tcg_gen_neg_vec(vece, t1, v2);
        if (opc == INDEX_op_shrv_vec) {
            opc = INDEX_op_arm_ushl_vec;
        } else {
            opc = INDEX_op_arm_sshl_vec;
        }
        vec_gen_3(opc, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotli_vec:
        t1 = tcg_temp_new_vec(type);
        tcg_gen_shri_vec(vece, t1, v1, -a2 & ((8 << vece) - 1));
        vec_gen_4(INDEX_op_arm_sli_vec, type, vece,
                  tcgv_vec_arg(v0), tcgv_vec_arg(t1), tcgv_vec_arg(v1), a2);
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotlv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        c1 = tcg_constant_vec(type, vece, 8 << vece);
        tcg_gen_sub_vec(vece, t1, v2, c1);
        /* Right shifts are negative left shifts for NEON.  */
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        tcg_gen_or_vec(vece, v0, v0, t1);
        tcg_temp_free_vec(t1);
        break;

    case INDEX_op_rotrv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t1 = tcg_temp_new_vec(type);
        t2 = tcg_temp_new_vec(type);
        c1 = tcg_constant_vec(type, vece, 8 << vece);
        tcg_gen_neg_vec(vece, t1, v2);
        tcg_gen_sub_vec(vece, t2, c1, v2);
        /* Right shifts are negative left shifts for NEON.  */
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1));
        vec_gen_3(INDEX_op_arm_ushl_vec, type, vece, tcgv_vec_arg(t2),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t2));
        tcg_gen_or_vec(vece, v0, t1, t2);
        tcg_temp_free_vec(t1);
        tcg_temp_free_vec(t2);
        break;

    default:
        g_assert_not_reached();
    }
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = INSN_NOP;
    }
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

#define STACK_ADDEND  (FRAME_SIZE - PUSH_SIZE)

static void tcg_target_qemu_prologue(TCGContext *s)
{
    /* Calling convention requires us to save r4-r11 and lr.  */
    /* stmdb sp!, { r4 - r11, lr } */
    tcg_out_ldstm(s, COND_AL, INSN_STMDB, TCG_REG_CALL_STACK,
                  (1 << TCG_REG_R4) | (1 << TCG_REG_R5) | (1 << TCG_REG_R6) |
                  (1 << TCG_REG_R7) | (1 << TCG_REG_R8) | (1 << TCG_REG_R9) |
                  (1 << TCG_REG_R10) | (1 << TCG_REG_R11) | (1 << TCG_REG_R14));

    /* Reserve callee argument and tcg temp space.  */
    tcg_out_dat_rI(s, COND_AL, ARITH_SUB, TCG_REG_CALL_STACK,
                   TCG_REG_CALL_STACK, STACK_ADDEND, 1);
    tcg_set_frame(s, TCG_REG_CALL_STACK, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

#ifndef CONFIG_SOFTMMU
    if (guest_base) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_GUEST_BASE, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_GUEST_BASE);
    }
#endif

    tcg_out_b_reg(s, COND_AL, tcg_target_call_iarg_regs[1]);

    /*
     * Return path for goto_ptr. Set return value to 0, a-la exit_tb,
     * and fall through to the rest of the epilogue.
     */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R0, 0);
    tcg_out_epilogue(s);
}

static void tcg_out_epilogue(TCGContext *s)
{
    /* Release local stack frame.  */
    tcg_out_dat_rI(s, COND_AL, ARITH_ADD, TCG_REG_CALL_STACK,
                   TCG_REG_CALL_STACK, STACK_ADDEND, 1);

    /* ldmia sp!, { r4 - r11, pc } */
    tcg_out_ldstm(s, COND_AL, INSN_LDMIA, TCG_REG_CALL_STACK,
                  (1 << TCG_REG_R4) | (1 << TCG_REG_R5) | (1 << TCG_REG_R6) |
                  (1 << TCG_REG_R7) | (1 << TCG_REG_R8) | (1 << TCG_REG_R9) |
                  (1 << TCG_REG_R10) | (1 << TCG_REG_R11) | (1 << TCG_REG_PC));
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

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
