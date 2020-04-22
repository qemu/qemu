/*
 * QEMU AArch64 CPU
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
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif
#include "sysemu/kvm.h"
#include "kvm_arm.h"
#include "qapi/visitor.h"

static inline void set_feature(CPUARMState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static inline void unset_feature(CPUARMState *env, int feature)
{
    env->features &= ~(1ULL << feature);
}

#ifndef CONFIG_USER_ONLY
static uint64_t a57_a53_l2ctlr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /* Number of cores is in [25:24]; otherwise we RAZ */
    return (cpu->core_count - 1) << 24;
}
#endif

static const ARMCPRegInfo cortex_a72_a57_a53_cp_reginfo[] = {
#ifndef CONFIG_USER_ONLY
    { .name = "L2CTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
    { .name = "L2CTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
#endif
    { .name = "L2ECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ECTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ACTLR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR",
      .cp = 15, .opc1 = 0, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUECTLR",
      .cp = 15, .opc1 = 1, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUMERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUMERRSR",
      .cp = 15, .opc1 = 2, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "L2MERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2MERRSR",
      .cp = 15, .opc1 = 3, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void aarch64_a57_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a57";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A57;
    cpu->midr = 0x411fd070;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10101105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_isar6 = 0;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001124;
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
}

static void aarch64_a53_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a53";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A53;
    cpu->midr = 0x410fd034;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x84448004; /* L1Ip = VIPT */
    cpu->reset_sctlr = 0x00c50838;
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10101105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_isar6 = 0;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001122; /* 40 bit physical addr */
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1024KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
}

static void aarch64_a72_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a72";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
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
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01260000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232042;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x00011142;
    cpu->isar.id_isar5 = 0x00011121;
    cpu->isar.id_aa64pfr0 = 0x00002222;
    cpu->isar.id_aa64dfr0 = 0x10305106;
    cpu->isar.id_aa64isar0 = 0x00011120;
    cpu->isar.id_aa64mmfr0 = 0x00001124;
    cpu->isar.dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1MB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a72_a57_a53_cp_reginfo);
}

