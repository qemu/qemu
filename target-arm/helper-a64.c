/*
 *  AArch64 specific helpers
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
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
#include "exec/gdbstub.h"
#include "helper.h"
#include "qemu/host-utils.h"
#include "sysemu/sysemu.h"
#include "qemu/bitops.h"

/* C2.4.7 Multiply and divide */
/* special cases for 0 and LLONG_MIN are mandated by the standard */
uint64_t HELPER(udiv64)(uint64_t num, uint64_t den)
{
    if (den == 0) {
        return 0;
    }
    return num / den;
}

int64_t HELPER(sdiv64)(int64_t num, int64_t den)
{
    if (den == 0) {
        return 0;
    }
    if (num == LLONG_MIN && den == -1) {
        return LLONG_MIN;
    }
    return num / den;
}

uint64_t HELPER(clz64)(uint64_t x)
{
    return clz64(x);
}

uint64_t HELPER(cls64)(uint64_t x)
{
    return clrsb64(x);
}

uint32_t HELPER(cls32)(uint32_t x)
{
    return clrsb32(x);
}

uint64_t HELPER(rbit64)(uint64_t x)
{
    /* assign the correct byte position */
    x = bswap64(x);

    /* assign the correct nibble position */
    x = ((x & 0xf0f0f0f0f0f0f0f0ULL) >> 4)
        | ((x & 0x0f0f0f0f0f0f0f0fULL) << 4);

    /* assign the correct bit position */
    x = ((x & 0x8888888888888888ULL) >> 3)
        | ((x & 0x4444444444444444ULL) >> 1)
        | ((x & 0x2222222222222222ULL) << 1)
        | ((x & 0x1111111111111111ULL) << 3);

    return x;
}

/* Convert a softfloat float_relation_ (as returned by
 * the float*_compare functions) to the correct ARM
 * NZCV flag state.
 */
static inline uint32_t float_rel_to_flags(int res)
{
    uint64_t flags;
    switch (res) {
    case float_relation_equal:
        flags = PSTATE_Z | PSTATE_C;
        break;
    case float_relation_less:
        flags = PSTATE_N;
        break;
    case float_relation_greater:
        flags = PSTATE_C;
        break;
    case float_relation_unordered:
    default:
        flags = PSTATE_C | PSTATE_V;
        break;
    }
    return flags;
}

uint64_t HELPER(vfp_cmps_a64)(float32 x, float32 y, void *fp_status)
{
    return float_rel_to_flags(float32_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpes_a64)(float32 x, float32 y, void *fp_status)
{
    return float_rel_to_flags(float32_compare(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpd_a64)(float64 x, float64 y, void *fp_status)
{
    return float_rel_to_flags(float64_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmped_a64)(float64 x, float64 y, void *fp_status)
{
    return float_rel_to_flags(float64_compare(x, y, fp_status));
}

uint64_t HELPER(simd_tbl)(CPUARMState *env, uint64_t result, uint64_t indices,
                          uint32_t rn, uint32_t numregs)
{
    /* Helper function for SIMD TBL and TBX. We have to do the table
     * lookup part for the 64 bits worth of indices we're passed in.
     * result is the initial results vector (either zeroes for TBL
     * or some guest values for TBX), rn the register number where
     * the table starts, and numregs the number of registers in the table.
     * We return the results of the lookups.
     */
    int shift;

    for (shift = 0; shift < 64; shift += 8) {
        int index = extract64(indices, shift, 8);
        if (index < 16 * numregs) {
            /* Convert index (a byte offset into the virtual table
             * which is a series of 128-bit vectors concatenated)
             * into the correct vfp.regs[] element plus a bit offset
             * into that element, bearing in mind that the table
             * can wrap around from V31 to V0.
             */
            int elt = (rn * 2 + (index >> 3)) % 64;
            int bitidx = (index & 7) * 8;
            uint64_t val = extract64(env->vfp.regs[elt], bitidx, 8);

            result = deposit64(result, shift, 8, val);
        }
    }
    return result;
}
