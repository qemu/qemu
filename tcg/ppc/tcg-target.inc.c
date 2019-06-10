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

#include "elf.h"
#include "tcg-pool.inc.c"

#if defined _CALL_DARWIN || defined __APPLE__
#define TCG_TARGET_CALL_DARWIN
#endif
#ifdef _CALL_SYSV
# define TCG_TARGET_CALL_ALIGN_ARGS   1
#endif

/* For some memory operations, we need a scratch that isn't R0.  For the AIX
   calling convention, we can re-use the TOC register since we'll be reloading
   it at every call.  Otherwise R12 will do nicely as neither a call-saved
   register nor a parameter register.  */
#ifdef _CALL_AIX
# define TCG_REG_TMP1   TCG_REG_R2
#else
# define TCG_REG_TMP1   TCG_REG_R12
#endif

#define TCG_REG_TB     TCG_REG_R31
#define USE_REG_TB     (TCG_TARGET_REG_BITS == 64)

/* Shorthand for size of a pointer.  Avoid promotion to unsigned.  */
#define SZP  ((int)sizeof(void *))

/* Shorthand for size of a register.  */
#define SZR  (TCG_TARGET_REG_BITS / 8)

#define TCG_CT_CONST_S16  0x100
#define TCG_CT_CONST_U16  0x200
#define TCG_CT_CONST_S32  0x400
#define TCG_CT_CONST_U32  0x800
#define TCG_CT_CONST_ZERO 0x1000
#define TCG_CT_CONST_MONE 0x2000
#define TCG_CT_CONST_WSZ  0x4000

static tcg_insn_unit *tb_ret_addr;

bool have_isa_2_06;
bool have_isa_3_00;

#define HAVE_ISA_2_06  have_isa_2_06
#define HAVE_ISEL      have_isa_2_06

#ifndef CONFIG_SOFTMMU
#define TCG_GUEST_BASE_REG 30
#endif

#ifdef CONFIG_DEBUG_TCG
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
    TCG_REG_R14,  /* call saved registers */
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
    TCG_REG_R27,
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31,
    TCG_REG_R12,  /* call clobbered, non-arguments */
    TCG_REG_R11,
    TCG_REG_R2,
    TCG_REG_R13,
    TCG_REG_R10,  /* call clobbered, arguments */
    TCG_REG_R9,
    TCG_REG_R8,
    TCG_REG_R7,
    TCG_REG_R6,
    TCG_REG_R5,
    TCG_REG_R4,
    TCG_REG_R3,
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
    TCG_REG_R3,
    TCG_REG_R4
};

static const int tcg_target_callee_save_regs[] = {
#ifdef TCG_TARGET_CALL_DARWIN
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

static inline bool in_range_b(tcg_target_long target)
{
    return target == sextract64(target, 0, 26);
}

static uint32_t reloc_pc24_val(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    tcg_debug_assert(in_range_b(disp));
    return disp & 0x3fffffc;
}

static bool reloc_pc24(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    if (in_range_b(disp)) {
        *pc = (*pc & ~0x3fffffc) | (disp & 0x3fffffc);
        return true;
    }
    return false;
}

static uint16_t reloc_pc14_val(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    tcg_debug_assert(disp == (int16_t) disp);
    return disp & 0xfffc;
}

static bool reloc_pc14(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    if (disp == (int16_t) disp) {
        *pc = (*pc & ~0xfffc) | (disp & 0xfffc);
        return true;
    }
    return false;
}

/* parse target specific constraints */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch (*ct_str++) {
    case 'A': case 'B': case 'C': case 'D':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, 3 + ct_str[0] - 'A');
        break;
    case 'r':
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        break;
    case 'L':                   /* qemu_ld constraint */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R5);
