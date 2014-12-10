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
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
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

#if defined(CONFIG_USER_ONLY)
#define HELPER_LD(name, insn, type)                                     \
static inline type do_##name(CPUMIPSState *env, target_ulong addr,      \
                             int mem_idx, uintptr_t retaddr)            \
{                                                                       \
    return (type) cpu_##insn##_data_ra(env, addr, retaddr);             \
}
#else
#define HELPER_LD(name, insn, type)                                     \
static inline type do_##name(CPUMIPSState *env, target_ulong addr,      \
                             int mem_idx, uintptr_t retaddr)            \
{                                                                       \
    switch (mem_idx)                                                    \
    {                                                                   \
    case 0: return (type) cpu_##insn##_kernel_ra(env, addr, retaddr);   \
    case 1: return (type) cpu_##insn##_super_ra(env, addr, retaddr);    \
    default:                                                            \
    case 2: return (type) cpu_##insn##_user_ra(env, addr, retaddr);     \
    case 3: return (type) cpu_##insn##_error_ra(env, addr, retaddr);    \
    }                                                                   \
}
#endif
HELPER_LD(lw, ldl, int32_t)
#if defined(TARGET_MIPS64)
HELPER_LD(ld, ldq, int64_t)
#endif
#undef HELPER_LD

#if defined(CONFIG_USER_ONLY)
#define HELPER_ST(name, insn, type)                                     \
static inline void do_##name(CPUMIPSState *env, target_ulong addr,      \
                             type val, int mem_idx, uintptr_t retaddr)  \
{                                                                       \
    cpu_##insn##_data_ra(env, addr, val, retaddr);                      \
}
#else
#define HELPER_ST(name, insn, type)                                     \
static inline void do_##name(CPUMIPSState *env, target_ulong addr,      \
                             type val, int mem_idx, uintptr_t retaddr)  \
{                                                                       \
    switch (mem_idx)                                                    \
    {                                                                   \
    case 0: cpu_##insn##_kernel_ra(env, addr, val, retaddr); break;     \
    case 1: cpu_##insn##_super_ra(env, addr, val, retaddr); break;      \
    default:                                                            \
    case 2: cpu_##insn##_user_ra(env, addr, val, retaddr); break;       \
    case 3:                                                             \
        cpu_##insn##_error_ra(env, addr, val, retaddr);                 \
        break;                                                          \
    }                                                                   \
}
#endif
HELPER_ST(sb, stb, uint8_t)
HELPER_ST(sw, stl, uint32_t)
#if defined(TARGET_MIPS64)
HELPER_ST(sd, stq, uint64_t)
#endif
#undef HELPER_ST

/* 64 bits arithmetic for 32 bits hosts */
static inline uint64_t get_HILO(CPUMIPSState *env)
{
    return ((uint64_t)(env->active_tc.HI[0]) << 32) | (uint32_t)env->active_tc.LO[0];
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

#ifndef CONFIG_USER_ONLY

static inline hwaddr do_translate_address(CPUMIPSState *env,
                                                      target_ulong address,
                                                      int rw, uintptr_t retaddr)
{
    hwaddr lladdr;
    CPUState *cs = CPU(mips_env_get_cpu(env));

    lladdr = cpu_mips_translate_address(env, address, rw);

    if (lladdr == -1LL) {
        cpu_loop_exit_restore(cs, retaddr);
    } else {
        return lladdr;
    }
}

#define HELPER_LD_ATOMIC(name, insn, almask)                                  \
target_ulong helper_##name(CPUMIPSState *env, target_ulong arg, int mem_idx)  \
{                                                                             \
    if (arg & almask) {                                                       \
        env->CP0_BadVAddr = arg;                                              \
        do_raise_exception(env, EXCP_AdEL, GETPC());                          \
    }                                                                         \
    env->lladdr = do_translate_address(env, arg, 0, GETPC());                 \
    env->llval = do_##insn(env, arg, mem_idx, GETPC());                       \
    return env->llval;                                                        \
}
HELPER_LD_ATOMIC(ll, lw, 0x3)
#ifdef TARGET_MIPS64
HELPER_LD_ATOMIC(lld, ld, 0x7)
#endif
#undef HELPER_LD_ATOMIC

#define HELPER_ST_ATOMIC(name, ld_insn, st_insn, almask)                      \
target_ulong helper_##name(CPUMIPSState *env, target_ulong arg1,              \
                           target_ulong arg2, int mem_idx)                    \
{                                                                             \
    target_long tmp;                                                          \
                                                                              \
    if (arg2 & almask) {                                                      \
        env->CP0_BadVAddr = arg2;                                             \
        do_raise_exception(env, EXCP_AdES, GETPC());                          \
    }                                                                         \
    if (do_translate_address(env, arg2, 1, GETPC()) == env->lladdr) {         \
        tmp = do_##ld_insn(env, arg2, mem_idx, GETPC());                      \
        if (tmp == env->llval) {                                              \
            do_##st_insn(env, arg2, arg1, mem_idx, GETPC());                  \
            return 1;                                                         \
        }                                                                     \
    }                                                                         \
    return 0;                                                                 \
}
HELPER_ST_ATOMIC(sc, lw, sw, 0x3)
#ifdef TARGET_MIPS64
HELPER_ST_ATOMIC(scd, ld, sd, 0x7)
#endif
#undef HELPER_ST_ATOMIC
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
    do_sb(env, arg2, (uint8_t)(arg1 >> 24), mem_idx, GETPC());

    if (GET_LMASK(arg2) <= 2) {
        do_sb(env, GET_OFFSET(arg2, 1), (uint8_t)(arg1 >> 16), mem_idx,
              GETPC());
    }

    if (GET_LMASK(arg2) <= 1) {
        do_sb(env, GET_OFFSET(arg2, 2), (uint8_t)(arg1 >> 8), mem_idx,
              GETPC());
    }

    if (GET_LMASK(arg2) == 0) {
        do_sb(env, GET_OFFSET(arg2, 3), (uint8_t)arg1, mem_idx,
              GETPC());
    }
}

void helper_swr(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    do_sb(env, arg2, (uint8_t)arg1, mem_idx, GETPC());

    if (GET_LMASK(arg2) >= 1) {
        do_sb(env, GET_OFFSET(arg2, -1), (uint8_t)(arg1 >> 8), mem_idx,
              GETPC());
    }

    if (GET_LMASK(arg2) >= 2) {
        do_sb(env, GET_OFFSET(arg2, -2), (uint8_t)(arg1 >> 16), mem_idx,
              GETPC());
    }

    if (GET_LMASK(arg2) == 3) {
        do_sb(env, GET_OFFSET(arg2, -3), (uint8_t)(arg1 >> 24), mem_idx,
              GETPC());
    }
}

#if defined(TARGET_MIPS64)
/* "half" load and stores.  We must do the memory access inline,
   or fault handling won't work.  */

#ifdef TARGET_WORDS_BIGENDIAN
#define GET_LMASK64(v) ((v) & 7)
#else
#define GET_LMASK64(v) (((v) & 7) ^ 7)
#endif

void helper_sdl(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    do_sb(env, arg2, (uint8_t)(arg1 >> 56), mem_idx, GETPC());

    if (GET_LMASK64(arg2) <= 6) {
        do_sb(env, GET_OFFSET(arg2, 1), (uint8_t)(arg1 >> 48), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) <= 5) {
        do_sb(env, GET_OFFSET(arg2, 2), (uint8_t)(arg1 >> 40), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) <= 4) {
        do_sb(env, GET_OFFSET(arg2, 3), (uint8_t)(arg1 >> 32), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) <= 3) {
        do_sb(env, GET_OFFSET(arg2, 4), (uint8_t)(arg1 >> 24), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) <= 2) {
        do_sb(env, GET_OFFSET(arg2, 5), (uint8_t)(arg1 >> 16), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) <= 1) {
        do_sb(env, GET_OFFSET(arg2, 6), (uint8_t)(arg1 >> 8), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) <= 0) {
        do_sb(env, GET_OFFSET(arg2, 7), (uint8_t)arg1, mem_idx,
              GETPC());
    }
}

void helper_sdr(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    do_sb(env, arg2, (uint8_t)arg1, mem_idx, GETPC());

    if (GET_LMASK64(arg2) >= 1) {
        do_sb(env, GET_OFFSET(arg2, -1), (uint8_t)(arg1 >> 8), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) >= 2) {
        do_sb(env, GET_OFFSET(arg2, -2), (uint8_t)(arg1 >> 16), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) >= 3) {
        do_sb(env, GET_OFFSET(arg2, -3), (uint8_t)(arg1 >> 24), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) >= 4) {
        do_sb(env, GET_OFFSET(arg2, -4), (uint8_t)(arg1 >> 32), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) >= 5) {
        do_sb(env, GET_OFFSET(arg2, -5), (uint8_t)(arg1 >> 40), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) >= 6) {
        do_sb(env, GET_OFFSET(arg2, -6), (uint8_t)(arg1 >> 48), mem_idx,
              GETPC());
    }

    if (GET_LMASK64(arg2) == 7) {
        do_sb(env, GET_OFFSET(arg2, -7), (uint8_t)(arg1 >> 56), mem_idx,
              GETPC());
    }
}
#endif /* TARGET_MIPS64 */

