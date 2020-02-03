/*
 *  MIPS emulation helpers for qemu.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internal.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/memop.h"
#include "sysemu/kvm.h"


/*****************************************************************************/
/* Exceptions processing helpers */

void helper_raise_exception_err(CPUMIPSState *env, uint32_t exception,
                                int error_code)
{
    do_raise_exception_err(env, exception, error_code, 0);
}

void helper_raise_exception(CPUMIPSState *env, uint32_t exception)
{
    do_raise_exception(env, exception, GETPC());
}

void helper_raise_exception_debug(CPUMIPSState *env)
{
    do_raise_exception(env, EXCP_DEBUG, 0);
}

static void raise_exception(CPUMIPSState *env, uint32_t exception)
{
    do_raise_exception(env, exception, 0);
}

/* 64 bits arithmetic for 32 bits hosts */
static inline uint64_t get_HILO(CPUMIPSState *env)
{
    return ((uint64_t)(env->active_tc.HI[0]) << 32) |
           (uint32_t)env->active_tc.LO[0];
}

static inline target_ulong set_HIT0_LO(CPUMIPSState *env, uint64_t HILO)
{
    env->active_tc.LO[0] = (int32_t)(HILO & 0xFFFFFFFF);
    return env->active_tc.HI[0] = (int32_t)(HILO >> 32);
}

static inline target_ulong set_HI_LOT0(CPUMIPSState *env, uint64_t HILO)
{
    target_ulong tmp = env->active_tc.LO[0] = (int32_t)(HILO & 0xFFFFFFFF);
    env->active_tc.HI[0] = (int32_t)(HILO >> 32);
    return tmp;
}

/* Multiplication variants of the vr54xx. */
target_ulong helper_muls(CPUMIPSState *env, target_ulong arg1,
                         target_ulong arg2)
{
    return set_HI_LOT0(env, 0 - ((int64_t)(int32_t)arg1 *
                                 (int64_t)(int32_t)arg2));
}

target_ulong helper_mulsu(CPUMIPSState *env, target_ulong arg1,
                          target_ulong arg2)
{
    return set_HI_LOT0(env, 0 - (uint64_t)(uint32_t)arg1 *
                       (uint64_t)(uint32_t)arg2);
}

target_ulong helper_macc(CPUMIPSState *env, target_ulong arg1,
                         target_ulong arg2)
{
    return set_HI_LOT0(env, (int64_t)get_HILO(env) + (int64_t)(int32_t)arg1 *
                       (int64_t)(int32_t)arg2);
}

target_ulong helper_macchi(CPUMIPSState *env, target_ulong arg1,
                           target_ulong arg2)
{
    return set_HIT0_LO(env, (int64_t)get_HILO(env) + (int64_t)(int32_t)arg1 *
                       (int64_t)(int32_t)arg2);
}

target_ulong helper_maccu(CPUMIPSState *env, target_ulong arg1,
                          target_ulong arg2)
{
    return set_HI_LOT0(env, (uint64_t)get_HILO(env) +
                       (uint64_t)(uint32_t)arg1 * (uint64_t)(uint32_t)arg2);
}

target_ulong helper_macchiu(CPUMIPSState *env, target_ulong arg1,
                            target_ulong arg2)
{
    return set_HIT0_LO(env, (uint64_t)get_HILO(env) +
                       (uint64_t)(uint32_t)arg1 * (uint64_t)(uint32_t)arg2);
}

target_ulong helper_msac(CPUMIPSState *env, target_ulong arg1,
                         target_ulong arg2)
{
    return set_HI_LOT0(env, (int64_t)get_HILO(env) - (int64_t)(int32_t)arg1 *
                       (int64_t)(int32_t)arg2);
}

target_ulong helper_msachi(CPUMIPSState *env, target_ulong arg1,
                           target_ulong arg2)
{
    return set_HIT0_LO(env, (int64_t)get_HILO(env) - (int64_t)(int32_t)arg1 *
                       (int64_t)(int32_t)arg2);
}

target_ulong helper_msacu(CPUMIPSState *env, target_ulong arg1,
                          target_ulong arg2)
{
    return set_HI_LOT0(env, (uint64_t)get_HILO(env) -
                       (uint64_t)(uint32_t)arg1 * (uint64_t)(uint32_t)arg2);
}

target_ulong helper_msachiu(CPUMIPSState *env, target_ulong arg1,
                            target_ulong arg2)
{
    return set_HIT0_LO(env, (uint64_t)get_HILO(env) -
                       (uint64_t)(uint32_t)arg1 * (uint64_t)(uint32_t)arg2);
}

target_ulong helper_mulhi(CPUMIPSState *env, target_ulong arg1,
                          target_ulong arg2)
{
    return set_HIT0_LO(env, (int64_t)(int32_t)arg1 * (int64_t)(int32_t)arg2);
}

target_ulong helper_mulhiu(CPUMIPSState *env, target_ulong arg1,
                           target_ulong arg2)
{
    return set_HIT0_LO(env, (uint64_t)(uint32_t)arg1 *
                       (uint64_t)(uint32_t)arg2);
}

target_ulong helper_mulshi(CPUMIPSState *env, target_ulong arg1,
                           target_ulong arg2)
{
    return set_HIT0_LO(env, 0 - (int64_t)(int32_t)arg1 *
                       (int64_t)(int32_t)arg2);
}

target_ulong helper_mulshiu(CPUMIPSState *env, target_ulong arg1,
                            target_ulong arg2)
{
    return set_HIT0_LO(env, 0 - (uint64_t)(uint32_t)arg1 *
                       (uint64_t)(uint32_t)arg2);
}

static inline target_ulong bitswap(target_ulong v)
{
    v = ((v >> 1) & (target_ulong)0x5555555555555555ULL) |
              ((v & (target_ulong)0x5555555555555555ULL) << 1);
    v = ((v >> 2) & (target_ulong)0x3333333333333333ULL) |
              ((v & (target_ulong)0x3333333333333333ULL) << 2);
    v = ((v >> 4) & (target_ulong)0x0F0F0F0F0F0F0F0FULL) |
              ((v & (target_ulong)0x0F0F0F0F0F0F0F0FULL) << 4);
    return v;
}

#ifdef TARGET_MIPS64
target_ulong helper_dbitswap(target_ulong rt)
{
    return bitswap(rt);
}
#endif

target_ulong helper_bitswap(target_ulong rt)
{
    return (int32_t)bitswap(rt);
}

