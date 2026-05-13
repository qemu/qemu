/*
 * QEMU ARM 'max' CPU
 *
 * Copyright (c) 2018 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "system/kvm.h"
#include "target/arm/internals.h"
#include "target/arm/cpregs.h"

void aarch64_aa32_a57_init(Object *obj, bool aa32_only)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;
    const bool aarch64_enabled = !aa32_only;

    cpu->dtb_compatible = "arm,cortex-a57";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
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

/* Share AArch32 -cpu max features with AArch64. */
void aa32_max_features(ARMCPU *cpu)
{
    uint32_t t;
    ARMISARegisters *isar = &cpu->isar;

    /* Add additional features supported by QEMU */
    t = GET_IDREG(isar, ID_ISAR5);
    t = FIELD_DP32(t, ID_ISAR5, AES, 2);          /* FEAT_PMULL */
    t = FIELD_DP32(t, ID_ISAR5, SHA1, 1);         /* FEAT_SHA1 */
    t = FIELD_DP32(t, ID_ISAR5, SHA2, 1);         /* FEAT_SHA256 */
    t = FIELD_DP32(t, ID_ISAR5, CRC32, 1);
    t = FIELD_DP32(t, ID_ISAR5, RDM, 1);          /* FEAT_RDM */
    t = FIELD_DP32(t, ID_ISAR5, VCMA, 1);         /* FEAT_FCMA */
    SET_IDREG(isar, ID_ISAR5, t);

    t = GET_IDREG(isar, ID_ISAR6);
    t = FIELD_DP32(t, ID_ISAR6, JSCVT, 1);        /* FEAT_JSCVT */
    t = FIELD_DP32(t, ID_ISAR6, DP, 1);           /* Feat_DotProd */
    t = FIELD_DP32(t, ID_ISAR6, FHM, 1);          /* FEAT_FHM */
    t = FIELD_DP32(t, ID_ISAR6, SB, 1);           /* FEAT_SB */
    t = FIELD_DP32(t, ID_ISAR6, SPECRES, 1);      /* FEAT_SPECRES */
    t = FIELD_DP32(t, ID_ISAR6, BF16, 1);         /* FEAT_AA32BF16 */
    t = FIELD_DP32(t, ID_ISAR6, I8MM, 1);         /* FEAT_AA32I8MM */
    SET_IDREG(isar, ID_ISAR6, t);

    t = cpu->isar.mvfr1;
    t = FIELD_DP32(t, MVFR1, FPHP, 3);            /* FEAT_FP16 */
    t = FIELD_DP32(t, MVFR1, SIMDHP, 2);          /* FEAT_FP16 */
    cpu->isar.mvfr1 = t;

    t = cpu->isar.mvfr2;
    t = FIELD_DP32(t, MVFR2, SIMDMISC, 3);        /* SIMD MaxNum */
    t = FIELD_DP32(t, MVFR2, FPMISC, 4);          /* FP MaxNum */
    cpu->isar.mvfr2 = t;

    FIELD_DP32_IDREG(isar, ID_MMFR3, PAN, 2);          /* FEAT_PAN2 */

    t = GET_IDREG(isar, ID_MMFR4);
    t = FIELD_DP32(t, ID_MMFR4, HPDS, 2);         /* FEAT_HPDS2 */
    t = FIELD_DP32(t, ID_MMFR4, AC2, 1);          /* ACTLR2, HACTLR2 */
    t = FIELD_DP32(t, ID_MMFR4, CNP, 1);          /* FEAT_TTCNP */
    t = FIELD_DP32(t, ID_MMFR4, XNX, 1);          /* FEAT_XNX */
    t = FIELD_DP32(t, ID_MMFR4, EVT, 2);          /* FEAT_EVT */
    SET_IDREG(isar, ID_MMFR4, t);

    FIELD_DP32_IDREG(isar, ID_MMFR5, ETS, 2);          /* FEAT_ETS2 */

    t = GET_IDREG(isar, ID_PFR0);
    t = FIELD_DP32(t, ID_PFR0, CSV2, 2);          /* FEAT_CSV2 */
    t = FIELD_DP32(t, ID_PFR0, DIT, 1);           /* FEAT_DIT */
    t = FIELD_DP32(t, ID_PFR0, RAS, 1);           /* FEAT_RAS */
    SET_IDREG(isar, ID_PFR0, t);

    t = GET_IDREG(isar, ID_PFR2);
    t = FIELD_DP32(t, ID_PFR2, CSV3, 1);          /* FEAT_CSV3 */
    t = FIELD_DP32(t, ID_PFR2, SSBS, 1);          /* FEAT_SSBS */
    SET_IDREG(isar, ID_PFR2, t);

    t = GET_IDREG(isar, ID_DFR0);
    t = FIELD_DP32(t, ID_DFR0, COPDBG, 10);       /* FEAT_Debugv8p8 */
    t = FIELD_DP32(t, ID_DFR0, COPSDBG, 10);      /* FEAT_Debugv8p8 */
    t = FIELD_DP32(t, ID_DFR0, PERFMON, 6);       /* FEAT_PMUv3p5 */
    SET_IDREG(isar, ID_DFR0, t);

    /* Debug ID registers. */

    /* Bit[15] is RES1, Bit[13] and Bits[11:0] are RES0. */
    t = 0x00008000;
    t = FIELD_DP32(t, DBGDIDR, SE_IMP, 1);
    t = FIELD_DP32(t, DBGDIDR, NSUHD_IMP, 1);
    t = FIELD_DP32(t, DBGDIDR, VERSION, 10);      /* FEAT_Debugv8p8 */
    t = FIELD_DP32(t, DBGDIDR, CTX_CMPS, 1);
    t = FIELD_DP32(t, DBGDIDR, BRPS, 5);
    t = FIELD_DP32(t, DBGDIDR, WRPS, 3);
    cpu->isar.dbgdidr = t;

    t = 0;
    t = FIELD_DP32(t, DBGDEVID, PCSAMPLE, 3);
    t = FIELD_DP32(t, DBGDEVID, WPADDRMASK, 1);
    t = FIELD_DP32(t, DBGDEVID, BPADDRMASK, 15);
    t = FIELD_DP32(t, DBGDEVID, VECTORCATCH, 0);
    t = FIELD_DP32(t, DBGDEVID, VIRTEXTNS, 1);
    t = FIELD_DP32(t, DBGDEVID, DOUBLELOCK, 1);
    t = FIELD_DP32(t, DBGDEVID, AUXREGS, 0);
    t = FIELD_DP32(t, DBGDEVID, CIDMASK, 0);
    cpu->isar.dbgdevid = t;

    /* Bits[31:4] are RES0. */
    t = 0;
    t = FIELD_DP32(t, DBGDEVID1, PCSROFFSET, 2);
    cpu->isar.dbgdevid1 = t;

    FIELD_DP32_IDREG(isar, ID_DFR1, HPMN0, 1);         /* FEAT_HPMN0 */
}
