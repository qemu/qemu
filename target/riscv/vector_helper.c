/*
 * RISC-V Vector Extension Helpers for QEMU.
 *
 * Copyright (c) 2020 T-Head Semiconductor Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/memop.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"
#include "internals.h"
#include <math.h>

target_ulong HELPER(vsetvl)(CPURISCVState *env, target_ulong s1,
                            target_ulong s2)
{
    int vlmax, vl;
    RISCVCPU *cpu = env_archcpu(env);
    uint16_t sew = 8 << FIELD_EX64(s2, VTYPE, VSEW);
    uint8_t ediv = FIELD_EX64(s2, VTYPE, VEDIV);
    bool vill = FIELD_EX64(s2, VTYPE, VILL);
    target_ulong reserved = FIELD_EX64(s2, VTYPE, RESERVED);

    if ((sew > cpu->cfg.elen) || vill || (ediv != 0) || (reserved != 0)) {
        /* only set vill bit. */
        env->vtype = FIELD_DP64(0, VTYPE, VILL, 1);
        env->vl = 0;
        env->vstart = 0;
        return 0;
    }

    vlmax = vext_get_vlmax(cpu, s2);
    if (s1 <= vlmax) {
        vl = s1;
    } else {
        vl = vlmax;
    }
    env->vl = vl;
    env->vtype = s2;
    env->vstart = 0;
    return vl;
}

/*
 * Note that vector data is stored in host-endian 64-bit chunks,
 * so addressing units smaller than that needs a host-endian fixup.
 */
#ifdef HOST_WORDS_BIGENDIAN
#define H1(x)   ((x) ^ 7)
#define H1_2(x) ((x) ^ 6)
#define H1_4(x) ((x) ^ 4)
#define H2(x)   ((x) ^ 3)
#define H4(x)   ((x) ^ 1)
#define H8(x)   ((x))
#else
#define H1(x)   (x)
#define H1_2(x) (x)
#define H1_4(x) (x)
#define H2(x)   (x)
#define H4(x)   (x)
#define H8(x)   (x)
#endif

static inline uint32_t vext_nf(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, NF);
}

static inline uint32_t vext_mlen(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, MLEN);
}

static inline uint32_t vext_vm(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, VM);
}

static inline uint32_t vext_lmul(uint32_t desc)
{
    return FIELD_EX32(simd_data(desc), VDATA, LMUL);
}

/*
 * Get vector group length in bytes. Its range is [64, 2048].
 *
 * As simd_desc support at most 256, the max vlen is 512 bits.
 * So vlen in bytes is encoded as maxsz.
 */
static inline uint32_t vext_maxsz(uint32_t desc)
{
    return simd_maxsz(desc) << vext_lmul(desc);
}

/*
 * This function checks watchpoint before real load operation.
 *
 * In softmmu mode, the TLB API probe_access is enough for watchpoint check.
 * In user mode, there is no watchpoint support now.
 *
 * It will trigger an exception if there is no mapping in TLB
 * and page table walk can't fill the TLB entry. Then the guest
 * software can return here after process the exception or never return.
 */
static void probe_pages(CPURISCVState *env, target_ulong addr,
                        target_ulong len, uintptr_t ra,
                        MMUAccessType access_type)
{
    target_ulong pagelen = -(addr | TARGET_PAGE_MASK);
    target_ulong curlen = MIN(pagelen, len);

    probe_access(env, addr, curlen, access_type,
                 cpu_mmu_index(env, false), ra);
    if (len > curlen) {
        addr += curlen;
        curlen = len - curlen;
        probe_access(env, addr, curlen, access_type,
                     cpu_mmu_index(env, false), ra);
    }
}

