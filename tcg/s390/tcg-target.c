/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright (c) 2010 Richard Henderson <rth@twiddle.net>
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

#include "tcg-be-ldst.h"

/* We only support generating code for 64-bit mode.  */
#if TCG_TARGET_REG_BITS != 64
#error "unsupported code generation mode"
#endif

#include "elf.h"

/* ??? The translation blocks produced by TCG are generally small enough to
   be entirely reachable with a 16-bit displacement.  Leaving the option for
   a 32-bit displacement here Just In Case.  */
#define USE_LONG_BRANCHES 0

#define TCG_CT_CONST_MULI  0x100
#define TCG_CT_CONST_ORI   0x200
#define TCG_CT_CONST_XORI  0x400
#define TCG_CT_CONST_CMPI  0x800

/* Several places within the instruction set 0 means "no register"
   rather than TCG_REG_R0.  */
#define TCG_REG_NONE    0

/* A scratch register that may be be used throughout the backend.  */
#define TCG_TMP0        TCG_REG_R14

#ifdef CONFIG_USE_GUEST_BASE
#define TCG_GUEST_BASE_REG TCG_REG_R13
#else
#define TCG_GUEST_BASE_REG TCG_REG_R0
#endif

#ifndef GUEST_BASE
#define GUEST_BASE 0
#endif


/* All of the following instructions are prefixed with their instruction
   format, and are defined as 8- or 16-bit quantities, even when the two
   halves of the 16-bit quantity may appear 32 bits apart in the insn.
   This makes it easy to copy the values from the tables in Appendix B.  */
typedef enum S390Opcode {
    RIL_AFI     = 0xc209,
    RIL_AGFI    = 0xc208,
    RIL_ALFI    = 0xc20b,
    RIL_ALGFI   = 0xc20a,
    RIL_BRASL   = 0xc005,
    RIL_BRCL    = 0xc004,
    RIL_CFI     = 0xc20d,
    RIL_CGFI    = 0xc20c,
    RIL_CLFI    = 0xc20f,
    RIL_CLGFI   = 0xc20e,
    RIL_IIHF    = 0xc008,
    RIL_IILF    = 0xc009,
    RIL_LARL    = 0xc000,
    RIL_LGFI    = 0xc001,
    RIL_LGRL    = 0xc408,
    RIL_LLIHF   = 0xc00e,
    RIL_LLILF   = 0xc00f,
    RIL_LRL     = 0xc40d,
    RIL_MSFI    = 0xc201,
    RIL_MSGFI   = 0xc200,
    RIL_NIHF    = 0xc00a,
    RIL_NILF    = 0xc00b,
    RIL_OIHF    = 0xc00c,
    RIL_OILF    = 0xc00d,
    RIL_SLFI    = 0xc205,
    RIL_SLGFI   = 0xc204,
    RIL_XIHF    = 0xc006,
    RIL_XILF    = 0xc007,

    RI_AGHI     = 0xa70b,
    RI_AHI      = 0xa70a,
    RI_BRC      = 0xa704,
    RI_IIHH     = 0xa500,
    RI_IIHL     = 0xa501,
    RI_IILH     = 0xa502,
    RI_IILL     = 0xa503,
    RI_LGHI     = 0xa709,
    RI_LLIHH    = 0xa50c,
    RI_LLIHL    = 0xa50d,
    RI_LLILH    = 0xa50e,
    RI_LLILL    = 0xa50f,
    RI_MGHI     = 0xa70d,
    RI_MHI      = 0xa70c,
    RI_NIHH     = 0xa504,
    RI_NIHL     = 0xa505,
    RI_NILH     = 0xa506,
    RI_NILL     = 0xa507,
    RI_OIHH     = 0xa508,
    RI_OIHL     = 0xa509,
    RI_OILH     = 0xa50a,
    RI_OILL     = 0xa50b,

    RIE_CGIJ    = 0xec7c,
    RIE_CGRJ    = 0xec64,
    RIE_CIJ     = 0xec7e,
    RIE_CLGRJ   = 0xec65,
    RIE_CLIJ    = 0xec7f,
    RIE_CLGIJ   = 0xec7d,
    RIE_CLRJ    = 0xec77,
    RIE_CRJ     = 0xec76,
    RIE_RISBG   = 0xec55,

    RRE_AGR     = 0xb908,
    RRE_ALGR    = 0xb90a,
    RRE_ALCR    = 0xb998,
    RRE_ALCGR   = 0xb988,
    RRE_CGR     = 0xb920,
    RRE_CLGR    = 0xb921,
    RRE_DLGR    = 0xb987,
    RRE_DLR     = 0xb997,
    RRE_DSGFR   = 0xb91d,
    RRE_DSGR    = 0xb90d,
    RRE_LGBR    = 0xb906,
    RRE_LCGR    = 0xb903,
    RRE_LGFR    = 0xb914,
    RRE_LGHR    = 0xb907,
    RRE_LGR     = 0xb904,
    RRE_LLGCR   = 0xb984,
    RRE_LLGFR   = 0xb916,
    RRE_LLGHR   = 0xb985,
    RRE_LRVR    = 0xb91f,
    RRE_LRVGR   = 0xb90f,
    RRE_LTGR    = 0xb902,
    RRE_MLGR    = 0xb986,
    RRE_MSGR    = 0xb90c,
    RRE_MSR     = 0xb252,
    RRE_NGR     = 0xb980,
    RRE_OGR     = 0xb981,
    RRE_SGR     = 0xb909,
    RRE_SLGR    = 0xb90b,
    RRE_SLBR    = 0xb999,
    RRE_SLBGR   = 0xb989,
    RRE_XGR     = 0xb982,

    RRF_LOCR    = 0xb9f2,
    RRF_LOCGR   = 0xb9e2,

    RR_AR       = 0x1a,
    RR_ALR      = 0x1e,
    RR_BASR     = 0x0d,
    RR_BCR      = 0x07,
    RR_CLR      = 0x15,
    RR_CR       = 0x19,
    RR_DR       = 0x1d,
    RR_LCR      = 0x13,
    RR_LR       = 0x18,
    RR_LTR      = 0x12,
    RR_NR       = 0x14,
    RR_OR       = 0x16,
    RR_SR       = 0x1b,
    RR_SLR      = 0x1f,
    RR_XR       = 0x17,

    RSY_RLL     = 0xeb1d,
    RSY_RLLG    = 0xeb1c,
    RSY_SLLG    = 0xeb0d,
    RSY_SRAG    = 0xeb0a,
    RSY_SRLG    = 0xeb0c,

    RS_SLL      = 0x89,
    RS_SRA      = 0x8a,
    RS_SRL      = 0x88,

    RXY_AG      = 0xe308,
    RXY_AY      = 0xe35a,
    RXY_CG      = 0xe320,
    RXY_CY      = 0xe359,
    RXY_LAY     = 0xe371,
    RXY_LB      = 0xe376,
    RXY_LG      = 0xe304,
    RXY_LGB     = 0xe377,
    RXY_LGF     = 0xe314,
    RXY_LGH     = 0xe315,
    RXY_LHY     = 0xe378,
    RXY_LLGC    = 0xe390,
    RXY_LLGF    = 0xe316,
    RXY_LLGH    = 0xe391,
    RXY_LMG     = 0xeb04,
    RXY_LRV     = 0xe31e,
    RXY_LRVG    = 0xe30f,
    RXY_LRVH    = 0xe31f,
    RXY_LY      = 0xe358,
    RXY_STCY    = 0xe372,
    RXY_STG     = 0xe324,
    RXY_STHY    = 0xe370,
    RXY_STMG    = 0xeb24,
    RXY_STRV    = 0xe33e,
    RXY_STRVG   = 0xe32f,
    RXY_STRVH   = 0xe33f,
    RXY_STY     = 0xe350,

    RX_A        = 0x5a,
    RX_C        = 0x59,
    RX_L        = 0x58,
    RX_LA       = 0x41,
    RX_LH       = 0x48,
    RX_ST       = 0x50,
    RX_STC      = 0x42,
    RX_STH      = 0x40,
} S390Opcode;

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%r0", "%r1", "%r2", "%r3", "%r4", "%r5", "%r6", "%r7",
    "%r8", "%r9", "%r10" "%r11" "%r12" "%r13" "%r14" "%r15"
};
#endif

/* Since R6 is a potential argument register, choose it last of the
   call-saved registers.  Likewise prefer the call-clobbered registers
   in reverse order to maximize the chance of avoiding the arguments.  */
static const int tcg_target_reg_alloc_order[] = {
    /* Call saved registers.  */
    TCG_REG_R13,
    TCG_REG_R12,
    TCG_REG_R11,
    TCG_REG_R10,
    TCG_REG_R9,
    TCG_REG_R8,
    TCG_REG_R7,
    TCG_REG_R6,
    /* Call clobbered registers.  */
    TCG_REG_R14,
    TCG_REG_R0,
    TCG_REG_R1,
    /* Argument registers, in reverse order of allocation.  */
    TCG_REG_R5,
    TCG_REG_R4,
    TCG_REG_R3,
    TCG_REG_R2,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_R2,
};

