/*
 * iwMMXt micro operations for XScale.
 *
 * Copyright (c) 2007 OpenedHand, Ltd.
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
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

#define M1	env->iwmmxt.regs[PARAM1]

/* iwMMXt macros extracted from GNU gdb.  */

/* Set the SIMD wCASF flags for 8, 16, 32 or 64-bit operations.  */
#define SIMD8_SET( v, n, b)	((v != 0) << ((((b) + 1) * 4) + (n)))
#define SIMD16_SET(v, n, h)	((v != 0) << ((((h) + 1) * 8) + (n)))
#define SIMD32_SET(v, n, w)	((v != 0) << ((((w) + 1) * 16) + (n)))
#define SIMD64_SET(v, n)	((v != 0) << (32 + (n)))
/* Flags to pass as "n" above.  */
#define SIMD_NBIT	-1
#define SIMD_ZBIT	-2
#define SIMD_CBIT	-3
#define SIMD_VBIT	-4
/* Various status bit macros.  */
#define NBIT8(x)	((x) & 0x80)
#define NBIT16(x)	((x) & 0x8000)
#define NBIT32(x)	((x) & 0x80000000)
#define NBIT64(x)	((x) & 0x8000000000000000ULL)
#define ZBIT8(x)	(((x) & 0xff) == 0)
#define ZBIT16(x)	(((x) & 0xffff) == 0)
#define ZBIT32(x)	(((x) & 0xffffffff) == 0)
#define ZBIT64(x)	(x == 0)
/* Sign extension macros.  */
#define EXTEND8H(a)	((uint16_t) (int8_t) (a))
#define EXTEND8(a)	((uint32_t) (int8_t) (a))
#define EXTEND16(a)	((uint32_t) (int16_t) (a))
#define EXTEND16S(a)	((int32_t) (int16_t) (a))
#define EXTEND32(a)	((uint64_t) (int32_t) (a))

void OPPROTO op_iwmmxt_movl_T0_T1_wRn(void)
{
    T0 = M1 & ~(uint32_t) 0;
    T1 = M1 >> 32;
}

void OPPROTO op_iwmmxt_movl_wRn_T0_T1(void)
{
    M1 = ((uint64_t) T1 << 32) | T0;
}

void OPPROTO op_iwmmxt_movq_M0_wRn(void)
{
    M0 = M1;
}

void OPPROTO op_iwmmxt_orq_M0_wRn(void)
{
    M0 |= M1;
}

void OPPROTO op_iwmmxt_andq_M0_wRn(void)
{
    M0 &= M1;
}

void OPPROTO op_iwmmxt_xorq_M0_wRn(void)
{
    M0 ^= M1;
}

void OPPROTO op_iwmmxt_maddsq_M0_wRn(void)
{
    M0 = ((
            EXTEND16S((M0 >> 0) & 0xffff) * EXTEND16S((M1 >> 0) & 0xffff) +
            EXTEND16S((M0 >> 16) & 0xffff) * EXTEND16S((M1 >> 16) & 0xffff)
        ) & 0xffffffff) | ((uint64_t) (
            EXTEND16S((M0 >> 32) & 0xffff) * EXTEND16S((M1 >> 32) & 0xffff) +
            EXTEND16S((M0 >> 48) & 0xffff) * EXTEND16S((M1 >> 48) & 0xffff)
        ) << 32);
}

void OPPROTO op_iwmmxt_madduq_M0_wRn(void)
{
    M0 = ((
            ((M0 >> 0) & 0xffff) * ((M1 >> 0) & 0xffff) +
            ((M0 >> 16) & 0xffff) * ((M1 >> 16) & 0xffff)
        ) & 0xffffffff) | ((
            ((M0 >> 32) & 0xffff) * ((M1 >> 32) & 0xffff) +
            ((M0 >> 48) & 0xffff) * ((M1 >> 48) & 0xffff)
        ) << 32);
}