#endif
        break;
    case 'S':                   /* qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R5);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R6);
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
    case 'W':
        ct->ct |= TCG_CT_CONST_WSZ;
        break;
    case 'Z':
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return NULL;
    }
    return ct_str;
}

/* test if a constant matches the constraint */
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    }

    /* The only 32-bit constraint we use aside from
       TCG_CT_CONST is TCG_CT_CONST_S16.  */
    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    }

    if ((ct & TCG_CT_CONST_S16) && val == (int16_t)val) {
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
    } else if ((ct & TCG_CT_CONST_WSZ)
               && val == (type == TCG_TYPE_I32 ? 32 : 64)) {
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
#define LWZUX  XO31( 55)
#define STB    OPCD( 38)
#define STH    OPCD( 44)
#define STW    OPCD( 36)

#define STD    XO62(  0)
#define STDU   XO62(  1)
#define STDX   XO31(149)

#define LD     XO58(  0)
#define LDX    XO31( 21)
#define LDU    XO58(  1)
#define LDUX   XO31( 53)
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
#define MULHW  XO31( 75)
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
#define CNTTZW XO31(538)
#define CNTTZD XO31(570)
#define CNTPOPW XO31(378)
#define CNTPOPD XO31(506)
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

#define EIEIO  XO31(854)
#define HWSYNC XO31(598)
#define LWSYNC (HWSYNC | (1u << 21))

#define SPR(a, b) ((((a)<<5)|(b))<<11)
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
#define TRAP   (TW | TO(31))

#define NOP    ORI  /* ori 0,0,0 */

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

#define BO_COND_TRUE  BO(12)
#define BO_COND_FALSE BO( 4)
#define BO_ALWAYS     BO(20)

enum {
    CR_LT,
    CR_GT,
    CR_EQ,
    CR_SO
};

static const uint32_t tcg_to_bc[] = {
    [TCG_COND_EQ]  = BC | BI(7, CR_EQ) | BO_COND_TRUE,
    [TCG_COND_NE]  = BC | BI(7, CR_EQ) | BO_COND_FALSE,
    [TCG_COND_LT]  = BC | BI(7, CR_LT) | BO_COND_TRUE,
    [TCG_COND_GE]  = BC | BI(7, CR_LT) | BO_COND_FALSE,
    [TCG_COND_LE]  = BC | BI(7, CR_GT) | BO_COND_FALSE,
    [TCG_COND_GT]  = BC | BI(7, CR_GT) | BO_COND_TRUE,
    [TCG_COND_LTU] = BC | BI(7, CR_LT) | BO_COND_TRUE,
    [TCG_COND_GEU] = BC | BI(7, CR_LT) | BO_COND_FALSE,
    [TCG_COND_LEU] = BC | BI(7, CR_GT) | BO_COND_FALSE,
    [TCG_COND_GTU] = BC | BI(7, CR_GT) | BO_COND_TRUE,
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

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_insn_unit *target;

    value += addend;
    target = (tcg_insn_unit *)value;

    switch (type) {
    case R_PPC_REL14:
        return reloc_pc14(code_ptr, target);
    case R_PPC_REL24:
        return reloc_pc24(code_ptr, target);
    case R_PPC_ADDR16:
        /*
         * We are (slightly) abusing this relocation type.  In particular,
         * assert that the low 2 bits are zero, and do not modify them.
         * That way we can use this with LD et al that have opcode bits
         * in the low 2 bits of the insn.
         */
        if ((value & 3) || value != (int16_t)value) {
            return false;
        }
        *code_ptr = (*code_ptr & ~0xfffc) | (value & 0xfffc);
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

static void tcg_out_mem_long(TCGContext *s, int opi, int opx, TCGReg rt,
                             TCGReg base, tcg_target_long offset);

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    tcg_debug_assert(TCG_TARGET_REG_BITS == 64 || type == TCG_TYPE_I32);
    if (ret != arg) {
        tcg_out32(s, OR | SAB(arg, ret, arg));
    }
    return true;
}

static inline void tcg_out_rld(TCGContext *s, int op, TCGReg ra, TCGReg rs,
                               int sh, int mb)
{
    tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
    sh = SH(sh & 0x1f) | (((sh >> 5) & 1) << 1);
    mb = MB64((mb >> 5) | ((mb << 1) & 0x3f));
    tcg_out32(s, op | RA(ra) | RS(rs) | sh | mb);
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

static inline void tcg_out_shli32(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rlw(s, RLWINM, dst, src, c, 0, 31 - c);
}

static inline void tcg_out_shli64(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rld(s, RLDICR, dst, src, c, 63 - c);
}

static inline void tcg_out_shri32(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rlw(s, RLWINM, dst, src, 32 - c, c, 31);
}

static inline void tcg_out_shri64(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rld(s, RLDICL, dst, src, 64 - c, c);
}

/* Emit a move into ret of arg, if it can be done in one insn.  */
static bool tcg_out_movi_one(TCGContext *s, TCGReg ret, tcg_target_long arg)
{
    if (arg == (int16_t)arg) {
        tcg_out32(s, ADDI | TAI(ret, 0, arg));
        return true;
    }
    if (arg == (int32_t)arg && (arg & 0xffff) == 0) {
        tcg_out32(s, ADDIS | TAI(ret, 0, arg >> 16));
        return true;
    }
    return false;
}

static void tcg_out_movi_int(TCGContext *s, TCGType type, TCGReg ret,
                             tcg_target_long arg, bool in_prologue)
{
    intptr_t tb_diff;
    tcg_target_long tmp;
    int shift;

    tcg_debug_assert(TCG_TARGET_REG_BITS == 64 || type == TCG_TYPE_I32);

    if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I32) {
        arg = (int32_t)arg;
    }

    /* Load 16-bit immediates with one insn.  */
    if (tcg_out_movi_one(s, ret, arg)) {
        return;
    }

    /* Load addresses within the TB with one insn.  */
    tb_diff = arg - (intptr_t)s->code_gen_ptr;
    if (!in_prologue && USE_REG_TB && tb_diff == (int16_t)tb_diff) {
        tcg_out32(s, ADDI | TAI(ret, TCG_REG_TB, tb_diff));
        return;
    }

    /* Load 32-bit immediates with two insns.  Note that we've already
       eliminated bare ADDIS, so we know both insns are required.  */
    if (TCG_TARGET_REG_BITS == 32 || arg == (int32_t)arg) {
        tcg_out32(s, ADDIS | TAI(ret, 0, arg >> 16));
        tcg_out32(s, ORI | SAI(ret, ret, arg));
        return;
    }
    if (arg == (uint32_t)arg && !(arg & 0x8000)) {
        tcg_out32(s, ADDI | TAI(ret, 0, arg));
        tcg_out32(s, ORIS | SAI(ret, ret, arg >> 16));
        return;
    }

    /* Load masked 16-bit value.  */
    if (arg > 0 && (arg & 0x8000)) {
        tmp = arg | 0x7fff;
        if ((tmp & (tmp + 1)) == 0) {
            int mb = clz64(tmp + 1) + 1;
            tcg_out32(s, ADDI | TAI(ret, 0, arg));
            tcg_out_rld(s, RLDICL, ret, ret, 0, mb);
            return;
        }
    }

    /* Load common masks with 2 insns.  */
    shift = ctz64(arg);
    tmp = arg >> shift;
    if (tmp == (int16_t)tmp) {
        tcg_out32(s, ADDI | TAI(ret, 0, tmp));
        tcg_out_shli64(s, ret, ret, shift);
        return;
    }
    shift = clz64(arg);
    if (tcg_out_movi_one(s, ret, arg << shift)) {
        tcg_out_shri64(s, ret, ret, shift);
        return;
    }

    /* Load addresses within 2GB of TB with 2 (or rarely 3) insns.  */
    if (!in_prologue && USE_REG_TB && tb_diff == (int32_t)tb_diff) {
        tcg_out_mem_long(s, ADDI, ADD, ret, TCG_REG_TB, tb_diff);
        return;
    }

    /* Use the constant pool, if possible.  */
    if (!in_prologue && USE_REG_TB) {
        new_pool_label(s, arg, R_PPC_ADDR16, s->code_ptr,
                       -(intptr_t)s->code_gen_ptr);
        tcg_out32(s, LD | TAI(ret, TCG_REG_TB, 0));
        return;
    }

    tmp = arg >> 31 >> 1;
    tcg_out_movi(s, TCG_TYPE_I32, ret, tmp);
    if (tmp) {
        tcg_out_shli64(s, ret, ret, 32);
    }
    if (arg & 0xffff0000) {
        tcg_out32(s, ORIS | SAI(ret, ret, arg >> 16));
    }
    if (arg & 0xffff) {
        tcg_out32(s, ORI | SAI(ret, ret, arg));
    }
}

static inline void tcg_out_movi(TCGContext *s, TCGType type, TCGReg ret,
                                tcg_target_long arg)
{
    tcg_out_movi_int(s, type, ret, arg, false);
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

    if (mask_operand(c, &mb, &me)) {
        tcg_out_rlw(s, RLWINM, dst, src, 0, mb, me);
    } else if ((c & 0xffff) == c) {
        tcg_out32(s, ANDI | SAI(src, dst, c));
        return;
    } else if ((c & 0xffff0000) == c) {
        tcg_out32(s, ANDIS | SAI(src, dst, c >> 16));
        return;
    } else {
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R0, c);
        tcg_out32(s, AND | SAB(src, dst, TCG_REG_R0));
    }
}

static void tcg_out_andi64(TCGContext *s, TCGReg dst, TCGReg src, uint64_t c)
{
    int mb, me;

    tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
    if (mask64_operand(c, &mb, &me)) {
        if (mb == 0) {
            tcg_out_rld(s, RLDICR, dst, src, 0, me);
        } else {
            tcg_out_rld(s, RLDICL, dst, src, 0, mb);
        }
    } else if ((c & 0xffff) == c) {
        tcg_out32(s, ANDI | SAI(src, dst, c));
        return;
    } else if ((c & 0xffff0000) == c) {
        tcg_out32(s, ANDIS | SAI(src, dst, c >> 16));
        return;
    } else {
        tcg_out_movi(s, TCG_TYPE_I64, TCG_REG_R0, c);
        tcg_out32(s, AND | SAB(src, dst, TCG_REG_R0));
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

static void tcg_out_b(TCGContext *s, int mask, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_pcrel_diff(s, target);
    if (in_range_b(disp)) {
        tcg_out32(s, B | (disp & 0x3fffffc) | mask);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R0, (uintptr_t)target);
        tcg_out32(s, MTSPR | RS(TCG_REG_R0) | CTR);
        tcg_out32(s, BCCTR | BO_ALWAYS | mask);
    }
}

static void tcg_out_mem_long(TCGContext *s, int opi, int opx, TCGReg rt,
                             TCGReg base, tcg_target_long offset)
{
    tcg_target_long orig = offset, l0, l1, extra = 0, align = 0;
    bool is_store = false;
    TCGReg rs = TCG_REG_TMP1;

    switch (opi) {
    case LD: case LWA:
        align = 3;
        /* FALLTHRU */
    default:
        if (rt != TCG_REG_R0) {
            rs = rt;
            break;
        }
        break;
    case STD:
        align = 3;
        /* FALLTHRU */
    case STB: case STH: case STW:
        is_store = true;
        break;
    }

    /* For unaligned, or very large offsets, use the indexed form.  */
    if (offset & align || offset != (int32_t)offset) {
        if (rs == base) {
            rs = TCG_REG_R0;
        }
        tcg_debug_assert(!is_store || rs != rt);
        tcg_out_movi(s, TCG_TYPE_PTR, rs, orig);
        tcg_out32(s, opx | TAB(rt, base, rs));
        return;
    }

    l0 = (int16_t)offset;
    offset = (offset - l0) >> 16;
    l1 = (int16_t)offset;

    if (l1 < 0 && orig >= 0) {
        extra = 0x4000;
        l1 = (int16_t)(offset - 0x4000);
    }
    if (l1) {
        tcg_out32(s, ADDIS | TAI(rs, base, l1));
        base = rs;
    }
    if (extra) {
        tcg_out32(s, ADDIS | TAI(rs, base, extra));
        base = rs;
    }
    if (opi != ADDI || base != rt || l0 != 0) {
        tcg_out32(s, opi | TAI(rt, base, l0));
    }
}

static inline void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                              TCGReg arg1, intptr_t arg2)
{
    int opi, opx;

    tcg_debug_assert(TCG_TARGET_REG_BITS == 64 || type == TCG_TYPE_I32);
    if (type == TCG_TYPE_I32) {
        opi = LWZ, opx = LWZX;
    } else {
        opi = LD, opx = LDX;
    }
    tcg_out_mem_long(s, opi, opx, ret, arg1, arg2);
}

static inline void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg arg1, intptr_t arg2)
{
    int opi, opx;

    tcg_debug_assert(TCG_TARGET_REG_BITS == 64 || type == TCG_TYPE_I32);
    if (type == TCG_TYPE_I32) {
        opi = STW, opx = STWX;
    } else {
        opi = STD, opx = STDX;
    }
    tcg_out_mem_long(s, opi, opx, arg, arg1, arg2);
}

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    return false;
}

static void tcg_out_cmp(TCGContext *s, int cond, TCGArg arg1, TCGArg arg2,
                        int const_arg2, int cr, TCGType type)
{
    int imm;
    uint32_t op;

    tcg_debug_assert(TCG_TARGET_REG_BITS == 64 || type == TCG_TYPE_I32);

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
            } else if ((uint16_t) arg2 == arg2) {
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
        tcg_abort();
    }
    op |= BF(cr) | ((type == TCG_TYPE_I64) << 21);

    if (imm) {
        tcg_out32(s, op | RA(arg1) | (arg2 & 0xffff));
    } else {
        if (const_arg2) {
            tcg_out_movi(s, type, TCG_REG_R0, arg2);
            arg2 = TCG_REG_R0;
        }
        tcg_out32(s, op | RA(arg1) | RB(arg2));
    }
}

static void tcg_out_setcond_eq0(TCGContext *s, TCGType type,
                                TCGReg dst, TCGReg src)
{
    if (type == TCG_TYPE_I32) {
        tcg_out32(s, CNTLZW | RS(src) | RA(dst));
        tcg_out_shri32(s, dst, dst, 5);
    } else {
        tcg_out32(s, CNTLZD | RS(src) | RA(dst));
        tcg_out_shri64(s, dst, dst, 6);
    }
}

