/*
 *  MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI support
 *
 *  Copyright (c) 2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#if SHIFT == 0
#define Reg MMXReg
#define SUFFIX _mmx
#else
#define Reg XMMReg
#define SUFFIX _xmm
#endif

#define dh_alias_Reg ptr
#define dh_alias_XMMReg ptr
#define dh_alias_MMXReg ptr
#define dh_ctype_Reg Reg *
#define dh_ctype_XMMReg XMMReg *
#define dh_ctype_MMXReg MMXReg *

DEF_HELPER_2(glue(psrlw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psraw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psllw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psrld, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psrad, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pslld, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psrlq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psllq, SUFFIX), void, Reg, Reg)

#if SHIFT == 1
DEF_HELPER_2(glue(psrldq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pslldq, SUFFIX), void, Reg, Reg)
#endif

#define SSE_HELPER_B(name, F)\
    DEF_HELPER_2(glue(name, SUFFIX), void, Reg, Reg)

#define SSE_HELPER_W(name, F)\
    DEF_HELPER_2(glue(name, SUFFIX), void, Reg, Reg)

#define SSE_HELPER_L(name, F)\
    DEF_HELPER_2(glue(name, SUFFIX), void, Reg, Reg)

#define SSE_HELPER_Q(name, F)\
    DEF_HELPER_2(glue(name, SUFFIX), void, Reg, Reg)

SSE_HELPER_B(paddb, FADD)
SSE_HELPER_W(paddw, FADD)
SSE_HELPER_L(paddl, FADD)
SSE_HELPER_Q(paddq, FADD)

SSE_HELPER_B(psubb, FSUB)
SSE_HELPER_W(psubw, FSUB)
SSE_HELPER_L(psubl, FSUB)
SSE_HELPER_Q(psubq, FSUB)

SSE_HELPER_B(paddusb, FADDUB)
SSE_HELPER_B(paddsb, FADDSB)
SSE_HELPER_B(psubusb, FSUBUB)
SSE_HELPER_B(psubsb, FSUBSB)

SSE_HELPER_W(paddusw, FADDUW)
SSE_HELPER_W(paddsw, FADDSW)
SSE_HELPER_W(psubusw, FSUBUW)
SSE_HELPER_W(psubsw, FSUBSW)

SSE_HELPER_B(pminub, FMINUB)
SSE_HELPER_B(pmaxub, FMAXUB)

SSE_HELPER_W(pminsw, FMINSW)
SSE_HELPER_W(pmaxsw, FMAXSW)

SSE_HELPER_Q(pand, FAND)
SSE_HELPER_Q(pandn, FANDN)
SSE_HELPER_Q(por, FOR)
SSE_HELPER_Q(pxor, FXOR)

SSE_HELPER_B(pcmpgtb, FCMPGTB)
SSE_HELPER_W(pcmpgtw, FCMPGTW)
SSE_HELPER_L(pcmpgtl, FCMPGTL)

SSE_HELPER_B(pcmpeqb, FCMPEQ)
SSE_HELPER_W(pcmpeqw, FCMPEQ)
SSE_HELPER_L(pcmpeql, FCMPEQ)

SSE_HELPER_W(pmullw, FMULLW)
#if SHIFT == 0
SSE_HELPER_W(pmulhrw, FMULHRW)
#endif
SSE_HELPER_W(pmulhuw, FMULHUW)
SSE_HELPER_W(pmulhw, FMULHW)

SSE_HELPER_B(pavgb, FAVG)
SSE_HELPER_W(pavgw, FAVG)

DEF_HELPER_2(glue(pmuludq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmaddwd, SUFFIX), void, Reg, Reg)

DEF_HELPER_2(glue(psadbw, SUFFIX), void, Reg, Reg)
DEF_HELPER_3(glue(maskmov, SUFFIX), void, Reg, Reg, tl)
DEF_HELPER_2(glue(movl_mm_T0, SUFFIX), void, Reg, i32)
#ifdef TARGET_X86_64
DEF_HELPER_2(glue(movq_mm_T0, SUFFIX), void, Reg, i64)
#endif

#if SHIFT == 0
DEF_HELPER_3(glue(pshufw, SUFFIX), void, Reg, Reg, int)
#else
DEF_HELPER_3(shufps, void, Reg, Reg, int)
DEF_HELPER_3(shufpd, void, Reg, Reg, int)
DEF_HELPER_3(glue(pshufd, SUFFIX), void, Reg, Reg, int)
DEF_HELPER_3(glue(pshuflw, SUFFIX), void, Reg, Reg, int)
DEF_HELPER_3(glue(pshufhw, SUFFIX), void, Reg, Reg, int)
#endif

#if SHIFT == 1
/* FPU ops */
/* XXX: not accurate */

