/*
 * QEMU x86 CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef QEMU_I386_CPU_QOM_H
#define QEMU_I386_CPU_QOM_H

#include "qom/cpu.h"
#include "cpu.h"
#include "qapi/error.h"

#ifdef TARGET_X86_64
#define TYPE_X86_CPU "x86_64-cpu"
#else
#define TYPE_X86_CPU "i386-cpu"
#endif

#define X86_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(X86CPUClass, (klass), TYPE_X86_CPU)
#define X86_CPU(obj) \
    OBJECT_CHECK(X86CPU, (obj), TYPE_X86_CPU)
#define X86_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(X86CPUClass, (obj), TYPE_X86_CPU)

/**
 * X86CPUDefinition:
 *
 * CPU model definition data that was not converted to QOM per-subclass
 * property defaults yet.
 */
typedef struct X86CPUDefinition X86CPUDefinition;

/**
 * X86CPUClass:
 * @cpu_def: CPU model definition
 * @kvm_required: Whether CPU model requires KVM to be enabled.
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * An x86 CPU model or family.
 */
typedef struct X86CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    /* Should be eventually replaced by subclass-specific property defaults. */
    X86CPUDefinition *cpu_def;

    bool kvm_required;

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} X86CPUClass;

/**
 * X86CPU:
 * @env: #CPUX86State
 * @migratable: If set, only migratable flags will be accepted when "enforce"
 * mode is used, and only migratable flags will be included in the "host"
 * CPU model.
 *
 * An x86 CPU.
 */
typedef struct X86CPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUX86State env;

    bool hyperv_vapic;
    bool hyperv_relaxed_timing;
    int hyperv_spinlock_attempts;
    bool hyperv_time;
    bool check_cpuid;
    bool enforce_cpuid;
    bool expose_kvm;
    bool migratable;

    /* if true the CPUID code directly forward host cache leaves to the guest */
    bool cache_info_passthrough;

    /* Features that were filtered out because of missing host capabilities */
    uint32_t filtered_features[FEATURE_WORDS];

    /* Enable PMU CPUID bits. This can't be enabled by default yet because
     * it doesn't have ABI stability guarantees, as it passes all PMU CPUID
     * bits returned by GET_SUPPORTED_CPUID (that depend on host CPU and kernel
     * capabilities) directly to the guest.
     */
    bool enable_pmu;

    /* in order to simplify APIC support, we leave this pointer to the
       user */
    struct DeviceState *apic_state;
} X86CPU;

static inline X86CPU *x86_env_get_cpu(CPUX86State *env)
{
    return container_of(env, X86CPU, env);
}

#define ENV_GET_CPU(e) CPU(x86_env_get_cpu(e))

#define ENV_OFFSET offsetof(X86CPU, env)

#ifndef CONFIG_USER_ONLY
extern struct VMStateDescription vmstate_x86_cpu;
#endif

/**
 * x86_cpu_do_interrupt:
 * @cpu: vCPU the interrupt is to be handled by.
 */
void x86_cpu_do_interrupt(CPUState *cpu);

int x86_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cpu,
                             int cpuid, void *opaque);
int x86_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                             int cpuid, void *opaque);
int x86_cpu_write_elf64_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                                 void *opaque);
int x86_cpu_write_elf32_qemunote(WriteCoreDumpFunction f, CPUState *cpu,
                                 void *opaque);

void x86_cpu_get_memory_mapping(CPUState *cpu, MemoryMappingList *list,
                                Error **errp);

void x86_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                        int flags);

hwaddr x86_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

int x86_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int x86_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

#endif
