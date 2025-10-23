/*
 * QEMU AArch64 TCG CPUs
 *
 * Copyright (c) 2013 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/module.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "qemu/units.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"

static void aarch64_a35_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a35";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* From B2.2 AArch64 identification registers. */
    cpu->midr = 0x411fd040;
    cpu->revidr = 0;
    cpu->ctr = 0x84448004;
    SET_IDREG(isar, ID_PFR0, 0x00000131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x03010066);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x00011121);
    SET_IDREG(isar, ID_AA64PFR0, 0x00002222);
    SET_IDREG(isar, ID_AA64PFR1, 0);
    SET_IDREG(isar, ID_AA64DFR0, 0x10305106);
    SET_IDREG(isar, ID_AA64DFR1, 0);
    SET_IDREG(isar, ID_AA64ISAR0, 0x00011120);
    SET_IDREG(isar, ID_AA64ISAR1, 0);
    SET_IDREG(isar, ID_AA64MMFR0, 0x00101122);
    SET_IDREG(isar, ID_AA64MMFR1, 0);
    SET_IDREG(isar, CLIDR, 0x0a200023);
    cpu->dcz_blocksize = 4;

    /* From B2.4 AArch64 Virtual Memory control registers */
    cpu->reset_sctlr = 0x00c50838;

    /* From B2.10 AArch64 performance monitor registers */
    cpu->isar.reset_pmcr_el0 = 0x410a3000;

    /* From B2.29 Cache ID registers */
    /* 32KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 7);
    /* 32KB L1 icache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 2);
    /* 512KB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 64, 512 * KiB, 7);

    /* From B3.5 VGIC Type register */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* From C6.4 Debug ID Register */
    cpu->isar.dbgdidr = 0x3516d000;
    /* From C6.5 Debug Device ID Register */
    cpu->isar.dbgdevid = 0x00110f13;
    /* From C6.6 Debug Device ID Register 1 */
    cpu->isar.dbgdevid1 = 0x2;

    /* From Cortex-A35 SIMD and Floating-point Support r1p0 */
    /* From 3.2 AArch32 register summary */
    cpu->reset_fpsid = 0x41034043;

    /* From 2.2 AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;

    /* These values are the same with A53/A57/A72. */
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

static void cpu_max_get_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t value;

    /* All vector lengths are disabled when SVE is off. */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        value = 0;
    } else {
        value = cpu->sve_max_vq;
    }
    visit_type_uint32(v, name, &value, errp);
}

static void cpu_max_set_sve_max_vq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t max_vq;

    if (!visit_type_uint32(v, name, &max_vq, errp)) {
        return;
    }

    if (max_vq == 0 || max_vq > ARM_MAX_VQ) {
        error_setg(errp, "unsupported SVE vector length");
        error_append_hint(errp, "Valid sve-max-vq in range [1-%d]\n",
                          ARM_MAX_VQ);
        return;
    }

    cpu->sve_max_vq = max_vq;
}

static bool cpu_arm_get_rme(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    return cpu_isar_feature(aa64_rme, cpu);
}

static void cpu_arm_set_rme(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    /* Enable FEAT_RME_GPC2 */
    FIELD_DP64_IDREG(&cpu->isar, ID_AA64PFR0, RME, value ? 2 : 0);
}

static void cpu_max_set_l0gptsz(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }

    /* Encode the value for the GPCCR_EL3 field. */
    switch (value) {
    case 30:
    case 34:
    case 36:
    case 39:
        cpu->reset_l0gptsz = value - 30;
        break;
    default:
        error_setg(errp, "invalid value for l0gptsz");
        error_append_hint(errp, "valid values are 30, 34, 36, 39\n");
        break;
    }
}

static void cpu_max_get_l0gptsz(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t value = cpu->reset_l0gptsz + 30;

    visit_type_uint32(v, name, &value, errp);
}

static const Property arm_cpu_lpa2_property =
    DEFINE_PROP_BOOL("lpa2", ARMCPU, prop_lpa2, true);