static const int multiple_regs[] = { 16, 17, 18, 19, 20, 21, 22, 23, 30 };

void helper_lwm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE (multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            env->active_tc.gpr[multiple_regs[i]] =
                (target_long)do_lw(env, addr, mem_idx, GETPC());
            addr += 4;
        }
    }

    if (do_r31) {
        env->active_tc.gpr[31] = (target_long)do_lw(env, addr, mem_idx,
                                                    GETPC());
    }
}

void helper_swm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE (multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            do_sw(env, addr, env->active_tc.gpr[multiple_regs[i]], mem_idx,
                  GETPC());
            addr += 4;
        }
    }

    if (do_r31) {
        do_sw(env, addr, env->active_tc.gpr[31], mem_idx, GETPC());
    }
}

#if defined(TARGET_MIPS64)
void helper_ldm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE (multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            env->active_tc.gpr[multiple_regs[i]] = do_ld(env, addr, mem_idx,
                                                         GETPC());
            addr += 8;
        }
    }

    if (do_r31) {
        env->active_tc.gpr[31] = do_ld(env, addr, mem_idx, GETPC());
    }
}

void helper_sdm(CPUMIPSState *env, target_ulong addr, target_ulong reglist,
                uint32_t mem_idx)
{
    target_ulong base_reglist = reglist & 0xf;
    target_ulong do_r31 = reglist & 0x10;

    if (base_reglist > 0 && base_reglist <= ARRAY_SIZE (multiple_regs)) {
        target_ulong i;

        for (i = 0; i < base_reglist; i++) {
            do_sd(env, addr, env->active_tc.gpr[multiple_regs[i]], mem_idx,
                  GETPC());
            addr += 8;
        }
    }

    if (do_r31) {
        do_sd(env, addr, env->active_tc.gpr[31], mem_idx, GETPC());
    }
}
#endif

#ifndef CONFIG_USER_ONLY
/* SMP helpers.  */
static bool mips_vpe_is_wfi(MIPSCPU *c)
{
    CPUState *cpu = CPU(c);
    CPUMIPSState *env = &c->env;

    /* If the VPE is halted but otherwise active, it means it's waiting for
       an interrupt.  */
    return cpu->halted && mips_vpe_active(env);
}

static bool mips_vp_is_wfi(MIPSCPU *c)
{
    CPUState *cpu = CPU(c);
    CPUMIPSState *env = &c->env;

    return cpu->halted && mips_vp_active(env);
}

static inline void mips_vpe_wake(MIPSCPU *c)
{
    /* Don't set ->halted = 0 directly, let it be done via cpu_has_work
       because there might be other conditions that state that c should
       be sleeping.  */
    cpu_interrupt(CPU(c), CPU_INTERRUPT_WAKE);
}

static inline void mips_vpe_sleep(MIPSCPU *cpu)
{
    CPUState *cs = CPU(cpu);

    /* The VPE was shut off, really go to bed.
       Reset any old _WAKE requests.  */
    cs->halted = 1;
    cpu_reset_interrupt(cs, CPU_INTERRUPT_WAKE);
}

static inline void mips_tc_wake(MIPSCPU *cpu, int tc)
{
    CPUMIPSState *c = &cpu->env;

    /* FIXME: TC reschedule.  */
    if (mips_vpe_active(c) && !mips_vpe_is_wfi(cpu)) {
        mips_vpe_wake(cpu);
    }
}

static inline void mips_tc_sleep(MIPSCPU *cpu, int tc)
{
    CPUMIPSState *c = &cpu->env;

    /* FIXME: TC reschedule.  */
    if (!mips_vpe_active(c)) {
        mips_vpe_sleep(cpu);
    }
}

/**
 * mips_cpu_map_tc:
 * @env: CPU from which mapping is performed.
 * @tc: Should point to an int with the value of the global TC index.
 *
 * This function will transform @tc into a local index within the
 * returned #CPUMIPSState.
 */
/* FIXME: This code assumes that all VPEs have the same number of TCs,
          which depends on runtime setup. Can probably be fixed by
          walking the list of CPUMIPSStates.  */
static CPUMIPSState *mips_cpu_map_tc(CPUMIPSState *env, int *tc)
{
    MIPSCPU *cpu;
    CPUState *cs;
    CPUState *other_cs;
    int vpe_idx;
    int tc_idx = *tc;

    if (!(env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP))) {
        /* Not allowed to address other CPUs.  */
        *tc = env->current_tc;
        return env;
    }

    cs = CPU(mips_env_get_cpu(env));
    vpe_idx = tc_idx / cs->nr_threads;
    *tc = tc_idx % cs->nr_threads;
    other_cs = qemu_get_cpu(vpe_idx);
    if (other_cs == NULL) {
        return env;
    }
    cpu = MIPS_CPU(other_cs);
    return &cpu->env;
}

/* The per VPE CP0_Status register shares some fields with the per TC
   CP0_TCStatus registers. These fields are wired to the same registers,
   so changes to either of them should be reflected on both registers.

   Also, EntryHi shares the bottom 8 bit ASID with TCStauts.

   These helper call synchronizes the regs for a given cpu.  */

/* Called for updates to CP0_Status.  Defined in "cpu.h" for gdbstub.c.  */
/* static inline void sync_c0_status(CPUMIPSState *env, CPUMIPSState *cpu,
                                     int tc);  */

/* Called for updates to CP0_TCStatus.  */
static void sync_c0_tcstatus(CPUMIPSState *cpu, int tc,
                             target_ulong v)
{
    uint32_t status;
    uint32_t tcu, tmx, tasid, tksu;
    uint32_t mask = ((1U << CP0St_CU3)
                       | (1 << CP0St_CU2)
                       | (1 << CP0St_CU1)
                       | (1 << CP0St_CU0)
                       | (1 << CP0St_MX)
                       | (3 << CP0St_KSU));

    tcu = (v >> CP0TCSt_TCU0) & 0xf;
    tmx = (v >> CP0TCSt_TMX) & 0x1;
    tasid = v & cpu->CP0_EntryHi_ASID_mask;
    tksu = (v >> CP0TCSt_TKSU) & 0x3;

    status = tcu << CP0St_CU0;
    status |= tmx << CP0St_MX;
    status |= tksu << CP0St_KSU;

    cpu->CP0_Status &= ~mask;
    cpu->CP0_Status |= status;

    /* Sync the TASID with EntryHi.  */
    cpu->CP0_EntryHi &= ~cpu->CP0_EntryHi_ASID_mask;
    cpu->CP0_EntryHi |= tasid;

    compute_hflags(cpu);
}

/* Called for updates to CP0_EntryHi.  */
static void sync_c0_entryhi(CPUMIPSState *cpu, int tc)
{
    int32_t *tcst;
    uint32_t asid, v = cpu->CP0_EntryHi;

    asid = v & cpu->CP0_EntryHi_ASID_mask;

    if (tc == cpu->current_tc) {
        tcst = &cpu->active_tc.CP0_TCStatus;
    } else {
        tcst = &cpu->tcs[tc].CP0_TCStatus;
    }

    *tcst &= ~cpu->CP0_EntryHi_ASID_mask;
    *tcst |= asid;
}

/* CP0 helpers */
target_ulong helper_mfc0_mvpcontrol(CPUMIPSState *env)
{
    return env->mvp->CP0_MVPControl;
}

target_ulong helper_mfc0_mvpconf0(CPUMIPSState *env)
{
    return env->mvp->CP0_MVPConf0;
}

target_ulong helper_mfc0_mvpconf1(CPUMIPSState *env)
{
    return env->mvp->CP0_MVPConf1;
}

target_ulong helper_mfc0_random(CPUMIPSState *env)
{
    return (int32_t)cpu_mips_get_random(env);
}

target_ulong helper_mfc0_tcstatus(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCStatus;
}

target_ulong helper_mftc0_tcstatus(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.CP0_TCStatus;
    else
        return other->tcs[other_tc].CP0_TCStatus;
}

target_ulong helper_mfc0_tcbind(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCBind;
}

target_ulong helper_mftc0_tcbind(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.CP0_TCBind;
    else
        return other->tcs[other_tc].CP0_TCBind;
}

target_ulong helper_mfc0_tcrestart(CPUMIPSState *env)
{
    return env->active_tc.PC;
}

target_ulong helper_mftc0_tcrestart(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.PC;
    else
        return other->tcs[other_tc].PC;
}

target_ulong helper_mfc0_tchalt(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCHalt;
}

target_ulong helper_mftc0_tchalt(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.CP0_TCHalt;
    else
        return other->tcs[other_tc].CP0_TCHalt;
}

target_ulong helper_mfc0_tccontext(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCContext;
}

target_ulong helper_mftc0_tccontext(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.CP0_TCContext;
    else
        return other->tcs[other_tc].CP0_TCContext;
}

target_ulong helper_mfc0_tcschedule(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCSchedule;
}

target_ulong helper_mftc0_tcschedule(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.CP0_TCSchedule;
    else
        return other->tcs[other_tc].CP0_TCSchedule;
}

target_ulong helper_mfc0_tcschefback(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCScheFBack;
}

target_ulong helper_mftc0_tcschefback(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.CP0_TCScheFBack;
    else
        return other->tcs[other_tc].CP0_TCScheFBack;
}

