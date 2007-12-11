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

 /*XXX*/
#define REGNAME g0
#define REG (env->gregs[0])
#include "op_template.h"
#define REGNAME g1
#define REG (env->gregs[1])
#include "op_template.h"
#define REGNAME g2
#define REG (env->gregs[2])
#include "op_template.h"
#define REGNAME g3
#define REG (env->gregs[3])
#include "op_template.h"
#define REGNAME g4
#define REG (env->gregs[4])
#include "op_template.h"
#define REGNAME g5
#define REG (env->gregs[5])
#include "op_template.h"
#define REGNAME g6
#define REG (env->gregs[6])
#include "op_template.h"
#define REGNAME g7
#define REG (env->gregs[7])
#include "op_template.h"
#define REGNAME i0
#define REG (REGWPTR[16])
#include "op_template.h"
#define REGNAME i1
#define REG (REGWPTR[17])
#include "op_template.h"
#define REGNAME i2
#define REG (REGWPTR[18])
#include "op_template.h"
#define REGNAME i3
#define REG (REGWPTR[19])
#include "op_template.h"
#define REGNAME i4
#define REG (REGWPTR[20])
#include "op_template.h"
#define REGNAME i5
#define REG (REGWPTR[21])
#include "op_template.h"
#define REGNAME i6
#define REG (REGWPTR[22])
#include "op_template.h"
#define REGNAME i7
#define REG (REGWPTR[23])
#include "op_template.h"
#define REGNAME l0
#define REG (REGWPTR[8])
#include "op_template.h"
#define REGNAME l1
#define REG (REGWPTR[9])
#include "op_template.h"
#define REGNAME l2
#define REG (REGWPTR[10])
#include "op_template.h"
#define REGNAME l3
#define REG (REGWPTR[11])
#include "op_template.h"
#define REGNAME l4
#define REG (REGWPTR[12])
#include "op_template.h"
#define REGNAME l5
#define REG (REGWPTR[13])
#include "op_template.h"
#define REGNAME l6
#define REG (REGWPTR[14])
#include "op_template.h"
#define REGNAME l7
#define REG (REGWPTR[15])
#include "op_template.h"
#define REGNAME o0
#define REG (REGWPTR[0])
#include "op_template.h"
#define REGNAME o1
#define REG (REGWPTR[1])
#include "op_template.h"
#define REGNAME o2
#define REG (REGWPTR[2])
#include "op_template.h"
#define REGNAME o3
#define REG (REGWPTR[3])
#include "op_template.h"
#define REGNAME o4
#define REG (REGWPTR[4])
#include "op_template.h"
#define REGNAME o5
#define REG (REGWPTR[5])
#include "op_template.h"
#define REGNAME o6
#define REG (REGWPTR[6])
#include "op_template.h"
#define REGNAME o7
#define REG (REGWPTR[7])
#include "op_template.h"

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

#ifdef TARGET_SPARC64
#ifdef WORDS_BIGENDIAN
typedef union UREG64 {
    struct { uint16_t v3, v2, v1, v0; } w;
    struct { uint32_t v1, v0; } l;
    uint64_t q;
} UREG64;
#else
typedef union UREG64 {
    struct { uint16_t v0, v1, v2, v3; } w;
    struct { uint32_t v0, v1; } l;
    uint64_t q;
} UREG64;
#endif

#define PARAMQ1 \
({\
    UREG64 __p;\
    __p.l.v1 = PARAM1;\
    __p.l.v0 = PARAM2;\
    __p.q;\
})

void OPPROTO op_movq_T0_im64(void)
{
    T0 = PARAMQ1;
}

void OPPROTO op_movq_T1_im64(void)
{
    T1 = PARAMQ1;
}

#define XFLAG_SET(x) ((env->xcc&x)?1:0)

#else
#define EIP (env->pc)
#endif

#define FLAG_SET(x) ((env->psr&x)?1:0)

void OPPROTO op_movl_T0_0(void)
{
    T0 = 0;
}

void OPPROTO op_movl_T0_im(void)
{
    T0 = (uint32_t)PARAM1;
}

void OPPROTO op_movl_T1_im(void)
{
    T1 = (uint32_t)PARAM1;
}

void OPPROTO op_movl_T2_im(void)
{
    T2 = (uint32_t)PARAM1;
}

void OPPROTO op_movl_T0_sim(void)
{
    T0 = (int32_t)PARAM1;
}

void OPPROTO op_movl_T1_sim(void)
{
    T1 = (int32_t)PARAM1;
}

void OPPROTO op_movl_T2_sim(void)
{
    T2 = (int32_t)PARAM1;
}

void OPPROTO op_movl_T0_env(void)
{
    T0 = *(uint32_t *)((char *)env + PARAM1);
}

void OPPROTO op_movl_env_T0(void)
{
    *(uint32_t *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_movtl_T0_env(void)
{
    T0 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_movtl_env_T0(void)
{
    *(target_ulong *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_add_T1_T0(void)
{
    T0 += T1;
}

void OPPROTO op_add_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 += T1;
    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((T0 & 0xffffffff) < (src1 & 0xffffffff))
        env->psr |= PSR_CARRY;
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff) ^ -1) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        env->psr |= PSR_OVF;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (T0 < src1)
        env->xcc |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1ULL << 63))
        env->xcc |= PSR_OVF;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (T0 < src1)
        env->psr |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
        env->psr |= PSR_OVF;
#endif
    FORCE_RET();
}

void OPPROTO op_addx_T1_T0(void)
{
    T0 += T1 + FLAG_SET(PSR_CARRY);
}

