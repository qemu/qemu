/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch CPU
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/qtest.h"
#include "system/tcg.h"
#include "system/kvm.h"
#include "kvm/kvm_loongarch.h"
#include "hw/qdev-properties.h"
#include "exec/exec-all.h"
#include "exec/translation-block.h"
#include "cpu.h"
#include "internals.h"
#include "fpu/softfloat-helpers.h"
#include "csr.h"
#ifndef CONFIG_USER_ONLY
#include "system/reset.h"
#endif
#include "vec.h"
#ifdef CONFIG_KVM
#include <linux/kvm.h>
#endif
#ifdef CONFIG_TCG
#include "accel/tcg/cpu-ldst.h"
#include "tcg/tcg.h"
#endif
#include "tcg/tcg_loongarch.h"

const char * const regnames[32] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};

const char * const fregnames[32] = {
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
    "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
};

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

const char *loongarch_exception_name(int32_t exception)
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

static void loongarch_cpu_set_pc(CPUState *cs, vaddr value)
{
    set_pc(cpu_env(cs), value);
}

static vaddr loongarch_cpu_get_pc(CPUState *cs)
{
    return cpu_env(cs)->pc;
}

#ifndef CONFIG_USER_ONLY
#include "hw/loongarch/virt.h"

void loongarch_cpu_set_irq(void *opaque, int irq, int level)
{
    LoongArchCPU *cpu = opaque;
    CPULoongArchState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    if (irq < 0 || irq >= N_IRQS) {
        return;
    }

    if (kvm_enabled()) {
        kvm_loongarch_set_interrupt(cpu, irq, level);
    } else if (tcg_enabled()) {
        env->CSR_ESTAT = deposit64(env->CSR_ESTAT, irq, 1, level != 0);
        if (FIELD_EX64(env->CSR_ESTAT, CSR_ESTAT, IS)) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

static inline bool cpu_loongarch_hw_interrupts_enabled(CPULoongArchState *env)
{
    bool ret = 0;

    ret = (FIELD_EX64(env->CSR_CRMD, CSR_CRMD, IE) &&
          !(FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)));

    return ret;
}

/* Check if there is pending and not masked out interrupt */
static inline bool cpu_loongarch_hw_interrupts_pending(CPULoongArchState *env)
{
    uint32_t pending;
    uint32_t status;

    pending = FIELD_EX64(env->CSR_ESTAT, CSR_ESTAT, IS);
    status  = FIELD_EX64(env->CSR_ECFG, CSR_ECFG, LIE);

    return (pending & status) != 0;
}
#endif

#ifdef CONFIG_TCG
#ifndef CONFIG_USER_ONLY
static void loongarch_cpu_do_interrupt(CPUState *cs)
{
    CPULoongArchState *env = cpu_env(cs);
    bool update_badinstr = 1;
    int cause = -1;
    bool tlbfill = FIELD_EX64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR);
    uint32_t vec_size = FIELD_EX64(env->CSR_ECFG, CSR_ECFG, VS);

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
#endif

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
#endif /* CONFIG_TCG */

#ifndef CONFIG_USER_ONLY
static bool loongarch_cpu_has_work(CPUState *cs)
{
    bool has_work = false;

    if ((cs->interrupt_request & CPU_INTERRUPT_HARD) &&
        cpu_loongarch_hw_interrupts_pending(cpu_env(cs))) {
        has_work = true;
    }

    return has_work;
}
#endif /* !CONFIG_USER_ONLY */

static int loongarch_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    CPULoongArchState *env = cpu_env(cs);

    if (FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PG)) {
        return FIELD_EX64(env->CSR_CRMD, CSR_CRMD, PLV);
    }
    return MMU_DA_IDX;
}

static void loongarch_la464_init_csr(Object *obj)
{
#ifndef CONFIG_USER_ONLY
    static bool initialized;
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);
    CPULoongArchState *env = &cpu->env;
    int i, num;

    if (!initialized) {
        initialized = true;
        num = FIELD_EX64(env->CSR_PRCFG1, CSR_PRCFG1, SAVE_NUM);
        for (i = num; i < 16; i++) {
            set_csr_flag(LOONGARCH_CSR_SAVE(i), CSRFL_UNUSED);
        }
        set_csr_flag(LOONGARCH_CSR_IMPCTL1, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_IMPCTL2, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_MERRCTL, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_MERRINFO1, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_MERRINFO2, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_MERRENTRY, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_MERRERA, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_MERRSAVE, CSRFL_UNUSED);
        set_csr_flag(LOONGARCH_CSR_CTAG, CSRFL_UNUSED);
    }
