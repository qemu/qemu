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
#include <stdlib.h>
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "sysemu/kvm.h"

#ifndef CONFIG_USER_ONLY
static inline void cpu_mips_tlb_flush (CPUMIPSState *env, int flush_global);
#endif

/*****************************************************************************/
/* Exceptions processing helpers */

static inline void QEMU_NORETURN do_raise_exception_err(CPUMIPSState *env,
                                                        uint32_t exception,
                                                        int error_code,
                                                        uintptr_t pc)
{
    CPUState *cs = CPU(mips_env_get_cpu(env));

    if (exception < EXCP_SC) {
        qemu_log("%s: %d %d\n", __func__, exception, error_code);
    }
    cs->exception_index = exception;
    env->error_code = error_code;

    if (pc) {
        /* now we have a real cpu fault */
        cpu_restore_state(cs, pc);
    }

    cpu_loop_exit(cs);
}

static inline void QEMU_NORETURN do_raise_exception(CPUMIPSState *env,
                                                    uint32_t exception,
                                                    uintptr_t pc)
{
    do_raise_exception_err(env, exception, 0, pc);
}

void helper_raise_exception_err(CPUMIPSState *env, uint32_t exception,
                                int error_code)
{
    do_raise_exception_err(env, exception, error_code, 0);
}

void helper_raise_exception(CPUMIPSState *env, uint32_t exception)
{
    do_raise_exception(env, exception, 0);
}

#if defined(CONFIG_USER_ONLY)
#define HELPER_LD(name, insn, type)                                     \
static inline type do_##name(CPUMIPSState *env, target_ulong addr,      \
                             int mem_idx)                               \
{                                                                       \
    return (type) insn##_raw(addr);                                     \
}
#else
#define HELPER_LD(name, insn, type)                                     \
static inline type do_##name(CPUMIPSState *env, target_ulong addr,      \
                             int mem_idx)                               \
{                                                                       \
    switch (mem_idx)                                                    \
    {                                                                   \
    case 0: return (type) cpu_##insn##_kernel(env, addr); break;        \
    case 1: return (type) cpu_##insn##_super(env, addr); break;         \
    default:                                                            \
    case 2: return (type) cpu_##insn##_user(env, addr); break;          \
    }                                                                   \
}
#endif
HELPER_LD(lbu, ldub, uint8_t)
HELPER_LD(lw, ldl, int32_t)
#ifdef TARGET_MIPS64
HELPER_LD(ld, ldq, int64_t)
#endif
#undef HELPER_LD

#if defined(CONFIG_USER_ONLY)
#define HELPER_ST(name, insn, type)                                     \
static inline void do_##name(CPUMIPSState *env, target_ulong addr,      \
                             type val, int mem_idx)                     \
{                                                                       \
    insn##_raw(addr, val);                                              \
}
#else
#define HELPER_ST(name, insn, type)                                     \
static inline void do_##name(CPUMIPSState *env, target_ulong addr,      \
                             type val, int mem_idx)                     \
{                                                                       \
    switch (mem_idx)                                                    \
    {                                                                   \
    case 0: cpu_##insn##_kernel(env, addr, val); break;                 \
    case 1: cpu_##insn##_super(env, addr, val); break;                  \
    default:                                                            \
    case 2: cpu_##insn##_user(env, addr, val); break;                   \
    }                                                                   \
}
#endif
HELPER_ST(sb, stb, uint8_t)
HELPER_ST(sw, stl, uint32_t)
#ifdef TARGET_MIPS64
HELPER_ST(sd, stq, uint64_t)
#endif
#undef HELPER_ST

target_ulong helper_clo (target_ulong arg1)
{
    return clo32(arg1);
}

target_ulong helper_clz (target_ulong arg1)
{
    return clz32(arg1);
}

#if defined(TARGET_MIPS64)
target_ulong helper_dclo (target_ulong arg1)
{
    return clo64(arg1);
}

target_ulong helper_dclz (target_ulong arg1)
{
    return clz64(arg1);
}
#endif /* TARGET_MIPS64 */

