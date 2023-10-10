/*
 *  MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI support
 *
 *  Copyright (c) 2005 Fabrice Bellard
 *  Copyright (c) 2008 Intel Corporation  <andrew.zaborowski@intel.com>
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

#include "crypto/aes.h"
#include "crypto/aes-round.h"
#include "crypto/clmul.h"

#if SHIFT == 0
#define Reg MMXReg
#define XMM_ONLY(...)
#define B(n) MMX_B(n)
#define W(n) MMX_W(n)
#define L(n) MMX_L(n)
#define Q(n) MMX_Q(n)
#define SUFFIX _mmx
#else
#define Reg ZMMReg
#define XMM_ONLY(...) __VA_ARGS__
#define B(n) ZMM_B(n)
#define W(n) ZMM_W(n)
#define L(n) ZMM_L(n)
#define Q(n) ZMM_Q(n)
#if SHIFT == 1
#define SUFFIX _xmm
#else
#define SUFFIX _ymm
#endif
#endif

#define LANE_WIDTH (SHIFT ? 16 : 8)
#define PACK_WIDTH (LANE_WIDTH / 2)

#if SHIFT == 0
#define FPSRL(x, c) ((x) >> shift)
#define FPSRAW(x, c) ((int16_t)(x) >> shift)
#define FPSRAL(x, c) ((int32_t)(x) >> shift)
#define FPSLL(x, c) ((x) << shift)
#endif

void glue(helper_psrlw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 15) {
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = 0;
        }
    } else {
        shift = c->B(0);
        for (int i = 0; i < 4 << SHIFT; i++) {
            d->W(i) = FPSRL(s->W(i), shift);
        }
    }
}

void glue(helper_psllw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 15) {
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = 0;
        }
    } else {
        shift = c->B(0);
        for (int i = 0; i < 4 << SHIFT; i++) {
            d->W(i) = FPSLL(s->W(i), shift);
        }
    }
}

void glue(helper_psraw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 15) {
        shift = 15;
    } else {
        shift = c->B(0);
    }
    for (int i = 0; i < 4 << SHIFT; i++) {
        d->W(i) = FPSRAW(s->W(i), shift);
    }
}

void glue(helper_psrld, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 31) {
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = 0;
        }
    } else {
        shift = c->B(0);
        for (int i = 0; i < 2 << SHIFT; i++) {
            d->L(i) = FPSRL(s->L(i), shift);
        }
    }
}

void glue(helper_pslld, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 31) {
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = 0;
        }
    } else {
        shift = c->B(0);
        for (int i = 0; i < 2 << SHIFT; i++) {
            d->L(i) = FPSLL(s->L(i), shift);
        }
    }
}

void glue(helper_psrad, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 31) {
        shift = 31;
    } else {
        shift = c->B(0);
    }
    for (int i = 0; i < 2 << SHIFT; i++) {
        d->L(i) = FPSRAL(s->L(i), shift);
    }
}

void glue(helper_psrlq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 63) {
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = 0;
        }
    } else {
        shift = c->B(0);
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = FPSRL(s->Q(i), shift);
        }
    }
}

void glue(helper_psllq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift;
    if (c->Q(0) > 63) {
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = 0;
        }
    } else {
        shift = c->B(0);
        for (int i = 0; i < 1 << SHIFT; i++) {
            d->Q(i) = FPSLL(s->Q(i), shift);
        }
    }
}

#if SHIFT >= 1
void glue(helper_psrldq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift, i, j;

    shift = c->L(0);
    if (shift > 16) {
        shift = 16;
    }
    for (j = 0; j < 8 << SHIFT; j += LANE_WIDTH) {
        for (i = 0; i < 16 - shift; i++) {
            d->B(j + i) = s->B(j + i + shift);
        }
        for (i = 16 - shift; i < 16; i++) {
            d->B(j + i) = 0;
        }
    }
}

void glue(helper_pslldq, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, Reg *c)
{
    int shift, i, j;

    shift = c->L(0);
    if (shift > 16) {
        shift = 16;
    }
    for (j = 0; j < 8 << SHIFT; j += LANE_WIDTH) {
        for (i = 15; i >= shift; i--) {
            d->B(j + i) = s->B(j + i - shift);
        }
        for (i = 0; i < shift; i++) {
            d->B(j + i) = 0;
        }
    }
}
#endif

#define SSE_HELPER_1(name, elem, num, F)                        \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)   \
    {                                                           \
        int n = num;                                            \
        for (int i = 0; i < n; i++) {                           \
            d->elem(i) = F(s->elem(i));                         \
        }                                                       \
    }

#define SSE_HELPER_2(name, elem, num, F)                        \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)   \
    {                                                           \
        int n = num;                                            \
        for (int i = 0; i < n; i++) {                           \
            d->elem(i) = F(v->elem(i), s->elem(i));             \
        }                                                       \
    }

#define SSE_HELPER_B(name, F)                                   \
    SSE_HELPER_2(name, B, 8 << SHIFT, F)

#define SSE_HELPER_W(name, F)                                   \
    SSE_HELPER_2(name, W, 4 << SHIFT, F)

#define SSE_HELPER_L(name, F)                                   \
    SSE_HELPER_2(name, L, 2 << SHIFT, F)

#define SSE_HELPER_Q(name, F)                                   \
    SSE_HELPER_2(name, Q, 1 << SHIFT, F)

#if SHIFT == 0
static inline int satub(int x)
{
    if (x < 0) {
        return 0;
    } else if (x > 255) {
        return 255;
    } else {
        return x;
    }
}

static inline int satuw(int x)
{
    if (x < 0) {
        return 0;
    } else if (x > 65535) {
        return 65535;
    } else {
        return x;
    }
}

static inline int satsb(int x)
{
    if (x < -128) {
        return -128;
    } else if (x > 127) {
        return 127;
    } else {
        return x;
    }
}

static inline int satsw(int x)
{
    if (x < -32768) {
        return -32768;
    } else if (x > 32767) {
        return 32767;
    } else {
        return x;
    }
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

#define FMULHRW(a, b) (((int16_t)(a) * (int16_t)(b) + 0x8000) >> 16)
#define FMULHUW(a, b) ((a) * (b) >> 16)
#define FMULHW(a, b) ((int16_t)(a) * (int16_t)(b) >> 16)

#define FAVG(a, b) (((a) + (b) + 1) >> 1)
#endif

SSE_HELPER_W(helper_pmulhuw, FMULHUW)
SSE_HELPER_W(helper_pmulhw, FMULHW)

#if SHIFT == 0
void glue(helper_pmulhrw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    d->W(0) = FMULHRW(d->W(0), s->W(0));
    d->W(1) = FMULHRW(d->W(1), s->W(1));
    d->W(2) = FMULHRW(d->W(2), s->W(2));
    d->W(3) = FMULHRW(d->W(3), s->W(3));
}
#endif

SSE_HELPER_B(helper_pavgb, FAVG)
SSE_HELPER_W(helper_pavgw, FAVG)

void glue(helper_pmuludq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;

    for (i = 0; i < (1 << SHIFT); i++) {
        d->Q(i) = (uint64_t)s->L(i * 2) * (uint64_t)v->L(i * 2);
    }
}

void glue(helper_pmaddwd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;

    for (i = 0; i < (2 << SHIFT); i++) {
        d->L(i) = (int16_t)s->W(2 * i) * (int16_t)v->W(2 * i) +
            (int16_t)s->W(2 * i + 1) * (int16_t)v->W(2 * i + 1);
    }
}

#if SHIFT == 0
static inline int abs1(int a)
{
    if (a < 0) {
        return -a;
    } else {
        return a;
    }
}
#endif
void glue(helper_psadbw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;

    for (i = 0; i < (1 << SHIFT); i++) {
        unsigned int val = 0;
        val += abs1(v->B(8 * i + 0) - s->B(8 * i + 0));
        val += abs1(v->B(8 * i + 1) - s->B(8 * i + 1));
        val += abs1(v->B(8 * i + 2) - s->B(8 * i + 2));
        val += abs1(v->B(8 * i + 3) - s->B(8 * i + 3));
        val += abs1(v->B(8 * i + 4) - s->B(8 * i + 4));
        val += abs1(v->B(8 * i + 5) - s->B(8 * i + 5));
        val += abs1(v->B(8 * i + 6) - s->B(8 * i + 6));
        val += abs1(v->B(8 * i + 7) - s->B(8 * i + 7));
        d->Q(i) = val;
    }
}

#if SHIFT < 2
void glue(helper_maskmov, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                  target_ulong a0)
{
    int i;

    for (i = 0; i < (8 << SHIFT); i++) {
        if (s->B(i) & 0x80) {
            cpu_stb_data_ra(env, a0 + i, d->B(i), GETPC());
        }
    }
}
#endif

#define SHUFFLE4(F, a, b, offset) do {      \
    r0 = a->F((order & 3) + offset);        \
    r1 = a->F(((order >> 2) & 3) + offset); \
    r2 = b->F(((order >> 4) & 3) + offset); \
    r3 = b->F(((order >> 6) & 3) + offset); \
    d->F(offset) = r0;                      \
    d->F(offset + 1) = r1;                  \
    d->F(offset + 2) = r2;                  \
    d->F(offset + 3) = r3;                  \
    } while (0)

#if SHIFT == 0
void glue(helper_pshufw, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint16_t r0, r1, r2, r3;

    SHUFFLE4(W, s, s, 0);
}
#else
void glue(helper_shufps, SUFFIX)(Reg *d, Reg *v, Reg *s, int order)
{
    uint32_t r0, r1, r2, r3;
    int i;

    for (i = 0; i < 2 << SHIFT; i += 4) {
        SHUFFLE4(L, v, s, i);
    }
}

void glue(helper_shufpd, SUFFIX)(Reg *d, Reg *v, Reg *s, int order)
{
    uint64_t r0, r1;
    int i;

    for (i = 0; i < 1 << SHIFT; i += 2) {
        r0 = v->Q(((order & 1) & 1) + i);
        r1 = s->Q(((order >> 1) & 1) + i);
        d->Q(i) = r0;
        d->Q(i + 1) = r1;
        order >>= 2;
    }
}

void glue(helper_pshufd, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint32_t r0, r1, r2, r3;
    int i;

    for (i = 0; i < 2 << SHIFT; i += 4) {
        SHUFFLE4(L, s, s, i);
    }
}

void glue(helper_pshuflw, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint16_t r0, r1, r2, r3;
    int i, j;

    for (i = 0, j = 1; j < 1 << SHIFT; i += 8, j += 2) {
        SHUFFLE4(W, s, s, i);
        d->Q(j) = s->Q(j);
    }
}

void glue(helper_pshufhw, SUFFIX)(Reg *d, Reg *s, int order)
{
    uint16_t r0, r1, r2, r3;
    int i, j;

    for (i = 4, j = 0; j < 1 << SHIFT; i += 8, j += 2) {
        d->Q(j) = s->Q(j);
        SHUFFLE4(W, s, s, i);
    }
}
#endif

#if SHIFT >= 1
/* FPU ops */
/* XXX: not accurate */