void OPPROTO op_iwmmxt_sadb_M0_wRn(void)
{
#define abs(x) (((x) >= 0) ? x : -x)
#define SADB(SHR) abs((int) ((M0 >> SHR) & 0xff) - (int) ((M1 >> SHR) & 0xff))
    M0 =
        SADB(0) + SADB(8) + SADB(16) + SADB(24) +
        SADB(32) + SADB(40) + SADB(48) + SADB(56);
#undef SADB
}

void OPPROTO op_iwmmxt_sadw_M0_wRn(void)
{
#define SADW(SHR) \
    abs((int) ((M0 >> SHR) & 0xffff) - (int) ((M1 >> SHR) & 0xffff))
    M0 = SADW(0) + SADW(16) + SADW(32) + SADW(48);
#undef SADW
}

void OPPROTO op_iwmmxt_addl_M0_wRn(void)
{
    M0 += env->iwmmxt.regs[PARAM1] & 0xffffffff;
}

void OPPROTO op_iwmmxt_mulsw_M0_wRn(void)
{
#define MULS(SHR) ((uint64_t) ((( \
        EXTEND16S((M0 >> SHR) & 0xffff) * EXTEND16S((M1 >> SHR) & 0xffff) \
    ) >> PARAM2) & 0xffff) << SHR)
    M0 = MULS(0) | MULS(16) | MULS(32) | MULS(48);
#undef MULS
}

void OPPROTO op_iwmmxt_muluw_M0_wRn(void)
{
#define MULU(SHR) ((uint64_t) ((( \
        ((M0 >> SHR) & 0xffff) * ((M1 >> SHR) & 0xffff) \
    ) >> PARAM2) & 0xffff) << SHR)
    M0 = MULU(0) | MULU(16) | MULU(32) | MULU(48);
#undef MULU
}

void OPPROTO op_iwmmxt_macsw_M0_wRn(void)
{
#define MACS(SHR) ( \
        EXTEND16((M0 >> SHR) & 0xffff) * EXTEND16S((M1 >> SHR) & 0xffff))
    M0 = (int64_t) (MACS(0) + MACS(16) + MACS(32) + MACS(48));
#undef MACS
}

void OPPROTO op_iwmmxt_macuw_M0_wRn(void)
{
#define MACU(SHR) ( \
        (uint32_t) ((M0 >> SHR) & 0xffff) * \
        (uint32_t) ((M1 >> SHR) & 0xffff))
    M0 = MACU(0) + MACU(16) + MACU(32) + MACU(48);
#undef MACU
}

void OPPROTO op_iwmmxt_addsq_M0_wRn(void)
{
    M0 = (int64_t) M0 + (int64_t) M1;
}

void OPPROTO op_iwmmxt_adduq_M0_wRn(void)
{
    M0 += M1;
}

void OPPROTO op_iwmmxt_movq_wRn_M0(void)
{
    M1 = M0;
}

void OPPROTO op_iwmmxt_movl_wCx_T0(void)
{
    env->iwmmxt.cregs[PARAM1] = T0;
}

void OPPROTO op_iwmmxt_movl_T0_wCx(void)
{
    T0 = env->iwmmxt.cregs[PARAM1];
}

void OPPROTO op_iwmmxt_movl_T1_wCx(void)
{
    T1 = env->iwmmxt.cregs[PARAM1];
}

void OPPROTO op_iwmmxt_set_mup(void)
{
    env->iwmmxt.cregs[ARM_IWMMXT_wCon] |= 2;
}

void OPPROTO op_iwmmxt_set_cup(void)
{
    env->iwmmxt.cregs[ARM_IWMMXT_wCon] |= 1;
}

void OPPROTO op_iwmmxt_setpsr_nz(void)
{
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        SIMD64_SET((M0 == 0), SIMD_ZBIT) |
        SIMD64_SET((M0 & (1ULL << 63)), SIMD_NBIT);
}

void OPPROTO op_iwmmxt_negq_M0(void)
{
    M0 = ~M0;
}

