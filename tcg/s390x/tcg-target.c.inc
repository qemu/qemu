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

#include "../tcg-ldst.c.inc"
#include "../tcg-pool.c.inc"
#include "elf.h"

#define TCG_CT_CONST_S16        (1 << 8)
#define TCG_CT_CONST_S32        (1 << 9)
#define TCG_CT_CONST_S33        (1 << 10)
#define TCG_CT_CONST_ZERO       (1 << 11)
#define TCG_CT_CONST_P32        (1 << 12)
#define TCG_CT_CONST_INV        (1 << 13)
#define TCG_CT_CONST_INVRISBG   (1 << 14)

#define ALL_GENERAL_REGS     MAKE_64BIT_MASK(0, 16)
#define ALL_VECTOR_REGS      MAKE_64BIT_MASK(32, 32)

/* Several places within the instruction set 0 means "no register"
   rather than TCG_REG_R0.  */
#define TCG_REG_NONE    0

/* A scratch register that may be be used throughout the backend.  */
#define TCG_TMP0        TCG_REG_R1

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
    RI_TMLL     = 0xa701,

    RIEb_CGRJ    = 0xec64,
    RIEb_CLGRJ   = 0xec65,
    RIEb_CLRJ    = 0xec77,
    RIEb_CRJ     = 0xec76,

    RIEc_CGIJ    = 0xec7c,
    RIEc_CIJ     = 0xec7e,
    RIEc_CLGIJ   = 0xec7d,
    RIEc_CLIJ    = 0xec7f,

    RIEf_RISBG   = 0xec55,

    RIEg_LOCGHI  = 0xec46,

    RRE_AGR     = 0xb908,
    RRE_ALGR    = 0xb90a,
    RRE_ALCR    = 0xb998,
    RRE_ALCGR   = 0xb988,
    RRE_ALGFR   = 0xb91a,
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

    RRFa_MGRK   = 0xb9ec,
    RRFa_MSRKC  = 0xb9fd,
    RRFa_MSGRKC = 0xb9ed,
    RRFa_NCRK   = 0xb9f5,
    RRFa_NCGRK  = 0xb9e5,
    RRFa_NNRK   = 0xb974,
    RRFa_NNGRK  = 0xb964,
    RRFa_NORK   = 0xb976,
    RRFa_NOGRK  = 0xb966,
    RRFa_NRK    = 0xb9f4,
    RRFa_NGRK   = 0xb9e4,
    RRFa_NXRK   = 0xb977,
    RRFa_NXGRK  = 0xb967,
    RRFa_OCRK   = 0xb975,
    RRFa_OCGRK  = 0xb965,
    RRFa_ORK    = 0xb9f6,
    RRFa_OGRK   = 0xb9e6,
    RRFa_SRK    = 0xb9f9,
    RRFa_SGRK   = 0xb9e9,
    RRFa_SLRK   = 0xb9fb,
    RRFa_SLGRK  = 0xb9eb,
    RRFa_XRK    = 0xb9f7,
    RRFa_XGRK   = 0xb9e7,

    RRFam_SELGR = 0xb9e3,

    RRFc_LOCR   = 0xb9f2,
    RRFc_LOCGR  = 0xb9e2,
    RRFc_POPCNT = 0xb9e1,

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

    VRIa_VGBM   = 0xe744,
    VRIa_VREPI  = 0xe745,
    VRIb_VGM    = 0xe746,
    VRIc_VREP   = 0xe74d,

    VRRa_VLC    = 0xe7de,
    VRRa_VLP    = 0xe7df,
    VRRa_VLR    = 0xe756,
    VRRc_VA     = 0xe7f3,
    VRRc_VCEQ   = 0xe7f8,   /* we leave the m5 cs field 0 */
    VRRc_VCH    = 0xe7fb,   /* " */
    VRRc_VCHL   = 0xe7f9,   /* " */
    VRRc_VERLLV = 0xe773,
    VRRc_VESLV  = 0xe770,
    VRRc_VESRAV = 0xe77a,
    VRRc_VESRLV = 0xe778,
    VRRc_VML    = 0xe7a2,
    VRRc_VMN    = 0xe7fe,
    VRRc_VMNL   = 0xe7fc,
    VRRc_VMX    = 0xe7ff,
    VRRc_VMXL   = 0xe7fd,
    VRRc_VN     = 0xe768,
    VRRc_VNC    = 0xe769,
    VRRc_VNN    = 0xe76e,
    VRRc_VNO    = 0xe76b,
    VRRc_VNX    = 0xe76c,
    VRRc_VO     = 0xe76a,
    VRRc_VOC    = 0xe76f,
    VRRc_VPKS   = 0xe797,   /* we leave the m5 cs field 0 */
    VRRc_VS     = 0xe7f7,
    VRRa_VUPH   = 0xe7d7,
    VRRa_VUPL   = 0xe7d6,
    VRRc_VX     = 0xe76d,
    VRRe_VSEL   = 0xe78d,
    VRRf_VLVGP  = 0xe762,

    VRSa_VERLL  = 0xe733,
    VRSa_VESL   = 0xe730,
    VRSa_VESRA  = 0xe73a,
    VRSa_VESRL  = 0xe738,
    VRSb_VLVG   = 0xe722,
    VRSc_VLGV   = 0xe721,

    VRX_VL      = 0xe706,
    VRX_VLLEZ   = 0xe704,
    VRX_VLREP   = 0xe705,
    VRX_VST     = 0xe70e,
    VRX_VSTEF   = 0xe70b,
    VRX_VSTEG   = 0xe70a,

    NOP         = 0x0707,
} S390Opcode;

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%r0",  "%r1",  "%r2",  "%r3",  "%r4",  "%r5",  "%r6",  "%r7",
    "%r8",  "%r9",  "%r10", "%r11", "%r12", "%r13", "%r14", "%r15",
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    "%v0",  "%v1",  "%v2",  "%v3",  "%v4",  "%v5",  "%v6",  "%v7",
    "%v8",  "%v9",  "%v10", "%v11", "%v12", "%v13", "%v14", "%v15",
    "%v16", "%v17", "%v18", "%v19", "%v20", "%v21", "%v22", "%v23",
    "%v24", "%v25", "%v26", "%v27", "%v28", "%v29", "%v30", "%v31",
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

    /* V8-V15 are call saved, and omitted. */
    TCG_REG_V0,
    TCG_REG_V1,
    TCG_REG_V2,
    TCG_REG_V3,
    TCG_REG_V4,
    TCG_REG_V5,
    TCG_REG_V6,
    TCG_REG_V7,
    TCG_REG_V16,
    TCG_REG_V17,
    TCG_REG_V18,
    TCG_REG_V19,
    TCG_REG_V20,
    TCG_REG_V21,
    TCG_REG_V22,
    TCG_REG_V23,
    TCG_REG_V24,
    TCG_REG_V25,
    TCG_REG_V26,
    TCG_REG_V27,
    TCG_REG_V28,
    TCG_REG_V29,
    TCG_REG_V30,
    TCG_REG_V31,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
};

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot == 0);
    return TCG_REG_R2;
}

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

static const tcg_insn_unit *tb_ret_addr;
uint64_t s390_facilities[3];

static inline bool is_general_reg(TCGReg r)
{
    return r <= TCG_REG_R15;
}

static inline bool is_vector_reg(TCGReg r)
{
    return r >= TCG_REG_V0 && r <= TCG_REG_V31;
}

static bool patch_reloc(tcg_insn_unit *src_rw, int type,
                        intptr_t value, intptr_t addend)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    intptr_t pcrel2;
    uint32_t old;

    value += addend;
    pcrel2 = (tcg_insn_unit *)value - src_rx;

    switch (type) {
    case R_390_PC16DBL:
        if (pcrel2 == (int16_t)pcrel2) {
            tcg_patch16(src_rw, pcrel2);
            return true;
        }
        break;
    case R_390_PC32DBL:
        if (pcrel2 == (int32_t)pcrel2) {
            tcg_patch32(src_rw, pcrel2);
            return true;
        }
        break;
    case R_390_20:
        if (value == sextract64(value, 0, 20)) {
            old = *(uint32_t *)src_rw & 0xf00000ff;
            old |= ((value & 0xfff) << 16) | ((value & 0xff000) >> 4);
            tcg_patch32(src_rw, old);
            return true;
        }
        break;
    default:
        g_assert_not_reached();
    }
    return false;
}

static int is_const_p16(uint64_t val)
{
    for (int i = 0; i < 4; ++i) {
        uint64_t mask = 0xffffull << (i * 16);
        if ((val & ~mask) == 0) {
            return i;
        }
    }
    return -1;
}

static int is_const_p32(uint64_t val)
{
    if ((val & 0xffffffff00000000ull) == 0) {
        return 0;
    }
    if ((val & 0x00000000ffffffffull) == 0) {
        return 1;
    }
    return -1;
}

/*
 * Accept bit patterns like these:
 *  0....01....1
 *  1....10....0
 *  1..10..01..1
 *  0..01..10..0
 * Copied from gcc sources.
 */
static bool risbg_mask(uint64_t c)
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