static void tcg_out_setcond_ne0(TCGContext *s, TCGReg dst, TCGReg src)
{
    /* X != 0 implies X + -1 generates a carry.  Extra addition
       trickery means: R = X-1 + ~X + C = X-1 + (-X+1) + C = C.  */
    if (dst != src) {
        tcg_out32(s, ADDIC | TAI(dst, src, -1));
        tcg_out32(s, SUBFE | TAB(dst, dst, src));
    } else {
        tcg_out32(s, ADDIC | TAI(TCG_REG_R0, src, -1));
        tcg_out32(s, SUBFE | TAB(dst, TCG_REG_R0, src));
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

    tcg_debug_assert(TCG_TARGET_REG_BITS == 64 || type == TCG_TYPE_I32);

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
            if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I32) {
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
            if (type == TCG_TYPE_I32) {
                tcg_out_shri32(s, arg0, arg1, 31);
            } else {
                tcg_out_shri64(s, arg0, arg1, 63);
            }
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
        if (TCG_TARGET_REG_BITS == 64 && type == TCG_TYPE_I32) {
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
        crop = CRNOR | BT(7, CR_EQ) | BA(7, CR_LT) | BB(7, CR_LT);
        goto crtest;

    case TCG_COND_LE:
    case TCG_COND_LEU:
        sh = 31;
        crop = CRNOR | BT(7, CR_EQ) | BA(7, CR_GT) | BB(7, CR_GT);
    crtest:
        tcg_out_cmp(s, cond, arg1, arg2, const_arg2, 7, type);
        if (crop) {
            tcg_out32(s, crop);
        }
        tcg_out32(s, MFOCRF | RT(TCG_REG_R0) | FXM(7));
        tcg_out_rlw(s, RLWINM, arg0, TCG_REG_R0, sh, 31, 31);
        break;

    default:
        tcg_abort();
    }
}

static void tcg_out_bc(TCGContext *s, int bc, TCGLabel *l)
{
    if (l->has_value) {
        bc |= reloc_pc14_val(s->code_ptr, l->u.value_ptr);
    } else {
        tcg_out_reloc(s, s->code_ptr, R_PPC_REL14, l, 0);
    }
    tcg_out32(s, bc);
}

static void tcg_out_brcond(TCGContext *s, TCGCond cond,
                           TCGArg arg1, TCGArg arg2, int const_arg2,
                           TCGLabel *l, TCGType type)
{
    tcg_out_cmp(s, cond, arg1, arg2, const_arg2, 7, type);
    tcg_out_bc(s, tcg_to_bc[cond], l);
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
            tcg_out_movi(s, type, TCG_REG_R0, 0);
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

static void tcg_out_cntxz(TCGContext *s, TCGType type, uint32_t opc,
                          TCGArg a0, TCGArg a1, TCGArg a2, bool const_a2)
{
    if (const_a2 && a2 == (type == TCG_TYPE_I32 ? 32 : 64)) {
        tcg_out32(s, opc | RA(a0) | RS(a1));
    } else {
        tcg_out_cmp(s, TCG_COND_EQ, a1, 0, 1, 7, type);
        /* Note that the only other valid constant for a2 is 0.  */
        if (HAVE_ISEL) {
            tcg_out32(s, opc | RA(TCG_REG_R0) | RS(a1));
            tcg_out32(s, tcg_to_isel[TCG_COND_EQ] | TAB(a0, a2, TCG_REG_R0));
        } else if (!const_a2 && a0 == a2) {
            tcg_out32(s, tcg_to_bc[TCG_COND_EQ] | 8);
            tcg_out32(s, opc | RA(a0) | RS(a1));
        } else {
            tcg_out32(s, opc | RA(a0) | RS(a1));
            tcg_out32(s, tcg_to_bc[TCG_COND_NE] | 8);
            if (const_a2) {
                tcg_out_movi(s, type, a0, 0);
            } else {
                tcg_out_mov(s, type, a0, a2);
            }
        }
    }
}

static void tcg_out_cmp2(TCGContext *s, const TCGArg *args,
                         const int *const_args)
{
    static const struct { uint8_t bit1, bit2; } bits[] = {
        [TCG_COND_LT ] = { CR_LT, CR_LT },
        [TCG_COND_LE ] = { CR_LT, CR_GT },
        [TCG_COND_GT ] = { CR_GT, CR_GT },
        [TCG_COND_GE ] = { CR_GT, CR_LT },
        [TCG_COND_LTU] = { CR_LT, CR_LT },
        [TCG_COND_LEU] = { CR_LT, CR_GT },
        [TCG_COND_GTU] = { CR_GT, CR_GT },
        [TCG_COND_GEU] = { CR_GT, CR_LT },
    };

    TCGCond cond = args[4], cond2;
    TCGArg al, ah, bl, bh;
    int blconst, bhconst;
    int op, bit1, bit2;

    al = args[0];
    ah = args[1];
    bl = args[2];
    bh = args[3];
    blconst = const_args[2];
    bhconst = const_args[3];

    switch (cond) {
    case TCG_COND_EQ:
        op = CRAND;
        goto do_equality;
    case TCG_COND_NE:
        op = CRNAND;
    do_equality:
        tcg_out_cmp(s, cond, al, bl, blconst, 6, TCG_TYPE_I32);
        tcg_out_cmp(s, cond, ah, bh, bhconst, 7, TCG_TYPE_I32);
        tcg_out32(s, op | BT(7, CR_EQ) | BA(6, CR_EQ) | BB(7, CR_EQ));
        break;

    case TCG_COND_LT:
    case TCG_COND_LE:
    case TCG_COND_GT:
    case TCG_COND_GE:
    case TCG_COND_LTU:
    case TCG_COND_LEU:
    case TCG_COND_GTU:
    case TCG_COND_GEU:
        bit1 = bits[cond].bit1;
        bit2 = bits[cond].bit2;
        op = (bit1 != bit2 ? CRANDC : CRAND);
        cond2 = tcg_unsigned_cond(cond);

        tcg_out_cmp(s, cond, ah, bh, bhconst, 6, TCG_TYPE_I32);
        tcg_out_cmp(s, cond2, al, bl, blconst, 7, TCG_TYPE_I32);
        tcg_out32(s, op | BT(7, CR_EQ) | BA(6, CR_EQ) | BB(7, bit2));
        tcg_out32(s, CROR | BT(7, CR_EQ) | BA(6, bit1) | BB(7, CR_EQ));
        break;

    default:
        tcg_abort();
    }
}

static void tcg_out_setcond2(TCGContext *s, const TCGArg *args,
                             const int *const_args)
{
    tcg_out_cmp2(s, args + 1, const_args + 1);
    tcg_out32(s, MFOCRF | RT(TCG_REG_R0) | FXM(7));
    tcg_out_rlw(s, RLWINM, args[0], TCG_REG_R0, 31, 31, 31);
}

static void tcg_out_brcond2 (TCGContext *s, const TCGArg *args,
                             const int *const_args)
{
    tcg_out_cmp2(s, args, const_args);
    tcg_out_bc(s, BC | BI(7, CR_EQ) | BO_COND_TRUE, arg_label(args[5]));
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    uint32_t insn = HWSYNC;
    a0 &= TCG_MO_ALL;
    if (a0 == TCG_MO_LD_LD) {
        insn = LWSYNC;
    } else if (a0 == TCG_MO_ST_ST) {
        insn = EIEIO;
    }
    tcg_out32(s, insn);
}

void tb_target_set_jmp_target(uintptr_t tc_ptr, uintptr_t jmp_addr,
                              uintptr_t addr)
{
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_insn_unit i1, i2;
        intptr_t tb_diff = addr - tc_ptr;
        intptr_t br_diff = addr - (jmp_addr + 4);
        uint64_t pair;

        /* This does not exercise the range of the branch, but we do
           still need to be able to load the new value of TCG_REG_TB.
           But this does still happen quite often.  */
        if (tb_diff == (int16_t)tb_diff) {
            i1 = ADDI | TAI(TCG_REG_TB, TCG_REG_TB, tb_diff);
            i2 = B | (br_diff & 0x3fffffc);
        } else {
            intptr_t lo = (int16_t)tb_diff;
            intptr_t hi = (int32_t)(tb_diff - lo);
            assert(tb_diff == hi + lo);
            i1 = ADDIS | TAI(TCG_REG_TB, TCG_REG_TB, hi >> 16);
            i2 = ADDI | TAI(TCG_REG_TB, TCG_REG_TB, lo);
        }
#ifdef HOST_WORDS_BIGENDIAN
        pair = (uint64_t)i1 << 32 | i2;
#else
        pair = (uint64_t)i2 << 32 | i1;
#endif

        /* As per the enclosing if, this is ppc64.  Avoid the _Static_assert
           within atomic_set that would fail to build a ppc32 host.  */
        atomic_set__nocheck((uint64_t *)jmp_addr, pair);
        flush_icache_range(jmp_addr, jmp_addr + 8);
    } else {
        intptr_t diff = addr - jmp_addr;
        tcg_debug_assert(in_range_b(diff));
        atomic_set((uint32_t *)jmp_addr, B | (diff & 0x3fffffc));
        flush_icache_range(jmp_addr, jmp_addr + 4);
    }
}

static void tcg_out_call(TCGContext *s, tcg_insn_unit *target)
{
#ifdef _CALL_AIX
    /* Look through the descriptor.  If the branch is in range, and we
       don't have to spend too much effort on building the toc.  */
    void *tgt = ((void **)target)[0];
    uintptr_t toc = ((uintptr_t *)target)[1];
    intptr_t diff = tcg_pcrel_diff(s, tgt);

    if (in_range_b(diff) && toc == (uint32_t)toc) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP1, toc);
        tcg_out_b(s, LK, tgt);
    } else {
        /* Fold the low bits of the constant into the addresses below.  */
        intptr_t arg = (intptr_t)target;
        int ofs = (int16_t)arg;

        if (ofs + 8 < 0x8000) {
            arg -= ofs;
        } else {
            ofs = 0;
        }
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP1, arg);
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R0, TCG_REG_TMP1, ofs);
        tcg_out32(s, MTSPR | RA(TCG_REG_R0) | CTR);
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R2, TCG_REG_TMP1, ofs + SZP);
        tcg_out32(s, BCCTR | BO_ALWAYS | LK);
    }
