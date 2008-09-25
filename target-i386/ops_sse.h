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
#define XMM_ONLY(x...)
#define B(n) MMX_B(n)
#define W(n) MMX_W(n)
#define L(n) MMX_L(n)
#define Q(n) q
#define SUFFIX _mmx
#else
#define Reg XMMReg
#define XMM_ONLY(x...) x
#define B(n) XMM_B(n)
#define W(n) XMM_W(n)
#define L(n) XMM_L(n)
#define Q(n) XMM_Q(n)
#define SUFFIX _xmm
#endif

void glue(helper_psrlw, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 15) {
        d->Q(0) = 0;
#if SHIFT == 1
        d->Q(1) = 0;
#endif
    } else {
        shift = s->B(0);
        d->W(0) >>= shift;
        d->W(1) >>= shift;
        d->W(2) >>= shift;
        d->W(3) >>= shift;
#if SHIFT == 1
        d->W(4) >>= shift;
        d->W(5) >>= shift;
        d->W(6) >>= shift;
        d->W(7) >>= shift;
#endif
    }
    FORCE_RET();
}

void glue(helper_psraw, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 15) {
        shift = 15;
    } else {
        shift = s->B(0);
    }
    d->W(0) = (int16_t)d->W(0) >> shift;
    d->W(1) = (int16_t)d->W(1) >> shift;
    d->W(2) = (int16_t)d->W(2) >> shift;
    d->W(3) = (int16_t)d->W(3) >> shift;
#if SHIFT == 1
    d->W(4) = (int16_t)d->W(4) >> shift;
    d->W(5) = (int16_t)d->W(5) >> shift;
    d->W(6) = (int16_t)d->W(6) >> shift;
    d->W(7) = (int16_t)d->W(7) >> shift;
#endif
}

void glue(helper_psllw, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 15) {
        d->Q(0) = 0;
#if SHIFT == 1
        d->Q(1) = 0;
#endif
    } else {
        shift = s->B(0);
        d->W(0) <<= shift;
        d->W(1) <<= shift;
        d->W(2) <<= shift;
        d->W(3) <<= shift;
#if SHIFT == 1
        d->W(4) <<= shift;
        d->W(5) <<= shift;
        d->W(6) <<= shift;
        d->W(7) <<= shift;
#endif
    }
    FORCE_RET();
}

void glue(helper_psrld, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 31) {
        d->Q(0) = 0;
#if SHIFT == 1
        d->Q(1) = 0;
#endif
    } else {
        shift = s->B(0);
        d->L(0) >>= shift;
        d->L(1) >>= shift;
#if SHIFT == 1
        d->L(2) >>= shift;
        d->L(3) >>= shift;
#endif
    }
    FORCE_RET();
}

void glue(helper_psrad, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 31) {
        shift = 31;
    } else {
        shift = s->B(0);
    }
    d->L(0) = (int32_t)d->L(0) >> shift;
    d->L(1) = (int32_t)d->L(1) >> shift;
#if SHIFT == 1
    d->L(2) = (int32_t)d->L(2) >> shift;
    d->L(3) = (int32_t)d->L(3) >> shift;
#endif
}

void glue(helper_pslld, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 31) {
        d->Q(0) = 0;
#if SHIFT == 1
        d->Q(1) = 0;
#endif
    } else {
        shift = s->B(0);
        d->L(0) <<= shift;
        d->L(1) <<= shift;
#if SHIFT == 1
        d->L(2) <<= shift;
        d->L(3) <<= shift;
#endif
    }
    FORCE_RET();
}

void glue(helper_psrlq, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 63) {
        d->Q(0) = 0;
#if SHIFT == 1
        d->Q(1) = 0;
#endif
    } else {
        shift = s->B(0);
        d->Q(0) >>= shift;
#if SHIFT == 1
        d->Q(1) >>= shift;
#endif
    }
    FORCE_RET();
}

void glue(helper_psllq, SUFFIX)(Reg *d, Reg *s)
{
    int shift;

    if (s->Q(0) > 63) {
        d->Q(0) = 0;
#if SHIFT == 1
        d->Q(1) = 0;
#endif
    } else {
        shift = s->B(0);
        d->Q(0) <<= shift;
#if SHIFT == 1
        d->Q(1) <<= shift;
#endif
    }
    FORCE_RET();
}

#if SHIFT == 1
void glue(helper_psrldq, SUFFIX)(Reg *d, Reg *s)
{
    int shift, i;

    shift = s->L(0);
    if (shift > 16)
        shift = 16;
    for(i = 0; i < 16 - shift; i++)
        d->B(i) = d->B(i + shift);
    for(i = 16 - shift; i < 16; i++)
        d->B(i) = 0;
    FORCE_RET();
}

void glue(helper_pslldq, SUFFIX)(Reg *d, Reg *s)
{
    int shift, i;

    shift = s->L(0);
    if (shift > 16)
        shift = 16;
    for(i = 15; i >= shift; i--)
        d->B(i) = d->B(i - shift);
    for(i = 0; i < shift; i++)
        d->B(i) = 0;
    FORCE_RET();
}
#endif

#define SSE_HELPER_B(name, F)\
void glue(name, SUFFIX) (Reg *d, Reg *s)\
{\
    d->B(0) = F(d->B(0), s->B(0));\
    d->B(1) = F(d->B(1), s->B(1));\
    d->B(2) = F(d->B(2), s->B(2));\
    d->B(3) = F(d->B(3), s->B(3));\
    d->B(4) = F(d->B(4), s->B(4));\
    d->B(5) = F(d->B(5), s->B(5));\
    d->B(6) = F(d->B(6), s->B(6));\
    d->B(7) = F(d->B(7), s->B(7));\
    XMM_ONLY(\
    d->B(8) = F(d->B(8), s->B(8));\
    d->B(9) = F(d->B(9), s->B(9));\
    d->B(10) = F(d->B(10), s->B(10));\
    d->B(11) = F(d->B(11), s->B(11));\
    d->B(12) = F(d->B(12), s->B(12));\
    d->B(13) = F(d->B(13), s->B(13));\
    d->B(14) = F(d->B(14), s->B(14));\
    d->B(15) = F(d->B(15), s->B(15));\
    )\
}

#define SSE_HELPER_W(name, F)\
void glue(name, SUFFIX) (Reg *d, Reg *s)\
{\
    d->W(0) = F(d->W(0), s->W(0));\
    d->W(1) = F(d->W(1), s->W(1));\
    d->W(2) = F(d->W(2), s->W(2));\
    d->W(3) = F(d->W(3), s->W(3));\
    XMM_ONLY(\
    d->W(4) = F(d->W(4), s->W(4));\
    d->W(5) = F(d->W(5), s->W(5));\
    d->W(6) = F(d->W(6), s->W(6));\
    d->W(7) = F(d->W(7), s->W(7));\
    )\
}

