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
#include "helper.h"
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

static inline uint64_t byte_zap(uint64_t op, uint8_t mskb)
{
    uint64_t mask;

    mask = 0;
    mask |= ((mskb >> 0) & 1) * 0x00000000000000FFULL;
    mask |= ((mskb >> 1) & 1) * 0x000000000000FF00ULL;
    mask |= ((mskb >> 2) & 1) * 0x0000000000FF0000ULL;
    mask |= ((mskb >> 3) & 1) * 0x00000000FF000000ULL;
    mask |= ((mskb >> 4) & 1) * 0x000000FF00000000ULL;
    mask |= ((mskb >> 5) & 1) * 0x0000FF0000000000ULL;
    mask |= ((mskb >> 6) & 1) * 0x00FF000000000000ULL;
    mask |= ((mskb >> 7) & 1) * 0xFF00000000000000ULL;

    return op & ~mask;
}

uint64_t helper_zap(uint64_t val, uint64_t mask)
{
    return byte_zap(val, mask);
}

uint64_t helper_zapnot(uint64_t val, uint64_t mask)
{
    return byte_zap(val, ~mask);
}

uint64_t helper_cmpbge(uint64_t op1, uint64_t op2)
{
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

uint64_t helper_addqv(CPUAlphaState *env, uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 += op2;
    if (unlikely((tmp ^ op2 ^ (-1ULL)) & (tmp ^ op1) & (1ULL << 63))) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
    return op1;
}

uint64_t helper_addlv(CPUAlphaState *env, uint64_t op1, uint64_t op2)
{
    uint64_t tmp = op1;
    op1 = (uint32_t)(op1 + op2);
    if (unlikely((tmp ^ op2 ^ (-1UL)) & (tmp ^ op1) & (1UL << 31))) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
    return op1;
}

uint64_t helper_subqv(CPUAlphaState *env, uint64_t op1, uint64_t op2)
{
    uint64_t res;
    res = op1 - op2;
    if (unlikely((op1 ^ op2) & (res ^ op1) & (1ULL << 63))) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
    return res;
}

uint64_t helper_sublv(CPUAlphaState *env, uint64_t op1, uint64_t op2)
{
    uint32_t res;
    res = op1 - op2;
    if (unlikely((op1 ^ op2) & (res ^ op1) & (1UL << 31))) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
    return res;
}

uint64_t helper_mullv(CPUAlphaState *env, uint64_t op1, uint64_t op2)
{
    int64_t res = (int64_t)op1 * (int64_t)op2;

    if (unlikely((int32_t)res != res)) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
    return (int64_t)((int32_t)res);
}

uint64_t helper_mulqv(CPUAlphaState *env, uint64_t op1, uint64_t op2)
{
    uint64_t tl, th;

    muls64(&tl, &th, op1, op2);
    /* If th != 0 && th != -1, then we had an overflow */
    if (unlikely((th + 1) > 1)) {
        arith_excp(env, GETPC(), EXC_M_IOV, 0);
    }
    return tl;
}
