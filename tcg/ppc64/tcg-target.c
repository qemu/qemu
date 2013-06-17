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

#define TCG_CT_CONST_S16  0x100
#define TCG_CT_CONST_U16  0x200
#define TCG_CT_CONST_S32  0x400
#define TCG_CT_CONST_U32  0x800
#define TCG_CT_CONST_ZERO 0x1000
#define TCG_CT_CONST_MONE 0x2000

static uint8_t *tb_ret_addr;

#define FAST_PATH

#if TARGET_LONG_BITS == 32
#define LD_ADDR LWZU
#define CMP_L 0
#else
#define LD_ADDR LDU
#define CMP_L (1<<21)
#endif

#ifndef GUEST_BASE
#define GUEST_BASE 0
#endif

#ifdef CONFIG_GETAUXVAL
#include <sys/auxv.h>
static bool have_isa_2_06;
#define HAVE_ISA_2_06  have_isa_2_06
#define HAVE_ISEL      have_isa_2_06
#else
#define HAVE_ISA_2_06  0
#define HAVE_ISEL      0
#endif

#ifdef CONFIG_USE_GUEST_BASE
#define TCG_GUEST_BASE_REG 30
#else
#define TCG_GUEST_BASE_REG 0
#endif

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "r0",
    "r1",
    "r2",
    "r3",
    "r4",
    "r5",
    "r6",
    "r7",
    "r8",
    "r9",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
    "r16",
    "r17",
    "r18",
    "r19",
    "r20",
    "r21",
    "r22",
    "r23",
    "r24",
    "r25",
    "r26",
    "r27",
    "r28",
    "r29",
    "r30",
    "r31"
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31,
#ifdef __APPLE__
    TCG_REG_R2,
#endif
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
#ifndef __APPLE__
    TCG_REG_R11,
#endif
    TCG_REG_R12,
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_R27
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_R3
};

static const int tcg_target_callee_save_regs[] = {
#ifdef __APPLE__
    TCG_REG_R11,
#endif
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_R27, /* currently used for the global env */
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31
};

static uint32_t reloc_pc24_val (void *pc, tcg_target_long target)
{
    tcg_target_long disp;

    disp = target - (tcg_target_long) pc;
    if ((disp << 38) >> 38 != disp)
        tcg_abort ();

    return disp & 0x3fffffc;
}

static void reloc_pc24 (void *pc, tcg_target_long target)
{
    *(uint32_t *) pc = (*(uint32_t *) pc & ~0x3fffffc)
        | reloc_pc24_val (pc, target);
}

static uint16_t reloc_pc14_val (void *pc, tcg_target_long target)
{
    tcg_target_long disp;

    disp = target - (tcg_target_long) pc;
    if (disp != (int16_t) disp)
        tcg_abort ();

    return disp & 0xfffc;
}

static void reloc_pc14 (void *pc, tcg_target_long target)
{
    *(uint32_t *) pc = (*(uint32_t *) pc & ~0xfffc)
        | reloc_pc14_val (pc, target);
}

static void patch_reloc (uint8_t *code_ptr, int type,
                         tcg_target_long value, tcg_target_long addend)
{
    value += addend;
    switch (type) {
    case R_PPC_REL14:
        reloc_pc14 (code_ptr, value);
        break;
    case R_PPC_REL24:
        reloc_pc24 (code_ptr, value);
        break;
    default:
        tcg_abort ();
    }
}

/* parse target specific constraints */
static int target_parse_constraint (TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'A': case 'B': case 'C': case 'D':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg (ct->u.regs, 3 + ct_str[0] - 'A');
        break;
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32 (ct->u.regs, 0, 0xffffffff);
        break;
    case 'L':                   /* qemu_ld constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32 (ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R3);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R5);
#endif
        break;
    case 'S':                   /* qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32 (ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R3);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R5);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R6);
#endif
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_S16;
        break;
    case 'J':
        ct->ct |= TCG_CT_CONST_U16;
        break;
    case 'M':
        ct->ct |= TCG_CT_CONST_MONE;
        break;
    case 'T':
        ct->ct |= TCG_CT_CONST_S32;
        break;
    case 'U':
        ct->ct |= TCG_CT_CONST_U32;
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
static int tcg_target_const_match (tcg_target_long val,
                                   const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S16) && val == (int16_t)val) {
        return 1;
    } else if ((ct & TCG_CT_CONST_U16) && val == (uint16_t)val) {
        return 1;
    } else if ((ct & TCG_CT_CONST_S32) && val == (int32_t)val) {
        return 1;
    } else if ((ct & TCG_CT_CONST_U32) && val == (uint32_t)val) {
        return 1;
    } else if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    } else if ((ct & TCG_CT_CONST_MONE) && val == -1) {
        return 1;
    }
    return 0;
}

#define OPCD(opc) ((opc)<<26)
#define XO19(opc) (OPCD(19)|((opc)<<1))
#define MD30(opc) (OPCD(30)|((opc)<<2))
#define MDS30(opc) (OPCD(30)|((opc)<<1))
#define XO31(opc) (OPCD(31)|((opc)<<1))
#define XO58(opc) (OPCD(58)|(opc))
#define XO62(opc) (OPCD(62)|(opc))

#define B      OPCD( 18)
#define BC     OPCD( 16)
#define LBZ    OPCD( 34)
#define LHZ    OPCD( 40)
#define LHA    OPCD( 42)
#define LWZ    OPCD( 32)
#define STB    OPCD( 38)
#define STH    OPCD( 44)
#define STW    OPCD( 36)

#define STD    XO62(  0)
#define STDU   XO62(  1)
#define STDX   XO31(149)

#define LD     XO58(  0)
#define LDX    XO31( 21)
#define LDU    XO58(  1)
#define LWA    XO58(  2)
#define LWAX   XO31(341)

#define ADDIC  OPCD( 12)
#define ADDI   OPCD( 14)
#define ADDIS  OPCD( 15)
#define ORI    OPCD( 24)
#define ORIS   OPCD( 25)
#define XORI   OPCD( 26)
#define XORIS  OPCD( 27)
#define ANDI   OPCD( 28)
#define ANDIS  OPCD( 29)
#define MULLI  OPCD(  7)
#define CMPLI  OPCD( 10)
#define CMPI   OPCD( 11)
#define SUBFIC OPCD( 8)

#define LWZU   OPCD( 33)
#define STWU   OPCD( 37)

#define RLWIMI OPCD( 20)
#define RLWINM OPCD( 21)
#define RLWNM  OPCD( 23)

#define RLDICL MD30(  0)
#define RLDICR MD30(  1)
#define RLDIMI MD30(  3)
#define RLDCL  MDS30( 8)

#define BCLR   XO19( 16)
#define BCCTR  XO19(528)
#define CRAND  XO19(257)
#define CRANDC XO19(129)
#define CRNAND XO19(225)
#define CROR   XO19(449)
#define CRNOR  XO19( 33)

#define EXTSB  XO31(954)
#define EXTSH  XO31(922)
#define EXTSW  XO31(986)
#define ADD    XO31(266)
#define ADDE   XO31(138)
#define ADDME  XO31(234)
#define ADDZE  XO31(202)
#define ADDC   XO31( 10)
#define AND    XO31( 28)
#define SUBF   XO31( 40)
#define SUBFC  XO31(  8)
#define SUBFE  XO31(136)
#define SUBFME XO31(232)
#define SUBFZE XO31(200)
#define OR     XO31(444)
#define XOR    XO31(316)
#define MULLW  XO31(235)
#define MULHWU XO31( 11)
#define DIVW   XO31(491)
#define DIVWU  XO31(459)
#define CMP    XO31(  0)
#define CMPL   XO31( 32)
#define LHBRX  XO31(790)
#define LWBRX  XO31(534)
#define LDBRX  XO31(532)
#define STHBRX XO31(918)
#define STWBRX XO31(662)
#define STDBRX XO31(660)
#define MFSPR  XO31(339)
#define MTSPR  XO31(467)
#define SRAWI  XO31(824)
#define NEG    XO31(104)
#define MFCR   XO31( 19)
#define MFOCRF (MFCR | (1u << 20))
#define NOR    XO31(124)
#define CNTLZW XO31( 26)
#define CNTLZD XO31( 58)
#define ANDC   XO31( 60)
#define ORC    XO31(412)
#define EQV    XO31(284)
#define NAND   XO31(476)
#define ISEL   XO31( 15)

#define MULLD  XO31(233)
#define MULHD  XO31( 73)
#define MULHDU XO31(  9)
#define DIVD   XO31(489)
#define DIVDU  XO31(457)

#define LBZX   XO31( 87)
#define LHZX   XO31(279)
#define LHAX   XO31(343)
#define LWZX   XO31( 23)
#define STBX   XO31(215)
#define STHX   XO31(407)
#define STWX   XO31(151)

#define SPR(a,b) ((((a)<<5)|(b))<<11)
#define LR     SPR(8, 0)
#define CTR    SPR(9, 0)

#define SLW    XO31( 24)
#define SRW    XO31(536)
#define SRAW   XO31(792)

#define SLD    XO31( 27)
#define SRD    XO31(539)
#define SRAD   XO31(794)
#define SRADI  XO31(413<<1)

#define TW     XO31( 4)
#define TRAP   (TW | TO (31))

#define RT(r) ((r)<<21)
#define RS(r) ((r)<<21)
#define RA(r) ((r)<<16)
#define RB(r) ((r)<<11)
#define TO(t) ((t)<<21)
#define SH(s) ((s)<<11)
#define MB(b) ((b)<<6)
#define ME(e) ((e)<<1)
#define BO(o) ((o)<<21)
#define MB64(b) ((b)<<5)
#define FXM(b) (1 << (19 - (b)))

#define LK    1

#define TAB(t, a, b) (RT(t) | RA(a) | RB(b))
#define SAB(s, a, b) (RS(s) | RA(a) | RB(b))
#define TAI(s, a, i) (RT(s) | RA(a) | ((i) & 0xffff))
#define SAI(s, a, i) (RS(s) | RA(a) | ((i) & 0xffff))

#define BF(n)    ((n)<<23)
#define BI(n, c) (((c)+((n)*4))<<16)
#define BT(n, c) (((c)+((n)*4))<<21)
#define BA(n, c) (((c)+((n)*4))<<16)
#define BB(n, c) (((c)+((n)*4))<<11)
#define BC_(n, c) (((c)+((n)*4))<<6)

#define BO_COND_TRUE  BO (12)
#define BO_COND_FALSE BO ( 4)
#define BO_ALWAYS     BO (20)

enum {
    CR_LT,
    CR_GT,
    CR_EQ,
    CR_SO
};

static const uint32_t tcg_to_bc[] = {
    [TCG_COND_EQ]  = BC | BI (7, CR_EQ) | BO_COND_TRUE,
    [TCG_COND_NE]  = BC | BI (7, CR_EQ) | BO_COND_FALSE,
    [TCG_COND_LT]  = BC | BI (7, CR_LT) | BO_COND_TRUE,
    [TCG_COND_GE]  = BC | BI (7, CR_LT) | BO_COND_FALSE,
    [TCG_COND_LE]  = BC | BI (7, CR_GT) | BO_COND_FALSE,
    [TCG_COND_GT]  = BC | BI (7, CR_GT) | BO_COND_TRUE,
    [TCG_COND_LTU] = BC | BI (7, CR_LT) | BO_COND_TRUE,
    [TCG_COND_GEU] = BC | BI (7, CR_LT) | BO_COND_FALSE,
    [TCG_COND_LEU] = BC | BI (7, CR_GT) | BO_COND_FALSE,
    [TCG_COND_GTU] = BC | BI (7, CR_GT) | BO_COND_TRUE,
};

/* The low bit here is set if the RA and RB fields must be inverted.  */
static const uint32_t tcg_to_isel[] = {
    [TCG_COND_EQ]  = ISEL | BC_(7, CR_EQ),
    [TCG_COND_NE]  = ISEL | BC_(7, CR_EQ) | 1,
    [TCG_COND_LT]  = ISEL | BC_(7, CR_LT),
    [TCG_COND_GE]  = ISEL | BC_(7, CR_LT) | 1,
    [TCG_COND_LE]  = ISEL | BC_(7, CR_GT) | 1,
    [TCG_COND_GT]  = ISEL | BC_(7, CR_GT),
    [TCG_COND_LTU] = ISEL | BC_(7, CR_LT),
    [TCG_COND_GEU] = ISEL | BC_(7, CR_LT) | 1,
    [TCG_COND_LEU] = ISEL | BC_(7, CR_GT) | 1,
    [TCG_COND_GTU] = ISEL | BC_(7, CR_GT),
};

static inline void tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg arg)
{
    tcg_out32 (s, OR | SAB (arg, ret, arg));
}

