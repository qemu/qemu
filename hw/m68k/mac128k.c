/*
 * Macintosh 128K system emulation.
 *
 * This code is licensed under the GPL
 */

#include "hw/hw.h"
#include "hw/m68k/mac128k.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"

#define ROM_LOAD_ADDR 0x400000
#define MAX_ROM_SIZE 0x20000
#define IWM_BASE_ADDR 0xDFE1FF // dBase

/* Board init.  */

static void mac128k_init(MachineState *machine)
{
    ram_addr_t ram_size = 0x20000;//machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    M68kCPU *cpu;
    CPUM68KState *env;
    int kernel_size;
    hwaddr entry;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    uint8_t header[16];
    FILE *f;

    if (!cpu_model) {
        cpu_model = "m68000";
    }
    cpu = cpu_m68k_init(cpu_model);
    if (!cpu) {
        hw_error("Unable to find m68k CPU definition\n");
    }
    env = &cpu->env;

    /* Initialize CPU registers.  */
    env->vbr = 0;

    /* RAM at address zero */
    memory_region_allocate_system_memory(ram, NULL, "mac128k.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* ROM */
    memory_region_init_ram(rom, NULL, "mac128k.rom", MAX_ROM_SIZE, &error_abort);
    memory_region_add_subregion(address_space_mem, ROM_LOAD_ADDR, rom);
    memory_region_set_readonly(rom, true);

    iwm_init(address_space_mem, IWM_BASE_ADDR, cpu);

    /* Load kernel.  */
    if (kernel_filename) {
        kernel_size = load_image_targphys(kernel_filename,
                                          ROM_LOAD_ADDR,
                                          MAX_ROM_SIZE);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }

        /* load the kernel header */
        f = fopen(kernel_filename, "rb");
        if (!f ||
            fread(header, 1, MIN(ARRAY_SIZE(header), kernel_size), f) !=
            MIN(ARRAY_SIZE(header), kernel_size)) {
            fprintf(stderr, "qemu: could not load kernel '%s': %s\n",
                    kernel_filename, strerror(errno));
            exit(1);
        }
        fclose(f);
        entry = ldl_p(header + 4);
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
