/*
   SPARC micro operations

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "exec.h"
#include "helper.h"

#define REGNAME f0
#define REG (env->fpr[0])
#include "fop_template.h"
#define REGNAME f1
#define REG (env->fpr[1])
#include "fop_template.h"
#define REGNAME f2
#define REG (env->fpr[2])
#include "fop_template.h"
#define REGNAME f3
#define REG (env->fpr[3])
#include "fop_template.h"
#define REGNAME f4
#define REG (env->fpr[4])
#include "fop_template.h"
#define REGNAME f5
#define REG (env->fpr[5])
#include "fop_template.h"
#define REGNAME f6
#define REG (env->fpr[6])
#include "fop_template.h"
#define REGNAME f7
#define REG (env->fpr[7])
#include "fop_template.h"
#define REGNAME f8
#define REG (env->fpr[8])
#include "fop_template.h"
#define REGNAME f9
#define REG (env->fpr[9])
#include "fop_template.h"
#define REGNAME f10
#define REG (env->fpr[10])
#include "fop_template.h"
#define REGNAME f11
#define REG (env->fpr[11])
#include "fop_template.h"
#define REGNAME f12
#define REG (env->fpr[12])
#include "fop_template.h"
#define REGNAME f13
#define REG (env->fpr[13])
#include "fop_template.h"
#define REGNAME f14
#define REG (env->fpr[14])
#include "fop_template.h"
#define REGNAME f15
#define REG (env->fpr[15])
#include "fop_template.h"
#define REGNAME f16
#define REG (env->fpr[16])
#include "fop_template.h"
#define REGNAME f17
#define REG (env->fpr[17])
#include "fop_template.h"
#define REGNAME f18
#define REG (env->fpr[18])
#include "fop_template.h"
#define REGNAME f19
#define REG (env->fpr[19])
#include "fop_template.h"
#define REGNAME f20
#define REG (env->fpr[20])
#include "fop_template.h"
#define REGNAME f21
#define REG (env->fpr[21])
#include "fop_template.h"
#define REGNAME f22
#define REG (env->fpr[22])
#include "fop_template.h"
#define REGNAME f23
#define REG (env->fpr[23])
#include "fop_template.h"
#define REGNAME f24
#define REG (env->fpr[24])
#include "fop_template.h"
#define REGNAME f25
#define REG (env->fpr[25])
#include "fop_template.h"
#define REGNAME f26
#define REG (env->fpr[26])
#include "fop_template.h"
#define REGNAME f27
#define REG (env->fpr[27])
#include "fop_template.h"
#define REGNAME f28
#define REG (env->fpr[28])
#include "fop_template.h"
#define REGNAME f29
#define REG (env->fpr[29])
#include "fop_template.h"
#define REGNAME f30
#define REG (env->fpr[30])
#include "fop_template.h"
#define REGNAME f31
#define REG (env->fpr[31])
#include "fop_template.h"

#ifdef TARGET_SPARC64
#define REGNAME f32
#define REG (env->fpr[32])
#include "fop_template.h"
#define REGNAME f34
#define REG (env->fpr[34])
#include "fop_template.h"
#define REGNAME f36
#define REG (env->fpr[36])
#include "fop_template.h"
#define REGNAME f38
#define REG (env->fpr[38])
#include "fop_template.h"
#define REGNAME f40
#define REG (env->fpr[40])
#include "fop_template.h"
#define REGNAME f42
#define REG (env->fpr[42])
#include "fop_template.h"
#define REGNAME f44
#define REG (env->fpr[44])
#include "fop_template.h"
#define REGNAME f46
#define REG (env->fpr[46])
#include "fop_template.h"
#define REGNAME f48
#define REG (env->fpr[47])
#include "fop_template.h"
#define REGNAME f50
#define REG (env->fpr[50])
#include "fop_template.h"
#define REGNAME f52
#define REG (env->fpr[52])
#include "fop_template.h"
#define REGNAME f54
#define REG (env->fpr[54])
#include "fop_template.h"
#define REGNAME f56
#define REG (env->fpr[56])
#include "fop_template.h"
#define REGNAME f58
#define REG (env->fpr[58])
#include "fop_template.h"
#define REGNAME f60
#define REG (env->fpr[60])
#include "fop_template.h"
#define REGNAME f62
#define REG (env->fpr[62])
#include "fop_template.h"
#endif

#define FLAG_SET(x) ((env->psr&x)?1:0)

void OPPROTO op_udiv_T1_T0(void)
{
    uint64_t x0;
    uint32_t x1;

    x0 = T0 | ((uint64_t) (env->y) << 32);
    x1 = T1;

    if (x1 == 0) {
        raise_exception(TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if (x0 > 0xffffffff) {
        T0 = 0xffffffff;
        T1 = 1;
    } else {
        T0 = x0;
        T1 = 0;
    }
    FORCE_RET();
}

void OPPROTO op_sdiv_T1_T0(void)
{
    int64_t x0;
    int32_t x1;

    x0 = T0 | ((int64_t) (env->y) << 32);
    x1 = T1;

    if (x1 == 0) {
        raise_exception(TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if ((int32_t) x0 != x0) {
        T0 = x0 < 0? 0x80000000: 0x7fffffff;
        T1 = 1;
    } else {
        T0 = x0;
        T1 = 0;
    }
    FORCE_RET();
}

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"

#define MEMSUFFIX _kernel
#include "op_mem.h"

#ifdef TARGET_SPARC64
#define MEMSUFFIX _hypv
#include "op_mem.h"
#endif
#endif

#ifndef TARGET_SPARC64
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void OPPROTO op_save(void)
{
    uint32_t cwp;
    cwp = (env->cwp - 1) & (NWINDOWS - 1);
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_OVF);
    }
    set_cwp(cwp);
    FORCE_RET();
}

void OPPROTO op_restore(void)
{
    uint32_t cwp;
    cwp = (env->cwp + 1) & (NWINDOWS - 1);
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_UNF);
    }
    set_cwp(cwp);
    FORCE_RET();
}
#else
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void OPPROTO op_save(void)
{
    uint32_t cwp;
    cwp = (env->cwp - 1) & (NWINDOWS - 1);
    if (env->cansave == 0) {
        raise_exception(TT_SPILL | (env->otherwin != 0 ?
                                    (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                    ((env->wstate & 0x7) << 2)));
    } else {
        if (env->cleanwin - env->canrestore == 0) {
            // XXX Clean windows without trap
            raise_exception(TT_CLRWIN);
        } else {
            env->cansave--;
            env->canrestore++;
            set_cwp(cwp);
        }
    }
    FORCE_RET();
}

void OPPROTO op_restore(void)
{
    uint32_t cwp;
    cwp = (env->cwp + 1) & (NWINDOWS - 1);
    if (env->canrestore == 0) {
        raise_exception(TT_FILL | (env->otherwin != 0 ?
                                   (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                   ((env->wstate & 0x7) << 2)));
    } else {
        env->cansave++;
        env->canrestore--;
        set_cwp(cwp);
    }
    FORCE_RET();
}
#endif

void OPPROTO op_jmp_label(void)
{
    GOTO_LABEL_PARAM(1);
}

#define F_OP(name, p) void OPPROTO op_f##name##p(void)

#if defined(CONFIG_USER_ONLY)
#define F_BINOP(name)                                           \
    F_OP(name, s)                                               \
    {                                                           \
        FT0 = float32_ ## name (FT0, FT1, &env->fp_status);     \
    }                                                           \
    F_OP(name, d)                                               \
    {                                                           \
        DT0 = float64_ ## name (DT0, DT1, &env->fp_status);     \
    }                                                           \
    F_OP(name, q)                                               \
    {                                                           \
        QT0 = float128_ ## name (QT0, QT1, &env->fp_status);    \
    }
#else
#define F_BINOP(name)                                           \
    F_OP(name, s)                                               \
    {                                                           \
        FT0 = float32_ ## name (FT0, FT1, &env->fp_status);     \
    }                                                           \
    F_OP(name, d)                                               \
    {                                                           \
        DT0 = float64_ ## name (DT0, DT1, &env->fp_status);     \
    }
#endif

F_BINOP(add);
F_BINOP(sub);
F_BINOP(mul);
F_BINOP(div);
#undef F_BINOP

void OPPROTO op_fsmuld(void)
{
    DT0 = float64_mul(float32_to_float64(FT0, &env->fp_status),
                      float32_to_float64(FT1, &env->fp_status),
                      &env->fp_status);
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fdmulq(void)
{
    QT0 = float128_mul(float64_to_float128(DT0, &env->fp_status),
                       float64_to_float128(DT1, &env->fp_status),
                       &env->fp_status);
}
#endif

#if defined(CONFIG_USER_ONLY)
#define F_HELPER(name)    \
    F_OP(name, s)         \
    {                     \
        do_f##name##s();  \
    }                     \
    F_OP(name, d)         \
    {                     \
        do_f##name##d();  \
    }                     \
    F_OP(name, q)         \
    {                     \
        do_f##name##q();  \
    }
#else
#define F_HELPER(name)    \
    F_OP(name, s)         \
    {                     \
        do_f##name##s();  \
    }                     \
    F_OP(name, d)         \
    {                     \
        do_f##name##d();  \
    }
#endif

F_OP(neg, s)
{
    FT0 = float32_chs(FT1);
}

#ifdef TARGET_SPARC64
F_OP(neg, d)
{
    DT0 = float64_chs(DT1);
}

#if defined(CONFIG_USER_ONLY)
F_OP(neg, q)
{
    QT0 = float128_chs(QT1);
}

#endif

#endif

/* Integer to float conversion.  */
#ifdef USE_INT_TO_FLOAT_HELPERS
F_HELPER(ito);
#ifdef TARGET_SPARC64
F_HELPER(xto);
#endif
#else
F_OP(ito, s)
{
    FT0 = int32_to_float32(*((int32_t *)&FT1), &env->fp_status);
}