#endif
}

static void loongarch_la464_initfn(Object *obj)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);
    CPULoongArchState *env = &cpu->env;
    uint32_t data = 0, field;
    int i;

    for (i = 0; i < 21; i++) {
        env->cpucfg[i] = 0x0;
    }

    cpu->dtb_compatible = "loongarch,Loongson-3A5000";
    env->cpucfg[0] = 0x14c010;  /* PRID */

    data = FIELD_DP32(data, CPUCFG1, ARCH, 2);
    data = FIELD_DP32(data, CPUCFG1, PGMMU, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR, 1);
    if (kvm_enabled()) {
        /* GPA address width of VM is 47, field value is 47 - 1 */
        field = 0x2e;
    } else {
        field = 0x2f; /* 48 bit - 1 */
    }
    data = FIELD_DP32(data, CPUCFG1, PALEN, field);
    data = FIELD_DP32(data, CPUCFG1, VALEN, 0x2f);
    data = FIELD_DP32(data, CPUCFG1, UAL, 1);
    data = FIELD_DP32(data, CPUCFG1, RI, 1);
    data = FIELD_DP32(data, CPUCFG1, EP, 1);
    data = FIELD_DP32(data, CPUCFG1, RPLV, 1);
    data = FIELD_DP32(data, CPUCFG1, HP, 1);
    data = FIELD_DP32(data, CPUCFG1, CRC, 1);
    env->cpucfg[1] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG2, FP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_SP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_DP, 1);
    data = FIELD_DP32(data, CPUCFG2, FP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSX, 1),
    data = FIELD_DP32(data, CPUCFG2, LASX, 1),
    data = FIELD_DP32(data, CPUCFG2, LLFTP, 1);
    data = FIELD_DP32(data, CPUCFG2, LLFTP_VER, 1);
    data = FIELD_DP32(data, CPUCFG2, LSPW, 1);
    data = FIELD_DP32(data, CPUCFG2, LAM, 1);
    env->cpucfg[2] = data;

    env->cpucfg[4] = 100 * 1000 * 1000; /* Crystal frequency */

    data = 0;
    data = FIELD_DP32(data, CPUCFG5, CC_MUL, 1);
    data = FIELD_DP32(data, CPUCFG5, CC_DIV, 1);
    env->cpucfg[5] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG16, L1_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L1_DPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L2_IUPRIV, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUPRE, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUUNIFY, 1);
    data = FIELD_DP32(data, CPUCFG16, L3_IUINCL, 1);
    env->cpucfg[16] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG17, L1IU_WAYS, 3);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG17, L1IU_SIZE, 6);
    env->cpucfg[17] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG18, L1D_WAYS, 3);
    data = FIELD_DP32(data, CPUCFG18, L1D_SETS, 8);
    data = FIELD_DP32(data, CPUCFG18, L1D_SIZE, 6);
    env->cpucfg[18] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG19, L2IU_WAYS, 15);
    data = FIELD_DP32(data, CPUCFG19, L2IU_SETS, 8);
    data = FIELD_DP32(data, CPUCFG19, L2IU_SIZE, 6);
    env->cpucfg[19] = data;

    data = 0;
    data = FIELD_DP32(data, CPUCFG20, L3IU_WAYS, 15);
    data = FIELD_DP32(data, CPUCFG20, L3IU_SETS, 14);
    data = FIELD_DP32(data, CPUCFG20, L3IU_SIZE, 6);
    env->cpucfg[20] = data;

    env->CSR_ASID = FIELD_DP64(0, CSR_ASID, ASIDBITS, 0xa);

    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, SAVE_NUM, 8);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, TIMER_BITS, 0x2f);
    env->CSR_PRCFG1 = FIELD_DP64(env->CSR_PRCFG1, CSR_PRCFG1, VSMAX, 7);

    env->CSR_PRCFG2 = 0x3ffff000;

    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, TLB_TYPE, 2);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, MTLB_ENTRY, 63);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_WAYS, 7);
    env->CSR_PRCFG3 = FIELD_DP64(env->CSR_PRCFG3, CSR_PRCFG3, STLB_SETS, 8);

    loongarch_la464_init_csr(obj);
    loongarch_cpu_post_init(obj);
}