#ifdef HOST_WORDS_BIGENDIAN
static void vext_clear(void *tail, uint32_t cnt, uint32_t tot)
{
    /*
     * Split the remaining range to two parts.
     * The first part is in the last uint64_t unit.
     * The second part start from the next uint64_t unit.
     */
    int part1 = 0, part2 = tot - cnt;
    if (cnt % 8) {
        part1 = 8 - (cnt % 8);
        part2 = tot - cnt - part1;
        memset((void *)((uintptr_t)tail & ~(7ULL)), 0, part1);
        memset((void *)(((uintptr_t)tail + 8) & ~(7ULL)), 0, part2);
    } else {
        memset(tail, 0, part2);
    }
}
#else
static void vext_clear(void *tail, uint32_t cnt, uint32_t tot)
{
    memset(tail, 0, tot - cnt);
}
#endif

static void clearb(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int8_t *cur = ((int8_t *)vd + H1(idx));
    vext_clear(cur, cnt, tot);
}

static void clearh(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int16_t *cur = ((int16_t *)vd + H2(idx));
    vext_clear(cur, cnt, tot);
}

static void clearl(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int32_t *cur = ((int32_t *)vd + H4(idx));
    vext_clear(cur, cnt, tot);
}

static void clearq(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot)
{
    int64_t *cur = (int64_t *)vd + idx;
    vext_clear(cur, cnt, tot);
}


static inline int vext_elem_mask(void *v0, int mlen, int index)
{
    int idx = (index * mlen) / 64;
    int pos = (index * mlen) % 64;
    return (((uint64_t *)v0)[idx] >> pos) & 1;
}

/* elements operations for load and store */
typedef void vext_ldst_elem_fn(CPURISCVState *env, target_ulong addr,
                               uint32_t idx, void *vd, uintptr_t retaddr);
typedef void clear_fn(void *vd, uint32_t idx, uint32_t cnt, uint32_t tot);

#define GEN_VEXT_LD_ELEM(NAME, MTYPE, ETYPE, H, LDSUF)     \
static void NAME(CPURISCVState *env, abi_ptr addr,         \
                 uint32_t idx, void *vd, uintptr_t retaddr)\
{                                                          \
    MTYPE data;                                            \
    ETYPE *cur = ((ETYPE *)vd + H(idx));                   \
    data = cpu_##LDSUF##_data_ra(env, addr, retaddr);      \
    *cur = data;                                           \
}                                                          \

GEN_VEXT_LD_ELEM(ldb_b, int8_t,  int8_t,  H1, ldsb)
GEN_VEXT_LD_ELEM(ldb_h, int8_t,  int16_t, H2, ldsb)
GEN_VEXT_LD_ELEM(ldb_w, int8_t,  int32_t, H4, ldsb)
GEN_VEXT_LD_ELEM(ldb_d, int8_t,  int64_t, H8, ldsb)
GEN_VEXT_LD_ELEM(ldh_h, int16_t, int16_t, H2, ldsw)
GEN_VEXT_LD_ELEM(ldh_w, int16_t, int32_t, H4, ldsw)
GEN_VEXT_LD_ELEM(ldh_d, int16_t, int64_t, H8, ldsw)
GEN_VEXT_LD_ELEM(ldw_w, int32_t, int32_t, H4, ldl)
GEN_VEXT_LD_ELEM(ldw_d, int32_t, int64_t, H8, ldl)
GEN_VEXT_LD_ELEM(lde_b, int8_t,  int8_t,  H1, ldsb)
GEN_VEXT_LD_ELEM(lde_h, int16_t, int16_t, H2, ldsw)
GEN_VEXT_LD_ELEM(lde_w, int32_t, int32_t, H4, ldl)
GEN_VEXT_LD_ELEM(lde_d, int64_t, int64_t, H8, ldq)
GEN_VEXT_LD_ELEM(ldbu_b, uint8_t,  uint8_t,  H1, ldub)
GEN_VEXT_LD_ELEM(ldbu_h, uint8_t,  uint16_t, H2, ldub)
GEN_VEXT_LD_ELEM(ldbu_w, uint8_t,  uint32_t, H4, ldub)
GEN_VEXT_LD_ELEM(ldbu_d, uint8_t,  uint64_t, H8, ldub)
GEN_VEXT_LD_ELEM(ldhu_h, uint16_t, uint16_t, H2, lduw)
GEN_VEXT_LD_ELEM(ldhu_w, uint16_t, uint32_t, H4, lduw)
GEN_VEXT_LD_ELEM(ldhu_d, uint16_t, uint64_t, H8, lduw)
GEN_VEXT_LD_ELEM(ldwu_w, uint32_t, uint32_t, H4, ldl)
GEN_VEXT_LD_ELEM(ldwu_d, uint32_t, uint64_t, H8, ldl)

