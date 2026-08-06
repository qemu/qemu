/*
 * Hexagon subsystem helpers shared between the machine models.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hexagon/hex-subsys.h"
#include "hw/hexagon/hexagon_globalreg.h"
#include "hw/hexagon/hexagon_tlb.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "system/address-spaces.h"

static DeviceState *globalreg_create(HexagonCommonMachineState *hms,
                                     const struct hexagon_machine_config *m_cfg,
                                     Rev_t rev)
{
    DeviceState *glob_regs = qdev_new(TYPE_HEXAGON_GLOBALREG);

    object_property_add_child(OBJECT(hms), "global-regs", OBJECT(glob_regs));
    qdev_prop_set_uint64(glob_regs, "config-table-addr", m_cfg->cfgbase);
    qdev_prop_set_uint32(glob_regs, "dsp-rev", rev);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(glob_regs), &error_fatal);

    return glob_regs;
}

static DeviceState *tlb_create(HexagonCommonMachineState *hms,
                               const struct hexagon_machine_config *m_cfg)
{
    DeviceState *tlb = qdev_new(TYPE_HEXAGON_TLB);

    object_property_add_child(OBJECT(hms), "tlb", OBJECT(tlb));
    qdev_prop_set_uint32(tlb, "num-entries", m_cfg->cfgtable.jtlb_size_entries);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(tlb), &error_fatal);

    return tlb;
}

void hex_subsys_create(HexagonCommonMachineState *hms,
                       const struct hexagon_machine_config *m_cfg, Rev_t rev)
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

    if (m_cfg->cfgtable.vtcm_size_kb > 0) {
        memory_region_init_ram(&hms->vtcm, NULL, "vtcm.ram",
                               m_cfg->cfgtable.vtcm_size_kb * 1024,
                               &error_fatal);
        memory_region_add_subregion(sysmem, m_cfg->cfgtable.vtcm_base << 16,
                                    &hms->vtcm);
    }

    hms->glob_regs = globalreg_create(hms, m_cfg, rev);
    hms->tlb = tlb_create(hms, m_cfg);
}

void hex_subsys_realize_cpu(HexagonCommonMachineState *hms, DeviceState *cpu)
{
    object_property_set_link(OBJECT(cpu), "global-regs",
                             OBJECT(hms->glob_regs), &error_fatal);
    object_property_set_link(OBJECT(cpu), "tlb", OBJECT(hms->tlb),
                             &error_fatal);
    qdev_realize_and_unref(cpu, NULL, &error_fatal);
}
