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
    T0 /= T1;
    FORCE_RET();
}

void OPPROTO op_sdivx_T1_T0(void)
{
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
    T0 <<= T1;
}

#ifdef TARGET_SPARC64
void OPPROTO op_srl(void)
{
    T0 = (T0 & 0xffffffff) >> T1;
}

void OPPROTO op_srlx(void)
{
    T0 >>= T1;
}

void OPPROTO op_sra(void)
{
    T0 = ((int32_t) (T0 & 0xffffffff)) >> T1;
}

void OPPROTO op_srax(void)
{
    T0 = ((int64_t) T0) >> T1;
}
#else
void OPPROTO op_srl(void)
{
    T0 >>= T1;
}

void OPPROTO op_sra(void)
{
    T0 = ((int32_t) T0) >> T1;
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
    T0 = 0; // XXX read cycle counter and bit 31
}

void OPPROTO op_wrtick(void)
{
    // XXX write cycle counter and bit 31
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
    T0 = NWINDOWS - 1 - env->cwp;
}

void OPPROTO op_wrcwp(void)
{
    env->cwp = NWINDOWS - 1 - T0;
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
}

void OPPROTO op_trap_T0(void)
{
    env->exception_index = TT_TRAP + (T0 & 0x7f);
    cpu_loop_exit();
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

#define F_OP(name, p) void OPPROTO op_f##name##p(void)

#define F_BINOP(name)                                           \
    F_OP(name, s)                                               \
    {                                                           \
        FT0 = float32_ ## name (FT0, FT1, &env->fp_status);     \
    }                                                           \
    F_OP(name, d)                                               \
    {                                                           \
        DT0 = float64_ ## name (DT0, DT1, &env->fp_status);     \
    }

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

#define F_HELPER(name)    \
    F_OP(name, s)         \
    {                     \
        do_f##name##s();  \
    }                     \
    F_OP(name, d)         \
    {                     \
        do_f##name##d();  \
    }

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

#ifdef TARGET_SPARC64
F_OP(neg, d)
{
    DT0 = float64_chs(DT1);
}

F_OP(abs, d)
{
    do_fabsd();
}

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
#endif

/* Integer to float conversion.  */
#ifdef USE_INT_TO_FLOAT_HELPERS
F_HELPER(ito);
#else
F_OP(ito, s)
{
    FT0 = int32_to_float32(*((int32_t *)&FT1), &env->fp_status);
}

F_OP(ito, d)
{
    DT0 = int32_to_float64(*((int32_t *)&FT1), &env->fp_status);
}

#ifdef TARGET_SPARC64
F_OP(xto, s)
{
    FT0 = int64_to_float32(*((int64_t *)&DT1), &env->fp_status);
}

F_OP(xto, d)
{
    DT0 = int64_to_float64(*((int64_t *)&DT1), &env->fp_status);
}
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

/* Float to integer conversion.  */
void OPPROTO op_fstoi(void)
{
    *((int32_t *)&FT0) = float32_to_int32(FT1, &env->fp_status);
}

void OPPROTO op_fdtoi(void)
{
    *((int32_t *)&FT0) = float64_to_int32(DT1, &env->fp_status);
}

#ifdef TARGET_SPARC64
void OPPROTO op_fstox(void)
{
    *((int64_t *)&DT0) = float32_to_int64(FT1, &env->fp_status);
}

void OPPROTO op_fdtox(void)
{
    *((int64_t *)&DT0) = float64_to_int64(DT1, &env->fp_status);
}

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
    // XXX

}

void OPPROTO op_ld_asi_reg()
{
    T0 += PARAM1;
    helper_ld_asi(env->asi, PARAM2, PARAM3);
}

void OPPROTO op_st_asi_reg()
{
    T0 += PARAM1;
    helper_st_asi(env->asi, PARAM2, PARAM3);
}
#endif

void OPPROTO op_ld_asi()
{
    helper_ld_asi(PARAM1, PARAM2, PARAM3);
}

void OPPROTO op_st_asi()
{
    helper_st_asi(PARAM1, PARAM2, PARAM3);
}

#ifdef TARGET_SPARC64
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
    (*((uint64_t *)&DT0)) = tmp;
}
#endif