#define SSE_HELPER_S(name, F)\
    DEF_HELPER_2(name ## ps , void, Reg, Reg)        \
    DEF_HELPER_2(name ## ss , void, Reg, Reg)        \
    DEF_HELPER_2(name ## pd , void, Reg, Reg)        \
    DEF_HELPER_2(name ## sd , void, Reg, Reg)

SSE_HELPER_S(add, FPU_ADD)
SSE_HELPER_S(sub, FPU_SUB)
SSE_HELPER_S(mul, FPU_MUL)
SSE_HELPER_S(div, FPU_DIV)
SSE_HELPER_S(min, FPU_MIN)
SSE_HELPER_S(max, FPU_MAX)
SSE_HELPER_S(sqrt, FPU_SQRT)


DEF_HELPER_2(cvtps2pd, void, Reg, Reg)
DEF_HELPER_2(cvtpd2ps, void, Reg, Reg)
DEF_HELPER_2(cvtss2sd, void, Reg, Reg)
DEF_HELPER_2(cvtsd2ss, void, Reg, Reg)
DEF_HELPER_2(cvtdq2ps, void, Reg, Reg)
DEF_HELPER_2(cvtdq2pd, void, Reg, Reg)
DEF_HELPER_2(cvtpi2ps, void, XMMReg, MMXReg)
DEF_HELPER_2(cvtpi2pd, void, XMMReg, MMXReg)
DEF_HELPER_2(cvtsi2ss, void, XMMReg, i32)
DEF_HELPER_2(cvtsi2sd, void, XMMReg, i32)

#ifdef TARGET_X86_64
DEF_HELPER_2(cvtsq2ss, void, XMMReg, i64)
DEF_HELPER_2(cvtsq2sd, void, XMMReg, i64)
#endif

DEF_HELPER_2(cvtps2dq, void, XMMReg, XMMReg)
DEF_HELPER_2(cvtpd2dq, void, XMMReg, XMMReg)
DEF_HELPER_2(cvtps2pi, void, MMXReg, XMMReg)
DEF_HELPER_2(cvtpd2pi, void, MMXReg, XMMReg)
DEF_HELPER_1(cvtss2si, s32, XMMReg)
DEF_HELPER_1(cvtsd2si, s32, XMMReg)
#ifdef TARGET_X86_64
DEF_HELPER_1(cvtss2sq, s64, XMMReg)
DEF_HELPER_1(cvtsd2sq, s64, XMMReg)
#endif

DEF_HELPER_2(cvttps2dq, void, XMMReg, XMMReg)
DEF_HELPER_2(cvttpd2dq, void, XMMReg, XMMReg)
DEF_HELPER_2(cvttps2pi, void, MMXReg, XMMReg)
DEF_HELPER_2(cvttpd2pi, void, MMXReg, XMMReg)
DEF_HELPER_1(cvttss2si, s32, XMMReg)
DEF_HELPER_1(cvttsd2si, s32, XMMReg)
#ifdef TARGET_X86_64
DEF_HELPER_1(cvttss2sq, s64, XMMReg)
DEF_HELPER_1(cvttsd2sq, s64, XMMReg)
#endif

DEF_HELPER_2(rsqrtps, void, XMMReg, XMMReg)
DEF_HELPER_2(rsqrtss, void, XMMReg, XMMReg)
DEF_HELPER_2(rcpps, void, XMMReg, XMMReg)
DEF_HELPER_2(rcpss, void, XMMReg, XMMReg)
DEF_HELPER_2(haddps, void, XMMReg, XMMReg)
DEF_HELPER_2(haddpd, void, XMMReg, XMMReg)
DEF_HELPER_2(hsubps, void, XMMReg, XMMReg)
DEF_HELPER_2(hsubpd, void, XMMReg, XMMReg)
DEF_HELPER_2(addsubps, void, XMMReg, XMMReg)
DEF_HELPER_2(addsubpd, void, XMMReg, XMMReg)

#define SSE_HELPER_CMP(name, F)\
    DEF_HELPER_2( name ## ps , void, Reg, Reg)        \
    DEF_HELPER_2( name ## ss , void, Reg, Reg)        \
    DEF_HELPER_2( name ## pd , void, Reg, Reg)        \
    DEF_HELPER_2( name ## sd , void, Reg, Reg)

SSE_HELPER_CMP(cmpeq, FPU_CMPEQ)
SSE_HELPER_CMP(cmplt, FPU_CMPLT)
SSE_HELPER_CMP(cmple, FPU_CMPLE)
SSE_HELPER_CMP(cmpunord, FPU_CMPUNORD)
SSE_HELPER_CMP(cmpneq, FPU_CMPNEQ)
SSE_HELPER_CMP(cmpnlt, FPU_CMPNLT)
SSE_HELPER_CMP(cmpnle, FPU_CMPNLE)
SSE_HELPER_CMP(cmpord, FPU_CMPORD)

DEF_HELPER_2(ucomiss, void, Reg, Reg)
DEF_HELPER_2(comiss, void, Reg, Reg)
DEF_HELPER_2(ucomisd, void, Reg, Reg)
DEF_HELPER_2(comisd, void, Reg, Reg)
DEF_HELPER_1(movmskps, i32, Reg)
DEF_HELPER_1(movmskpd, i32, Reg)
#endif

DEF_HELPER_1(glue(pmovmskb, SUFFIX), i32, Reg)
DEF_HELPER_2(glue(packsswb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(packuswb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(packssdw, SUFFIX), void, Reg, Reg)
#define UNPCK_OP(base_name, base)                               \
    DEF_HELPER_2(glue(punpck ## base_name ## bw, SUFFIX) , void, Reg, Reg) \
    DEF_HELPER_2(glue(punpck ## base_name ## wd, SUFFIX) , void, Reg, Reg) \
    DEF_HELPER_2(glue(punpck ## base_name ## dq, SUFFIX) , void, Reg, Reg)

UNPCK_OP(l, 0)
UNPCK_OP(h, 1)

#if SHIFT == 1
DEF_HELPER_2(glue(punpcklqdq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(punpckhqdq, SUFFIX), void, Reg, Reg)
#endif

/* 3DNow! float ops */
#if SHIFT == 0
DEF_HELPER_2(pi2fd, void, MMXReg, MMXReg)
DEF_HELPER_2(pi2fw, void, MMXReg, MMXReg)
DEF_HELPER_2(pf2id, void, MMXReg, MMXReg)
DEF_HELPER_2(pf2iw, void, MMXReg, MMXReg)
DEF_HELPER_2(pfacc, void, MMXReg, MMXReg)
DEF_HELPER_2(pfadd, void, MMXReg, MMXReg)
DEF_HELPER_2(pfcmpeq, void, MMXReg, MMXReg)
DEF_HELPER_2(pfcmpge, void, MMXReg, MMXReg)
DEF_HELPER_2(pfcmpgt, void, MMXReg, MMXReg)
DEF_HELPER_2(pfmax, void, MMXReg, MMXReg)
DEF_HELPER_2(pfmin, void, MMXReg, MMXReg)
DEF_HELPER_2(pfmul, void, MMXReg, MMXReg)
DEF_HELPER_2(pfnacc, void, MMXReg, MMXReg)
DEF_HELPER_2(pfpnacc, void, MMXReg, MMXReg)
DEF_HELPER_2(pfrcp, void, MMXReg, MMXReg)
DEF_HELPER_2(pfrsqrt, void, MMXReg, MMXReg)
DEF_HELPER_2(pfsub, void, MMXReg, MMXReg)
DEF_HELPER_2(pfsubr, void, MMXReg, MMXReg)
DEF_HELPER_2(pswapd, void, MMXReg, MMXReg)
#endif

/* SSSE3 op helpers */
DEF_HELPER_2(glue(phaddw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(phaddd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(phaddsw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(phsubw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(phsubd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(phsubsw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pabsb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pabsw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pabsd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmaddubsw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmulhrsw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pshufb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psignb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psignw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(psignd, SUFFIX), void, Reg, Reg)
DEF_HELPER_3(glue(palignr, SUFFIX), void, Reg, Reg, s32)

/* SSE4.1 op helpers */
#if SHIFT == 1
DEF_HELPER_2(glue(pblendvb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(blendvps, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(blendvpd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(ptest, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovsxbw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovsxbd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovsxbq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovsxwd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovsxwq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovsxdq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovzxbw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovzxbd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovzxbq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovzxwd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovzxwq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmovzxdq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmuldq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pcmpeqq, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(packusdw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pminsb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pminsd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pminuw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pminud, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmaxsb, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmaxsd, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmaxuw, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmaxud, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(pmulld, SUFFIX), void, Reg, Reg)
DEF_HELPER_2(glue(phminposuw, SUFFIX), void, Reg, Reg)
DEF_HELPER_3(glue(roundps, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(roundpd, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(roundss, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(roundsd, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(blendps, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(blendpd, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(pblendw, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(dpps, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(dppd, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(mpsadbw, SUFFIX), void, Reg, Reg, i32)
#endif

/* SSE4.2 op helpers */
#if SHIFT == 1
DEF_HELPER_2(glue(pcmpgtq, SUFFIX), void, Reg, Reg)
DEF_HELPER_3(glue(pcmpestri, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(pcmpestrm, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(pcmpistri, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(glue(pcmpistrm, SUFFIX), void, Reg, Reg, i32)
DEF_HELPER_3(crc32, tl, i32, tl, i32)
DEF_HELPER_2(popcnt, tl, tl, i32)
#endif

#undef SHIFT
#undef Reg
#undef SUFFIX

#undef SSE_HELPER_B
#undef SSE_HELPER_W
#undef SSE_HELPER_L
#undef SSE_HELPER_Q
#undef SSE_HELPER_S
#undef SSE_HELPER_CMP
#undef UNPCK_OP
