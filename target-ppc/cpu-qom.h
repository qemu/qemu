/*
 * QEMU PowerPC CPU
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
#ifndef QEMU_PPC_CPU_QOM_H
#define QEMU_PPC_CPU_QOM_H

#include "qom/cpu.h"

#ifdef TARGET_PPC64
#define TYPE_POWERPC_CPU "powerpc64-cpu"
#elif defined(TARGET_PPCEMB)
#define TYPE_POWERPC_CPU "embedded-powerpc-cpu"
#else
#define TYPE_POWERPC_CPU "powerpc-cpu"
#endif

#define POWERPC_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(PowerPCCPUClass, (klass), TYPE_POWERPC_CPU)
#define POWERPC_CPU(obj) \
    OBJECT_CHECK(PowerPCCPU, (obj), TYPE_POWERPC_CPU)
#define POWERPC_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(PowerPCCPUClass, (obj), TYPE_POWERPC_CPU)

typedef struct PowerPCCPU PowerPCCPU;

/**
 * PowerPCCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A PowerPC CPU model.
 */
typedef struct PowerPCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);

    uint32_t pvr;
    bool (*pvr_match)(struct PowerPCCPUClass *pcc, uint32_t pvr);
    uint64_t pcr_mask;
    uint32_t svr;
    uint64_t insns_flags;
    uint64_t insns_flags2;
    uint64_t msr_mask;
    powerpc_mmu_t   mmu_model;
    powerpc_excp_t  excp_model;
    powerpc_input_t bus_model;
    uint32_t flags;
    int bfd_mach;
    uint32_t l1_dcache_size, l1_icache_size;
    const struct ppc_segment_page_sizes *sps;
    void (*init_proc)(CPUPPCState *env);
    int  (*check_pow)(CPUPPCState *env);
    int (*handle_mmu_fault)(PowerPCCPU *cpu, vaddr eaddr, int rwx, int mmu_idx);
    bool (*interrupts_big_endian)(PowerPCCPU *cpu);
} PowerPCCPUClass;

/**
 * PowerPCCPU:
 * @env: #CPUPPCState
 * @cpu_dt_id: CPU index used in the device tree. KVM uses this index too
 * @max_compat: Maximal supported logical PVR from the command line
 * @cpu_version: Current logical PVR, zero if in "raw" mode
 *
 * A PowerPC CPU.
 */
struct PowerPCCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUPPCState env;
    int cpu_dt_id;
    uint32_t max_compat;
    uint32_t cpu_version;
};

static inline PowerPCCPU *ppc_env_get_cpu(CPUPPCState *env)
{
    return container_of(env, PowerPCCPU, env);
}

#define ENV_GET_CPU(e) CPU(ppc_env_get_cpu(e))

#define ENV_OFFSET offsetof(PowerPCCPU, env)

PowerPCCPUClass *ppc_cpu_class_by_pvr(uint32_t pvr);
PowerPCCPUClass *ppc_cpu_class_by_pvr_mask(uint32_t pvr);

void ppc_cpu_do_interrupt(CPUState *cpu);
bool ppc_cpu_exec_interrupt(CPUState *cpu, int int_req);
void ppc_cpu_dump_state(CPUState *cpu, FILE *f, fprintf_function cpu_fprintf,
                        int flags);
void ppc_cpu_dump_statistics(CPUState *cpu, FILE *f,
                             fprintf_function cpu_fprintf, int flags);
int ppc_cpu_get_monitor_def(CPUState *cs, const char *name,
                            uint64_t *pval);
hwaddr ppc_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
int ppc_cpu_gdb_read_register(CPUState *cpu, uint8_t *buf, int reg);
int ppc_cpu_gdb_read_register_apple(CPUState *cpu, uint8_t *buf, int reg);
int ppc_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg);
int ppc_cpu_gdb_write_register_apple(CPUState *cpu, uint8_t *buf, int reg);
int ppc64_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque);
#ifndef CONFIG_USER_ONLY
void ppc_cpu_do_system_reset(CPUState *cs);
extern const struct VMStateDescription vmstate_ppc_cpu;

typedef struct PPCTimebase {
    uint64_t guest_timebase;
    int64_t time_of_the_day_ns;
} PPCTimebase;

extern const struct VMStateDescription vmstate_ppc_timebase;

#define VMSTATE_PPC_TIMEBASE_V(_field, _state, _version) {            \
    .name       = (stringify(_field)),                                \
    .version_id = (_version),                                         \
    .size       = sizeof(PPCTimebase),                                \
    .vmsd       = &vmstate_ppc_timebase,                              \
    .flags      = VMS_STRUCT,                                         \
    .offset     = vmstate_offset_value(_state, _field, PPCTimebase),  \
}
#endif

#endif
