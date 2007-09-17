/*
 *  MMX/SSE/SSE2/PNI support
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

void OPPROTO glue(op_psrlw, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_psraw, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_psllw, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_psrld, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_psrad, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_pslld, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_psrlq, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_psllq, SUFFIX)(void)
{
    Reg *d, *s;
    int shift;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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
void OPPROTO glue(op_psrldq, SUFFIX)(void)
{
    Reg *d, *s;
    int shift, i;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    shift = s->L(0);
    if (shift > 16)
        shift = 16;
    for(i = 0; i < 16 - shift; i++)
        d->B(i) = d->B(i + shift);
    for(i = 16 - shift; i < 16; i++)
        d->B(i) = 0;
    FORCE_RET();
}

void OPPROTO glue(op_pslldq, SUFFIX)(void)
{
    Reg *d, *s;
    int shift, i;

    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
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

#define SSE_OP_B(name, F)\
void OPPROTO glue(name, SUFFIX) (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
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

#define SSE_OP_W(name, F)\
void OPPROTO glue(name, SUFFIX) (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
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

#define SSE_OP_L(name, F)\
void OPPROTO glue(name, SUFFIX) (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->L(0) = F(d->L(0), s->L(0));\
    d->L(1) = F(d->L(1), s->L(1));\
    XMM_ONLY(\
    d->L(2) = F(d->L(2), s->L(2));\
    d->L(3) = F(d->L(3), s->L(3));\
    )\
}

#define SSE_OP_Q(name, F)\
void OPPROTO glue(name, SUFFIX) (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
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
#define FMULHUW(a, b) (a) * (b) >> 16
#define FMULHW(a, b) (int16_t)(a) * (int16_t)(b) >> 16

#define FAVG(a, b) ((a) + (b) + 1) >> 1
#endif

SSE_OP_B(op_paddb, FADD)
SSE_OP_W(op_paddw, FADD)
SSE_OP_L(op_paddl, FADD)
SSE_OP_Q(op_paddq, FADD)

SSE_OP_B(op_psubb, FSUB)
SSE_OP_W(op_psubw, FSUB)
SSE_OP_L(op_psubl, FSUB)
SSE_OP_Q(op_psubq, FSUB)

SSE_OP_B(op_paddusb, FADDUB)
SSE_OP_B(op_paddsb, FADDSB)
SSE_OP_B(op_psubusb, FSUBUB)
SSE_OP_B(op_psubsb, FSUBSB)

SSE_OP_W(op_paddusw, FADDUW)
SSE_OP_W(op_paddsw, FADDSW)
SSE_OP_W(op_psubusw, FSUBUW)
SSE_OP_W(op_psubsw, FSUBSW)

SSE_OP_B(op_pminub, FMINUB)
SSE_OP_B(op_pmaxub, FMAXUB)

SSE_OP_W(op_pminsw, FMINSW)
SSE_OP_W(op_pmaxsw, FMAXSW)

SSE_OP_Q(op_pand, FAND)
SSE_OP_Q(op_pandn, FANDN)
SSE_OP_Q(op_por, FOR)
SSE_OP_Q(op_pxor, FXOR)

SSE_OP_B(op_pcmpgtb, FCMPGTB)
SSE_OP_W(op_pcmpgtw, FCMPGTW)
SSE_OP_L(op_pcmpgtl, FCMPGTL)

SSE_OP_B(op_pcmpeqb, FCMPEQ)
SSE_OP_W(op_pcmpeqw, FCMPEQ)
SSE_OP_L(op_pcmpeql, FCMPEQ)

SSE_OP_W(op_pmullw, FMULLW)
SSE_OP_W(op_pmulhuw, FMULHUW)
SSE_OP_W(op_pmulhw, FMULHW)

SSE_OP_B(op_pavgb, FAVG)
SSE_OP_W(op_pavgw, FAVG)

void OPPROTO glue(op_pmuludq, SUFFIX) (void)
{
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

    d->Q(0) = (uint64_t)s->L(0) * (uint64_t)d->L(0);
#if SHIFT == 1
    d->Q(1) = (uint64_t)s->L(2) * (uint64_t)d->L(2);
#endif
}

void OPPROTO glue(op_pmaddwd, SUFFIX) (void)
{
    int i;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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
void OPPROTO glue(op_psadbw, SUFFIX) (void)
{
    unsigned int val;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_maskmov, SUFFIX) (void)
{
    int i;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    for(i = 0; i < (8 << SHIFT); i++) {
        if (s->B(i) & 0x80)
            stb(A0 + i, d->B(i));
    }
    FORCE_RET();
}

void OPPROTO glue(op_movl_mm_T0, SUFFIX) (void)
{
    Reg *d;
    d = (Reg *)((char *)env + PARAM1);
    d->L(0) = T0;
    d->L(1) = 0;
#if SHIFT == 1
    d->Q(1) = 0;
#endif
}

void OPPROTO glue(op_movl_T0_mm, SUFFIX) (void)
{
    Reg *s;
    s = (Reg *)((char *)env + PARAM1);
    T0 = s->L(0);
}

#ifdef TARGET_X86_64
void OPPROTO glue(op_movq_mm_T0, SUFFIX) (void)
{
    Reg *d;
    d = (Reg *)((char *)env + PARAM1);
    d->Q(0) = T0;
#if SHIFT == 1
    d->Q(1) = 0;
#endif
}

void OPPROTO glue(op_movq_T0_mm, SUFFIX) (void)
{
    Reg *s;
    s = (Reg *)((char *)env + PARAM1);
    T0 = s->Q(0);
}
#endif

#if SHIFT == 0
void OPPROTO glue(op_pshufw, SUFFIX) (void)
{
    Reg r, *d, *s;
    int order;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    order = PARAM3;
    r.W(0) = s->W(order & 3);
    r.W(1) = s->W((order >> 2) & 3);
    r.W(2) = s->W((order >> 4) & 3);
    r.W(3) = s->W((order >> 6) & 3);
    *d = r;
}
#else
void OPPROTO op_shufps(void)
{
    Reg r, *d, *s;
    int order;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    order = PARAM3;
    r.L(0) = d->L(order & 3);
    r.L(1) = d->L((order >> 2) & 3);
    r.L(2) = s->L((order >> 4) & 3);
    r.L(3) = s->L((order >> 6) & 3);
    *d = r;
}

void OPPROTO op_shufpd(void)
{
    Reg r, *d, *s;
    int order;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    order = PARAM3;
    r.Q(0) = d->Q(order & 1);
    r.Q(1) = s->Q((order >> 1) & 1);
    *d = r;
}

void OPPROTO glue(op_pshufd, SUFFIX) (void)
{
    Reg r, *d, *s;
    int order;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    order = PARAM3;
    r.L(0) = s->L(order & 3);
    r.L(1) = s->L((order >> 2) & 3);
    r.L(2) = s->L((order >> 4) & 3);
    r.L(3) = s->L((order >> 6) & 3);
    *d = r;
}

void OPPROTO glue(op_pshuflw, SUFFIX) (void)
{
    Reg r, *d, *s;
    int order;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    order = PARAM3;
    r.W(0) = s->W(order & 3);
    r.W(1) = s->W((order >> 2) & 3);
    r.W(2) = s->W((order >> 4) & 3);
    r.W(3) = s->W((order >> 6) & 3);
    r.Q(1) = s->Q(1);
    *d = r;
}

void OPPROTO glue(op_pshufhw, SUFFIX) (void)
{
    Reg r, *d, *s;
    int order;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    order = PARAM3;
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

#define SSE_OP_S(name, F)\
void OPPROTO op_ ## name ## ps (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->XMM_S(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
    d->XMM_S(1) = F(32, d->XMM_S(1), s->XMM_S(1));\
    d->XMM_S(2) = F(32, d->XMM_S(2), s->XMM_S(2));\
    d->XMM_S(3) = F(32, d->XMM_S(3), s->XMM_S(3));\
}\
\
void OPPROTO op_ ## name ## ss (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->XMM_S(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
}\
void OPPROTO op_ ## name ## pd (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->XMM_D(0) = F(64, d->XMM_D(0), s->XMM_D(0));\
    d->XMM_D(1) = F(64, d->XMM_D(1), s->XMM_D(1));\
}\
\
void OPPROTO op_ ## name ## sd (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->XMM_D(0) = F(64, d->XMM_D(0), s->XMM_D(0));\
}

#define FPU_ADD(size, a, b) float ## size ## _add(a, b, &env->sse_status)
#define FPU_SUB(size, a, b) float ## size ## _sub(a, b, &env->sse_status)
#define FPU_MUL(size, a, b) float ## size ## _mul(a, b, &env->sse_status)
#define FPU_DIV(size, a, b) float ## size ## _div(a, b, &env->sse_status)
#define FPU_MIN(size, a, b) (a) < (b) ? (a) : (b)
#define FPU_MAX(size, a, b) (a) > (b) ? (a) : (b)
#define FPU_SQRT(size, a, b) float ## size ## _sqrt(b, &env->sse_status)

SSE_OP_S(add, FPU_ADD)
SSE_OP_S(sub, FPU_SUB)
SSE_OP_S(mul, FPU_MUL)
SSE_OP_S(div, FPU_DIV)
SSE_OP_S(min, FPU_MIN)
SSE_OP_S(max, FPU_MAX)
SSE_OP_S(sqrt, FPU_SQRT)


/* float to float conversions */
void OPPROTO op_cvtps2pd(void)
{
    float32 s0, s1;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    s0 = s->XMM_S(0);
    s1 = s->XMM_S(1);
    d->XMM_D(0) = float32_to_float64(s0, &env->sse_status);
    d->XMM_D(1) = float32_to_float64(s1, &env->sse_status);
}

