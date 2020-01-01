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

/* We only support generating code for 64-bit mode.  */
#if TCG_TARGET_REG_BITS != 64
#error "unsupported code generation mode"
#endif

#include "../tcg-pool.inc.c"
#include "elf.h"

/* ??? The translation blocks produced by TCG are generally small enough to
   be entirely reachable with a 16-bit displacement.  Leaving the option for
   a 32-bit displacement here Just In Case.  */
#define USE_LONG_BRANCHES 0

#define TCG_CT_CONST_S16   0x100
#define TCG_CT_CONST_S32   0x200
#define TCG_CT_CONST_S33   0x400
#define TCG_CT_CONST_ZERO  0x800

/* Several places within the instruction set 0 means "no register"
   rather than TCG_REG_R0.  */
#define TCG_REG_NONE    0

/* A scratch register that may be be used throughout the backend.  */
#define TCG_TMP0        TCG_REG_R1

/* A scratch register that holds a pointer to the beginning of the TB.
   We don't need this when we have pc-relative loads with the general
   instructions extension facility.  */
#define TCG_REG_TB      TCG_REG_R12
#define USE_REG_TB      (!(s390_facilities & FACILITY_GEN_INST_EXT))

#ifndef CONFIG_SOFTMMU
#define TCG_GUEST_BASE_REG TCG_REG_R13
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
    RIL_CLRL    = 0xc60f,
    RIL_CLGRL   = 0xc60a,
    RIL_CRL     = 0xc60d,
    RIL_CGRL    = 0xc608,
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
    RI_CHI      = 0xa70e,
    RI_CGHI     = 0xa70f,
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
    RIE_LOCGHI  = 0xec46,
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
    RRE_FLOGR   = 0xb983,
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
    RRF_NRK     = 0xb9f4,
    RRF_NGRK    = 0xb9e4,
    RRF_ORK     = 0xb9f6,
    RRF_OGRK    = 0xb9e6,
    RRF_SRK     = 0xb9f9,
    RRF_SGRK    = 0xb9e9,
    RRF_SLRK    = 0xb9fb,
    RRF_SLGRK   = 0xb9eb,
    RRF_XRK     = 0xb9f7,
    RRF_XGRK    = 0xb9e7,

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
    RSY_SLLK    = 0xebdf,
    RSY_SRAG    = 0xeb0a,
    RSY_SRAK    = 0xebdc,
    RSY_SRLG    = 0xeb0c,
    RSY_SRLK    = 0xebde,

    RS_SLL      = 0x89,
    RS_SRA      = 0x8a,
    RS_SRL      = 0x88,

    RXY_AG      = 0xe308,
    RXY_AY      = 0xe35a,
    RXY_CG      = 0xe320,
    RXY_CLG     = 0xe321,
    RXY_CLY     = 0xe355,
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
    RXY_NG      = 0xe380,
    RXY_OG      = 0xe381,
    RXY_STCY    = 0xe372,
    RXY_STG     = 0xe324,
    RXY_STHY    = 0xe370,
    RXY_STMG    = 0xeb24,
    RXY_STRV    = 0xe33e,
    RXY_STRVG   = 0xe32f,
    RXY_STRVH   = 0xe33f,
    RXY_STY     = 0xe350,
    RXY_XG      = 0xe382,

    RX_A        = 0x5a,
    RX_C        = 0x59,
    RX_L        = 0x58,
    RX_LA       = 0x41,
    RX_LH       = 0x48,
    RX_ST       = 0x50,
    RX_STC      = 0x42,
    RX_STH      = 0x40,

    NOP         = 0x0707,
} S390Opcode;

#ifdef CONFIG_DEBUG_TCG
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
uint64_t s390_facilities;

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    intptr_t pcrel2;
    uint32_t old;

    value += addend;
    pcrel2 = (tcg_insn_unit *)value - code_ptr;

    switch (type) {
    case R_390_PC16DBL:
        if (pcrel2 == (int16_t)pcrel2) {
            tcg_patch16(code_ptr, pcrel2);
            return true;
        }
        break;
    case R_390_PC32DBL:
        if (pcrel2 == (int32_t)pcrel2) {
            tcg_patch32(code_ptr, pcrel2);
            return true;
        }
        break;
    case R_390_20:
        if (value == sextract64(value, 0, 20)) {
            old = *(uint32_t *)code_ptr & 0xf00000ff;
            old |= ((value & 0xfff) << 16) | ((value & 0xff000) >> 4);
            tcg_patch32(code_ptr, old);
            return true;
        }
        break;
    default:
        g_assert_not_reached();
    }
    return false;
}