#define GEN_VEXT_ST_ELEM(NAME, ETYPE, H, STSUF)            \
static void NAME(CPURISCVState *env, abi_ptr addr,         \
                 uint32_t idx, void *vd, uintptr_t retaddr)\
{                                                          \
    ETYPE data = *((ETYPE *)vd + H(idx));                  \
    cpu_##STSUF##_data_ra(env, addr, data, retaddr);       \
}

GEN_VEXT_ST_ELEM(stb_b, int8_t,  H1, stb)
GEN_VEXT_ST_ELEM(stb_h, int16_t, H2, stb)
GEN_VEXT_ST_ELEM(stb_w, int32_t, H4, stb)
GEN_VEXT_ST_ELEM(stb_d, int64_t, H8, stb)
GEN_VEXT_ST_ELEM(sth_h, int16_t, H2, stw)
GEN_VEXT_ST_ELEM(sth_w, int32_t, H4, stw)
GEN_VEXT_ST_ELEM(sth_d, int64_t, H8, stw)
GEN_VEXT_ST_ELEM(stw_w, int32_t, H4, stl)
GEN_VEXT_ST_ELEM(stw_d, int64_t, H8, stl)
GEN_VEXT_ST_ELEM(ste_b, int8_t,  H1, stb)
GEN_VEXT_ST_ELEM(ste_h, int16_t, H2, stw)
GEN_VEXT_ST_ELEM(ste_w, int32_t, H4, stl)
GEN_VEXT_ST_ELEM(ste_d, int64_t, H8, stq)

/*
 *** stride: access vector element from strided memory
 */
static void
vext_ldst_stride(void *vd, void *v0, target_ulong base,
                 target_ulong stride, CPURISCVState *env,
                 uint32_t desc, uint32_t vm,
                 vext_ldst_elem_fn *ldst_elem, clear_fn *clear_elem,
                 uint32_t esz, uint32_t msz, uintptr_t ra,
                 MMUAccessType access_type)
{
    uint32_t i, k;
    uint32_t nf = vext_nf(desc);
    uint32_t mlen = vext_mlen(desc);
    uint32_t vlmax = vext_maxsz(desc) / esz;

    /* probe every access*/
    for (i = 0; i < env->vl; i++) {
        if (!vm && !vext_elem_mask(v0, mlen, i)) {
            continue;
        }
        probe_pages(env, base + stride * i, nf * msz, ra, access_type);
    }
    /* do real access */
    for (i = 0; i < env->vl; i++) {
        k = 0;
        if (!vm && !vext_elem_mask(v0, mlen, i)) {
            continue;
        }
        while (k < nf) {
            target_ulong addr = base + stride * i + k * msz;
            ldst_elem(env, addr, i + k * vlmax, vd, ra);
            k++;
        }
    }
    /* clear tail elements */
    if (clear_elem) {
        for (k = 0; k < nf; k++) {
            clear_elem(vd, env->vl + k * vlmax, env->vl * esz, vlmax * esz);
        }
    }
}

#define GEN_VEXT_LD_STRIDE(NAME, MTYPE, ETYPE, LOAD_FN, CLEAR_FN)       \
void HELPER(NAME)(void *vd, void * v0, target_ulong base,               \
                  target_ulong stride, CPURISCVState *env,              \
                  uint32_t desc)                                        \
{                                                                       \
    uint32_t vm = vext_vm(desc);                                        \
    vext_ldst_stride(vd, v0, base, stride, env, desc, vm, LOAD_FN,      \
                     CLEAR_FN, sizeof(ETYPE), sizeof(MTYPE),            \
                     GETPC(), MMU_DATA_LOAD);                           \
}