#define NZBIT8(x, i) \
    SIMD8_SET(NBIT8((x) & 0xff), SIMD_NBIT, i) | \
    SIMD8_SET(ZBIT8((x) & 0xff), SIMD_ZBIT, i)
#define NZBIT16(x, i) \
    SIMD16_SET(NBIT16((x) & 0xffff), SIMD_NBIT, i) | \
    SIMD16_SET(ZBIT16((x) & 0xffff), SIMD_ZBIT, i)
#define NZBIT32(x, i) \
    SIMD32_SET(NBIT32((x) & 0xffffffff), SIMD_NBIT, i) | \
    SIMD32_SET(ZBIT32((x) & 0xffffffff), SIMD_ZBIT, i)
#define NZBIT64(x) \
    SIMD64_SET(NBIT64(x), SIMD_NBIT) | \
    SIMD64_SET(ZBIT64(x), SIMD_ZBIT)
#define IWMMXT_OP_UNPACK(S, SH0, SH1, SH2, SH3)			\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, b_M0_wRn))(void)	\
{								\
    M0 =							\
        (((M0 >> SH0) & 0xff) << 0) | (((M1 >> SH0) & 0xff) << 8) |	\
        (((M0 >> SH1) & 0xff) << 16) | (((M1 >> SH1) & 0xff) << 24) |	\
        (((M0 >> SH2) & 0xff) << 32) | (((M1 >> SH2) & 0xff) << 40) |	\
        (((M0 >> SH3) & 0xff) << 48) | (((M1 >> SH3) & 0xff) << 56);	\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT8(M0 >> 0, 0) | NZBIT8(M0 >> 8, 1) |		\
        NZBIT8(M0 >> 16, 2) | NZBIT8(M0 >> 24, 3) |		\
        NZBIT8(M0 >> 32, 4) | NZBIT8(M0 >> 40, 5) |		\
        NZBIT8(M0 >> 48, 6) | NZBIT8(M0 >> 56, 7);		\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, w_M0_wRn))(void)	\
{								\
    M0 =							\
        (((M0 >> SH0) & 0xffff) << 0) |				\
        (((M1 >> SH0) & 0xffff) << 16) |			\
        (((M0 >> SH2) & 0xffff) << 32) |			\
        (((M1 >> SH2) & 0xffff) << 48);				\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT8(M0 >> 0, 0) | NZBIT8(M0 >> 16, 1) |		\
        NZBIT8(M0 >> 32, 2) | NZBIT8(M0 >> 48, 3);		\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, l_M0_wRn))(void)	\
{								\
    M0 =							\
        (((M0 >> SH0) & 0xffffffff) << 0) |			\
        (((M1 >> SH0) & 0xffffffff) << 32);			\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);		\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, ub_M0))(void)	\
{								\
    M0 =							\
        (((M0 >> SH0) & 0xff) << 0) |				\
        (((M0 >> SH1) & 0xff) << 16) |				\
        (((M0 >> SH2) & 0xff) << 32) |				\
        (((M0 >> SH3) & 0xff) << 48);				\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |		\
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);		\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, uw_M0))(void)	\
{								\
    M0 =							\
        (((M0 >> SH0) & 0xffff) << 0) |				\
        (((M0 >> SH2) & 0xffff) << 32);				\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);		\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, ul_M0))(void)	\
{								\
    M0 = (((M0 >> SH0) & 0xffffffff) << 0);			\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] = NZBIT64(M0 >> 0);	\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, sb_M0))(void)	\
{								\
    M0 =							\
        ((uint64_t) EXTEND8H((M0 >> SH0) & 0xff) << 0) |	\
        ((uint64_t) EXTEND8H((M0 >> SH1) & 0xff) << 16) |	\
        ((uint64_t) EXTEND8H((M0 >> SH2) & 0xff) << 32) |	\
        ((uint64_t) EXTEND8H((M0 >> SH3) & 0xff) << 48);	\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |		\
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);		\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, sw_M0))(void)	\
{								\
    M0 =							\
        ((uint64_t) EXTEND16((M0 >> SH0) & 0xffff) << 0) |	\
        ((uint64_t) EXTEND16((M0 >> SH2) & 0xffff) << 32);	\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);		\
}								\
void OPPROTO glue(op_iwmmxt_unpack, glue(S, sl_M0))(void)	\
{								\
    M0 = EXTEND32((M0 >> SH0) & 0xffffffff);			\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] = NZBIT64(M0 >> 0);	\
}
IWMMXT_OP_UNPACK(l, 0, 8, 16, 24)
IWMMXT_OP_UNPACK(h, 32, 40, 48, 56)