/* 64 bits arithmetic for 32 bits hosts */
static inline uint64_t get_HILO(CPUMIPSState *env)
{
    return ((uint64_t)(env->active_tc.HI[0]) << 32) | (uint32_t)env->active_tc.LO[0];
}

static inline target_ulong set_HIT0_LO(CPUMIPSState *env, uint64_t HILO)
{
    target_ulong tmp;
    env->active_tc.LO[0] = (int32_t)(HILO & 0xFFFFFFFF);
    tmp = env->active_tc.HI[0] = (int32_t)(HILO >> 32);
    return tmp;
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

#ifndef CONFIG_USER_ONLY

static inline hwaddr do_translate_address(CPUMIPSState *env,
                                                      target_ulong address,
                                                      int rw)
{
    hwaddr lladdr;

    lladdr = cpu_mips_translate_address(env, address, rw);

    if (lladdr == -1LL) {
        cpu_loop_exit(CPU(mips_env_get_cpu(env)));
    } else {
        return lladdr;
    }
}

#define HELPER_LD_ATOMIC(name, insn)                                          \
target_ulong helper_##name(CPUMIPSState *env, target_ulong arg, int mem_idx)  \
{                                                                             \
    env->lladdr = do_translate_address(env, arg, 0);                          \
    env->llval = do_##insn(env, arg, mem_idx);                                \
    return env->llval;                                                        \
}
HELPER_LD_ATOMIC(ll, lw)
#ifdef TARGET_MIPS64
HELPER_LD_ATOMIC(lld, ld)
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
        helper_raise_exception(env, EXCP_AdES);                               \
    }                                                                         \
    if (do_translate_address(env, arg2, 1) == env->lladdr) {                  \
        tmp = do_##ld_insn(env, arg2, mem_idx);                               \
        if (tmp == env->llval) {                                              \
            do_##st_insn(env, arg2, arg1, mem_idx);                           \
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
    do_sb(env, arg2, (uint8_t)(arg1 >> 24), mem_idx);

    if (GET_LMASK(arg2) <= 2)
        do_sb(env, GET_OFFSET(arg2, 1), (uint8_t)(arg1 >> 16), mem_idx);

    if (GET_LMASK(arg2) <= 1)
        do_sb(env, GET_OFFSET(arg2, 2), (uint8_t)(arg1 >> 8), mem_idx);

    if (GET_LMASK(arg2) == 0)
        do_sb(env, GET_OFFSET(arg2, 3), (uint8_t)arg1, mem_idx);
}

void helper_swr(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    do_sb(env, arg2, (uint8_t)arg1, mem_idx);

    if (GET_LMASK(arg2) >= 1)
        do_sb(env, GET_OFFSET(arg2, -1), (uint8_t)(arg1 >> 8), mem_idx);

    if (GET_LMASK(arg2) >= 2)
        do_sb(env, GET_OFFSET(arg2, -2), (uint8_t)(arg1 >> 16), mem_idx);

    if (GET_LMASK(arg2) == 3)
        do_sb(env, GET_OFFSET(arg2, -3), (uint8_t)(arg1 >> 24), mem_idx);
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
    do_sb(env, arg2, (uint8_t)(arg1 >> 56), mem_idx);

    if (GET_LMASK64(arg2) <= 6)
        do_sb(env, GET_OFFSET(arg2, 1), (uint8_t)(arg1 >> 48), mem_idx);

    if (GET_LMASK64(arg2) <= 5)
        do_sb(env, GET_OFFSET(arg2, 2), (uint8_t)(arg1 >> 40), mem_idx);

    if (GET_LMASK64(arg2) <= 4)
        do_sb(env, GET_OFFSET(arg2, 3), (uint8_t)(arg1 >> 32), mem_idx);

    if (GET_LMASK64(arg2) <= 3)
        do_sb(env, GET_OFFSET(arg2, 4), (uint8_t)(arg1 >> 24), mem_idx);

    if (GET_LMASK64(arg2) <= 2)
        do_sb(env, GET_OFFSET(arg2, 5), (uint8_t)(arg1 >> 16), mem_idx);

    if (GET_LMASK64(arg2) <= 1)
        do_sb(env, GET_OFFSET(arg2, 6), (uint8_t)(arg1 >> 8), mem_idx);

    if (GET_LMASK64(arg2) <= 0)
        do_sb(env, GET_OFFSET(arg2, 7), (uint8_t)arg1, mem_idx);
}

void helper_sdr(CPUMIPSState *env, target_ulong arg1, target_ulong arg2,
                int mem_idx)
{
    do_sb(env, arg2, (uint8_t)arg1, mem_idx);

    if (GET_LMASK64(arg2) >= 1)
        do_sb(env, GET_OFFSET(arg2, -1), (uint8_t)(arg1 >> 8), mem_idx);

    if (GET_LMASK64(arg2) >= 2)
        do_sb(env, GET_OFFSET(arg2, -2), (uint8_t)(arg1 >> 16), mem_idx);

    if (GET_LMASK64(arg2) >= 3)
        do_sb(env, GET_OFFSET(arg2, -3), (uint8_t)(arg1 >> 24), mem_idx);

    if (GET_LMASK64(arg2) >= 4)
        do_sb(env, GET_OFFSET(arg2, -4), (uint8_t)(arg1 >> 32), mem_idx);

    if (GET_LMASK64(arg2) >= 5)
        do_sb(env, GET_OFFSET(arg2, -5), (uint8_t)(arg1 >> 40), mem_idx);

    if (GET_LMASK64(arg2) >= 6)
        do_sb(env, GET_OFFSET(arg2, -6), (uint8_t)(arg1 >> 48), mem_idx);

    if (GET_LMASK64(arg2) == 7)
        do_sb(env, GET_OFFSET(arg2, -7), (uint8_t)(arg1 >> 56), mem_idx);
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
                (target_long)do_lw(env, addr, mem_idx);
            addr += 4;
        }
    }

    if (do_r31) {
        env->active_tc.gpr[31] = (target_long)do_lw(env, addr, mem_idx);
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
            do_sw(env, addr, env->active_tc.gpr[multiple_regs[i]], mem_idx);
            addr += 4;
        }
    }

    if (do_r31) {
        do_sw(env, addr, env->active_tc.gpr[31], mem_idx);
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
            env->active_tc.gpr[multiple_regs[i]] = do_ld(env, addr, mem_idx);
            addr += 8;
        }
    }

    if (do_r31) {
        env->active_tc.gpr[31] = do_ld(env, addr, mem_idx);
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
            do_sd(env, addr, env->active_tc.gpr[multiple_regs[i]], mem_idx);
            addr += 8;
        }
    }

    if (do_r31) {
        do_sd(env, addr, env->active_tc.gpr[31], mem_idx);
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

static inline void mips_vpe_wake(MIPSCPU *c)
{
    /* Dont set ->halted = 0 directly, let it be done via cpu_has_work
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

/* Called for updates to CP0_Status.  */
static void sync_c0_status(CPUMIPSState *env, CPUMIPSState *cpu, int tc)
{
    int32_t tcstatus, *tcst;
    uint32_t v = cpu->CP0_Status;
    uint32_t cu, mx, asid, ksu;
    uint32_t mask = ((1 << CP0TCSt_TCU3)
                       | (1 << CP0TCSt_TCU2)
                       | (1 << CP0TCSt_TCU1)
                       | (1 << CP0TCSt_TCU0)
                       | (1 << CP0TCSt_TMX)
                       | (3 << CP0TCSt_TKSU)
                       | (0xff << CP0TCSt_TASID));

    cu = (v >> CP0St_CU0) & 0xf;
    mx = (v >> CP0St_MX) & 0x1;
    ksu = (v >> CP0St_KSU) & 0x3;
    asid = env->CP0_EntryHi & 0xff;

    tcstatus = cu << CP0TCSt_TCU0;
    tcstatus |= mx << CP0TCSt_TMX;
    tcstatus |= ksu << CP0TCSt_TKSU;
    tcstatus |= asid;

    if (tc == cpu->current_tc) {
        tcst = &cpu->active_tc.CP0_TCStatus;
    } else {
        tcst = &cpu->tcs[tc].CP0_TCStatus;
    }

    *tcst &= ~mask;
    *tcst |= tcstatus;
    compute_hflags(cpu);
}

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
    tasid = v & 0xff;
    tksu = (v >> CP0TCSt_TKSU) & 0x3;

    status = tcu << CP0St_CU0;
    status |= tmx << CP0St_MX;
    status |= tksu << CP0St_KSU;

    cpu->CP0_Status &= ~mask;
    cpu->CP0_Status |= status;

    /* Sync the TASID with EntryHi.  */
    cpu->CP0_EntryHi &= ~0xff;
    cpu->CP0_EntryHi = tasid;

    compute_hflags(cpu);
}

/* Called for updates to CP0_EntryHi.  */
static void sync_c0_entryhi(CPUMIPSState *cpu, int tc)
{
    int32_t *tcst;
    uint32_t asid, v = cpu->CP0_EntryHi;

    asid = v & 0xff;

    if (tc == cpu->current_tc) {
        tcst = &cpu->active_tc.CP0_TCStatus;
    } else {
        tcst = &cpu->tcs[tc].CP0_TCStatus;
    }

    *tcst &= ~0xff;
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
    return (int32_t)cpu_mips_get_count(env);
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

target_ulong helper_dmfc0_watchlo(CPUMIPSState *env, uint32_t sel)
{
    return env->CP0_WatchLo[sel];
}
#endif /* TARGET_MIPS64 */

void helper_mtc0_index(CPUMIPSState *env, target_ulong arg1)
{
    int num = 1;
    unsigned int tmp = env->tlb->nb_tlb;

    do {
        tmp >>= 1;
        num <<= 1;
    } while (tmp);
    env->CP0_Index = (env->CP0_Index & 0x80000000) | (arg1 & (num - 1));
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

void helper_mtc0_entrylo0(CPUMIPSState *env, target_ulong arg1)
{
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo0 = arg1 & 0x3FFFFFFF;
}

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
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_EntryLo1 = arg1 & 0x3FFFFFFF;
}

void helper_mtc0_context(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Context = (env->CP0_Context & 0x007FFFFF) | (arg1 & ~0x007FFFFF);
}

void helper_mtc0_pagemask(CPUMIPSState *env, target_ulong arg1)
{
    /* 1k pages not implemented */
    env->CP0_PageMask = arg1 & (0x1FFFFFFF & (TARGET_PAGE_MASK << 1));
}

void helper_mtc0_pagegrain(CPUMIPSState *env, target_ulong arg1)
{
    /* SmartMIPS not implemented */
    /* Large physaddr (PABITS) not implemented */
    /* 1k pages not implemented */
    env->CP0_PageGrain = 0;
}

void helper_mtc0_wired(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Wired = arg1 % env->tlb->nb_tlb;
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
    cpu_mips_store_count(env, arg1);
}

void helper_mtc0_entryhi(CPUMIPSState *env, target_ulong arg1)
{
    target_ulong old, val;

    /* 1k pages not implemented */
    val = arg1 & ((TARGET_PAGE_MASK << 1) | 0xFF);
#if defined(TARGET_MIPS64)
    val &= env->SEGMask;
#endif
    old = env->CP0_EntryHi;
    env->CP0_EntryHi = val;
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        sync_c0_entryhi(env, env->current_tc);
    }
    /* If the ASID changes, flush qemu's TLB.  */
    if ((old & 0xFF) != (val & 0xFF))
        cpu_mips_tlb_flush(env, 1);
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
    cpu_mips_store_compare(env, arg1);
}

void helper_mtc0_status(CPUMIPSState *env, target_ulong arg1)
{
    MIPSCPU *cpu = mips_env_get_cpu(env);
    uint32_t val, old;
    uint32_t mask = env->CP0_Status_rw_bitmask;

    val = arg1 & mask;
    old = env->CP0_Status;
    env->CP0_Status = (env->CP0_Status & ~mask) | val;
    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        sync_c0_status(env, env, env->current_tc);
    } else {
        compute_hflags(env);
    }

    if (qemu_loglevel_mask(CPU_LOG_EXEC)) {
        qemu_log("Status %08x (%08x) => %08x (%08x) Cause %08x",
                old, old & env->CP0_Cause & CP0Ca_IP_mask,
                val, val & env->CP0_Cause & CP0Ca_IP_mask,
                env->CP0_Cause);
        switch (env->hflags & MIPS_HFLAG_KSU) {
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
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    other->CP0_Status = arg1 & ~0xf1000018;
    sync_c0_status(env, other, other_tc);
}

void helper_mtc0_intctl(CPUMIPSState *env, target_ulong arg1)
{
    /* vectored interrupts not implemented, no performance counters. */
    env->CP0_IntCtl = (env->CP0_IntCtl & ~0x000003e0) | (arg1 & 0x000003e0);
}

void helper_mtc0_srsctl(CPUMIPSState *env, target_ulong arg1)
{
    uint32_t mask = (0xf << CP0SRSCtl_ESS) | (0xf << CP0SRSCtl_PSS);
    env->CP0_SRSCtl = (env->CP0_SRSCtl & ~mask) | (arg1 & mask);
}

static void mtc0_cause(CPUMIPSState *cpu, target_ulong arg1)
{
    uint32_t mask = 0x00C00300;
    uint32_t old = cpu->CP0_Cause;
    int i;

    if (cpu->insn_flags & ISA_MIPS32R2) {
        mask |= 1 << CP0Ca_DC;
    }

    cpu->CP0_Cause = (cpu->CP0_Cause & ~mask) | (arg1 & mask);

    if ((old ^ cpu->CP0_Cause) & (1 << CP0Ca_DC)) {
        if (cpu->CP0_Cause & (1 << CP0Ca_DC)) {
            cpu_mips_stop_count(cpu);
        } else {
            cpu_mips_start_count(cpu);
        }
    }

    /* Set/reset software interrupts */
    for (i = 0 ; i < 2 ; i++) {
        if ((old ^ cpu->CP0_Cause) & (1 << (CP0Ca_IP + i))) {
            cpu_mips_soft_irq(cpu, i, cpu->CP0_Cause & (1 << (CP0Ca_IP + i)));
        }
    }
}

void helper_mtc0_cause(CPUMIPSState *env, target_ulong arg1)
{
    mtc0_cause(env, arg1);
}

void helper_mttc0_cause(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);

    mtc0_cause(other, arg1);
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
    /* vectored interrupts not implemented */
    env->CP0_EBase = (env->CP0_EBase & ~0x3FFFF000) | (arg1 & 0x3FFFF000);
}

void helper_mttc0_ebase(CPUMIPSState *env, target_ulong arg1)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    CPUMIPSState *other = mips_cpu_map_tc(env, &other_tc);
    other->CP0_EBase = (other->CP0_EBase & ~0x3FFFF000) | (arg1 & 0x3FFFF000);
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

void helper_mtc0_config4(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config4 = (env->CP0_Config4 & (~env->CP0_Config4_rw_bitmask)) |
                       (arg1 & env->CP0_Config4_rw_bitmask);
}

void helper_mtc0_config5(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_Config5 = (env->CP0_Config5 & (~env->CP0_Config5_rw_bitmask)) |
                       (arg1 & env->CP0_Config5_rw_bitmask);
}

void helper_mtc0_lladdr(CPUMIPSState *env, target_ulong arg1)
{
    target_long mask = env->CP0_LLAddr_rw_bitmask;
    arg1 = arg1 << env->CP0_LLAddr_shift;
    env->lladdr = (env->lladdr & ~mask) | (arg1 & mask);
}

void helper_mtc0_watchlo(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    /* Watch exceptions for instructions, data loads, data stores
       not implemented. */
    env->CP0_WatchLo[sel] = (arg1 & ~0x7);
}

void helper_mtc0_watchhi(CPUMIPSState *env, target_ulong arg1, uint32_t sel)
{
    env->CP0_WatchHi[sel] = (arg1 & 0x40FF0FF8);
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

void helper_mtc0_taglo(CPUMIPSState *env, target_ulong arg1)
{
    env->CP0_TagLo = arg1 & 0xFFFFFCF6;
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
                helper_raise_exception(env, EXCP_THREAD);
            }
        }
    } else if (arg1 == 0) {
        if (0 /* TODO: TC underflow */) {
            env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
            helper_raise_exception(env, EXCP_THREAD);
        } else {
            // TODO: Deallocate TC
        }
    } else if (arg1 > 0) {
        /* Yield qualifier inputs not implemented. */
        env->CP0_VPEControl &= ~(0x7 << CP0VPECo_EXCPT);
        env->CP0_VPEControl |= 2 << CP0VPECo_EXCPT;
        helper_raise_exception(env, EXCP_THREAD);
    }
    return env->CP0_YQMask;
}

