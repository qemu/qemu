/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Crypto helper functions
 */

#ifndef TARGET_S390_CRYPTO_HELPER_H
#define TARGET_S390_CRYPTO_HELPER_H

/*
 * helper function to read len * u8 from guest to local buffer
 */
static inline void read_guest_wrap_u8(CPUS390XState *env, const int mmu_idx,
                                      const uintptr_t ra, uint64_t guest_addr,
                                      uint8_t *dest, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr++) {
        uint64_t waddr = wrap_address(env, guest_addr);
        dest[i] = cpu_ldb_mmu(env, waddr, oi, ra);
    }
}

/*
 * helper function to write len * u8 from local buffer to guest
 */
static inline void write_guest_wrap_u8(CPUS390XState *env, const int mmu_idx,
                                       const uintptr_t ra, uint64_t guest_addr,
                                       const uint8_t *src, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_8, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr++) {
        uint64_t waddr = wrap_address(env, guest_addr);
        cpu_stb_mmu(env, waddr, src[i], oi, ra);
    }
}

/*
 * helper function to read len * u32 from guest to local buffer
 */
static inline void read_guest_wrap_u32(CPUS390XState *env, const int mmu_idx,
                                      const uintptr_t ra, uint64_t guest_addr,
                                      uint32_t *dest, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_BE | MO_32 | MO_UNALN, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr += 4) {
        uint64_t waddr = wrap_address(env, guest_addr);
        dest[i] = cpu_ldl_mmu(env, waddr, oi, ra);
    }
}

/*
 * helper function to write len * u32 from local buffer to guest
 */
static inline void write_guest_wrap_u32(CPUS390XState *env, const int mmu_idx,
                                        const uintptr_t ra, uint64_t guest_addr,
                                        const uint32_t *src, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_BE | MO_32 | MO_UNALN, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr += 4) {
        uint64_t waddr = wrap_address(env, guest_addr);
        cpu_stl_mmu(env, waddr, src[i], oi, ra);
    }
}

/*
 * helper function to read len * u64 from guest to local buffer
 */
static inline void read_guest_wrap_u64(CPUS390XState *env, const int mmu_idx,
                                      const uintptr_t ra, uint64_t guest_addr,
                                      uint64_t *dest, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_BE | MO_64 | MO_UNALN, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr += 8) {
        uint64_t waddr = wrap_address(env, guest_addr);
        dest[i] = cpu_ldq_mmu(env, waddr, oi, ra);
    }
}

/*
 * helper function to write len * u64 from local buffer to guest
 */
static inline void write_guest_wrap_u64(CPUS390XState *env, const int mmu_idx,
                                        const uintptr_t ra, uint64_t guest_addr,
                                        const uint64_t *src, size_t len)
{
    const MemOpIdx oi = make_memop_idx(MO_BE | MO_64 | MO_UNALN, mmu_idx);

    for (size_t i = 0; i < len; i++, guest_addr += 8) {
        uint64_t waddr = wrap_address(env, guest_addr);
        cpu_stq_mmu(env, waddr, src[i], oi, ra);
    }
}

#endif /* TARGET_S390_CRYPTO_HELPER_H */