target_ulong helper_rotx(target_ulong rs, uint32_t shift, uint32_t shiftx,
                        uint32_t stripe)
{
    int i;
    uint64_t tmp0 = ((uint64_t)rs) << 32 | ((uint64_t)rs & 0xffffffff);
    uint64_t tmp1 = tmp0;
    for (i = 0; i <= 46; i++) {
        int s;
        if (i & 0x8) {
            s = shift;
        } else {
            s = shiftx;
        }

        if (stripe != 0 && !(i & 0x4)) {
            s = ~s;
        }
        if (s & 0x10) {
            if (tmp0 & (1LL << (i + 16))) {
                tmp1 |= 1LL << i;
            } else {
                tmp1 &= ~(1LL << i);
            }
        }
    }

    uint64_t tmp2 = tmp1;
    for (i = 0; i <= 38; i++) {
        int s;
        if (i & 0x4) {
            s = shift;
        } else {
            s = shiftx;
        }

        if (s & 0x8) {
            if (tmp1 & (1LL << (i + 8))) {
                tmp2 |= 1LL << i;
            } else {
                tmp2 &= ~(1LL << i);
            }
        }
    }

    uint64_t tmp3 = tmp2;
    for (i = 0; i <= 34; i++) {
        int s;
        if (i & 0x2) {
            s = shift;
        } else {
            s = shiftx;
        }
        if (s & 0x4) {
            if (tmp2 & (1LL << (i + 4))) {
                tmp3 |= 1LL << i;
            } else {
                tmp3 &= ~(1LL << i);
            }
        }
    }

    uint64_t tmp4 = tmp3;
    for (i = 0; i <= 32; i++) {
        int s;
        if (i & 0x1) {
            s = shift;
        } else {
            s = shiftx;
        }
        if (s & 0x2) {
            if (tmp3 & (1LL << (i + 2))) {
                tmp4 |= 1LL << i;
            } else {
                tmp4 &= ~(1LL << i);
            }
        }
    }

    uint64_t tmp5 = tmp4;
    for (i = 0; i <= 31; i++) {
        int s;
        s = shift;
        if (s & 0x1) {
            if (tmp4 & (1LL << (i + 1))) {
                tmp5 |= 1LL << i;
            } else {
                tmp5 &= ~(1LL << i);
            }
        }
    }

    return (int64_t)(int32_t)(uint32_t)tmp5;
}

#ifndef CONFIG_USER_ONLY

static inline hwaddr do_translate_address(CPUMIPSState *env,
                                                      target_ulong address,
                                                      int rw, uintptr_t retaddr)
{
    hwaddr paddr;
    CPUState *cs = env_cpu(env);

    paddr = cpu_mips_translate_address(env, address, rw);

    if (paddr == -1LL) {
        cpu_loop_exit_restore(cs, retaddr);
    } else {
        return paddr;
    }
}

#define HELPER_LD_ATOMIC(name, insn, almask, do_cast)                         \
target_ulong helper_##name(CPUMIPSState *env, target_ulong arg, int mem_idx)  \
{                                                                             \
    if (arg & almask) {                                                       \
        if (!(env->hflags & MIPS_HFLAG_DM)) {                                 \
            env->CP0_BadVAddr = arg;                                          \
        }                                                                     \
        do_raise_exception(env, EXCP_AdEL, GETPC());                          \
    }                                                                         \
    env->CP0_LLAddr = do_translate_address(env, arg, 0, GETPC());             \
    env->lladdr = arg;                                                        \
    env->llval = do_cast cpu_##insn##_mmuidx_ra(env, arg, mem_idx, GETPC());  \
    return env->llval;                                                        \
}
HELPER_LD_ATOMIC(ll, ldl, 0x3, (target_long)(int32_t))
#ifdef TARGET_MIPS64
HELPER_LD_ATOMIC(lld, ldq, 0x7, (target_ulong))
#endif
#undef HELPER_LD_ATOMIC
#endif

#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK(v) ((v) & 3)
#define GET_OFFSET(addr, offset) (addr + (offset))
#else
#define GET_LMASK(v) (((v) & 3) ^ 3)
#define GET_OFFSET(addr, offset) (addr - (offset))
#endif

void helper_swl(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)(arg1 >> 24), mem_idx, GETPC());

    if (GET_LMASK(arg2) <= 2) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 1), (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (GET_LMASK(arg2) <= 1) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 2), (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (GET_LMASK(arg2) == 0) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 3), (uint8_t)arg1,
                          mem_idx, GETPC());
    }
}

void helper_swr(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)arg1, mem_idx, GETPC());

    if (GET_LMASK(arg2) >= 1) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -1), (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (GET_LMASK(arg2) >= 2) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -2), (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (GET_LMASK(arg2) == 3) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -3), (uint8_t)(arg1 >> 24),
                          mem_idx, GETPC());
    }
}

#if defined(TARGET_MIPS64)
/*
 * "half" load and stores.  We must do the memory access inline,
 * or fault handling won't work.
 */
#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK64(v) ((v) & 7)
#else
#define GET_LMASK64(v) (((v) & 7) ^ 7)
#endif

void helper_sdl(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)(arg1 >> 56), mem_idx, GETPC());

    if (GET_LMASK64(arg2) <= 6) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 1), (uint8_t)(arg1 >> 48),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) <= 5) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 2), (uint8_t)(arg1 >> 40),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) <= 4) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 3), (uint8_t)(arg1 >> 32),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) <= 3) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 4), (uint8_t)(arg1 >> 24),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) <= 2) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 5), (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) <= 1) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 6), (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) <= 0) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, 7), (uint8_t)arg1,
                          mem_idx, GETPC());
    }
}

void helper_sdr(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    cpu_stb_mmuidx_ra(env, arg2, (uint8_t)arg1, mem_idx, GETPC());

    if (GET_LMASK64(arg2) >= 1) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -1), (uint8_t)(arg1 >> 8),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) >= 2) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -2), (uint8_t)(arg1 >> 16),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) >= 3) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -3), (uint8_t)(arg1 >> 24),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) >= 4) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -4), (uint8_t)(arg1 >> 32),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) >= 5) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -5), (uint8_t)(arg1 >> 40),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) >= 6) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -6), (uint8_t)(arg1 >> 48),
                          mem_idx, GETPC());
    }

    if (GET_LMASK64(arg2) == 7) {
        cpu_stb_mmuidx_ra(env, GET_OFFSET(arg2, -7), (uint8_t)(arg1 >> 56),
                          mem_idx, GETPC());
    }
}
#endif /* TARGET_MIPS64 */

static const int multiple_regs[] = { 16, 17, 18, 19, 20, 21, 22, 23, 30 };

void helper_lwm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            env->active_tc.gpr[multiple_regs[i]] =
                (target_long)cpu_ldl_mmuidx_ra(env, addr, mem_idx, GETPC());
            addr += 4;
        }
    }

    if (do_r31) {
        env->active_tc.gpr[31] =
            (target_long)cpu_ldl_mmuidx_ra(env, addr, mem_idx, GETPC());
    }
}

void helper_swm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            cpu_stw_mmuidx_ra(env, addr, env->active_tc.gpr[multiple_regs[i]],
                              mem_idx, GETPC());
            addr += 4;
        }
    }

    if (do_r31) {
        cpu_stw_mmuidx_ra(env, addr, env->active_tc.gpr[31], mem_idx, GETPC());
    }
}

#if defined(TARGET_MIPS64)
void helper_ldm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            env->active_tc.gpr[multiple_regs[i]] =
                cpu_ldq_mmuidx_ra(env, addr, mem_idx, GETPC());
            addr += 8;
        }
    }

    if (do_r31) {
        env->active_tc.gpr[31] =
            cpu_ldq_mmuidx_ra(env, addr, mem_idx, GETPC());
    }
}

void helper_sdm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE(multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            cpu_stq_mmuidx_ra(env, addr, env->active_tc.gpr[multiple_regs[i]],
                              mem_idx, GETPC());
            addr += 8;
        }
    }

    if (do_r31) {
        cpu_stq_mmuidx_ra(env, addr, env->active_tc.gpr[31], mem_idx, GETPC());
    }
}
#endif


void helper_fork(target_ulong arg1, target_ulong arg2)
{
    /*
     * arg1 = rt, arg2 = rs
     * TODO: store to TC register
     */
}