static inline void tcg_out_rld(TCGContext *s, int op, TCGReg ra, TCGReg rs,
                               int sh, int mb)
{
    sh = SH (sh & 0x1f) | (((sh >> 5) & 1) << 1);
    mb = MB64 ((mb >> 5) | ((mb << 1) & 0x3f));
    tcg_out32 (s, op | RA (ra) | RS (rs) | sh | mb);
}

static inline void tcg_out_rlw(TCGContext *s, int op, TCGReg ra, TCGReg rs,
                               int sh, int mb, int me)
{
    tcg_out32(s, op | RA(ra) | RS(rs) | SH(sh) | MB(mb) | ME(me));
}

static inline void tcg_out_ext32u(TCGContext *s, TCGReg dst, TCGReg src)
{
    tcg_out_rld(s, RLDICL, dst, src, 0, 32);
}

static inline void tcg_out_shli64(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rld(s, RLDICR, dst, src, c, 63 - c);
}

static inline void tcg_out_shri64(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rld(s, RLDICL, dst, src, 64 - c, c);
}

static void tcg_out_movi32(TCGContext *s, TCGReg ret, int32_t arg)
{
    if (arg == (int16_t) arg) {
        tcg_out32(s, ADDI | TAI(ret, 0, arg));
    } else {
        tcg_out32(s, ADDIS | TAI(ret, 0, arg >> 16));
        if (arg & 0xffff) {
            tcg_out32(s, ORI | SAI(ret, ret, arg));
        }
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg ret,
                         tcg_target_long arg)
{
    if (type == TCG_TYPE_I32 || arg == (int32_t)arg) {
        tcg_out_movi32(s, ret, arg);
    } else if (arg == (uint32_t)arg && !(arg & 0x8000)) {
        tcg_out32(s, ADDI | TAI(ret, 0, arg));
        tcg_out32(s, ORIS | SAI(ret, ret, arg >> 16));
    } else {
        int32_t high = arg >> 32;
        tcg_out_movi32(s, ret, high);
        if (high) {
            tcg_out_shli64(s, ret, ret, 32);
        }
        if (arg & 0xffff0000) {
            tcg_out32(s, ORIS | SAI(ret, ret, arg >> 16));
        }
        if (arg & 0xffff) {
            tcg_out32(s, ORI | SAI(ret, ret, arg));
        }
    }
}

static bool mask_operand(uint32_t c, int *mb, int *me)
{
    uint32_t lsb, test;

    /* Accept a bit pattern like:
           0....01....1
           1....10....0
           0..01..10..0
       Keep track of the transitions.  */
    if (c == 0 || c == -1) {
        return false;
    }
    test = c;
    lsb = test & -test;
    test += lsb;
    if (test & (test - 1)) {
        return false;
    }

    *me = clz32(lsb);
    *mb = test ? clz32(test & -test) + 1 : 0;
    return true;
}

static bool mask64_operand(uint64_t c, int *mb, int *me)
{
    uint64_t lsb;

    if (c == 0) {
        return false;
    }

    lsb = c & -c;
    /* Accept 1..10..0.  */
    if (c == -lsb) {
        *mb = 0;
        *me = clz64(lsb);
        return true;
    }
    /* Accept 0..01..1.  */
    if (lsb == 1 && (c & (c + 1)) == 0) {
        *mb = clz64(c + 1) + 1;
        *me = 63;
        return true;
    }
    return false;
}

static void tcg_out_andi32(TCGContext *s, TCGReg dst, TCGReg src, uint32_t c)
{
    int mb, me;

    if ((c & 0xffff) == c) {
        tcg_out32(s, ANDI | SAI(src, dst, c));
        return;
    } else if ((c & 0xffff0000) == c) {
        tcg_out32(s, ANDIS | SAI(src, dst, c >> 16));
        return;
    } else if (mask_operand(c, &mb, &me)) {
        tcg_out_rlw(s, RLWINM, dst, src, 0, mb, me);
    } else {
        tcg_out_movi(s, TCG_TYPE_I32, 0, c);
        tcg_out32(s, AND | SAB(src, dst, 0));
    }
}

static void tcg_out_andi64(TCGContext *s, TCGReg dst, TCGReg src, uint64_t c)
{
    int mb, me;

    if ((c & 0xffff) == c) {
        tcg_out32(s, ANDI | SAI(src, dst, c));
        return;
    } else if ((c & 0xffff0000) == c) {
        tcg_out32(s, ANDIS | SAI(src, dst, c >> 16));
        return;
    } else if (mask64_operand(c, &mb, &me)) {
        if (mb == 0) {
            tcg_out_rld(s, RLDICR, dst, src, 0, me);
        } else {
            tcg_out_rld(s, RLDICL, dst, src, 0, mb);
        }
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, 0, c);
        tcg_out32(s, AND | SAB(src, dst, 0));
    }
}

