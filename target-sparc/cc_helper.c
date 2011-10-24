/*
 * Helpers for lazy condition code handling
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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

static uint32_t compute_all_flags(CPUState *env)
{
    return env->psr & PSR_ICC;
}

static uint32_t compute_C_flags(CPUState *env)
{
    return env->psr & PSR_CARRY;
}

static inline uint32_t get_NZ_icc(int32_t dst)
{
    uint32_t ret = 0;

    if (dst == 0) {
        ret = PSR_ZERO;
    } else if (dst < 0) {
        ret = PSR_NEG;
    }
    return ret;
}

#ifdef TARGET_SPARC64
static uint32_t compute_all_flags_xcc(CPUState *env)
{
    return env->xcc & PSR_ICC;
}

static uint32_t compute_C_flags_xcc(CPUState *env)
{
    return env->xcc & PSR_CARRY;
}

static inline uint32_t get_NZ_xcc(target_long dst)
{
    uint32_t ret = 0;

    if (!dst) {
        ret = PSR_ZERO;
    } else if (dst < 0) {
        ret = PSR_NEG;
    }
    return ret;
}
#endif

static inline uint32_t get_V_div_icc(target_ulong src2)
{
    uint32_t ret = 0;

    if (src2 != 0) {
        ret = PSR_OVF;
    }
    return ret;
}

static uint32_t compute_all_div(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_V_div_icc(CC_SRC2);
    return ret;
}

static uint32_t compute_C_div(CPUState *env)
{
    return 0;
}

static inline uint32_t get_C_add_icc(uint32_t dst, uint32_t src1)
{
    uint32_t ret = 0;

    if (dst < src1) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_C_addx_icc(uint32_t dst, uint32_t src1,
                                      uint32_t src2)
{
    uint32_t ret = 0;

    if (((src1 & src2) | (~dst & (src1 | src2))) & (1U << 31)) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_V_add_icc(uint32_t dst, uint32_t src1,
                                     uint32_t src2)
{
    uint32_t ret = 0;

    if (((src1 ^ src2 ^ -1) & (src1 ^ dst)) & (1U << 31)) {
        ret = PSR_OVF;
    }
    return ret;
}

#ifdef TARGET_SPARC64
static inline uint32_t get_C_add_xcc(target_ulong dst, target_ulong src1)
{
    uint32_t ret = 0;

    if (dst < src1) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_C_addx_xcc(target_ulong dst, target_ulong src1,
                                      target_ulong src2)
{
    uint32_t ret = 0;

    if (((src1 & src2) | (~dst & (src1 | src2))) & (1ULL << 63)) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_V_add_xcc(target_ulong dst, target_ulong src1,
                                     target_ulong src2)
{
    uint32_t ret = 0;

    if (((src1 ^ src2 ^ -1) & (src1 ^ dst)) & (1ULL << 63)) {
        ret = PSR_OVF;
    }
    return ret;
}

static uint32_t compute_all_add_xcc(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_xcc(CC_DST);
    ret |= get_C_add_xcc(CC_DST, CC_SRC);
    ret |= get_V_add_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_add_xcc(CPUState *env)
{
    return get_C_add_xcc(CC_DST, CC_SRC);
}
#endif

static uint32_t compute_all_add(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_add_icc(CC_DST, CC_SRC);
    ret |= get_V_add_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_add(CPUState *env)
{
    return get_C_add_icc(CC_DST, CC_SRC);
}

#ifdef TARGET_SPARC64
static uint32_t compute_all_addx_xcc(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_xcc(CC_DST);
    ret |= get_C_addx_xcc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_add_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_addx_xcc(CPUState *env)
{
    uint32_t ret;

    ret = get_C_addx_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}
#endif

static uint32_t compute_all_addx(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_addx_icc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_add_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_addx(CPUState *env)
{
    uint32_t ret;

    ret = get_C_addx_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static inline uint32_t get_V_tag_icc(target_ulong src1, target_ulong src2)
{
    uint32_t ret = 0;

    if ((src1 | src2) & 0x3) {
        ret = PSR_OVF;
    }
    return ret;
}

static uint32_t compute_all_tadd(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_add_icc(CC_DST, CC_SRC);
    ret |= get_V_add_icc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_tag_icc(CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_all_taddtv(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_add_icc(CC_DST, CC_SRC);
    return ret;
}

static inline uint32_t get_C_sub_icc(uint32_t src1, uint32_t src2)
{
    uint32_t ret = 0;

    if (src1 < src2) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_C_subx_icc(uint32_t dst, uint32_t src1,
                                      uint32_t src2)
{
    uint32_t ret = 0;

    if (((~src1 & src2) | (dst & (~src1 | src2))) & (1U << 31)) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_V_sub_icc(uint32_t dst, uint32_t src1,
                                     uint32_t src2)
{
    uint32_t ret = 0;

    if (((src1 ^ src2) & (src1 ^ dst)) & (1U << 31)) {
        ret = PSR_OVF;
    }
    return ret;
}


#ifdef TARGET_SPARC64
static inline uint32_t get_C_sub_xcc(target_ulong src1, target_ulong src2)
{
    uint32_t ret = 0;

    if (src1 < src2) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_C_subx_xcc(target_ulong dst, target_ulong src1,
                                      target_ulong src2)
{
    uint32_t ret = 0;

    if (((~src1 & src2) | (dst & (~src1 | src2))) & (1ULL << 63)) {
        ret = PSR_CARRY;
    }
    return ret;
}

static inline uint32_t get_V_sub_xcc(target_ulong dst, target_ulong src1,
                                     target_ulong src2)
{
    uint32_t ret = 0;

    if (((src1 ^ src2) & (src1 ^ dst)) & (1ULL << 63)) {
        ret = PSR_OVF;
    }
    return ret;
}

static uint32_t compute_all_sub_xcc(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_xcc(CC_DST);
    ret |= get_C_sub_xcc(CC_SRC, CC_SRC2);
    ret |= get_V_sub_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_sub_xcc(CPUState *env)
{
    return get_C_sub_xcc(CC_SRC, CC_SRC2);
}
#endif

static uint32_t compute_all_sub(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_sub_icc(CC_SRC, CC_SRC2);
    ret |= get_V_sub_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_sub(CPUState *env)
{
    return get_C_sub_icc(CC_SRC, CC_SRC2);
}

#ifdef TARGET_SPARC64
static uint32_t compute_all_subx_xcc(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_xcc(CC_DST);
    ret |= get_C_subx_xcc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_sub_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_subx_xcc(CPUState *env)
{
    uint32_t ret;

    ret = get_C_subx_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}
#endif

static uint32_t compute_all_subx(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_subx_icc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_sub_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_subx(CPUState *env)
{
    uint32_t ret;

    ret = get_C_subx_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_all_tsub(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_sub_icc(CC_SRC, CC_SRC2);
    ret |= get_V_sub_icc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_tag_icc(CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_all_tsubtv(CPUState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_sub_icc(CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_all_logic(CPUState *env)
{
    return get_NZ_icc(CC_DST);
}

static uint32_t compute_C_logic(CPUState *env)
{
    return 0;
}

#ifdef TARGET_SPARC64
static uint32_t compute_all_logic_xcc(CPUState *env)
{
    return get_NZ_xcc(CC_DST);
}
#endif

typedef struct CCTable {
    uint32_t (*compute_all)(CPUState *env); /* return all the flags */
    uint32_t (*compute_c)(CPUState *env);  /* return the C flag */
} CCTable;

