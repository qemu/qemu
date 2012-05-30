/*
 *  PowerPC emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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

#include "cpu.h"
#include "helper_regs.h"
#include "kvm.h"
#include "kvm_ppc.h"
#include "cpus.h"

PowerPCCPU *cpu_ppc_init(const char *cpu_model)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;
    const ppc_def_t *def;

    def = cpu_ppc_find_by_name(cpu_model);
    if (!def) {
        return NULL;
    }

    cpu = POWERPC_CPU(object_new(TYPE_POWERPC_CPU));
    env = &cpu->env;

    if (tcg_enabled()) {
        ppc_translate_init();
    }

    env->cpu_model_str = cpu_model;
    cpu_ppc_register_internal(env, def);

    qemu_init_vcpu(env);

    return cpu;
}