void OPPROTO op_cvtpd2ps(void)
{
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    d->XMM_S(0) = float64_to_float32(s->XMM_D(0), &env->sse_status);
    d->XMM_S(1) = float64_to_float32(s->XMM_D(1), &env->sse_status);
    d->Q(1) = 0;
}

void OPPROTO op_cvtss2sd(void)
{
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    d->XMM_D(0) = float32_to_float64(s->XMM_S(0), &env->sse_status);
}

void OPPROTO op_cvtsd2ss(void)
{
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);
    d->XMM_S(0) = float64_to_float32(s->XMM_D(0), &env->sse_status);
}

/* integer to float */
void OPPROTO op_cvtdq2ps(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_S(0) = int32_to_float32(s->XMM_L(0), &env->sse_status);
    d->XMM_S(1) = int32_to_float32(s->XMM_L(1), &env->sse_status);
    d->XMM_S(2) = int32_to_float32(s->XMM_L(2), &env->sse_status);
    d->XMM_S(3) = int32_to_float32(s->XMM_L(3), &env->sse_status);
}

void OPPROTO op_cvtdq2pd(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    int32_t l0, l1;
    l0 = (int32_t)s->XMM_L(0);
    l1 = (int32_t)s->XMM_L(1);
    d->XMM_D(0) = int32_to_float64(l0, &env->sse_status);
    d->XMM_D(1) = int32_to_float64(l1, &env->sse_status);
}

