/*
 * QEMU S/390 CPU
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
#ifndef QEMU_S390_CPU_QOM_H
#define QEMU_S390_CPU_QOM_H

#include "qom/cpu.h"
#include "cpu.h"

#define TYPE_S390_CPU "s390-cpu"

#define S390_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390CPUClass, (klass), TYPE_S390_CPU)
#define S390_CPU(obj) \
    OBJECT_CHECK(S390CPU, (obj), TYPE_S390_CPU)
#define S390_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390CPUClass, (obj), TYPE_S390_CPU)

/**
 * S390CPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 * @load_normal: Performs a load normal.
 * @cpu_reset: Performs a CPU reset.
 * @initial_cpu_reset: Performs an initial CPU reset.
 *
 * An S/390 CPU model.
 */
typedef struct S390CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
    void (*load_normal)(CPUState *cpu);
    void (*cpu_reset)(CPUState *cpu);
    void (*initial_cpu_reset)(CPUState *cpu);
} S390CPUClass;

/**
 * S390CPU:
 * @env: #CPUS390XState.
 *
 * An S/390 CPU.
 */
typedef struct S390CPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUS390XState env;
} S390CPU;

static inline S390CPU *s390_env_get_cpu(CPUS390XState *env)
{
    return container_of(env, S390CPU, env);
}

#define ENV_GET_CPU(e) CPU(s390_env_get_cpu(e))

#define ENV_OFFSET offsetof(S390CPU, env)

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vmstate_s390_cpu;
#endif

void s390_cpu_do_interrupt(CPUState *cpu);
bool s390_cpu_exec_interrupt(CPUState *cpu, int int_req);
void s390_cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                         int flags);
int s390_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                              int cpuid, void *opaque);

int s390_cpu_write_elf64_qemunote(WriteCoreDumpFunction f,
                                  CPUState *cpu, void *opaque);
hwaddr s390_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
hwaddr s390_cpu_get_phys_addr_debug(CPUState *cpu, vaddr addr);
int s390_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int s390_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void s390_cpu_gdb_init(CPUState *cs);

#endif