#ifndef CONFIG_USER_ONLY
/* TLB management */
static void cpu_mips_tlb_flush (CPUMIPSState *env, int flush_global)
{
    MIPSCPU *cpu = mips_env_get_cpu(env);

    /* Flush qemu's TLB and discard all shadowed entries.  */
    tlb_flush(CPU(cpu), flush_global);
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
}

static void r4k_mips_tlb_flush_extra (CPUMIPSState *env, int first)
{
    /* Discard entries from env->tlb[first] onwards.  */
    while (env->tlb->tlb_in_use > first) {
        r4k_invalidate_tlb(env, --env->tlb->tlb_in_use, 0);
    }
}

static void r4k_fill_tlb(CPUMIPSState *env, int idx)
{
    r4k_tlb_t *tlb;

    /* XXX: detect conflicting TLBs and raise a MCHECK exception when needed */
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    tlb->VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    tlb->VPN &= env->SEGMask;
#endif
    tlb->ASID = env->CP0_EntryHi & 0xFF;
    tlb->PageMask = env->CP0_PageMask;
    tlb->G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    tlb->V0 = (env->CP0_EntryLo0 & 2) != 0;
    tlb->D0 = (env->CP0_EntryLo0 & 4) != 0;
    tlb->C0 = (env->CP0_EntryLo0 >> 3) & 0x7;
    tlb->PFN[0] = (env->CP0_EntryLo0 >> 6) << 12;
    tlb->V1 = (env->CP0_EntryLo1 & 2) != 0;
    tlb->D1 = (env->CP0_EntryLo1 & 4) != 0;
    tlb->C1 = (env->CP0_EntryLo1 >> 3) & 0x7;
    tlb->PFN[1] = (env->CP0_EntryLo1 >> 6) << 12;
}