#define IWMMXT_OP_CMP(SUFF, Tb, Tw, Tl, O)				\
void OPPROTO glue(op_iwmmxt_, glue(SUFF, b_M0_wRn))(void)	\
{								\
    M0 =							\
        CMP(0, Tb, O, 0xff) | CMP(8, Tb, O, 0xff) |		\
        CMP(16, Tb, O, 0xff) | CMP(24, Tb, O, 0xff) |		\
        CMP(32, Tb, O, 0xff) | CMP(40, Tb, O, 0xff) |		\
        CMP(48, Tb, O, 0xff) | CMP(56, Tb, O, 0xff);		\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT8(M0 >> 0, 0) | NZBIT8(M0 >> 8, 1) |		\
        NZBIT8(M0 >> 16, 2) | NZBIT8(M0 >> 24, 3) |		\
        NZBIT8(M0 >> 32, 4) | NZBIT8(M0 >> 40, 5) |		\
        NZBIT8(M0 >> 48, 6) | NZBIT8(M0 >> 56, 7);		\
}								\
void OPPROTO glue(op_iwmmxt_, glue(SUFF, w_M0_wRn))(void)	\
{								\
    M0 = CMP(0, Tw, O, 0xffff) | CMP(16, Tw, O, 0xffff) |	\
        CMP(32, Tw, O, 0xffff) | CMP(48, Tw, O, 0xffff);	\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |		\
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);		\
}								\
void OPPROTO glue(op_iwmmxt_, glue(SUFF, l_M0_wRn))(void)	\
{								\
    M0 = CMP(0, Tl, O, 0xffffffff) |				\
        CMP(32, Tl, O, 0xffffffff);				\
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =			\
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);		\
}
#define CMP(SHR, TYPE, OPER, MASK) ((((TYPE) ((M0 >> SHR) & MASK) OPER \
            (TYPE) ((M1 >> SHR) & MASK)) ? (uint64_t) MASK : 0) << SHR)
IWMMXT_OP_CMP(cmpeq, uint8_t, uint16_t, uint32_t, ==)
IWMMXT_OP_CMP(cmpgts, int8_t, int16_t, int32_t, >)
IWMMXT_OP_CMP(cmpgtu, uint8_t, uint16_t, uint32_t, >)
#undef CMP
#define CMP(SHR, TYPE, OPER, MASK) ((((TYPE) ((M0 >> SHR) & MASK) OPER \
            (TYPE) ((M1 >> SHR) & MASK)) ? M0 : M1) & ((uint64_t) MASK << SHR))
IWMMXT_OP_CMP(mins, int8_t, int16_t, int32_t, <)
IWMMXT_OP_CMP(minu, uint8_t, uint16_t, uint32_t, <)
IWMMXT_OP_CMP(maxs, int8_t, int16_t, int32_t, >)
IWMMXT_OP_CMP(maxu, uint8_t, uint16_t, uint32_t, >)
#undef CMP
#define CMP(SHR, TYPE, OPER, MASK) ((uint64_t) (((TYPE) ((M0 >> SHR) & MASK) \
            OPER (TYPE) ((M1 >> SHR) & MASK)) & MASK) << SHR)
