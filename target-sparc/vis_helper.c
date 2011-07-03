/*
 * VIS op helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"

#define DT0 (env->dt0)
#define DT1 (env->dt1)
#define QT0 (env->qt0)
#define QT1 (env->qt1)

/* This function uses non-native bit order */
#define GET_FIELD(X, FROM, TO)                                  \
    ((X) >> (63 - (TO)) & ((1ULL << ((TO) - (FROM) + 1)) - 1))

/* This function uses the order in the manuals, i.e. bit 0 is 2^0 */
#define GET_FIELD_SP(X, FROM, TO)               \
    GET_FIELD(X, 63 - (TO), 63 - (FROM))

target_ulong helper_array8(CPUState *env, target_ulong pixel_addr,
                           target_ulong cubesize)
{
    return (GET_FIELD_SP(pixel_addr, 60, 63) << (17 + 2 * cubesize)) |
        (GET_FIELD_SP(pixel_addr, 39, 39 + cubesize - 1) << (17 + cubesize)) |
        (GET_FIELD_SP(pixel_addr, 17 + cubesize - 1, 17) << 17) |
        (GET_FIELD_SP(pixel_addr, 56, 59) << 13) |
        (GET_FIELD_SP(pixel_addr, 35, 38) << 9) |
        (GET_FIELD_SP(pixel_addr, 13, 16) << 5) |
        (((pixel_addr >> 55) & 1) << 4) |
        (GET_FIELD_SP(pixel_addr, 33, 34) << 2) |
        GET_FIELD_SP(pixel_addr, 11, 12);
}

target_ulong helper_alignaddr(CPUState *env, target_ulong addr,
                              target_ulong offset)
{
    uint64_t tmp;

    tmp = addr + offset;
    env->gsr &= ~7ULL;
    env->gsr |= tmp & 7ULL;
    return tmp & ~7ULL;
}

void helper_faligndata(CPUState *env)
{
    uint64_t tmp;

    tmp = (*((uint64_t *)&DT0)) << ((env->gsr & 7) * 8);
    /* on many architectures a shift of 64 does nothing */
    if ((env->gsr & 7) != 0) {
        tmp |= (*((uint64_t *)&DT1)) >> (64 - (env->gsr & 7) * 8);
    }
    *((uint64_t *)&DT0) = tmp;
}

#ifdef HOST_WORDS_BIGENDIAN
#define VIS_B64(n) b[7 - (n)]
#define VIS_W64(n) w[3 - (n)]
#define VIS_SW64(n) sw[3 - (n)]
#define VIS_L64(n) l[1 - (n)]
#define VIS_B32(n) b[3 - (n)]
#define VIS_W32(n) w[1 - (n)]
#else
#define VIS_B64(n) b[n]
#define VIS_W64(n) w[n]
#define VIS_SW64(n) sw[n]
#define VIS_L64(n) l[n]
#define VIS_B32(n) b[n]
#define VIS_W32(n) w[n]
#endif

typedef union {
    uint8_t b[8];
    uint16_t w[4];
    int16_t sw[4];
    uint32_t l[2];
    uint64_t ll;
    float64 d;
} VIS64;

typedef union {
    uint8_t b[4];
    uint16_t w[2];
    uint32_t l;
    float32 f;
} VIS32;

void helper_fpmerge(CPUState *env)
{
    VIS64 s, d;

    s.d = DT0;
    d.d = DT1;

    /* Reverse calculation order to handle overlap */
    d.VIS_B64(7) = s.VIS_B64(3);
    d.VIS_B64(6) = d.VIS_B64(3);
    d.VIS_B64(5) = s.VIS_B64(2);
    d.VIS_B64(4) = d.VIS_B64(2);
    d.VIS_B64(3) = s.VIS_B64(1);
    d.VIS_B64(2) = d.VIS_B64(1);
    d.VIS_B64(1) = s.VIS_B64(0);
    /* d.VIS_B64(0) = d.VIS_B64(0); */

    DT0 = d.d;
}