#define SSE_HELPER_L(name, F)\
void glue(name, SUFFIX) (Reg *d, Reg *s)\
{\
    d->L(0) = F(d->L(0), s->L(0));\
    d->L(1) = F(d->L(1), s->L(1));\
    XMM_ONLY(\
    d->L(2) = F(d->L(2), s->L(2));\
    d->L(3) = F(d->L(3), s->L(3));\
    )\
}

#define SSE_HELPER_Q(name, F)\
void glue(name, SUFFIX) (Reg *d, Reg *s)\
{\
    d->Q(0) = F(d->Q(0), s->Q(0));\
    XMM_ONLY(\
    d->Q(1) = F(d->Q(1), s->Q(1));\
    )\
}

#if SHIFT == 0
static inline int satub(int x)
{
    if (x < 0)
        return 0;
    else if (x > 255)
        return 255;
    else
        return x;
}

static inline int satuw(int x)
{
    if (x < 0)
        return 0;
    else if (x > 65535)
        return 65535;
    else
        return x;
}

static inline int satsb(int x)
{
    if (x < -128)
        return -128;
    else if (x > 127)
        return 127;
    else
        return x;
}

static inline int satsw(int x)
{
    if (x < -32768)
        return -32768;
    else if (x > 32767)
        return 32767;
    else
        return x;
}

#define FADD(a, b) ((a) + (b))
#define FADDUB(a, b) satub((a) + (b))
#define FADDUW(a, b) satuw((a) + (b))
#define FADDSB(a, b) satsb((int8_t)(a) + (int8_t)(b))
#define FADDSW(a, b) satsw((int16_t)(a) + (int16_t)(b))

#define FSUB(a, b) ((a) - (b))
#define FSUBUB(a, b) satub((a) - (b))
#define FSUBUW(a, b) satuw((a) - (b))
#define FSUBSB(a, b) satsb((int8_t)(a) - (int8_t)(b))
#define FSUBSW(a, b) satsw((int16_t)(a) - (int16_t)(b))
#define FMINUB(a, b) ((a) < (b)) ? (a) : (b)
#define FMINSW(a, b) ((int16_t)(a) < (int16_t)(b)) ? (a) : (b)
#define FMAXUB(a, b) ((a) > (b)) ? (a) : (b)
#define FMAXSW(a, b) ((int16_t)(a) > (int16_t)(b)) ? (a) : (b)

#define FAND(a, b) (a) & (b)
#define FANDN(a, b) ((~(a)) & (b))
#define FOR(a, b) (a) | (b)
#define FXOR(a, b) (a) ^ (b)

#define FCMPGTB(a, b) (int8_t)(a) > (int8_t)(b) ? -1 : 0
#define FCMPGTW(a, b) (int16_t)(a) > (int16_t)(b) ? -1 : 0
#define FCMPGTL(a, b) (int32_t)(a) > (int32_t)(b) ? -1 : 0
#define FCMPEQ(a, b) (a) == (b) ? -1 : 0

#define FMULLW(a, b) (a) * (b)
#define FMULHRW(a, b) ((int16_t)(a) * (int16_t)(b) + 0x8000) >> 16
#define FMULHUW(a, b) (a) * (b) >> 16
#define FMULHW(a, b) (int16_t)(a) * (int16_t)(b) >> 16

#define FAVG(a, b) ((a) + (b) + 1) >> 1
#endif

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

void glue(helper_pmuludq, SUFFIX) (Reg *d, Reg *s)
{
    d->Q(0) = (uint64_t)s->L(0) * (uint64_t)d->L(0);
#if SHIFT == 1
    d->Q(1) = (uint64_t)s->L(2) * (uint64_t)d->L(2);
#endif
}

void glue(helper_pmaddwd, SUFFIX) (Reg *d, Reg *s)
{
    int i;

    for(i = 0; i < (2 << SHIFT); i++) {
        d->L(i) = (int16_t)s->W(2*i) * (int16_t)d->W(2*i) +
            (int16_t)s->W(2*i+1) * (int16_t)d->W(2*i+1);
    }
    FORCE_RET();
}

#if SHIFT == 0
static inline int abs1(int a)
{
    if (a < 0)
        return -a;
    else
        return a;
}
#endif
void glue(helper_psadbw, SUFFIX) (Reg *d, Reg *s)
{
    unsigned int val;

    val = 0;
    val += abs1(d->B(0) - s->B(0));
    val += abs1(d->B(1) - s->B(1));
    val += abs1(d->B(2) - s->B(2));
    val += abs1(d->B(3) - s->B(3));
    val += abs1(d->B(4) - s->B(4));
    val += abs1(d->B(5) - s->B(5));
    val += abs1(d->B(6) - s->B(6));
    val += abs1(d->B(7) - s->B(7));
    d->Q(0) = val;
#if SHIFT == 1
    val = 0;
    val += abs1(d->B(8) - s->B(8));
    val += abs1(d->B(9) - s->B(9));
    val += abs1(d->B(10) - s->B(10));
    val += abs1(d->B(11) - s->B(11));
    val += abs1(d->B(12) - s->B(12));
    val += abs1(d->B(13) - s->B(13));
    val += abs1(d->B(14) - s->B(14));
    val += abs1(d->B(15) - s->B(15));
    d->Q(1) = val;
#endif
}

void glue(helper_maskmov, SUFFIX) (Reg *d, Reg *s, target_ulong a0)
{
    int i;
    for(i = 0; i < (8 << SHIFT); i++) {
        if (s->B(i) & 0x80)
            stb(a0 + i, d->B(i));
    }
    FORCE_RET();
}

void glue(helper_movl_mm_T0, SUFFIX) (Reg *d, uint32_t val)
{
    d->L(0) = val;
    d->L(1) = 0;
#if SHIFT == 1
    d->Q(1) = 0;
#endif
}

#ifdef TARGET_X86_64
void glue(helper_movq_mm_T0, SUFFIX) (Reg *d, uint64_t val)
{
    d->Q(0) = val;
#if SHIFT == 1
    d->Q(1) = 0;
#endif
}
#endif

