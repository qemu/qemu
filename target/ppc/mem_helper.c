/*
 *  PowerPC memory access emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "exec/helper-proto.h"
#include "helper_regs.h"
#include "exec/cpu_ldst.h"
#include "internal.h"
#include "qemu/atomic128.h"

/* #define DEBUG_OP */

static inline bool needs_byteswap(const CPUPPCState *env)
{
#if TARGET_BIG_ENDIAN
  return FIELD_EX64(env->msr, MSR, LE);
#else
  return !FIELD_EX64(env->msr, MSR, LE);
#endif
}

/*****************************************************************************/
/* Memory load and stores */

static inline target_ulong addr_add(CPUPPCState *env, target_ulong addr,
                                    target_long arg)
{
#if defined(TARGET_PPC64)
    if (!msr_is_64bit(env, env->msr)) {
        return (uint32_t)(addr + arg);
    } else
#endif
    {
        return addr + arg;
    }
}

static void *probe_contiguous(CPUPPCState *env, target_ulong addr, uint32_t nb,
                              MMUAccessType access_type, int mmu_idx,
                              uintptr_t raddr)
{
    void *host1, *host2;
    uint32_t nb_pg1, nb_pg2;

    nb_pg1 = -(addr | TARGET_PAGE_MASK);
    if (likely(nb <= nb_pg1)) {
        /* The entire operation is on a single page.  */
        return probe_access(env, addr, nb, access_type, mmu_idx, raddr);
    }

    /* The operation spans two pages.  */
    nb_pg2 = nb - nb_pg1;
    host1 = probe_access(env, addr, nb_pg1, access_type, mmu_idx, raddr);
    addr = addr_add(env, addr, nb_pg1);
    host2 = probe_access(env, addr, nb_pg2, access_type, mmu_idx, raddr);

    /* If the two host pages are contiguous, optimize.  */
    if (host2 == host1 + nb_pg1) {
        return host1;
    }
    return NULL;
}

void helper_lmw(CPUPPCState *env, target_ulong addr, uint32_t reg)
{
    uintptr_t raddr = GETPC();
    int mmu_idx = cpu_mmu_index(env, false);
    void *host = probe_contiguous(env, addr, (32 - reg) * 4,
                                  MMU_DATA_LOAD, mmu_idx, raddr);

    if (likely(host)) {
        /* Fast path -- the entire operation is in RAM at host.  */
        for (; reg < 32; reg++) {
            env->gpr[reg] = (uint32_t)ldl_be_p(host);
            host += 4;
        }
    } else {
        /* Slow path -- at least some of the operation requires i/o.  */
        for (; reg < 32; reg++) {
            env->gpr[reg] = cpu_ldl_mmuidx_ra(env, addr, mmu_idx, raddr);
            addr = addr_add(env, addr, 4);
        }
    }
}

void helper_stmw(CPUPPCState *env, target_ulong addr, uint32_t reg)
{
    uintptr_t raddr = GETPC();
    int mmu_idx = cpu_mmu_index(env, false);
    void *host = probe_contiguous(env, addr, (32 - reg) * 4,
                                  MMU_DATA_STORE, mmu_idx, raddr);

    if (likely(host)) {
        /* Fast path -- the entire operation is in RAM at host.  */
        for (; reg < 32; reg++) {
            stl_be_p(host, env->gpr[reg]);
            host += 4;
        }
    } else {
        /* Slow path -- at least some of the operation requires i/o.  */
        for (; reg < 32; reg++) {
            cpu_stl_mmuidx_ra(env, addr, env->gpr[reg], mmu_idx, raddr);
            addr = addr_add(env, addr, 4);
        }
    }
}

