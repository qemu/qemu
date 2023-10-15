/*
 * Helpers for lazy condition code handling
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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

static inline uint32_t get_C_add_icc(uint32_t dst, uint32_t src1)
{
    uint32_t ret = 0;

    if (dst < src1) {
        ret = PSR_CARRY;
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

static inline uint32_t get_V_add_xcc(target_ulong dst, target_ulong src1,
                                     target_ulong src2)
{
    uint32_t ret = 0;

    if (((src1 ^ src2 ^ -1) & (src1 ^ dst)) & (1ULL << 63)) {
        ret = PSR_OVF;
    }
    return ret;
}

static uint32_t compute_all_add_xcc(CPUSPARCState *env)
{
    uint32_t ret;

    ret = get_NZ_xcc(CC_DST);
    ret |= get_C_add_xcc(CC_DST, CC_SRC);
    ret |= get_V_add_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_add_xcc(CPUSPARCState *env)
{
    return get_C_add_xcc(CC_DST, CC_SRC);
}
#endif

static uint32_t compute_C_add(CPUSPARCState *env)
{
    return get_C_add_icc(CC_DST, CC_SRC);
}

static inline uint32_t get_V_tag_icc(target_ulong src1, target_ulong src2)
{
    uint32_t ret = 0;

    if ((src1 | src2) & 0x3) {
        ret = PSR_OVF;
    }
    return ret;
}

static uint32_t compute_all_taddtv(CPUSPARCState *env)
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

static uint32_t compute_all_sub_xcc(CPUSPARCState *env)
{
    uint32_t ret;

    ret = get_NZ_xcc(CC_DST);
    ret |= get_C_sub_xcc(CC_SRC, CC_SRC2);
    ret |= get_V_sub_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_sub_xcc(CPUSPARCState *env)
{
    return get_C_sub_xcc(CC_SRC, CC_SRC2);
}
#endif

static uint32_t compute_all_sub(CPUSPARCState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_sub_icc(CC_SRC, CC_SRC2);
    ret |= get_V_sub_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_sub(CPUSPARCState *env)
{
    return get_C_sub_icc(CC_SRC, CC_SRC2);
}

#ifdef TARGET_SPARC64
static uint32_t compute_all_subx_xcc(CPUSPARCState *env)
{
    uint32_t ret;

    ret = get_NZ_xcc(CC_DST);
    ret |= get_C_subx_xcc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_sub_xcc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_subx_xcc(CPUSPARCState *env)
{
    return get_C_subx_xcc(CC_DST, CC_SRC, CC_SRC2);
}
#endif

static uint32_t compute_all_subx(CPUSPARCState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_subx_icc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_sub_icc(CC_DST, CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_C_subx(CPUSPARCState *env)
{
    return get_C_subx_icc(CC_DST, CC_SRC, CC_SRC2);
}

static uint32_t compute_all_tsub(CPUSPARCState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_sub_icc(CC_SRC, CC_SRC2);
    ret |= get_V_sub_icc(CC_DST, CC_SRC, CC_SRC2);
    ret |= get_V_tag_icc(CC_SRC, CC_SRC2);
    return ret;
}

static uint32_t compute_all_tsubtv(CPUSPARCState *env)
{
    uint32_t ret;

    ret = get_NZ_icc(CC_DST);
    ret |= get_C_sub_icc(CC_SRC, CC_SRC2);
    return ret;
}

typedef struct CCTable {
    uint32_t (*compute_all)(CPUSPARCState *env); /* return all the flags */
    uint32_t (*compute_c)(CPUSPARCState *env);  /* return the C flag */
} CCTable;

static const CCTable icc_table[CC_OP_NB] = {
    /* CC_OP_DYNAMIC should never happen */
    [CC_OP_TADDTV] = { compute_all_taddtv, compute_C_add },
    [CC_OP_SUB] = { compute_all_sub, compute_C_sub },
    [CC_OP_SUBX] = { compute_all_subx, compute_C_subx },
    [CC_OP_TSUB] = { compute_all_tsub, compute_C_sub },
    [CC_OP_TSUBTV] = { compute_all_tsubtv, compute_C_sub },
};

#ifdef TARGET_SPARC64
static const CCTable xcc_table[CC_OP_NB] = {
    /* CC_OP_DYNAMIC should never happen */
    [CC_OP_TADDTV] = { compute_all_add_xcc, compute_C_add_xcc },
    [CC_OP_SUB] = { compute_all_sub_xcc, compute_C_sub_xcc },
    [CC_OP_SUBX] = { compute_all_subx_xcc, compute_C_subx_xcc },
    [CC_OP_TSUB] = { compute_all_sub_xcc, compute_C_sub_xcc },
    [CC_OP_TSUBTV] = { compute_all_sub_xcc, compute_C_sub_xcc },
};
#endif

void helper_compute_psr(CPUSPARCState *env)
{
    if (CC_OP == CC_OP_FLAGS) {
        return;
    }

    uint32_t icc = icc_table[CC_OP].compute_all(env);
#ifdef TARGET_SPARC64
    uint32_t xcc = xcc_table[CC_OP].compute_all(env);

    env->cc_N = deposit64(-(icc & PSR_NEG), 32, 32, -(xcc & PSR_NEG));
    env->cc_V = deposit64(-(icc & PSR_OVF), 32, 32, -(xcc & PSR_OVF));
    env->icc_C = (uint64_t)icc << (32 - PSR_CARRY_SHIFT);
    env->xcc_C = (xcc >> PSR_CARRY_SHIFT) & 1;
    env->xcc_Z = ~xcc & PSR_ZERO;
#else
    env->cc_N = -(icc & PSR_NEG);
    env->cc_V = -(icc & PSR_OVF);
    env->icc_C = (icc >> PSR_CARRY_SHIFT) & 1;
#endif
    env->icc_Z = ~icc & PSR_ZERO;

    CC_OP = CC_OP_FLAGS;
}

uint32_t helper_compute_C_icc(CPUSPARCState *env)
{
    if (CC_OP == CC_OP_FLAGS) {
#ifdef TARGET_SPARC64
        return extract64(env->icc_C, 32, 1);
#else
        return env->icc_C;
#endif
    }
    return icc_table[CC_OP].compute_c(env) >> PSR_CARRY_SHIFT;
}