target_ulong helper_yield(CPUMIPSState *env, target_ulong arg)
{
    target_long arg1 = arg;

    if (arg1 < 0) {
        /* No scheduling policy implemented. */
        if (arg1 != -2) {
            if (env->CP0_VPEControl & (1 << CP0VPECo_YSI) &&
                env->active_tc.CP0_TCStatus & (1 << CP0TCSt_DT)) {
                env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
                env->CP0_VPEControl |= 4 << CP0VPECo_EXCPT;
                do_raise_exception(env, EXCP_THREAD, GETPC());
            }
        }
    } else if (arg1 == 0) {
        if (0) {
            /* TODO: TC underflow */
            env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
            do_raise_exception(env, EXCP_THREAD, GETPC());
        } else {
            /* TODO: Deallocate TC */
        }
    } else if (arg1 > 0) {
        /* Yield qualifier inputs not implemented. */
        env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
        env->CP0_VPEControl |= 2 << CP0VPECo_EXCPT;
        do_raise_exception(env, EXCP_THREAD, GETPC());
    }
    return env->CP0_YQMask;
}

#ifndef CONFIG_USER_ONLY
/* TLB management */
static void r4k_mips_tlb_flush_extra(CPUMIPSState *env, int first)
{
    /* Discard entries from env->tlb[first] onwards.  */
    while (env->tlb->tlb_in_use > first) {
        r4k_invalidate_tlb(env, --env->tlb->tlb_in_use, 0);
    }
}

static inline uint64_t get_tlb_pfn_from_entrylo(uint64_t entrylo)
{
#if defined(TARGET_MIPS64)
    return extract64(entrylo, 6, 54);
#else
    return extract64(entrylo, 6, 24) | /* PFN */
           (extract64(entrylo, 32, 32) << 24); /* PFNX */
#endif
}

static void r4k_fill_tlb(CPUMIPSState *env, int idx)
{
    r4k_tlb_t *tlb;
    uint64_t mask = env->CP0_PageMask >> (TARGET_PAGE_BITS + 1);

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    if (env->CP0_EntryHi & (1 << CP0EnHi_EHINV)) {
        tlb->EHINV = 1;
        return;
    }
    tlb->EHINV = 0;
    tlb->VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    tlb->VPN &= env->SEGMask;
#endif
    tlb->ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    tlb->MMID = env->CP0_MemoryMapID;
    tlb->PageMask = env->CP0_PageMask;
    tlb->G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    tlb->V0 = (env->CP0_EntryLo0 & 2) != 0;
    tlb->D0 = (env->CP0_EntryLo0 & 4) != 0;
    tlb->C0 = (env->CP0_EntryLo0 >> 3) & 0x7;
    tlb->XI0 = (env->CP0_EntryLo0 >> CP0EnLo_XI) & 1;
    tlb->RI0 = (env->CP0_EntryLo0 >> CP0EnLo_RI) & 1;
    tlb->PFN[0] = (get_tlb_pfn_from_entrylo(env->CP0_EntryLo0) & ~mask) << 12;
    tlb->V1 = (env->CP0_EntryLo1 & 2) != 0;
    tlb->D1 = (env->CP0_EntryLo1 & 4) != 0;
    tlb->C1 = (env->CP0_EntryLo1 >> 3) & 0x7;
    tlb->XI1 = (env->CP0_EntryLo1 >> CP0EnLo_XI) & 1;
    tlb->RI1 = (env->CP0_EntryLo1 >> CP0EnLo_RI) & 1;
    tlb->PFN[1] = (get_tlb_pfn_from_entrylo(env->CP0_EntryLo1) & ~mask) << 12;
}

void r4k_helper_tlbinv(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    r4k_tlb_t *tlb;
    int idx;

    MMID = mi ? MMID : (uint32_t) ASID;
    for (idx = 0; idx < env->tlb->nb_tlb; idx++) {
        tlb = &env->tlb->mmu.r4k.tlb[idx];
        tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
        if (!tlb->G && tlb_mmid == MMID) {
            tlb->EHINV = 1;
        }
    }
    cpu_mips_tlb_flush(env);
}

void r4k_helper_tlbinvf(CPUMIPSState *env)
{
    int idx;

    for (idx = 0; idx < env->tlb->nb_tlb; idx++) {
        env->tlb->mmu.r4k.tlb[idx].EHINV = 1;
    }
    cpu_mips_tlb_flush(env);
}

void r4k_helper_tlbwi(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    target_ulong VPN;
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    bool EHINV, G, V0, D0, V1, D1, XI0, XI1, RI0, RI1;
    r4k_tlb_t *tlb;
    int idx;

    MMID = mi ? MMID : (uint32_t) ASID;

    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    VPN &= env->SEGMask;
#endif
    EHINV = (env->CP0_EntryHi & (1 << CP0EnHi_EHINV)) != 0;
    G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    V0 = (env->CP0_EntryLo0 & 2) != 0;
    D0 = (env->CP0_EntryLo0 & 4) != 0;
    XI0 = (env->CP0_EntryLo0 >> CP0EnLo_XI) &1;
    RI0 = (env->CP0_EntryLo0 >> CP0EnLo_RI) &1;
    V1 = (env->CP0_EntryLo1 & 2) != 0;
    D1 = (env->CP0_EntryLo1 & 4) != 0;
    XI1 = (env->CP0_EntryLo1 >> CP0EnLo_XI) &1;
    RI1 = (env->CP0_EntryLo1 >> CP0EnLo_RI) &1;

    tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
    /*
     * Discard cached TLB entries, unless tlbwi is just upgrading access
     * permissions on the current entry.
     */
    if (tlb->VPN != VPN || tlb_mmid != MMID || tlb->G != G ||
        (!tlb->EHINV && EHINV) ||
        (tlb->V0 && !V0) || (tlb->D0 && !D0) ||
        (!tlb->XI0 && XI0) || (!tlb->RI0 && RI0) ||
        (tlb->V1 && !V1) || (tlb->D1 && !D1) ||
        (!tlb->XI1 && XI1) || (!tlb->RI1 && RI1)) {
        r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);
    }

    r4k_invalidate_tlb(env, idx, 0);
    r4k_fill_tlb(env, idx);
}

void r4k_helper_tlbwr(CPUMIPSState *env)
{
    int r = cpu_mips_get_random(env);

    r4k_invalidate_tlb(env, r, 1);
    r4k_fill_tlb(env, r);
}

void r4k_helper_tlbp(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    r4k_tlb_t *tlb;
    target_ulong mask;
    target_ulong tag;
    target_ulong VPN;
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    int i;

    MMID = mi ? MMID : (uint32_t) ASID;
    for (i = 0; i < env->tlb->nb_tlb; i++) {
        tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        tag = env->CP0_EntryHi & ~mask;
        VPN = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
        tag &= env->SEGMask;
#endif
        tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
        /* Check ASID/MMID, virtual page number & size */
        if ((tlb->G == 1 || tlb_mmid == MMID) && VPN == tag && !tlb->EHINV) {
            /* TLB match */
            env->CP0_Index = i;
            break;
        }
    }
    if (i == env->tlb->nb_tlb) {
        /* No match.  Discard any shadow entries, if any of them match.  */
        for (i = env->tlb->nb_tlb; i < env->tlb->tlb_in_use; i++) {
            tlb = &env->tlb->mmu.r4k.tlb[i];
            /* 1k pages are not supported. */
            mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
            tag = env->CP0_EntryHi & ~mask;
            VPN = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
            tag &= env->SEGMask;
#endif
            tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
            /* Check ASID/MMID, virtual page number & size */
            if ((tlb->G == 1 || tlb_mmid == MMID) && VPN == tag) {
                r4k_mips_tlb_flush_extra(env, i);
                break;
            }
        }

        env->CP0_Index |= 0x80000000;
    }
}

static inline uint64_t get_entrylo_pfn_from_tlb(uint64_t tlb_pfn)
{
#if defined(TARGET_MIPS64)
    return tlb_pfn << 6;
#else
    return (extract64(tlb_pfn, 0, 24) << 6) | /* PFN */
           (extract64(tlb_pfn, 24, 32) << 32); /* PFNX */
#endif
}