void r4k_helper_tlbwi(CPUMIPSState *env)
{
    r4k_tlb_t *tlb;
    int idx;
    target_ulong VPN;
    uint8_t ASID;
    bool G, V0, D0, V1, D1;

    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];
    VPN = env->CP0_EntryHi & (TARGET_PAGE_MASK << 1);
#if defined(TARGET_MIPS64)
    VPN &= env->SEGMask;
#endif
    ASID = env->CP0_EntryHi & 0xff;
    G = env->CP0_EntryLo0 & env->CP0_EntryLo1 & 1;
    V0 = (env->CP0_EntryLo0 & 2) != 0;
    D0 = (env->CP0_EntryLo0 & 4) != 0;
    V1 = (env->CP0_EntryLo1 & 2) != 0;
    D1 = (env->CP0_EntryLo1 & 4) != 0;

    /* Discard cached TLB entries, unless tlbwi is just upgrading access
       permissions on the current entry. */
    if (tlb->VPN != VPN || tlb->ASID != ASID || tlb->G != G ||
        (tlb->V0 && !V0) || (tlb->D0 && !D0) ||
        (tlb->V1 && !V1) || (tlb->D1 && !D1)) {
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
    uint8_t ASID;
    int i;

    ASID = env->CP0_EntryHi & 0xFF;
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
        if ((tlb->G == 1 || tlb->ASID == ASID) && VPN == tag) {
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

void r4k_helper_tlbr(CPUMIPSState *env)
{
    r4k_tlb_t *tlb;
    uint8_t ASID;
    int idx;

    ASID = env->CP0_EntryHi & 0xFF;
    idx = (env->CP0_Index & ~0x80000000) % env->tlb->nb_tlb;
    tlb = &env->tlb->mmu.r4k.tlb[idx];

    /* If this will change the current ASID, flush qemu's TLB.  */
    if (ASID != tlb->ASID)
        cpu_mips_tlb_flush (env, 1);

    r4k_mips_tlb_flush_extra(env, env->tlb->nb_tlb);

    env->CP0_EntryHi = tlb->VPN | tlb->ASID;
    env->CP0_PageMask = tlb->PageMask;
    env->CP0_EntryLo0 = tlb->G | (tlb->V0 << 1) | (tlb->D0 << 2) |
                        (tlb->C0 << 3) | (tlb->PFN[0] >> 6);
    env->CP0_EntryLo1 = tlb->G | (tlb->V1 << 1) | (tlb->D1 << 2) |
                        (tlb->C1 << 3) | (tlb->PFN[1] >> 6);
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
        switch (env->hflags & MIPS_HFLAG_KSU) {
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

void helper_eret(CPUMIPSState *env)
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
    env->lladdr = 1;
}

void helper_deret(CPUMIPSState *env)
{
    debug_pre_eret(env);
    set_pc(env, env->CP0_DEPC);

    env->hflags &= MIPS_HFLAG_DM;
    compute_hflags(env);
    debug_post_eret(env);
    env->lladdr = 1;
}
#endif /* !CONFIG_USER_ONLY */

target_ulong helper_rdhwr_cpunum(CPUMIPSState *env)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 0)))
        return env->CP0_EBase & 0x3ff;
    else
        helper_raise_exception(env, EXCP_RI);

    return 0;
}