static void do_lsw(CPUPPCState *env, target_ulong addr, uint32_t nb,
                   uint32_t reg, uintptr_t raddr)
{
    int mmu_idx;
    void *host;
    uint32_t val;

    if (unlikely(nb == 0)) {
        return;
    }

    mmu_idx = cpu_mmu_index(env, false);
    host = probe_contiguous(env, addr, nb, MMU_DATA_LOAD, mmu_idx, raddr);

    if (likely(host)) {
        /* Fast path -- the entire operation is in RAM at host.  */
        for (; nb > 3; nb -= 4) {
            env->gpr[reg] = (uint32_t)ldl_be_p(host);
            reg = (reg + 1) % 32;
            host += 4;
        }
        switch (nb) {
        default:
            return;
        case 1:
            val = ldub_p(host) << 24;
            break;
        case 2:
            val = lduw_be_p(host) << 16;
            break;
        case 3:
            val = (lduw_be_p(host) << 16) | (ldub_p(host + 2) << 8);
            break;
        }
    } else {
        /* Slow path -- at least some of the operation requires i/o.  */
        for (; nb > 3; nb -= 4) {
            env->gpr[reg] = cpu_ldl_mmuidx_ra(env, addr, mmu_idx, raddr);
            reg = (reg + 1) % 32;
            addr = addr_add(env, addr, 4);
        }
        switch (nb) {
        default:
            return;
        case 1:
            val = cpu_ldub_mmuidx_ra(env, addr, mmu_idx, raddr) << 24;
            break;
        case 2:
            val = cpu_lduw_mmuidx_ra(env, addr, mmu_idx, raddr) << 16;
            break;
        case 3:
            val = cpu_lduw_mmuidx_ra(env, addr, mmu_idx, raddr) << 16;
            addr = addr_add(env, addr, 2);
            val |= cpu_ldub_mmuidx_ra(env, addr, mmu_idx, raddr) << 8;
            break;
        }
    }
    env->gpr[reg] = val;
}

void helper_lsw(CPUPPCState *env, target_ulong addr,
                uint32_t nb, uint32_t reg)
{
    do_lsw(env, addr, nb, reg, GETPC());
}

/*
 * PPC32 specification says we must generate an exception if rA is in
 * the range of registers to be loaded.  In an other hand, IBM says
 * this is valid, but rA won't be loaded.  For now, I'll follow the
 * spec...
 */
void helper_lswx(CPUPPCState *env, target_ulong addr, uint32_t reg,
                 uint32_t ra, uint32_t rb)
{
    if (likely(xer_bc != 0)) {
        int num_used_regs = DIV_ROUND_UP(xer_bc, 4);
        if (unlikely((ra != 0 && lsw_reg_in_range(reg, num_used_regs, ra)) ||
                     lsw_reg_in_range(reg, num_used_regs, rb))) {
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                                   POWERPC_EXCP_INVAL |
                                   POWERPC_EXCP_INVAL_LSWX, GETPC());
        } else {
            do_lsw(env, addr, xer_bc, reg, GETPC());
        }
    }
}

void helper_stsw(CPUPPCState *env, target_ulong addr, uint32_t nb,
                 uint32_t reg)
{
    uintptr_t raddr = GETPC();
    int mmu_idx;
    void *host;
    uint32_t val;

    if (unlikely(nb == 0)) {
        return;
    }

    mmu_idx = cpu_mmu_index(env, false);
    host = probe_contiguous(env, addr, nb, MMU_DATA_STORE, mmu_idx, raddr);

    if (likely(host)) {
        /* Fast path -- the entire operation is in RAM at host.  */
        for (; nb > 3; nb -= 4) {
            stl_be_p(host, env->gpr[reg]);
            reg = (reg + 1) % 32;
            host += 4;
        }
        val = env->gpr[reg];
        switch (nb) {
        case 1:
            stb_p(host, val >> 24);
            break;
        case 2:
            stw_be_p(host, val >> 16);
            break;
        case 3:
            stw_be_p(host, val >> 16);
            stb_p(host + 2, val >> 8);
            break;
        }
    } else {
        for (; nb > 3; nb -= 4) {
            cpu_stl_mmuidx_ra(env, addr, env->gpr[reg], mmu_idx, raddr);
            reg = (reg + 1) % 32;
            addr = addr_add(env, addr, 4);
        }
        val = env->gpr[reg];
        switch (nb) {
        case 1:
            cpu_stb_mmuidx_ra(env, addr, val >> 24, mmu_idx, raddr);
            break;
        case 2:
            cpu_stw_mmuidx_ra(env, addr, val >> 16, mmu_idx, raddr);
            break;
        case 3:
            cpu_stw_mmuidx_ra(env, addr, val >> 16, mmu_idx, raddr);
            addr = addr_add(env, addr, 2);
            cpu_stb_mmuidx_ra(env, addr, val >> 8, mmu_idx, raddr);
            break;
        }
    }
}

