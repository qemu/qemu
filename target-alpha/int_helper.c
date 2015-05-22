/*
 *  Helpers for integer and multimedia instructions.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"


uint64_t helper_ctpop(uint64_t arg)
{
    return ctpop64(arg);
}

uint64_t helper_ctlz(uint64_t arg)
{
    return clz64(arg);
}

uint64_t helper_cttz(uint64_t arg)
{
    return ctz64(arg);
}

uint64_t helper_zapnot(uint64_t val, uint64_t mskb)
{
    uint64_t mask;

    mask  = -(mskb & 0x01) & 0x00000000000000ffull;
    mask |= -(mskb & 0x02) & 0x000000000000ff00ull;
    mask |= -(mskb & 0x04) & 0x0000000000ff0000ull;
    mask |= -(mskb & 0x08) & 0x00000000ff000000ull;
    mask |= -(mskb & 0x10) & 0x000000ff00000000ull;
    mask |= -(mskb & 0x20) & 0x0000ff0000000000ull;
    mask |= -(mskb & 0x40) & 0x00ff000000000000ull;
    mask |= -(mskb & 0x80) & 0xff00000000000000ull;

    return val & mask;
}

uint64_t helper_zap(uint64_t val, uint64_t mask)
{
    return helper_zapnot(val, ~mask);
}

uint64_t helper_cmpbge(uint64_t op1, uint64_t op2)
{
#if defined(__SSE2__)
    uint64_t r;

    /* The cmpbge instruction is heavily used in the implementation of
       every string function on Alpha.  We can do much better than either
       the default loop below, or even an unrolled version by using the
       native vector support.  */
    {
        typedef uint64_t Q __attribute__((vector_size(16)));
        typedef uint8_t B __attribute__((vector_size(16)));

        Q q1 = (Q){ op1, 0 };
        Q q2 = (Q){ op2, 0 };

        q1 = (Q)((B)q1 >= (B)q2);

        r = q1[0];
    }

    /* Select only one bit from each byte.  */
    r &= 0x0101010101010101;

    /* Collect the bits into the bottom byte.  */
    /* .......A.......B.......C.......D.......E.......F.......G.......H */
    r |= r >> (8 - 1);

    /* .......A......AB......BC......CD......DE......EF......FG......GH */
    r |= r >> (16 - 2);

    /* .......A......AB.....ABC....ABCD....BCDE....CDEF....DEFG....EFGH */
    r |= r >> (32 - 4);

    /* .......A......AB.....ABC....ABCD...ABCDE..ABCDEF.ABCDEFGABCDEFGH */
    /* Return only the low 8 bits.  */
    return r & 0xff;
#else
    uint8_t opa, opb, res;
    int i;

    res = 0;
    for (i = 0; i < 8; i++) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        if (opa >= opb) {
            res |= 1 << i;
        }
    }
    return res;
#endif
}

uint64_t helper_minub8(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint8_t opa, opb, opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_minsb8(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int8_t opa, opb;
    uint8_t opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_minuw4(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint16_t opa, opb, opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_minsw4(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int16_t opa, opb;
    uint16_t opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa < opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_maxub8(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint8_t opa, opb, opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_maxsb8(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int8_t opa, opb;
    uint8_t opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 8);
    }
    return res;
}

uint64_t helper_maxuw4(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint16_t opa, opb, opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_maxsw4(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    int16_t opa, opb;
    uint16_t opr;
    int i;

    for (i = 0; i < 4; ++i) {
        opa = op1 >> (i * 16);
        opb = op2 >> (i * 16);
        opr = opa > opb ? opa : opb;
        res |= (uint64_t)opr << (i * 16);
    }
    return res;
}

uint64_t helper_perr(uint64_t op1, uint64_t op2)
{
    uint64_t res = 0;
    uint8_t opa, opb, opr;
    int i;

    for (i = 0; i < 8; ++i) {
        opa = op1 >> (i * 8);
        opb = op2 >> (i * 8);
        if (opa >= opb) {
            opr = opa - opb;
        } else {
            opr = opb - opa;
        }
        res += opr;
    }
    return res;
}

uint64_t helper_pklb(uint64_t op1)
{
    return (op1 & 0xff) | ((op1 >> 24) & 0xff00);
}

uint64_t helper_pkwb(uint64_t op1)
{
    return ((op1 & 0xff)
            | ((op1 >> 8) & 0xff00)
            | ((op1 >> 16) & 0xff0000)
            | ((op1 >> 24) & 0xff000000));
}

uint64_t helper_unpkbl(uint64_t op1)
{
    return (op1 & 0xff) | ((op1 & 0xff00) << 24);
}

uint64_t helper_unpkbw(uint64_t op1)
{
    return ((op1 & 0xff)
            | ((op1 & 0xff00) << 8)
            | ((op1 & 0xff0000) << 16)
            | ((op1 & 0xff000000) << 24));
}

void helper_check_overflow(CPUAlphaState *env, uint64_t op1, uint64_t op2)
{
    if (unlikely(op1 != op2)) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
}