#define SSE_HELPER_P(name, F)                                           \
    void glue(helper_ ## name ## ps, SUFFIX)(CPUX86State *env,          \
            Reg *d, Reg *v, Reg *s)                                     \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < 2 << SHIFT; i++) {                              \
            d->ZMM_S(i) = F(32, v->ZMM_S(i), s->ZMM_S(i));              \
        }                                                               \
    }                                                                   \
                                                                        \
    void glue(helper_ ## name ## pd, SUFFIX)(CPUX86State *env,          \
            Reg *d, Reg *v, Reg *s)                                     \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < 1 << SHIFT; i++) {                              \
            d->ZMM_D(i) = F(64, v->ZMM_D(i), s->ZMM_D(i));              \
        }                                                               \
    }

#if SHIFT == 1

#define SSE_HELPER_S(name, F)                                           \
    SSE_HELPER_P(name, F)                                               \
                                                                        \
    void helper_ ## name ## ss(CPUX86State *env, Reg *d, Reg *v, Reg *s)\
    {                                                                   \
        int i;                                                          \
        d->ZMM_S(0) = F(32, v->ZMM_S(0), s->ZMM_S(0));                  \
        for (i = 1; i < 2 << SHIFT; i++) {                              \
            d->ZMM_L(i) = v->ZMM_L(i);                                  \
        }                                                               \
    }                                                                   \
                                                                        \
    void helper_ ## name ## sd(CPUX86State *env, Reg *d, Reg *v, Reg *s)\
    {                                                                   \
        int i;                                                          \
        d->ZMM_D(0) = F(64, v->ZMM_D(0), s->ZMM_D(0));                  \
        for (i = 1; i < 1 << SHIFT; i++) {                              \
            d->ZMM_Q(i) = v->ZMM_Q(i);                                  \
        }                                                               \
    }

#else

#define SSE_HELPER_S(name, F) SSE_HELPER_P(name, F)

#endif

#define FPU_ADD(size, a, b) float ## size ## _add(a, b, &env->sse_status)
#define FPU_SUB(size, a, b) float ## size ## _sub(a, b, &env->sse_status)
#define FPU_MUL(size, a, b) float ## size ## _mul(a, b, &env->sse_status)
#define FPU_DIV(size, a, b) float ## size ## _div(a, b, &env->sse_status)

/* Note that the choice of comparison op here is important to get the
 * special cases right: for min and max Intel specifies that (-0,0),
 * (NaN, anything) and (anything, NaN) return the second argument.
 */
#define FPU_MIN(size, a, b)                                     \
    (float ## size ## _lt(a, b, &env->sse_status) ? (a) : (b))
#define FPU_MAX(size, a, b)                                     \
    (float ## size ## _lt(b, a, &env->sse_status) ? (a) : (b))

SSE_HELPER_S(add, FPU_ADD)
SSE_HELPER_S(sub, FPU_SUB)
SSE_HELPER_S(mul, FPU_MUL)
SSE_HELPER_S(div, FPU_DIV)
SSE_HELPER_S(min, FPU_MIN)
SSE_HELPER_S(max, FPU_MAX)

void glue(helper_sqrtps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;
    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_S(i) = float32_sqrt(s->ZMM_S(i), &env->sse_status);
    }
}

void glue(helper_sqrtpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;
    for (i = 0; i < 1 << SHIFT; i++) {
        d->ZMM_D(i) = float64_sqrt(s->ZMM_D(i), &env->sse_status);
    }
}

#if SHIFT == 1
void helper_sqrtss(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    d->ZMM_S(0) = float32_sqrt(s->ZMM_S(0), &env->sse_status);
    for (i = 1; i < 2 << SHIFT; i++) {
        d->ZMM_L(i) = v->ZMM_L(i);
    }
}

void helper_sqrtsd(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    d->ZMM_D(0) = float64_sqrt(s->ZMM_D(0), &env->sse_status);
    for (i = 1; i < 1 << SHIFT; i++) {
        d->ZMM_Q(i) = v->ZMM_Q(i);
    }
}
#endif

/* float to float conversions */
void glue(helper_cvtps2pd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;
    for (i = 1 << SHIFT; --i >= 0; ) {
        d->ZMM_D(i) = float32_to_float64(s->ZMM_S(i), &env->sse_status);
    }
}

void glue(helper_cvtpd2ps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;
    for (i = 0; i < 1 << SHIFT; i++) {
         d->ZMM_S(i) = float64_to_float32(s->ZMM_D(i), &env->sse_status);
    }
    for (i >>= 1; i < 1 << SHIFT; i++) {
         d->Q(i) = 0;
    }
}

#if SHIFT >= 1
void glue(helper_cvtph2ps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;

    for (i = 2 << SHIFT; --i >= 0; ) {
         d->ZMM_S(i) = float16_to_float32(s->ZMM_H(i), true, &env->sse_status);
    }
}

void glue(helper_cvtps2ph, SUFFIX)(CPUX86State *env, Reg *d, Reg *s, int mode)
{
    int i;
    FloatRoundMode prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        set_x86_rounding_mode(mode & 3, &env->sse_status);
    }

    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_H(i) = float32_to_float16(s->ZMM_S(i), true, &env->sse_status);
    }
    for (i >>= 2; i < 1 << SHIFT; i++) {
        d->Q(i) = 0;
    }

    env->sse_status.float_rounding_mode = prev_rounding_mode;
}
#endif

#if SHIFT == 1
void helper_cvtss2sd(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    d->ZMM_D(0) = float32_to_float64(s->ZMM_S(0), &env->sse_status);
    for (i = 1; i < 1 << SHIFT; i++) {
        d->ZMM_Q(i) = v->ZMM_Q(i);
    }
}

void helper_cvtsd2ss(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    d->ZMM_S(0) = float64_to_float32(s->ZMM_D(0), &env->sse_status);
    for (i = 1; i < 2 << SHIFT; i++) {
        d->ZMM_L(i) = v->ZMM_L(i);
    }
}
#endif

/* integer to float */
void glue(helper_cvtdq2ps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;
    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_S(i) = int32_to_float32(s->ZMM_L(i), &env->sse_status);
    }
}

void glue(helper_cvtdq2pd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int i;
    for (i = 1 << SHIFT; --i >= 0; ) {
        int32_t l = s->ZMM_L(i);
        d->ZMM_D(i) = int32_to_float64(l, &env->sse_status);
    }
}

#if SHIFT == 1
void helper_cvtpi2ps(CPUX86State *env, ZMMReg *d, MMXReg *s)
{
    d->ZMM_S(0) = int32_to_float32(s->MMX_L(0), &env->sse_status);
    d->ZMM_S(1) = int32_to_float32(s->MMX_L(1), &env->sse_status);
}

void helper_cvtpi2pd(CPUX86State *env, ZMMReg *d, MMXReg *s)
{
    d->ZMM_D(0) = int32_to_float64(s->MMX_L(0), &env->sse_status);
    d->ZMM_D(1) = int32_to_float64(s->MMX_L(1), &env->sse_status);
}

void helper_cvtsi2ss(CPUX86State *env, ZMMReg *d, uint32_t val)
{
    d->ZMM_S(0) = int32_to_float32(val, &env->sse_status);
}

void helper_cvtsi2sd(CPUX86State *env, ZMMReg *d, uint32_t val)
{
    d->ZMM_D(0) = int32_to_float64(val, &env->sse_status);
}

#ifdef TARGET_X86_64
void helper_cvtsq2ss(CPUX86State *env, ZMMReg *d, uint64_t val)
{
    d->ZMM_S(0) = int64_to_float32(val, &env->sse_status);
}

void helper_cvtsq2sd(CPUX86State *env, ZMMReg *d, uint64_t val)
{
    d->ZMM_D(0) = int64_to_float64(val, &env->sse_status);
}
#endif

#endif

/* float to integer */

#if SHIFT == 1
/*
 * x86 mandates that we return the indefinite integer value for the result
 * of any float-to-integer conversion that raises the 'invalid' exception.
 * Wrap the softfloat functions to get this behaviour.
 */
#define WRAP_FLOATCONV(RETTYPE, FN, FLOATTYPE, INDEFVALUE)              \
    static inline RETTYPE x86_##FN(FLOATTYPE a, float_status *s)        \
    {                                                                   \
        int oldflags, newflags;                                         \
        RETTYPE r;                                                      \
                                                                        \
        oldflags = get_float_exception_flags(s);                        \
        set_float_exception_flags(0, s);                                \
        r = FN(a, s);                                                   \
        newflags = get_float_exception_flags(s);                        \
        if (newflags & float_flag_invalid) {                            \
            r = INDEFVALUE;                                             \
        }                                                               \
        set_float_exception_flags(newflags | oldflags, s);              \
        return r;                                                       \
    }

WRAP_FLOATCONV(int32_t, float32_to_int32, float32, INT32_MIN)
WRAP_FLOATCONV(int32_t, float32_to_int32_round_to_zero, float32, INT32_MIN)
WRAP_FLOATCONV(int32_t, float64_to_int32, float64, INT32_MIN)
WRAP_FLOATCONV(int32_t, float64_to_int32_round_to_zero, float64, INT32_MIN)
WRAP_FLOATCONV(int64_t, float32_to_int64, float32, INT64_MIN)
WRAP_FLOATCONV(int64_t, float32_to_int64_round_to_zero, float32, INT64_MIN)
WRAP_FLOATCONV(int64_t, float64_to_int64, float64, INT64_MIN)
WRAP_FLOATCONV(int64_t, float64_to_int64_round_to_zero, float64, INT64_MIN)
#endif

void glue(helper_cvtps2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    int i;
    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_L(i) = x86_float32_to_int32(s->ZMM_S(i), &env->sse_status);
    }
}

void glue(helper_cvtpd2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    int i;
    for (i = 0; i < 1 << SHIFT; i++) {
        d->ZMM_L(i) = x86_float64_to_int32(s->ZMM_D(i), &env->sse_status);
    }
    for (i >>= 1; i < 1 << SHIFT; i++) {
         d->Q(i) = 0;
    }
}

#if SHIFT == 1
void helper_cvtps2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float32_to_int32(s->ZMM_S(0), &env->sse_status);
    d->MMX_L(1) = x86_float32_to_int32(s->ZMM_S(1), &env->sse_status);
}

void helper_cvtpd2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float64_to_int32(s->ZMM_D(0), &env->sse_status);
    d->MMX_L(1) = x86_float64_to_int32(s->ZMM_D(1), &env->sse_status);
}

int32_t helper_cvtss2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int32(s->ZMM_S(0), &env->sse_status);
}

int32_t helper_cvtsd2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int32(s->ZMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
int64_t helper_cvtss2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int64(s->ZMM_S(0), &env->sse_status);
}

int64_t helper_cvtsd2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int64(s->ZMM_D(0), &env->sse_status);
}
#endif
#endif

/* float to integer truncated */
void glue(helper_cvttps2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    int i;
    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_L(i) = x86_float32_to_int32_round_to_zero(s->ZMM_S(i),
                                                         &env->sse_status);
    }
}