#elif defined(_CALL_ELF) && _CALL_ELF == 2
    intptr_t diff;

    /* In the ELFv2 ABI, we have to set up r12 to contain the destination
       address, which the callee uses to compute its TOC address.  */
    /* FIXME: when the branch is in range, we could avoid r12 load if we
       knew that the destination uses the same TOC, and what its local
       entry point offset is.  */
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R12, (intptr_t)target);

    diff = tcg_pcrel_diff(s, target);
    if (in_range_b(diff)) {
        tcg_out_b(s, LK, target);
    } else {
        tcg_out32(s, MTSPR | RS(TCG_REG_R12) | CTR);
        tcg_out32(s, BCCTR | BO_ALWAYS | LK);
    }
#else
    tcg_out_b(s, LK, target);
#endif
}

static const uint32_t qemu_ldx_opc[16] = {
    [MO_UB] = LBZX,
    [MO_UW] = LHZX,
    [MO_UL] = LWZX,
    [MO_Q]  = LDX,
    [MO_SW] = LHAX,
    [MO_SL] = LWAX,
    [MO_BSWAP | MO_UB] = LBZX,
    [MO_BSWAP | MO_UW] = LHBRX,
    [MO_BSWAP | MO_UL] = LWBRX,
    [MO_BSWAP | MO_Q]  = LDBRX,
};

static const uint32_t qemu_stx_opc[16] = {
    [MO_UB] = STBX,
    [MO_UW] = STHX,
    [MO_UL] = STWX,
    [MO_Q]  = STDX,
    [MO_BSWAP | MO_UB] = STBX,
    [MO_BSWAP | MO_UW] = STHBRX,
    [MO_BSWAP | MO_UL] = STWBRX,
    [MO_BSWAP | MO_Q]  = STDBRX,
};

static const uint32_t qemu_exts_opc[4] = {
    EXTSB, EXTSH, EXTSW, 0
};

#if defined (CONFIG_SOFTMMU)
#include "tcg-ldst.inc.c"

/* helper signature: helper_ld_mmu(CPUState *env, target_ulong addr,
 *                                 int mmu_idx, uintptr_t ra)
 */
static void * const qemu_ld_helpers[16] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
    [MO_LEQ]  = helper_le_ldq_mmu,
    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
    [MO_BEQ]  = helper_be_ldq_mmu,
};

/* helper signature: helper_st_mmu(CPUState *env, target_ulong addr,
 *                                 uintxx_t val, int mmu_idx, uintptr_t ra)
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

/* We expect to use a 16-bit negative offset from ENV.  */
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -32768);

/* Perform the TLB load and compare.  Places the result of the comparison
   in CR7, loads the addend of the TLB into R3, and returns the register
   containing the guest address (zero-extended into R4).  Clobbers R0 and R2. */

static TCGReg tcg_out_tlb_read(TCGContext *s, TCGMemOp opc,
                               TCGReg addrlo, TCGReg addrhi,
                               int mem_index, bool is_read)
{
    int cmp_off
        = (is_read
           ? offsetof(CPUTLBEntry, addr_read)
           : offsetof(CPUTLBEntry, addr_write));
    int fast_off = TLB_MASK_TABLE_OFS(mem_index);
    int mask_off = fast_off + offsetof(CPUTLBDescFast, mask);
    int table_off = fast_off + offsetof(CPUTLBDescFast, table);
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_bits = get_alignment_bits(opc);

    /* Load tlb_mask[mmu_idx] and tlb_table[mmu_idx].  */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R3, TCG_AREG0, mask_off);
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R4, TCG_AREG0, table_off);

    /* Extract the page index, shifted into place for tlb index.  */
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_out_shri32(s, TCG_REG_TMP1, addrlo,
                       TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    } else {
        tcg_out_shri64(s, TCG_REG_TMP1, addrlo,
                       TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    }
    tcg_out32(s, AND | SAB(TCG_REG_R3, TCG_REG_R3, TCG_REG_TMP1));

    /* Load the TLB comparator.  */
    if (cmp_off == 0 && TCG_TARGET_REG_BITS >= TARGET_LONG_BITS) {
        uint32_t lxu = (TCG_TARGET_REG_BITS == 32 || TARGET_LONG_BITS == 32
                        ? LWZUX : LDUX);
        tcg_out32(s, lxu | TAB(TCG_REG_TMP1, TCG_REG_R3, TCG_REG_R4));
    } else {
        tcg_out32(s, ADD | TAB(TCG_REG_R3, TCG_REG_R3, TCG_REG_R4));
        if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
            tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_TMP1, TCG_REG_R3, cmp_off + 4);
            tcg_out_ld(s, TCG_TYPE_I32, TCG_REG_R4, TCG_REG_R3, cmp_off);
        } else {
            tcg_out_ld(s, TCG_TYPE_TL, TCG_REG_TMP1, TCG_REG_R3, cmp_off);
        }
    }

    /* Load the TLB addend for use on the fast path.  Do this asap
       to minimize any load use delay.  */
    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R3, TCG_REG_R3,
               offsetof(CPUTLBEntry, addend));

    /* Clear the non-page, non-alignment bits from the address */
    if (TCG_TARGET_REG_BITS == 32) {
        /* We don't support unaligned accesses on 32-bits.
         * Preserve the bottom bits and thus trigger a comparison
         * failure on unaligned accesses.
         */
        if (a_bits < s_bits) {
            a_bits = s_bits;
        }
        tcg_out_rlw(s, RLWINM, TCG_REG_R0, addrlo, 0,
                    (32 - a_bits) & 31, 31 - TARGET_PAGE_BITS);
    } else {
        TCGReg t = addrlo;

        /* If the access is unaligned, we need to make sure we fail if we
         * cross a page boundary.  The trick is to add the access size-1
         * to the address before masking the low bits.  That will make the
         * address overflow to the next page if we cross a page boundary,
         * which will then force a mismatch of the TLB compare.
         */
        if (a_bits < s_bits) {
            unsigned a_mask = (1 << a_bits) - 1;
            unsigned s_mask = (1 << s_bits) - 1;
            tcg_out32(s, ADDI | TAI(TCG_REG_R0, t, s_mask - a_mask));
            t = TCG_REG_R0;
        }

        /* Mask the address for the requested alignment.  */
        if (TARGET_LONG_BITS == 32) {
            tcg_out_rlw(s, RLWINM, TCG_REG_R0, t, 0,
                        (32 - a_bits) & 31, 31 - TARGET_PAGE_BITS);
            /* Zero-extend the address for use in the final address.  */
            tcg_out_ext32u(s, TCG_REG_R4, addrlo);
            addrlo = TCG_REG_R4;
        } else if (a_bits == 0) {
            tcg_out_rld(s, RLDICR, TCG_REG_R0, t, 0, 63 - TARGET_PAGE_BITS);
        } else {
            tcg_out_rld(s, RLDICL, TCG_REG_R0, t,
                        64 - TARGET_PAGE_BITS, TARGET_PAGE_BITS - a_bits);
            tcg_out_rld(s, RLDICL, TCG_REG_R0, TCG_REG_R0, TARGET_PAGE_BITS, 0);
        }
    }

    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        tcg_out_cmp(s, TCG_COND_EQ, TCG_REG_R0, TCG_REG_TMP1,
                    0, 7, TCG_TYPE_I32);
        tcg_out_cmp(s, TCG_COND_EQ, addrhi, TCG_REG_R4, 0, 6, TCG_TYPE_I32);
        tcg_out32(s, CRAND | BT(7, CR_EQ) | BA(6, CR_EQ) | BB(7, CR_EQ));
    } else {
        tcg_out_cmp(s, TCG_COND_EQ, TCG_REG_R0, TCG_REG_TMP1,
                    0, 7, TCG_TYPE_TL);
    }

    return addrlo;
}

/* Record the context of a call to the out of line helper code for the slow
   path for a load or store, so that we can later generate the correct
   helper code.  */
