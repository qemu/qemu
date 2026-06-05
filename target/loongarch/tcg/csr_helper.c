/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation helpers for CSRs
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-ldst.h"
#include "hw/core/irq.h"
#include "cpu-csr.h"
#include "cpu-mmu.h"

target_ulong helper_csrwr_stlbps(CPULoongArchState *env, target_ulong val)
{
    CPUSysState *sys = env_sys(env);
    int64_t old_v = sys->CSR_STLBPS;

    /*
     * The real hardware only supports the min tlb_ps is 12
     * tlb_ps=0 may cause undefined-behavior.
     */
    uint8_t tlb_ps = FIELD_EX64(val, CSR_STLBPS, PS);
    if (!check_ps(env, tlb_ps)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set ps %d\n", tlb_ps);
    } else {
        /* Only update PS field, reserved bit keeps zero */
        val = FIELD_DP64(val, CSR_STLBPS, RESERVE, 0);
        sys->CSR_STLBPS = val;
    }

    return old_v;
}

target_ulong helper_csrrd_pgd(CPULoongArchState *env)
{
    int64_t v;
    CPUSysState *sys = env_sys(env);

    if (sys->CSR_TLBRERA & 0x1) {
        v = sys->CSR_TLBRBADV;
    } else {
        v = sys->CSR_BADV;
    }

    if ((v >> 63) & 0x1) {
        v = sys->CSR_PGDH;
    } else {
        v = sys->CSR_PGDL;
    }

    return v;
}

target_ulong helper_csrrd_cpuid(CPULoongArchState *env)
{
    LoongArchCPU *lac = env_archcpu(env);
    CPUSysState *sys = env_sys(env);

    sys->CSR_CPUID = CPU(lac)->cpu_index;

    return sys->CSR_CPUID;
}

target_ulong helper_csrrd_tval(CPULoongArchState *env)
{
    LoongArchCPU *cpu = env_archcpu(env);

    return cpu_loongarch_get_constant_timer_ticks(cpu);
}

target_ulong helper_csrrd_msgir(CPULoongArchState *env)
{
    int irq, new;
    CPUSysState *sys = env_sys(env);

    irq = find_first_bit((unsigned long *)sys->CSR_MSGIS, 256);
    if (irq < 256) {
        clear_bit(irq, (unsigned long *)sys->CSR_MSGIS);
        new = find_first_bit((unsigned long *)sys->CSR_MSGIS, 256);
        if (new < 256) {
            return irq;
        }

        sys->CSR_ESTAT = FIELD_DP64(sys->CSR_ESTAT, CSR_ESTAT, MSGINT, 0);
    } else {
        /* bit 31 set 1 for no invalid irq */
        irq = BIT(31);
    }

    return irq;
}

target_ulong helper_csrwr_estat(CPULoongArchState *env, target_ulong val)
{
    CPUSysState *sys = env_sys(env);
    int64_t old_v = sys->CSR_ESTAT;

    /* Only IS[1:0] can be written */
    sys->CSR_ESTAT = deposit64(sys->CSR_ESTAT, 0, 2, val);

    return old_v;
}

target_ulong helper_csrwr_asid(CPULoongArchState *env, target_ulong val)
{
    CPUSysState *sys = env_sys(env);
    int64_t old_v = sys->CSR_ASID;

    /* Only ASID filed of CSR_ASID can be written */
    sys->CSR_ASID = deposit64(sys->CSR_ASID, 0, 10, val);
    if (old_v != sys->CSR_ASID) {
        tlb_flush(env_cpu(env));
    }
    return old_v;
}

target_ulong helper_csrwr_tcfg(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = env_archcpu(env);
    CPUSysState *sys = env_sys(env);
    int64_t old_v = sys->CSR_TCFG;

    cpu_loongarch_store_constant_timer_config(cpu, val);

    return old_v;
}

target_ulong helper_csrwr_ticlr(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = env_archcpu(env);
    int64_t old_v = 0;

    if (val & 0x1) {
        bql_lock();
        loongarch_cpu_set_irq(cpu, IRQ_TIMER, 0);
        bql_unlock();
    }
    return old_v;
}

target_ulong helper_csrwr_pwcl(CPULoongArchState *env, target_ulong val)
{
    uint8_t shift, ptbase;
    CPUSysState *sys = env_sys(env);
    int64_t old_v = sys->CSR_PWCL;

    /*
     * The real hardware only supports 64bit PTE width now, 128bit or others
     * treated as illegal.
     */
    shift = FIELD_EX64(val, CSR_PWCL, PTEWIDTH);
    ptbase = FIELD_EX64(val, CSR_PWCL, PTBASE);
    if (shift) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set pte width with %d bit\n", 64 << shift);
        val = FIELD_DP64(val, CSR_PWCL, PTEWIDTH, 0);
    }
    if (!check_ps(env, ptbase)) {
         qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempted set ptbase 2^%d\n", ptbase);
    }
    sys->CSR_PWCL = val;
    return old_v;
}

target_ulong helper_csrwr_pwch(CPULoongArchState *env, target_ulong val)
{
    uint8_t has_ptw;
    CPUSysState *sys = env_sys(env);
    int64_t old_v = sys->CSR_PWCH;

    val = FIELD_DP64(val, CSR_PWCH, RESERVE, 0);
    has_ptw = FIELD_EX32(env->cpucfg[2], CPUCFG2, HPTW);
    if (!has_ptw) {
        val = FIELD_DP64(val, CSR_PWCH, HPTW_EN, 0);
    }

    sys->CSR_PWCH = val;
    return old_v;
 }