void OPPROTO op_cvtpi2ps(void)
{
    XMMReg *d = (Reg *)((char *)env + PARAM1);
    MMXReg *s = (MMXReg *)((char *)env + PARAM2);
    d->XMM_S(0) = int32_to_float32(s->MMX_L(0), &env->sse_status);
    d->XMM_S(1) = int32_to_float32(s->MMX_L(1), &env->sse_status);
}

void OPPROTO op_cvtpi2pd(void)
{
    XMMReg *d = (Reg *)((char *)env + PARAM1);
    MMXReg *s = (MMXReg *)((char *)env + PARAM2);
    d->XMM_D(0) = int32_to_float64(s->MMX_L(0), &env->sse_status);
    d->XMM_D(1) = int32_to_float64(s->MMX_L(1), &env->sse_status);
}

void OPPROTO op_cvtsi2ss(void)
{
    XMMReg *d = (Reg *)((char *)env + PARAM1);
    d->XMM_S(0) = int32_to_float32(T0, &env->sse_status);
}

void OPPROTO op_cvtsi2sd(void)
{
    XMMReg *d = (Reg *)((char *)env + PARAM1);
    d->XMM_D(0) = int32_to_float64(T0, &env->sse_status);
}

#ifdef TARGET_X86_64
void OPPROTO op_cvtsq2ss(void)
{
    XMMReg *d = (Reg *)((char *)env + PARAM1);
    d->XMM_S(0) = int64_to_float32(T0, &env->sse_status);
}

void OPPROTO op_cvtsq2sd(void)
{
    XMMReg *d = (Reg *)((char *)env + PARAM1);
    d->XMM_D(0) = int64_to_float64(T0, &env->sse_status);
}
#endif

/* float to integer */
void OPPROTO op_cvtps2dq(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_L(0) = float32_to_int32(s->XMM_S(0), &env->sse_status);
    d->XMM_L(1) = float32_to_int32(s->XMM_S(1), &env->sse_status);
    d->XMM_L(2) = float32_to_int32(s->XMM_S(2), &env->sse_status);
    d->XMM_L(3) = float32_to_int32(s->XMM_S(3), &env->sse_status);
}