void helper_fmul8x16(CPUState *env)
{
    VIS64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(r) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f) {                                  \
        tmp += 0x100;                                           \
    }                                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8x16al(CPUState *env)
{
    VIS64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(1) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f) {                                  \
        tmp += 0x100;                                           \
    }                                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8x16au(CPUState *env)
{
    VIS64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(0) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f) {                                  \
        tmp += 0x100;                                           \
    }                                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8sux16(CPUState *env)
{
    VIS64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((int32_t)s.VIS_SW64(r) >> 8);       \
    if ((tmp & 0xff) > 0x7f) {                                          \
        tmp += 0x100;                                                   \
    }                                                                   \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8ulx16(CPUState *env)
{
    VIS64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((uint32_t)s.VIS_B64(r * 2));        \
    if ((tmp & 0xff) > 0x7f) {                                          \
        tmp += 0x100;                                                   \
    }                                                                   \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmuld8sux16(CPUState *env)
{
    VIS64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((int32_t)s.VIS_SW64(r) >> 8);       \
    if ((tmp & 0xff) > 0x7f) {                                          \
        tmp += 0x100;                                                   \
    }                                                                   \
    d.VIS_L64(r) = tmp;

    /* Reverse calculation order to handle overlap */
    PMUL(1);
    PMUL(0);
#undef PMUL

    DT0 = d.d;
}

void helper_fmuld8ulx16(CPUState *env)
{
    VIS64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((uint32_t)s.VIS_B64(r * 2));        \
    if ((tmp & 0xff) > 0x7f) {                                          \
        tmp += 0x100;                                                   \
    }                                                                   \
    d.VIS_L64(r) = tmp;

    /* Reverse calculation order to handle overlap */
    PMUL(1);
    PMUL(0);
#undef PMUL

    DT0 = d.d;
}

void helper_fexpand(CPUState *env)
{
    VIS32 s;
    VIS64 d;

    s.l = (uint32_t)(*(uint64_t *)&DT0 & 0xffffffff);
    d.d = DT1;
    d.VIS_W64(0) = s.VIS_B32(0) << 4;
    d.VIS_W64(1) = s.VIS_B32(1) << 4;
    d.VIS_W64(2) = s.VIS_B32(2) << 4;
    d.VIS_W64(3) = s.VIS_B32(3) << 4;

    DT0 = d.d;
}

#define VIS_HELPER(name, F)                             \
    void name##16(CPUState *env)                        \
    {                                                   \
        VIS64 s, d;                                     \
                                                        \
        s.d = DT0;                                      \
        d.d = DT1;                                      \
                                                        \
        d.VIS_W64(0) = F(d.VIS_W64(0), s.VIS_W64(0));   \
        d.VIS_W64(1) = F(d.VIS_W64(1), s.VIS_W64(1));   \
        d.VIS_W64(2) = F(d.VIS_W64(2), s.VIS_W64(2));   \
        d.VIS_W64(3) = F(d.VIS_W64(3), s.VIS_W64(3));   \
                                                        \
        DT0 = d.d;                                      \
    }                                                   \
                                                        \
    uint32_t name##16s(CPUState *env, uint32_t src1,    \
                       uint32_t src2)                   \
    {                                                   \
        VIS32 s, d;                                     \
                                                        \
        s.l = src1;                                     \
        d.l = src2;                                     \
                                                        \
        d.VIS_W32(0) = F(d.VIS_W32(0), s.VIS_W32(0));   \
        d.VIS_W32(1) = F(d.VIS_W32(1), s.VIS_W32(1));   \
                                                        \
        return d.l;                                     \
    }                                                   \
                                                        \
    void name##32(CPUState *env)                        \
    {                                                   \
        VIS64 s, d;                                     \
                                                        \
        s.d = DT0;                                      \
        d.d = DT1;                                      \
                                                        \
        d.VIS_L64(0) = F(d.VIS_L64(0), s.VIS_L64(0));   \
        d.VIS_L64(1) = F(d.VIS_L64(1), s.VIS_L64(1));   \
                                                        \
        DT0 = d.d;                                      \
    }                                                   \
                                                        \
    uint32_t name##32s(CPUState *env, uint32_t src1,    \
                       uint32_t src2)                   \
    {                                                   \
        VIS32 s, d;                                     \
                                                        \
        s.l = src1;                                     \
        d.l = src2;                                     \
                                                        \
        d.l = F(d.l, s.l);                              \
                                                        \
        return d.l;                                     \
    }

#define FADD(a, b) ((a) + (b))
#define FSUB(a, b) ((a) - (b))
VIS_HELPER(helper_fpadd, FADD)
VIS_HELPER(helper_fpsub, FSUB)

#define VIS_CMPHELPER(name, F)                                    \
    uint64_t name##16(CPUState *env)                              \
    {                                                             \
        VIS64 s, d;                                               \
                                                                  \
        s.d = DT0;                                                \
        d.d = DT1;                                                \
                                                                  \
        d.VIS_W64(0) = F(s.VIS_W64(0), d.VIS_W64(0)) ? 1 : 0;     \
        d.VIS_W64(0) |= F(s.VIS_W64(1), d.VIS_W64(1)) ? 2 : 0;    \
        d.VIS_W64(0) |= F(s.VIS_W64(2), d.VIS_W64(2)) ? 4 : 0;    \
        d.VIS_W64(0) |= F(s.VIS_W64(3), d.VIS_W64(3)) ? 8 : 0;    \
        d.VIS_W64(1) = d.VIS_W64(2) = d.VIS_W64(3) = 0;           \
                                                                  \
        return d.ll;                                              \
    }                                                             \
                                                                  \
    uint64_t name##32(CPUState *env)                              \
    {                                                             \
        VIS64 s, d;                                               \
                                                                  \
        s.d = DT0;                                                \
        d.d = DT1;                                                \
                                                                  \
        d.VIS_L64(0) = F(s.VIS_L64(0), d.VIS_L64(0)) ? 1 : 0;     \
        d.VIS_L64(0) |= F(s.VIS_L64(1), d.VIS_L64(1)) ? 2 : 0;    \
        d.VIS_L64(1) = 0;                                         \
                                                                  \
        return d.ll;                                              \
    }

#define FCMPGT(a, b) ((a) > (b))
#define FCMPEQ(a, b) ((a) == (b))
#define FCMPLE(a, b) ((a) <= (b))
#define FCMPNE(a, b) ((a) != (b))

VIS_CMPHELPER(helper_fcmpgt, FCMPGT)
VIS_CMPHELPER(helper_fcmpeq, FCMPEQ)
VIS_CMPHELPER(helper_fcmple, FCMPLE)
VIS_CMPHELPER(helper_fcmpne, FCMPNE)