static void tcg_out_zori32(TCGContext *s, TCGReg dst, TCGReg src, uint32_t c,
                           int op_lo, int op_hi)
{
    if (c >> 16) {
        tcg_out32(s, op_hi | SAI(src, dst, c >> 16));
        src = dst;
    }
    if (c & 0xffff) {
        tcg_out32(s, op_lo | SAI(src, dst, c));
        src = dst;
    }
}

static void tcg_out_ori32(TCGContext *s, TCGReg dst, TCGReg src, uint32_t c)
{
    tcg_out_zori32(s, dst, src, c, ORI, ORIS);
}

static void tcg_out_xori32(TCGContext *s, TCGReg dst, TCGReg src, uint32_t c)
{
    tcg_out_zori32(s, dst, src, c, XORI, XORIS);
}

static void tcg_out_b (TCGContext *s, int mask, tcg_target_long target)
{
    tcg_target_long disp;

    disp = target - (tcg_target_long) s->code_ptr;
    if ((disp << 38) >> 38 == disp)
        tcg_out32 (s, B | (disp & 0x3fffffc) | mask);
    else {
        tcg_out_movi (s, TCG_TYPE_I64, 0, (tcg_target_long) target);
        tcg_out32 (s, MTSPR | RS (0) | CTR);
        tcg_out32 (s, BCCTR | BO_ALWAYS | mask);
    }
}

static void tcg_out_call (TCGContext *s, tcg_target_long arg, int const_arg)
{
#ifdef __APPLE__
    if (const_arg) {
        tcg_out_b (s, LK, arg);
    }
    else {
        tcg_out32 (s, MTSPR | RS (arg) | LR);
        tcg_out32 (s, BCLR | BO_ALWAYS | LK);
    }
#else
    int reg;

    if (const_arg) {
        reg = 2;
        tcg_out_movi (s, TCG_TYPE_I64, reg, arg);
    }
    else reg = arg;

    tcg_out32 (s, LD | RT (0) | RA (reg));
    tcg_out32 (s, MTSPR | RA (0) | CTR);
    tcg_out32 (s, LD | RT (11) | RA (reg) | 16);
    tcg_out32 (s, LD | RT (2) | RA (reg) | 8);
    tcg_out32 (s, BCCTR | BO_ALWAYS | LK);
#endif
}

static void tcg_out_ldst(TCGContext *s, TCGReg ret, TCGReg addr,
                         int offset, int op1, int op2)
{
    if (offset == (int16_t) offset) {
        tcg_out32(s, op1 | TAI(ret, addr, offset));
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, 0, offset);
        tcg_out32(s, op2 | TAB(ret, addr, 0));
    }
}

static void tcg_out_ldsta(TCGContext *s, TCGReg ret, TCGReg addr,
                          int offset, int op1, int op2)
{
    if (offset == (int16_t) (offset & ~3)) {
        tcg_out32(s, op1 | TAI(ret, addr, offset));
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, 0, offset);
        tcg_out32(s, op2 | TAB(ret, addr, 0));
    }
}

#if defined (CONFIG_SOFTMMU)

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

static void tcg_out_tlb_read(TCGContext *s, TCGReg r0, TCGReg r1, TCGReg r2,
                             TCGReg addr_reg, int s_bits, int offset)
{
#if TARGET_LONG_BITS == 32
    tcg_out_ext32u(s, addr_reg, addr_reg);

    tcg_out_rlw(s, RLWINM, r0, addr_reg,
                32 - (TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS),
                32 - (CPU_TLB_BITS + CPU_TLB_ENTRY_BITS),
                31 - CPU_TLB_ENTRY_BITS);
    tcg_out32(s, ADD | TAB(r0, r0, TCG_AREG0));
    tcg_out32(s, LWZU | TAI(r1, r0, offset));
    tcg_out_rlw(s, RLWINM, r2, addr_reg, 0,
                (32 - s_bits) & 31, 31 - TARGET_PAGE_BITS);
#else
    tcg_out_rld (s, RLDICL, r0, addr_reg,
                 64 - TARGET_PAGE_BITS,
                 64 - CPU_TLB_BITS);
    tcg_out_shli64(s, r0, r0, CPU_TLB_ENTRY_BITS);

    tcg_out32(s, ADD | TAB(r0, r0, TCG_AREG0));
    tcg_out32(s, LD_ADDR | TAI(r1, r0, offset));

    if (!s_bits) {
        tcg_out_rld (s, RLDICR, r2, addr_reg, 0, 63 - TARGET_PAGE_BITS);
    }
    else {
        tcg_out_rld (s, RLDICL, r2, addr_reg,
                     64 - TARGET_PAGE_BITS,
                     TARGET_PAGE_BITS - s_bits);
        tcg_out_rld (s, RLDICL, r2, r2, TARGET_PAGE_BITS, 0);
    }
#endif
}
#endif

static const uint32_t qemu_ldx_opc[8] = {
#ifdef TARGET_WORDS_BIGENDIAN
    LBZX, LHZX, LWZX, LDX,
    0,    LHAX, LWAX, LDX
#else
    LBZX, LHBRX, LWBRX, LDBRX,
    0,    0,     0,     LDBRX,
#endif
};

static const uint32_t qemu_stx_opc[4] = {
#ifdef TARGET_WORDS_BIGENDIAN
    STBX, STHX, STWX, STDX
#else
    STBX, STHBRX, STWBRX, STDBRX,
#endif
};

static const uint32_t qemu_exts_opc[4] = {
    EXTSB, EXTSH, EXTSW, 0
};

static void tcg_out_qemu_ld (TCGContext *s, const TCGArg *args, int opc)
{
    TCGReg addr_reg, data_reg, r0, r1, rbase;
    uint32_t insn, s_bits;
#ifdef CONFIG_SOFTMMU
    TCGReg r2, ir;
    int mem_index;
    void *label1_ptr, *label2_ptr;
#endif

    data_reg = *args++;
    addr_reg = *args++;
    s_bits = opc & 3;

#ifdef CONFIG_SOFTMMU
    mem_index = *args;

    r0 = 3;
    r1 = 4;
    r2 = 0;
    rbase = 0;

    tcg_out_tlb_read (s, r0, r1, r2, addr_reg, s_bits,
                      offsetof (CPUArchState, tlb_table[mem_index][0].addr_read));

    tcg_out32 (s, CMP | BF (7) | RA (r2) | RB (r1) | CMP_L);

    label1_ptr = s->code_ptr;
#ifdef FAST_PATH
    tcg_out32 (s, BC | BI (7, CR_EQ) | BO_COND_TRUE);
#endif

    /* slow path */
    ir = 3;
    tcg_out_mov (s, TCG_TYPE_I64, ir++, TCG_AREG0);
    tcg_out_mov (s, TCG_TYPE_I64, ir++, addr_reg);
    tcg_out_movi (s, TCG_TYPE_I64, ir++, mem_index);

    tcg_out_call (s, (tcg_target_long) qemu_ld_helpers[s_bits], 1);

    if (opc & 4) {
        insn = qemu_exts_opc[s_bits];
        tcg_out32(s, insn | RA(data_reg) | RS(3));
    } else if (data_reg != 3) {
        tcg_out_mov(s, TCG_TYPE_I64, data_reg, 3);
    }
    label2_ptr = s->code_ptr;
    tcg_out32 (s, B);

    /* label1: fast path */
#ifdef FAST_PATH
    reloc_pc14 (label1_ptr, (tcg_target_long) s->code_ptr);
#endif

    /* r0 now contains &env->tlb_table[mem_index][index].addr_read */
    tcg_out32(s, LD | TAI(r0, r0,
                          offsetof(CPUTLBEntry, addend)
                          - offsetof(CPUTLBEntry, addr_read)));
    /* r0 = env->tlb_table[mem_index][index].addend */
    tcg_out32(s, ADD | TAB(r0, r0, addr_reg));
    /* r0 = env->tlb_table[mem_index][index].addend + addr */

#else  /* !CONFIG_SOFTMMU */
#if TARGET_LONG_BITS == 32
    tcg_out_ext32u(s, addr_reg, addr_reg);
#endif
    r0 = addr_reg;
    r1 = 3;
    rbase = GUEST_BASE ? TCG_GUEST_BASE_REG : 0;
#endif

    insn = qemu_ldx_opc[opc];
    if (!HAVE_ISA_2_06 && insn == LDBRX) {
        tcg_out32(s, ADDI | TAI(r1, r0, 4));
        tcg_out32(s, LWBRX | TAB(data_reg, rbase, r0));
        tcg_out32(s, LWBRX | TAB(      r1, rbase, r1));
        tcg_out_rld(s, RLDIMI, data_reg, r1, 32, 0);
    } else if (insn) {
        tcg_out32(s, insn | TAB(data_reg, rbase, r0));
    } else {
        insn = qemu_ldx_opc[s_bits];
        tcg_out32(s, insn | TAB(data_reg, rbase, r0));
        insn = qemu_exts_opc[s_bits];
        tcg_out32 (s, insn | RA(data_reg) | RS(data_reg));
    }

#ifdef CONFIG_SOFTMMU
    reloc_pc24 (label2_ptr, (tcg_target_long) s->code_ptr);
#endif
}