target_ulong helper_mfc0_count(CPUMIPSState *env)
{
    int32_t count;
    qemu_mutex_lock_iothread();
    count = (int32_t) cpu_mips_get_count(env);
    qemu_mutex_unlock_iothread();
    return count;
}

target_ulong helper_mftc0_entryhi(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_EntryHi;
}

target_ulong helper_mftc0_cause(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tccause;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        tccause = other->CP0_Cause;
    } else {
        tccause = other->CP0_Cause;
    }

    return tccause;
}

target_ulong helper_mftc0_status(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_Status;
}

target_ulong helper_mfc0_lladdr(CPUMIPSState *env)
{
    return (int32_t)(env->lladdr >> env->CP0_LLAddr_shift);
}

target_ulong helper_mfc0_maar(CPUMIPSState *env)
{
    return (int32_t) env->CP0_MAAR[env->CP0_MAARI];
}

target_ulong helper_mfhc0_maar(CPUMIPSState *env)
{
    return env->CP0_MAAR[env->CP0_MAARI] >> 32;
}

target_ulong helper_mfc0_watchlo(CPUMIPSState *env, uint32_t sel)
{
    return (int32_t)env->CP0_WatchLo[sel];
}

target_ulong helper_mfc0_watchhi(CPUMIPSState *env, uint32_t sel)
{
    return env->CP0_WatchHi[sel];
}

target_ulong helper_mfc0_debug(CPUMIPSState *env)
{
    target_ulong t0 = env->CP0_Debug;
    if (env->hflags & MIPS_HFLAG_DM)
        t0 |= 1 << CP0DB_DM;

    return t0;
}

target_ulong helper_mftc0_debug(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    int32_t tcstatus;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        tcstatus = other->active_tc.CP0_Debug_tcstatus;
    else
        tcstatus = other->tcs[other_tc].CP0_Debug_tcstatus;

    /* XXX: Might be wrong, check with EJTAG spec. */
    return (other->CP0_Debug & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
            (tcstatus & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

#if defined(TARGET_MIPS64)
target_ulong helper_dmfc0_tcrestart(CPUMIPSState *env)
{
    return env->active_tc.PC;
}

target_ulong helper_dmfc0_tchalt(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCHalt;
}

target_ulong helper_dmfc0_tccontext(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCContext;
}

target_ulong helper_dmfc0_tcschedule(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCSchedule;
}

target_ulong helper_dmfc0_tcschefback(CPUMIPSState *env)
{
    return env->active_tc.CP0_TCScheFBack;
}

target_ulong helper_dmfc0_lladdr(CPUMIPSState *env)
{
    return env->lladdr >> env->CP0_LLAddr_shift;
}

target_ulong helper_dmfc0_maar(CPUMIPSState *env)
{
    return env->CP0_MAAR[env->CP0_MAARI];
}

target_ulong helper_dmfc0_watchlo(CPUMIPSState *env, uint32_t sel)
{
    return env->CP0_WatchLo[sel];
}
#endif /* TARGET_MIPS64 */

void helper_mtc0_index(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t index_p = env->CP0_Index & 0x80000000;
    uint32_t tlb_index = arg1 & 0x7fffffff;
    if (tlb_index < env->tlb->nb_tlb) {
        if (env->insn_flags & ISA_MIPS32R6) {
            index_p |= arg1 & 0x80000000;
        }
        env->CP0_Index = index_p | tlb_index;
    }
}

void helper_mtc0_mvpcontrol(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP))
        mask |= (1 << CP0MVPCo_CPA) | (1 << CP0MVPCo_VPC) |
                (1 << CP0MVPCo_EVP);
    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0MVPCo_STLB);
    newval = (env->mvp->CP0_MVPControl & ~mask) | (arg1 & mask);

    // TODO: Enable/disable shared TLB, enable/disable VPEs.

    env->mvp->CP0_MVPControl = newval;
}

void helper_mtc0_vpecontrol(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask;
    uint32_t newval;

    mask = (1 << CP0VPECo_YSI) | (1 << CP0VPECo_GSI) |
           (1 << CP0VPECo_TE) | (0xff << CP0VPECo_TargTC);
    newval = (env->CP0_VPEControl & ~mask) | (arg1 & mask);

    /* Yield scheduler intercept not implemented. */
    /* Gating storage scheduler intercept not implemented. */

    // TODO: Enable/disable TCs.

    env->CP0_VPEControl = newval;
}

void helper_mttc0_vpecontrol(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    uint32_t mask;
    uint32_t newval;

    mask = (1 << CP0VPECo_YSI) | (1 << CP0VPECo_GSI) |
           (1 << CP0VPECo_TE) | (0xff << CP0VPECo_TargTC);
    newval = (other->CP0_VPEControl & ~mask) | (arg1 & mask);

    /* TODO: Enable/disable TCs.  */

    other->CP0_VPEControl = newval;
}

target_ulong helper_mftc0_vpecontrol(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    /* FIXME: Mask away return zero on read bits.  */
    return other->CP0_VPEControl;
}

target_ulong helper_mftc0_vpeconf0(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_VPEConf0;
}

void helper_mtc0_vpeconf0(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) {
        if (env->CP0_VPEConf0 & (1 << CP0VPEC0_VPA))
            mask |= (0xff << CP0VPEC0_XTC);
        mask |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    }
    newval = (env->CP0_VPEConf0 & ~mask) | (arg1 & mask);

    // TODO: TC exclusive handling due to ERL/EXL.

    env->CP0_VPEConf0 = newval;
}

void helper_mttc0_vpeconf0(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    uint32_t mask = 0;
    uint32_t newval;

    mask |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);
    newval = (other->CP0_VPEConf0 & ~mask) | (arg1 & mask);

    /* TODO: TC exclusive handling due to ERL/EXL.  */
    other->CP0_VPEConf0 = newval;
}

void helper_mtc0_vpeconf1(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0;
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (0xff << CP0VPEC1_NCX) | (0xff << CP0VPEC1_NCP2) |
                (0xff << CP0VPEC1_NCP1);
    newval = (env->CP0_VPEConf1 & ~mask) | (arg1 & mask);

    /* UDI not implemented. */
    /* CP2 not implemented. */

    // TODO: Handle FPU (CP1) binding.

    env->CP0_VPEConf1 = newval;
}

void helper_mtc0_yqmask(CPUMIPSState *env, target_ulong arg1)
{
    /* Yield qualifier inputs not implemented. */
    env->CP0_YQMask = 0x00000000;
}

void helper_mtc0_vpeopt(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_VPEOpt = arg1 & 0x0000ffff;
}

#define MTC0_ENTRYLO_MASK(env) ((env->PAMask >> 6) & 0x3FFFFFFF)

void helper_mtc0_entrylo0(CPUMIPSState *env, target_ulong arg1)
{
    /* 1k pages not implemented */
    target_ulong rxi = arg1 & (env->CP0_PageGrain & (3u << CP0PG_XIE));
    env->CP0_EntryLo0 = (arg1 & MTC0_ENTRYLO_MASK(env))
                        | (rxi << (CP0EnLo_XI - 30));
}

#if defined(TARGET_MIPS64)
#define DMTC0_ENTRYLO_MASK(env) (env->PAMask >> 6)

void helper_dmtc0_entrylo0(CPUMIPSState *env, uint64_t arg1)
{
    uint64_t rxi = arg1 & ((env->CP0_PageGrain & (3ull << CP0PG_XIE)) << 32);
    env->CP0_EntryLo0 = (arg1 & DMTC0_ENTRYLO_MASK(env)) | rxi;
}
#endif

void helper_mtc0_tcstatus(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = env->CP0_TCStatus_rw_bitmask;
    uint32_t newval;

    newval = (env->active_tc.CP0_TCStatus & ~mask) | (arg1 & mask);

    env->active_tc.CP0_TCStatus = newval;
    sync_c0_tcstatus(env, env->current_tc, newval);
}

void helper_mttc0_tcstatus(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.CP0_TCStatus = arg1;
    else
        other->tcs[other_tc].CP0_TCStatus = arg1;
    sync_c0_tcstatus(other, other_tc, arg1);
}

void helper_mtc0_tcbind(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;

    if (env->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    newval = (env->active_tc.CP0_TCBind & ~mask) | (arg1 & mask);
    env->active_tc.CP0_TCBind = newval;
}

void helper_mttc0_tcbind(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t mask = (1 << CP0TCBd_TBE);
    uint32_t newval;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other->mvp->CP0_MVPControl & (1 << CP0MVPCo_VPC))
        mask |= (1 << CP0TCBd_CurVPE);
    if (other_tc == other->current_tc) {
        newval = (other->active_tc.CP0_TCBind & ~mask) | (arg1 & mask);
        other->active_tc.CP0_TCBind = newval;
    } else {
        newval = (other->tcs[other_tc].CP0_TCBind & ~mask) | (arg1 & mask);
        other->tcs[other_tc].CP0_TCBind = newval;
    }
}

void helper_mtc0_tcrestart(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.PC = arg1;
    env->active_tc.CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
    env->lladdr = 0ULL;
    /* MIPS16 not implemented. */
}

void helper_mttc0_tcrestart(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc) {
        other->active_tc.PC = arg1;
        other->active_tc.CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
        other->lladdr = 0ULL;
        /* MIPS16 not implemented. */
    } else {
        other->tcs[other_tc].PC = arg1;
        other->tcs[other_tc].CP0_TCStatus &= ~(1 << CP0TCSt_TDS);
        other->lladdr = 0ULL;
        /* MIPS16 not implemented. */
    }
}

