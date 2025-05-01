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

static void plugin_load_cb(CPUArchState *env, vaddr addr,
                           uint64_t value_low,
                           uint64_t value_high,
                           MemOpIdx oi)
{
    if (cpu_plugin_mem_cbs_enabled(env_cpu(env))) {
        qemu_plugin_vcpu_mem_cb(env_cpu(env), addr,
                                value_low, value_high,
                                oi, QEMU_PLUGIN_MEM_R);
    }
}

uint8_t cpu_ldb_mmu(CPUArchState *env, vaddr addr, MemOpIdx oi, uintptr_t ra)
{
    uint8_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_UB);
    ret = do_ld1_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, ret, 0, oi);
    return ret;
}

uint16_t cpu_ldw_mmu(CPUArchState *env, vaddr addr,
                     MemOpIdx oi, uintptr_t ra)
{
    uint16_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_16);
    ret = do_ld2_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, ret, 0, oi);
    return ret;
}

uint32_t cpu_ldl_mmu(CPUArchState *env, vaddr addr,
                     MemOpIdx oi, uintptr_t ra)
{
    uint32_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_32);
    ret = do_ld4_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, ret, 0, oi);
    return ret;
}

uint64_t cpu_ldq_mmu(CPUArchState *env, vaddr addr,
                     MemOpIdx oi, uintptr_t ra)
{
    uint64_t ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_64);
    ret = do_ld8_mmu(env_cpu(env), addr, oi, ra, MMU_DATA_LOAD);
    plugin_load_cb(env, addr, ret, 0, oi);
    return ret;
}

Int128 cpu_ld16_mmu(CPUArchState *env, vaddr addr,
                    MemOpIdx oi, uintptr_t ra)
{
    Int128 ret;

    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_128);
    ret = do_ld16_mmu(env_cpu(env), addr, oi, ra);
    plugin_load_cb(env, addr, int128_getlo(ret), int128_gethi(ret), oi);
    return ret;
}

/*
 * Store helpers for cpu_ldst.h
 */

static void plugin_store_cb(CPUArchState *env, vaddr addr,
                            uint64_t value_low,
                            uint64_t value_high,
                            MemOpIdx oi)
{
    if (cpu_plugin_mem_cbs_enabled(env_cpu(env))) {
        qemu_plugin_vcpu_mem_cb(env_cpu(env), addr,
                                value_low, value_high,
                                oi, QEMU_PLUGIN_MEM_W);
    }
}

void cpu_stb_mmu(CPUArchState *env, vaddr addr, uint8_t val,
                 MemOpIdx oi, uintptr_t retaddr)
{
    helper_stb_mmu(env, addr, val, oi, retaddr);
    plugin_store_cb(env, addr, val, 0, oi);
}

void cpu_stw_mmu(CPUArchState *env, vaddr addr, uint16_t val,
                 MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_16);
    do_st2_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, val, 0, oi);
}

void cpu_stl_mmu(CPUArchState *env, vaddr addr, uint32_t val,
                    MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_32);
    do_st4_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, val, 0, oi);
}

void cpu_stq_mmu(CPUArchState *env, vaddr addr, uint64_t val,
                 MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_64);
    do_st8_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, val, 0, oi);
}

void cpu_st16_mmu(CPUArchState *env, vaddr addr, Int128 val,
                  MemOpIdx oi, uintptr_t retaddr)
{
    tcg_debug_assert((get_memop(oi) & MO_SIZE) == MO_128);
    do_st16_mmu(env_cpu(env), addr, val, oi, retaddr);
    plugin_store_cb(env, addr, int128_getlo(val), int128_gethi(val), oi);
}