IWMMXT_OP_CMP(subn, uint8_t, uint16_t, uint32_t, -)
IWMMXT_OP_CMP(addn, uint8_t, uint16_t, uint32_t, +)
#undef CMP
/* TODO Signed- and Unsigned-Saturation */
#define CMP(SHR, TYPE, OPER, MASK) ((uint64_t) (((TYPE) ((M0 >> SHR) & MASK) \
            OPER (TYPE) ((M1 >> SHR) & MASK)) & MASK) << SHR)
IWMMXT_OP_CMP(subu, uint8_t, uint16_t, uint32_t, -)
IWMMXT_OP_CMP(addu, uint8_t, uint16_t, uint32_t, +)
IWMMXT_OP_CMP(subs, int8_t, int16_t, int32_t, -)
IWMMXT_OP_CMP(adds, int8_t, int16_t, int32_t, +)
#undef CMP
#undef IWMMXT_OP_CMP

void OPPROTO op_iwmmxt_avgb_M0_wRn(void)
{
#define AVGB(SHR) ((( \
        ((M0 >> SHR) & 0xff) + ((M1 >> SHR) & 0xff) + PARAM2) >> 1) << SHR)
    M0 =
        AVGB(0) | AVGB(8) | AVGB(16) | AVGB(24) |
        AVGB(32) | AVGB(40) | AVGB(48) | AVGB(56);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        SIMD8_SET(ZBIT8((M0 >> 0) & 0xff), SIMD_ZBIT, 0) |
        SIMD8_SET(ZBIT8((M0 >> 8) & 0xff), SIMD_ZBIT, 1) |
        SIMD8_SET(ZBIT8((M0 >> 16) & 0xff), SIMD_ZBIT, 2) |
        SIMD8_SET(ZBIT8((M0 >> 24) & 0xff), SIMD_ZBIT, 3) |
        SIMD8_SET(ZBIT8((M0 >> 32) & 0xff), SIMD_ZBIT, 4) |
        SIMD8_SET(ZBIT8((M0 >> 40) & 0xff), SIMD_ZBIT, 5) |
        SIMD8_SET(ZBIT8((M0 >> 48) & 0xff), SIMD_ZBIT, 6) |
        SIMD8_SET(ZBIT8((M0 >> 56) & 0xff), SIMD_ZBIT, 7);
#undef AVGB
}

void OPPROTO op_iwmmxt_avgw_M0_wRn(void)
{
#define AVGW(SHR) ((( \
        ((M0 >> SHR) & 0xffff) + ((M1 >> SHR) & 0xffff) + PARAM2) >> 1) << SHR)
    M0 = AVGW(0) | AVGW(16) | AVGW(32) | AVGW(48);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        SIMD16_SET(ZBIT16((M0 >> 0) & 0xffff), SIMD_ZBIT, 0) |
        SIMD16_SET(ZBIT16((M0 >> 16) & 0xffff), SIMD_ZBIT, 1) |
        SIMD16_SET(ZBIT16((M0 >> 32) & 0xffff), SIMD_ZBIT, 2) |
        SIMD16_SET(ZBIT16((M0 >> 48) & 0xffff), SIMD_ZBIT, 3);
#undef AVGW
}

void OPPROTO op_iwmmxt_msadb_M0_wRn(void)
{
    M0 = ((((M0 >> 0) & 0xffff) * ((M1 >> 0) & 0xffff) +
           ((M0 >> 16) & 0xffff) * ((M1 >> 16) & 0xffff)) & 0xffffffff) |
         ((((M0 >> 32) & 0xffff) * ((M1 >> 32) & 0xffff) +
           ((M0 >> 48) & 0xffff) * ((M1 >> 48) & 0xffff)) << 32);
}

void OPPROTO op_iwmmxt_align_M0_T0_wRn(void)
{
    M0 >>= T0 << 3;
    M0 |= M1 << (64 - (T0 << 3));
}

void OPPROTO op_iwmmxt_insr_M0_T0_T1(void)
{
    M0 &= ~((uint64_t) T1 << PARAM1);
    M0 |= (uint64_t) (T0 & T1) << PARAM1;
}

