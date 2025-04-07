/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/probe.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "cpu.h"
#include "internal.h"
#include "macros.h"
#include "arch.h"
#include "hex_arch_types.h"
#include "fma_emu.h"
#include "mmvec/mmvec.h"
#include "mmvec/macros.h"
#include "op_helper.h"
#include "translate.h"

#define SF_BIAS        127
#define SF_MANTBITS    23

/* Exceptions processing helpers */
G_NORETURN void hexagon_raise_exception_err(CPUHexagonState *env,
                                            uint32_t exception,
                                            uintptr_t pc)
{
    CPUState *cs = env_cpu(env);
    qemu_log_mask(CPU_LOG_INT, "%s: %d\n", __func__, exception);
    cs->exception_index = exception;
    cpu_loop_exit_restore(cs, pc);
}

G_NORETURN void HELPER(raise_exception)(CPUHexagonState *env, uint32_t excp)
{
    hexagon_raise_exception_err(env, excp, 0);
}

void log_store32(CPUHexagonState *env, target_ulong addr,
                 target_ulong val, int width, int slot)
{
    env->mem_log_stores[slot].va = addr;
    env->mem_log_stores[slot].width = width;
    env->mem_log_stores[slot].data32 = val;
}

void log_store64(CPUHexagonState *env, target_ulong addr,
                 int64_t val, int width, int slot)
{
    env->mem_log_stores[slot].va = addr;
    env->mem_log_stores[slot].width = width;
    env->mem_log_stores[slot].data64 = val;
}

static void commit_store(CPUHexagonState *env, int slot_num, uintptr_t ra)
{
    uint8_t width = env->mem_log_stores[slot_num].width;
    target_ulong va = env->mem_log_stores[slot_num].va;

    switch (width) {
    case 1:
        cpu_stb_data_ra(env, va, env->mem_log_stores[slot_num].data32, ra);
        break;
    case 2:
        cpu_stw_data_ra(env, va, env->mem_log_stores[slot_num].data32, ra);
        break;
    case 4:
        cpu_stl_data_ra(env, va, env->mem_log_stores[slot_num].data32, ra);
        break;
    case 8:
        cpu_stq_data_ra(env, va, env->mem_log_stores[slot_num].data64, ra);
        break;
    default:
        g_assert_not_reached();
    }
}

void HELPER(commit_store)(CPUHexagonState *env, int slot_num)
{
    uintptr_t ra = GETPC();
    commit_store(env, slot_num, ra);
}

void HELPER(gather_store)(CPUHexagonState *env, uint32_t addr, int slot)
{
    mem_gather_store(env, addr, slot);
}

void HELPER(commit_hvx_stores)(CPUHexagonState *env)
{
    uintptr_t ra = GETPC();

    /* Normal (possibly masked) vector store */
    for (int i = 0; i < VSTORES_MAX; i++) {
        if (env->vstore_pending[i]) {
            env->vstore_pending[i] = 0;
            target_ulong va = env->vstore[i].va;
            int size = env->vstore[i].size;
            for (int j = 0; j < size; j++) {
                if (test_bit(j, env->vstore[i].mask)) {
                    cpu_stb_data_ra(env, va + j, env->vstore[i].data.ub[j], ra);
                }
            }
        }
    }

    /* Scatter store */
    if (env->vtcm_pending) {
        env->vtcm_pending = false;
        if (env->vtcm_log.op) {
            /* Need to perform the scatter read/modify/write at commit time */
            if (env->vtcm_log.op_size == 2) {
                SCATTER_OP_WRITE_TO_MEM(uint16_t);
            } else if (env->vtcm_log.op_size == 4) {
                /* Word Scatter += */
                SCATTER_OP_WRITE_TO_MEM(uint32_t);
            } else {
                g_assert_not_reached();
            }
        } else {
            for (int i = 0; i < sizeof(MMVector); i++) {
                if (test_bit(i, env->vtcm_log.mask)) {
                    cpu_stb_data_ra(env, env->vtcm_log.va[i],
                                    env->vtcm_log.data.ub[i], ra);
                    clear_bit(i, env->vtcm_log.mask);
                    env->vtcm_log.data.ub[i] = 0;
                }

            }
        }
    }
}