/* Test if a constant matches the constraint. */
static bool tcg_target_const_match(int64_t val, TCGType type, int ct)
{
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

    if (ct & TCG_CT_CONST_INV) {
        val = ~val;
    }
    /*
     * Note that is_const_p16 is a subset of is_const_p32,
     * so we don't need both constraints.
     */
    if ((ct & TCG_CT_CONST_P32) && is_const_p32(val) >= 0) {
        return true;
    }
    if ((ct & TCG_CT_CONST_INVRISBG) && risbg_mask(~val)) {
        return true;
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

/* RRF-a without the m4 field */
static void tcg_out_insn_RRFa(TCGContext *s, S390Opcode op,
                              TCGReg r1, TCGReg r2, TCGReg r3)
{
    tcg_out32(s, (op << 16) | (r3 << 12) | (r1 << 4) | r2);
}

/* RRF-a with the m4 field */
static void tcg_out_insn_RRFam(TCGContext *s, S390Opcode op,
                               TCGReg r1, TCGReg r2, TCGReg r3, int m4)
{
    tcg_out32(s, (op << 16) | (r3 << 12) | (m4 << 8) | (r1 << 4) | r2);
}

static void tcg_out_insn_RRFc(TCGContext *s, S390Opcode op,
                              TCGReg r1, TCGReg r2, int m3)
{
    tcg_out32(s, (op << 16) | (m3 << 12) | (r1 << 4) | r2);
}

static void tcg_out_insn_RI(TCGContext *s, S390Opcode op, TCGReg r1, int i2)
{
    tcg_out32(s, (op << 16) | (r1 << 20) | (i2 & 0xffff));
}

static void tcg_out_insn_RIEg(TCGContext *s, S390Opcode op, TCGReg r1,
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

static int RXB(TCGReg v1, TCGReg v2, TCGReg v3, TCGReg v4)
{
    /*
     * Shift bit 4 of each regno to its corresponding bit of RXB.
     * RXB itself begins at bit 8 of the instruction so 8 - 4 = 4
     * is the left-shift of the 4th operand.
     */
    return ((v1 & 0x10) << (4 + 3))
         | ((v2 & 0x10) << (4 + 2))
         | ((v3 & 0x10) << (4 + 1))
         | ((v4 & 0x10) << (4 + 0));
}

static void tcg_out_insn_VRIa(TCGContext *s, S390Opcode op,
                              TCGReg v1, uint16_t i2, int m3)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4));
    tcg_out16(s, i2);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, 0, 0, 0) | (m3 << 12));
}

static void tcg_out_insn_VRIb(TCGContext *s, S390Opcode op,
                              TCGReg v1, uint8_t i2, uint8_t i3, int m4)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4));
    tcg_out16(s, (i2 << 8) | (i3 & 0xff));
    tcg_out16(s, (op & 0x00ff) | RXB(v1, 0, 0, 0) | (m4 << 12));
}

static void tcg_out_insn_VRIc(TCGContext *s, S390Opcode op,
                              TCGReg v1, uint16_t i2, TCGReg v3, int m4)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(is_vector_reg(v3));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | (v3 & 0xf));
    tcg_out16(s, i2);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, 0, v3, 0) | (m4 << 12));
}

static void tcg_out_insn_VRRa(TCGContext *s, S390Opcode op,
                              TCGReg v1, TCGReg v2, int m3)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(is_vector_reg(v2));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | (v2 & 0xf));
    tcg_out32(s, (op & 0x00ff) | RXB(v1, v2, 0, 0) | (m3 << 12));
}

static void tcg_out_insn_VRRc(TCGContext *s, S390Opcode op,
                              TCGReg v1, TCGReg v2, TCGReg v3, int m4)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(is_vector_reg(v2));
    tcg_debug_assert(is_vector_reg(v3));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | (v2 & 0xf));
    tcg_out16(s, v3 << 12);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, v2, v3, 0) | (m4 << 12));
}

static void tcg_out_insn_VRRe(TCGContext *s, S390Opcode op,
                              TCGReg v1, TCGReg v2, TCGReg v3, TCGReg v4)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(is_vector_reg(v2));
    tcg_debug_assert(is_vector_reg(v3));
    tcg_debug_assert(is_vector_reg(v4));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | (v2 & 0xf));
    tcg_out16(s, v3 << 12);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, v2, v3, v4) | (v4 << 12));
}

static void tcg_out_insn_VRRf(TCGContext *s, S390Opcode op,
                              TCGReg v1, TCGReg r2, TCGReg r3)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(is_general_reg(r2));
    tcg_debug_assert(is_general_reg(r3));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | r2);
    tcg_out16(s, r3 << 12);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, 0, 0, 0));
}

static void tcg_out_insn_VRSa(TCGContext *s, S390Opcode op, TCGReg v1,
                              intptr_t d2, TCGReg b2, TCGReg v3, int m4)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(d2 >= 0 && d2 <= 0xfff);
    tcg_debug_assert(is_general_reg(b2));
    tcg_debug_assert(is_vector_reg(v3));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | (v3 & 0xf));
    tcg_out16(s, b2 << 12 | d2);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, 0, v3, 0) | (m4 << 12));
}

static void tcg_out_insn_VRSb(TCGContext *s, S390Opcode op, TCGReg v1,
                              intptr_t d2, TCGReg b2, TCGReg r3, int m4)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(d2 >= 0 && d2 <= 0xfff);
    tcg_debug_assert(is_general_reg(b2));
    tcg_debug_assert(is_general_reg(r3));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | r3);
    tcg_out16(s, b2 << 12 | d2);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, 0, 0, 0) | (m4 << 12));
}

static void tcg_out_insn_VRSc(TCGContext *s, S390Opcode op, TCGReg r1,
                              intptr_t d2, TCGReg b2, TCGReg v3, int m4)
{
    tcg_debug_assert(is_general_reg(r1));
    tcg_debug_assert(d2 >= 0 && d2 <= 0xfff);
    tcg_debug_assert(is_general_reg(b2));
    tcg_debug_assert(is_vector_reg(v3));
    tcg_out16(s, (op & 0xff00) | (r1 << 4) | (v3 & 0xf));
    tcg_out16(s, b2 << 12 | d2);
    tcg_out16(s, (op & 0x00ff) | RXB(0, 0, v3, 0) | (m4 << 12));
}

static void tcg_out_insn_VRX(TCGContext *s, S390Opcode op, TCGReg v1,
                             TCGReg b2, TCGReg x2, intptr_t d2, int m3)
{
    tcg_debug_assert(is_vector_reg(v1));
    tcg_debug_assert(d2 >= 0 && d2 <= 0xfff);
    tcg_debug_assert(is_general_reg(x2));
    tcg_debug_assert(is_general_reg(b2));
    tcg_out16(s, (op & 0xff00) | ((v1 & 0xf) << 4) | x2);
    tcg_out16(s, (b2 << 12) | d2);
    tcg_out16(s, (op & 0x00ff) | RXB(v1, 0, 0, 0) | (m3 << 12));
}

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
    if (src == dst) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I32:
        if (likely(is_general_reg(dst) && is_general_reg(src))) {
            tcg_out_insn(s, RR, LR, dst, src);
            break;
        }
        /* fallthru */

    case TCG_TYPE_I64:
        if (likely(is_general_reg(dst))) {
            if (likely(is_general_reg(src))) {
                tcg_out_insn(s, RRE, LGR, dst, src);
            } else {
                tcg_out_insn(s, VRSc, VLGV, dst, 0, 0, src, 3);
            }
            break;
        } else if (is_general_reg(src)) {
            tcg_out_insn(s, VRSb, VLVG, dst, 0, 0, src, 3);
            break;
        }
        /* fallthru */

    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
        tcg_out_insn(s, VRRa, VLR, dst, src, 0);
        break;

    default:
        g_assert_not_reached();
    }
    return true;
}

static const S390Opcode li_insns[4] = {
    RI_LLILL, RI_LLILH, RI_LLIHL, RI_LLIHH
};
static const S390Opcode oi_insns[4] = {
    RI_OILL, RI_OILH, RI_OIHL, RI_OIHH
};
static const S390Opcode lif_insns[2] = {
    RIL_LLILF, RIL_LLIHF,
};

/* load a register with an immediate value */
static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long sval)
{
    tcg_target_ulong uval = sval;
    ptrdiff_t pc_off;
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

    i = is_const_p16(uval);
    if (i >= 0) {
        tcg_out_insn_RI(s, li_insns[i], ret, uval >> (i * 16));
        return;
    }

    /* Try all 48-bit insns that can load it in one go.  */
    if (sval == (int32_t)sval) {
        tcg_out_insn(s, RIL, LGFI, ret, sval);
        return;
    }

    i = is_const_p32(uval);
    if (i >= 0) {
        tcg_out_insn_RIL(s, lif_insns[i], ret, uval >> (i * 32));
        return;
    }

    /* Try for PC-relative address load.  For odd addresses, add one. */
    pc_off = tcg_pcrel_diff(s, (void *)sval) >> 1;
    if (pc_off == (int32_t)pc_off) {
        tcg_out_insn(s, RIL, LARL, ret, pc_off);
        if (sval & 1) {
            tcg_out_insn(s, RI, AGHI, ret, 1);
        }
        return;
    }

    /* Otherwise, load it by parts. */
    i = is_const_p16((uint32_t)uval);
    if (i >= 0) {
        tcg_out_insn_RI(s, li_insns[i], ret, uval >> (i * 16));
    } else {
        tcg_out_insn(s, RIL, LLILF, ret, uval);
    }
    uval >>= 32;
    i = is_const_p16(uval);
    if (i >= 0) {
        tcg_out_insn_RI(s, oi_insns[i + 2], ret, uval >> (i * 16));
    } else {
        tcg_out_insn(s, RIL, OIHF, ret, uval);
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

static void tcg_out_vrx_mem(TCGContext *s, S390Opcode opc_vrx,
                            TCGReg data, TCGReg base, TCGReg index,
                            tcg_target_long ofs, int m3)
{
    if (ofs < 0 || ofs >= 0x1000) {
        if (ofs >= -0x80000 && ofs < 0x80000) {
            tcg_out_insn(s, RXY, LAY, TCG_TMP0, base, index, ofs);
            base = TCG_TMP0;
            index = TCG_REG_NONE;
            ofs = 0;
        } else {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, ofs);
            if (index != TCG_REG_NONE) {
                tcg_out_insn(s, RRE, AGR, TCG_TMP0, index);
            }
            index = TCG_TMP0;
            ofs = 0;
        }
    }
    tcg_out_insn_VRX(s, opc_vrx, data, base, index, ofs, m3);
}

/* load data without address translation or endianness conversion */
static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg data,
                       TCGReg base, intptr_t ofs)
{
    switch (type) {
    case TCG_TYPE_I32:
        if (likely(is_general_reg(data))) {
            tcg_out_mem(s, RX_L, RXY_LY, data, base, TCG_REG_NONE, ofs);
            break;
        }
        tcg_out_vrx_mem(s, VRX_VLLEZ, data, base, TCG_REG_NONE, ofs, MO_32);
        break;

    case TCG_TYPE_I64:
        if (likely(is_general_reg(data))) {
            tcg_out_mem(s, 0, RXY_LG, data, base, TCG_REG_NONE, ofs);
            break;
        }
        /* fallthru */

    case TCG_TYPE_V64:
        tcg_out_vrx_mem(s, VRX_VLLEZ, data, base, TCG_REG_NONE, ofs, MO_64);
        break;

    case TCG_TYPE_V128:
        /* Hint quadword aligned.  */
        tcg_out_vrx_mem(s, VRX_VL, data, base, TCG_REG_NONE, ofs, 4);
        break;

    default:
        g_assert_not_reached();
    }
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg data,
                       TCGReg base, intptr_t ofs)
{
    switch (type) {
    case TCG_TYPE_I32:
        if (likely(is_general_reg(data))) {
            tcg_out_mem(s, RX_ST, RXY_STY, data, base, TCG_REG_NONE, ofs);
        } else {
            tcg_out_vrx_mem(s, VRX_VSTEF, data, base, TCG_REG_NONE, ofs, 1);
        }
        break;

    case TCG_TYPE_I64:
        if (likely(is_general_reg(data))) {
            tcg_out_mem(s, 0, RXY_STG, data, base, TCG_REG_NONE, ofs);
            break;
        }
        /* fallthru */

    case TCG_TYPE_V64:
        tcg_out_vrx_mem(s, VRX_VSTEG, data, base, TCG_REG_NONE, ofs, 0);
        break;

    case TCG_TYPE_V128:
        /* Hint quadword aligned.  */
        tcg_out_vrx_mem(s, VRX_VST, data, base, TCG_REG_NONE, ofs, 4);
        break;

    default:
        g_assert_not_reached();
    }
}

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    return false;
}

