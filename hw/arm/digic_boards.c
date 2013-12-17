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

#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/arm/digic.h"

typedef struct DigicBoardState {
    DigicState *digic;
    MemoryRegion ram;
} DigicBoardState;

typedef struct DigicBoard {
    hwaddr ram_size;
} DigicBoard;

static void digic4_board_setup_ram(DigicBoardState *s, hwaddr ram_size)
{
    memory_region_init_ram(&s->ram, NULL, "ram", ram_size);
    memory_region_add_subregion(get_system_memory(), 0, &s->ram);
    vmstate_register_ram_global(&s->ram);
}

static void digic4_board_init(DigicBoard *board)
{
    Error *err = NULL;

    DigicBoardState *s = g_new(DigicBoardState, 1);

    s->digic = DIGIC(object_new(TYPE_DIGIC));
    object_property_set_bool(OBJECT(s->digic), true, "realized", &err);
    if (err != NULL) {
        error_report("Couldn't realize DIGIC SoC: %s\n",
                     error_get_pretty(err));
        exit(1);
    }

    digic4_board_setup_ram(s, board->ram_size);
}

static DigicBoard digic4_board_canon_a1100 = {
    .ram_size = 64 * 1024 * 1024,
};

static void canon_a1100_init(QEMUMachineInitArgs *args)
{
    digic4_board_init(&digic4_board_canon_a1100);
}

static QEMUMachine canon_a1100 = {
    .name = "canon-a1100",
    .desc = "Canon PowerShot A1100 IS",
    .init = &canon_a1100_init,
};

static void digic_register_machines(void)
{
    qemu_register_machine(&canon_a1100);
}

machine_init(digic_register_machines)
