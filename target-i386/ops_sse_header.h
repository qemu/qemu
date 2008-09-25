/*
 *  MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/PNI support
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#if SHIFT == 0
#define Reg MMXReg
#define SUFFIX _mmx
#else
#define Reg XMMReg
#define SUFFIX _xmm
#endif

DEF_HELPER(void, glue(helper_psrlw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psraw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psllw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psrld, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psrad, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pslld, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psrlq, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psllq, SUFFIX), (Reg *d, Reg *s))

#if SHIFT == 1
DEF_HELPER(void, glue(helper_psrldq, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pslldq, SUFFIX), (Reg *d, Reg *s))
#endif

#define SSE_HELPER_B(name, F)\
    DEF_HELPER(void, glue(name, SUFFIX), (Reg *d, Reg *s))

#define SSE_HELPER_W(name, F)\
    DEF_HELPER(void, glue(name, SUFFIX), (Reg *d, Reg *s))

#define SSE_HELPER_L(name, F)\
    DEF_HELPER(void, glue(name, SUFFIX), (Reg *d, Reg *s))

#define SSE_HELPER_Q(name, F)\
    DEF_HELPER(void, glue(name, SUFFIX), (Reg *d, Reg *s))

SSE_HELPER_B(helper_paddb, FADD)
SSE_HELPER_W(helper_paddw, FADD)
SSE_HELPER_L(helper_paddl, FADD)
SSE_HELPER_Q(helper_paddq, FADD)

SSE_HELPER_B(helper_psubb, FSUB)
SSE_HELPER_W(helper_psubw, FSUB)
SSE_HELPER_L(helper_psubl, FSUB)
SSE_HELPER_Q(helper_psubq, FSUB)

SSE_HELPER_B(helper_paddusb, FADDUB)
SSE_HELPER_B(helper_paddsb, FADDSB)
SSE_HELPER_B(helper_psubusb, FSUBUB)
SSE_HELPER_B(helper_psubsb, FSUBSB)

SSE_HELPER_W(helper_paddusw, FADDUW)
SSE_HELPER_W(helper_paddsw, FADDSW)
SSE_HELPER_W(helper_psubusw, FSUBUW)
SSE_HELPER_W(helper_psubsw, FSUBSW)

SSE_HELPER_B(helper_pminub, FMINUB)
SSE_HELPER_B(helper_pmaxub, FMAXUB)

SSE_HELPER_W(helper_pminsw, FMINSW)
SSE_HELPER_W(helper_pmaxsw, FMAXSW)

SSE_HELPER_Q(helper_pand, FAND)
SSE_HELPER_Q(helper_pandn, FANDN)
SSE_HELPER_Q(helper_por, FOR)
SSE_HELPER_Q(helper_pxor, FXOR)

SSE_HELPER_B(helper_pcmpgtb, FCMPGTB)
SSE_HELPER_W(helper_pcmpgtw, FCMPGTW)
SSE_HELPER_L(helper_pcmpgtl, FCMPGTL)

SSE_HELPER_B(helper_pcmpeqb, FCMPEQ)
SSE_HELPER_W(helper_pcmpeqw, FCMPEQ)
SSE_HELPER_L(helper_pcmpeql, FCMPEQ)

SSE_HELPER_W(helper_pmullw, FMULLW)
#if SHIFT == 0
SSE_HELPER_W(helper_pmulhrw, FMULHRW)
#endif
SSE_HELPER_W(helper_pmulhuw, FMULHUW)
SSE_HELPER_W(helper_pmulhw, FMULHW)

SSE_HELPER_B(helper_pavgb, FAVG)
SSE_HELPER_W(helper_pavgw, FAVG)

DEF_HELPER(void, glue(helper_pmuludq, SUFFIX) , (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pmaddwd, SUFFIX) , (Reg *d, Reg *s))

DEF_HELPER(void, glue(helper_psadbw, SUFFIX) , (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_maskmov, SUFFIX) , (Reg *d, Reg *s, target_ulong a0))
DEF_HELPER(void, glue(helper_movl_mm_T0, SUFFIX) , (Reg *d, uint32_t val))
#ifdef TARGET_X86_64
DEF_HELPER(void, glue(helper_movq_mm_T0, SUFFIX) , (Reg *d, uint64_t val))
#endif

#if SHIFT == 0
DEF_HELPER(void, glue(helper_pshufw, SUFFIX) , (Reg *d, Reg *s, int order))
#else
DEF_HELPER(void, helper_shufps, (Reg *d, Reg *s, int order))
DEF_HELPER(void, helper_shufpd, (Reg *d, Reg *s, int order))
DEF_HELPER(void, glue(helper_pshufd, SUFFIX) , (Reg *d, Reg *s, int order))
DEF_HELPER(void, glue(helper_pshuflw, SUFFIX) , (Reg *d, Reg *s, int order))
DEF_HELPER(void, glue(helper_pshufhw, SUFFIX) , (Reg *d, Reg *s, int order))
#endif

#if SHIFT == 1
/* FPU ops */
/* XXX: not accurate */