static const CCTable icc_table[CC_OP_NB] = {
    /* CC_OP_DYNAMIC should never happen */
    [CC_OP_FLAGS] = { compute_all_flags, compute_C_flags },
    [CC_OP_DIV] = { compute_all_div, compute_C_div },
    [CC_OP_ADD] = { compute_all_add, compute_C_add },
    [CC_OP_ADDX] = { compute_all_addx, compute_C_addx },
    [CC_OP_TADD] = { compute_all_tadd, compute_C_add },
    [CC_OP_TADDTV] = { compute_all_taddtv, compute_C_add },
    [CC_OP_SUB] = { compute_all_sub, compute_C_sub },
    [CC_OP_SUBX] = { compute_all_subx, compute_C_subx },
    [CC_OP_TSUB] = { compute_all_tsub, compute_C_sub },
    [CC_OP_TSUBTV] = { compute_all_tsubtv, compute_C_sub },
    [CC_OP_LOGIC] = { compute_all_logic, compute_C_logic },
};

#ifdef TARGET_SPARC64
static const CCTable xcc_table[CC_OP_NB] = {
    /* CC_OP_DYNAMIC should never happen */
    [CC_OP_FLAGS] = { compute_all_flags_xcc, compute_C_flags_xcc },
    [CC_OP_DIV] = { compute_all_logic_xcc, compute_C_logic },
    [CC_OP_ADD] = { compute_all_add_xcc, compute_C_add_xcc },
    [CC_OP_ADDX] = { compute_all_addx_xcc, compute_C_addx_xcc },
    [CC_OP_TADD] = { compute_all_add_xcc, compute_C_add_xcc },
    [CC_OP_TADDTV] = { compute_all_add_xcc, compute_C_add_xcc },
    [CC_OP_SUB] = { compute_all_sub_xcc, compute_C_sub_xcc },
    [CC_OP_SUBX] = { compute_all_subx_xcc, compute_C_subx_xcc },
    [CC_OP_TSUB] = { compute_all_sub_xcc, compute_C_sub_xcc },
    [CC_OP_TSUBTV] = { compute_all_sub_xcc, compute_C_sub_xcc },
    [CC_OP_LOGIC] = { compute_all_logic_xcc, compute_C_logic },
};
#endif

void helper_compute_psr(CPUState *env)
{
    uint32_t new_psr;

    new_psr = icc_table[CC_OP].compute_all(env);
    env->psr = new_psr;
#ifdef TARGET_SPARC64
    new_psr = xcc_table[CC_OP].compute_all(env);
    env->xcc = new_psr;
#endif
    CC_OP = CC_OP_FLAGS;
}

uint32_t helper_compute_C_icc(CPUState *env)
{
    uint32_t ret;

    ret = icc_table[CC_OP].compute_c(env) >> PSR_CARRY_SHIFT;
    return ret;
}
