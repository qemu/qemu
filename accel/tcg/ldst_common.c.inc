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
/*
 * Load helpers for tcg-ldst.h
 */

tcg_target_ulong helper_ldub_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_8);
    return do_ld1_mmu(env_cpu(env), addr, oi, retaddr, MMU_DATA_LOAD);
}

tcg_target_ulong helper_lduw_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_16);
    return do_ld2_mmu(env_cpu(env), addr, oi, retaddr, MMU_DATA_LOAD);
}

tcg_target_ulong helper_ldul_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_32);
    return do_ld4_mmu(env_cpu(env), addr, oi, retaddr, MMU_DATA_LOAD);
}

uint64_t helper_ldq_mmu(CPUArchState *env, uint64_t addr,
                        MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_64);
    return do_ld8_mmu(env_cpu(env), addr, oi, retaddr, MMU_DATA_LOAD);
}

/*
 * Provide signed versions of the load routines as well.  We can of course
 * avoid this for 64-bit data, or for 32-bit data on 32-bit host.
 */

tcg_target_ulong helper_ldsb_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr)
{
    return (int8_t)helper_ldub_mmu(env, addr, oi, retaddr);
}

tcg_target_ulong helper_ldsw_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr)
{
    return (int16_t)helper_lduw_mmu(env, addr, oi, retaddr);
}

tcg_target_ulong helper_ldsl_mmu(CPUArchState *env, uint64_t addr,
                                 MemOpIdx oi, uintptr_t retaddr)
{
    return (int32_t)helper_ldul_mmu(env, addr, oi, retaddr);
}

Int128 helper_ld16_mmu(CPUArchState *env, uint64_t addr,
                       MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_128);
    return do_ld16_mmu(env_cpu(env), addr, oi, retaddr);
}

Int128 helper_ld_i128(CPUArchState *env, uint64_t addr, uint32_t oi)
{
    return helper_ld16_mmu(env, addr, oi, GETPC());
}

/*
 * Store helpers for tcg-ldst.h
 */

void helper_stb_mmu(CPUArchState *env, uint64_t addr, uint32_t val,
                    MemOpIdx oi, uintptr_t ra)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_8);
    do_st1_mmu(env_cpu(env), addr, val, oi, ra);
}

void helper_stw_mmu(CPUArchState *env, uint64_t addr, uint32_t val,
                    MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_16);
    do_st2_mmu(env_cpu(env), addr, val, oi, retaddr);
}

void helper_stl_mmu(CPUArchState *env, uint64_t addr, uint32_t val,
                    MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_32);
    do_st4_mmu(env_cpu(env), addr, val, oi, retaddr);
}

void helper_stq_mmu(CPUArchState *env, uint64_t addr, uint64_t val,
                    MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_64);
    do_st8_mmu(env_cpu(env), addr, val, oi, retaddr);
}

void helper_st16_mmu(CPUArchState *env, uint64_t addr, Int128 val,
                     MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_128);
    do_st16_mmu(env_cpu(env), addr, val, oi, retaddr);
}

void helper_st_i128(CPUArchState *env, uint64_t addr, Int128 val, MemOpIdx oi)
{
    helper_st16_mmu(env, addr, val, oi, GETPC());
}

/*
 * Load helpers for cpu_ldst.h
 */

static void plugin_load_cb(CPUArchState *env, abi_ptr addr, MemOpIdx oi)
{
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_R);
}

uint8_t cpu_ldb_mmu(CPUArchState *env, abi_ptr addr, MemOpIdx oi, uintptr_t ra)
{
    uint8_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_UB);
    ret = do_ld1_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, oi);
    return ret;
}

uint16_t cpu_ldw_mmu(CPUArchState *env, abi_ptr addr,
                     MemOpIdx oi, uintptr_t ra)
{
    uint16_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_16);
    ret = do_ld2_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, oi);
    return ret;
}

uint32_t cpu_ldl_mmu(CPUArchState *env, abi_ptr addr,
                     MemOpIdx oi, uintptr_t ra)
{
    uint32_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_32);
    ret = do_ld4_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, oi);
    return ret;
}

uint64_t cpu_ldq_mmu(CPUArchState *env, abi_ptr addr,
                     MemOpIdx oi, uintptr_t ra)
{
    uint64_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_64);
    ret = do_ld8_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, oi);
    return ret;
}

Int128 cpu_ld16_mmu(CPUArchState *env, abi_ptr addr,
                    MemOpIdx oi, uintptr_t ra)
{
    Int128 ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_128);
    ret = do_ld16_mmu(env_cpu(env), addr, oi, ra);
    plugin_load_cb(env, addr, oi);
    return ret;
}

/*
 * Store helpers for cpu_ldst.h
 */

static void plugin_store_cb(CPUArchState *env, abi_ptr addr, MemOpIdx oi)
{
    qemu_plugin_vcpu_mem_cb(env_cpu(env), addr, oi, QEMU_PLUGIN_MEM_W);
}

void cpu_stb_mmu(CPUArchState *env, abi_ptr addr, uint8_t val,
                 MemOpIdx oi, uintptr_t retaddr)
{
    helper_stb_mmu(env, addr, val, oi, retaddr);
    plugin_store_cb(env, addr, oi);
}

void cpu_stw_mmu(CPUArchState *env, abi_ptr addr, uint16_t val,
                 MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_16);
    do_st2_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, oi);
}

void cpu_stl_mmu(CPUArchState *env, abi_ptr addr, uint32_t val,
                    MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_32);
    do_st4_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, oi);
}

void cpu_stq_mmu(CPUArchState *env, abi_ptr addr, uint64_t val,
                 MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_64);
    do_st8_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, oi);
}

void cpu_st16_mmu(CPUArchState *env, abi_ptr addr, Int128 val,
                  MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_128);
    do_st16_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, oi);
}

/*
 * Wrappers of the above
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
