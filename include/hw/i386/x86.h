/*
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_I386_X86_H
#define HW_I386_X86_H

#include "hw/boards.h"

uint32_t x86_cpu_apic_id_from_index(PCMachineState *pcms,
                                    unsigned int cpu_index);
void x86_cpu_new(PCMachineState *pcms, int64_t apic_id, Error **errp);
void x86_cpus_init(PCMachineState *pcms);
CpuInstanceProperties x86_cpu_index_to_props(MachineState *ms,
                                             unsigned cpu_index);
int64_t x86_get_default_cpu_node_id(const MachineState *ms, int idx);
const CPUArchIdList *x86_possible_cpu_arch_ids(MachineState *ms);

void x86_bios_rom_init(MemoryRegion *rom_memory, bool isapc_ram_fw);

void x86_load_linux(PCMachineState *x86ms, FWCfgState *fw_cfg);

#endif
