/*
 *  User-only accessor function support
 *
 * Generate inline load/store functions for one data size.
 *
 * Generate a store function as well as signed and unsigned loads.
 *
 * Not used directly but included from cpu_ldst.h.
 *
 *  Copyright (c) 2015 Linaro Limited
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

#if !defined(CODE_ACCESS)
#include "trace-root.h"
#endif

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

static inline RES_TYPE
glue(glue(cpu_ld, USUFFIX), MEMSUFFIX)(CPUArchState *env, abi_ptr ptr)
{
    RES_TYPE ret;
#ifdef CODE_ACCESS
    set_helper_retaddr(1);
    ret = glue(glue(ld, USUFFIX), _p)(g2h(ptr));
    clear_helper_retaddr();
#else
    uint16_t meminfo = trace_mem_build_info(SHIFT, false, MO_TE, false,
                                            MMU_USER_IDX);
    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = glue(glue(ld, USUFFIX), _p)(g2h(ptr));
#endif
    return ret;
}

#ifndef CODE_ACCESS
static inline RES_TYPE
glue(glue(glue(cpu_ld, USUFFIX), MEMSUFFIX), _ra)(CPUArchState *env,
                                                  abi_ptr ptr,
                                                  uintptr_t retaddr)
{
    RES_TYPE ret;
    set_helper_retaddr(retaddr);
    ret = glue(glue(cpu_ld, USUFFIX), MEMSUFFIX)(env, ptr);
    clear_helper_retaddr();
    return ret;
}
#endif

#if DATA_SIZE <= 2
static inline int
glue(glue(cpu_lds, SUFFIX), MEMSUFFIX)(CPUArchState *env, abi_ptr ptr)
{
    int ret;
#ifdef CODE_ACCESS
    set_helper_retaddr(1);
    ret = glue(glue(lds, SUFFIX), _p)(g2h(ptr));
    clear_helper_retaddr();
#else
    uint16_t meminfo = trace_mem_build_info(SHIFT, true, MO_TE, false,
                                            MMU_USER_IDX);
    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    ret = glue(glue(lds, SUFFIX), _p)(g2h(ptr));
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
#endif
    return ret;
}

#ifndef CODE_ACCESS
static inline int
glue(glue(glue(cpu_lds, SUFFIX), MEMSUFFIX), _ra)(CPUArchState *env,
                                                  abi_ptr ptr,
                                                  uintptr_t retaddr)
{
    int ret;
    set_helper_retaddr(retaddr);
    ret = glue(glue(cpu_lds, SUFFIX), MEMSUFFIX)(env, ptr);
    clear_helper_retaddr();
    return ret;
}
#endif /* CODE_ACCESS */
#endif /* DATA_SIZE <= 2 */

#ifndef CODE_ACCESS
static inline void
glue(glue(cpu_st, SUFFIX), MEMSUFFIX)(CPUArchState *env, abi_ptr ptr,
                                      RES_TYPE v)
{
    uint16_t meminfo = trace_mem_build_info(SHIFT, false, MO_TE, true,
                                            MMU_USER_IDX);
    trace_guest_mem_before_exec(env_cpu(env), ptr, meminfo);
    glue(glue(st, SUFFIX), _p)(g2h(ptr), v);
    qemu_plugin_vcpu_mem_cb(env_cpu(env), ptr, meminfo);
}

static inline void
glue(glue(glue(cpu_st, SUFFIX), MEMSUFFIX), _ra)(CPUArchState *env,
                                                  abi_ptr ptr,
                                                  RES_TYPE v,
                                                  uintptr_t retaddr)
{
    set_helper_retaddr(retaddr);
    glue(glue(cpu_st, SUFFIX), MEMSUFFIX)(env, ptr, v);
    clear_helper_retaddr();
}
#endif

#undef RES_TYPE
#undef DATA_TYPE
#undef DATA_STYPE
#undef SUFFIX
#undef USUFFIX
#undef DATA_SIZE
#undef SHIFT