void OPPROTO op_addx_T1_T0_cc(void)
{
    target_ulong src1;
    src1 = T0;
    if (FLAG_SET(PSR_CARRY))
    {
      T0 += T1 + 1;
      env->psr = 0;
#ifdef TARGET_SPARC64
      if ((T0 & 0xffffffff) <= (src1 & 0xffffffff))
        env->psr |= PSR_CARRY;
      env->xcc = 0;
      if (T0 <= src1)
        env->xcc |= PSR_CARRY;
#else
      if (T0 <= src1)
        env->psr |= PSR_CARRY;
#endif
    }
    else
    {
      T0 += T1;
      env->psr = 0;
#ifdef TARGET_SPARC64
      if ((T0 & 0xffffffff) < (src1 & 0xffffffff))
        env->psr |= PSR_CARRY;
      env->xcc = 0;
      if (T0 < src1)
        env->xcc |= PSR_CARRY;
#else
      if (T0 < src1)
        env->psr |= PSR_CARRY;
#endif
    }
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff) ^ -1) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        env->psr |= PSR_OVF;

    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1ULL << 63))
        env->xcc |= PSR_OVF;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
        env->psr |= PSR_OVF;
#endif
    FORCE_RET();
}

void OPPROTO op_tadd_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 += T1;
    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((T0 & 0xffffffff) < (src1 & 0xffffffff))
        env->psr |= PSR_CARRY;
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff) ^ -1) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        env->psr |= PSR_OVF;
    if ((src1 & 0x03) || (T1 & 0x03))
        env->psr |= PSR_OVF;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (T0 < src1)
        env->xcc |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1ULL << 63))
        env->xcc |= PSR_OVF;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (T0 < src1)
        env->psr |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
        env->psr |= PSR_OVF;
    if ((src1 & 0x03) || (T1 & 0x03))
        env->psr |= PSR_OVF;
#endif
    FORCE_RET();
}

void OPPROTO op_tadd_T1_T0_ccTV(void)
{
    target_ulong src1;

    if ((T0 & 0x03) || (T1 & 0x03)) {
        raise_exception(TT_TOVF);
        FORCE_RET();
        return;
    }

    src1 = T0;
    T0 += T1;

#ifdef TARGET_SPARC64
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff) ^ -1) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        raise_exception(TT_TOVF);
#else
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
        raise_exception(TT_TOVF);
#endif

    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((T0 & 0xffffffff) < (src1 & 0xffffffff))
        env->psr |= PSR_CARRY;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (T0 < src1)
        env->xcc |= PSR_CARRY;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (T0 < src1)
        env->psr |= PSR_CARRY;
#endif
    FORCE_RET();
}

void OPPROTO op_sub_T1_T0(void)
{
    T0 -= T1;
}

void OPPROTO op_sub_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 -= T1;
    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((src1 & 0xffffffff) < (T1 & 0xffffffff))
        env->psr |= PSR_CARRY;
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff)) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        env->psr |= PSR_OVF;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (src1 < T1)
        env->xcc |= PSR_CARRY;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1ULL << 63))
        env->xcc |= PSR_OVF;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (src1 < T1)
        env->psr |= PSR_CARRY;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1 << 31))
        env->psr |= PSR_OVF;
#endif
    FORCE_RET();
}

void OPPROTO op_subx_T1_T0(void)
{
    T0 -= T1 + FLAG_SET(PSR_CARRY);
}

void OPPROTO op_subx_T1_T0_cc(void)
{
    target_ulong src1;
    src1 = T0;
    if (FLAG_SET(PSR_CARRY))
    {
      T0 -= T1 + 1;
      env->psr = 0;
#ifdef TARGET_SPARC64
      if ((src1 & 0xffffffff) <= (T1 & 0xffffffff))
        env->psr |= PSR_CARRY;
      env->xcc = 0;
      if (src1 <= T1)
        env->xcc |= PSR_CARRY;
#else
      if (src1 <= T1)
        env->psr |= PSR_CARRY;
#endif
    }
    else
    {
      T0 -= T1;
      env->psr = 0;
#ifdef TARGET_SPARC64
      if ((src1 & 0xffffffff) < (T1 & 0xffffffff))
        env->psr |= PSR_CARRY;
      env->xcc = 0;
      if (src1 < T1)
        env->xcc |= PSR_CARRY;
#else
      if (src1 < T1)
        env->psr |= PSR_CARRY;
#endif
    }
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff)) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        env->psr |= PSR_OVF;

    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1ULL << 63))
        env->xcc |= PSR_OVF;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1 << 31))
        env->psr |= PSR_OVF;
#endif
    FORCE_RET();
}

void OPPROTO op_tsub_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 -= T1;
    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((src1 & 0xffffffff) < (T1 & 0xffffffff))
        env->psr |= PSR_CARRY;
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff)) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        env->psr |= PSR_OVF;
    if ((src1 & 0x03) || (T1 & 0x03))
        env->psr |= PSR_OVF;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (src1 < T1)
        env->xcc |= PSR_CARRY;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1ULL << 63))
        env->xcc |= PSR_OVF;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (src1 < T1)
        env->psr |= PSR_CARRY;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1 << 31))
        env->psr |= PSR_OVF;
    if ((src1 & 0x03) || (T1 & 0x03))
        env->psr |= PSR_OVF;
#endif
    FORCE_RET();
}

