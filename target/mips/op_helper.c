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
#include "fpu/softfloat.h"


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

/* Complex FPU operations which may need stack space. */

#define FLOAT_TWO32 make_float32(1 << 30)
#define FLOAT_TWO64 make_float64(1ULL << 62)

#define FP_TO_INT32_OVERFLOW 0x7fffffff
#define FP_TO_INT64_OVERFLOW 0x7fffffffffffffffULL

/* convert MIPS rounding mode in FCR31 to IEEE library */
unsigned int ieee_rm[] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

target_ulong helper_cfc1(CPUMIPSState *env, uint32_t reg)
{
    target_ulong arg1 = 0;

    switch (reg) {
    case 0:
        arg1 = (int32_t)env->active_fpu.fcr0;
        break;
    case 1:
        /* UFR Support - Read Status FR */
        if (env->active_fpu.fcr0 & (1 << FCR0_UFRP)) {
            if (env->CP0_Config5 & (1 << CP0C5_UFR)) {
                arg1 = (int32_t)
                       ((env->CP0_Status & (1  << CP0St_FR)) >> CP0St_FR);
            } else {
                do_raise_exception(env, EXCP_RI, GETPC());
            }
        }
        break;
    case 5:
        /* FRE Support - read Config5.FRE bit */
        if (env->active_fpu.fcr0 & (1 << FCR0_FREP)) {
            if (env->CP0_Config5 & (1 << CP0C5_UFE)) {
                arg1 = (env->CP0_Config5 >> CP0C5_FRE) & 1;
            } else {
                helper_raise_exception(env, EXCP_RI);
            }
        }
        break;
    case 25:
        arg1 = ((env->active_fpu.fcr31 >> 24) & 0xfe) |
               ((env->active_fpu.fcr31 >> 23) & 0x1);
        break;
    case 26:
        arg1 = env->active_fpu.fcr31 & 0x0003f07c;
        break;
    case 28:
        arg1 = (env->active_fpu.fcr31 & 0x00000f83) |
               ((env->active_fpu.fcr31 >> 22) & 0x4);
        break;
    default:
        arg1 = (int32_t)env->active_fpu.fcr31;
        break;
    }

    return arg1;
}

void helper_ctc1(CPUMIPSState *env, target_ulong arg1, uint32_t fs, uint32_t rt)
{
    switch (fs) {
    case 1:
        /* UFR Alias - Reset Status FR */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_UFRP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFR)) {
            env->CP0_Status &= ~(1 << CP0St_FR);
            compute_hflags(env);
        } else {
            do_raise_exception(env, EXCP_RI, GETPC());
        }
        break;
    case 4:
        /* UNFR Alias - Set Status FR */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_UFRP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFR)) {
            env->CP0_Status |= (1 << CP0St_FR);
            compute_hflags(env);
        } else {
            do_raise_exception(env, EXCP_RI, GETPC());
        }
        break;
    case 5:
        /* FRE Support - clear Config5.FRE bit */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_FREP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFE)) {
            env->CP0_Config5 &= ~(1 << CP0C5_FRE);
            compute_hflags(env);
        } else {
            helper_raise_exception(env, EXCP_RI);
        }
        break;
    case 6:
        /* FRE Support - set Config5.FRE bit */
        if (!((env->active_fpu.fcr0 & (1 << FCR0_FREP)) && (rt == 0))) {
            return;
        }
        if (env->CP0_Config5 & (1 << CP0C5_UFE)) {
            env->CP0_Config5 |= (1 << CP0C5_FRE);
            compute_hflags(env);
        } else {
            helper_raise_exception(env, EXCP_RI);
        }
        break;
    case 25:
        if ((env->insn_flags & ISA_MIPS32R6) || (arg1 & 0xffffff00)) {
            return;
        }
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0x017fffff) |
                                ((arg1 & 0xfe) << 24) |
                                ((arg1 & 0x1) << 23);
        break;
    case 26:
        if (arg1 & 0x007c0000) {
            return;
        }
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfffc0f83) |
                                (arg1 & 0x0003f07c);
        break;
    case 28:
        if (arg1 & 0x007c0000) {
            return;
        }
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfefff07c) |
                                (arg1 & 0x00000f83) |
                                ((arg1 & 0x4) << 22);
        break;
    case 31:
        env->active_fpu.fcr31 = (arg1 & env->active_fpu.fcr31_rw_bitmask) |
               (env->active_fpu.fcr31 & ~(env->active_fpu.fcr31_rw_bitmask));
        break;
    default:
        if (env->insn_flags & ISA_MIPS32R6) {
            do_raise_exception(env, EXCP_RI, GETPC());
        }
        return;
    }
    restore_fp_status(env);
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    if ((GET_FP_ENABLE(env->active_fpu.fcr31) | 0x20) &
        GET_FP_CAUSE(env->active_fpu.fcr31)) {
        do_raise_exception(env, EXCP_FPE, GETPC());
    }
}

