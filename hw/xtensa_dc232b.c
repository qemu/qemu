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

#include "sysemu.h"
#include "boards.h"
#include "loader.h"
#include "elf.h"
#include "memory.h"
#include "exec-memory.h"

static uint64_t translate_phys_addr(void *env, uint64_t addr)
{
    return cpu_get_phys_page_debug(env, addr);
}

static void dc232b_reset(void *env)
{
    cpu_reset(env);
}

static void dc232b_init(ram_addr_t ram_size,
        const char *boot_device,
        const char *kernel_filename, const char *kernel_cmdline,
        const char *initrd_filename, const char *cpu_model)
{
    CPUState *env = NULL;
    MemoryRegion *ram, *rom;
    int n;

    for (n = 0; n < smp_cpus; n++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find CPU definition\n");
            exit(1);
        }
        env->sregs[PRID] = n;
        qemu_register_reset(dc232b_reset, env);
        /* Need MMU initialized prior to ELF loading,
         * so that ELF gets loaded into virtual addresses
         */
        dc232b_reset(env);
    }

    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, NULL, "xtensa.sram", ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    rom = g_malloc(sizeof(*rom));
    memory_region_init_ram(rom, NULL, "xtensa.rom", 0x1000);
    memory_region_add_subregion(get_system_memory(), 0xfe000000, rom);

    if (kernel_filename) {
        uint64_t elf_entry;
        uint64_t elf_lowaddr;
#ifdef TARGET_WORDS_BIGENDIAN
        int success = load_elf(kernel_filename, translate_phys_addr, env,
                &elf_entry, &elf_lowaddr, NULL, 1, ELF_MACHINE, 0);
#else
        int success = load_elf(kernel_filename, translate_phys_addr, env,
                &elf_entry, &elf_lowaddr, NULL, 0, ELF_MACHINE, 0);
#endif
        if (success > 0) {
            env->pc = elf_entry;
        }
    }
}

static void xtensa_dc232b_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    if (!cpu_model) {
        cpu_model = "dc232b";
    }
    dc232b_init(ram_size, boot_device, kernel_filename, kernel_cmdline,
            initrd_filename, cpu_model);
}

static QEMUMachine xtensa_dc232b_machine = {
    .name = "dc232b",
    .desc = "Diamond 232L Standard Core Rev.B (LE) (dc232b)",
    .init = xtensa_dc232b_init,
    .max_cpus = 4,
};

static void xtensa_dc232b_machine_init(void)
{
    qemu_register_machine(&xtensa_dc232b_machine);
}

machine_init(xtensa_dc232b_machine_init);