void OPPROTO op_iwmmxt_extrsb_T0_M0(void)
{
    T0 = EXTEND8((M0 >> PARAM1) & 0xff);
}

void OPPROTO op_iwmmxt_extrsw_T0_M0(void)
{
    T0 = EXTEND16((M0 >> PARAM1) & 0xffff);
}

void OPPROTO op_iwmmxt_extru_T0_M0_T1(void)
{
    T0 = (M0 >> PARAM1) & T1;
}

void OPPROTO op_iwmmxt_bcstb_M0_T0(void)
{
    T0 &= 0xff;
    M0 =
        ((uint64_t) T0 << 0) | ((uint64_t) T0 << 8) |
        ((uint64_t) T0 << 16) | ((uint64_t) T0 << 24) |
        ((uint64_t) T0 << 32) | ((uint64_t) T0 << 40) |
        ((uint64_t) T0 << 48) | ((uint64_t) T0 << 56);
}

void OPPROTO op_iwmmxt_bcstw_M0_T0(void)
{
    T0 &= 0xffff;
    M0 =
        ((uint64_t) T0 << 0) | ((uint64_t) T0 << 16) |
        ((uint64_t) T0 << 32) | ((uint64_t) T0 << 48);
}

void OPPROTO op_iwmmxt_bcstl_M0_T0(void)
{
    M0 = ((uint64_t) T0 << 0) | ((uint64_t) T0 << 32);
}

void OPPROTO op_iwmmxt_addcb_M0(void)
{
    M0 =
        ((M0 >> 0) & 0xff) + ((M0 >> 8) & 0xff) +
        ((M0 >> 16) & 0xff) + ((M0 >> 24) & 0xff) +
        ((M0 >> 32) & 0xff) + ((M0 >> 40) & 0xff) +
        ((M0 >> 48) & 0xff) + ((M0 >> 56) & 0xff);
}

void OPPROTO op_iwmmxt_addcw_M0(void)
{
    M0 =
        ((M0 >> 0) & 0xffff) + ((M0 >> 16) & 0xffff) +
        ((M0 >> 32) & 0xffff) + ((M0 >> 48) & 0xffff);
}

void OPPROTO op_iwmmxt_addcl_M0(void)
{
    M0 = (M0 & 0xffffffff) + (M0 >> 32);
}

void OPPROTO op_iwmmxt_msbb_T0_M0(void)
{
    T0 =
        ((M0 >> 7) & 0x01) | ((M0 >> 14) & 0x02) |
        ((M0 >> 21) & 0x04) | ((M0 >> 28) & 0x08) |
        ((M0 >> 35) & 0x10) | ((M0 >> 42) & 0x20) |
        ((M0 >> 49) & 0x40) | ((M0 >> 56) & 0x80);
}

void OPPROTO op_iwmmxt_msbw_T0_M0(void)
{
    T0 =
        ((M0 >> 15) & 0x01) | ((M0 >> 30) & 0x02) |
        ((M0 >> 45) & 0x04) | ((M0 >> 52) & 0x08);
}

void OPPROTO op_iwmmxt_msbl_T0_M0(void)
{
    T0 = ((M0 >> 31) & 0x01) | ((M0 >> 62) & 0x02);
}

void OPPROTO op_iwmmxt_srlw_M0_T0(void)
{
    M0 =
        (((M0 & (0xffffll << 0)) >> T0) & (0xffffll << 0)) |
        (((M0 & (0xffffll << 16)) >> T0) & (0xffffll << 16)) |
        (((M0 & (0xffffll << 32)) >> T0) & (0xffffll << 32)) |
        (((M0 & (0xffffll << 48)) >> T0) & (0xffffll << 48));
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);
}

void OPPROTO op_iwmmxt_srll_M0_T0(void)
{
    M0 =
        ((M0 & (0xffffffffll << 0)) >> T0) |
        ((M0 >> T0) & (0xffffffffll << 32));
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);
}