void glue(helper_cvttpd2dq, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    int i;
    for (i = 0; i < 1 << SHIFT; i++) {
        d->ZMM_L(i) = x86_float64_to_int32_round_to_zero(s->ZMM_D(i),
                                                         &env->sse_status);
    }
    for (i >>= 1; i < 1 << SHIFT; i++) {
         d->Q(i) = 0;
    }
}

#if SHIFT == 1
void helper_cvttps2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float32_to_int32_round_to_zero(s->ZMM_S(0), &env->sse_status);
    d->MMX_L(1) = x86_float32_to_int32_round_to_zero(s->ZMM_S(1), &env->sse_status);
}

void helper_cvttpd2pi(CPUX86State *env, MMXReg *d, ZMMReg *s)
{
    d->MMX_L(0) = x86_float64_to_int32_round_to_zero(s->ZMM_D(0), &env->sse_status);
    d->MMX_L(1) = x86_float64_to_int32_round_to_zero(s->ZMM_D(1), &env->sse_status);
}

int32_t helper_cvttss2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int32_round_to_zero(s->ZMM_S(0), &env->sse_status);
}

int32_t helper_cvttsd2si(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int32_round_to_zero(s->ZMM_D(0), &env->sse_status);
}

#ifdef TARGET_X86_64
int64_t helper_cvttss2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float32_to_int64_round_to_zero(s->ZMM_S(0), &env->sse_status);
}

int64_t helper_cvttsd2sq(CPUX86State *env, ZMMReg *s)
{
    return x86_float64_to_int64_round_to_zero(s->ZMM_D(0), &env->sse_status);
}
#endif
#endif

void glue(helper_rsqrtps, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    int i;
    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_S(i) = float32_div(float32_one,
                                  float32_sqrt(s->ZMM_S(i), &env->sse_status),
                                  &env->sse_status);
    }
    set_float_exception_flags(old_flags, &env->sse_status);
}

#if SHIFT == 1
void helper_rsqrtss(CPUX86State *env, ZMMReg *d, ZMMReg *v, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    int i;
    d->ZMM_S(0) = float32_div(float32_one,
                              float32_sqrt(s->ZMM_S(0), &env->sse_status),
                              &env->sse_status);
    set_float_exception_flags(old_flags, &env->sse_status);
    for (i = 1; i < 2 << SHIFT; i++) {
        d->ZMM_L(i) = v->ZMM_L(i);
    }
}
#endif

void glue(helper_rcpps, SUFFIX)(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    int i;
    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_S(i) = float32_div(float32_one, s->ZMM_S(i), &env->sse_status);
    }
    set_float_exception_flags(old_flags, &env->sse_status);
}

#if SHIFT == 1
void helper_rcpss(CPUX86State *env, ZMMReg *d, ZMMReg *v, ZMMReg *s)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    int i;
    d->ZMM_S(0) = float32_div(float32_one, s->ZMM_S(0), &env->sse_status);
    for (i = 1; i < 2 << SHIFT; i++) {
        d->ZMM_L(i) = v->ZMM_L(i);
    }
    set_float_exception_flags(old_flags, &env->sse_status);
}
#endif

#if SHIFT == 1
static inline uint64_t helper_extrq(uint64_t src, int shift, int len)
{
    uint64_t mask;

    if (len == 0) {
        mask = ~0LL;
    } else {
        mask = (1ULL << len) - 1;
    }
    return (src >> shift) & mask;
}

void helper_extrq_r(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_Q(0) = helper_extrq(d->ZMM_Q(0), s->ZMM_B(1) & 63, s->ZMM_B(0) & 63);
}

void helper_extrq_i(CPUX86State *env, ZMMReg *d, int index, int length)
{
    d->ZMM_Q(0) = helper_extrq(d->ZMM_Q(0), index, length);
}

static inline uint64_t helper_insertq(uint64_t dest, uint64_t src, int shift, int len)
{
    uint64_t mask;

    if (len == 0) {
        mask = ~0ULL;
    } else {
        mask = (1ULL << len) - 1;
    }
    return (dest & ~(mask << shift)) | ((src & mask) << shift);
}

void helper_insertq_r(CPUX86State *env, ZMMReg *d, ZMMReg *s)
{
    d->ZMM_Q(0) = helper_insertq(d->ZMM_Q(0), s->ZMM_Q(0), s->ZMM_B(9) & 63, s->ZMM_B(8) & 63);
}

void helper_insertq_i(CPUX86State *env, ZMMReg *d, ZMMReg *s, int index, int length)
{
    d->ZMM_Q(0) = helper_insertq(d->ZMM_Q(0), s->ZMM_Q(0), index, length);
}
#endif

#define SSE_HELPER_HPS(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                                                                 \
    float32 r[2 << SHIFT];                                        \
    int i, j, k;                                                  \
    for (k = 0; k < 2 << SHIFT; k += LANE_WIDTH / 4) {            \
        for (i = j = 0; j < 4; i++, j += 2) {                     \
            r[i + k] = F(v->ZMM_S(j + k), v->ZMM_S(j + k + 1), &env->sse_status); \
        }                                                         \
        for (j = 0; j < 4; i++, j += 2) {                         \
            r[i + k] = F(s->ZMM_S(j + k), s->ZMM_S(j + k + 1), &env->sse_status); \
        }                                                         \
    }                                                             \
    for (i = 0; i < 2 << SHIFT; i++) {                            \
        d->ZMM_S(i) = r[i];                                       \
    }                                                             \
}

SSE_HELPER_HPS(haddps, float32_add)
SSE_HELPER_HPS(hsubps, float32_sub)

#define SSE_HELPER_HPD(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                                                                 \
    float64 r[1 << SHIFT];                                        \
    int i, j, k;                                                  \
    for (k = 0; k < 1 << SHIFT; k += LANE_WIDTH / 8) {            \
        for (i = j = 0; j < 2; i++, j += 2) {                     \
            r[i + k] = F(v->ZMM_D(j + k), v->ZMM_D(j + k + 1), &env->sse_status); \
        }                                                         \
        for (j = 0; j < 2; i++, j += 2) {                         \
            r[i + k] = F(s->ZMM_D(j + k), s->ZMM_D(j + k + 1), &env->sse_status); \
        }                                                         \
    }                                                             \
    for (i = 0; i < 1 << SHIFT; i++) {                            \
        d->ZMM_D(i) = r[i];                                       \
    }                                                             \
}

SSE_HELPER_HPD(haddpd, float64_add)
SSE_HELPER_HPD(hsubpd, float64_sub)

void glue(helper_addsubps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    for (i = 0; i < 2 << SHIFT; i += 2) {
        d->ZMM_S(i) = float32_sub(v->ZMM_S(i), s->ZMM_S(i), &env->sse_status);
        d->ZMM_S(i+1) = float32_add(v->ZMM_S(i+1), s->ZMM_S(i+1), &env->sse_status);
    }
}

void glue(helper_addsubpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    for (i = 0; i < 1 << SHIFT; i += 2) {
        d->ZMM_D(i) = float64_sub(v->ZMM_D(i), s->ZMM_D(i), &env->sse_status);
        d->ZMM_D(i+1) = float64_add(v->ZMM_D(i+1), s->ZMM_D(i+1), &env->sse_status);
    }
}

#define SSE_HELPER_CMP_P(name, F, C)                                    \
    void glue(helper_ ## name ## ps, SUFFIX)(CPUX86State *env,          \
                                             Reg *d, Reg *v, Reg *s)    \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < 2 << SHIFT; i++) {                              \
            d->ZMM_L(i) = C(F(32, v->ZMM_S(i), s->ZMM_S(i))) ? -1 : 0;  \
        }                                                               \
    }                                                                   \
                                                                        \
    void glue(helper_ ## name ## pd, SUFFIX)(CPUX86State *env,          \
                                             Reg *d, Reg *v, Reg *s)    \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < 1 << SHIFT; i++) {                              \
            d->ZMM_Q(i) = C(F(64, v->ZMM_D(i), s->ZMM_D(i))) ? -1 : 0;  \
        }                                                               \
    }

#if SHIFT == 1
#define SSE_HELPER_CMP(name, F, C)                                          \
    SSE_HELPER_CMP_P(name, F, C)                                            \
    void helper_ ## name ## ss(CPUX86State *env, Reg *d, Reg *v, Reg *s)    \
    {                                                                       \
        int i;                                                              \
        d->ZMM_L(0) = C(F(32, v->ZMM_S(0), s->ZMM_S(0))) ? -1 : 0;          \
        for (i = 1; i < 2 << SHIFT; i++) {                                  \
            d->ZMM_L(i) = v->ZMM_L(i);                                      \
        }                                                                   \
    }                                                                       \
                                                                            \
    void helper_ ## name ## sd(CPUX86State *env, Reg *d, Reg *v, Reg *s)    \
    {                                                                       \
        int i;                                                              \
        d->ZMM_Q(0) = C(F(64, v->ZMM_D(0), s->ZMM_D(0))) ? -1 : 0;          \
        for (i = 1; i < 1 << SHIFT; i++) {                                  \
            d->ZMM_Q(i) = v->ZMM_Q(i);                                      \
        }                                                                   \
    }

static inline bool FPU_EQU(FloatRelation x)
{
    return (x == float_relation_equal || x == float_relation_unordered);
}
static inline bool FPU_GE(FloatRelation x)
{
    return (x == float_relation_equal || x == float_relation_greater);
}
#define FPU_EQ(x) (x == float_relation_equal)
#define FPU_LT(x) (x == float_relation_less)
#define FPU_LE(x) (x <= float_relation_equal)
#define FPU_GT(x) (x == float_relation_greater)
#define FPU_UNORD(x) (x == float_relation_unordered)
/* We must make sure we evaluate the argument in case it is a signalling NAN */
#define FPU_FALSE(x) (x == float_relation_equal && 0)

#define FPU_CMPQ(size, a, b) \
    float ## size ## _compare_quiet(a, b, &env->sse_status)
#define FPU_CMPS(size, a, b) \
    float ## size ## _compare(a, b, &env->sse_status)

#else
#define SSE_HELPER_CMP(name, F, C) SSE_HELPER_CMP_P(name, F, C)
#endif

SSE_HELPER_CMP(cmpeq, FPU_CMPQ, FPU_EQ)
SSE_HELPER_CMP(cmplt, FPU_CMPS, FPU_LT)
SSE_HELPER_CMP(cmple, FPU_CMPS, FPU_LE)
SSE_HELPER_CMP(cmpunord, FPU_CMPQ,  FPU_UNORD)
SSE_HELPER_CMP(cmpneq, FPU_CMPQ, !FPU_EQ)
SSE_HELPER_CMP(cmpnlt, FPU_CMPS, !FPU_LT)
SSE_HELPER_CMP(cmpnle, FPU_CMPS, !FPU_LE)
SSE_HELPER_CMP(cmpord, FPU_CMPQ, !FPU_UNORD)