void helper_mtc0_tchalt(CPUMIPSState *env, target_ulong arg1)
{
    MIPSCPU *cpu = mips_env_get_cpu(env);

    env->active_tc.CP0_TCHalt = arg1 & 0x1;

    // TODO: Halt TC / Restart (if allocated+active) TC.
    if (env->active_tc.CP0_TCHalt & 1) {
        mips_tc_sleep(cpu, env->current_tc);
    } else {
        mips_tc_wake(cpu, env->current_tc);
    }
}

void helper_mttc0_tchalt(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    MIPSCPU *other_cpu = mips_env_get_cpu(other);

    // TODO: Halt TC / Restart (if allocated+active) TC.

    if (other_tc == other->current_tc)
        other->active_tc.CP0_TCHalt = arg1;
    else
        other->tcs[other_tc].CP0_TCHalt = arg1;

    if (arg1 & 1) {
        mips_tc_sleep(other_cpu, other_tc);
    } else {
        mips_tc_wake(other_cpu, other_tc);
    }
}

void helper_mtc0_tccontext(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.CP0_TCContext = arg1;
}

void helper_mttc0_tccontext(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.CP0_TCContext = arg1;
    else
        other->tcs[other_tc].CP0_TCContext = arg1;
}

void helper_mtc0_tcschedule(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.CP0_TCSchedule = arg1;
}

void helper_mttc0_tcschedule(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.CP0_TCSchedule = arg1;
    else
        other->tcs[other_tc].CP0_TCSchedule = arg1;
}

void helper_mtc0_tcschefback(CPUMIPSState *env, target_ulong arg1)
{
    env->active_tc.CP0_TCScheFBack = arg1;
}

void helper_mttc0_tcschefback(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.CP0_TCScheFBack = arg1;
    else
        other->tcs[other_tc].CP0_TCScheFBack = arg1;
}

void helper_mtc0_entrylo1(CPUMIPSState *env, target_ulong arg1)
{
    /* 1k pages not implemented */
    target_ulong rxi = arg1 & (env->CP0_PageGrain & (3u << CP0PG_XIE));
    env->CP0_EntryLo1 = (arg1 & MTC0_ENTRYLO_MASK(env))
                        | (rxi << (CP0EnLo_XI - 30));
}

#if defined(TARGET_MIPS64)
void helper_dmtc0_entrylo1(CPUMIPSState *env, uint64_t arg1)
{
    uint64_t rxi = arg1 & ((env->CP0_PageGrain & (3ull << CP0PG_XIE)) << 32);
    env->CP0_EntryLo1 = (arg1 & DMTC0_ENTRYLO_MASK(env)) | rxi;
}
#endif

void helper_mtc0_context(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (arg1 & ~0x007FFFFF);
}

void helper_mtc0_pagemask(CPUMIPSState *env, target_ulong arg1)
{
    uint64_t mask = arg1 >> (TARGET_PAGE_BITS + 1);
    if (!(env->insn_flags & ISA_MIPS32R6) || (arg1 == ~0) ||
        (mask == 0x0000 || mask == 0x0003 || mask == 0x000F ||
         mask == 0x003F || mask == 0x00FF || mask == 0x03FF ||
         mask == 0x0FFF || mask == 0x3FFF || mask == 0xFFFF)) {
        env->CP0_PageMask = arg1 & (0x1FFFFFFF & (TARGET_PAGE_MASK << 1));
    }
}

void helper_mtc0_pagegrain(CPUMIPSState *env, target_ulong arg1)
{
    /* SmartMIPS not implemented */
    /* 1k pages not implemented */
    env->CP0_PageGrain = (arg1 & env->CP0_PageGrain_rw_bitmask) |
                         (env->CP0_PageGrain & ~env->CP0_PageGrain_rw_bitmask);
    compute_hflags(env);
    restore_pamask(env);
}

void helper_mtc0_segctl0(CPUMIPSState *env, target_ulong arg1)
{
    CPUState *cs = CPU(mips_env_get_cpu(env));

    env->CP0_SegCtl0 = arg1 & CP0SC0_MASK;
    tlb_flush(cs);
}

void helper_mtc0_segctl1(CPUMIPSState *env, target_ulong arg1)
{
    CPUState *cs = CPU(mips_env_get_cpu(env));

    env->CP0_SegCtl1 = arg1 & CP0SC1_MASK;
    tlb_flush(cs);
}

void helper_mtc0_segctl2(CPUMIPSState *env, target_ulong arg1)
{
    CPUState *cs = CPU(mips_env_get_cpu(env));

    env->CP0_SegCtl2 = arg1 & CP0SC2_MASK;
    tlb_flush(cs);
}

void helper_mtc0_wired(CPUMIPSState *env, target_ulong arg1)
{
    if (env->insn_flags & ISA_MIPS32R6) {
        if (arg1 < env->tlb->nb_tlb) {
            env->CP0_Wired = arg1;
        }
    } else {
        env->CP0_Wired = arg1 % env->tlb->nb_tlb;
    }
}

void helper_mtc0_srsconf0(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf0 |= arg1 & env->CP0_SRSConf0_rw_bitmask;
}

void helper_mtc0_srsconf1(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf1 |= arg1 & env->CP0_SRSConf1_rw_bitmask;
}

void helper_mtc0_srsconf2(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf2 |= arg1 & env->CP0_SRSConf2_rw_bitmask;
}

void helper_mtc0_srsconf3(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf3 |= arg1 & env->CP0_SRSConf3_rw_bitmask;
}

void helper_mtc0_srsconf4(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_SRSConf4 |= arg1 & env->CP0_SRSConf4_rw_bitmask;
}

void helper_mtc0_hwrena(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = 0x0000000F;

    if ((env->CP0_Config1 & (1 << CP0C1_PC)) &&
        (env->insn_flags & ISA_MIPS32R6)) {
        mask |= (1 << 4);
    }
    if (env->insn_flags & ISA_MIPS32R6) {
        mask |= (1 << 5);
    }
    if (env->CP0_Config3 & (1 << CP0C3_ULRI)) {
        mask |= (1 << 29);

        if (arg1 & (1 << 29)) {
            env->hflags |= MIPS_HFLAG_HWRENA_ULR;
        } else {
            env->hflags &= ~MIPS_HFLAG_HWRENA_ULR;
        }
    }

    env->CP0_HWREna = arg1 & mask;
}

void helper_mtc0_count(CPUMIPSState *env, target_ulong arg1)
{
    qemu_mutex_lock_iothread();
    cpu_mips_store_count(env, arg1);
    qemu_mutex_unlock_iothread();
}

void helper_mtc0_entryhi(CPUMIPSState *env, target_ulong arg1)
{
    target_ulong old, val, mask;
    mask = (TARGET_PAGE_MASK << 1) | env->CP0_EntryHi_ASID_mask;
    if (((env->CP0_Config4 >> CP0C4_IE) & 0x3) >= 2) {
        mask |= 1 << CP0EnHi_EHINV;
    }

    /* 1k pages not implemented */
#if defined(TARGET_MIPS64)
    if (env->insn_flags & ISA_MIPS32R6) {
        int entryhi_r = extract64(arg1, 62, 2);
        int config0_at = extract32(env->CP0_Config0, 13, 2);
        bool no_supervisor = (env->CP0_Status_rw_bitmask & 0x8) == 0;
        if ((entryhi_r == 2) ||
            (entryhi_r == 1 && (no_supervisor || config0_at == 1))) {
            /* skip EntryHi.R field if new value is reserved */
            mask &= ~(0x3ull << 62);
        }
    }
    mask &= env->SEGMask;
#endif
    old = env->CP0_EntryHi;
    val = (arg1 & mask) | (old & ~mask);
    env->CP0_EntryHi = val;
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        sync_c0_entryhi(env, env->current_tc);
    }
    /* If the ASID changes, flush qemu's TLB.  */
    if ((old & env->CP0_EntryHi_ASID_mask) !=
        (val & env->CP0_EntryHi_ASID_mask)) {
        tlb_flush(CPU(mips_env_get_cpu(env)));
    }
}

void helper_mttc0_entryhi(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    other->CP0_EntryHi = arg1;
    sync_c0_entryhi(other, other_tc);
}

void helper_mtc0_compare(CPUMIPSState *env, target_ulong arg1)
{
    qemu_mutex_lock_iothread();
    cpu_mips_store_compare(env, arg1);
    qemu_mutex_unlock_iothread();
}

void helper_mtc0_status(CPUMIPSState *env, target_ulong arg1)
{
    MIPSCPU *cpu = mips_env_get_cpu(env);
    uint32_t val, old;

    old = env->CP0_Status;
    cpu_mips_store_status(env, arg1);
    val = env->CP0_Status;

    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("Status %08x (%08x) => %08x (%08x) Cause %08x",
                old, old & env->CP0_Cause & CP0Ca_IP_mask,
                val, val & env->CP0_Cause & CP0Ca_IP_mask,
                env->CP0_Cause);
        switch (cpu_mmu_index(env, false)) {
        case 3:
            qemu_log(", ERL\n");
            break;
        case MIPS_HFLAG_UM: qemu_log(", UM\n"); break;
        case MIPS_HFLAG_SM: qemu_log(", SM\n"); break;
        case MIPS_HFLAG_KM: qemu_log("\n"); break;
        default:
            cpu_abort(CPU(cpu), "Invalid MMU mode!\n");
            break;
        }
    }
}