static void add_qemu_ldst_label(TCGContext *s, bool is_ld, TCGMemOpIdx oi,
                                TCGReg datalo_reg, TCGReg datahi_reg,
                                TCGReg addrlo_reg, TCGReg addrhi_reg,
                                tcg_insn_unit *raddr, tcg_insn_unit *lptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->oi = oi;
    label->datalo_reg = datalo_reg;
    label->datahi_reg = datahi_reg;
    label->addrlo_reg = addrlo_reg;
    label->addrhi_reg = addrhi_reg;
    label->raddr = raddr;
    label->label_ptr[0] = lptr;
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGMemOpIdx oi = lb->oi;
    TCGMemOp opc = get_memop(oi);
    TCGReg hi, lo, arg = TCG_REG_R3;

    if (!reloc_pc14(lb->label_ptr[0], s->code_ptr)) {
        return false;
    }

    tcg_out_mov(s, TCG_TYPE_PTR, arg++, TCG_AREG0);

    lo = lb->addrlo_reg;
    hi = lb->addrhi_reg;
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        arg |= 1;
#endif
        tcg_out_mov(s, TCG_TYPE_I32, arg++, hi);
        tcg_out_mov(s, TCG_TYPE_I32, arg++, lo);
    } else {
        /* If the address needed to be zero-extended, we'll have already
           placed it in R4.  The only remaining case is 64-bit guest.  */
        tcg_out_mov(s, TCG_TYPE_TL, arg++, lo);
    }

    tcg_out_movi(s, TCG_TYPE_I32, arg++, oi);
    tcg_out32(s, MFSPR | RT(arg) | LR);

    tcg_out_call(s, qemu_ld_helpers[opc & (MO_BSWAP | MO_SIZE)]);

    lo = lb->datalo_reg;
    hi = lb->datahi_reg;
    if (TCG_TARGET_REG_BITS == 32 && (opc & MO_SIZE) == MO_64) {
        tcg_out_mov(s, TCG_TYPE_I32, lo, TCG_REG_R4);
        tcg_out_mov(s, TCG_TYPE_I32, hi, TCG_REG_R3);
    } else if (opc & MO_SIGN) {
        uint32_t insn = qemu_exts_opc[opc & MO_SIZE];
        tcg_out32(s, insn | RA(lo) | RS(TCG_REG_R3));
    } else {
        tcg_out_mov(s, TCG_TYPE_REG, lo, TCG_REG_R3);
    }

    tcg_out_b(s, 0, lb->raddr);
    return true;
}

static bool tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    TCGMemOpIdx oi = lb->oi;
    TCGMemOp opc = get_memop(oi);
    TCGMemOp s_bits = opc & MO_SIZE;
    TCGReg hi, lo, arg = TCG_REG_R3;

    if (!reloc_pc14(lb->label_ptr[0], s->code_ptr)) {
        return false;
    }

    tcg_out_mov(s, TCG_TYPE_PTR, arg++, TCG_AREG0);

    lo = lb->addrlo_reg;
    hi = lb->addrhi_reg;
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        arg |= 1;
#endif
        tcg_out_mov(s, TCG_TYPE_I32, arg++, hi);
        tcg_out_mov(s, TCG_TYPE_I32, arg++, lo);
    } else {
        /* If the address needed to be zero-extended, we'll have already
           placed it in R4.  The only remaining case is 64-bit guest.  */
        tcg_out_mov(s, TCG_TYPE_TL, arg++, lo);
    }

    lo = lb->datalo_reg;
    hi = lb->datahi_reg;
    if (TCG_TARGET_REG_BITS == 32) {
        switch (s_bits) {
        case MO_64:
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
            arg |= 1;
#endif
            tcg_out_mov(s, TCG_TYPE_I32, arg++, hi);
            /* FALLTHRU */
        case MO_32:
            tcg_out_mov(s, TCG_TYPE_I32, arg++, lo);
            break;
        default:
            tcg_out_rlw(s, RLWINM, arg++, lo, 0, 32 - (8 << s_bits), 31);
            break;
        }
    } else {
        if (s_bits == MO_64) {
            tcg_out_mov(s, TCG_TYPE_I64, arg++, lo);
        } else {
            tcg_out_rld(s, RLDICL, arg++, lo, 0, 64 - (8 << s_bits));
        }
    }

    tcg_out_movi(s, TCG_TYPE_I32, arg++, oi);
    tcg_out32(s, MFSPR | RT(arg) | LR);

    tcg_out_call(s, qemu_st_helpers[opc & (MO_BSWAP | MO_SIZE)]);

    tcg_out_b(s, 0, lb->raddr);
    return true;
}
#endif /* SOFTMMU */

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is_64)
{
    TCGReg datalo, datahi, addrlo, rbase;
    TCGReg addrhi __attribute__((unused));
    TCGMemOpIdx oi;
    TCGMemOp opc, s_bits;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    tcg_insn_unit *label_ptr;
#endif

    datalo = *args++;
    datahi = (TCG_TARGET_REG_BITS == 32 && is_64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TCG_TARGET_REG_BITS < TARGET_LONG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);
    s_bits = opc & MO_SIZE;

#ifdef CONFIG_SOFTMMU
    mem_index = get_mmuidx(oi);
    addrlo = tcg_out_tlb_read(s, opc, addrlo, addrhi, mem_index, true);

    /* Load a pointer into the current opcode w/conditional branch-link. */
    label_ptr = s->code_ptr;
    tcg_out32(s, BC | BI(7, CR_EQ) | BO_COND_FALSE | LK);

    rbase = TCG_REG_R3;
#else  /* !CONFIG_SOFTMMU */
    rbase = guest_base ? TCG_GUEST_BASE_REG : 0;
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, TCG_REG_TMP1, addrlo);
        addrlo = TCG_REG_TMP1;
    }
#endif

    if (TCG_TARGET_REG_BITS == 32 && s_bits == MO_64) {
        if (opc & MO_BSWAP) {
            tcg_out32(s, ADDI | TAI(TCG_REG_R0, addrlo, 4));
            tcg_out32(s, LWBRX | TAB(datalo, rbase, addrlo));
            tcg_out32(s, LWBRX | TAB(datahi, rbase, TCG_REG_R0));
        } else if (rbase != 0) {
            tcg_out32(s, ADDI | TAI(TCG_REG_R0, addrlo, 4));
            tcg_out32(s, LWZX | TAB(datahi, rbase, addrlo));
            tcg_out32(s, LWZX | TAB(datalo, rbase, TCG_REG_R0));
        } else if (addrlo == datahi) {
            tcg_out32(s, LWZ | TAI(datalo, addrlo, 4));
            tcg_out32(s, LWZ | TAI(datahi, addrlo, 0));
        } else {
            tcg_out32(s, LWZ | TAI(datahi, addrlo, 0));
            tcg_out32(s, LWZ | TAI(datalo, addrlo, 4));
        }
    } else {
        uint32_t insn = qemu_ldx_opc[opc & (MO_BSWAP | MO_SSIZE)];
        if (!HAVE_ISA_2_06 && insn == LDBRX) {
            tcg_out32(s, ADDI | TAI(TCG_REG_R0, addrlo, 4));
            tcg_out32(s, LWBRX | TAB(datalo, rbase, addrlo));
            tcg_out32(s, LWBRX | TAB(TCG_REG_R0, rbase, TCG_REG_R0));
            tcg_out_rld(s, RLDIMI, datalo, TCG_REG_R0, 32, 0);
        } else if (insn) {
            tcg_out32(s, insn | TAB(datalo, rbase, addrlo));
        } else {
            insn = qemu_ldx_opc[opc & (MO_SIZE | MO_BSWAP)];
            tcg_out32(s, insn | TAB(datalo, rbase, addrlo));
            insn = qemu_exts_opc[s_bits];
            tcg_out32(s, insn | RA(datalo) | RS(datalo));
        }
    }

#ifdef CONFIG_SOFTMMU
    add_qemu_ldst_label(s, true, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#endif
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, bool is_64)
{
    TCGReg datalo, datahi, addrlo, rbase;
    TCGReg addrhi __attribute__((unused));
    TCGMemOpIdx oi;
    TCGMemOp opc, s_bits;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    tcg_insn_unit *label_ptr;
#endif

    datalo = *args++;
    datahi = (TCG_TARGET_REG_BITS == 32 && is_64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TCG_TARGET_REG_BITS < TARGET_LONG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);
    s_bits = opc & MO_SIZE;

#ifdef CONFIG_SOFTMMU
    mem_index = get_mmuidx(oi);
    addrlo = tcg_out_tlb_read(s, opc, addrlo, addrhi, mem_index, false);

    /* Load a pointer into the current opcode w/conditional branch-link. */
    label_ptr = s->code_ptr;
    tcg_out32(s, BC | BI(7, CR_EQ) | BO_COND_FALSE | LK);

    rbase = TCG_REG_R3;
#else  /* !CONFIG_SOFTMMU */
    rbase = guest_base ? TCG_GUEST_BASE_REG : 0;
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, TCG_REG_TMP1, addrlo);
        addrlo = TCG_REG_TMP1;
    }