SSE_HELPER_CMP(cmpequ, FPU_CMPQ, FPU_EQU)
SSE_HELPER_CMP(cmpnge, FPU_CMPS, !FPU_GE)
SSE_HELPER_CMP(cmpngt, FPU_CMPS, !FPU_GT)
SSE_HELPER_CMP(cmpfalse, FPU_CMPQ,  FPU_FALSE)
SSE_HELPER_CMP(cmpnequ, FPU_CMPQ, !FPU_EQU)
SSE_HELPER_CMP(cmpge, FPU_CMPS, FPU_GE)
SSE_HELPER_CMP(cmpgt, FPU_CMPS, FPU_GT)
SSE_HELPER_CMP(cmptrue, FPU_CMPQ,  !FPU_FALSE)

SSE_HELPER_CMP(cmpeqs, FPU_CMPS, FPU_EQ)
SSE_HELPER_CMP(cmpltq, FPU_CMPQ, FPU_LT)
SSE_HELPER_CMP(cmpleq, FPU_CMPQ, FPU_LE)
SSE_HELPER_CMP(cmpunords, FPU_CMPS,  FPU_UNORD)
SSE_HELPER_CMP(cmpneqq, FPU_CMPS, !FPU_EQ)
SSE_HELPER_CMP(cmpnltq, FPU_CMPQ, !FPU_LT)
SSE_HELPER_CMP(cmpnleq, FPU_CMPQ, !FPU_LE)
SSE_HELPER_CMP(cmpords, FPU_CMPS, !FPU_UNORD)

SSE_HELPER_CMP(cmpequs, FPU_CMPS, FPU_EQU)
SSE_HELPER_CMP(cmpngeq, FPU_CMPQ, !FPU_GE)
SSE_HELPER_CMP(cmpngtq, FPU_CMPQ, !FPU_GT)
SSE_HELPER_CMP(cmpfalses, FPU_CMPS,  FPU_FALSE)
SSE_HELPER_CMP(cmpnequs, FPU_CMPS, !FPU_EQU)
SSE_HELPER_CMP(cmpgeq, FPU_CMPQ, FPU_GE)
SSE_HELPER_CMP(cmpgtq, FPU_CMPQ, FPU_GT)
SSE_HELPER_CMP(cmptrues, FPU_CMPS,  !FPU_FALSE)

#undef SSE_HELPER_CMP