int32_t HELPER(fcircadd)(int32_t RxV, int32_t offset, int32_t M, int32_t CS)
{
    uint32_t K_const = extract32(M, 24, 4);
    uint32_t length = extract32(M, 0, 17);
    uint32_t new_ptr = RxV + offset;
    uint32_t start_addr;
    uint32_t end_addr;

    if (K_const == 0 && length >= 4) {
        start_addr = CS;
        end_addr = start_addr + length;
    } else {
        /*
         * Versions v3 and earlier used the K value to specify a power-of-2 size
         * 2^(K+2) that is greater than the buffer length
         */
        int32_t mask = (1 << (K_const + 2)) - 1;
        start_addr = RxV & (~mask);
        end_addr = start_addr | length;
    }

    if (new_ptr >= end_addr) {
        new_ptr -= length;
    } else if (new_ptr < start_addr) {
        new_ptr += length;
    }

    return new_ptr;
}

uint32_t HELPER(fbrev)(uint32_t addr)
{
    /*
     *  Bit reverse the low 16 bits of the address
     */
    return deposit32(addr, 0, 16, revbit16(addr));
}

static float32 build_float32(uint8_t sign, uint32_t exp, uint32_t mant)
{
    return make_float32(
        ((sign & 1) << 31) |
        ((exp & 0xff) << SF_MANTBITS) |
        (mant & ((1 << SF_MANTBITS) - 1)));
}

/*
 * sfrecipa, sfinvsqrta have two 32-bit results
 *     r0,p0=sfrecipa(r1,r2)
 *     r0,p0=sfinvsqrta(r1)
 *
 * Since helpers can only return a single value, we pack the two results
 * into a 64-bit value.
 */
uint64_t HELPER(sfrecipa)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    int32_t PeV = 0;
    float32 RdV;
    int idx;
    int adjust;
    int mant;
    int exp;

    arch_fpop_start(env);
    if (arch_sf_recip_common(&RsV, &RtV, &RdV, &adjust, &env->fp_status)) {
        PeV = adjust;
        idx = (RtV >> 16) & 0x7f;
        mant = (recip_lookup_table[idx] << 15) | 1;
        exp = SF_BIAS - (float32_getexp(RtV) - SF_BIAS) - 1;
        RdV = build_float32(extract32(RtV, 31, 1), exp, mant);
    }
    arch_fpop_end(env);
    return ((uint64_t)RdV << 32) | PeV;
}

uint64_t HELPER(sfinvsqrta)(CPUHexagonState *env, float32 RsV)
{
    int PeV = 0;
    float32 RdV;
    int idx;
    int adjust;
    int mant;
    int exp;

    arch_fpop_start(env);
    if (arch_sf_invsqrt_common(&RsV, &RdV, &adjust, &env->fp_status)) {
        PeV = adjust;
        idx = (RsV >> 17) & 0x7f;
        mant = (invsqrt_lookup_table[idx] << 15);
        exp = SF_BIAS - ((float32_getexp(RsV) - SF_BIAS) >> 1) - 1;
        RdV = build_float32(extract32(RsV, 31, 1), exp, mant);
    }
    arch_fpop_end(env);
    return ((uint64_t)RdV << 32) | PeV;
}

int64_t HELPER(vacsh_val)(CPUHexagonState *env,
                           int64_t RxxV, int64_t RssV, int64_t RttV,
                           uint32_t pkt_need_commit)
{
    for (int i = 0; i < 4; i++) {
        int xv = sextract64(RxxV, i * 16, 16);
        int sv = sextract64(RssV, i * 16, 16);
        int tv = sextract64(RttV, i * 16, 16);
        int max;
        xv = xv + tv;
        sv = sv - tv;
        max = xv > sv ? xv : sv;
        /* Note that fSATH can set the OVF bit in usr */
        RxxV = deposit64(RxxV, i * 16, 16, fSATH(max));
    }
    return RxxV;
}

int32_t HELPER(vacsh_pred)(CPUHexagonState *env,
                           int64_t RxxV, int64_t RssV, int64_t RttV)
{
    int32_t PeV = 0;
    for (int i = 0; i < 4; i++) {
        int xv = sextract64(RxxV, i * 16, 16);
        int sv = sextract64(RssV, i * 16, 16);
        int tv = sextract64(RttV, i * 16, 16);
        xv = xv + tv;
        sv = sv - tv;
        PeV = deposit32(PeV, i * 2, 1, (xv > sv));
        PeV = deposit32(PeV, i * 2 + 1, 1, (xv > sv));
    }
    return PeV;
}