static void loongarch_la132_initfn(Object *obj)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);
    CPULoongArchState *env = &cpu->env;
    uint32_t data = 0;
    int i;

    for (i = 0; i < 21; i++) {
        env->cpucfg[i] = 0x0;
    }

    cpu->dtb_compatible = "loongarch,Loongson-1C103";
    env->cpucfg[0] = 0x148042;  /* PRID */

    data = FIELD_DP32(data, CPUCFG1, ARCH, 1); /* LA32 */
    data = FIELD_DP32(data, CPUCFG1, PGMMU, 1);
    data = FIELD_DP32(data, CPUCFG1, IOCSR, 1);
    data = FIELD_DP32(data, CPUCFG1, PALEN, 0x1f); /* 32 bits */
    data = FIELD_DP32(data, CPUCFG1, VALEN, 0x1f); /* 32 bits */
    data = FIELD_DP32(data, CPUCFG1, UAL, 1);
    data = FIELD_DP32(data, CPUCFG1, RI, 0);
    data = FIELD_DP32(data, CPUCFG1, EP, 0);
    data = FIELD_DP32(data, CPUCFG1, RPLV, 0);
    data = FIELD_DP32(data, CPUCFG1, HP, 1);
    data = FIELD_DP32(data, CPUCFG1, CRC, 1);
    env->cpucfg[1] = data;
}

static void loongarch_max_initfn(Object *obj)
{
    /* '-cpu max' for TCG: we use cpu la464. */
    loongarch_la464_initfn(obj);
}

static void loongarch_cpu_reset_hold(Object *obj, ResetType type)
{
    uint8_t tlb_ps;
    CPUState *cs = CPU(obj);
    LoongArchCPUClass *lacc = LOONGARCH_CPU_GET_CLASS(obj);
    CPULoongArchState *env = cpu_env(cs);

    if (lacc->parent_phases.hold) {
        lacc->parent_phases.hold(obj, type);
    }

#ifdef CONFIG_TCG
    env->fcsr0_mask = FCSR0_M1 | FCSR0_M2 | FCSR0_M3;
#endif
    env->fcsr0 = 0x0;

    int n;
    /* Set csr registers value after reset, see the manual 6.4. */
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PLV, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, IE, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DA, 1);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, PG, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATF, 0);
    env->CSR_CRMD = FIELD_DP64(env->CSR_CRMD, CSR_CRMD, DATM, 0);

    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, FPE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, SXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, ASXE, 0);
    env->CSR_EUEN = FIELD_DP64(env->CSR_EUEN, CSR_EUEN, BTE, 0);

    env->CSR_MISC = 0;

    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, VS, 0);
    env->CSR_ECFG = FIELD_DP64(env->CSR_ECFG, CSR_ECFG, LIE, 0);

    env->CSR_ESTAT = env->CSR_ESTAT & (~MAKE_64BIT_MASK(0, 2));
    env->CSR_RVACFG = FIELD_DP64(env->CSR_RVACFG, CSR_RVACFG, RBITS, 0);
    env->CSR_CPUID = cs->cpu_index;
    env->CSR_TCFG = FIELD_DP64(env->CSR_TCFG, CSR_TCFG, EN, 0);
    env->CSR_LLBCTL = FIELD_DP64(env->CSR_LLBCTL, CSR_LLBCTL, KLO, 0);
    env->CSR_TLBRERA = FIELD_DP64(env->CSR_TLBRERA, CSR_TLBRERA, ISTLBR, 0);
    env->CSR_MERRCTL = FIELD_DP64(env->CSR_MERRCTL, CSR_MERRCTL, ISMERR, 0);
    env->CSR_TID = cs->cpu_index;
    /*
     * Workaround for edk2-stable202408, CSR PGD register is set only if
     * its value is equal to zero for boot cpu, it causes reboot issue.
     *
     * Here clear CSR registers relative with TLB.
     */
    env->CSR_PGDH = 0;
    env->CSR_PGDL = 0;
    env->CSR_PWCH = 0;
    env->CSR_EENTRY = 0;
    env->CSR_TLBRENTRY = 0;
    env->CSR_MERRENTRY = 0;
    /* set CSR_PWCL.PTBASE and CSR_STLBPS.PS bits from CSR_PRCFG2 */
    if (env->CSR_PRCFG2 == 0) {
        env->CSR_PRCFG2 = 0x3fffff000;
    }
    tlb_ps = ctz32(env->CSR_PRCFG2);
    env->CSR_STLBPS = FIELD_DP64(env->CSR_STLBPS, CSR_STLBPS, PS, tlb_ps);
    env->CSR_PWCL = FIELD_DP64(env->CSR_PWCL, CSR_PWCL, PTBASE, tlb_ps);
    for (n = 0; n < 4; n++) {
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV0, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV1, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV2, 0);
        env->CSR_DMW[n] = FIELD_DP64(env->CSR_DMW[n], CSR_DMW, PLV3, 0);
    }

