/*
 * Bananapi M2U emulation
 *
 * Copyright (C) 2023 qianfan Zhao <qianfanguijin@163.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/i2c/i2c.h"
#include "hw/qdev-properties.h"
#include "hw/arm/allwinner-r40.h"
#include "hw/arm/boot.h"

static struct arm_boot_info bpim2u_binfo;

/*
 * R40 can boot from mmc0 and mmc2, and bpim2u has two mmc interface, one is
 * connected to sdcard and another mount an emmc media.
 * Attach the mmc driver and try loading bootloader.
 */
static void mmc_attach_drive(AwR40State *s, AwSdHostState *mmc, int unit,
                             bool load_bootroom, bool *bootroom_loaded)
{
    DriveInfo *di = drive_get(IF_SD, 0, unit);
    BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
    BusState *bus;
    DeviceState *carddev;

    bus = qdev_get_child_bus(DEVICE(mmc), "sd-bus");
    if (bus == NULL) {
        error_report("No SD bus found in SOC object");
        exit(1);
    }

    carddev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
    qdev_realize_and_unref(carddev, bus, &error_fatal);

    if (load_bootroom && blk && blk_is_available(blk)) {
        /* Use Boot ROM to copy data from SD card to SRAM */
        *bootroom_loaded = allwinner_r40_bootrom_setup(s, blk, unit);
    }
}

static void bpim2u_init(MachineState *machine)
{
    bool bootroom_loaded = false;
    AwR40State *r40;
    I2CBus *i2c;

    /* BIOS is not supported by this board */
    if (machine->firmware) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    r40 = AW_R40(object_new(TYPE_AW_R40));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(r40));
    object_unref(OBJECT(r40));

    /* Setup timer properties */
    object_property_set_int(OBJECT(r40), "clk0-freq", 32768, &error_abort);
    object_property_set_int(OBJECT(r40), "clk1-freq", 24 * 1000 * 1000,
                            &error_abort);

    /* DRAMC */
    r40->ram_size = machine->ram_size / MiB;
    object_property_set_uint(OBJECT(r40), "ram-addr",
                             r40->memmap[AW_R40_DEV_SDRAM], &error_abort);
    object_property_set_int(OBJECT(r40), "ram-size",
                            r40->ram_size, &error_abort);

    /* GMAC PHY */
    object_property_set_uint(OBJECT(r40), "gmac-phy-addr", 1, &error_abort);

    /* Mark R40 object realized */
    qdev_realize(DEVICE(r40), NULL, &error_abort);

    /*
     * Plug in SD card and try load bootrom, R40 has 4 mmc controllers but can
     * only booting from mmc0 and mmc2.
     */
    for (int i = 0; i < AW_R40_NUM_MMCS; i++) {
        switch (i) {
        case 0:
        case 2:
            mmc_attach_drive(r40, &r40->mmc[i], i,
                             !machine->kernel_filename && !bootroom_loaded,
                             &bootroom_loaded);
            break;
        default:
            mmc_attach_drive(r40, &r40->mmc[i], i, false, NULL);
            break;
        }
    }

    /* Connect AXP221 */
    i2c = I2C_BUS(qdev_get_child_bus(DEVICE(&r40->i2c0), "i2c"));
    i2c_slave_create_simple(i2c, "axp221_pmu", 0x34);

    /* SDRAM */
    memory_region_add_subregion(get_system_memory(),
                                r40->memmap[AW_R40_DEV_SDRAM], machine->ram);

    bpim2u_binfo.loader_start = r40->memmap[AW_R40_DEV_SDRAM];
    bpim2u_binfo.ram_size = machine->ram_size;
    bpim2u_binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    arm_load_kernel(&r40->cpus[0], machine, &bpim2u_binfo);
}

static void bpim2u_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a7"),
        NULL
    };

    mc->desc = "Bananapi M2U (Cortex-A7)";
    mc->init = bpim2u_init;
    mc->min_cpus = AW_R40_NUM_CPUS;
    mc->max_cpus = AW_R40_NUM_CPUS;
    mc->default_cpus = AW_R40_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "bpim2u.ram";
    mc->auto_create_sdcard = true;
}

DEFINE_MACHINE("bpim2u", bpim2u_machine_init)
