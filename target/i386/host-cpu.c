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
#include "sysemu/sysemu.h"

/* Note: Only safe for use on x86(-64) hosts */
static uint32_t host_cpu_phys_bits(void)
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

static void host_cpu_enable_cpu_pm(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    host_cpuid(5, 0, &cpu->mwait.eax, &cpu->mwait.ebx,
               &cpu->mwait.ecx, &cpu->mwait.edx);
    env->features[FEAT_1_ECX] |= CPUID_EXT_MONITOR;
}

static uint32_t host_cpu_adjust_phys_bits(X86CPU *cpu)
{
    uint32_t host_phys_bits = host_cpu_phys_bits();
    uint32_t phys_bits = cpu->phys_bits;
    static bool warned;

    /*
     * Print a warning if the user set it to a value that's not the
     * host value.
     */
    if (phys_bits != host_phys_bits && phys_bits != 0 &&
        !warned) {
        warn_report("Host physical bits (%u)"
                    " does not match phys-bits property (%u)",
                    host_phys_bits, phys_bits);
        warned = true;
    }

    if (cpu->host_phys_bits) {
        /* The user asked for us to use the host physical bits */
        phys_bits = host_phys_bits;
        if (cpu->host_phys_bits_limit &&
            phys_bits > cpu->host_phys_bits_limit) {
            phys_bits = cpu->host_phys_bits_limit;
        }
    }

    return phys_bits;
}

bool host_cpu_realizefn(CPUState *cs, Error **errp)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    if (cpu->max_features && enable_cpu_pm) {
        host_cpu_enable_cpu_pm(cpu);
    }
    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        uint32_t phys_bits = host_cpu_adjust_phys_bits(cpu);

        if (phys_bits &&
            (phys_bits > TARGET_PHYS_ADDR_SPACE_BITS ||
             phys_bits < 32)) {
            error_setg(errp, "phys-bits should be between 32 and %u "
                       " (but is %u)",
                       TARGET_PHYS_ADDR_SPACE_BITS, phys_bits);
            return false;
        }
        cpu->phys_bits = phys_bits;
    }
    return true;
}

#define CPUID_MODEL_ID_SZ 48
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

    host_cpuid(0x0, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_vendor_words2str(vendor, ebx, edx, ecx);

    host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
    if (family) {
        *family = ((eax >> 8) & 0x0F) + ((eax >> 20) & 0xFF);
    }
    if (model) {
        *model = ((eax >> 4) & 0x0F) | ((eax & 0xF0000) >> 12);
    }
    if (stepping) {
        *stepping = eax & 0x0F;
    }
}

void host_cpu_instance_init(X86CPU *cpu)
{
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);

    if (xcc->model) {
        uint32_t ebx = 0, ecx = 0, edx = 0;
        char vendor[CPUID_VENDOR_SZ + 1];

        host_cpuid(0, 0, NULL, &ebx, &ecx, &edx);
        x86_cpu_vendor_words2str(vendor, ebx, edx, ecx);
        object_property_set_str(OBJECT(cpu), "vendor", vendor, &error_abort);
    }
}

void host_cpu_max_instance_init(X86CPU *cpu)
{
    char vendor[CPUID_VENDOR_SZ + 1] = { 0 };
    char model_id[CPUID_MODEL_ID_SZ + 1] = { 0 };
    int family, model, stepping;

    /* Use max host physical address bits if -cpu max option is applied */
    object_property_set_bool(OBJECT(cpu), "host-phys-bits", true, &error_abort);

    host_cpu_vendor_fms(vendor, &family, &model, &stepping);
    host_cpu_fill_model_id(model_id);

    object_property_set_str(OBJECT(cpu), "vendor", vendor, &error_abort);
    object_property_set_int(OBJECT(cpu), "family", family, &error_abort);
    object_property_set_int(OBJECT(cpu), "model", model, &error_abort);
    object_property_set_int(OBJECT(cpu), "stepping", stepping,
                            &error_abort);
    object_property_set_str(OBJECT(cpu), "model-id", model_id,
                            &error_abort);
}

static void host_cpu_class_init(ObjectClass *oc, void *data)
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