static void aarch64_a55_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a55";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by B2.4 AArch64 registers by functional group */
    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x84448004; /* L1Ip = VIPT */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000010305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000100001ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101122ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x0000000010112222ull);
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000010ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x412FD050;          /* r2p0 */
    cpu->revidr = 0;

    /* From B2.23 CCSIDR_EL1 */
    /* 32KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 7);
    /* 32KB L1 icache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 2);
    /* 512KB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 64, 512 * KiB, 7);

    /* From B2.96 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From B4.45 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From D5.4 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x410b3000;
}

static void aarch64_a72_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a72";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fd083;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034080;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    SET_IDREG(isar, ID_PFR0, 0x00000131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x03010066);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00011142);
    SET_IDREG(isar, ID_ISAR5, 0x00011121);
    SET_IDREG(isar, ID_AA64PFR0, 0x00002222);
    SET_IDREG(isar, ID_AA64DFR0, 0x10305106);
    SET_IDREG(isar, ID_AA64ISAR0, 0x00011120);
    SET_IDREG(isar, ID_AA64MMFR0, 0x00001124);
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->isar.dbgdevid = 0x01110f13;
    cpu->isar.dbgdevid1 = 0x2;
    cpu->isar.reset_pmcr_el0 = 0x41023000;
    SET_IDREG(isar, CLIDR, 0x0a200023);
    /* 32KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 32 * KiB, 7);
    /* 48KB L1 dcache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 3, 64, 48 * KiB, 2);
    /* 1MB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 64, 1 * MiB, 7);
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

static void aarch64_a76_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a76";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by B2.4 AArch64 registers by functional group */
    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x8444C004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000010305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000100001ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101122ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1100000010111112ull); /* GIC filled in later */
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000010ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00010000); /* GIC filled in later */
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x414fd0b1;          /* r4p1 */
    cpu->revidr = 0;

    /* From B2.18 CCSIDR_EL1 */
    /* 64KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 7);
    /* 64KB L1 icache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 2);
    /* 512KB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 8, 64, 512 * KiB, 7);

    /* From B2.93 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From B4.23 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* From B5.1 AdvSIMD AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From D5.1 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x410b3000;
}

static void aarch64_a78ae_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a78ae";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by 3.2.4 AArch64 registers by functional group */
    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x9444c004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000110305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0010100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000001200031ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000100001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1100000010111112ull); /* GIC filled in later */
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000010ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00010000); /* GIC filled in later */
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x410fd423;          /* r0p3 */
    cpu->revidr = 0;

    /* From 3.2.33 CCSIDR_EL1 */
    /* 64KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 7);
    /* 64KB L1 icache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 2);
    /* 512KB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 8, 64, 512 * KiB, 7);

    /* From 3.2.118 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From 3.4.23 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    /* From 3.4.8 ICC_CTLR_EL3 */
    cpu->gic_pribits = 5;

    /* From 3.5.1 AdvSIMD AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From 5.5.1 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x41223000;
}

static void aarch64_a64fx_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,a64fx";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x461f0010;
    cpu->revidr = 0x00000000;
    cpu->ctr = 0x86668006;
    cpu->reset_sctlr = 0x30000180;
    SET_IDREG(isar, ID_AA64PFR0, 0x0000000101111111); /* No RAS Extensions */
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000010305408);
    SET_IDREG(isar, ID_AA64DFR1, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64AFR0, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64AFR1, 0x0000000000000000);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000001122);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000011212100);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000000010211120);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000010001);
    SET_IDREG(isar, ID_AA64ZFR0, 0x0000000000000000);
    SET_IDREG(isar, CLIDR, 0x0000000080000023);
    /* 64KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 256, 64 * KiB, 7);
    /* 64KB L1 icache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 256, 64 * KiB, 2);
    /* 8MB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 16, 256, 8 * MiB, 7);
    cpu->dcz_blocksize = 6; /* 256 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* The A64FX supports only 128, 256 and 512 bit vector lengths */
    aarch64_add_sve_properties(obj);
    cpu->sve_vq.supported = (1 << 0)  /* 128bit */
                          | (1 << 1)  /* 256bit */
                          | (1 << 3); /* 512bit */

    cpu->isar.reset_pmcr_el0 = 0x46014040;

    /* TODO:  Add A64FX specific HPC extension registers */
}