static bool tcg_out_xchg(TCGContext *s, TCGType type, TCGReg r1, TCGReg r2)
{
    return false;
}

static void tcg_out_addi_ptr(TCGContext *s, TCGReg rd, TCGReg rs,
                             tcg_target_long imm)
{
    /* This function is only used for passing structs by reference. */
    tcg_out_mem(s, RX_LA, RXY_LAY, rd, rs, TCG_REG_NONE, imm);
}

static inline void tcg_out_risbg(TCGContext *s, TCGReg dest, TCGReg src,
                                 int msb, int lsb, int ofs, int z)
{
    /* Format RIE-f */
    tcg_out16(s, (RIEf_RISBG & 0xff00) | (dest << 4) | src);
    tcg_out16(s, (msb << 8) | (z << 7) | lsb);
    tcg_out16(s, (ofs << 8) | (RIEf_RISBG & 0xff));
}

static void tcg_out_ext8s(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LGBR, dest, src);
}

static void tcg_out_ext8u(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LLGCR, dest, src);
}

static void tcg_out_ext16s(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LGHR, dest, src);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LLGHR, dest, src);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LGFR, dest, src);
}

static void tcg_out_ext32u(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_insn(s, RRE, LLGFR, dest, src);
}

static void tcg_out_exts_i32_i64(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_ext32s(s, dest, src);
}

static void tcg_out_extu_i32_i64(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_ext32u(s, dest, src);
}

static void tcg_out_extrl_i64_i32(TCGContext *s, TCGReg dest, TCGReg src)
{
    tcg_out_mov(s, TCG_TYPE_I32, dest, src);
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
        tcg_out_ext32u(s, dest, dest);
        return;
    }
    if ((val & valid) == 0xff) {
        tcg_out_ext8u(s, dest, dest);
        return;
    }
    if ((val & valid) == 0xffff) {
        tcg_out_ext16u(s, dest, dest);
        return;
    }

    i = is_const_p16(~val & valid);
    if (i >= 0) {
        tcg_out_insn_RI(s, ni_insns[i], dest, val >> (i * 16));
        return;
    }

    i = is_const_p32(~val & valid);
    tcg_debug_assert(i == 0 || type != TCG_TYPE_I32);
    if (i >= 0) {
        tcg_out_insn_RIL(s, nif_insns[i], dest, val >> (i * 32));
        return;
    }

    if (risbg_mask(val)) {
        tgen_andi_risbg(s, dest, dest, val);
        return;
    }

    g_assert_not_reached();
}

static void tgen_ori(TCGContext *s, TCGReg dest, uint64_t val)
{
    static const S390Opcode oif_insns[2] = {
        RIL_OILF, RIL_OIHF
    };

    int i;

    i = is_const_p16(val);
    if (i >= 0) {
        tcg_out_insn_RI(s, oi_insns[i], dest, val >> (i * 16));
        return;
    }

    i = is_const_p32(val);
    if (i >= 0) {
        tcg_out_insn_RIL(s, oif_insns[i], dest, val >> (i * 32));
        return;
    }

    g_assert_not_reached();
}

static void tgen_xori(TCGContext *s, TCGReg dest, uint64_t val)
{
    switch (is_const_p32(val)) {
    case 0:
        tcg_out_insn(s, RIL, XILF, dest, val);
        break;
    case 1:
        tcg_out_insn(s, RIL, XIHF, dest, val >> 32);
        break;
    default:
        g_assert_not_reached();
    }
}

static int tgen_cmp2(TCGContext *s, TCGType type, TCGCond c, TCGReg r1,
                     TCGArg c2, bool c2const, bool need_carry, int *inv_cc)
{
    bool is_unsigned = is_unsigned_cond(c);
    TCGCond inv_c = tcg_invert_cond(c);
    S390Opcode op;

    if (c2const) {
        if (c2 == 0) {
            if (!(is_unsigned && need_carry)) {
                if (type == TCG_TYPE_I32) {
                    tcg_out_insn(s, RR, LTR, r1, r1);
                } else {
                    tcg_out_insn(s, RRE, LTGR, r1, r1);
                }
                *inv_cc = tcg_cond_to_ltr_cond[inv_c];
                return tcg_cond_to_ltr_cond[c];
            }
        }

        if (!is_unsigned && c2 == (int16_t)c2) {
            op = (type == TCG_TYPE_I32 ? RI_CHI : RI_CGHI);
            tcg_out_insn_RI(s, op, r1, c2);
            goto exit;
        }

        if (type == TCG_TYPE_I32) {
            op = (is_unsigned ? RIL_CLFI : RIL_CFI);
            tcg_out_insn_RIL(s, op, r1, c2);
            goto exit;
        }

        /*
         * Constraints are for a signed 33-bit operand, which is a
         * convenient superset of this signed/unsigned test.
         */
        if (c2 == (is_unsigned ? (TCGArg)(uint32_t)c2 : (TCGArg)(int32_t)c2)) {
            op = (is_unsigned ? RIL_CLGFI : RIL_CGFI);
            tcg_out_insn_RIL(s, op, r1, c2);
            goto exit;
        }

        /* Load everything else into a register. */
        tcg_out_movi(s, TCG_TYPE_I64, TCG_TMP0, c2);
        c2 = TCG_TMP0;
    }

    if (type == TCG_TYPE_I32) {
        op = (is_unsigned ? RR_CLR : RR_CR);
        tcg_out_insn_RR(s, op, r1, c2);
    } else {
        op = (is_unsigned ? RRE_CLGR : RRE_CGR);
        tcg_out_insn_RRE(s, op, r1, c2);
    }

 exit:
    *inv_cc = tcg_cond_to_s390_cond[inv_c];
    return tcg_cond_to_s390_cond[c];
}

static int tgen_cmp(TCGContext *s, TCGType type, TCGCond c, TCGReg r1,
                    TCGArg c2, bool c2const, bool need_carry)
{
    int inv_cc;
    return tgen_cmp2(s, type, c, r1, c2, c2const, need_carry, &inv_cc);
}

static void tgen_setcond(TCGContext *s, TCGType type, TCGCond cond,
                         TCGReg dest, TCGReg c1, TCGArg c2, int c2const)
{
    int cc;

    /* With LOC2, we can always emit the minimum 3 insns.  */
    if (HAVE_FACILITY(LOAD_ON_COND2)) {
        /* Emit: d = 0, d = (cc ? 1 : d).  */
        cc = tgen_cmp(s, type, cond, c1, c2, c2const, false);
        tcg_out_movi(s, TCG_TYPE_I64, dest, 0);
        tcg_out_insn(s, RIEg, LOCGHI, dest, 1, cc);
        return;
    }

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
        if (!c2const) {
            TCGReg t = c1;
            c1 = c2;
            c2 = t;
            cond = tcg_swap_cond(cond);
            goto restart;
        }
        break;

    default:
        g_assert_not_reached();
    }

    cc = tgen_cmp(s, type, cond, c1, c2, c2const, false);
    /* Emit: d = 0, t = 1, d = (cc ? t : d).  */
    tcg_out_movi(s, TCG_TYPE_I64, dest, 0);
    tcg_out_movi(s, TCG_TYPE_I64, TCG_TMP0, 1);
    tcg_out_insn(s, RRFc, LOCGR, dest, TCG_TMP0, cc);
}

static void tgen_movcond_int(TCGContext *s, TCGType type, TCGReg dest,
                             TCGArg v3, int v3const, TCGReg v4,
                             int cc, int inv_cc)
{
    TCGReg src;

    if (v3const) {
        if (dest == v4) {
            if (HAVE_FACILITY(LOAD_ON_COND2)) {
                /* Emit: if (cc) dest = v3. */
                tcg_out_insn(s, RIEg, LOCGHI, dest, v3, cc);
                return;
            }
            tcg_out_insn(s, RI, LGHI, TCG_TMP0, v3);
            src = TCG_TMP0;
        } else {
            /* LGR+LOCGHI is larger than LGHI+LOCGR. */
            tcg_out_insn(s, RI, LGHI, dest, v3);
            cc = inv_cc;
            src = v4;
        }
    } else {
        if (HAVE_FACILITY(MISC_INSN_EXT3)) {
            /* Emit: dest = cc ? v3 : v4. */
            tcg_out_insn(s, RRFam, SELGR, dest, v3, v4, cc);
            return;
        }
        if (dest == v4) {
            src = v3;
        } else {
            tcg_out_mov(s, type, dest, v3);
            cc = inv_cc;
            src = v4;
        }
    }

    /* Emit: if (cc) dest = src. */
    tcg_out_insn(s, RRFc, LOCGR, dest, src, cc);
}

