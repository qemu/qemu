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
#include "cpregs.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sysemu/hvf.h"
#include "sysemu/qtest.h"
#include "sysemu/tcg.h"
#include "kvm_arm.h"
#include "hvf_arm.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"

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
    uint32_t vq_map = cpu->sve_vq.map;
    uint32_t vq_init = cpu->sve_vq.init;
    uint32_t vq_supported;
    uint32_t vq_mask = 0;
    uint32_t tmp, vq, max_vq = 0;

    /*
     * CPU models specify a set of supported vector lengths which are
     * enabled by default.  Attempting to enable any vector length not set
     * in the supported bitmap results in an error.  When KVM is enabled we
     * fetch the supported bitmap from the host.
     */
    if (kvm_enabled()) {
        if (kvm_arm_sve_supported()) {
            cpu->sve_vq.supported = kvm_arm_sve_get_vls(cpu);
            vq_supported = cpu->sve_vq.supported;
        } else {
            assert(!cpu_isar_feature(aa64_sve, cpu));
            vq_supported = 0;
        }
    } else {
        vq_supported = cpu->sve_vq.supported;
    }

    /*
     * Process explicit sve<N> properties.
     * From the properties, sve_vq_map<N> implies sve_vq_init<N>.
     * Check first for any sve<N> enabled.
     */
    if (vq_map != 0) {
        max_vq = 32 - clz32(vq_map);
        vq_mask = MAKE_64BIT_MASK(0, max_vq);

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
             * For KVM we have to automatically enable all supported uninitialized
             * lengths, even when the smaller lengths are not all powers-of-two.
             */
            vq_map |= vq_supported & ~vq_init & vq_mask;
        } else {
            /* Propagate enabled bits down through required powers-of-two. */
            vq_map |= SVE_VQ_POW2_MAP & ~vq_init & vq_mask;
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
            tmp = vq_init & vq_supported;
        } else {
            /* Disabling a power-of-two disables all larger lengths. */
            tmp = vq_init & SVE_VQ_POW2_MAP;
        }
        vq = ctz32(tmp) + 1;

        max_vq = vq <= ARM_MAX_VQ ? vq - 1 : ARM_MAX_VQ;
        vq_mask = max_vq > 0 ? MAKE_64BIT_MASK(0, max_vq) : 0;
        vq_map = vq_supported & ~vq_init & vq_mask;

        if (vq_map == 0) {
            error_setg(errp, "cannot disable sve%d", vq * 128);
            error_append_hint(errp, "Disabling sve%d results in all "
                              "vector lengths being disabled.\n",
                              vq * 128);
            error_append_hint(errp, "With SVE enabled, at least one "
                              "vector length must be enabled.\n");
            return;
        }

        max_vq = 32 - clz32(vq_map);
        vq_mask = MAKE_64BIT_MASK(0, max_vq);
    }

    /*
     * Process the sve-max-vq property.
     * Note that we know from the above that no bit above
     * sve-max-vq is currently set.
     */
    if (cpu->sve_max_vq != 0) {
        max_vq = cpu->sve_max_vq;
        vq_mask = MAKE_64BIT_MASK(0, max_vq);

        if (vq_init & ~vq_map & (1 << (max_vq - 1))) {
            error_setg(errp, "cannot disable sve%d", max_vq * 128);
            error_append_hint(errp, "The maximum vector length must be "
                              "enabled, sve-max-vq=%d (%d bits)\n",
                              max_vq, max_vq * 128);
            return;
        }

        /* Set all bits not explicitly set within sve-max-vq. */
        vq_map |= ~vq_init & vq_mask;
    }

    /*
     * We should know what max-vq is now.  Also, as we're done
     * manipulating sve-vq-map, we ensure any bits above max-vq
     * are clear, just in case anybody looks.
     */
    assert(max_vq != 0);
    assert(vq_mask != 0);
    vq_map &= vq_mask;

    /* Ensure the set of lengths matches what is supported. */
    tmp = vq_map ^ (vq_supported & vq_mask);
    if (tmp) {
        vq = 32 - clz32(tmp);
        if (vq_map & (1 << (vq - 1))) {
            if (cpu->sve_max_vq) {
                error_setg(errp, "cannot set sve-max-vq=%d", cpu->sve_max_vq);
                error_append_hint(errp, "This CPU does not support "
                                  "the vector length %d-bits.\n", vq * 128);
                error_append_hint(errp, "It may not be possible to use "
                                  "sve-max-vq with this CPU. Try "
                                  "using only sve<N> properties.\n");
            } else {
                error_setg(errp, "cannot enable sve%d", vq * 128);
                if (vq_supported) {
                    error_append_hint(errp, "This CPU does not support "
                                      "the vector length %d-bits.\n", vq * 128);
                } else {
                    error_append_hint(errp, "SVE not supported by KVM "
                                      "on this host\n");
                }
            }
            return;
        } else {
            if (kvm_enabled()) {
                error_setg(errp, "cannot disable sve%d", vq * 128);
                error_append_hint(errp, "The KVM host requires all "
                                  "supported vector lengths smaller "
                                  "than %d bits to also be enabled.\n",
                                  max_vq * 128);
                return;
            } else {
                /* Ensure all required powers-of-two are enabled. */
                tmp = SVE_VQ_POW2_MAP & vq_mask & ~vq_map;
                if (tmp) {
                    vq = 32 - clz32(tmp);
                    error_setg(errp, "cannot disable sve%d", vq * 128);
                    error_append_hint(errp, "sve%d is required as it "
                                      "is a power-of-two length smaller "
                                      "than the maximum, sve%d\n",
                                      vq * 128, max_vq * 128);
                    return;
                }
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
    cpu->sve_vq.map = vq_map;
}

/*
 * Note that cpu_arm_{get,set}_vq cannot use the simpler
 * object_property_add_bool interface because they make use of the
 * contents of "name" to determine which bit on which to operate.
 */
static void cpu_arm_get_vq(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMVQMap *vq_map = opaque;
    uint32_t vq = atoi(&name[3]) / 128;
    bool sve = vq_map == &cpu->sve_vq;
    bool value;

    /* All vector lengths are disabled when feature is off. */
    if (sve
        ? !cpu_isar_feature(aa64_sve, cpu)
        : !cpu_isar_feature(aa64_sme, cpu)) {
        value = false;
    } else {
        value = extract32(vq_map->map, vq - 1, 1);
    }
    visit_type_bool(v, name, &value, errp);
}

static void cpu_arm_set_vq(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    ARMVQMap *vq_map = opaque;
    uint32_t vq = atoi(&name[3]) / 128;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    vq_map->map = deposit32(vq_map->map, vq - 1, 1, value);
    vq_map->init |= 1 << (vq - 1);
}

static bool cpu_arm_get_sve(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    return cpu_isar_feature(aa64_sve, cpu);
}

static void cpu_arm_set_sve(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint64_t t;

    if (value && kvm_enabled() && !kvm_arm_sve_supported()) {
        error_setg(errp, "'sve' feature not supported by KVM on this host");
        return;
    }

    t = cpu->isar.id_aa64pfr0;
    t = FIELD_DP64(t, ID_AA64PFR0, SVE, value);
    cpu->isar.id_aa64pfr0 = t;
}

void arm_cpu_sme_finalize(ARMCPU *cpu, Error **errp)
{
    uint32_t vq_map = cpu->sme_vq.map;
    uint32_t vq_init = cpu->sme_vq.init;
    uint32_t vq_supported = cpu->sme_vq.supported;
    uint32_t vq;

    if (vq_map == 0) {
        if (!cpu_isar_feature(aa64_sme, cpu)) {
            cpu->isar.id_aa64smfr0 = 0;
            return;
        }

        /* TODO: KVM will require limitations via SMCR_EL2. */
        vq_map = vq_supported & ~vq_init;

        if (vq_map == 0) {
            vq = ctz32(vq_supported) + 1;
            error_setg(errp, "cannot disable sme%d", vq * 128);
            error_append_hint(errp, "All SME vector lengths are disabled.\n");
            error_append_hint(errp, "With SME enabled, at least one "
                              "vector length must be enabled.\n");
            return;
        }
    } else {
        if (!cpu_isar_feature(aa64_sme, cpu)) {
            vq = 32 - clz32(vq_map);
            error_setg(errp, "cannot enable sme%d", vq * 128);
            error_append_hint(errp, "SME must be enabled to enable "
                              "vector lengths.\n");
            error_append_hint(errp, "Add sme=on to the CPU property list.\n");
            return;
        }
        /* TODO: KVM will require limitations via SMCR_EL2. */
    }

    cpu->sme_vq.map = vq_map;
}

static bool cpu_arm_get_sme(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    return cpu_isar_feature(aa64_sme, cpu);
}

static void cpu_arm_set_sme(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint64_t t;

    t = cpu->isar.id_aa64pfr1;
    t = FIELD_DP64(t, ID_AA64PFR1, SME, value);
    cpu->isar.id_aa64pfr1 = t;
}

static bool cpu_arm_get_sme_fa64(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    return cpu_isar_feature(aa64_sme, cpu) &&
           cpu_isar_feature(aa64_sme_fa64, cpu);
}

static void cpu_arm_set_sme_fa64(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint64_t t;

    t = cpu->isar.id_aa64smfr0;
    t = FIELD_DP64(t, ID_AA64SMFR0, FA64, value);
    cpu->isar.id_aa64smfr0 = t;
}

#ifdef CONFIG_USER_ONLY
/* Mirror linux /proc/sys/abi/{sve,sme}_default_vector_length. */
static void cpu_arm_set_default_vec_len(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    uint32_t *ptr_default_vq = opaque;
    int32_t default_len, default_vq, remainder;

    if (!visit_type_int32(v, name, &default_len, errp)) {
        return;
    }

    /* Undocumented, but the kernel allows -1 to indicate "maximum". */
    if (default_len == -1) {
        *ptr_default_vq = ARM_MAX_VQ;
        return;
    }

    default_vq = default_len / 16;
    remainder = default_len % 16;

    /*
     * Note that the 512 max comes from include/uapi/asm/sve_context.h
     * and is the maximum architectural width of ZCR_ELx.LEN.
     */
    if (remainder || default_vq < 1 || default_vq > 512) {
        ARMCPU *cpu = ARM_CPU(obj);
        const char *which =
            (ptr_default_vq == &cpu->sve_default_vq ? "sve" : "sme");

        error_setg(errp, "cannot set %s-default-vector-length", which);
        if (remainder) {
            error_append_hint(errp, "Vector length not a multiple of 16\n");
        } else if (default_vq < 1) {
            error_append_hint(errp, "Vector length smaller than 16\n");
        } else {
            error_append_hint(errp, "Vector length larger than %d\n",
                              512 * 16);
        }
        return;
    }

    *ptr_default_vq = default_vq;
}

static void cpu_arm_get_default_vec_len(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    uint32_t *ptr_default_vq = opaque;
    int32_t value = *ptr_default_vq * 16;

    visit_type_int32(v, name, &value, errp);
}
#endif

void aarch64_add_sve_properties(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t vq;

    object_property_add_bool(obj, "sve", cpu_arm_get_sve, cpu_arm_set_sve);

    for (vq = 1; vq <= ARM_MAX_VQ; ++vq) {
        char name[8];
        sprintf(name, "sve%d", vq * 128);
        object_property_add(obj, name, "bool", cpu_arm_get_vq,
                            cpu_arm_set_vq, NULL, &cpu->sve_vq);
    }

#ifdef CONFIG_USER_ONLY
    /* Mirror linux /proc/sys/abi/sve_default_vector_length. */
    object_property_add(obj, "sve-default-vector-length", "int32",
                        cpu_arm_get_default_vec_len,
                        cpu_arm_set_default_vec_len, NULL,
                        &cpu->sve_default_vq);
#endif
}

void aarch64_add_sme_properties(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    uint32_t vq;

    object_property_add_bool(obj, "sme", cpu_arm_get_sme, cpu_arm_set_sme);
    object_property_add_bool(obj, "sme_fa64", cpu_arm_get_sme_fa64,
                             cpu_arm_set_sme_fa64);

    for (vq = 1; vq <= ARM_MAX_VQ; vq <<= 1) {
        char name[8];
        sprintf(name, "sme%d", vq * 128);
        object_property_add(obj, name, "bool", cpu_arm_get_vq,
                            cpu_arm_set_vq, NULL, &cpu->sme_vq);
    }

#ifdef CONFIG_USER_ONLY
    /* Mirror linux /proc/sys/abi/sme_default_vector_length. */
    object_property_add(obj, "sme-default-vector-length", "int32",
                        cpu_arm_get_default_vec_len,
                        cpu_arm_set_default_vec_len, NULL,
                        &cpu->sme_default_vq);
#endif
}

void arm_cpu_pauth_finalize(ARMCPU *cpu, Error **errp)
{
    ARMPauthFeature features = cpu_isar_feature(pauth_feature, cpu);
    uint64_t isar1, isar2;

    /*
     * These properties enable or disable Pauth as a whole, or change
     * the pauth algorithm, but do not change the set of features that
     * are present.  We have saved a copy of those features above and
     * will now place it into the field that chooses the algorithm.
     *
     * Begin by disabling all fields.
     */
    isar1 = cpu->isar.id_aa64isar1;
    isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, APA, 0);
    isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, GPA, 0);
    isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, API, 0);
    isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, GPI, 0);

    isar2 = cpu->isar.id_aa64isar2;
    isar2 = FIELD_DP64(isar2, ID_AA64ISAR2, APA3, 0);
    isar2 = FIELD_DP64(isar2, ID_AA64ISAR2, GPA3, 0);

    if (kvm_enabled() || hvf_enabled()) {
        /*
         * Exit early if PAuth is enabled and fall through to disable it.
         * The algorithm selection properties are not present.
         */
        if (cpu->prop_pauth) {
            if (features == 0) {
                error_setg(errp, "'pauth' feature not supported by "
                           "%s on this host", current_accel_name());
            }
            return;
        }
    } else {
        /* Pauth properties are only present when the model supports it. */
        if (features == 0) {
            assert(!cpu->prop_pauth);
            return;
        }

        if (cpu->prop_pauth) {
            if (cpu->prop_pauth_impdef && cpu->prop_pauth_qarma3) {
                error_setg(errp,
                           "cannot enable both pauth-impdef and pauth-qarma3");
                return;
            }

            if (cpu->prop_pauth_impdef) {
                isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, API, features);
                isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, GPI, 1);
            } else if (cpu->prop_pauth_qarma3) {
                isar2 = FIELD_DP64(isar2, ID_AA64ISAR2, APA3, features);
                isar2 = FIELD_DP64(isar2, ID_AA64ISAR2, GPA3, 1);
            } else {
                isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, APA, features);
                isar1 = FIELD_DP64(isar1, ID_AA64ISAR1, GPA, 1);
            }
        } else if (cpu->prop_pauth_impdef || cpu->prop_pauth_qarma3) {
            error_setg(errp, "cannot enable pauth-impdef or "
                       "pauth-qarma3 without pauth");
            error_append_hint(errp, "Add pauth=on to the CPU property list.\n");
        }
    }

    cpu->isar.id_aa64isar1 = isar1;
    cpu->isar.id_aa64isar2 = isar2;
}