#define S390_CC_EQ      8
#define S390_CC_LT      4
#define S390_CC_GT      2
#define S390_CC_OV      1
#define S390_CC_NE      (S390_CC_LT | S390_CC_GT)
#define S390_CC_LE      (S390_CC_LT | S390_CC_EQ)
#define S390_CC_GE      (S390_CC_GT | S390_CC_EQ)
#define S390_CC_NEVER   0
#define S390_CC_ALWAYS  15

/* Condition codes that result from a COMPARE and COMPARE LOGICAL.  */
static const uint8_t tcg_cond_to_s390_cond[] = {
    [TCG_COND_EQ]  = S390_CC_EQ,
    [TCG_COND_NE]  = S390_CC_NE,
    [TCG_COND_LT]  = S390_CC_LT,
    [TCG_COND_LE]  = S390_CC_LE,
    [TCG_COND_GT]  = S390_CC_GT,
    [TCG_COND_GE]  = S390_CC_GE,
    [TCG_COND_LTU] = S390_CC_LT,
    [TCG_COND_LEU] = S390_CC_LE,
    [TCG_COND_GTU] = S390_CC_GT,
    [TCG_COND_GEU] = S390_CC_GE,
};

/* Condition codes that result from a LOAD AND TEST.  Here, we have no
   unsigned instruction variation, however since the test is vs zero we
   can re-map the outcomes appropriately.  */
static const uint8_t tcg_cond_to_ltr_cond[] = {
    [TCG_COND_EQ]  = S390_CC_EQ,
    [TCG_COND_NE]  = S390_CC_NE,
    [TCG_COND_LT]  = S390_CC_LT,
    [TCG_COND_LE]  = S390_CC_LE,
    [TCG_COND_GT]  = S390_CC_GT,
    [TCG_COND_GE]  = S390_CC_GE,
    [TCG_COND_LTU] = S390_CC_NEVER,
    [TCG_COND_LEU] = S390_CC_EQ,
    [TCG_COND_GTU] = S390_CC_NE,
    [TCG_COND_GEU] = S390_CC_ALWAYS,
};

#ifdef CONFIG_SOFTMMU
static void * const qemu_ld_helpers[16] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_SB]   = helper_ret_ldsb_mmu,
    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LESW] = helper_le_ldsw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
    [MO_LESL] = helper_le_ldsl_mmu,
    [MO_LEQ]  = helper_le_ldq_mmu,
    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BESW] = helper_be_ldsw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
    [MO_BESL] = helper_be_ldsl_mmu,
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
#endif

static tcg_insn_unit *tb_ret_addr;

/* A list of relevant facilities used by this translator.  Some of these
   are required for proper operation, and these are checked at startup.  */

#define FACILITY_ZARCH_ACTIVE	(1ULL << (63 - 2))
#define FACILITY_LONG_DISP	(1ULL << (63 - 18))
#define FACILITY_EXT_IMM	(1ULL << (63 - 21))
#define FACILITY_GEN_INST_EXT	(1ULL << (63 - 34))
#define FACILITY_LOAD_ON_COND   (1ULL << (63 - 45))

static uint64_t facilities;

static void patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    intptr_t pcrel2 = (tcg_insn_unit *)value - (code_ptr - 1);
    assert(addend == -2);

    switch (type) {
    case R_390_PC16DBL:
        assert(pcrel2 == (int16_t)pcrel2);
        tcg_patch16(code_ptr, pcrel2);
        break;
    case R_390_PC32DBL:
        assert(pcrel2 == (int32_t)pcrel2);
        tcg_patch32(code_ptr, pcrel2);
        break;
    default:
        tcg_abort();
        break;
    }
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str = *pct_str;

    switch (ct_str[0]) {
    case 'r':                  /* all registers */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        break;
    case 'R':                  /* not R0 */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        break;
    case 'L':                  /* qemu_ld/st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffff);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R2);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R3);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R4);
        break;
    case 'a':                  /* force R2 for division */
        ct->ct |= TCG_CT_REG;
        tcg_regset_clear(ct->u.regs);
        tcg_regset_set_reg(ct->u.regs, TCG_REG_R2);
        break;
    case 'b':                  /* force R3 for division */
        ct->ct |= TCG_CT_REG;
        tcg_regset_clear(ct->u.regs);
        tcg_regset_set_reg(ct->u.regs, TCG_REG_R3);
        break;
    case 'K':
        ct->ct |= TCG_CT_CONST_MULI;
        break;
    case 'O':
        ct->ct |= TCG_CT_CONST_ORI;
        break;
    case 'X':
        ct->ct |= TCG_CT_CONST_XORI;
        break;
    case 'C':
        ct->ct |= TCG_CT_CONST_CMPI;
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;

    return 0;
}

/* Immediates to be used with logical OR.  This is an optimization only,
   since a full 64-bit immediate OR can always be performed with 4 sequential
   OI[LH][LH] instructions.  What we're looking for is immediates that we
   can load efficiently, and the immediate load plus the reg-reg OR is
   smaller than the sequential OI's.  */

static int tcg_match_ori(TCGType type, tcg_target_long val)
{
    if (facilities & FACILITY_EXT_IMM) {
        if (type == TCG_TYPE_I32) {
            /* All 32-bit ORs can be performed with 1 48-bit insn.  */
            return 1;
        }
    }

    /* Look for negative values.  These are best to load with LGHI.  */
    if (val < 0) {
        if (val == (int16_t)val) {
            return 0;
        }
        if (facilities & FACILITY_EXT_IMM) {
            if (val == (int32_t)val) {
                return 0;
            }
        }
    }

    return 1;
}

/* Immediates to be used with logical XOR.  This is almost, but not quite,
   only an optimization.  XOR with immediate is only supported with the
   extended-immediate facility.  That said, there are a few patterns for
   which it is better to load the value into a register first.  */

static int tcg_match_xori(TCGType type, tcg_target_long val)
{
    if ((facilities & FACILITY_EXT_IMM) == 0) {
        return 0;
    }

    if (type == TCG_TYPE_I32) {
        /* All 32-bit XORs can be performed with 1 48-bit insn.  */
        return 1;
    }

    /* Look for negative values.  These are best to load with LGHI.  */
    if (val < 0 && val == (int32_t)val) {
        return 0;
    }

    return 1;
}

/* Imediates to be used with comparisons.  */

static int tcg_match_cmpi(TCGType type, tcg_target_long val)
{
    if (facilities & FACILITY_EXT_IMM) {
        /* The COMPARE IMMEDIATE instruction is available.  */
        if (type == TCG_TYPE_I32) {
            /* We have a 32-bit immediate and can compare against anything.  */
            return 1;
        } else {
            /* ??? We have no insight here into whether the comparison is
               signed or unsigned.  The COMPARE IMMEDIATE insn uses a 32-bit
               signed immediate, and the COMPARE LOGICAL IMMEDIATE insn uses
               a 32-bit unsigned immediate.  If we were to use the (semi)
               obvious "val == (int32_t)val" we would be enabling unsigned
               comparisons vs very large numbers.  The only solution is to
               take the intersection of the ranges.  */
            /* ??? Another possible solution is to simply lie and allow all
               constants here and force the out-of-range values into a temp
               register in tgen_cmp when we have knowledge of the actual
               comparison code in use.  */
            return val >= 0 && val <= 0x7fffffff;
        }
    } else {
        /* Only the LOAD AND TEST instruction is available.  */
        return val == 0;
    }
}

/* Test if a constant matches the constraint. */
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;

    if (ct & TCG_CT_CONST) {
        return 1;
    }

    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    /* The following are mutually exclusive.  */
    if (ct & TCG_CT_CONST_MULI) {
        /* Immediates that may be used with multiply.  If we have the
           general-instruction-extensions, then we have MULTIPLY SINGLE
           IMMEDIATE with a signed 32-bit, otherwise we have only
           MULTIPLY HALFWORD IMMEDIATE, with a signed 16-bit.  */
        if (facilities & FACILITY_GEN_INST_EXT) {
            return val == (int32_t)val;
        } else {
            return val == (int16_t)val;
        }
    } else if (ct & TCG_CT_CONST_ORI) {
        return tcg_match_ori(type, val);
    } else if (ct & TCG_CT_CONST_XORI) {
        return tcg_match_xori(type, val);
    } else if (ct & TCG_CT_CONST_CMPI) {
        return tcg_match_cmpi(type, val);
    }

    return 0;
}

/* Emit instructions according to the given instruction format.  */

static void tcg_out_insn_RR(TCGContext *s, S390Opcode op, TCGReg r1, TCGReg r2)
{
    tcg_out16(s, (op << 8) | (r1 << 4) | r2);
}

static void tcg_out_insn_RRE(TCGContext *s, S390Opcode op,
                             TCGReg r1, TCGReg r2)
{
    tcg_out32(s, (op << 16) | (r1 << 4) | r2);
}

static void tcg_out_insn_RRF(TCGContext *s, S390Opcode op,
                             TCGReg r1, TCGReg r2, int m3)
{
    tcg_out32(s, (op << 16) | (m3 << 12) | (r1 << 4) | r2);
}

static void tcg_out_insn_RI(TCGContext *s, S390Opcode op, TCGReg r1, int i2)
{
    tcg_out32(s, (op << 16) | (r1 << 20) | (i2 & 0xffff));
}

static void tcg_out_insn_RIL(TCGContext *s, S390Opcode op, TCGReg r1, int i2)
{
    tcg_out16(s, op | (r1 << 4));
    tcg_out32(s, i2);
}