void OPPROTO op_tsub_T1_T0_ccTV(void)
{
    target_ulong src1;

    if ((T0 & 0x03) || (T1 & 0x03))
        raise_exception(TT_TOVF);

    src1 = T0;
    T0 -= T1;

#ifdef TARGET_SPARC64
    if ((((src1 & 0xffffffff) ^ (T1 & 0xffffffff)) &
         ((src1 & 0xffffffff) ^ (T0 & 0xffffffff))) & (1 << 31))
        raise_exception(TT_TOVF);
#else
    if (((src1 ^ T1) & (src1 ^ T0)) & (1 << 31))
        raise_exception(TT_TOVF);
#endif

    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if ((src1 & 0xffffffff) < (T1 & 0xffffffff))
        env->psr |= PSR_CARRY;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
    if (src1 < T1)
        env->xcc |= PSR_CARRY;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (src1 < T1)
        env->psr |= PSR_CARRY;
#endif
    FORCE_RET();
}

void OPPROTO op_and_T1_T0(void)
{
    T0 &= T1;
}

void OPPROTO op_or_T1_T0(void)
{
    T0 |= T1;
}

void OPPROTO op_xor_T1_T0(void)
{
    T0 ^= T1;
}

void OPPROTO op_andn_T1_T0(void)
{
    T0 &= ~T1;
}

void OPPROTO op_orn_T1_T0(void)
{
    T0 |= ~T1;
}

void OPPROTO op_xnor_T1_T0(void)
{
    T0 ^= ~T1;
}

void OPPROTO op_umul_T1_T0(void)
{
    uint64_t res;
    res = (uint64_t) T0 * (uint64_t) T1;
#ifdef TARGET_SPARC64
    T0 = res;
#else
    T0 = res & 0xffffffff;
#endif
    env->y = res >> 32;
}

void OPPROTO op_smul_T1_T0(void)
{
    uint64_t res;
    res = (int64_t) ((int32_t) T0) * (int64_t) ((int32_t) T1);
#ifdef TARGET_SPARC64
    T0 = res;
#else
    T0 = res & 0xffffffff;
#endif
    env->y = res >> 32;
}

void OPPROTO op_mulscc_T1_T0(void)
{
    unsigned int b1, N, V, b2;
    target_ulong src1;

    N = FLAG_SET(PSR_NEG);
    V = FLAG_SET(PSR_OVF);
    b1 = N ^ V;
    b2 = T0 & 1;
    T0 = (b1 << 31) | (T0 >> 1);
    if (!(env->y & 1))
        T1 = 0;
    /* do addition and update flags */
    src1 = T0;
    T0 += T1;
    env->psr = 0;
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (T0 < src1)
        env->psr |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
        env->psr |= PSR_OVF;
    env->y = (b2 << 31) | (env->y >> 1);
    FORCE_RET();
}

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

void OPPROTO op_div_cc(void)
{
    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (T1)
        env->psr |= PSR_OVF;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
    if (T1)
        env->psr |= PSR_OVF;
#endif
    FORCE_RET();
}

#ifdef TARGET_SPARC64
void OPPROTO op_mulx_T1_T0(void)
{
    T0 *= T1;
    FORCE_RET();
}

void OPPROTO op_udivx_T1_T0(void)
{
    if (T1 == 0) {
        raise_exception(TT_DIV_ZERO);
    }
    T0 /= T1;
    FORCE_RET();
}

void OPPROTO op_sdivx_T1_T0(void)
{
    if (T1 == 0) {
        raise_exception(TT_DIV_ZERO);
    }
    if (T0 == INT64_MIN && T1 == -1)
        T0 = INT64_MIN;
    else
        T0 /= (target_long) T1;
    FORCE_RET();
}
#endif

void OPPROTO op_logic_T0_cc(void)
{
    env->psr = 0;
#ifdef TARGET_SPARC64
    if (!(T0 & 0xffffffff))
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;

    env->xcc = 0;
    if (!T0)
        env->xcc |= PSR_ZERO;
    if ((int64_t) T0 < 0)
        env->xcc |= PSR_NEG;
#else
    if (!T0)
        env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
        env->psr |= PSR_NEG;
#endif
    FORCE_RET();
}

void OPPROTO op_sll(void)
{
    T0 <<= (T1 & 0x1f);
}

#ifdef TARGET_SPARC64
void OPPROTO op_sllx(void)
{
    T0 <<= (T1 & 0x3f);
}

void OPPROTO op_srl(void)
{
    T0 = (T0 & 0xffffffff) >> (T1 & 0x1f);
}

void OPPROTO op_srlx(void)
{
    T0 >>= (T1 & 0x3f);
}

void OPPROTO op_sra(void)
{
    T0 = ((int32_t) (T0 & 0xffffffff)) >> (T1 & 0x1f);
}

void OPPROTO op_srax(void)
{
    T0 = ((int64_t) T0) >> (T1 & 0x3f);
}
#else
void OPPROTO op_srl(void)
{
    T0 >>= (T1 & 0x1f);
}

void OPPROTO op_sra(void)
{
    T0 = ((int32_t) T0) >> (T1 & 0x1f);
}
#endif

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

void OPPROTO op_ldfsr(void)
{
    PUT_FSR32(env, *((uint32_t *) &FT0));
    helper_ldfsr();
}

void OPPROTO op_stfsr(void)
{
    *((uint32_t *) &FT0) = GET_FSR32(env);
}

#ifndef TARGET_SPARC64
void OPPROTO op_rdpsr(void)
{
    do_rdpsr();
}

void OPPROTO op_wrpsr(void)
{
    do_wrpsr();
    FORCE_RET();
}

