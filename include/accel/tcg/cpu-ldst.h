/*
 *  Software MMU support (per-target)
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
 *
 */

/*
 * Generate inline load/store functions for all MMU modes (typically
 * at least _user and _kernel) as well as _data versions, for all data
 * sizes.
 *
 * Used by target op helpers.
 *
 * The syntax for the accessors is:
 *
 * load:  cpu_ld{sign}{size}{end}_{mmusuffix}(env, ptr)
 *        cpu_ld{sign}{size}{end}_{mmusuffix}_ra(env, ptr, retaddr)
 *        cpu_ld{sign}{size}{end}_mmuidx_ra(env, ptr, mmu_idx, retaddr)
 *        cpu_ld{sign}{size}{end}_mmu(env, ptr, oi, retaddr)
 *
 * store: cpu_st{size}{end}_{mmusuffix}(env, ptr, val)
 *        cpu_st{size}{end}_{mmusuffix}_ra(env, ptr, val, retaddr)
 *        cpu_st{size}{end}_mmuidx_ra(env, ptr, val, mmu_idx, retaddr)
 *        cpu_st{size}{end}_mmu(env, ptr, val, oi, retaddr)
 *
 * sign is:
 * (empty): for 32 and 64 bit sizes
 *   u    : unsigned
 *   s    : signed
 *
 * size is:
 *   b: 8 bits
 *   w: 16 bits
 *   l: 32 bits
 *   q: 64 bits
 *
 * end is:
 * (empty): for target native endian, or for 8 bit access
 *     _be: for forced big endian
 *     _le: for forced little endian
 *
 * mmusuffix is one of the generic suffixes "data" or "code", or "mmuidx".
 * The "mmuidx" suffix carries an extra mmu_idx argument that specifies
 * the index to use; the "data" and "code" suffixes take the index from
 * cpu_mmu_index().
 *
 * The "mmu" suffix carries the full MemOpIdx, with both mmu_idx and the
 * MemOp including alignment requirements.  The alignment will be enforced.
 */
#ifndef ACCEL_TCG_CPU_LDST_H
#define ACCEL_TCG_CPU_LDST_H

#ifndef CONFIG_TCG
#error Can only include this header with TCG
#endif

#include "exec/cpu-common.h"
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/abi_ptr.h"

#if defined(CONFIG_USER_ONLY)
#include "user/guest-host.h"
#endif /* CONFIG_USER_ONLY */

static inline uint32_t
cpu_ldub_mmuidx_ra(CPUArchState *env, abi_ptr addr, int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);
    return cpu_ldb_mmu(env, addr, oi, ra);
}

static inline int
cpu_ldsb_mmuidx_ra(CPUArchState *env, abi_ptr addr, int mmu_idx, uintptr_t ra)
{
    return (int8_t)cpu_ldub_mmuidx_ra(env, addr, mmu_idx, ra);
}

static inline uint32_t
cpu_lduw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                      int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUW | MO_UNALN, mmu_idx);
    return cpu_ldw_mmu(env, addr, oi, ra);
}

static inline int
cpu_ldsw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                      int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_lduw_be_mmuidx_ra(env, addr, mmu_idx, ra);
}

static inline uint32_t
cpu_ldl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUL | MO_UNALN, mmu_idx);
    return cpu_ldl_mmu(env, addr, oi, ra);
}

static inline uint64_t
cpu_ldq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUQ | MO_UNALN, mmu_idx);
    return cpu_ldq_mmu(env, addr, oi, ra);
}

static inline uint32_t
cpu_lduw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                      int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUW | MO_UNALN, mmu_idx);
    return cpu_ldw_mmu(env, addr, oi, ra);
}

static inline int
cpu_ldsw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                      int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_lduw_le_mmuidx_ra(env, addr, mmu_idx, ra);
}

static inline uint32_t
cpu_ldl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUL | MO_UNALN, mmu_idx);
    return cpu_ldl_mmu(env, addr, oi, ra);
}

static inline uint64_t
cpu_ldq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUQ | MO_UNALN, mmu_idx);
    return cpu_ldq_mmu(env, addr, oi, ra);
}

static inline void
cpu_stb_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                  int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);
    cpu_stb_mmu(env, addr, val, oi, ra);
}

static inline void
cpu_stw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUW | MO_UNALN, mmu_idx);
    cpu_stw_mmu(env, addr, val, oi, ra);
}

static inline void
cpu_stl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUL | MO_UNALN, mmu_idx);
    cpu_stl_mmu(env, addr, val, oi, ra);
}

static inline void
cpu_stq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUQ | MO_UNALN, mmu_idx);
    cpu_stq_mmu(env, addr, val, oi, ra);
}

static inline void
cpu_stw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUW | MO_UNALN, mmu_idx);
    cpu_stw_mmu(env, addr, val, oi, ra);
}

static inline void
cpu_stl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUL | MO_UNALN, mmu_idx);
    cpu_stl_mmu(env, addr, val, oi, ra);
}