/* parse target specific constraints */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch (*ct_str++) {
    case 'r':                  /* all registers */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffff;
        break;
    case 'L':                  /* qemu_ld/st constraint */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffff;
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R2);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
        break;
    case 'a':                  /* force R2 for division */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_R2);
        break;
    case 'b':                  /* force R3 for division */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0;
        tcg_regset_set_reg(ct->u.regs, TCG_REG_R3);
        break;
    case 'A':
        ct->ct |= TCG_CT_CONST_S33;
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_S16;
        break;
    case 'J':
        ct->ct |= TCG_CT_CONST_S32;
        break;
    case 'Z':
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return NULL;
    }
    return ct_str;
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
    if (ct & TCG_CT_CONST_S16) {
        return val == (int16_t)val;
    } else if (ct & TCG_CT_CONST_S32) {
        return val == (int32_t)val;
    } else if (ct & TCG_CT_CONST_S33) {
        return val >= -0xffffffffll && val <= 0xffffffffll;
    } else if (ct & TCG_CT_CONST_ZERO) {
        return val == 0;
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

static void tcg_out_insn_RIE(TCGContext *s, S390Opcode op, TCGReg r1,
                             int i2, int m3)
{
    tcg_out16(s, (op & 0xff00) | (r1 << 4) | m3);
    tcg_out32(s, (i2 << 16) | (op & 0xff));
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

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg dst, TCGReg src)
{
    if (src != dst) {
        if (type == TCG_TYPE_I32) {
            tcg_out_insn(s, RR, LR, dst, src);
        } else {
            tcg_out_insn(s, RRE, LGR, dst, src);
        }
    }
    return true;
}

static const S390Opcode lli_insns[4] = {
    RI_LLILL, RI_LLILH, RI_LLIHL, RI_LLIHH
};

static bool maybe_out_small_movi(TCGContext *s, TCGType type,
                                 TCGReg ret, tcg_target_long sval)
{
    tcg_target_ulong uval = sval;
    int i;

    if (type == TCG_TYPE_I32) {
        uval = (uint32_t)sval;
        sval = (int32_t)sval;
    }

    /* Try all 32-bit insns that can load it in one go.  */
    if (sval >= -0x8000 && sval < 0x8000) {
        tcg_out_insn(s, RI, LGHI, ret, sval);
        return true;
    }

    for (i = 0; i < 4; i++) {
        tcg_target_long mask = 0xffffull << i*16;
        if ((uval & mask) == uval) {
            tcg_out_insn_RI(s, lli_insns[i], ret, uval >> i*16);
            return true;
        }
    }

    return false;
}

/* load a register with an immediate value */
static void tcg_out_movi_int(TCGContext *s, TCGType type, TCGReg ret,
                             tcg_target_long sval, bool in_prologue)
{
    tcg_target_ulong uval;

    /* Try all 32-bit insns that can load it in one go.  */
    if (maybe_out_small_movi(s, type, ret, sval)) {
        return;
    }

    uval = sval;
    if (type == TCG_TYPE_I32) {
        uval = (uint32_t)sval;
        sval = (int32_t)sval;
    }

    /* Try all 48-bit insns that can load it in one go.  */
    if (s390_facilities & FACILITY_EXT_IMM) {
        if (sval == (int32_t)sval) {
            tcg_out_insn(s, RIL, LGFI, ret, sval);
            return;
        }
        if (uval <= 0xffffffff) {
            tcg_out_insn(s, RIL, LLILF, ret, uval);
            return;
        }
        if ((uval & 0xffffffff) == 0) {
            tcg_out_insn(s, RIL, LLIHF, ret, uval >> 32);
            return;
        }
    }

    /* Try for PC-relative address load.  For odd addresses,
       attempt to use an offset from the start of the TB.  */
    if ((sval & 1) == 0) {
        ptrdiff_t off = tcg_pcrel_diff(s, (void *)sval) >> 1;
        if (off == (int32_t)off) {
            tcg_out_insn(s, RIL, LARL, ret, off);
            return;
        }
    } else if (USE_REG_TB && !in_prologue) {
        ptrdiff_t off = sval - (uintptr_t)s->code_gen_ptr;
        if (off == sextract64(off, 0, 20)) {
            /* This is certain to be an address within TB, and therefore
               OFF will be negative; don't try RX_LA.  */
            tcg_out_insn(s, RXY, LAY, ret, TCG_REG_TB, TCG_REG_NONE, off);
            return;
        }
    }

    /* A 32-bit unsigned value can be loaded in 2 insns.  And given
       that LLILL, LLIHL, LLILF above did not succeed, we know that
       both insns are required.  */
    if (uval <= 0xffffffff) {
        tcg_out_insn(s, RI, LLILL, ret, uval);
        tcg_out_insn(s, RI, IILH, ret, uval >> 16);
        return;
    }

    /* Otherwise, stuff it in the constant pool.  */
    if (s390_facilities & FACILITY_GEN_INST_EXT) {
        tcg_out_insn(s, RIL, LGRL, ret, 0);
        new_pool_label(s, sval, R_390_PC32DBL, s->code_ptr - 2, 2);
    } else if (USE_REG_TB && !in_prologue) {
        tcg_out_insn(s, RXY, LG, ret, TCG_REG_TB, TCG_REG_NONE, 0);
        new_pool_label(s, sval, R_390_20, s->code_ptr - 2,
                       -(intptr_t)s->code_gen_ptr);
    } else {
        TCGReg base = ret ? ret : TCG_TMP0;
        tcg_out_insn(s, RIL, LARL, base, 0);
        new_pool_label(s, sval, R_390_PC32DBL, s->code_ptr - 2, 2);
        tcg_out_insn(s, RXY, LG, ret, base, TCG_REG_NONE, 0);
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long sval)
{
    tcg_out_movi_int(s, type, ret, sval, false);
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

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    return false;
}

/* load data from an absolute host address */
static void tcg_out_ld_abs(TCGContext *s, TCGType type, TCGReg dest, void *abs)
{
    intptr_t addr = (intptr_t)abs;

    if ((s390_facilities & FACILITY_GEN_INST_EXT) && !(addr & 1)) {
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
    if (USE_REG_TB) {
        ptrdiff_t disp = abs - (void *)s->code_gen_ptr;
        if (disp == sextract64(disp, 0, 20)) {
            tcg_out_ld(s, type, dest, TCG_REG_TB, disp);
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
    if (s390_facilities & FACILITY_EXT_IMM) {
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
    if (s390_facilities & FACILITY_EXT_IMM) {
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
    if (s390_facilities & FACILITY_EXT_IMM) {
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
    if (s390_facilities & FACILITY_EXT_IMM) {
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

static void tgen_andi_risbg(TCGContext *s, TCGReg out, TCGReg in, uint64_t val)
{
    int msb, lsb;
    if ((val & 0x8000000000000001ull) == 0x8000000000000001ull) {
        /* Achieve wraparound by swapping msb and lsb.  */
        msb = 64 - ctz64(~val);
        lsb = clz64(~val) - 1;
    } else {
        msb = clz64(val);
        lsb = 63 - ctz64(val);
    }
    tcg_out_risbg(s, out, in, msb, lsb, 0, 1);
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
    if (s390_facilities & FACILITY_EXT_IMM) {
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
    if (s390_facilities & FACILITY_EXT_IMM) {
        for (i = 0; i < 2; i++) {
            tcg_target_ulong mask = ~(0xffffffffull << i*32);
            if (((val | ~valid) & mask) == mask) {
                tcg_out_insn_RIL(s, nif_insns[i], dest, val >> i*32);
                return;
            }
        }
    }
    if ((s390_facilities & FACILITY_GEN_INST_EXT) && risbg_mask(val)) {
        tgen_andi_risbg(s, dest, dest, val);
        return;
    }

    /* Use the constant pool if USE_REG_TB, but not for small constants.  */
    if (USE_REG_TB) {
        if (!maybe_out_small_movi(s, type, TCG_TMP0, val)) {
            tcg_out_insn(s, RXY, NG, dest, TCG_REG_TB, TCG_REG_NONE, 0);
            new_pool_label(s, val & valid, R_390_20, s->code_ptr - 2,
                           -(intptr_t)s->code_gen_ptr);
            return;
        }
    } else {
        tcg_out_movi(s, type, TCG_TMP0, val);
    }
    if (type == TCG_TYPE_I32) {
        tcg_out_insn(s, RR, NR, dest, TCG_TMP0);
    } else {
        tcg_out_insn(s, RRE, NGR, dest, TCG_TMP0);
    }
}

static void tgen_ori(TCGContext *s, TCGType type, TCGReg dest, uint64_t val)
{
    static const S390Opcode oi_insns[4] = {
        RI_OILL, RI_OILH, RI_OIHL, RI_OIHH
    };
    static const S390Opcode oif_insns[2] = {
        RIL_OILF, RIL_OIHF
    };

    int i;

    /* Look for no-op.  */
    if (unlikely(val == 0)) {
        return;
    }

    /* Try all 32-bit insns that can perform it in one go.  */
    for (i = 0; i < 4; i++) {
        tcg_target_ulong mask = (0xffffull << i*16);
        if ((val & mask) != 0 && (val & ~mask) == 0) {
            tcg_out_insn_RI(s, oi_insns[i], dest, val >> i*16);
            return;
        }
    }

    /* Try all 48-bit insns that can perform it in one go.  */
    if (s390_facilities & FACILITY_EXT_IMM) {
        for (i = 0; i < 2; i++) {
            tcg_target_ulong mask = (0xffffffffull << i*32);
            if ((val & mask) != 0 && (val & ~mask) == 0) {
                tcg_out_insn_RIL(s, oif_insns[i], dest, val >> i*32);
                return;
            }
        }
    }

    /* Use the constant pool if USE_REG_TB, but not for small constants.  */
    if (maybe_out_small_movi(s, type, TCG_TMP0, val)) {
        if (type == TCG_TYPE_I32) {
            tcg_out_insn(s, RR, OR, dest, TCG_TMP0);
        } else {
            tcg_out_insn(s, RRE, OGR, dest, TCG_TMP0);
        }
    } else if (USE_REG_TB) {
        tcg_out_insn(s, RXY, OG, dest, TCG_REG_TB, TCG_REG_NONE, 0);
        new_pool_label(s, val, R_390_20, s->code_ptr - 2,
                       -(intptr_t)s->code_gen_ptr);
    } else {
        /* Perform the OR via sequential modifications to the high and
           low parts.  Do this via recursion to handle 16-bit vs 32-bit
           masks in each half.  */
        tcg_debug_assert(s390_facilities & FACILITY_EXT_IMM);
        tgen_ori(s, type, dest, val & 0x00000000ffffffffull);
        tgen_ori(s, type, dest, val & 0xffffffff00000000ull);
    }
}

static void tgen_xori(TCGContext *s, TCGType type, TCGReg dest, uint64_t val)
{
    /* Try all 48-bit insns that can perform it in one go.  */
    if (s390_facilities & FACILITY_EXT_IMM) {
        if ((val & 0xffffffff00000000ull) == 0) {
            tcg_out_insn(s, RIL, XILF, dest, val);
            return;
        }
        if ((val & 0x00000000ffffffffull) == 0) {
            tcg_out_insn(s, RIL, XIHF, dest, val >> 32);
            return;
        }
    }

    /* Use the constant pool if USE_REG_TB, but not for small constants.  */
    if (maybe_out_small_movi(s, type, TCG_TMP0, val)) {
        if (type == TCG_TYPE_I32) {
            tcg_out_insn(s, RR, XR, dest, TCG_TMP0);
        } else {
            tcg_out_insn(s, RRE, XGR, dest, TCG_TMP0);
        }
    } else if (USE_REG_TB) {
        tcg_out_insn(s, RXY, XG, dest, TCG_REG_TB, TCG_REG_NONE, 0);
        new_pool_label(s, val, R_390_20, s->code_ptr - 2,
                       -(intptr_t)s->code_gen_ptr);
    } else {
        /* Perform the xor by parts.  */
        tcg_debug_assert(s390_facilities & FACILITY_EXT_IMM);
        if (val & 0xffffffff) {
            tcg_out_insn(s, RIL, XILF, dest, val);
        }
        if (val > 0xffffffff) {
            tcg_out_insn(s, RIL, XIHF, dest, val >> 32);
        }
    }
}

static int tgen_cmp(TCGContext *s, TCGType type, TCGCond c, TCGReg r1,
                    TCGArg c2, bool c2const, bool need_carry)
{
    bool is_unsigned = is_unsigned_cond(c);
    S390Opcode op;

    if (c2const) {
        if (c2 == 0) {
            if (!(is_unsigned && need_carry)) {
                if (type == TCG_TYPE_I32) {
                    tcg_out_insn(s, RR, LTR, r1, r1);
                } else {
                    tcg_out_insn(s, RRE, LTGR, r1, r1);
                }
                return tcg_cond_to_ltr_cond[c];
            }
        }

        if (!is_unsigned && c2 == (int16_t)c2) {
            op = (type == TCG_TYPE_I32 ? RI_CHI : RI_CGHI);
            tcg_out_insn_RI(s, op, r1, c2);
            goto exit;
        }

        if (s390_facilities & FACILITY_EXT_IMM) {
            if (type == TCG_TYPE_I32) {
                op = (is_unsigned ? RIL_CLFI : RIL_CFI);
                tcg_out_insn_RIL(s, op, r1, c2);
                goto exit;
            } else if (c2 == (is_unsigned ? (uint32_t)c2 : (int32_t)c2)) {
                op = (is_unsigned ? RIL_CLGFI : RIL_CGFI);
                tcg_out_insn_RIL(s, op, r1, c2);
                goto exit;
            }
        }

        /* Use the constant pool, but not for small constants.  */
        if (maybe_out_small_movi(s, type, TCG_TMP0, c2)) {
            c2 = TCG_TMP0;
            /* fall through to reg-reg */
        } else if (USE_REG_TB) {
            if (type == TCG_TYPE_I32) {
                op = (is_unsigned ? RXY_CLY : RXY_CY);
                tcg_out_insn_RXY(s, op, r1, TCG_REG_TB, TCG_REG_NONE, 0);
                new_pool_label(s, (uint32_t)c2, R_390_20, s->code_ptr - 2,
                               4 - (intptr_t)s->code_gen_ptr);
            } else {
                op = (is_unsigned ? RXY_CLG : RXY_CG);
                tcg_out_insn_RXY(s, op, r1, TCG_REG_TB, TCG_REG_NONE, 0);
                new_pool_label(s, c2, R_390_20, s->code_ptr - 2,
                               -(intptr_t)s->code_gen_ptr);
            }
            goto exit;
        } else {
            if (type == TCG_TYPE_I32) {
                op = (is_unsigned ? RIL_CLRL : RIL_CRL);
                tcg_out_insn_RIL(s, op, r1, 0);
                new_pool_label(s, (uint32_t)c2, R_390_PC32DBL,
                               s->code_ptr - 2, 2 + 4);
            } else {
                op = (is_unsigned ? RIL_CLGRL : RIL_CGRL);
                tcg_out_insn_RIL(s, op, r1, 0);
                new_pool_label(s, c2, R_390_PC32DBL, s->code_ptr - 2, 2);
            }
            goto exit;
        }
    }

    if (type == TCG_TYPE_I32) {
        op = (is_unsigned ? RR_CLR : RR_CR);
        tcg_out_insn_RR(s, op, r1, c2);
    } else {
        op = (is_unsigned ? RRE_CLGR : RRE_CGR);
        tcg_out_insn_RRE(s, op, r1, c2);
    }

 exit:
    return tcg_cond_to_s390_cond[c];
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg dest, TCGReg c1, TCGArg c2, int c2const)
{
    int cc;
    bool have_loc;

    /* With LOC2, we can always emit the minimum 3 insns.  */
    if (s390_facilities & FACILITY_LOAD_ON_COND2) {
        /* Emit: d = 0, d = (cc ? 1 : d).  */
        cc = tgen_cmp(s, type, cond, c1, c2, c2const, false);
        tcg_out_movi(s, TCG_TYPE_I64, dest, 0);
        tcg_out_insn(s, RIE, LOCGHI, dest, 1, cc);
        return;
    }

    have_loc = (s390_facilities & FACILITY_LOAD_ON_COND) != 0;

    /* For HAVE_LOC, only the paths through GTU/GT/LEU/LE are smaller.  */
 restart:
    switch (cond) {
    case TCG_COND_NE:
        /* X != 0 is X > 0.  */
        if (c2const && c2 == 0) {
            cond = TCG_COND_GTU;
        } else {
            break;
        }
        /* fallthru */

    case TCG_COND_GTU:
    case TCG_COND_GT:
        /* The result of a compare has CC=2 for GT and CC=3 unused.
           ADD LOGICAL WITH CARRY considers (CC & 2) the carry bit.  */
        tgen_cmp(s, type, cond, c1, c2, c2const, true);
        tcg_out_movi(s, type, dest, 0);
        tcg_out_insn(s, RRE, ALCGR, dest, dest);
        return;

    case TCG_COND_EQ:
        /* X == 0 is X <= 0.  */
        if (c2const && c2 == 0) {
            cond = TCG_COND_LEU;
        } else {
            break;
        }
        /* fallthru */

    case TCG_COND_LEU:
    case TCG_COND_LE:
        /* As above, but we're looking for borrow, or !carry.
           The second insn computes d - d - borrow, or -1 for true
           and 0 for false.  So we must mask to 1 bit afterward.  */
        tgen_cmp(s, type, cond, c1, c2, c2const, true);
        tcg_out_insn(s, RRE, SLBGR, dest, dest);
        tgen_andi(s, type, dest, 1);
        return;

    case TCG_COND_GEU:
    case TCG_COND_LTU:
    case TCG_COND_LT:
    case TCG_COND_GE:
        /* Swap operands so that we can use LEU/GTU/GT/LE.  */
        if (c2const) {
            if (have_loc) {
                break;
            }
            tcg_out_movi(s, type, TCG_TMP0, c2);
            c2 = c1;
            c2const = 0;
            c1 = TCG_TMP0;
        } else {
            TCGReg t = c1;
            c1 = c2;
            c2 = t;
        }
        cond = tcg_swap_cond(cond);
        goto restart;

    default:
        g_assert_not_reached();
    }

    cc = tgen_cmp(s, type, cond, c1, c2, c2const, false);
    if (have_loc) {
        /* Emit: d = 0, t = 1, d = (cc ? t : d).  */
        tcg_out_movi(s, TCG_TYPE_I64, dest, 0);
        tcg_out_movi(s, TCG_TYPE_I64, TCG_TMP0, 1);
        tcg_out_insn(s, RRF, LOCGR, dest, TCG_TMP0, cc);
    } else {
        /* Emit: d = 1; if (cc) goto over; d = 0; over:  */
        tcg_out_movi(s, type, dest, 1);
        tcg_out_insn(s, RI, BRC, cc, (4 + 4) >> 1);
        tcg_out_movi(s, type, dest, 0);
    }
}

static void tgen_movcond(TCGContext *s, TCGType type, TCGCond c, TCGReg dest,
                         TCGReg c1, TCGArg c2, int c2const,
                         TCGArg v3, int v3const)
{
    int cc;
    if (s390_facilities & FACILITY_LOAD_ON_COND) {
        cc = tgen_cmp(s, type, c, c1, c2, c2const, false);
        if (v3const) {
            tcg_out_insn(s, RIE, LOCGHI, dest, v3, cc);
        } else {
            tcg_out_insn(s, RRF, LOCGR, dest, v3, cc);
        }
    } else {
        c = tcg_invert_cond(c);
        cc = tgen_cmp(s, type, c, c1, c2, c2const, false);

        /* Emit: if (cc) goto over; dest = r3; over:  */
        tcg_out_insn(s, RI, BRC, cc, (4 + 4) >> 1);
        tcg_out_insn(s, RRE, LGR, dest, v3);
    }
}

static void tgen_clz(TCGContext *s, TCGReg dest, TCGReg a1,
                     TCGArg a2, int a2const)
{
    /* Since this sets both R and R+1, we have no choice but to store the
       result into R0, allowing R1 == TCG_TMP0 to be clobbered as well.  */
    QEMU_BUILD_BUG_ON(TCG_TMP0 != TCG_REG_R1);
    tcg_out_insn(s, RRE, FLOGR, TCG_REG_R0, a1);

    if (a2const && a2 == 64) {
        tcg_out_mov(s, TCG_TYPE_I64, dest, TCG_REG_R0);
    } else {
        if (a2const) {
            tcg_out_movi(s, TCG_TYPE_I64, dest, a2);
        } else {
            tcg_out_mov(s, TCG_TYPE_I64, dest, a2);
        }
        if (s390_facilities & FACILITY_LOAD_ON_COND) {
            /* Emit: if (one bit found) dest = r0.  */
            tcg_out_insn(s, RRF, LOCGR, dest, TCG_REG_R0, 2);
        } else {
            /* Emit: if (no one bit found) goto over; dest = r0; over:  */
            tcg_out_insn(s, RI, BRC, 8, (4 + 4) >> 1);
            tcg_out_insn(s, RRE, LGR, dest, TCG_REG_R0);
        }
    }
}

static void tgen_deposit(TCGContext *s, TCGReg dest, TCGReg src,
                         int ofs, int len, int z)
{
    int lsb = (63 - ofs);
    int msb = lsb - (len - 1);
    tcg_out_risbg(s, dest, src, msb, lsb, ofs, z);
}

static void tgen_extract(TCGContext *s, TCGReg dest, TCGReg src,
                         int ofs, int len)
{
    tcg_out_risbg(s, dest, src, 64 - len, 63, 64 - ofs, 1);
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

static void tgen_branch(TCGContext *s, int cc, TCGLabel *l)
{
    if (l->has_value) {
        tgen_gotoi(s, cc, l->u.value_ptr);
    } else if (USE_LONG_BRANCHES) {
        tcg_out16(s, RIL_BRCL | (cc << 4));
        tcg_out_reloc(s, s->code_ptr, R_390_PC32DBL, l, 2);
        s->code_ptr += 2;
    } else {
        tcg_out16(s, RI_BRC | (cc << 4));
        tcg_out_reloc(s, s->code_ptr, R_390_PC16DBL, l, 2);
        s->code_ptr += 1;
    }
}

static void tgen_compare_branch(TCGContext *s, S390Opcode opc, int cc,
                                TCGReg r1, TCGReg r2, TCGLabel *l)
{
    intptr_t off = 0;

    if (l->has_value) {
        off = l->u.value_ptr - s->code_ptr;
        tcg_debug_assert(off == (int16_t)off);
    } else {
        tcg_out_reloc(s, s->code_ptr + 1, R_390_PC16DBL, l, 2);
    }

    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | r2);
    tcg_out16(s, off);
    tcg_out16(s, cc << 12 | (opc & 0xff));
}

static void tgen_compare_imm_branch(TCGContext *s, S390Opcode opc, int cc,
                                    TCGReg r1, int i2, TCGLabel *l)
{
    tcg_target_long off = 0;

    if (l->has_value) {
        off = l->u.value_ptr - s->code_ptr;
        tcg_debug_assert(off == (int16_t)off);
    } else {
        tcg_out_reloc(s, s->code_ptr + 1, R_390_PC16DBL, l, 2);
    }

    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | cc);
    tcg_out16(s, off);
    tcg_out16(s, (i2 << 8) | (opc & 0xff));
}

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond c,
                        TCGReg r1, TCGArg c2, int c2const, TCGLabel *l)
{
    int cc;

    if (s390_facilities & FACILITY_GEN_INST_EXT) {
        bool is_unsigned = is_unsigned_cond(c);
        bool in_range;
        S390Opcode opc;

        cc = tcg_cond_to_s390_cond[c];

        if (!c2const) {
            opc = (type == TCG_TYPE_I32
                   ? (is_unsigned ? RIE_CLRJ : RIE_CRJ)
                   : (is_unsigned ? RIE_CLGRJ : RIE_CGRJ));
            tgen_compare_branch(s, opc, cc, r1, c2, l);
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
            tgen_compare_imm_branch(s, opc, cc, r1, c2, l);
            return;
        }
    }

    cc = tgen_cmp(s, type, c, r1, c2, c2const, false);
    tgen_branch(s, cc, l);
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

static void tcg_out_qemu_ld_direct(TCGContext *s, MemOp opc, TCGReg data,
                                   TCGReg base, TCGReg index, int disp)
{
    switch (opc & (MO_SSIZE | MO_BSWAP)) {
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

static void tcg_out_qemu_st_direct(TCGContext *s, MemOp opc, TCGReg data,
                                   TCGReg base, TCGReg index, int disp)
{
    switch (opc & (MO_SIZE | MO_BSWAP)) {
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
#include "../tcg-ldst.inc.c"

/* We're expecting to use a 20-bit negative offset on the tlb memory ops.  */
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -(1 << 19));

/* Load and compare a TLB entry, leaving the flags set.  Loads the TLB
   addend into R2.  Returns a register with the santitized guest address.  */
static TCGReg tcg_out_tlb_read(TCGContext *s, TCGReg addr_reg, MemOp opc,
                               int mem_index, bool is_ld)
{
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_bits = get_alignment_bits(opc);
    unsigned s_mask = (1 << s_bits) - 1;
    unsigned a_mask = (1 << a_bits) - 1;
    int fast_off = TLB_MASK_TABLE_OFS(mem_index);
    int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
    int table_off = fast_off + offsetof(CPUTLBDescFast, table);
    int ofs, a_off;
    uint64_t tlb_mask;

    tcg_out_sh64(s, RSY_SRLG, TCG_REG_R2, addr_reg, TCG_REG_NONE,
                 TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    tcg_out_insn(s, RXY, NG, TCG_REG_R2, TCG_AREG0, TCG_REG_NONE, mask_off);
    tcg_out_insn(s, RXY, AG, TCG_REG_R2, TCG_AREG0, TCG_REG_NONE, table_off);

    /* For aligned accesses, we check the first byte and include the alignment
       bits within the address.  For unaligned access, we check that we don't
       cross pages using the address of the last byte of the access.  */
    a_off = (a_bits >= s_bits ? 0 : s_mask - a_mask);
    tlb_mask = (uint64_t)TARGET_PAGE_MASK | a_mask;
    if ((s390_facilities & FACILITY_GEN_INST_EXT) && a_off == 0) {
        tgen_andi_risbg(s, TCG_REG_R3, addr_reg, tlb_mask);
    } else {
        tcg_out_insn(s, RX, LA, TCG_REG_R3, addr_reg, TCG_REG_NONE, a_off);
        tgen_andi(s, TCG_TYPE_TL, TCG_REG_R3, tlb_mask);
    }

    if (is_ld) {
        ofs = offsetof(CPUTLBEntry, addr_read);
    } else {
        ofs = offsetof(CPUTLBEntry, addr_write);
    }
    if (TARGET_LONG_BITS == 32) {
        tcg_out_insn(s, RX, C, TCG_REG_R3, TCG_REG_R2, TCG_REG_NONE, ofs);
    } else {
        tcg_out_insn(s, RXY, CG, TCG_REG_R3, TCG_REG_R2, TCG_REG_NONE, ofs);
    }

    tcg_out_insn(s, RXY, LG, TCG_REG_R2, TCG_REG_R2, TCG_REG_NONE,
                 offsetof(CPUTLBEntry, addend));

    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, TCG_REG_R3, addr_reg);
        return TCG_REG_R3;
    }
    return addr_reg;
}

static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOpIdx oi,
                                TCGReg data, TCGReg addr,
                                tcg_insn_unit *raddr, tcg_insn_unit *label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->oi = oi;
    label->datalo_reg = data;
    label->addrlo_reg = addr;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr;
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg addr_reg = lb->addrlo_reg;
    TCGReg data_reg = lb->datalo_reg;
    TCGMemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);

    if (!patch_reloc(lb->label_ptr[0], R_390_PC16DBL,
                     (intptr_t)s->code_ptr, 2)) {
        return false;
    }

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R2, TCG_AREG0);
    if (TARGET_LONG_BITS == 64) {
        tcg_out_mov(s, TCG_TYPE_I64, TCG_REG_R3, addr_reg);
    }
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R4, oi);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R5, (uintptr_t)lb->raddr);
    tcg_out_call(s, qemu_ld_helpers[opc & (MO_BSWAP | MO_SSIZE)]);
    tcg_out_mov(s, TCG_TYPE_I64, data_reg, TCG_REG_R2);

    tgen_gotoi(s, S390_CC_ALWAYS, lb->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGReg addr_reg = lb->addrlo_reg;
    TCGReg data_reg = lb->datalo_reg;
    TCGMemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);

    if (!patch_reloc(lb->label_ptr[0], R_390_PC16DBL,
                     (intptr_t)s->code_ptr, 2)) {
        return false;
    }

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
    tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R5, oi);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R6, (uintptr_t)lb->raddr);
    tcg_out_call(s, qemu_st_helpers[opc & (MO_BSWAP | MO_SIZE)]);

    tgen_gotoi(s, S390_CC_ALWAYS, lb->raddr);
    return true;
}
#else
static void tcg_prepare_user_ldst(TCGContext *s, TCGReg *addr_reg,
                                  TCGReg *index_reg, tcg_target_long *disp)
{
    if (TARGET_LONG_BITS == 32) {
        tgen_ext32u(s, TCG_TMP0, *addr_reg);
        *addr_reg = TCG_TMP0;
    }
    if (guest_base < 0x80000) {
        *index_reg = TCG_REG_NONE;
        *disp = guest_base;
    } else {
        *index_reg = TCG_GUEST_BASE_REG;
        *disp = 0;
    }
}
#endif /* CONFIG_SOFTMMU */

static void tcg_out_qemu_ld(TCGContext* s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOpIdx oi)
{
    MemOp opc = get_memop(oi);
#ifdef CONFIG_SOFTMMU
    unsigned mem_index = get_mmuidx(oi);
    tcg_insn_unit *label_ptr;
    TCGReg base_reg;

    base_reg = tcg_out_tlb_read(s, addr_reg, opc, mem_index, 1);

    tcg_out16(s, RI_BRC | (S390_CC_NE << 4));
    label_ptr = s->code_ptr;
    s->code_ptr += 1;

    tcg_out_qemu_ld_direct(s, opc, data_reg, base_reg, TCG_REG_R2, 0);

    add_qemu_ldst_label(s, 1, oi, data_reg, addr_reg, s->code_ptr, label_ptr);
#else
    TCGReg index_reg;
    tcg_target_long disp;

    tcg_prepare_user_ldst(s, &addr_reg, &index_reg, &disp);
    tcg_out_qemu_ld_direct(s, opc, data_reg, addr_reg, index_reg, disp);
#endif
}

static void tcg_out_qemu_st(TCGContext* s, TCGReg data_reg, TCGReg addr_reg,
                            TCGMemOpIdx oi)
{
    MemOp opc = get_memop(oi);
#ifdef CONFIG_SOFTMMU
    unsigned mem_index = get_mmuidx(oi);
    tcg_insn_unit *label_ptr;
    TCGReg base_reg;

    base_reg = tcg_out_tlb_read(s, addr_reg, opc, mem_index, 0);

    tcg_out16(s, RI_BRC | (S390_CC_NE << 4));
    label_ptr = s->code_ptr;
    s->code_ptr += 1;

    tcg_out_qemu_st_direct(s, opc, data_reg, base_reg, TCG_REG_R2, 0);

    add_qemu_ldst_label(s, 0, oi, data_reg, addr_reg, s->code_ptr, label_ptr);
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
    S390Opcode op, op2;
    TCGArg a0, a1, a2;

    switch (opc) {
    case INDEX_op_exit_tb:
        /* Reuse the zeroing that exists for goto_ptr.  */
        a0 = args[0];
        if (a0 == 0) {
            tgen_gotoi(s, S390_CC_ALWAYS, s->code_gen_epilogue);
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, a0);
            tgen_gotoi(s, S390_CC_ALWAYS, tb_ret_addr);
        }
        break;

    case INDEX_op_goto_tb:
        a0 = args[0];
        if (s->tb_jmp_insn_offset) {
            /* branch displacement must be aligned for atomic patching;
             * see if we need to add extra nop before branch
             */
            if (!QEMU_PTR_IS_ALIGNED(s->code_ptr + 1, 4)) {
                tcg_out16(s, NOP);
            }
            tcg_debug_assert(!USE_REG_TB);
            tcg_out16(s, RIL_BRCL | (S390_CC_ALWAYS << 4));
            s->tb_jmp_insn_offset[a0] = tcg_current_code_size(s);
            s->code_ptr += 2;
        } else {
            /* load address stored at s->tb_jmp_target_addr + a0 */
            tcg_out_ld_abs(s, TCG_TYPE_PTR, TCG_REG_TB,
                           s->tb_jmp_target_addr + a0);
            /* and go there */
            tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, TCG_REG_TB);
        }
        set_jmp_reset_offset(s, a0);

        /* For the unlinked path of goto_tb, we need to reset
           TCG_REG_TB to the beginning of this TB.  */
        if (USE_REG_TB) {
            int ofs = -tcg_current_code_size(s);
            assert(ofs == (int16_t)ofs);
            tcg_out_insn(s, RI, AGHI, TCG_REG_TB, ofs);
        }
        break;

    case INDEX_op_goto_ptr:
        a0 = args[0];
        if (USE_REG_TB) {
            tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_TB, a0);
        }
        tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, a0);
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
                if (s390_facilities & FACILITY_EXT_IMM) {
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
        } else if (a0 == a1) {
            tcg_out_insn(s, RR, SR, a0, a2);
        } else {
            tcg_out_insn(s, RRF, SRK, a0, a1, a2);
        }
        break;

    case INDEX_op_and_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tgen_andi(s, TCG_TYPE_I32, a0, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RR, NR, a0, a2);
        } else {
            tcg_out_insn(s, RRF, NRK, a0, a1, a2);
        }
        break;
    case INDEX_op_or_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tgen_ori(s, TCG_TYPE_I32, a0, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RR, OR, a0, a2);
        } else {
            tcg_out_insn(s, RRF, ORK, a0, a1, a2);
        }
        break;
    case INDEX_op_xor_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tgen_xori(s, TCG_TYPE_I32, a0, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RR, XR, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRF, XRK, a0, a1, a2);
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
        op2 = RSY_SLLK;
    do_shift32:
        a0 = args[0], a1 = args[1], a2 = (int32_t)args[2];
        if (a0 == a1) {
            if (const_args[2]) {
                tcg_out_sh32(s, op, a0, TCG_REG_NONE, a2);
            } else {
                tcg_out_sh32(s, op, a0, a2, 0);
            }
        } else {
            /* Using tcg_out_sh64 here for the format; it is a 32-bit shift.  */
            if (const_args[2]) {
                tcg_out_sh64(s, op2, a0, a1, TCG_REG_NONE, a2);
            } else {
                tcg_out_sh64(s, op2, a0, a1, a2, 0);
            }
        }
        break;
    case INDEX_op_shr_i32:
        op = RS_SRL;
        op2 = RSY_SRLK;
        goto do_shift32;
    case INDEX_op_sar_i32:
        op = RS_SRA;
        op2 = RSY_SRAK;
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
        if (const_args[4]) {
            tcg_out_insn(s, RIL, ALFI, args[0], args[4]);
        } else {
            tcg_out_insn(s, RR, ALR, args[0], args[4]);
        }
        tcg_out_insn(s, RRE, ALCR, args[1], args[5]);
        break;
    case INDEX_op_sub2_i32:
        if (const_args[4]) {
            tcg_out_insn(s, RIL, SLFI, args[0], args[4]);
        } else {
            tcg_out_insn(s, RR, SLR, args[0], args[4]);
        }
        tcg_out_insn(s, RRE, SLBR, args[1], args[5]);
        break;

    case INDEX_op_br:
        tgen_branch(s, S390_CC_ALWAYS, arg_label(args[0]));
        break;

    case INDEX_op_brcond_i32:
        tgen_brcond(s, TCG_TYPE_I32, args[2], args[0],
                    args[1], const_args[1], arg_label(args[3]));
        break;
    case INDEX_op_setcond_i32:
        tgen_setcond(s, TCG_TYPE_I32, args[3], args[0], args[1],
                     args[2], const_args[2]);
        break;
    case INDEX_op_movcond_i32:
        tgen_movcond(s, TCG_TYPE_I32, args[5], args[0], args[1],
                     args[2], const_args[2], args[3], const_args[3]);
        break;

    case INDEX_op_qemu_ld_i32:
        /* ??? Technically we can use a non-extending instruction.  */
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, args[0], args[1], args[2]);
        break;
    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, args[0], args[1], args[2]);
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
                if (s390_facilities & FACILITY_EXT_IMM) {
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
        } else if (a0 == a1) {
            tcg_out_insn(s, RRE, SGR, a0, a2);
        } else {
            tcg_out_insn(s, RRF, SGRK, a0, a1, a2);
        }
        break;

    case INDEX_op_and_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_andi(s, TCG_TYPE_I64, args[0], args[2]);
        } else if (a0 == a1) {
            tcg_out_insn(s, RRE, NGR, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRF, NGRK, a0, a1, a2);
        }
        break;
    case INDEX_op_or_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_ori(s, TCG_TYPE_I64, a0, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RRE, OGR, a0, a2);
        } else {
            tcg_out_insn(s, RRF, OGRK, a0, a1, a2);
        }
        break;
    case INDEX_op_xor_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_xori(s, TCG_TYPE_I64, a0, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RRE, XGR, a0, a2);
        } else {
            tcg_out_insn(s, RRF, XGRK, a0, a1, a2);
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
    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        tgen_ext32s(s, args[0], args[1]);
        break;
    case INDEX_op_ext8u_i64:
        tgen_ext8u(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ext16u_i64:
        tgen_ext16u(s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_extu_i32_i64:
    case INDEX_op_ext32u_i64:
        tgen_ext32u(s, args[0], args[1]);
        break;

    case INDEX_op_add2_i64:
        if (const_args[4]) {
            if ((int64_t)args[4] >= 0) {
                tcg_out_insn(s, RIL, ALGFI, args[0], args[4]);
            } else {
                tcg_out_insn(s, RIL, SLGFI, args[0], -args[4]);
            }
        } else {
            tcg_out_insn(s, RRE, ALGR, args[0], args[4]);
        }
        tcg_out_insn(s, RRE, ALCGR, args[1], args[5]);
        break;
    case INDEX_op_sub2_i64:
        if (const_args[4]) {
            if ((int64_t)args[4] >= 0) {
                tcg_out_insn(s, RIL, SLGFI, args[0], args[4]);
            } else {
                tcg_out_insn(s, RIL, ALGFI, args[0], -args[4]);
            }
        } else {
            tcg_out_insn(s, RRE, SLGR, args[0], args[4]);
        }
        tcg_out_insn(s, RRE, SLBGR, args[1], args[5]);
        break;

    case INDEX_op_brcond_i64:
        tgen_brcond(s, TCG_TYPE_I64, args[2], args[0],
                    args[1], const_args[1], arg_label(args[3]));
        break;
    case INDEX_op_setcond_i64:
        tgen_setcond(s, TCG_TYPE_I64, args[3], args[0], args[1],
                     args[2], const_args[2]);
        break;
    case INDEX_op_movcond_i64:
        tgen_movcond(s, TCG_TYPE_I64, args[5], args[0], args[1],
                     args[2], const_args[2], args[3], const_args[3]);
        break;

    OP_32_64(deposit):
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[1]) {
            tgen_deposit(s, a0, a2, args[3], args[4], 1);
        } else {
            /* Since we can't support "0Z" as a constraint, we allow a1 in
               any register.  Fix things up as if a matching constraint.  */
            if (a0 != a1) {
                TCGType type = (opc == INDEX_op_deposit_i64);
                if (a0 == a2) {
                    tcg_out_mov(s, type, TCG_TMP0, a2);
                    a2 = TCG_TMP0;
                }
                tcg_out_mov(s, type, a0, a1);
            }
            tgen_deposit(s, a0, a2, args[3], args[4], 0);
        }
        break;

    OP_32_64(extract):
        tgen_extract(s, args[0], args[1], args[2], args[3]);
        break;

    case INDEX_op_clz_i64:
        tgen_clz(s, args[0], args[1], args[2], const_args[2]);
        break;

    case INDEX_op_mb:
        /* The host memory model is quite strong, we simply need to
           serialize the instruction stream.  */
        if (args[0] & TCG_MO_ST_LD) {
            tcg_out_insn(s, RR, BCR,
                         s390_facilities & FACILITY_FAST_BCR_SER ? 14 : 15, 0);
        }
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

static const TCGTargetOpDef *tcg_target_op_def(TCGOpcode op)
{
    static const TCGTargetOpDef r = { .args_ct_str = { "r" } };
    static const TCGTargetOpDef r_r = { .args_ct_str = { "r", "r" } };
    static const TCGTargetOpDef r_L = { .args_ct_str = { "r", "L" } };
    static const TCGTargetOpDef L_L = { .args_ct_str = { "L", "L" } };
    static const TCGTargetOpDef r_ri = { .args_ct_str = { "r", "ri" } };
    static const TCGTargetOpDef r_r_ri = { .args_ct_str = { "r", "r", "ri" } };
    static const TCGTargetOpDef r_0_ri = { .args_ct_str = { "r", "0", "ri" } };
    static const TCGTargetOpDef r_0_rI = { .args_ct_str = { "r", "0", "rI" } };
    static const TCGTargetOpDef r_0_rJ = { .args_ct_str = { "r", "0", "rJ" } };
    static const TCGTargetOpDef a2_r
        = { .args_ct_str = { "r", "r", "0", "1", "r", "r" } };
    static const TCGTargetOpDef a2_ri
        = { .args_ct_str = { "r", "r", "0", "1", "ri", "r" } };
    static const TCGTargetOpDef a2_rA
        = { .args_ct_str = { "r", "r", "0", "1", "rA", "r" } };

    switch (op) {
    case INDEX_op_goto_ptr:
        return &r;

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
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return &r_r;

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
        return &r_r_ri;
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
        return (s390_facilities & FACILITY_DISTINCT_OPS ? &r_r_ri : &r_0_ri);

    case INDEX_op_mul_i32:
        /* If we have the general-instruction-extensions, then we have
           MULTIPLY SINGLE IMMEDIATE with a signed 32-bit, otherwise we
           have only MULTIPLY HALFWORD IMMEDIATE, with a signed 16-bit.  */
        return (s390_facilities & FACILITY_GEN_INST_EXT ? &r_0_ri : &r_0_rI);
    case INDEX_op_mul_i64:
        return (s390_facilities & FACILITY_GEN_INST_EXT ? &r_0_rJ : &r_0_rI);

    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
        return (s390_facilities & FACILITY_DISTINCT_OPS ? &r_r_ri : &r_0_ri);

    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
        return &r_r_ri;

    case INDEX_op_rotl_i32:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i32:
    case INDEX_op_rotr_i64:
        return &r_r_ri;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return &r_ri;

    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
    case INDEX_op_ext8s_i32:
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
    case INDEX_op_extract_i32:
    case INDEX_op_extract_i64:
        return &r_r;

    case INDEX_op_clz_i64:
    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
        return &r_r_ri;

    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        return &r_L;
    case INDEX_op_qemu_st_i64:
    case INDEX_op_qemu_st_i32:
        return &L_L;

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        {
            static const TCGTargetOpDef dep
                = { .args_ct_str = { "r", "rZ", "r" } };
            return &dep;
        }
    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        {
            static const TCGTargetOpDef movc
                = { .args_ct_str = { "r", "r", "ri", "r", "0" } };
            static const TCGTargetOpDef movc_l
                = { .args_ct_str = { "r", "r", "ri", "rI", "0" } };
            return (s390_facilities & FACILITY_LOAD_ON_COND2 ? &movc_l : &movc);
        }
    case INDEX_op_div2_i32:
    case INDEX_op_div2_i64:
    case INDEX_op_divu2_i32:
    case INDEX_op_divu2_i64:
        {
            static const TCGTargetOpDef div2
                = { .args_ct_str = { "b", "a", "0", "1", "r" } };
            return &div2;
        }
    case INDEX_op_mulu2_i64:
        {
            static const TCGTargetOpDef mul2
                = { .args_ct_str = { "b", "a", "0", "r" } };
            return &mul2;
        }

    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        return (s390_facilities & FACILITY_EXT_IMM ? &a2_ri : &a2_r);
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i64:
        return (s390_facilities & FACILITY_EXT_IMM ? &a2_rA : &a2_r);

    default:
        break;
    }
    return NULL;
}

static void query_s390_facilities(void)
{
    unsigned long hwcap = qemu_getauxval(AT_HWCAP);

    /* Is STORE FACILITY LIST EXTENDED available?  Honestly, I believe this
       is present on all 64-bit systems, but let's check for it anyway.  */
    if (hwcap & HWCAP_S390_STFLE) {
        register int r0 __asm__("0");
        register void *r1 __asm__("1");

        /* stfle 0(%r1) */
        r1 = &s390_facilities;
        asm volatile(".word 0xb2b0,0x1000"
                     : "=r"(r0) : "0"(0), "r"(r1) : "memory", "cc");
    }
}

static void tcg_target_init(TCGContext *s)
{
    query_s390_facilities();

    tcg_target_available_regs[TCG_TYPE_I32] = 0xffff;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffff;

    tcg_target_call_clobber_regs = 0;
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

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP0);
    /* XXX many insns can't be used with R0, so we better avoid it for now */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
    if (USE_REG_TB) {
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_TB);
    }
}

