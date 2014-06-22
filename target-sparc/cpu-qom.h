/*
 * QEMU SPARC CPU
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
#ifndef QEMU_SPARC_CPU_QOM_H
#define QEMU_SPARC_CPU_QOM_H

#include "qom/cpu.h"
#include "cpu.h"

#ifdef TARGET_SPARC64
#define TYPE_SPARC_CPU "sparc64-cpu"
#else
#define TYPE_SPARC_CPU "sparc-cpu"
#endif

#define SPARC_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(SPARCCPUClass, (klass), TYPE_SPARC_CPU)
#define SPARC_CPU(obj) \
    OBJECT_CHECK(SPARCCPU, (obj), TYPE_SPARC_CPU)
#define SPARC_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SPARCCPUClass, (obj), TYPE_SPARC_CPU)

/**
 * SPARCCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A SPARC CPU model.
 */
typedef struct SPARCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} SPARCCPUClass;

/**
 * SPARCCPU:
 * @env: #CPUSPARCState
 *
 * A SPARC CPU.
 */
typedef struct SPARCCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUSPARCState env;
} SPARCCPU;

static inline SPARCCPU *sparc_env_get_cpu(CPUSPARCState *env)
{
    return container_of(env, SPARCCPU, env);
}

#define ENV_GET_CPU(e) CPU(sparc_env_get_cpu(e))

#define ENV_OFFSET offsetof(SPARCCPU, env)

void sparc_cpu_do_interrupt(CPUState *cpu);
void sparc_cpu_dump_state(CPUState *cpu, FILE *f,
                          fprintf_function cpu_fprintf, int flags);
hwaddr sparc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int sparc_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int sparc_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void QEMU_NORETURN sparc_cpu_do_unaligned_access(CPUState *cpu,
                                                 vaddr addr, int is_write,
                                                 int is_user, uintptr_t retaddr);

#endif