int64_t HELPER(cabacdecbin_val)(int64_t RssV, int64_t RttV)
{
    int64_t RddV = 0;
    size4u_t state;
    size4u_t valMPS;
    size4u_t bitpos;
    size4u_t range;
    size4u_t offset;
    size4u_t rLPS;
    size4u_t rMPS;

    state =  fEXTRACTU_RANGE(fGETWORD(1, RttV), 5, 0);
    valMPS = fEXTRACTU_RANGE(fGETWORD(1, RttV), 8, 8);
    bitpos = fEXTRACTU_RANGE(fGETWORD(0, RttV), 4, 0);
    range =  fGETWORD(0, RssV);
    offset = fGETWORD(1, RssV);

    /* calculate rLPS */
    range <<= bitpos;
    offset <<= bitpos;
    rLPS = rLPS_table_64x4[state][(range >> 29) & 3];
    rLPS  = rLPS << 23;   /* left aligned */

    /* calculate rMPS */
    rMPS = (range & 0xff800000) - rLPS;

    /* most probable region */
    if (offset < rMPS) {
        RddV = AC_next_state_MPS_64[state];
        fINSERT_RANGE(RddV, 8, 8, valMPS);
        fINSERT_RANGE(RddV, 31, 23, (rMPS >> 23));
        fSETWORD(1, RddV, offset);
    }
    /* least probable region */
    else {
        RddV = AC_next_state_LPS_64[state];
        fINSERT_RANGE(RddV, 8, 8, ((!state) ? (1 - valMPS) : (valMPS)));
        fINSERT_RANGE(RddV, 31, 23, (rLPS >> 23));
        fSETWORD(1, RddV, (offset - rMPS));
    }
    return RddV;
}

int32_t HELPER(cabacdecbin_pred)(int64_t RssV, int64_t RttV)
{
    int32_t p0 = 0;
    size4u_t state;
    size4u_t valMPS;
    size4u_t bitpos;
    size4u_t range;
    size4u_t offset;
    size4u_t rLPS;
    size4u_t rMPS;

    state =  fEXTRACTU_RANGE(fGETWORD(1, RttV), 5, 0);
    valMPS = fEXTRACTU_RANGE(fGETWORD(1, RttV), 8, 8);
    bitpos = fEXTRACTU_RANGE(fGETWORD(0, RttV), 4, 0);
    range =  fGETWORD(0, RssV);
    offset = fGETWORD(1, RssV);

    /* calculate rLPS */
    range <<= bitpos;
    offset <<= bitpos;
    rLPS = rLPS_table_64x4[state][(range >> 29) & 3];
    rLPS  = rLPS << 23;   /* left aligned */

    /* calculate rMPS */
    rMPS = (range & 0xff800000) - rLPS;

    /* most probable region */
    if (offset < rMPS) {
        p0 = valMPS;

    }
    /* least probable region */
    else {
        p0 = valMPS ^ 1;
    }
    return p0;
}

static void probe_store(CPUHexagonState *env, int slot, int mmu_idx,
                        bool is_predicated, uintptr_t retaddr)
{
    if (!is_predicated || !(env->slot_cancelled & (1 << slot))) {
        size1u_t width = env->mem_log_stores[slot].width;
        target_ulong va = env->mem_log_stores[slot].va;
        probe_write(env, va, width, mmu_idx, retaddr);
    }
}

/*
 * Called from a mem_noshuf packet to make sure the load doesn't
 * raise an exception
 */
void HELPER(probe_noshuf_load)(CPUHexagonState *env, target_ulong va,
                               int size, int mmu_idx)
{
    uintptr_t retaddr = GETPC();
    probe_read(env, va, size, mmu_idx, retaddr);
}

/* Called during packet commit when there are two scalar stores */
void HELPER(probe_pkt_scalar_store_s0)(CPUHexagonState *env, int args)
{
    int mmu_idx = FIELD_EX32(args, PROBE_PKT_SCALAR_STORE_S0, MMU_IDX);
    bool is_predicated =
        FIELD_EX32(args, PROBE_PKT_SCALAR_STORE_S0, IS_PREDICATED);
    uintptr_t ra = GETPC();
    probe_store(env, 0, mmu_idx, is_predicated, ra);
}

static void probe_hvx_stores(CPUHexagonState *env, int mmu_idx,
                                    uintptr_t retaddr)
{
    /* Normal (possibly masked) vector store */
    for (int i = 0; i < VSTORES_MAX; i++) {
        if (env->vstore_pending[i]) {
            target_ulong va = env->vstore[i].va;
            int size = env->vstore[i].size;
            for (int j = 0; j < size; j++) {
                if (test_bit(j, env->vstore[i].mask)) {
                    probe_write(env, va + j, 1, mmu_idx, retaddr);
                }
            }
        }
    }

    /* Scatter store */
    if (env->vtcm_pending) {
        if (env->vtcm_log.op) {
            /* Need to perform the scatter read/modify/write at commit time */
            if (env->vtcm_log.op_size == 2) {
                SCATTER_OP_PROBE_MEM(size2u_t, mmu_idx, retaddr);
            } else if (env->vtcm_log.op_size == 4) {
                /* Word Scatter += */
                SCATTER_OP_PROBE_MEM(size4u_t, mmu_idx, retaddr);
            } else {
                g_assert_not_reached();
            }
        } else {
            for (int i = 0; i < sizeof(MMVector); i++) {
                if (test_bit(i, env->vtcm_log.mask)) {
                    probe_write(env, env->vtcm_log.va[i], 1, mmu_idx, retaddr);
                }

            }
        }
    }
}

