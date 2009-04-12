/* omap_sx1.c Support for the Siemens SX1 smartphone emulation.
 *
 *   Copyright (C) 2008
 * 	Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *   Copyright (C) 2007 Vladimir Ananiev <vovan888@gmail.com>
 *
 *   based on PalmOne's (TM) PDAs support (palm.c)
 */

/*
 * PalmOne's (TM) PDAs.
 *
 * Copyright (C) 2006-2007 Andrzej Zaborowski <balrog@zabor.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "hw.h"
#include "sysemu.h"
#include "console.h"
#include "omap.h"
#include "boards.h"
#include "arm-misc.h"
#include "flash.h"

/*****************************************************************************/
/* Siemens SX1 Cellphone V1 */
/* - ARM OMAP310 processor
 * - SRAM                192 kB
 * - SDRAM                32 MB at 0x10000000
 * - Boot flash           16 MB at 0x00000000
 * - Application flash     8 MB at 0x04000000
 * - 3 serial ports
 * - 1 SecureDigital
 * - 1 LCD display
 * - 1 RTC
 */

/*****************************************************************************/
/* Siemens SX1 Cellphone V2 */
/* - ARM OMAP310 processor
 * - SRAM                192 kB
 * - SDRAM                32 MB at 0x10000000
 * - Boot flash           32 MB at 0x00000000
 * - 3 serial ports
 * - 1 SecureDigital
 * - 1 LCD display
 * - 1 RTC
 */

static uint32_t static_readb(void *opaque, target_phys_addr_t offset)
{
    uint32_t *val = (uint32_t *) opaque;

    return *val >> ((offset & 3) << 3);
}

static uint32_t static_readh(void *opaque, target_phys_addr_t offset)
{
    uint32_t *val = (uint32_t *) opaque;

    return *val >> ((offset & 1) << 3);
}

static uint32_t static_readw(void *opaque, target_phys_addr_t offset)
{
    uint32_t *val = (uint32_t *) opaque;

    return *val >> ((offset & 0) << 3);
}

static void static_write(void *opaque, target_phys_addr_t offset,
                uint32_t value)
{
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

#define sdram_size	0x02000000
#define sector_size	(128 * 1024)
#define flash0_size	(16 * 1024 * 1024)
#define flash1_size	( 8 * 1024 * 1024)
#define flash2_size	(32 * 1024 * 1024)
#define total_ram_v1	(sdram_size + flash0_size + flash1_size + OMAP15XX_SRAM_SIZE)
#define total_ram_v2	(sdram_size + flash2_size + OMAP15XX_SRAM_SIZE)

static struct arm_boot_info sx1_binfo = {
    .loader_start = OMAP_EMIFF_BASE,
    .ram_size = sdram_size,
    .board_id = 0x265,
};

static void sx1_init(ram_addr_t ram_size, int vga_ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model,
                const int version)
{
    struct omap_mpu_state_s *cpu;
    int io;
    static uint32_t cs0val = 0x00213090;
    static uint32_t cs1val = 0x00215070;
    static uint32_t cs2val = 0x00001139;
    static uint32_t cs3val = 0x00001139;
    ram_addr_t phys_flash;
    int index;
    int fl_idx;
    uint32_t flash_size = flash0_size;

    if (version == 2) {
        flash_size = flash2_size;
    }

    cpu = omap310_mpu_init(sx1_binfo.ram_size, cpu_model);

    /* External Flash (EMIFS) */
    cpu_register_physical_memory(OMAP_CS0_BASE, flash_size,
                    (phys_flash = qemu_ram_alloc(flash_size)) | IO_MEM_ROM);

    io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs0val);
    cpu_register_physical_memory(OMAP_CS0_BASE + flash_size,
                    OMAP_CS0_SIZE - flash_size, io);
    io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs2val);
    cpu_register_physical_memory(OMAP_CS2_BASE, OMAP_CS2_SIZE, io);
    io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs3val);
    cpu_register_physical_memory(OMAP_CS3_BASE, OMAP_CS3_SIZE, io);

    fl_idx = 0;

    if ((index = drive_get_index(IF_PFLASH, 0, fl_idx)) > -1) {
        if (!pflash_cfi01_register(OMAP_CS0_BASE, qemu_ram_alloc(flash_size),
            drives_table[index].bdrv, sector_size, flash_size / sector_size,
            4, 0, 0, 0, 0)) {
            fprintf(stderr, "qemu: Error registering flash memory %d.\n",
                           fl_idx);
        }
        fl_idx++;
    }

    if ((version == 1) &&
            (index = drive_get_index(IF_PFLASH, 0, fl_idx)) > -1) {
        cpu_register_physical_memory(OMAP_CS1_BASE, flash1_size,
                        (phys_flash = qemu_ram_alloc(flash1_size)) |
                        IO_MEM_ROM);
        io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs1val);
        cpu_register_physical_memory(OMAP_CS1_BASE + flash1_size,
                        OMAP_CS1_SIZE - flash1_size, io);

        if (!pflash_cfi01_register(OMAP_CS1_BASE, qemu_ram_alloc(flash1_size),
            drives_table[index].bdrv, sector_size, flash1_size / sector_size,
            4, 0, 0, 0, 0)) {
            fprintf(stderr, "qemu: Error registering flash memory %d.\n",
                           fl_idx);
        }
        fl_idx++;
    } else {
        io = cpu_register_io_memory(0, static_readfn, static_writefn, &cs1val);
        cpu_register_physical_memory(OMAP_CS1_BASE, OMAP_CS1_SIZE, io);
    }

    if (!kernel_filename && !fl_idx) {
        fprintf(stderr, "Kernel or Flash image must be specified\n");
        exit(1);
    }

    /* Load the kernel.  */
    if (kernel_filename) {
        /* Start at bootloader.  */
        cpu->env->regs[15] = sx1_binfo.loader_start;

        sx1_binfo.kernel_filename = kernel_filename;
        sx1_binfo.kernel_cmdline = kernel_cmdline;
        sx1_binfo.initrd_filename = initrd_filename;
        arm_load_kernel(cpu->env, &sx1_binfo);
    } else {
        cpu->env->regs[15] = 0x00000000;
    }

    /* TODO: fix next line */
    //~ qemu_console_resize(ds, 640, 480);
}

static void sx1_init_v1(ram_addr_t ram_size, int vga_ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    sx1_init(ram_size, vga_ram_size, boot_device, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, 1);
}

static void sx1_init_v2(ram_addr_t ram_size, int vga_ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    sx1_init(ram_size, vga_ram_size, boot_device, kernel_filename,
                kernel_cmdline, initrd_filename, cpu_model, 2);
}

QEMUMachine sx1_machine_v2 = {
    .name = "sx1",
    .desc = "Siemens SX1 (OMAP310) V2",
    .init = sx1_init_v2,
};

QEMUMachine sx1_machine_v1 = {
    .name = "sx1-v1",
    .desc = "Siemens SX1 (OMAP310) V1",
    .init = sx1_init_v1,
};