void OPPROTO op_wrwim(void)
{
#if NWINDOWS == 32
    env->wim = T0;
#else
    env->wim = T0 & ((1 << NWINDOWS) - 1);
#endif
}

void OPPROTO op_rett(void)
{
    helper_rett();
    FORCE_RET();
}

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
void OPPROTO op_rdccr(void)
{
    T0 = GET_CCR(env);
}

void OPPROTO op_wrccr(void)
{
    PUT_CCR(env, T0);
}

void OPPROTO op_rdtick(void)
{
    T0 = do_tick_get_count(env->tick);
}

void OPPROTO op_wrtick(void)
{
    do_tick_set_count(env->tick, T0);
}

void OPPROTO op_wrtick_cmpr(void)
{
    do_tick_set_limit(env->tick, T0);
}

void OPPROTO op_rdstick(void)
{
    T0 = do_tick_get_count(env->stick);
}

void OPPROTO op_wrstick(void)
{
    do_tick_set_count(env->stick, T0);
    do_tick_set_count(env->hstick, T0);
}

void OPPROTO op_wrstick_cmpr(void)
{
    do_tick_set_limit(env->stick, T0);
}

void OPPROTO op_wrhstick_cmpr(void)
{
    do_tick_set_limit(env->hstick, T0);
}

void OPPROTO op_rdtpc(void)
{
    T0 = env->tpc[env->tl];
}

void OPPROTO op_wrtpc(void)
{
    env->tpc[env->tl] = T0;
}

void OPPROTO op_rdtnpc(void)
{
    T0 = env->tnpc[env->tl];
}

void OPPROTO op_wrtnpc(void)
{
    env->tnpc[env->tl] = T0;
}

void OPPROTO op_rdtstate(void)
{
    T0 = env->tstate[env->tl];
}

void OPPROTO op_wrtstate(void)
{
    env->tstate[env->tl] = T0;
}

void OPPROTO op_rdtt(void)
{
    T0 = env->tt[env->tl];
}

void OPPROTO op_wrtt(void)
{
    env->tt[env->tl] = T0;
}

void OPPROTO op_rdpstate(void)
{
    T0 = env->pstate;
}

void OPPROTO op_wrpstate(void)
{
    do_wrpstate();
}

// CWP handling is reversed in V9, but we still use the V8 register
// order.
void OPPROTO op_rdcwp(void)
{
    T0 = GET_CWP64(env);
}

void OPPROTO op_wrcwp(void)
{
    PUT_CWP64(env, T0);
}

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

void OPPROTO op_exception(void)
{
    env->exception_index = PARAM1;
    cpu_loop_exit();
    FORCE_RET();
}

void OPPROTO op_trap_T0(void)
{
    env->exception_index = TT_TRAP + (T0 & 0x7f);
    cpu_loop_exit();
    FORCE_RET();
}

void OPPROTO op_trapcc_T0(void)
{
    if (T2) {
        env->exception_index = TT_TRAP + (T0 & 0x7f);
        cpu_loop_exit();
    }
    FORCE_RET();
}

void OPPROTO op_fpexception_im(void)
{
    env->exception_index = TT_FP_EXCP;
    env->fsr &= ~FSR_FTT_MASK;
    env->fsr |= PARAM1;
    cpu_loop_exit();
    FORCE_RET();
}

void OPPROTO op_debug(void)
{
    helper_debug();
}

void OPPROTO op_exit_tb(void)
{
    EXIT_TB();
}

void OPPROTO op_eval_ba(void)
{
    T2 = 1;
}

void OPPROTO op_eval_be(void)
{
    T2 = FLAG_SET(PSR_ZERO);
}

void OPPROTO op_eval_ble(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = Z | (N ^ V);
}

void OPPROTO op_eval_bl(void)
{
    target_ulong N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = N ^ V;
}

void OPPROTO op_eval_bleu(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), C = FLAG_SET(PSR_CARRY);

    T2 = C | Z;
}

void OPPROTO op_eval_bcs(void)
{
    T2 = FLAG_SET(PSR_CARRY);
}

void OPPROTO op_eval_bvs(void)
{
    T2 = FLAG_SET(PSR_OVF);
}

void OPPROTO op_eval_bn(void)
{
    T2 = 0;
}

void OPPROTO op_eval_bneg(void)
{
    T2 = FLAG_SET(PSR_NEG);
}

void OPPROTO op_eval_bne(void)
{
    T2 = !FLAG_SET(PSR_ZERO);
}

void OPPROTO op_eval_bg(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = !(Z | (N ^ V));
}

void OPPROTO op_eval_bge(void)
{
    target_ulong N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = !(N ^ V);
}

void OPPROTO op_eval_bgu(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), C = FLAG_SET(PSR_CARRY);

    T2 = !(C | Z);
}

void OPPROTO op_eval_bcc(void)
{
    T2 = !FLAG_SET(PSR_CARRY);
}

void OPPROTO op_eval_bpos(void)
{
    T2 = !FLAG_SET(PSR_NEG);
}

void OPPROTO op_eval_bvc(void)
{
    T2 = !FLAG_SET(PSR_OVF);
}

#ifdef TARGET_SPARC64
void OPPROTO op_eval_xbe(void)
{
    T2 = XFLAG_SET(PSR_ZERO);
}

void OPPROTO op_eval_xble(void)
{
    target_ulong Z = XFLAG_SET(PSR_ZERO), N = XFLAG_SET(PSR_NEG), V = XFLAG_SET(PSR_OVF);

    T2 = Z | (N ^ V);
}