void HELPER(probe_hvx_stores)(CPUHexagonState *env, int mmu_idx)
{
    uintptr_t retaddr = GETPC();
    probe_hvx_stores(env, mmu_idx, retaddr);
}

void HELPER(probe_pkt_scalar_hvx_stores)(CPUHexagonState *env, int mask)
{
    bool has_st0 = FIELD_EX32(mask, PROBE_PKT_SCALAR_HVX_STORES, HAS_ST0);
    bool has_st1 = FIELD_EX32(mask, PROBE_PKT_SCALAR_HVX_STORES, HAS_ST1);
    bool has_hvx_stores =
        FIELD_EX32(mask, PROBE_PKT_SCALAR_HVX_STORES, HAS_HVX_STORES);
    bool s0_is_pred = FIELD_EX32(mask, PROBE_PKT_SCALAR_HVX_STORES, S0_IS_PRED);
    bool s1_is_pred = FIELD_EX32(mask, PROBE_PKT_SCALAR_HVX_STORES, S1_IS_PRED);
    int mmu_idx = FIELD_EX32(mask, PROBE_PKT_SCALAR_HVX_STORES, MMU_IDX);
    uintptr_t ra = GETPC();

    if (has_st0) {
        probe_store(env, 0, mmu_idx, s0_is_pred, ra);
    }
    if (has_st1) {
        probe_store(env, 1, mmu_idx, s1_is_pred, ra);
    }
    if (has_hvx_stores) {
        probe_hvx_stores(env, mmu_idx, ra);
    }
}

#ifndef CONFIG_HEXAGON_IDEF_PARSER
/*
 * mem_noshuf
 * Section 5.5 of the Hexagon V67 Programmer's Reference Manual
 *
 * If the load is in slot 0 and there is a store in slot1 (that
 * wasn't cancelled), we have to do the store first.
 */
static void check_noshuf(CPUHexagonState *env, bool pkt_has_scalar_store_s1,
                         uint32_t slot, target_ulong vaddr, int size,
                         uintptr_t ra)
{
    if (slot == 0 && pkt_has_scalar_store_s1 &&
        ((env->slot_cancelled & (1 << 1)) == 0)) {
        probe_read(env, vaddr, size, MMU_USER_IDX, ra);
        commit_store(env, 1, ra);
    }
}
#endif

/* Floating point */
float64 HELPER(conv_sf2df)(CPUHexagonState *env, float32 RsV)
{
    float64 out_f64;
    arch_fpop_start(env);
    out_f64 = float32_to_float64(RsV, &env->fp_status);
    arch_fpop_end(env);
    return out_f64;
}

float32 HELPER(conv_df2sf)(CPUHexagonState *env, float64 RssV)
{
    float32 out_f32;
    arch_fpop_start(env);
    out_f32 = float64_to_float32(RssV, &env->fp_status);
    arch_fpop_end(env);
    return out_f32;
}