#ifndef CONFIG_USER_ONLY
    env->pc = 0x1c000000;
#ifdef CONFIG_TCG
    memset(env->tlb, 0, sizeof(env->tlb));
#endif
    if (kvm_enabled()) {
        kvm_arch_reset_vcpu(cs);
    }
#endif

#ifdef CONFIG_TCG
    restore_fp_status(env);
#endif
    cs->exception_index = -1;
}

static void loongarch_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    info->endian = BFD_ENDIAN_LITTLE;
    info->print_insn = print_insn_loongarch;
}

static void loongarch_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    LoongArchCPUClass *lacc = LOONGARCH_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    loongarch_cpu_register_gdb_regs_for_features(cs);

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    lacc->parent_realize(dev, errp);
}

static void loongarch_cpu_unrealizefn(DeviceState *dev)
{
    LoongArchCPUClass *lacc = LOONGARCH_CPU_GET_CLASS(dev);

#ifndef CONFIG_USER_ONLY
    cpu_remove_sync(CPU(dev));
#endif

    lacc->parent_unrealize(dev);
}

static bool loongarch_get_lsx(Object *obj, Error **errp)
{
    return LOONGARCH_CPU(obj)->lsx != ON_OFF_AUTO_OFF;
}

static void loongarch_set_lsx(Object *obj, bool value, Error **errp)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);
    uint32_t val;

    cpu->lsx = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
    if (cpu->lsx == ON_OFF_AUTO_OFF) {
        cpu->lasx = ON_OFF_AUTO_OFF;
        if (cpu->lasx == ON_OFF_AUTO_ON) {
            error_setg(errp, "Failed to disable LSX since LASX is enabled");
            return;
        }
    }

    if (kvm_enabled()) {
        /* kvm feature detection in function kvm_arch_init_vcpu */
        return;
    }

    /* LSX feature detection in TCG mode */
    val = cpu->env.cpucfg[2];
    if (cpu->lsx == ON_OFF_AUTO_ON) {
        if (FIELD_EX32(val, CPUCFG2, LSX) == 0) {
            error_setg(errp, "Failed to enable LSX in TCG mode");
            return;
        }
    } else {
        cpu->env.cpucfg[2] = FIELD_DP32(val, CPUCFG2, LASX, 0);
        val = cpu->env.cpucfg[2];
    }

    cpu->env.cpucfg[2] = FIELD_DP32(val, CPUCFG2, LSX, value);
}

static bool loongarch_get_lasx(Object *obj, Error **errp)
{
    return LOONGARCH_CPU(obj)->lasx != ON_OFF_AUTO_OFF;
}

static void loongarch_set_lasx(Object *obj, bool value, Error **errp)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);
    uint32_t val;

    cpu->lasx = value ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
    if ((cpu->lsx == ON_OFF_AUTO_OFF) && (cpu->lasx == ON_OFF_AUTO_ON)) {
        error_setg(errp, "Failed to enable LASX since lSX is disabled");
        return;
    }

    if (kvm_enabled()) {
        /* kvm feature detection in function kvm_arch_init_vcpu */
        return;
    }

    /* LASX feature detection in TCG mode */
    val = cpu->env.cpucfg[2];
    if (cpu->lasx == ON_OFF_AUTO_ON) {
        if (FIELD_EX32(val, CPUCFG2, LASX) == 0) {
            error_setg(errp, "Failed to enable LASX in TCG mode");
            return;
        }
    }

    cpu->env.cpucfg[2] = FIELD_DP32(val, CPUCFG2, LASX, value);
}

void loongarch_cpu_post_init(Object *obj)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);

    cpu->lbt = ON_OFF_AUTO_OFF;
    cpu->pmu = ON_OFF_AUTO_OFF;
    cpu->lsx = ON_OFF_AUTO_AUTO;
    cpu->lasx = ON_OFF_AUTO_AUTO;
    object_property_add_bool(obj, "lsx", loongarch_get_lsx,
                             loongarch_set_lsx);
    object_property_add_bool(obj, "lasx", loongarch_get_lasx,
                             loongarch_set_lasx);
    /* lbt is enabled only in kvm mode, not supported in tcg mode */
    if (kvm_enabled()) {
        kvm_loongarch_cpu_post_init(cpu);
    }
}

