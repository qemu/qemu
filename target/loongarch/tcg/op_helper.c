/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation helpers for QEMU.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "internals.h"
#include "qemu/crc32c.h"
#include <zlib.h> /* for crc32 */
#include "cpu-csr.h"

/* Exceptions helpers */
void helper_raise_exception(CPULoongArchState *env, uint32_t exception)
{
    do_raise_exception(env, exception, GETPC());
}

target_ulong helper_bitrev_w(target_ulong rj)
{
    return (int32_t)revbit32(rj);
}

target_ulong helper_bitrev_d(target_ulong rj)
{
    return revbit64(rj);
}

target_ulong helper_bitswap(target_ulong v)
{
    v = ((v >> 1) & (target_ulong)0x5555555555555555ULL) |
        ((v & (target_ulong)0x5555555555555555ULL) << 1);
    v = ((v >> 2) & (target_ulong)0x3333333333333333ULL) |
        ((v & (target_ulong)0x3333333333333333ULL) << 2);
    v = ((v >> 4) & (target_ulong)0x0F0F0F0F0F0F0F0FULL) |
        ((v & (target_ulong)0x0F0F0F0F0F0F0F0FULL) << 4);
    return v;
}

/* loongarch assert op */
void helper_asrtle_d(CPULoongArchState *env, target_ulong rj, target_ulong rk)
{
    if (rj > rk) {
        env->CSR_BADV = rj;
        do_raise_exception(env, EXCCODE_BCE, GETPC());
    }
}

void helper_asrtgt_d(CPULoongArchState *env, target_ulong rj, target_ulong rk)
{
    if (rj <= rk) {
        env->CSR_BADV = rj;
        do_raise_exception(env, EXCCODE_BCE, GETPC());
    }
}

target_ulong helper_crc32(target_ulong val, target_ulong m, uint64_t sz)
{
    uint8_t buf[8];
    target_ulong mask = ((sz * 8) == 64) ? -1ULL : ((1ULL << (sz * 8)) - 1);

    m &= mask;
    stq_le_p(buf, m);
    return (int32_t) (crc32(val ^ 0xffffffff, buf, sz) ^ 0xffffffff);
}

target_ulong helper_crc32c(target_ulong val, target_ulong m, uint64_t sz)
{
    uint8_t buf[8];
    target_ulong mask = ((sz * 8) == 64) ? -1ULL : ((1ULL << (sz * 8)) - 1);
    m &= mask;
    stq_le_p(buf, m);
    return (int32_t) (crc32c(val, buf, sz) ^ 0xffffffff);
}

target_ulong helper_cpucfg(CPULoongArchState *env, target_ulong rj)
{
    return rj >= ARRAY_SIZE(env->cpucfg) ? 0 : env->cpucfg[rj];
}

uint64_t helper_rdtime_d(CPULoongArchState *env)
{
#ifdef CONFIG_USER_ONLY
    return cpu_get_host_ticks();
#else
    uint64_t plv;
    LoongArchCPU *cpu = env_archcpu(env);

    plv = FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV);
    if (extract64(env->CSR_MISC, R_CSR_MISC_DRDTL_SHIFT + plv, 1)) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }

    return cpu_loongarch_get_constant_timer_counter(cpu);
#endif
}

#ifndef CONFIG_USER_ONLY
void helper_ertn(CPULoongArchState *env)
{
    uint64_t csr_pplv, csr_pie;
    if (FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR)) {
        csr_pplv = FIELD_EX64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PPLV);
        csr_pie = FIELD_EX64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PIE);

        env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR, 0);
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 0);
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 1);
        set_pc(env, env->CSR_TLBRERA);
        qemu_log_mask(CPU_LOG_INT, "%s: TLBRERA " TARGET_FMT_lx "\n",
                      __func__, env->CSR_TLBRERA);
    } else {
        csr_pplv = FIELD_EX64(env->CSR_PRMD, CSR_PRMD, PPLV);
        csr_pie = FIELD_EX64(env->CSR_PRMD, CSR_PRMD, PIE);

        set_pc(env, env->CSR_ERA);
        qemu_log_mask(CPU_LOG_INT, "%s: ERA " TARGET_FMT_lx "\n",
                      __func__, env->CSR_ERA);
    }
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, csr_pplv);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, csr_pie);

    env->lladdr = 1;
}

void helper_idle(CPULoongArchState *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    do_raise_exception(env, EXCP_HLT, 0);
}
#endif