F_OP(ito, d)
{
    DT0 = int32_to_float64(*((int32_t *)&FT1), &env->fp_status);
}

#if defined(CONFIG_USER_ONLY)
F_OP(ito, q)
{
    QT0 = int32_to_float128(*((int32_t *)&FT1), &env->fp_status);
}
#endif

#ifdef TARGET_SPARC64
F_OP(xto, s)
{
    FT0 = int64_to_float32(*((int64_t *)&DT1), &env->fp_status);
}

F_OP(xto, d)
{
    DT0 = int64_to_float64(*((int64_t *)&DT1), &env->fp_status);
}
#if defined(CONFIG_USER_ONLY)
F_OP(xto, q)
{
    QT0 = int64_to_float128(*((int64_t *)&DT1), &env->fp_status);
}
#endif
#endif
#endif
#undef F_HELPER

/* floating point conversion */
void OPPROTO op_fdtos(void)
{
    FT0 = float64_to_float32(DT1, &env->fp_status);
}

void OPPROTO op_fstod(void)
{
    DT0 = float32_to_float64(FT1, &env->fp_status);
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fqtos(void)
{
    FT0 = float128_to_float32(QT1, &env->fp_status);
}

void OPPROTO op_fstoq(void)
{
    QT0 = float32_to_float128(FT1, &env->fp_status);
}

void OPPROTO op_fqtod(void)
{
    DT0 = float128_to_float64(QT1, &env->fp_status);
}

void OPPROTO op_fdtoq(void)
{
    QT0 = float64_to_float128(DT1, &env->fp_status);
}
#endif

/* Float to integer conversion.  */
void OPPROTO op_fstoi(void)
{
    *((int32_t *)&FT0) = float32_to_int32_round_to_zero(FT1, &env->fp_status);
}

void OPPROTO op_fdtoi(void)
{
    *((int32_t *)&FT0) = float64_to_int32_round_to_zero(DT1, &env->fp_status);
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fqtoi(void)
{
    *((int32_t *)&FT0) = float128_to_int32_round_to_zero(QT1, &env->fp_status);
}
#endif

#ifdef TARGET_SPARC64
void OPPROTO op_fstox(void)
{
    *((int64_t *)&DT0) = float32_to_int64_round_to_zero(FT1, &env->fp_status);
}

void OPPROTO op_fdtox(void)
{
    *((int64_t *)&DT0) = float64_to_int64_round_to_zero(DT1, &env->fp_status);
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fqtox(void)
{
    *((int64_t *)&DT0) = float128_to_int64_round_to_zero(QT1, &env->fp_status);
}
#endif

void OPPROTO op_flushw(void)
{
    if (env->cansave != NWINDOWS - 2) {
        raise_exception(TT_SPILL | (env->otherwin != 0 ?
                                    (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                    ((env->wstate & 0x7) << 2)));
    }
}

void OPPROTO op_saved(void)
{
    env->cansave++;
    if (env->otherwin == 0)
        env->canrestore--;
    else
        env->otherwin--;
    FORCE_RET();
}

void OPPROTO op_restored(void)
{
    env->canrestore++;
    if (env->cleanwin < NWINDOWS - 1)
        env->cleanwin++;
    if (env->otherwin == 0)
        env->cansave--;
    else
        env->otherwin--;
    FORCE_RET();
}
#endif

#ifdef TARGET_SPARC64
void OPPROTO op_faligndata()
{
    uint64_t tmp;

    tmp = (*((uint64_t *)&DT0)) << ((env->gsr & 7) * 8);
    tmp |= (*((uint64_t *)&DT1)) >> (64 - (env->gsr & 7) * 8);
    *((uint64_t *)&DT0) = tmp;
}

void OPPROTO op_movl_FT0_0(void)
{
    *((uint32_t *)&FT0) = 0;
}

void OPPROTO op_movl_DT0_0(void)
{
    *((uint64_t *)&DT0) = 0;
}

void OPPROTO op_movl_FT0_1(void)
{
    *((uint32_t *)&FT0) = 0xffffffff;
}

void OPPROTO op_movl_DT0_1(void)
{
    *((uint64_t *)&DT0) = 0xffffffffffffffffULL;
}

void OPPROTO op_fnot(void)
{
    *(uint64_t *)&DT0 = ~*(uint64_t *)&DT1;
}

void OPPROTO op_fnots(void)
{
    *(uint32_t *)&FT0 = ~*(uint32_t *)&FT1;
}

void OPPROTO op_fnor(void)
{
    *(uint64_t *)&DT0 = ~(*(uint64_t *)&DT0 | *(uint64_t *)&DT1);
}

void OPPROTO op_fnors(void)
{
    *(uint32_t *)&FT0 = ~(*(uint32_t *)&FT0 | *(uint32_t *)&FT1);
}

void OPPROTO op_for(void)
{
    *(uint64_t *)&DT0 |= *(uint64_t *)&DT1;
}

void OPPROTO op_fors(void)
{
    *(uint32_t *)&FT0 |= *(uint32_t *)&FT1;
}

void OPPROTO op_fxor(void)
{
    *(uint64_t *)&DT0 ^= *(uint64_t *)&DT1;
}

void OPPROTO op_fxors(void)
{
    *(uint32_t *)&FT0 ^= *(uint32_t *)&FT1;
}

void OPPROTO op_fand(void)
{
    *(uint64_t *)&DT0 &= *(uint64_t *)&DT1;
}

void OPPROTO op_fands(void)
{
    *(uint32_t *)&FT0 &= *(uint32_t *)&FT1;
}

void OPPROTO op_fornot(void)
{
    *(uint64_t *)&DT0 = *(uint64_t *)&DT0 | ~*(uint64_t *)&DT1;
}

void OPPROTO op_fornots(void)
{
    *(uint32_t *)&FT0 = *(uint32_t *)&FT0 | ~*(uint32_t *)&FT1;
}

void OPPROTO op_fandnot(void)
{
    *(uint64_t *)&DT0 = *(uint64_t *)&DT0 & ~*(uint64_t *)&DT1;
}

void OPPROTO op_fandnots(void)
{
    *(uint32_t *)&FT0 = *(uint32_t *)&FT0 & ~*(uint32_t *)&FT1;
}

void OPPROTO op_fnand(void)
{
    *(uint64_t *)&DT0 = ~(*(uint64_t *)&DT0 & *(uint64_t *)&DT1);
}

void OPPROTO op_fnands(void)
{
    *(uint32_t *)&FT0 = ~(*(uint32_t *)&FT0 & *(uint32_t *)&FT1);
}

void OPPROTO op_fxnor(void)
{
    *(uint64_t *)&DT0 ^= ~*(uint64_t *)&DT1;
}

void OPPROTO op_fxnors(void)
{
    *(uint32_t *)&FT0 ^= ~*(uint32_t *)&FT1;
}

#ifdef WORDS_BIGENDIAN
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
    float64 d;
} vis64;

typedef union {
    uint8_t b[4];
    uint16_t w[2];
    uint32_t l;
    float32 f;
} vis32;

void OPPROTO op_fpmerge(void)
{
    vis64 s, d;

    s.d = DT0;
    d.d = DT1;

    // Reverse calculation order to handle overlap
    d.VIS_B64(7) = s.VIS_B64(3);
    d.VIS_B64(6) = d.VIS_B64(3);
    d.VIS_B64(5) = s.VIS_B64(2);
    d.VIS_B64(4) = d.VIS_B64(2);
    d.VIS_B64(3) = s.VIS_B64(1);
    d.VIS_B64(2) = d.VIS_B64(1);
    d.VIS_B64(1) = s.VIS_B64(0);
    //d.VIS_B64(0) = d.VIS_B64(0);

    DT0 = d.d;
}

void OPPROTO op_fmul8x16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(r) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f)                                    \
        tmp += 0x100;                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void OPPROTO op_fmul8x16al(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(1) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f)                                    \
        tmp += 0x100;                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void OPPROTO op_fmul8x16au(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(0) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f)                                    \
        tmp += 0x100;                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void OPPROTO op_fmul8sux16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((int32_t)s.VIS_SW64(r) >> 8);       \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void OPPROTO op_fmul8ulx16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((uint32_t)s.VIS_B64(r * 2));        \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void OPPROTO op_fmuld8sux16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((int32_t)s.VIS_SW64(r) >> 8);       \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_L64(r) = tmp;

    // Reverse calculation order to handle overlap
    PMUL(1);
    PMUL(0);
#undef PMUL

    DT0 = d.d;
}

void OPPROTO op_fmuld8ulx16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((uint32_t)s.VIS_B64(r * 2));        \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_L64(r) = tmp;

    // Reverse calculation order to handle overlap
    PMUL(1);
    PMUL(0);
#undef PMUL

    DT0 = d.d;
}