void helper_mttc0_status(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t mask = env->CP0_Status_rw_bitmask & ~0xf1000018;
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    other->CP0_Status = (other->CP0_Status & ~mask) | (arg1 & mask);
    sync_c0_status(env, other, other_tc);
}

void helper_mtc0_intctl(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_IntCtl = (env->CP0_IntCtl & ~0x000003e0) | (arg1 & 0x000003e0);
}

void helper_mtc0_srsctl(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = (0xf << CP0SRSCtl_ESS) | (0xf << CP0SRSCtl_PSS);
    env->CP0_SRSCtl = (env->CP0_SRSCtl & ~mask) | (arg1 & mask);
}

void helper_mtc0_cause(CPUMIPSState *env, target_ulong arg1)
{
    qemu_mutex_lock_iothread();
    cpu_mips_store_cause(env, arg1);
    qemu_mutex_unlock_iothread();
}

void helper_mttc0_cause(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    cpu_mips_store_cause(other, arg1);
}

target_ulong helper_mftc0_epc(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_EPC;
}

target_ulong helper_mftc0_ebase(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    return other->CP0_EBase;
}

void helper_mtc0_ebase(CPUMIPSState *env, target_ulong arg1)
{
    target_ulong mask = 0x3FFFF000 | env->CP0_EBaseWG_rw_bitmask;
    if (arg1 & env->CP0_EBaseWG_rw_bitmask) {
        mask |= ~0x3FFFFFFF;
    }
    env->CP0_EBase = (env->CP0_EBase & ~mask) | (arg1 & mask);
}

void helper_mttc0_ebase(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    target_ulong mask = 0x3FFFF000 | env->CP0_EBaseWG_rw_bitmask;
    if (arg1 & env->CP0_EBaseWG_rw_bitmask) {
        mask |= ~0x3FFFFFFF;
    }
    other->CP0_EBase = (other->CP0_EBase & ~mask) | (arg1 & mask);
}

target_ulong helper_mftc0_configx(CPUMIPSState *env, target_ulong idx)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    switch (idx) {
    case 0: return other->CP0_Config0;
    case 1: return other->CP0_Config1;
    case 2: return other->CP0_Config2;
    case 3: return other->CP0_Config3;
    /* 4 and 5 are reserved.  */
    case 6: return other->CP0_Config6;
    case 7: return other->CP0_Config7;
    default:
        break;
    }
    return 0;
}

void helper_mtc0_config0(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config0 = (env->CP0_Config0 & 0x81FFFFF8) | (arg1 & 0x00000007);
}

void helper_mtc0_config2(CPUMIPSState *env, target_ulong arg1)
{
    /* tertiary/secondary caches not implemented */
    env->CP0_Config2 = (env->CP0_Config2 & 0x8FFF0FFF);
}

void helper_mtc0_config3(CPUMIPSState *env, target_ulong arg1)
{
    if (env->insn_flags & ASE_MICROMIPS) {
        env->CP0_Config3 = (env->CP0_Config3 & ~(1 << CP0C3_ISA_ON_EXC)) |
                           (arg1 & (1 << CP0C3_ISA_ON_EXC));
    }
}

void helper_mtc0_config4(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config4 = (env->CP0_Config4 & (~env->CP0_Config4_rw_bitmask)) |
                       (arg1 & env->CP0_Config4_rw_bitmask);
}

void helper_mtc0_config5(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config5 = (env->CP0_Config5 & (~env->CP0_Config5_rw_bitmask)) |
                       (arg1 & env->CP0_Config5_rw_bitmask);
    compute_hflags(env);
}

void helper_mtc0_lladdr(CPUMIPSState *env, target_ulong arg1)
{
    target_long mask = env->CP0_LLAddr_rw_bitmask;
    arg1 = arg1 << env->CP0_LLAddr_shift;
    env->lladdr = (env->lladdr & ~mask) | (arg1 & mask);
}

#define MTC0_MAAR_MASK(env) \
        ((0x1ULL << 63) | ((env->PAMask >> 4) & ~0xFFFull) | 0x3)

void helper_mtc0_maar(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_MAAR[env->CP0_MAARI] = arg1 & MTC0_MAAR_MASK(env);
}

void helper_mthc0_maar(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_MAAR[env->CP0_MAARI] =
        (((uint64_t) arg1 << 32) & MTC0_MAAR_MASK(env)) |
        (env->CP0_MAAR[env->CP0_MAARI] & 0x00000000ffffffffULL);
}

void helper_mtc0_maari(CPUMIPSState *env, target_ulong arg1)
{
    int index = arg1 & 0x3f;
    if (index == 0x3f) {
        /* Software may write all ones to INDEX to determine the
           maximum value supported. */
        env->CP0_MAARI = MIPS_MAAR_MAX - 1;
    } else if (index < MIPS_MAAR_MAX) {
        env->CP0_MAARI = index;
    }
    /* Other than the all ones, if the
       value written is not supported, then INDEX is unchanged
       from its previous value. */
}

void helper_mtc0_watchlo(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    /* Watch exceptions for instructions, data loads, data stores
       not implemented. */
    env->CP0_WatchLo[sel] = (arg1 & ~0x7);
}

void helper_mtc0_watchhi(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int mask = 0x40000FF8 | (env->CP0_EntryHi_ASID_mask << CP0WH_ASID);
    env->CP0_WatchHi[sel] = arg1 & mask;
    env->CP0_WatchHi[sel] &= ~(env->CP0_WatchHi[sel] & arg1 & 0x7);
}

void helper_mtc0_xcontext(CPUMIPSState *env, target_ulong arg1)
{
    target_ulong mask = (1ULL << (env->SEGBITS - 7)) - 1;
    env->CP0_XContext = (env->CP0_XContext & mask) | (arg1 & ~mask);
}

void helper_mtc0_framemask(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Framemask = arg1; /* XXX */
}

void helper_mtc0_debug(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Debug = (env->CP0_Debug & 0x8C03FC1F) | (arg1 & 0x13300120);
    if (arg1 & (1 << CP0DB_DM))
        env->hflags |= MIPS_HFLAG_DM;
    else
        env->hflags &= ~MIPS_HFLAG_DM;
}

void helper_mttc0_debug(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    uint32_t val = arg1 & ((1 << CP0DB_SSt) | (1 << CP0DB_Halt));
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    /* XXX: Might be wrong, check with EJTAG spec. */
    if (other_tc == other->current_tc)
        other->active_tc.CP0_Debug_tcstatus = val;
    else
        other->tcs[other_tc].CP0_Debug_tcstatus = val;
    other->CP0_Debug = (other->CP0_Debug &
                     ((1 << CP0DB_SSt) | (1 << CP0DB_Halt))) |
                     (arg1 & ~((1 << CP0DB_SSt) | (1 << CP0DB_Halt)));
}

void helper_mtc0_performance0(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Performance0 = arg1 & 0x000007ff;
}

void helper_mtc0_errctl(CPUMIPSState *env, target_ulong arg1)
{
    int32_t wst = arg1 & (1 << CP0EC_WST);
    int32_t spr = arg1 & (1 << CP0EC_SPR);
    int32_t itc = env->itc_tag ? (arg1 & (1 << CP0EC_ITC)) : 0;

    env->CP0_ErrCtl = wst | spr | itc;

    if (itc && !wst && !spr) {
        env->hflags |= MIPS_HFLAG_ITC_CACHE;
    } else {
        env->hflags &= ~MIPS_HFLAG_ITC_CACHE;
    }
}

void helper_mtc0_taglo(CPUMIPSState *env, target_ulong arg1)
{
    if (env->hflags & MIPS_HFLAG_ITC_CACHE) {
        /* If CACHE instruction is configured for ITC tags then make all
           CP0.TagLo bits writable. The actual write to ITC Configuration
           Tag will take care of the read-only bits. */
        env->CP0_TagLo = arg1;
    } else {
        env->CP0_TagLo = arg1 & 0xFFFFFCF6;
    }
}

void helper_mtc0_datalo(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_DataLo = arg1; /* XXX */
}

void helper_mtc0_taghi(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_TagHi = arg1; /* XXX */
}

void helper_mtc0_datahi(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_DataHi = arg1; /* XXX */
}

/* MIPS MT functions */
target_ulong helper_mftgpr(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.gpr[sel];
    else
        return other->tcs[other_tc].gpr[sel];
}

target_ulong helper_mftlo(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.LO[sel];
    else
        return other->tcs[other_tc].LO[sel];
}

target_ulong helper_mfthi(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.HI[sel];
    else
        return other->tcs[other_tc].HI[sel];
}

target_ulong helper_mftacx(CPUMIPSState *env, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.ACX[sel];
    else
        return other->tcs[other_tc].ACX[sel];
}

target_ulong helper_mftdsp(CPUMIPSState *env)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        return other->active_tc.DSPControl;
    else
        return other->tcs[other_tc].DSPControl;
}

void helper_mttgpr(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.gpr[sel] = arg1;
    else
        other->tcs[other_tc].gpr[sel] = arg1;
}

