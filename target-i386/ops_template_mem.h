/*
 *  i386 micro operations (included several times to generate
 *  different operand sizes)
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#ifdef MEM_WRITE

#if MEM_WRITE == 0

#if DATA_BITS == 8
#define MEM_SUFFIX b_raw
#elif DATA_BITS == 16
#define MEM_SUFFIX w_raw
#elif DATA_BITS == 32
#define MEM_SUFFIX l_raw
#elif DATA_BITS == 64
#define MEM_SUFFIX q_raw
#endif

#elif MEM_WRITE == 1

#if DATA_BITS == 8
#define MEM_SUFFIX b_kernel
#elif DATA_BITS == 16
#define MEM_SUFFIX w_kernel
#elif DATA_BITS == 32
#define MEM_SUFFIX l_kernel
#elif DATA_BITS == 64
#define MEM_SUFFIX q_kernel
#endif

#elif MEM_WRITE == 2

#if DATA_BITS == 8
#define MEM_SUFFIX b_user
#elif DATA_BITS == 16
#define MEM_SUFFIX w_user
#elif DATA_BITS == 32
#define MEM_SUFFIX l_user
#elif DATA_BITS == 64
#define MEM_SUFFIX q_user
#endif

#else

#error invalid MEM_WRITE

#endif

#else

#define MEM_SUFFIX SUFFIX

#endif

/* carry add/sub (we only need to set CC_OP differently) */

void OPPROTO glue(glue(op_adc, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int cf;
    cf = cc_table[CC_OP].compute_c();
    T0 = T0 + T1 + cf;
#ifdef MEM_WRITE
    glue(st, MEM_SUFFIX)(A0, T0);
#endif
    CC_SRC = T1;
    CC_DST = T0;
    CC_OP = CC_OP_ADDB + SHIFT + cf * 4;
}

void OPPROTO glue(glue(op_sbb, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int cf;
    cf = cc_table[CC_OP].compute_c();
    T0 = T0 - T1 - cf;
#ifdef MEM_WRITE
    glue(st, MEM_SUFFIX)(A0, T0);
#endif
    CC_SRC = T1;
    CC_DST = T0;
    CC_OP = CC_OP_SUBB + SHIFT + cf * 4;
}

void OPPROTO glue(glue(op_cmpxchg, MEM_SUFFIX), _T0_T1_EAX_cc)(void)
{
    target_ulong src, dst;

    src = T0;
    dst = EAX - T0;
    if ((DATA_TYPE)dst == 0) {
        T0 = T1;
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
    } else {
        EAX = (EAX & ~DATA_MASK) | (T0 & DATA_MASK);
    }
    CC_SRC = src;
    CC_DST = dst;
    FORCE_RET();
}

#undef MEM_SUFFIX
#undef MEM_WRITE