void OPPROTO op_iwmmxt_srlq_M0_T0(void)
{
    M0 >>= T0;
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] = NZBIT64(M0);
}

void OPPROTO op_iwmmxt_sllw_M0_T0(void)
{
    M0 =
        (((M0 & (0xffffll << 0)) << T0) & (0xffffll << 0)) |
        (((M0 & (0xffffll << 16)) << T0) & (0xffffll << 16)) |
        (((M0 & (0xffffll << 32)) << T0) & (0xffffll << 32)) |
        (((M0 & (0xffffll << 48)) << T0) & (0xffffll << 48));
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);
}

void OPPROTO op_iwmmxt_slll_M0_T0(void)
{
    M0 =
        ((M0 << T0) & (0xffffffffll << 0)) |
        ((M0 & (0xffffffffll << 32)) << T0);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);
}

void OPPROTO op_iwmmxt_sllq_M0_T0(void)
{
    M0 <<= T0;
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] = NZBIT64(M0);
}

void OPPROTO op_iwmmxt_sraw_M0_T0(void)
{
    M0 =
        ((uint64_t) ((EXTEND16(M0 >> 0) >> T0) & 0xffff) << 0) |
        ((uint64_t) ((EXTEND16(M0 >> 16) >> T0) & 0xffff) << 16) |
        ((uint64_t) ((EXTEND16(M0 >> 32) >> T0) & 0xffff) << 32) |
        ((uint64_t) ((EXTEND16(M0 >> 48) >> T0) & 0xffff) << 48);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);
}

void OPPROTO op_iwmmxt_sral_M0_T0(void)
{
    M0 =
        (((EXTEND32(M0 >> 0) >> T0) & 0xffffffff) << 0) |
        (((EXTEND32(M0 >> 32) >> T0) & 0xffffffff) << 32);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);
}

void OPPROTO op_iwmmxt_sraq_M0_T0(void)
{
    M0 = (int64_t) M0 >> T0;
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] = NZBIT64(M0);
}

void OPPROTO op_iwmmxt_rorw_M0_T0(void)
{
    M0 =
        ((((M0 & (0xffffll << 0)) >> T0) |
          ((M0 & (0xffffll << 0)) << (16 - T0))) & (0xffffll << 0)) |
        ((((M0 & (0xffffll << 16)) >> T0) |
          ((M0 & (0xffffll << 16)) << (16 - T0))) & (0xffffll << 16)) |
        ((((M0 & (0xffffll << 32)) >> T0) |
          ((M0 & (0xffffll << 32)) << (16 - T0))) & (0xffffll << 32)) |
        ((((M0 & (0xffffll << 48)) >> T0) |
          ((M0 & (0xffffll << 48)) << (16 - T0))) & (0xffffll << 48));
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);
}

void OPPROTO op_iwmmxt_rorl_M0_T0(void)
{
    M0 =
        ((M0 & (0xffffffffll << 0)) >> T0) |
        ((M0 >> T0) & (0xffffffffll << 32)) |
        ((M0 << (32 - T0)) & (0xffffffffll << 0)) |
        ((M0 & (0xffffffffll << 32)) << (32 - T0));
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);
}

void OPPROTO op_iwmmxt_rorq_M0_T0(void)
{
    M0 = (M0 >> T0) | (M0 << (64 - T0));
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] = NZBIT64(M0);
}

void OPPROTO op_iwmmxt_shufh_M0_T0(void)
{
    M0 =
        (((M0 >> ((T0 << 4) & 0x30)) & 0xffff) << 0) |
        (((M0 >> ((T0 << 2) & 0x30)) & 0xffff) << 16) |
        (((M0 >> ((T0 << 0) & 0x30)) & 0xffff) << 32) |
        (((M0 >> ((T0 >> 2) & 0x30)) & 0xffff) << 48);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);
}