GEN_VEXT_LD_STRIDE(vlsb_v_b,  int8_t,   int8_t,   ldb_b,  clearb)
GEN_VEXT_LD_STRIDE(vlsb_v_h,  int8_t,   int16_t,  ldb_h,  clearh)
GEN_VEXT_LD_STRIDE(vlsb_v_w,  int8_t,   int32_t,  ldb_w,  clearl)
GEN_VEXT_LD_STRIDE(vlsb_v_d,  int8_t,   int64_t,  ldb_d,  clearq)
GEN_VEXT_LD_STRIDE(vlsh_v_h,  int16_t,  int16_t,  ldh_h,  clearh)
GEN_VEXT_LD_STRIDE(vlsh_v_w,  int16_t,  int32_t,  ldh_w,  clearl)
GEN_VEXT_LD_STRIDE(vlsh_v_d,  int16_t,  int64_t,  ldh_d,  clearq)
GEN_VEXT_LD_STRIDE(vlsw_v_w,  int32_t,  int32_t,  ldw_w,  clearl)
GEN_VEXT_LD_STRIDE(vlsw_v_d,  int32_t,  int64_t,  ldw_d,  clearq)
GEN_VEXT_LD_STRIDE(vlse_v_b,  int8_t,   int8_t,   lde_b,  clearb)
GEN_VEXT_LD_STRIDE(vlse_v_h,  int16_t,  int16_t,  lde_h,  clearh)
GEN_VEXT_LD_STRIDE(vlse_v_w,  int32_t,  int32_t,  lde_w,  clearl)
GEN_VEXT_LD_STRIDE(vlse_v_d,  int64_t,  int64_t,  lde_d,  clearq)
GEN_VEXT_LD_STRIDE(vlsbu_v_b, uint8_t,  uint8_t,  ldbu_b, clearb)
GEN_VEXT_LD_STRIDE(vlsbu_v_h, uint8_t,  uint16_t, ldbu_h, clearh)
GEN_VEXT_LD_STRIDE(vlsbu_v_w, uint8_t,  uint32_t, ldbu_w, clearl)
GEN_VEXT_LD_STRIDE(vlsbu_v_d, uint8_t,  uint64_t, ldbu_d, clearq)
GEN_VEXT_LD_STRIDE(vlshu_v_h, uint16_t, uint16_t, ldhu_h, clearh)
GEN_VEXT_LD_STRIDE(vlshu_v_w, uint16_t, uint32_t, ldhu_w, clearl)
GEN_VEXT_LD_STRIDE(vlshu_v_d, uint16_t, uint64_t, ldhu_d, clearq)
GEN_VEXT_LD_STRIDE(vlswu_v_w, uint32_t, uint32_t, ldwu_w, clearl)
GEN_VEXT_LD_STRIDE(vlswu_v_d, uint32_t, uint64_t, ldwu_d, clearq)

#define GEN_VEXT_ST_STRIDE(NAME, MTYPE, ETYPE, STORE_FN)                \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,                \
                  target_ulong stride, CPURISCVState *env,              \
                  uint32_t desc)                                        \
{                                                                       \
    uint32_t vm = vext_vm(desc);                                        \
    vext_ldst_stride(vd, v0, base, stride, env, desc, vm, STORE_FN,     \
                     NULL, sizeof(ETYPE), sizeof(MTYPE),                \
                     GETPC(), MMU_DATA_STORE);                          \
}