static CPAccessResult access_actlr_w(CPUARMState *env, const ARMCPRegInfo *r,
                                     bool read)
{
    if (!read) {
        int el = arm_current_el(env);

        /* Because ACTLR_EL2 is constant 0, writes below EL2 trap to EL2. */
        if (el < 2 && arm_is_el2_enabled(env)) {
            return CP_ACCESS_TRAP_EL2;
        }
        /* Because ACTLR_EL3 is constant 0, writes below EL3 trap to EL3. */
        if (el < 3 && arm_feature(env, ARM_FEATURE_EL3)) {
            return CP_ACCESS_TRAP_EL3;
        }
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo neoverse_n1_cp_reginfo[] = {
    { .name = "ATCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      /* Traps and enables are the same as for TCR_EL1. */
      .accessfn = access_tvm_trvm, .fgt = FGT_TCR_EL1, },
    { .name = "ATCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ATCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ATCR_EL12", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AVTCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR3_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    /*
     * Report CPUCFR_EL1.SCU as 1, as we do not implement the DSU
     * (and in particular its system registers).
     */
    { .name = "CPUCFR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 4 },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 4,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0x961563010,
      .accessfn = access_actlr_w },
    { .name = "CPUPCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPMR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 3,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPOR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 2,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPSELR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPWRCTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 7,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ERXPFGCDN_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ERXPFGCTL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ERXPFGF_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
};

static void define_neoverse_n1_cp_reginfo(ARMCPU *cpu)
{
    define_arm_cp_regs(cpu, neoverse_n1_cp_reginfo);
}

static const ARMCPRegInfo neoverse_v1_cp_reginfo[] = {
    { .name = "CPUECTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 5,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUPPMCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR3_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 6,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void define_neoverse_v1_cp_reginfo(ARMCPU *cpu)
{
    /*
     * The Neoverse V1 has all of the Neoverse N1's IMPDEF
     * registers and a few more of its own.
     */
    define_arm_cp_regs(cpu, neoverse_n1_cp_reginfo);
    define_arm_cp_regs(cpu, neoverse_v1_cp_reginfo);
}

static void aarch64_neoverse_n1_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,neoverse-n1";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by B2.4 AArch64 registers by functional group */
    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0x8444c004;
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64DFR0, 0x0000000110305408ull);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0000100010211120ull);
    SET_IDREG(isar, ID_AA64ISAR1, 0x0000000000100001ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0000000000001011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1100000010111112ull); /* GIC filled in later */
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000020ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x04010088);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x01011121);
    SET_IDREG(isar, ID_ISAR6, 0x00000010);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x00021110);
    SET_IDREG(isar, ID_PFR0, 0x10010131);
    SET_IDREG(isar, ID_PFR1, 0x00010000); /* GIC filled in later */
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x414fd0c1;          /* r4p1 */
    cpu->revidr = 0;

    /* From B2.23 CCSIDR_EL1 */
    /* 64KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 7);
    /* 64KB L1 icache */
    cpu->ccsidr[1] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 4, 64, 64 * KiB, 2);
    /* 1MB L2 dcache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_LEGACY, 8, 64, 1 * MiB, 7);

    /* From B2.98 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From B4.23 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* From B5.1 AdvSIMD AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From D5.1 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x410c3000;

    define_neoverse_n1_cp_reginfo(cpu);
}

static void aarch64_neoverse_v1_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,neoverse-v1";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by 3.2.4 AArch64 registers by functional group */
    SET_IDREG(isar, CLIDR, 0x82000023);
    cpu->ctr = 0xb444c004; /* With DIC and IDC set */
    cpu->dcz_blocksize = 4;
    SET_IDREG(isar, ID_AA64AFR0, 0x00000000);
    SET_IDREG(isar, ID_AA64AFR1, 0x00000000);
    SET_IDREG(isar, ID_AA64DFR0, 0x000001f210305519ull);
    SET_IDREG(isar, ID_AA64DFR1, 0x00000000);
    SET_IDREG(isar, ID_AA64ISAR0, 0x1011111110212120ull); /* with FEAT_RNG */
    SET_IDREG(isar, ID_AA64ISAR1, 0x0011000001211032ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000000000101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x0220011102101011ull);
    SET_IDREG(isar, ID_AA64PFR0, 0x1101110120111112ull); /* GIC filled in later */
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000020ull);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_DFR0, 0x15011099);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x11011121);
    SET_IDREG(isar, ID_ISAR6, 0x01100111);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_MMFR4, 0x01021110);
    SET_IDREG(isar, ID_PFR0, 0x21110131);
    SET_IDREG(isar, ID_PFR1, 0x00010000); /* GIC filled in later */
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    cpu->midr = 0x411FD402;          /* r1p2 */
    cpu->revidr = 0;

    /*
     * The Neoverse-V1 r1p2 TRM lists 32-bit format CCSIDR_EL1 values,
     * but also says it implements CCIDX, which means they should be
     * 64-bit format. So we here use values which are based on the textual
     * information in chapter 2 of the TRM:
     *
     * L1: 4-way set associative 64-byte line size, total size 64K.
     * L2: 8-way set associative, 64 byte line size, either 512K or 1MB.
     * L3: No L3 (this matches the CLIDR_EL1 value).
     */
    /* 64KB L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 4, 64, 64 * KiB, 0);
    /* 64KB L1 icache */
    cpu->ccsidr[1] = cpu->ccsidr[0];
    /* 1MB L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 8, 64, 1 * MiB, 0);

    /* From 3.2.115 SCTLR_EL3 */
    cpu->reset_sctlr = 0x30c50838;

    /* From 3.4.8 ICC_CTLR_EL3 and 3.4.23 ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* From 3.5.1 AdvSIMD AArch64 register summary */
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x13211111;
    cpu->isar.mvfr2 = 0x00000043;

    /* From 3.7.5 ID_AA64ZFR0_EL1 */
    SET_IDREG(isar, ID_AA64ZFR0, 0x0000100000100000);
    cpu->sve_vq.supported = (1 << 0)  /* 128bit */
                            | (1 << 1);  /* 256bit */

    /* From 5.5.1 AArch64 PMU register summary */
    cpu->isar.reset_pmcr_el0 = 0x41213000;

    define_neoverse_v1_cp_reginfo(cpu);

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
}