static Property arm_cpu_pauth_property =
    DEFINE_PROP_BOOL("pauth", ARMCPU, prop_pauth, true);
static Property arm_cpu_pauth_impdef_property =
    DEFINE_PROP_BOOL("pauth-impdef", ARMCPU, prop_pauth_impdef, false);
static Property arm_cpu_pauth_qarma3_property =
    DEFINE_PROP_BOOL("pauth-qarma3", ARMCPU, prop_pauth_qarma3, false);

void aarch64_add_pauth_properties(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    /* Default to PAUTH on, with the architected algorithm on TCG. */
    qdev_property_add_static(DEVICE(obj), &arm_cpu_pauth_property);
    if (kvm_enabled() || hvf_enabled()) {
        /*
         * Mirror PAuth support from the probed sysregs back into the
         * property for KVM or hvf. Is it just a bit backward? Yes it is!
         * Note that prop_pauth is true whether the host CPU supports the
         * architected QARMA5 algorithm or the IMPDEF one. We don't
         * provide the separate pauth-impdef property for KVM or hvf,
         * only for TCG.
         */
        cpu->prop_pauth = cpu_isar_feature(aa64_pauth, cpu);
    } else {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_pauth_impdef_property);
        qdev_property_add_static(DEVICE(obj), &arm_cpu_pauth_qarma3_property);
    }
}