int ieee_ex_to_mips(int xcpt)
{
    int ret = 0;
    if (xcpt) {
        if (xcpt & float_flag_invalid) {
            ret |= FP_INVALID;
        }
        if (xcpt & float_flag_overflow) {
            ret |= FP_OVERFLOW;
        }
        if (xcpt & float_flag_underflow) {
            ret |= FP_UNDERFLOW;
        }
        if (xcpt & float_flag_divbyzero) {
            ret |= FP_DIV0;
        }
        if (xcpt & float_flag_inexact) {
            ret |= FP_INEXACT;
        }
    }
    return ret;
}

static inline void update_fcr31(CPUMIPSState *env, uintptr_t pc)
{
    int tmp = ieee_ex_to_mips(get_float_exception_flags(
                                  &env->active_fpu.fp_status));

    SET_FP_CAUSE(env->active_fpu.fcr31, tmp);

    if (tmp) {
        set_float_exception_flags(0, &env->active_fpu.fp_status);

        if (GET_FP_ENABLE(env->active_fpu.fcr31) & tmp) {
            do_raise_exception(env, EXCP_FPE, pc);
        } else {
            UPDATE_FP_FLAGS(env->active_fpu.fcr31, tmp);
        }
    }
}

/*
 * Float support.
 * Single precition routines have a "s" suffix, double precision a
 * "d" suffix, 32bit integer "w", 64bit integer "l", paired single "ps",
 * paired single lower "pl", paired single upper "pu".
 */

/* unary operations, modifying fp status  */
uint64_t helper_float_sqrt_d(CPUMIPSState *env, uint64_t fdt0)
{
    fdt0 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt0;
}

uint32_t helper_float_sqrt_s(CPUMIPSState *env, uint32_t fst0)
{
    fst0 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst0;
}