target_ulong helper_rdhwr_synci_step(CPUMIPSState *env)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 1)))
        return env->SYNCI_Step;
    else
        helper_raise_exception(env, EXCP_RI);

    return 0;
}

target_ulong helper_rdhwr_cc(CPUMIPSState *env)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 2)))
        return env->CP0_Count;
    else
        helper_raise_exception(env, EXCP_RI);

    return 0;
}

target_ulong helper_rdhwr_ccres(CPUMIPSState *env)
{
    if ((env->hflags & MIPS_HFLAG_CP0) ||
        (env->CP0_HWREna & (1 << 3)))
        return env->CCRes;
    else
        helper_raise_exception(env, EXCP_RI);

    return 0;
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
    helper_raise_exception(env, EXCP_HLT);
}

#if !defined(CONFIG_USER_ONLY)

void mips_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                  int is_write, int is_user, uintptr_t retaddr)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;

    env->CP0_BadVAddr = addr;
    do_raise_exception(env, (is_write == 1) ? EXCP_AdES : EXCP_AdEL, retaddr);
}

void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;

    ret = mips_cpu_handle_mmu_fault(cs, addr, is_write, mmu_idx);
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
        helper_raise_exception(env, EXCP_IBE);
    } else {
        helper_raise_exception(env, EXCP_DBE);
    }
}
#endif /* !CONFIG_USER_ONLY */

