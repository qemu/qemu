/*
 * x86 host CPU functions, and "host" cpu type initialization
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "host-cpu.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/system.h"

/* Note: Only safe for use on x86(-64) hosts */
uint32_t host_cpu_phys_bits(void)
{
    uint32_t eax;
    uint32_t host_phys_bits;

    host_cpuid(0x80000000, 0, &eax, NULL, NULL, NULL);
    if (eax >= 0x80000008) {
        host_cpuid(0x80000008, 0, &eax, NULL, NULL, NULL);
        /*
         * Note: According to AMD doc 25481 rev 2.34 they have a field
         * at 23:16 that can specify a maximum physical address bits for
         * the guest that can override this value; but I've not seen
         * anything with that set.
         */
        host_phys_bits = eax & 0xff;
    } else {
        /*
         * It's an odd 64 bit machine that doesn't have the leaf for
         * physical address bits; fall back to 36 that's most older
         * Intel.
         */
        host_phys_bits = 36;
    }

    return host_phys_bits;
}

static void host_cpu_adjust_phys_bits(X86CPU *cpu)
{
    uint32_t host_phys_bits = host_cpu_phys_bits();
    uint32_t phys_bits = cpu->phys_bits;

    /*
     * Print a warning if the user set it to a value that's not the
     * host value.
     */
    if (phys_bits != host_phys_bits && phys_bits != 0) {
        warn_report_once("Host physical bits (%u)"
                         " does not match phys-bits property (%u)",
                         host_phys_bits, phys_bits);
    }

    if (cpu->host_phys_bits) {
        /* The user asked for us to use the host physical bits */
        phys_bits = host_phys_bits;
        if (cpu->host_phys_bits_limit &&
            phys_bits > cpu->host_phys_bits_limit) {
            phys_bits = cpu->host_phys_bits_limit;
        }
    }

    cpu->phys_bits = phys_bits;
}

bool host_cpu_realizefn(CPUState *cs, Error **errp)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        host_cpu_adjust_phys_bits(cpu);
    }
    return true;
}

/**
 * cpu_x86_fill_model_id:
 * Get CPUID model ID string from host CPU.
 *
 * @str should have at least CPUID_MODEL_ID_SZ bytes
 *
 * The function does NOT add a null terminator to the string
 * automatically.
 */
static int host_cpu_fill_model_id(char *str)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    int i;

    for (i = 0; i < 3; i++) {
        host_cpuid(0x80000002 + i, 0, &eax, &ebx, &ecx, &edx);
        memcpy(str + i * 16 +  0, &eax, 4);
        memcpy(str + i * 16 +  4, &ebx, 4);
        memcpy(str + i * 16 +  8, &ecx, 4);
        memcpy(str + i * 16 + 12, &edx, 4);
    }
    return 0;
}

void host_cpu_vendor_fms(char *vendor, int *family, int *model, int *stepping)
{
    uint32_t eax, ebx, ecx, edx;

    host_cpuid(0x0, 0, NULL, &ebx, &ecx, &edx);
    x86_cpu_vendor_words2str(vendor, ebx, edx, ecx);

    if (!family && !model && !stepping) {
        return;
    }

    host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
    if (family) {
        *family = x86_cpu_family(eax);
    }
    if (model) {
        *model = x86_cpu_model(eax);
    }
    if (stepping) {
        *stepping = x86_cpu_stepping(eax);
    }
}

void host_cpu_instance_init(X86CPU *cpu)
{
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);

    char vendor[CPUID_VENDOR_SZ + 1] = { 0 };
    char model_id[CPUID_MODEL_ID_SZ + 1] = { 0 };
    int family, model, stepping;

    /*
     * setting vendor applies to both max/host and builtin_x86_defs CPU.
     * FIXME: this probably should warn or should be skipped if vendors do
     * not match, because family numbers are incompatible between Intel and AMD.
     */
    host_cpu_vendor_fms(vendor, &family, &model, &stepping);
    object_property_set_str(OBJECT(cpu), "vendor", vendor, &error_abort);

    if (!xcc->max_features) {
        return;
    }

    host_cpu_fill_model_id(model_id);

    /* Use max host physical address bits if -cpu max option is applied */
    object_property_set_bool(OBJECT(cpu), "host-phys-bits", true, &error_abort);

    object_property_set_int(OBJECT(cpu), "family", family, &error_abort);
    object_property_set_int(OBJECT(cpu), "model", model, &error_abort);
    object_property_set_int(OBJECT(cpu), "stepping", stepping,
                            &error_abort);
    object_property_set_str(OBJECT(cpu), "model-id", model_id,
                            &error_abort);
}

bool is_host_cpu_intel(void)
{
    char vendor[CPUID_VENDOR_SZ + 1];

    host_cpu_vendor_fms(vendor, NULL, NULL, NULL);

    return g_str_equal(vendor, CPUID_VENDOR_INTEL);
}

static void host_cpu_class_init(ObjectClass *oc, const void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->host_cpuid_required = true;
    xcc->ordering = 8;
    xcc->model_description =
        g_strdup_printf("processor with all supported host features ");
}

static const TypeInfo host_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("host"),
    .parent = X86_CPU_TYPE_NAME("max"),
    .class_init = host_cpu_class_init,
};

static void host_cpu_type_init(void)
{
    type_register_static(&host_cpu_type_info);
}

type_init(host_cpu_type_init);
