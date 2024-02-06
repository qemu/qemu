/*
 * Arm MPS3 board emulation for Cortex-R-based FPGA images.
 * (For M-profile images see mps2.c and mps2tz.c.)
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * The MPS3 is an FPGA based dev board. This file handles FPGA images
 * which use the Cortex-R CPUs. We model these separately from the
 * M-profile images, because on M-profile the FPGA image is based on
 * a "Subsystem for Embedded" which is similar to an SoC, whereas
 * the R-profile FPGA images don't have that abstraction layer.
 *
 * We model the following FPGA images here:
 *  "mps3-an536" -- dual Cortex-R52 as documented in Arm Application Note AN536
 *
 * Application Note AN536:
 * https://developer.arm.com/documentation/dai0536/latest/
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"

/* Define the layout of RAM and ROM in a board */
typedef struct RAMInfo {
    const char *name;
    hwaddr base;
    hwaddr size;
    int mrindex; /* index into rams[]; -1 for the system RAM block */
    int flags;
} RAMInfo;

/*
 * The MPS3 DDR is 3GiB, but on a 32-bit host QEMU doesn't permit
 * emulation of that much guest RAM, so artificially make it smaller.
 */
#if HOST_LONG_BITS == 32
#define MPS3_DDR_SIZE (1 * GiB)
#else
#define MPS3_DDR_SIZE (3 * GiB)
#endif

/*
 * Flag values:
 * IS_MAIN: this is the main machine RAM
 * IS_ROM: this area is read-only
 */
#define IS_MAIN 1
#define IS_ROM 2

#define MPS3R_RAM_MAX 9

typedef enum MPS3RFPGAType {
    FPGA_AN536,
} MPS3RFPGAType;

struct MPS3RMachineClass {
    MachineClass parent;
    MPS3RFPGAType fpga_type;
    const RAMInfo *raminfo;
};

struct MPS3RMachineState {
    MachineState parent;
    MemoryRegion ram[MPS3R_RAM_MAX];
};

#define TYPE_MPS3R_MACHINE "mps3r"
#define TYPE_MPS3R_AN536_MACHINE MACHINE_TYPE_NAME("mps3-an536")

OBJECT_DECLARE_TYPE(MPS3RMachineState, MPS3RMachineClass, MPS3R_MACHINE)

static const RAMInfo an536_raminfo[] = {
    {
        .name = "ATCM",
        .base = 0x00000000,
        .size = 0x00008000,
        .mrindex = 0,
    }, {
        /* We model the QSPI flash as simple ROM for now */
        .name = "QSPI",
        .base = 0x08000000,
        .size = 0x00800000,
        .flags = IS_ROM,
        .mrindex = 1,
    }, {
        .name = "BRAM",
        .base = 0x10000000,
        .size = 0x00080000,
        .mrindex = 2,
    }, {
        .name = "DDR",
        .base = 0x20000000,
        .size = MPS3_DDR_SIZE,
        .mrindex = -1,
    }, {
        .name = "ATCM0",
        .base = 0xee000000,
        .size = 0x00008000,
        .mrindex = 3,
    }, {
        .name = "BTCM0",
        .base = 0xee100000,
        .size = 0x00008000,
        .mrindex = 4,
    }, {
        .name = "CTCM0",
        .base = 0xee200000,
        .size = 0x00008000,
        .mrindex = 5,
    }, {
        .name = "ATCM1",
        .base = 0xee400000,
        .size = 0x00008000,
        .mrindex = 6,
    }, {
        .name = "BTCM1",
        .base = 0xee500000,
        .size = 0x00008000,
        .mrindex = 7,
    }, {
        .name = "CTCM1",
        .base = 0xee600000,
        .size = 0x00008000,
        .mrindex = 8,
    }, {
        .name = NULL,
    }
};

static MemoryRegion *mr_for_raminfo(MPS3RMachineState *mms,
                                    const RAMInfo *raminfo)
{
    /* Return an initialized MemoryRegion for the RAMInfo. */
    MemoryRegion *ram;

    if (raminfo->mrindex < 0) {
        /* Means this RAMInfo is for QEMU's "system memory" */
        MachineState *machine = MACHINE(mms);
        assert(!(raminfo->flags & IS_ROM));
        return machine->ram;
    }

    assert(raminfo->mrindex < MPS3R_RAM_MAX);
    ram = &mms->ram[raminfo->mrindex];

    memory_region_init_ram(ram, NULL, raminfo->name,
                           raminfo->size, &error_fatal);
    if (raminfo->flags & IS_ROM) {
        memory_region_set_readonly(ram, true);
    }
    return ram;
}

static void mps3r_common_init(MachineState *machine)
{
    MPS3RMachineState *mms = MPS3R_MACHINE(machine);
    MPS3RMachineClass *mmc = MPS3R_MACHINE_GET_CLASS(mms);
    MemoryRegion *sysmem = get_system_memory();

    for (const RAMInfo *ri = mmc->raminfo; ri->name; ri++) {
        MemoryRegion *mr = mr_for_raminfo(mms, ri);
        memory_region_add_subregion(sysmem, ri->base, mr);
    }
}

static void mps3r_set_default_ram_info(MPS3RMachineClass *mmc)
{
    /*
     * Set mc->default_ram_size and default_ram_id from the
     * information in mmc->raminfo.
     */
    MachineClass *mc = MACHINE_CLASS(mmc);
    const RAMInfo *p;

    for (p = mmc->raminfo; p->name; p++) {
        if (p->mrindex < 0) {
            /* Found the entry for "system memory" */
            mc->default_ram_size = p->size;
            mc->default_ram_id = p->name;
            return;
        }
    }
    g_assert_not_reached();
}

static void mps3r_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = mps3r_common_init;
}

static void mps3r_an536_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MPS3RMachineClass *mmc = MPS3R_MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-r52"),
        NULL
    };

    mc->desc = "ARM MPS3 with AN536 FPGA image for Cortex-R52";
    mc->default_cpus = 2;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-r52");
    mc->valid_cpu_types = valid_cpu_types;
    mmc->raminfo = an536_raminfo;
    mps3r_set_default_ram_info(mmc);
}

static const TypeInfo mps3r_machine_types[] = {
    {
        .name = TYPE_MPS3R_MACHINE,
        .parent = TYPE_MACHINE,
        .abstract = true,
        .instance_size = sizeof(MPS3RMachineState),
        .class_size = sizeof(MPS3RMachineClass),
        .class_init = mps3r_class_init,
    }, {
        .name = TYPE_MPS3R_AN536_MACHINE,
        .parent = TYPE_MPS3R_MACHINE,
        .class_init = mps3r_an536_class_init,
    },
};

DEFINE_TYPES(mps3r_machine_types);