/* Complex FPU operations which may need stack space. */

#define FLOAT_TWO32 make_float32(1 << 30)
#define FLOAT_TWO64 make_float64(1ULL << 62)
#define FP_TO_INT32_OVERFLOW 0x7fffffff
#define FP_TO_INT64_OVERFLOW 0x7fffffffffffffffULL

/* convert MIPS rounding mode in FCR31 to IEEE library */
static unsigned int ieee_rm[] = {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
};

static inline void restore_rounding_mode(CPUMIPSState *env)
{
    set_float_rounding_mode(ieee_rm[env->active_fpu.fcr31 & 3],
                            &env->active_fpu.fp_status);
}

static inline void restore_flush_mode(CPUMIPSState *env)
{
    set_flush_to_zero((env->active_fpu.fcr31 & (1 << 24)) != 0,
                      &env->active_fpu.fp_status);
}

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
            helper_raise_exception(env, EXCP_RI);
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
            helper_raise_exception(env, EXCP_RI);
        }
        break;
    case 25:
        if (arg1 & 0xffffff00)
            return;
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
        if (arg1 & 0x007c0000)
            return;
        env->active_fpu.fcr31 = arg1;
        break;
    default:
        return;
    }
    /* set rounding mode */
    restore_rounding_mode(env);
    /* set flush-to-zero mode */
    restore_flush_mode(env);
    set_float_exception_flags(0, &env->active_fpu.fp_status);
    if ((GET_FP_ENABLE(env->active_fpu.fcr31) | 0x20) & GET_FP_CAUSE(env->active_fpu.fcr31))
        do_raise_exception(env, EXCP_FPE, GETPC());
}

