/*
 *  MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI support
 *
 *  Copyright (c) 2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#if SHIFT == 0
#define Reg MMXReg
#define SUFFIX _mmx
#else
#define Reg ZMMReg
#define SUFFIX _xmm
#endif

#define dh_alias_Reg ptr
#define dh_alias_ZMMReg ptr
#define dh_alias_MMXReg ptr
#define dh_ctype_Reg Reg *
#define dh_ctype_ZMMReg ZMMReg *
#define dh_ctype_MMXReg MMXReg *
#define dh_is_signed_Reg dh_is_signed_ptr
#define dh_is_signed_ZMMReg dh_is_signed_ptr
#define dh_is_signed_MMXReg dh_is_signed_ptr

DEF_HELPER_3(glue(psrlw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psraw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psllw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psrld, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psrad, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pslld, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psrlq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psllq, SUFFIX), void, env, Reg, Reg)

#if SHIFT == 1
DEF_HELPER_3(glue(psrldq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pslldq, SUFFIX), void, env, Reg, Reg)
#endif

#define SSE_HELPER_B(name, F)\
    DEF_HELPER_3(glue(name, SUFFIX), void, env, Reg, Reg)

#define SSE_HELPER_W(name, F)\
    DEF_HELPER_3(glue(name, SUFFIX), void, env, Reg, Reg)

#define SSE_HELPER_L(name, F)\
    DEF_HELPER_3(glue(name, SUFFIX), void, env, Reg, Reg)

#define SSE_HELPER_Q(name, F)\
    DEF_HELPER_3(glue(name, SUFFIX), void, env, Reg, Reg)

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

DEF_HELPER_3(glue(pmuludq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmaddwd, SUFFIX), void, env, Reg, Reg)

DEF_HELPER_3(glue(psadbw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_4(glue(maskmov, SUFFIX), void, env, Reg, Reg, tl)
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

#define SSE_HELPER_S(name, F)                            \
    DEF_HELPER_3(name ## ps, void, env, Reg, Reg)        \
    DEF_HELPER_3(name ## ss, void, env, Reg, Reg)        \
    DEF_HELPER_3(name ## pd, void, env, Reg, Reg)        \
    DEF_HELPER_3(name ## sd, void, env, Reg, Reg)

SSE_HELPER_S(add, FPU_ADD)
SSE_HELPER_S(sub, FPU_SUB)
SSE_HELPER_S(mul, FPU_MUL)
SSE_HELPER_S(div, FPU_DIV)
SSE_HELPER_S(min, FPU_MIN)
SSE_HELPER_S(max, FPU_MAX)
SSE_HELPER_S(sqrt, FPU_SQRT)


DEF_HELPER_3(cvtps2pd, void, env, Reg, Reg)
DEF_HELPER_3(cvtpd2ps, void, env, Reg, Reg)
DEF_HELPER_3(cvtss2sd, void, env, Reg, Reg)
DEF_HELPER_3(cvtsd2ss, void, env, Reg, Reg)
DEF_HELPER_3(cvtdq2ps, void, env, Reg, Reg)
DEF_HELPER_3(cvtdq2pd, void, env, Reg, Reg)
DEF_HELPER_3(cvtpi2ps, void, env, ZMMReg, MMXReg)
DEF_HELPER_3(cvtpi2pd, void, env, ZMMReg, MMXReg)
DEF_HELPER_3(cvtsi2ss, void, env, ZMMReg, i32)
DEF_HELPER_3(cvtsi2sd, void, env, ZMMReg, i32)

#ifdef TARGET_X86_64
DEF_HELPER_3(cvtsq2ss, void, env, ZMMReg, i64)
DEF_HELPER_3(cvtsq2sd, void, env, ZMMReg, i64)
#endif

DEF_HELPER_3(cvtps2dq, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(cvtpd2dq, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(cvtps2pi, void, env, MMXReg, ZMMReg)
DEF_HELPER_3(cvtpd2pi, void, env, MMXReg, ZMMReg)
DEF_HELPER_2(cvtss2si, s32, env, ZMMReg)
DEF_HELPER_2(cvtsd2si, s32, env, ZMMReg)
#ifdef TARGET_X86_64
DEF_HELPER_2(cvtss2sq, s64, env, ZMMReg)
DEF_HELPER_2(cvtsd2sq, s64, env, ZMMReg)
#endif

DEF_HELPER_3(cvttps2dq, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(cvttpd2dq, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(cvttps2pi, void, env, MMXReg, ZMMReg)
DEF_HELPER_3(cvttpd2pi, void, env, MMXReg, ZMMReg)
DEF_HELPER_2(cvttss2si, s32, env, ZMMReg)
DEF_HELPER_2(cvttsd2si, s32, env, ZMMReg)
#ifdef TARGET_X86_64
DEF_HELPER_2(cvttss2sq, s64, env, ZMMReg)
DEF_HELPER_2(cvttsd2sq, s64, env, ZMMReg)
#endif

DEF_HELPER_3(rsqrtps, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(rsqrtss, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(rcpps, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(rcpss, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(extrq_r, void, env, ZMMReg, ZMMReg)
DEF_HELPER_4(extrq_i, void, env, ZMMReg, int, int)
DEF_HELPER_3(insertq_r, void, env, ZMMReg, ZMMReg)
DEF_HELPER_4(insertq_i, void, env, ZMMReg, int, int)
DEF_HELPER_3(haddps, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(haddpd, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(hsubps, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(hsubpd, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(addsubps, void, env, ZMMReg, ZMMReg)
DEF_HELPER_3(addsubpd, void, env, ZMMReg, ZMMReg)

#define SSE_HELPER_CMP(name, F)                           \
    DEF_HELPER_3(name ## ps, void, env, Reg, Reg)         \
    DEF_HELPER_3(name ## ss, void, env, Reg, Reg)         \
    DEF_HELPER_3(name ## pd, void, env, Reg, Reg)         \
    DEF_HELPER_3(name ## sd, void, env, Reg, Reg)

SSE_HELPER_CMP(cmpeq, FPU_CMPEQ)
SSE_HELPER_CMP(cmplt, FPU_CMPLT)
SSE_HELPER_CMP(cmple, FPU_CMPLE)
SSE_HELPER_CMP(cmpunord, FPU_CMPUNORD)
SSE_HELPER_CMP(cmpneq, FPU_CMPNEQ)
SSE_HELPER_CMP(cmpnlt, FPU_CMPNLT)
SSE_HELPER_CMP(cmpnle, FPU_CMPNLE)
SSE_HELPER_CMP(cmpord, FPU_CMPORD)

DEF_HELPER_3(ucomiss, void, env, Reg, Reg)
DEF_HELPER_3(comiss, void, env, Reg, Reg)
DEF_HELPER_3(ucomisd, void, env, Reg, Reg)
DEF_HELPER_3(comisd, void, env, Reg, Reg)
DEF_HELPER_2(movmskps, i32, env, Reg)
DEF_HELPER_2(movmskpd, i32, env, Reg)
#endif

DEF_HELPER_2(glue(pmovmskb, SUFFIX), i32, env, Reg)
DEF_HELPER_3(glue(packsswb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(packuswb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(packssdw, SUFFIX), void, env, Reg, Reg)
#define UNPCK_OP(base_name, base)                                       \
    DEF_HELPER_3(glue(punpck ## base_name ## bw, SUFFIX), void, env, Reg, Reg) \
    DEF_HELPER_3(glue(punpck ## base_name ## wd, SUFFIX), void, env, Reg, Reg) \
    DEF_HELPER_3(glue(punpck ## base_name ## dq, SUFFIX), void, env, Reg, Reg)

UNPCK_OP(l, 0)
UNPCK_OP(h, 1)

#if SHIFT == 1
DEF_HELPER_3(glue(punpcklqdq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(punpckhqdq, SUFFIX), void, env, Reg, Reg)
#endif

/* 3DNow! float ops */
#if SHIFT == 0
DEF_HELPER_3(pi2fd, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pi2fw, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pf2id, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pf2iw, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfacc, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfadd, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfcmpeq, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfcmpge, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfcmpgt, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfmax, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfmin, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfmul, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfnacc, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfpnacc, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfrcp, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfrsqrt, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfsub, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pfsubr, void, env, MMXReg, MMXReg)
DEF_HELPER_3(pswapd, void, env, MMXReg, MMXReg)
#endif

