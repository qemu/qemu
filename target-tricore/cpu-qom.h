/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_TRICORE_CPU_QOM_H
#define QEMU_TRICORE_CPU_QOM_H

#include "qom/cpu.h"


#define TYPE_TRICORE_CPU "tricore-cpu"

#define TRICORE_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(TriCoreCPUClass, (klass), TYPE_TRICORE_CPU)
#define TRICORE_CPU(obj) \
    OBJECT_CHECK(TriCoreCPU, (obj), TYPE_TRICORE_CPU)
#define TRICORE_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(TriCoreCPUClass, (obj), TYPE_TRICORE_CPU)

typedef struct TriCoreCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} TriCoreCPUClass;

/**
 * TriCoreCPU:
 * @env: #CPUTriCoreState
 *
 * A TriCore CPU.
 */
typedef struct TriCoreCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUTriCoreState env;
} TriCoreCPU;

static inline TriCoreCPU *tricore_env_get_cpu(CPUTriCoreState *env)
{
    return TRICORE_CPU(container_of(env, TriCoreCPU, env));
}

#define ENV_GET_CPU(e) CPU(tricore_env_get_cpu(e))

#define ENV_OFFSET offsetof(TriCoreCPU, env)

hwaddr tricore_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
void tricore_cpu_do_interrupt(CPUState *cpu);
void tricore_cpu_dump_state(CPUState *cpu, FILE *f,
                            fprintf_function cpu_fprintf, int flags);


#endif /*QEMU_TRICORE_CPU_QOM_H */