void r4k_helper_tlbr(CPUMIPSState *env)
{
    bool mi = !!((env->CP0_Config5 >> CP0C5_MI) & 1);
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    uint32_t MMID = env->CP0_MemoryMapID;
    uint32_t tlb_mmid;
    r4k_tlb_t *tlb;
    int idx;

    MMID = mi ? MMID : (uint32_t) ASID;
    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];

    tlb_mmid = mi ? tlb->MMID : (uint32_t) tlb->ASID;
    /* If this will change the current ASID/MMID, flush qemu's TLB.  */
    if (MMID != tlb_mmid) {
        cpu_mips_tlb_flush(env);
    }

    r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);

    if (tlb->EHINV) {
        env->CP0_EntryHi = 1 << CP0EnHi_EHINV;
        env->CP0_PageMask = 0;
        env->CP0_EntryLo0 = 0;
        env->CP0_EntryLo1 = 0;
    } else {
        env->CP0_EntryHi = mi ? tlb->VPN : tlb->VPN | tlb->ASID;
        env->CP0_MemoryMapID = tlb->MMID;
        env->CP0_PageMask = tlb->PageMask;
        env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
                        ((uint64_t)tlb->RI0 << CP0EnLo_RI) |
                        ((uint64_t)tlb->XI0 << CP0EnLo_XI) | (tlb->C0 << 3) |
                        get_entrylo_pfn_from_tlb(tlb->PFN[0] >> 12);
        env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
                        ((uint64_t)tlb->RI1 << CP0EnLo_RI) |
                        ((uint64_t)tlb->XI1 << CP0EnLo_XI) | (tlb->C1 << 3) |
                        get_entrylo_pfn_from_tlb(tlb->PFN[1] >> 12);
    }
}

void helper_tlbwi(CPUMIPSState *env)
{
    env->tlb->helper_tlbwi(env);
}

void helper_tlbwr(CPUMIPSState *env)
{
    env->tlb->helper_tlbwr(env);
}

void helper_tlbp(CPUMIPSState *env)
{
    env->tlb->helper_tlbp(env);
}

void helper_tlbr(CPUMIPSState *env)
{
    env->tlb->helper_tlbr(env);
}

void helper_tlbinv(CPUMIPSState *env)
{
    env->tlb->helper_tlbinv(env);
}

void helper_tlbinvf(CPUMIPSState *env)
{
    env->tlb->helper_tlbinvf(env);
}

static void global_invalidate_tlb(CPUMIPSState *env,
                           uint32_t invMsgVPN2,
                           uint8_t invMsgR,
                           uint32_t invMsgMMid,
                           bool invAll,
                           bool invVAMMid,
                           bool invMMid,
                           bool invVA)
{

    int idx;
    r4k_tlb_t *tlb;
    bool VAMatch;
    bool MMidMatch;

    for (idx = 0; idx < env->tlb->nb_tlb; idx++) {
        tlb = &env->tlb->mmu.r4k.tlb[idx];
        VAMatch =
            (((tlb->VPN & ~tlb->PageMask) == (invMsgVPN2 & ~tlb->PageMask))
#ifdef TARGET_MIPS64
            &&
            (extract64(env->CP0_EntryHi, 62, 2) == invMsgR)
#endif
            );
        MMidMatch = tlb->MMID == invMsgMMid;
        if ((invAll && (idx > env->CP0_Wired)) ||
            (VAMatch && invVAMMid && (tlb->G || MMidMatch)) ||
            (VAMatch && invVA) ||
            (MMidMatch && !(tlb->G) && invMMid)) {
            tlb->EHINV = 1;
        }
    }
    cpu_mips_tlb_flush(env);
}

void helper_ginvt(CPUMIPSState *env, target_ulong arg, uint32_t type)
{
    bool invAll = type == 0;
    bool invVA = type == 1;
    bool invMMid = type == 2;
    bool invVAMMid = type == 3;
    uint32_t invMsgVPN2 = arg & (TARGET_PAGE_MASK << 1);
    uint8_t invMsgR = 0;
    uint32_t invMsgMMid = env->CP0_MemoryMapID;
    CPUState *other_cs = first_cpu;

#ifdef TARGET_MIPS64
    invMsgR = extract64(arg, 62, 2);
#endif

    CPU_FOREACH(other_cs) {
        MIPSCPU *other_cpu = MIPS_CPU(other_cs);
        global_invalidate_tlb(&other_cpu->env, invMsgVPN2, invMsgR, invMsgMMid,
                              invAll, invVAMMid, invMMid, invVA);
    }
}

/* Specials */
target_ulong helper_di(CPUMIPSState *env)
{
    target_ulong t0 = env->CP0_Status;

    env->CP0_Status = t0 & ~(1 << CP0St_IE);
    return t0;
}

target_ulong helper_ei(CPUMIPSState *env)
{
    target_ulong t0 = env->CP0_Status;

    env->CP0_Status = t0 | (1 << CP0St_IE);
    return t0;
}

static void debug_pre_eret(CPUMIPSState *env)
{
    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("ERET: PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
                env->active_tc.PC, env->CP0_EPC);
        if (env->CP0_Status & (1 << CP0St_ERL)) {
            qemu_log(" ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
        }
        if (env->hflags & MIPS_HFLAG_DM) {
            qemu_log(" DEPC " TARGET_FMT_lx, env->CP0_DEPC);
        }
        qemu_log("\n");
    }
}

static void debug_post_eret(CPUMIPSState *env)
{
    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("  =>  PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
                env->active_tc.PC, env->CP0_EPC);
        if (env->CP0_Status & (1 << CP0St_ERL)) {
            qemu_log(" ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
        }
        if (env->hflags & MIPS_HFLAG_DM) {
            qemu_log(" DEPC " TARGET_FMT_lx, env->CP0_DEPC);
        }
        switch (cpu_mmu_index(env, false)) {
        case 3:
            qemu_log(", ERL\n");
            break;
        case MIPS_HFLAG_UM:
            qemu_log(", UM\n");
            break;
        case MIPS_HFLAG_SM:
            qemu_log(", SM\n");
            break;
        case MIPS_HFLAG_KM:
            qemu_log("\n");
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid MMU mode!\n");
            break;
        }
    }
}

static void set_pc(CPUMIPSState *env, target_ulong error_pc)
{
    env->active_tc.PC = error_pc & ~(target_ulong)1;
    if (error_pc & 1) {
        env->hflags |= MIPS_HFLAG_M16;
    } else {
        env->hflags &= ~(MIPS_HFLAG_M16);
    }
}

static inline void exception_return(CPUMIPSState *env)
{
    debug_pre_eret(env);
    if (env->CP0_Status & (1 << CP0St_ERL)) {
        set_pc(env, env->CP0_ErrorEPC);
        env->CP0_Status &= ~(1 << CP0St_ERL);
    } else {
        set_pc(env, env->CP0_EPC);
        env->CP0_Status &= ~(1 << CP0St_EXL);
    }
    compute_hflags(env);
    debug_post_eret(env);
}

void helper_eret(CPUMIPSState *env)
{
    exception_return(env);
    env->CP0_LLAddr = 1;
    env->lladdr = 1;
}

void helper_eretnc(CPUMIPSState *env)
{
    exception_return(env);
}

void helper_deret(CPUMIPSState *env)
{
    debug_pre_eret(env);

    env->hflags &= ~MIPS_HFLAG_DM;
    compute_hflags(env);

    set_pc(env, env->CP0_DEPC);

    debug_post_eret(env);
}
#endif /* !CONFIG_USER_ONLY */