#endif

    if (TCG_TARGET_REG_BITS == 32 && s_bits == MO_64) {
        if (opc & MO_BSWAP) {
            tcg_out32(s, ADDI | TAI(TCG_REG_R0, addrlo, 4));
            tcg_out32(s, STWBRX | SAB(datalo, rbase, addrlo));
            tcg_out32(s, STWBRX | SAB(datahi, rbase, TCG_REG_R0));
        } else if (rbase != 0) {
            tcg_out32(s, ADDI | TAI(TCG_REG_R0, addrlo, 4));
            tcg_out32(s, STWX | SAB(datahi, rbase, addrlo));
            tcg_out32(s, STWX | SAB(datalo, rbase, TCG_REG_R0));
        } else {
            tcg_out32(s, STW | TAI(datahi, addrlo, 0));
            tcg_out32(s, STW | TAI(datalo, addrlo, 4));
        }
    } else {
        uint32_t insn = qemu_stx_opc[opc & (MO_BSWAP | MO_SIZE)];
        if (!HAVE_ISA_2_06 && insn == STDBRX) {
            tcg_out32(s, STWBRX | SAB(datalo, rbase, addrlo));
            tcg_out32(s, ADDI | TAI(TCG_REG_TMP1, addrlo, 4));
            tcg_out_shri64(s, TCG_REG_R0, datalo, 32);
            tcg_out32(s, STWBRX | SAB(TCG_REG_R0, rbase, TCG_REG_TMP1));
        } else {
            tcg_out32(s, insn | SAB(datalo, rbase, addrlo));
        }
    }

#ifdef CONFIG_SOFTMMU
    add_qemu_ldst_label(s, false, oi, datalo, datahi, addrlo, addrhi,
                        s->code_ptr, label_ptr);
#endif
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        p[i] = NOP;
    }
}

/* Parameters for function call generation, used in tcg.c.  */
#define TCG_TARGET_STACK_ALIGN       16
#define TCG_TARGET_EXTEND_ARGS       1

#ifdef _CALL_AIX
# define LINK_AREA_SIZE                (6 * SZR)
# define LR_OFFSET                     (1 * SZR)
# define TCG_TARGET_CALL_STACK_OFFSET  (LINK_AREA_SIZE + 8 * SZR)
#elif defined(TCG_TARGET_CALL_DARWIN)
# define LINK_AREA_SIZE                (6 * SZR)
# define LR_OFFSET                     (2 * SZR)
#elif TCG_TARGET_REG_BITS == 64
# if defined(_CALL_ELF) && _CALL_ELF == 2
#  define LINK_AREA_SIZE               (4 * SZR)
#  define LR_OFFSET                    (1 * SZR)
# endif
#else /* TCG_TARGET_REG_BITS == 32 */
# if defined(_CALL_SYSV)
#  define LINK_AREA_SIZE               (2 * SZR)
#  define LR_OFFSET                    (1 * SZR)
# endif
#endif
#ifndef LR_OFFSET
# error "Unhandled abi"
#endif
#ifndef TCG_TARGET_CALL_STACK_OFFSET
# define TCG_TARGET_CALL_STACK_OFFSET  LINK_AREA_SIZE
#endif

#define CPU_TEMP_BUF_SIZE  (CPU_TEMP_BUF_NLONGS * (int)sizeof(long))
#define REG_SAVE_SIZE      ((int)ARRAY_SIZE(tcg_target_callee_save_regs) * SZR)

#define FRAME_SIZE ((TCG_TARGET_CALL_STACK_OFFSET   \
                     + TCG_STATIC_CALL_ARGS_SIZE    \
                     + CPU_TEMP_BUF_SIZE            \
                     + REG_SAVE_SIZE                \
                     + TCG_TARGET_STACK_ALIGN - 1)  \
                    & -TCG_TARGET_STACK_ALIGN)

#define REG_SAVE_BOT (FRAME_SIZE - REG_SAVE_SIZE)

static void tcg_target_qemu_prologue(TCGContext *s)
{
    int i;

#ifdef _CALL_AIX
    void **desc = (void **)s->code_ptr;
    desc[0] = desc + 2;                   /* entry point */
    desc[1] = 0;                          /* environment pointer */
    s->code_ptr = (void *)(desc + 2);     /* skip over descriptor */
#endif

    tcg_set_frame(s, TCG_REG_CALL_STACK, REG_SAVE_BOT - CPU_TEMP_BUF_SIZE,
                  CPU_TEMP_BUF_SIZE);

    /* Prologue */
    tcg_out32(s, MFSPR | RT(TCG_REG_R0) | LR);
    tcg_out32(s, (SZR == 8 ? STDU : STWU)
              | SAI(TCG_REG_R1, TCG_REG_R1, -FRAME_SIZE));

    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); ++i) {
        tcg_out_st(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_R1, REG_SAVE_BOT + i * SZR);
    }
    tcg_out_st(s, TCG_TYPE_PTR, TCG_REG_R0, TCG_REG_R1, FRAME_SIZE+LR_OFFSET);

