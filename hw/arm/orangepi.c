/*
 * Orange Pi emulation
 *
 * Copyright (C) 2019 Niek Linnenbank <nieklinnenbank@gmail.com>
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
#include "hw/qdev-properties.h"
#include "hw/arm/allwinner-h3.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"

static struct arm_boot_info orangepi_binfo;

static void orangepi_init(MachineState *machine)
{
    AwH3State *h3;
    DriveInfo *di;
    BlockBackend *blk;
    BusState *bus;
    DeviceState *carddev;

    /* BIOS is not supported by this board */
    if (machine->firmware) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    /* This board has fixed size RAM */
    if (machine->ram_size != 1 * GiB) {
        error_report("This machine can only be used with 1GiB of RAM");
        exit(1);
    }

    h3 = AW_H3(object_new(TYPE_AW_H3));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(h3));
    object_unref(OBJECT(h3));

    /* Setup timer properties */
    object_property_set_int(OBJECT(h3), "clk0-freq", 32768, &error_abort);
    object_property_set_int(OBJECT(h3), "clk1-freq", 24 * 1000 * 1000,
                            &error_abort);

    /* Setup SID properties. Currently using a default fixed SID identifier. */
    if (qemu_uuid_is_null(&h3->sid.identifier)) {
        qdev_prop_set_string(DEVICE(h3), "identifier",
                             "02c00081-1111-2222-3333-000044556677");
    } else if (ldl_be_p(&h3->sid.identifier.data[0]) != 0x02c00081) {
        warn_report("Security Identifier value does not include H3 prefix");
    }

    /* Setup EMAC properties */
    object_property_set_int(OBJECT(&h3->emac), "phy-addr", 1, &error_abort);

    /* DRAMC */
    object_property_set_uint(OBJECT(h3), "ram-addr", h3->memmap[AW_H3_DEV_SDRAM],
                             &error_abort);
    object_property_set_int(OBJECT(h3), "ram-size", machine->ram_size / MiB,
                            &error_abort);

    /* Mark H3 object realized */
    qdev_realize(DEVICE(h3), NULL, &error_abort);

    /* Retrieve SD bus */
    di = drive_get(IF_SD, 0, 0);
    blk = di ? blk_by_legacy_dinfo(di) : NULL;
    bus = qdev_get_child_bus(DEVICE(h3), "sd-bus");

    /* Plug in SD card */
    carddev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
    qdev_realize_and_unref(carddev, bus, &error_fatal);

    /* SDRAM */
    memory_region_add_subregion(get_system_memory(), h3->memmap[AW_H3_DEV_SDRAM],
                                machine->ram);

    /* Load target kernel or start using BootROM */
    if (!machine->kernel_filename && blk && blk_is_available(blk)) {
        /* Use Boot ROM to copy data from SD card to SRAM */
        allwinner_h3_bootrom_setup(h3, blk);
    }
    orangepi_binfo.loader_start = h3->memmap[AW_H3_DEV_SDRAM];
    orangepi_binfo.ram_size = machine->ram_size;
    orangepi_binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    arm_load_kernel(&h3->cpus[0], machine, &orangepi_binfo);
}

static void orangepi_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a7"),
        NULL
    };

    mc->desc = "Orange Pi PC (Cortex-A7)";
    mc->init = orangepi_init;
    mc->block_default_type = IF_SD;
    mc->units_per_default_bus = 1;
    mc->min_cpus = AW_H3_NUM_CPUS;
    mc->max_cpus = AW_H3_NUM_CPUS;
    mc->default_cpus = AW_H3_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a7");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "orangepi.ram";
    mc->auto_create_sdcard = true;
}

DEFINE_MACHINE_ARM("orangepi-pc", orangepi_machine_init)
