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
#include "../tcg-pool.c.inc"
#include "../tcg-ldst.c.inc"

/*
 * Standardize on the _CALL_FOO symbols used by GCC:
 * Apple XCode does not define _CALL_DARWIN.
 * Clang defines _CALL_ELF (64-bit) but not _CALL_SYSV (32-bit).
 */
#if !defined(_CALL_SYSV) && \
    !defined(_CALL_DARWIN) && \
    !defined(_CALL_AIX) && \
    !defined(_CALL_ELF)
# if defined(__APPLE__)
#  define _CALL_DARWIN
# elif defined(__ELF__) && TCG_TARGET_REG_BITS == 32
#  define _CALL_SYSV
# else
#  error "Unknown ABI"
# endif
#endif

#if TCG_TARGET_REG_BITS == 64
# define TCG_TARGET_CALL_ARG_I32   TCG_CALL_ARG_EXTEND
#else
# define TCG_TARGET_CALL_ARG_I32   TCG_CALL_ARG_NORMAL
#endif
#ifdef _CALL_SYSV
# define TCG_TARGET_CALL_ARG_I64   TCG_CALL_ARG_EVEN
#else
# define TCG_TARGET_CALL_ARG_I64   TCG_CALL_ARG_NORMAL
#endif
/* Note sysv arg alignment applies only to 2-word types, not more. */
#define TCG_TARGET_CALL_ARG_I128   TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_RET_I128   TCG_CALL_RET_NORMAL

/* For some memory operations, we need a scratch that isn't R0.  For the AIX
   calling convention, we can re-use the TOC register since we'll be reloading
   it at every call.  Otherwise R12 will do nicely as neither a call-saved
   register nor a parameter register.  */
#ifdef _CALL_AIX
# define TCG_REG_TMP1   TCG_REG_R2
#else
# define TCG_REG_TMP1   TCG_REG_R12
#endif

#define TCG_VEC_TMP1    TCG_REG_V0
#define TCG_VEC_TMP2    TCG_REG_V1

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

#define ALL_GENERAL_REGS  0xffffffffu
#define ALL_VECTOR_REGS   0xffffffff00000000ull

#ifdef CONFIG_SOFTMMU
#define ALL_QLOAD_REGS \
    (ALL_GENERAL_REGS & \
     ~((1 << TCG_REG_R3) | (1 << TCG_REG_R4) | (1 << TCG_REG_R5)))
#define ALL_QSTORE_REGS \
    (ALL_GENERAL_REGS & ~((1 << TCG_REG_R3) | (1 << TCG_REG_R4) | \
                          (1 << TCG_REG_R5) | (1 << TCG_REG_R6)))
#else
#define ALL_QLOAD_REGS  (ALL_GENERAL_REGS & ~(1 << TCG_REG_R3))
#define ALL_QSTORE_REGS ALL_QLOAD_REGS
#endif

TCGPowerISA have_isa;
static bool have_isel;
bool have_altivec;
bool have_vsx;

#ifndef CONFIG_SOFTMMU
#define TCG_GUEST_BASE_REG 30
#endif

#ifdef CONFIG_DEBUG_TCG
static const char tcg_target_reg_names[TCG_TARGET_NB_REGS][4] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
    "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
    "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
    "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
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

    /* V0 and V1 reserved as temporaries; V20 - V31 are call-saved */
    TCG_REG_V2,   /* call clobbered, vectors */
    TCG_REG_V3,
    TCG_REG_V4,
    TCG_REG_V5,
    TCG_REG_V6,
    TCG_REG_V7,
    TCG_REG_V8,
    TCG_REG_V9,
    TCG_REG_V10,
    TCG_REG_V11,
    TCG_REG_V12,
    TCG_REG_V13,
    TCG_REG_V14,
    TCG_REG_V15,
    TCG_REG_V16,
    TCG_REG_V17,
    TCG_REG_V18,
    TCG_REG_V19,
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

static TCGReg tcg_target_call_oarg_reg(TCGCallReturnKind kind, int slot)
{
    tcg_debug_assert(kind == TCG_CALL_RET_NORMAL);
    tcg_debug_assert(slot >= 0 && slot <= 1);
    return TCG_REG_R3 + slot;
}