void arm_cpu_sve_finalize(ARMCPU *cpu, Error **errp)
{
    /*
     * If any vector lengths are explicitly enabled with sve<N> properties,
     * then all other lengths are implicitly disabled.  If sve-max-vq is
     * specified then it is the same as explicitly enabling all lengths
     * up to and including the specified maximum, which means all larger
     * lengths will be implicitly disabled.  If no sve<N> properties
     * are enabled and sve-max-vq is not specified, then all lengths not
     * explicitly disabled will be enabled.  Additionally, all power-of-two
     * vector lengths less than the maximum enabled length will be
     * automatically enabled and all vector lengths larger than the largest
     * disabled power-of-two vector length will be automatically disabled.
     * Errors are generated if the user provided input that interferes with
     * any of the above.  Finally, if SVE is not disabled, then at least one
     * vector length must be enabled.
     */
    DECLARE_BITMAP(kvm_supported, ARM_MAX_VQ);
    DECLARE_BITMAP(tmp, ARM_MAX_VQ);
    uint32_t vq, max_vq = 0;

    /* Collect the set of vector lengths supported by KVM. */
    bitmap_zero(kvm_supported, ARM_MAX_VQ);
    if (kvm_enabled() && kvm_arm_sve_supported(CPU(cpu))) {
        kvm_arm_sve_get_vls(CPU(cpu), kvm_supported);
    } else if (kvm_enabled()) {
        assert(!cpu_isar_feature(aa64_sve, cpu));
    }

    /*
     * Process explicit sve<N> properties.
     * From the properties, sve_vq_map<N> implies sve_vq_init<N>.
     * Check first for any sve<N> enabled.
     */
    if (!bitmap_empty(cpu->sve_vq_map, ARM_MAX_VQ)) {
        max_vq = find_last_bit(cpu->sve_vq_map, ARM_MAX_VQ) + 1;

        if (cpu->sve_max_vq && max_vq > cpu->sve_max_vq) {
            error_setg(errp, "cannot enable sve%d", max_vq * 128);
            error_append_hint(errp, "sve%d is larger than the maximum vector "
                              "length, sve-max-vq=%d (%d bits)\n",
                              max_vq * 128, cpu->sve_max_vq,
                              cpu->sve_max_vq * 128);
            return;
        }

        if (kvm_enabled()) {
            /*
             * For KVM we have to automatically enable all supported unitialized
             * lengths, even when the smaller lengths are not all powers-of-two.
             */
            bitmap_andnot(tmp, kvm_supported, cpu->sve_vq_init, max_vq);
            bitmap_or(cpu->sve_vq_map, cpu->sve_vq_map, tmp, max_vq);
        } else {
            /* Propagate enabled bits down through required powers-of-two. */
            for (vq = pow2floor(max_vq); vq >= 1; vq >>= 1) {
                if (!test_bit(vq - 1, cpu->sve_vq_init)) {
                    set_bit(vq - 1, cpu->sve_vq_map);
                }
            }
        }
    } else if (cpu->sve_max_vq == 0) {
        /*
         * No explicit bits enabled, and no implicit bits from sve-max-vq.
         */
        if (!cpu_isar_feature(aa64_sve, cpu)) {
            /* SVE is disabled and so are all vector lengths.  Good. */
            return;
        }

        if (kvm_enabled()) {
            /* Disabling a supported length disables all larger lengths. */
            for (vq = 1; vq <= ARM_MAX_VQ; ++vq) {
                if (test_bit(vq - 1, cpu->sve_vq_init) &&
                    test_bit(vq - 1, kvm_supported)) {
                    break;
                }
            }
            max_vq = vq <= ARM_MAX_VQ ? vq - 1 : ARM_MAX_VQ;
            bitmap_andnot(cpu->sve_vq_map, kvm_supported,
                          cpu->sve_vq_init, max_vq);
            if (max_vq == 0 || bitmap_empty(cpu->sve_vq_map, max_vq)) {
                error_setg(errp, "cannot disable sve%d", vq * 128);
                error_append_hint(errp, "Disabling sve%d results in all "
                                  "vector lengths being disabled.\n",
                                  vq * 128);
                error_append_hint(errp, "With SVE enabled, at least one "
                                  "vector length must be enabled.\n");
                return;
            }
        } else {
            /* Disabling a power-of-two disables all larger lengths. */
            if (test_bit(0, cpu->sve_vq_init)) {
                error_setg(errp, "cannot disable sve128");
                error_append_hint(errp, "Disabling sve128 results in all "
                                  "vector lengths being disabled.\n");
                error_append_hint(errp, "With SVE enabled, at least one "
                                  "vector length must be enabled.\n");
                return;
            }
            for (vq = 2; vq <= ARM_MAX_VQ; vq <<= 1) {
                if (test_bit(vq - 1, cpu->sve_vq_init)) {
                    break;
                }
            }
            max_vq = vq <= ARM_MAX_VQ ? vq - 1 : ARM_MAX_VQ;
            bitmap_complement(cpu->sve_vq_map, cpu->sve_vq_init, max_vq);
        }

        max_vq = find_last_bit(cpu->sve_vq_map, max_vq) + 1;
    }

    /*
     * Process the sve-max-vq property.
     * Note that we know from the above that no bit above
     * sve-max-vq is currently set.
     */
    if (cpu->sve_max_vq != 0) {
        max_vq = cpu->sve_max_vq;

        if (!test_bit(max_vq - 1, cpu->sve_vq_map) &&
            test_bit(max_vq - 1, cpu->sve_vq_init)) {
            error_setg(errp, "cannot disable sve%d", max_vq * 128);
            error_append_hint(errp, "The maximum vector length must be "
                              "enabled, sve-max-vq=%d (%d bits)\n",
                              max_vq, max_vq * 128);
            return;
        }

        /* Set all bits not explicitly set within sve-max-vq. */
        bitmap_complement(tmp, cpu->sve_vq_init, max_vq);
        bitmap_or(cpu->sve_vq_map, cpu->sve_vq_map, tmp, max_vq);
    }

    /*
     * We should know what max-vq is now.  Also, as we're done
     * manipulating sve-vq-map, we ensure any bits above max-vq
     * are clear, just in case anybody looks.
     */
    assert(max_vq != 0);
    bitmap_clear(cpu->sve_vq_map, max_vq, ARM_MAX_VQ - max_vq);

    if (kvm_enabled()) {
        /* Ensure the set of lengths matches what KVM supports. */
        bitmap_xor(tmp, cpu->sve_vq_map, kvm_supported, max_vq);
        if (!bitmap_empty(tmp, max_vq)) {
            vq = find_last_bit(tmp, max_vq) + 1;
            if (test_bit(vq - 1, cpu->sve_vq_map)) {
                if (cpu->sve_max_vq) {
                    error_setg(errp, "cannot set sve-max-vq=%d",
                               cpu->sve_max_vq);
                    error_append_hint(errp, "This KVM host does not support "
                                      "the vector length %d-bits.\n",
                                      vq * 128);
                    error_append_hint(errp, "It may not be possible to use "
                                      "sve-max-vq with this KVM host. Try "
                                      "using only sve<N> properties.\n");
                } else {
                    error_setg(errp, "cannot enable sve%d", vq * 128);
                    error_append_hint(errp, "This KVM host does not support "
                                      "the vector length %d-bits.\n",
                                      vq * 128);
                }
            } else {
                error_setg(errp, "cannot disable sve%d", vq * 128);
                error_append_hint(errp, "The KVM host requires all "
                                  "supported vector lengths smaller "
                                  "than %d bits to also be enabled.\n",
                                  max_vq * 128);
            }
            return;
        }
    } else {
        /* Ensure all required powers-of-two are enabled. */
        for (vq = pow2floor(max_vq); vq >= 1; vq >>= 1) {
            if (!test_bit(vq - 1, cpu->sve_vq_map)) {
                error_setg(errp, "cannot disable sve%d", vq * 128);
                error_append_hint(errp, "sve%d is required as it "
                                  "is a power-of-two length smaller than "
                                  "the maximum, sve%d\n",
                                  vq * 128, max_vq * 128);
                return;
            }
        }
    }

    /*
     * Now that we validated all our vector lengths, the only question
     * left to answer is if we even want SVE at all.
     */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        error_setg(errp, "cannot enable sve%d", max_vq * 128);
        error_append_hint(errp, "SVE must be enabled to enable vector "
                          "lengths.\n");
        error_append_hint(errp, "Add sve=on to the CPU property list.\n");
        return;
    }

    /* From now on sve_max_vq is the actual maximum supported length. */
    cpu->sve_max_vq = max_vq;
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
    Error *err = NULL;
    uint32_t max_vq;

    visit_type_uint32(v, name, &max_vq, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (kvm_enabled() && !kvm_arm_sve_supported(CPU(cpu))) {
        error_setg(errp, "cannot set sve-max-vq");
        error_append_hint(errp, "SVE not supported by KVM on this host\n");
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

static void cpu_arm_get_sve_vq(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t vq = atoi(&name[3]) / 128;
    bool value;

    /* All vector lengths are disabled when SVE is off. */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        value = false;
    } else {
        value = test_bit(vq - 1, cpu->sve_vq_map);
    }
    visit_type_bool(v, name, &value, errp);
}

static void cpu_arm_set_sve_vq(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t vq = atoi(&name[3]) / 128;
    Error *err = NULL;
    bool value;

    visit_type_bool(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (value && kvm_enabled() && !kvm_arm_sve_supported(CPU(cpu))) {
        error_setg(errp, "cannot enable %s", name);
        error_append_hint(errp, "SVE not supported by KVM on this host\n");
        return;
    }

    if (value) {
        set_bit(vq - 1, cpu->sve_vq_map);
    } else {
        clear_bit(vq - 1, cpu->sve_vq_map);
    }
    set_bit(vq - 1, cpu->sve_vq_init);
}

static void cpu_arm_get_sve(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    bool value = cpu_isar_feature(aa64_sve, cpu);

    visit_type_bool(v, name, &value, errp);
}

static void cpu_arm_set_sve(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    Error *err = NULL;
    bool value;
    uint64_t t;

    visit_type_bool(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (value && kvm_enabled() && !kvm_arm_sve_supported(CPU(cpu))) {
        error_setg(errp, "'sve' feature not supported by KVM on this host");
        return;
    }

    t = cpu->isar.id_aa64pfr0;
    t = FIELD_DP64(t, ID_AA64PFR0, SVE, value);
    cpu->isar.id_aa64pfr0 = t;
}

void aarch64_add_sve_properties(Object *obj)
{
    uint32_t vq;

    object_property_add(obj, "sve", "bool", cpu_arm_get_sve,
                        cpu_arm_set_sve, NULL, NULL, &error_fatal);

    for (vq = 1; vq <= ARM_MAX_VQ; ++vq) {
        char name[8];
        sprintf(name, "sve%d", vq * 128);
        object_property_add(obj, name, "bool", cpu_arm_get_sve_vq,
                            cpu_arm_set_sve_vq, NULL, NULL, &error_fatal);
    }
}

/* -cpu max: if KVM is enabled, like -cpu host (best possible with this host);
 * otherwise, a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-arm is defined in cpu.c;
 * this only needs to handle 64 bits.
 */
static void aarch64_max_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    if (kvm_enabled()) {
        kvm_arm_set_cpu_features_from_host(cpu);
        kvm_arm_add_vcpu_properties(obj);
    } else {
        uint64_t t;
        uint32_t u;
        aarch64_a57_initfn(obj);

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

        t = cpu->isar.id_aa64isar0;
        t = FIELD_DP64(t, ID_AA64ISAR0, AES, 2); /* AES + PMULL */
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA1, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA2, 2); /* SHA512 */
        t = FIELD_DP64(t, ID_AA64ISAR0, CRC32, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, ATOMIC, 2);
        t = FIELD_DP64(t, ID_AA64ISAR0, RDM, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SHA3, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SM3, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, SM4, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, DP, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, FHM, 1);
        t = FIELD_DP64(t, ID_AA64ISAR0, TS, 2); /* v8.5-CondM */
        t = FIELD_DP64(t, ID_AA64ISAR0, RNDR, 1);
        cpu->isar.id_aa64isar0 = t;

        t = cpu->isar.id_aa64isar1;
        t = FIELD_DP64(t, ID_AA64ISAR1, DPB, 2);
        t = FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, FCMA, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, APA, 1); /* PAuth, architected only */
        t = FIELD_DP64(t, ID_AA64ISAR1, API, 0);
        t = FIELD_DP64(t, ID_AA64ISAR1, GPA, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, GPI, 0);
        t = FIELD_DP64(t, ID_AA64ISAR1, SB, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, SPECRES, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 1);
        t = FIELD_DP64(t, ID_AA64ISAR1, LRCPC, 2); /* ARMv8.4-RCPC */
        cpu->isar.id_aa64isar1 = t;

        t = cpu->isar.id_aa64pfr0;
        t = FIELD_DP64(t, ID_AA64PFR0, SVE, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, FP, 1);
        t = FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 1);
        cpu->isar.id_aa64pfr0 = t;

        t = cpu->isar.id_aa64pfr1;
        t = FIELD_DP64(t, ID_AA64PFR1, BT, 1);
        cpu->isar.id_aa64pfr1 = t;

        t = cpu->isar.id_aa64mmfr1;
        t = FIELD_DP64(t, ID_AA64MMFR1, HPDS, 1); /* HPD */
        t = FIELD_DP64(t, ID_AA64MMFR1, LO, 1);
        t = FIELD_DP64(t, ID_AA64MMFR1, VH, 1);
        t = FIELD_DP64(t, ID_AA64MMFR1, PAN, 2); /* ATS1E1 */
        t = FIELD_DP64(t, ID_AA64MMFR1, VMIDBITS, 2); /* VMID16 */
        cpu->isar.id_aa64mmfr1 = t;

        t = cpu->isar.id_aa64mmfr2;
        t = FIELD_DP64(t, ID_AA64MMFR2, UAO, 1);
        t = FIELD_DP64(t, ID_AA64MMFR2, CNP, 1); /* TTCNP */
        cpu->isar.id_aa64mmfr2 = t;

        /* Replicate the same data to the 32-bit id registers.  */
        u = cpu->isar.id_isar5;
        u = FIELD_DP32(u, ID_ISAR5, AES, 2); /* AES + PMULL */
        u = FIELD_DP32(u, ID_ISAR5, SHA1, 1);
        u = FIELD_DP32(u, ID_ISAR5, SHA2, 1);
        u = FIELD_DP32(u, ID_ISAR5, CRC32, 1);
        u = FIELD_DP32(u, ID_ISAR5, RDM, 1);
        u = FIELD_DP32(u, ID_ISAR5, VCMA, 1);
        cpu->isar.id_isar5 = u;

        u = cpu->isar.id_isar6;
        u = FIELD_DP32(u, ID_ISAR6, JSCVT, 1);
        u = FIELD_DP32(u, ID_ISAR6, DP, 1);
        u = FIELD_DP32(u, ID_ISAR6, FHM, 1);
        u = FIELD_DP32(u, ID_ISAR6, SB, 1);
        u = FIELD_DP32(u, ID_ISAR6, SPECRES, 1);
        cpu->isar.id_isar6 = u;

        u = cpu->isar.id_mmfr3;
        u = FIELD_DP32(u, ID_MMFR3, PAN, 2); /* ATS1E1 */
        cpu->isar.id_mmfr3 = u;

        u = cpu->isar.id_mmfr4;
        u = FIELD_DP32(u, ID_MMFR4, HPDS, 1); /* AA32HPD */
        u = FIELD_DP32(u, ID_MMFR4, AC2, 1); /* ACTLR2, HACTLR2 */
        u = FIELD_DP32(u, ID_MMFR4, CNP, 1); /* TTCNP */
        cpu->isar.id_mmfr4 = u;

        u = cpu->isar.id_aa64dfr0;
        u = FIELD_DP64(u, ID_AA64DFR0, PMUVER, 5); /* v8.4-PMU */
        cpu->isar.id_aa64dfr0 = u;

        u = cpu->isar.id_dfr0;
        u = FIELD_DP32(u, ID_DFR0, PERFMON, 5); /* v8.4-PMU */
        cpu->isar.id_dfr0 = u;

        /*
         * FIXME: We do not yet support ARMv8.2-fp16 for AArch32 yet,
         * so do not set MVFR1.FPHP.  Strictly speaking this is not legal,
         * but it is also not legal to enable SVE without support for FP16,
         * and enabling SVE in system mode is more useful in the short term.
         */

#ifdef CONFIG_USER_ONLY
        /* For usermode -cpu max we can use a larger and more efficient DCZ
         * blocksize since we don't have to follow what the hardware does.
         */
        cpu->ctr = 0x80038003; /* 32 byte I and D cacheline size, VIPT icache */
        cpu->dcz_blocksize = 7; /*  512 bytes */
#endif
    }

    aarch64_add_sve_properties(obj);
    object_property_add(obj, "sve-max-vq", "uint32", cpu_max_get_sve_max_vq,
                        cpu_max_set_sve_max_vq, NULL, NULL, &error_fatal);
}

