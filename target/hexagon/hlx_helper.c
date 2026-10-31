/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/helper-proto.h"
#include "hlx/hlx.h"
#include "hw/hexagon/hexagon_hlx.h"
#include "accel/tcg/probe.h"
#include "tcg/tcg-gvec-desc.h"

#define HLX_MEM_ALIGN_MASK (~(HLX_VEC_SIZE_BYTES - 1))

/* Align down to 2KB. Above and below are unused in V85. */
void HELPER(hlx_load_vec)(CPUHexagonState *env, void *dst_void,
                          target_ulong ea, uint32_t above, uint32_t below)
{
    HLXVector *dst = (HLXVector *)dst_void;
    target_ulong aligned_ea = ea & HLX_MEM_ALIGN_MASK;
    uintptr_t raddr = GETPC();
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);

    /* A 2KB-aligned access stays within one page. */
    void *host = probe_read(env, aligned_ea, HLX_VEC_SIZE_BYTES, mmu_idx,
                            raddr);

    if (host) {
        memcpy(dst, host, HLX_VEC_SIZE_BYTES);
    } else {
        for (int i = 0; i < HLX_VEC_SIZE_BYTES; i += 8) {
            uint64_t val = cpu_ldq_le_data_ra(env, aligned_ea + i, raddr);
            memcpy(&dst->ub[i], &val, 8);
        }
    }
}

void HELPER(hlx_store_vec)(CPUHexagonState *env, target_ulong ea,
                           void *src_void, uint32_t above, uint32_t below)
{
    target_ulong aligned_ea = ea & HLX_MEM_ALIGN_MASK;
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);

    probe_write(env, aligned_ea, HLX_VEC_SIZE_BYTES, mmu_idx, GETPC());
    memcpy(&env->hlx_store_data, src_void, sizeof(HLXVector));
    env->hlx_store_va = aligned_ea;
    env->hlx_store_pending = true;
}

void HELPER(hlx_commit_store)(CPUHexagonState *env)
{
    target_ulong va = env->hlx_store_va;
    uintptr_t raddr = GETPC();
    int mmu_idx = cpu_mmu_index(env_cpu(env), false);
    void *host;

    if (!env->hlx_store_pending) {
        return;
    }
    env->hlx_store_pending = false;

    host = probe_write(env, va, HLX_VEC_SIZE_BYTES, mmu_idx, raddr);
    if (host) {
        memcpy(host, &env->hlx_store_data, HLX_VEC_SIZE_BYTES);
    } else {
        for (int i = 0; i < HLX_VEC_SIZE_BYTES; i += 8) {
            uint64_t val;
            memcpy(&val, &env->hlx_store_data.ub[i], 8);
            cpu_stq_le_data_ra(env, va + i, val, raddr);
        }
    }
}

void HELPER(hlx_copy_live_to_future)(CPUHexagonState *env, uint32_t rnum)
{
    g_assert(env->hlx_ctx != NULL);
    memcpy(&env->future_LRegs[rnum], &env->hlx_ctx->LRegs[rnum],
           sizeof(HLXVector));
}

void HELPER(hlx_copy_live_to_tmp)(CPUHexagonState *env, uint32_t rnum)
{
    g_assert(env->hlx_ctx != NULL);
    memcpy(&env->tmp_LRegs[rnum], &env->hlx_ctx->LRegs[rnum],
           sizeof(HLXVector));
}

void HELPER(hlx_commit_reg)(CPUHexagonState *env, uint32_t rnum)
{
    g_assert(env->hlx_ctx != NULL);
    memcpy(&env->hlx_ctx->LRegs[rnum], &env->future_LRegs[rnum],
           sizeof(HLXVector));
}

/* HLX vectors exceed the gvec inline limit. */
void HELPER(hlx_gvec_ushsat_add16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int32_t r = (int32_t)(*(int16_t *)((char *)a + i))
                  + (int32_t)(*(int16_t *)((char *)b + i));
        if (r < 0) {
            r = 0;
        } else if (r > 0xFFFF) {
            r = 0xFFFF;
        }
        *(uint16_t *)((char *)d + i) = (uint16_t)r;
    }
}

void HELPER(hlx_gvec_ushsat_sub16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int32_t r = (int32_t)(*(int16_t *)((char *)a + i))
                  - (int32_t)(*(int16_t *)((char *)b + i));
        if (r < 0) {
            r = 0;
        } else if (r > 0xFFFF) {
            r = 0xFFFF;
        }
        *(uint16_t *)((char *)d + i) = (uint16_t)r;
    }
}

void HELPER(hlx_gvec_abs_uh_sat16)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t v = *(int16_t *)((char *)a + i);
        int32_t r = (v < 0) ? -(int32_t)v : (int32_t)v;
        if (r > INT16_MAX) {
            r = INT16_MAX;
        }
        *(int16_t *)((char *)d + i) = (int16_t)r;
    }
}

void HELPER(hlx_gvec_avg_sh16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t av = *(int16_t *)((char *)a + i);
        int16_t bv = *(int16_t *)((char *)b + i);
        int16_t r = (int16_t)((av & bv) + ((av ^ bv) >> 1));
        *(int16_t *)((char *)d + i) = r;
    }
}

void HELPER(hlx_gvec_avg_sh16_rnd)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t av = *(int16_t *)((char *)a + i);
        int16_t bv = *(int16_t *)((char *)b + i);
        int16_t r = (int16_t)((av | bv) - ((av ^ bv) >> 1));
        *(int16_t *)((char *)d + i) = r;
    }
}

void HELPER(hlx_gvec_avg_uh16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint16_t av = *(uint16_t *)((char *)a + i);
        uint16_t bv = *(uint16_t *)((char *)b + i);
        uint16_t r = (uint16_t)((av & bv) + ((av ^ bv) >> 1));
        *(uint16_t *)((char *)d + i) = r;
    }
}

void HELPER(hlx_gvec_avg_uh16_rnd)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint16_t av = *(uint16_t *)((char *)a + i);
        uint16_t bv = *(uint16_t *)((char *)b + i);
        uint16_t r = (uint16_t)((av | bv) - ((av ^ bv) >> 1));
        *(uint16_t *)((char *)d + i) = r;
    }
}

void HELPER(hlx_gvec_absdiff_sh16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t av = *(int16_t *)((char *)a + i);
        int16_t bv = *(int16_t *)((char *)b + i);
        int16_t mx = (av > bv) ? av : bv;
        int16_t mn = (av > bv) ? bv : av;
        *(uint16_t *)((char *)d + i) = (uint16_t)(mx - mn);
    }
}

void HELPER(hlx_gvec_absdiff_uh16)(void *d, void *a, void *b, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(uint16_t)) {
        uint16_t av = *(uint16_t *)((char *)a + i);
        uint16_t bv = *(uint16_t *)((char *)b + i);
        uint16_t mx = (av > bv) ? av : bv;
        uint16_t mn = (av > bv) ? bv : av;
        *(uint16_t *)((char *)d + i) = mx - mn;
    }
}

void HELPER(hlx_gvec_sari15_h16)(void *d, void *a, uint32_t desc)
{
    intptr_t oprsz = simd_oprsz(desc);
    intptr_t i;

    for (i = 0; i < oprsz; i += sizeof(int16_t)) {
        int16_t av = *(int16_t *)((char *)a + i);
        *(uint16_t *)((char *)d + i) = (uint16_t)(av >> 15);
    }
}