static inline void check_hwrena(CPUMIPSState *env, int reg, uintptr_t pc)
{
    if ((env->hflags & MIPS_HFLAG_CP0) || (env->CP0_HWREna & (1 << reg))) {
        return;
    }
    do_raise_exception(env, EXCP_RI, pc);
}

target_ulong helper_rdhwr_cpunum(CPUMIPSState *env)
{
    check_hwrena(env, 0, GETPC());
    return env->CP0_EBase & 0x3ff;
}

target_ulong helper_rdhwr_synci_step(CPUMIPSState *env)
{
    check_hwrena(env, 1, GETPC());
    return env->SYNCI_Step;
}

target_ulong helper_rdhwr_cc(CPUMIPSState *env)
{
    check_hwrena(env, 2, GETPC());
#ifdef CONFIG_USER_ONLY
    return env->CP0_Count;
#else
    return (int32_t)cpu_mips_get_count(env);
#endif
}

target_ulong helper_rdhwr_ccres(CPUMIPSState *env)
{
    check_hwrena(env, 3, GETPC());
    return env->CCRes;
}

target_ulong helper_rdhwr_performance(CPUMIPSState *env)
{
    check_hwrena(env, 4, GETPC());
    return env->CP0_Performance0;
}

target_ulong helper_rdhwr_xnp(CPUMIPSState *env)
{
    check_hwrena(env, 5, GETPC());
    return (env->CP0_Config5 >> CP0C5_XNP) & 1;
}

void helper_pmon(CPUMIPSState *env, int function)
{
    function /= 2;
    switch (function) {
    case 2: /* TODO: char inbyte(int waitflag); */
        if (env->active_tc.gpr[4] == 0) {
            env->active_tc.gpr[2] = -1;
        }
        /* Fall through */
    case 11: /* TODO: char inbyte (void); */
        env->active_tc.gpr[2] = -1;
        break;
    case 3:
    case 12:
        printf("%c", (char)(env->active_tc.gpr[4] & 0xFF));
        break;
    case 17:
        break;
    case 158:
        {
            unsigned char *fmt = (void *)(uintptr_t)env->active_tc.gpr[4];
            printf("%s", fmt);
        }
        break;
    }
}

void helper_wait(CPUMIPSState *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    cpu_reset_interrupt(cs, CPU_INTERRUPT_WAKE);
    /*
     * Last instruction in the block, PC was updated before
     * - no need to recover PC and icount.
     */
    raise_exception(env, EXCP_HLT);
}

#if !defined(CONFIG_USER_ONLY)

void mips_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                  MMUAccessType access_type,
                                  int mmu_idx, uintptr_t retaddr)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int error_code = 0;
    int excp;

    if (!(env->hflags & MIPS_HFLAG_DM)) {
        env->CP0_BadVAddr = addr;
    }

    if (access_type == MMU_DATA_STORE) {
        excp = EXCP_AdES;
    } else {
        excp = EXCP_AdEL;
        if (access_type == MMU_INST_FETCH) {
            error_code |= EXCP_INST_NOTAVAIL;
        }
    }

    do_raise_exception_err(env, excp, error_code, retaddr);
}

void mips_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                    vaddr addr, unsigned size,
                                    MMUAccessType access_type,
                                    int mmu_idx, MemTxAttrs attrs,
                                    MemTxResult response, uintptr_t retaddr)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;

    if (access_type == MMU_INST_FETCH) {
        do_raise_exception(env, EXCP_IBE, retaddr);
    } else {
        do_raise_exception(env, EXCP_DBE, retaddr);
    }
}
#endif /* !CONFIG_USER_ONLY */


/* MSA */
/* Data format min and max values */
#define DF_BITS(df) (1 << ((df) + 3))

/* Element-by-element access macros */
#define DF_ELEMENTS(df) (MSA_WRLEN / DF_BITS(df))

#if !defined(CONFIG_USER_ONLY)
#define MEMOP_IDX(DF)                                           \
        TCGMemOpIdx oi = make_memop_idx(MO_TE | DF | MO_UNALN,  \
                                        cpu_mmu_index(env, false));
#else
#define MEMOP_IDX(DF)
#endif

