/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"

static void xtensa_create_memory_regions(const XtensaMemory *memory,
                                         const char *name)
{
    unsigned i;
    char *num_name = malloc(strlen(name) + sizeof(i) * 3 + 1);

    for (i = 0; i < memory->num; ++i) {
        MemoryRegion *m;

        sprintf(num_name, "%s%u", name, i);
        m = g_malloc(sizeof(*m));
        memory_region_init_ram(m, NULL, num_name,
                               memory->location[i].size,
                               &error_fatal);
        vmstate_register_ram_global(m);
        memory_region_add_subregion(get_system_memory(),
                                    memory->location[i].addr, m);
    }
    free(num_name);
}

static uint64_t translate_phys_addr(void *opaque, uint64_t addr)
{
    XtensaCPU *cpu = opaque;

    return cpu_get_phys_page_debug(CPU(cpu), addr);
}

static void sim_reset(void *opaque)
{
    XtensaCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void xtensa_sim_init(MachineState *machine)
{
    XtensaCPU *cpu = NULL;
    CPUXtensaState *env = NULL;
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    int n;

    if (!cpu_model) {
        cpu_model = XTENSA_DEFAULT_CPU_MODEL;
    }

    for (n = 0; n < smp_cpus; n++) {
        cpu = cpu_xtensa_init(cpu_model);
        if (cpu == NULL) {
            error_report("unable to find CPU definition '%s'",
                         cpu_model);
            exit(EXIT_FAILURE);
        }
        env = &cpu->env;

        env->sregs[PRID] = n;
        qemu_register_reset(sim_reset, cpu);
        /* Need MMU initialized prior to ELF loading,
         * so that ELF gets loaded into virtual addresses
         */
        sim_reset(cpu);
    }

    if (env) {
        XtensaMemory sysram = env->config->sysram;

        sysram.location[0].size = ram_size;
        xtensa_create_memory_regions(&env->config->instrom, "xtensa.instrom");
        xtensa_create_memory_regions(&env->config->instram, "xtensa.instram");
        xtensa_create_memory_regions(&env->config->datarom, "xtensa.datarom");
        xtensa_create_memory_regions(&env->config->dataram, "xtensa.dataram");
        xtensa_create_memory_regions(&env->config->sysrom, "xtensa.sysrom");
        xtensa_create_memory_regions(&sysram, "xtensa.sysram");
    }

    if (kernel_filename) {
        uint64_t elf_entry;
        uint64_t elf_lowaddr;
#ifdef TARGET_WORDS_BIGENDIAN
        int success = load_elf(kernel_filename, translate_phys_addr, cpu,
                &elf_entry, &elf_lowaddr, NULL, 1, EM_XTENSA, 0, 0);
#else
        int success = load_elf(kernel_filename, translate_phys_addr, cpu,
                &elf_entry, &elf_lowaddr, NULL, 0, EM_XTENSA, 0, 0);
#endif
        if (success > 0) {
            env->pc = elf_entry;
        }
    }
}

static void xtensa_sim_machine_init(MachineClass *mc)
{
    mc->desc = "sim machine (" XTENSA_DEFAULT_CPU_MODEL ")";
    mc->is_default = true;
    mc->init = xtensa_sim_init;
    mc->max_cpus = 4;
}

DEFINE_MACHINE("sim", xtensa_sim_machine_init)