static void dcbz_common(CPUPPCState *env, target_ulong addr,
                        uint32_t opcode, bool epid, uintptr_t retaddr)
{
    target_ulong mask, dcbz_size = env->dcache_line_size;
    uint32_t i;
    void *haddr;
    int mmu_idx = epid ? PPC_TLB_EPID_STORE : cpu_mmu_index(env, false);

#if defined(TARGET_PPC64)
    /* Check for dcbz vs dcbzl on 970 */
    if (env->excp_model == POWERPC_EXCP_970 &&
        !(opcode & 0x00200000) && ((env->spr[SPR_970_HID5] >> 7) & 0x3) == 1) {
        dcbz_size = 32;
    }
#endif

    /* Align address */
    mask = ~(dcbz_size - 1);
    addr &= mask;

    /* Check reservation */
    if ((env->reserve_addr & mask) == addr)  {
        env->reserve_addr = (target_ulong)-1ULL;
    }

    /* Try fast path translate */
    haddr = probe_write(env, addr, dcbz_size, mmu_idx, retaddr);
    if (haddr) {
        memset(haddr, 0, dcbz_size);
    } else {
        /* Slow path */
        for (i = 0; i < dcbz_size; i += 8) {
            cpu_stq_mmuidx_ra(env, addr + i, 0, mmu_idx, retaddr);
        }
    }
}

void helper_dcbz(CPUPPCState *env, target_ulong addr, uint32_t opcode)
{
    dcbz_common(env, addr, opcode, false, GETPC());
}

void helper_dcbzep(CPUPPCState *env, target_ulong addr, uint32_t opcode)
{
    dcbz_common(env, addr, opcode, true, GETPC());
}

void helper_icbi(CPUPPCState *env, target_ulong addr)
{
    addr &= ~(env->dcache_line_size - 1);
    /*
     * Invalidate one cache line :
     * PowerPC specification says this is to be treated like a load
     * (not a fetch) by the MMU. To be sure it will be so,
     * do the load "by hand".
     */
    cpu_ldl_data_ra(env, addr, GETPC());
}

void helper_icbiep(CPUPPCState *env, target_ulong addr)
{
#if !defined(CONFIG_USER_ONLY)
    /* See comments above */
    addr &= ~(env->dcache_line_size - 1);
    cpu_ldl_mmuidx_ra(env, addr, PPC_TLB_EPID_LOAD, GETPC());
#endif
}

/* XXX: to be tested */
target_ulong helper_lscbx(CPUPPCState *env, target_ulong addr, uint32_t reg,
                          uint32_t ra, uint32_t rb)
{
    int i, c, d;

    d = 24;
    for (i = 0; i < xer_bc; i++) {
        c = cpu_ldub_data_ra(env, addr, GETPC());
        addr = addr_add(env, addr, 1);
        /* ra (if not 0) and rb are never modified */
        if (likely(reg != rb && (ra == 0 || reg != ra))) {
            env->gpr[reg] = (env->gpr[reg] & ~(0xFF << d)) | (c << d);
        }
        if (unlikely(c == xer_cmp)) {
            break;
        }
        if (likely(d != 0)) {
            d -= 8;
        } else {
            d = 24;
            reg++;
            reg = reg & 0x1F;
        }
    }
    return i;
}

#ifdef TARGET_PPC64
uint64_t helper_lq_le_parallel(CPUPPCState *env, target_ulong addr,
                               uint32_t opidx)
{
    Int128 ret;

    /* We will have raised EXCP_ATOMIC from the translator.  */
    assert(HAVE_ATOMIC128);
    ret = cpu_atomic_ldo_le_mmu(env, addr, opidx, GETPC());
    env->retxh = int128_gethi(ret);
    return int128_getlo(ret);
}

uint64_t helper_lq_be_parallel(CPUPPCState *env, target_ulong addr,
                               uint32_t opidx)
{
    Int128 ret;

    /* We will have raised EXCP_ATOMIC from the translator.  */
    assert(HAVE_ATOMIC128);
    ret = cpu_atomic_ldo_be_mmu(env, addr, opidx, GETPC());
    env->retxh = int128_gethi(ret);
    return int128_getlo(ret);
}

void helper_stq_le_parallel(CPUPPCState *env, target_ulong addr,
                            uint64_t lo, uint64_t hi, uint32_t opidx)
{
    Int128 val;

    /* We will have raised EXCP_ATOMIC from the translator.  */
    assert(HAVE_ATOMIC128);
    val = int128_make128(lo, hi);
    cpu_atomic_sto_le_mmu(env, addr, val, opidx, GETPC());
}