GEN_VEXT_ST_STRIDE(vssb_v_b, int8_t,  int8_t,  stb_b)
GEN_VEXT_ST_STRIDE(vssb_v_h, int8_t,  int16_t, stb_h)
GEN_VEXT_ST_STRIDE(vssb_v_w, int8_t,  int32_t, stb_w)
GEN_VEXT_ST_STRIDE(vssb_v_d, int8_t,  int64_t, stb_d)
GEN_VEXT_ST_STRIDE(vssh_v_h, int16_t, int16_t, sth_h)
GEN_VEXT_ST_STRIDE(vssh_v_w, int16_t, int32_t, sth_w)
GEN_VEXT_ST_STRIDE(vssh_v_d, int16_t, int64_t, sth_d)
GEN_VEXT_ST_STRIDE(vssw_v_w, int32_t, int32_t, stw_w)
GEN_VEXT_ST_STRIDE(vssw_v_d, int32_t, int64_t, stw_d)
GEN_VEXT_ST_STRIDE(vsse_v_b, int8_t,  int8_t,  ste_b)
GEN_VEXT_ST_STRIDE(vsse_v_h, int16_t, int16_t, ste_h)
GEN_VEXT_ST_STRIDE(vsse_v_w, int32_t, int32_t, ste_w)
GEN_VEXT_ST_STRIDE(vsse_v_d, int64_t, int64_t, ste_d)

/*
 *** unit-stride: access elements stored contiguously in memory
 */

/* unmasked unit-stride load and store operation*/
static void
vext_ldst_us(void *vd, target_ulong base, CPURISCVState *env, uint32_t desc,
             vext_ldst_elem_fn *ldst_elem, clear_fn *clear_elem,
             uint32_t esz, uint32_t msz, uintptr_t ra,
             MMUAccessType access_type)
{
    uint32_t i, k;
    uint32_t nf = vext_nf(desc);
    uint32_t vlmax = vext_maxsz(desc) / esz;

    /* probe every access */
    probe_pages(env, base, env->vl * nf * msz, ra, access_type);
    /* load bytes from guest memory */
    for (i = 0; i < env->vl; i++) {
        k = 0;
        while (k < nf) {
            target_ulong addr = base + (i * nf + k) * msz;
            ldst_elem(env, addr, i + k * vlmax, vd, ra);
            k++;
        }
    }
    /* clear tail elements */
    if (clear_elem) {
        for (k = 0; k < nf; k++) {
            clear_elem(vd, env->vl + k * vlmax, env->vl * esz, vlmax * esz);
        }
    }
}

/*
 * masked unit-stride load and store operation will be a special case of stride,
 * stride = NF * sizeof (MTYPE)
 */

#define GEN_VEXT_LD_US(NAME, MTYPE, ETYPE, LOAD_FN, CLEAR_FN)           \
void HELPER(NAME##_mask)(void *vd, void *v0, target_ulong base,         \
                         CPURISCVState *env, uint32_t desc)             \
{                                                                       \
    uint32_t stride = vext_nf(desc) * sizeof(MTYPE);                    \
    vext_ldst_stride(vd, v0, base, stride, env, desc, false, LOAD_FN,   \
                     CLEAR_FN, sizeof(ETYPE), sizeof(MTYPE),            \
                     GETPC(), MMU_DATA_LOAD);                           \
}                                                                       \
                                                                        \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,                \
                  CPURISCVState *env, uint32_t desc)                    \
{                                                                       \
    vext_ldst_us(vd, base, env, desc, LOAD_FN, CLEAR_FN,                \
                 sizeof(ETYPE), sizeof(MTYPE), GETPC(), MMU_DATA_LOAD); \
}