void helper_msa_ld_b(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    MEMOP_IDX(DF_BYTE)
#if !defined(CONFIG_USER_ONLY)
#if !defined(HOST_WORDS_BIGENDIAN)
    pwd->b[0]  = helper_ret_ldub_mmu(env, addr + (0  << DF_BYTE), oi, GETPC());
    pwd->b[1]  = helper_ret_ldub_mmu(env, addr + (1  << DF_BYTE), oi, GETPC());
    pwd->b[2]  = helper_ret_ldub_mmu(env, addr + (2  << DF_BYTE), oi, GETPC());
    pwd->b[3]  = helper_ret_ldub_mmu(env, addr + (3  << DF_BYTE), oi, GETPC());
    pwd->b[4]  = helper_ret_ldub_mmu(env, addr + (4  << DF_BYTE), oi, GETPC());
    pwd->b[5]  = helper_ret_ldub_mmu(env, addr + (5  << DF_BYTE), oi, GETPC());
    pwd->b[6]  = helper_ret_ldub_mmu(env, addr + (6  << DF_BYTE), oi, GETPC());
    pwd->b[7]  = helper_ret_ldub_mmu(env, addr + (7  << DF_BYTE), oi, GETPC());
    pwd->b[8]  = helper_ret_ldub_mmu(env, addr + (8  << DF_BYTE), oi, GETPC());
    pwd->b[9]  = helper_ret_ldub_mmu(env, addr + (9  << DF_BYTE), oi, GETPC());
    pwd->b[10] = helper_ret_ldub_mmu(env, addr + (10 << DF_BYTE), oi, GETPC());
    pwd->b[11] = helper_ret_ldub_mmu(env, addr + (11 << DF_BYTE), oi, GETPC());
    pwd->b[12] = helper_ret_ldub_mmu(env, addr + (12 << DF_BYTE), oi, GETPC());
    pwd->b[13] = helper_ret_ldub_mmu(env, addr + (13 << DF_BYTE), oi, GETPC());
    pwd->b[14] = helper_ret_ldub_mmu(env, addr + (14 << DF_BYTE), oi, GETPC());
    pwd->b[15] = helper_ret_ldub_mmu(env, addr + (15 << DF_BYTE), oi, GETPC());
#else
    pwd->b[0]  = helper_ret_ldub_mmu(env, addr + (7  << DF_BYTE), oi, GETPC());
    pwd->b[1]  = helper_ret_ldub_mmu(env, addr + (6  << DF_BYTE), oi, GETPC());
    pwd->b[2]  = helper_ret_ldub_mmu(env, addr + (5  << DF_BYTE), oi, GETPC());
    pwd->b[3]  = helper_ret_ldub_mmu(env, addr + (4  << DF_BYTE), oi, GETPC());
    pwd->b[4]  = helper_ret_ldub_mmu(env, addr + (3  << DF_BYTE), oi, GETPC());
    pwd->b[5]  = helper_ret_ldub_mmu(env, addr + (2  << DF_BYTE), oi, GETPC());
    pwd->b[6]  = helper_ret_ldub_mmu(env, addr + (1  << DF_BYTE), oi, GETPC());
    pwd->b[7]  = helper_ret_ldub_mmu(env, addr + (0  << DF_BYTE), oi, GETPC());
    pwd->b[8]  = helper_ret_ldub_mmu(env, addr + (15 << DF_BYTE), oi, GETPC());
    pwd->b[9]  = helper_ret_ldub_mmu(env, addr + (14 << DF_BYTE), oi, GETPC());
    pwd->b[10] = helper_ret_ldub_mmu(env, addr + (13 << DF_BYTE), oi, GETPC());
    pwd->b[11] = helper_ret_ldub_mmu(env, addr + (12 << DF_BYTE), oi, GETPC());
    pwd->b[12] = helper_ret_ldub_mmu(env, addr + (11 << DF_BYTE), oi, GETPC());
    pwd->b[13] = helper_ret_ldub_mmu(env, addr + (10 << DF_BYTE), oi, GETPC());
    pwd->b[14] = helper_ret_ldub_mmu(env, addr + (9  << DF_BYTE), oi, GETPC());
    pwd->b[15] = helper_ret_ldub_mmu(env, addr + (8  << DF_BYTE), oi, GETPC());
#endif
#else
#if !defined(HOST_WORDS_BIGENDIAN)
    pwd->b[0]  = cpu_ldub_data(env, addr + (0  << DF_BYTE));
    pwd->b[1]  = cpu_ldub_data(env, addr + (1  << DF_BYTE));
    pwd->b[2]  = cpu_ldub_data(env, addr + (2  << DF_BYTE));
    pwd->b[3]  = cpu_ldub_data(env, addr + (3  << DF_BYTE));
    pwd->b[4]  = cpu_ldub_data(env, addr + (4  << DF_BYTE));
    pwd->b[5]  = cpu_ldub_data(env, addr + (5  << DF_BYTE));
    pwd->b[6]  = cpu_ldub_data(env, addr + (6  << DF_BYTE));
    pwd->b[7]  = cpu_ldub_data(env, addr + (7  << DF_BYTE));
    pwd->b[8]  = cpu_ldub_data(env, addr + (8  << DF_BYTE));
    pwd->b[9]  = cpu_ldub_data(env, addr + (9  << DF_BYTE));
    pwd->b[10] = cpu_ldub_data(env, addr + (10 << DF_BYTE));
    pwd->b[11] = cpu_ldub_data(env, addr + (11 << DF_BYTE));
    pwd->b[12] = cpu_ldub_data(env, addr + (12 << DF_BYTE));
    pwd->b[13] = cpu_ldub_data(env, addr + (13 << DF_BYTE));
    pwd->b[14] = cpu_ldub_data(env, addr + (14 << DF_BYTE));
    pwd->b[15] = cpu_ldub_data(env, addr + (15 << DF_BYTE));
#else
    pwd->b[0]  = cpu_ldub_data(env, addr + (7  << DF_BYTE));
    pwd->b[1]  = cpu_ldub_data(env, addr + (6  << DF_BYTE));
    pwd->b[2]  = cpu_ldub_data(env, addr + (5  << DF_BYTE));
    pwd->b[3]  = cpu_ldub_data(env, addr + (4  << DF_BYTE));
    pwd->b[4]  = cpu_ldub_data(env, addr + (3  << DF_BYTE));
    pwd->b[5]  = cpu_ldub_data(env, addr + (2  << DF_BYTE));
    pwd->b[6]  = cpu_ldub_data(env, addr + (1  << DF_BYTE));
    pwd->b[7]  = cpu_ldub_data(env, addr + (0  << DF_BYTE));
    pwd->b[8]  = cpu_ldub_data(env, addr + (15 << DF_BYTE));
    pwd->b[9]  = cpu_ldub_data(env, addr + (14 << DF_BYTE));
    pwd->b[10] = cpu_ldub_data(env, addr + (13 << DF_BYTE));
    pwd->b[11] = cpu_ldub_data(env, addr + (12 << DF_BYTE));
    pwd->b[12] = cpu_ldub_data(env, addr + (11 << DF_BYTE));
    pwd->b[13] = cpu_ldub_data(env, addr + (10 << DF_BYTE));
    pwd->b[14] = cpu_ldub_data(env, addr + (9 << DF_BYTE));
    pwd->b[15] = cpu_ldub_data(env, addr + (8 << DF_BYTE));
#endif
#endif
}

void helper_msa_ld_h(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    MEMOP_IDX(DF_HALF)
#if !defined(CONFIG_USER_ONLY)
#if !defined(HOST_WORDS_BIGENDIAN)
    pwd->h[0] = helper_ret_lduw_mmu(env, addr + (0 << DF_HALF), oi, GETPC());
    pwd->h[1] = helper_ret_lduw_mmu(env, addr + (1 << DF_HALF), oi, GETPC());
    pwd->h[2] = helper_ret_lduw_mmu(env, addr + (2 << DF_HALF), oi, GETPC());
    pwd->h[3] = helper_ret_lduw_mmu(env, addr + (3 << DF_HALF), oi, GETPC());
    pwd->h[4] = helper_ret_lduw_mmu(env, addr + (4 << DF_HALF), oi, GETPC());
    pwd->h[5] = helper_ret_lduw_mmu(env, addr + (5 << DF_HALF), oi, GETPC());
    pwd->h[6] = helper_ret_lduw_mmu(env, addr + (6 << DF_HALF), oi, GETPC());
    pwd->h[7] = helper_ret_lduw_mmu(env, addr + (7 << DF_HALF), oi, GETPC());
#else
    pwd->h[0] = helper_ret_lduw_mmu(env, addr + (3 << DF_HALF), oi, GETPC());
    pwd->h[1] = helper_ret_lduw_mmu(env, addr + (2 << DF_HALF), oi, GETPC());
    pwd->h[2] = helper_ret_lduw_mmu(env, addr + (1 << DF_HALF), oi, GETPC());
    pwd->h[3] = helper_ret_lduw_mmu(env, addr + (0 << DF_HALF), oi, GETPC());
    pwd->h[4] = helper_ret_lduw_mmu(env, addr + (7 << DF_HALF), oi, GETPC());
    pwd->h[5] = helper_ret_lduw_mmu(env, addr + (6 << DF_HALF), oi, GETPC());
    pwd->h[6] = helper_ret_lduw_mmu(env, addr + (5 << DF_HALF), oi, GETPC());
    pwd->h[7] = helper_ret_lduw_mmu(env, addr + (4 << DF_HALF), oi, GETPC());
#endif
#else
#if !defined(HOST_WORDS_BIGENDIAN)
    pwd->h[0] = cpu_lduw_data(env, addr + (0 << DF_HALF));
    pwd->h[1] = cpu_lduw_data(env, addr + (1 << DF_HALF));
    pwd->h[2] = cpu_lduw_data(env, addr + (2 << DF_HALF));
    pwd->h[3] = cpu_lduw_data(env, addr + (3 << DF_HALF));
    pwd->h[4] = cpu_lduw_data(env, addr + (4 << DF_HALF));
    pwd->h[5] = cpu_lduw_data(env, addr + (5 << DF_HALF));
    pwd->h[6] = cpu_lduw_data(env, addr + (6 << DF_HALF));
    pwd->h[7] = cpu_lduw_data(env, addr + (7 << DF_HALF));
#else
    pwd->h[0] = cpu_lduw_data(env, addr + (3 << DF_HALF));
    pwd->h[1] = cpu_lduw_data(env, addr + (2 << DF_HALF));
    pwd->h[2] = cpu_lduw_data(env, addr + (1 << DF_HALF));
    pwd->h[3] = cpu_lduw_data(env, addr + (0 << DF_HALF));
    pwd->h[4] = cpu_lduw_data(env, addr + (7 << DF_HALF));
    pwd->h[5] = cpu_lduw_data(env, addr + (6 << DF_HALF));
    pwd->h[6] = cpu_lduw_data(env, addr + (5 << DF_HALF));
    pwd->h[7] = cpu_lduw_data(env, addr + (4 << DF_HALF));
#endif
#endif
}