static void loongarch_cpu_init(Object *obj)
{
#ifndef CONFIG_USER_ONLY
    LoongArchCPU *cpu = LOONGARCH_CPU(obj);

    qdev_init_gpio_in(DEVICE(cpu), loongarch_cpu_set_irq, N_IRQS);
#ifdef CONFIG_TCG
    timer_init_ns(&cpu->timer, QEMU_CLOCK_VIRTUAL,
                  &loongarch_constant_timer_cb, cpu);
#endif
#endif
}

static ObjectClass *loongarch_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;

    oc = object_class_by_name(cpu_model);
    if (!oc) {
        g_autofree char *typename
            = g_strdup_printf(LOONGARCH_CPU_TYPE_NAME("%s"), cpu_model);
        oc = object_class_by_name(typename);
    }

    return oc;
}

static void loongarch_cpu_dump_csr(CPUState *cs, FILE *f)
{
#ifndef CONFIG_USER_ONLY
    CPULoongArchState *env = cpu_env(cs);
    CSRInfo *csr_info;
    int64_t *addr;
    int i, j, len, col = 0;

    qemu_fprintf(f, "\n");

    /* Dump all generic CSR register */
    for (i = 0; i < LOONGARCH_CSR_DBG; i++) {
        csr_info = get_csr(i);
        if (!csr_info || (csr_info->flags & CSRFL_UNUSED)) {
            if (i == (col + 3)) {
                qemu_fprintf(f, "\n");
            }

            continue;
        }

        if ((i >  (col + 3)) || (i == col)) {
            col = i & ~3;
            qemu_fprintf(f, " CSR%03d:", col);
        }

        addr = (void *)env + csr_info->offset;
        qemu_fprintf(f, " %s ", csr_info->name);
        len = strlen(csr_info->name);
        for (; len < 6; len++) {
            qemu_fprintf(f, " ");
        }

        qemu_fprintf(f, "%" PRIx64, *addr);
        j = find_last_bit((void *)addr, BITS_PER_LONG) & (BITS_PER_LONG - 1);
        len += j / 4 + 1;
        for (; len < 22; len++) {
                qemu_fprintf(f, " ");
        }

        if (i == (col + 3)) {
            qemu_fprintf(f, "\n");
        }
    }
    qemu_fprintf(f, "\n");
#endif
}

static void loongarch_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPULoongArchState *env = cpu_env(cs);
    int i;

    qemu_fprintf(f, " PC=%016" PRIx64 " ", env->pc);
    qemu_fprintf(f, " FCSR0 0x%08x\n", env->fcsr0);

    /* gpr */
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0) {
            qemu_fprintf(f, " GPR%02d:", i);
        }
        qemu_fprintf(f, " %s %016" PRIx64, regnames[i], env->gpr[i]);
        if ((i & 3) == 3) {
            qemu_fprintf(f, "\n");
        }
    }

    /* csr */
    loongarch_cpu_dump_csr(cs, f);

    /* fpr */
    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < 32; i++) {
            qemu_fprintf(f, " %s %016" PRIx64, fregnames[i], env->fpr[i].vreg.D(0));
            if ((i & 3) == 3) {
                qemu_fprintf(f, "\n");
            }
        }
    }
}

#ifdef CONFIG_TCG
#include "accel/tcg/cpu-ops.h"

static const TCGCPUOps loongarch_tcg_ops = {
    .guest_default_memory_order = 0,
    .mttcg_supported = true,

    .initialize = loongarch_translate_init,
    .translate_code = loongarch_translate_code,
    .synchronize_from_tb = loongarch_cpu_synchronize_from_tb,
    .restore_state_to_opc = loongarch_restore_state_to_opc,
    .mmu_index = loongarch_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = loongarch_cpu_tlb_fill,
    .cpu_exec_interrupt = loongarch_cpu_exec_interrupt,
    .cpu_exec_halt = loongarch_cpu_has_work,
    .do_interrupt = loongarch_cpu_do_interrupt,
    .do_transaction_failed = loongarch_cpu_do_transaction_failed,
#endif
};
#endif /* CONFIG_TCG */

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps loongarch_sysemu_ops = {
    .has_work = loongarch_cpu_has_work,
    .write_elf64_note = loongarch_cpu_write_elf64_note,
    .get_phys_page_debug = loongarch_cpu_get_phys_page_debug,
};