/* SSSE3 op helpers */
DEF_HELPER_3(glue(phaddw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(phaddd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(phaddsw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(phsubw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(phsubd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(phsubsw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pabsb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pabsw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pabsd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmaddubsw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmulhrsw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pshufb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psignb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psignw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(psignd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_4(glue(palignr, SUFFIX), void, env, Reg, Reg, s32)

/* SSE4.1 op helpers */
#if SHIFT == 1
DEF_HELPER_3(glue(pblendvb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(blendvps, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(blendvpd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(ptest, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovsxbw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovsxbd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovsxbq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovsxwd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovsxwq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovsxdq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovzxbw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovzxbd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovzxbq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovzxwd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovzxwq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmovzxdq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmuldq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pcmpeqq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(packusdw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pminsb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pminsd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pminuw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pminud, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmaxsb, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmaxsd, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmaxuw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmaxud, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(pmulld, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(phminposuw, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_4(glue(roundps, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(roundpd, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(roundss, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(roundsd, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(blendps, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(blendpd, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(pblendw, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(dpps, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(dppd, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(mpsadbw, SUFFIX), void, env, Reg, Reg, i32)
#endif

/* SSE4.2 op helpers */
#if SHIFT == 1
DEF_HELPER_3(glue(pcmpgtq, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_4(glue(pcmpestri, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(pcmpestrm, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(pcmpistri, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(pcmpistrm, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_3(crc32, tl, i32, tl, i32)
#endif

/* AES-NI op helpers */
#if SHIFT == 1
DEF_HELPER_3(glue(aesdec, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(aesdeclast, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(aesenc, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(aesenclast, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_3(glue(aesimc, SUFFIX), void, env, Reg, Reg)
DEF_HELPER_4(glue(aeskeygenassist, SUFFIX), void, env, Reg, Reg, i32)
DEF_HELPER_4(glue(pclmulqdq, SUFFIX), void, env, Reg, Reg, i32)
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
