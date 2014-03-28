/*
 *  Software MMU support
 *
 * Generate inline load/store functions for one MMU mode and data
 * size.
 *
 * Generate a store function as well as signed and unsigned loads. For
 * 32 and 64 bit cases, also generate floating point functions with
 * the same size.
 *
 * Not used directly but included from softmmu_exec.h and exec-all.h.
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#if DATA_SIZE == 8
#define SUFFIX q
#define USUFFIX q
#define DATA_TYPE uint64_t
#elif DATA_SIZE == 4
#define SUFFIX l
#define USUFFIX l
#define DATA_TYPE uint32_t
#elif DATA_SIZE == 2
#define SUFFIX w
#define USUFFIX uw
#define DATA_TYPE uint16_t
#define DATA_STYPE int16_t
#elif DATA_SIZE == 1
#define SUFFIX b
#define USUFFIX ub
#define DATA_TYPE uint8_t
#define DATA_STYPE int8_t
#else
#error unsupported data size
#endif

#if ACCESS_TYPE < (NB_MMU_MODES)

#define CPU_MMU_INDEX ACCESS_TYPE
#define MMUSUFFIX _mmu

#elif ACCESS_TYPE == (NB_MMU_MODES)

#define CPU_MMU_INDEX (cpu_mmu_index(env))
#define MMUSUFFIX _mmu

#elif ACCESS_TYPE == (NB_MMU_MODES + 1)

#define CPU_MMU_INDEX (cpu_mmu_index(env))
#define MMUSUFFIX _cmmu

#else
#error invalid ACCESS_TYPE
#endif

#if DATA_SIZE == 8
#define RES_TYPE uint64_t
#else
#define RES_TYPE uint32_t
#endif

#ifdef SOFTMMU_CODE_ACCESS
#define ADDR_READ addr_code
#else
#define ADDR_READ addr_read
#endif

/* generic load/store macros */

static inline RES_TYPE
glue(glue(cpu_ld, USUFFIX), MEMSUFFIX)(CPUArchState *env, target_ulong ptr)
{
    int page_index;
    RES_TYPE res;
    target_ulong addr;
    int mmu_idx;

    addr = ptr;
    page_index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    mmu_idx = CPU_MMU_INDEX;
    if (unlikely(env->tlb_table[mmu_idx][page_index].ADDR_READ !=
                 (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))))) {
        res = glue(glue(helper_ld, SUFFIX), MMUSUFFIX)(env, addr, mmu_idx);
    } else {
        uintptr_t hostaddr = addr + env->tlb_table[mmu_idx][page_index].addend;
        res = glue(glue(ld, USUFFIX), _raw)(hostaddr);
    }
    return res;
}

#if DATA_SIZE <= 2
static inline int
glue(glue(cpu_lds, SUFFIX), MEMSUFFIX)(CPUArchState *env, target_ulong ptr)
{
    int res, page_index;
    target_ulong addr;
    int mmu_idx;

    addr = ptr;
    page_index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    mmu_idx = CPU_MMU_INDEX;
    if (unlikely(env->tlb_table[mmu_idx][page_index].ADDR_READ !=
                 (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))))) {
        res = (DATA_STYPE)glue(glue(helper_ld, SUFFIX),
                               MMUSUFFIX)(env, addr, mmu_idx);
    } else {
        uintptr_t hostaddr = addr + env->tlb_table[mmu_idx][page_index].addend;
        res = glue(glue(lds, SUFFIX), _raw)(hostaddr);
    }
    return res;
}
#endif

#ifndef SOFTMMU_CODE_ACCESS

/* generic store macro */

static inline void
glue(glue(cpu_st, SUFFIX), MEMSUFFIX)(CPUArchState *env, target_ulong ptr,
                                      RES_TYPE v)
{
    int page_index;
    target_ulong addr;
    int mmu_idx;

    addr = ptr;
    page_index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    mmu_idx = CPU_MMU_INDEX;
    if (unlikely(env->tlb_table[mmu_idx][page_index].addr_write !=
                 (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))))) {
        glue(glue(helper_st, SUFFIX), MMUSUFFIX)(env, addr, v, mmu_idx);
    } else {
        uintptr_t hostaddr = addr + env->tlb_table[mmu_idx][page_index].addend;
        glue(glue(st, SUFFIX), _raw)(hostaddr, v);
    }
}



#if DATA_SIZE == 8
static inline float64 glue(cpu_ldfq, MEMSUFFIX)(CPUArchState *env,
                                                target_ulong ptr)
{
    union {
        float64 d;
        uint64_t i;
    } u;
    u.i = glue(cpu_ldq, MEMSUFFIX)(env, ptr);
    return u.d;
}

static inline void glue(cpu_stfq, MEMSUFFIX)(CPUArchState *env,
                                             target_ulong ptr, float64 v)
{
    union {
        float64 d;
        uint64_t i;
    } u;
    u.d = v;
    glue(cpu_stq, MEMSUFFIX)(env, ptr, u.i);
}
#endif /* DATA_SIZE == 8 */

#if DATA_SIZE == 4
static inline float32 glue(cpu_ldfl, MEMSUFFIX)(CPUArchState *env,
                                                target_ulong ptr)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.i = glue(cpu_ldl, MEMSUFFIX)(env, ptr);
    return u.f;
}

static inline void glue(cpu_stfl, MEMSUFFIX)(CPUArchState *env,
                                             target_ulong ptr, float32 v)
{
    union {
        float32 f;
        uint32_t i;
    } u;
    u.f = v;
    glue(cpu_stl, MEMSUFFIX)(env, ptr, u.i);
}
#endif /* DATA_SIZE == 4 */

#endif /* !SOFTMMU_CODE_ACCESS */

#undef RES_TYPE
#undef DATA_TYPE
#undef DATA_STYPE
#undef SUFFIX
#undef USUFFIX
#undef DATA_SIZE
#undef CPU_MMU_INDEX
#undef MMUSUFFIX
#undef ADDR_READ