static inline void
cpu_stq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                     int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUQ | MO_UNALN, mmu_idx);
    cpu_stq_mmu(env, addr, val, oi, ra);
}

/*--------------------------*/

static inline uint32_t
cpu_ldub_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    return cpu_ldub_mmuidx_ra(env, addr, mmu_index, ra);
}

static inline int
cpu_ldsb_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int8_t)cpu_ldub_data_ra(env, addr, ra);
}

static inline uint32_t
cpu_lduw_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    return cpu_lduw_be_mmuidx_ra(env, addr, mmu_index, ra);
}

static inline int
cpu_ldsw_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int16_t)cpu_lduw_be_data_ra(env, addr, ra);
}

static inline uint32_t
cpu_ldl_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    return cpu_ldl_be_mmuidx_ra(env, addr, mmu_index, ra);
}

static inline uint64_t
cpu_ldq_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    return cpu_ldq_be_mmuidx_ra(env, addr, mmu_index, ra);
}

static inline uint32_t
cpu_lduw_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    return cpu_lduw_le_mmuidx_ra(env, addr, mmu_index, ra);
}

static inline int
cpu_ldsw_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int16_t)cpu_lduw_le_data_ra(env, addr, ra);
}

static inline uint32_t
cpu_ldl_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    return cpu_ldl_le_mmuidx_ra(env, addr, mmu_index, ra);
}

static inline uint64_t
cpu_ldq_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    return cpu_ldq_le_mmuidx_ra(env, addr, mmu_index, ra);
}

static inline void
cpu_stb_data_ra(CPUArchState *env, abi_ptr addr, uint32_t val, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    cpu_stb_mmuidx_ra(env, addr, val, mmu_index, ra);
}

static inline void
cpu_stw_be_data_ra(CPUArchState *env, abi_ptr addr, uint32_t val, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    cpu_stw_be_mmuidx_ra(env, addr, val, mmu_index, ra);
}

static inline void
cpu_stl_be_data_ra(CPUArchState *env, abi_ptr addr, uint32_t val, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    cpu_stl_be_mmuidx_ra(env, addr, val, mmu_index, ra);
}

static inline void
cpu_stq_be_data_ra(CPUArchState *env, abi_ptr addr, uint64_t val, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    cpu_stq_be_mmuidx_ra(env, addr, val, mmu_index, ra);
}

static inline void
cpu_stw_le_data_ra(CPUArchState *env, abi_ptr addr, uint32_t val, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    cpu_stw_le_mmuidx_ra(env, addr, val, mmu_index, ra);
}

static inline void
cpu_stl_le_data_ra(CPUArchState *env, abi_ptr addr, uint32_t val, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    cpu_stl_le_mmuidx_ra(env, addr, val, mmu_index, ra);
}

static inline void
cpu_stq_le_data_ra(CPUArchState *env, abi_ptr addr, uint64_t val, uintptr_t ra)
{
    int mmu_index = cpu_mmu_index(env_cpu(env), false);
    cpu_stq_le_mmuidx_ra(env, addr, val, mmu_index, ra);
}

/*--------------------------*/

static inline uint32_t
cpu_ldub_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldub_data_ra(env, addr, 0);
}

static inline int
cpu_ldsb_data(CPUArchState *env, abi_ptr addr)
{
    return (int8_t)cpu_ldub_data(env, addr);
}

static inline uint32_t
cpu_lduw_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_lduw_be_data_ra(env, addr, 0);
}

static inline int
cpu_ldsw_be_data(CPUArchState *env, abi_ptr addr)
{
    return (int16_t)cpu_lduw_be_data(env, addr);
}

static inline uint32_t
cpu_ldl_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldl_be_data_ra(env, addr, 0);
}

static inline uint64_t
cpu_ldq_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldq_be_data_ra(env, addr, 0);
}

static inline uint32_t
cpu_lduw_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_lduw_le_data_ra(env, addr, 0);
}

static inline int
cpu_ldsw_le_data(CPUArchState *env, abi_ptr addr)
{
    return (int16_t)cpu_lduw_le_data(env, addr);
}

static inline uint32_t
cpu_ldl_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldl_le_data_ra(env, addr, 0);
}

static inline uint64_t
cpu_ldq_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldq_le_data_ra(env, addr, 0);
}

static inline void
cpu_stb_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stb_data_ra(env, addr, val, 0);
}

static inline void
cpu_stw_be_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stw_be_data_ra(env, addr, val, 0);
}

static inline void
cpu_stl_be_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stl_be_data_ra(env, addr, val, 0);
}

static inline void
cpu_stq_be_data(CPUArchState *env, abi_ptr addr, uint64_t val)
{
    cpu_stq_be_data_ra(env, addr, val, 0);
}

static inline void
cpu_stw_le_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stw_le_data_ra(env, addr, val, 0);
}