GEN_VEXT_LD_US(vlb_v_b,  int8_t,   int8_t,   ldb_b,  clearb)
GEN_VEXT_LD_US(vlb_v_h,  int8_t,   int16_t,  ldb_h,  clearh)
GEN_VEXT_LD_US(vlb_v_w,  int8_t,   int32_t,  ldb_w,  clearl)
GEN_VEXT_LD_US(vlb_v_d,  int8_t,   int64_t,  ldb_d,  clearq)
GEN_VEXT_LD_US(vlh_v_h,  int16_t,  int16_t,  ldh_h,  clearh)
GEN_VEXT_LD_US(vlh_v_w,  int16_t,  int32_t,  ldh_w,  clearl)
GEN_VEXT_LD_US(vlh_v_d,  int16_t,  int64_t,  ldh_d,  clearq)
GEN_VEXT_LD_US(vlw_v_w,  int32_t,  int32_t,  ldw_w,  clearl)
GEN_VEXT_LD_US(vlw_v_d,  int32_t,  int64_t,  ldw_d,  clearq)
GEN_VEXT_LD_US(vle_v_b,  int8_t,   int8_t,   lde_b,  clearb)
GEN_VEXT_LD_US(vle_v_h,  int16_t,  int16_t,  lde_h,  clearh)
GEN_VEXT_LD_US(vle_v_w,  int32_t,  int32_t,  lde_w,  clearl)
GEN_VEXT_LD_US(vle_v_d,  int64_t,  int64_t,  lde_d,  clearq)
GEN_VEXT_LD_US(vlbu_v_b, uint8_t,  uint8_t,  ldbu_b, clearb)
GEN_VEXT_LD_US(vlbu_v_h, uint8_t,  uint16_t, ldbu_h, clearh)
GEN_VEXT_LD_US(vlbu_v_w, uint8_t,  uint32_t, ldbu_w, clearl)
GEN_VEXT_LD_US(vlbu_v_d, uint8_t,  uint64_t, ldbu_d, clearq)
GEN_VEXT_LD_US(vlhu_v_h, uint16_t, uint16_t, ldhu_h, clearh)
GEN_VEXT_LD_US(vlhu_v_w, uint16_t, uint32_t, ldhu_w, clearl)
GEN_VEXT_LD_US(vlhu_v_d, uint16_t, uint64_t, ldhu_d, clearq)
GEN_VEXT_LD_US(vlwu_v_w, uint32_t, uint32_t, ldwu_w, clearl)
GEN_VEXT_LD_US(vlwu_v_d, uint32_t, uint64_t, ldwu_d, clearq)

#define GEN_VEXT_ST_US(NAME, MTYPE, ETYPE, STORE_FN)                    \
void HELPER(NAME##_mask)(void *vd, void *v0, target_ulong base,         \
                         CPURISCVState *env, uint32_t desc)             \
{                                                                       \
    uint32_t stride = vext_nf(desc) * sizeof(MTYPE);                    \
    vext_ldst_stride(vd, v0, base, stride, env, desc, false, STORE_FN,  \
                     NULL, sizeof(ETYPE), sizeof(MTYPE),                \
                     GETPC(), MMU_DATA_STORE);                          \
}                                                                       \
                                                                        \
void HELPER(NAME)(void *vd, void *v0, target_ulong base,                \
                  CPURISCVState *env, uint32_t desc)                    \
{                                                                       \
    vext_ldst_us(vd, base, env, desc, STORE_FN, NULL,                   \
                 sizeof(ETYPE), sizeof(MTYPE), GETPC(), MMU_DATA_STORE);\
}

GEN_VEXT_ST_US(vsb_v_b, int8_t,  int8_t , stb_b)
GEN_VEXT_ST_US(vsb_v_h, int8_t,  int16_t, stb_h)
GEN_VEXT_ST_US(vsb_v_w, int8_t,  int32_t, stb_w)
GEN_VEXT_ST_US(vsb_v_d, int8_t,  int64_t, stb_d)
GEN_VEXT_ST_US(vsh_v_h, int16_t, int16_t, sth_h)
GEN_VEXT_ST_US(vsh_v_w, int16_t, int32_t, sth_w)
GEN_VEXT_ST_US(vsh_v_d, int16_t, int64_t, sth_d)
GEN_VEXT_ST_US(vsw_v_w, int32_t, int32_t, stw_w)
GEN_VEXT_ST_US(vsw_v_d, int32_t, int64_t, stw_d)
GEN_VEXT_ST_US(vse_v_b, int8_t,  int8_t , ste_b)
GEN_VEXT_ST_US(vse_v_h, int16_t, int16_t, ste_h)
GEN_VEXT_ST_US(vse_v_w, int32_t, int32_t, ste_w)
GEN_VEXT_ST_US(vse_v_d, int64_t, int64_t, ste_d)