static const ARMCPRegInfo cortex_a710_cp_reginfo[] = {
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR3_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR4_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 4,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUECTLR2_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 1, .opc2 = 5,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUPPMCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 4,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPWRCTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 7,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ATCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR5_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 8, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR6_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 8, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "CPUACTLR7_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 8, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0,
      .accessfn = access_actlr_w },
    { .name = "ATCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AVTCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 15, .crm = 7, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR4_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 4,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR5_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 5,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPPMCR6_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 2, .opc2 = 6,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 4, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ATCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 7, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPSELR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPOR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 2,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPMR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 3,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPOR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 4,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPMR2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 5,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUPFR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 8, .opc2 = 6,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    /*
     * Report CPUCFR_EL1.SCU as 1, as we do not implement the DSU
     * (and in particular its system registers).
     */
    { .name = "CPUCFR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 4 },

    /*
     * Stub RAMINDEX, as we don't actually implement caches, BTB,
     * or anything else with cpu internal memory.
     * "Read" zeros into the IDATA* and DDATA* output registers.
     */
    { .name = "RAMINDEX_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IDATA0_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IDATA1_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 1,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IDATA2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 0, .opc2 = 2,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DDATA0_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DDATA1_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 1, .opc2 = 1,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DDATA2_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 1, .opc2 = 2,
      .access = PL3_R, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void aarch64_a710_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a710";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by Section B.4: AArch64 registers */
    cpu->midr          = 0x412FD471; /* r2p1 */
    cpu->revidr        = 0;
    SET_IDREG(isar, ID_PFR0, 0x21110131);
    SET_IDREG(isar, ID_PFR1, 0x00010000); /* GIC filled in later */
    SET_IDREG(isar, ID_DFR0, 0x16011099);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x11011121); /* with Crypto */
    SET_IDREG(isar, ID_MMFR4, 0x21021110);
    SET_IDREG(isar, ID_ISAR6, 0x01111111);
    cpu->isar.mvfr0    = 0x10110222;
    cpu->isar.mvfr1    = 0x13211111;
    cpu->isar.mvfr2    = 0x00000043;
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    SET_IDREG(isar, ID_AA64PFR0, 0x1201111120111112ull); /* GIC filled in later */
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000221ull);
    SET_IDREG(isar, ID_AA64ZFR0, 0x0000110100110021ull); /* with Crypto */
    SET_IDREG(isar, ID_AA64DFR0, 0x000011f010305619ull);
    SET_IDREG(isar, ID_AA64DFR1, 0);
    SET_IDREG(isar, ID_AA64AFR0, 0);
    SET_IDREG(isar, ID_AA64AFR1, 0);
    SET_IDREG(isar, ID_AA64ISAR0, 0x0221111110212120ull); /* with Crypto */
    SET_IDREG(isar, ID_AA64ISAR1, 0x0010111101211052ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000022200101122ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x1221011110101011ull);
    SET_IDREG(isar, CLIDR, 0x0000001482000023ull);
    cpu->gm_blocksize      = 4;
    cpu->ctr               = 0x000000049444c004ull;
    cpu->dcz_blocksize     = 4;
    /* TODO FEAT_MPAM: mpamidr_el1 = 0x0000_0001_0006_003f */

    /* Section B.5.2: PMCR_EL0 */
    cpu->isar.reset_pmcr_el0 = 0xa000;  /* with 20 counters */

    /* Section B.6.7: ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* Section 14: Scalable Vector Extensions support */
    cpu->sve_vq.supported = 1 << 0;  /* 128bit */

    /*
     * The cortex-a710 TRM does not list CCSIDR values.  The layout of
     * the caches are in text in Table 7-1, Table 8-1, and Table 9-1.
     *
     * L1: 4-way set associative 64-byte line size, total either 32K or 64K.
     * L2: 8-way set associative 64 byte line size, total either 256K or 512K.
     */
    /* L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 4, 64, 64 * KiB, 0);
    /* L1 icache */
    cpu->ccsidr[1] = cpu->ccsidr[0];
    /* L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 8, 64, 512 * KiB, 0);

    /* FIXME: Not documented -- copied from neoverse-v1 */
    cpu->reset_sctlr = 0x30c50838;

    define_arm_cp_regs(cpu, cortex_a710_cp_reginfo);

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
}

/* Extra IMPDEF regs in the N2 beyond those in the A710 */
static const ARMCPRegInfo neoverse_n2_cp_reginfo[] = {
    { .name = "CPURNDBR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 3, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPURNDPEID_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 15, .crm = 3, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void aarch64_neoverse_n2_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,neoverse-n2";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);

    /* Ordered by Section B.5: AArch64 ID registers */
    cpu->midr          = 0x410FD493; /* r0p3 */
    cpu->revidr        = 0;
    SET_IDREG(isar, ID_PFR0, 0x21110131);
    SET_IDREG(isar, ID_PFR1, 0x00010000); /* GIC filled in later */
    SET_IDREG(isar, ID_DFR0, 0x16011099);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01260000);
    SET_IDREG(isar, ID_MMFR3, 0x02122211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232042);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x11011121); /* with Crypto */
    SET_IDREG(isar, ID_MMFR4, 0x01021110);
    SET_IDREG(isar, ID_ISAR6, 0x01111111);
    cpu->isar.mvfr0    = 0x10110222;
    cpu->isar.mvfr1    = 0x13211111;
    cpu->isar.mvfr2    = 0x00000043;
    SET_IDREG(isar, ID_PFR2, 0x00000011);
    SET_IDREG(isar, ID_AA64PFR0, 0x1201111120111112ull); /* GIC filled in later */
    SET_IDREG(isar, ID_AA64PFR1, 0x0000000000000221ull);
    SET_IDREG(isar, ID_AA64ZFR0, 0x0000110100110021ull); /* with Crypto */
    SET_IDREG(isar, ID_AA64DFR0, 0x000011f210305619ull);
    SET_IDREG(isar, ID_AA64DFR1, 0);
    SET_IDREG(isar, ID_AA64AFR0, 0);
    SET_IDREG(isar, ID_AA64AFR1, 0);
    SET_IDREG(isar, ID_AA64ISAR0, 0x1221111110212120ull); /* with Crypto and FEAT_RNG */
    SET_IDREG(isar, ID_AA64ISAR1, 0x0011111101211052ull);
    SET_IDREG(isar, ID_AA64MMFR0, 0x0000022200101125ull);
    SET_IDREG(isar, ID_AA64MMFR1, 0x0000000010212122ull);
    SET_IDREG(isar, ID_AA64MMFR2, 0x1221011112101011ull);
    SET_IDREG(isar, CLIDR, 0x0000001482000023ull);
    cpu->gm_blocksize      = 4;
    cpu->ctr               = 0x00000004b444c004ull;
    cpu->dcz_blocksize     = 4;
    /* TODO FEAT_MPAM: mpamidr_el1 = 0x0000_0001_001e_01ff */

    /* Section B.7.2: PMCR_EL0 */
    cpu->isar.reset_pmcr_el0 = 0x3000;  /* with 6 counters */

    /* Section B.8.9: ICH_VTR_EL2 */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;

    /* Section 14: Scalable Vector Extensions support */
    cpu->sve_vq.supported = 1 << 0;  /* 128bit */

    /*
     * The Neoverse N2 TRM does not list CCSIDR values.  The layout of
     * the caches are in text in Table 7-1, Table 8-1, and Table 9-1.
     *
     * L1: 4-way set associative 64-byte line size, total 64K.
     * L2: 8-way set associative 64 byte line size, total either 512K or 1024K.
     */
    /* L1 dcache */
    cpu->ccsidr[0] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 4, 64, 64 * KiB, 0);
    /* L1 icache */
    cpu->ccsidr[1] = cpu->ccsidr[0];
    /* L2 cache */
    cpu->ccsidr[2] = make_ccsidr(CCSIDR_FORMAT_CCIDX, 8, 64, 512 * KiB, 0);
    /* FIXME: Not documented -- copied from neoverse-v1 */
    cpu->reset_sctlr = 0x30c50838;

    /*
     * The Neoverse N2 has all of the Cortex-A710 IMPDEF registers,
     * and a few more RNG related ones.
     */
    define_arm_cp_regs(cpu, cortex_a710_cp_reginfo);
    define_arm_cp_regs(cpu, neoverse_n2_cp_reginfo);

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
}