void helper_stq_be_parallel(CPUPPCState *env, target_ulong addr,
                            uint64_t lo, uint64_t hi, uint32_t opidx)
{
    Int128 val;

    /* We will have raised EXCP_ATOMIC from the translator.  */
    assert(HAVE_ATOMIC128);
    val = int128_make128(lo, hi);
    cpu_atomic_sto_be_mmu(env, addr, val, opidx, GETPC());
}

uint32_t helper_stqcx_le_parallel(CPUPPCState *env, target_ulong addr,
                                  uint64_t new_lo, uint64_t new_hi,
                                  uint32_t opidx)
{
    bool success = false;

    /* We will have raised EXCP_ATOMIC from the translator.  */
    assert(HAVE_CMPXCHG128);

    if (likely(addr == env->reserve_addr)) {
        Int128 oldv, cmpv, newv;

        cmpv = int128_make128(env->reserve_val2, env->reserve_val);
        newv = int128_make128(new_lo, new_hi);
        oldv = cpu_atomic_cmpxchgo_le_mmu(env, addr, cmpv, newv,
                                          opidx, GETPC());
        success = int128_eq(oldv, cmpv);
    }
    env->reserve_addr = -1;
    return env->so + success * CRF_EQ_BIT;
}

uint32_t helper_stqcx_be_parallel(CPUPPCState *env, target_ulong addr,
                                  uint64_t new_lo, uint64_t new_hi,
                                  uint32_t opidx)
{
    bool success = false;

    /* We will have raised EXCP_ATOMIC from the translator.  */
    assert(HAVE_CMPXCHG128);

    if (likely(addr == env->reserve_addr)) {
        Int128 oldv, cmpv, newv;

        cmpv = int128_make128(env->reserve_val2, env->reserve_val);
        newv = int128_make128(new_lo, new_hi);
        oldv = cpu_atomic_cmpxchgo_be_mmu(env, addr, cmpv, newv,
                                          opidx, GETPC());
        success = int128_eq(oldv, cmpv);
    }
    env->reserve_addr = -1;
    return env->so + success * CRF_EQ_BIT;
}
#endif

/*****************************************************************************/
/* Altivec extension helpers */
#if HOST_BIG_ENDIAN
#define HI_IDX 0
#define LO_IDX 1
#else
#define HI_IDX 1
#define LO_IDX 0
#endif

/*
 * We use MSR_LE to determine index ordering in a vector.  However,
 * byteswapping is not simply controlled by MSR_LE.  We also need to
 * take into account endianness of the target.  This is done for the
 * little-endian PPC64 user-mode target.
 */

#define LVE(name, access, swap, element)                        \
    void helper_##name(CPUPPCState *env, ppc_avr_t *r,          \
                       target_ulong addr)                       \
    {                                                           \
        size_t n_elems = ARRAY_SIZE(r->element);                \
        int adjust = HI_IDX * (n_elems - 1);                    \
        int sh = sizeof(r->element[0]) >> 1;                    \
        int index = (addr & 0xf) >> sh;                         \
        if (FIELD_EX64(env->msr, MSR, LE)) {                    \
            index = n_elems - index - 1;                        \
        }                                                       \
                                                                \
        if (needs_byteswap(env)) {                              \
            r->element[LO_IDX ? index : (adjust - index)] =     \
                swap(access(env, addr, GETPC()));               \
        } else {                                                \
            r->element[LO_IDX ? index : (adjust - index)] =     \
                access(env, addr, GETPC());                     \
        }                                                       \
    }
#define I(x) (x)
LVE(lvebx, cpu_ldub_data_ra, I, u8)
LVE(lvehx, cpu_lduw_data_ra, bswap16, u16)
LVE(lvewx, cpu_ldl_data_ra, bswap32, u32)
#undef I
#undef LVE

#define STVE(name, access, swap, element)                               \
    void helper_##name(CPUPPCState *env, ppc_avr_t *r,                  \
                       target_ulong addr)                               \
    {                                                                   \
        size_t n_elems = ARRAY_SIZE(r->element);                        \
        int adjust = HI_IDX * (n_elems - 1);                            \
        int sh = sizeof(r->element[0]) >> 1;                            \
        int index = (addr & 0xf) >> sh;                                 \
        if (FIELD_EX64(env->msr, MSR, LE)) {                            \
            index = n_elems - index - 1;                                \
        }                                                               \
                                                                        \
        if (needs_byteswap(env)) {                                      \
            access(env, addr, swap(r->element[LO_IDX ? index :          \
                                              (adjust - index)]),       \
                        GETPC());                                       \
        } else {                                                        \
            access(env, addr, r->element[LO_IDX ? index :               \
                                         (adjust - index)], GETPC());   \
        }                                                               \
    }
