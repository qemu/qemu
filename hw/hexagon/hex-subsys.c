/*
 * Hexagon subsystem helpers shared between the machine models.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hexagon/hex-subsys.h"
#include "hw/core/loader.h"
#include "system/address-spaces.h"

void hex_subsys_create(HexagonCommonMachineState *hms,
                       const struct hexagon_machine_config *m_cfg)
{
    MachineState *machine = MACHINE(hms);
    MemoryRegion *sysmem = get_system_memory();

    /* Main DDR at the reset vector. */
    memory_region_init_ram(&hms->ram, NULL, "ddr.ram", machine->ram_size,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0x0, &hms->ram);

    /* Config-table ROM and the blob that backs it. */
    memory_region_init_rom(&hms->cfgtable_rom, NULL, "config_table.rom",
                           sizeof(m_cfg->cfgtable), &error_fatal);
    memory_region_add_subregion(sysmem, m_cfg->cfgbase, &hms->cfgtable_rom);
    rom_add_blob_fixed_as("config_table.rom", &m_cfg->cfgtable,
                          sizeof(m_cfg->cfgtable), m_cfg->cfgbase,
                          &address_space_memory);
}