void arm_cpu_lpa2_finalize(ARMCPU *cpu, Error **errp)
{
    uint64_t t;

    /*
     * We only install the property for tcg -cpu max; this is the
     * only situation in which the cpu field can be true.
     */
    if (!cpu->prop_lpa2) {
        return;
    }

    t = cpu->isar.id_aa64mmfr0;
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN16, 2);   /* 16k pages w/ LPA2 */
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN4, 1);    /*  4k pages w/ LPA2 */
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN16_2, 3); /* 16k stage2 w/ LPA2 */
    t = FIELD_DP64(t, ID_AA64MMFR0, TGRAN4_2, 3);  /*  4k stage2 w/ LPA2 */
    cpu->isar.id_aa64mmfr0 = t;
}

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
    cpu->isar.id_pfr0 = 0x00000131;
    cpu->isar.id_pfr1 = 0x00011011;
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
    cpu->isar.dbgdevid = 0x01110f13;
    cpu->isar.dbgdevid1 = 0x2;
    cpu->isar.reset_pmcr_el0 = 0x41013000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
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
    cpu->revidr = 0x00000100;
    cpu->reset_fpsid = 0x41034070;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x84448004; /* L1Ip = VIPT */
    cpu->reset_sctlr = 0x00c50838;
    cpu->isar.id_pfr0 = 0x00000131;
    cpu->isar.id_pfr1 = 0x00011011;
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
    cpu->isar.dbgdevid = 0x00110f13;
    cpu->isar.dbgdevid1 = 0x1;
    cpu->isar.reset_pmcr_el0 = 0x41033000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1024KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    cpu->gic_pribits = 5;
    define_cortex_a72_a57_a53_cp_reginfo(cpu);
}