float32 HELPER(conv_uw2sf)(CPUHexagonState *env, int32_t RsV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = uint32_to_float32(RsV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

float64 HELPER(conv_uw2df)(CPUHexagonState *env, int32_t RsV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = uint32_to_float64(RsV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

float32 HELPER(conv_w2sf)(CPUHexagonState *env, int32_t RsV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = int32_to_float32(RsV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

float64 HELPER(conv_w2df)(CPUHexagonState *env, int32_t RsV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = int32_to_float64(RsV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

float32 HELPER(conv_ud2sf)(CPUHexagonState *env, int64_t RssV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = uint64_to_float32(RssV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

float64 HELPER(conv_ud2df)(CPUHexagonState *env, int64_t RssV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = uint64_to_float64(RssV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

float32 HELPER(conv_d2sf)(CPUHexagonState *env, int64_t RssV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = int64_to_float32(RssV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

float64 HELPER(conv_d2df)(CPUHexagonState *env, int64_t RssV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = int64_to_float64(RssV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

uint32_t HELPER(conv_sf2uw)(CPUHexagonState *env, float32 RsV)
{
    uint32_t RdV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float32_is_neg(RsV) && !float32_is_any_nan(RsV) && !float32_is_zero(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = 0;
    } else {
        RdV = float32_to_uint32(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

int32_t HELPER(conv_sf2w)(CPUHexagonState *env, float32 RsV)
{
    int32_t RdV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float32_is_any_nan(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = -1;
    } else {
        RdV = float32_to_int32(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

uint64_t HELPER(conv_sf2ud)(CPUHexagonState *env, float32 RsV)
{
    uint64_t RddV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float32_is_neg(RsV) && !float32_is_any_nan(RsV) && !float32_is_zero(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = 0;
    } else {
        RddV = float32_to_uint64(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

int64_t HELPER(conv_sf2d)(CPUHexagonState *env, float32 RsV)
{
    int64_t RddV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float32_is_any_nan(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = -1;
    } else {
        RddV = float32_to_int64(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

uint32_t HELPER(conv_df2uw)(CPUHexagonState *env, float64 RssV)
{
    uint32_t RdV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float64_is_neg(RssV) && !float64_is_any_nan(RssV) && !float64_is_zero(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = 0;
    } else {
        RdV = float64_to_uint32(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

int32_t HELPER(conv_df2w)(CPUHexagonState *env, float64 RssV)
{
    int32_t RdV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float64_is_any_nan(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = -1;
    } else {
        RdV = float64_to_int32(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

uint64_t HELPER(conv_df2ud)(CPUHexagonState *env, float64 RssV)
{
    uint64_t RddV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float64_is_neg(RssV) && !float64_is_any_nan(RssV) && !float64_is_zero(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = 0;
    } else {
        RddV = float64_to_uint64(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

int64_t HELPER(conv_df2d)(CPUHexagonState *env, float64 RssV)
{
    int64_t RddV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float64_is_any_nan(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = -1;
    } else {
        RddV = float64_to_int64(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

uint32_t HELPER(conv_sf2uw_chop)(CPUHexagonState *env, float32 RsV)
{
    uint32_t RdV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float32_is_neg(RsV) && !float32_is_any_nan(RsV) && !float32_is_zero(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = 0;
    } else {
        RdV = float32_to_uint32_round_to_zero(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

int32_t HELPER(conv_sf2w_chop)(CPUHexagonState *env, float32 RsV)
{
    int32_t RdV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float32_is_any_nan(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = -1;
    } else {
        RdV = float32_to_int32_round_to_zero(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

uint64_t HELPER(conv_sf2ud_chop)(CPUHexagonState *env, float32 RsV)
{
    uint64_t RddV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float32_is_neg(RsV) && !float32_is_any_nan(RsV) && !float32_is_zero(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = 0;
    } else {
        RddV = float32_to_uint64_round_to_zero(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

int64_t HELPER(conv_sf2d_chop)(CPUHexagonState *env, float32 RsV)
{
    int64_t RddV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float32_is_any_nan(RsV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = -1;
    } else {
        RddV = float32_to_int64_round_to_zero(RsV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

uint32_t HELPER(conv_df2uw_chop)(CPUHexagonState *env, float64 RssV)
{
    uint32_t RdV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float64_is_neg(RssV) && !float64_is_any_nan(RssV) && !float64_is_zero(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = 0;
    } else {
        RdV = float64_to_uint32_round_to_zero(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

int32_t HELPER(conv_df2w_chop)(CPUHexagonState *env, float64 RssV)
{
    int32_t RdV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float64_is_any_nan(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RdV = -1;
    } else {
        RdV = float64_to_int32_round_to_zero(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RdV;
}

uint64_t HELPER(conv_df2ud_chop)(CPUHexagonState *env, float64 RssV)
{
    uint64_t RddV;
    arch_fpop_start(env);
    /* Hexagon checks the sign before rounding */
    if (float64_is_neg(RssV) && !float64_is_any_nan(RssV) && !float64_is_zero(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = 0;
    } else {
        RddV = float64_to_uint64_round_to_zero(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

int64_t HELPER(conv_df2d_chop)(CPUHexagonState *env, float64 RssV)
{
    int64_t RddV;
    arch_fpop_start(env);
    /* Hexagon returns -1 for NaN */
    if (float64_is_any_nan(RssV)) {
        float_raise(float_flag_invalid, &env->fp_status);
        RddV = -1;
    } else {
        RddV = float64_to_int64_round_to_zero(RssV, &env->fp_status);
    }
    arch_fpop_end(env);
    return RddV;
}

float32 HELPER(sfadd)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = float32_add(RsV, RtV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

float32 HELPER(sfsub)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = float32_sub(RsV, RtV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

int32_t HELPER(sfcmpeq)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    int32_t PdV;
    arch_fpop_start(env);
    PdV = f8BITSOF(float32_eq_quiet(RsV, RtV, &env->fp_status));
    arch_fpop_end(env);
    return PdV;
}

int32_t HELPER(sfcmpgt)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    int cmp;
    int32_t PdV;
    arch_fpop_start(env);
    cmp = float32_compare_quiet(RsV, RtV, &env->fp_status);
    PdV = f8BITSOF(cmp == float_relation_greater);
    arch_fpop_end(env);
    return PdV;
}

int32_t HELPER(sfcmpge)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    int cmp;
    int32_t PdV;
    arch_fpop_start(env);
    cmp = float32_compare_quiet(RsV, RtV, &env->fp_status);
    PdV = f8BITSOF(cmp == float_relation_greater ||
                   cmp == float_relation_equal);
    arch_fpop_end(env);
    return PdV;
}

int32_t HELPER(sfcmpuo)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    int32_t PdV;
    arch_fpop_start(env);
    PdV = f8BITSOF(float32_unordered_quiet(RsV, RtV, &env->fp_status));
    arch_fpop_end(env);
    return PdV;
}

float32 HELPER(sfmax)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = float32_maximum_number(RsV, RtV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

float32 HELPER(sfmin)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = float32_minimum_number(RsV, RtV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

int32_t HELPER(sfclass)(CPUHexagonState *env, float32 RsV, int32_t uiV)
{
    int32_t PdV = 0;
    arch_fpop_start(env);
    if (fGETBIT(0, uiV) && float32_is_zero(RsV)) {
        PdV = 0xff;
    }
    if (fGETBIT(1, uiV) && float32_is_normal(RsV)) {
        PdV = 0xff;
    }
    if (fGETBIT(2, uiV) && float32_is_denormal(RsV)) {
        PdV = 0xff;
    }
    if (fGETBIT(3, uiV) && float32_is_infinity(RsV)) {
        PdV = 0xff;
    }
    if (fGETBIT(4, uiV) && float32_is_any_nan(RsV)) {
        PdV = 0xff;
    }
    set_float_exception_flags(0, &env->fp_status);
    arch_fpop_end(env);
    return PdV;
}

float32 HELPER(sffixupn)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    float32 RdV = 0;
    int adjust;
    arch_fpop_start(env);
    arch_sf_recip_common(&RsV, &RtV, &RdV, &adjust, &env->fp_status);
    RdV = RsV;
    arch_fpop_end(env);
    return RdV;
}

float32 HELPER(sffixupd)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    float32 RdV = 0;
    int adjust;
    arch_fpop_start(env);
    arch_sf_recip_common(&RsV, &RtV, &RdV, &adjust, &env->fp_status);
    RdV = RtV;
    arch_fpop_end(env);
    return RdV;
}

float32 HELPER(sffixupr)(CPUHexagonState *env, float32 RsV)
{
    float32 RdV = 0;
    int adjust;
    arch_fpop_start(env);
    arch_sf_invsqrt_common(&RsV, &RdV, &adjust, &env->fp_status);
    RdV = RsV;
    arch_fpop_end(env);
    return RdV;
}

float64 HELPER(dfadd)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = float64_add(RssV, RttV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

float64 HELPER(dfsub)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = float64_sub(RssV, RttV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

float64 HELPER(dfmax)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = float64_maximum_number(RssV, RttV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

float64 HELPER(dfmin)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    float64 RddV;
    arch_fpop_start(env);
    RddV = float64_minimum_number(RssV, RttV, &env->fp_status);
    arch_fpop_end(env);
    return RddV;
}

int32_t HELPER(dfcmpeq)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    int32_t PdV;
    arch_fpop_start(env);
    PdV = f8BITSOF(float64_eq_quiet(RssV, RttV, &env->fp_status));
    arch_fpop_end(env);
    return PdV;
}

int32_t HELPER(dfcmpgt)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    int cmp;
    int32_t PdV;
    arch_fpop_start(env);
    cmp = float64_compare_quiet(RssV, RttV, &env->fp_status);
    PdV = f8BITSOF(cmp == float_relation_greater);
    arch_fpop_end(env);
    return PdV;
}

int32_t HELPER(dfcmpge)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    int cmp;
    int32_t PdV;
    arch_fpop_start(env);
    cmp = float64_compare_quiet(RssV, RttV, &env->fp_status);
    PdV = f8BITSOF(cmp == float_relation_greater ||
                   cmp == float_relation_equal);
    arch_fpop_end(env);
    return PdV;
}

int32_t HELPER(dfcmpuo)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    int32_t PdV;
    arch_fpop_start(env);
    PdV = f8BITSOF(float64_unordered_quiet(RssV, RttV, &env->fp_status));
    arch_fpop_end(env);
    return PdV;
}

int32_t HELPER(dfclass)(CPUHexagonState *env, float64 RssV, int32_t uiV)
{
    int32_t PdV = 0;
    arch_fpop_start(env);
    if (fGETBIT(0, uiV) && float64_is_zero(RssV)) {
        PdV = 0xff;
    }
    if (fGETBIT(1, uiV) && float64_is_normal(RssV)) {
        PdV = 0xff;
    }
    if (fGETBIT(2, uiV) && float64_is_denormal(RssV)) {
        PdV = 0xff;
    }
    if (fGETBIT(3, uiV) && float64_is_infinity(RssV)) {
        PdV = 0xff;
    }
    if (fGETBIT(4, uiV) && float64_is_any_nan(RssV)) {
        PdV = 0xff;
    }
    set_float_exception_flags(0, &env->fp_status);
    arch_fpop_end(env);
    return PdV;
}

float32 HELPER(sfmpy)(CPUHexagonState *env, float32 RsV, float32 RtV)
{
    float32 RdV;
    arch_fpop_start(env);
    RdV = float32_mul(RsV, RtV, &env->fp_status);
    arch_fpop_end(env);
    return RdV;
}

float32 HELPER(sffma)(CPUHexagonState *env, float32 RxV,
                      float32 RsV, float32 RtV)
{
    arch_fpop_start(env);
    RxV = float32_muladd(RsV, RtV, RxV, 0, &env->fp_status);
    arch_fpop_end(env);
    return RxV;
}

float32 HELPER(sffma_sc)(CPUHexagonState *env, float32 RxV,
                         float32 RsV, float32 RtV, float32 PuV)
{
    arch_fpop_start(env);
    RxV = float32_muladd_scalbn(RsV, RtV, RxV, fSXTN(8, 64, PuV),
                                float_muladd_suppress_add_product_zero,
                                &env->fp_status);
    arch_fpop_end(env);
    return RxV;
}

float32 HELPER(sffms)(CPUHexagonState *env, float32 RxV,
                      float32 RsV, float32 RtV)
{
    arch_fpop_start(env);
    RxV = float32_muladd(RsV, RtV, RxV, float_muladd_negate_product,
                         &env->fp_status);
    arch_fpop_end(env);
    return RxV;
}

static float32 do_sffma_lib(CPUHexagonState *env, float32 RxV,
                            float32 RsV, float32 RtV, int negate)
{
    int flags;

    arch_fpop_start(env);

    set_float_rounding_mode(float_round_nearest_even_max, &env->fp_status);
    RxV = float32_muladd(RsV, RtV, RxV,
                         negate | float_muladd_suppress_add_product_zero,
                         &env->fp_status);

    flags = get_float_exception_flags(&env->fp_status);
    if (flags) {
        /* Flags are suppressed by this instruction. */
        set_float_exception_flags(0, &env->fp_status);

        /* Return 0 for Inf - Inf. */
        if (flags & float_flag_invalid_isi) {
            RxV = 0;
        }
    }

    arch_fpop_end(env);
    return RxV;
}

float32 HELPER(sffma_lib)(CPUHexagonState *env, float32 RxV,
                          float32 RsV, float32 RtV)
{
    return do_sffma_lib(env, RxV, RsV, RtV, 0);
}

float32 HELPER(sffms_lib)(CPUHexagonState *env, float32 RxV,
                          float32 RsV, float32 RtV)
{
    return do_sffma_lib(env, RxV, RsV, RtV, float_muladd_negate_product);
}

float64 HELPER(dfmpyfix)(CPUHexagonState *env, float64 RssV, float64 RttV)
{
    int64_t RddV;
    arch_fpop_start(env);
    if (float64_is_denormal(RssV) &&
        (float64_getexp(RttV) >= 512) &&
        float64_is_normal(RttV)) {
        RddV = float64_mul(RssV, make_float64(0x4330000000000000),
                           &env->fp_status);
    } else if (float64_is_denormal(RttV) &&
               (float64_getexp(RssV) >= 512) &&
               float64_is_normal(RssV)) {
        RddV = float64_mul(RssV, make_float64(0x3cb0000000000000),
                           &env->fp_status);
    } else {
        RddV = RssV;
    }
    arch_fpop_end(env);
    return RddV;
}

float64 HELPER(dfmpyhh)(CPUHexagonState *env, float64 RxxV,
                        float64 RssV, float64 RttV)
{
    arch_fpop_start(env);
    RxxV = internal_mpyhh(RssV, RttV, RxxV, &env->fp_status);
    arch_fpop_end(env);
    return RxxV;
}

/* Histogram instructions */

void HELPER(vhist)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int lane = 0; lane < 8; lane++) {
        for (int i = 0; i < sizeof(MMVector) / 8; ++i) {
            unsigned char value = input->ub[(sizeof(MMVector) / 8) * lane + i];
            unsigned char regno = value >> 3;
            unsigned char element = value & 7;

            env->VRegs[regno].uh[(sizeof(MMVector) / 16) * lane + element]++;
        }
    }
}

void HELPER(vhistq)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int lane = 0; lane < 8; lane++) {
        for (int i = 0; i < sizeof(MMVector) / 8; ++i) {
            unsigned char value = input->ub[(sizeof(MMVector) / 8) * lane + i];
            unsigned char regno = value >> 3;
            unsigned char element = value & 7;

            if (fGETQBIT(env->qtmp, sizeof(MMVector) / 8 * lane + i)) {
                env->VRegs[regno].uh[
                    (sizeof(MMVector) / 16) * lane + element]++;
            }
        }
    }
}

void HELPER(vwhist256)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 0) & (~7)) | ((bucket >> 0) & 7);

        env->VRegs[vindex].uh[elindex] =
            env->VRegs[vindex].uh[elindex] + weight;
    }
}

void HELPER(vwhist256q)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 0) & (~7)) | ((bucket >> 0) & 7);

        if (fGETQBIT(env->qtmp, 2 * i)) {
            env->VRegs[vindex].uh[elindex] =
                env->VRegs[vindex].uh[elindex] + weight;
        }
    }
}

void HELPER(vwhist256_sat)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 0) & (~7)) | ((bucket >> 0) & 7);

        env->VRegs[vindex].uh[elindex] =
            fVSATUH(env->VRegs[vindex].uh[elindex] + weight);
    }
}

void HELPER(vwhist256q_sat)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 0) & (~7)) | ((bucket >> 0) & 7);

        if (fGETQBIT(env->qtmp, 2 * i)) {
            env->VRegs[vindex].uh[elindex] =
                fVSATUH(env->VRegs[vindex].uh[elindex] + weight);
        }
    }
}

void HELPER(vwhist128)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 1) & (~3)) | ((bucket >> 1) & 3);

        env->VRegs[vindex].uw[elindex] =
            env->VRegs[vindex].uw[elindex] + weight;
    }
}

void HELPER(vwhist128q)(CPUHexagonState *env)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 1) & (~3)) | ((bucket >> 1) & 3);

        if (fGETQBIT(env->qtmp, 2 * i)) {
            env->VRegs[vindex].uw[elindex] =
                env->VRegs[vindex].uw[elindex] + weight;
        }
    }
}

void HELPER(vwhist128m)(CPUHexagonState *env, int32_t uiV)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 1) & (~3)) | ((bucket >> 1) & 3);

        if ((bucket & 1) == uiV) {
            env->VRegs[vindex].uw[elindex] =
                env->VRegs[vindex].uw[elindex] + weight;
        }
    }
}

void HELPER(vwhist128qm)(CPUHexagonState *env, int32_t uiV)
{
    MMVector *input = &env->tmp_VRegs[0];

    for (int i = 0; i < (sizeof(MMVector) / 2); i++) {
        unsigned int bucket = fGETUBYTE(0, input->h[i]);
        unsigned int weight = fGETUBYTE(1, input->h[i]);
        unsigned int vindex = (bucket >> 3) & 0x1F;
        unsigned int elindex = ((i >> 1) & (~3)) | ((bucket >> 1) & 3);

        if (((bucket & 1) == uiV) && fGETQBIT(env->qtmp, 2 * i)) {
            env->VRegs[vindex].uw[elindex] =
                env->VRegs[vindex].uw[elindex] + weight;
        }
    }
}

/* These macros can be referenced in the generated helper functions */
#define warn(...) /* Nothing */
#define fatal(...) g_assert_not_reached();

#define BOGUS_HELPER(tag) \
    printf("ERROR: bogus helper: " #tag "\n")

#include "helper_funcs_generated.c.inc"