/* TODO: Unsigned-Saturation */
void OPPROTO op_iwmmxt_packuw_M0_wRn(void)
{
    M0 =
        (((M0 >> 0) & 0xff) << 0) | (((M0 >> 16) & 0xff) << 8) |
        (((M0 >> 32) & 0xff) << 16) | (((M0 >> 48) & 0xff) << 24) |
        (((M1 >> 0) & 0xff) << 32) | (((M1 >> 16) & 0xff) << 40) |
        (((M1 >> 32) & 0xff) << 48) | (((M1 >> 48) & 0xff) << 56);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT8(M0 >> 0, 0) | NZBIT8(M0 >> 8, 1) |
        NZBIT8(M0 >> 16, 2) | NZBIT8(M0 >> 24, 3) |
        NZBIT8(M0 >> 32, 4) | NZBIT8(M0 >> 40, 5) |
        NZBIT8(M0 >> 48, 6) | NZBIT8(M0 >> 56, 7);
}

void OPPROTO op_iwmmxt_packul_M0_wRn(void)
{
    M0 =
        (((M0 >> 0) & 0xffff) << 0) | (((M0 >> 32) & 0xffff) << 16) |
        (((M1 >> 0) & 0xffff) << 32) | (((M1 >> 32) & 0xffff) << 48);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);
}

void OPPROTO op_iwmmxt_packuq_M0_wRn(void)
{
    M0 = (M0 & 0xffffffff) | ((M1 & 0xffffffff) << 32);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);
}

/* TODO: Signed-Saturation */
void OPPROTO op_iwmmxt_packsw_M0_wRn(void)
{
    M0 =
        (((M0 >> 0) & 0xff) << 0) | (((M0 >> 16) & 0xff) << 8) |
        (((M0 >> 32) & 0xff) << 16) | (((M0 >> 48) & 0xff) << 24) |
        (((M1 >> 0) & 0xff) << 32) | (((M1 >> 16) & 0xff) << 40) |
        (((M1 >> 32) & 0xff) << 48) | (((M1 >> 48) & 0xff) << 56);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT8(M0 >> 0, 0) | NZBIT8(M0 >> 8, 1) |
        NZBIT8(M0 >> 16, 2) | NZBIT8(M0 >> 24, 3) |
        NZBIT8(M0 >> 32, 4) | NZBIT8(M0 >> 40, 5) |
        NZBIT8(M0 >> 48, 6) | NZBIT8(M0 >> 56, 7);
}

void OPPROTO op_iwmmxt_packsl_M0_wRn(void)
{
    M0 =
        (((M0 >> 0) & 0xffff) << 0) | (((M0 >> 32) & 0xffff) << 16) |
        (((M1 >> 0) & 0xffff) << 32) | (((M1 >> 32) & 0xffff) << 48);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT16(M0 >> 0, 0) | NZBIT16(M0 >> 16, 1) |
        NZBIT16(M0 >> 32, 2) | NZBIT16(M0 >> 48, 3);
}

void OPPROTO op_iwmmxt_packsq_M0_wRn(void)
{
    M0 = (M0 & 0xffffffff) | ((M1 & 0xffffffff) << 32);
    env->iwmmxt.cregs[ARM_IWMMXT_wCASF] =
        NZBIT32(M0 >> 0, 0) | NZBIT32(M0 >> 32, 1);
}

void OPPROTO op_iwmmxt_muladdsl_M0_T0_T1(void)
{
    M0 += (int32_t) EXTEND32(T0) * (int32_t) EXTEND32(T1);
}

void OPPROTO op_iwmmxt_muladdsw_M0_T0_T1(void)
{
    M0 += EXTEND32(EXTEND16S((T0 >> 0) & 0xffff) *
                   EXTEND16S((T1 >> 0) & 0xffff));
    M0 += EXTEND32(EXTEND16S((T0 >> 16) & 0xffff) *
                   EXTEND16S((T1 >> 16) & 0xffff));
}

void OPPROTO op_iwmmxt_muladdswl_M0_T0_T1(void)
{
    M0 += EXTEND32(EXTEND16S(T0 & 0xffff) *
                   EXTEND16S(T1 & 0xffff));
}