#ifndef CONFIG_SOFTMMU
    if (guest_base) {
        tcg_out_movi_int(s, TCG_TYPE_PTR, TCG_GUEST_BASE_REG, guest_base, true);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out32(s, MTSPR | RS(tcg_target_call_iarg_regs[1]) | CTR);
    if (USE_REG_TB) {
        tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_TB, tcg_target_call_iarg_regs[1]);
    }
    tcg_out32(s, BCCTR | BO_ALWAYS);

    /* Epilogue */
    s->code_gen_epilogue = tb_ret_addr = s->code_ptr;

    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R0, TCG_REG_R1, FRAME_SIZE+LR_OFFSET);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); ++i) {
        tcg_out_ld(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_R1, REG_SAVE_BOT + i * SZR);
    }
    tcg_out32(s, MTSPR | RS(TCG_REG_R0) | LR);
    tcg_out32(s, ADDI | TAI(TCG_REG_R1, TCG_REG_R1, FRAME_SIZE));
    tcg_out32(s, BCLR | BO_ALWAYS);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                       const int *const_args)
{
    TCGArg a0, a1, a2;
    int c;

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R3, args[0]);
        tcg_out_b(s, 0, tb_ret_addr);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_insn_offset) {
            /* Direct jump. */
            if (TCG_TARGET_REG_BITS == 64) {
                /* Ensure the next insns are 8-byte aligned. */
                if ((uintptr_t)s->code_ptr & 7) {
                    tcg_out32(s, NOP);
                }
                s->tb_jmp_insn_offset[args[0]] = tcg_current_code_size(s);
                tcg_out32(s, ADDIS | TAI(TCG_REG_TB, TCG_REG_TB, 0));
                tcg_out32(s, ADDI | TAI(TCG_REG_TB, TCG_REG_TB, 0));
            } else {
                s->tb_jmp_insn_offset[args[0]] = tcg_current_code_size(s);
                tcg_out32(s, B);
                s->tb_jmp_reset_offset[args[0]] = tcg_current_code_size(s);
                break;
            }
        } else {
            /* Indirect jump. */
            tcg_debug_assert(s->tb_jmp_insn_offset == NULL);
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TB, 0,
                       (intptr_t)(s->tb_jmp_insn_offset + args[0]));
        }
        tcg_out32(s, MTSPR | RS(TCG_REG_TB) | CTR);
        tcg_out32(s, BCCTR | BO_ALWAYS);
        set_jmp_reset_offset(s, args[0]);
        if (USE_REG_TB) {
            /* For the unlinked case, need to reset TCG_REG_TB.  */
            c = -tcg_current_code_size(s);
            assert(c == (int16_t)c);
            tcg_out32(s, ADDI | TAI(TCG_REG_TB, TCG_REG_TB, c));
        }
        break;
    case INDEX_op_goto_ptr:
        tcg_out32(s, MTSPR | RS(args[0]) | CTR);
        if (USE_REG_TB) {
            tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_TB, args[0]);
        }
        tcg_out32(s, ADDI | TAI(TCG_REG_R3, 0, 0));
        tcg_out32(s, BCCTR | BO_ALWAYS);
        break;
    case INDEX_op_br:
        {
            TCGLabel *l = arg_label(args[0]);
            uint32_t insn = B;

            if (l->has_value) {
                insn |= reloc_pc24_val(s->code_ptr, l->u.value_ptr);
            } else {
                tcg_out_reloc(s, s->code_ptr, R_PPC_REL24, l, 0);
            }
            tcg_out32(s, insn);
        }
        break;
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
        tcg_out_mem_long(s, LBZ, LBZX, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
        tcg_out_mem_long(s, LBZ, LBZX, args[0], args[1], args[2]);
        tcg_out32(s, EXTSB | RS(args[0]) | RA(args[0]));
        break;
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
        tcg_out_mem_long(s, LHZ, LHZX, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
        tcg_out_mem_long(s, LHA, LHAX, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i32:
    case INDEX_op_ld32u_i64:
        tcg_out_mem_long(s, LWZ, LWZX, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld32s_i64:
        tcg_out_mem_long(s, LWA, LWAX, args[0], args[1], args[2]);
        break;
    case INDEX_op_ld_i64:
        tcg_out_mem_long(s, LD, LDX, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
        tcg_out_mem_long(s, STB, STBX, args[0], args[1], args[2]);
        break;
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
        tcg_out_mem_long(s, STH, STHX, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        tcg_out_mem_long(s, STW, STWX, args[0], args[1], args[2]);
        break;
    case INDEX_op_st_i64:
        tcg_out_mem_long(s, STD, STDX, args[0], args[1], args[2]);
        break;

    case INDEX_op_add_i32:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
        do_addi_32:
            tcg_out_mem_long(s, ADDI, ADD, a0, a1, (int32_t)a2);
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

    case INDEX_op_clz_i32:
        tcg_out_cntxz(s, TCG_TYPE_I32, CNTLZW, args[0], args[1],
                      args[2], const_args[2]);
        break;
    case INDEX_op_ctz_i32:
        tcg_out_cntxz(s, TCG_TYPE_I32, CNTTZW, args[0], args[1],
                      args[2], const_args[2]);
        break;
    case INDEX_op_ctpop_i32:
        tcg_out32(s, CNTPOPW | SAB(args[1], args[0], 0));
        break;

    case INDEX_op_clz_i64:
        tcg_out_cntxz(s, TCG_TYPE_I64, CNTLZD, args[0], args[1],
                      args[2], const_args[2]);
        break;
    case INDEX_op_ctz_i64:
        tcg_out_cntxz(s, TCG_TYPE_I64, CNTTZD, args[0], args[1],
                      args[2], const_args[2]);
        break;
    case INDEX_op_ctpop_i64:
        tcg_out32(s, CNTPOPD | SAB(args[1], args[0], 0));
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
        tcg_out32(s, DIVW | TAB(args[0], args[1], args[2]));
        break;

    case INDEX_op_divu_i32:
        tcg_out32(s, DIVWU | TAB(args[0], args[1], args[2]));
        break;

    case INDEX_op_shl_i32:
        if (const_args[2]) {
            tcg_out_shli32(s, args[0], args[1], args[2]);
        } else {
            tcg_out32(s, SLW | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_shr_i32:
        if (const_args[2]) {
            tcg_out_shri32(s, args[0], args[1], args[2]);
        } else {
            tcg_out32(s, SRW | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_sar_i32:
        if (const_args[2]) {
            tcg_out32(s, SRAWI | RS(args[1]) | RA(args[0]) | SH(args[2]));
        } else {
            tcg_out32(s, SRAW | SAB(args[1], args[0], args[2]));
        }
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
            tcg_out32(s, SUBFIC | TAI(TCG_REG_R0, args[2], 32));
            tcg_out32(s, RLWNM | SAB(args[1], args[0], TCG_REG_R0)
                         | MB(0) | ME(31));
        }
        break;

    case INDEX_op_brcond_i32:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1],
                       arg_label(args[3]), TCG_TYPE_I32);
        break;
    case INDEX_op_brcond_i64:
        tcg_out_brcond(s, args[2], args[0], args[1], const_args[1],
                       arg_label(args[3]), TCG_TYPE_I64);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args, const_args);
        break;

    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
        tcg_out32(s, NEG | RT(args[0]) | RA(args[1]));
        break;

    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
        tcg_out32(s, NOR | SAB(args[1], args[0], args[1]));
        break;

    case INDEX_op_add_i64:
        a0 = args[0], a1 = args[1], a2 = args[2];
        if (const_args[2]) {
        do_addi_64:
            tcg_out_mem_long(s, ADDI, ADD, a0, a1, a2);
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
        if (const_args[2]) {
            tcg_out_shli64(s, args[0], args[1], args[2]);
        } else {
            tcg_out32(s, SLD | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_shr_i64:
        if (const_args[2]) {
            tcg_out_shri64(s, args[0], args[1], args[2]);
        } else {
            tcg_out32(s, SRD | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_sar_i64:
        if (const_args[2]) {
            int sh = SH(args[2] & 0x1f) | (((args[2] >> 5) & 1) << 1);
            tcg_out32(s, SRADI | RA(args[0]) | RS(args[1]) | sh);
        } else {
            tcg_out32(s, SRAD | SAB(args[1], args[0], args[2]));
        }
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
            tcg_out32(s, SUBFIC | TAI(TCG_REG_R0, args[2], 64));
            tcg_out32(s, RLDCL | SAB(args[1], args[0], TCG_REG_R0) | MB64(0));
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
        tcg_out32(s, DIVD | TAB(args[0], args[1], args[2]));
        break;
    case INDEX_op_divu_i64:
        tcg_out32(s, DIVDU | TAB(args[0], args[1], args[2]));
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, args, false);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, args, true);
        break;
    case INDEX_op_qemu_st_i32:
        tcg_out_qemu_st(s, args, false);
        break;
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, args, true);
        break;

    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
        c = EXTSB;
        goto gen_ext;
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
        c = EXTSH;
        goto gen_ext;
    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        c = EXTSW;
        goto gen_ext;
    gen_ext:
        tcg_out32(s, c | RS(args[1]) | RA(args[0]));
        break;
    case INDEX_op_extu_i32_i64:
        tcg_out_ext32u(s, args[0], args[1]);
        break;

    case INDEX_op_setcond_i32:
        tcg_out_setcond(s, TCG_TYPE_I32, args[3], args[0], args[1], args[2],
                        const_args[2]);
        break;
    case INDEX_op_setcond_i64:
        tcg_out_setcond(s, TCG_TYPE_I64, args[3], args[0], args[1], args[2],
                        const_args[2]);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2(s, args, const_args);
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
            tcg_out_mov(s, TCG_TYPE_REG, args[0], a0);
        }
        break;

    case INDEX_op_bswap64_i64:
        a0 = args[0], a1 = args[1], a2 = TCG_REG_R0;
        if (a0 == a1) {
            a0 = TCG_REG_R0;
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
            tcg_out_mov(s, TCG_TYPE_REG, args[0], a0);
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

    case INDEX_op_extract_i32:
        tcg_out_rlw(s, RLWINM, args[0], args[1],
                    32 - args[2], 32 - args[3], 31);
        break;
    case INDEX_op_extract_i64:
        tcg_out_rld(s, RLDICL, args[0], args[1], 64 - args[2], 64 - args[3]);
        break;

    case INDEX_op_movcond_i32:
        tcg_out_movcond(s, TCG_TYPE_I32, args[5], args[0], args[1], args[2],
                        args[3], args[4], const_args[2]);
        break;
    case INDEX_op_movcond_i64:
        tcg_out_movcond(s, TCG_TYPE_I64, args[5], args[0], args[1], args[2],
                        args[3], args[4], const_args[2]);
        break;

#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_add2_i64:
#else
    case INDEX_op_add2_i32:
#endif
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
            tcg_out_mov(s, TCG_TYPE_REG, args[0], a0);
        }
        break;

#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_sub2_i64:
#else
    case INDEX_op_sub2_i32:
#endif
        a0 = args[0], a1 = args[1];
        if (a0 == args[5] || (!const_args[3] && a0 == args[3])) {
            a0 = TCG_REG_R0;
        }
        if (const_args[2]) {
            tcg_out32(s, SUBFIC | TAI(a0, args[4], args[2]));
        } else {
            tcg_out32(s, SUBFC | TAB(a0, args[4], args[2]));
        }
        if (const_args[3]) {
            tcg_out32(s, (args[3] ? SUBFME : SUBFZE) | RT(a1) | RA(args[5]));
        } else {
            tcg_out32(s, SUBFE | TAB(a1, args[5], args[3]));
        }
        if (a0 != args[0]) {
            tcg_out_mov(s, TCG_TYPE_REG, args[0], a0);
        }
        break;

    case INDEX_op_muluh_i32:
        tcg_out32(s, MULHWU | TAB(args[0], args[1], args[2]));
        break;
    case INDEX_op_mulsh_i32:
        tcg_out32(s, MULHW | TAB(args[0], args[1], args[2]));
        break;
    case INDEX_op_muluh_i64:
        tcg_out32(s, MULHDU | TAB(args[0], args[1], args[2]));
        break;
    case INDEX_op_mulsh_i64:
        tcg_out32(s, MULHD | TAB(args[0], args[1], args[2]));
        break;

    case INDEX_op_mb:
        tcg_out_mb(s, args[0]);
        break;

    case INDEX_op_mov_i32:   /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_movi_i32:  /* Always emitted via tcg_out_movi.  */
    case INDEX_op_movi_i64:
    case INDEX_op_call:      /* Always emitted via tcg_out_call.  */
    default:
        tcg_abort();
    }
}

static const TCGTargetOpDef *tcg_target_op_def(TCGOpcode op)
{
    static const TCGTargetOpDef r = { .args_ct_str = { "r" } };
    static const TCGTargetOpDef r_r = { .args_ct_str = { "r", "r" } };
    static const TCGTargetOpDef r_L = { .args_ct_str = { "r", "L" } };
    static const TCGTargetOpDef S_S = { .args_ct_str = { "S", "S" } };
    static const TCGTargetOpDef r_ri = { .args_ct_str = { "r", "ri" } };
    static const TCGTargetOpDef r_r_r = { .args_ct_str = { "r", "r", "r" } };
    static const TCGTargetOpDef r_L_L = { .args_ct_str = { "r", "L", "L" } };
    static const TCGTargetOpDef L_L_L = { .args_ct_str = { "L", "L", "L" } };
    static const TCGTargetOpDef S_S_S = { .args_ct_str = { "S", "S", "S" } };
    static const TCGTargetOpDef r_r_ri = { .args_ct_str = { "r", "r", "ri" } };
    static const TCGTargetOpDef r_r_rI = { .args_ct_str = { "r", "r", "rI" } };
    static const TCGTargetOpDef r_r_rT = { .args_ct_str = { "r", "r", "rT" } };
    static const TCGTargetOpDef r_r_rU = { .args_ct_str = { "r", "r", "rU" } };
    static const TCGTargetOpDef r_rI_ri
        = { .args_ct_str = { "r", "rI", "ri" } };
    static const TCGTargetOpDef r_rI_rT
        = { .args_ct_str = { "r", "rI", "rT" } };
    static const TCGTargetOpDef r_r_rZW
        = { .args_ct_str = { "r", "r", "rZW" } };
    static const TCGTargetOpDef L_L_L_L
        = { .args_ct_str = { "L", "L", "L", "L" } };
    static const TCGTargetOpDef S_S_S_S
        = { .args_ct_str = { "S", "S", "S", "S" } };
    static const TCGTargetOpDef movc
        = { .args_ct_str = { "r", "r", "ri", "rZ", "rZ" } };
    static const TCGTargetOpDef dep
        = { .args_ct_str = { "r", "0", "rZ" } };
    static const TCGTargetOpDef br2
        = { .args_ct_str = { "r", "r", "ri", "ri" } };
    static const TCGTargetOpDef setc2
        = { .args_ct_str = { "r", "r", "r", "ri", "ri" } };
    static const TCGTargetOpDef add2
        = { .args_ct_str = { "r", "r", "r", "r", "rI", "rZM" } };
    static const TCGTargetOpDef sub2
        = { .args_ct_str = { "r", "r", "rI", "rZM", "r", "r" } };

    switch (op) {
    case INDEX_op_goto_ptr:
        return &r;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_ctpop_i32:
    case INDEX_op_neg_i32:
    case INDEX_op_not_i32:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext16s_i32:
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap32_i32:
    case INDEX_op_extract_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
    case INDEX_op_ctpop_i64:
    case INDEX_op_neg_i64:
    case INDEX_op_not_i64:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_extract_i64:
        return &r_r;

    case INDEX_op_add_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_andc_i32:
    case INDEX_op_orc_i32:
    case INDEX_op_eqv_i32:
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_rotl_i32:
    case INDEX_op_rotr_i32:
    case INDEX_op_setcond_i32:
    case INDEX_op_and_i64:
    case INDEX_op_andc_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i64:
    case INDEX_op_setcond_i64:
        return &r_r_ri;
    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
        return &r_r_rI;
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_nand_i32:
    case INDEX_op_nor_i32:
    case INDEX_op_muluh_i32:
    case INDEX_op_mulsh_i32:
    case INDEX_op_orc_i64:
    case INDEX_op_eqv_i64:
    case INDEX_op_nand_i64:
    case INDEX_op_nor_i64:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
    case INDEX_op_mulsh_i64:
    case INDEX_op_muluh_i64:
        return &r_r_r;
    case INDEX_op_sub_i32:
        return &r_rI_ri;
    case INDEX_op_add_i64:
        return &r_r_rT;
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
        return &r_r_rU;
    case INDEX_op_sub_i64:
        return &r_rI_rT;
    case INDEX_op_clz_i32:
    case INDEX_op_ctz_i32:
    case INDEX_op_clz_i64:
    case INDEX_op_ctz_i64:
        return &r_r_rZW;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return &r_ri;

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        return &movc;
    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        return &dep;
    case INDEX_op_brcond2_i32:
        return &br2;
    case INDEX_op_setcond2_i32:
        return &setc2;
    case INDEX_op_add2_i64:
    case INDEX_op_add2_i32:
        return &add2;
    case INDEX_op_sub2_i64:
    case INDEX_op_sub2_i32:
        return &sub2;

    case INDEX_op_qemu_ld_i32:
        return (TCG_TARGET_REG_BITS == 64 || TARGET_LONG_BITS == 32
                ? &r_L : &r_L_L);
    case INDEX_op_qemu_st_i32:
        return (TCG_TARGET_REG_BITS == 64 || TARGET_LONG_BITS == 32
                ? &S_S : &S_S_S);
    case INDEX_op_qemu_ld_i64:
        return (TCG_TARGET_REG_BITS == 64 ? &r_L
                : TARGET_LONG_BITS == 32 ? &L_L_L : &L_L_L_L);
    case INDEX_op_qemu_st_i64:
        return (TCG_TARGET_REG_BITS == 64 ? &S_S
                : TARGET_LONG_BITS == 32 ? &S_S_S : &S_S_S_S);

    default:
        return NULL;
    }
}

static void tcg_target_init(TCGContext *s)
{
    unsigned long hwcap = qemu_getauxval(AT_HWCAP);
    unsigned long hwcap2 = qemu_getauxval(AT_HWCAP2);

    if (hwcap & PPC_FEATURE_ARCH_2_06) {
        have_isa_2_06 = true;
    }
#ifdef PPC_FEATURE2_ARCH_3_00
    if (hwcap2 & PPC_FEATURE2_ARCH_3_00) {
        have_isa_3_00 = true;
    }
#endif

    tcg_target_available_regs[TCG_TYPE_I32] = 0xffffffff;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffffffff;

    tcg_target_call_clobber_regs = 0;
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R5);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R6);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R7);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R8);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R9);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R10);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R11);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_R12);

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0); /* tcg temp */
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R1); /* stack pointer */
#if defined(_CALL_SYSV)
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R2); /* toc pointer */
#endif
#if defined(_CALL_SYSV) || TCG_TARGET_REG_BITS == 64
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R13); /* thread pointer */
#endif
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_TMP1); /* mem temp */
    if (USE_REG_TB) {
        tcg_regset_set_reg(s->reserved_regs, TCG_REG_TB);  /* tb->tc_ptr */
    }
}