uint64_t helper_float_cvtd_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t fdt2;

    fdt2 = float32_to_float64(fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint64_t helper_float_cvtd_w(CPUMIPSState *env, uint32_t wt0)
{
    uint64_t fdt2;

    fdt2 = int32_to_float64(wt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint64_t helper_float_cvtd_l(CPUMIPSState *env, uint64_t dt0)
{
    uint64_t fdt2;

    fdt2 = int64_to_float64(dt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint64_t helper_float_cvt_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_cvt_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_cvtps_pw(CPUMIPSState *env, uint64_t dt0)
{
    uint32_t fst2;
    uint32_t fsth2;

    fst2 = int32_to_float32(dt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    fsth2 = int32_to_float32(dt0 >> 32, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_cvtpw_ps(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;
    uint32_t wth2;
    int excp, excph;

    wt2 = float32_to_int32(fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    excp = get_float_exception_flags(&env->active_fpu.fp_status);
    if (excp & (float_flag_overflow | float_flag_invalid)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }

    set_float_exception_flags(0, &env->active_fpu.fp_status);
    wth2 = float32_to_int32(fdt0 >> 32, &env->active_fpu.fp_status);
    excph = get_float_exception_flags(&env->active_fpu.fp_status);
    if (excph & (float_flag_overflow | float_flag_invalid)) {
        wth2 = FP_TO_INT32_OVERFLOW;
    }

    set_float_exception_flags(excp | excph, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());

    return ((uint64_t)wth2 << 32) | wt2;
}

uint32_t helper_float_cvts_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t fst2;

    fst2 = float64_to_float32(fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint32_t helper_float_cvts_w(CPUMIPSState *env, uint32_t wt0)
{
    uint32_t fst2;

    fst2 = int32_to_float32(wt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint32_t helper_float_cvts_l(CPUMIPSState *env, uint64_t dt0)
{
    uint32_t fst2;

    fst2 = int64_to_float32(dt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint32_t helper_float_cvts_pl(CPUMIPSState *env, uint32_t wt0)
{
    uint32_t wt2;

    wt2 = wt0;
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvts_pu(CPUMIPSState *env, uint32_t wth0)
{
    uint32_t wt2;

    wt2 = wth0;
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvt_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvt_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_round_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_round_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_round_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_round_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
                            &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_trunc_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64_round_to_zero(fdt0,
                                         &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_trunc_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_trunc_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32_round_to_zero(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_trunc_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_ceil_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_ceil_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_ceil_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_ceil_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_floor_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_floor_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        dt2 = FP_TO_INT64_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_floor_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_floor_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_cvt_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_cvt_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_cvt_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_cvt_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_round_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_round_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_round_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_round_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_nearest_even,
            &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_trunc_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    dt2 = float64_to_int64_round_to_zero(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_trunc_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    dt2 = float32_to_int64_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_trunc_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    wt2 = float64_to_int32_round_to_zero(fdt0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_trunc_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32_round_to_zero(fst0, &env->active_fpu.fp_status);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_ceil_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_ceil_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_ceil_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_ceil_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_up, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint64_t helper_float_floor_2008_l_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float64_to_int64(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint64_t helper_float_floor_2008_l_s(CPUMIPSState *env, uint32_t fst0)
{
    uint64_t dt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    dt2 = float32_to_int64(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            dt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return dt2;
}

uint32_t helper_float_floor_2008_w_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float64_to_int32(fdt0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float64_is_any_nan(fdt0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

uint32_t helper_float_floor_2008_w_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    set_float_rounding_mode(float_round_down, &env->active_fpu.fp_status);
    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    restore_rounding_mode(env);
    if (get_float_exception_flags(&env->active_fpu.fp_status)
            & float_flag_invalid) {
        if (float32_is_any_nan(fst0)) {
            wt2 = 0;
        }
    }
    update_fcr31(env, GETPC());
    return wt2;
}

/* unary operations, not modifying fp status  */
#define FLOAT_UNOP(name)                                       \
uint64_t helper_float_ ## name ## _d(uint64_t fdt0)                \
{                                                              \
    return float64_ ## name(fdt0);                             \
}                                                              \
uint32_t helper_float_ ## name ## _s(uint32_t fst0)                \
{                                                              \
    return float32_ ## name(fst0);                             \
}                                                              \
uint64_t helper_float_ ## name ## _ps(uint64_t fdt0)               \
{                                                              \
    uint32_t wt0;                                              \
    uint32_t wth0;                                             \
                                                               \
    wt0 = float32_ ## name(fdt0 & 0XFFFFFFFF);                 \
    wth0 = float32_ ## name(fdt0 >> 32);                       \
    return ((uint64_t)wth0 << 32) | wt0;                       \
}
FLOAT_UNOP(abs)
FLOAT_UNOP(chs)
#undef FLOAT_UNOP

/* MIPS specific unary operations */
uint64_t helper_float_recip_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_div(float64_one, fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_recip_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_div(float32_one, fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_rsqrt_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    fdt2 = float64_div(float64_one, fdt2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_rsqrt_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    fst2 = float32_div(float32_one, fst2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_recip1_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_div(float64_one, fdt0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_recip1_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_div(float32_one, fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_recip1_ps(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t fst2;
    uint32_t fsth2;

    fst2 = float32_div(float32_one, fdt0 & 0XFFFFFFFF,
                       &env->active_fpu.fp_status);
    fsth2 = float32_div(float32_one, fdt0 >> 32, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_rsqrt1_d(CPUMIPSState *env, uint64_t fdt0)
{
    uint64_t fdt2;

    fdt2 = float64_sqrt(fdt0, &env->active_fpu.fp_status);
    fdt2 = float64_div(float64_one, fdt2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_rsqrt1_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t fst2;

    fst2 = float32_sqrt(fst0, &env->active_fpu.fp_status);
    fst2 = float32_div(float32_one, fst2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_rsqrt1_ps(CPUMIPSState *env, uint64_t fdt0)
{
    uint32_t fst2;
    uint32_t fsth2;

    fst2 = float32_sqrt(fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
    fsth2 = float32_sqrt(fdt0 >> 32, &env->active_fpu.fp_status);
    fst2 = float32_div(float32_one, fst2, &env->active_fpu.fp_status);
    fsth2 = float32_div(float32_one, fsth2, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

#define FLOAT_RINT(name, bits)                                              \
uint ## bits ## _t helper_float_ ## name(CPUMIPSState *env,                 \
                                         uint ## bits ## _t fs)             \
{                                                                           \
    uint ## bits ## _t fdret;                                               \
                                                                            \
    fdret = float ## bits ## _round_to_int(fs, &env->active_fpu.fp_status); \
    update_fcr31(env, GETPC());                                             \
    return fdret;                                                           \
}

FLOAT_RINT(rint_s, 32)
FLOAT_RINT(rint_d, 64)
#undef FLOAT_RINT

#define FLOAT_CLASS_SIGNALING_NAN      0x001
#define FLOAT_CLASS_QUIET_NAN          0x002
#define FLOAT_CLASS_NEGATIVE_INFINITY  0x004
#define FLOAT_CLASS_NEGATIVE_NORMAL    0x008
#define FLOAT_CLASS_NEGATIVE_SUBNORMAL 0x010
#define FLOAT_CLASS_NEGATIVE_ZERO      0x020
#define FLOAT_CLASS_POSITIVE_INFINITY  0x040
#define FLOAT_CLASS_POSITIVE_NORMAL    0x080
#define FLOAT_CLASS_POSITIVE_SUBNORMAL 0x100
#define FLOAT_CLASS_POSITIVE_ZERO      0x200

#define FLOAT_CLASS(name, bits)                                      \
uint ## bits ## _t float_ ## name(uint ## bits ## _t arg,            \
                                  float_status *status)              \
{                                                                    \
    if (float ## bits ## _is_signaling_nan(arg, status)) {           \
        return FLOAT_CLASS_SIGNALING_NAN;                            \
    } else if (float ## bits ## _is_quiet_nan(arg, status)) {        \
        return FLOAT_CLASS_QUIET_NAN;                                \
    } else if (float ## bits ## _is_neg(arg)) {                      \
        if (float ## bits ## _is_infinity(arg)) {                    \
            return FLOAT_CLASS_NEGATIVE_INFINITY;                    \
        } else if (float ## bits ## _is_zero(arg)) {                 \
            return FLOAT_CLASS_NEGATIVE_ZERO;                        \
        } else if (float ## bits ## _is_zero_or_denormal(arg)) {     \
            return FLOAT_CLASS_NEGATIVE_SUBNORMAL;                   \
        } else {                                                     \
            return FLOAT_CLASS_NEGATIVE_NORMAL;                      \
        }                                                            \
    } else {                                                         \
        if (float ## bits ## _is_infinity(arg)) {                    \
            return FLOAT_CLASS_POSITIVE_INFINITY;                    \
        } else if (float ## bits ## _is_zero(arg)) {                 \
            return FLOAT_CLASS_POSITIVE_ZERO;                        \
        } else if (float ## bits ## _is_zero_or_denormal(arg)) {     \
            return FLOAT_CLASS_POSITIVE_SUBNORMAL;                   \
        } else {                                                     \
            return FLOAT_CLASS_POSITIVE_NORMAL;                      \
        }                                                            \
    }                                                                \
}                                                                    \
                                                                     \
uint ## bits ## _t helper_float_ ## name(CPUMIPSState *env,          \
                                         uint ## bits ## _t arg)     \
{                                                                    \
    return float_ ## name(arg, &env->active_fpu.fp_status);          \
}

FLOAT_CLASS(class_s, 32)
FLOAT_CLASS(class_d, 64)
#undef FLOAT_CLASS

/* binary operations */
#define FLOAT_BINOP(name)                                          \
uint64_t helper_float_ ## name ## _d(CPUMIPSState *env,            \
                                     uint64_t fdt0, uint64_t fdt1) \
{                                                                  \
    uint64_t dt2;                                                  \
                                                                   \
    dt2 = float64_ ## name(fdt0, fdt1, &env->active_fpu.fp_status);\
    update_fcr31(env, GETPC());                                    \
    return dt2;                                                    \
}                                                                  \
                                                                   \
uint32_t helper_float_ ## name ## _s(CPUMIPSState *env,            \
                                     uint32_t fst0, uint32_t fst1) \
{                                                                  \
    uint32_t wt2;                                                  \
                                                                   \
    wt2 = float32_ ## name(fst0, fst1, &env->active_fpu.fp_status);\
    update_fcr31(env, GETPC());                                    \
    return wt2;                                                    \
}                                                                  \
                                                                   \
uint64_t helper_float_ ## name ## _ps(CPUMIPSState *env,           \
                                      uint64_t fdt0,               \
                                      uint64_t fdt1)               \
{                                                                  \
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;                             \
    uint32_t fsth0 = fdt0 >> 32;                                   \
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;                             \
    uint32_t fsth1 = fdt1 >> 32;                                   \
    uint32_t wt2;                                                  \
    uint32_t wth2;                                                 \
                                                                   \
    wt2 = float32_ ## name(fst0, fst1, &env->active_fpu.fp_status);     \
    wth2 = float32_ ## name(fsth0, fsth1, &env->active_fpu.fp_status);  \
    update_fcr31(env, GETPC());                                    \
    return ((uint64_t)wth2 << 32) | wt2;                           \
}

FLOAT_BINOP(add)
FLOAT_BINOP(sub)
FLOAT_BINOP(mul)
FLOAT_BINOP(div)
#undef FLOAT_BINOP

/* MIPS specific binary operations */
uint64_t helper_float_recip2_d(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    fdt2 = float64_mul(fdt0, fdt2, &env->active_fpu.fp_status);
    fdt2 = float64_chs(float64_sub(fdt2, float64_one,
                                   &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_recip2_s(CPUMIPSState *env, uint32_t fst0, uint32_t fst2)
{
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_sub(fst2, float32_one,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_recip2_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fsth0, fsth2, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_sub(fst2, float32_one,
                                       &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_sub(fsth2, float32_one,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_rsqrt2_d(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    fdt2 = float64_mul(fdt0, fdt2, &env->active_fpu.fp_status);
    fdt2 = float64_sub(fdt2, float64_one, &env->active_fpu.fp_status);
    fdt2 = float64_chs(float64_div(fdt2, FLOAT_TWO64,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_rsqrt2_s(CPUMIPSState *env, uint32_t fst0, uint32_t fst2)
{
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_sub(fst2, float32_one, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_div(fst2, FLOAT_TWO32,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fst2;
}

uint64_t helper_float_rsqrt2_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst2 = fdt2 & 0XFFFFFFFF;
    uint32_t fsth2 = fdt2 >> 32;

    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fsth0, fsth2, &env->active_fpu.fp_status);
    fst2 = float32_sub(fst2, float32_one, &env->active_fpu.fp_status);
    fsth2 = float32_sub(fsth2, float32_one, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_div(fst2, FLOAT_TWO32,
                                       &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_div(fsth2, FLOAT_TWO32,
                                       &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_addr_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fst2;
    uint32_t fsth2;

    fst2 = float32_add(fst0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_add(fst1, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_mulr_ps(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt1)
{
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;
    uint32_t fsth0 = fdt0 >> 32;
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;
    uint32_t fsth1 = fdt1 >> 32;
    uint32_t fst2;
    uint32_t fsth2;

    fst2 = float32_mul(fst0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_mul(fst1, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

#define FLOAT_MINMAX(name, bits, minmaxfunc)                            \
uint ## bits ## _t helper_float_ ## name(CPUMIPSState *env,             \
                                         uint ## bits ## _t fs,         \
                                         uint ## bits ## _t ft)         \
{                                                                       \
    uint ## bits ## _t fdret;                                           \
                                                                        \
    fdret = float ## bits ## _ ## minmaxfunc(fs, ft,                    \
                                           &env->active_fpu.fp_status); \
    update_fcr31(env, GETPC());                                         \
    return fdret;                                                       \
}

FLOAT_MINMAX(max_s, 32, maxnum)
FLOAT_MINMAX(max_d, 64, maxnum)
FLOAT_MINMAX(maxa_s, 32, maxnummag)
FLOAT_MINMAX(maxa_d, 64, maxnummag)

FLOAT_MINMAX(min_s, 32, minnum)
FLOAT_MINMAX(min_d, 64, minnum)
FLOAT_MINMAX(mina_s, 32, minnummag)
FLOAT_MINMAX(mina_d, 64, minnummag)
#undef FLOAT_MINMAX

/* ternary operations */
#define UNFUSED_FMA(prefix, a, b, c, flags)                          \
{                                                                    \
    a = prefix##_mul(a, b, &env->active_fpu.fp_status);              \
    if ((flags) & float_muladd_negate_c) {                           \
        a = prefix##_sub(a, c, &env->active_fpu.fp_status);          \
    } else {                                                         \
        a = prefix##_add(a, c, &env->active_fpu.fp_status);          \
    }                                                                \
    if ((flags) & float_muladd_negate_result) {                      \
        a = prefix##_chs(a);                                         \
    }                                                                \
}

/* FMA based operations */
#define FLOAT_FMA(name, type)                                        \
uint64_t helper_float_ ## name ## _d(CPUMIPSState *env,              \
                                     uint64_t fdt0, uint64_t fdt1,   \
                                     uint64_t fdt2)                  \
{                                                                    \
    UNFUSED_FMA(float64, fdt0, fdt1, fdt2, type);                    \
    update_fcr31(env, GETPC());                                      \
    return fdt0;                                                     \
}                                                                    \
                                                                     \
uint32_t helper_float_ ## name ## _s(CPUMIPSState *env,              \
                                     uint32_t fst0, uint32_t fst1,   \
                                     uint32_t fst2)                  \
{                                                                    \
    UNFUSED_FMA(float32, fst0, fst1, fst2, type);                    \
    update_fcr31(env, GETPC());                                      \
    return fst0;                                                     \
}                                                                    \
                                                                     \
uint64_t helper_float_ ## name ## _ps(CPUMIPSState *env,             \
                                      uint64_t fdt0, uint64_t fdt1,  \
                                      uint64_t fdt2)                 \
{                                                                    \
    uint32_t fst0 = fdt0 & 0XFFFFFFFF;                               \
    uint32_t fsth0 = fdt0 >> 32;                                     \
    uint32_t fst1 = fdt1 & 0XFFFFFFFF;                               \
    uint32_t fsth1 = fdt1 >> 32;                                     \
    uint32_t fst2 = fdt2 & 0XFFFFFFFF;                               \
    uint32_t fsth2 = fdt2 >> 32;                                     \
                                                                     \
    UNFUSED_FMA(float32, fst0, fst1, fst2, type);                    \
    UNFUSED_FMA(float32, fsth0, fsth1, fsth2, type);                 \
    update_fcr31(env, GETPC());                                      \
    return ((uint64_t)fsth0 << 32) | fst0;                           \
}
FLOAT_FMA(madd, 0)
FLOAT_FMA(msub, float_muladd_negate_c)
FLOAT_FMA(nmadd, float_muladd_negate_result)
FLOAT_FMA(nmsub, float_muladd_negate_result | float_muladd_negate_c)
#undef FLOAT_FMA

#define FLOAT_FMADDSUB(name, bits, muladd_arg)                          \
uint ## bits ## _t helper_float_ ## name(CPUMIPSState *env,             \
                                         uint ## bits ## _t fs,         \
                                         uint ## bits ## _t ft,         \
                                         uint ## bits ## _t fd)         \
{                                                                       \
    uint ## bits ## _t fdret;                                           \
                                                                        \
    fdret = float ## bits ## _muladd(fs, ft, fd, muladd_arg,            \
                                     &env->active_fpu.fp_status);       \
    update_fcr31(env, GETPC());                                         \
    return fdret;                                                       \
}

FLOAT_FMADDSUB(maddf_s, 32, 0)
FLOAT_FMADDSUB(maddf_d, 64, 0)
FLOAT_FMADDSUB(msubf_s, 32, float_muladd_negate_product)
FLOAT_FMADDSUB(msubf_d, 64, float_muladd_negate_product)
#undef FLOAT_FMADDSUB

/* compare operations */
#define FOP_COND_D(op, cond)                                   \
void helper_cmp_d_ ## op(CPUMIPSState *env, uint64_t fdt0,     \
                         uint64_t fdt1, int cc)                \
{                                                              \
    int c;                                                     \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}                                                              \
void helper_cmpabs_d_ ## op(CPUMIPSState *env, uint64_t fdt0,  \
                            uint64_t fdt1, int cc)             \
{                                                              \
    int c;                                                     \
    fdt0 = float64_abs(fdt0);                                  \
    fdt1 = float64_abs(fdt1);                                  \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered_quiet() is still called.
 */
FOP_COND_D(f,    (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_D(un,   float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status))
FOP_COND_D(eq,   float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ueq,  float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(olt,  float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ult,  float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ole,  float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ule,  float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered() is still called.
 */
FOP_COND_D(sf,   (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_D(ngle, float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status))
FOP_COND_D(seq,  float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ngl,  float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(lt,   float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(nge,  float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(le,   float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status))
FOP_COND_D(ngt,  float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status))

#define FOP_COND_S(op, cond)                                   \
void helper_cmp_s_ ## op(CPUMIPSState *env, uint32_t fst0,     \
                         uint32_t fst1, int cc)                \
{                                                              \
    int c;                                                     \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}                                                              \
void helper_cmpabs_s_ ## op(CPUMIPSState *env, uint32_t fst0,  \
                            uint32_t fst1, int cc)             \
{                                                              \
    int c;                                                     \
    fst0 = float32_abs(fst0);                                  \
    fst1 = float32_abs(fst1);                                  \
    c = cond;                                                  \
    update_fcr31(env, GETPC());                                \
    if (c)                                                     \
        SET_FP_COND(cc, env->active_fpu);                      \
    else                                                       \
        CLEAR_FP_COND(cc, env->active_fpu);                    \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called.
 */
FOP_COND_S(f,    (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_S(un,   float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status))
FOP_COND_S(eq,   float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ueq,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(olt,  float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ult,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ole,  float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ule,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called.
 */
FOP_COND_S(sf,   (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_S(ngle, float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status))
FOP_COND_S(seq,  float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ngl,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(lt,   float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(nge,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(le,   float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status))
FOP_COND_S(ngt,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                 || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status))

#define FOP_COND_PS(op, condl, condh)                           \
void helper_cmp_ps_ ## op(CPUMIPSState *env, uint64_t fdt0,     \
                          uint64_t fdt1, int cc)                \
{                                                               \
    uint32_t fst0, fsth0, fst1, fsth1;                          \
    int ch, cl;                                                 \
    fst0 = fdt0 & 0XFFFFFFFF;                                   \
    fsth0 = fdt0 >> 32;                                         \
    fst1 = fdt1 & 0XFFFFFFFF;                                   \
    fsth1 = fdt1 >> 32;                                         \
    cl = condl;                                                 \
    ch = condh;                                                 \
    update_fcr31(env, GETPC());                                 \
    if (cl)                                                     \
        SET_FP_COND(cc, env->active_fpu);                       \
    else                                                        \
        CLEAR_FP_COND(cc, env->active_fpu);                     \
    if (ch)                                                     \
        SET_FP_COND(cc + 1, env->active_fpu);                   \
    else                                                        \
        CLEAR_FP_COND(cc + 1, env->active_fpu);                 \
}                                                               \
void helper_cmpabs_ps_ ## op(CPUMIPSState *env, uint64_t fdt0,  \
                             uint64_t fdt1, int cc)             \
{                                                               \
    uint32_t fst0, fsth0, fst1, fsth1;                          \
    int ch, cl;                                                 \
    fst0 = float32_abs(fdt0 & 0XFFFFFFFF);                      \
    fsth0 = float32_abs(fdt0 >> 32);                            \
    fst1 = float32_abs(fdt1 & 0XFFFFFFFF);                      \
    fsth1 = float32_abs(fdt1 >> 32);                            \
    cl = condl;                                                 \
    ch = condh;                                                 \
    update_fcr31(env, GETPC());                                 \
    if (cl)                                                     \
        SET_FP_COND(cc, env->active_fpu);                       \
    else                                                        \
        CLEAR_FP_COND(cc, env->active_fpu);                     \
    if (ch)                                                     \
        SET_FP_COND(cc + 1, env->active_fpu);                   \
    else                                                        \
        CLEAR_FP_COND(cc + 1, env->active_fpu);                 \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called.
 */
FOP_COND_PS(f,    (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status), 0),
                  (float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_PS(un,   float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(eq,   float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_eq_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ueq,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_eq_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(olt,  float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_lt_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ult,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_lt_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ole,  float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_le_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ule,  float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered_quiet(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_le_quiet(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called.
 */
FOP_COND_PS(sf,   (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status), 0),
                  (float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status), 0))
FOP_COND_PS(ngle, float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(seq,  float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_eq(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ngl,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_eq(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(lt,   float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_lt(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(nge,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_lt(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(le,   float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_le(fsth0, fsth1,
                                       &env->active_fpu.fp_status))
FOP_COND_PS(ngt,  float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                  || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status),
                  float32_unordered(fsth1, fsth0,
                                       &env->active_fpu.fp_status)
                  || float32_le(fsth0, fsth1,
                                       &env->active_fpu.fp_status))

/* R6 compare operations */
#define FOP_CONDN_D(op, cond)                                       \
uint64_t helper_r6_cmp_d_ ## op(CPUMIPSState *env, uint64_t fdt0,   \
                                uint64_t fdt1)                      \
{                                                                   \
    uint64_t c;                                                     \
    c = cond;                                                       \
    update_fcr31(env, GETPC());                                     \
    if (c) {                                                        \
        return -1;                                                  \
    } else {                                                        \
        return 0;                                                   \
    }                                                               \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered_quiet() is still called.
 */
FOP_CONDN_D(af,  (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_D(un,  (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(eq,  (float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ueq, (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_eq_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(lt,  (float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ult, (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(le,  (float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ule, (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                 || float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered() is still called.\
 */
FOP_CONDN_D(saf,  (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_D(sun,  (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(seq,  (float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sueq, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_eq(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(slt,  (float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sult, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sle,  (float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sule, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(or,   (float64_le_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_le_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(une,  (float64_unordered_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(ne,   (float64_lt_quiet(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sor,  (float64_le(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_le(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sune, (float64_unordered(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_D(sne,  (float64_lt(fdt1, fdt0,
                                       &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1,
                                       &env->active_fpu.fp_status)))

#define FOP_CONDN_S(op, cond)                                       \
uint32_t helper_r6_cmp_s_ ## op(CPUMIPSState *env, uint32_t fst0,   \
                                uint32_t fst1)                      \
{                                                                   \
    uint64_t c;                                                     \
    c = cond;                                                       \
    update_fcr31(env, GETPC());                                     \
    if (c) {                                                        \
        return -1;                                                  \
    } else {                                                        \
        return 0;                                                   \
    }                                                               \
}

/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called.
 */
FOP_CONDN_S(af,   (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_S(un,   (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(eq,   (float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ueq,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_eq_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(lt,   (float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ult,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(le,   (float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ule,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
/*
 * NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called.
 */
FOP_CONDN_S(saf,  (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status), 0))
FOP_CONDN_S(sun,  (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(seq,  (float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sueq, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_eq(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(slt,  (float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sult, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sle,  (float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sule, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(or,   (float32_le_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(une,  (float32_unordered_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(ne,   (float32_lt_quiet(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sor,  (float32_le(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_le(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sune, (float32_unordered(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))
FOP_CONDN_S(sne,  (float32_lt(fst1, fst0,
                                       &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1,
                                       &env->active_fpu.fp_status)))

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