static void tcg_out_insn_RS(TCGContext *s, S390Opcode op, TCGReg r1,
                            TCGReg b2, TCGReg r3, int disp)
{
    tcg_out32(s, (op << 24) | (r1 << 20) | (r3 << 16) | (b2 << 12)
              | (disp & 0xfff));
}

static void tcg_out_insn_RSY(TCGContext *s, S390Opcode op, TCGReg r1,
                             TCGReg b2, TCGReg r3, int disp)
{
    tcg_out16(s, (op & 0xff00) | (r1 << 4) | r3);
    tcg_out32(s, (op & 0xff) | (b2 << 28)
              | ((disp & 0xfff) << 16) | ((disp & 0xff000) >> 4));
}

#define tcg_out_insn_RX   tcg_out_insn_RS
#define tcg_out_insn_RXY  tcg_out_insn_RSY

/* Emit an opcode with "type-checking" of the format.  */
#define tcg_out_insn(S, FMT, OP, ...) \
    glue(tcg_out_insn_,FMT)(S, glue(glue(FMT,_),OP), ## __VA_ARGS__)


/* emit 64-bit shifts */
static void tcg_out_sh64(TCGContext* s, S390Opcode op, TCGReg dest,
                         TCGReg src, TCGReg sh_reg, int sh_imm)
{
    tcg_out_insn_RSY(s, op, dest, sh_reg, src, sh_imm);
}

/* emit 32-bit shifts */
static void tcg_out_sh32(TCGContext* s, S390Opcode op, TCGReg dest,
                         TCGReg sh_reg, int sh_imm)
{
    tcg_out_insn_RS(s, op, dest, sh_reg, 0, sh_imm);
}

static void tcg_out_mov(TCGContext *s, TCGType type, TCGReg dst, TCGReg src)
{
    if (src != dst) {
        if (type == TCG_TYPE_I32) {
            tcg_out_insn(s, RR, LR, dst, src);
        } else {
            tcg_out_insn(s, RRE, LGR, dst, src);
        }
    }
}

/* load a register with an immediate value */
static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long sval)
{
    static const S390Opcode lli_insns[4] = {
        RI_LLILL, RI_LLILH, RI_LLIHL, RI_LLIHH
    };

    tcg_target_ulong uval = sval;
    int i;

    if (type == TCG_TYPE_I32) {
        uval = (uint32_t)sval;
        sval = (int32_t)sval;
    }

    /* Try all 32-bit insns that can load it in one go.  */
    if (sval >= -0x8000 && sval < 0x8000) {
        tcg_out_insn(s, RI, LGHI, ret, sval);
        return;
    }

    for (i = 0; i < 4; i++) {
        tcg_target_long mask = 0xffffull << i*16;
        if ((uval & mask) == uval) {
            tcg_out_insn_RI(s, lli_insns[i], ret, uval >> i*16);
            return;
        }
    }

    /* Try all 48-bit insns that can load it in one go.  */
    if (facilities & FACILITY_EXT_IMM) {
        if (sval == (int32_t)sval) {
            tcg_out_insn(s, RIL, LGFI, ret, sval);
            return;
        }
        if (uval <= 0xffffffff) {
            tcg_out_insn(s, RIL, LLILF, ret, uval);
            return;
        }
        if ((uval & 0xffffffff) == 0) {
            tcg_out_insn(s, RIL, LLIHF, ret, uval >> 31 >> 1);
            return;
        }
    }

    /* Try for PC-relative address load.  */
    if ((sval & 1) == 0) {
        ptrdiff_t off = tcg_pcrel_diff(s, (void *)sval) >> 1;
        if (off == (int32_t)off) {
            tcg_out_insn(s, RIL, LARL, ret, off);
            return;
        }
    }

    /* If extended immediates are not present, then we may have to issue
       several instructions to load the low 32 bits.  */
    if (!(facilities & FACILITY_EXT_IMM)) {
        /* A 32-bit unsigned value can be loaded in 2 insns.  And given
           that the lli_insns loop above did not succeed, we know that
           both insns are required.  */
        if (uval <= 0xffffffff) {
            tcg_out_insn(s, RI, LLILL, ret, uval);
            tcg_out_insn(s, RI, IILH, ret, uval >> 16);
            return;
        }

        /* If all high bits are set, the value can be loaded in 2 or 3 insns.
           We first want to make sure that all the high bits get set.  With
           luck the low 16-bits can be considered negative to perform that for
           free, otherwise we load an explicit -1.  */
        if (sval >> 31 >> 1 == -1) {
            if (uval & 0x8000) {
                tcg_out_insn(s, RI, LGHI, ret, uval);
            } else {
                tcg_out_insn(s, RI, LGHI, ret, -1);
                tcg_out_insn(s, RI, IILL, ret, uval);
            }
            tcg_out_insn(s, RI, IILH, ret, uval >> 16);
            return;
        }
    }

    /* If we get here, both the high and low parts have non-zero bits.  */

    /* Recurse to load the lower 32-bits.  */
    tcg_out_movi(s, TCG_TYPE_I64, ret, uval & 0xffffffff);

    /* Insert data into the high 32-bits.  */
    uval = uval >> 31 >> 1;
    if (facilities & FACILITY_EXT_IMM) {
        if (uval < 0x10000) {
            tcg_out_insn(s, RI, IIHL, ret, uval);
        } else if ((uval & 0xffff) == 0) {
            tcg_out_insn(s, RI, IIHH, ret, uval >> 16);
        } else {
            tcg_out_insn(s, RIL, IIHF, ret, uval);
        }
    } else {
        if (uval & 0xffff) {
            tcg_out_insn(s, RI, IIHL, ret, uval);
        }
        if (uval & 0xffff0000) {
            tcg_out_insn(s, RI, IIHH, ret, uval >> 16);
        }
    }
}


/* Emit a load/store type instruction.  Inputs are:
   DATA:     The register to be loaded or stored.
   BASE+OFS: The effective address.
   OPC_RX:   If the operation has an RX format opcode (e.g. STC), otherwise 0.
   OPC_RXY:  The RXY format opcode for the operation (e.g. STCY).  */

static void tcg_out_mem(TCGContext *s, S390Opcode opc_rx, S390Opcode opc_rxy,
                        TCGReg data, TCGReg base, TCGReg index,
                        tcg_target_long ofs)
{
    if (ofs < -0x80000 || ofs >= 0x80000) {
        /* Combine the low 20 bits of the offset with the actual load insn;
           the high 44 bits must come from an immediate load.  */
        tcg_target_long low = ((ofs & 0xfffff) ^ 0x80000) - 0x80000;
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, ofs - low);
        ofs = low;

        /* If we were already given an index register, add it in.  */
        if (index != TCG_REG_NONE) {
            tcg_out_insn(s, RRE, AGR, TCG_TMP0, index);
        }
        index = TCG_TMP0;
    }

    if (opc_rx && ofs >= 0 && ofs < 0x1000) {
        tcg_out_insn_RX(s, opc_rx, data, base, index, ofs);
    } else {
        tcg_out_insn_RXY(s, opc_rxy, data, base, index, ofs);
    }
}


/* load data without address translation or endianness conversion */
static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg data,
                              TCGReg base, intptr_t ofs)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_mem(s, RX_L, RXY_LY, data, base, TCG_REG_NONE, ofs);
    } else {
        tcg_out_mem(s, 0, RXY_LG, data, base, TCG_REG_NONE, ofs);
    }
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg data,
                              TCGReg base, intptr_t ofs)
{
    if (type == TCG_TYPE_I32) {
        tcg_out_mem(s, RX_ST, RXY_STY, data, base, TCG_REG_NONE, ofs);
    } else {
        tcg_out_mem(s, 0, RXY_STG, data, base, TCG_REG_NONE, ofs);
    }
}

/* load data from an absolute host address */
static void tcg_out_ld_abs(TCGContext *s, TCGType type, TCGReg dest, void *abs)
{
    intptr_t addr = (intptr_t)abs;

    if ((facilities & FACILITY_GEN_INST_EXT) && !(addr & 1)) {
        ptrdiff_t disp = tcg_pcrel_diff(s, abs) >> 1;
        if (disp == (int32_t)disp) {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RIL, LRL, dest, disp);
            } else {
                tcg_out_insn(s, RIL, LGRL, dest, disp);
            }
            return;
        }
    }

    tcg_out_movi(s, TCG_TYPE_PTR, dest, addr & ~0xffff);
    tcg_out_ld(s, type, dest, dest, addr & 0xffff);
}

static inline void tcg_out_risbg(TCGContext *s, TCGReg dest, TCGReg src,
                                 int msb, int lsb, int ofs, int z)
{
    /* Format RIE-f */
    tcg_out16(s, (RIE_RISBG & 0xff00) | (dest << 4) | src);
    tcg_out16(s, (msb << 8) | (z << 7) | lsb);
    tcg_out16(s, (ofs << 8) | (RIE_RISBG & 0xff));
}