#define I(x) (x)
STVE(stvebx, cpu_stb_data_ra, I, u8)
STVE(stvehx, cpu_stw_data_ra, bswap16, u16)
STVE(stvewx, cpu_stl_data_ra, bswap32, u32)
#undef I
#undef LVE

#ifdef TARGET_PPC64
#define GET_NB(rb) ((rb >> 56) & 0xFF)

#define VSX_LXVL(name, lj)                                              \
void helper_##name(CPUPPCState *env, target_ulong addr,                 \
                   ppc_vsr_t *xt, target_ulong rb)                      \
{                                                                       \
    ppc_vsr_t t;                                                        \
    uint64_t nb = GET_NB(rb);                                           \
    int i;                                                              \
                                                                        \
    t.s128 = int128_zero();                                             \
    if (nb) {                                                           \
        nb = (nb >= 16) ? 16 : nb;                                      \
        if (FIELD_EX64(env->msr, MSR, LE) && !lj) {                     \
            for (i = 16; i > 16 - nb; i--) {                            \
                t.VsrB(i - 1) = cpu_ldub_data_ra(env, addr, GETPC());   \
                addr = addr_add(env, addr, 1);                          \
            }                                                           \
        } else {                                                        \
            for (i = 0; i < nb; i++) {                                  \
                t.VsrB(i) = cpu_ldub_data_ra(env, addr, GETPC());       \
                addr = addr_add(env, addr, 1);                          \
            }                                                           \
        }                                                               \
    }                                                                   \
    *xt = t;                                                            \
}

VSX_LXVL(lxvl, 0)
VSX_LXVL(lxvll, 1)
#undef VSX_LXVL

#define VSX_STXVL(name, lj)                                       \
void helper_##name(CPUPPCState *env, target_ulong addr,           \
                   ppc_vsr_t *xt, target_ulong rb)                \
{                                                                 \
    target_ulong nb = GET_NB(rb);                                 \
    int i;                                                        \
                                                                  \
    if (!nb) {                                                    \
        return;                                                   \
    }                                                             \
                                                                  \
    nb = (nb >= 16) ? 16 : nb;                                    \
    if (FIELD_EX64(env->msr, MSR, LE) && !lj) {                   \
        for (i = 16; i > 16 - nb; i--) {                          \
            cpu_stb_data_ra(env, addr, xt->VsrB(i - 1), GETPC()); \
            addr = addr_add(env, addr, 1);                        \
        }                                                         \
    } else {                                                      \
        for (i = 0; i < nb; i++) {                                \
            cpu_stb_data_ra(env, addr, xt->VsrB(i), GETPC());     \
            addr = addr_add(env, addr, 1);                        \
        }                                                         \
    }                                                             \
}

VSX_STXVL(stxvl, 0)
VSX_STXVL(stxvll, 1)
#undef VSX_STXVL
#undef GET_NB
#endif /* TARGET_PPC64 */

#undef HI_IDX
#undef LO_IDX

void helper_tbegin(CPUPPCState *env)
{
    /*
     * As a degenerate implementation, always fail tbegin.  The reason
     * given is "Nesting overflow".  The "persistent" bit is set,
     * providing a hint to the error handler to not retry.  The TFIAR
     * captures the address of the failure, which is this tbegin
     * instruction.  Instruction execution will continue with the next
     * instruction in memory, which is precisely what we want.
     */

    env->spr[SPR_TEXASR] =
        (1ULL << TEXASR_FAILURE_PERSISTENT) |
        (1ULL << TEXASR_NESTING_OVERFLOW) |
        (FIELD_EX64_HV(env->msr) << TEXASR_PRIVILEGE_HV) |
        (FIELD_EX64(env->msr, MSR, PR) << TEXASR_PRIVILEGE_PR) |
        (1ULL << TEXASR_FAILURE_SUMMARY) |
        (1ULL << TEXASR_TFIAR_EXACT);
    env->spr[SPR_TFIAR] = env->nip | (FIELD_EX64_HV(env->msr) << 1) |
                          FIELD_EX64(env->msr, MSR, PR);
    env->spr[SPR_TFHAR] = env->nip + 4;
    env->crf[0] = 0xB; /* 0b1010 = transaction failure */
}