void OPPROTO op_eval_xbl(void)
{
    target_ulong N = XFLAG_SET(PSR_NEG), V = XFLAG_SET(PSR_OVF);

    T2 = N ^ V;
}

void OPPROTO op_eval_xbleu(void)
{
    target_ulong Z = XFLAG_SET(PSR_ZERO), C = XFLAG_SET(PSR_CARRY);

    T2 = C | Z;
}

void OPPROTO op_eval_xbcs(void)
{
    T2 = XFLAG_SET(PSR_CARRY);
}

void OPPROTO op_eval_xbvs(void)
{
    T2 = XFLAG_SET(PSR_OVF);
}

void OPPROTO op_eval_xbneg(void)
{
    T2 = XFLAG_SET(PSR_NEG);
}

void OPPROTO op_eval_xbne(void)
{
    T2 = !XFLAG_SET(PSR_ZERO);
}

void OPPROTO op_eval_xbg(void)
{
    target_ulong Z = XFLAG_SET(PSR_ZERO), N = XFLAG_SET(PSR_NEG), V = XFLAG_SET(PSR_OVF);

    T2 = !(Z | (N ^ V));
}

void OPPROTO op_eval_xbge(void)
{
    target_ulong N = XFLAG_SET(PSR_NEG), V = XFLAG_SET(PSR_OVF);

    T2 = !(N ^ V);
}

void OPPROTO op_eval_xbgu(void)
{
    target_ulong Z = XFLAG_SET(PSR_ZERO), C = XFLAG_SET(PSR_CARRY);

    T2 = !(C | Z);
}

void OPPROTO op_eval_xbcc(void)
{
    T2 = !XFLAG_SET(PSR_CARRY);
}

void OPPROTO op_eval_xbpos(void)
{
    T2 = !XFLAG_SET(PSR_NEG);
}

void OPPROTO op_eval_xbvc(void)
{
    T2 = !XFLAG_SET(PSR_OVF);
}
#endif

#define FCC
#define FFLAG_SET(x) (env->fsr & x? 1: 0)
#include "fbranch_template.h"

#ifdef TARGET_SPARC64
#define FCC _fcc1
#define FFLAG_SET(x) ((env->fsr & ((uint64_t)x >> 32))? 1: 0)
#include "fbranch_template.h"
#define FCC _fcc2
#define FFLAG_SET(x) ((env->fsr & ((uint64_t)x >> 34))? 1: 0)
#include "fbranch_template.h"
#define FCC _fcc3
#define FFLAG_SET(x) ((env->fsr & ((uint64_t)x >> 36))? 1: 0)
#include "fbranch_template.h"
#endif

#ifdef TARGET_SPARC64
void OPPROTO op_eval_brz(void)
{
    T2 = (T0 == 0);
}

void OPPROTO op_eval_brnz(void)
{
    T2 = (T0 != 0);
}

void OPPROTO op_eval_brlz(void)
{
    T2 = ((int64_t)T0 < 0);
}

void OPPROTO op_eval_brlez(void)
{
    T2 = ((int64_t)T0 <= 0);
}

void OPPROTO op_eval_brgz(void)
{
    T2 = ((int64_t)T0 > 0);
}

void OPPROTO op_eval_brgez(void)
{
    T2 = ((int64_t)T0 >= 0);
}

void OPPROTO op_jmp_im64(void)
{
    env->pc = PARAMQ1;
}

void OPPROTO op_movq_npc_im64(void)
{
    env->npc = PARAMQ1;
}
#endif

void OPPROTO op_jmp_im(void)
{
    env->pc = (uint32_t)PARAM1;
}

void OPPROTO op_movl_npc_im(void)
{
    env->npc = (uint32_t)PARAM1;
}

void OPPROTO op_movl_npc_T0(void)
{
    env->npc = T0;
}

void OPPROTO op_mov_pc_npc(void)
{
    env->pc = env->npc;
}

void OPPROTO op_next_insn(void)
{
    env->pc = env->npc;
    env->npc = env->npc + 4;
}

void OPPROTO op_goto_tb0(void)
{
    GOTO_TB(op_goto_tb0, PARAM1, 0);
}

void OPPROTO op_goto_tb1(void)
{
    GOTO_TB(op_goto_tb1, PARAM1, 1);
}

void OPPROTO op_jmp_label(void)
{
    GOTO_LABEL_PARAM(1);
}