static void tgen_ext8s(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LGBR, dest, src);
        return;
    }

    if (type == TCG_TYPE_I32) {
        if (dest == src) {
            tcg_out_sh32(s, RS_SLL, dest, TCG_REG_NONE, 24);
        } else {
            tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 24);
        }
        tcg_out_sh32(s, RS_SRA, dest, TCG_REG_NONE, 24);
    } else {
        tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 56);
        tcg_out_sh64(s, RSY_SRAG, dest, dest, TCG_REG_NONE, 56);
    }
}

static void tgen_ext8u(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LLGCR, dest, src);
        return;
    }

    if (dest == src) {
        tcg_out_movi(s, type, TCG_TMP0, 0xff);
        src = TCG_TMP0;
    } else {
        tcg_out_movi(s, type, dest, 0xff);
    }
    if (type == TCG_TYPE_I32) {
        tcg_out_insn(s, RR, NR, dest, src);
    } else {
        tcg_out_insn(s, RRE, NGR, dest, src);
    }
}

static void tgen_ext16s(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LGHR, dest, src);
        return;
    }

    if (type == TCG_TYPE_I32) {
        if (dest == src) {
            tcg_out_sh32(s, RS_SLL, dest, TCG_REG_NONE, 16);
        } else {
            tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 16);
        }
        tcg_out_sh32(s, RS_SRA, dest, TCG_REG_NONE, 16);
    } else {
        tcg_out_sh64(s, RSY_SLLG, dest, src, TCG_REG_NONE, 48);
        tcg_out_sh64(s, RSY_SRAG, dest, dest, TCG_REG_NONE, 48);
    }
}

static void tgen_ext16u(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    if (facilities & FACILITY_EXT_IMM) {
        tcg_out_insn(s, RRE, LLGHR, dest, src);
        return;
    }

    if (dest == src) {
        tcg_out_movi(s, type, TCG_TMP0, 0xffff);
        src = TCG_TMP0;
    } else {
        tcg_out_movi(s, type, dest, 0xffff);
    }
    if (type == TCG_TYPE_I32) {
        tcg_out_insn(s, RR, NR, dest, src);
    } else {
        tcg_out_insn(s, RRE, NGR, dest, src);
    }
}

static inline void tgen_ext32s(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LGFR, dest, src);
}

static inline void tgen_ext32u(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LLGFR, dest, src);
}

/* Accept bit patterns like these:
    0....01....1
    1....10....0
    1..10..01..1
    0..01..10..0
   Copied from gcc sources.  */
static inline bool risbg_mask(uint64_t c)
{
    uint64_t lsb;
    /* We don't change the number of transitions by inverting,
       so make sure we start with the LSB zero.  */
    if (c & 1) {
        c = ~c;
    }
    /* Reject all zeros or all ones.  */
    if (c == 0) {
        return false;
    }
    /* Find the first transition.  */
    lsb = c & -c;
    /* Invert to look for a second transition.  */
    c = ~c;
    /* Erase the first transition.  */
    c &= -lsb;
    /* Find the second transition, if any.  */
    lsb = c & -c;
    /* Match if all the bits are 1's, or if c is zero.  */
    return c == -lsb;
}

static void tgen_andi(TCGContext *s, TCGType type, TCGReg dest, uint64_t val)
{
    static const S390Opcode ni_insns[4] = {
        RI_NILL, RI_NILH, RI_NIHL, RI_NIHH
    };
    static const S390Opcode nif_insns[2] = {
        RIL_NILF, RIL_NIHF
    };
    uint64_t valid = (type == TCG_TYPE_I32 ? 0xffffffffull : -1ull);
    int i;

    /* Look for the zero-extensions.  */
    if ((val & valid) == 0xffffffff) {
        tgen_ext32u(s, dest, dest);
        return;
    }
    if (facilities & FACILITY_EXT_IMM) {
        if ((val & valid) == 0xff) {
            tgen_ext8u(s, TCG_TYPE_I64, dest, dest);
            return;
        }
        if ((val & valid) == 0xffff) {
            tgen_ext16u(s, TCG_TYPE_I64, dest, dest);
            return;
        }
    }

    /* Try all 32-bit insns that can perform it in one go.  */
    for (i = 0; i < 4; i++) {
        tcg_target_ulong mask = ~(0xffffull << i*16);
        if (((val | ~valid) & mask) == mask) {
            tcg_out_insn_RI(s, ni_insns[i], dest, val >> i*16);
            return;
        }
    }

    /* Try all 48-bit insns that can perform it in one go.  */
    if (facilities & FACILITY_EXT_IMM) {
        for (i = 0; i < 2; i++) {
            tcg_target_ulong mask = ~(0xffffffffull << i*32);
            if (((val | ~valid) & mask) == mask) {
                tcg_out_insn_RIL(s, nif_insns[i], dest, val >> i*32);
                return;
            }
        }
    }
    if ((facilities & FACILITY_GEN_INST_EXT) && risbg_mask(val)) {
        int msb, lsb;
        if ((val & 0x8000000000000001ull) == 0x8000000000000001ull) {
            /* Achieve wraparound by swapping msb and lsb.  */
            msb = 64 - ctz64(~val);
            lsb = clz64(~val) - 1;
        } else {
            msb = clz64(val);
            lsb = 63 - ctz64(val);
        }
        tcg_out_risbg(s, dest, dest, msb, lsb, 0, 1);
        return;
    }

    /* Fall back to loading the constant.  */
    tcg_out_movi(s, type, TCG_TMP0, val);
    if (type == TCG_TYPE_I32) {
        tcg_out_insn(s, RR, NR, dest, TCG_TMP0);
    } else {
        tcg_out_insn(s, RRE, NGR, dest, TCG_TMP0);
    }
}

static void tgen64_ori(TCGContext *s, TCGReg dest, tcg_target_ulong val)
{
    static const S390Opcode oi_insns[4] = {
        RI_OILL, RI_OILH, RI_OIHL, RI_OIHH
    };
    static const S390Opcode nif_insns[2] = {
        RIL_OILF, RIL_OIHF
    };

    int i;

    /* Look for no-op.  */
    if (val == 0) {
        return;
    }

    if (facilities & FACILITY_EXT_IMM) {
        /* Try all 32-bit insns that can perform it in one go.  */
        for (i = 0; i < 4; i++) {
            tcg_target_ulong mask = (0xffffull << i*16);
            if ((val & mask) != 0 && (val & ~mask) == 0) {
                tcg_out_insn_RI(s, oi_insns[i], dest, val >> i*16);
                return;
            }
        }

        /* Try all 48-bit insns that can perform it in one go.  */
        for (i = 0; i < 2; i++) {
            tcg_target_ulong mask = (0xffffffffull << i*32);
            if ((val & mask) != 0 && (val & ~mask) == 0) {
                tcg_out_insn_RIL(s, nif_insns[i], dest, val >> i*32);
                return;
            }
        }

        /* Perform the OR via sequential modifications to the high and
           low parts.  Do this via recursion to handle 16-bit vs 32-bit
           masks in each half.  */
        tgen64_ori(s, dest, val & 0x00000000ffffffffull);
        tgen64_ori(s, dest, val & 0xffffffff00000000ull);
    } else {
        /* With no extended-immediate facility, we don't need to be so
           clever.  Just iterate over the insns and mask in the constant.  */
        for (i = 0; i < 4; i++) {
            tcg_target_ulong mask = (0xffffull << i*16);
            if ((val & mask) != 0) {
                tcg_out_insn_RI(s, oi_insns[i], dest, val >> i*16);
            }
        }
    }
}

static void tgen64_xori(TCGContext *s, TCGReg dest, tcg_target_ulong val)
{
    /* Perform the xor by parts.  */
    if (val & 0xffffffff) {
        tcg_out_insn(s, RIL, XILF, dest, val);
    }
    if (val > 0xffffffff) {
        tcg_out_insn(s, RIL, XIHF, dest, val >> 31 >> 1);
    }
}

static int tgen_cmp(TCGContext *s, TCGType type, TCGCond c, TCGReg r1,
                    TCGArg c2, int c2const)
{
    bool is_unsigned = is_unsigned_cond(c);
    if (c2const) {
        if (c2 == 0) {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RR, LTR, r1, r1);
            } else {
                tcg_out_insn(s, RRE, LTGR, r1, r1);
            }
            return tcg_cond_to_ltr_cond[c];
        } else {
            if (is_unsigned) {
                if (type == TCG_TYPE_I32) {
                    tcg_out_insn(s, RIL, CLFI, r1, c2);
                } else {
                    tcg_out_insn(s, RIL, CLGFI, r1, c2);
                }
            } else {
                if (type == TCG_TYPE_I32) {
                    tcg_out_insn(s, RIL, CFI, r1, c2);
                } else {
                    tcg_out_insn(s, RIL, CGFI, r1, c2);
                }
            }
        }
    } else {
        if (is_unsigned) {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RR, CLR, r1, c2);
            } else {
                tcg_out_insn(s, RRE, CLGR, r1, c2);
            }
        } else {
            if (type == TCG_TYPE_I32) {
                tcg_out_insn(s, RR, CR, r1, c2);
            } else {
                tcg_out_insn(s, RRE, CGR, r1, c2);
            }
        }
    }
    return tcg_cond_to_s390_cond[c];
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond c,
                         TCGReg dest, TCGReg c1, TCGArg c2, int c2const)
{
    int cc = tgen_cmp(s, type, c, c1, c2, c2const);

    /* Emit: r1 = 1; if (cc) goto over; r1 = 0; over:  */
    tcg_out_movi(s, type, dest, 1);
    tcg_out_insn(s, RI, BRC, cc, (4 + 4) >> 1);
    tcg_out_movi(s, type, dest, 0);
}