void OPPROTO op_cvtpd2dq(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_L(0) = float64_to_int32(s->XMM_D(0), &env->sse_status);
    d->XMM_L(1) = float64_to_int32(s->XMM_D(1), &env->sse_status);
    d->XMM_Q(1) = 0;
}

void OPPROTO op_cvtps2pi(void)
{
    MMXReg *d = (MMXReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->MMX_L(0) = float32_to_int32(s->XMM_S(0), &env->sse_status);
    d->MMX_L(1) = float32_to_int32(s->XMM_S(1), &env->sse_status);
}

void OPPROTO op_cvtpd2pi(void)
{
    MMXReg *d = (MMXReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->MMX_L(0) = float64_to_int32(s->XMM_D(0), &env->sse_status);
    d->MMX_L(1) = float64_to_int32(s->XMM_D(1), &env->sse_status);
}

void OPPROTO op_cvtss2si(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float32_to_int32(s->XMM_S(0), &env->sse_status);
}

void OPPROTO op_cvtsd2si(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float64_to_int32(s->XMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
void OPPROTO op_cvtss2sq(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float32_to_int64(s->XMM_S(0), &env->sse_status);
}

void OPPROTO op_cvtsd2sq(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float64_to_int64(s->XMM_D(0), &env->sse_status);
}
#endif

/* float to integer truncated */
void OPPROTO op_cvttps2dq(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_L(0) = float32_to_int32_round_to_zero(s->XMM_S(0), &env->sse_status);
    d->XMM_L(1) = float32_to_int32_round_to_zero(s->XMM_S(1), &env->sse_status);
    d->XMM_L(2) = float32_to_int32_round_to_zero(s->XMM_S(2), &env->sse_status);
    d->XMM_L(3) = float32_to_int32_round_to_zero(s->XMM_S(3), &env->sse_status);
}

void OPPROTO op_cvttpd2dq(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_L(0) = float64_to_int32_round_to_zero(s->XMM_D(0), &env->sse_status);
    d->XMM_L(1) = float64_to_int32_round_to_zero(s->XMM_D(1), &env->sse_status);
    d->XMM_Q(1) = 0;
}

void OPPROTO op_cvttps2pi(void)
{
    MMXReg *d = (MMXReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->MMX_L(0) = float32_to_int32_round_to_zero(s->XMM_S(0), &env->sse_status);
    d->MMX_L(1) = float32_to_int32_round_to_zero(s->XMM_S(1), &env->sse_status);
}

void OPPROTO op_cvttpd2pi(void)
{
    MMXReg *d = (MMXReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->MMX_L(0) = float64_to_int32_round_to_zero(s->XMM_D(0), &env->sse_status);
    d->MMX_L(1) = float64_to_int32_round_to_zero(s->XMM_D(1), &env->sse_status);
}

void OPPROTO op_cvttss2si(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float32_to_int32_round_to_zero(s->XMM_S(0), &env->sse_status);
}

void OPPROTO op_cvttsd2si(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float64_to_int32_round_to_zero(s->XMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
void OPPROTO op_cvttss2sq(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float32_to_int64_round_to_zero(s->XMM_S(0), &env->sse_status);
}

void OPPROTO op_cvttsd2sq(void)
{
    XMMReg *s = (XMMReg *)((char *)env + PARAM1);
    T0 = float64_to_int64_round_to_zero(s->XMM_D(0), &env->sse_status);
}
#endif

void OPPROTO op_rsqrtps(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_S(0) = approx_rsqrt(s->XMM_S(0));
    d->XMM_S(1) = approx_rsqrt(s->XMM_S(1));
    d->XMM_S(2) = approx_rsqrt(s->XMM_S(2));
    d->XMM_S(3) = approx_rsqrt(s->XMM_S(3));
}

void OPPROTO op_rsqrtss(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_S(0) = approx_rsqrt(s->XMM_S(0));
}

void OPPROTO op_rcpps(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_S(0) = approx_rcp(s->XMM_S(0));
    d->XMM_S(1) = approx_rcp(s->XMM_S(1));
    d->XMM_S(2) = approx_rcp(s->XMM_S(2));
    d->XMM_S(3) = approx_rcp(s->XMM_S(3));
}

void OPPROTO op_rcpss(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_S(0) = approx_rcp(s->XMM_S(0));
}

void OPPROTO op_haddps(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    XMMReg r;
    r.XMM_S(0) = d->XMM_S(0) + d->XMM_S(1);
    r.XMM_S(1) = d->XMM_S(2) + d->XMM_S(3);
    r.XMM_S(2) = s->XMM_S(0) + s->XMM_S(1);
    r.XMM_S(3) = s->XMM_S(2) + s->XMM_S(3);
    *d = r;
}

void OPPROTO op_haddpd(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    XMMReg r;
    r.XMM_D(0) = d->XMM_D(0) + d->XMM_D(1);
    r.XMM_D(1) = s->XMM_D(0) + s->XMM_D(1);
    *d = r;
}

void OPPROTO op_hsubps(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    XMMReg r;
    r.XMM_S(0) = d->XMM_S(0) - d->XMM_S(1);
    r.XMM_S(1) = d->XMM_S(2) - d->XMM_S(3);
    r.XMM_S(2) = s->XMM_S(0) - s->XMM_S(1);
    r.XMM_S(3) = s->XMM_S(2) - s->XMM_S(3);
    *d = r;
}

void OPPROTO op_hsubpd(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    XMMReg r;
    r.XMM_D(0) = d->XMM_D(0) - d->XMM_D(1);
    r.XMM_D(1) = s->XMM_D(0) - s->XMM_D(1);
    *d = r;
}

void OPPROTO op_addsubps(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_S(0) = d->XMM_S(0) - s->XMM_S(0);
    d->XMM_S(1) = d->XMM_S(1) + s->XMM_S(1);
    d->XMM_S(2) = d->XMM_S(2) - s->XMM_S(2);
    d->XMM_S(3) = d->XMM_S(3) + s->XMM_S(3);
}

void OPPROTO op_addsubpd(void)
{
    XMMReg *d = (XMMReg *)((char *)env + PARAM1);
    XMMReg *s = (XMMReg *)((char *)env + PARAM2);
    d->XMM_D(0) = d->XMM_D(0) - s->XMM_D(0);
    d->XMM_D(1) = d->XMM_D(1) + s->XMM_D(1);
}

/* XXX: unordered */
#define SSE_OP_CMP(name, F)\
void OPPROTO op_ ## name ## ps (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->XMM_L(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
    d->XMM_L(1) = F(32, d->XMM_S(1), s->XMM_S(1));\
    d->XMM_L(2) = F(32, d->XMM_S(2), s->XMM_S(2));\
    d->XMM_L(3) = F(32, d->XMM_S(3), s->XMM_S(3));\
}\
\
void OPPROTO op_ ## name ## ss (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->XMM_L(0) = F(32, d->XMM_S(0), s->XMM_S(0));\
}\
void OPPROTO op_ ## name ## pd (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
    d->XMM_Q(0) = F(64, d->XMM_D(0), s->XMM_D(0));\
    d->XMM_Q(1) = F(64, d->XMM_D(1), s->XMM_D(1));\
}\
\
void OPPROTO op_ ## name ## sd (void)\
{\
    Reg *d, *s;\
    d = (Reg *)((char *)env + PARAM1);\
    s = (Reg *)((char *)env + PARAM2);\
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

SSE_OP_CMP(cmpeq, FPU_CMPEQ)
SSE_OP_CMP(cmplt, FPU_CMPLT)
SSE_OP_CMP(cmple, FPU_CMPLE)
SSE_OP_CMP(cmpunord, FPU_CMPUNORD)
SSE_OP_CMP(cmpneq, FPU_CMPNEQ)
SSE_OP_CMP(cmpnlt, FPU_CMPNLT)
SSE_OP_CMP(cmpnle, FPU_CMPNLE)
SSE_OP_CMP(cmpord, FPU_CMPORD)

const int comis_eflags[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void OPPROTO op_ucomiss(void)
{
    int ret;
    float32 s0, s1;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

    s0 = d->XMM_S(0);
    s1 = s->XMM_S(0);
    ret = float32_compare_quiet(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

void OPPROTO op_comiss(void)
{
    int ret;
    float32 s0, s1;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

    s0 = d->XMM_S(0);
    s1 = s->XMM_S(0);
    ret = float32_compare(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

void OPPROTO op_ucomisd(void)
{
    int ret;
    float64 d0, d1;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

    d0 = d->XMM_D(0);
    d1 = s->XMM_D(0);
    ret = float64_compare_quiet(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

void OPPROTO op_comisd(void)
{
    int ret;
    float64 d0, d1;
    Reg *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

    d0 = d->XMM_D(0);
    d1 = s->XMM_D(0);
    ret = float64_compare(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
    FORCE_RET();
}

void OPPROTO op_movmskps(void)
{
    int b0, b1, b2, b3;
    Reg *s;
    s = (Reg *)((char *)env + PARAM1);
    b0 = s->XMM_L(0) >> 31;
    b1 = s->XMM_L(1) >> 31;
    b2 = s->XMM_L(2) >> 31;
    b3 = s->XMM_L(3) >> 31;
    T0 = b0 | (b1 << 1) | (b2 << 2) | (b3 << 3);
}

void OPPROTO op_movmskpd(void)
{
    int b0, b1;
    Reg *s;
    s = (Reg *)((char *)env + PARAM1);
    b0 = s->XMM_L(1) >> 31;
    b1 = s->XMM_L(3) >> 31;
    T0 = b0 | (b1 << 1);
}

#endif

void OPPROTO glue(op_pmovmskb, SUFFIX)(void)
{
    Reg *s;
    s = (Reg *)((char *)env + PARAM1);
    T0 = 0;
    T0 |= (s->XMM_B(0) >> 7);
    T0 |= (s->XMM_B(1) >> 6) & 0x02;
    T0 |= (s->XMM_B(2) >> 5) & 0x04;
    T0 |= (s->XMM_B(3) >> 4) & 0x08;
    T0 |= (s->XMM_B(4) >> 3) & 0x10;
    T0 |= (s->XMM_B(5) >> 2) & 0x20;
    T0 |= (s->XMM_B(6) >> 1) & 0x40;
    T0 |= (s->XMM_B(7)) & 0x80;
#if SHIFT == 1
    T0 |= (s->XMM_B(8) << 1) & 0x0100;
    T0 |= (s->XMM_B(9) << 2) & 0x0200;
    T0 |= (s->XMM_B(10) << 3) & 0x0400;
    T0 |= (s->XMM_B(11) << 4) & 0x0800;
    T0 |= (s->XMM_B(12) << 5) & 0x1000;
    T0 |= (s->XMM_B(13) << 6) & 0x2000;
    T0 |= (s->XMM_B(14) << 7) & 0x4000;
    T0 |= (s->XMM_B(15) << 8) & 0x8000;
#endif
}

void OPPROTO glue(op_pinsrw, SUFFIX) (void)
{
    Reg *d = (Reg *)((char *)env + PARAM1);
    int pos = PARAM2;

    d->W(pos) = T0;
}

void OPPROTO glue(op_pextrw, SUFFIX) (void)
{
    Reg *s = (Reg *)((char *)env + PARAM1);
    int pos = PARAM2;

    T0 = s->W(pos);
}

void OPPROTO glue(op_packsswb, SUFFIX) (void)
{
    Reg r, *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_packuswb, SUFFIX) (void)
{
    Reg r, *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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

void OPPROTO glue(op_packssdw, SUFFIX) (void)
{
    Reg r, *d, *s;
    d = (Reg *)((char *)env + PARAM1);
    s = (Reg *)((char *)env + PARAM2);

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
void OPPROTO glue(op_punpck ## base_name ## bw, SUFFIX) (void)   \
{                                                               \
    Reg r, *d, *s;                                              \
    d = (Reg *)((char *)env + PARAM1);                          \
    s = (Reg *)((char *)env + PARAM2);                          \
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
void OPPROTO glue(op_punpck ## base_name ## wd, SUFFIX) (void)   \
{                                                               \
    Reg r, *d, *s;                                              \
    d = (Reg *)((char *)env + PARAM1);                          \
    s = (Reg *)((char *)env + PARAM2);                          \
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
void OPPROTO glue(op_punpck ## base_name ## dq, SUFFIX) (void)   \
{                                                               \
    Reg r, *d, *s;                                              \
    d = (Reg *)((char *)env + PARAM1);                          \
    s = (Reg *)((char *)env + PARAM2);                          \
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
void OPPROTO glue(op_punpck ## base_name ## qdq, SUFFIX) (void)  \
{                                                               \
    Reg r, *d, *s;                                              \
    d = (Reg *)((char *)env + PARAM1);                          \
    s = (Reg *)((char *)env + PARAM2);                          \
                                                                \
    r.Q(0) = d->Q(base);                                        \
    r.Q(1) = s->Q(base);                                        \
    *d = r;                                                     \
}                                                               \
)

UNPCK_OP(l, 0)
UNPCK_OP(h, 1)

#undef SHIFT
#undef XMM_ONLY
#undef Reg
#undef B
#undef W
#undef L
#undef Q
#undef SUFFIX
