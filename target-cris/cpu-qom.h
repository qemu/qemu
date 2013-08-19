/*
 * QEMU CRIS CPU
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
#ifndef QEMU_CRIS_CPU_QOM_H
#define QEMU_CRIS_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_CRIS_CPU "cris-cpu"

#define CRIS_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(CRISCPUClass, (klass), TYPE_CRIS_CPU)
#define CRIS_CPU(obj) \
    OBJECT_CHECK(CRISCPU, (obj), TYPE_CRIS_CPU)
#define CRIS_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(CRISCPUClass, (obj), TYPE_CRIS_CPU)

/**
 * CRISCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 * @vr: Version Register value.
 *
 * A CRIS CPU model.
 */
typedef struct CRISCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);

    uint32_t vr;
} CRISCPUClass;

/**
 * CRISCPU:
 * @env: #CPUCRISState
 *
 * A CRIS CPU.
 */
typedef struct CRISCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUCRISState env;
} CRISCPU;

static inline CRISCPU *cris_env_get_cpu(CPUCRISState *env)
{
    return container_of(env, CRISCPU, env);
}

#define ENV_GET_CPU(e) CPU(cris_env_get_cpu(e))

#define ENV_OFFSET offsetof(CRISCPU, env)

void cris_cpu_do_interrupt(CPUState *cpu);
void crisv10_cpu_do_interrupt(CPUState *cpu);

void cris_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags);

hwaddr cris_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

int crisv10_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int cris_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int cris_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);

#endif
