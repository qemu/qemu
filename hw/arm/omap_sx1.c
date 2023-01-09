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
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "hw/arm/omap.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "hw/block/flash.h"
#include "sysemu/qtest.h"
#include "exec/address-spaces.h"
#include "cpu.h"
#include "qemu/cutils.h"

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
    uint32_t *val = opaque;
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

#define SDRAM_SIZE      (32 * MiB)
#define SECTOR_SIZE     (128 * KiB)
#define FLASH0_SIZE     (16 * MiB)
#define FLASH1_SIZE     (8 * MiB)
#define FLASH2_SIZE     (32 * MiB)

static struct arm_boot_info sx1_binfo = {
    .loader_start = OMAP_EMIFF_BASE,
    .ram_size = SDRAM_SIZE,
    .board_id = 0x265,
};

static void sx1_init(MachineState *machine, const int version)
{
    struct omap_mpu_state_s *mpu;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *address_space = get_system_memory();
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *cs = g_new(MemoryRegion, 4);
    static uint32_t cs0val = 0x00213090;
    static uint32_t cs1val = 0x00215070;
    static uint32_t cs2val = 0x00001139;
    static uint32_t cs3val = 0x00001139;
    DriveInfo *dinfo;
    int fl_idx;
    uint32_t flash_size = FLASH0_SIZE;

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    if (version == 2) {
        flash_size = FLASH2_SIZE;
    }

    memory_region_add_subregion(address_space, OMAP_EMIFF_BASE, machine->ram);

    mpu = omap310_mpu_init(machine->ram, machine->cpu_type);

    /* External Flash (EMIFS) */
    memory_region_init_rom(flash, NULL, "omap_sx1.flash0-0", flash_size,
                           &error_fatal);
    memory_region_add_subregion(address_space, OMAP_CS0_BASE, flash);

    memory_region_init_io(&cs[0], NULL, &static_ops, &cs0val,
                          "sx1.cs0", OMAP_CS0_SIZE - flash_size);
    memory_region_add_subregion(address_space,
                                OMAP_CS0_BASE + flash_size, &cs[0]);


    memory_region_init_io(&cs[2], NULL, &static_ops, &cs2val,
                          "sx1.cs2", OMAP_CS2_SIZE);
    memory_region_add_subregion(address_space,
                                OMAP_CS2_BASE, &cs[2]);

    memory_region_init_io(&cs[3], NULL, &static_ops, &cs3val,
                          "sx1.cs3", OMAP_CS3_SIZE);
    memory_region_add_subregion(address_space,
                                OMAP_CS2_BASE, &cs[3]);

    fl_idx = 0;
    if ((dinfo = drive_get(IF_PFLASH, 0, fl_idx)) != NULL) {
        pflash_cfi01_register(OMAP_CS0_BASE,
                              "omap_sx1.flash0-1", flash_size,
                              blk_by_legacy_dinfo(dinfo),
                              SECTOR_SIZE, 4, 0, 0, 0, 0, 0);
        fl_idx++;
    }

    if ((version == 1) &&
            (dinfo = drive_get(IF_PFLASH, 0, fl_idx)) != NULL) {
        MemoryRegion *flash_1 = g_new(MemoryRegion, 1);
        memory_region_init_rom(flash_1, NULL, "omap_sx1.flash1-0",
                               FLASH1_SIZE, &error_fatal);
        memory_region_add_subregion(address_space, OMAP_CS1_BASE, flash_1);

        memory_region_init_io(&cs[1], NULL, &static_ops, &cs1val,
                              "sx1.cs1", OMAP_CS1_SIZE - FLASH1_SIZE);
        memory_region_add_subregion(address_space,
                                OMAP_CS1_BASE + FLASH1_SIZE, &cs[1]);

        pflash_cfi01_register(OMAP_CS1_BASE,
                              "omap_sx1.flash1-1", FLASH1_SIZE,
                              blk_by_legacy_dinfo(dinfo),
                              SECTOR_SIZE, 4, 0, 0, 0, 0, 0);
        fl_idx++;
    } else {
        memory_region_init_io(&cs[1], NULL, &static_ops, &cs1val,
                              "sx1.cs1", OMAP_CS1_SIZE);
        memory_region_add_subregion(address_space,
                                OMAP_CS1_BASE, &cs[1]);
    }

    if (!machine->kernel_filename && !fl_idx && !qtest_enabled()) {
        error_report("Kernel or Flash image must be specified");
        exit(1);
    }

    /* Load the kernel.  */
    arm_load_kernel(mpu->cpu, machine, &sx1_binfo);

    /* TODO: fix next line */
    //~ qemu_console_resize(ds, 640, 480);
}

static void sx1_init_v1(MachineState *machine)
{
    sx1_init(machine, 1);
}

static void sx1_init_v2(MachineState *machine)
{
    sx1_init(machine, 2);
}

static void sx1_machine_v2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Siemens SX1 (OMAP310) V2";
    mc->init = sx1_init_v2;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("ti925t");
    mc->default_ram_size = SDRAM_SIZE;
    mc->default_ram_id = "omap1.dram";
}

static const TypeInfo sx1_machine_v2_type = {
    .name = MACHINE_TYPE_NAME("sx1"),
    .parent = TYPE_MACHINE,
    .class_init = sx1_machine_v2_class_init,
};

static void sx1_machine_v1_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Siemens SX1 (OMAP310) V1";
    mc->init = sx1_init_v1;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("ti925t");
    mc->default_ram_size = SDRAM_SIZE;
    mc->default_ram_id = "omap1.dram";
}

static const TypeInfo sx1_machine_v1_type = {
    .name = MACHINE_TYPE_NAME("sx1-v1"),
    .parent = TYPE_MACHINE,
    .class_init = sx1_machine_v1_class_init,
};

static void sx1_machine_init(void)
{
    type_register_static(&sx1_machine_v1_type);
    type_register_static(&sx1_machine_v2_type);
}

type_init(sx1_machine_init)
