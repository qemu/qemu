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
#define DATA_BITS (1 << (3 + SHIFT))
#define SHIFT_MASK (DATA_BITS - 1)
#define SIGN_MASK (((target_ulong)1) << (DATA_BITS - 1))
#if DATA_BITS <= 32
#define SHIFT1_MASK 0x1f
#else
#define SHIFT1_MASK 0x3f
#endif

#if DATA_BITS == 8
#define SUFFIX b
#define DATA_TYPE uint8_t
#define DATA_STYPE int8_t
#define DATA_MASK 0xff
#elif DATA_BITS == 16
#define SUFFIX w
#define DATA_TYPE uint16_t
#define DATA_STYPE int16_t
#define DATA_MASK 0xffff
#elif DATA_BITS == 32
#define SUFFIX l
#define DATA_TYPE uint32_t
#define DATA_STYPE int32_t
#define DATA_MASK 0xffffffff
#elif DATA_BITS == 64
#define SUFFIX q
#define DATA_TYPE uint64_t
#define DATA_STYPE int64_t
#define DATA_MASK 0xffffffffffffffffULL
#else
#error unhandled operand size
#endif

/* various optimized jumps cases */

void OPPROTO glue(op_jb_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    if ((DATA_TYPE)src1 < (DATA_TYPE)src2)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_jz_sub, SUFFIX)(void)
{
    if ((DATA_TYPE)CC_DST == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_jnz_sub, SUFFIX)(void)
{
    if ((DATA_TYPE)CC_DST != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_jbe_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    if ((DATA_TYPE)src1 <= (DATA_TYPE)src2)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_js_sub, SUFFIX)(void)
{
    if (CC_DST & SIGN_MASK)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_jl_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    if ((DATA_STYPE)src1 < (DATA_STYPE)src2)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_jle_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    if ((DATA_STYPE)src1 <= (DATA_STYPE)src2)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

/* oldies */

#if DATA_BITS >= 16

void OPPROTO glue(op_loopnz, SUFFIX)(void)
{
    if ((DATA_TYPE)ECX != 0 && !(T0 & CC_Z))
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_loopz, SUFFIX)(void)
{
    if ((DATA_TYPE)ECX != 0 && (T0 & CC_Z))
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_jz_ecx, SUFFIX)(void)
{
    if ((DATA_TYPE)ECX == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO glue(op_jnz_ecx, SUFFIX)(void)
{
    if ((DATA_TYPE)ECX != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

#endif

/* various optimized set cases */

void OPPROTO glue(op_setb_T0_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    T0 = ((DATA_TYPE)src1 < (DATA_TYPE)src2);
}

void OPPROTO glue(op_setz_T0_sub, SUFFIX)(void)
{
    T0 = ((DATA_TYPE)CC_DST == 0);
}

void OPPROTO glue(op_setbe_T0_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    T0 = ((DATA_TYPE)src1 <= (DATA_TYPE)src2);
}

void OPPROTO glue(op_sets_T0_sub, SUFFIX)(void)
{
    T0 = lshift(CC_DST, -(DATA_BITS - 1)) & 1;
}

void OPPROTO glue(op_setl_T0_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    T0 = ((DATA_STYPE)src1 < (DATA_STYPE)src2);
}

void OPPROTO glue(op_setle_T0_sub, SUFFIX)(void)
{
    target_long src1, src2;
    src1 = CC_DST + CC_SRC;
    src2 = CC_SRC;

    T0 = ((DATA_STYPE)src1 <= (DATA_STYPE)src2);
}

/* string operations */

void OPPROTO glue(op_movl_T0_Dshift, SUFFIX)(void)
{
    T0 = DF << SHIFT;
}

#undef DATA_BITS
#undef SHIFT_MASK
#undef SHIFT1_MASK
#undef SIGN_MASK
#undef DATA_TYPE
#undef DATA_STYPE
#undef DATA_MASK
#undef SUFFIX