#define FRAME_SIZE  ((int)(TCG_TARGET_CALL_STACK_OFFSET          \
                           + TCG_STATIC_CALL_ARGS_SIZE           \
                           + CPU_TEMP_BUF_NLONGS * sizeof(long)))

static void tcg_target_qemu_prologue(TCGContext *s)
{
    /* stmg %r6,%r15,48(%r15) (save registers) */
    tcg_out_insn(s, RXY, STMG, TCG_REG_R6, TCG_REG_R15, TCG_REG_R15, 48);

    /* aghi %r15,-frame_size */
    tcg_out_insn(s, RI, AGHI, TCG_REG_R15, -FRAME_SIZE);

    tcg_set_frame(s, TCG_REG_CALL_STACK,
                  TCG_STATIC_CALL_ARGS_SIZE + TCG_TARGET_CALL_STACK_OFFSET,
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

#ifndef CONFIG_SOFTMMU
    if (guest_base >= 0x80000) {
        tcg_out_movi_int(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base, true);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    if (USE_REG_TB) {
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_TB,
                    tcg_target_call_iarg_regs[1]);
    }

    /* br %r3 (go to TB) */
    tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, tcg_target_call_iarg_regs[1]);

    /*
     * Return path for goto_ptr. Set return value to 0, a-la exit_tb,
     * and fall through to the rest of the epilogue.
     */
    s->code_gen_epilogue = s->code_ptr;
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, 0);

    /* TB epilogue */
    tb_ret_addr = s->code_ptr;

    /* lmg %r6,%r15,fs+48(%r15) (restore registers) */
    tcg_out_insn(s, RXY, LMG, TCG_REG_R6, TCG_REG_R15, TCG_REG_R15,
                 FRAME_SIZE + 48);

    /* br %r14 (return) */
    tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, TCG_REG_R14);
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    memset(p, 0x07, count * sizeof(tcg_insn_unit));
}

