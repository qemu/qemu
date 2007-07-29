/*
 * PalmOne's (TM) PDAs.
 *
 * Copyright (C) 2006-2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include "vl.h"

static uint32_t static_readb(void *opaque, target_phys_addr_t offset)
{
    uint32_t *val = (uint32_t *) opaque;
    return *val >> ((offset & 3) << 3);
}

static uint32_t static_readh(void *opaque, target_phys_addr_t offset) {
    uint32_t *val = (uint32_t *) opaque;
    return *val >> ((offset & 1) << 3);
}

static uint32_t static_readw(void *opaque, target_phys_addr_t offset) {
    uint32_t *val = (uint32_t *) opaque;
    return *val >> ((offset & 0) << 3);
}

static void static_write(void *opaque, target_phys_addr_t offset,
                uint32_t value) {
#ifdef SPY
    printf("%s: value %08lx written at " PA_FMT "\n",
                    __FUNCTION__, value, offset);
#endif
}

static CPUReadMemoryFunc *static_readfn[] = {
    static_readb,
    static_readh,
    static_readw,
};

static CPUWriteMemoryFunc *static_writefn[] = {
    static_write,
    static_write,
    static_write,
};

/* Palm Tunsgten|E support */
static void palmte_microwire_setup(struct omap_mpu_state_s *cpu)
{
}

static void palmte_init(int ram_size, int vga_ram_size, int boot_device,
                DisplayState *ds, const char **fd_filename, int snapshot,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    struct omap_mpu_state_s *cpu;
    int flash_size = 0x00800000;
    int sdram_size = 0x02000000;
    int io;
    static uint32_t cs0val = 0xffffffff;
    static uint32_t cs1val = 0x0000e1a0;
    static uint32_t cs2val = 0x0000e1a0;
    static uint32_t cs3val = 0xe1a0e1a0;
    ram_addr_t phys_flash;
    int rom_size, rom_loaded = 0;

    if (ram_size < flash_size + sdram_size + OMAP15XX_SRAM_SIZE) {
        fprintf(stderr, "This architecture uses %i bytes of memory\n",
                        flash_size + sdram_size + OMAP15XX_SRAM_SIZE);
        exit(1);
    }

    cpu = omap310_mpu_init(sdram_size, ds, cpu_model);

    /* External Flash (EMIFS) */
    cpu_register_physical_memory(OMAP_CS0_BASE, flash_size,
                    (phys_flash = qemu_ram_alloc(flash_size)) | IO_MEM_ROM);

    io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs0val);
    cpu_register_physical_memory(OMAP_CS0_BASE + flash_size,
                    OMAP_CS0_SIZE - flash_size, io);
    io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs1val);
    cpu_register_physical_memory(OMAP_CS1_BASE, OMAP_CS1_SIZE, io);
    io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs2val);
    cpu_register_physical_memory(OMAP_CS2_BASE, OMAP_CS2_SIZE, io);
    io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs3val);
    cpu_register_physical_memory(OMAP_CS3_BASE, OMAP_CS3_SIZE, io);

    palmte_microwire_setup(cpu);

    /* Setup initial (reset) machine state */
    if (nb_option_roms) {
        rom_size = get_image_size(option_rom[0]);
        if (rom_size > flash_size)
            fprintf(stderr, "%s: ROM image too big (%x > %x)\n",
                            __FUNCTION__, rom_size, flash_size);
        else if (rom_size > 0 && load_image(option_rom[0],
                                phys_ram_base + phys_flash) > 0) {
            rom_loaded = 1;
            cpu->env->regs[15] = 0x00000000;
        } else
            fprintf(stderr, "%s: error loading '%s'\n",
                            __FUNCTION__, option_rom[0]);
    }

    if (!rom_loaded && !kernel_filename) {
        fprintf(stderr, "Kernel or ROM image must be specified\n");
        exit(1);
    }

    /* Load the kernel.  */
    if (kernel_filename) {
        /* Start at bootloader.  */
        cpu->env->regs[15] = OMAP_EMIFF_BASE;

        arm_load_kernel(cpu->env, sdram_size, kernel_filename, kernel_cmdline,
                        initrd_filename, 0x331, OMAP_EMIFF_BASE);
    }

    dpy_resize(ds, 320, 320);
}

QEMUMachine palmte_machine = {
    "cheetah",
    "Palm Tungsten|E aka. Cheetah PDA (OMAP310)",
    palmte_init,
};