void helper_mttlo(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.LO[sel] = arg1;
    else
        other->tcs[other_tc].LO[sel] = arg1;
}

void helper_mtthi(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.HI[sel] = arg1;
    else
        other->tcs[other_tc].HI[sel] = arg1;
}

void helper_mttacx(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.ACX[sel] = arg1;
    else
        other->tcs[other_tc].ACX[sel] = arg1;
}

void helper_mttdsp(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    if (other_tc == other->current_tc)
        other->active_tc.DSPControl = arg1;
    else
        other->tcs[other_tc].DSPControl = arg1;
}

/* MIPS MT functions */
target_ulong helper_dmt(void)
{
    // TODO
     return 0;
}

target_ulong helper_emt(void)
{
    // TODO
    return 0;
}

target_ulong helper_dvpe(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->mvp->CP0_MVPControl;

    CPU_FOREACH(other_cs) {
        MIPSCPU *other_cpu = MIPS_CPU(other_cs);
        /* Turn off all VPEs except the one executing the dvpe.  */
        if (&other_cpu->env != env) {
            other_cpu->env.mvp->CP0_MVPControl &= ~(1 << CP0MVPCo_EVP);
            mips_vpe_sleep(other_cpu);
        }
    }
    return prev;
}

target_ulong helper_evpe(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->mvp->CP0_MVPControl;

    CPU_FOREACH(other_cs) {
        MIPSCPU *other_cpu = MIPS_CPU(other_cs);

        if (&other_cpu->env != env
            /* If the VPE is WFI, don't disturb its sleep.  */
            && !mips_vpe_is_wfi(other_cpu)) {
            /* Enable the VPE.  */
            other_cpu->env.mvp->CP0_MVPControl |= (1 << CP0MVPCo_EVP);
            mips_vpe_wake(other_cpu); /* And wake it up.  */
        }
    }
    return prev;
}
#endif /* !CONFIG_USER_ONLY */

void helper_fork(target_ulong arg1, target_ulong arg2)
{
    // arg1 = rt, arg2 = rs
    // TODO: store to TC register
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
        if (0 /* TODO: TC underflow */) {
            env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
            do_raise_exception(env, EXCP_THREAD, GETPC());
        } else {
            // TODO: Deallocate TC
        }
    } else if (arg1 > 0) {
        /* Yield qualifier inputs not implemented. */
        env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
        env->CP0_VPEControl |= 2 << CP0VPECo_EXCPT;
        do_raise_exception(env, EXCP_THREAD, GETPC());
    }
    return env->CP0_YQMask;
}

/* R6 Multi-threading */
#ifndef CONFIG_USER_ONLY
target_ulong helper_dvp(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->CP0_VPControl;

    if (!((env->CP0_VPControl >> CP0VPCtl_DIS) & 1)) {
        CPU_FOREACH(other_cs) {
            MIPSCPU *other_cpu = MIPS_CPU(other_cs);
            /* Turn off all VPs except the one executing the dvp. */
            if (&other_cpu->env != env) {
                mips_vpe_sleep(other_cpu);
            }
        }
        env->CP0_VPControl |= (1 << CP0VPCtl_DIS);
    }
    return prev;
}

target_ulong helper_evp(CPUMIPSState *env)
{
    CPUState *other_cs = first_cpu;
    target_ulong prev = env->CP0_VPControl;

    if ((env->CP0_VPControl >> CP0VPCtl_DIS) & 1) {
        CPU_FOREACH(other_cs) {
            MIPSCPU *other_cpu = MIPS_CPU(other_cs);
            if ((&other_cpu->env != env) && !mips_vp_is_wfi(other_cpu)) {
                /* If the VP is WFI, don't disturb its sleep.
                 * Otherwise, wake it up. */
                mips_vpe_wake(other_cpu);
            }
        }
        env->CP0_VPControl &= ~(1 << CP0VPCtl_DIS);
    }
    return prev;
}
#endif /* !CONFIG_USER_ONLY */

#ifndef CONFIG_USER_ONLY
/* TLB management */
static void r4k_mips_tlb_flush_extra (CPUMIPSState *env, int first)
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
    int idx;
    r4k_tlb_t *tlb;
    uint16_t ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;

    for (idx = 0; idx < env->tlb->nb_tlb; idx++) {
        tlb = &env->tlb->mmu.r4k.tlb[idx];
        if (!tlb->G && tlb->ASID == ASID) {
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
    r4k_tlb_t *tlb;
    int idx;
    target_ulong VPN;
    uint16_t ASID;
    bool EHINV, G, V0, D0, V1, D1, XI0, XI1, RI0, RI1;

    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    VPN &= env->SEGMask;
#endif
    ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
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

    /* Discard cached TLB entries, unless tlbwi is just upgrading access
       permissions on the current entry. */
    if (tlb->VPN != VPN || tlb->ASID != ASID || tlb->G != G ||
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
    r4k_tlb_t *tlb;
    target_ulong mask;
    target_ulong tag;
    target_ulong VPN;
    uint16_t ASID;
    int i;

    ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    for (i = 0; i < env->tlb->nb_tlb; i++) {
        tlb = &env->tlb->mmu.r4k.tlb[i];
        /* 1k pages are not supported. */
        mask = tlb->PageMask | ~(TARGET_PAGE_MASK << 1);
        tag = env->CP0_EntryHi & ~mask;
        VPN = tlb->VPN & ~mask;
#if defined(TARGET_MIPS64)
        tag &= env->SEGMask;
#endif
        /* Check ASID, virtual page number & size */
        if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag && !tlb->EHINV) {
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
            /* Check ASID, virtual page number & size */
            if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
                r4k_mips_tlb_flush_extra (env, i);
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
    r4k_tlb_t *tlb;
    uint16_t ASID;
    int idx;

    ASID = env->CP0_EntryHi & env->CP0_EntryHi_ASID_mask;
    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];

    /* If this will change the current ASID, flush qemu's TLB.  */
    if (ASID != tlb->ASID)
        cpu_mips_tlb_flush(env);

    r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);

    if (tlb->EHINV) {
        env->CP0_EntryHi = 1 << CP0EnHi_EHINV;
        env->CP0_PageMask = 0;
        env->CP0_EntryLo0 = 0;
        env->CP0_EntryLo1 = 0;
    } else {
        env->CP0_EntryHi = tlb->VPN | tlb->ASID;
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
        if (env->CP0_Status & (1 << CP0St_ERL))
            qemu_log(" ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
        if (env->hflags & MIPS_HFLAG_DM)
            qemu_log(" DEPC " TARGET_FMT_lx, env->CP0_DEPC);
        qemu_log("\n");
    }
}

static void debug_post_eret(CPUMIPSState *env)
{
    MIPSCPU *cpu = mips_env_get_cpu(env);

    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("  =>  PC " TARGET_FMT_lx " EPC " TARGET_FMT_lx,
                env->active_tc.PC, env->CP0_EPC);
        if (env->CP0_Status & (1 << CP0St_ERL))
            qemu_log(" ErrorEPC " TARGET_FMT_lx, env->CP0_ErrorEPC);
        if (env->hflags & MIPS_HFLAG_DM)
            qemu_log(" DEPC " TARGET_FMT_lx, env->CP0_DEPC);
        switch (cpu_mmu_index(env, false)) {
        case 3:
            qemu_log(", ERL\n");
            break;
        case MIPS_HFLAG_UM: qemu_log(", UM\n"); break;
        case MIPS_HFLAG_SM: qemu_log(", SM\n"); break;
        case MIPS_HFLAG_KM: qemu_log("\n"); break;
        default:
            cpu_abort(CPU(cpu), "Invalid MMU mode!\n");
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
    env->lladdr = 1;
}

void helper_eretnc(CPUMIPSState *env)
{
    exception_return(env);
}

void helper_deret(CPUMIPSState *env)
{
    debug_pre_eret(env);
    set_pc(env, env->CP0_DEPC);

    env->hflags &= ~MIPS_HFLAG_DM;
    compute_hflags(env);
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
    int32_t count;
    check_hwrena(env, 2, GETPC());
#ifdef CONFIG_USER_ONLY
    count = env->CP0_Count;
#else
    qemu_mutex_lock_iothread();
    count = (int32_t)cpu_mips_get_count(env);
    qemu_mutex_unlock_iothread();
#endif
    return count;
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
        if (env->active_tc.gpr[4] == 0)
            env->active_tc.gpr[2] = -1;
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
    CPUState *cs = CPU(mips_env_get_cpu(env));

    cs->halted = 1;
    cpu_reset_interrupt(cs, CPU_INTERRUPT_WAKE);
    /* Last instruction in the block, PC was updated before
       - no need to recover PC and icount */
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

    env->CP0_BadVAddr = addr;

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

void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType access_type,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = mips_cpu_handle_mmu_fault(cs, addr, access_type, mmu_idx);
    if (ret) {
        MIPSCPU *cpu = MIPS_CPU(cs);
        CPUMIPSState *env = &cpu->env;

        do_raise_exception_err(env, cs->exception_index,
                               env->error_code, retaddr);
    }
}

void mips_cpu_unassigned_access(CPUState *cs, hwaddr addr,
                                bool is_write, bool is_exec, int unused,
                                unsigned size)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;

    /*
     * Raising an exception with KVM enabled will crash because it won't be from
     * the main execution loop so the longjmp won't have a matching setjmp.
     * Until we can trigger a bus error exception through KVM lets just ignore
     * the access.
     */
    if (kvm_enabled()) {
        return;
    }

    if (is_exec) {
        raise_exception(env, EXCP_IBE);
    } else {
        raise_exception(env, EXCP_DBE);
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
        arg1 = ((env->active_fpu.fcr31 >> 24) & 0xfe) | ((env->active_fpu.fcr31 >> 23) & 0x1);
        break;
    case 26:
        arg1 = env->active_fpu.fcr31 & 0x0003f07c;
        break;
    case 28:
        arg1 = (env->active_fpu.fcr31 & 0x00000f83) | ((env->active_fpu.fcr31 >> 22) & 0x4);
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
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0x017fffff) | ((arg1 & 0xfe) << 24) |
                     ((arg1 & 0x1) << 23);
        break;
    case 26:
        if (arg1 & 0x007c0000)
            return;
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfffc0f83) | (arg1 & 0x0003f07c);
        break;
    case 28:
        if (arg1 & 0x007c0000)
            return;
        env->active_fpu.fcr31 = (env->active_fpu.fcr31 & 0xfefff07c) | (arg1 & 0x00000f83) |
                     ((arg1 & 0x4) << 22);
        break;
    case 31:
        env->active_fpu.fcr31 = (arg1 & env->active_fpu.fcr31_rw_bitmask) |
               (env->active_fpu.fcr31 & ~(env->active_fpu.fcr31_rw_bitmask));
        break;
    default:
        return;
    }
    restore_fp_status(env);
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    if ((GET_FP_ENABLE(env->active_fpu.fcr31) | 0x20) & GET_FP_CAUSE(env->active_fpu.fcr31))
        do_raise_exception(env, EXCP_FPE, GETPC());
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
    int tmp = ieee_ex_to_mips(get_float_exception_flags(&env->active_fpu.fp_status));

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

/* Float support.
   Single precition routines have a "s" suffix, double precision a
   "d" suffix, 32bit integer "w", 64bit integer "l", paired single "ps",
   paired single lower "pl", paired single upper "pu".  */

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
    fdt2 = float64_maybe_silence_nan(fdt2, &env->active_fpu.fp_status);
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
    fst2 = float32_maybe_silence_nan(fst2, &env->active_fpu.fp_status);
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

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
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

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
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

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
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

    set_float_rounding_mode(float_round_nearest_even, &env->active_fpu.fp_status);
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

    dt2 = float64_to_int64_round_to_zero(fdt0, &env->active_fpu.fp_status);
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

    fst2 = float32_div(float32_one, fdt0 & 0XFFFFFFFF, &env->active_fpu.fp_status);
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
uint ## bits ## _t helper_float_ ## name (CPUMIPSState *env,                \
                                          uint ## bits ## _t fs)            \
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
uint ## bits ## _t float_ ## name (uint ## bits ## _t arg,           \
                                   float_status *status)             \
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
uint ## bits ## _t helper_float_ ## name (CPUMIPSState *env,         \
                                          uint ## bits ## _t arg)    \
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
    dt2 = float64_ ## name (fdt0, fdt1, &env->active_fpu.fp_status);     \
    update_fcr31(env, GETPC());                                    \
    return dt2;                                                    \
}                                                                  \
                                                                   \
