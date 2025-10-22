/*
 * QEMU x86 CPU <-> APIC
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qobject/qdict.h"
#include "qapi/error.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "system/hw_accel.h"
#include "system/kvm.h"
#include "system/xen.h"
#include "system/address-spaces.h"
#include "hw/qdev-properties.h"
#include "hw/i386/apic_internal.h"
#include "cpu-internal.h"

APICCommonClass *apic_get_class(Error **errp)
{
    const char *apic_type = "apic";

    /* TODO: in-kernel irqchip for hvf */
    if (kvm_enabled()) {
        if (!kvm_irqchip_in_kernel()) {
            error_setg(errp, "KVM does not support userspace APIC");
            return NULL;
        }
        apic_type = "kvm-apic";
    } else if (xen_enabled()) {
        apic_type = "xen-apic";
    } else if (whpx_apic_in_platform()) {
        apic_type = "whpx-apic";
    }

    return APIC_COMMON_CLASS(object_class_by_name(apic_type));
}

void x86_cpu_apic_create(X86CPU *cpu, Error **errp)
{
    APICCommonClass *apic_class = apic_get_class(errp);

    if (!apic_class) {
        return;
    }

    cpu->apic_state = APIC_COMMON(object_new_with_class(OBJECT_CLASS(apic_class)));
    object_property_add_child(OBJECT(cpu), "lapic",
                              OBJECT(cpu->apic_state));
    object_unref(OBJECT(cpu->apic_state));

    /* TODO: convert to link<> */
    cpu->apic_state->cpu = cpu;
    cpu->apic_state->apicbase = APIC_DEFAULT_ADDRESS | MSR_IA32_APICBASE_ENABLE;

    /*
     * apic_common_set_id needs to check if the CPU has x2APIC
     * feature in case APIC ID >= 255, so we need to set cpu->apic_state->cpu
     * before setting APIC ID
     */
    qdev_prop_set_uint32(DEVICE(cpu->apic_state), "id", cpu->apic_id);
}

void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
    static bool apic_mmio_map_once;

    if (cpu->apic_state == NULL) {
        return;
    }
    qdev_realize(DEVICE(cpu->apic_state), NULL, errp);

    /* Map APIC MMIO area */
    if (!apic_mmio_map_once) {
        memory_region_add_subregion_overlap(get_system_memory(),
                                            cpu->apic_state->apicbase &
                                            MSR_IA32_APICBASE_BASE,
                                            &cpu->apic_state->io_memory,
                                            0x1000);
        apic_mmio_map_once = true;
     }
}

void hmp_info_local_apic(Monitor *mon, const QDict *qdict)
{
    CPUState *cs;

    if (qdict_haskey(qdict, "apic-id")) {
        int id = qdict_get_try_int(qdict, "apic-id", 0);

        cs = cpu_by_arch_id(id);
        if (cs) {
            cpu_synchronize_state(cs);
        }
    } else {
        cs = mon_get_cpu(mon);
    }


    if (!cs) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    x86_cpu_dump_local_apic_state(cs, CPU_DUMP_FPU);
}
