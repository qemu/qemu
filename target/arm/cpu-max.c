/*
 * QEMU ARM 'max' CPU
 *
 * Copyright (c) 2018 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/target-info.h"
#include "system/hw_accel.h"
#include "system/kvm.h"
#include "system/qtest.h"
#include "system/tcg.h"
#include "target/arm/internals.h"
#include "target/arm/cpregs.h"

void aarch64_aa32_a57_init(ARMCPU *cpu, bool aarch64_enabled)
{
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a57";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_NEON_TRAPS);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    if (aarch64_enabled) {
        set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    }
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    if (kvm_enabled()) {
        cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A57;
    }
    cpu->midr = 0x411fd070;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    SET_IDREG(isar, ID_PFR0, 0x00000131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x03010066);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_MMFR0, 0x10101105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x00011121);
    SET_IDREG(isar, ID_ISAR6, 0);
    if (aarch64_enabled) {
        SET_IDREG(isar, ID_AA64PFR0, 0x00002222);
        SET_IDREG(isar, ID_AA64DFR0, 0x10305106);
        SET_IDREG(isar, ID_AA64ISAR0, 0x00011120);
        SET_IDREG(isar, ID_AA64MMFR0, 0x00001124);
    }
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->isar.dbgdevid = 0x01110f13;
    cpu->isar.dbgdevid1 = 0x2;
    cpu->isar.reset_pmcr_el0 = 0x41013000;
    SET_IDREG(isar, CLIDR, 0x0a200023);
    /* 32KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 7);
    /* 48KB L1 icache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 3, 64, 48 * KiB, 2);
    /* 2048KB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 64, 2 * MiB, 7);
    if (aarch64_enabled) {
        set_dczid_bs(cpu, 4); /* 64 bytes */
        cpu->gic_num_lrs = 4;
        cpu->gic_vpribits = 5;
        cpu->gic_vprebits = 5;
        cpu->gic_pribits = 5;
    }
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

/*
 * -cpu max: a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-aarch64 is defined in cpu64.c;
 * this only needs to handle 32 bits, and need not care about KVM.
 */
static void cpu_max_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    const bool aarch64_enabled = target_aarch64();

    if (hwaccel_enabled()) {
        assert(aarch64_enabled);
        /*
         * When hardware acceleration enabled, '-cpu max' is
         * identical to '-cpu host'
         */
        aarch64_host_initfn(obj);
        return;
    }

    if (tcg_enabled()) {
        if (!aarch64_enabled) {
            aarch32_max_v8_tcg_initfn(obj);
        } else {
            aarch64_max_v9_tcg_initfn(obj);
        }
        return;
    }

    /* Ultimate fallback: -cpu max as cortex-a57. */
    assert(qtest_enabled());
    aarch64_aa32_a57_init(cpu, aarch64_enabled);
}

static const ARMCPUInfo arm_max_cpu = {
    .name = "max",
    .initfn = cpu_max_initfn,
};

static void arm_max_cpu_register_types(void)
{
    arm_cpu_register(&arm_max_cpu);
}

type_init(arm_max_cpu_register_types)
