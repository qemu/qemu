/*
 * QEMU MIPS CPU
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
#ifndef QEMU_MIPS_CPU_QOM_H
#define QEMU_MIPS_CPU_QOM_H

#include "qom/cpu.h"

#ifdef TARGET_MIPS64
#define TYPE_MIPS_CPU "mips64-cpu"
#else
#define TYPE_MIPS_CPU "mips-cpu"
#endif

#define MIPS_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(MIPSCPUClass, (klass), TYPE_MIPS_CPU)
#define MIPS_CPU(obj) \
    OBJECT_CHECK(MIPSCPU, (obj), TYPE_MIPS_CPU)
#define MIPS_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MIPSCPUClass, (obj), TYPE_MIPS_CPU)

/**
 * MIPSCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A MIPS CPU model.
 */
typedef struct MIPSCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} MIPSCPUClass;

/**
 * MIPSCPU:
 * @env: #CPUMIPSState
 *
 * A MIPS CPU.
 */
typedef struct MIPSCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUMIPSState env;
} MIPSCPU;

static inline MIPSCPU *mips_env_get_cpu(CPUMIPSState *env)
{
    return container_of(env, MIPSCPU, env);
}

#define ENV_GET_CPU(e) CPU(mips_env_get_cpu(e))

#define ENV_OFFSET offsetof(MIPSCPU, env)

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vmstate_mips_cpu;
#endif

void mips_cpu_do_interrupt(CPUState *cpu);
bool mips_cpu_exec_interrupt(CPUState *cpu, int int_req);
void mips_cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                         int flags);
hwaddr mips_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int mips_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int mips_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
void mips_cpu_do_unaligned_access(CPUState *cpu, vaddr addr,
                                  int is_write, int is_user, uintptr_t retaddr);

#endif
