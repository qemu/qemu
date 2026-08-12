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
#include "hw/intc/hex-l2vic.h"
#include "hw/timer/qct-qtimer.h"
#include "hw/cpu/cluster.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "system/address-spaces.h"

#define HEX_L2VIC_CPU_IRQS 8

/* Number of QTimer frames instantiated for every Hexagon machine. */
#define HEX_QTIMER_NR_FRAMES 3

#define HEX_QTIMER_L2VIC_IRQ_BASE 2

static DeviceState *l2vic_create(HexagonCommonMachineState *hms,
                                 const struct hexagon_machine_config *m_cfg)
{
    DeviceState *l2vic = qdev_new(TYPE_HEX_L2VIC);

    object_property_add_child(OBJECT(hms), "l2vic", OBJECT(l2vic));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(l2vic), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(l2vic), 0, m_cfg->l2vic_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(l2vic), 1,
                    m_cfg->cfgtable.fastl2vic_base << 16);

    return l2vic;
}

static void l2vic_connect_cpu(DeviceState *l2vic, DeviceState *cpu)
{
    int i;

    for (i = 0; i < HEX_L2VIC_CPU_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(l2vic), i, qdev_get_gpio_in(cpu, i));
    }
}

static DeviceState *qtimer_create(HexagonCommonMachineState *hms,
                                  const struct hexagon_machine_config *m_cfg)
{
    DeviceState *qtimer = qdev_new(TYPE_QCT_QTIMER);

    object_property_add_child(OBJECT(hms), "qtimer", OBJECT(qtimer));
    qdev_prop_set_uint32(qtimer, "nr_frames", HEX_QTIMER_NR_FRAMES);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(qtimer), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(qtimer), 0, m_cfg->csr_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(qtimer), 1, m_cfg->qtmr_region);
    for (unsigned int i = 0; i < HEX_QTIMER_NR_FRAMES; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(qtimer), i,
                           qdev_get_gpio_in(hms->l2vic,
                                            HEX_QTIMER_L2VIC_IRQ_BASE + i));
    }

    return qtimer;
}

static DeviceState *globalreg_create(HexagonCommonMachineState *hms,
                                     const struct hexagon_machine_config *m_cfg,
                                     Rev_t rev)
{
    DeviceState *glob_regs = qdev_new(TYPE_HEXAGON_GLOBALREG);

    object_property_add_child(OBJECT(hms), "global-regs", OBJECT(glob_regs));
    qdev_prop_set_uint64(glob_regs, "config-table-addr", m_cfg->cfgbase);
    qdev_prop_set_uint32(glob_regs, "dsp-rev", rev);
    object_property_set_link(OBJECT(glob_regs), "l2vic", OBJECT(hms->l2vic),
                             &error_fatal);
    object_property_set_link(OBJECT(glob_regs), "qtimer", OBJECT(hms->qtimer),
                             &error_fatal);
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

static DeviceState *cluster_create(HexagonCommonMachineState *hms)
{
    DeviceState *cluster = qdev_new(TYPE_CPU_CLUSTER);

    object_property_add_child(OBJECT(hms), "cluster", OBJECT(cluster));
    qdev_prop_set_uint32(cluster, "cluster-id", 0);

    return cluster;
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

    hms->cluster = cluster_create(hms);
    hms->l2vic = l2vic_create(hms, m_cfg);
    hms->qtimer = qtimer_create(hms, m_cfg);
    hms->glob_regs = globalreg_create(hms, m_cfg, rev);
    hms->tlb = tlb_create(hms, m_cfg);
}

void hex_subsys_add_cpu(HexagonCommonMachineState *hms, DeviceState *cpu)
{
    object_property_add_child(OBJECT(hms->cluster), "cpu[*]", OBJECT(cpu));
    object_property_set_link(OBJECT(cpu), "global-regs",
                             OBJECT(hms->glob_regs), &error_fatal);
    object_property_set_link(OBJECT(cpu), "tlb", OBJECT(hms->tlb),
                             &error_fatal);
    object_property_set_link(OBJECT(cpu), "l2vic", OBJECT(hms->l2vic),
                             &error_fatal);
}

void hex_subsys_realize_cluster(HexagonCommonMachineState *hms)
{
    /*
     * The cluster must be realized after its CPUs have been parented into it
     * (see hex_subsys_add_cpu()) but before any CPU is itself realized, since
     * qdev_realize_and_unref() on a CPU latches cluster_index into the TCG
     * cflags at that point.
     */
    qdev_realize_and_unref(hms->cluster, NULL, &error_fatal);
}

void hex_subsys_realize_cpu(HexagonCommonMachineState *hms, DeviceState *cpu,
                            bool boot_cpu)
{
    qdev_realize_and_unref(cpu, NULL, &error_fatal);

    if (boot_cpu) {
        l2vic_connect_cpu(hms->l2vic, cpu);
    }
}