void helper_msa_ld_w(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    MEMOP_IDX(DF_WORD)
#if !defined(CONFIG_USER_ONLY)
#if !defined(HOST_WORDS_BIGENDIAN)
    pwd->w[0] = helper_ret_ldul_mmu(env, addr + (0 << DF_WORD), oi, GETPC());
    pwd->w[1] = helper_ret_ldul_mmu(env, addr + (1 << DF_WORD), oi, GETPC());
    pwd->w[2] = helper_ret_ldul_mmu(env, addr + (2 << DF_WORD), oi, GETPC());
    pwd->w[3] = helper_ret_ldul_mmu(env, addr + (3 << DF_WORD), oi, GETPC());
#else
    pwd->w[0] = helper_ret_ldul_mmu(env, addr + (1 << DF_WORD), oi, GETPC());
    pwd->w[1] = helper_ret_ldul_mmu(env, addr + (0 << DF_WORD), oi, GETPC());
    pwd->w[2] = helper_ret_ldul_mmu(env, addr + (3 << DF_WORD), oi, GETPC());
    pwd->w[3] = helper_ret_ldul_mmu(env, addr + (2 << DF_WORD), oi, GETPC());
#endif
#else
#if !defined(HOST_WORDS_BIGENDIAN)
    pwd->w[0] = cpu_ldl_data(env, addr + (0 << DF_WORD));
    pwd->w[1] = cpu_ldl_data(env, addr + (1 << DF_WORD));
    pwd->w[2] = cpu_ldl_data(env, addr + (2 << DF_WORD));
    pwd->w[3] = cpu_ldl_data(env, addr + (3 << DF_WORD));
#else
    pwd->w[0] = cpu_ldl_data(env, addr + (1 << DF_WORD));
    pwd->w[1] = cpu_ldl_data(env, addr + (0 << DF_WORD));
    pwd->w[2] = cpu_ldl_data(env, addr + (3 << DF_WORD));
    pwd->w[3] = cpu_ldl_data(env, addr + (2 << DF_WORD));
#endif
#endif
}

void helper_msa_ld_d(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    MEMOP_IDX(DF_DOUBLE)
#if !defined(CONFIG_USER_ONLY)
    pwd->d[0] = helper_ret_ldq_mmu(env, addr + (0 << DF_DOUBLE), oi, GETPC());
    pwd->d[1] = helper_ret_ldq_mmu(env, addr + (1 << DF_DOUBLE), oi, GETPC());
#else
    pwd->d[0] = cpu_ldq_data(env, addr + (0 << DF_DOUBLE));
    pwd->d[1] = cpu_ldq_data(env, addr + (1 << DF_DOUBLE));
#endif
}

#define MSA_PAGESPAN(x) \
        ((((x) & ~TARGET_PAGE_MASK) + MSA_WRLEN / 8 - 1) >= TARGET_PAGE_SIZE)

static inline void ensure_writable_pages(CPUMIPSState *env,
                                         target_ulong addr,
                                         int mmu_idx,
                                         uintptr_t retaddr)
{
    /* FIXME: Probe the actual accesses (pass and use a size) */
    if (unlikely(MSA_PAGESPAN(addr))) {
        /* first page */
        probe_write(env, addr, 0, mmu_idx, retaddr);
        /* second page */
        addr = (addr & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
        probe_write(env, addr, 0, mmu_idx, retaddr);
    }
}

void helper_msa_st_b(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);

    MEMOP_IDX(DF_BYTE)
    ensure_writable_pages(env, addr, mmu_idx, GETPC());
#if !defined(CONFIG_USER_ONLY)
#if !defined(HOST_WORDS_BIGENDIAN)
    helper_ret_stb_mmu(env, addr + (0  << DF_BYTE), pwd->b[0],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (1  << DF_BYTE), pwd->b[1],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (2  << DF_BYTE), pwd->b[2],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (3  << DF_BYTE), pwd->b[3],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (4  << DF_BYTE), pwd->b[4],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (5  << DF_BYTE), pwd->b[5],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (6  << DF_BYTE), pwd->b[6],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (7  << DF_BYTE), pwd->b[7],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (8  << DF_BYTE), pwd->b[8],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (9  << DF_BYTE), pwd->b[9],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (10 << DF_BYTE), pwd->b[10], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (11 << DF_BYTE), pwd->b[11], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (12 << DF_BYTE), pwd->b[12], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (13 << DF_BYTE), pwd->b[13], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (14 << DF_BYTE), pwd->b[14], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (15 << DF_BYTE), pwd->b[15], oi, GETPC());
#else
    helper_ret_stb_mmu(env, addr + (7  << DF_BYTE), pwd->b[0],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (6  << DF_BYTE), pwd->b[1],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (5  << DF_BYTE), pwd->b[2],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (4  << DF_BYTE), pwd->b[3],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (3  << DF_BYTE), pwd->b[4],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (2  << DF_BYTE), pwd->b[5],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (1  << DF_BYTE), pwd->b[6],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (0  << DF_BYTE), pwd->b[7],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (15 << DF_BYTE), pwd->b[8],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (14 << DF_BYTE), pwd->b[9],  oi, GETPC());
    helper_ret_stb_mmu(env, addr + (13 << DF_BYTE), pwd->b[10], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (12 << DF_BYTE), pwd->b[11], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (11 << DF_BYTE), pwd->b[12], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (10 << DF_BYTE), pwd->b[13], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (9  << DF_BYTE), pwd->b[14], oi, GETPC());
    helper_ret_stb_mmu(env, addr + (8  << DF_BYTE), pwd->b[15], oi, GETPC());
#endif
#else
#if !defined(HOST_WORDS_BIGENDIAN)
    cpu_stb_data(env, addr + (0  << DF_BYTE), pwd->b[0]);
    cpu_stb_data(env, addr + (1  << DF_BYTE), pwd->b[1]);
    cpu_stb_data(env, addr + (2  << DF_BYTE), pwd->b[2]);
    cpu_stb_data(env, addr + (3  << DF_BYTE), pwd->b[3]);
    cpu_stb_data(env, addr + (4  << DF_BYTE), pwd->b[4]);
    cpu_stb_data(env, addr + (5  << DF_BYTE), pwd->b[5]);
    cpu_stb_data(env, addr + (6  << DF_BYTE), pwd->b[6]);
    cpu_stb_data(env, addr + (7  << DF_BYTE), pwd->b[7]);
    cpu_stb_data(env, addr + (8  << DF_BYTE), pwd->b[8]);
    cpu_stb_data(env, addr + (9  << DF_BYTE), pwd->b[9]);
    cpu_stb_data(env, addr + (10 << DF_BYTE), pwd->b[10]);
    cpu_stb_data(env, addr + (11 << DF_BYTE), pwd->b[11]);
    cpu_stb_data(env, addr + (12 << DF_BYTE), pwd->b[12]);
    cpu_stb_data(env, addr + (13 << DF_BYTE), pwd->b[13]);
    cpu_stb_data(env, addr + (14 << DF_BYTE), pwd->b[14]);
    cpu_stb_data(env, addr + (15 << DF_BYTE), pwd->b[15]);
#else
    cpu_stb_data(env, addr + (7  << DF_BYTE), pwd->b[0]);
    cpu_stb_data(env, addr + (6  << DF_BYTE), pwd->b[1]);
    cpu_stb_data(env, addr + (5  << DF_BYTE), pwd->b[2]);
    cpu_stb_data(env, addr + (4  << DF_BYTE), pwd->b[3]);
    cpu_stb_data(env, addr + (3  << DF_BYTE), pwd->b[4]);
    cpu_stb_data(env, addr + (2  << DF_BYTE), pwd->b[5]);
    cpu_stb_data(env, addr + (1  << DF_BYTE), pwd->b[6]);
    cpu_stb_data(env, addr + (0  << DF_BYTE), pwd->b[7]);
    cpu_stb_data(env, addr + (15 << DF_BYTE), pwd->b[8]);
    cpu_stb_data(env, addr + (14 << DF_BYTE), pwd->b[9]);
    cpu_stb_data(env, addr + (13 << DF_BYTE), pwd->b[10]);
    cpu_stb_data(env, addr + (12 << DF_BYTE), pwd->b[11]);
    cpu_stb_data(env, addr + (11 << DF_BYTE), pwd->b[12]);
    cpu_stb_data(env, addr + (10 << DF_BYTE), pwd->b[13]);
    cpu_stb_data(env, addr + (9  << DF_BYTE), pwd->b[14]);
    cpu_stb_data(env, addr + (8  << DF_BYTE), pwd->b[15]);
#endif
#endif
}