void OPPROTO op_jnz_T2_label(void)
{
    if (T2)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_jz_T2_label(void)
{
    if (!T2)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_flush_T0(void)
{
    helper_flush(T0);
}

void OPPROTO op_clear_ieee_excp_and_FTT(void)
{
    env->fsr &= ~(FSR_FTT_MASK | FSR_CEXC_MASK);;
}

#define F_OP(name, p) void OPPROTO op_f##name##p(void)

#if defined(CONFIG_USER_ONLY)
#define F_BINOP(name)                                           \
    F_OP(name, s)                                               \
    {                                                           \
        set_float_exception_flags(0, &env->fp_status);          \
        FT0 = float32_ ## name (FT0, FT1, &env->fp_status);     \
        check_ieee_exceptions();                                \
    }                                                           \
    F_OP(name, d)                                               \
    {                                                           \
        set_float_exception_flags(0, &env->fp_status);          \
        DT0 = float64_ ## name (DT0, DT1, &env->fp_status);     \
        check_ieee_exceptions();                                \
    }                                                           \
    F_OP(name, q)                                               \
    {                                                           \
        set_float_exception_flags(0, &env->fp_status);          \
        QT0 = float128_ ## name (QT0, QT1, &env->fp_status);    \
        check_ieee_exceptions();                                \
    }
#else
#define F_BINOP(name)                                           \
    F_OP(name, s)                                               \
    {                                                           \
        set_float_exception_flags(0, &env->fp_status);          \
        FT0 = float32_ ## name (FT0, FT1, &env->fp_status);     \
        check_ieee_exceptions();                                \
    }                                                           \
    F_OP(name, d)                                               \
    {                                                           \
        set_float_exception_flags(0, &env->fp_status);          \
        DT0 = float64_ ## name (DT0, DT1, &env->fp_status);     \
        check_ieee_exceptions();                                \
    }
#endif

F_BINOP(add);
F_BINOP(sub);
F_BINOP(mul);
F_BINOP(div);
#undef F_BINOP

void OPPROTO op_fsmuld(void)
{
    set_float_exception_flags(0, &env->fp_status);
    DT0 = float64_mul(float32_to_float64(FT0, &env->fp_status),
                      float32_to_float64(FT1, &env->fp_status),
                      &env->fp_status);
    check_ieee_exceptions();
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fdmulq(void)
{
    set_float_exception_flags(0, &env->fp_status);
    QT0 = float128_mul(float64_to_float128(DT0, &env->fp_status),
                       float64_to_float128(DT1, &env->fp_status),
                       &env->fp_status);
    check_ieee_exceptions();
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

F_HELPER(sqrt);

F_OP(neg, s)
{
    FT0 = float32_chs(FT1);
}

F_OP(abs, s)
{
    do_fabss();
}

F_HELPER(cmp);
F_HELPER(cmpe);

#ifdef TARGET_SPARC64
F_OP(neg, d)
{
    DT0 = float64_chs(DT1);
}

F_OP(abs, d)
{
    do_fabsd();
}

#if defined(CONFIG_USER_ONLY)
F_OP(neg, q)
{
    QT0 = float128_chs(QT1);
}

F_OP(abs, q)
{
    do_fabsd();
}
#endif

void OPPROTO op_fcmps_fcc1(void)
{
    do_fcmps_fcc1();
}

void OPPROTO op_fcmpd_fcc1(void)
{
    do_fcmpd_fcc1();
}

void OPPROTO op_fcmps_fcc2(void)
{
    do_fcmps_fcc2();
}

void OPPROTO op_fcmpd_fcc2(void)
{
    do_fcmpd_fcc2();
}

void OPPROTO op_fcmps_fcc3(void)
{
    do_fcmps_fcc3();
}

void OPPROTO op_fcmpd_fcc3(void)
{
    do_fcmpd_fcc3();
}

void OPPROTO op_fcmpes_fcc1(void)
{
    do_fcmpes_fcc1();
}

void OPPROTO op_fcmped_fcc1(void)
{
    do_fcmped_fcc1();
}

void OPPROTO op_fcmpes_fcc2(void)
{
    do_fcmpes_fcc2();
}

void OPPROTO op_fcmped_fcc2(void)
{
    do_fcmped_fcc2();
}

void OPPROTO op_fcmpes_fcc3(void)
{
    do_fcmpes_fcc3();
}

void OPPROTO op_fcmped_fcc3(void)
{
    do_fcmped_fcc3();
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fcmpq_fcc1(void)
{
    do_fcmpq_fcc1();
}

void OPPROTO op_fcmpq_fcc2(void)
{
    do_fcmpq_fcc2();
}

void OPPROTO op_fcmpq_fcc3(void)
{
    do_fcmpq_fcc3();
}

void OPPROTO op_fcmpeq_fcc1(void)
{
    do_fcmpeq_fcc1();
}

void OPPROTO op_fcmpeq_fcc2(void)
{
    do_fcmpeq_fcc2();
}

void OPPROTO op_fcmpeq_fcc3(void)
{
    do_fcmpeq_fcc3();
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
    set_float_exception_flags(0, &env->fp_status);
    FT0 = int32_to_float32(*((int32_t *)&FT1), &env->fp_status);
    check_ieee_exceptions();
}

F_OP(ito, d)
{
    set_float_exception_flags(0, &env->fp_status);
    DT0 = int32_to_float64(*((int32_t *)&FT1), &env->fp_status);
    check_ieee_exceptions();
}

#if defined(CONFIG_USER_ONLY)
F_OP(ito, q)
{
    set_float_exception_flags(0, &env->fp_status);
    QT0 = int32_to_float128(*((int32_t *)&FT1), &env->fp_status);
    check_ieee_exceptions();
}
#endif

#ifdef TARGET_SPARC64
F_OP(xto, s)
{
    set_float_exception_flags(0, &env->fp_status);
    FT0 = int64_to_float32(*((int64_t *)&DT1), &env->fp_status);
    check_ieee_exceptions();
}

F_OP(xto, d)
{
    set_float_exception_flags(0, &env->fp_status);
    DT0 = int64_to_float64(*((int64_t *)&DT1), &env->fp_status);
    check_ieee_exceptions();
}
#if defined(CONFIG_USER_ONLY)
F_OP(xto, q)
{
    set_float_exception_flags(0, &env->fp_status);
    QT0 = int64_to_float128(*((int64_t *)&DT1), &env->fp_status);
    check_ieee_exceptions();
}
#endif
#endif
#endif
#undef F_HELPER

/* floating point conversion */
void OPPROTO op_fdtos(void)
{
    set_float_exception_flags(0, &env->fp_status);
    FT0 = float64_to_float32(DT1, &env->fp_status);
    check_ieee_exceptions();
}

void OPPROTO op_fstod(void)
{
    set_float_exception_flags(0, &env->fp_status);
    DT0 = float32_to_float64(FT1, &env->fp_status);
    check_ieee_exceptions();
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fqtos(void)
{
    set_float_exception_flags(0, &env->fp_status);
    FT0 = float128_to_float32(QT1, &env->fp_status);
    check_ieee_exceptions();
}

void OPPROTO op_fstoq(void)
{
    set_float_exception_flags(0, &env->fp_status);
    QT0 = float32_to_float128(FT1, &env->fp_status);
    check_ieee_exceptions();
}

void OPPROTO op_fqtod(void)
{
    set_float_exception_flags(0, &env->fp_status);
    DT0 = float128_to_float64(QT1, &env->fp_status);
    check_ieee_exceptions();
}

void OPPROTO op_fdtoq(void)
{
    set_float_exception_flags(0, &env->fp_status);
    QT0 = float64_to_float128(DT1, &env->fp_status);
    check_ieee_exceptions();
}
#endif

/* Float to integer conversion.  */
void OPPROTO op_fstoi(void)
{
    set_float_exception_flags(0, &env->fp_status);
    *((int32_t *)&FT0) = float32_to_int32_round_to_zero(FT1, &env->fp_status);
    check_ieee_exceptions();
}

void OPPROTO op_fdtoi(void)
{
    set_float_exception_flags(0, &env->fp_status);
    *((int32_t *)&FT0) = float64_to_int32_round_to_zero(DT1, &env->fp_status);
    check_ieee_exceptions();
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fqtoi(void)
{
    set_float_exception_flags(0, &env->fp_status);
    *((int32_t *)&FT0) = float128_to_int32_round_to_zero(QT1, &env->fp_status);
    check_ieee_exceptions();
}
#endif

#ifdef TARGET_SPARC64
void OPPROTO op_fstox(void)
{
    set_float_exception_flags(0, &env->fp_status);
    *((int64_t *)&DT0) = float32_to_int64_round_to_zero(FT1, &env->fp_status);
    check_ieee_exceptions();
}

void OPPROTO op_fdtox(void)
{
    set_float_exception_flags(0, &env->fp_status);
    *((int64_t *)&DT0) = float64_to_int64_round_to_zero(DT1, &env->fp_status);
    check_ieee_exceptions();
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fqtox(void)
{
    set_float_exception_flags(0, &env->fp_status);
    *((int64_t *)&DT0) = float128_to_int64_round_to_zero(QT1, &env->fp_status);
    check_ieee_exceptions();
}
#endif

void OPPROTO op_fmovs_cc(void)
{
    if (T2)
        FT0 = FT1;
}

void OPPROTO op_fmovd_cc(void)
{
    if (T2)
        DT0 = DT1;
}

#if defined(CONFIG_USER_ONLY)
void OPPROTO op_fmovq_cc(void)
{
    if (T2)
        QT0 = QT1;
}
#endif

void OPPROTO op_mov_cc(void)
{
    if (T2)
        T0 = T1;
}

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

void OPPROTO op_popc(void)
{
    do_popc();
}

void OPPROTO op_done(void)
{
    do_done();
}

void OPPROTO op_retry(void)
{
    do_retry();
}

void OPPROTO op_sir(void)
{
    T0 = 0;  // XXX
}

void OPPROTO op_ld_asi_reg()
{
    T0 += PARAM1;
    helper_ld_asi(env->asi, PARAM2, PARAM3);
}

void OPPROTO op_st_asi_reg()
{
    T0 += PARAM1;
    helper_st_asi(env->asi, PARAM2);
}

void OPPROTO op_ldf_asi_reg()
{
    T0 += PARAM1;
    helper_ldf_asi(env->asi, PARAM2, PARAM3);
}

void OPPROTO op_stf_asi_reg()
{
    T0 += PARAM1;
    helper_stf_asi(env->asi, PARAM2, PARAM3);
}

void OPPROTO op_ldf_asi()
{
    helper_ldf_asi(PARAM1, PARAM2, PARAM3);
}

void OPPROTO op_stf_asi()
{
    helper_stf_asi(PARAM1, PARAM2, PARAM3);
}

void OPPROTO op_ldstub_asi_reg()             /* XXX: should be atomically */
{
    target_ulong tmp;

    T0 += PARAM1;
    helper_ld_asi(env->asi, 1, 0);
    tmp = T1;
    T1 = 0xff;
    helper_st_asi(env->asi, 1);
    T1 = tmp;
}

void OPPROTO op_swap_asi_reg()               /* XXX: should be atomically */
{
    target_ulong tmp1, tmp2;

    T0 += PARAM1;
    tmp1 = T1;
    helper_ld_asi(env->asi, 4, 0);
    tmp2 = T1;
    T1 = tmp1;
    helper_st_asi(env->asi, 4);
    T1 = tmp2;
}

void OPPROTO op_ldda_asi()
{
    helper_ld_asi(PARAM1, 8, 0);
    T0 = T1 & 0xffffffffUL;
    T1 >>= 32;
}

void OPPROTO op_ldda_asi_reg()
{
    T0 += PARAM1;
    helper_ld_asi(env->asi, 8, 0);
    T0 = T1 & 0xffffffffUL;
    T1 >>= 32;
}

void OPPROTO op_stda_asi()
{
    T1 <<= 32;
    T1 += T2 & 0xffffffffUL;
    helper_st_asi(PARAM1, 8);
}

void OPPROTO op_stda_asi_reg()
{
    T0 += PARAM1;
    T1 <<= 32;
    T1 += T2 & 0xffffffffUL;
    helper_st_asi(env->asi, 8);
}

void OPPROTO op_cas_asi()                    /* XXX: should be atomically */
{
    target_ulong tmp;

    tmp = T1 & 0xffffffffUL;
    helper_ld_asi(PARAM1, 4, 0);
    if (tmp == T1) {
        tmp = T1;
        T1 = T2 & 0xffffffffUL;
        helper_st_asi(PARAM1, 4);
        T1 = tmp;
    }
    T1 &= 0xffffffffUL;
}

void OPPROTO op_cas_asi_reg()                /* XXX: should be atomically */
{
    target_ulong tmp;

    T0 += PARAM1;
    tmp = T1 & 0xffffffffUL;
    helper_ld_asi(env->asi, 4, 0);
    if (tmp == T1) {
        tmp = T1;
        T1 = T2 & 0xffffffffUL;
        helper_st_asi(env->asi, 4);
        T1 = tmp;
    }
    T1 &= 0xffffffffUL;
}

void OPPROTO op_casx_asi()                   /* XXX: should be atomically */
{
    target_ulong tmp;

    tmp = T1;
    helper_ld_asi(PARAM1, 8, 0);
    if (tmp == T1) {
        tmp = T1;
        T1 = T2;
        helper_st_asi(PARAM1, 8);
        T1 = tmp;
    }
}

void OPPROTO op_casx_asi_reg()               /* XXX: should be atomically */
{
    target_ulong tmp;

    T0 += PARAM1;
    tmp = T1;
    helper_ld_asi(env->asi, 8, 0);
    if (tmp == T1) {
        tmp = T1;
        T1 = T2;
        helper_st_asi(env->asi, 8);
        T1 = tmp;
    }
}
#endif

#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
void OPPROTO op_ld_asi()
{
    helper_ld_asi(PARAM1, PARAM2, PARAM3);
}

void OPPROTO op_st_asi()
{
    helper_st_asi(PARAM1, PARAM2);
}

void OPPROTO op_ldstub_asi()                 /* XXX: should be atomically */
{
    target_ulong tmp;

    helper_ld_asi(PARAM1, 1, 0);
    tmp = T1;
    T1 = 0xff;
    helper_st_asi(PARAM1, 1);
    T1 = tmp;
}

void OPPROTO op_swap_asi()                   /* XXX: should be atomically */
{
    target_ulong tmp1, tmp2;

    tmp1 = T1;
    helper_ld_asi(PARAM1, 4, 0);
    tmp2 = T1;
    T1 = tmp1;
    helper_st_asi(PARAM1, 4);
    T1 = tmp2;
}
#endif

#ifdef TARGET_SPARC64
// This function uses non-native bit order
#define GET_FIELD(X, FROM, TO)                                  \
    ((X) >> (63 - (TO)) & ((1ULL << ((TO) - (FROM) + 1)) - 1))

// This function uses the order in the manuals, i.e. bit 0 is 2^0
#define GET_FIELD_SP(X, FROM, TO)               \
    GET_FIELD(X, 63 - (TO), 63 - (FROM))

void OPPROTO op_array8()
{
    T0 = (GET_FIELD_SP(T0, 60, 63) << (17 + 2 * T1)) |
        (GET_FIELD_SP(T0, 39, 39 + T1 - 1) << (17 + T1)) |
        (GET_FIELD_SP(T0, 17 + T1 - 1, 17) << 17) |
        (GET_FIELD_SP(T0, 56, 59) << 13) | (GET_FIELD_SP(T0, 35, 38) << 9) |
        (GET_FIELD_SP(T0, 13, 16) << 5) | (((T0 >> 55) & 1) << 4) |
        (GET_FIELD_SP(T0, 33, 34) << 2) | GET_FIELD_SP(T0, 11, 12);
}

void OPPROTO op_array16()
{
    T0 = ((GET_FIELD_SP(T0, 60, 63) << (17 + 2 * T1)) |
          (GET_FIELD_SP(T0, 39, 39 + T1 - 1) << (17 + T1)) |
          (GET_FIELD_SP(T0, 17 + T1 - 1, 17) << 17) |
          (GET_FIELD_SP(T0, 56, 59) << 13) | (GET_FIELD_SP(T0, 35, 38) << 9) |
          (GET_FIELD_SP(T0, 13, 16) << 5) | (((T0 >> 55) & 1) << 4) |
          (GET_FIELD_SP(T0, 33, 34) << 2) | GET_FIELD_SP(T0, 11, 12)) << 1;
}

void OPPROTO op_array32()
{
    T0 = ((GET_FIELD_SP(T0, 60, 63) << (17 + 2 * T1)) |
          (GET_FIELD_SP(T0, 39, 39 + T1 - 1) << (17 + T1)) |
          (GET_FIELD_SP(T0, 17 + T1 - 1, 17) << 17) |
          (GET_FIELD_SP(T0, 56, 59) << 13) | (GET_FIELD_SP(T0, 35, 38) << 9) |
          (GET_FIELD_SP(T0, 13, 16) << 5) | (((T0 >> 55) & 1) << 4) |
          (GET_FIELD_SP(T0, 33, 34) << 2) | GET_FIELD_SP(T0, 11, 12)) << 2;
}

void OPPROTO op_alignaddr()
{
    uint64_t tmp;

    tmp = T0 + T1;
    env->gsr &= ~7ULL;
    env->gsr |= tmp & 7ULL;
    T0 = tmp & ~7ULL;
}

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