#if SHIFT == 0
void glue(helper_pshufw, SUFFIX) (Reg *d, Reg *s, int order)
{
    Reg r;
    r.W(0) = s->W(order & 3);
    r.W(1) = s->W((order >> 2) & 3);
    r.W(2) = s->W((order >> 4) & 3);
    r.W(3) = s->W((order >> 6) & 3);
    *d = r;
}
#else
void helper_shufps(Reg *d, Reg *s, int order)
{
    Reg r;
    r.L(0) = d->L(order & 3);
    r.L(1) = d->L((order >> 2) & 3);
    r.L(2) = s->L((order >> 4) & 3);
    r.L(3) = s->L((order >> 6) & 3);
    *d = r;
}

void helper_shufpd(Reg *d, Reg *s, int order)
{
    Reg r;
    r.Q(0) = d->Q(order & 1);
    r.Q(1) = s->Q((order >> 1) & 1);
    *d = r;
}

void glue(helper_pshufd, SUFFIX) (Reg *d, Reg *s, int order)
{
    Reg r;
    r.L(0) = s->L(order & 3);
    r.L(1) = s->L((order >> 2) & 3);
    r.L(2) = s->L((order >> 4) & 3);
    r.L(3) = s->L((order >> 6) & 3);
    *d = r;
}

void glue(helper_pshuflw, SUFFIX) (Reg *d, Reg *s, int order)
{
    Reg r;
    r.W(0) = s->W(order & 3);
    r.W(1) = s->W((order >> 2) & 3);
    r.W(2) = s->W((order >> 4) & 3);
    r.W(3) = s->W((order >> 6) & 3);
    r.Q(1) = s->Q(1);
    *d = r;
}

void glue(helper_pshufhw, SUFFIX) (Reg *d, Reg *s, int order)
{
    Reg r;
    r.Q(0) = s->Q(0);
    r.W(4) = s->W(4 + (order & 3));
    r.W(5) = s->W(4 + ((order >> 2) & 3));
    r.W(6) = s->W(4 + ((order >> 4) & 3));
    r.W(7) = s->W(4 + ((order >> 6) & 3));
    *d = r;
}
#endif

#if SHIFT == 1
/* FPU ops */
/* XXX: not accurate */