typedef struct {
    DebugFrameHeader h;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[18];
} DebugFrame;

/* We're expecting a 2 byte uleb128 encoded value.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE >= (1 << 14));

#define ELF_HOST_MACHINE  EM_S390

static const DebugFrame debug_frame = {
    .h.cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .h.cie.id = -1,
    .h.cie.version = 1,
    .h.cie.code_align = 1,
    .h.cie.data_align = 8,                /* sleb128 8 */
    .h.cie.return_column = TCG_REG_R14,

    /* Total FDE size does not include the "len" member.  */
    .h.fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, h.fde.cie_offset),

    .fde_def_cfa = {
        12, TCG_REG_CALL_STACK,         /* DW_CFA_def_cfa %r15, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        0x86, 6,                        /* DW_CFA_offset, %r6, 48 */
        0x87, 7,                        /* DW_CFA_offset, %r7, 56 */
        0x88, 8,                        /* DW_CFA_offset, %r8, 64 */
        0x89, 9,                        /* DW_CFA_offset, %r92, 72 */
        0x8a, 10,                       /* DW_CFA_offset, %r10, 80 */
        0x8b, 11,                       /* DW_CFA_offset, %r11, 88 */
        0x8c, 12,                       /* DW_CFA_offset, %r12, 96 */
        0x8d, 13,                       /* DW_CFA_offset, %r13, 104 */
        0x8e, 14,                       /* DW_CFA_offset, %r14, 112 */
    }
};

void tcg_register_jit(void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
