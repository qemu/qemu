/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation helpers for CSRs
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "hw/irq.h"
#include "cpu-csr.h"
#include "tcg/tcg-ldst.h"

target_ulong helper_csrrd_pgd(CPULoongArchState *env)
{
    int64_t v;

    if (env->CSR_TLBRERA & 0x1) {
        v = env->CSR_TLBRBADV;
    } else {
        v = env->CSR_BADV;
    }

    if ((v >> 63) & 0x1) {
        v = env->CSR_PGDH;
    } else {
        v = env->CSR_PGDL;
    }

    return v;
}

target_ulong helper_csrrd_cpuid(CPULoongArchState *env)
{
    LoongArchCPU *lac = env_archcpu(env);

    env->CSR_CPUID = CPU(lac)->cpu_index;

    return env->CSR_CPUID;
}

target_ulong helper_csrrd_tval(CPULoongArchState *env)
{
    LoongArchCPU *cpu = env_archcpu(env);

    return cpu_loongarch_get_constant_timer_ticks(cpu);
}

target_ulong helper_csrwr_estat(CPULoongArchState *env, target_ulong val)
{
    int64_t old_v = env->CSR_ESTAT;

    /* Only IS[1:0] can be written */
    env->CSR_ESTAT = deposit64(env->CSR_ESTAT, 0, 2, val);

    return old_v;
}

target_ulong helper_csrwr_asid(CPULoongArchState *env, target_ulong val)
{
    int64_t old_v = env->CSR_ASID;

    /* Only ASID filed of CSR_ASID can be written */
    env->CSR_ASID = deposit64(env->CSR_ASID, 0, 10, val);
    if (old_v != env->CSR_ASID) {
        tlb_flush(env_cpu(env));
    }
    return old_v;
}

target_ulong helper_csrwr_tcfg(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = env_archcpu(env);
    int64_t old_v = env->CSR_TCFG;

    cpu_loongarch_store_constant_timer_config(cpu, val);

    return old_v;
}

target_ulong helper_csrwr_ticlr(CPULoongArchState *env, target_ulong val)
{
    LoongArchCPU *cpu = env_archcpu(env);
    int64_t old_v = 0;

    if (val & 0x1) {
        qemu_mutex_lock_iothread();
        loongarch_cpu_set_irq(cpu, IRQ_TIMER, 0);
        qemu_mutex_unlock_iothread();
    }
    return old_v;
}