#if SHIFT == 1
static const int comis_eflags[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

void helper_ucomiss(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float32 s0, s1;

    s0 = d->ZMM_S(0);
    s1 = s->ZMM_S(0);
    ret = float32_compare_quiet(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}

void helper_comiss(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float32 s0, s1;

    s0 = d->ZMM_S(0);
    s1 = s->ZMM_S(0);
    ret = float32_compare(s0, s1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}

void helper_ucomisd(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float64 d0, d1;

    d0 = d->ZMM_D(0);
    d1 = s->ZMM_D(0);
    ret = float64_compare_quiet(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}

void helper_comisd(CPUX86State *env, Reg *d, Reg *s)
{
    FloatRelation ret;
    float64 d0, d1;

    d0 = d->ZMM_D(0);
    d1 = s->ZMM_D(0);
    ret = float64_compare(d0, d1, &env->sse_status);
    CC_SRC = comis_eflags[ret + 1];
}
#endif

uint32_t glue(helper_movmskps, SUFFIX)(CPUX86State *env, Reg *s)
{
    uint32_t mask;
    int i;

    mask = 0;
    for (i = 0; i < 2 << SHIFT; i++) {
        mask |= (s->ZMM_L(i) >> (31 - i)) & (1 << i);
    }
    return mask;
}

uint32_t glue(helper_movmskpd, SUFFIX)(CPUX86State *env, Reg *s)
{
    uint32_t mask;
    int i;

    mask = 0;
    for (i = 0; i < 1 << SHIFT; i++) {
        mask |= (s->ZMM_Q(i) >> (63 - i)) & (1 << i);
    }
    return mask;
}

#endif

#define PACK_HELPER_B(name, F) \
void glue(helper_pack ## name, SUFFIX)(CPUX86State *env,      \
        Reg *d, Reg *v, Reg *s)                               \
{                                                             \
    uint8_t r[PACK_WIDTH * 2];                                \
    int j, k;                                                 \
    for (j = 0; j < 4 << SHIFT; j += PACK_WIDTH) {            \
        for (k = 0; k < PACK_WIDTH; k++) {                    \
            r[k] = F((int16_t)v->W(j + k));                   \
        }                                                     \
        for (k = 0; k < PACK_WIDTH; k++) {                    \
            r[PACK_WIDTH + k] = F((int16_t)s->W(j + k));      \
        }                                                     \
        for (k = 0; k < PACK_WIDTH * 2; k++) {                \
            d->B(2 * j + k) = r[k];                           \
        }                                                     \
    }                                                         \
}

PACK_HELPER_B(sswb, satsb)
PACK_HELPER_B(uswb, satub)

void glue(helper_packssdw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint16_t r[PACK_WIDTH];
    int j, k;

    for (j = 0; j < 2 << SHIFT; j += PACK_WIDTH / 2) {
        for (k = 0; k < PACK_WIDTH / 2; k++) {
            r[k] = satsw(v->L(j + k));
        }
        for (k = 0; k < PACK_WIDTH / 2; k++) {
            r[PACK_WIDTH / 2 + k] = satsw(s->L(j + k));
        }
        for (k = 0; k < PACK_WIDTH; k++) {
            d->W(2 * j + k) = r[k];
        }
    }
}

#define UNPCK_OP(base_name, base)                                       \
                                                                        \
    void glue(helper_punpck ## base_name ## bw, SUFFIX)(CPUX86State *env,\
                                                Reg *d, Reg *v, Reg *s) \
    {                                                                   \
        uint8_t r[PACK_WIDTH * 2];                                      \
        int j, i;                                                       \
                                                                        \
        for (j = 0; j < 8 << SHIFT; ) {                                 \
            int k = j + base * PACK_WIDTH;                              \
            for (i = 0; i < PACK_WIDTH; i++) {                          \
                r[2 * i] = v->B(k + i);                                 \
                r[2 * i + 1] = s->B(k + i);                             \
            }                                                           \
            for (i = 0; i < PACK_WIDTH * 2; i++, j++) {                 \
                d->B(j) = r[i];                                         \
            }                                                           \
        }                                                               \
    }                                                                   \
                                                                        \
    void glue(helper_punpck ## base_name ## wd, SUFFIX)(CPUX86State *env,\
                                                Reg *d, Reg *v, Reg *s) \
    {                                                                   \
        uint16_t r[PACK_WIDTH];                                         \
        int j, i;                                                       \
                                                                        \
        for (j = 0; j < 4 << SHIFT; ) {                                 \
            int k = j + base * PACK_WIDTH / 2;                          \
            for (i = 0; i < PACK_WIDTH / 2; i++) {                      \
                r[2 * i] = v->W(k + i);                                 \
                r[2 * i + 1] = s->W(k + i);                             \
            }                                                           \
            for (i = 0; i < PACK_WIDTH; i++, j++) {                     \
                d->W(j) = r[i];                                         \
            }                                                           \
        }                                                               \
    }                                                                   \
                                                                        \
    void glue(helper_punpck ## base_name ## dq, SUFFIX)(CPUX86State *env,\
                                                Reg *d, Reg *v, Reg *s) \
    {                                                                   \
        uint32_t r[PACK_WIDTH / 2];                                     \
        int j, i;                                                       \
                                                                        \
        for (j = 0; j < 2 << SHIFT; ) {                                 \
            int k = j + base * PACK_WIDTH / 4;                          \
            for (i = 0; i < PACK_WIDTH / 4; i++) {                      \
                r[2 * i] = v->L(k + i);                                 \
                r[2 * i + 1] = s->L(k + i);                             \
            }                                                           \
            for (i = 0; i < PACK_WIDTH / 2; i++, j++) {                 \
                d->L(j) = r[i];                                         \
            }                                                           \
        }                                                               \
    }                                                                   \
                                                                        \
    XMM_ONLY(                                                           \
             void glue(helper_punpck ## base_name ## qdq, SUFFIX)(      \
                        CPUX86State *env, Reg *d, Reg *v, Reg *s)       \
             {                                                          \
                 uint64_t r[2];                                         \
                 int i;                                                 \
                                                                        \
                 for (i = 0; i < 1 << SHIFT; i += 2) {                  \
                     r[0] = v->Q(base + i);                             \
                     r[1] = s->Q(base + i);                             \
                     d->Q(i) = r[0];                                    \
                     d->Q(i + 1) = r[1];                                \
                 }                                                      \
             }                                                          \
                                                                        )

UNPCK_OP(l, 0)
UNPCK_OP(h, 1)

#undef PACK_WIDTH
#undef PACK_HELPER_B
#undef UNPCK_OP


/* 3DNow! float ops */
#if SHIFT == 0
void helper_pi2fd(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = int32_to_float32(s->MMX_L(0), &env->mmx_status);
    d->MMX_S(1) = int32_to_float32(s->MMX_L(1), &env->mmx_status);
}

void helper_pi2fw(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = int32_to_float32((int16_t)s->MMX_W(0), &env->mmx_status);
    d->MMX_S(1) = int32_to_float32((int16_t)s->MMX_W(2), &env->mmx_status);
}

void helper_pf2id(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_to_int32_round_to_zero(s->MMX_S(0), &env->mmx_status);
    d->MMX_L(1) = float32_to_int32_round_to_zero(s->MMX_S(1), &env->mmx_status);
}

void helper_pf2iw(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = satsw(float32_to_int32_round_to_zero(s->MMX_S(0),
                                                       &env->mmx_status));
    d->MMX_L(1) = satsw(float32_to_int32_round_to_zero(s->MMX_S(1),
                                                       &env->mmx_status));
}

void helper_pfacc(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    float32 r;

    r = float32_add(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    d->MMX_S(1) = float32_add(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    d->MMX_S(0) = r;
}

void helper_pfadd(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_add(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_add(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfcmpeq(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_eq_quiet(d->MMX_S(0), s->MMX_S(0),
                                   &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_eq_quiet(d->MMX_S(1), s->MMX_S(1),
                                   &env->mmx_status) ? -1 : 0;
}

void helper_pfcmpge(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_le(s->MMX_S(0), d->MMX_S(0),
                             &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_le(s->MMX_S(1), d->MMX_S(1),
                             &env->mmx_status) ? -1 : 0;
}

void helper_pfcmpgt(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(0) = float32_lt(s->MMX_S(0), d->MMX_S(0),
                             &env->mmx_status) ? -1 : 0;
    d->MMX_L(1) = float32_lt(s->MMX_S(1), d->MMX_S(1),
                             &env->mmx_status) ? -1 : 0;
}

void helper_pfmax(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    if (float32_lt(d->MMX_S(0), s->MMX_S(0), &env->mmx_status)) {
        d->MMX_S(0) = s->MMX_S(0);
    }
    if (float32_lt(d->MMX_S(1), s->MMX_S(1), &env->mmx_status)) {
        d->MMX_S(1) = s->MMX_S(1);
    }
}

void helper_pfmin(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    if (float32_lt(s->MMX_S(0), d->MMX_S(0), &env->mmx_status)) {
        d->MMX_S(0) = s->MMX_S(0);
    }
    if (float32_lt(s->MMX_S(1), d->MMX_S(1), &env->mmx_status)) {
        d->MMX_S(1) = s->MMX_S(1);
    }
}

void helper_pfmul(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_mul(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_mul(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfnacc(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    float32 r;

    r = float32_sub(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    d->MMX_S(1) = float32_sub(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    d->MMX_S(0) = r;
}

void helper_pfpnacc(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    float32 r;

    r = float32_sub(d->MMX_S(0), d->MMX_S(1), &env->mmx_status);
    d->MMX_S(1) = float32_add(s->MMX_S(0), s->MMX_S(1), &env->mmx_status);
    d->MMX_S(0) = r;
}

void helper_pfrcp(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_div(float32_one, s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = d->MMX_S(0);
}

void helper_pfrsqrt(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_L(1) = s->MMX_L(0) & 0x7fffffff;
    d->MMX_S(1) = float32_div(float32_one,
                              float32_sqrt(d->MMX_S(1), &env->mmx_status),
                              &env->mmx_status);
    d->MMX_L(1) |= s->MMX_L(0) & 0x80000000;
    d->MMX_L(0) = d->MMX_L(1);
}

void helper_pfsub(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_sub(d->MMX_S(0), s->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_sub(d->MMX_S(1), s->MMX_S(1), &env->mmx_status);
}

void helper_pfsubr(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    d->MMX_S(0) = float32_sub(s->MMX_S(0), d->MMX_S(0), &env->mmx_status);
    d->MMX_S(1) = float32_sub(s->MMX_S(1), d->MMX_S(1), &env->mmx_status);
}

void helper_pswapd(CPUX86State *env, MMXReg *d, MMXReg *s)
{
    uint32_t r;

    r = s->MMX_L(0);
    d->MMX_L(0) = s->MMX_L(1);
    d->MMX_L(1) = r;
}
#endif

/* SSSE3 op helpers */
void glue(helper_pshufb, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
#if SHIFT == 0
    uint8_t r[8];

    for (i = 0; i < 8; i++) {
        r[i] = (s->B(i) & 0x80) ? 0 : (v->B(s->B(i) & 7));
    }
    for (i = 0; i < 8; i++) {
        d->B(i) = r[i];
    }
#else
    uint8_t r[8 << SHIFT];

    for (i = 0; i < 8 << SHIFT; i++) {
        int j = i & ~0xf;
        r[i] = (s->B(i) & 0x80) ? 0 : v->B(j | (s->B(i) & 0xf));
    }
    for (i = 0; i < 8 << SHIFT; i++) {
        d->B(i) = r[i];
    }
#endif
}

#define SSE_HELPER_HW(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                                                          \
    uint16_t r[4 << SHIFT];                                \
    int i, j, k;                                           \
    for (k = 0; k < 4 << SHIFT; k += LANE_WIDTH / 2) {     \
        for (i = j = 0; j < LANE_WIDTH / 2; i++, j += 2) { \
            r[i + k] = F(v->W(j + k), v->W(j + k + 1));    \
        }                                                  \
        for (j = 0; j < LANE_WIDTH / 2; i++, j += 2) {     \
            r[i + k] = F(s->W(j + k), s->W(j + k + 1));    \
        }                                                  \
    }                                                      \
    for (i = 0; i < 4 << SHIFT; i++) {                     \
        d->W(i) = r[i];                                    \
    }                                                      \
}

#define SSE_HELPER_HL(name, F)  \
void glue(helper_ ## name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s) \
{                                                          \
    uint32_t r[2 << SHIFT];                                \
    int i, j, k;                                           \
    for (k = 0; k < 2 << SHIFT; k += LANE_WIDTH / 4) {     \
        for (i = j = 0; j < LANE_WIDTH / 4; i++, j += 2) { \
            r[i + k] = F(v->L(j + k), v->L(j + k + 1));    \
        }                                                  \
        for (j = 0; j < LANE_WIDTH / 4; i++, j += 2) {     \
            r[i + k] = F(s->L(j + k), s->L(j + k + 1));    \
        }                                                  \
    }                                                      \
    for (i = 0; i < 2 << SHIFT; i++) {                     \
        d->L(i) = r[i];                                    \
    }                                                      \
}

SSE_HELPER_HW(phaddw, FADD)
SSE_HELPER_HW(phsubw, FSUB)
SSE_HELPER_HW(phaddsw, FADDSW)
SSE_HELPER_HW(phsubsw, FSUBSW)
SSE_HELPER_HL(phaddd, FADD)
SSE_HELPER_HL(phsubd, FSUB)

#undef SSE_HELPER_HW
#undef SSE_HELPER_HL

void glue(helper_pmaddubsw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;
    for (i = 0; i < 4 << SHIFT; i++) {
        d->W(i) = satsw((int8_t)s->B(i * 2) * (uint8_t)v->B(i * 2) +
                        (int8_t)s->B(i * 2 + 1) * (uint8_t)v->B(i * 2 + 1));
    }
}

#define FMULHRSW(d, s) (((int16_t) d * (int16_t)s + 0x4000) >> 15)
SSE_HELPER_W(helper_pmulhrsw, FMULHRSW)

#define FSIGNB(d, s) (s <= INT8_MAX  ? s ? d : 0 : -(int8_t)d)
#define FSIGNW(d, s) (s <= INT16_MAX ? s ? d : 0 : -(int16_t)d)
#define FSIGNL(d, s) (s <= INT32_MAX ? s ? d : 0 : -(int32_t)d)
SSE_HELPER_B(helper_psignb, FSIGNB)
SSE_HELPER_W(helper_psignw, FSIGNW)
SSE_HELPER_L(helper_psignd, FSIGNL)

void glue(helper_palignr, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                  uint32_t imm)
{
    int i;

    /* XXX could be checked during translation */
    if (imm >= (SHIFT ? 32 : 16)) {
        for (i = 0; i < (1 << SHIFT); i++) {
            d->Q(i) = 0;
        }
    } else {
        int shift = imm * 8;
#define SHR(v, i) (i < 64 && i > -64 ? i > 0 ? v >> (i) : (v << -(i)) : 0)
#if SHIFT == 0
        d->Q(0) = SHR(s->Q(0), shift - 0) |
            SHR(v->Q(0), shift -  64);
#else
        for (i = 0; i < (1 << SHIFT); i += 2) {
            uint64_t r0, r1;

            r0 = SHR(s->Q(i), shift - 0) |
                 SHR(s->Q(i + 1), shift -  64) |
                 SHR(v->Q(i), shift - 128) |
                 SHR(v->Q(i + 1), shift - 192);
            r1 = SHR(s->Q(i), shift + 64) |
                 SHR(s->Q(i + 1), shift -   0) |
                 SHR(v->Q(i), shift -  64) |
                 SHR(v->Q(i + 1), shift - 128);
            d->Q(i) = r0;
            d->Q(i + 1) = r1;
        }
#endif
#undef SHR
    }
}

#if SHIFT >= 1

#define SSE_HELPER_V(name, elem, num, F)                                \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,   \
                            Reg *m)                                     \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < num; i++) {                                     \
            d->elem(i) = F(v->elem(i), s->elem(i), m->elem(i));         \
        }                                                               \
    }

#define SSE_HELPER_I(name, elem, num, F)                                \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,   \
                            uint32_t imm)                               \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < num; i++) {                                     \
            int j = i & 7;                                              \
            d->elem(i) = F(v->elem(i), s->elem(i), (imm >> j) & 1);     \
        }                                                               \
    }

/* SSE4.1 op helpers */
#define FBLENDVB(v, s, m) ((m & 0x80) ? s : v)
#define FBLENDVPS(v, s, m) ((m & 0x80000000) ? s : v)
#define FBLENDVPD(v, s, m) ((m & 0x8000000000000000LL) ? s : v)
SSE_HELPER_V(helper_pblendvb, B, 8 << SHIFT, FBLENDVB)
SSE_HELPER_V(helper_blendvps, L, 2 << SHIFT, FBLENDVPS)
SSE_HELPER_V(helper_blendvpd, Q, 1 << SHIFT, FBLENDVPD)

void glue(helper_ptest, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint64_t zf = 0, cf = 0;
    int i;

    for (i = 0; i < 1 << SHIFT; i++) {
        zf |= (s->Q(i) &  d->Q(i));
        cf |= (s->Q(i) & ~d->Q(i));
    }
    CC_SRC = (zf ? 0 : CC_Z) | (cf ? 0 : CC_C);
}

#define FMOVSLDUP(i) s->L((i) & ~1)
#define FMOVSHDUP(i) s->L((i) | 1)
#define FMOVDLDUP(i) s->Q((i) & ~1)

#define SSE_HELPER_F(name, elem, num, F)                        \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)   \
    {                                                           \
        int n = num;                                            \
        for (int i = n; --i >= 0; ) {                           \
            d->elem(i) = F(i);                                  \
        }                                                       \
    }

#if SHIFT > 0
SSE_HELPER_F(helper_pmovsxbw, W, 4 << SHIFT, (int8_t) s->B)
SSE_HELPER_F(helper_pmovsxbd, L, 2 << SHIFT, (int8_t) s->B)
SSE_HELPER_F(helper_pmovsxbq, Q, 1 << SHIFT, (int8_t) s->B)
SSE_HELPER_F(helper_pmovsxwd, L, 2 << SHIFT, (int16_t) s->W)
SSE_HELPER_F(helper_pmovsxwq, Q, 1 << SHIFT, (int16_t) s->W)
SSE_HELPER_F(helper_pmovsxdq, Q, 1 << SHIFT, (int32_t) s->L)
SSE_HELPER_F(helper_pmovzxbw, W, 4 << SHIFT, s->B)
SSE_HELPER_F(helper_pmovzxbd, L, 2 << SHIFT, s->B)
SSE_HELPER_F(helper_pmovzxbq, Q, 1 << SHIFT, s->B)
SSE_HELPER_F(helper_pmovzxwd, L, 2 << SHIFT, s->W)
SSE_HELPER_F(helper_pmovzxwq, Q, 1 << SHIFT, s->W)
SSE_HELPER_F(helper_pmovzxdq, Q, 1 << SHIFT, s->L)
SSE_HELPER_F(helper_pmovsldup, L, 2 << SHIFT, FMOVSLDUP)
SSE_HELPER_F(helper_pmovshdup, L, 2 << SHIFT, FMOVSHDUP)
SSE_HELPER_F(helper_pmovdldup, Q, 1 << SHIFT, FMOVDLDUP)
#endif

void glue(helper_pmuldq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;

    for (i = 0; i < 1 << SHIFT; i++) {
        d->Q(i) = (int64_t)(int32_t) v->L(2 * i) * (int32_t) s->L(2 * i);
    }
}

void glue(helper_packusdw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint16_t r[8];
    int i, j, k;

    for (i = 0, j = 0; i <= 2 << SHIFT; i += 8, j += 4) {
        r[0] = satuw(v->L(j));
        r[1] = satuw(v->L(j + 1));
        r[2] = satuw(v->L(j + 2));
        r[3] = satuw(v->L(j + 3));
        r[4] = satuw(s->L(j));
        r[5] = satuw(s->L(j + 1));
        r[6] = satuw(s->L(j + 2));
        r[7] = satuw(s->L(j + 3));
        for (k = 0; k < 8; k++) {
            d->W(i + k) = r[k];
        }
    }
}

#if SHIFT == 1
void glue(helper_phminposuw, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    int idx = 0;

    if (s->W(1) < s->W(idx)) {
        idx = 1;
    }
    if (s->W(2) < s->W(idx)) {
        idx = 2;
    }
    if (s->W(3) < s->W(idx)) {
        idx = 3;
    }
    if (s->W(4) < s->W(idx)) {
        idx = 4;
    }
    if (s->W(5) < s->W(idx)) {
        idx = 5;
    }
    if (s->W(6) < s->W(idx)) {
        idx = 6;
    }
    if (s->W(7) < s->W(idx)) {
        idx = 7;
    }

    d->W(0) = s->W(idx);
    d->W(1) = idx;
    d->L(1) = 0;
    d->Q(1) = 0;
}
#endif

void glue(helper_roundps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;
    int i;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        set_x86_rounding_mode(mode & 3, &env->sse_status);
    }

    for (i = 0; i < 2 << SHIFT; i++) {
        d->ZMM_S(i) = float32_round_to_int(s->ZMM_S(i), &env->sse_status);
    }

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}

void glue(helper_roundpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;
    int i;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        set_x86_rounding_mode(mode & 3, &env->sse_status);
    }

    for (i = 0; i < 1 << SHIFT; i++) {
        d->ZMM_D(i) = float64_round_to_int(s->ZMM_D(i), &env->sse_status);
    }

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}

#if SHIFT == 1
void glue(helper_roundss, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;
    int i;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        set_x86_rounding_mode(mode & 3, &env->sse_status);
    }

    d->ZMM_S(0) = float32_round_to_int(s->ZMM_S(0), &env->sse_status);
    for (i = 1; i < 2 << SHIFT; i++) {
        d->ZMM_L(i) = v->ZMM_L(i);
    }

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}

void glue(helper_roundsd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                  uint32_t mode)
{
    uint8_t old_flags = get_float_exception_flags(&env->sse_status);
    signed char prev_rounding_mode;
    int i;

    prev_rounding_mode = env->sse_status.float_rounding_mode;
    if (!(mode & (1 << 2))) {
        set_x86_rounding_mode(mode & 3, &env->sse_status);
    }

    d->ZMM_D(0) = float64_round_to_int(s->ZMM_D(0), &env->sse_status);
    for (i = 1; i < 1 << SHIFT; i++) {
        d->ZMM_Q(i) = v->ZMM_Q(i);
    }

    if (mode & (1 << 3) && !(old_flags & float_flag_inexact)) {
        set_float_exception_flags(get_float_exception_flags(&env->sse_status) &
                                  ~float_flag_inexact,
                                  &env->sse_status);
    }
    env->sse_status.float_rounding_mode = prev_rounding_mode;
}
#endif

#define FBLENDP(v, s, m) (m ? s : v)
SSE_HELPER_I(helper_blendps, L, 2 << SHIFT, FBLENDP)
SSE_HELPER_I(helper_blendpd, Q, 1 << SHIFT, FBLENDP)
SSE_HELPER_I(helper_pblendw, W, 4 << SHIFT, FBLENDP)

void glue(helper_dpps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                               uint32_t mask)
{
    float32 prod1, prod2, temp2, temp3, temp4;
    int i;

    for (i = 0; i < 2 << SHIFT; i += 4) {
        /*
         * We must evaluate (A+B)+(C+D), not ((A+B)+C)+D
         * to correctly round the intermediate results
         */
        if (mask & (1 << 4)) {
            prod1 = float32_mul(v->ZMM_S(i), s->ZMM_S(i), &env->sse_status);
        } else {
            prod1 = float32_zero;
        }
        if (mask & (1 << 5)) {
            prod2 = float32_mul(v->ZMM_S(i+1), s->ZMM_S(i+1), &env->sse_status);
        } else {
            prod2 = float32_zero;
        }
        temp2 = float32_add(prod1, prod2, &env->sse_status);
        if (mask & (1 << 6)) {
            prod1 = float32_mul(v->ZMM_S(i+2), s->ZMM_S(i+2), &env->sse_status);
        } else {
            prod1 = float32_zero;
        }
        if (mask & (1 << 7)) {
            prod2 = float32_mul(v->ZMM_S(i+3), s->ZMM_S(i+3), &env->sse_status);
        } else {
            prod2 = float32_zero;
        }
        temp3 = float32_add(prod1, prod2, &env->sse_status);
        temp4 = float32_add(temp2, temp3, &env->sse_status);

        d->ZMM_S(i) = (mask & (1 << 0)) ? temp4 : float32_zero;
        d->ZMM_S(i+1) = (mask & (1 << 1)) ? temp4 : float32_zero;
        d->ZMM_S(i+2) = (mask & (1 << 2)) ? temp4 : float32_zero;
        d->ZMM_S(i+3) = (mask & (1 << 3)) ? temp4 : float32_zero;
    }
}

#if SHIFT == 1
/* Oddly, there is no ymm version of dppd */
void glue(helper_dppd, SUFFIX)(CPUX86State *env,
                               Reg *d, Reg *v, Reg *s, uint32_t mask)
{
    float64 prod1, prod2, temp2;

    if (mask & (1 << 4)) {
        prod1 = float64_mul(v->ZMM_D(0), s->ZMM_D(0), &env->sse_status);
    } else {
        prod1 = float64_zero;
    }
    if (mask & (1 << 5)) {
        prod2 = float64_mul(v->ZMM_D(1), s->ZMM_D(1), &env->sse_status);
    } else {
        prod2 = float64_zero;
    }
    temp2 = float64_add(prod1, prod2, &env->sse_status);
    d->ZMM_D(0) = (mask & (1 << 0)) ? temp2 : float64_zero;
    d->ZMM_D(1) = (mask & (1 << 1)) ? temp2 : float64_zero;
}
#endif

void glue(helper_mpsadbw, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                  uint32_t offset)
{
    int i, j;
    uint16_t r[8];

    for (j = 0; j < 4 << SHIFT; ) {
        int s0 = (j * 2) + ((offset & 3) << 2);
        int d0 = (j * 2) + ((offset & 4) << 0);
        for (i = 0; i < LANE_WIDTH / 2; i++, d0++) {
            r[i] = 0;
            r[i] += abs1(v->B(d0 + 0) - s->B(s0 + 0));
            r[i] += abs1(v->B(d0 + 1) - s->B(s0 + 1));
            r[i] += abs1(v->B(d0 + 2) - s->B(s0 + 2));
            r[i] += abs1(v->B(d0 + 3) - s->B(s0 + 3));
        }
        for (i = 0; i < LANE_WIDTH / 2; i++, j++) {
            d->W(j) = r[i];
        }
        offset >>= 3;
    }
}

/* SSE4.2 op helpers */
#if SHIFT == 1
static inline int pcmp_elen(CPUX86State *env, int reg, uint32_t ctrl)
{
    target_long val, limit;

    /* Presence of REX.W is indicated by a bit higher than 7 set */
    if (ctrl >> 8) {
        val = (target_long)env->regs[reg];
    } else {
        val = (int32_t)env->regs[reg];
    }
    if (ctrl & 1) {
        limit = 8;
    } else {
        limit = 16;
    }
    if ((val > limit) || (val < -limit)) {
        return limit;
    }
    return abs1(val);
}

static inline int pcmp_ilen(Reg *r, uint8_t ctrl)
{
    int val = 0;

    if (ctrl & 1) {
        while (val < 8 && r->W(val)) {
            val++;
        }
    } else {
        while (val < 16 && r->B(val)) {
            val++;
        }
    }

    return val;
}

static inline int pcmp_val(Reg *r, uint8_t ctrl, int i)
{
    switch ((ctrl >> 0) & 3) {
    case 0:
        return r->B(i);
    case 1:
        return r->W(i);
    case 2:
        return (int8_t)r->B(i);
    case 3:
    default:
        return (int16_t)r->W(i);
    }
}

static inline unsigned pcmpxstrx(CPUX86State *env, Reg *d, Reg *s,
                                 uint8_t ctrl, int valids, int validd)
{
    unsigned int res = 0;
    int v;
    int j, i;
    int upper = (ctrl & 1) ? 7 : 15;

    valids--;
    validd--;

    CC_SRC = (valids < upper ? CC_Z : 0) | (validd < upper ? CC_S : 0);

    switch ((ctrl >> 2) & 3) {
    case 0:
        for (j = valids; j >= 0; j--) {
            res <<= 1;
            v = pcmp_val(s, ctrl, j);
            for (i = validd; i >= 0; i--) {
                res |= (v == pcmp_val(d, ctrl, i));
            }
        }
        break;
    case 1:
        for (j = valids; j >= 0; j--) {
            res <<= 1;
            v = pcmp_val(s, ctrl, j);
            for (i = ((validd - 1) | 1); i >= 0; i -= 2) {
                res |= (pcmp_val(d, ctrl, i - 0) >= v &&
                        pcmp_val(d, ctrl, i - 1) <= v);
            }
        }
        break;
    case 2:
        res = (1 << (upper - MAX(valids, validd))) - 1;
        res <<= MAX(valids, validd) - MIN(valids, validd);
        for (i = MIN(valids, validd); i >= 0; i--) {
            res <<= 1;
            v = pcmp_val(s, ctrl, i);
            res |= (v == pcmp_val(d, ctrl, i));
        }
        break;
    case 3:
        if (validd == -1) {
            res = (2 << upper) - 1;
            break;
        }
        for (j = valids == upper ? valids : valids - validd; j >= 0; j--) {
            res <<= 1;
            v = 1;
            for (i = MIN(valids - j, validd); i >= 0; i--) {
                v &= (pcmp_val(s, ctrl, i + j) == pcmp_val(d, ctrl, i));
            }
            res |= v;
        }
        break;
    }

    switch ((ctrl >> 4) & 3) {
    case 1:
        res ^= (2 << upper) - 1;
        break;
    case 3:
        res ^= (1 << (valids + 1)) - 1;
        break;
    }

    if (res) {
        CC_SRC |= CC_C;
    }
    if (res & 1) {
        CC_SRC |= CC_O;
    }

    return res;
}

void glue(helper_pcmpestri, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_elen(env, R_EDX, ctrl),
                                 pcmp_elen(env, R_EAX, ctrl));

    if (res) {
        env->regs[R_ECX] = (ctrl & (1 << 6)) ? 31 - clz32(res) : ctz32(res);
    } else {
        env->regs[R_ECX] = 16 >> (ctrl & (1 << 0));
    }
}

void glue(helper_pcmpestrm, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    int i;
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_elen(env, R_EDX, ctrl),
                                 pcmp_elen(env, R_EAX, ctrl));

    if ((ctrl >> 6) & 1) {
        if (ctrl & 1) {
            for (i = 0; i < 8; i++, res >>= 1) {
                env->xmm_regs[0].W(i) = (res & 1) ? ~0 : 0;
            }
        } else {
            for (i = 0; i < 16; i++, res >>= 1) {
                env->xmm_regs[0].B(i) = (res & 1) ? ~0 : 0;
            }
        }
    } else {
        env->xmm_regs[0].Q(1) = 0;
        env->xmm_regs[0].Q(0) = res;
    }
}

void glue(helper_pcmpistri, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_ilen(s, ctrl),
                                 pcmp_ilen(d, ctrl));

    if (res) {
        env->regs[R_ECX] = (ctrl & (1 << 6)) ? 31 - clz32(res) : ctz32(res);
    } else {
        env->regs[R_ECX] = 16 >> (ctrl & (1 << 0));
    }
}

void glue(helper_pcmpistrm, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                    uint32_t ctrl)
{
    int i;
    unsigned int res = pcmpxstrx(env, d, s, ctrl,
                                 pcmp_ilen(s, ctrl),
                                 pcmp_ilen(d, ctrl));

    if ((ctrl >> 6) & 1) {
        if (ctrl & 1) {
            for (i = 0; i < 8; i++, res >>= 1) {
                env->xmm_regs[0].W(i) = (res & 1) ? ~0 : 0;
            }
        } else {
            for (i = 0; i < 16; i++, res >>= 1) {
                env->xmm_regs[0].B(i) = (res & 1) ? ~0 : 0;
            }
        }
    } else {
        env->xmm_regs[0].Q(1) = 0;
        env->xmm_regs[0].Q(0) = res;
    }
}

#define CRCPOLY        0x1edc6f41
#define CRCPOLY_BITREV 0x82f63b78
target_ulong helper_crc32(uint32_t crc1, target_ulong msg, uint32_t len)
{
    target_ulong crc = (msg & ((target_ulong) -1 >>
                               (TARGET_LONG_BITS - len))) ^ crc1;

    while (len--) {
        crc = (crc >> 1) ^ ((crc & 1) ? CRCPOLY_BITREV : 0);
    }

    return crc;
}

#endif

void glue(helper_pclmulqdq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s,
                                    uint32_t ctrl)
{
    int a_idx = (ctrl & 1) != 0;
    int b_idx = (ctrl & 16) != 0;

    for (int i = 0; i < SHIFT; i++) {
        uint64_t a = v->Q(2 * i + a_idx);
        uint64_t b = s->Q(2 * i + b_idx);
        Int128 *r = (Int128 *)&d->ZMM_X(i);

        *r = clmul_64(a, b);
    }
}

void glue(helper_aesdec, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    for (int i = 0; i < SHIFT; i++) {
        AESState *ad = (AESState *)&d->ZMM_X(i);
        AESState *st = (AESState *)&v->ZMM_X(i);
        AESState *rk = (AESState *)&s->ZMM_X(i);

        aesdec_ISB_ISR_IMC_AK(ad, st, rk, false);
    }
}

void glue(helper_aesdeclast, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    for (int i = 0; i < SHIFT; i++) {
        AESState *ad = (AESState *)&d->ZMM_X(i);
        AESState *st = (AESState *)&v->ZMM_X(i);
        AESState *rk = (AESState *)&s->ZMM_X(i);

        aesdec_ISB_ISR_AK(ad, st, rk, false);
    }
}

void glue(helper_aesenc, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    for (int i = 0; i < SHIFT; i++) {
        AESState *ad = (AESState *)&d->ZMM_X(i);
        AESState *st = (AESState *)&v->ZMM_X(i);
        AESState *rk = (AESState *)&s->ZMM_X(i);

        aesenc_SB_SR_MC_AK(ad, st, rk, false);
    }
}

void glue(helper_aesenclast, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    for (int i = 0; i < SHIFT; i++) {
        AESState *ad = (AESState *)&d->ZMM_X(i);
        AESState *st = (AESState *)&v->ZMM_X(i);
        AESState *rk = (AESState *)&s->ZMM_X(i);

        aesenc_SB_SR_AK(ad, st, rk, false);
    }
}

#if SHIFT == 1
void glue(helper_aesimc, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    AESState *ad = (AESState *)&d->ZMM_X(0);
    AESState *st = (AESState *)&s->ZMM_X(0);

    aesdec_IMC(ad, st, false);
}

void glue(helper_aeskeygenassist, SUFFIX)(CPUX86State *env, Reg *d, Reg *s,
                                          uint32_t ctrl)
{
    int i;
    Reg tmp = *s;

    for (i = 0 ; i < 4 ; i++) {
        d->B(i) = AES_sbox[tmp.B(i + 4)];
        d->B(i + 8) = AES_sbox[tmp.B(i + 12)];
    }
    d->L(1) = (d->L(0) << 24 | d->L(0) >> 8) ^ ctrl;
    d->L(3) = (d->L(2) << 24 | d->L(2) >> 8) ^ ctrl;
}
#endif
#endif

#if SHIFT >= 1
void glue(helper_vpermilpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint64_t r0, r1;
    int i;

    for (i = 0; i < 1 << SHIFT; i += 2) {
        r0 = v->Q(i + ((s->Q(i) >> 1) & 1));
        r1 = v->Q(i + ((s->Q(i+1) >> 1) & 1));
        d->Q(i) = r0;
        d->Q(i+1) = r1;
    }
}

void glue(helper_vpermilps, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    uint32_t r0, r1, r2, r3;
    int i;

    for (i = 0; i < 2 << SHIFT; i += 4) {
        r0 = v->L(i + (s->L(i) & 3));
        r1 = v->L(i + (s->L(i+1) & 3));
        r2 = v->L(i + (s->L(i+2) & 3));
        r3 = v->L(i + (s->L(i+3) & 3));
        d->L(i) = r0;
        d->L(i+1) = r1;
        d->L(i+2) = r2;
        d->L(i+3) = r3;
    }
}

void glue(helper_vpermilpd_imm, SUFFIX)(Reg *d, Reg *s, uint32_t order)
{
    uint64_t r0, r1;
    int i;

    for (i = 0; i < 1 << SHIFT; i += 2) {
        r0 = s->Q(i + ((order >> 0) & 1));
        r1 = s->Q(i + ((order >> 1) & 1));
        d->Q(i) = r0;
        d->Q(i+1) = r1;

        order >>= 2;
    }
}

void glue(helper_vpermilps_imm, SUFFIX)(Reg *d, Reg *s, uint32_t order)
{
    uint32_t r0, r1, r2, r3;
    int i;

    for (i = 0; i < 2 << SHIFT; i += 4) {
        r0 = s->L(i + ((order >> 0) & 3));
        r1 = s->L(i + ((order >> 2) & 3));
        r2 = s->L(i + ((order >> 4) & 3));
        r3 = s->L(i + ((order >> 6) & 3));
        d->L(i) = r0;
        d->L(i+1) = r1;
        d->L(i+2) = r2;
        d->L(i+3) = r3;
    }
}

#if SHIFT == 1
#define FPSRLVD(x, c) (c < 32 ? ((x) >> c) : 0)
#define FPSRLVQ(x, c) (c < 64 ? ((x) >> c) : 0)
#define FPSRAVD(x, c) ((int32_t)(x) >> (c < 32 ? c : 31))
#define FPSRAVQ(x, c) ((int64_t)(x) >> (c < 64 ? c : 63))
#define FPSLLVD(x, c) (c < 32 ? ((x) << c) : 0)
#define FPSLLVQ(x, c) (c < 64 ? ((x) << c) : 0)
#endif

SSE_HELPER_L(helper_vpsrlvd, FPSRLVD)
SSE_HELPER_L(helper_vpsravd, FPSRAVD)
SSE_HELPER_L(helper_vpsllvd, FPSLLVD)

SSE_HELPER_Q(helper_vpsrlvq, FPSRLVQ)
SSE_HELPER_Q(helper_vpsravq, FPSRAVQ)
SSE_HELPER_Q(helper_vpsllvq, FPSLLVQ)

void glue(helper_vtestps, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint32_t zf = 0, cf = 0;
    int i;

    for (i = 0; i < 2 << SHIFT; i++) {
        zf |= (s->L(i) &  d->L(i));
        cf |= (s->L(i) & ~d->L(i));
    }
    CC_SRC = ((zf >> 31) ? 0 : CC_Z) | ((cf >> 31) ? 0 : CC_C);
}

void glue(helper_vtestpd, SUFFIX)(CPUX86State *env, Reg *d, Reg *s)
{
    uint64_t zf = 0, cf = 0;
    int i;

    for (i = 0; i < 1 << SHIFT; i++) {
        zf |= (s->Q(i) &  d->Q(i));
        cf |= (s->Q(i) & ~d->Q(i));
    }
    CC_SRC = ((zf >> 63) ? 0 : CC_Z) | ((cf >> 63) ? 0 : CC_C);
}

void glue(helper_vpmaskmovd_st, SUFFIX)(CPUX86State *env,
                                        Reg *v, Reg *s, target_ulong a0)
{
    int i;

    for (i = 0; i < (2 << SHIFT); i++) {
        if (v->L(i) >> 31) {
            cpu_stl_data_ra(env, a0 + i * 4, s->L(i), GETPC());
        }
    }
}

void glue(helper_vpmaskmovq_st, SUFFIX)(CPUX86State *env,
                                        Reg *v, Reg *s, target_ulong a0)
{
    int i;

    for (i = 0; i < (1 << SHIFT); i++) {
        if (v->Q(i) >> 63) {
            cpu_stq_data_ra(env, a0 + i * 8, s->Q(i), GETPC());
        }
    }
}

void glue(helper_vpmaskmovd, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;

    for (i = 0; i < (2 << SHIFT); i++) {
        d->L(i) = (v->L(i) >> 31) ? s->L(i) : 0;
    }
}

void glue(helper_vpmaskmovq, SUFFIX)(CPUX86State *env, Reg *d, Reg *v, Reg *s)
{
    int i;

    for (i = 0; i < (1 << SHIFT); i++) {
        d->Q(i) = (v->Q(i) >> 63) ? s->Q(i) : 0;
    }
}

void glue(helper_vpgatherdd, SUFFIX)(CPUX86State *env,
        Reg *d, Reg *v, Reg *s, target_ulong a0, unsigned scale)
{
    int i;
    for (i = 0; i < (2 << SHIFT); i++) {
        if (v->L(i) >> 31) {
            target_ulong addr = a0
                + ((target_ulong)(int32_t)s->L(i) << scale);
            d->L(i) = cpu_ldl_data_ra(env, addr, GETPC());
        }
        v->L(i) = 0;
    }
}

void glue(helper_vpgatherdq, SUFFIX)(CPUX86State *env,
        Reg *d, Reg *v, Reg *s, target_ulong a0, unsigned scale)
{
    int i;
    for (i = 0; i < (1 << SHIFT); i++) {
        if (v->Q(i) >> 63) {
            target_ulong addr = a0
                + ((target_ulong)(int32_t)s->L(i) << scale);
            d->Q(i) = cpu_ldq_data_ra(env, addr, GETPC());
        }
        v->Q(i) = 0;
    }
}

void glue(helper_vpgatherqd, SUFFIX)(CPUX86State *env,
        Reg *d, Reg *v, Reg *s, target_ulong a0, unsigned scale)
{
    int i;
    for (i = 0; i < (1 << SHIFT); i++) {
        if (v->L(i) >> 31) {
            target_ulong addr = a0
                + ((target_ulong)(int64_t)s->Q(i) << scale);
            d->L(i) = cpu_ldl_data_ra(env, addr, GETPC());
        }
        v->L(i) = 0;
    }
    for (i /= 2; i < 1 << SHIFT; i++) {
        d->Q(i) = 0;
        v->Q(i) = 0;
    }
}

void glue(helper_vpgatherqq, SUFFIX)(CPUX86State *env,
        Reg *d, Reg *v, Reg *s, target_ulong a0, unsigned scale)
{
    int i;
    for (i = 0; i < (1 << SHIFT); i++) {
        if (v->Q(i) >> 63) {
            target_ulong addr = a0
                + ((target_ulong)(int64_t)s->Q(i) << scale);
            d->Q(i) = cpu_ldq_data_ra(env, addr, GETPC());
        }
        v->Q(i) = 0;
    }
}
#endif

#if SHIFT >= 2
void helper_vpermdq_ymm(Reg *d, Reg *v, Reg *s, uint32_t order)
{
    uint64_t r0, r1, r2, r3;

    switch (order & 3) {
    case 0:
        r0 = v->Q(0);
        r1 = v->Q(1);
        break;
    case 1:
        r0 = v->Q(2);
        r1 = v->Q(3);
        break;
    case 2:
        r0 = s->Q(0);
        r1 = s->Q(1);
        break;
    case 3:
        r0 = s->Q(2);
        r1 = s->Q(3);
        break;
    default: /* default case added to help the compiler to avoid warnings */
        g_assert_not_reached();
    }
    switch ((order >> 4) & 3) {
    case 0:
        r2 = v->Q(0);
        r3 = v->Q(1);
        break;
    case 1:
        r2 = v->Q(2);
        r3 = v->Q(3);
        break;
    case 2:
        r2 = s->Q(0);
        r3 = s->Q(1);
        break;
    case 3:
        r2 = s->Q(2);
        r3 = s->Q(3);
        break;
    default: /* default case added to help the compiler to avoid warnings */
        g_assert_not_reached();
    }
    d->Q(0) = r0;
    d->Q(1) = r1;
    d->Q(2) = r2;
    d->Q(3) = r3;
    if (order & 0x8) {
        d->Q(0) = 0;
        d->Q(1) = 0;
    }
    if (order & 0x80) {
        d->Q(2) = 0;
        d->Q(3) = 0;
    }
}

void helper_vpermq_ymm(Reg *d, Reg *s, uint32_t order)
{
    uint64_t r0, r1, r2, r3;
    r0 = s->Q(order & 3);
    r1 = s->Q((order >> 2) & 3);
    r2 = s->Q((order >> 4) & 3);
    r3 = s->Q((order >> 6) & 3);
    d->Q(0) = r0;
    d->Q(1) = r1;
    d->Q(2) = r2;
    d->Q(3) = r3;
}

void helper_vpermd_ymm(Reg *d, Reg *v, Reg *s)
{
    uint32_t r[8];
    int i;

    for (i = 0; i < 8; i++) {
        r[i] = s->L(v->L(i) & 7);
    }
    for (i = 0; i < 8; i++) {
        d->L(i) = r[i];
    }
}
#endif

/* FMA3 op helpers */
#if SHIFT == 1
#define SSE_HELPER_FMAS(name, elem, F)                                         \
    void name(CPUX86State *env, Reg *d, Reg *a, Reg *b, Reg *c, int flags)     \
    {                                                                          \
        d->elem(0) = F(a->elem(0), b->elem(0), c->elem(0), flags, &env->sse_status); \
    }
#define SSE_HELPER_FMAP(name, elem, num, F)                                    \
    void glue(name, SUFFIX)(CPUX86State *env, Reg *d, Reg *a, Reg *b, Reg *c,  \
                            int flags, int flip)                               \
    {                                                                          \
        int i;                                                                 \
        for (i = 0; i < num; i++) {                                            \
            d->elem(i) = F(a->elem(i), b->elem(i), c->elem(i), flags, &env->sse_status); \
            flags ^= flip;                                                     \
        }                                                                      \
    }

SSE_HELPER_FMAS(helper_fma4ss,  ZMM_S, float32_muladd)
SSE_HELPER_FMAS(helper_fma4sd,  ZMM_D, float64_muladd)
#endif

#if SHIFT >= 1
SSE_HELPER_FMAP(helper_fma4ps,  ZMM_S, 2 << SHIFT, float32_muladd)
SSE_HELPER_FMAP(helper_fma4pd,  ZMM_D, 1 << SHIFT, float64_muladd)
#endif

#if SHIFT == 1
#define SSE_HELPER_SHA1RNDS4(name, F, K) \
    void name(Reg *d, Reg *a, Reg *b)                                       \
    {                                                                       \
        uint32_t A, B, C, D, E, t, i;                                       \
                                                                            \
        A = a->L(3);                                                        \
        B = a->L(2);                                                        \
        C = a->L(1);                                                        \
        D = a->L(0);                                                        \
        E = 0;                                                              \
                                                                            \
        for (i = 0; i <= 3; i++) {                                          \
            t = F(B, C, D) + rol32(A, 5) + b->L(3 - i) + E + K;             \
            E = D;                                                          \
            D = C;                                                          \
            C = rol32(B, 30);                                               \
            B = A;                                                          \
            A = t;                                                          \
        }                                                                   \
                                                                            \
        d->L(3) = A;                                                        \
        d->L(2) = B;                                                        \
        d->L(1) = C;                                                        \
        d->L(0) = D;                                                        \
    }

#define SHA1_F0(b, c, d) (((b) & (c)) ^ (~(b) & (d)))
#define SHA1_F1(b, c, d) ((b) ^ (c) ^ (d))
#define SHA1_F2(b, c, d) (((b) & (c)) ^ ((b) & (d)) ^ ((c) & (d)))

SSE_HELPER_SHA1RNDS4(helper_sha1rnds4_f0, SHA1_F0, 0x5A827999)
SSE_HELPER_SHA1RNDS4(helper_sha1rnds4_f1, SHA1_F1, 0x6ED9EBA1)
SSE_HELPER_SHA1RNDS4(helper_sha1rnds4_f2, SHA1_F2, 0x8F1BBCDC)
SSE_HELPER_SHA1RNDS4(helper_sha1rnds4_f3, SHA1_F1, 0xCA62C1D6)

void helper_sha1nexte(Reg *d, Reg *a, Reg *b)
{
    d->L(3) = b->L(3) + rol32(a->L(3), 30);
    d->L(2) = b->L(2);
    d->L(1) = b->L(1);
    d->L(0) = b->L(0);
}

void helper_sha1msg1(Reg *d, Reg *a, Reg *b)
{
    /* These could be overwritten by the first two assignments, save them.  */
    uint32_t b3 = b->L(3);
    uint32_t b2 = b->L(2);

    d->L(3) = a->L(3) ^ a->L(1);
    d->L(2) = a->L(2) ^ a->L(0);
    d->L(1) = a->L(1) ^ b3;
    d->L(0) = a->L(0) ^ b2;
}

void helper_sha1msg2(Reg *d, Reg *a, Reg *b)
{
    d->L(3) = rol32(a->L(3) ^ b->L(2), 1);
    d->L(2) = rol32(a->L(2) ^ b->L(1), 1);
    d->L(1) = rol32(a->L(1) ^ b->L(0), 1);
    d->L(0) = rol32(a->L(0) ^ d->L(3), 1);
}

#define SHA256_CH(e, f, g)  (((e) & (f)) ^ (~(e) & (g)))
#define SHA256_MAJ(a, b, c) (((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))

#define SHA256_RNDS0(w) (ror32((w), 2) ^ ror32((w), 13) ^ ror32((w), 22))
#define SHA256_RNDS1(w) (ror32((w), 6) ^ ror32((w), 11) ^ ror32((w), 25))
#define SHA256_MSGS0(w) (ror32((w), 7) ^ ror32((w), 18) ^ ((w) >> 3))
#define SHA256_MSGS1(w) (ror32((w), 17) ^ ror32((w), 19) ^ ((w) >> 10))

void helper_sha256rnds2(Reg *d, Reg *a, Reg *b, uint32_t wk0, uint32_t wk1)
{
    uint32_t t, AA, EE;

    uint32_t A = b->L(3);
    uint32_t B = b->L(2);
    uint32_t C = a->L(3);
    uint32_t D = a->L(2);
    uint32_t E = b->L(1);
    uint32_t F = b->L(0);
    uint32_t G = a->L(1);
    uint32_t H = a->L(0);

    /* Even round */
    t = SHA256_CH(E, F, G) + SHA256_RNDS1(E) + wk0 + H;
    AA = t + SHA256_MAJ(A, B, C) + SHA256_RNDS0(A);
    EE = t + D;

    /* These will be B and F at the end of the odd round */
    d->L(2) = AA;
    d->L(0) = EE;

    D = C, C = B, B = A, A = AA;
    H = G, G = F, F = E, E = EE;

    /* Odd round */
    t = SHA256_CH(E, F, G) + SHA256_RNDS1(E) + wk1 + H;
    AA = t + SHA256_MAJ(A, B, C) + SHA256_RNDS0(A);
    EE = t + D;

    d->L(3) = AA;
    d->L(1) = EE;
}

void helper_sha256msg1(Reg *d, Reg *a, Reg *b)
{
    /* b->L(0) could be overwritten by the first assignment, save it.  */
    uint32_t b0 = b->L(0);

    d->L(0) = a->L(0) + SHA256_MSGS0(a->L(1));
    d->L(1) = a->L(1) + SHA256_MSGS0(a->L(2));
    d->L(2) = a->L(2) + SHA256_MSGS0(a->L(3));
    d->L(3) = a->L(3) + SHA256_MSGS0(b0);
}

void helper_sha256msg2(Reg *d, Reg *a, Reg *b)
{
    /* Earlier assignments cannot overwrite any of the two operands.  */
    d->L(0) = a->L(0) + SHA256_MSGS1(b->L(2));
    d->L(1) = a->L(1) + SHA256_MSGS1(b->L(3));
    /* Yes, this reuses the previously computed values.  */
    d->L(2) = a->L(2) + SHA256_MSGS1(d->L(0));
    d->L(3) = a->L(3) + SHA256_MSGS1(d->L(1));
}
#endif

#undef SSE_HELPER_S

#undef LANE_WIDTH
#undef SHIFT
#undef XMM_ONLY
#undef Reg
#undef B
#undef W
#undef L
#undef Q
#undef SUFFIX