static int64_t loongarch_cpu_get_arch_id(CPUState *cs)
{
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);

    return cpu->phy_id;
}
#endif

static const Property loongarch_cpu_properties[] = {
    DEFINE_PROP_INT32("socket-id", LoongArchCPU, socket_id, 0),
    DEFINE_PROP_INT32("core-id", LoongArchCPU, core_id, 0),
    DEFINE_PROP_INT32("thread-id", LoongArchCPU, thread_id, 0),
    DEFINE_PROP_INT32("node-id", LoongArchCPU, node_id, CPU_UNSET_NUMA_NODE_ID),
};

static void loongarch_cpu_class_init(ObjectClass *c, void *data)
{
    LoongArchCPUClass *lacc = LOONGARCH_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_props(dc, loongarch_cpu_properties);
    device_class_set_parent_realize(dc, loongarch_cpu_realizefn,
                                    &lacc->parent_realize);
    device_class_set_parent_unrealize(dc, loongarch_cpu_unrealizefn,
                                      &lacc->parent_unrealize);
    resettable_class_set_parent_phases(rc, NULL, loongarch_cpu_reset_hold, NULL,
                                       &lacc->parent_phases);

    cc->class_by_name = loongarch_cpu_class_by_name;
    cc->dump_state = loongarch_cpu_dump_state;
    cc->set_pc = loongarch_cpu_set_pc;
    cc->get_pc = loongarch_cpu_get_pc;
#ifndef CONFIG_USER_ONLY
    cc->get_arch_id = loongarch_cpu_get_arch_id;
    dc->vmsd = &vmstate_loongarch_cpu;
    cc->sysemu_ops = &loongarch_sysemu_ops;
#endif
    cc->disas_set_info = loongarch_cpu_disas_set_info;
    cc->gdb_read_register = loongarch_cpu_gdb_read_register;
    cc->gdb_write_register = loongarch_cpu_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;

#ifdef CONFIG_TCG
    cc->tcg_ops = &loongarch_tcg_ops;
#endif
    dc->user_creatable = true;
}

static const gchar *loongarch32_gdb_arch_name(CPUState *cs)
{
    return "loongarch32";
}

static void loongarch32_cpu_class_init(ObjectClass *c, void *data)
{
    CPUClass *cc = CPU_CLASS(c);

    cc->gdb_core_xml_file = "loongarch-base32.xml";
    cc->gdb_arch_name = loongarch32_gdb_arch_name;
}

static const gchar *loongarch64_gdb_arch_name(CPUState *cs)
{
    return "loongarch64";
}

static void loongarch64_cpu_class_init(ObjectClass *c, void *data)
{
    CPUClass *cc = CPU_CLASS(c);

    cc->gdb_core_xml_file = "loongarch-base64.xml";
    cc->gdb_arch_name = loongarch64_gdb_arch_name;
}

#define DEFINE_LOONGARCH_CPU_TYPE(size, model, initfn) \
    { \
        .parent = TYPE_LOONGARCH##size##_CPU, \
        .instance_init = initfn, \
        .name = LOONGARCH_CPU_TYPE_NAME(model), \
    }

static const TypeInfo loongarch_cpu_type_infos[] = {
    {
        .name = TYPE_LOONGARCH_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(LoongArchCPU),
        .instance_align = __alignof(LoongArchCPU),
        .instance_init = loongarch_cpu_init,

        .abstract = true,
        .class_size = sizeof(LoongArchCPUClass),
        .class_init = loongarch_cpu_class_init,
    },
    {
        .name = TYPE_LOONGARCH32_CPU,
        .parent = TYPE_LOONGARCH_CPU,

        .abstract = true,
        .class_init = loongarch32_cpu_class_init,
    },
    {
        .name = TYPE_LOONGARCH64_CPU,
        .parent = TYPE_LOONGARCH_CPU,

        .abstract = true,
        .class_init = loongarch64_cpu_class_init,
    },
    DEFINE_LOONGARCH_CPU_TYPE(64, "la464", loongarch_la464_initfn),
    DEFINE_LOONGARCH_CPU_TYPE(32, "la132", loongarch_la132_initfn),
    DEFINE_LOONGARCH_CPU_TYPE(64, "max", loongarch_max_initfn),
};

DEFINE_TYPES(loongarch_cpu_type_infos)