#define SSE_HELPER_S(name, F)\
void helper_ ## name ## ps (Reg *d, Reg *s)\
{\
    d->XMM_S(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
    d->XMM_S(1) = F(32, d->XMM_S(1), s->XMM_S(1));\
    d->XMM_S(2) = F(32, d->XMM_S(2), s->XMM_S(2));\
    d->XMM_S(3) = F(32, d->XMM_S(3), s->XMM_S(3));\
}\
\
void helper_ ## name ## ss (Reg *d, Reg *s)\
{\
    d->XMM_S(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
}\
void helper_ ## name ## pd (Reg *d, Reg *s)\
{\
    d->XMM_D(0) = F(64, d->XMM_D(0), s->XMM_D(0));\
    d->XMM_D(1) = F(64, d->XMM_D(1), s->XMM_D(1));\
}\
\
void helper_ ## name ## sd (Reg *d, Reg *s)\
{\
    d->XMM_D(0) = F(64, d->XMM_D(0), s->XMM_D(0));\
}

#define FPU_ADD(size, a, b) float ## size ## _add(a, b, &env->sse_status)
#define FPU_SUB(size, a, b) float ## size ## _sub(a, b, &env->sse_status)
#define FPU_MUL(size, a, b) float ## size ## _mul(a, b, &env->sse_status)
#define FPU_DIV(size, a, b) float ## size ## _div(a, b, &env->sse_status)
#define FPU_MIN(size, a, b) (a) < (b) ? (a) : (b)
#define FPU_MAX(size, a, b) (a) > (b) ? (a) : (b)
#define FPU_SQRT(size, a, b) float ## size ## _sqrt(b, &env->sse_status)

SSE_HELPER_S(add, FPU_ADD)
SSE_HELPER_S(sub, FPU_SUB)
SSE_HELPER_S(mul, FPU_MUL)
SSE_HELPER_S(div, FPU_DIV)
SSE_HELPER_S(min, FPU_MIN)
SSE_HELPER_S(max, FPU_MAX)
SSE_HELPER_S(sqrt, FPU_SQRT)


/* float to float conversions */
void helper_cvtps2pd(Reg *d, Reg *s)
{
    float32 s0, s1;
    s0 = s->XMM_S(0);
    s1 = s->XMM_S(1);
    d->XMM_D(0) = float32_to_float64(s0, &env->sse_status);
    d->XMM_D(1) = float32_to_float64(s1, &env->sse_status);
}

void helper_cvtpd2ps(Reg *d, Reg *s)
{
    d->XMM_S(0) = float64_to_float32(s->XMM_D(0), &env->sse_status);
    d->XMM_S(1) = float64_to_float32(s->XMM_D(1), &env->sse_status);
    d->Q(1) = 0;
}

void helper_cvtss2sd(Reg *d, Reg *s)
{
    d->XMM_D(0) = float32_to_float64(s->XMM_S(0), &env->sse_status);
}

void helper_cvtsd2ss(Reg *d, Reg *s)
{
    d->XMM_S(0) = float64_to_float32(s->XMM_D(0), &env->sse_status);
}

/* integer to float */
void helper_cvtdq2ps(Reg *d, Reg *s)
{
    d->XMM_S(0) = int32_to_float32(s->XMM_L(0), &env->sse_status);
    d->XMM_S(1) = int32_to_float32(s->XMM_L(1), &env->sse_status);
    d->XMM_S(2) = int32_to_float32(s->XMM_L(2), &env->sse_status);
    d->XMM_S(3) = int32_to_float32(s->XMM_L(3), &env->sse_status);
}

void helper_cvtdq2pd(Reg *d, Reg *s)
{
    int32_t l0, l1;
    l0 = (int32_t)s->XMM_L(0);
    l1 = (int32_t)s->XMM_L(1);
    d->XMM_D(0) = int32_to_float64(l0, &env->sse_status);
    d->XMM_D(1) = int32_to_float64(l1, &env->sse_status);
}

void helper_cvtpi2ps(XMMReg *d, MMXReg *s)
{
    d->XMM_S(0) = int32_to_float32(s->MMX_L(0), &env->sse_status);
    d->XMM_S(1) = int32_to_float32(s->MMX_L(1), &env->sse_status);
}

void helper_cvtpi2pd(XMMReg *d, MMXReg *s)
{
    d->XMM_D(0) = int32_to_float64(s->MMX_L(0), &env->sse_status);
    d->XMM_D(1) = int32_to_float64(s->MMX_L(1), &env->sse_status);
}

void helper_cvtsi2ss(XMMReg *d, uint32_t val)
{
    d->XMM_S(0) = int32_to_float32(val, &env->sse_status);
}

void helper_cvtsi2sd(XMMReg *d, uint32_t val)
{
    d->XMM_D(0) = int32_to_float64(val, &env->sse_status);
}

#ifdef TARGET_X86_64
void helper_cvtsq2ss(XMMReg *d, uint64_t val)
{
    d->XMM_S(0) = int64_to_float32(val, &env->sse_status);
}

void helper_cvtsq2sd(XMMReg *d, uint64_t val)
{
    d->XMM_D(0) = int64_to_float64(val, &env->sse_status);
}
#endif

/* float to integer */
void helper_cvtps2dq(XMMReg *d, XMMReg *s)
{
    d->XMM_L(0) = float32_to_int32(s->XMM_S(0), &env->sse_status);
    d->XMM_L(1) = float32_to_int32(s->XMM_S(1), &env->sse_status);
    d->XMM_L(2) = float32_to_int32(s->XMM_S(2), &env->sse_status);
    d->XMM_L(3) = float32_to_int32(s->XMM_S(3), &env->sse_status);
}

void helper_cvtpd2dq(XMMReg *d, XMMReg *s)
{
    d->XMM_L(0) = float64_to_int32(s->XMM_D(0), &env->sse_status);
    d->XMM_L(1) = float64_to_int32(s->XMM_D(1), &env->sse_status);
    d->XMM_Q(1) = 0;
}

void helper_cvtps2pi(MMXReg *d, XMMReg *s)
{
    d->MMX_L(0) = float32_to_int32(s->XMM_S(0), &env->sse_status);
    d->MMX_L(1) = float32_to_int32(s->XMM_S(1), &env->sse_status);
}

void helper_cvtpd2pi(MMXReg *d, XMMReg *s)
{
    d->MMX_L(0) = float64_to_int32(s->XMM_D(0), &env->sse_status);
    d->MMX_L(1) = float64_to_int32(s->XMM_D(1), &env->sse_status);
}

int32_t helper_cvtss2si(XMMReg *s)
{
    return float32_to_int32(s->XMM_S(0), &env->sse_status);
}

int32_t helper_cvtsd2si(XMMReg *s)
{
    return float64_to_int32(s->XMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
int64_t helper_cvtss2sq(XMMReg *s)
{
    return float32_to_int64(s->XMM_S(0), &env->sse_status);
}

int64_t helper_cvtsd2sq(XMMReg *s)
{
    return float64_to_int64(s->XMM_D(0), &env->sse_status);
}
#endif

/* float to integer truncated */
void helper_cvttps2dq(XMMReg *d, XMMReg *s)
{
    d->XMM_L(0) = float32_to_int32_round_to_zero(s->XMM_S(0), &env->sse_status);
    d->XMM_L(1) = float32_to_int32_round_to_zero(s->XMM_S(1), &env->sse_status);
    d->XMM_L(2) = float32_to_int32_round_to_zero(s->XMM_S(2), &env->sse_status);
    d->XMM_L(3) = float32_to_int32_round_to_zero(s->XMM_S(3), &env->sse_status);
}

void helper_cvttpd2dq(XMMReg *d, XMMReg *s)
{
    d->XMM_L(0) = float64_to_int32_round_to_zero(s->XMM_D(0), &env->sse_status);
    d->XMM_L(1) = float64_to_int32_round_to_zero(s->XMM_D(1), &env->sse_status);
    d->XMM_Q(1) = 0;
}

void helper_cvttps2pi(MMXReg *d, XMMReg *s)
{
    d->MMX_L(0) = float32_to_int32_round_to_zero(s->XMM_S(0), &env->sse_status);
    d->MMX_L(1) = float32_to_int32_round_to_zero(s->XMM_S(1), &env->sse_status);
}

void helper_cvttpd2pi(MMXReg *d, XMMReg *s)
{
    d->MMX_L(0) = float64_to_int32_round_to_zero(s->XMM_D(0), &env->sse_status);
    d->MMX_L(1) = float64_to_int32_round_to_zero(s->XMM_D(1), &env->sse_status);
}

int32_t helper_cvttss2si(XMMReg *s)
{
    return float32_to_int32_round_to_zero(s->XMM_S(0), &env->sse_status);
}

int32_t helper_cvttsd2si(XMMReg *s)
{
    return float64_to_int32_round_to_zero(s->XMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
int64_t helper_cvttss2sq(XMMReg *s)
{
    return float32_to_int64_round_to_zero(s->XMM_S(0), &env->sse_status);
}

int64_t helper_cvttsd2sq(XMMReg *s)
{
    return float64_to_int64_round_to_zero(s->XMM_D(0), &env->sse_status);
}
#endif

void helper_rsqrtps(XMMReg *d, XMMReg *s)
{
    d->XMM_S(0) = approx_rsqrt(s->XMM_S(0));
    d->XMM_S(1) = approx_rsqrt(s->XMM_S(1));
    d->XMM_S(2) = approx_rsqrt(s->XMM_S(2));
    d->XMM_S(3) = approx_rsqrt(s->XMM_S(3));
}

void helper_rsqrtss(XMMReg *d, XMMReg *s)
{
    d->XMM_S(0) = approx_rsqrt(s->XMM_S(0));
}

void helper_rcpps(XMMReg *d, XMMReg *s)
{
    d->XMM_S(0) = approx_rcp(s->XMM_S(0));
    d->XMM_S(1) = approx_rcp(s->XMM_S(1));
    d->XMM_S(2) = approx_rcp(s->XMM_S(2));
    d->XMM_S(3) = approx_rcp(s->XMM_S(3));
}

void helper_rcpss(XMMReg *d, XMMReg *s)
{
    d->XMM_S(0) = approx_rcp(s->XMM_S(0));
}

void helper_haddps(XMMReg *d, XMMReg *s)
{
    XMMReg r;
    r.XMM_S(0) = d->XMM_S(0) + d->XMM_S(1);
    r.XMM_S(1) = d->XMM_S(2) + d->XMM_S(3);
    r.XMM_S(2) = s->XMM_S(0) + s->XMM_S(1);
    r.XMM_S(3) = s->XMM_S(2) + s->XMM_S(3);
    *d = r;
}

void helper_haddpd(XMMReg *d, XMMReg *s)
{
    XMMReg r;
    r.XMM_D(0) = d->XMM_D(0) + d->XMM_D(1);
    r.XMM_D(1) = s->XMM_D(0) + s->XMM_D(1);
    *d = r;
}

void helper_hsubps(XMMReg *d, XMMReg *s)
{
    XMMReg r;
    r.XMM_S(0) = d->XMM_S(0) - d->XMM_S(1);
    r.XMM_S(1) = d->XMM_S(2) - d->XMM_S(3);
    r.XMM_S(2) = s->XMM_S(0) - s->XMM_S(1);
    r.XMM_S(3) = s->XMM_S(2) - s->XMM_S(3);
    *d = r;
}

void helper_hsubpd(XMMReg *d, XMMReg *s)
{
    XMMReg r;
    r.XMM_D(0) = d->XMM_D(0) - d->XMM_D(1);
    r.XMM_D(1) = s->XMM_D(0) - s->XMM_D(1);
    *d = r;
}

void helper_addsubps(XMMReg *d, XMMReg *s)
{
    d->XMM_S(0) = d->XMM_S(0) - s->XMM_S(0);
    d->XMM_S(1) = d->XMM_S(1) + s->XMM_S(1);
    d->XMM_S(2) = d->XMM_S(2) - s->XMM_S(2);
    d->XMM_S(3) = d->XMM_S(3) + s->XMM_S(3);
}

void helper_addsubpd(XMMReg *d, XMMReg *s)
{
    d->XMM_D(0) = d->XMM_D(0) - s->XMM_D(0);
    d->XMM_D(1) = d->XMM_D(1) + s->XMM_D(1);
}

/* XXX: unordered */
#define SSE_HELPER_CMP(name, F)\
void helper_ ## name ## ps (Reg *d, Reg *s)\
{\
    d->XMM_L(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
    d->XMM_L(1) = F(32, d->XMM_S(1), s->XMM_S(1));\
    d->XMM_L(2) = F(32, d->XMM_S(2), s->XMM_S(2));\
    d->XMM_L(3) = F(32, d->XMM_S(3), s->XMM_S(3));\
}\
\
void helper_ ## name ## ss (Reg *d, Reg *s)\
{\
    d->XMM_L(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
}\
void helper_ ## name ## pd (Reg *d, Reg *s)\
{\
    d->XMM_Q(0) = F(64, d->XMM_D(0), s->XMM_D(0));\
    d->XMM_Q(1) = F(64, d->XMM_D(1), s->XMM_D(1));\
}\
\
void helper_ ## name ## sd (Reg *d, Reg *s)\
{\
    d->XMM_Q(0) = F(64, d->XMM_D(0), s->XMM_D(0));\
}

#define FPU_CMPEQ(size, a, b) float ## size ## _eq(a, b, &env->sse_status) ? -1 : 0
#define FPU_CMPLT(size, a, b) float ## size ## _lt(a, b, &env->sse_status) ? -1 : 0
#define FPU_CMPLE(size, a, b) float ## size ## _le(a, b, &env->sse_status) ? -1 : 0
#define FPU_CMPUNORD(size, a, b) float ## size ## _unordered(a, b, &env->sse_status) ? - 1 : 0
#define FPU_CMPNEQ(size, a, b) float ## size ## _eq(a, b, &env->sse_status) ? 0 : -1
#define FPU_CMPNLT(size, a, b) float ## size ## _lt(a, b, &env->sse_status) ? 0 : -1
#define FPU_CMPNLE(size, a, b) float ## size ## _le(a, b, &env->sse_status) ? 0 : -1
#define FPU_CMPORD(size, a, b) float ## size ## _unordered(a, b, &env->sse_status) ? 0 : -1

SSE_HELPER_CMP(cmpeq, FPU_CMPEQ)
SSE_HELPER_CMP(cmplt, FPU_CMPLT)
SSE_HELPER_CMP(cmple, FPU_CMPLE)
SSE_HELPER_CMP(cmpunord, FPU_CMPUNORD)
SSE_HELPER_CMP(cmpneq, FPU_CMPNEQ)
SSE_HELPER_CMP(cmpnlt, FPU_CMPNLT)
SSE_HELPER_CMP(cmpnle, FPU_CMPNLE)
SSE_HELPER_CMP(cmpord, FPU_CMPORD)

const int comis_eflags[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void helper_ucomiss(Reg *d, Reg *s)
{
    int ret;
    float32 s0, s1;

    s0 = d->XMM_S(0);
    s1 = s->XMM_S(0);
    ret = float32_compare_quiet(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

void helper_comiss(Reg *d, Reg *s)
{
    int ret;
    float32 s0, s1;

    s0 = d->XMM_S(0);
    s1 = s->XMM_S(0);
    ret = float32_compare(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

void helper_ucomisd(Reg *d, Reg *s)
{
    int ret;
    float64 d0, d1;

    d0 = d->XMM_D(0);
    d1 = s->XMM_D(0);
    ret = float64_compare_quiet(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

void helper_comisd(Reg *d, Reg *s)
{
    int ret;
    float64 d0, d1;

    d0 = d->XMM_D(0);
    d1 = s->XMM_D(0);
    ret = float64_compare(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

uint32_t helper_movmskps(Reg *s)
{
    int b0, b1, b2, b3;
    b0 = s->XMM_L(0) >> 31;
    b1 = s->XMM_L(1) >> 31;
    b2 = s->XMM_L(2) >> 31;
    b3 = s->XMM_L(3) >> 31;
    return b0 | (b1 << 1) | (b2 << 2) | (b3 << 3);
}

uint32_t helper_movmskpd(Reg *s)
{
    int b0, b1;
    b0 = s->XMM_L(1) >> 31;
    b1 = s->XMM_L(3) >> 31;
    return b0 | (b1 << 1);
}

#endif

uint32_t glue(helper_pmovmskb, SUFFIX)(Reg *s)
{
    uint32_t val;
    val = 0;
    val |= (s->XMM_B(0) >> 7);
    val |= (s->XMM_B(1) >> 6) & 0x02;
    val |= (s->XMM_B(2) >> 5) & 0x04;
    val |= (s->XMM_B(3) >> 4) & 0x08;
    val |= (s->XMM_B(4) >> 3) & 0x10;
    val |= (s->XMM_B(5) >> 2) & 0x20;
    val |= (s->XMM_B(6) >> 1) & 0x40;
    val |= (s->XMM_B(7)) & 0x80;
#if SHIFT == 1
    val |= (s->XMM_B(8) << 1) & 0x0100;
    val |= (s->XMM_B(9) << 2) & 0x0200;
    val |= (s->XMM_B(10) << 3) & 0x0400;
    val |= (s->XMM_B(11) << 4) & 0x0800;
    val |= (s->XMM_B(12) << 5) & 0x1000;
    val |= (s->XMM_B(13) << 6) & 0x2000;
    val |= (s->XMM_B(14) << 7) & 0x4000;
    val |= (s->XMM_B(15) << 8) & 0x8000;
#endif
    return val;
}

void glue(helper_packsswb, SUFFIX) (Reg *d, Reg *s)
{
    Reg r;

    r.B(0) = satsb((int16_t)d->W(0));
    r.B(1) = satsb((int16_t)d->W(1));
    r.B(2) = satsb((int16_t)d->W(2));
    r.B(3) = satsb((int16_t)d->W(3));
#if SHIFT == 1
    r.B(4) = satsb((int16_t)d->W(4));
    r.B(5) = satsb((int16_t)d->W(5));
    r.B(6) = satsb((int16_t)d->W(6));
    r.B(7) = satsb((int16_t)d->W(7));
#endif
    r.B((4 << SHIFT) + 0) = satsb((int16_t)s->W(0));
    r.B((4 << SHIFT) + 1) = satsb((int16_t)s->W(1));
    r.B((4 << SHIFT) + 2) = satsb((int16_t)s->W(2));
    r.B((4 << SHIFT) + 3) = satsb((int16_t)s->W(3));
#if SHIFT == 1
    r.B(12) = satsb((int16_t)s->W(4));
    r.B(13) = satsb((int16_t)s->W(5));
    r.B(14) = satsb((int16_t)s->W(6));
    r.B(15) = satsb((int16_t)s->W(7));
#endif
    *d = r;
}

void glue(helper_packuswb, SUFFIX) (Reg *d, Reg *s)
{
    Reg r;

    r.B(0) = satub((int16_t)d->W(0));
    r.B(1) = satub((int16_t)d->W(1));
    r.B(2) = satub((int16_t)d->W(2));
    r.B(3) = satub((int16_t)d->W(3));
#if SHIFT == 1
    r.B(4) = satub((int16_t)d->W(4));
    r.B(5) = satub((int16_t)d->W(5));
    r.B(6) = satub((int16_t)d->W(6));
    r.B(7) = satub((int16_t)d->W(7));
#endif
    r.B((4 << SHIFT) + 0) = satub((int16_t)s->W(0));
    r.B((4 << SHIFT) + 1) = satub((int16_t)s->W(1));
    r.B((4 << SHIFT) + 2) = satub((int16_t)s->W(2));
    r.B((4 << SHIFT) + 3) = satub((int16_t)s->W(3));
#if SHIFT == 1
    r.B(12) = satub((int16_t)s->W(4));
    r.B(13) = satub((int16_t)s->W(5));
    r.B(14) = satub((int16_t)s->W(6));
    r.B(15) = satub((int16_t)s->W(7));
#endif
    *d = r;
}

void glue(helper_packssdw, SUFFIX) (Reg *d, Reg *s)
{
    Reg r;

    r.W(0) = satsw(d->L(0));
    r.W(1) = satsw(d->L(1));
#if SHIFT == 1
    r.W(2) = satsw(d->L(2));
    r.W(3) = satsw(d->L(3));
#endif
    r.W((2 << SHIFT) + 0) = satsw(s->L(0));
    r.W((2 << SHIFT) + 1) = satsw(s->L(1));
#if SHIFT == 1
    r.W(6) = satsw(s->L(2));
    r.W(7) = satsw(s->L(3));
#endif
    *d = r;
}

#define UNPCK_OP(base_name, base)                               \
                                                                \
void glue(helper_punpck ## base_name ## bw, SUFFIX) (Reg *d, Reg *s)   \
{                                                               \
    Reg r;                                              \
                                                                \
    r.B(0) = d->B((base << (SHIFT + 2)) + 0);                   \
    r.B(1) = s->B((base << (SHIFT + 2)) + 0);                   \
    r.B(2) = d->B((base << (SHIFT + 2)) + 1);                   \
    r.B(3) = s->B((base << (SHIFT + 2)) + 1);                   \
    r.B(4) = d->B((base << (SHIFT + 2)) + 2);                   \
    r.B(5) = s->B((base << (SHIFT + 2)) + 2);                   \
    r.B(6) = d->B((base << (SHIFT + 2)) + 3);                   \
    r.B(7) = s->B((base << (SHIFT + 2)) + 3);                   \
XMM_ONLY(                                                       \
    r.B(8) = d->B((base << (SHIFT + 2)) + 4);                   \
    r.B(9) = s->B((base << (SHIFT + 2)) + 4);                   \
    r.B(10) = d->B((base << (SHIFT + 2)) + 5);                  \
    r.B(11) = s->B((base << (SHIFT + 2)) + 5);                  \
    r.B(12) = d->B((base << (SHIFT + 2)) + 6);                  \
    r.B(13) = s->B((base << (SHIFT + 2)) + 6);                  \
    r.B(14) = d->B((base << (SHIFT + 2)) + 7);                  \
    r.B(15) = s->B((base << (SHIFT + 2)) + 7);                  \
)                                                               \
    *d = r;                                                     \
}                                                               \
                                                                \
void glue(helper_punpck ## base_name ## wd, SUFFIX) (Reg *d, Reg *s)   \
{                                                               \
    Reg r;                                              \
                                                                \
    r.W(0) = d->W((base << (SHIFT + 1)) + 0);                   \
    r.W(1) = s->W((base << (SHIFT + 1)) + 0);                   \
    r.W(2) = d->W((base << (SHIFT + 1)) + 1);                   \
    r.W(3) = s->W((base << (SHIFT + 1)) + 1);                   \
XMM_ONLY(                                                       \
    r.W(4) = d->W((base << (SHIFT + 1)) + 2);                   \
    r.W(5) = s->W((base << (SHIFT + 1)) + 2);                   \
    r.W(6) = d->W((base << (SHIFT + 1)) + 3);                   \
    r.W(7) = s->W((base << (SHIFT + 1)) + 3);                   \
)                                                               \
    *d = r;                                                     \
}                                                               \
                                                                \
void glue(helper_punpck ## base_name ## dq, SUFFIX) (Reg *d, Reg *s)   \
{                                                               \
    Reg r;                                              \
                                                                \
    r.L(0) = d->L((base << SHIFT) + 0);                         \
    r.L(1) = s->L((base << SHIFT) + 0);                         \
XMM_ONLY(                                                       \
    r.L(2) = d->L((base << SHIFT) + 1);                         \
    r.L(3) = s->L((base << SHIFT) + 1);                         \
)                                                               \
    *d = r;                                                     \
}                                                               \
                                                                \
XMM_ONLY(                                                       \
void glue(helper_punpck ## base_name ## qdq, SUFFIX) (Reg *d, Reg *s)  \
{                                                               \
    Reg r;                                              \
                                                                \
    r.Q(0) = d->Q(base);                                        \
    r.Q(1) = s->Q(base);                                        \
    *d = r;                                                     \
}                                                               \
)

UNPCK_OP(l, 0)
UNPCK_OP(h, 1)

/* 3DNow! float ops */
#if SHIFT == 0
void helper_pi2fd(MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = int32_to_float32(s->MMX_L(0), &env->mmx_status);
    d->MMX_S(1) = int32_to_float32(s->MMX_L(1), &env->mmx_status);
}

void helper_pi2fw(MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = int32_to_float32((int16_t)s->MMX_W(0), &env->mmx_status);
    d->MMX_S(1) = int32_to_float32((int16_t)s->MMX_W(2), &env->mmx_status);
}

void helper_pf2id(MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_to_int32_round_to_zero(s->MMX_S(0), &env->mmx_status);
    d->MMX_L(1) = float32_to_int32_round_to_zero(s->MMX_S(1), &env->mmx_status);
}

void helper_pf2iw(MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = satsw(float32_to_int32_round_to_zero(s->MMX_S(0), &env->mmx_status));
    d->MMX_L(1) = satsw(float32_to_int32_round_to_zero(s->MMX_S(1), &env->mmx_status));
}

void helper_pfacc(MMXReg *d, MMXReg *s)
{
    MMXReg r;
    r.MMX_S(0) = float32_add(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    r.MMX_S(1) = float32_add(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    *d = r;
}

void helper_pfadd(MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_add(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_add(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfcmpeq(MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_eq(d->MMX_S(0), s->MMX_S(0), &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_eq(d->MMX_S(1), s->MMX_S(1), &env->mmx_status) ? -1 : 0;
}

void helper_pfcmpge(MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_le(s->MMX_S(0), d->MMX_S(0), &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_le(s->MMX_S(1), d->MMX_S(1), &env->mmx_status) ? -1 : 0;
}

void helper_pfcmpgt(MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_lt(s->MMX_S(0), d->MMX_S(0), &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_lt(s->MMX_S(1), d->MMX_S(1), &env->mmx_status) ? -1 : 0;
}

void helper_pfmax(MMXReg *d, MMXReg *s)
{
    if (float32_lt(d->MMX_S(0), s->MMX_S(0), &env->mmx_status))
        d->MMX_S(0) = s->MMX_S(0);
    if (float32_lt(d->MMX_S(1), s->MMX_S(1), &env->mmx_status))
        d->MMX_S(1) = s->MMX_S(1);
}

void helper_pfmin(MMXReg *d, MMXReg *s)
{
    if (float32_lt(s->MMX_S(0), d->MMX_S(0), &env->mmx_status))
        d->MMX_S(0) = s->MMX_S(0);
    if (float32_lt(s->MMX_S(1), d->MMX_S(1), &env->mmx_status))
        d->MMX_S(1) = s->MMX_S(1);
}

void helper_pfmul(MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_mul(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_mul(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfnacc(MMXReg *d, MMXReg *s)
{
    MMXReg r;
    r.MMX_S(0) = float32_sub(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    r.MMX_S(1) = float32_sub(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    *d = r;
}

void helper_pfpnacc(MMXReg *d, MMXReg *s)
{
    MMXReg r;
    r.MMX_S(0) = float32_sub(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    r.MMX_S(1) = float32_add(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    *d = r;
}

void helper_pfrcp(MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = approx_rcp(s->MMX_S(0));
    d->MMX_S(1) = d->MMX_S(0);
}

void helper_pfrsqrt(MMXReg *d, MMXReg *s)
{
    d->MMX_L(1) = s->MMX_L(0) & 0x7fffffff;
    d->MMX_S(1) = approx_rsqrt(d->MMX_S(1));
    d->MMX_L(1) |= s->MMX_L(0) & 0x80000000;
    d->MMX_L(0) = d->MMX_L(1);
}

void helper_pfsub(MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_sub(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_sub(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfsubr(MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_sub(s->MMX_S(0), d->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_sub(s->MMX_S(1), d->MMX_S(1), &env->mmx_status);
}

void helper_pswapd(MMXReg *d, MMXReg *s)
{
    MMXReg r;
    r.MMX_L(0) = s->MMX_L(1);
    r.MMX_L(1) = s->MMX_L(0);
    *d = r;
}
#endif

/* SSSE3 op helpers */
void glue(helper_pshufb, SUFFIX) (Reg *d, Reg *s)
{
    int i;
    Reg r;

    for (i = 0; i < (8 << SHIFT); i++)
        r.B(i) = (s->B(i) & 0x80) ? 0 : (d->B(s->B(i) & ((8 << SHIFT) - 1)));

    *d = r;
}

void glue(helper_phaddw, SUFFIX) (Reg *d, Reg *s)
{
    d->W(0) = (int16_t)d->W(0) + (int16_t)d->W(1);
    d->W(1) = (int16_t)d->W(2) + (int16_t)d->W(3);
    XMM_ONLY(d->W(2) = (int16_t)d->W(4) + (int16_t)d->W(5));
    XMM_ONLY(d->W(3) = (int16_t)d->W(6) + (int16_t)d->W(7));
    d->W((2 << SHIFT) + 0) = (int16_t)s->W(0) + (int16_t)s->W(1);
    d->W((2 << SHIFT) + 1) = (int16_t)s->W(2) + (int16_t)s->W(3);
    XMM_ONLY(d->W(6) = (int16_t)s->W(4) + (int16_t)s->W(5));
    XMM_ONLY(d->W(7) = (int16_t)s->W(6) + (int16_t)s->W(7));
}

void glue(helper_phaddd, SUFFIX) (Reg *d, Reg *s)
{
    d->L(0) = (int32_t)d->L(0) + (int32_t)d->L(1);
    XMM_ONLY(d->L(1) = (int32_t)d->L(2) + (int32_t)d->L(3));
    d->L((1 << SHIFT) + 0) = (int32_t)s->L(0) + (int32_t)s->L(1);
    XMM_ONLY(d->L(3) = (int32_t)s->L(2) + (int32_t)s->L(3));
}

void glue(helper_phaddsw, SUFFIX) (Reg *d, Reg *s)
{
    d->W(0) = satsw((int16_t)d->W(0) + (int16_t)d->W(1));
    d->W(1) = satsw((int16_t)d->W(2) + (int16_t)d->W(3));
    XMM_ONLY(d->W(2) = satsw((int16_t)d->W(4) + (int16_t)d->W(5)));
    XMM_ONLY(d->W(3) = satsw((int16_t)d->W(6) + (int16_t)d->W(7)));
    d->W((2 << SHIFT) + 0) = satsw((int16_t)s->W(0) + (int16_t)s->W(1));
    d->W((2 << SHIFT) + 1) = satsw((int16_t)s->W(2) + (int16_t)s->W(3));
    XMM_ONLY(d->W(6) = satsw((int16_t)s->W(4) + (int16_t)s->W(5)));
    XMM_ONLY(d->W(7) = satsw((int16_t)s->W(6) + (int16_t)s->W(7)));
}

void glue(helper_pmaddubsw, SUFFIX) (Reg *d, Reg *s)
{
    d->W(0) = satsw((int8_t)s->B( 0) * (uint8_t)d->B( 0) +
                    (int8_t)s->B( 1) * (uint8_t)d->B( 1));
    d->W(1) = satsw((int8_t)s->B( 2) * (uint8_t)d->B( 2) +
                    (int8_t)s->B( 3) * (uint8_t)d->B( 3));
    d->W(2) = satsw((int8_t)s->B( 4) * (uint8_t)d->B( 4) +
                    (int8_t)s->B( 5) * (uint8_t)d->B( 5));
    d->W(3) = satsw((int8_t)s->B( 6) * (uint8_t)d->B( 6) +
                    (int8_t)s->B( 7) * (uint8_t)d->B( 7));
#if SHIFT == 1
    d->W(4) = satsw((int8_t)s->B( 8) * (uint8_t)d->B( 8) +
                    (int8_t)s->B( 9) * (uint8_t)d->B( 9));
    d->W(5) = satsw((int8_t)s->B(10) * (uint8_t)d->B(10) +
                    (int8_t)s->B(11) * (uint8_t)d->B(11));
    d->W(6) = satsw((int8_t)s->B(12) * (uint8_t)d->B(12) +
                    (int8_t)s->B(13) * (uint8_t)d->B(13));
    d->W(7) = satsw((int8_t)s->B(14) * (uint8_t)d->B(14) +
                    (int8_t)s->B(15) * (uint8_t)d->B(15));
#endif
}

void glue(helper_phsubw, SUFFIX) (Reg *d, Reg *s)
{
    d->W(0) = (int16_t)d->W(0) - (int16_t)d->W(1);
    d->W(1) = (int16_t)d->W(2) - (int16_t)d->W(3);
    XMM_ONLY(d->W(2) = (int16_t)d->W(4) - (int16_t)d->W(5));
    XMM_ONLY(d->W(3) = (int16_t)d->W(6) - (int16_t)d->W(7));
    d->W((2 << SHIFT) + 0) = (int16_t)s->W(0) - (int16_t)s->W(1);
    d->W((2 << SHIFT) + 1) = (int16_t)s->W(2) - (int16_t)s->W(3);
    XMM_ONLY(d->W(6) = (int16_t)s->W(4) - (int16_t)s->W(5));
    XMM_ONLY(d->W(7) = (int16_t)s->W(6) - (int16_t)s->W(7));
}

void glue(helper_phsubd, SUFFIX) (Reg *d, Reg *s)
{
    d->L(0) = (int32_t)d->L(0) - (int32_t)d->L(1);
    XMM_ONLY(d->L(1) = (int32_t)d->L(2) - (int32_t)d->L(3));
    d->L((1 << SHIFT) + 0) = (int32_t)s->L(0) - (int32_t)s->L(1);
    XMM_ONLY(d->L(3) = (int32_t)s->L(2) - (int32_t)s->L(3));
}

void glue(helper_phsubsw, SUFFIX) (Reg *d, Reg *s)
{
    d->W(0) = satsw((int16_t)d->W(0) - (int16_t)d->W(1));
    d->W(1) = satsw((int16_t)d->W(2) - (int16_t)d->W(3));
    XMM_ONLY(d->W(2) = satsw((int16_t)d->W(4) - (int16_t)d->W(5)));
    XMM_ONLY(d->W(3) = satsw((int16_t)d->W(6) - (int16_t)d->W(7)));
    d->W((2 << SHIFT) + 0) = satsw((int16_t)s->W(0) - (int16_t)s->W(1));
    d->W((2 << SHIFT) + 1) = satsw((int16_t)s->W(2) - (int16_t)s->W(3));
    XMM_ONLY(d->W(6) = satsw((int16_t)s->W(4) - (int16_t)s->W(5)));
    XMM_ONLY(d->W(7) = satsw((int16_t)s->W(6) - (int16_t)s->W(7)));
}

#define FABSB(_, x) x > INT8_MAX  ? -(int8_t ) x : x
#define FABSW(_, x) x > INT16_MAX ? -(int16_t) x : x
#define FABSL(_, x) x > INT32_MAX ? -(int32_t) x : x
SSE_HELPER_B(helper_pabsb, FABSB)
SSE_HELPER_W(helper_pabsw, FABSW)
SSE_HELPER_L(helper_pabsd, FABSL)

#define FMULHRSW(d, s) ((int16_t) d * (int16_t) s + 0x4000) >> 15
SSE_HELPER_W(helper_pmulhrsw, FMULHRSW)

#define FSIGNB(d, s) s <= INT8_MAX  ? s ? d : 0 : -(int8_t ) d
#define FSIGNW(d, s) s <= INT16_MAX ? s ? d : 0 : -(int16_t) d
#define FSIGNL(d, s) s <= INT32_MAX ? s ? d : 0 : -(int32_t) d
SSE_HELPER_B(helper_psignb, FSIGNB)
SSE_HELPER_W(helper_psignw, FSIGNW)
SSE_HELPER_L(helper_psignd, FSIGNL)

void glue(helper_palignr, SUFFIX) (Reg *d, Reg *s, int32_t shift)
{
    Reg r;

    /* XXX could be checked during translation */
    if (shift >= (16 << SHIFT)) {
        r.Q(0) = 0;
        XMM_ONLY(r.Q(1) = 0);
    } else {
        shift <<= 3;
#define SHR(v, i) (i < 64 && i > -64 ? i > 0 ? v >> (i) : (v << -(i)) : 0)
#if SHIFT == 0
        r.Q(0) = SHR(s->Q(0), shift -   0) |
                 SHR(d->Q(0), shift -  64);
#else
        r.Q(0) = SHR(s->Q(0), shift -   0) |
                 SHR(s->Q(1), shift -  64) |
                 SHR(d->Q(0), shift - 128) |
                 SHR(d->Q(1), shift - 192);
        r.Q(1) = SHR(s->Q(0), shift +  64) |
                 SHR(s->Q(1), shift -   0) |
                 SHR(d->Q(0), shift -  64) |
                 SHR(d->Q(1), shift - 128);
#endif
#undef SHR
    }

    *d = r;
}

#undef SHIFT
#undef XMM_ONLY
#undef Reg
#undef B
#undef W
#undef L
#undef Q
#undef SUFFIX