static void aarch64_host_initfn(Object *obj)
{
#if defined(CONFIG_KVM)
    ARMCPU *cpu = ARM_CPU(obj);
    kvm_arm_set_cpu_features_from_host(cpu);
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        aarch64_add_sve_properties(obj);
        aarch64_add_pauth_properties(obj);
    }
#elif defined(CONFIG_HVF)
    ARMCPU *cpu = ARM_CPU(obj);
    hvf_arm_set_cpu_features_from_host(cpu);
    aarch64_add_pauth_properties(obj);
#else
    g_assert_not_reached();
#endif
}

static void aarch64_max_initfn(Object *obj)
{
    if (kvm_enabled() || hvf_enabled()) {
        /* With KVM or HVF, '-cpu max' is identical to '-cpu host' */
        aarch64_host_initfn(obj);
        return;
    }

    if (tcg_enabled() || qtest_enabled()) {
        aarch64_a57_initfn(obj);
    }

    /* '-cpu max' for TCG: we currently do this as "A57 with extra things" */
    if (tcg_enabled()) {
        aarch64_max_tcg_initfn(obj);
    }
}

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a57",         .initfn = aarch64_a57_initfn },
    { .name = "cortex-a53",         .initfn = aarch64_a53_initfn },
    { .name = "max",                .initfn = aarch64_max_initfn },