static void tgen_movcond(TCGContext *s, TCGType type, TCGCond c, TCGReg dest,
                         TCGReg c1, TCGArg c2, int c2const, TCGReg r3)
{
    int cc;
    if (facilities & FACILITY_LOAD_ON_COND) {
        cc = tgen_cmp(s, type, c, c1, c2, c2const);
        tcg_out_insn(s, RRF, LOCGR, dest, r3, cc);
    } else {
        c = tcg_invert_cond(c);
        cc = tgen_cmp(s, type, c, c1, c2, c2const);

        /* Emit: if (cc) goto over; dest = r3; over:  */
        tcg_out_insn(s, RI, BRC, cc, (4 + 4) >> 1);
        tcg_out_insn(s, RRE, LGR, dest, r3);
    }
}

bool tcg_target_deposit_valid(int ofs, int len)
{
    return (facilities & FACILITY_GEN_INST_EXT) != 0;
}

static void tgen_deposit(TCGContext *s, TCGReg dest, TCGReg src,
                         int ofs, int len)
{
    int lsb = (63 - ofs);
    int msb = lsb - (len - 1);
    tcg_out_risbg(s, dest, src, msb, lsb, ofs, 0);
}

static void tgen_gotoi(TCGContext *s, int cc, tcg_insn_unit *dest)
{
    ptrdiff_t off = dest - s->code_ptr;
    if (off == (int16_t)off) {
        tcg_out_insn(s, RI, BRC, cc, off);
    } else if (off == (int32_t)off) {
        tcg_out_insn(s, RIL, BRCL, cc, off);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, (uintptr_t)dest);
        tcg_out_insn(s, RR, BCR, cc, TCG_TMP0);
    }
}

static void tgen_branch(TCGContext *s, int cc, int labelno)
{
    TCGLabel* l = &s->labels[labelno];
    if (l->has_value) {
        tgen_gotoi(s, cc, l->u.value_ptr);
    } else if (USE_LONG_BRANCHES) {
        tcg_out16(s, RIL_BRCL | (cc << 4));
        tcg_out_reloc(s, s->code_ptr, R_390_PC32DBL, labelno, -2);
        s->code_ptr += 2;
    } else {
        tcg_out16(s, RI_BRC | (cc << 4));
        tcg_out_reloc(s, s->code_ptr, R_390_PC16DBL, labelno, -2);
        s->code_ptr += 1;
    }
}

static void tgen_compare_branch(TCGContext *s, S390Opcode opc, int cc,
                                TCGReg r1, TCGReg r2, int labelno)
{
    TCGLabel* l = &s->labels[labelno];
    intptr_t off;

    if (l->has_value) {
        off = l->u.value_ptr - s->code_ptr;
    } else {
        /* We need to keep the offset unchanged for retranslation.  */
        off = s->code_ptr[1];
        tcg_out_reloc(s, s->code_ptr + 1, R_390_PC16DBL, labelno, -2);
    }

    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | r2);
    tcg_out16(s, off);
    tcg_out16(s, cc << 12 | (opc & 0xff));
}

static void tgen_compare_imm_branch(TCGContext *s, S390Opcode opc, int cc,
                                    TCGReg r1, int i2, int labelno)
{
    TCGLabel* l = &s->labels[labelno];
    tcg_target_long off;

    if (l->has_value) {
        off = l->u.value_ptr - s->code_ptr;
    } else {
        /* We need to keep the offset unchanged for retranslation.  */
        off = s->code_ptr[1];
        tcg_out_reloc(s, s->code_ptr + 1, R_390_PC16DBL, labelno, -2);
    }

    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | cc);
    tcg_out16(s, off);
    tcg_out16(s, (i2 << 8) | (opc & 0xff));
}

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond c,
                        TCGReg r1, TCGArg c2, int c2const, int labelno)
{
    int cc;

    if (facilities & FACILITY_GEN_INST_EXT) {
        bool is_unsigned = is_unsigned_cond(c);
        bool in_range;
        S390Opcode opc;

        cc = tcg_cond_to_s390_cond[c];

        if (!c2const) {
            opc = (type == TCG_TYPE_I32
                   ? (is_unsigned ? RIE_CLRJ : RIE_CRJ)
                   : (is_unsigned ? RIE_CLGRJ : RIE_CGRJ));
            tgen_compare_branch(s, opc, cc, r1, c2, labelno);
            return;
        }

        /* COMPARE IMMEDIATE AND BRANCH RELATIVE has an 8-bit immediate field.
           If the immediate we've been given does not fit that range, we'll
           fall back to separate compare and branch instructions using the
           larger comparison range afforded by COMPARE IMMEDIATE.  */
        if (type == TCG_TYPE_I32) {
            if (is_unsigned) {
                opc = RIE_CLIJ;
                in_range = (uint32_t)c2 == (uint8_t)c2;
            } else {
                opc = RIE_CIJ;
                in_range = (int32_t)c2 == (int8_t)c2;
            }
        } else {
            if (is_unsigned) {
                opc = RIE_CLGIJ;
                in_range = (uint64_t)c2 == (uint8_t)c2;
            } else {
                opc = RIE_CGIJ;
                in_range = (int64_t)c2 == (int8_t)c2;
            }
        }
        if (in_range) {
            tgen_compare_imm_branch(s, opc, cc, r1, c2, labelno);
            return;
        }
    }

    cc = tgen_cmp(s, type, c, r1, c2, c2const);
    tgen_branch(s, cc, labelno);
}

static void tcg_out_call(TCGContext *s, tcg_insn_unit *dest)
{
    ptrdiff_t off = dest - s->code_ptr;
    if (off == (int32_t)off) {
        tcg_out_insn(s, RIL, BRASL, TCG_REG_R14, off);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, (uintptr_t)dest);
        tcg_out_insn(s, RR, BASR, TCG_REG_R14, TCG_TMP0);
    }
}

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGMemOp opc, TCGReg data,
                                   TCGReg base, TCGReg index, int disp)
{
    switch (opc) {
    case MO_UB:
        tcg_out_insn(s, RXY, LLGC, data, base, index, disp);
        break;
    case MO_SB:
        tcg_out_insn(s, RXY, LGB, data, base, index, disp);
        break;

    case MO_UW | MO_BSWAP:
        /* swapped unsigned halfword load with upper bits zeroed */
        tcg_out_insn(s, RXY, LRVH, data, base, index, disp);
        tgen_ext16u(s, TCG_TYPE_I64, data, data);
        break;
    case MO_UW:
        tcg_out_insn(s, RXY, LLGH, data, base, index, disp);
        break;

    case MO_SW | MO_BSWAP:
        /* swapped sign-extended halfword load */
        tcg_out_insn(s, RXY, LRVH, data, base, index, disp);
        tgen_ext16s(s, TCG_TYPE_I64, data, data);
        break;
    case MO_SW:
        tcg_out_insn(s, RXY, LGH, data, base, index, disp);
        break;

    case MO_UL | MO_BSWAP:
        /* swapped unsigned int load with upper bits zeroed */
        tcg_out_insn(s, RXY, LRV, data, base, index, disp);
        tgen_ext32u(s, data, data);
        break;
    case MO_UL:
        tcg_out_insn(s, RXY, LLGF, data, base, index, disp);
        break;

    case MO_SL | MO_BSWAP:
        /* swapped sign-extended int load */
        tcg_out_insn(s, RXY, LRV, data, base, index, disp);
        tgen_ext32s(s, data, data);
        break;
    case MO_SL:
        tcg_out_insn(s, RXY, LGF, data, base, index, disp);
        break;

    case MO_Q | MO_BSWAP:
        tcg_out_insn(s, RXY, LRVG, data, base, index, disp);
        break;
    case MO_Q:
        tcg_out_insn(s, RXY, LG, data, base, index, disp);
        break;

    default:
        tcg_abort();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, TCGMemOp opc, TCGReg data,
                                   TCGReg base, TCGReg index, int disp)
{
    switch (opc) {
    case MO_UB:
        if (disp >= 0 && disp < 0x1000) {
            tcg_out_insn(s, RX, STC, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, STCY, data, base, index, disp);
        }
        break;

    case MO_UW | MO_BSWAP:
        tcg_out_insn(s, RXY, STRVH, data, base, index, disp);
        break;
    case MO_UW:
        if (disp >= 0 && disp < 0x1000) {
            tcg_out_insn(s, RX, STH, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, STHY, data, base, index, disp);
        }
        break;

    case MO_UL | MO_BSWAP:
        tcg_out_insn(s, RXY, STRV, data, base, index, disp);
        break;
    case MO_UL:
        if (disp >= 0 && disp < 0x1000) {
            tcg_out_insn(s, RX, ST, data, base, index, disp);
        } else {
            tcg_out_insn(s, RXY, STY, data, base, index, disp);
        }
        break;

    case MO_Q | MO_BSWAP:
        tcg_out_insn(s, RXY, STRVG, data, base, index, disp);
        break;
    case MO_Q:
        tcg_out_insn(s, RXY, STG, data, base, index, disp);
        break;

    default:
        tcg_abort();
    }
}

#if defined(CONFIG_SOFTMMU)
/* We're expecting to use a 20-bit signed offset on the tlb memory ops.
   Using the offset of the second entry in the last tlb table ensures
   that we can index all of the elements of the first entry.  */
QEMU_BUILD_BUG_ON(offsetof(CPUArchState, tlb_table[NB_MMU_MODES - 1][1])
                  > 0x7ffff);

/* Load and compare a TLB entry, leaving the flags set.  Loads the TLB
   addend into R2.  Returns a register with the santitized guest address.  */
static TCGReg tcg_out_tlb_read(TCGContext* s, TCGReg addr_reg, TCGMemOp opc,
                               int mem_index, bool is_ld)
{
    TCGMemOp s_bits = opc & MO_SIZE;
    int ofs;

    tcg_out_sh64(s, RSY_SRLG, TCG_REG_R2, addr_reg, TCG_REG_NONE,
                 TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);

    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, TCG_REG_R3, addr_reg);
    } else {
        tcg_out_mov(s, TCG_TYPE_I64, TCG_REG_R3, addr_reg);
    }

    tgen_andi(s, TCG_TYPE_I64, TCG_REG_R2,
              (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);
    tgen_andi(s, TCG_TYPE_I64, TCG_REG_R3,
              TARGET_PAGE_MASK | ((1 << s_bits) - 1));

    if (is_ld) {
        ofs = offsetof(CPUArchState, tlb_table[mem_index][0].addr_read);
    } else {
        ofs = offsetof(CPUArchState, tlb_table[mem_index][0].addr_write);
    }
    if (TARGET_LONG_BITS == 32) {
        tcg_out_mem(s, RX_C, RXY_CY, TCG_REG_R3, TCG_REG_R2, TCG_AREG0, ofs);
    } else {
        tcg_out_mem(s, 0, RXY_CG, TCG_REG_R3, TCG_REG_R2, TCG_AREG0, ofs);
    }

    ofs = offsetof(CPUArchState, tlb_table[mem_index][0].addend);
    tcg_out_mem(s, 0, RXY_LG, TCG_REG_R2, TCG_REG_R2, TCG_AREG0, ofs);

    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, TCG_REG_R3, addr_reg);
        return TCG_REG_R3;
    }
    return addr_reg;
}