/*
 * -cpu max: a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-arm is defined in cpu32.c;
 * this only needs to handle 64 bits.
 */
void aarch64_max_tcg_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;
    uint64_t t;
    uint32_t u;

    /*
     * Unset ARM_FEATURE_BACKCOMPAT_CNTFRQ, which we would otherwise default
     * to because we started with aarch64_a57_initfn(). A 'max' CPU might
     * be a v8.6-or-later one, in which case the cntfrq must be 1GHz; and
     * because it is our "may change" CPU type we are OK with it not being
     * backwards-compatible with how it worked in old QEMU.
     */
    unset_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);

    /*
     * Reset MIDR so the guest doesn't mistake our 'max' CPU type for a real
     * one and try to apply errata workarounds or use impdef features we
     * don't provide.
     * An IMPLEMENTER field of 0 means "reserved for software use";
     * ARCHITECTURE must be 0xf indicating "v7 or later, check ID registers
     * to see which features are present";
     * the VARIANT, PARTNUM and REVISION fields are all implementation
     * defined and we choose to define PARTNUM just in case guest
     * code needs to distinguish this QEMU CPU from other software
     * implementations, though this shouldn't be needed.
     */
    t = FIELD_DP64(0, MIDR_EL1, IMPLEMENTER, 0);
    t = FIELD_DP64(t, MIDR_EL1, ARCHITECTURE, 0xf);
    t = FIELD_DP64(t, MIDR_EL1, PARTNUM, 'Q');
    t = FIELD_DP64(t, MIDR_EL1, VARIANT, 0);
    t = FIELD_DP64(t, MIDR_EL1, REVISION, 0);
    cpu->midr = t;

    /*
     * We're going to set FEAT_S2FWB, which mandates that CLIDR_EL1.{LoUU,LoUIS}
     * are zero.
     */
    u = GET_IDREG(isar, CLIDR);
    u = FIELD_DP32(u, CLIDR_EL1, LOUIS, 0);
    u = FIELD_DP32(u, CLIDR_EL1, LOUU, 0);
    SET_IDREG(isar, CLIDR, u);

    /*
     * Set CTR_EL0.DIC and IDC to tell the guest it doesnt' need to
     * do any cache maintenance for data-to-instruction or
     * instruction-to-guest coherence. (Our cache ops are nops.)
     */
    t = cpu->ctr;
    t = FIELD_DP64(t, CTR_EL0, IDC, 1);
    t = FIELD_DP64(t, CTR_EL0, DIC, 1);
    cpu->ctr = t;

    t = GET_IDREG(isar, ID_AA64ISAR0);
    t = FIELD_DP64(t, ID_AA64ISAR0, AES, 2);      /* FEAT_PMULL */
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA1, 1);     /* FEAT_SHA1 */
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA2, 2);     /* FEAT_SHA512 */
    t = FIELD_DP64(t, ID_AA64ISAR0, CRC32, 1);    /* FEAT_CRC32 */
    t = FIELD_DP64(t, ID_AA64ISAR0, ATOMIC, 3);   /* FEAT_LSE, FEAT_LSE128 */
    t = FIELD_DP64(t, ID_AA64ISAR0, RDM, 1);      /* FEAT_RDM */
    t = FIELD_DP64(t, ID_AA64ISAR0, SHA3, 1);     /* FEAT_SHA3 */
    t = FIELD_DP64(t, ID_AA64ISAR0, SM3, 1);      /* FEAT_SM3 */
    t = FIELD_DP64(t, ID_AA64ISAR0, SM4, 1);      /* FEAT_SM4 */
    t = FIELD_DP64(t, ID_AA64ISAR0, DP, 1);       /* FEAT_DotProd */
    t = FIELD_DP64(t, ID_AA64ISAR0, FHM, 1);      /* FEAT_FHM */
    t = FIELD_DP64(t, ID_AA64ISAR0, TS, 2);       /* FEAT_FlagM2 */
    t = FIELD_DP64(t, ID_AA64ISAR0, TLB, 2);      /* FEAT_TLBIRANGE */
    t = FIELD_DP64(t, ID_AA64ISAR0, RNDR, 1);     /* FEAT_RNG */
    SET_IDREG(isar, ID_AA64ISAR0, t);

    t = GET_IDREG(isar, ID_AA64ISAR1);
    t = FIELD_DP64(t, ID_AA64ISAR1, DPB, 2);      /* FEAT_DPB2 */
    t = FIELD_DP64(t, ID_AA64ISAR1, APA, PauthFeat_FPACCOMBINED);
    t = FIELD_DP64(t, ID_AA64ISAR1, API, 1);
    t = FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 1);    /* FEAT_JSCVT */
    t = FIELD_DP64(t, ID_AA64ISAR1, FCMA, 1);     /* FEAT_FCMA */
    t = FIELD_DP64(t, ID_AA64ISAR1, LRCPC, 2);    /* FEAT_LRCPC2 */
    t = FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 1);  /* FEAT_FRINTTS */
    t = FIELD_DP64(t, ID_AA64ISAR1, SB, 1);       /* FEAT_SB */
    t = FIELD_DP64(t, ID_AA64ISAR1, SPECRES, 1);  /* FEAT_SPECRES */
    t = FIELD_DP64(t, ID_AA64ISAR1, BF16, 2);     /* FEAT_BF16, FEAT_EBF16 */
    t = FIELD_DP64(t, ID_AA64ISAR1, DGH, 1);      /* FEAT_DGH */
    t = FIELD_DP64(t, ID_AA64ISAR1, I8MM, 1);     /* FEAT_I8MM */
    t = FIELD_DP64(t, ID_AA64ISAR1, XS, 1);       /* FEAT_XS */
    SET_IDREG(isar, ID_AA64ISAR1, t);

    t = GET_IDREG(isar, ID_AA64ISAR2);
    t = FIELD_DP64(t, ID_AA64ISAR2, RPRES, 1);    /* FEAT_RPRES */
    t = FIELD_DP64(t, ID_AA64ISAR2, MOPS, 1);     /* FEAT_MOPS */
    t = FIELD_DP64(t, ID_AA64ISAR2, BC, 1);       /* FEAT_HBC */
    t = FIELD_DP64(t, ID_AA64ISAR2, WFXT, 2);     /* FEAT_WFxT */
    t = FIELD_DP64(t, ID_AA64ISAR2, CSSC, 1);     /* FEAT_CSSC */
    t = FIELD_DP64(t, ID_AA64ISAR2, ATS1A, 1);    /* FEAT_ATS1A */
    SET_IDREG(isar, ID_AA64ISAR2, t);

    t = GET_IDREG(isar, ID_AA64PFR0);
    t = FIELD_DP64(t, ID_AA64PFR0, FP, 1);        /* FEAT_FP16 */
    t = FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 1);   /* FEAT_FP16 */
    t = FIELD_DP64(t, ID_AA64PFR0, RAS, 2);       /* FEAT_RASv1p1 + FEAT_DoubleFault */
    t = FIELD_DP64(t, ID_AA64PFR0, SVE, 1);
    t = FIELD_DP64(t, ID_AA64PFR0, SEL2, 1);      /* FEAT_SEL2 */
    t = FIELD_DP64(t, ID_AA64PFR0, DIT, 1);       /* FEAT_DIT */
    t = FIELD_DP64(t, ID_AA64PFR0, CSV2, 3);      /* FEAT_CSV2_3 */
    t = FIELD_DP64(t, ID_AA64PFR0, CSV3, 1);      /* FEAT_CSV3 */
    SET_IDREG(isar, ID_AA64PFR0, t);

    t = GET_IDREG(isar, ID_AA64PFR1);
    t = FIELD_DP64(t, ID_AA64PFR1, BT, 1);        /* FEAT_BTI */
    t = FIELD_DP64(t, ID_AA64PFR1, SSBS, 2);      /* FEAT_SSBS2 */
    /*
     * Begin with full support for MTE. This will be downgraded to MTE=0
     * during realize if the board provides no tag memory, much like
     * we do for EL2 with the virtualization=on property.
     */
    t = FIELD_DP64(t, ID_AA64PFR1, MTE, 3);       /* FEAT_MTE3 */
    t = FIELD_DP64(t, ID_AA64PFR1, RAS_FRAC, 0);  /* FEAT_RASv1p1 + FEAT_DoubleFault */
    t = FIELD_DP64(t, ID_AA64PFR1, SME, 2);       /* FEAT_SME2 */
    t = FIELD_DP64(t, ID_AA64PFR1, CSV2_FRAC, 0); /* FEAT_CSV2_3 */
    t = FIELD_DP64(t, ID_AA64PFR1, NMI, 1);       /* FEAT_NMI */
    t = FIELD_DP64(t, ID_AA64PFR1, GCS, 1);       /* FEAT_GCS */
    SET_IDREG(isar, ID_AA64PFR1, t);

    t = GET_IDREG(isar, ID_AA64MMFR0);
    t = FIELD_DP64(t, ID_AA64MMFR0, PARANGE, 6); /* FEAT_LPA: 52 bits */
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN16, 1);   /* 16k pages supported */
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN16_2, 2); /* 16k stage2 supported */
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN64_2, 2); /* 64k stage2 supported */
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN4_2, 2);  /*  4k stage2 supported */
    t = FIELD_DP64(t, ID_AA64MMFR0, FGT, 1);       /* FEAT_FGT */
    t = FIELD_DP64(t, ID_AA64MMFR0, ECV, 2);       /* FEAT_ECV */
    SET_IDREG(isar, ID_AA64MMFR0, t);

    t = GET_IDREG(isar, ID_AA64MMFR1);
    t = FIELD_DP64(t, ID_AA64MMFR1, HAFDBS, 2);   /* FEAT_HAFDBS */
    t = FIELD_DP64(t, ID_AA64MMFR1, VMIDBITS, 2); /* FEAT_VMID16 */
    t = FIELD_DP64(t, ID_AA64MMFR1, VH, 1);       /* FEAT_VHE */
    t = FIELD_DP64(t, ID_AA64MMFR1, HPDS, 2);     /* FEAT_HPDS2 */
    t = FIELD_DP64(t, ID_AA64MMFR1, LO, 1);       /* FEAT_LOR */
    t = FIELD_DP64(t, ID_AA64MMFR1, PAN, 3);      /* FEAT_PAN3 */
    t = FIELD_DP64(t, ID_AA64MMFR1, XNX, 1);      /* FEAT_XNX */
    t = FIELD_DP64(t, ID_AA64MMFR1, ETS, 2);      /* FEAT_ETS2 */
    t = FIELD_DP64(t, ID_AA64MMFR1, HCX, 1);      /* FEAT_HCX */
    t = FIELD_DP64(t, ID_AA64MMFR1, AFP, 1);      /* FEAT_AFP */
    t = FIELD_DP64(t, ID_AA64MMFR1, TIDCP1, 1);   /* FEAT_TIDCP1 */
    t = FIELD_DP64(t, ID_AA64MMFR1, CMOW, 1);     /* FEAT_CMOW */
    SET_IDREG(isar, ID_AA64MMFR1, t);

    t = GET_IDREG(isar, ID_AA64MMFR2);
    t = FIELD_DP64(t, ID_AA64MMFR2, CNP, 1);      /* FEAT_TTCNP */
    t = FIELD_DP64(t, ID_AA64MMFR2, UAO, 1);      /* FEAT_UAO */
    t = FIELD_DP64(t, ID_AA64MMFR2, IESB, 1);     /* FEAT_IESB */
    t = FIELD_DP64(t, ID_AA64MMFR2, VARANGE, 1);  /* FEAT_LVA */
    t = FIELD_DP64(t, ID_AA64MMFR2, NV, 2);       /* FEAT_NV2 */
    t = FIELD_DP64(t, ID_AA64MMFR2, ST, 1);       /* FEAT_TTST */
    t = FIELD_DP64(t, ID_AA64MMFR2, AT, 1);       /* FEAT_LSE2 */
    t = FIELD_DP64(t, ID_AA64MMFR2, IDS, 1);      /* FEAT_IDST */
    t = FIELD_DP64(t, ID_AA64MMFR2, FWB, 1);      /* FEAT_S2FWB */
    t = FIELD_DP64(t, ID_AA64MMFR2, TTL, 1);      /* FEAT_TTL */
    t = FIELD_DP64(t, ID_AA64MMFR2, BBM, 2);      /* FEAT_BBM at level 2 */
    t = FIELD_DP64(t, ID_AA64MMFR2, EVT, 2);      /* FEAT_EVT */
    t = FIELD_DP64(t, ID_AA64MMFR2, E0PD, 1);     /* FEAT_E0PD */
    SET_IDREG(isar, ID_AA64MMFR2, t);

    t = GET_IDREG(isar, ID_AA64MMFR3);
    t = FIELD_DP64(t, ID_AA64MMFR3, TCRX, 1);       /* FEAT_TCR2 */
    t = FIELD_DP64(t, ID_AA64MMFR3, SCTLRX, 1);     /* FEAT_SCTLR2 */
    t = FIELD_DP64(t, ID_AA64MMFR3, MEC, 1);        /* FEAT_MEC */
    t = FIELD_DP64(t, ID_AA64MMFR3, SPEC_FPACC, 1); /* FEAT_FPACC_SPEC */
    t = FIELD_DP64(t, ID_AA64MMFR3, S1PIE, 1);    /* FEAT_S1PIE */
    t = FIELD_DP64(t, ID_AA64MMFR3, S2PIE, 1);    /* FEAT_S2PIE */
    t = FIELD_DP64(t, ID_AA64MMFR3, AIE, 1);      /* FEAT_AIE */
    SET_IDREG(isar, ID_AA64MMFR3, t);

    t = GET_IDREG(isar, ID_AA64ZFR0);
    t = FIELD_DP64(t, ID_AA64ZFR0, SVEVER, 2);    /* FEAT_SVE2p1 */
    t = FIELD_DP64(t, ID_AA64ZFR0, AES, 2);       /* FEAT_SVE_PMULL128 */
    t = FIELD_DP64(t, ID_AA64ZFR0, BITPERM, 1);   /* FEAT_SVE_BitPerm */
    t = FIELD_DP64(t, ID_AA64ZFR0, BFLOAT16, 2);  /* FEAT_BF16, FEAT_EBF16 */
    t = FIELD_DP64(t, ID_AA64ZFR0, B16B16, 1);    /* FEAT_SVE_B16B16 */
    t = FIELD_DP64(t, ID_AA64ZFR0, SHA3, 1);      /* FEAT_SVE_SHA3 */
    t = FIELD_DP64(t, ID_AA64ZFR0, SM4, 1);       /* FEAT_SVE_SM4 */
    t = FIELD_DP64(t, ID_AA64ZFR0, I8MM, 1);      /* FEAT_I8MM */
    t = FIELD_DP64(t, ID_AA64ZFR0, F32MM, 1);     /* FEAT_F32MM */
    t = FIELD_DP64(t, ID_AA64ZFR0, F64MM, 1);     /* FEAT_F64MM */
    SET_IDREG(isar, ID_AA64ZFR0, t);

    t = GET_IDREG(isar, ID_AA64DFR0);
    t = FIELD_DP64(t, ID_AA64DFR0, DEBUGVER, 10); /* FEAT_Debugv8p8 */
    t = FIELD_DP64(t, ID_AA64DFR0, PMUVER, 6);    /* FEAT_PMUv3p5 */
    t = FIELD_DP64(t, ID_AA64DFR0, HPMN0, 1);     /* FEAT_HPMN0 */
    SET_IDREG(isar, ID_AA64DFR0, t);

    t = GET_IDREG(isar, ID_AA64SMFR0);
    t = FIELD_DP64(t, ID_AA64SMFR0, F32F32, 1);   /* FEAT_SME */
    t = FIELD_DP64(t, ID_AA64SMFR0, BI32I32, 1);  /* FEAT_SME2 */
    t = FIELD_DP64(t, ID_AA64SMFR0, B16F32, 1);   /* FEAT_SME */
    t = FIELD_DP64(t, ID_AA64SMFR0, F16F32, 1);   /* FEAT_SME */
    t = FIELD_DP64(t, ID_AA64SMFR0, I8I32, 0xf);  /* FEAT_SME */
    t = FIELD_DP64(t, ID_AA64SMFR0, F16F16, 1);   /* FEAT_SME_F16F16 */
    t = FIELD_DP64(t, ID_AA64SMFR0, B16B16, 1);   /* FEAT_SME_B16B16 */
    t = FIELD_DP64(t, ID_AA64SMFR0, I16I32, 5);   /* FEAT_SME2 */
    t = FIELD_DP64(t, ID_AA64SMFR0, F64F64, 1);   /* FEAT_SME_F64F64 */
    t = FIELD_DP64(t, ID_AA64SMFR0, I16I64, 0xf); /* FEAT_SME_I16I64 */
    t = FIELD_DP64(t, ID_AA64SMFR0, SMEVER, 2);   /* FEAT_SME2p1 */
    t = FIELD_DP64(t, ID_AA64SMFR0, FA64, 1);     /* FEAT_SME_FA64 */
    SET_IDREG(isar, ID_AA64SMFR0, t);

    /* Replicate the same data to the 32-bit id registers.  */
    aa32_max_features(cpu);