static void tgen_movcond(TCGContext *s, TCGType type, TCGCond c, TCGReg dest,
                         TCGReg c1, TCGArg c2, int c2const,
                         TCGArg v3, int v3const, TCGReg v4)
{
    int cc, inv_cc;

    cc = tgen_cmp2(s, type, c, c1, c2, c2const, false, &inv_cc);
    tgen_movcond_int(s, type, dest, v3, v3const, v4, cc, inv_cc);
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
        return;
    }

    /*
     * Conditions from FLOGR are:
     *   2 -> one bit found
     *   8 -> no one bit found
     */
    tgen_movcond_int(s, TCG_TYPE_I64, dest, a2, a2const, TCG_REG_R0, 8, 2);
}

static void tgen_ctpop(TCGContext *s, TCGType type, TCGReg dest, TCGReg src)
{
    /* With MIE3, and bit 0 of m4 set, we get the complete result. */
    if (HAVE_FACILITY(MISC_INSN_EXT3)) {
        if (type == TCG_TYPE_I32) {
            tcg_out_ext32u(s, dest, src);
            src = dest;
        }
        tcg_out_insn(s, RRFc, POPCNT, dest, src, 8);
        return;
    }

    /* Without MIE3, each byte gets the count of bits for the byte. */
    tcg_out_insn(s, RRFc, POPCNT, dest, src, 0);

    /* Multiply to sum each byte at the top of the word. */
    if (type == TCG_TYPE_I32) {
        tcg_out_insn(s, RIL, MSFI, dest, 0x01010101);
        tcg_out_sh32(s, RS_SRL, dest, TCG_REG_NONE, 24);
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_TMP0, 0x0101010101010101ull);
        tcg_out_insn(s, RRE, MSGR, dest, TCG_TMP0);
        tcg_out_sh64(s, RSY_SRLG, dest, dest, TCG_REG_NONE, 56);
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

static void tgen_gotoi(TCGContext *s, int cc, const tcg_insn_unit *dest)
{
    ptrdiff_t off = tcg_pcrel_diff(s, dest) >> 1;
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
    } else {
        tcg_out16(s, RI_BRC | (cc << 4));
        tcg_out_reloc(s, s->code_ptr, R_390_PC16DBL, l, 2);
        s->code_ptr += 1;
    }
}

static void tgen_compare_branch(TCGContext *s, S390Opcode opc, int cc,
                                TCGReg r1, TCGReg r2, TCGLabel *l)
{
    tcg_out_reloc(s, s->code_ptr + 1, R_390_PC16DBL, l, 2);
    /* Format RIE-b */
    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | r2);
    tcg_out16(s, 0);
    tcg_out16(s, cc << 12 | (opc & 0xff));
}

static void tgen_compare_imm_branch(TCGContext *s, S390Opcode opc, int cc,
                                    TCGReg r1, int i2, TCGLabel *l)
{
    tcg_out_reloc(s, s->code_ptr + 1, R_390_PC16DBL, l, 2);
    /* Format RIE-c */
    tcg_out16(s, (opc & 0xff00) | (r1 << 4) | cc);
    tcg_out16(s, 0);
    tcg_out16(s, (i2 << 8) | (opc & 0xff));
}

static void tgen_brcond(TCGContext *s, TCGType type, TCGCond c,
                        TCGReg r1, TCGArg c2, int c2const, TCGLabel *l)
{
    int cc;
    bool is_unsigned = is_unsigned_cond(c);
    bool in_range;
    S390Opcode opc;

    cc = tcg_cond_to_s390_cond[c];

    if (!c2const) {
        opc = (type == TCG_TYPE_I32
               ? (is_unsigned ? RIEb_CLRJ : RIEb_CRJ)
               : (is_unsigned ? RIEb_CLGRJ : RIEb_CGRJ));
        tgen_compare_branch(s, opc, cc, r1, c2, l);
        return;
    }

    /*
     * COMPARE IMMEDIATE AND BRANCH RELATIVE has an 8-bit immediate field.
     * If the immediate we've been given does not fit that range, we'll
     * fall back to separate compare and branch instructions using the
     * larger comparison range afforded by COMPARE IMMEDIATE.
     */
    if (type == TCG_TYPE_I32) {
        if (is_unsigned) {
            opc = RIEc_CLIJ;
            in_range = (uint32_t)c2 == (uint8_t)c2;
        } else {
            opc = RIEc_CIJ;
            in_range = (int32_t)c2 == (int8_t)c2;
        }
    } else {
        if (is_unsigned) {
            opc = RIEc_CLGIJ;
            in_range = (uint64_t)c2 == (uint8_t)c2;
        } else {
            opc = RIEc_CGIJ;
            in_range = (int64_t)c2 == (int8_t)c2;
        }
    }
    if (in_range) {
        tgen_compare_imm_branch(s, opc, cc, r1, c2, l);
        return;
    }

    cc = tgen_cmp(s, type, c, r1, c2, c2const, false);
    tgen_branch(s, cc, l);
}

static void tcg_out_call_int(TCGContext *s, const tcg_insn_unit *dest)
{
    ptrdiff_t off = tcg_pcrel_diff(s, dest) >> 1;
    if (off == (int32_t)off) {
        tcg_out_insn(s, RIL, BRASL, TCG_REG_R14, off);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_TMP0, (uintptr_t)dest);
        tcg_out_insn(s, RR, BASR, TCG_REG_R14, TCG_TMP0);
    }
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *dest,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, dest);
}

typedef struct {
    TCGReg base;
    TCGReg index;
    int disp;
    TCGAtomAlign aa;
} HostAddress;

bool tcg_target_has_memory_bswap(MemOp memop)
{
    return true;
}

static void tcg_out_qemu_ld_direct(TCGContext *s, MemOp opc, TCGReg data,
                                   HostAddress h)
{
    switch (opc & (MO_SSIZE | MO_BSWAP)) {
    case MO_UB:
        tcg_out_insn(s, RXY, LLGC, data, h.base, h.index, h.disp);
        break;
    case MO_SB:
        tcg_out_insn(s, RXY, LGB, data, h.base, h.index, h.disp);
        break;

    case MO_UW | MO_BSWAP:
        /* swapped unsigned halfword load with upper bits zeroed */
        tcg_out_insn(s, RXY, LRVH, data, h.base, h.index, h.disp);
        tcg_out_ext16u(s, data, data);
        break;
    case MO_UW:
        tcg_out_insn(s, RXY, LLGH, data, h.base, h.index, h.disp);
        break;

    case MO_SW | MO_BSWAP:
        /* swapped sign-extended halfword load */
        tcg_out_insn(s, RXY, LRVH, data, h.base, h.index, h.disp);
        tcg_out_ext16s(s, TCG_TYPE_REG, data, data);
        break;
    case MO_SW:
        tcg_out_insn(s, RXY, LGH, data, h.base, h.index, h.disp);
        break;

    case MO_UL | MO_BSWAP:
        /* swapped unsigned int load with upper bits zeroed */
        tcg_out_insn(s, RXY, LRV, data, h.base, h.index, h.disp);
        tcg_out_ext32u(s, data, data);
        break;
    case MO_UL:
        tcg_out_insn(s, RXY, LLGF, data, h.base, h.index, h.disp);
        break;

    case MO_SL | MO_BSWAP:
        /* swapped sign-extended int load */
        tcg_out_insn(s, RXY, LRV, data, h.base, h.index, h.disp);
        tcg_out_ext32s(s, data, data);
        break;
    case MO_SL:
        tcg_out_insn(s, RXY, LGF, data, h.base, h.index, h.disp);
        break;

    case MO_UQ | MO_BSWAP:
        tcg_out_insn(s, RXY, LRVG, data, h.base, h.index, h.disp);
        break;
    case MO_UQ:
        tcg_out_insn(s, RXY, LG, data, h.base, h.index, h.disp);
        break;

    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_st_direct(TCGContext *s, MemOp opc, TCGReg data,
                                   HostAddress h)
{
    switch (opc & (MO_SIZE | MO_BSWAP)) {
    case MO_UB:
        if (h.disp >= 0 && h.disp < 0x1000) {
            tcg_out_insn(s, RX, STC, data, h.base, h.index, h.disp);
        } else {
            tcg_out_insn(s, RXY, STCY, data, h.base, h.index, h.disp);
        }
        break;

    case MO_UW | MO_BSWAP:
        tcg_out_insn(s, RXY, STRVH, data, h.base, h.index, h.disp);
        break;
    case MO_UW:
        if (h.disp >= 0 && h.disp < 0x1000) {
            tcg_out_insn(s, RX, STH, data, h.base, h.index, h.disp);
        } else {
            tcg_out_insn(s, RXY, STHY, data, h.base, h.index, h.disp);
        }
        break;

    case MO_UL | MO_BSWAP:
        tcg_out_insn(s, RXY, STRV, data, h.base, h.index, h.disp);
        break;
    case MO_UL:
        if (h.disp >= 0 && h.disp < 0x1000) {
            tcg_out_insn(s, RX, ST, data, h.base, h.index, h.disp);
        } else {
            tcg_out_insn(s, RXY, STY, data, h.base, h.index, h.disp);
        }
        break;

    case MO_UQ | MO_BSWAP:
        tcg_out_insn(s, RXY, STRVG, data, h.base, h.index, h.disp);
        break;
    case MO_UQ:
        tcg_out_insn(s, RXY, STG, data, h.base, h.index, h.disp);
        break;

    default:
        g_assert_not_reached();
    }
}

static const TCGLdstHelperParam ldst_helper_param = {
    .ntmp = 1, .tmp = { TCG_TMP0 }
};

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);

    if (!patch_reloc(lb->label_ptr[0], R_390_PC16DBL,
                     (intptr_t)tcg_splitwx_to_rx(s->code_ptr), 2)) {
        return false;
    }

    tcg_out_ld_helper_args(s, lb, &ldst_helper_param);
    tcg_out_call_int(s, qemu_ld_helpers[opc & MO_SIZE]);
    tcg_out_ld_helper_ret(s, lb, false, &ldst_helper_param);

    tgen_gotoi(s, S390_CC_ALWAYS, lb->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOp opc = get_memop(lb->oi);

    if (!patch_reloc(lb->label_ptr[0], R_390_PC16DBL,
                     (intptr_t)tcg_splitwx_to_rx(s->code_ptr), 2)) {
        return false;
    }

    tcg_out_st_helper_args(s, lb, &ldst_helper_param);
    tcg_out_call_int(s, qemu_st_helpers[opc & MO_SIZE]);

    tgen_gotoi(s, S390_CC_ALWAYS, lb->raddr);
    return true;
}

/*
 * For softmmu, perform the TLB load and compare.
 * For useronly, perform any required alignment tests.
 * In both cases, return a TCGLabelQemuLdst structure if the slow path
 * is required and fill in @h with the host address for the fast path.
 */
static TCGLabelQemuLdst *prepare_host_addr(TCGContext *s, HostAddress *h,
                                           TCGReg addr_reg, MemOpIdx oi,
                                           bool is_ld)
{
    TCGLabelQemuLdst *ldst = NULL;
    MemOp opc = get_memop(oi);
    unsigned a_mask;

    h->aa = atom_and_align_for_opc(s, opc, MO_ATOM_IFALIGN, false);
    a_mask = (1 << h->aa.align) - 1;

#ifdef CONFIG_SOFTMMU
    unsigned s_bits = opc & MO_SIZE;
    unsigned s_mask = (1 << s_bits) - 1;
    int mem_index = get_mmuidx(oi);
    int fast_off = TLB_MASK_TABLE_OFS(mem_index);
    int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
    int table_off = fast_off + offsetof(CPUTLBDescFast, table);
    int ofs, a_off;
    uint64_t tlb_mask;

    ldst = new_ldst_label(s);
    ldst->is_ld = is_ld;
    ldst->oi = oi;
    ldst->addrlo_reg = addr_reg;

    tcg_out_sh64(s, RSY_SRLG, TCG_TMP0, addr_reg, TCG_REG_NONE,
                 s->page_bits - CPU_TLB_ENTRY_BITS);

    QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
    QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -(1 << 19));
    tcg_out_insn(s, RXY, NG, TCG_TMP0, TCG_AREG0, TCG_REG_NONE, mask_off);
    tcg_out_insn(s, RXY, AG, TCG_TMP0, TCG_AREG0, TCG_REG_NONE, table_off);

    /*
     * For aligned accesses, we check the first byte and include the alignment
     * bits within the address.  For unaligned access, we check that we don't
     * cross pages using the address of the last byte of the access.
     */
    a_off = (a_mask >= s_mask ? 0 : s_mask - a_mask);
    tlb_mask = (uint64_t)s->page_mask | a_mask;
    if (a_off == 0) {
        tgen_andi_risbg(s, TCG_REG_R0, addr_reg, tlb_mask);
    } else {
        tcg_out_insn(s, RX, LA, TCG_REG_R0, addr_reg, TCG_REG_NONE, a_off);
        tgen_andi(s, TCG_TYPE_TL, TCG_REG_R0, tlb_mask);
    }

    if (is_ld) {
        ofs = offsetof(CPUTLBEntry, addr_read);
    } else {
        ofs = offsetof(CPUTLBEntry, addr_write);
    }
    if (TARGET_LONG_BITS == 32) {
        tcg_out_insn(s, RX, C, TCG_REG_R0, TCG_TMP0, TCG_REG_NONE, ofs);
    } else {
        tcg_out_insn(s, RXY, CG, TCG_REG_R0, TCG_TMP0, TCG_REG_NONE, ofs);
    }

    tcg_out16(s, RI_BRC | (S390_CC_NE << 4));
    ldst->label_ptr[0] = s->code_ptr++;

    h->index = TCG_TMP0;
    tcg_out_insn(s, RXY, LG, h->index, TCG_TMP0, TCG_REG_NONE,
                 offsetof(CPUTLBEntry, addend));

    if (TARGET_LONG_BITS == 32) {
        tcg_out_insn(s, RRE, ALGFR, h->index, addr_reg);
        h->base = TCG_REG_NONE;
    } else {
        h->base = addr_reg;
    }
    h->disp = 0;
#else
    if (a_mask) {
        ldst = new_ldst_label(s);
        ldst->is_ld = is_ld;
        ldst->oi = oi;
        ldst->addrlo_reg = addr_reg;

        /* We are expecting a_bits to max out at 7, much lower than TMLL. */
        tcg_debug_assert(a_mask <= 0xffff);
        tcg_out_insn(s, RI, TMLL, addr_reg, a_mask);

        tcg_out16(s, RI_BRC | (7 << 4)); /* CC in {1,2,3} */
        ldst->label_ptr[0] = s->code_ptr++;
    }

    h->base = addr_reg;
    if (TARGET_LONG_BITS == 32) {
        tcg_out_ext32u(s, TCG_TMP0, addr_reg);
        h->base = TCG_TMP0;
    }
    if (guest_base < 0x80000) {
        h->index = TCG_REG_NONE;
        h->disp = guest_base;
    } else {
        h->index = TCG_GUEST_BASE_REG;
        h->disp = 0;
    }
#endif

    return ldst;
}