void helper_msa_st_h(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);

    MEMOP_IDX(DF_HALF)
    ensure_writable_pages(env, addr, mmu_idx, GETPC());
#if !defined(CONFIG_USER_ONLY)
#if !defined(HOST_WORDS_BIGENDIAN)
    helper_ret_stw_mmu(env, addr + (0 << DF_HALF), pwd->h[0], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (1 << DF_HALF), pwd->h[1], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (2 << DF_HALF), pwd->h[2], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (3 << DF_HALF), pwd->h[3], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (4 << DF_HALF), pwd->h[4], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (5 << DF_HALF), pwd->h[5], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (6 << DF_HALF), pwd->h[6], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (7 << DF_HALF), pwd->h[7], oi, GETPC());
#else
    helper_ret_stw_mmu(env, addr + (3 << DF_HALF), pwd->h[0], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (2 << DF_HALF), pwd->h[1], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (1 << DF_HALF), pwd->h[2], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (0 << DF_HALF), pwd->h[3], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (7 << DF_HALF), pwd->h[4], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (6 << DF_HALF), pwd->h[5], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (5 << DF_HALF), pwd->h[6], oi, GETPC());
    helper_ret_stw_mmu(env, addr + (4 << DF_HALF), pwd->h[7], oi, GETPC());
#endif
#else
#if !defined(HOST_WORDS_BIGENDIAN)
    cpu_stw_data(env, addr + (0 << DF_HALF), pwd->h[0]);
    cpu_stw_data(env, addr + (1 << DF_HALF), pwd->h[1]);
    cpu_stw_data(env, addr + (2 << DF_HALF), pwd->h[2]);
    cpu_stw_data(env, addr + (3 << DF_HALF), pwd->h[3]);
    cpu_stw_data(env, addr + (4 << DF_HALF), pwd->h[4]);
    cpu_stw_data(env, addr + (5 << DF_HALF), pwd->h[5]);
    cpu_stw_data(env, addr + (6 << DF_HALF), pwd->h[6]);
    cpu_stw_data(env, addr + (7 << DF_HALF), pwd->h[7]);
#else
    cpu_stw_data(env, addr + (3 << DF_HALF), pwd->h[0]);
    cpu_stw_data(env, addr + (2 << DF_HALF), pwd->h[1]);
    cpu_stw_data(env, addr + (1 << DF_HALF), pwd->h[2]);
    cpu_stw_data(env, addr + (0 << DF_HALF), pwd->h[3]);
    cpu_stw_data(env, addr + (7 << DF_HALF), pwd->h[4]);
    cpu_stw_data(env, addr + (6 << DF_HALF), pwd->h[5]);
    cpu_stw_data(env, addr + (5 << DF_HALF), pwd->h[6]);
    cpu_stw_data(env, addr + (4 << DF_HALF), pwd->h[7]);
#endif
#endif
}

void helper_msa_st_w(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);

    MEMOP_IDX(DF_WORD)
    ensure_writable_pages(env, addr, mmu_idx, GETPC());
#if !defined(CONFIG_USER_ONLY)
#if !defined(HOST_WORDS_BIGENDIAN)
    helper_ret_stl_mmu(env, addr + (0 << DF_WORD), pwd->w[0], oi, GETPC());
    helper_ret_stl_mmu(env, addr + (1 << DF_WORD), pwd->w[1], oi, GETPC());
    helper_ret_stl_mmu(env, addr + (2 << DF_WORD), pwd->w[2], oi, GETPC());
    helper_ret_stl_mmu(env, addr + (3 << DF_WORD), pwd->w[3], oi, GETPC());
#else
    helper_ret_stl_mmu(env, addr + (1 << DF_WORD), pwd->w[0], oi, GETPC());
    helper_ret_stl_mmu(env, addr + (0 << DF_WORD), pwd->w[1], oi, GETPC());
    helper_ret_stl_mmu(env, addr + (3 << DF_WORD), pwd->w[2], oi, GETPC());
    helper_ret_stl_mmu(env, addr + (2 << DF_WORD), pwd->w[3], oi, GETPC());
#endif
#else
#if !defined(HOST_WORDS_BIGENDIAN)
    cpu_stl_data(env, addr + (0 << DF_WORD), pwd->w[0]);
    cpu_stl_data(env, addr + (1 << DF_WORD), pwd->w[1]);
    cpu_stl_data(env, addr + (2 << DF_WORD), pwd->w[2]);
    cpu_stl_data(env, addr + (3 << DF_WORD), pwd->w[3]);
#else
    cpu_stl_data(env, addr + (1 << DF_WORD), pwd->w[0]);
    cpu_stl_data(env, addr + (0 << DF_WORD), pwd->w[1]);
    cpu_stl_data(env, addr + (3 << DF_WORD), pwd->w[2]);
    cpu_stl_data(env, addr + (2 << DF_WORD), pwd->w[3]);
#endif
#endif
}

void helper_msa_st_d(CPUMIPSState *env, uint32_t wd,
                     target_ulong addr)
{
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);
    int mmu_idx = cpu_mmu_index(env, false);

    MEMOP_IDX(DF_DOUBLE)
    ensure_writable_pages(env, addr, mmu_idx, GETPC());
#if !defined(CONFIG_USER_ONLY)
    helper_ret_stq_mmu(env, addr + (0 << DF_DOUBLE), pwd->d[0], oi, GETPC());
    helper_ret_stq_mmu(env, addr + (1 << DF_DOUBLE), pwd->d[1], oi, GETPC());
#else
    cpu_stq_data(env, addr + (0 << DF_DOUBLE), pwd->d[0]);
    cpu_stq_data(env, addr + (1 << DF_DOUBLE), pwd->d[1]);
#endif
}

void helper_cache(CPUMIPSState *env, target_ulong addr, uint32_t op)
{
#ifndef CONFIG_USER_ONLY
    target_ulong index = addr & 0x1fffffff;
    if (op == 9) {
        /* Index Store Tag */
        memory_region_dispatch_write(env->itc_tag, index, env->CP0_TagLo,
                                     MO_64, MEMTXATTRS_UNSPECIFIED);
    } else if (op == 5) {
        /* Index Load Tag */
        memory_region_dispatch_read(env->itc_tag, index, &env->CP0_TagLo,
                                    MO_64, MEMTXATTRS_UNSPECIFIED);
    }
#endif
}
