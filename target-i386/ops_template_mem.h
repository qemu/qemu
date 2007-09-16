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

void OPPROTO glue(glue(op_rol, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int count;
    target_long src;

    if (T1 & SHIFT1_MASK) {
        count = T1 & SHIFT_MASK;
        src = T0;
        T0 &= DATA_MASK;
        T0 = (T0 << count) | (T0 >> (DATA_BITS - count));
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#else
        /* gcc 3.2 workaround. This is really a bug in gcc. */
        asm volatile("" : : "r" (T0));
#endif
        CC_SRC = (cc_table[CC_OP].compute_all() & ~(CC_O | CC_C)) |
            (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) |
            (T0 & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_ror, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int count;
    target_long src;

    if (T1 & SHIFT1_MASK) {
        count = T1 & SHIFT_MASK;
        src = T0;
        T0 &= DATA_MASK;
        T0 = (T0 >> count) | (T0 << (DATA_BITS - count));
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#else
        /* gcc 3.2 workaround. This is really a bug in gcc. */
        asm volatile("" : : "r" (T0));
#endif
        CC_SRC = (cc_table[CC_OP].compute_all() & ~(CC_O | CC_C)) |
            (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) |
            ((T0 >> (DATA_BITS - 1)) & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_rol, MEM_SUFFIX), _T0_T1)(void)
{
    int count;
    count = T1 & SHIFT_MASK;
    if (count) {
        T0 &= DATA_MASK;
        T0 = (T0 << count) | (T0 >> (DATA_BITS - count));
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_ror, MEM_SUFFIX), _T0_T1)(void)
{
    int count;
    count = T1 & SHIFT_MASK;
    if (count) {
        T0 &= DATA_MASK;
        T0 = (T0 >> count) | (T0 << (DATA_BITS - count));
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_rcl, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int count, eflags;
    target_ulong src;
    target_long res;

    count = T1 & SHIFT1_MASK;
#if DATA_BITS == 16
    count = rclw_table[count];
#elif DATA_BITS == 8
    count = rclb_table[count];
#endif
    if (count) {
        eflags = cc_table[CC_OP].compute_all();
        T0 &= DATA_MASK;
        src = T0;
        res = (T0 << count) | ((target_ulong)(eflags & CC_C) << (count - 1));
        if (count > 1)
            res |= T0 >> (DATA_BITS + 1 - count);
        T0 = res;
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = (eflags & ~(CC_C | CC_O)) |
            (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) |
            ((src >> (DATA_BITS - count)) & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_rcr, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int count, eflags;
    target_ulong src;
    target_long res;

    count = T1 & SHIFT1_MASK;
#if DATA_BITS == 16
    count = rclw_table[count];
#elif DATA_BITS == 8
    count = rclb_table[count];
#endif
    if (count) {
        eflags = cc_table[CC_OP].compute_all();
        T0 &= DATA_MASK;
        src = T0;
        res = (T0 >> count) | ((target_ulong)(eflags & CC_C) << (DATA_BITS - count));
        if (count > 1)
            res |= T0 << (DATA_BITS + 1 - count);
        T0 = res;
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = (eflags & ~(CC_C | CC_O)) |
            (lshift(src ^ T0, 11 - (DATA_BITS - 1)) & CC_O) |
            ((src >> (count - 1)) & CC_C);
        CC_OP = CC_OP_EFLAGS;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_shl, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int count;
    target_long src;

    count = T1 & SHIFT1_MASK;
    if (count) {
        src = (DATA_TYPE)T0 << (count - 1);
        T0 = T0 << count;
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = src;
        CC_DST = T0;
        CC_OP = CC_OP_SHLB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_shr, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int count;
    target_long src;

    count = T1 & SHIFT1_MASK;
    if (count) {
        T0 &= DATA_MASK;
        src = T0 >> (count - 1);
        T0 = T0 >> count;
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = src;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_sar, MEM_SUFFIX), _T0_T1_cc)(void)
{
    int count;
    target_long src;

    count = T1 & SHIFT1_MASK;
    if (count) {
        src = (DATA_STYPE)T0;
        T0 = src >> count;
        src = src >> (count - 1);
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = src;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
    FORCE_RET();
}

#if DATA_BITS == 16
/* XXX: overflow flag might be incorrect in some cases in shldw */
void OPPROTO glue(glue(op_shld, MEM_SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    unsigned int res, tmp;
    count = PARAM1;
    T1 &= 0xffff;
    res = T1 | (T0 << 16);
    tmp = res >> (32 - count);
    res <<= count;
    if (count > 16)
        res |= T1 << (count - 16);
    T0 = res >> 16;
#ifdef MEM_WRITE
    glue(st, MEM_SUFFIX)(A0, T0);
#endif
    CC_SRC = tmp;
    CC_DST = T0;
}

void OPPROTO glue(glue(op_shld, MEM_SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    unsigned int res, tmp;
    count = ECX & 0x1f;
    if (count) {
        T1 &= 0xffff;
        res = T1 | (T0 << 16);
        tmp = res >> (32 - count);
        res <<= count;
        if (count > 16)
          res |= T1 << (count - 16);
        T0 = res >> 16;
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = tmp;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_shrd, MEM_SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    unsigned int res, tmp;

    count = PARAM1;
    res = (T0 & 0xffff) | (T1 << 16);
    tmp = res >> (count - 1);
    res >>= count;
    if (count > 16)
        res |= T1 << (32 - count);
    T0 = res;
#ifdef MEM_WRITE
    glue(st, MEM_SUFFIX)(A0, T0);
#endif
    CC_SRC = tmp;
    CC_DST = T0;
}


void OPPROTO glue(glue(op_shrd, MEM_SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    unsigned int res, tmp;

    count = ECX & 0x1f;
    if (count) {
        res = (T0 & 0xffff) | (T1 << 16);
        tmp = res >> (count - 1);
        res >>= count;
        if (count > 16)
            res |= T1 << (32 - count);
        T0 = res;
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = tmp;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
    FORCE_RET();
}
#endif

#if DATA_BITS >= 32
void OPPROTO glue(glue(op_shld, MEM_SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    target_long tmp;

    count = PARAM1;
    T0 &= DATA_MASK;
    T1 &= DATA_MASK;
    tmp = T0 << (count - 1);
    T0 = (T0 << count) | (T1 >> (DATA_BITS - count));
#ifdef MEM_WRITE
    glue(st, MEM_SUFFIX)(A0, T0);
#endif
    CC_SRC = tmp;
    CC_DST = T0;
}

void OPPROTO glue(glue(op_shld, MEM_SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    target_long tmp;

    count = ECX & SHIFT1_MASK;
    if (count) {
        T0 &= DATA_MASK;
        T1 &= DATA_MASK;
        tmp = T0 << (count - 1);
        T0 = (T0 << count) | (T1 >> (DATA_BITS - count));
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = tmp;
        CC_DST = T0;
        CC_OP = CC_OP_SHLB + SHIFT;
    }
    FORCE_RET();
}

void OPPROTO glue(glue(op_shrd, MEM_SUFFIX), _T0_T1_im_cc)(void)
{
    int count;
    target_long tmp;

    count = PARAM1;
    T0 &= DATA_MASK;
    T1 &= DATA_MASK;
    tmp = T0 >> (count - 1);
    T0 = (T0 >> count) | (T1 << (DATA_BITS - count));
#ifdef MEM_WRITE
    glue(st, MEM_SUFFIX)(A0, T0);
#endif
    CC_SRC = tmp;
    CC_DST = T0;
}


void OPPROTO glue(glue(op_shrd, MEM_SUFFIX), _T0_T1_ECX_cc)(void)
{
    int count;
    target_long tmp;

    count = ECX & SHIFT1_MASK;
    if (count) {
        T0 &= DATA_MASK;
        T1 &= DATA_MASK;
        tmp = T0 >> (count - 1);
        T0 = (T0 >> count) | (T1 << (DATA_BITS - count));
#ifdef MEM_WRITE
        glue(st, MEM_SUFFIX)(A0, T0);
#endif
        CC_SRC = tmp;
        CC_DST = T0;
        CC_OP = CC_OP_SARB + SHIFT;
    }
    FORCE_RET();
}
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