struct ARMCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
    void (*class_init)(ObjectClass *oc, void *data);
};

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a57",         .initfn = aarch64_a57_initfn },
    { .name = "cortex-a53",         .initfn = aarch64_a53_initfn },
    { .name = "cortex-a72",         .initfn = aarch64_a72_initfn },
    { .name = "max",                .initfn = aarch64_max_initfn },
    { .name = NULL }
};

static bool aarch64_cpu_get_aarch64(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    return arm_feature(&cpu->env, ARM_FEATURE_AARCH64);
}

static void aarch64_cpu_set_aarch64(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    /* At this time, this property is only allowed if KVM is enabled.  This
     * restriction allows us to avoid fixing up functionality that assumes a
     * uniform execution state like do_interrupt.
     */
    if (value == false) {
        if (!kvm_enabled() || !kvm_arm_aarch32_supported(CPU(cpu))) {
            error_setg(errp, "'aarch64' feature cannot be disabled "
                             "unless KVM is enabled and 32-bit EL1 "
                             "is supported");
            return;
        }
        unset_feature(&cpu->env, ARM_FEATURE_AARCH64);
    } else {
        set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    }
}

static void aarch64_cpu_initfn(Object *obj)
{
    object_property_add_bool(obj, "aarch64", aarch64_cpu_get_aarch64,
                             aarch64_cpu_set_aarch64, NULL);
    object_property_set_description(obj, "aarch64",
                                    "Set on/off to enable/disable aarch64 "
                                    "execution state ",
                                    NULL);
}