static void tcg_out_qemu_ld(TCGContext* s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
{
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr_reg, oi, true);
    tcg_out_qemu_ld_direct(s, get_memop(oi), data_reg, h);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static void tcg_out_qemu_st(TCGContext* s, TCGReg data_reg, TCGReg addr_reg,
                            MemOpIdx oi, TCGType data_type)
{
    TCGLabelQemuLdst *ldst;
    HostAddress h;

    ldst = prepare_host_addr(s, &h, addr_reg, oi, false);
    tcg_out_qemu_st_direct(s, get_memop(oi), data_reg, h);

    if (ldst) {
        ldst->type = data_type;
        ldst->datalo_reg = data_reg;
        ldst->raddr = tcg_splitwx_to_rx(s->code_ptr);
    }
}

static void tcg_out_exit_tb(TCGContext *s, uintptr_t a0)
{
    /* Reuse the zeroing that exists for goto_ptr.  */
    if (a0 == 0) {
        tgen_gotoi(s, S390_CC_ALWAYS, tcg_code_gen_epilogue);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, a0);
        tgen_gotoi(s, S390_CC_ALWAYS, tb_ret_addr);
    }
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    /*
     * Branch displacement must be aligned for atomic patching;
     * see if we need to add extra nop before branch
     */
    if (!QEMU_PTR_IS_ALIGNED(s->code_ptr + 1, 4)) {
        tcg_out16(s, NOP);
    }
    tcg_out16(s, RIL_BRCL | (S390_CC_ALWAYS << 4));
    set_jmp_insn_offset(s, which);
    s->code_ptr += 2;
    set_jmp_reset_offset(s, which);
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    if (!HAVE_FACILITY(GEN_INST_EXT)) {
        return;
    }
    /* patch the branch destination */
    uintptr_t addr = tb->jmp_target_addr[n];
    intptr_t disp = addr - (jmp_rx - 2);
    qatomic_set((int32_t *)jmp_rw, disp / 2);
    /* no need to flush icache explicitly */
}

# define OP_32_64(x) \
        case glue(glue(INDEX_op_,x),_i32): \
        case glue(glue(INDEX_op_,x),_i64)

