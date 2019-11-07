/*
 *  Software MMU support
 *
 * Generate inline load/store functions for one MMU mode and data
 * size.
 *
 * Generate a store function as well as signed and unsigned loads.
 *
 * Not used directly but included from cpu_ldst.h.
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

#if !defined(SOFTMMU_CODE_ACCESS)
#include "trace-root.h"
#endif

#include "qemu/plugin.h"
#include "trace/mem.h"

#if DATA_SIZE == 8
#define SUFFIX q
#define USUFFIX q
#define DATA_TYPE uint64_t
#define SHIFT 3
#elif DATA_SIZE == 4
#define SUFFIX l
#define USUFFIX l
#define DATA_TYPE uint32_t
#define SHIFT 2
#elif DATA_SIZE == 2
#define SUFFIX w
#define USUFFIX uw
#define DATA_TYPE uint16_t
#define DATA_STYPE int16_t
#define SHIFT 1
#elif DATA_SIZE == 1
#define SUFFIX b
#define USUFFIX ub
#define DATA_TYPE uint8_t
#define DATA_STYPE int8_t
#define SHIFT 0
#else
#error unsupported data size
#endif

#if DATA_SIZE == 8
#define RES_TYPE uint64_t
#else
#define RES_TYPE uint32_t
#endif

#ifdef SOFTMMU_CODE_ACCESS
#define ADDR_READ addr_code
#define MMUSUFFIX _cmmu
#define URETSUFFIX USUFFIX
#define SRETSUFFIX glue(s, SUFFIX)
#else
#define ADDR_READ addr_read
#define MMUSUFFIX _mmu
#define URETSUFFIX USUFFIX
#define SRETSUFFIX glue(s, SUFFIX)
#endif

/* generic load/store macros */

static inline RES_TYPE
glue(glue(glue(cpu_ld, USUFFIX), MEMSUFFIX), _ra)(CPUArchState *env,
                                                  target_ulong ptr,
                                                  uintptr_t retaddr)
{
    CPUTLBEntry *entry;
    RES_TYPE res;
    target_ulong addr;
    int mmu_idx = CPU_MMU_INDEX;
    TCGMemOpIdx oi;
#if !defined(SOFTMMU_CODE_ACCESS)
    uint16_t meminfo = trace_mem_build_info(SHIFT, false, MO_TE, false, mmu_idx);
    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
#endif

    addr = ptr;
    entry = tlb_entry(env, mmu_idx, addr);
    if (unlikely(entry->ADDR_READ !=
                 (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))))) {
        oi = make_memop_idx(SHIFT, mmu_idx);
        res = glue(glue(helper_ret_ld, URETSUFFIX), MMUSUFFIX)(env, addr,
                                                            oi, retaddr);
    } else {
        uintptr_t hostaddr = addr + entry->addend;
        res = glue(glue(ld, USUFFIX), _p)((uint8_t *)hostaddr);
    }
#ifndef SOFTMMU_CODE_ACCESS
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
#endif
    return res;
}

static inline RES_TYPE
glue(glue(cpu_ld, USUFFIX), MEMSUFFIX)(CPUArchState *env, target_ulong ptr)
{
    return glue(glue(glue(cpu_ld, USUFFIX), MEMSUFFIX), _ra)(env, ptr, 0);
}

#if DATA_SIZE <= 2
static inline int
glue(glue(glue(cpu_lds, SUFFIX), MEMSUFFIX), _ra)(CPUArchState *env,
                                                  target_ulong ptr,
                                                  uintptr_t retaddr)
{
    CPUTLBEntry *entry;
    int res;
    target_ulong addr;
    int mmu_idx = CPU_MMU_INDEX;
    TCGMemOpIdx oi;
#if !defined(SOFTMMU_CODE_ACCESS)
    uint16_t meminfo = trace_mem_build_info(SHIFT, true, MO_TE, false, mmu_idx);
    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
#endif

    addr = ptr;
    entry = tlb_entry(env, mmu_idx, addr);
    if (unlikely(entry->ADDR_READ !=
                 (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))))) {
        oi = make_memop_idx(SHIFT, mmu_idx);
        res = (DATA_STYPE)glue(glue(helper_ret_ld, SRETSUFFIX),
                               MMUSUFFIX)(env, addr, oi, retaddr);
    } else {
        uintptr_t hostaddr = addr + entry->addend;
        res = glue(glue(lds, SUFFIX), _p)((uint8_t *)hostaddr);
    }
#ifndef SOFTMMU_CODE_ACCESS
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
#endif
    return res;
}

static inline int
glue(glue(cpu_lds, SUFFIX), MEMSUFFIX)(CPUArchState *env, target_ulong ptr)
{
    return glue(glue(glue(cpu_lds, SUFFIX), MEMSUFFIX), _ra)(env, ptr, 0);
}
#endif

#ifndef SOFTMMU_CODE_ACCESS

/* generic store macro */

static inline void
glue(glue(glue(cpu_st, SUFFIX), MEMSUFFIX), _ra)(CPUArchState *env,
                                                 target_ulong ptr,
                                                 RES_TYPE v, uintptr_t retaddr)
{
    CPUTLBEntry *entry;
    target_ulong addr;
    int mmu_idx = CPU_MMU_INDEX;
    TCGMemOpIdx oi;
#if !defined(SOFTMMU_CODE_ACCESS)
    uint16_t meminfo = trace_mem_build_info(SHIFT, false, MO_TE, true, mmu_idx);
    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
#endif

    addr = ptr;
    entry = tlb_entry(env, mmu_idx, addr);
    if (unlikely(tlb_addr_write(entry) !=
                 (addr & (TARGET_PAGE_MASK | (DATA_SIZE - 1))))) {
        oi = make_memop_idx(SHIFT, mmu_idx);
        glue(glue(helper_ret_st, SUFFIX), MMUSUFFIX)(env, addr, v, oi,
                                                     retaddr);
    } else {
        uintptr_t hostaddr = addr + entry->addend;
        glue(glue(st, SUFFIX), _p)((uint8_t *)hostaddr, v);
    }
#ifndef SOFTMMU_CODE_ACCESS
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
#endif
}

static inline void
glue(glue(cpu_st, SUFFIX), MEMSUFFIX)(CPUArchState *env, target_ulong ptr,
                                      RES_TYPE v)
{
    glue(glue(glue(cpu_st, SUFFIX), MEMSUFFIX), _ra)(env, ptr, v, 0);
}

#endif /* !SOFTMMU_CODE_ACCESS */

#undef RES_TYPE
#undef DATA_TYPE
#undef DATA_STYPE
#undef SUFFIX
#undef USUFFIX
#undef DATA_SIZE
#undef MMUSUFFIX
#undef ADDR_READ
#undef URETSUFFIX
#undef SRETSUFFIX
#undef SHIFT
