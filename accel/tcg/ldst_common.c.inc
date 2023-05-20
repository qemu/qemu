/*
 * Routines common to user and system emulation of load/store.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

uint32_t cpu_ldub_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                            int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);
    return cpu_ldb_mmu(env, addr, oi, ra);
}

int cpu_ldsb_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                       int mmu_idx, uintptr_t ra)
{
    return (int8_t)cpu_ldub_mmuidx_ra(env, addr, mmu_idx, ra);
}

uint32_t cpu_lduw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUW | MO_UNALN, mmu_idx);
    return cpu_ldw_mmu(env, addr, oi, ra);
}

int cpu_ldsw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_lduw_be_mmuidx_ra(env, addr, mmu_idx, ra);
}

uint32_t cpu_ldl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUL | MO_UNALN, mmu_idx);
    return cpu_ldl_mmu(env, addr, oi, ra);
}

uint64_t cpu_ldq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUQ | MO_UNALN, mmu_idx);
    return cpu_ldq_mmu(env, addr, oi, ra);
}

uint32_t cpu_lduw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUW | MO_UNALN, mmu_idx);
    return cpu_ldw_mmu(env, addr, oi, ra);
}

int cpu_ldsw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_lduw_le_mmuidx_ra(env, addr, mmu_idx, ra);
}

uint32_t cpu_ldl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUL | MO_UNALN, mmu_idx);
    return cpu_ldl_mmu(env, addr, oi, ra);
}

uint64_t cpu_ldq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUQ | MO_UNALN, mmu_idx);
    return cpu_ldq_mmu(env, addr, oi, ra);
}

void cpu_stb_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                       int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);
    cpu_stb_mmu(env, addr, val, oi, ra);
}

void cpu_stw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUW | MO_UNALN, mmu_idx);
    cpu_stw_mmu(env, addr, val, oi, ra);
}

void cpu_stl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUL | MO_UNALN, mmu_idx);
    cpu_stl_mmu(env, addr, val, oi, ra);
}

void cpu_stq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUQ | MO_UNALN, mmu_idx);
    cpu_stq_mmu(env, addr, val, oi, ra);
}

void cpu_stw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUW | MO_UNALN, mmu_idx);
    cpu_stw_mmu(env, addr, val, oi, ra);
}

void cpu_stl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUL | MO_UNALN, mmu_idx);
    cpu_stl_mmu(env, addr, val, oi, ra);
}

void cpu_stq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUQ | MO_UNALN, mmu_idx);
    cpu_stq_mmu(env, addr, val, oi, ra);
}

/*--------------------------*/

uint32_t cpu_ldub_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldub_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

int cpu_ldsb_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int8_t)cpu_ldub_data_ra(env, addr, ra);
}

uint32_t cpu_lduw_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_lduw_be_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

int cpu_ldsw_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int16_t)cpu_lduw_be_data_ra(env, addr, ra);
}

uint32_t cpu_ldl_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldl_be_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

uint64_t cpu_ldq_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldq_be_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

uint32_t cpu_lduw_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_lduw_le_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

int cpu_ldsw_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int16_t)cpu_lduw_le_data_ra(env, addr, ra);
}

uint32_t cpu_ldl_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldl_le_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

uint64_t cpu_ldq_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldq_le_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

void cpu_stb_data_ra(CPUArchState *env, abi_ptr addr,
                     uint32_t val, uintptr_t ra)
{
    cpu_stb_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stw_be_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stw_be_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stl_be_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stl_be_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stq_be_data_ra(CPUArchState *env, abi_ptr addr,
                        uint64_t val, uintptr_t ra)
{
    cpu_stq_be_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stw_le_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stw_le_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stl_le_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stl_le_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stq_le_data_ra(CPUArchState *env, abi_ptr addr,
                        uint64_t val, uintptr_t ra)
{
    cpu_stq_le_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

/*--------------------------*/

uint32_t cpu_ldub_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldub_data_ra(env, addr, 0);
}

int cpu_ldsb_data(CPUArchState *env, abi_ptr addr)
{
    return (int8_t)cpu_ldub_data(env, addr);
}

uint32_t cpu_lduw_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_lduw_be_data_ra(env, addr, 0);
}

int cpu_ldsw_be_data(CPUArchState *env, abi_ptr addr)
{
    return (int16_t)cpu_lduw_be_data(env, addr);
}

uint32_t cpu_ldl_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldl_be_data_ra(env, addr, 0);
}

uint64_t cpu_ldq_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldq_be_data_ra(env, addr, 0);
}

uint32_t cpu_lduw_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_lduw_le_data_ra(env, addr, 0);
}

int cpu_ldsw_le_data(CPUArchState *env, abi_ptr addr)
{
    return (int16_t)cpu_lduw_le_data(env, addr);
}

uint32_t cpu_ldl_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldl_le_data_ra(env, addr, 0);
}

uint64_t cpu_ldq_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldq_le_data_ra(env, addr, 0);
}

void cpu_stb_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stb_data_ra(env, addr, val, 0);
}

void cpu_stw_be_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stw_be_data_ra(env, addr, val, 0);
}

void cpu_stl_be_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stl_be_data_ra(env, addr, val, 0);
}

void cpu_stq_be_data(CPUArchState *env, abi_ptr addr, uint64_t val)
{
    cpu_stq_be_data_ra(env, addr, val, 0);
}

void cpu_stw_le_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stw_le_data_ra(env, addr, val, 0);
}

void cpu_stl_le_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stl_le_data_ra(env, addr, val, 0);
}

void cpu_stq_le_data(CPUArchState *env, abi_ptr addr, uint64_t val)
{
    cpu_stq_le_data_ra(env, addr, val, 0);
}