static inline void tcg_out_op(TCGContext *s, TCGOpcode opc,
                              const TCGArg args[TCG_MAX_OP_ARGS],
                              const int const_args[TCG_MAX_OP_ARGS])
{
    S390Opcode op, op2;
    TCGArg a0, a1, a2;

    switch (opc) {
    case INDEX_op_goto_ptr:
        a0 = args[0];
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
                tcg_out_insn(s, RIL, AFI, a0, a2);
                break;
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
            tcg_out_insn(s, RRFa, SRK, a0, a1, a2);
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
            tcg_out_insn(s, RRFa, NRK, a0, a1, a2);
        }
        break;
    case INDEX_op_or_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tgen_ori(s, a0, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RR, OR, a0, a2);
        } else {
            tcg_out_insn(s, RRFa, ORK, a0, a1, a2);
        }
        break;
    case INDEX_op_xor_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tcg_out_insn(s, RIL, XILF, a0, a2);
        } else if (a0 == a1) {
            tcg_out_insn(s, RR, XR, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRFa, XRK, a0, a1, a2);
        }
        break;

    case INDEX_op_andc_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tgen_andi(s, TCG_TYPE_I32, a0, (uint32_t)~a2);
	} else {
            tcg_out_insn(s, RRFa, NCRK, a0, a1, a2);
	}
        break;
    case INDEX_op_orc_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tgen_ori(s, a0, (uint32_t)~a2);
        } else {
            tcg_out_insn(s, RRFa, OCRK, a0, a1, a2);
        }
        break;
    case INDEX_op_eqv_i32:
        a0 = args[0], a1 = args[1], a2 = (uint32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            tcg_out_insn(s, RIL, XILF, a0, ~a2);
        } else {
            tcg_out_insn(s, RRFa, NXRK, a0, a1, a2);
        }
        break;
    case INDEX_op_nand_i32:
        tcg_out_insn(s, RRFa, NNRK, args[0], args[1], args[2]);
        break;
    case INDEX_op_nor_i32:
        tcg_out_insn(s, RRFa, NORK, args[0], args[1], args[2]);
        break;

    case INDEX_op_neg_i32:
        tcg_out_insn(s, RR, LCR, args[0], args[1]);
        break;
    case INDEX_op_not_i32:
        tcg_out_insn(s, RRFa, NORK, args[0], args[1], args[1]);
        break;

    case INDEX_op_mul_i32:
        a0 = args[0], a1 = args[1], a2 = (int32_t)args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I32, a0, a1);
            if (a2 == (int16_t)a2) {
                tcg_out_insn(s, RI, MHI, a0, a2);
            } else {
                tcg_out_insn(s, RIL, MSFI, a0, a2);
            }
        } else if (a0 == a1) {
            tcg_out_insn(s, RRE, MSR, a0, a2);
        } else {
            tcg_out_insn(s, RRFa, MSRKC, a0, a1, a2);
        }
        break;

    case INDEX_op_div2_i32:
        tcg_debug_assert(args[0] == args[2]);
        tcg_debug_assert(args[1] == args[3]);
        tcg_debug_assert((args[1] & 1) == 0);
        tcg_debug_assert(args[0] == args[1] + 1);
        tcg_out_insn(s, RR, DR, args[1], args[4]);
        break;
    case INDEX_op_divu2_i32:
        tcg_debug_assert(args[0] == args[2]);
        tcg_debug_assert(args[1] == args[3]);
        tcg_debug_assert((args[1] & 1) == 0);
        tcg_debug_assert(args[0] == args[1] + 1);
        tcg_out_insn(s, RRE, DLR, args[1], args[4]);
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

    case INDEX_op_bswap16_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        tcg_out_insn(s, RRE, LRVR, a0, a1);
        if (a2 & TCG_BSWAP_OS) {
            tcg_out_sh32(s, RS_SRA, a0, TCG_REG_NONE, 16);
        } else {
            tcg_out_sh32(s, RS_SRL, a0, TCG_REG_NONE, 16);
        }
        break;
    case INDEX_op_bswap16_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        tcg_out_insn(s, RRE, LRVGR, a0, a1);
        if (a2 & TCG_BSWAP_OS) {
            tcg_out_sh64(s, RSY_SRAG, a0, a0, TCG_REG_NONE, 48);
        } else {
            tcg_out_sh64(s, RSY_SRLG, a0, a0, TCG_REG_NONE, 48);
        }
        break;

    case INDEX_op_bswap32_i32:
        tcg_out_insn(s, RRE, LRVR, args[0], args[1]);
        break;
    case INDEX_op_bswap32_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        tcg_out_insn(s, RRE, LRVR, a0, a1);
        if (a2 & TCG_BSWAP_OS) {
            tcg_out_ext32s(s, a0, a0);
        } else if ((a2 & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
            tcg_out_ext32u(s, a0, a0);
        }
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
                     args[2], const_args[2], args[3], const_args[3], args[4]);
        break;

    case INDEX_op_qemu_ld_a32_i32:
    case INDEX_op_qemu_ld_a64_i32:
        tcg_out_qemu_ld(s, args[0], args[1], args[2], TCG_TYPE_I32);
        break;
    case INDEX_op_qemu_ld_a32_i64:
    case INDEX_op_qemu_ld_a64_i64:
        tcg_out_qemu_ld(s, args[0], args[1], args[2], TCG_TYPE_I64);
        break;
    case INDEX_op_qemu_st_a32_i32:
    case INDEX_op_qemu_st_a64_i32:
        tcg_out_qemu_st(s, args[0], args[1], args[2], TCG_TYPE_I32);
        break;
    case INDEX_op_qemu_st_a32_i64:
    case INDEX_op_qemu_st_a64_i64:
        tcg_out_qemu_st(s, args[0], args[1], args[2], TCG_TYPE_I64);
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
                if (a2 == (int32_t)a2) {
                    tcg_out_insn(s, RIL, AGFI, a0, a2);
                    break;
                }
                if (a2 == (uint32_t)a2) {
                    tcg_out_insn(s, RIL, ALGFI, a0, a2);
                    break;
                }
                if (-a2 == (uint32_t)-a2) {
                    tcg_out_insn(s, RIL, SLGFI, a0, -a2);
                    break;
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
            tcg_out_insn(s, RRFa, SGRK, a0, a1, a2);
        }
        break;

    case INDEX_op_and_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_andi(s, TCG_TYPE_I64, args[0], args[2]);
        } else {
            tcg_out_insn(s, RRFa, NGRK, a0, a1, a2);
        }
        break;
    case INDEX_op_or_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_ori(s, a0, a2);
        } else {
            tcg_out_insn(s, RRFa, OGRK, a0, a1, a2);
        }
        break;
    case INDEX_op_xor_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_xori(s, a0, a2);
        } else {
            tcg_out_insn(s, RRFa, XGRK, a0, a1, a2);
        }
        break;

    case INDEX_op_andc_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_andi(s, TCG_TYPE_I64, a0, ~a2);
        } else {
            tcg_out_insn(s, RRFa, NCGRK, a0, a1, a2);
        }
        break;
    case INDEX_op_orc_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_ori(s, a0, ~a2);
        } else {
            tcg_out_insn(s, RRFa, OCGRK, a0, a1, a2);
        }
        break;
    case INDEX_op_eqv_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            tgen_xori(s, a0, ~a2);
        } else {
            tcg_out_insn(s, RRFa, NXGRK, a0, a1, a2);
        }
        break;
    case INDEX_op_nand_i64:
        tcg_out_insn(s, RRFa, NNGRK, args[0], args[1], args[2]);
        break;
    case INDEX_op_nor_i64:
        tcg_out_insn(s, RRFa, NOGRK, args[0], args[1], args[2]);
        break;

    case INDEX_op_neg_i64:
        tcg_out_insn(s, RRE, LCGR, args[0], args[1]);
        break;
    case INDEX_op_not_i64:
        tcg_out_insn(s, RRFa, NOGRK, args[0], args[1], args[1]);
        break;
    case INDEX_op_bswap64_i64:
        tcg_out_insn(s, RRE, LRVGR, args[0], args[1]);
        break;

    case INDEX_op_mul_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_mov(s, TCG_TYPE_I64, a0, a1);
            if (a2 == (int16_t)a2) {
                tcg_out_insn(s, RI, MGHI, a0, a2);
            } else {
                tcg_out_insn(s, RIL, MSGFI, a0, a2);
            }
        } else if (a0 == a1) {
            tcg_out_insn(s, RRE, MSGR, a0, a2);
        } else {
            tcg_out_insn(s, RRFa, MSGRKC, a0, a1, a2);
        }
        break;

    case INDEX_op_div2_i64:
        /*
         * ??? We get an unnecessary sign-extension of the dividend
         * into op0 with this definition, but as we do in fact always
         * produce both quotient and remainder using INDEX_op_div_i64
         * instead requires jumping through even more hoops.
         */
        tcg_debug_assert(args[0] == args[2]);
        tcg_debug_assert(args[1] == args[3]);
        tcg_debug_assert((args[1] & 1) == 0);
        tcg_debug_assert(args[0] == args[1] + 1);
        tcg_out_insn(s, RRE, DSGR, args[1], args[4]);
        break;
    case INDEX_op_divu2_i64:
        tcg_debug_assert(args[0] == args[2]);
        tcg_debug_assert(args[1] == args[3]);
        tcg_debug_assert((args[1] & 1) == 0);
        tcg_debug_assert(args[0] == args[1] + 1);
        tcg_out_insn(s, RRE, DLGR, args[1], args[4]);
        break;
    case INDEX_op_mulu2_i64:
        tcg_debug_assert(args[0] == args[2]);
        tcg_debug_assert((args[1] & 1) == 0);
        tcg_debug_assert(args[0] == args[1] + 1);
        tcg_out_insn(s, RRE, MLGR, args[1], args[3]);
        break;
    case INDEX_op_muls2_i64:
        tcg_debug_assert((args[1] & 1) == 0);
        tcg_debug_assert(args[0] == args[1] + 1);
        tcg_out_insn(s, RRFa, MGRK, args[1], args[2], args[3]);
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
                     args[2], const_args[2], args[3], const_args[3], args[4]);
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

    case INDEX_op_ctpop_i32:
        tgen_ctpop(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ctpop_i64:
        tgen_ctpop(s, TCG_TYPE_I64, args[0], args[1]);
        break;

    case INDEX_op_mb:
        /* The host memory model is quite strong, we simply need to
           serialize the instruction stream.  */
        if (args[0] & TCG_MO_ST_LD) {
            /* fast-bcr-serialization facility (45) is present */
            tcg_out_insn(s, RR, BCR, 14, 0);
        }
        break;

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    case INDEX_op_exit_tb:  /* Always emitted via tcg_out_exit_tb.  */
    case INDEX_op_goto_tb:  /* Always emitted via tcg_out_goto_tb.  */
    case INDEX_op_ext8s_i32:  /* Always emitted via tcg_reg_alloc_op.  */
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
    case INDEX_op_extrl_i64_i32:
    default:
        g_assert_not_reached();
    }
}

static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg dst, TCGReg src)
{
    if (is_general_reg(src)) {
        /* Replicate general register into two MO_64. */
        tcg_out_insn(s, VRRf, VLVGP, dst, src, src);
        if (vece == MO_64) {
            return true;
        }
        src = dst;
    }

    /*
     * Recall that the "standard" integer, within a vector, is the
     * rightmost element of the leftmost doubleword, a-la VLLEZ.
     */
    tcg_out_insn(s, VRIc, VREP, dst, (8 >> vece) - 1, src, vece);
    return true;
}

static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg dst, TCGReg base, intptr_t offset)
{
    tcg_out_vrx_mem(s, VRX_VLREP, dst, base, TCG_REG_NONE, offset, vece);
    return true;
}

