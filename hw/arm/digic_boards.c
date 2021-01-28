/*
 * QEMU model of the Canon DIGIC boards (cameras indeed :).
 *
 * Copyright (C) 2013 Antony Pavlov <antonynpavlov@gmail.com>
 *
 * This model is based on reverse engineering efforts
 * made by CHDK (http://chdk.wikia.com) and
 * Magic Lantern (http://www.magiclantern.fm) projects
 * contributors.
 *
 * See docs here:
 *   http://magiclantern.wikia.com/wiki/Register_Map
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/arm/digic.h"
#include "hw/block/flash.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "qemu/units.h"
#include "qemu/cutils.h"

#define DIGIC4_ROM0_BASE      0xf0000000
#define DIGIC4_ROM1_BASE      0xf8000000
#define DIGIC4_ROM_MAX_SIZE   0x08000000

typedef struct DigicBoard {
    void (*add_rom0)(DigicState *, hwaddr, const char *);
    const char *rom0_def_filename;
    void (*add_rom1)(DigicState *, hwaddr, const char *);
    const char *rom1_def_filename;
} DigicBoard;

static void digic4_board_init(MachineState *machine, DigicBoard *board)
{
    Error *err = NULL;
    DigicState *s = DIGIC(object_new(TYPE_DIGIC));
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    if (!qdev_realize(DEVICE(s), NULL, &err)) {
        error_reportf_err(err, "Couldn't realize DIGIC SoC: ");
        exit(1);
    }

    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    if (board->add_rom0) {
        board->add_rom0(s, DIGIC4_ROM0_BASE,
                        machine->firmware ?: board->rom0_def_filename);
    }

    if (board->add_rom1) {
        board->add_rom1(s, DIGIC4_ROM1_BASE,
                        machine->firmware ?: board->rom1_def_filename);
    }
}

static void digic_load_rom(DigicState *s, hwaddr addr,
                           hwaddr max_size, const char *filename)
{
    target_long rom_size;

    if (qtest_enabled()) {
        /* qtest runs no code so don't attempt a ROM load which
         * could fail and result in a spurious test failure.
         */
        return;
    }

    if (filename) {
        char *fn = qemu_find_file(QEMU_FILE_TYPE_BIOS, filename);

        if (!fn) {
            error_report("Couldn't find rom image '%s'.", filename);
            exit(1);
        }

        rom_size = load_image_targphys(fn, addr, max_size);
        if (rom_size < 0 || rom_size > max_size) {
            error_report("Couldn't load rom image '%s'.", filename);
            exit(1);
        }
        g_free(fn);
    }
}

/*
 * Samsung K8P3215UQB
 * 64M Bit (4Mx16) Page Mode / Multi-Bank NOR Flash Memory
 */
static void digic4_add_k8p3215uqb_rom(DigicState *s, hwaddr addr,
                                      const char *filename)
{
#define FLASH_K8P3215UQB_SIZE (4 * 1024 * 1024)
#define FLASH_K8P3215UQB_SECTOR_SIZE (64 * 1024)

    pflash_cfi02_register(addr, "pflash", FLASH_K8P3215UQB_SIZE,
                          NULL, FLASH_K8P3215UQB_SECTOR_SIZE,
                          DIGIC4_ROM_MAX_SIZE / FLASH_K8P3215UQB_SIZE,
                          4,
                          0x00EC, 0x007E, 0x0003, 0x0001,
                          0x0555, 0x2aa, 0);

    digic_load_rom(s, addr, FLASH_K8P3215UQB_SIZE, filename);
}

static DigicBoard digic4_board_canon_a1100 = {
    .add_rom1 = digic4_add_k8p3215uqb_rom,
    .rom1_def_filename = "canon-a1100-rom1.bin",
};

static void canon_a1100_init(MachineState *machine)
{
    digic4_board_init(machine, &digic4_board_canon_a1100);
}

static void canon_a1100_machine_init(MachineClass *mc)
{
    mc->desc = "Canon PowerShot A1100 IS";
    mc->init = &canon_a1100_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_size = 64 * MiB;
    mc->default_ram_id = "ram";
}

DEFINE_MACHINE("canon-a1100", canon_a1100_machine_init)
