/*
 * QEMU PPC CHRP/PMAC hardware System Emulator
 * 
 * Copyright (c) 2004 Fabrice Bellard
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "vl.h"

#define BIOS_FILENAME "ppc_rom.bin"
#define NVRAM_SIZE        0x2000

/* PowerPC PREP hardware initialisation */
void ppc_chrp_init(int ram_size, int vga_ram_size, int boot_device,
		   DisplayState *ds, const char **fd_filename, int snapshot,
		   const char *kernel_filename, const char *kernel_cmdline,
		   const char *initrd_filename)
{
    char buf[1024];
    m48t59_t *nvram;
    int PPC_io_memory;
    int ret, linux_boot, i, fd;
    unsigned long bios_offset;
    
    linux_boot = (kernel_filename != NULL);

    /* allocate RAM */
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);

    /* allocate and load BIOS */
    bios_offset = ram_size + vga_ram_size;
    snprintf(buf, sizeof(buf), "%s/%s", bios_dir, BIOS_FILENAME);
    ret = load_image(buf, phys_ram_base + bios_offset);
    if (ret != BIOS_SIZE) {
        fprintf(stderr, "qemu: could not load PPC PREP bios '%s'\n", buf);
        exit(1);
    }
    cpu_register_physical_memory((uint32_t)(-BIOS_SIZE), 
                                 BIOS_SIZE, bios_offset | IO_MEM_ROM);
    cpu_single_env->nip = 0xfffffffc;

    /* Register CPU as a 74x/75x */
    cpu_ppc_register(cpu_single_env, 0x00080000);
    /* Set time-base frequency to 100 Mhz */
    cpu_ppc_tb_init(cpu_single_env, 100UL * 1000UL * 1000UL);

    isa_mem_base = 0xc0000000;
    pci_pmac_init();

    /* Register 64 KB of ISA IO space */
    PPC_io_memory = cpu_register_io_memory(0, PPC_io_read, PPC_io_write);
    cpu_register_physical_memory(0x80000000, 0x10000, PPC_io_memory);
    //    cpu_register_physical_memory(0xfe000000, 0xfe010000, PPC_io_memory);

    /* init basic PC hardware */
    vga_initialize(ds, phys_ram_base + ram_size, ram_size, 
                   vga_ram_size, 1);
    //    openpic = openpic_init(0x00000000, 0xF0000000, 1);
    //    pic_init(openpic);
    pic_init();
    //    pit = pit_init(0x40, 0);

    /* XXX: use Mac Serial port */
    fd = serial_open_device();
    serial_init(0x3f8, 4, fd);

    for(i = 0; i < nb_nics; i++) {
        pci_ne2000_init(&nd_table[i]);
    }

    pci_ide_init(bs_table);

    kbd_init();

    nvram = m48t59_init(8, 0x0074, NVRAM_SIZE);

    PPC_NVRAM_set_params(nvram, NVRAM_SIZE, "PREP", ram_size, boot_device,
                         0, 0,
                         0,
                         0,
                         0, 0,
                         /* XXX: need an option to load a NVRAM image */
                         0
                         );

    /* Special port to get debug messages from Open-Firmware */
    register_ioport_write(0xFF00, 0x04, 1, &PREP_debug_write, NULL);
    register_ioport_write(0xFF00, 0x04, 2, &PREP_debug_write, NULL);
    
    pci_ppc_bios_init();
}