static void tcg_out_qemu_st (TCGContext *s, const TCGArg *args, int opc)
{
    TCGReg addr_reg, r0, r1, rbase, data_reg;
    uint32_t insn;
#ifdef CONFIG_SOFTMMU
    TCGReg r2, ir;
    int mem_index;
    void *label1_ptr, *label2_ptr;
#endif

    data_reg = *args++;
    addr_reg = *args++;

#ifdef CONFIG_SOFTMMU
    mem_index = *args;

    r0 = 3;
    r1 = 4;
    r2 = 0;
    rbase = 0;

    tcg_out_tlb_read (s, r0, r1, r2, addr_reg, opc,
                      offsetof (CPUArchState, tlb_table[mem_index][0].addr_write));

    tcg_out32 (s, CMP | BF (7) | RA (r2) | RB (r1) | CMP_L);

    label1_ptr = s->code_ptr;
#ifdef FAST_PATH
    tcg_out32 (s, BC | BI (7, CR_EQ) | BO_COND_TRUE);
#endif

    /* slow path */
    ir = 3;
    tcg_out_mov (s, TCG_TYPE_I64, ir++, TCG_AREG0);
    tcg_out_mov (s, TCG_TYPE_I64, ir++, addr_reg);
    tcg_out_rld (s, RLDICL, ir++, data_reg, 0, 64 - (1 << (3 + opc)));
    tcg_out_movi (s, TCG_TYPE_I64, ir++, mem_index);

    tcg_out_call (s, (tcg_target_long) qemu_st_helpers[opc], 1);

    label2_ptr = s->code_ptr;
    tcg_out32 (s, B);

    /* label1: fast path */
#ifdef FAST_PATH
    reloc_pc14 (label1_ptr, (tcg_target_long) s->code_ptr);
#endif

    tcg_out32 (s, (LD
                   | RT (r0)
                   | RA (r0)
                   | (offsetof (CPUTLBEntry, addend)
                      - offsetof (CPUTLBEntry, addr_write))
                   ));
    /* r0 = env->tlb_table[mem_index][index].addend */
    tcg_out32(s, ADD | TAB(r0, r0, addr_reg));
    /* r0 = env->tlb_table[mem_index][index].addend + addr */

#else  /* !CONFIG_SOFTMMU */
#if TARGET_LONG_BITS == 32
    tcg_out_ext32u(s, addr_reg, addr_reg);
#endif
    r1 = 3;
    r0 = addr_reg;
    rbase = GUEST_BASE ? TCG_GUEST_BASE_REG : 0;
#endif

    insn = qemu_stx_opc[opc];
    if (!HAVE_ISA_2_06 && insn == STDBRX) {
        tcg_out32(s, STWBRX | SAB(data_reg, rbase, r0));
        tcg_out32(s, ADDI | TAI(r1, r0, 4));
        tcg_out_shri64(s, 0, data_reg, 32);
        tcg_out32(s, STWBRX | SAB(0, rbase, r1));
    } else {
        tcg_out32(s, insn | SAB(data_reg, rbase, r0));
    }

#ifdef CONFIG_SOFTMMU
    reloc_pc24 (label2_ptr, (tcg_target_long) s->code_ptr);
#endif
}