#define SSE_HELPER_S(name, F)\
    DEF_HELPER(void, helper_ ## name ## ps , (Reg *d, Reg *s))        \
    DEF_HELPER(void, helper_ ## name ## ss , (Reg *d, Reg *s))        \
    DEF_HELPER(void, helper_ ## name ## pd , (Reg *d, Reg *s))        \
    DEF_HELPER(void, helper_ ## name ## sd , (Reg *d, Reg *s))

SSE_HELPER_S(add, FPU_ADD)
SSE_HELPER_S(sub, FPU_SUB)
SSE_HELPER_S(mul, FPU_MUL)
SSE_HELPER_S(div, FPU_DIV)
SSE_HELPER_S(min, FPU_MIN)
SSE_HELPER_S(max, FPU_MAX)
SSE_HELPER_S(sqrt, FPU_SQRT)


DEF_HELPER(void, helper_cvtps2pd, (Reg *d, Reg *s))
DEF_HELPER(void, helper_cvtpd2ps, (Reg *d, Reg *s))
DEF_HELPER(void, helper_cvtss2sd, (Reg *d, Reg *s))
DEF_HELPER(void, helper_cvtsd2ss, (Reg *d, Reg *s))
DEF_HELPER(void, helper_cvtdq2ps, (Reg *d, Reg *s))
DEF_HELPER(void, helper_cvtdq2pd, (Reg *d, Reg *s))
DEF_HELPER(void, helper_cvtpi2ps, (XMMReg *d, MMXReg *s))
DEF_HELPER(void, helper_cvtpi2pd, (XMMReg *d, MMXReg *s))
DEF_HELPER(void, helper_cvtsi2ss, (XMMReg *d, uint32_t val))
DEF_HELPER(void, helper_cvtsi2sd, (XMMReg *d, uint32_t val))

#ifdef TARGET_X86_64
DEF_HELPER(void, helper_cvtsq2ss, (XMMReg *d, uint64_t val))
DEF_HELPER(void, helper_cvtsq2sd, (XMMReg *d, uint64_t val))
#endif

DEF_HELPER(void, helper_cvtps2dq, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_cvtpd2dq, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_cvtps2pi, (MMXReg *d, XMMReg *s))
DEF_HELPER(void, helper_cvtpd2pi, (MMXReg *d, XMMReg *s))
DEF_HELPER(int32_t, helper_cvtss2si, (XMMReg *s))
DEF_HELPER(int32_t, helper_cvtsd2si, (XMMReg *s))
#ifdef TARGET_X86_64
DEF_HELPER(int64_t, helper_cvtss2sq, (XMMReg *s))
DEF_HELPER(int64_t, helper_cvtsd2sq, (XMMReg *s))
#endif

DEF_HELPER(void, helper_cvttps2dq, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_cvttpd2dq, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_cvttps2pi, (MMXReg *d, XMMReg *s))
DEF_HELPER(void, helper_cvttpd2pi, (MMXReg *d, XMMReg *s))
DEF_HELPER(int32_t, helper_cvttss2si, (XMMReg *s))
DEF_HELPER(int32_t, helper_cvttsd2si, (XMMReg *s))
#ifdef TARGET_X86_64
DEF_HELPER(int64_t, helper_cvttss2sq, (XMMReg *s))
DEF_HELPER(int64_t, helper_cvttsd2sq, (XMMReg *s))
#endif

DEF_HELPER(void, helper_rsqrtps, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_rsqrtss, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_rcpps, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_rcpss, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_haddps, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_haddpd, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_hsubps, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_hsubpd, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_addsubps, (XMMReg *d, XMMReg *s))
DEF_HELPER(void, helper_addsubpd, (XMMReg *d, XMMReg *s))

#define SSE_HELPER_CMP(name, F)\
    DEF_HELPER(void, helper_ ## name ## ps , (Reg *d, Reg *s))        \
    DEF_HELPER(void, helper_ ## name ## ss , (Reg *d, Reg *s))        \
    DEF_HELPER(void, helper_ ## name ## pd , (Reg *d, Reg *s))        \
    DEF_HELPER(void, helper_ ## name ## sd , (Reg *d, Reg *s))

SSE_HELPER_CMP(cmpeq, FPU_CMPEQ)
SSE_HELPER_CMP(cmplt, FPU_CMPLT)
SSE_HELPER_CMP(cmple, FPU_CMPLE)
SSE_HELPER_CMP(cmpunord, FPU_CMPUNORD)
SSE_HELPER_CMP(cmpneq, FPU_CMPNEQ)
SSE_HELPER_CMP(cmpnlt, FPU_CMPNLT)
SSE_HELPER_CMP(cmpnle, FPU_CMPNLE)
SSE_HELPER_CMP(cmpord, FPU_CMPORD)

DEF_HELPER(void, helper_ucomiss, (Reg *d, Reg *s))
DEF_HELPER(void, helper_comiss, (Reg *d, Reg *s))
DEF_HELPER(void, helper_ucomisd, (Reg *d, Reg *s))
DEF_HELPER(void, helper_comisd, (Reg *d, Reg *s))
DEF_HELPER(uint32_t, helper_movmskps, (Reg *s))
DEF_HELPER(uint32_t, helper_movmskpd, (Reg *s))
#endif

DEF_HELPER(uint32_t, glue(helper_pmovmskb, SUFFIX), (Reg *s))
DEF_HELPER(void, glue(helper_packsswb, SUFFIX) , (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_packuswb, SUFFIX) , (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_packssdw, SUFFIX) , (Reg *d, Reg *s))
#define UNPCK_OP(base_name, base)                               \
    DEF_HELPER(void, glue(helper_punpck ## base_name ## bw, SUFFIX) , (Reg *d, Reg *s)) \
    DEF_HELPER(void, glue(helper_punpck ## base_name ## wd, SUFFIX) , (Reg *d, Reg *s)) \
    DEF_HELPER(void, glue(helper_punpck ## base_name ## dq, SUFFIX) , (Reg *d, Reg *s))

UNPCK_OP(l, 0)
UNPCK_OP(h, 1)

#if SHIFT == 1
DEF_HELPER(void, glue(helper_punpcklqdq, SUFFIX) , (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_punpckhqdq, SUFFIX) , (Reg *d, Reg *s))
#endif

/* 3DNow! float ops */
#if SHIFT == 0
DEF_HELPER(void, helper_pi2fd, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pi2fw, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pf2id, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pf2iw, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfacc, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfadd, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfcmpeq, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfcmpge, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfcmpgt, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfmax, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfmin, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfmul, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfnacc, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfpnacc, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfrcp, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfrsqrt, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfsub, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pfsubr, (MMXReg *d, MMXReg *s))
DEF_HELPER(void, helper_pswapd, (MMXReg *d, MMXReg *s))
#endif

/* SSSE3 op helpers */
DEF_HELPER(void, glue(helper_phaddw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_phaddd, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_phaddsw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_phsubw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_phsubd, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_phsubsw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pabsb, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pabsw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pabsd, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pmaddubsw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pmulhrsw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_pshufb, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psignb, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psignw, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_psignd, SUFFIX), (Reg *d, Reg *s))
DEF_HELPER(void, glue(helper_palignr, SUFFIX), (Reg *d, Reg *s, int32_t shift))

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