static inline int ieee_ex_to_mips(int xcpt)
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

uint64_t helper_float_cvtl_d(CPUMIPSState *env, uint64_t fdt0)
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

uint64_t helper_float_cvtl_s(CPUMIPSState *env, uint32_t fst0)
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

uint32_t helper_float_cvtw_s(CPUMIPSState *env, uint32_t fst0)
{
    uint32_t wt2;

    wt2 = float32_to_int32(fst0, &env->active_fpu.fp_status);
    update_fcr31(env, GETPC());
    if (get_float_exception_flags(&env->active_fpu.fp_status)
        & (float_flag_invalid | float_flag_overflow)) {
        wt2 = FP_TO_INT32_OVERFLOW;
    }
    return wt2;
}

uint32_t helper_float_cvtw_d(CPUMIPSState *env, uint64_t fdt0)
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

uint64_t helper_float_roundl_d(CPUMIPSState *env, uint64_t fdt0)
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

uint64_t helper_float_roundl_s(CPUMIPSState *env, uint32_t fst0)
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

uint32_t helper_float_roundw_d(CPUMIPSState *env, uint64_t fdt0)
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

uint32_t helper_float_roundw_s(CPUMIPSState *env, uint32_t fst0)
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

uint64_t helper_float_truncl_d(CPUMIPSState *env, uint64_t fdt0)
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

uint64_t helper_float_truncl_s(CPUMIPSState *env, uint32_t fst0)
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

uint32_t helper_float_truncw_d(CPUMIPSState *env, uint64_t fdt0)
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

uint32_t helper_float_truncw_s(CPUMIPSState *env, uint32_t fst0)
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

uint64_t helper_float_ceill_d(CPUMIPSState *env, uint64_t fdt0)
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

uint64_t helper_float_ceill_s(CPUMIPSState *env, uint32_t fst0)
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

uint32_t helper_float_ceilw_d(CPUMIPSState *env, uint64_t fdt0)
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

uint32_t helper_float_ceilw_s(CPUMIPSState *env, uint32_t fst0)
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

uint64_t helper_float_floorl_d(CPUMIPSState *env, uint64_t fdt0)
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

uint64_t helper_float_floorl_s(CPUMIPSState *env, uint32_t fst0)
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

uint32_t helper_float_floorw_d(CPUMIPSState *env, uint64_t fdt0)
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

uint32_t helper_float_floorw_s(CPUMIPSState *env, uint32_t fst0)
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

#define FLOAT_OP(name, p) void helper_float_##name##_##p(CPUMIPSState *env)

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