#ifdef __ELF__
typedef struct {
    DebugFrameCIE cie;
    DebugFrameFDEHeader fde;
    uint8_t fde_def_cfa[4];
    uint8_t fde_reg_ofs[ARRAY_SIZE(tcg_target_callee_save_regs) * 2 + 3];
} DebugFrame;

/* We're expecting a 2 byte uleb128 encoded value.  */
QEMU_BUILD_BUG_ON(FRAME_SIZE >= (1 << 14));

#if TCG_TARGET_REG_BITS == 64
# define ELF_HOST_MACHINE EM_PPC64
#else
# define ELF_HOST_MACHINE EM_PPC
#endif

static DebugFrame debug_frame = {
    .cie.len = sizeof(DebugFrameCIE)-4, /* length after .len member */
    .cie.id = -1,
    .cie.version = 1,
    .cie.code_align = 1,
    .cie.data_align = (-SZR & 0x7f),         /* sleb128 -SZR */
    .cie.return_column = 65,

    /* Total FDE size does not include the "len" member.  */
    .fde.len = sizeof(DebugFrame) - offsetof(DebugFrame, fde.cie_offset),

    .fde_def_cfa = {
        12, TCG_REG_R1,                 /* DW_CFA_def_cfa r1, ... */
        (FRAME_SIZE & 0x7f) | 0x80,     /* ... uleb128 FRAME_SIZE */
        (FRAME_SIZE >> 7)
    },
    .fde_reg_ofs = {
        /* DW_CFA_offset_extended_sf, lr, LR_OFFSET */
        0x11, 65, (LR_OFFSET / -SZR) & 0x7f,
    }
};

void tcg_register_jit(void *buf, size_t buf_size)
{
    uint8_t *p = &debug_frame.fde_reg_ofs[3];
    int i;

    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); ++i, p += 2) {
        p[0] = 0x80 + tcg_target_callee_save_regs[i];
        p[1] = (FRAME_SIZE - (REG_SAVE_BOT + i * SZR)) / SZR;
    }

    debug_frame.fde.func_start = (uintptr_t)buf;
    debug_frame.fde.func_len = buf_size;

    tcg_register_jit_int(buf, buf_size, &debug_frame, sizeof(debug_frame));
}
#endif /* __ELF__ */

void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    uintptr_t p, start1, stop1;
    size_t dsize = qemu_dcache_linesize;
    size_t isize = qemu_icache_linesize;

    start1 = start & ~(dsize - 1);
    stop1 = (stop + dsize - 1) & ~(dsize - 1);
    for (p = start1; p < stop1; p += dsize) {
        asm volatile ("dcbst 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");

    start &= start & ~(isize - 1);
    stop1 = (stop + isize - 1) & ~(isize - 1);
    for (p = start1; p < stop1; p += isize) {
        asm volatile ("icbi 0,%0" : : "r"(p) : "memory");
    }
    asm volatile ("sync" : : : "memory");
    asm volatile ("isync" : : : "memory");
}
