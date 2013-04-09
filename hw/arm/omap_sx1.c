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
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/arm/omap.h"
#include "hw/boards.h"
#include "hw/arm/arm.h"
#include "hw/block/flash.h"
#include "sysemu/blockdev.h"
#include "exec/address-spaces.h"

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

static uint64_t static_read(void *opaque, hwaddr offset,
                            unsigned size)
{
    uint32_t *val = (uint32_t *) opaque;
    uint32_t mask = (4 / size) - 1;

    return *val >> ((offset & mask) << 3);
}

static void static_write(void *opaque, hwaddr offset,
                         uint64_t value, unsigned size)
{
#ifdef SPY
    printf("%s: value %" PRIx64 " %u bytes written at 0x%x\n",
                    __func__, value, size, (int)offset);
#endif
}

static const MemoryRegionOps static_ops = {
    .read = static_read,
    .write = static_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
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

static void sx1_init(QEMUMachineInitArgs *args, const int version)
{
    struct omap_mpu_state_s *mpu;
    MemoryRegion *address_space = get_system_memory();
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *flash_1 = g_new(MemoryRegion, 1);
    MemoryRegion *cs = g_new(MemoryRegion, 4);
    static uint32_t cs0val = 0x00213090;
    static uint32_t cs1val = 0x00215070;
    static uint32_t cs2val = 0x00001139;
    static uint32_t cs3val = 0x00001139;
    DriveInfo *dinfo;
    int fl_idx;
    uint32_t flash_size = flash0_size;
    int be;

    if (version == 2) {
        flash_size = flash2_size;
    }

    mpu = omap310_mpu_init(address_space, sx1_binfo.ram_size, args->cpu_model);

    /* External Flash (EMIFS) */
    memory_region_init_ram(flash, "omap_sx1.flash0-0", flash_size);
    vmstate_register_ram_global(flash);
    memory_region_set_readonly(flash, true);
    memory_region_add_subregion(address_space, OMAP_CS0_BASE, flash);

    memory_region_init_io(&cs[0], &static_ops, &cs0val,
                          "sx1.cs0", OMAP_CS0_SIZE - flash_size);
    memory_region_add_subregion(address_space,
                                OMAP_CS0_BASE + flash_size, &cs[0]);


    memory_region_init_io(&cs[2], &static_ops, &cs2val,
                          "sx1.cs2", OMAP_CS2_SIZE);
    memory_region_add_subregion(address_space,
                                OMAP_CS2_BASE, &cs[2]);

    memory_region_init_io(&cs[3], &static_ops, &cs3val,
                          "sx1.cs3", OMAP_CS3_SIZE);
    memory_region_add_subregion(address_space,
                                OMAP_CS2_BASE, &cs[3]);

    fl_idx = 0;
#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif

    if ((dinfo = drive_get(IF_PFLASH, 0, fl_idx)) != NULL) {
        if (!pflash_cfi01_register(OMAP_CS0_BASE, NULL,
                                   "omap_sx1.flash0-1", flash_size,
                                   dinfo->bdrv, sector_size,
                                   flash_size / sector_size,
                                   4, 0, 0, 0, 0, be)) {
            fprintf(stderr, "qemu: Error registering flash memory %d.\n",
                           fl_idx);
        }
        fl_idx++;
    }

    if ((version == 1) &&
            (dinfo = drive_get(IF_PFLASH, 0, fl_idx)) != NULL) {
        memory_region_init_ram(flash_1, "omap_sx1.flash1-0", flash1_size);
        vmstate_register_ram_global(flash_1);
        memory_region_set_readonly(flash_1, true);
        memory_region_add_subregion(address_space, OMAP_CS1_BASE, flash_1);

        memory_region_init_io(&cs[1], &static_ops, &cs1val,
                              "sx1.cs1", OMAP_CS1_SIZE - flash1_size);
        memory_region_add_subregion(address_space,
                                OMAP_CS1_BASE + flash1_size, &cs[1]);

        if (!pflash_cfi01_register(OMAP_CS1_BASE, NULL,
                                   "omap_sx1.flash1-1", flash1_size,
                                   dinfo->bdrv, sector_size,
                                   flash1_size / sector_size,
                                   4, 0, 0, 0, 0, be)) {
            fprintf(stderr, "qemu: Error registering flash memory %d.\n",
                           fl_idx);
        }
        fl_idx++;
    } else {
        memory_region_init_io(&cs[1], &static_ops, &cs1val,
                              "sx1.cs1", OMAP_CS1_SIZE);
        memory_region_add_subregion(address_space,
                                OMAP_CS1_BASE, &cs[1]);
    }

    if (!args->kernel_filename && !fl_idx) {
        fprintf(stderr, "Kernel or Flash image must be specified\n");
        exit(1);
    }

    /* Load the kernel.  */
    if (args->kernel_filename) {
        sx1_binfo.kernel_filename = args->kernel_filename;
        sx1_binfo.kernel_cmdline = args->kernel_cmdline;
        sx1_binfo.initrd_filename = args->initrd_filename;
        arm_load_kernel(mpu->cpu, &sx1_binfo);
    }

    /* TODO: fix next line */
    //~ qemu_console_resize(ds, 640, 480);
}

static void sx1_init_v1(QEMUMachineInitArgs *args)
{
    sx1_init(args, 1);
}

static void sx1_init_v2(QEMUMachineInitArgs *args)
{
    sx1_init(args, 2);
}

static QEMUMachine sx1_machine_v2 = {
    .name = "sx1",
    .desc = "Siemens SX1 (OMAP310) V2",
    .init = sx1_init_v2,
    DEFAULT_MACHINE_OPTIONS,
};

static QEMUMachine sx1_machine_v1 = {
    .name = "sx1-v1",
    .desc = "Siemens SX1 (OMAP310) V1",
    .init = sx1_init_v1,
    DEFAULT_MACHINE_OPTIONS,
};

static void sx1_machine_init(void)
{
    qemu_register_machine(&sx1_machine_v2);
    qemu_register_machine(&sx1_machine_v1);
}

machine_init(sx1_machine_init);