uint32_t helper_float_ ## name ## _s(CPUMIPSState *env,            \
                                     uint32_t fst0, uint32_t fst1) \
{                                                                  \
    uint32_t wt2;                                                  \
                                                                   \
    wt2 = float32_ ## name (fst0, fst1, &env->active_fpu.fp_status);     \
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
    wt2 = float32_ ## name (fst0, fst1, &env->active_fpu.fp_status);     \
    wth2 = float32_ ## name (fsth0, fsth1, &env->active_fpu.fp_status);  \
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
    fdt2 = float64_chs(float64_sub(fdt2, float64_one, &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_recip2_s(CPUMIPSState *env, uint32_t fst0, uint32_t fst2)
{
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_sub(fst2, float32_one, &env->active_fpu.fp_status));
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
    fst2 = float32_chs(float32_sub(fst2, float32_one, &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_sub(fsth2, float32_one, &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

uint64_t helper_float_rsqrt2_d(CPUMIPSState *env, uint64_t fdt0, uint64_t fdt2)
{
    fdt2 = float64_mul(fdt0, fdt2, &env->active_fpu.fp_status);
    fdt2 = float64_sub(fdt2, float64_one, &env->active_fpu.fp_status);
    fdt2 = float64_chs(float64_div(fdt2, FLOAT_TWO64, &env->active_fpu.fp_status));
    update_fcr31(env, GETPC());
    return fdt2;
}

uint32_t helper_float_rsqrt2_s(CPUMIPSState *env, uint32_t fst0, uint32_t fst2)
{
    fst2 = float32_mul(fst0, fst2, &env->active_fpu.fp_status);
    fst2 = float32_sub(fst2, float32_one, &env->active_fpu.fp_status);
    fst2 = float32_chs(float32_div(fst2, FLOAT_TWO32, &env->active_fpu.fp_status));
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
    fst2 = float32_chs(float32_div(fst2, FLOAT_TWO32, &env->active_fpu.fp_status));
    fsth2 = float32_chs(float32_div(fsth2, FLOAT_TWO32, &env->active_fpu.fp_status));
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

    fst2 = float32_add (fst0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_add (fst1, fsth1, &env->active_fpu.fp_status);
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

    fst2 = float32_mul (fst0, fsth0, &env->active_fpu.fp_status);
    fsth2 = float32_mul (fst1, fsth1, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    return ((uint64_t)fsth2 << 32) | fst2;
}

#define FLOAT_MINMAX(name, bits, minmaxfunc)                            \
uint ## bits ## _t helper_float_ ## name (CPUMIPSState *env,            \
                                          uint ## bits ## _t fs,        \
                                          uint ## bits ## _t ft)        \
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
uint ## bits ## _t helper_float_ ## name (CPUMIPSState *env,            \
                                          uint ## bits ## _t fs,        \
                                          uint ## bits ## _t ft,        \
                                          uint ## bits ## _t fd)        \
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

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered_quiet() is still called. */
FOP_COND_D(f,   (float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status), 0))
FOP_COND_D(un,  float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status))
FOP_COND_D(eq,  float64_eq_quiet(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ueq, float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)  || float64_eq_quiet(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(olt, float64_lt_quiet(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ult, float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)  || float64_lt_quiet(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ole, float64_le_quiet(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ule, float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)  || float64_le_quiet(fdt0, fdt1, &env->active_fpu.fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered() is still called. */
FOP_COND_D(sf,  (float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status), 0))
FOP_COND_D(ngle,float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status))
FOP_COND_D(seq, float64_eq(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ngl, float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)  || float64_eq(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(lt,  float64_lt(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(nge, float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)  || float64_lt(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(le,  float64_le(fdt0, fdt1, &env->active_fpu.fp_status))
FOP_COND_D(ngt, float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)  || float64_le(fdt0, fdt1, &env->active_fpu.fp_status))

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

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called. */
FOP_COND_S(f,   (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status), 0))
FOP_COND_S(un,  float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status))
FOP_COND_S(eq,  float32_eq_quiet(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ueq, float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)  || float32_eq_quiet(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(olt, float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ult, float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)  || float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ole, float32_le_quiet(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ule, float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)  || float32_le_quiet(fst0, fst1, &env->active_fpu.fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called. */
FOP_COND_S(sf,  (float32_unordered(fst1, fst0, &env->active_fpu.fp_status), 0))
FOP_COND_S(ngle,float32_unordered(fst1, fst0, &env->active_fpu.fp_status))
FOP_COND_S(seq, float32_eq(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ngl, float32_unordered(fst1, fst0, &env->active_fpu.fp_status)  || float32_eq(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(lt,  float32_lt(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(nge, float32_unordered(fst1, fst0, &env->active_fpu.fp_status)  || float32_lt(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(le,  float32_le(fst0, fst1, &env->active_fpu.fp_status))
FOP_COND_S(ngt, float32_unordered(fst1, fst0, &env->active_fpu.fp_status)  || float32_le(fst0, fst1, &env->active_fpu.fp_status))

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

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called. */
FOP_COND_PS(f,   (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status), 0),
                 (float32_unordered_quiet(fsth1, fsth0, &env->active_fpu.fp_status), 0))
FOP_COND_PS(un,  float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status),
                 float32_unordered_quiet(fsth1, fsth0, &env->active_fpu.fp_status))
FOP_COND_PS(eq,  float32_eq_quiet(fst0, fst1, &env->active_fpu.fp_status),
                 float32_eq_quiet(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ueq, float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)    || float32_eq_quiet(fst0, fst1, &env->active_fpu.fp_status),
                 float32_unordered_quiet(fsth1, fsth0, &env->active_fpu.fp_status)  || float32_eq_quiet(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(olt, float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status),
                 float32_lt_quiet(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ult, float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)    || float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status),
                 float32_unordered_quiet(fsth1, fsth0, &env->active_fpu.fp_status)  || float32_lt_quiet(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ole, float32_le_quiet(fst0, fst1, &env->active_fpu.fp_status),
                 float32_le_quiet(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ule, float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)    || float32_le_quiet(fst0, fst1, &env->active_fpu.fp_status),
                 float32_unordered_quiet(fsth1, fsth0, &env->active_fpu.fp_status)  || float32_le_quiet(fsth0, fsth1, &env->active_fpu.fp_status))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called. */
FOP_COND_PS(sf,  (float32_unordered(fst1, fst0, &env->active_fpu.fp_status), 0),
                 (float32_unordered(fsth1, fsth0, &env->active_fpu.fp_status), 0))
FOP_COND_PS(ngle,float32_unordered(fst1, fst0, &env->active_fpu.fp_status),
                 float32_unordered(fsth1, fsth0, &env->active_fpu.fp_status))
FOP_COND_PS(seq, float32_eq(fst0, fst1, &env->active_fpu.fp_status),
                 float32_eq(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ngl, float32_unordered(fst1, fst0, &env->active_fpu.fp_status)    || float32_eq(fst0, fst1, &env->active_fpu.fp_status),
                 float32_unordered(fsth1, fsth0, &env->active_fpu.fp_status)  || float32_eq(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(lt,  float32_lt(fst0, fst1, &env->active_fpu.fp_status),
                 float32_lt(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(nge, float32_unordered(fst1, fst0, &env->active_fpu.fp_status)    || float32_lt(fst0, fst1, &env->active_fpu.fp_status),
                 float32_unordered(fsth1, fsth0, &env->active_fpu.fp_status)  || float32_lt(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(le,  float32_le(fst0, fst1, &env->active_fpu.fp_status),
                 float32_le(fsth0, fsth1, &env->active_fpu.fp_status))
FOP_COND_PS(ngt, float32_unordered(fst1, fst0, &env->active_fpu.fp_status)    || float32_le(fst0, fst1, &env->active_fpu.fp_status),
                 float32_unordered(fsth1, fsth0, &env->active_fpu.fp_status)  || float32_le(fsth0, fsth1, &env->active_fpu.fp_status))

/* R6 compare operations */
#define FOP_CONDN_D(op, cond)                                       \
uint64_t helper_r6_cmp_d_ ## op(CPUMIPSState * env, uint64_t fdt0,  \
                         uint64_t fdt1)                             \
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

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered_quiet() is still called. */
FOP_CONDN_D(af,  (float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status), 0))
FOP_CONDN_D(un,  (float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)))
FOP_CONDN_D(eq,  (float64_eq_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(ueq, (float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)
                  || float64_eq_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(lt,  (float64_lt_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(ult, (float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)
                  || float64_lt_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(le,  (float64_le_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(ule, (float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)
                  || float64_le_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float64_unordered() is still called. */
FOP_CONDN_D(saf,  (float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status), 0))
FOP_CONDN_D(sun,  (float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)))
FOP_CONDN_D(seq,  (float64_eq(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(sueq, (float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_eq(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(slt,  (float64_lt(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(sult, (float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(sle,  (float64_le(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(sule, (float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_le(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(or,   (float64_le_quiet(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_le_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(une,  (float64_unordered_quiet(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(ne,   (float64_lt_quiet(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_lt_quiet(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(sor,  (float64_le(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_le(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(sune, (float64_unordered(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_lt(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1, &env->active_fpu.fp_status)))
FOP_CONDN_D(sne,  (float64_lt(fdt1, fdt0, &env->active_fpu.fp_status)
                   || float64_lt(fdt0, fdt1, &env->active_fpu.fp_status)))

#define FOP_CONDN_S(op, cond)                                       \
uint32_t helper_r6_cmp_s_ ## op(CPUMIPSState * env, uint32_t fst0,  \
                         uint32_t fst1)                             \
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

/* NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered_quiet() is still called. */
FOP_CONDN_S(af,   (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status), 0))
FOP_CONDN_S(un,   (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)))
FOP_CONDN_S(eq,   (float32_eq_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(ueq,  (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_eq_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(lt,   (float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(ult,  (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(le,   (float32_le_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(ule,  (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_le_quiet(fst0, fst1, &env->active_fpu.fp_status)))
/* NOTE: the comma operator will make "cond" to eval to false,
 * but float32_unordered() is still called. */
FOP_CONDN_S(saf,  (float32_unordered(fst1, fst0, &env->active_fpu.fp_status), 0))
FOP_CONDN_S(sun,  (float32_unordered(fst1, fst0, &env->active_fpu.fp_status)))
FOP_CONDN_S(seq,  (float32_eq(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(sueq, (float32_unordered(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_eq(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(slt,  (float32_lt(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(sult, (float32_unordered(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(sle,  (float32_le(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(sule, (float32_unordered(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_le(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(or,   (float32_le_quiet(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_le_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(une,  (float32_unordered_quiet(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(ne,   (float32_lt_quiet(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt_quiet(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(sor,  (float32_le(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_le(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(sune, (float32_unordered(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1, &env->active_fpu.fp_status)))
FOP_CONDN_S(sne,  (float32_lt(fst1, fst0, &env->active_fpu.fp_status)
                   || float32_lt(fst0, fst1, &env->active_fpu.fp_status)))

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

#define MSA_LD_DF(DF, TYPE, LD_INSN, ...)                               \
void helper_msa_ld_ ## TYPE(CPUMIPSState *env, uint32_t wd,             \
                            target_ulong addr)                          \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    wr_t wx;                                                            \
    int i;                                                              \
    MEMOP_IDX(DF)                                                       \
    for (i = 0; i < DF_ELEMENTS(DF); i++) {                             \
        wx.TYPE[i] = LD_INSN(env, addr + (i << DF), ##__VA_ARGS__);     \
    }                                                                   \
    memcpy(pwd, &wx, sizeof(wr_t));                                     \
}

#if !defined(CONFIG_USER_ONLY)
MSA_LD_DF(DF_BYTE,   b, helper_ret_ldub_mmu, oi, GETPC())
MSA_LD_DF(DF_HALF,   h, helper_ret_lduw_mmu, oi, GETPC())
MSA_LD_DF(DF_WORD,   w, helper_ret_ldul_mmu, oi, GETPC())
MSA_LD_DF(DF_DOUBLE, d, helper_ret_ldq_mmu,  oi, GETPC())
#else
MSA_LD_DF(DF_BYTE,   b, cpu_ldub_data)
MSA_LD_DF(DF_HALF,   h, cpu_lduw_data)
MSA_LD_DF(DF_WORD,   w, cpu_ldl_data)
MSA_LD_DF(DF_DOUBLE, d, cpu_ldq_data)
#endif

#define MSA_PAGESPAN(x) \
        ((((x) & ~TARGET_PAGE_MASK) + MSA_WRLEN/8 - 1) >= TARGET_PAGE_SIZE)

static inline void ensure_writable_pages(CPUMIPSState *env,
                                         target_ulong addr,
                                         int mmu_idx,
                                         uintptr_t retaddr)
{
#if !defined(CONFIG_USER_ONLY)
    target_ulong page_addr;
    if (unlikely(MSA_PAGESPAN(addr))) {
        /* first page */
        probe_write(env, addr, mmu_idx, retaddr);
        /* second page */
        page_addr = (addr & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
        probe_write(env, page_addr, mmu_idx, retaddr);
    }
#endif
}

#define MSA_ST_DF(DF, TYPE, ST_INSN, ...)                               \
void helper_msa_st_ ## TYPE(CPUMIPSState *env, uint32_t wd,             \
                            target_ulong addr)                          \
{                                                                       \
    wr_t *pwd = &(env->active_fpu.fpr[wd].wr);                          \
    int mmu_idx = cpu_mmu_index(env, false);				\
    int i;                                                              \
    MEMOP_IDX(DF)                                                       \
    ensure_writable_pages(env, addr, mmu_idx, GETPC());                 \
    for (i = 0; i < DF_ELEMENTS(DF); i++) {                             \
        ST_INSN(env, addr + (i << DF), pwd->TYPE[i], ##__VA_ARGS__);    \
    }                                                                   \
}

#if !defined(CONFIG_USER_ONLY)
MSA_ST_DF(DF_BYTE,   b, helper_ret_stb_mmu, oi, GETPC())
MSA_ST_DF(DF_HALF,   h, helper_ret_stw_mmu, oi, GETPC())
MSA_ST_DF(DF_WORD,   w, helper_ret_stl_mmu, oi, GETPC())
MSA_ST_DF(DF_DOUBLE, d, helper_ret_stq_mmu, oi, GETPC())
#else
MSA_ST_DF(DF_BYTE,   b, cpu_stb_data)
MSA_ST_DF(DF_HALF,   h, cpu_stw_data)
MSA_ST_DF(DF_WORD,   w, cpu_stl_data)
MSA_ST_DF(DF_DOUBLE, d, cpu_stq_data)
#endif

void helper_cache(CPUMIPSState *env, target_ulong addr, uint32_t op)
{
#ifndef CONFIG_USER_ONLY
    target_ulong index = addr & 0x1fffffff;
    if (op == 9) {
        /* Index Store Tag */
        memory_region_dispatch_write(env->itc_tag, index, env->CP0_TagLo,
                                     8, MEMTXATTRS_UNSPECIFIED);
    } else if (op == 5) {
        /* Index Load Tag */
        memory_region_dispatch_read(env->itc_tag, index, &env->CP0_TagLo,
                                    8, MEMTXATTRS_UNSPECIFIED);
    }
#endif
}