static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOp opc,
                                TCGReg data, TCGReg addr, int mem_index,
                                tcg_insn_unit *raddr, tcg_insn_unit *label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->opc = opc;
    label->datalo_reg = data;
    label->addrlo_reg = addr;
    label->mem_index = mem_index;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr;
}

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg addr_reg = lb->addrlo_reg;
    TCGReg data_reg = lb->datalo_reg;
    TCGMemOp opc = lb->opc;

    patch_reloc(lb->label_ptr[0], R_390_PC16DBL, (intptr_t)s->code_ptr, -2);

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R2, TCG_AREG0);
    if (TARGET_LONG_BITS == 64) {
        tcg_out_mov(s, TCG_TYPE_I64, TCG_REG_R3, addr_reg);
    }
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R4, lb->mem_index);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R5, (uintptr_t)lb->raddr);
    tcg_out_call(s, qemu_ld_helpers[opc]);
    tcg_out_mov(s, TCG_TYPE_I64, data_reg, TCG_REG_R2);

    tgen_gotoi(s, S390_CC_ALWAYS, lb->raddr);
}

static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg addr_reg = lb->addrlo_reg;
    TCGReg data_reg = lb->datalo_reg;
    TCGMemOp opc = lb->opc;

    patch_reloc(lb->label_ptr[0], R_390_PC16DBL, (intptr_t)s->code_ptr, -2);

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R2, TCG_AREG0);
    if (TARGET_LONG_BITS == 64) {
        tcg_out_mov(s, TCG_TYPE_I64, TCG_REG_R3, addr_reg);
    }
    switch (opc & MO_SIZE) {
    case MO_UB:
        tgen_ext8u(s, TCG_TYPE_I64, TCG_REG_R4, data_reg);
        break;
    case MO_UW:
        tgen_ext16u(s, TCG_TYPE_I64, TCG_REG_R4, data_reg);
        break;
    case MO_UL:
        tgen_ext32u(s, TCG_REG_R4, data_reg);
        break;
    case MO_Q:
        tcg_out_mov(s, TCG_TYPE_I64, TCG_REG_R4, data_reg);
        break;
    default:
        tcg_abort();
    }
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R5, lb->mem_index);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R6, (uintptr_t)lb->raddr);
    tcg_out_call(s, qemu_st_helpers[opc]);

    tgen_gotoi(s, S390_CC_ALWAYS, lb->raddr);
}
#else
static void tcg_prepare_user_ldst(TCGContext *s, TCGReg *addr_reg,
                                  TCGReg *index_reg, tcg_target_long *disp)
{
    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, TCG_TMP0, *addr_reg);
        *addr_reg = TCG_TMP0;
    }
    if (GUEST_BASE < 0x80000) {
        *index_reg = TCG_REG_NONE;
        *disp = GUEST_BASE;
    } else {
        *index_reg = TCG_GUEST_BASE_REG;
        *disp = 0;
    }
}
#endif /* CONFIG_SOFTMMU */

static void tcg_out_qemu_ld(TCGContext* s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOp opc, int mem_index)
{
#ifdef CONFIG_SOFTMMU
    tcg_insn_unit *label_ptr;
    TCGReg base_reg;

    base_reg = tcg_out_tlb_read(s, addr_reg, opc, mem_index, 1);

    label_ptr = s->code_ptr + 1;
    tcg_out_insn(s, RI, BRC, S390_CC_NE, 0);

    tcg_out_qemu_ld_direct(s, opc, data_reg, base_reg, TCG_REG_R2, 0);

    add_qemu_ldst_label(s, 1, opc, data_reg, addr_reg, mem_index,
                        s->code_ptr, label_ptr);
#else
    TCGReg index_reg;
    tcg_target_long disp;

    tcg_prepare_user_ldst(s, &addr_reg, &index_reg, &disp);
    tcg_out_qemu_ld_direct(s, opc, data_reg, addr_reg, index_reg, disp);
#endif
}

static void tcg_out_qemu_st(TCGContext* s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOp opc, int mem_index)
{
#ifdef CONFIG_SOFTMMU
    tcg_insn_unit *label_ptr;
    TCGReg base_reg;

    base_reg = tcg_out_tlb_read(s, addr_reg, opc, mem_index, 0);

    label_ptr = s->code_ptr + 1;
    tcg_out_insn(s, RI, BRC, S390_CC_NE, 0);

    tcg_out_qemu_st_direct(s, opc, data_reg, base_reg, TCG_REG_R2, 0);

    add_qemu_ldst_label(s, 0, opc, data_reg, addr_reg, mem_index,
                        s->code_ptr, label_ptr);
#else
    TCGReg index_reg;
    tcg_target_long disp;

    tcg_prepare_user_ldst(s, &addr_reg, &index_reg, &disp);
    tcg_out_qemu_st_direct(s, opc, data_reg, addr_reg, index_reg, disp);
#endif
}