#if defined(CONFIG_KVM) || defined(CONFIG_HVF)
    { .name = "host",               .initfn = aarch64_host_initfn },
#endif
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
        if (!kvm_enabled() || !kvm_arm_aarch32_supported()) {
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

static void aarch64_cpu_finalizefn(Object *obj)
{
}

static const gchar *aarch64_gdb_arch_name(CPUState *cs)
{
    return "aarch64";
}

static void aarch64_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);

    cc->gdb_read_register = aarch64_cpu_gdb_read_register;
    cc->gdb_write_register = aarch64_cpu_gdb_write_register;
    cc->gdb_core_xml_file = "aarch64-core.xml";
    cc->gdb_arch_name = aarch64_gdb_arch_name;

    object_class_property_add_bool(oc, "aarch64", aarch64_cpu_get_aarch64,
                                   aarch64_cpu_set_aarch64);
    object_class_property_set_description(oc, "aarch64",
                                          "Set on/off to enable/disable aarch64 "
                                          "execution state ");
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

void aarch64_cpu_register(const ARMCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_AARCH64_CPU,
        .instance_init = aarch64_cpu_instance_init,
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
    .instance_finalize = aarch64_cpu_finalizefn,
    .abstract = true,
    .class_init = aarch64_cpu_class_init,
};

static void aarch64_cpu_register_types(void)
{
    size_t i;

    type_register_static(&aarch64_cpu_type_info);

    for (i = 0; i < ARRAY_SIZE(aarch64_cpus); ++i) {
        aarch64_cpu_register(&aarch64_cpus[i]);
    }
}

type_init(aarch64_cpu_register_types)
