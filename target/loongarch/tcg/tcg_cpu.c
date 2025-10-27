/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch CPU parameters for QEMU.
 *
 * Copyright (c) 2025 Loongson Technology Corporation Limited
 */
#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/plugin.h"
#include "accel/accel-cpu-target.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/cpu-ops.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "tcg_loongarch.h"
#include "internals.h"

struct TypeExcp {
    int32_t exccode;
    const char * const name;
};

static const struct TypeExcp excp_names[] = {
    {EXCCODE_INT, "Interrupt"},
    {EXCCODE_PIL, "Page invalid exception for load"},
    {EXCCODE_PIS, "Page invalid exception for store"},
    {EXCCODE_PIF, "Page invalid exception for fetch"},
    {EXCCODE_PME, "Page modified exception"},
    {EXCCODE_PNR, "Page Not Readable exception"},
    {EXCCODE_PNX, "Page Not Executable exception"},
    {EXCCODE_PPI, "Page Privilege error"},
    {EXCCODE_ADEF, "Address error for instruction fetch"},
    {EXCCODE_ADEM, "Address error for Memory access"},
    {EXCCODE_SYS, "Syscall"},
    {EXCCODE_BRK, "Break"},
    {EXCCODE_INE, "Instruction Non-Existent"},
    {EXCCODE_IPE, "Instruction privilege error"},
    {EXCCODE_FPD, "Floating Point Disabled"},
    {EXCCODE_FPE, "Floating Point Exception"},
    {EXCCODE_DBP, "Debug breakpoint"},
    {EXCCODE_BCE, "Bound Check Exception"},
    {EXCCODE_SXD, "128 bit vector instructions Disable exception"},
    {EXCCODE_ASXD, "256 bit vector instructions Disable exception"},
    {EXCP_HLT, "EXCP_HLT"},
};

static const char *loongarch_exception_name(int32_t exception)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(excp_names); i++) {
        if (excp_names[i].exccode == exception) {
            return excp_names[i].name;
        }
    }
    return "Unknown";
}

void G_NORETURN do_raise_exception(CPULoongArchState *env,
                                   uint32_t exception,
                                   uintptr_t pc)
{
    CPUState *cs = env_cpu(env);

    qemu_log_mask(CPU_LOG_INT, "%s: exception: %d (%s)\n",
                  __func__,
                  exception,
                  loongarch_exception_name(exception));
    cs->exception_index = exception;

    cpu_loop_exit_restore(cs, pc);
}