# define OP_32_64(x) \
        case glue(glue(INDEX_op_,x),_i32): \
        case glue(glue(INDEX_op_,x),_i64)

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                const TCGArg *args, const int *const_args)
{
    S390Opcode op;
    TCGArg a0, a1, a2;

    switch (opc) {
    case INDEX_op_exit_tb:
        /* return value */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, args[0]);
        tgen_gotoi(s, S390_CC_ALWAYS, tb_ret_addr);
        break;

    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            tcg_abort();
        } else {
            /* load address stored at s->tb_next + args[0] */
            tcg_out_ld_abs(s, TCG_TYPE_PTR, TCG_TMP0, s->tb_next + args[0]);
            /* and go there */
            tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, TCG_TMP0);
        }
        s->tb_next_offset[args[0]] = tcg_current_code_size(s);
        break;

    OP_32_64(ld8u):
        /* ??? LLC (RXY format) is only present with the extended-immediate
           facility, whereas LLGC is always present.  */
        tcg_out_mem(s, 0, RXY_LLGC, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    OP_32_64(ld8s):
        /* ??? LB is no smaller than LGB, so no point to using it.  */
        tcg_out_mem(s, 0, RXY_LGB, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    OP_32_64(ld16u):
        /* ??? LLH (RXY format) is only present with the extended-immediate
           facility, whereas LLGH is always present.  */
        tcg_out_mem(s, 0, RXY_LLGH, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    case INDEX_op_ld16s_i32:
        tcg_out_mem(s, RX_LH, RXY_LHY, args[0], args[1], TCG_REG_NONE, args[2]);
        break;

    case INDEX_op_ld_i32:
        tcg_out_ld(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;

    OP_32_64(st8):
        tcg_out_mem(s, RX_STC, RXY_STCY, args[0], args[1],
                    TCG_REG_NONE, args[2]);
        break;

    OP_32_64(st16):
        tcg_out_mem(s, RX_STH, RXY_STHY, args[0], args[1],
                    TCG_REG_NONE, args[2]);
        break;

    case INDEX_op_st_i32:
        tcg_out_st(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i32:
        a0 = args[0], a1 = args[1], a2 = (int32_t)args[2];
        if (const_args[2]) {
        do_addi_32:
            if (a0 == a1) {
                if (a2 == (int16_t)a2) {
                    tcg_out_insn(s, RI, AHI, a0, a2);
                    break;
                }
                if (facilities & FACILITY_EXT_IMM) {
                    tcg_out_insn(s, RIL, AFI, a0, a2);
                    break;
                }
            }
            tcg_out_mem(s, RX_LA, RXY_LAY, a0, a1, TCG_REG_NONE, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RR, AR, a0, a2);
        } else {
            tcg_out_insn(s, RX, LA, a0, a1, a2, 0);
        }
        break;
    case INDEX_op_sub_i32:
        a0 = args[0], a1 = args[1], a2 = (int32_t)args[2];
        if (const_args[2]) {
            a2 = -a2;
            goto do_addi_32;
        }
        tcg_out_insn(s, RR, SR, args[0], args[2]);
        break;

    case INDEX_op_and_i32:
        if (const_args[2]) {
            tgen_andi(s, TCG_TYPE_I32, args[0], args[2]);
        } else {
            tcg_out_insn(s, RR, NR, args[0], args[2]);
        }
        break;
    case INDEX_op_or_i32:
        if (const_args[2]) {
            tgen64_ori(s, args[0], args[2] & 0xffffffff);
        } else {
            tcg_out_insn(s, RR, OR, args[0], args[2]);
        }
        break;
    case INDEX_op_xor_i32:
        if (const_args[2]) {
            tgen64_xori(s, args[0], args[2] & 0xffffffff);
        } else {
            tcg_out_insn(s, RR, XR, args[0], args[2]);
        }
        break;

    case INDEX_op_neg_i32:
        tcg_out_insn(s, RR, LCR, args[0], args[1]);
        break;

    case INDEX_op_mul_i32:
        if (const_args[2]) {
            if ((int32_t)args[2] == (int16_t)args[2]) {
                tcg_out_insn(s, RI, MHI, args[0], args[2]);
            } else {
                tcg_out_insn(s, RIL, MSFI, args[0], args[2]);
            }
        } else {
            tcg_out_insn(s, RRE, MSR, args[0], args[2]);
        }
        break;

    case INDEX_op_div2_i32:
        tcg_out_insn(s, RR, DR, TCG_REG_R2, args[4]);
        break;
    case INDEX_op_divu2_i32:
        tcg_out_insn(s, RRE, DLR, TCG_REG_R2, args[4]);
        break;

    case INDEX_op_shl_i32:
        op = RS_SLL;
    do_shift32:
        if (const_args[2]) {
            tcg_out_sh32(s, op, args[0], TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh32(s, op, args[0], args[2], 0);
        }
        break;
    case INDEX_op_shr_i32:
        op = RS_SRL;
        goto do_shift32;
    case INDEX_op_sar_i32:
        op = RS_SRA;
        goto do_shift32;

    case INDEX_op_rotl_i32:
        /* ??? Using tcg_out_sh64 here for the format; it is a 32-bit rol.  */
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLL, args[0], args[1], TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh64(s, RSY_RLL, args[0], args[1], args[2], 0);
        }
        break;
    case INDEX_op_rotr_i32:
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLL, args[0], args[1],
                         TCG_REG_NONE, (32 - args[2]) & 31);
        } else {
            tcg_out_insn(s, RR, LCR, TCG_TMP0, args[2]);
            tcg_out_sh64(s, RSY_RLL, args[0], args[1], TCG_TMP0, 0);
        }
        break;

    case INDEX_op_ext8s_i32:
        tgen_ext8s(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
        tgen_ext16s(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i32:
        tgen_ext8u(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i32:
        tgen_ext16u(s, TCG_TYPE_I32, args[0], args[1]);
        break;

    OP_32_64(bswap16):
        /* The TCG bswap definition requires bits 0-47 already be zero.
           Thus we don't need the G-type insns to implement bswap16_i64.  */
        tcg_out_insn(s, RRE, LRVR, args[0], args[1]);
        tcg_out_sh32(s, RS_SRL, args[0], TCG_REG_NONE, 16);
        break;
    OP_32_64(bswap32):
        tcg_out_insn(s, RRE, LRVR, args[0], args[1]);
        break;

    case INDEX_op_add2_i32:
        /* ??? Make use of ALFI.  */
        tcg_out_insn(s, RR, ALR, args[0], args[4]);
        tcg_out_insn(s, RRE, ALCR, args[1], args[5]);
        break;
    case INDEX_op_sub2_i32:
        /* ??? Make use of SLFI.  */
        tcg_out_insn(s, RR, SLR, args[0], args[4]);
        tcg_out_insn(s, RRE, SLBR, args[1], args[5]);
        break;

    case INDEX_op_br:
        tgen_branch(s, S390_CC_ALWAYS, args[0]);
        break;

    case INDEX_op_brcond_i32:
        tgen_brcond(s, TCG_TYPE_I32, args[2], args[0],
                    args[1], const_args[1], args[3]);
        break;
    case INDEX_op_setcond_i32:
        tgen_setcond(s, TCG_TYPE_I32, args[3], args[0], args[1],
                     args[2], const_args[2]);
        break;
    case INDEX_op_movcond_i32:
        tgen_movcond(s, TCG_TYPE_I32, args[5], args[0], args[1],
                     args[2], const_args[2], args[3]);
        break;

    case INDEX_op_qemu_ld_i32:
        /* ??? Technically we can use a non-extending instruction.  */
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, args[0], args[1], args[2], args[3]);
        break;
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, args[0], args[1], args[2], args[3]);
        break;

    case INDEX_op_ld16s_i64:
        tcg_out_mem(s, 0, RXY_LGH, args[0], args[1], TCG_REG_NONE, args[2]);
        break;
    case INDEX_op_ld32u_i64:
        tcg_out_mem(s, 0, RXY_LLGF, args[0], args[1], TCG_REG_NONE, args[2]);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_mem(s, 0, RXY_LGF, args[0], args[1], TCG_REG_NONE, args[2]);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ld(s, TCG_TYPE_I64, args[0], args[1], args[2]);
        break;

    case INDEX_op_st32_i64:
        tcg_out_st(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i64:
        tcg_out_st(s, TCG_TYPE_I64, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
        do_addi_64:
            if (a0 == a1) {
                if (a2 == (int16_t)a2) {
                    tcg_out_insn(s, RI, AGHI, a0, a2);
                    break;
                }
                if (facilities & FACILITY_EXT_IMM) {
                    if (a2 == (int32_t)a2) {
                        tcg_out_insn(s, RIL, AGFI, a0, a2);
                        break;
                    } else if (a2 == (uint32_t)a2) {
                        tcg_out_insn(s, RIL, ALGFI, a0, a2);
                        break;
                    } else if (-a2 == (uint32_t)-a2) {
                        tcg_out_insn(s, RIL, SLGFI, a0, -a2);
                        break;
                    }
                }
            }
            tcg_out_mem(s, RX_LA, RXY_LAY, a0, a1, TCG_REG_NONE, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RRE, AGR, a0, a2);
        } else {
            tcg_out_insn(s, RX, LA, a0, a1, a2, 0);
        }
        break;
    case INDEX_op_sub_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            a2 = -a2;
            goto do_addi_64;
        } else {
            tcg_out_insn(s, RRE, SGR, args[0], args[2]);
        }
        break;

    case INDEX_op_and_i64:
        if (const_args[2]) {
            tgen_andi(s, TCG_TYPE_I64, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRE, NGR, args[0], args[2]);
        }
        break;
    case INDEX_op_or_i64:
        if (const_args[2]) {
            tgen64_ori(s, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRE, OGR, args[0], args[2]);
        }
        break;
    case INDEX_op_xor_i64:
        if (const_args[2]) {
            tgen64_xori(s, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRE, XGR, args[0], args[2]);
        }
        break;

    case INDEX_op_neg_i64:
        tcg_out_insn(s, RRE, LCGR, args[0], args[1]);
        break;
    case INDEX_op_bswap64_i64:
        tcg_out_insn(s, RRE, LRVGR, args[0], args[1]);
        break;

    case INDEX_op_mul_i64:
        if (const_args[2]) {
            if (args[2] == (int16_t)args[2]) {
                tcg_out_insn(s, RI, MGHI, args[0], args[2]);
            } else {
                tcg_out_insn(s, RIL, MSGFI, args[0], args[2]);
            }
        } else {
            tcg_out_insn(s, RRE, MSGR, args[0], args[2]);
        }
        break;

    case INDEX_op_div2_i64:
        /* ??? We get an unnecessary sign-extension of the dividend
           into R3 with this definition, but as we do in fact always
           produce both quotient and remainder using INDEX_op_div_i64
           instead requires jumping through even more hoops.  */
        tcg_out_insn(s, RRE, DSGR, TCG_REG_R2, args[4]);
        break;
    case INDEX_op_divu2_i64:
        tcg_out_insn(s, RRE, DLGR, TCG_REG_R2, args[4]);
        break;
    case INDEX_op_mulu2_i64:
        tcg_out_insn(s, RRE, MLGR, TCG_REG_R2, args[3]);
        break;

    case INDEX_op_shl_i64:
        op = RSY_SLLG;
    do_shift64:
        if (const_args[2]) {
            tcg_out_sh64(s, op, args[0], args[1], TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh64(s, op, args[0], args[1], args[2], 0);
        }
        break;
    case INDEX_op_shr_i64:
        op = RSY_SRLG;
        goto do_shift64;
    case INDEX_op_sar_i64:
        op = RSY_SRAG;
        goto do_shift64;

    case INDEX_op_rotl_i64:
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1],
                         TCG_REG_NONE, args[2]);
        } else {
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1], args[2], 0);
        }
        break;
    case INDEX_op_rotr_i64:
        if (const_args[2]) {
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1],
                         TCG_REG_NONE, (64 - args[2]) & 63);
        } else {
            /* We can use the smaller 32-bit negate because only the
               low 6 bits are examined for the rotate.  */
            tcg_out_insn(s, RR, LCR, TCG_TMP0, args[2]);
            tcg_out_sh64(s, RSY_RLLG, args[0], args[1], TCG_TMP0, 0);
        }
        break;

    case INDEX_op_ext8s_i64:
        tgen_ext8s(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i64:
        tgen_ext16s(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext32s_i64:
        tgen_ext32s(s, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i64:
        tgen_ext8u(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i64:
        tgen_ext16u(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext32u_i64:
        tgen_ext32u(s, args[0], args[1]);
        break;

    case INDEX_op_add2_i64:
        /* ??? Make use of ALGFI and SLGFI.  */
        tcg_out_insn(s, RRE, ALGR, args[0], args[4]);
        tcg_out_insn(s, RRE, ALCGR, args[1], args[5]);
        break;
    case INDEX_op_sub2_i64:
        /* ??? Make use of ALGFI and SLGFI.  */
        tcg_out_insn(s, RRE, SLGR, args[0], args[4]);
        tcg_out_insn(s, RRE, SLBGR, args[1], args[5]);
        break;

    case INDEX_op_brcond_i64:
        tgen_brcond(s, TCG_TYPE_I64, args[2], args[0],
                    args[1], const_args[1], args[3]);
        break;
    case INDEX_op_setcond_i64:
        tgen_setcond(s, TCG_TYPE_I64, args[3], args[0], args[1],
                     args[2], const_args[2]);
        break;
    case INDEX_op_movcond_i64:
        tgen_movcond(s, TCG_TYPE_I64, args[5], args[0], args[1],
                     args[2], const_args[2], args[3]);
        break;

    OP_32_64(deposit):
        tgen_deposit(s, args[0], args[2], args[3], args[4]);
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

static const TCGTargetOpDef s390_op_defs[] = {
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

    { INDEX_op_add_i32, { "r", "r", "ri" } },
    { INDEX_op_sub_i32, { "r", "0", "ri" } },
    { INDEX_op_mul_i32, { "r", "0", "rK" } },

    { INDEX_op_div2_i32, { "b", "a", "0", "1", "r" } },
    { INDEX_op_divu2_i32, { "b", "a", "0", "1", "r" } },

    { INDEX_op_and_i32, { "r", "0", "ri" } },
    { INDEX_op_or_i32, { "r", "0", "rO" } },
    { INDEX_op_xor_i32, { "r", "0", "rX" } },

    { INDEX_op_neg_i32, { "r", "r" } },

    { INDEX_op_shl_i32, { "r", "0", "Ri" } },
    { INDEX_op_shr_i32, { "r", "0", "Ri" } },
    { INDEX_op_sar_i32, { "r", "0", "Ri" } },

    { INDEX_op_rotl_i32, { "r", "r", "Ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "Ri" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext8u_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext16u_i32, { "r", "r" } },

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },

    { INDEX_op_add2_i32, { "r", "r", "0", "1", "r", "r" } },
    { INDEX_op_sub2_i32, { "r", "r", "0", "1", "r", "r" } },

    { INDEX_op_brcond_i32, { "r", "rC" } },
    { INDEX_op_setcond_i32, { "r", "r", "rC" } },
    { INDEX_op_movcond_i32, { "r", "r", "rC", "r", "0" } },
    { INDEX_op_deposit_i32, { "r", "0", "r" } },

    { INDEX_op_qemu_ld_i32, { "r", "L" } },
    { INDEX_op_qemu_ld_i64, { "r", "L" } },
    { INDEX_op_qemu_st_i32, { "L", "L" } },
    { INDEX_op_qemu_st_i64, { "L", "L" } },

    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },

    { INDEX_op_st8_i64, { "r", "r" } },
    { INDEX_op_st16_i64, { "r", "r" } },
    { INDEX_op_st32_i64, { "r", "r" } },
    { INDEX_op_st_i64, { "r", "r" } },

    { INDEX_op_add_i64, { "r", "r", "ri" } },
    { INDEX_op_sub_i64, { "r", "0", "ri" } },
    { INDEX_op_mul_i64, { "r", "0", "rK" } },

    { INDEX_op_div2_i64, { "b", "a", "0", "1", "r" } },
    { INDEX_op_divu2_i64, { "b", "a", "0", "1", "r" } },
    { INDEX_op_mulu2_i64, { "b", "a", "0", "r" } },

    { INDEX_op_and_i64, { "r", "0", "ri" } },
    { INDEX_op_or_i64, { "r", "0", "rO" } },
    { INDEX_op_xor_i64, { "r", "0", "rX" } },

    { INDEX_op_neg_i64, { "r", "r" } },

    { INDEX_op_shl_i64, { "r", "r", "Ri" } },
    { INDEX_op_shr_i64, { "r", "r", "Ri" } },
    { INDEX_op_sar_i64, { "r", "r", "Ri" } },

    { INDEX_op_rotl_i64, { "r", "r", "Ri" } },
    { INDEX_op_rotr_i64, { "r", "r", "Ri" } },

    { INDEX_op_ext8s_i64, { "r", "r" } },
    { INDEX_op_ext8u_i64, { "r", "r" } },
    { INDEX_op_ext16s_i64, { "r", "r" } },
    { INDEX_op_ext16u_i64, { "r", "r" } },
    { INDEX_op_ext32s_i64, { "r", "r" } },
    { INDEX_op_ext32u_i64, { "r", "r" } },

    { INDEX_op_bswap16_i64, { "r", "r" } },
    { INDEX_op_bswap32_i64, { "r", "r" } },
    { INDEX_op_bswap64_i64, { "r", "r" } },

    { INDEX_op_add2_i64, { "r", "r", "0", "1", "r", "r" } },
    { INDEX_op_sub2_i64, { "r", "r", "0", "1", "r", "r" } },

    { INDEX_op_brcond_i64, { "r", "rC" } },
    { INDEX_op_setcond_i64, { "r", "r", "rC" } },
    { INDEX_op_movcond_i64, { "r", "r", "rC", "r", "0" } },
    { INDEX_op_deposit_i64, { "r", "0", "r" } },

    { -1 },
};

static void query_facilities(void)
{
    unsigned long hwcap = qemu_getauxval(AT_HWCAP);

    /* Is STORE FACILITY LIST EXTENDED available?  Honestly, I believe this
       is present on all 64-bit systems, but let's check for it anyway.  */
    if (hwcap & HWCAP_S390_STFLE) {
        register int r0 __asm__("0");
        register void *r1 __asm__("1");

        /* stfle 0(%r1) */
        r1 = &facilities;
        asm volatile(".word 0xb2b0,0x1000"
                     : "=r"(r0) : "0"(0), "r"(r1) : "memory", "cc");
    }
}

static void tcg_target_init(TCGContext *s)
{
    query_facilities();

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffff);
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffff);

    tcg_regset_clear(tcg_target_call_clobber_regs);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R5);
    /* The r6 register is technically call-saved, but it's also a parameter
       register, so it can get killed by setup for the qemu_st helper.  */
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R6);
    /* The return register can be considered call-clobbered.  */
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R14);

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP0);
    /* XXX many insns can't be used with R0, so we better avoid it for now */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);

    tcg_add_target_add_op_defs(s390_op_defs);
}