static const int tcg_target_callee_save_regs[] = {
#ifdef _CALL_DARWIN
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

static uint32_t reloc_pc24_val(const tcg_insn_unit *pc,
			       const tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    tcg_debug_assert(in_range_b(disp));
    return disp & 0x3fffffc;
}

static bool reloc_pc24(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t disp = tcg_ptr_byte_diff(target, src_rx);

    if (in_range_b(disp)) {
        *src_rw = (*src_rw & ~0x3fffffc) | (disp & 0x3fffffc);
        return true;
    }
    return false;
}

static uint16_t reloc_pc14_val(const tcg_insn_unit *pc,
			       const tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    tcg_debug_assert(disp == (int16_t) disp);
    return disp & 0xfffc;
}

static bool reloc_pc14(tcg_insn_unit *src_rw, const tcg_insn_unit *target)
{
    const tcg_insn_unit *src_rx = tcg_splitwx_to_rx(src_rw);
    ptrdiff_t disp = tcg_ptr_byte_diff(target, src_rx);

    if (disp == (int16_t) disp) {
        *src_rw = (*src_rw & ~0xfffc) | (disp & 0xfffc);
        return true;
    }
    return false;
}

/* test if a constant matches the constraint */
static bool tcg_target_const_match(int64_t val, TCGType type, int ct)
{
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
#define VX4(opc)  (OPCD(4)|(opc))

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
#define MODSW  XO31(779)
#define MODUW  XO31(267)
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
#define MODSD  XO31(777)
#define MODUD  XO31(265)

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

#define BRH    XO31(219)
#define BRW    XO31(155)
#define BRD    XO31(187)

#define TW     XO31( 4)
#define TRAP   (TW | TO(31))

#define NOP    ORI  /* ori 0,0,0 */

#define LVX        XO31(103)
#define LVEBX      XO31(7)
#define LVEHX      XO31(39)
#define LVEWX      XO31(71)
#define LXSDX      (XO31(588) | 1)  /* v2.06, force tx=1 */
#define LXVDSX     (XO31(332) | 1)  /* v2.06, force tx=1 */
#define LXSIWZX    (XO31(12) | 1)   /* v2.07, force tx=1 */
#define LXV        (OPCD(61) | 8 | 1)  /* v3.00, force tx=1 */
#define LXSD       (OPCD(57) | 2)   /* v3.00 */
#define LXVWSX     (XO31(364) | 1)  /* v3.00, force tx=1 */

#define STVX       XO31(231)
#define STVEWX     XO31(199)
#define STXSDX     (XO31(716) | 1)  /* v2.06, force sx=1 */
#define STXSIWX    (XO31(140) | 1)  /* v2.07, force sx=1 */
#define STXV       (OPCD(61) | 8 | 5) /* v3.00, force sx=1 */
#define STXSD      (OPCD(61) | 2)   /* v3.00 */

#define VADDSBS    VX4(768)
#define VADDUBS    VX4(512)
#define VADDUBM    VX4(0)
#define VADDSHS    VX4(832)
#define VADDUHS    VX4(576)
#define VADDUHM    VX4(64)
#define VADDSWS    VX4(896)
#define VADDUWS    VX4(640)
#define VADDUWM    VX4(128)
#define VADDUDM    VX4(192)       /* v2.07 */

#define VSUBSBS    VX4(1792)
#define VSUBUBS    VX4(1536)
#define VSUBUBM    VX4(1024)
#define VSUBSHS    VX4(1856)
#define VSUBUHS    VX4(1600)
#define VSUBUHM    VX4(1088)
#define VSUBSWS    VX4(1920)
#define VSUBUWS    VX4(1664)
#define VSUBUWM    VX4(1152)
#define VSUBUDM    VX4(1216)      /* v2.07 */

#define VNEGW      (VX4(1538) | (6 << 16))  /* v3.00 */
#define VNEGD      (VX4(1538) | (7 << 16))  /* v3.00 */

#define VMAXSB     VX4(258)
#define VMAXSH     VX4(322)
#define VMAXSW     VX4(386)
#define VMAXSD     VX4(450)       /* v2.07 */
#define VMAXUB     VX4(2)
#define VMAXUH     VX4(66)
#define VMAXUW     VX4(130)
#define VMAXUD     VX4(194)       /* v2.07 */
#define VMINSB     VX4(770)
#define VMINSH     VX4(834)
#define VMINSW     VX4(898)
#define VMINSD     VX4(962)       /* v2.07 */
#define VMINUB     VX4(514)
#define VMINUH     VX4(578)
#define VMINUW     VX4(642)
#define VMINUD     VX4(706)       /* v2.07 */

#define VCMPEQUB   VX4(6)
#define VCMPEQUH   VX4(70)
#define VCMPEQUW   VX4(134)
#define VCMPEQUD   VX4(199)       /* v2.07 */
#define VCMPGTSB   VX4(774)
#define VCMPGTSH   VX4(838)
#define VCMPGTSW   VX4(902)
#define VCMPGTSD   VX4(967)       /* v2.07 */
#define VCMPGTUB   VX4(518)
#define VCMPGTUH   VX4(582)
#define VCMPGTUW   VX4(646)
#define VCMPGTUD   VX4(711)       /* v2.07 */
#define VCMPNEB    VX4(7)         /* v3.00 */
#define VCMPNEH    VX4(71)        /* v3.00 */
#define VCMPNEW    VX4(135)       /* v3.00 */

#define VSLB       VX4(260)
#define VSLH       VX4(324)
#define VSLW       VX4(388)
#define VSLD       VX4(1476)      /* v2.07 */
#define VSRB       VX4(516)
#define VSRH       VX4(580)
#define VSRW       VX4(644)
#define VSRD       VX4(1732)      /* v2.07 */
#define VSRAB      VX4(772)
#define VSRAH      VX4(836)
#define VSRAW      VX4(900)
#define VSRAD      VX4(964)       /* v2.07 */
#define VRLB       VX4(4)
#define VRLH       VX4(68)
#define VRLW       VX4(132)
#define VRLD       VX4(196)       /* v2.07 */

#define VMULEUB    VX4(520)
#define VMULEUH    VX4(584)
#define VMULEUW    VX4(648)       /* v2.07 */
#define VMULOUB    VX4(8)
#define VMULOUH    VX4(72)
#define VMULOUW    VX4(136)       /* v2.07 */
#define VMULUWM    VX4(137)       /* v2.07 */
#define VMULLD     VX4(457)       /* v3.10 */
#define VMSUMUHM   VX4(38)

#define VMRGHB     VX4(12)
#define VMRGHH     VX4(76)
#define VMRGHW     VX4(140)
#define VMRGLB     VX4(268)
#define VMRGLH     VX4(332)
#define VMRGLW     VX4(396)

#define VPKUHUM    VX4(14)
#define VPKUWUM    VX4(78)

#define VAND       VX4(1028)
#define VANDC      VX4(1092)
#define VNOR       VX4(1284)
#define VOR        VX4(1156)
#define VXOR       VX4(1220)
#define VEQV       VX4(1668)      /* v2.07 */
#define VNAND      VX4(1412)      /* v2.07 */
#define VORC       VX4(1348)      /* v2.07 */

#define VSPLTB     VX4(524)
#define VSPLTH     VX4(588)
#define VSPLTW     VX4(652)
#define VSPLTISB   VX4(780)
#define VSPLTISH   VX4(844)
#define VSPLTISW   VX4(908)

#define VSLDOI     VX4(44)

#define XXPERMDI   (OPCD(60) | (10 << 3) | 7)  /* v2.06, force ax=bx=tx=1 */
#define XXSEL      (OPCD(60) | (3 << 4) | 0xf) /* v2.06, force ax=bx=cx=tx=1 */
#define XXSPLTIB   (OPCD(60) | (360 << 1) | 1) /* v3.00, force tx=1 */

#define MFVSRD     (XO31(51) | 1)   /* v2.07, force sx=1 */
#define MFVSRWZ    (XO31(115) | 1)  /* v2.07, force sx=1 */
#define MTVSRD     (XO31(179) | 1)  /* v2.07, force tx=1 */
#define MTVSRWZ    (XO31(243) | 1)  /* v2.07, force tx=1 */
#define MTVSRDD    (XO31(435) | 1)  /* v3.00, force tx=1 */
#define MTVSRWS    (XO31(403) | 1)  /* v3.00, force tx=1 */

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

#define VRT(r)  (((r) & 31) << 21)
#define VRA(r)  (((r) & 31) << 16)
#define VRB(r)  (((r) & 31) << 11)
#define VRC(r)  (((r) & 31) <<  6)

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
    const tcg_insn_unit *target;
    int16_t lo;
    int32_t hi;

    value += addend;
    target = (const tcg_insn_unit *)value;

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
    case R_PPC_ADDR32:
        /*
         * We are abusing this relocation type.  Again, this points to
         * a pair of insns, lis + load.  This is an absolute address
         * relocation for PPC32 so the lis cannot be removed.
         */
        lo = value;
        hi = value - lo;
        if (hi + lo != value) {
            return false;
        }
        code_ptr[0] = deposit32(code_ptr[0], 0, 16, hi >> 16);
        code_ptr[1] = deposit32(code_ptr[1], 0, 16, lo);
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
    if (ret == arg) {
        return true;
    }
    switch (type) {
    case TCG_TYPE_I64:
        tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
        /* fallthru */
    case TCG_TYPE_I32:
        if (ret < TCG_REG_V0) {
            if (arg < TCG_REG_V0) {
                tcg_out32(s, OR | SAB(arg, ret, arg));
                break;
            } else if (have_isa_2_07) {
                tcg_out32(s, (type == TCG_TYPE_I32 ? MFVSRWZ : MFVSRD)
                          | VRT(arg) | RA(ret));
                break;
            } else {
                /* Altivec does not support vector->integer moves.  */
                return false;
            }
        } else if (arg < TCG_REG_V0) {
            if (have_isa_2_07) {
                tcg_out32(s, (type == TCG_TYPE_I32 ? MTVSRWZ : MTVSRD)
                          | VRT(ret) | RA(arg));
                break;
            } else {
                /* Altivec does not support integer->vector moves.  */
                return false;
            }
        }
        /* fallthru */
    case TCG_TYPE_V64:
    case TCG_TYPE_V128:
        tcg_debug_assert(ret >= TCG_REG_V0 && arg >= TCG_REG_V0);
        tcg_out32(s, VOR | VRT(ret) | VRA(arg) | VRB(arg));
        break;
    default:
        g_assert_not_reached();
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

static inline void tcg_out_ext8s(TCGContext *s, TCGReg dst, TCGReg src)
{
    tcg_out32(s, EXTSB | RA(dst) | RS(src));
}

static inline void tcg_out_ext16s(TCGContext *s, TCGReg dst, TCGReg src)
{
    tcg_out32(s, EXTSH | RA(dst) | RS(src));
}

static inline void tcg_out_ext16u(TCGContext *s, TCGReg dst, TCGReg src)
{
    tcg_out32(s, ANDI | SAI(src, dst, 0xffff));
}

static inline void tcg_out_ext32s(TCGContext *s, TCGReg dst, TCGReg src)
{
    tcg_out32(s, EXTSW | RA(dst) | RS(src));
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

static inline void tcg_out_sari32(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    /* Limit immediate shift count lest we create an illegal insn.  */
    tcg_out32(s, SRAWI | RA(dst) | RS(src) | SH(c & 31));
}

static inline void tcg_out_shri32(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rlw(s, RLWINM, dst, src, 32 - c, c, 31);
}

static inline void tcg_out_shri64(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out_rld(s, RLDICL, dst, src, 64 - c, c);
}

static inline void tcg_out_sari64(TCGContext *s, TCGReg dst, TCGReg src, int c)
{
    tcg_out32(s, SRADI | RA(dst) | RS(src) | SH(c & 0x1f) | ((c >> 4) & 2));
}

static void tcg_out_bswap16(TCGContext *s, TCGReg dst, TCGReg src, int flags)
{
    TCGReg tmp = dst == src ? TCG_REG_R0 : dst;

    if (have_isa_3_10) {
        tcg_out32(s, BRH | RA(dst) | RS(src));
        if (flags & TCG_BSWAP_OS) {
            tcg_out_ext16s(s, dst, dst);
        } else if ((flags & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
            tcg_out_ext16u(s, dst, dst);
        }
        return;
    }

    /*
     * In the following,
     *   dep(a, b, m) -> (a & ~m) | (b & m)
     *
     * Begin with:                              src = xxxxabcd
     */
    /* tmp = rol32(src, 24) & 0x000000ff            = 0000000c */
    tcg_out_rlw(s, RLWINM, tmp, src, 24, 24, 31);
    /* tmp = dep(tmp, rol32(src, 8), 0x0000ff00)    = 000000dc */
    tcg_out_rlw(s, RLWIMI, tmp, src, 8, 16, 23);

    if (flags & TCG_BSWAP_OS) {
        tcg_out_ext16s(s, dst, tmp);
    } else {
        tcg_out_mov(s, TCG_TYPE_REG, dst, tmp);
    }
}

static void tcg_out_bswap32(TCGContext *s, TCGReg dst, TCGReg src, int flags)
{
    TCGReg tmp = dst == src ? TCG_REG_R0 : dst;

    if (have_isa_3_10) {
        tcg_out32(s, BRW | RA(dst) | RS(src));
        if (flags & TCG_BSWAP_OS) {
            tcg_out_ext32s(s, dst, dst);
        } else if ((flags & (TCG_BSWAP_IZ | TCG_BSWAP_OZ)) == TCG_BSWAP_OZ) {
            tcg_out_ext32u(s, dst, dst);
        }
        return;
    }

    /*
     * Stolen from gcc's builtin_bswap32.
     * In the following,
     *   dep(a, b, m) -> (a & ~m) | (b & m)
     *
     * Begin with:                              src = xxxxabcd
     */
    /* tmp = rol32(src, 8) & 0xffffffff             = 0000bcda */
    tcg_out_rlw(s, RLWINM, tmp, src, 8, 0, 31);
    /* tmp = dep(tmp, rol32(src, 24), 0xff000000)   = 0000dcda */
    tcg_out_rlw(s, RLWIMI, tmp, src, 24, 0, 7);
    /* tmp = dep(tmp, rol32(src, 24), 0x0000ff00)   = 0000dcba */
    tcg_out_rlw(s, RLWIMI, tmp, src, 24, 16, 23);

    if (flags & TCG_BSWAP_OS) {
        tcg_out_ext32s(s, dst, tmp);
    } else {
        tcg_out_mov(s, TCG_TYPE_REG, dst, tmp);
    }
}

static void tcg_out_bswap64(TCGContext *s, TCGReg dst, TCGReg src)
{
    TCGReg t0 = dst == src ? TCG_REG_R0 : dst;
    TCGReg t1 = dst == src ? dst : TCG_REG_R0;

    if (have_isa_3_10) {
        tcg_out32(s, BRD | RA(dst) | RS(src));
        return;
    }

    /*
     * In the following,
     *   dep(a, b, m) -> (a & ~m) | (b & m)
     *
     * Begin with:                              src = abcdefgh
     */
    /* t0 = rol32(src, 8) & 0xffffffff              = 0000fghe */
    tcg_out_rlw(s, RLWINM, t0, src, 8, 0, 31);
    /* t0 = dep(t0, rol32(src, 24), 0xff000000)     = 0000hghe */
    tcg_out_rlw(s, RLWIMI, t0, src, 24, 0, 7);
    /* t0 = dep(t0, rol32(src, 24), 0x0000ff00)     = 0000hgfe */
    tcg_out_rlw(s, RLWIMI, t0, src, 24, 16, 23);

    /* t0 = rol64(t0, 32)                           = hgfe0000 */
    tcg_out_rld(s, RLDICL, t0, t0, 32, 0);
    /* t1 = rol64(src, 32)                          = efghabcd */
    tcg_out_rld(s, RLDICL, t1, src, 32, 0);

    /* t0 = dep(t0, rol32(t1, 24), 0xffffffff)      = hgfebcda */
    tcg_out_rlw(s, RLWIMI, t0, t1, 8, 0, 31);
    /* t0 = dep(t0, rol32(t1, 24), 0xff000000)      = hgfedcda */
    tcg_out_rlw(s, RLWIMI, t0, t1, 24, 0, 7);
    /* t0 = dep(t0, rol32(t1, 24), 0x0000ff00)      = hgfedcba */
    tcg_out_rlw(s, RLWIMI, t0, t1, 24, 16, 23);

    tcg_out_mov(s, TCG_TYPE_REG, dst, t0);
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
    tb_diff = tcg_tbrel_diff(s, (void *)arg);
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
                       tcg_tbrel_diff(s, NULL));
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

static void tcg_out_dupi_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg ret, int64_t val)
{
    uint32_t load_insn;
    int rel, low;
    intptr_t add;

    switch (vece) {
    case MO_8:
        low = (int8_t)val;
        if (low >= -16 && low < 16) {
            tcg_out32(s, VSPLTISB | VRT(ret) | ((val & 31) << 16));
            return;
        }
        if (have_isa_3_00) {
            tcg_out32(s, XXSPLTIB | VRT(ret) | ((val & 0xff) << 11));
            return;
        }
        break;

    case MO_16:
        low = (int16_t)val;
        if (low >= -16 && low < 16) {
            tcg_out32(s, VSPLTISH | VRT(ret) | ((val & 31) << 16));
            return;
        }
        break;

    case MO_32:
        low = (int32_t)val;
        if (low >= -16 && low < 16) {
            tcg_out32(s, VSPLTISW | VRT(ret) | ((val & 31) << 16));
            return;
        }
        break;
    }

    /*
     * Otherwise we must load the value from the constant pool.
     */
    if (USE_REG_TB) {
        rel = R_PPC_ADDR16;
        add = tcg_tbrel_diff(s, NULL);
    } else {
        rel = R_PPC_ADDR32;
        add = 0;
    }

    if (have_vsx) {
        load_insn = type == TCG_TYPE_V64 ? LXSDX : LXVDSX;
        load_insn |= VRT(ret) | RB(TCG_REG_TMP1);
        if (TCG_TARGET_REG_BITS == 64) {
            new_pool_label(s, val, rel, s->code_ptr, add);
        } else {
            new_pool_l2(s, rel, s->code_ptr, add, val >> 32, val);
        }
    } else {
        load_insn = LVX | VRT(ret) | RB(TCG_REG_TMP1);
        if (TCG_TARGET_REG_BITS == 64) {
            new_pool_l2(s, rel, s->code_ptr, add, val, val);
        } else {
            new_pool_l4(s, rel, s->code_ptr, add,
                        val >> 32, val, val >> 32, val);
        }
    }

    if (USE_REG_TB) {
        tcg_out32(s, ADDI | TAI(TCG_REG_TMP1, 0, 0));
        load_insn |= RA(TCG_REG_TB);
    } else {
        tcg_out32(s, ADDIS | TAI(TCG_REG_TMP1, 0, 0));
        tcg_out32(s, ADDI | TAI(TCG_REG_TMP1, TCG_REG_TMP1, 0));
    }
    tcg_out32(s, load_insn);
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg ret,
                         tcg_target_long arg)
{
    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        tcg_debug_assert(ret < TCG_REG_V0);
        tcg_out_movi_int(s, type, ret, arg, false);
        break;

    default:
        g_assert_not_reached();
    }
}

static void tcg_out_addi_ptr(TCGContext *s, TCGReg rd, TCGReg rs,
                             tcg_target_long imm)
{
    /* This function is only used for passing structs by reference. */
    g_assert_not_reached();
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

static void tcg_out_b(TCGContext *s, int mask, const tcg_insn_unit *target)
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
    bool is_int_store = false;
    TCGReg rs = TCG_REG_TMP1;

    switch (opi) {
    case LD: case LWA:
        align = 3;
        /* FALLTHRU */
    default:
        if (rt > TCG_REG_R0 && rt < TCG_REG_V0) {
            rs = rt;
            break;
        }
        break;
    case LXSD:
    case STXSD:
        align = 3;
        break;
    case LXV:
    case STXV:
        align = 15;
        break;
    case STD:
        align = 3;
        /* FALLTHRU */
    case STB: case STH: case STW:
        is_int_store = true;
        break;
    }

    /* For unaligned, or very large offsets, use the indexed form.  */
    if (offset & align || offset != (int32_t)offset || opi == 0) {
        if (rs == base) {
            rs = TCG_REG_R0;
        }
        tcg_debug_assert(!is_int_store || rs != rt);
        tcg_out_movi(s, TCG_TYPE_PTR, rs, orig);
        tcg_out32(s, opx | TAB(rt & 31, base, rs));
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
        tcg_out32(s, opi | TAI(rt & 31, base, l0));
    }
}

static void tcg_out_vsldoi(TCGContext *s, TCGReg ret,
                           TCGReg va, TCGReg vb, int shb)
{
    tcg_out32(s, VSLDOI | VRT(ret) | VRA(va) | VRB(vb) | (shb << 6));
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                       TCGReg base, intptr_t offset)
{
    int shift;

    switch (type) {
    case TCG_TYPE_I32:
        if (ret < TCG_REG_V0) {
            tcg_out_mem_long(s, LWZ, LWZX, ret, base, offset);
            break;
        }
        if (have_isa_2_07 && have_vsx) {
            tcg_out_mem_long(s, 0, LXSIWZX, ret, base, offset);
            break;
        }
        tcg_debug_assert((offset & 3) == 0);
        tcg_out_mem_long(s, 0, LVEWX, ret, base, offset);
        shift = (offset - 4) & 0xc;
        if (shift) {
            tcg_out_vsldoi(s, ret, ret, ret, shift);
        }
        break;
    case TCG_TYPE_I64:
        if (ret < TCG_REG_V0) {
            tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
            tcg_out_mem_long(s, LD, LDX, ret, base, offset);
            break;
        }
        /* fallthru */
    case TCG_TYPE_V64:
        tcg_debug_assert(ret >= TCG_REG_V0);
        if (have_vsx) {
            tcg_out_mem_long(s, have_isa_3_00 ? LXSD : 0, LXSDX,
                             ret, base, offset);
            break;
        }
        tcg_debug_assert((offset & 7) == 0);
        tcg_out_mem_long(s, 0, LVX, ret, base, offset & -16);
        if (offset & 8) {
            tcg_out_vsldoi(s, ret, ret, ret, 8);
        }
        break;
    case TCG_TYPE_V128:
        tcg_debug_assert(ret >= TCG_REG_V0);
        tcg_debug_assert((offset & 15) == 0);
        tcg_out_mem_long(s, have_isa_3_00 ? LXV : 0,
                         LVX, ret, base, offset);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                              TCGReg base, intptr_t offset)
{
    int shift;

    switch (type) {
    case TCG_TYPE_I32:
        if (arg < TCG_REG_V0) {
            tcg_out_mem_long(s, STW, STWX, arg, base, offset);
            break;
        }
        if (have_isa_2_07 && have_vsx) {
            tcg_out_mem_long(s, 0, STXSIWX, arg, base, offset);
            break;
        }
        assert((offset & 3) == 0);
        tcg_debug_assert((offset & 3) == 0);
        shift = (offset - 4) & 0xc;
        if (shift) {
            tcg_out_vsldoi(s, TCG_VEC_TMP1, arg, arg, shift);
            arg = TCG_VEC_TMP1;
        }
        tcg_out_mem_long(s, 0, STVEWX, arg, base, offset);
        break;
    case TCG_TYPE_I64:
        if (arg < TCG_REG_V0) {
            tcg_debug_assert(TCG_TARGET_REG_BITS == 64);
            tcg_out_mem_long(s, STD, STDX, arg, base, offset);
            break;
        }
        /* fallthru */
    case TCG_TYPE_V64:
        tcg_debug_assert(arg >= TCG_REG_V0);
        if (have_vsx) {
            tcg_out_mem_long(s, have_isa_3_00 ? STXSD : 0,
                             STXSDX, arg, base, offset);
            break;
        }
        tcg_debug_assert((offset & 7) == 0);
        if (offset & 8) {
            tcg_out_vsldoi(s, TCG_VEC_TMP1, arg, arg, 8);
            arg = TCG_VEC_TMP1;
        }
        tcg_out_mem_long(s, 0, STVEWX, arg, base, offset);
        tcg_out_mem_long(s, 0, STVEWX, arg, base, offset + 4);
        break;
    case TCG_TYPE_V128:
        tcg_debug_assert(arg >= TCG_REG_V0);
        tcg_out_mem_long(s, have_isa_3_00 ? STXV : 0,
                         STVX, arg, base, offset);
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
    if (have_isel) {
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
        bc |= reloc_pc14_val(tcg_splitwx_to_rx(s->code_ptr), l->u.value_ptr);
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

    if (have_isel) {
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
        if (have_isel) {
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
    uint32_t insn;

    if (a0 & TCG_MO_ST_LD) {
        insn = HWSYNC;
    } else {
        insn = LWSYNC;
    }

    tcg_out32(s, insn);
}

static void tcg_out_call_int(TCGContext *s, int lk,
                             const tcg_insn_unit *target)
{
#ifdef _CALL_AIX
    /* Look through the descriptor.  If the branch is in range, and we
       don't have to spend too much effort on building the toc.  */
    const void *tgt = ((const void * const *)target)[0];
    uintptr_t toc = ((const uintptr_t *)target)[1];
    intptr_t diff = tcg_pcrel_diff(s, tgt);

    if (in_range_b(diff) && toc == (uint32_t)toc) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP1, toc);
        tcg_out_b(s, lk, tgt);
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
        tcg_out32(s, BCCTR | BO_ALWAYS | lk);
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
        tcg_out_b(s, lk, target);
    } else {
        tcg_out32(s, MTSPR | RS(TCG_REG_R12) | CTR);
        tcg_out32(s, BCCTR | BO_ALWAYS | lk);
    }
#else
    tcg_out_b(s, lk, target);
#endif
}

static void tcg_out_call(TCGContext *s, const tcg_insn_unit *target,
                         const TCGHelperInfo *info)
{
    tcg_out_call_int(s, LK, target);
}

static const uint32_t qemu_ldx_opc[(MO_SSIZE + MO_BSWAP) + 1] = {
    [MO_UB] = LBZX,
    [MO_UW] = LHZX,
    [MO_UL] = LWZX,
    [MO_UQ] = LDX,
    [MO_SW] = LHAX,
    [MO_SL] = LWAX,
    [MO_BSWAP | MO_UB] = LBZX,
    [MO_BSWAP | MO_UW] = LHBRX,
    [MO_BSWAP | MO_UL] = LWBRX,
    [MO_BSWAP | MO_UQ] = LDBRX,
};

static const uint32_t qemu_stx_opc[(MO_SIZE + MO_BSWAP) + 1] = {
    [MO_UB] = STBX,
    [MO_UW] = STHX,
    [MO_UL] = STWX,
    [MO_UQ] = STDX,
    [MO_BSWAP | MO_UB] = STBX,
    [MO_BSWAP | MO_UW] = STHBRX,
    [MO_BSWAP | MO_UL] = STWBRX,
    [MO_BSWAP | MO_UQ] = STDBRX,
};

static const uint32_t qemu_exts_opc[4] = {
    EXTSB, EXTSH, EXTSW, 0
};

#if defined (CONFIG_SOFTMMU)
/* helper signature: helper_ld_mmu(CPUState *env, target_ulong addr,
 *                                 int mmu_idx, uintptr_t ra)
 */
static void * const qemu_ld_helpers[(MO_SIZE | MO_BSWAP) + 1] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
    [MO_LEUQ] = helper_le_ldq_mmu,
    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
    [MO_BEUQ] = helper_be_ldq_mmu,
};

/* helper signature: helper_st_mmu(CPUState *env, target_ulong addr,
 *                                 uintxx_t val, int mmu_idx, uintptr_t ra)
 */
static void * const qemu_st_helpers[(MO_SIZE | MO_BSWAP) + 1] = {
    [MO_UB]   = helper_ret_stb_mmu,
    [MO_LEUW] = helper_le_stw_mmu,
    [MO_LEUL] = helper_le_stl_mmu,
    [MO_LEUQ] = helper_le_stq_mmu,
    [MO_BEUW] = helper_be_stw_mmu,
    [MO_BEUL] = helper_be_stl_mmu,
    [MO_BEUQ] = helper_be_stq_mmu,
};

/* We expect to use a 16-bit negative offset from ENV.  */
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) > 0);
QEMU_BUILD_BUG_ON(TLB_MASK_TABLE_OFS(0) < -32768);

/* Perform the TLB load and compare.  Places the result of the comparison
   in CR7, loads the addend of the TLB into R3, and returns the register
   containing the guest address (zero-extended into R4).  Clobbers R0 and R2. */

static TCGReg tcg_out_tlb_read(TCGContext *s, MemOp opc,
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
static void add_qemu_ldst_label(TCGContext *s, bool is_ld, MemOpIdx oi,
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
    label->raddr = tcg_splitwx_to_rx(raddr);
    label->label_ptr[0] = lptr;
}

static bool tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *lb)
{
    MemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);
    TCGReg hi, lo, arg = TCG_REG_R3;

    if (!reloc_pc14(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    tcg_out_mov(s, TCG_TYPE_PTR, arg++, TCG_AREG0);

    lo = lb->addrlo_reg;
    hi = lb->addrhi_reg;
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        arg |= (TCG_TARGET_CALL_ARG_I64 == TCG_CALL_ARG_EVEN);
        tcg_out_mov(s, TCG_TYPE_I32, arg++, hi);
        tcg_out_mov(s, TCG_TYPE_I32, arg++, lo);
    } else {
        /* If the address needed to be zero-extended, we'll have already
           placed it in R4.  The only remaining case is 64-bit guest.  */
        tcg_out_mov(s, TCG_TYPE_TL, arg++, lo);
    }

    tcg_out_movi(s, TCG_TYPE_I32, arg++, oi);
    tcg_out32(s, MFSPR | RT(arg) | LR);

    tcg_out_call_int(s, LK, qemu_ld_helpers[opc & (MO_BSWAP | MO_SIZE)]);

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
    MemOpIdx oi = lb->oi;
    MemOp opc = get_memop(oi);
    MemOp s_bits = opc & MO_SIZE;
    TCGReg hi, lo, arg = TCG_REG_R3;

    if (!reloc_pc14(lb->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    tcg_out_mov(s, TCG_TYPE_PTR, arg++, TCG_AREG0);

    lo = lb->addrlo_reg;
    hi = lb->addrhi_reg;
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        arg |= (TCG_TARGET_CALL_ARG_I64 == TCG_CALL_ARG_EVEN);
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
            arg |= (TCG_TARGET_CALL_ARG_I64 == TCG_CALL_ARG_EVEN);
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

    tcg_out_call_int(s, LK, qemu_st_helpers[opc & (MO_BSWAP | MO_SIZE)]);

    tcg_out_b(s, 0, lb->raddr);
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

    /* We are expecting a_bits to max out at 7, much lower than ANDI. */
    tcg_debug_assert(a_bits < 16);
    tcg_out32(s, ANDI | SAI(addrlo, TCG_REG_R0, a_mask));

    label->label_ptr[0] = s->code_ptr;
    tcg_out32(s, BC | BI(0, CR_EQ) | BO_COND_FALSE | LK);

    label->raddr = tcg_splitwx_to_rx(s->code_ptr);
}

static bool tcg_out_fail_alignment(TCGContext *s, TCGLabelQemuLdst *l)
{
    if (!reloc_pc14(l->label_ptr[0], tcg_splitwx_to_rx(s->code_ptr))) {
        return false;
    }

    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        TCGReg arg = TCG_REG_R4;

        arg |= (TCG_TARGET_CALL_ARG_I64 == TCG_CALL_ARG_EVEN);
        if (l->addrlo_reg != arg) {
            tcg_out_mov(s, TCG_TYPE_I32, arg, l->addrhi_reg);
            tcg_out_mov(s, TCG_TYPE_I32, arg + 1, l->addrlo_reg);
        } else if (l->addrhi_reg != arg + 1) {
            tcg_out_mov(s, TCG_TYPE_I32, arg + 1, l->addrlo_reg);
            tcg_out_mov(s, TCG_TYPE_I32, arg, l->addrhi_reg);
        } else {
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R0, arg);
            tcg_out_mov(s, TCG_TYPE_I32, arg, arg + 1);
            tcg_out_mov(s, TCG_TYPE_I32, arg + 1, TCG_REG_R0);
        }
    } else {
        tcg_out_mov(s, TCG_TYPE_TL, TCG_REG_R4, l->addrlo_reg);
    }
    tcg_out_mov(s, TCG_TYPE_TL, TCG_REG_R3, TCG_AREG0);

    /* "Tail call" to the helper, with the return address back inline. */
    tcg_out_call_int(s, 0, (const void *)(l->is_ld ? helper_unaligned_ld
                                          : helper_unaligned_st));
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

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is_64)
{
    TCGReg datalo, datahi, addrlo, rbase;
    TCGReg addrhi __attribute__((unused));
    MemOpIdx oi;
    MemOp opc, s_bits;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    tcg_insn_unit *label_ptr;
#else
    unsigned a_bits;
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
    a_bits = get_alignment_bits(opc);
    if (a_bits) {
        tcg_out_test_alignment(s, true, addrlo, addrhi, a_bits);
    }
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
        if (!have_isa_2_06 && insn == LDBRX) {
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
    MemOpIdx oi;
    MemOp opc, s_bits;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    tcg_insn_unit *label_ptr;
#else
    unsigned a_bits;
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
    a_bits = get_alignment_bits(opc);
    if (a_bits) {
        tcg_out_test_alignment(s, false, addrlo, addrhi, a_bits);
    }
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
        if (!have_isa_2_06 && insn == STDBRX) {
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

#ifdef _CALL_AIX
# define LINK_AREA_SIZE                (6 * SZR)
# define LR_OFFSET                     (1 * SZR)
# define TCG_TARGET_CALL_STACK_OFFSET  (LINK_AREA_SIZE + 8 * SZR)
#elif defined(_CALL_DARWIN)
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
    const void **desc = (const void **)s->code_ptr;
    desc[0] = tcg_splitwx_to_rx(desc + 2);  /* entry point */
    desc[1] = 0;                            /* environment pointer */
    s->code_ptr = (void *)(desc + 2);       /* skip over descriptor */
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
    tcg_code_gen_epilogue = tcg_splitwx_to_rx(s->code_ptr);

    tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R0, TCG_REG_R1, FRAME_SIZE+LR_OFFSET);
    for (i = 0; i < ARRAY_SIZE(tcg_target_callee_save_regs); ++i) {
        tcg_out_ld(s, TCG_TYPE_REG, tcg_target_callee_save_regs[i],
                   TCG_REG_R1, REG_SAVE_BOT + i * SZR);
    }
    tcg_out32(s, MTSPR | RS(TCG_REG_R0) | LR);
    tcg_out32(s, ADDI | TAI(TCG_REG_R1, TCG_REG_R1, FRAME_SIZE));
    tcg_out32(s, BCLR | BO_ALWAYS);
}

static void tcg_out_exit_tb(TCGContext *s, uintptr_t arg)
{
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R3, arg);
    tcg_out_b(s, 0, tcg_code_gen_epilogue);
}

static void tcg_out_goto_tb(TCGContext *s, int which)
{
    uintptr_t ptr = get_jmp_target_addr(s, which);

    if (USE_REG_TB) {
        ptrdiff_t offset = tcg_tbrel_diff(s, (void *)ptr);
        tcg_out_mem_long(s, LD, LDX, TCG_REG_TB, TCG_REG_TB, offset);
    
        /* Direct branch will be patched by tb_target_set_jmp_target. */
        set_jmp_insn_offset(s, which);
        tcg_out32(s, MTSPR | RS(TCG_REG_TB) | CTR);

        /* When branch is out of range, fall through to indirect. */
        tcg_out32(s, BCCTR | BO_ALWAYS);

        /* For the unlinked case, need to reset TCG_REG_TB.  */
        set_jmp_reset_offset(s, which);
        tcg_out_mem_long(s, ADDI, ADD, TCG_REG_TB, TCG_REG_TB,
                         -tcg_current_code_size(s));
    } else {
        /* Direct branch will be patched by tb_target_set_jmp_target. */
        set_jmp_insn_offset(s, which);
        tcg_out32(s, NOP);

        /* When branch is out of range, fall through to indirect. */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP1, ptr - (int16_t)ptr);
        tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_TMP1, TCG_REG_TMP1, (int16_t)ptr);
        tcg_out32(s, MTSPR | RS(TCG_REG_TMP1) | CTR);
        tcg_out32(s, BCCTR | BO_ALWAYS);
        set_jmp_reset_offset(s, which);
    }
}

void tb_target_set_jmp_target(const TranslationBlock *tb, int n,
                              uintptr_t jmp_rx, uintptr_t jmp_rw)
{
    uintptr_t addr = tb->jmp_target_addr[n];
    intptr_t diff = addr - jmp_rx;
    tcg_insn_unit insn;

    if (in_range_b(diff)) {
        insn = B | (diff & 0x3fffffc);
    } else if (USE_REG_TB) {
        insn = MTSPR | RS(TCG_REG_TB) | CTR;
    } else {
        insn = NOP;
    }

    qatomic_set((uint32_t *)jmp_rw, insn);
    flush_idcache_range(jmp_rx, jmp_rw, 4);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    TCGArg a0, a1, a2;

    switch (opc) {
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
                insn |= reloc_pc24_val(tcg_splitwx_to_rx(s->code_ptr),
                                       l->u.value_ptr);
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
        tcg_out_ext8s(s, args[0], args[0]);
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

    case INDEX_op_rem_i32:
        tcg_out32(s, MODSW | TAB(args[0], args[1], args[2]));
        break;

    case INDEX_op_remu_i32:
        tcg_out32(s, MODUW | TAB(args[0], args[1], args[2]));
        break;

    case INDEX_op_shl_i32:
        if (const_args[2]) {
            /* Limit immediate shift count lest we create an illegal insn.  */
            tcg_out_shli32(s, args[0], args[1], args[2] & 31);
        } else {
            tcg_out32(s, SLW | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_shr_i32:
        if (const_args[2]) {
            /* Limit immediate shift count lest we create an illegal insn.  */
            tcg_out_shri32(s, args[0], args[1], args[2] & 31);
        } else {
            tcg_out32(s, SRW | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_sar_i32:
        if (const_args[2]) {
            tcg_out_sari32(s, args[0], args[1], args[2]);
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
            /* Limit immediate shift count lest we create an illegal insn.  */
            tcg_out_shli64(s, args[0], args[1], args[2] & 63);
        } else {
            tcg_out32(s, SLD | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_shr_i64:
        if (const_args[2]) {
            /* Limit immediate shift count lest we create an illegal insn.  */
            tcg_out_shri64(s, args[0], args[1], args[2] & 63);
        } else {
            tcg_out32(s, SRD | SAB(args[1], args[0], args[2]));
        }
        break;
    case INDEX_op_sar_i64:
        if (const_args[2]) {
            tcg_out_sari64(s, args[0], args[1], args[2]);
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
    case INDEX_op_rem_i64:
        tcg_out32(s, MODSD | TAB(args[0], args[1], args[2]));
        break;
    case INDEX_op_remu_i64:
        tcg_out32(s, MODUD | TAB(args[0], args[1], args[2]));
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
        tcg_out_ext8s(s, args[0], args[1]);
        break;
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
        tcg_out_ext16s(s, args[0], args[1]);
        break;
    case INDEX_op_ext_i32_i64:
    case INDEX_op_ext32s_i64:
        tcg_out_ext32s(s, args[0], args[1]);
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
        tcg_out_bswap16(s, args[0], args[1], args[2]);
        break;
    case INDEX_op_bswap32_i32:
        tcg_out_bswap32(s, args[0], args[1], 0);
        break;
    case INDEX_op_bswap32_i64:
        tcg_out_bswap32(s, args[0], args[1], args[2]);
        break;
    case INDEX_op_bswap64_i64:
        tcg_out_bswap64(s, args[0], args[1]);
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
    case INDEX_op_call:      /* Always emitted via tcg_out_call.  */
    case INDEX_op_exit_tb:   /* Always emitted via tcg_out_exit_tb.  */
    case INDEX_op_goto_tb:   /* Always emitted via tcg_out_goto_tb.  */
    default:
        tcg_abort();
    }
}

int tcg_can_emit_vec_op(TCGOpcode opc, TCGType type, unsigned vece)
{
    switch (opc) {
    case INDEX_op_and_vec:
    case INDEX_op_or_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_andc_vec:
    case INDEX_op_not_vec:
    case INDEX_op_nor_vec:
    case INDEX_op_eqv_vec:
    case INDEX_op_nand_vec:
        return 1;
    case INDEX_op_orc_vec:
        return have_isa_2_07;
    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_rotlv_vec:
        return vece <= MO_32 || have_isa_2_07;
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
        return vece <= MO_32;
    case INDEX_op_cmp_vec:
    case INDEX_op_shli_vec:
    case INDEX_op_shri_vec:
    case INDEX_op_sari_vec:
    case INDEX_op_rotli_vec:
        return vece <= MO_32 || have_isa_2_07 ? -1 : 0;
    case INDEX_op_neg_vec:
        return vece >= MO_32 && have_isa_3_00;
    case INDEX_op_mul_vec:
        switch (vece) {
        case MO_8:
        case MO_16:
            return -1;
        case MO_32:
            return have_isa_2_07 ? 1 : -1;
        case MO_64:
            return have_isa_3_10;
        }
        return 0;
    case INDEX_op_bitsel_vec:
        return have_vsx;
    case INDEX_op_rotrv_vec:
        return -1;
    default:
        return 0;
    }
}

static bool tcg_out_dup_vec(TCGContext *s, TCGType type, unsigned vece,
                            TCGReg dst, TCGReg src)
{
    tcg_debug_assert(dst >= TCG_REG_V0);

    /* Splat from integer reg allowed via constraints for v3.00.  */
    if (src < TCG_REG_V0) {
        tcg_debug_assert(have_isa_3_00);
        switch (vece) {
        case MO_64:
            tcg_out32(s, MTVSRDD | VRT(dst) | RA(src) | RB(src));
            return true;
        case MO_32:
            tcg_out32(s, MTVSRWS | VRT(dst) | RA(src));
            return true;
        default:
            /* Fail, so that we fall back on either dupm or mov+dup.  */
            return false;
        }
    }

    /*
     * Recall we use (or emulate) VSX integer loads, so the integer is
     * right justified within the left (zero-index) double-word.
     */
    switch (vece) {
    case MO_8:
        tcg_out32(s, VSPLTB | VRT(dst) | VRB(src) | (7 << 16));
        break;
    case MO_16:
        tcg_out32(s, VSPLTH | VRT(dst) | VRB(src) | (3 << 16));
        break;
    case MO_32:
        tcg_out32(s, VSPLTW | VRT(dst) | VRB(src) | (1 << 16));
        break;
    case MO_64:
        if (have_vsx) {
            tcg_out32(s, XXPERMDI | VRT(dst) | VRA(src) | VRB(src));
            break;
        }
        tcg_out_vsldoi(s, TCG_VEC_TMP1, src, src, 8);
        tcg_out_vsldoi(s, dst, TCG_VEC_TMP1, src, 8);
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

static bool tcg_out_dupm_vec(TCGContext *s, TCGType type, unsigned vece,
                             TCGReg out, TCGReg base, intptr_t offset)
{
    int elt;

    tcg_debug_assert(out >= TCG_REG_V0);
    switch (vece) {
    case MO_8:
        if (have_isa_3_00) {
            tcg_out_mem_long(s, LXV, LVX, out, base, offset & -16);
        } else {
            tcg_out_mem_long(s, 0, LVEBX, out, base, offset);
        }
        elt = extract32(offset, 0, 4);
#if !HOST_BIG_ENDIAN
        elt ^= 15;
#endif
        tcg_out32(s, VSPLTB | VRT(out) | VRB(out) | (elt << 16));
        break;
    case MO_16:
        tcg_debug_assert((offset & 1) == 0);
        if (have_isa_3_00) {
            tcg_out_mem_long(s, LXV | 8, LVX, out, base, offset & -16);
        } else {
            tcg_out_mem_long(s, 0, LVEHX, out, base, offset);
        }
        elt = extract32(offset, 1, 3);
#if !HOST_BIG_ENDIAN
        elt ^= 7;
#endif
        tcg_out32(s, VSPLTH | VRT(out) | VRB(out) | (elt << 16));
        break;
    case MO_32:
        if (have_isa_3_00) {
            tcg_out_mem_long(s, 0, LXVWSX, out, base, offset);
            break;
        }
        tcg_debug_assert((offset & 3) == 0);
        tcg_out_mem_long(s, 0, LVEWX, out, base, offset);
        elt = extract32(offset, 2, 2);
#if !HOST_BIG_ENDIAN
        elt ^= 3;
#endif
        tcg_out32(s, VSPLTW | VRT(out) | VRB(out) | (elt << 16));
        break;
    case MO_64:
        if (have_vsx) {
            tcg_out_mem_long(s, 0, LXVDSX, out, base, offset);
            break;
        }
        tcg_debug_assert((offset & 7) == 0);
        tcg_out_mem_long(s, 0, LVX, out, base, offset & -16);
        tcg_out_vsldoi(s, TCG_VEC_TMP1, out, out, 8);
        elt = extract32(offset, 3, 1);
#if !HOST_BIG_ENDIAN
        elt = !elt;
#endif
        if (elt) {
            tcg_out_vsldoi(s, out, out, TCG_VEC_TMP1, 8);
        } else {
            tcg_out_vsldoi(s, out, TCG_VEC_TMP1, out, 8);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

static void tcg_out_vec_op(TCGContext *s, TCGOpcode opc,
                           unsigned vecl, unsigned vece,
                           const TCGArg args[TCG_MAX_OP_ARGS],
                           const int const_args[TCG_MAX_OP_ARGS])
{
    static const uint32_t
        add_op[4] = { VADDUBM, VADDUHM, VADDUWM, VADDUDM },
        sub_op[4] = { VSUBUBM, VSUBUHM, VSUBUWM, VSUBUDM },
        mul_op[4] = { 0, 0, VMULUWM, VMULLD },
        neg_op[4] = { 0, 0, VNEGW, VNEGD },
        eq_op[4]  = { VCMPEQUB, VCMPEQUH, VCMPEQUW, VCMPEQUD },
        ne_op[4]  = { VCMPNEB, VCMPNEH, VCMPNEW, 0 },
        gts_op[4] = { VCMPGTSB, VCMPGTSH, VCMPGTSW, VCMPGTSD },
        gtu_op[4] = { VCMPGTUB, VCMPGTUH, VCMPGTUW, VCMPGTUD },
        ssadd_op[4] = { VADDSBS, VADDSHS, VADDSWS, 0 },
        usadd_op[4] = { VADDUBS, VADDUHS, VADDUWS, 0 },
        sssub_op[4] = { VSUBSBS, VSUBSHS, VSUBSWS, 0 },
        ussub_op[4] = { VSUBUBS, VSUBUHS, VSUBUWS, 0 },
        umin_op[4] = { VMINUB, VMINUH, VMINUW, VMINUD },
        smin_op[4] = { VMINSB, VMINSH, VMINSW, VMINSD },
        umax_op[4] = { VMAXUB, VMAXUH, VMAXUW, VMAXUD },
        smax_op[4] = { VMAXSB, VMAXSH, VMAXSW, VMAXSD },
        shlv_op[4] = { VSLB, VSLH, VSLW, VSLD },
        shrv_op[4] = { VSRB, VSRH, VSRW, VSRD },
        sarv_op[4] = { VSRAB, VSRAH, VSRAW, VSRAD },
        mrgh_op[4] = { VMRGHB, VMRGHH, VMRGHW, 0 },
        mrgl_op[4] = { VMRGLB, VMRGLH, VMRGLW, 0 },
        muleu_op[4] = { VMULEUB, VMULEUH, VMULEUW, 0 },
        mulou_op[4] = { VMULOUB, VMULOUH, VMULOUW, 0 },
        pkum_op[4] = { VPKUHUM, VPKUWUM, 0, 0 },
        rotl_op[4] = { VRLB, VRLH, VRLW, VRLD };

    TCGType type = vecl + TCG_TYPE_V64;
    TCGArg a0 = args[0], a1 = args[1], a2 = args[2];
    uint32_t insn;

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

    case INDEX_op_add_vec:
        insn = add_op[vece];
        break;
    case INDEX_op_sub_vec:
        insn = sub_op[vece];
        break;
    case INDEX_op_neg_vec:
        insn = neg_op[vece];
        a2 = a1;
        a1 = 0;
        break;
    case INDEX_op_mul_vec:
        insn = mul_op[vece];
        break;
    case INDEX_op_ssadd_vec:
        insn = ssadd_op[vece];
        break;
    case INDEX_op_sssub_vec:
        insn = sssub_op[vece];
        break;
    case INDEX_op_usadd_vec:
        insn = usadd_op[vece];
        break;
    case INDEX_op_ussub_vec:
        insn = ussub_op[vece];
        break;
    case INDEX_op_smin_vec:
        insn = smin_op[vece];
        break;
    case INDEX_op_umin_vec:
        insn = umin_op[vece];
        break;
    case INDEX_op_smax_vec:
        insn = smax_op[vece];
        break;
    case INDEX_op_umax_vec:
        insn = umax_op[vece];
        break;
    case INDEX_op_shlv_vec:
        insn = shlv_op[vece];
        break;
    case INDEX_op_shrv_vec:
        insn = shrv_op[vece];
        break;
    case INDEX_op_sarv_vec:
        insn = sarv_op[vece];
        break;
    case INDEX_op_and_vec:
        insn = VAND;
        break;
    case INDEX_op_or_vec:
        insn = VOR;
        break;
    case INDEX_op_xor_vec:
        insn = VXOR;
        break;
    case INDEX_op_andc_vec:
        insn = VANDC;
        break;
    case INDEX_op_not_vec:
        insn = VNOR;
        a2 = a1;
        break;
    case INDEX_op_orc_vec:
        insn = VORC;
        break;
    case INDEX_op_nand_vec:
        insn = VNAND;
        break;
    case INDEX_op_nor_vec:
        insn = VNOR;
        break;
    case INDEX_op_eqv_vec:
        insn = VEQV;
        break;

    case INDEX_op_cmp_vec:
        switch (args[3]) {
        case TCG_COND_EQ:
            insn = eq_op[vece];
            break;
        case TCG_COND_NE:
            insn = ne_op[vece];
            break;
        case TCG_COND_GT:
            insn = gts_op[vece];
            break;
        case TCG_COND_GTU:
            insn = gtu_op[vece];
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case INDEX_op_bitsel_vec:
        tcg_out32(s, XXSEL | VRT(a0) | VRC(a1) | VRB(a2) | VRA(args[3]));
        return;

    case INDEX_op_dup2_vec:
        assert(TCG_TARGET_REG_BITS == 32);
        /* With inputs a1 = xLxx, a2 = xHxx  */
        tcg_out32(s, VMRGHW | VRT(a0) | VRA(a2) | VRB(a1));  /* a0  = xxHL */
        tcg_out_vsldoi(s, TCG_VEC_TMP1, a0, a0, 8);          /* tmp = HLxx */
        tcg_out_vsldoi(s, a0, a0, TCG_VEC_TMP1, 8);          /* a0  = HLHL */
        return;

    case INDEX_op_ppc_mrgh_vec:
        insn = mrgh_op[vece];
        break;
    case INDEX_op_ppc_mrgl_vec:
        insn = mrgl_op[vece];
        break;
    case INDEX_op_ppc_muleu_vec:
        insn = muleu_op[vece];
        break;
    case INDEX_op_ppc_mulou_vec:
        insn = mulou_op[vece];
        break;
    case INDEX_op_ppc_pkum_vec:
        insn = pkum_op[vece];
        break;
    case INDEX_op_rotlv_vec:
        insn = rotl_op[vece];
        break;
    case INDEX_op_ppc_msum_vec:
        tcg_debug_assert(vece == MO_16);
        tcg_out32(s, VMSUMUHM | VRT(a0) | VRA(a1) | VRB(a2) | VRC(args[3]));
        return;

    case INDEX_op_mov_vec:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_dup_vec:  /* Always emitted via tcg_out_dup_vec.  */
    default:
        g_assert_not_reached();
    }

    tcg_debug_assert(insn != 0);
    tcg_out32(s, insn | VRT(a0) | VRA(a1) | VRB(a2));
}

static void expand_vec_shi(TCGType type, unsigned vece, TCGv_vec v0,
                           TCGv_vec v1, TCGArg imm, TCGOpcode opci)
{
    TCGv_vec t1;

    if (vece == MO_32) {
        /*
         * Only 5 bits are significant, and VSPLTISB can represent -16..15.
         * So using negative numbers gets us the 4th bit easily.
         */
        imm = sextract32(imm, 0, 5);
    } else {
        imm &= (8 << vece) - 1;
    }

    /* Splat w/bytes for xxspltib when 2.07 allows MO_64. */
    t1 = tcg_constant_vec(type, MO_8, imm);
    vec_gen_3(opci, type, vece, tcgv_vec_arg(v0),
              tcgv_vec_arg(v1), tcgv_vec_arg(t1));
}

static void expand_vec_cmp(TCGType type, unsigned vece, TCGv_vec v0,
                           TCGv_vec v1, TCGv_vec v2, TCGCond cond)
{
    bool need_swap = false, need_inv = false;

    tcg_debug_assert(vece <= MO_32 || have_isa_2_07);

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_GT:
    case TCG_COND_GTU:
        break;
    case TCG_COND_NE:
        if (have_isa_3_00 && vece <= MO_32) {
            break;
        }
        /* fall through */
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

    if (need_inv) {
        tcg_gen_not_vec(vece, v0, v0);
    }
}

static void expand_vec_mul(TCGType type, unsigned vece, TCGv_vec v0,
                           TCGv_vec v1, TCGv_vec v2)
{
    TCGv_vec t1 = tcg_temp_new_vec(type);
    TCGv_vec t2 = tcg_temp_new_vec(type);
    TCGv_vec c0, c16;

    switch (vece) {
    case MO_8:
    case MO_16:
        vec_gen_3(INDEX_op_ppc_muleu_vec, type, vece, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        vec_gen_3(INDEX_op_ppc_mulou_vec, type, vece, tcgv_vec_arg(t2),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        vec_gen_3(INDEX_op_ppc_mrgh_vec, type, vece + 1, tcgv_vec_arg(v0),
                  tcgv_vec_arg(t1), tcgv_vec_arg(t2));
        vec_gen_3(INDEX_op_ppc_mrgl_vec, type, vece + 1, tcgv_vec_arg(t1),
                  tcgv_vec_arg(t1), tcgv_vec_arg(t2));
        vec_gen_3(INDEX_op_ppc_pkum_vec, type, vece, tcgv_vec_arg(v0),
                  tcgv_vec_arg(v0), tcgv_vec_arg(t1));
	break;

    case MO_32:
        tcg_debug_assert(!have_isa_2_07);
        /*
         * Only 5 bits are significant, and VSPLTISB can represent -16..15.
         * So using -16 is a quick way to represent 16.
         */
        c16 = tcg_constant_vec(type, MO_8, -16);
        c0 = tcg_constant_vec(type, MO_8, 0);

        vec_gen_3(INDEX_op_rotlv_vec, type, MO_32, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v2), tcgv_vec_arg(c16));
        vec_gen_3(INDEX_op_ppc_mulou_vec, type, MO_16, tcgv_vec_arg(t2),
                  tcgv_vec_arg(v1), tcgv_vec_arg(v2));
        vec_gen_4(INDEX_op_ppc_msum_vec, type, MO_16, tcgv_vec_arg(t1),
                  tcgv_vec_arg(v1), tcgv_vec_arg(t1), tcgv_vec_arg(c0));
        vec_gen_3(INDEX_op_shlv_vec, type, MO_32, tcgv_vec_arg(t1),
                  tcgv_vec_arg(t1), tcgv_vec_arg(c16));
        tcg_gen_add_vec(MO_32, v0, t1, t2);
        break;

    default:
        g_assert_not_reached();
    }
    tcg_temp_free_vec(t1);
    tcg_temp_free_vec(t2);
}

void tcg_expand_vec_op(TCGOpcode opc, TCGType type, unsigned vece,
                       TCGArg a0, ...)
{
    va_list va;
    TCGv_vec v0, v1, v2, t0;
    TCGArg a2;

    va_start(va, a0);
    v0 = temp_tcgv_vec(arg_temp(a0));
    v1 = temp_tcgv_vec(arg_temp(va_arg(va, TCGArg)));
    a2 = va_arg(va, TCGArg);

    switch (opc) {
    case INDEX_op_shli_vec:
        expand_vec_shi(type, vece, v0, v1, a2, INDEX_op_shlv_vec);
        break;
    case INDEX_op_shri_vec:
        expand_vec_shi(type, vece, v0, v1, a2, INDEX_op_shrv_vec);
        break;
    case INDEX_op_sari_vec:
        expand_vec_shi(type, vece, v0, v1, a2, INDEX_op_sarv_vec);
        break;
    case INDEX_op_rotli_vec:
        expand_vec_shi(type, vece, v0, v1, a2, INDEX_op_rotlv_vec);
        break;
    case INDEX_op_cmp_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        expand_vec_cmp(type, vece, v0, v1, v2, va_arg(va, TCGArg));
        break;
    case INDEX_op_mul_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        expand_vec_mul(type, vece, v0, v1, v2);
        break;
    case INDEX_op_rotlv_vec:
        v2 = temp_tcgv_vec(arg_temp(a2));
        t0 = tcg_temp_new_vec(type);
        tcg_gen_neg_vec(vece, t0, v2);
        tcg_gen_rotlv_vec(vece, v0, v1, t0);
        tcg_temp_free_vec(t0);
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
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
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
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return C_O0_I2(r, r);

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
        return C_O1_I2(r, r, ri);

    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
        return C_O1_I2(r, r, rI);

    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
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
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
    case INDEX_op_mulsh_i64:
    case INDEX_op_muluh_i64:
        return C_O1_I2(r, r, r);

    case INDEX_op_sub_i32:
        return C_O1_I2(r, rI, ri);
    case INDEX_op_add_i64:
        return C_O1_I2(r, r, rT);
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
        return C_O1_I2(r, r, rU);
    case INDEX_op_sub_i64:
        return C_O1_I2(r, rI, rT);
    case INDEX_op_clz_i32:
    case INDEX_op_ctz_i32:
    case INDEX_op_clz_i64:
    case INDEX_op_ctz_i64:
        return C_O1_I2(r, r, rZW);

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return C_O0_I2(r, ri);

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        return C_O1_I4(r, r, ri, rZ, rZ);
    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        return C_O1_I2(r, 0, rZ);
    case INDEX_op_brcond2_i32:
        return C_O0_I4(r, r, ri, ri);
    case INDEX_op_setcond2_i32:
        return C_O1_I4(r, r, r, ri, ri);
    case INDEX_op_add2_i64:
    case INDEX_op_add2_i32:
        return C_O2_I4(r, r, r, r, rI, rZM);
    case INDEX_op_sub2_i64:
    case INDEX_op_sub2_i32:
        return C_O2_I4(r, r, rI, rZM, r, r);

    case INDEX_op_qemu_ld_i32:
        return (TCG_TARGET_REG_BITS == 64 || TARGET_LONG_BITS == 32
                ? C_O1_I1(r, L)
                : C_O1_I2(r, L, L));

    case INDEX_op_qemu_st_i32:
        return (TCG_TARGET_REG_BITS == 64 || TARGET_LONG_BITS == 32
                ? C_O0_I2(S, S)
                : C_O0_I3(S, S, S));

    case INDEX_op_qemu_ld_i64:
        return (TCG_TARGET_REG_BITS == 64 ? C_O1_I1(r, L)
                : TARGET_LONG_BITS == 32 ? C_O2_I1(L, L, L)
                : C_O2_I2(L, L, L, L));

    case INDEX_op_qemu_st_i64:
        return (TCG_TARGET_REG_BITS == 64 ? C_O0_I2(S, S)
                : TARGET_LONG_BITS == 32 ? C_O0_I3(S, S, S)
                : C_O0_I4(S, S, S, S));

    case INDEX_op_add_vec:
    case INDEX_op_sub_vec:
    case INDEX_op_mul_vec:
    case INDEX_op_and_vec:
    case INDEX_op_or_vec:
    case INDEX_op_xor_vec:
    case INDEX_op_andc_vec:
    case INDEX_op_orc_vec:
    case INDEX_op_nor_vec:
    case INDEX_op_eqv_vec:
    case INDEX_op_nand_vec:
    case INDEX_op_cmp_vec:
    case INDEX_op_ssadd_vec:
    case INDEX_op_sssub_vec:
    case INDEX_op_usadd_vec:
    case INDEX_op_ussub_vec:
    case INDEX_op_smax_vec:
    case INDEX_op_smin_vec:
    case INDEX_op_umax_vec:
    case INDEX_op_umin_vec:
    case INDEX_op_shlv_vec:
    case INDEX_op_shrv_vec:
    case INDEX_op_sarv_vec:
    case INDEX_op_rotlv_vec:
    case INDEX_op_rotrv_vec:
    case INDEX_op_ppc_mrgh_vec:
    case INDEX_op_ppc_mrgl_vec:
    case INDEX_op_ppc_muleu_vec:
    case INDEX_op_ppc_mulou_vec:
    case INDEX_op_ppc_pkum_vec:
    case INDEX_op_dup2_vec:
        return C_O1_I2(v, v, v);

    case INDEX_op_not_vec:
    case INDEX_op_neg_vec:
        return C_O1_I1(v, v);

    case INDEX_op_dup_vec:
        return have_isa_3_00 ? C_O1_I1(v, vr) : C_O1_I1(v, v);

    case INDEX_op_ld_vec:
    case INDEX_op_dupm_vec:
        return C_O1_I1(v, r);

    case INDEX_op_st_vec:
        return C_O0_I2(v, r);

    case INDEX_op_bitsel_vec:
    case INDEX_op_ppc_msum_vec:
        return C_O1_I3(v, v, v, v);

    default:
        g_assert_not_reached();
    }
}

static void tcg_target_init(TCGContext *s)
{
    unsigned long hwcap = qemu_getauxval(AT_HWCAP);
    unsigned long hwcap2 = qemu_getauxval(AT_HWCAP2);

    have_isa = tcg_isa_base;
    if (hwcap & PPC_FEATURE_ARCH_2_06) {
        have_isa = tcg_isa_2_06;
    }
#ifdef PPC_FEATURE2_ARCH_2_07
    if (hwcap2 & PPC_FEATURE2_ARCH_2_07) {
        have_isa = tcg_isa_2_07;
    }
#endif
#ifdef PPC_FEATURE2_ARCH_3_00
    if (hwcap2 & PPC_FEATURE2_ARCH_3_00) {
        have_isa = tcg_isa_3_00;
    }
#endif
#ifdef PPC_FEATURE2_ARCH_3_10
    if (hwcap2 & PPC_FEATURE2_ARCH_3_10) {
        have_isa = tcg_isa_3_10;
    }
#endif

#ifdef PPC_FEATURE2_HAS_ISEL
    /* Prefer explicit instruction from the kernel. */
    have_isel = (hwcap2 & PPC_FEATURE2_HAS_ISEL) != 0;
#else
    /* Fall back to knowing Power7 (2.06) has ISEL. */
    have_isel = have_isa_2_06;
#endif

    if (hwcap & PPC_FEATURE_HAS_ALTIVEC) {
        have_altivec = true;
        /* We only care about the portion of VSX that overlaps Altivec. */
        if (hwcap & PPC_FEATURE_HAS_VSX) {
            have_vsx = true;
        }
    }

    tcg_target_available_regs[TCG_TYPE_I32] = 0xffffffff;
    tcg_target_available_regs[TCG_TYPE_I64] = 0xffffffff;
    if (have_altivec) {
        tcg_target_available_regs[TCG_TYPE_V64] = 0xffffffff00000000ull;
        tcg_target_available_regs[TCG_TYPE_V128] = 0xffffffff00000000ull;
    }

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

    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V0);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V1);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V2);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V3);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V4);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V5);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V6);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V7);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V8);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V9);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V10);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V11);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V12);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V13);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V14);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V15);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V16);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V17);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V18);
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_V19);

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
    tcg_regset_set_reg(s->reserved_regs, TCG_VEC_TMP1);
    tcg_regset_set_reg(s->reserved_regs, TCG_VEC_TMP2);
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

void tcg_register_jit(const void *buf, size_t buf_size)
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
#undef VMULEUB
#undef VMULEUH
#undef VMULEUW
#undef VMULOUB
#undef VMULOUH
#undef VMULOUW
#undef VMSUMUHM