#ifndef CONFIG_USER_ONLY
static void loongarch_cpu_do_interrupt(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    bool update_badinstr = 1;
    int cause = -1;
    bool tlbfill = FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR);
    uint32_t vec_size = FIELD_EX64(env->CSR_ECFG, CSR_ECFG, VS);
    uint64_t last_pc = env->pc;

    if (cs->exception_index != EXCCODE_INT) {
        qemu_log_mask(CPU_LOG_INT,
                     "%s enter: pc " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                     " TLBRERA " TARGET_FMT_lx " exception: %d (%s)\n",
                     __func__, env->pc, env->CSR_ERA, env->CSR_TLBRERA,
                     cs->exception_index,
                     loongarch_exception_name(cs->exception_index));
    }

    switch (cs->exception_index) {
    case EXCCODE_DBP:
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DCL, 1);
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, ECODE, 0xC);
        goto set_DERA;
    set_DERA:
        env->CSR_DERA = env->pc;
        env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DST, 1);
        set_pc(env, env->CSR_EENTRY + 0x480);
        break;
    case EXCCODE_INT:
        if (FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)) {
            env->CSR_DBG = FIELD_DP64(env->CSR_DBG, CSR_DBG, DEI, 1);
            goto set_DERA;
        }
        QEMU_FALLTHROUGH;
    case EXCCODE_PIF:
    case EXCCODE_ADEF:
        cause = cs->exception_index;
        update_badinstr = 0;
        break;
    case EXCCODE_SYS:
    case EXCCODE_BRK:
    case EXCCODE_INE:
    case EXCCODE_IPE:
    case EXCCODE_FPD:
    case EXCCODE_FPE:
    case EXCCODE_SXD:
    case EXCCODE_ASXD:
        env->CSR_BADV = env->pc;
        QEMU_FALLTHROUGH;
    case EXCCODE_BCE:
    case EXCCODE_ADEM:
    case EXCCODE_PIL:
    case EXCCODE_PIS:
    case EXCCODE_PME:
    case EXCCODE_PNR:
    case EXCCODE_PNX:
    case EXCCODE_PPI:
        cause = cs->exception_index;
        break;
    default:
        qemu_log("Error: exception(%d) has not been supported\n",
                 cs->exception_index);
        abort();
    }

    if (update_badinstr) {
        env->CSR_BADI = cpu_ldl_code(env, env->pc);
    }

    /* Save PLV and IE */
    if (tlbfill) {
        env->CSR_TLBRPRMD = FIELD_DP64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PPLV,
                                       FIELD_EX64(env->CSR_CRMD,
                                       CSR_CRMD, PLV));
        env->CSR_TLBRPRMD = FIELD_DP64(env->CSR_TLBRPRMD, CSR_TLBRPRMD, PIE,
                                       FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE));
        /* set the DA mode */
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 1);
        env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 0);
        env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA,
                                      PC, (env->pc >> 2));
    } else {
        env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ECODE,
                                    EXCODE_MCODE(cause));
        env->CSR_ESTAT = FIELD_DP64(env->CSR_ESTAT, CSR_ESTAT, ESUBCODE,
                                    EXCODE_SUBCODE(cause));
        env->CSR_PRMD = FIELD_DP64(env->CSR_PRMD, CSR_PRMD, PPLV,
                                   FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV));
        env->CSR_PRMD = FIELD_DP64(env->CSR_PRMD, CSR_PRMD, PIE,
                                   FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE));
        env->CSR_ERA = env->pc;
    }

    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, 0);

    if (vec_size) {
        vec_size = (1 << vec_size) * 4;
    }

    if  (cs->exception_index == EXCCODE_INT) {
        /* Interrupt */
        uint32_t vector = 0;
        uint32_t pending = FIELD_EX64(env->CSR_ESTAT, CSR_ESTAT, IS);
        pending &= FIELD_EX64(env->CSR_ECFG, CSR_ECFG, LIE);

        /* Find the highest-priority interrupt. */
        vector = 31 - clz32(pending);
        set_pc(env, env->CSR_EENTRY + \
               (EXCCODE_EXTERNAL_INT + vector) * vec_size);
        qemu_log_mask(CPU_LOG_INT,
                      "%s: PC " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                      " cause %d\n" "    A " TARGET_FMT_lx " D "
                      TARGET_FMT_lx " vector = %d ExC " TARGET_FMT_lx "ExS"
                      TARGET_FMT_lx "\n",
                      __func__, env->pc, env->CSR_ERA,
                      cause, env->CSR_BADV, env->CSR_DERA, vector,
                      env->CSR_ECFG, env->CSR_ESTAT);
        qemu_plugin_vcpu_interrupt_cb(cs, last_pc);
    } else {
        if (tlbfill) {
            set_pc(env, env->CSR_TLBRENTRY);
        } else {
            set_pc(env, env->CSR_EENTRY + EXCODE_MCODE(cause) * vec_size);
        }
        qemu_log_mask(CPU_LOG_INT,
                      "%s: PC " TARGET_FMT_lx " ERA " TARGET_FMT_lx
                      " cause %d%s\n, ESTAT " TARGET_FMT_lx
                      " EXCFG " TARGET_FMT_lx " BADVA " TARGET_FMT_lx
                      "BADI " TARGET_FMT_lx " SYS_NUM " TARGET_FMT_lu
                      " cpu %d asid " TARGET_FMT_lx "\n", __func__, env->pc,
                      tlbfill ? env->CSR_TLBRERA : env->CSR_ERA,
                      cause, tlbfill ? "(refill)" : "", env->CSR_ESTAT,
                      env->CSR_ECFG,
                      tlbfill ? env->CSR_TLBRBADV : env->CSR_BADV,
                      env->CSR_BADI, env->gpr[11], cs->cpu_index,
                      env->CSR_ASID);
        qemu_plugin_vcpu_exception_cb(cs, last_pc);
    }
    cs->exception_index = -1;
}

