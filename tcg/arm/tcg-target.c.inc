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
#include "../tcg-pool.c.inc"

int arm_arch = __ARM_ARCH;

#ifndef use_idiv_instructions
bool use_idiv_instructions;
#endif

/* ??? Ought to think about changing CONFIG_SOFTMMU to always defined.  */
#ifdef CONFIG_SOFTMMU
# define USING_SOFTMMU 1
#else
# define USING_SOFTMMU 0
#endif

#ifdef CONFIG_DEBUG_TCG
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

    INSN_CLZ       = 0x016f0f10,
    INSN_RBIT      = 0x06ff0f30,

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

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_debug_assert(addend == 0);

    if (type == R_ARM_PC24) {
        return reloc_pc24(code_ptr, (const tcg_insn_unit *)value);
    } else if (type == R_ARM_PC13) {
        return reloc_pc13(code_ptr, (const tcg_insn_unit *)value);
    } else {
        g_assert_not_reached();
    }
}

#define TCG_CT_CONST_ARM  0x100
#define TCG_CT_CONST_INV  0x200
#define TCG_CT_CONST_NEG  0x400
#define TCG_CT_CONST_ZERO 0x800

#define ALL_GENERAL_REGS  0xffffu

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

static inline void tcg_out_b(TCGContext *s, int cond, int32_t offset)
{
    tcg_out32(s, (cond << 28) | 0x0a000000 |
                    (((offset - 8) >> 2) & 0x00ffffff));
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
    tcg_out32(s, INSN_NOP);
}

static inline void tcg_out_mov_reg(TCGContext *s, int cond, int rd, int rm)
{
    /* Simple reg-reg move, optimising out the 'do nothing' case */
    if (rd != rm) {
        tcg_out_dat_reg(s, cond, ARITH_MOV, rd, 0, rm, SHIFT_IMM_LSL(0));
    }
}

static inline void tcg_out_bx(TCGContext *s, int cond, TCGReg rn)
{
    /* Unless the C portion of QEMU is compiled as thumb, we don't
       actually need true BX semantics; merely a branch to an address
       held in a register.  */
    if (use_armv5t_instructions) {
        tcg_out32(s, (cond << 28) | 0x012fff10 | rn);
    } else {
        tcg_out_mov_reg(s, cond, TCG_REG_PC, rn);
    }
}

