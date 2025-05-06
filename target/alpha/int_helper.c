/*
 *  Helpers for integer and multimedia instructions.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"


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

uint64_t helper_cmpbe0(uint64_t a)
{
    uint64_t m = 0x7f7f7f7f7f7f7f7fULL;
    uint64_t c = ~(((a & m) + m) | a | m);
    /* a.......b.......c.......d.......e.......f.......g.......h....... */
    c |= c << 7;
    /* ab......bc......cd......de......ef......fg......gh......h....... */
    c |= c << 14;
    /* abcd....bcde....cdef....defg....efgh....fgh.....gh......h....... */
    c |= c << 28;
    /* abcdefghbcdefgh.cdefgh..defgh...efgh....fgh.....gh......h....... */
    return c >> 56;
}

uint64_t helper_cmpbge(uint64_t a, uint64_t b)
{
    uint64_t mask = 0x00ff00ff00ff00ffULL;
    uint64_t test = 0x0100010001000100ULL;
    uint64_t al, ah, bl, bh, cl, ch;

    /* Separate the bytes to avoid false positives.  */
    al = a & mask;
    bl = b & mask;
    ah = (a >> 8) & mask;
    bh = (b >> 8) & mask;

    /* "Compare".  If a byte in B is greater than a byte in A,
       it will clear the test bit.  */
    cl = ((al | test) - bl) & test;
    ch = ((ah | test) - bh) & test;

    /* Fold all of the test bits into a contiguous set.  */
    /* ch=.......a...............c...............e...............g........ */
    /* cl=.......b...............d...............f...............h........ */
    cl += ch << 1;
    /* cl=......ab..............cd..............ef..............gh........ */
    cl |= cl << 14;
    /* cl=......abcd............cdef............efgh............gh........ */
    cl |= cl << 28;
    /* cl=......abcdefgh........cdefgh..........efgh............gh........ */
    return cl >> 50;
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