static void tcg_target_qemu_prologue(TCGContext *s)
{
    tcg_target_long frame_size;

    /* stmg %r6,%r15,48(%r15) (save registers) */
    tcg_out_insn(s, RXY, STMG, TCG_REG_R6, TCG_REG_R15, TCG_REG_R15, 48);

    /* aghi %r15,-frame_size */
    frame_size = TCG_TARGET_CALL_STACK_OFFSET;
    frame_size += TCG_STATIC_CALL_ARGS_SIZE;
    frame_size += CPU_TEMP_BUF_NLONGS * sizeof(long);
    tcg_out_insn(s, RI, AGHI, TCG_REG_R15, -frame_size);

    tcg_set_frame(s, TCG_REG_CALL_STACK,
                  TCG_STATIC_CALL_ARGS_SIZE + TCG_TARGET_CALL_STACK_OFFSET,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

    if (GUEST_BASE >= 0x80000) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    /* br %r3 (go to TB) */
    tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, tcg_target_call_iarg_regs[1]);

    tb_ret_addr = s->code_ptr;

    /* lmg %r6,%r15,fs+48(%r15) (restore registers) */
    tcg_out_insn(s, RXY, LMG, TCG_REG_R6, TCG_REG_R15, TCG_REG_R15,
                 frame_size + 48);

    /* br %r14 (return) */
    tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, TCG_REG_R14);
}