static inline void tcg_out_dat_imm(TCGContext *s,
                int cond, int opc, int rd, int rn, int im)
{
    tcg_out32(s, (cond << 28) | (1 << 25) | opc |
                    (rn << 16) | (rd << 12) | im);
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

static inline void tcg_out_ldrd_rwb(TCGContext *s, int cond, TCGReg rt,
                                    TCGReg rn, TCGReg rm)
{
    tcg_out_memop_r(s, cond, INSN_LDRD_REG, rt, rn, rm, 1, 1, 1);
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

static void tcg_out_movi_pool(TCGContext *s, int cond, int rd, uint32_t arg)
{
    new_pool_label(s, arg, R_ARM_PC13, s->code_ptr, 0);
    tcg_out_ld32_12(s, cond, rd, TCG_REG_PC, 0);
}

static void tcg_out_movi32(TCGContext *s, int cond, int rd, uint32_t arg)
{
    int rot, diff, opc, sh1, sh2;
    uint32_t tt0, tt1, tt2;

    /* Check a single MOV/MVN before anything else.  */
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

    /* Check for a pc-relative address.  This will usually be the TB,
       or within the TB, which is immediately before the code block.  */
    diff = tcg_pcrel_diff(s, (void *)arg) - 8;
    if (diff >= 0) {
        rot = encode_imm(diff);
        if (rot >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_ADD, rd, TCG_REG_PC,
                            rotl(diff, rot) | (rot << 7));
            return;
        }
    } else {
        rot = encode_imm(-diff);
        if (rot >= 0) {
            tcg_out_dat_imm(s, cond, ARITH_SUB, rd, TCG_REG_PC,
                            rotl(-diff, rot) | (rot << 7));
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

static inline void tcg_out_dat_rI(TCGContext *s, int cond, int opc, TCGArg dst,
                                  TCGArg lhs, TCGArg rhs, int rhs_is_const)
{
    /* Emit either the reg,imm or reg,reg form of a data-processing insn.
     * rhs must satisfy the "rI" constraint.
     */
    if (rhs_is_const) {
        int rot = encode_imm(rhs);
        tcg_debug_assert(rot >= 0);
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
            tcg_debug_assert(rot >= 0);
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
            tcg_debug_assert(rot >= 0);
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

static inline void tcg_out_extract(TCGContext *s, int cond, TCGReg rd,
                                   TCGArg a1, int ofs, int len)
{
    /* ubfx */
    tcg_out32(s, 0x07e00050 | (cond << 28) | (rd << 12) | a1
              | (ofs << 7) | ((len - 1) << 16));
}

static inline void tcg_out_sextract(TCGContext *s, int cond, TCGReg rd,
                                    TCGArg a1, int ofs, int len)
{
    /* sbfx */
    tcg_out32(s, 0x07a00050 | (cond << 28) | (rd << 12) | a1
              | (ofs << 7) | ((len - 1) << 16));
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
static void tcg_out_goto(TCGContext *s, int cond, const tcg_insn_unit *addr)
{
    intptr_t addri = (intptr_t)addr;
    ptrdiff_t disp = tcg_pcrel_diff(s, addr);

    if ((addri & 1) == 0 && disp - 8 < 0x01fffffd && disp - 8 > -0x01fffffd) {
        tcg_out_b(s, cond, disp);
        return;
    }
    tcg_out_movi_pool(s, cond, TCG_REG_PC, addri);
}

/* The call case is mostly used for helpers - so it's not unreasonable
 * for them to be beyond branch range */
static void tcg_out_call(TCGContext *s, const tcg_insn_unit *addr)
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
        /* ??? Know that movi_pool emits exactly 1 insn.  */
        tcg_out_dat_imm(s, COND_AL, ARITH_ADD, TCG_REG_R14, TCG_REG_PC, 0);
        tcg_out_movi_pool(s, COND_AL, TCG_REG_PC, addri);
    }
}

static inline void tcg_out_goto_label(TCGContext *s, int cond, TCGLabel *l)
{
    if (l->has_value) {
        tcg_out_goto(s, cond, l->u.value_ptr);
    } else {
        tcg_out_reloc(s, s->code_ptr, R_ARM_PC24, l, 0);
        tcg_out_b(s, cond, 0);
    }
}

static inline void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    if (use_armv7_instructions) {
        tcg_out32(s, INSN_DMB_ISH);
    } else if (use_armv6_instructions) {
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

#ifdef CONFIG_SOFTMMU
#include "../tcg-ldst.c.inc"

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
    int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
    int table_off = fast_off + offsetof(CPUTLBDescFast, table);
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_bits = get_alignment_bits(opc);

    /*
     * We don't support inline unaligned acceses, but we can easily
     * support overalignment checks.
     */
    if (a_bits < s_bits) {
        a_bits = s_bits;
    }

    /* Load env_tlb(env)->f[mmu_idx].{mask,table} into {r0,r1}.  */
    if (use_armv6_instructions) {
        tcg_out_ldrd_8(s, COND_AL, TCG_REG_R0, TCG_AREG0, fast_off);
    } else {
        tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_R0, TCG_AREG0, mask_off);
        tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_R1, TCG_AREG0, table_off);
    }

    /* Extract the tlb index from the address into R0.  */
    tcg_out_dat_reg(s, COND_AL, ARITH_AND, TCG_REG_R0, TCG_REG_R0, addrlo,
                    SHIFT_IMM_LSR(TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS));

    /*
     * Add the tlb_table pointer, creating the CPUTLBEntry address in R1.
     * Load the tlb comparator into R2/R3 and the fast path addend into R1.
     */
    if (cmp_off == 0) {
        if (use_armv6_instructions && TARGET_LONG_BITS == 64) {
            tcg_out_ldrd_rwb(s, COND_AL, TCG_REG_R2, TCG_REG_R1, TCG_REG_R0);
        } else {
            tcg_out_ld32_rwb(s, COND_AL, TCG_REG_R2, TCG_REG_R1, TCG_REG_R0);
        }
    } else {
        tcg_out_dat_reg(s, COND_AL, ARITH_ADD,
                        TCG_REG_R1, TCG_REG_R1, TCG_REG_R0, 0);
        if (use_armv6_instructions && TARGET_LONG_BITS == 64) {
            tcg_out_ldrd_8(s, COND_AL, TCG_REG_R2, TCG_REG_R1, cmp_off);
        } else {
            tcg_out_ld32_12(s, COND_AL, TCG_REG_R2, TCG_REG_R1, cmp_off);
        }
    }
    if (!use_armv6_instructions && TARGET_LONG_BITS == 64) {
        tcg_out_ld32_12(s, COND_AL, TCG_REG_R3, TCG_REG_R1, cmp_off + 4);
    }

    /* Load the tlb addend.  */
    tcg_out_ld32_12(s, COND_AL, TCG_REG_R1, TCG_REG_R1,
                    offsetof(CPUTLBEntry, addend));

    /*
     * Check alignment, check comparators.
     * Do this in no more than 3 insns.  Use MOVW for v7, if possible,
     * to reduce the number of sequential conditional instructions.
     * Almost all guests have at least 4k pages, which means that we need
     * to clear at least 9 bits even for an 8-byte memory, which means it
     * isn't worth checking for an immediate operand for BIC.
     */
    if (use_armv7_instructions && TARGET_PAGE_BITS <= 16) {
        tcg_target_ulong mask = ~(TARGET_PAGE_MASK | ((1 << a_bits) - 1));

        tcg_out_movi32(s, COND_AL, TCG_REG_TMP, mask);
        tcg_out_dat_reg(s, COND_AL, ARITH_BIC, TCG_REG_TMP,
                        addrlo, TCG_REG_TMP, 0);
        tcg_out_dat_reg(s, COND_AL, ARITH_CMP, 0, TCG_REG_R2, TCG_REG_TMP, 0);
    } else {
        if (a_bits) {
            tcg_out_dat_imm(s, COND_AL, ARITH_TST, 0, addrlo,
                            (1 << a_bits) - 1);
        }
        tcg_out_dat_reg(s, COND_AL, ARITH_MOV, TCG_REG_TMP, 0, addrlo,
                        SHIFT_IMM_LSR(TARGET_PAGE_BITS));
        tcg_out_dat_reg(s, (a_bits ? COND_EQ : COND_AL), ARITH_CMP,
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
static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOpIdx oi,
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
    TCGMemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);
    void *func;

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

    /* For armv6 we can use the canonical unsigned helpers and minimize
       icache usage.  For pre-armv6, use the signed helpers since we do
       not have a single insn sign-extend.  */
    if (use_armv6_instructions) {
        func = qemu_ld_helpers[opc & (MO_BSWAP | MO_SIZE)];
    } else {
        func = qemu_ld_helpers[opc & (MO_BSWAP | MO_SSIZE)];
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
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg argreg, datalo, datahi;
    TCGMemOpIdx oi = lb->oi;
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
    tcg_out_goto(s, COND_AL, qemu_st_helpers[opc & (MO_BSWAP | MO_SIZE)]);
    return true;
}
#endif /* SOFTMMU */

static inline void tcg_out_qemu_ld_index(TCGContext *s, MemOp opc,
                                         TCGReg datalo, TCGReg datahi,
                                         TCGReg addrlo, TCGReg addend)
{
    MemOp bswap = opc & MO_BSWAP;

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

static inline void tcg_out_qemu_ld_direct(TCGContext *s, MemOp opc,
                                          TCGReg datalo, TCGReg datahi,
                                          TCGReg addrlo)
{
    MemOp bswap = opc & MO_BSWAP;

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
    TCGMemOpIdx oi;
    MemOp opc;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    TCGReg addend;
    tcg_insn_unit *label_ptr;
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
    tcg_out_bl(s, COND_NE, 0);

    tcg_out_qemu_ld_index(s, opc, datalo, datahi, addrlo, addend);

    add_qemu_ldst_label(s, true, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    if (guest_base) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP, guest_base);
        tcg_out_qemu_ld_index(s, opc, datalo, datahi, addrlo, TCG_REG_TMP);
    } else {
        tcg_out_qemu_ld_direct(s, opc, datalo, datahi, addrlo);
    }
#endif
}

static inline void tcg_out_qemu_st_index(TCGContext *s, int cond, MemOp opc,
                                         TCGReg datalo, TCGReg datahi,
                                         TCGReg addrlo, TCGReg addend)
{
    MemOp bswap = opc & MO_BSWAP;

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

static inline void tcg_out_qemu_st_direct(TCGContext *s, MemOp opc,
                                          TCGReg datalo, TCGReg datahi,
                                          TCGReg addrlo)
{
    MemOp bswap = opc & MO_BSWAP;

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
    TCGMemOpIdx oi;
    MemOp opc;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    TCGReg addend;
    tcg_insn_unit *label_ptr;
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

    tcg_out_qemu_st_index(s, COND_EQ, opc, datalo, datahi, addrlo, addend);

    /* The conditional call must come last, as we're going to return here.  */
    label_ptr = s->code_ptr;
    tcg_out_bl(s, COND_NE, 0);

    add_qemu_ldst_label(s, false, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#else /* !CONFIG_SOFTMMU */
    if (guest_base) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP, guest_base);
        tcg_out_qemu_st_index(s, COND_AL, opc, datalo,
                              datahi, addrlo, TCG_REG_TMP);
    } else {
        tcg_out_qemu_st_direct(s, opc, datalo, datahi, addrlo);
    }
#endif
}

static void tcg_out_epilogue(TCGContext *s);

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                const TCGArg *args, const int *const_args)
{
    TCGArg a0, a1, a2, a3, a4, a5;
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R0, args[0]);
        tcg_out_epilogue(s);
        break;
    case INDEX_op_goto_tb:
        {
            /* Indirect jump method */
            intptr_t ptr, dif, dil;
            TCGReg base = TCG_REG_PC;

            tcg_debug_assert(s->tb_jmp_insn_offset == 0);
            ptr = (intptr_t)tcg_splitwx_to_rx(s->tb_jmp_target_addr + args[0]);
            dif = tcg_pcrel_diff(s, (void *)ptr) - 8;
            dil = sextract32(dif, 0, 12);
            if (dif != dil) {
                /* The TB is close, but outside the 12 bits addressable by
                   the load.  We can extend this to 20 bits with a sub of a
                   shifted immediate from pc.  In the vastly unlikely event
                   the code requires more than 1MB, we'll use 2 insns and
                   be no worse off.  */
                base = TCG_REG_R0;
                tcg_out_movi32(s, COND_AL, base, ptr - dil);
            }
            tcg_out_ld32_12(s, COND_AL, TCG_REG_PC, base, dil);
            set_jmp_reset_offset(s, args[0]);
        }
        break;
    case INDEX_op_goto_ptr:
        tcg_out_bx(s, COND_AL, args[0]);
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
        return TARGET_LONG_BITS == 32 ? C_O2_I1(r, r, l) : C_O2_I2(r, r, l, l);
    case INDEX_op_qemu_st_i32:
        return TARGET_LONG_BITS == 32 ? C_O0_I2(s, s) : C_O0_I3(s, s, s);
    case INDEX_op_qemu_st_i64:
        return TARGET_LONG_BITS == 32 ? C_O0_I3(s, s, s) : C_O0_I4(s, s, s, s);

    default:
        g_assert_not_reached();
    }
}

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

    tcg_target_available_regs[TCG_TYPE_I32] = 0xffff;

    tcg_target_call_clobber_regs = 0;
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R12);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R14);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_PC);
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

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    return false;
}

static inline bool tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg arg)
{
    tcg_out_mov_reg(s, COND_AL, ret, arg);
    return true;
}

static inline void tcg_out_movi(TCGContext *s, TCGType type,
                                TCGReg ret, tcg_target_long arg)
{
    tcg_out_movi32(s, COND_AL, ret, arg);
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
    tcg_out32(s, (COND_AL << 28) | 0x092d4ff0);

    /* Reserve callee argument and tcg temp space.  */
    tcg_out_dat_rI(s, COND_AL, ARITH_SUB, TCG_REG_CALL_STACK,
                   TCG_REG_CALL_STACK, STACK_ADDEND, 1);
    tcg_set_frame(s, TCG_REG_CALL_STACK, TCG_STATIC_CALL_ARGS_SIZE,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

    tcg_out_bx(s, COND_AL, tcg_target_call_iarg_regs[1]);

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

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