void OPPROTO op_fexpand(void)
{
    vis32 s;
    vis64 d;

    s.l = (uint32_t)(*(uint64_t *)&DT0 & 0xffffffff);
    d.d = DT1;
    d.VIS_L64(0) = s.VIS_W32(0) << 4;
    d.VIS_L64(1) = s.VIS_W32(1) << 4;
    d.VIS_L64(2) = s.VIS_W32(2) << 4;
    d.VIS_L64(3) = s.VIS_W32(3) << 4;

    DT0 = d.d;
}

#define VIS_OP(name, F)                                 \
    void OPPROTO name##16(void)                         \
    {                                                   \
        vis64 s, d;                                     \
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
    void OPPROTO name##16s(void)                        \
    {                                                   \
        vis32 s, d;                                     \
                                                        \
        s.f = FT0;                                      \
        d.f = FT1;                                      \
                                                        \
        d.VIS_W32(0) = F(d.VIS_W32(0), s.VIS_W32(0));   \
        d.VIS_W32(1) = F(d.VIS_W32(1), s.VIS_W32(1));   \
                                                        \
        FT0 = d.f;                                      \
    }                                                   \
                                                        \
    void OPPROTO name##32(void)                         \
    {                                                   \
        vis64 s, d;                                     \
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
    void OPPROTO name##32s(void)                        \
    {                                                   \
        vis32 s, d;                                     \
                                                        \
        s.f = FT0;                                      \
        d.f = FT1;                                      \
                                                        \
        d.l = F(d.l, s.l);                              \
                                                        \
        FT0 = d.f;                                      \
    }

#define FADD(a, b) ((a) + (b))
#define FSUB(a, b) ((a) - (b))
VIS_OP(op_fpadd, FADD)
VIS_OP(op_fpsub, FSUB)

#define VIS_CMPOP(name, F)                                        \
    void OPPROTO name##16(void)                                   \
    {                                                             \
        vis64 s, d;                                               \
                                                                  \
        s.d = DT0;                                                \
        d.d = DT1;                                                \
                                                                  \
        d.VIS_W64(0) = F(d.VIS_W64(0), s.VIS_W64(0))? 1: 0;       \
        d.VIS_W64(0) |= F(d.VIS_W64(1), s.VIS_W64(1))? 2: 0;      \
        d.VIS_W64(0) |= F(d.VIS_W64(2), s.VIS_W64(2))? 4: 0;      \
        d.VIS_W64(0) |= F(d.VIS_W64(3), s.VIS_W64(3))? 8: 0;      \
                                                                  \
        DT0 = d.d;                                                \
    }                                                             \
                                                                  \
    void OPPROTO name##32(void)                                   \
    {                                                             \
        vis64 s, d;                                               \
                                                                  \
        s.d = DT0;                                                \
        d.d = DT1;                                                \
                                                                  \
        d.VIS_L64(0) = F(d.VIS_L64(0), s.VIS_L64(0))? 1: 0;       \
        d.VIS_L64(0) |= F(d.VIS_L64(1), s.VIS_L64(1))? 2: 0;      \
                                                                  \
        DT0 = d.d;                                                \
    }

#define FCMPGT(a, b) ((a) > (b))
#define FCMPEQ(a, b) ((a) == (b))
#define FCMPLE(a, b) ((a) <= (b))
#define FCMPNE(a, b) ((a) != (b))

VIS_CMPOP(op_fcmpgt, FCMPGT)
VIS_CMPOP(op_fcmpeq, FCMPEQ)
VIS_CMPOP(op_fcmple, FCMPLE)
VIS_CMPOP(op_fcmpne, FCMPNE)

#endif

#define CHECK_ALIGN_OP(align)                           \
    void OPPROTO op_check_align_T0_ ## align (void)     \
    {                                                   \
        if (T0 & align)                                 \
            raise_exception(TT_UNALIGNED);              \
        FORCE_RET();                                    \
    }

CHECK_ALIGN_OP(1)
CHECK_ALIGN_OP(3)
CHECK_ALIGN_OP(7)