static void loongarch_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                                vaddr addr, unsigned size,
                                                MMUAccessType access_type,
                                                int mmu_idx, MemTxAttrs attrs,
                                                MemTxResult response,
                                                uintptr_t retaddr)
{
    CPULoongArchState *env = cpu_env(cs);

    if (access_type == MMU_INST_FETCH) {
        do_raise_exception(env, EXCCODE_ADEF, retaddr);
    } else {
        do_raise_exception(env, EXCCODE_ADEM, retaddr);
    }
}

static inline bool cpu_loongarch_hw_interrupts_enabled(CPULoongArchState *env)
{
    bool ret = 0;

    ret = (FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE) &&
          !(FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)));

    return ret;
}

static bool loongarch_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        CPULoongArchState *env = cpu_env(cs);

        if (cpu_loongarch_hw_interrupts_enabled(env) &&
            cpu_loongarch_hw_interrupts_pending(env)) {
            /* Raise it */
            cs->exception_index = EXCCODE_INT;
            loongarch_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}

static vaddr loongarch_pointer_wrap(CPUState *cs, int mmu_idx,
                                    vaddr result, vaddr base)
{
    return is_va32(cpu_env(cs)) ? (uint32_t)result : result;
}
#endif

static TCGTBCPUState loongarch_get_tb_cpu_state(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    uint32_t flags;

    flags = env->CSR_CRMD & (R_CSR_CRMD_PLV_MASK | R_CSR_CRMD_PG_MASK);
    flags |= FIELD_EX64(env->CSR_EUEN, CSR_EUEN, FPE) * HW_FLAGS_EUEN_FPE;
    flags |= FIELD_EX64(env->CSR_EUEN, CSR_EUEN, SXE) * HW_FLAGS_EUEN_SXE;
    flags |= FIELD_EX64(env->CSR_EUEN, CSR_EUEN, ASXE) * HW_FLAGS_EUEN_ASXE;
    flags |= is_va32(env) * HW_FLAGS_VA32;

    return (TCGTBCPUState){ .pc = env->pc, .flags = flags };
}

static void loongarch_cpu_synchronize_from_tb(CPUState *cs,
                                              const TranslationBlock *tb)
{
    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    set_pc(cpu_env(cs), tb->pc);
}

static void loongarch_restore_state_to_opc(CPUState *cs,
                                           const TranslationBlock *tb,
                                           const uint64_t *data)
{
    set_pc(cpu_env(cs), data[0]);
}

static int loongarch_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    CPULoongArchState *env = cpu_env(cs);

    if (FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PG)) {
        return FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV);
    }
    return MMU_DA_IDX;
}

const TCGCPUOps loongarch_tcg_ops = {
    .guest_default_memory_order = 0,
    .mttcg_supported = true,

    .initialize = loongarch_translate_init,
    .translate_code = loongarch_translate_code,
    .get_tb_cpu_state = loongarch_get_tb_cpu_state,
    .synchronize_from_tb = loongarch_cpu_synchronize_from_tb,
    .restore_state_to_opc = loongarch_restore_state_to_opc,
    .mmu_index = loongarch_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = loongarch_cpu_tlb_fill,
    .pointer_wrap = loongarch_pointer_wrap,
    .cpu_exec_interrupt = loongarch_cpu_exec_interrupt,
    .cpu_exec_halt = loongarch_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = loongarch_cpu_do_interrupt,
    .do_transaction_failed = loongarch_cpu_do_transaction_failed,
#endif
};
