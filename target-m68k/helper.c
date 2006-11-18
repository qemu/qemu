/*
 *  m68k op helpers
 * 
 *  Copyright (c) 2006 CodeSourcery
 *  Written by Paul Brook
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>

#include "config.h"
#include "cpu.h"
#include "exec-all.h"

void cpu_m68k_flush_flags(CPUM68KState *env, int cc_op)
{
    int flags;
    uint32_t src;
    uint32_t dest;
    uint32_t tmp;

#define HIGHBIT 0x80000000u

#define SET_NZ(x) do { \
    if ((x) == 0) \
        flags |= CCF_Z; \
    else if ((int32_t)(x) < 0) \
        flags |= CCF_N; \
    } while (0)

#define SET_FLAGS_SUB(type, utype) do { \
    SET_NZ((type)dest); \
    tmp = dest + src; \
    if ((utype) tmp < (utype) src) \
        flags |= CCF_C; \
    if ((1u << (sizeof(type) * 8 - 1)) & (tmp ^ dest) & (tmp ^ src)) \
        flags |= CCF_V; \
    } while (0)

    flags = 0;
    src = env->cc_src;
    dest = env->cc_dest;
    switch (cc_op) {
    case CC_OP_FLAGS:
        flags = dest;
        break;
    case CC_OP_LOGIC:
        SET_NZ(dest);
        break;
    case CC_OP_ADD:
        SET_NZ(dest);
        if (dest < src)
            flags |= CCF_C;
        tmp = dest - src;
        if (HIGHBIT & (src ^ dest) & ~(tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SUB:
        SET_FLAGS_SUB(int32_t, uint32_t);
        break;
    case CC_OP_CMPB:
        SET_FLAGS_SUB(int8_t, uint8_t);
        break;
    case CC_OP_CMPW:
        SET_FLAGS_SUB(int16_t, uint16_t);
        break;
    case CC_OP_ADDX:
        SET_NZ(dest);
        if (dest <= src)
            flags |= CCF_C;
        tmp = dest - src - 1;
        if (HIGHBIT & (src ^ dest) & ~(tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SUBX:
        SET_NZ(dest);
        tmp = dest + src + 1;
        if (tmp <= src)
            flags |= CCF_C;
        if (HIGHBIT & (tmp ^ dest) & (tmp ^ src))
            flags |= CCF_V;
        break;
    case CC_OP_SHL:
        if (src >= 32) {
            SET_NZ(0);
        } else {
            tmp = dest << src;
            SET_NZ(tmp);
        }
        if (src && src <= 32 && (dest & (1 << (32 - src))))
            flags |= CCF_C;
        break;
    case CC_OP_SHR:
        if (src >= 32) {
            SET_NZ(0);
        } else {
            tmp = dest >> src;
            SET_NZ(tmp);
        }
        if (src && src <= 32 && ((dest >> (src - 1)) & 1))
            flags |= CCF_C;
        break;
    case CC_OP_SAR:
        if (src >= 32) {
            SET_NZ(-1);
        } else {
            tmp = (int32_t)dest >> src;
            SET_NZ(tmp);
        }
        if (src && src <= 32 && (((int32_t)dest >> (src - 1)) & 1))
            flags |= CCF_C;
        break;
    default:
        cpu_abort(env, "Bad CC_OP %d", cc_op);
    }
    env->cc_op = CC_OP_FLAGS;
    env->cc_dest = flags;
}

float64 helper_sub_cmpf64(CPUM68KState *env, float64 src0, float64 src1)
{
    /* ??? This may incorrectly raise exceptions.  */
    /* ??? Should flush denormals to zero.  */
    float64 res;
    res = float64_sub(src0, src1, &env->fp_status);
    if (float64_is_nan(res)) {
        /* +/-inf compares equal against itself, but sub returns nan.  */
        if (!float64_is_nan(src0)
            && !float64_is_nan(src1)) {
            res = 0;
            if (float64_lt_quiet(src0, res, &env->fp_status))
                res = float64_chs(res);
        }
    }
    return res;
}