static inline void
cpu_stl_le_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stl_le_data_ra(env, addr, val, 0);
}

static inline void
cpu_stq_le_data(CPUArchState *env, abi_ptr addr, uint64_t val)
{
    cpu_stq_le_data_ra(env, addr, val, 0);
}

#if TARGET_BIG_ENDIAN
# define cpu_lduw_data        cpu_lduw_be_data
# define cpu_ldsw_data        cpu_ldsw_be_data
# define cpu_ldl_data         cpu_ldl_be_data
# define cpu_ldq_data         cpu_ldq_be_data
# define cpu_lduw_data_ra     cpu_lduw_be_data_ra
# define cpu_ldsw_data_ra     cpu_ldsw_be_data_ra
# define cpu_ldl_data_ra      cpu_ldl_be_data_ra
# define cpu_ldq_data_ra      cpu_ldq_be_data_ra
# define cpu_lduw_mmuidx_ra   cpu_lduw_be_mmuidx_ra
# define cpu_ldsw_mmuidx_ra   cpu_ldsw_be_mmuidx_ra
# define cpu_ldl_mmuidx_ra    cpu_ldl_be_mmuidx_ra
# define cpu_ldq_mmuidx_ra    cpu_ldq_be_mmuidx_ra
# define cpu_stw_data         cpu_stw_be_data
# define cpu_stl_data         cpu_stl_be_data
# define cpu_stq_data         cpu_stq_be_data
# define cpu_stw_data_ra      cpu_stw_be_data_ra
# define cpu_stl_data_ra      cpu_stl_be_data_ra
# define cpu_stq_data_ra      cpu_stq_be_data_ra
# define cpu_stw_mmuidx_ra    cpu_stw_be_mmuidx_ra
# define cpu_stl_mmuidx_ra    cpu_stl_be_mmuidx_ra
# define cpu_stq_mmuidx_ra    cpu_stq_be_mmuidx_ra
#else
# define cpu_lduw_data        cpu_lduw_le_data
# define cpu_ldsw_data        cpu_ldsw_le_data
# define cpu_ldl_data         cpu_ldl_le_data
# define cpu_ldq_data         cpu_ldq_le_data
# define cpu_lduw_data_ra     cpu_lduw_le_data_ra
# define cpu_ldsw_data_ra     cpu_ldsw_le_data_ra
# define cpu_ldl_data_ra      cpu_ldl_le_data_ra
# define cpu_ldq_data_ra      cpu_ldq_le_data_ra
# define cpu_lduw_mmuidx_ra   cpu_lduw_le_mmuidx_ra
# define cpu_ldsw_mmuidx_ra   cpu_ldsw_le_mmuidx_ra
# define cpu_ldl_mmuidx_ra    cpu_ldl_le_mmuidx_ra
# define cpu_ldq_mmuidx_ra    cpu_ldq_le_mmuidx_ra
# define cpu_stw_data         cpu_stw_le_data
# define cpu_stl_data         cpu_stl_le_data
# define cpu_stq_data         cpu_stq_le_data
# define cpu_stw_data_ra      cpu_stw_le_data_ra
# define cpu_stl_data_ra      cpu_stl_le_data_ra
# define cpu_stq_data_ra      cpu_stq_le_data_ra
# define cpu_stw_mmuidx_ra    cpu_stw_le_mmuidx_ra
# define cpu_stl_mmuidx_ra    cpu_stl_le_mmuidx_ra
# define cpu_stq_mmuidx_ra    cpu_stq_le_mmuidx_ra
#endif

static inline uint32_t cpu_ldub_code(CPUArchState *env, abi_ptr addr)
{
    CPUState *cs = env_cpu(env);
    MemOpIdx oi = make_memop_idx(MO_UB, cpu_mmu_index(cs, true));
    return cpu_ldb_code_mmu(env, addr, oi, 0);
}

static inline uint32_t cpu_lduw_code(CPUArchState *env, abi_ptr addr)
{
    CPUState *cs = env_cpu(env);
    MemOpIdx oi = make_memop_idx(MO_TEUW, cpu_mmu_index(cs, true));
    return cpu_ldw_code_mmu(env, addr, oi, 0);
}

static inline uint32_t cpu_ldl_code(CPUArchState *env, abi_ptr addr)
{
    CPUState *cs = env_cpu(env);
    MemOpIdx oi = make_memop_idx(MO_TEUL, cpu_mmu_index(cs, true));
    return cpu_ldl_code_mmu(env, addr, oi, 0);
}

static inline uint64_t cpu_ldq_code(CPUArchState *env, abi_ptr addr)
{
    CPUState *cs = env_cpu(env);
    MemOpIdx oi = make_memop_idx(MO_TEUQ, cpu_mmu_index(cs, true));
    return cpu_ldq_code_mmu(env, addr, oi, 0);
}

#endif /* ACCEL_TCG_CPU_LDST_H */
