/*
 * Macintosh 128K system emulation.
 *
 * This code is licensed under the GPL
 */

#include "hw/hw.h"
#include "hw/m68k/mcf.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"

#define KERNEL_LOAD_ADDR 0x400000
//#define AN5206_MBAR_ADDR1 0x10000000
//#define AN5206_RAMBAR_ADDR1 0x20000000

/* Board init.  */

static void mac128k_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    M68kCPU *cpu;
    CPUM68KState *env;
    int kernel_size;
    uint64_t elf_entry;
    hwaddr entry;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);

    if (!cpu_model) {
        cpu_model = "mc68000";
    }
    cpu = cpu_m68k_init(cpu_model);
    if (!cpu) {
        hw_error("Unable to find m68k CPU definition\n");
    }
    env = &cpu->env;

    /* Initialize CPU registers.  */
    env->vbr = 0;
    /* TODO: allow changing MBAR and RAMBAR.  */
    //env->mbar = AN5206_MBAR_ADDR1 | 1;
    //env->rambar0 = AN5206_RAMBAR_ADDR1 | 1;

    /* DRAM at address zero */
    memory_region_allocate_system_memory(ram, NULL, "mac128k.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* Internal SRAM.  */
    memory_region_init_ram(sram, NULL, "mac128k.sram", 512, &error_abort);
    vmstate_register_ram_global(sram);
    //memory_region_add_subregion(address_space_mem, AN5206_RAMBAR_ADDR1, sram);

    //mcf5206_init(address_space_mem, AN5206_MBAR_ADDR1, cpu);

    /* Load kernel.  */
	if (kernel_filename) {
		kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
							   NULL, NULL, 1, ELF_MACHINE, 0);
		entry = elf_entry;
		if (kernel_size < 0) {
			kernel_size = load_uimage(kernel_filename, &entry, NULL, NULL,
									  NULL, NULL);
		}
		if (kernel_size < 0) {
			kernel_size = load_image_targphys(kernel_filename,
											  KERNEL_LOAD_ADDR,
											  ram_size - KERNEL_LOAD_ADDR);
			entry = KERNEL_LOAD_ADDR;
		}
		if (kernel_size < 0) {
			fprintf(stderr, "qemu: could not load kernel '%s'\n",
					kernel_filename);
			exit(1);
		}
	} else {
		entry = 0;
	}
	env->pc = entry;
}

static QEMUMachine mac128k_machine = {
    .name = "mac128k",
    .desc = "Macintosh 128K",
    .init = mac128k_init,
};

static void mac128k_machine_init(void)
{
    qemu_register_machine(&mac128k_machine);
}

machine_init(mac128k_machine_init);