#ifdef CONFIG_USER_ONLY
    /*
     * For usermode -cpu max we can use a larger and more efficient DCZ
     * blocksize since we don't have to follow what the hardware does.
     */
    cpu->ctr = 0x80038003; /* 32 byte I and D cacheline size, VIPT icache */
    cpu->dcz_blocksize = 7; /*  512 bytes */
#endif
    cpu->gm_blocksize = 6;  /*  256 bytes */

    cpu->sve_vq.supported = MAKE_64BIT_MASK(0, ARM_MAX_VQ);
    cpu->sme_vq.supported = SVE_VQ_POW2_MAP;

    aarch64_add_pauth_properties(obj);
    aarch64_add_sve_properties(obj);
    aarch64_add_sme_properties(obj);
    object_property_add(obj, "sve-max-vq", "uint32", cpu_max_get_sve_max_vq,
                        cpu_max_set_sve_max_vq, NULL, NULL);
    object_property_add_bool(obj, "x-rme", cpu_arm_get_rme, cpu_arm_set_rme);
    object_property_add(obj, "x-l0gptsz", "uint32", cpu_max_get_l0gptsz,
                        cpu_max_set_l0gptsz, NULL, NULL);
    qdev_property_add_static(DEVICE(obj), &arm_cpu_lpa2_property);
}

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a35",         .initfn = aarch64_a35_initfn },
    { .name = "cortex-a55",         .initfn = aarch64_a55_initfn },
    { .name = "cortex-a72",         .initfn = aarch64_a72_initfn },
    { .name = "cortex-a76",         .initfn = aarch64_a76_initfn },
    /*
     * The Cortex-A78AE differs slightly from the plain Cortex-A78. We don't
     * currently model the latter.
     */
    { .name = "cortex-a78ae",       .initfn = aarch64_a78ae_initfn },
    { .name = "cortex-a710",        .initfn = aarch64_a710_initfn },
    { .name = "a64fx",              .initfn = aarch64_a64fx_initfn },
    { .name = "neoverse-n1",        .initfn = aarch64_neoverse_n1_initfn },
    { .name = "neoverse-v1",        .initfn = aarch64_neoverse_v1_initfn },
    { .name = "neoverse-n2",        .initfn = aarch64_neoverse_n2_initfn },
};

static void aarch64_cpu_register_types(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(aarch64_cpus); ++i) {
        arm_cpu_register(&aarch64_cpus[i]);
    }
}

type_init(aarch64_cpu_register_types)