static void aarch64_cpu_finalizefn(Object *obj)
{
}

static gchar *aarch64_gdb_arch_name(CPUState *cs)
{
    return g_strdup("aarch64");
}

static void aarch64_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);

    cc->cpu_exec_interrupt = arm_cpu_exec_interrupt;
    cc->gdb_read_register = aarch64_cpu_gdb_read_register;
    cc->gdb_write_register = aarch64_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 34;
    cc->gdb_core_xml_file = "aarch64-core.xml";
    cc->gdb_arch_name = aarch64_gdb_arch_name;
}

static void aarch64_cpu_instance_init(Object *obj)
{
    ARMCPUClass *acc = ARM_CPU_GET_CLASS(obj);

    acc->info->initfn(obj);
    arm_cpu_post_init(obj);
}

static void cpu_register_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);

    acc->info = data;
}

static void aarch64_cpu_register(const ARMCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_AARCH64_CPU,
        .instance_size = sizeof(ARMCPU),
        .instance_init = aarch64_cpu_instance_init,
        .class_size = sizeof(ARMCPUClass),
        .class_init = info->class_init ?: cpu_register_class_init,
        .class_data = (void *)info,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_ARM_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo aarch64_cpu_type_info = {
    .name = TYPE_AARCH64_CPU,
    .parent = TYPE_ARM_CPU,
    .instance_size = sizeof(ARMCPU),
    .instance_init = aarch64_cpu_initfn,
    .instance_finalize = aarch64_cpu_finalizefn,
    .abstract = true,
    .class_size = sizeof(AArch64CPUClass),
    .class_init = aarch64_cpu_class_init,
};

static void aarch64_cpu_register_types(void)
{
    const ARMCPUInfo *info = aarch64_cpus;

    type_register_static(&aarch64_cpu_type_info);

    while (info->name) {
        aarch64_cpu_register(info);
        info++;
    }
}

type_init(aarch64_cpu_register_types)