static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg dst, int64_t val)
{
    int i, mask, msb, lsb;

    /* Look for int16_t elements.  */
    if (vece <= MO_16 ||
        (vece == MO_32 ? (int32_t)val : val) == (int16_t)val) {
        tcg_out_insn(s, VRIa, VREPI, dst, val, vece);
        return;
    }

    /* Look for bit masks.  */
    if (vece == MO_32) {
        if (risbg_mask((int32_t)val)) {
            /* Handle wraparound by swapping msb and lsb.  */
            if ((val & 0x80000001u) == 0x80000001u) {
                msb = 32 - ctz32(~val);
                lsb = clz32(~val) - 1;
            } else {
                msb = clz32(val);
                lsb = 31 - ctz32(val);
            }
            tcg_out_insn(s, VRIb, VGM, dst, msb, lsb, MO_32);
            return;
        }
    } else {
        if (risbg_mask(val)) {
            /* Handle wraparound by swapping msb and lsb.  */
            if ((val & 0x8000000000000001ull) == 0x8000000000000001ull) {
                /* Handle wraparound by swapping msb and lsb.  */
                msb = 64 - ctz64(~val);
                lsb = clz64(~val) - 1;
            } else {
                msb = clz64(val);
                lsb = 63 - ctz64(val);
            }
            tcg_out_insn(s, VRIb, VGM, dst, msb, lsb, MO_64);
            return;
        }
    }

    /* Look for all bytes 0x00 or 0xff.  */
    for (i = mask = 0; i < 8; i++) {
        uint8_t byte = val >> (i * 8);
        if (byte == 0xff) {
            mask |= 1 << i;
        } else if (byte != 0) {
            break;
        }
    }
    if (i == 8) {
        tcg_out_insn(s, VRIa, VGBM, dst, mask * 0x0101, 0);
        return;
    }

    /* Otherwise, stuff it in the constant pool.  */
    tcg_out_insn(s, RIL, LARL, TCG_TMP0, 0);
    new_pool_label(s, val, R_390_PC32DBL, s->code_ptr - 2, 2);
    tcg_out_insn(s, VRX, VLREP, dst, TCG_TMP0, TCG_REG_NONE, 0, MO_64);
}

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS])
{
    TCGType type = vecl + TCG_TYPE_V64;
    TCGArg a0 = args[0], a1 = args[1], a2 = args[2];

    switch (opc) {
    case INDEX_op_ld_vec:
        tcg_out_ld(s, type, a0, a1, a2);
        break;
    case INDEX_op_st_vec:
        tcg_out_st(s, type, a0, a1, a2);
        break;
    case INDEX_op_dupm_vec:
        tcg_out_dupm_vec(s, type, vece, a0, a1, a2);
        break;

    case INDEX_op_abs_vec:
        tcg_out_insn(s, VRRa, VLP, a0, a1, vece);
        break;
    case INDEX_op_neg_vec:
        tcg_out_insn(s, VRRa, VLC, a0, a1, vece);
        break;
    case INDEX_op_not_vec:
        tcg_out_insn(s, VRRc, VNO, a0, a1, a1, 0);
        break;

    case INDEX_op_add_vec:
        tcg_out_insn(s, VRRc, VA, a0, a1, a2, vece);
        break;
    case INDEX_op_sub_vec:
        tcg_out_insn(s, VRRc, VS, a0, a1, a2, vece);
        break;
    case INDEX_op_and_vec:
        tcg_out_insn(s, VRRc, VN, a0, a1, a2, 0);
        break;
    case INDEX_op_andc_vec:
        tcg_out_insn(s, VRRc, VNC, a0, a1, a2, 0);
        break;
    case INDEX_op_mul_vec:
        tcg_out_insn(s, VRRc, VML, a0, a1, a2, vece);
        break;
    case INDEX_op_or_vec:
        tcg_out_insn(s, VRRc, VO, a0, a1, a2, 0);
        break;
    case INDEX_op_orc_vec:
        tcg_out_insn(s, VRRc, VOC, a0, a1, a2, 0);
        break;
    case INDEX_op_xor_vec:
        tcg_out_insn(s, VRRc, VX, a0, a1, a2, 0);
        break;
    case INDEX_op_nand_vec:
        tcg_out_insn(s, VRRc, VNN, a0, a1, a2, 0);
        break;
    case INDEX_op_nor_vec:
        tcg_out_insn(s, VRRc, VNO, a0, a1, a2, 0);
        break;
    case INDEX_op_eqv_vec:
        tcg_out_insn(s, VRRc, VNX, a0, a1, a2, 0);
        break;

    case INDEX_op_shli_vec:
        tcg_out_insn(s, VRSa, VESL, a0, a2, TCG_REG_NONE, a1, vece);
        break;
    case INDEX_op_shri_vec:
        tcg_out_insn(s, VRSa, VESRL, a0, a2, TCG_REG_NONE, a1, vece);
        break;
    case INDEX_op_sari_vec:
        tcg_out_insn(s, VRSa, VESRA, a0, a2, TCG_REG_NONE, a1, vece);
        break;
    case INDEX_op_rotli_vec:
        tcg_out_insn(s, VRSa, VERLL, a0, a2, TCG_REG_NONE, a1, vece);
        break;
    case INDEX_op_shls_vec:
        tcg_out_insn(s, VRSa, VESL, a0, 0, a2, a1, vece);
        break;
    case INDEX_op_shrs_vec:
        tcg_out_insn(s, VRSa, VESRL, a0, 0, a2, a1, vece);
        break;
    case INDEX_op_sars_vec:
        tcg_out_insn(s, VRSa, VESRA, a0, 0, a2, a1, vece);
        break;
    case INDEX_op_rotls_vec:
        tcg_out_insn(s, VRSa, VERLL, a0, 0, a2, a1, vece);
        break;
    case INDEX_op_shlv_vec:
        tcg_out_insn(s, VRRc, VESLV, a0, a1, a2, vece);
        break;
    case INDEX_op_shrv_vec:
        tcg_out_insn(s, VRRc, VESRLV, a0, a1, a2, vece);
        break;
    case INDEX_op_sarv_vec:
        tcg_out_insn(s, VRRc, VESRAV, a0, a1, a2, vece);
        break;
    case INDEX_op_rotlv_vec:
        tcg_out_insn(s, VRRc, VERLLV, a0, a1, a2, vece);
        break;

    case INDEX_op_smin_vec:
        tcg_out_insn(s, VRRc, VMN, a0, a1, a2, vece);
        break;
    case INDEX_op_smax_vec:
        tcg_out_insn(s, VRRc, VMX, a0, a1, a2, vece);
        break;
    case INDEX_op_umin_vec:
        tcg_out_insn(s, VRRc, VMNL, a0, a1, a2, vece);
        break;
    case INDEX_op_umax_vec:
        tcg_out_insn(s, VRRc, VMXL, a0, a1, a2, vece);
        break;

    case INDEX_op_bitsel_vec:
        tcg_out_insn(s, VRRe, VSEL, a0, a2, args[3], a1);
        break;

    case INDEX_op_cmp_vec:
        switch ((TCGCond)args[3]) {
        case TCG_COND_EQ:
            tcg_out_insn(s, VRRc, VCEQ, a0, a1, a2, vece);
            break;
        case TCG_COND_GT:
            tcg_out_insn(s, VRRc, VCH, a0, a1, a2, vece);
            break;
        case TCG_COND_GTU:
            tcg_out_insn(s, VRRc, VCHL, a0, a1, a2, vece);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case INDEX_op_s390_vuph_vec:
        tcg_out_insn(s, VRRa, VUPH, a0, a1, vece);
        break;
    case INDEX_op_s390_vupl_vec:
        tcg_out_insn(s, VRRa, VUPL, a0, a1, vece);
        break;
    case INDEX_op_s390_vpks_vec:
        tcg_out_insn(s, VRRc, VPKS, a0, a1, a2, vece);
        break;

    case INDEX_op_mov_vec:   /* Always emitted via tcg_out_mov.  */
    case INDEX_op_dup_vec:   /* Always emitted via tcg_out_dup_vec.  */
    default:
        g_assert_not_reached();
    }
}

int tcg_can_emit_vec_op(TCGOpcode opc, TCGType type, unsigned vece)
{
    switch (opc) {
    case INDEX_op_abs_vec:
    case INDEX_op_add_vec:
    case INDEX_op_and_vec:
    case INDEX_op_andc_vec:
    case INDEX_op_bitsel_vec:
    case INDEX_op_eqv_vec:
    case INDEX_op_nand_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_nor_vec:
    case INDEX_op_not_vec:
    case INDEX_op_or_vec:
    case INDEX_op_orc_vec:
    case INDEX_op_rotli_vec:
    case INDEX_op_rotls_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_sars_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shls_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_shrs_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_xor_vec:
        return 1;
    case INDEX_op_cmp_vec:
    case INDEX_op_cmpsel_vec:
    case INDEX_op_rotrv_vec:
        return -1;
    case INDEX_op_mul_vec:
        return vece < MO_64;
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
        return vece < MO_64 ? -1 : 0;
    default:
        return 0;
    }
}

static bool expand_vec_cmp_noinv(TCGType type, unsigned vece, TCGv_vec v0,
                                 TCGv_vec v1, TCGv_vec v2, TCGCond cond)
{
    bool need_swap = false, need_inv = false;

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_GT:
    case TCG_COND_GTU:
        break;
    case TCG_COND_NE:
    case TCG_COND_LE:
    case TCG_COND_LEU:
        need_inv = true;
        break;
    case TCG_COND_LT:
    case TCG_COND_LTU:
        need_swap = true;
        break;
    case TCG_COND_GE:
    case TCG_COND_GEU:
        need_swap = need_inv = true;
        break;
    default:
        g_assert_not_reached();
    }

    if (need_inv) {
        cond = tcg_invert_cond(cond);
    }
    if (need_swap) {
        TCGv_vec t1;
        t1 = v1, v1 = v2, v2 = t1;
        cond = tcg_swap_cond(cond);
    }

    vec_gen_4(INDEX_op_cmp_vec, type, vece, tcgv_vec_arg(v0),
              tcgv_vec_arg(v1), tcgv_vec_arg(v2), cond);

    return need_inv;
}

static void expand_vec_cmp(TCGType type, unsigned vece, TCGv_vec v0,
                           TCGv_vec v1, TCGv_vec v2, TCGCond cond)
{
    if (expand_vec_cmp_noinv(type, vece, v0, v1, v2, cond)) {
        tcg_gen_not_vec(vece, v0, v0);
    }
}

static void expand_vec_cmpsel(TCGType type, unsigned vece, TCGv_vec v0,
                              TCGv_vec c1, TCGv_vec c2,
                              TCGv_vec v3, TCGv_vec v4, TCGCond cond)
{
    TCGv_vec t = tcg_temp_new_vec(type);

    if (expand_vec_cmp_noinv(type, vece, t, c1, c2, cond)) {
        /* Invert the sense of the compare by swapping arguments.  */
        tcg_gen_bitsel_vec(vece, v0, t, v4, v3);
    } else {
        tcg_gen_bitsel_vec(vece, v0, t, v3, v4);
    }
    tcg_temp_free_vec(t);
}

static void expand_vec_sat(TCGType type, unsigned vece, TCGv_vec v0,
                           TCGv_vec v1, TCGv_vec v2, TCGOpcode add_sub_opc)
{
    TCGv_vec h1 = tcg_temp_new_vec(type);
    TCGv_vec h2 = tcg_temp_new_vec(type);
    TCGv_vec l1 = tcg_temp_new_vec(type);
    TCGv_vec l2 = tcg_temp_new_vec(type);

    tcg_debug_assert (vece < MO_64);

    /* Unpack with sign-extension. */
    vec_gen_2(INDEX_op_s390_vuph_vec, type, vece,
              tcgv_vec_arg(h1), tcgv_vec_arg(v1));
    vec_gen_2(INDEX_op_s390_vuph_vec, type, vece,
              tcgv_vec_arg(h2), tcgv_vec_arg(v2));

    vec_gen_2(INDEX_op_s390_vupl_vec, type, vece,
              tcgv_vec_arg(l1), tcgv_vec_arg(v1));
    vec_gen_2(INDEX_op_s390_vupl_vec, type, vece,
              tcgv_vec_arg(l2), tcgv_vec_arg(v2));

    /* Arithmetic on a wider element size. */
    vec_gen_3(add_sub_opc, type, vece + 1, tcgv_vec_arg(h1),
              tcgv_vec_arg(h1), tcgv_vec_arg(h2));
    vec_gen_3(add_sub_opc, type, vece + 1, tcgv_vec_arg(l1),
              tcgv_vec_arg(l1), tcgv_vec_arg(l2));

    /* Pack with saturation. */
    vec_gen_3(INDEX_op_s390_vpks_vec, type, vece + 1,
              tcgv_vec_arg(v0), tcgv_vec_arg(h1), tcgv_vec_arg(l1));

    tcg_temp_free_vec(h1);
    tcg_temp_free_vec(h2);
    tcg_temp_free_vec(l1);
    tcg_temp_free_vec(l2);
}