static void tcg_target_qemu_prologue (TCGContext *s)
{
    int i, frame_size;
#ifndef __APPLE__
    uint64_t addr;
#endif

    frame_size = 0
        + 8                     /* back chain */
        + 8                     /* CR */
        + 8                     /* LR */
        + 8                     /* compiler doubleword */
        + 8                     /* link editor doubleword */
        + 8                     /* TOC save area */
        + TCG_STATIC_CALL_ARGS_SIZE
        + ARRAY_SIZE (tcg_target_callee_save_regs) * 8
        + CPU_TEMP_BUF_NLONGS * sizeof(long)
        ;
    frame_size = (frame_size + 15) & ~15;

    tcg_set_frame (s, TCG_REG_CALL_STACK, frame_size
                   - CPU_TEMP_BUF_NLONGS * sizeof (long),
                   CPU_TEMP_BUF_NLONGS * sizeof (long));

#ifndef __APPLE__
    /* First emit adhoc function descriptor */
    addr = (uint64_t) s->code_ptr + 24;
    tcg_out32 (s, addr >> 32); tcg_out32 (s, addr); /* entry point */
    s->code_ptr += 16;          /* skip TOC and environment pointer */
#endif

    /* Prologue */
    tcg_out32 (s, MFSPR | RT (0) | LR);
    tcg_out32 (s, STDU | RS (1) | RA (1) | (-frame_size & 0xffff));
    for (i = 0; i < ARRAY_SIZE (tcg_target_callee_save_regs); ++i)
        tcg_out32 (s, (STD
                       | RS (tcg_target_callee_save_regs[i])
                       | RA (1)
                       | (i * 8 + 48 + TCG_STATIC_CALL_ARGS_SIZE)
                       )
            );
    tcg_out32 (s, STD | RS (0) | RA (1) | (frame_size + 16));

#ifdef CONFIG_USE_GUEST_BASE
    if (GUEST_BASE) {
        tcg_out_movi (s, TCG_TYPE_I64, TCG_GUEST_BASE_REG, GUEST_BASE);
        tcg_regset_set_reg (s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out_mov (s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out32 (s, MTSPR | RS (tcg_target_call_iarg_regs[1]) | CTR);
    tcg_out32 (s, BCCTR | BO_ALWAYS);

    /* Epilogue */
    tb_ret_addr = s->code_ptr;

    for (i = 0; i < ARRAY_SIZE (tcg_target_callee_save_regs); ++i)
        tcg_out32 (s, (LD
                       | RT (tcg_target_callee_save_regs[i])
                       | RA (1)
                       | (i * 8 + 48 + TCG_STATIC_CALL_ARGS_SIZE)
                       )
            );
    tcg_out32(s, LD | TAI(0, 1, frame_size + 16));
    tcg_out32(s, MTSPR | RS(0) | LR);
    tcg_out32(s, ADDI | TAI(1, 1, frame_size));
    tcg_out32(s, BCLR | BO_ALWAYS);
}

static void tcg_out_ld (TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1,
                        tcg_target_long arg2)
{
    if (type == TCG_TYPE_I32)
        tcg_out_ldst (s, ret, arg1, arg2, LWZ, LWZX);
    else
        tcg_out_ldsta (s, ret, arg1, arg2, LD, LDX);
}

static void tcg_out_st (TCGContext *s, TCGType type, TCGReg arg, TCGReg arg1,
                        tcg_target_long arg2)
{
    if (type == TCG_TYPE_I32)
        tcg_out_ldst (s, arg, arg1, arg2, STW, STWX);
    else
        tcg_out_ldsta (s, arg, arg1, arg2, STD, STDX);
}

static void tcg_out_cmp(TCGContext *s, int cond, TCGArg arg1, TCGArg arg2,
                        int const_arg2, int cr, TCGType type)
{
    int imm;
    uint32_t op;

    /* Simplify the comparisons below wrt CMPI.  */
    if (type == TCG_TYPE_I32) {
        arg2 = (int32_t)arg2;
    }

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        if (const_arg2) {
            if ((int16_t) arg2 == arg2) {
                op = CMPI;
                imm = 1;
                break;
            }
            else if ((uint16_t) arg2 == arg2) {
                op = CMPLI;
                imm = 1;
                break;
            }
        }
        op = CMPL;
        imm = 0;
        break;

    case TCG_COND_LT:
    case TCG_COND_GE:
    case TCG_COND_LE:
    case TCG_COND_GT:
        if (const_arg2) {
            if ((int16_t) arg2 == arg2) {
                op = CMPI;
                imm = 1;
                break;
            }
        }
        op = CMP;
        imm = 0;
        break;

    case TCG_COND_LTU:
    case TCG_COND_GEU:
    case TCG_COND_LEU:
    case TCG_COND_GTU:
        if (const_arg2) {
            if ((uint16_t) arg2 == arg2) {
                op = CMPLI;
                imm = 1;
                break;
            }
        }
        op = CMPL;
        imm = 0;
        break;

    default:
        tcg_abort ();
    }
    op |= BF(cr) | ((type == TCG_TYPE_I64) << 21);

    if (imm) {
        tcg_out32(s, op | RA(arg1) | (arg2 & 0xffff));
    } else {
        if (const_arg2) {
            tcg_out_movi(s, type, 0, arg2);
            arg2 = 0;
        }
        tcg_out32(s, op | RA(arg1) | RB(arg2));
    }
}

static void tcg_out_setcond_eq0(TCGContext *s, TCGType type,
                                TCGReg dst, TCGReg src)
{
    tcg_out32(s, (type == TCG_TYPE_I64 ? CNTLZD : CNTLZW) | RS(src) | RA(dst));
    tcg_out_shri64(s, dst, dst, type == TCG_TYPE_I64 ? 6 : 5);
}

static void tcg_out_setcond_ne0(TCGContext *s, TCGReg dst, TCGReg src)
{
    /* X != 0 implies X + -1 generates a carry.  Extra addition
       trickery means: R = X-1 + ~X + C = X-1 + (-X+1) + C = C.  */
    if (dst != src) {
        tcg_out32(s, ADDIC | TAI(dst, src, -1));
        tcg_out32(s, SUBFE | TAB(dst, dst, src));
    } else {
        tcg_out32(s, ADDIC | TAI(0, src, -1));
        tcg_out32(s, SUBFE | TAB(dst, 0, src));
    }
}

static TCGReg tcg_gen_setcond_xor(TCGContext *s, TCGReg arg1, TCGArg arg2,
                                  bool const_arg2)
{
    if (const_arg2) {
        if ((uint32_t)arg2 == arg2) {
            tcg_out_xori32(s, TCG_REG_R0, arg1, arg2);
        } else {
            tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_R0, arg2);
            tcg_out32(s, XOR | SAB(arg1, TCG_REG_R0, TCG_REG_R0));
        }
    } else {
        tcg_out32(s, XOR | SAB(arg1, TCG_REG_R0, arg2));
    }
    return TCG_REG_R0;
}

static void tcg_out_setcond(TCGContext *s, TCGType type, TCGCond cond,
                            TCGArg arg0, TCGArg arg1, TCGArg arg2,
                            int const_arg2)
{
    int crop, sh;

    /* Ignore high bits of a potential constant arg2.  */
    if (type == TCG_TYPE_I32) {
        arg2 = (uint32_t)arg2;
    }

    /* Handle common and trivial cases before handling anything else.  */
    if (arg2 == 0) {
        switch (cond) {
        case TCG_COND_EQ:
            tcg_out_setcond_eq0(s, type, arg0, arg1);
            return;
        case TCG_COND_NE:
            if (type == TCG_TYPE_I32) {
                tcg_out_ext32u(s, TCG_REG_R0, arg1);
                arg1 = TCG_REG_R0;
            }
            tcg_out_setcond_ne0(s, arg0, arg1);
            return;
        case TCG_COND_GE:
            tcg_out32(s, NOR | SAB(arg1, arg0, arg1));
            arg1 = arg0;
            /* FALLTHRU */
        case TCG_COND_LT:
            /* Extract the sign bit.  */
            tcg_out_rld(s, RLDICL, arg0, arg1,
                        type == TCG_TYPE_I64 ? 1 : 33, 63);
            return;
        default:
            break;
        }
    }

    /* If we have ISEL, we can implement everything with 3 or 4 insns.
       All other cases below are also at least 3 insns, so speed up the
       code generator by not considering them and always using ISEL.  */
    if (HAVE_ISEL) {
        int isel, tab;

        tcg_out_cmp(s, cond, arg1, arg2, const_arg2, 7, type);

        isel = tcg_to_isel[cond];

        tcg_out_movi(s, type, arg0, 1);
        if (isel & 1) {
            /* arg0 = (bc ? 0 : 1) */
            tab = TAB(arg0, 0, arg0);
            isel &= ~1;
        } else {
            /* arg0 = (bc ? 1 : 0) */
            tcg_out_movi(s, type, TCG_REG_R0, 0);
            tab = TAB(arg0, arg0, TCG_REG_R0);
        }
        tcg_out32(s, isel | tab);
        return;
    }

    switch (cond) {
    case TCG_COND_EQ:
        arg1 = tcg_gen_setcond_xor(s, arg1, arg2, const_arg2);
        tcg_out_setcond_eq0(s, type, arg0, arg1);
        return;

    case TCG_COND_NE:
        arg1 = tcg_gen_setcond_xor(s, arg1, arg2, const_arg2);
        /* Discard the high bits only once, rather than both inputs.  */
        if (type == TCG_TYPE_I32) {
            tcg_out_ext32u(s, TCG_REG_R0, arg1);
            arg1 = TCG_REG_R0;
        }
        tcg_out_setcond_ne0(s, arg0, arg1);
        return;

    case TCG_COND_GT:
    case TCG_COND_GTU:
        sh = 30;
        crop = 0;
        goto crtest;

    case TCG_COND_LT:
    case TCG_COND_LTU:
        sh = 29;
        crop = 0;
        goto crtest;

    case TCG_COND_GE:
    case TCG_COND_GEU:
        sh = 31;
        crop = CRNOR | BT (7, CR_EQ) | BA (7, CR_LT) | BB (7, CR_LT);
        goto crtest;

    case TCG_COND_LE:
    case TCG_COND_LEU:
        sh = 31;
        crop = CRNOR | BT (7, CR_EQ) | BA (7, CR_GT) | BB (7, CR_GT);
    crtest:
        tcg_out_cmp(s, cond, arg1, arg2, const_arg2, 7, type);
        if (crop) {
            tcg_out32(s, crop);
        }
        tcg_out32(s, MFOCRF | RT(TCG_REG_R0) | FXM(7));
        tcg_out_rlw(s, RLWINM, arg0, TCG_REG_R0, sh, 31, 31);
        break;

    default:
        tcg_abort ();
    }
}

static void tcg_out_bc (TCGContext *s, int bc, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (l->has_value)
        tcg_out32 (s, bc | reloc_pc14_val (s->code_ptr, l->u.value));
    else {
        uint16_t val = *(uint16_t *) &s->code_ptr[2];

        /* Thanks to Andrzej Zaborowski */
        tcg_out32 (s, bc | (val & 0xfffc));
        tcg_out_reloc (s, s->code_ptr - 4, R_PPC_REL14, label_index, 0);
    }
}

static void tcg_out_brcond(TCGContext *s, TCGCond cond,
                           TCGArg arg1, TCGArg arg2, int const_arg2,
                           int label_index, TCGType type)
{
    tcg_out_cmp(s, cond, arg1, arg2, const_arg2, 7, type);
    tcg_out_bc(s, tcg_to_bc[cond], label_index);
}

static void tcg_out_movcond(TCGContext *s, TCGType type, TCGCond cond,
                            TCGArg dest, TCGArg c1, TCGArg c2, TCGArg v1,
                            TCGArg v2, bool const_c2)
{
    /* If for some reason both inputs are zero, don't produce bad code.  */
    if (v1 == 0 && v2 == 0) {
        tcg_out_movi(s, type, dest, 0);
        return;
    }

    tcg_out_cmp(s, cond, c1, c2, const_c2, 7, type);

    if (HAVE_ISEL) {
        int isel = tcg_to_isel[cond];

        /* Swap the V operands if the operation indicates inversion.  */
        if (isel & 1) {
            int t = v1;
            v1 = v2;
            v2 = t;
            isel &= ~1;
        }
        /* V1 == 0 is handled by isel; V2 == 0 must be handled by hand.  */
        if (v2 == 0) {
            tcg_out_movi(s, type, 0, 0);
        }
        tcg_out32(s, isel | TAB(dest, v1, v2));
    } else {
        if (dest == v2) {
            cond = tcg_invert_cond(cond);
            v2 = v1;
        } else if (dest != v1) {
            if (v1 == 0) {
                tcg_out_movi(s, type, dest, 0);
            } else {
                tcg_out_mov(s, type, dest, v1);
            }
        }
        /* Branch forward over one insn */
        tcg_out32(s, tcg_to_bc[cond] | 8);
        if (v2 == 0) {
            tcg_out_movi(s, type, dest, 0);
        } else {
            tcg_out_mov(s, type, dest, v2);
        }
    }
}

void ppc_tb_set_jmp_target (unsigned long jmp_addr, unsigned long addr)
{
    TCGContext s;
    unsigned long patch_size;

    s.code_ptr = (uint8_t *) jmp_addr;
    tcg_out_b (&s, 0, addr);
    patch_size = s.code_ptr - (uint8_t *) jmp_addr;
    flush_icache_range (jmp_addr, jmp_addr + patch_size);
}

static void tcg_out_op (TCGContext *s, TCGOpcode opc, const TCGArg *args,
                        const int *const_args)
{
    TCGArg a0, a1, a2;
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi (s, TCG_TYPE_I64, TCG_REG_R3, args[0]);
        tcg_out_b (s, 0, (tcg_target_long) tb_ret_addr);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */

            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
            s->code_ptr += 28;
        }
        else {
            tcg_abort ();
        }
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_br:
        {
            TCGLabel *l = &s->labels[args[0]];

            if (l->has_value) {
                tcg_out_b (s, 0, l->u.value);
            }
            else {
                uint32_t val = *(uint32_t *) s->code_ptr;

                /* Thanks to Andrzej Zaborowski */
                tcg_out32 (s, B | (val & 0x3fffffc));
                tcg_out_reloc (s, s->code_ptr - 4, R_PPC_REL24, args[0], 0);
            }
        }
        break;
    case INDEX_op_call:
        tcg_out_call (s, args[0], const_args[0]);
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi (s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_movi_i64:
        tcg_out_movi (s, TCG_TYPE_I64, args[0], args[1]);
        break;
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], LBZ, LBZX);
        break;
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], LBZ, LBZX);
        tcg_out32 (s, EXTSB | RS (args[0]) | RA (args[0]));
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], LHZ, LHZX);
        break;
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], LHA, LHAX);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], LWZ, LWZX);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_ldsta (s, args[0], args[1], args[2], LWA, LWAX);
        break;
    case INDEX_op_ld_i64:
        tcg_out_ldsta (s, args[0], args[1], args[2], LD, LDX);
        break;
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], STB, STBX);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], STH, STHX);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_ldst (s, args[0], args[1], args[2], STW, STWX);
        break;
    case INDEX_op_st_i64:
        tcg_out_ldsta (s, args[0], args[1], args[2], STD, STDX);
        break;

    case INDEX_op_add_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            int32_t l, h;
        do_addi_32:
            l = (int16_t)a2;
            h = a2 - l;
            if (h) {
                tcg_out32(s, ADDIS | TAI(a0, a1, h >> 16));
                a1 = a0;
            }
            if (l || a0 != a1) {
                tcg_out32(s, ADDI | TAI(a0, a1, l));
            }
        } else {
            tcg_out32(s, ADD | TAB(a0, a1, a2));
        }
        break;
    case INDEX_op_sub_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[1]) {
            if (const_args[2]) {
                tcg_out_movi(s, TCG_TYPE_I32, a0, a1 - a2);
            } else {
                tcg_out32(s, SUBFIC | TAI(a0, a2, a1));
            }
        } else if (const_args[2]) {
            a2 = -a2;
            goto do_addi_32;
        } else {
            tcg_out32(s, SUBF | TAB(a0, a2, a1));
        }
        break;

    case INDEX_op_and_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_andi32(s, a0, a1, a2);
        } else {
            tcg_out32(s, AND | SAB(a1, a0, a2));
        }
        break;
    case INDEX_op_and_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_andi64(s, a0, a1, a2);
        } else {
            tcg_out32(s, AND | SAB(a1, a0, a2));
        }
        break;
    case INDEX_op_or_i64:
    case INDEX_op_or_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_ori32(s, a0, a1, a2);
        } else {
            tcg_out32(s, OR | SAB(a1, a0, a2));
        }
        break;
    case INDEX_op_xor_i64:
    case INDEX_op_xor_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_xori32(s, a0, a1, a2);
        } else {
            tcg_out32(s, XOR | SAB(a1, a0, a2));
        }
        break;
    case INDEX_op_andc_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_andi32(s, a0, a1, ~a2);
        } else {
            tcg_out32(s, ANDC | SAB(a1, a0, a2));
        }
        break;
    case INDEX_op_andc_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out_andi64(s, a0, a1, ~a2);
        } else {
            tcg_out32(s, ANDC | SAB(a1, a0, a2));
        }
        break;
    case INDEX_op_orc_i32:
        if (const_args[2]) {
            tcg_out_ori32(s, args[0], args[1], ~args[2]);
            break;
        }
        /* FALLTHRU */
    case INDEX_op_orc_i64:
        tcg_out32(s, ORC | SAB(args[1], args[0], args[2]));
        break;
    case INDEX_op_eqv_i32:
        if (const_args[2]) {
            tcg_out_xori32(s, args[0], args[1], ~args[2]);
            break;
        }
        /* FALLTHRU */
    case INDEX_op_eqv_i64:
        tcg_out32(s, EQV | SAB(args[1], args[0], args[2]));
        break;
    case INDEX_op_nand_i32:
    case INDEX_op_nand_i64:
        tcg_out32(s, NAND | SAB(args[1], args[0], args[2]));
        break;
    case INDEX_op_nor_i32:
    case INDEX_op_nor_i64:
        tcg_out32(s, NOR | SAB(args[1], args[0], args[2]));
        break;

    case INDEX_op_mul_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out32(s, MULLI | TAI(a0, a1, a2));
        } else {
            tcg_out32(s, MULLW | TAB(a0, a1, a2));
        }
        break;

    case INDEX_op_div_i32:
        tcg_out32 (s, DIVW | TAB (args[0], args[1], args[2]));
        break;

    case INDEX_op_divu_i32:
        tcg_out32 (s, DIVWU | TAB (args[0], args[1], args[2]));
        break;

    case INDEX_op_rem_i32:
        tcg_out32 (s, DIVW | TAB (0, args[1], args[2]));
        tcg_out32 (s, MULLW | TAB (0, 0, args[2]));
        tcg_out32 (s, SUBF | TAB (args[0], 0, args[1]));
        break;

    case INDEX_op_remu_i32:
        tcg_out32 (s, DIVWU | TAB (0, args[1], args[2]));
        tcg_out32 (s, MULLW | TAB (0, 0, args[2]));
        tcg_out32 (s, SUBF | TAB (args[0], 0, args[1]));
        break;

    case INDEX_op_shl_i32:
        if (const_args[2]) {
            tcg_out_rlw(s, RLWINM, args[0], args[1], args[2], 0, 31 - args[2]);
        } else {
            tcg_out32 (s, SLW | SAB (args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_shr_i32:
        if (const_args[2]) {
            tcg_out_rlw(s, RLWINM, args[0], args[1], 32 - args[2], args[2], 31);
        } else {
            tcg_out32 (s, SRW | SAB (args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_sar_i32:
        if (const_args[2])
            tcg_out32 (s, SRAWI | RS (args[1]) | RA (args[0]) | SH (args[2]));
        else
            tcg_out32 (s, SRAW | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_rotl_i32:
        if (const_args[2]) {
            tcg_out_rlw(s, RLWINM, args[0], args[1], args[2], 0, 31);
        } else {
            tcg_out32(s, RLWNM | SAB(args[1], args[0], args[2])
                         | MB(0) | ME(31));
        }
        break;
    case INDEX_op_rotr_i32:
        if (const_args[2]) {
            tcg_out_rlw(s, RLWINM, args[0], args[1], 32 - args[2], 0, 31);
        } else {
            tcg_out32(s, SUBFIC | TAI(0, args[2], 32));
            tcg_out32(s, RLWNM | SAB(args[1], args[0], 0)
                         | MB(0) | ME(31));
        }
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1],
                       args[3], TCG_TYPE_I32);
        break;

    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1],
                       args[3], TCG_TYPE_I64);
        break;

    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
        tcg_out32 (s, NEG | RT (args[0]) | RA (args[1]));
        break;

    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
        tcg_out32 (s, NOR | SAB (args[1], args[0], args[1]));
        break;

    case INDEX_op_add_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            int32_t l0, h1, h2;
        do_addi_64:
            /* We can always split any 32-bit signed constant into 3 pieces.
               Note the positive 0x80000000 coming from the sub_i64 path,
               handled with the same code we need for eg 0x7fff8000.  */
            assert(a2 == (int32_t)a2 || a2 == 0x80000000);
            l0 = (int16_t)a2;
            h1 = a2 - l0;
            h2 = 0;
            if (h1 < 0 && (int64_t)a2 > 0) {
                h2 = 0x40000000;
                h1 = a2 - h2 - l0;
            }
            assert((TCGArg)h2 + h1 + l0 == a2);

            if (h2) {
                tcg_out32(s, ADDIS | TAI(a0, a1, h2 >> 16));
                a1 = a0;
            }
            if (h1) {
                tcg_out32(s, ADDIS | TAI(a0, a1, h1 >> 16));
                a1 = a0;
            }
            if (l0 || a0 != a1) {
                tcg_out32(s, ADDI | TAI(a0, a1, l0));
            }
        } else {
            tcg_out32(s, ADD | TAB(a0, a1, a2));
        }
        break;
    case INDEX_op_sub_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[1]) {
            if (const_args[2]) {
                tcg_out_movi(s, TCG_TYPE_I64, a0, a1 - a2);
            } else {
                tcg_out32(s, SUBFIC | TAI(a0, a2, a1));
            }
        } else if (const_args[2]) {
            a2 = -a2;
            goto do_addi_64;
        } else {
            tcg_out32(s, SUBF | TAB(a0, a2, a1));
        }
        break;

    case INDEX_op_shl_i64:
        if (const_args[2])
            tcg_out_shli64(s, args[0], args[1], args[2]);
        else
            tcg_out32 (s, SLD | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_shr_i64:
        if (const_args[2])
            tcg_out_shri64(s, args[0], args[1], args[2]);
        else
            tcg_out32 (s, SRD | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_sar_i64:
        if (const_args[2]) {
            int sh = SH (args[2] & 0x1f) | (((args[2] >> 5) & 1) << 1);
            tcg_out32 (s, SRADI | RA (args[0]) | RS (args[1]) | sh);
        }
        else
            tcg_out32 (s, SRAD | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_rotl_i64:
        if (const_args[2]) {
            tcg_out_rld(s, RLDICL, args[0], args[1], args[2], 0);
        } else {
            tcg_out32(s, RLDCL | SAB(args[1], args[0], args[2]) | MB64(0));
        }
        break;
    case INDEX_op_rotr_i64:
        if (const_args[2]) {
            tcg_out_rld(s, RLDICL, args[0], args[1], 64 - args[2], 0);
        } else {
            tcg_out32(s, SUBFIC | TAI(0, args[2], 64));
            tcg_out32(s, RLDCL | SAB(args[1], args[0], 0) | MB64(0));
        }
        break;

    case INDEX_op_mul_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
            tcg_out32(s, MULLI | TAI(a0, a1, a2));
        } else {
            tcg_out32(s, MULLD | TAB(a0, a1, a2));
        }
        break;
    case INDEX_op_div_i64:
        tcg_out32 (s, DIVD | TAB (args[0], args[1], args[2]));
        break;
    case INDEX_op_divu_i64:
        tcg_out32 (s, DIVDU | TAB (args[0], args[1], args[2]));
        break;
    case INDEX_op_rem_i64:
        tcg_out32 (s, DIVD | TAB (0, args[1], args[2]));
        tcg_out32 (s, MULLD | TAB (0, 0, args[2]));
        tcg_out32 (s, SUBF | TAB (args[0], 0, args[1]));
        break;
    case INDEX_op_remu_i64:
        tcg_out32 (s, DIVDU | TAB (0, args[1], args[2]));
        tcg_out32 (s, MULLD | TAB (0, 0, args[2]));
        tcg_out32 (s, SUBF | TAB (args[0], 0, args[1]));
        break;

    case INDEX_op_qemu_ld8u:
        tcg_out_qemu_ld (s, args, 0);
        break;
    case INDEX_op_qemu_ld8s:
        tcg_out_qemu_ld (s, args, 0 | 4);
        break;
    case INDEX_op_qemu_ld16u:
        tcg_out_qemu_ld (s, args, 1);
        break;
    case INDEX_op_qemu_ld16s:
        tcg_out_qemu_ld (s, args, 1 | 4);
        break;
    case INDEX_op_qemu_ld32:
    case INDEX_op_qemu_ld32u:
        tcg_out_qemu_ld (s, args, 2);
        break;
    case INDEX_op_qemu_ld32s:
        tcg_out_qemu_ld (s, args, 2 | 4);
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_qemu_ld (s, args, 3);
        break;
    case INDEX_op_qemu_st8:
        tcg_out_qemu_st (s, args, 0);
        break;
    case INDEX_op_qemu_st16:
        tcg_out_qemu_st (s, args, 1);
        break;
    case INDEX_op_qemu_st32:
        tcg_out_qemu_st (s, args, 2);
        break;
    case INDEX_op_qemu_st64:
        tcg_out_qemu_st (s, args, 3);
        break;

    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
        c = EXTSB;
        goto gen_ext;
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
        c = EXTSH;
        goto gen_ext;
    case INDEX_op_ext32s_i64:
        c = EXTSW;
        goto gen_ext;
    gen_ext:
        tcg_out32 (s, c | RS (args[1]) | RA (args[0]));
        break;

    case INDEX_op_setcond_i32:
        tcg_out_setcond (s, TCG_TYPE_I32, args[3], args[0], args[1], args[2],
                         const_args[2]);
        break;
    case INDEX_op_setcond_i64:
        tcg_out_setcond (s, TCG_TYPE_I64, args[3], args[0], args[1], args[2],
                         const_args[2]);
        break;

    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
        a0 = args[0], a1 = args[1];
        /* a1 = abcd */
        if (a0 != a1) {
            /* a0 = (a1 r<< 24) & 0xff # 000c */
            tcg_out_rlw(s, RLWINM, a0, a1, 24, 24, 31);
            /* a0 = (a0 & ~0xff00) | (a1 r<< 8) & 0xff00 # 00dc */
            tcg_out_rlw(s, RLWIMI, a0, a1, 8, 16, 23);
        } else {
            /* r0 = (a1 r<< 8) & 0xff00 # 00d0 */
            tcg_out_rlw(s, RLWINM, TCG_REG_R0, a1, 8, 16, 23);
            /* a0 = (a1 r<< 24) & 0xff # 000c */
            tcg_out_rlw(s, RLWINM, a0, a1, 24, 24, 31);
            /* a0 = a0 | r0 # 00dc */
            tcg_out32(s, OR | SAB(TCG_REG_R0, a0, a0));
        }
        break;

    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
        /* Stolen from gcc's builtin_bswap32 */
        a1 = args[1];
        a0 = args[0] == a1 ? TCG_REG_R0 : args[0];

        /* a1 = args[1] # abcd */
        /* a0 = rotate_left (a1, 8) # bcda */
        tcg_out_rlw(s, RLWINM, a0, a1, 8, 0, 31);
        /* a0 = (a0 & ~0xff000000) | ((a1 r<< 24) & 0xff000000) # dcda */
        tcg_out_rlw(s, RLWIMI, a0, a1, 24, 0, 7);
        /* a0 = (a0 & ~0x0000ff00) | ((a1 r<< 24) & 0x0000ff00) # dcba */
        tcg_out_rlw(s, RLWIMI, a0, a1, 24, 16, 23);

        if (a0 == TCG_REG_R0) {
            tcg_out_mov(s, TCG_TYPE_I64, args[0], a0);
        }
        break;

    case INDEX_op_bswap64_i64:
        a0 = args[0], a1 = args[1], a2 = 0;
        if (a0 == a1) {
            a0 = 0;
            a2 = a1;
        }

        /* a1 = # abcd efgh */
        /* a0 = rl32(a1, 8) # 0000 fghe */
        tcg_out_rlw(s, RLWINM, a0, a1, 8, 0, 31);
        /* a0 = dep(a0, rl32(a1, 24), 0xff000000) # 0000 hghe */
        tcg_out_rlw(s, RLWIMI, a0, a1, 24, 0, 7);
        /* a0 = dep(a0, rl32(a1, 24), 0x0000ff00) # 0000 hgfe */
        tcg_out_rlw(s, RLWIMI, a0, a1, 24, 16, 23);

        /* a0 = rl64(a0, 32) # hgfe 0000 */
        /* a2 = rl64(a1, 32) # efgh abcd */
        tcg_out_rld(s, RLDICL, a0, a0, 32, 0);
        tcg_out_rld(s, RLDICL, a2, a1, 32, 0);

        /* a0 = dep(a0, rl32(a2, 8), 0xffffffff)  # hgfe bcda */
        tcg_out_rlw(s, RLWIMI, a0, a2, 8, 0, 31);
        /* a0 = dep(a0, rl32(a2, 24), 0xff000000) # hgfe dcda */
        tcg_out_rlw(s, RLWIMI, a0, a2, 24, 0, 7);
        /* a0 = dep(a0, rl32(a2, 24), 0x0000ff00) # hgfe dcba */
        tcg_out_rlw(s, RLWIMI, a0, a2, 24, 16, 23);

        if (a0 == 0) {
            tcg_out_mov(s, TCG_TYPE_I64, args[0], a0);
        }
        break;

    case INDEX_op_deposit_i32:
        if (const_args[2]) {
            uint32_t mask = ((2u << (args[4] - 1)) - 1) << args[3];
            tcg_out_andi32(s, args[0], args[0], ~mask);
        } else {
            tcg_out_rlw(s, RLWIMI, args[0], args[2], args[3],
                        32 - args[3] - args[4], 31 - args[3]);
        }
        break;
    case INDEX_op_deposit_i64:
        if (const_args[2]) {
            uint64_t mask = ((2ull << (args[4] - 1)) - 1) << args[3];
            tcg_out_andi64(s, args[0], args[0], ~mask);
        } else {
            tcg_out_rld(s, RLDIMI, args[0], args[2], args[3],
                        64 - args[3] - args[4]);
        }
        break;

    case INDEX_op_movcond_i32:
        tcg_out_movcond(s, TCG_TYPE_I32, args[5], args[0], args[1], args[2],
                        args[3], args[4], const_args[2]);
        break;
    case INDEX_op_movcond_i64:
        tcg_out_movcond(s, TCG_TYPE_I64, args[5], args[0], args[1], args[2],
                        args[3], args[4], const_args[2]);
        break;

    case INDEX_op_add2_i64:
        /* Note that the CA bit is defined based on the word size of the
           environment.  So in 64-bit mode it's always carry-out of bit 63.
           The fallback code using deposit works just as well for 32-bit.  */
        a0 = args[0], a1 = args[1];
        if (a0 == args[3] || (!const_args[5] && a0 == args[5])) {
            a0 = TCG_REG_R0;
        }
        if (const_args[4]) {
            tcg_out32(s, ADDIC | TAI(a0, args[2], args[4]));
        } else {
            tcg_out32(s, ADDC | TAB(a0, args[2], args[4]));
        }
        if (const_args[5]) {
            tcg_out32(s, (args[5] ? ADDME : ADDZE) | RT(a1) | RA(args[3]));
        } else {
            tcg_out32(s, ADDE | TAB(a1, args[3], args[5]));
        }
        if (a0 != args[0]) {
            tcg_out_mov(s, TCG_TYPE_I64, args[0], a0);
        }
        break;

    case INDEX_op_sub2_i64:
        a0 = args[0], a1 = args[1];
        if (a0 == args[5] || (!const_args[4] && a0 == args[4])) {
            a0 = TCG_REG_R0;
        }
        if (const_args[2]) {
            tcg_out32(s, SUBFIC | TAI(a0, args[3], args[2]));
        } else {
            tcg_out32(s, SUBFC | TAB(a0, args[3], args[2]));
        }
        if (const_args[4]) {
            tcg_out32(s, (args[4] ? SUBFME : SUBFZE) | RT(a1) | RA(args[5]));
        } else {
            tcg_out32(s, SUBFE | TAB(a1, args[5], args[4]));
        }
        if (a0 != args[0]) {
            tcg_out_mov(s, TCG_TYPE_I64, args[0], a0);
        }
        break;

    case INDEX_op_mulu2_i64:
    case INDEX_op_muls2_i64:
        {
            int oph = (opc == INDEX_op_mulu2_i64 ? MULHDU : MULHD);
            TCGReg outl = args[0], outh = args[1];
            a0 = args[2], a1 = args[3];

            if (outl == a0 || outl == a1) {
                if (outh == a0 || outh == a1) {
                    outl = TCG_REG_R0;
                } else {
                    tcg_out32(s, oph | TAB(outh, a0, a1));
                    oph = 0;
                }
            }
            tcg_out32(s, MULLD | TAB(outl, a0, a1));
            if (oph != 0) {
                tcg_out32(s, oph | TAB(outh, a0, a1));
            }
            if (outl != args[0]) {
                tcg_out_mov(s, TCG_TYPE_I64, args[0], outl);
            }
        }
        break;

    default:
        tcg_dump_ops (s);
        tcg_abort ();
    }
}

static const TCGTargetOpDef ppc_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_mov_i64, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },
    { INDEX_op_movi_i64, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },
    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st8_i64, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st16_i64, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },
    { INDEX_op_st_i64, { "r", "r" } },
    { INDEX_op_st32_i64, { "r", "r" } },

    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },

    { INDEX_op_add_i32, { "r", "r", "ri" } },
    { INDEX_op_mul_i32, { "r", "r", "rI" } },
    { INDEX_op_div_i32, { "r", "r", "r" } },
    { INDEX_op_divu_i32, { "r", "r", "r" } },
    { INDEX_op_rem_i32, { "r", "r", "r" } },
    { INDEX_op_remu_i32, { "r", "r", "r" } },
    { INDEX_op_sub_i32, { "r", "rI", "ri" } },
    { INDEX_op_and_i32, { "r", "r", "ri" } },
    { INDEX_op_or_i32, { "r", "r", "ri" } },
    { INDEX_op_xor_i32, { "r", "r", "ri" } },
    { INDEX_op_andc_i32, { "r", "r", "ri" } },
    { INDEX_op_orc_i32, { "r", "r", "ri" } },
    { INDEX_op_eqv_i32, { "r", "r", "ri" } },
    { INDEX_op_nand_i32, { "r", "r", "r" } },
    { INDEX_op_nor_i32, { "r", "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "ri" } },
    { INDEX_op_shr_i32, { "r", "r", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },
    { INDEX_op_rotl_i32, { "r", "r", "ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "ri" } },

    { INDEX_op_brcond_i32, { "r", "ri" } },
    { INDEX_op_brcond_i64, { "r", "ri" } },

    { INDEX_op_neg_i32, { "r", "r" } },
    { INDEX_op_not_i32, { "r", "r" } },

    { INDEX_op_add_i64, { "r", "r", "rT" } },
    { INDEX_op_sub_i64, { "r", "rI", "rT" } },
    { INDEX_op_and_i64, { "r", "r", "ri" } },
    { INDEX_op_or_i64, { "r", "r", "rU" } },
    { INDEX_op_xor_i64, { "r", "r", "rU" } },
    { INDEX_op_andc_i64, { "r", "r", "ri" } },
    { INDEX_op_orc_i64, { "r", "r", "r" } },
    { INDEX_op_eqv_i64, { "r", "r", "r" } },
    { INDEX_op_nand_i64, { "r", "r", "r" } },
    { INDEX_op_nor_i64, { "r", "r", "r" } },

    { INDEX_op_shl_i64, { "r", "r", "ri" } },
    { INDEX_op_shr_i64, { "r", "r", "ri" } },
    { INDEX_op_sar_i64, { "r", "r", "ri" } },
    { INDEX_op_rotl_i64, { "r", "r", "ri" } },
    { INDEX_op_rotr_i64, { "r", "r", "ri" } },

    { INDEX_op_mul_i64, { "r", "r", "rI" } },
    { INDEX_op_div_i64, { "r", "r", "r" } },
    { INDEX_op_divu_i64, { "r", "r", "r" } },
    { INDEX_op_rem_i64, { "r", "r", "r" } },
    { INDEX_op_remu_i64, { "r", "r", "r" } },

    { INDEX_op_neg_i64, { "r", "r" } },
    { INDEX_op_not_i64, { "r", "r" } },

    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L" } },
    { INDEX_op_qemu_ld32s, { "r", "L" } },
    { INDEX_op_qemu_ld64, { "r", "L" } },

    { INDEX_op_qemu_st8, { "S", "S" } },
    { INDEX_op_qemu_st16, { "S", "S" } },
    { INDEX_op_qemu_st32, { "S", "S" } },
    { INDEX_op_qemu_st64, { "S", "S" } },

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext8s_i64, { "r", "r" } },
    { INDEX_op_ext16s_i64, { "r", "r" } },
    { INDEX_op_ext32s_i64, { "r", "r" } },

    { INDEX_op_setcond_i32, { "r", "r", "ri" } },
    { INDEX_op_setcond_i64, { "r", "r", "ri" } },
    { INDEX_op_movcond_i32, { "r", "r", "ri", "rZ", "rZ" } },
    { INDEX_op_movcond_i64, { "r", "r", "ri", "rZ", "rZ" } },

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap16_i64, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },
    { INDEX_op_bswap32_i64, { "r", "r" } },
    { INDEX_op_bswap64_i64, { "r", "r" } },

    { INDEX_op_deposit_i32, { "r", "0", "rZ" } },
    { INDEX_op_deposit_i64, { "r", "0", "rZ" } },

    { INDEX_op_add2_i64, { "r", "r", "r", "r", "rI", "rZM" } },
    { INDEX_op_sub2_i64, { "r", "r", "rI", "r", "rZM", "r" } },
    { INDEX_op_muls2_i64, { "r", "r", "r", "r" } },
    { INDEX_op_mulu2_i64, { "r", "r", "r", "r" } },

    { -1 },
};

static void tcg_target_init (TCGContext *s)
{
#ifdef CONFIG_GETAUXVAL
    unsigned long hwcap = getauxval(AT_HWCAP);
    if (hwcap & PPC_FEATURE_ARCH_2_06) {
        have_isa_2_06 = true;
    }
#endif

    tcg_regset_set32 (tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
    tcg_regset_set32 (tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffffffff);
    tcg_regset_set32 (tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_R0) |
#ifdef __APPLE__
                     (1 << TCG_REG_R2) |
#endif
                     (1 << TCG_REG_R3) |
                     (1 << TCG_REG_R4) |
                     (1 << TCG_REG_R5) |
                     (1 << TCG_REG_R6) |
                     (1 << TCG_REG_R7) |
                     (1 << TCG_REG_R8) |
                     (1 << TCG_REG_R9) |
                     (1 << TCG_REG_R10) |
                     (1 << TCG_REG_R11) |
                     (1 << TCG_REG_R12)
        );

    tcg_regset_clear (s->reserved_regs);
    tcg_regset_set_reg (s->reserved_regs, TCG_REG_R0);
    tcg_regset_set_reg (s->reserved_regs, TCG_REG_R1);
#ifndef __APPLE__
    tcg_regset_set_reg (s->reserved_regs, TCG_REG_R2);
#endif
    tcg_regset_set_reg (s->reserved_regs, TCG_REG_R13);

    tcg_add_target_add_op_defs (ppc_op_defs);
}