void tcg_expand_vec_op(TCGOpcode opc, TCGType type, unsigned vece,
                       TCGArg a0, ...)
{
    va_list va;
    TCGv_vec v0, v1, v2, v3, v4, t0;

    va_start(va, a0);
    v0 = temp_tcgv_vec(arg_temp(a0));
    v1 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
    v2 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));

    switch (opc) {
    case INDEX_op_cmp_vec:
        expand_vec_cmp(type, vece, v0, v1, v2, va_arg(va, TCGArg));
        break;

    case INDEX_op_cmpsel_vec:
        v3 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
        v4 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
        expand_vec_cmpsel(type, vece, v0, v1, v2, v3, v4, va_arg(va, TCGArg));
        break;

    case INDEX_op_rotrv_vec:
        t0 = tcg_temp_new_vec(type);
        tcg_gen_neg_vec(vece, t0, v2);
        tcg_gen_rotlv_vec(vece, v0, v1, t0);
        tcg_temp_free_vec(t0);
        break;

    case INDEX_op_ssadd_vec:
        expand_vec_sat(type, vece, v0, v1, v2, INDEX_op_add_vec);
        break;
    case INDEX_op_sssub_vec:
        expand_vec_sat(type, vece, v0, v1, v2, INDEX_op_sub_vec);
        break;

    default:
        g_assert_not_reached();
    }
    va_end(va);
}

static TCGConstraintSetIndex tcg_target_op_def(TCGOpcode op)
{
    switch (op) {
    case INDEX_op_goto_ptr:
        return C_O0_I1(r);

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
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return C_O0_I2(r, r);

    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_rotl_i32:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i32:
    case INDEX_op_rotr_i64:
    case INDEX_op_setcond_i32:
        return C_O1_I2(r, r, ri);
    case INDEX_op_setcond_i64:
        return C_O1_I2(r, r, rA);

    case INDEX_op_clz_i64:
        return C_O1_I2(r, r, rI);

    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
        return C_O1_I2(r, r, ri);
    case INDEX_op_and_i64:
        return C_O1_I2(r, r, rNKR);
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
        return C_O1_I2(r, r, rK);

    case INDEX_op_andc_i32:
    case INDEX_op_orc_i32:
    case INDEX_op_eqv_i32:
        return C_O1_I2(r, r, ri);
    case INDEX_op_andc_i64:
        return C_O1_I2(r, r, rKR);
    case INDEX_op_orc_i64:
    case INDEX_op_eqv_i64:
        return C_O1_I2(r, r, rNK);

    case INDEX_op_nand_i32:
    case INDEX_op_nand_i64:
    case INDEX_op_nor_i32:
    case INDEX_op_nor_i64:
        return C_O1_I2(r, r, r);

    case INDEX_op_mul_i32:
        return (HAVE_FACILITY(MISC_INSN_EXT2)
                ? C_O1_I2(r, r, ri)
                : C_O1_I2(r, 0, ri));
    case INDEX_op_mul_i64:
        return (HAVE_FACILITY(MISC_INSN_EXT2)
                ? C_O1_I2(r, r, rJ)
                : C_O1_I2(r, 0, rJ));

    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
        return C_O1_I2(r, r, ri);

    case INDEX_op_brcond_i32:
        return C_O0_I2(r, ri);
    case INDEX_op_brcond_i64:
        return C_O0_I2(r, rA);

    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
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
    case INDEX_op_ctpop_i32:
    case INDEX_op_ctpop_i64:
        return C_O1_I1(r, r);

    case INDEX_op_qemu_ld_a32_i32:
    case INDEX_op_qemu_ld_a64_i32:
    case INDEX_op_qemu_ld_a32_i64:
    case INDEX_op_qemu_ld_a64_i64:
        return C_O1_I1(r, r);
    case INDEX_op_qemu_st_a32_i64:
    case INDEX_op_qemu_st_a64_i64:
    case INDEX_op_qemu_st_a32_i32:
    case INDEX_op_qemu_st_a64_i32:
        return C_O0_I2(r, r);

    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        return C_O1_I2(r, rZ, r);

    case INDEX_op_movcond_i32:
        return C_O1_I4(r, r, ri, rI, r);
    case INDEX_op_movcond_i64:
        return C_O1_I4(r, r, rA, rI, r);

    case INDEX_op_div2_i32:
    case INDEX_op_div2_i64:
    case INDEX_op_divu2_i32:
    case INDEX_op_divu2_i64:
        return C_O2_I3(o, m, 0, 1, r);

    case INDEX_op_mulu2_i64:
        return C_O2_I2(o, m, 0, r);
    case INDEX_op_muls2_i64:
        return C_O2_I2(o, m, r, r);

    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        return C_O2_I4(r, r, 0, 1, ri, r);

    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i64:
        return C_O2_I4(r, r, 0, 1, rA, r);

    case INDEX_op_st_vec:
        return C_O0_I2(v, r);
    case INDEX_op_ld_vec:
    case INDEX_op_dupm_vec:
        return C_O1_I1(v, r);
    case INDEX_op_dup_vec:
        return C_O1_I1(v, vr);
    case INDEX_op_abs_vec:
    case INDEX_op_neg_vec:
    case INDEX_op_not_vec:
    case INDEX_op_rotli_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_s390_vuph_vec:
    case INDEX_op_s390_vupl_vec:
        return C_O1_I1(v, v);
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_and_vec:
    case INDEX_op_andc_vec:
    case INDEX_op_or_vec:
    case INDEX_op_orc_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_nand_vec:
    case INDEX_op_nor_vec:
    case INDEX_op_eqv_vec:
    case INDEX_op_cmp_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_s390_vpks_vec:
        return C_O1_I2(v, v, v);
    case INDEX_op_rotls_vec:
    case INDEX_op_shls_vec:
    case INDEX_op_shrs_vec:
    case INDEX_op_sars_vec:
        return C_O1_I2(v, v, r);
    case INDEX_op_bitsel_vec:
        return C_O1_I3(v, v, v, v);

    default:
        g_assert_not_reached();
    }
}

/*
 * Mainline glibc added HWCAP_S390_VX before it was kernel abi.
 * Some distros have fixed this up locally, others have not.
 */
#ifndef HWCAP_S390_VXRS
#define HWCAP_S390_VXRS 2048
#endif

static void query_s390_facilities(void)
{
    unsigned long hwcap = qemu_getauxval(AT_HWCAP);
    const char *which;

    /* Is STORE FACILITY LIST EXTENDED available?  Honestly, I believe this
       is present on all 64-bit systems, but let's check for it anyway.  */
    if (hwcap & HWCAP_S390_STFLE) {
        register int r0 __asm__("0") = ARRAY_SIZE(s390_facilities) - 1;
        register void *r1 __asm__("1") = s390_facilities;

        /* stfle 0(%r1) */
        asm volatile(".word 0xb2b0,0x1000"
                     : "=r"(r0) : "r"(r0), "r"(r1) : "memory", "cc");
    }

    /*
     * Use of vector registers requires os support beyond the facility bit.
     * If the kernel does not advertise support, disable the facility bits.
     * There is nothing else we currently care about in the 3rd word, so
     * disable VECTOR with one store.
     */
    if (!(hwcap & HWCAP_S390_VXRS)) {
        s390_facilities[2] = 0;
    }

    /*
     * Minimum supported cpu revision is z196.
     * Check for all required facilities.
     * ZARCH_ACTIVE is done via preprocessor check for 64-bit.
     */
    if (!HAVE_FACILITY(LONG_DISP)) {
        which = "long-displacement";
        goto fail;
    }
    if (!HAVE_FACILITY(EXT_IMM)) {
        which = "extended-immediate";
        goto fail;
    }
    if (!HAVE_FACILITY(GEN_INST_EXT)) {
        which = "general-instructions-extension";
        goto fail;
    }
    /*
     * Facility 45 is a big bin that contains: distinct-operands,
     * fast-BCR-serialization, high-word, population-count,
     * interlocked-access-1, and load/store-on-condition-1
     */
    if (!HAVE_FACILITY(45)) {
        which = "45";
        goto fail;
    }
    return;

 fail:
    error_report("%s: missing required facility %s", __func__, which);
    exit(EXIT_FAILURE);
}

static void tcg_target_init(TCGContext *s)
{
    query_s390_facilities();

    tcg_target_available_regs[TCG_TYPE_I32] = 0xffff;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffff;
    if (HAVE_FACILITY(VECTOR)) {
        tcg_target_available_regs[TCG_TYPE_V64] = 0xffffffff00000000ull;
        tcg_target_available_regs[TCG_TYPE_V128] = 0xffffffff00000000ull;
    }

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

    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V5);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V6);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V7);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V16);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V17);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V18);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V19);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V20);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V21);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V22);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V23);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V24);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V25);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V26);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V27);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V28);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V29);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V30);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V31);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_TMP0);
    /* XXX many insns can't be used with R0, so we better avoid it for now */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
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
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);

    /* br %r3 (go to TB) */
    tcg_out_insn(s, RR, BCR, S390_CC_ALWAYS, tcg_target_call_iarg_regs[1]);

    /*
     * Return path for goto_ptr. Set return value to 0, a-la exit_tb,
     * and fall through to the rest of the epilogue.
     */
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, 0);

    /* TB epilogue */
    tb_ret_addr = tcg_splitwx_to_rx(s->code_ptr);

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

void tcg_register_jit(const void *buf, size_t buf_size)
{
    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
