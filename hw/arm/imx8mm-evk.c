/*
 * NXP i.MX 8MM Evaluation Kit System Emulation
 *
 * Copyright (c) 2025, NXP Semiconductors
 * Author: Gaurav Sharma <gaurav.sharma_7@nxp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/boot.h"
#include "hw/arm/fsl-imx8mm.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "system/kvm.h"
#include "system/qtest.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include <libfdt.h>

#define TYPE_IMX8MM_EVK_MACHINE MACHINE_TYPE_NAME("imx8mm-evk")
OBJECT_DECLARE_SIMPLE_TYPE(Imx8mmEvkMachineState, IMX8MM_EVK_MACHINE)

struct Imx8mmEvkMachineState {
    MachineState parent;

    struct arm_boot_info bootinfo;
};

static void imx8mm_evk_modify_dtb(const struct arm_boot_info *info, void *fdt)
{
    int i, offset;

    /* Temporarily disable following nodes until they are implemented */
    const char *nodes_to_remove[] = {
        "nxp,imx8mm-fspi",
        "fsl,imx8mm-mipi-csi",
        "fsl,imx8mm-mipi-dsim"
    };

    for (i = 0; i < ARRAY_SIZE(nodes_to_remove); i++) {
        const char *dev_str = nodes_to_remove[i];

        offset = fdt_node_offset_by_compatible(fdt, -1, dev_str);
        while (offset >= 0) {
            fdt_nop_node(fdt, offset);
            offset = fdt_node_offset_by_compatible(fdt, offset, dev_str);
        }
    }

    /* Remove cpu-idle-states property from CPU nodes */
    offset = fdt_node_offset_by_compatible(fdt, -1, "arm,cortex-a53");
    while (offset >= 0) {
        fdt_nop_property(fdt, offset, "cpu-idle-states");
        offset = fdt_node_offset_by_compatible(fdt, offset, "arm,cortex-a53");
    }

    if (kvm_enabled()) {
        /* Use system counter frequency from host CPU to fix time in guest */
        offset = fdt_node_offset_by_compatible(fdt, -1, "arm,armv8-timer");
        while (offset >= 0) {
            fdt_nop_property(fdt, offset, "clock-frequency");
            offset = fdt_node_offset_by_compatible(fdt, offset, "arm,armv8-timer");
        }
    }
}

static void imx8mm_evk_init(MachineState *machine)
{
    Imx8mmEvkMachineState *ims = IMX8MM_EVK_MACHINE(machine);
    FslImx8mmState *s;

    if (machine->ram_size > FSL_IMX8MM_RAM_SIZE_MAX) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08" PRIx64 ")",
                     machine->ram_size, FSL_IMX8MM_RAM_SIZE_MAX);
        exit(1);
    }

    ims->bootinfo.loader_start = FSL_IMX8MM_RAM_START;
    ims->bootinfo.board_id = -1;
    ims->bootinfo.ram_size = machine->ram_size;
    ims->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    ims->bootinfo.modify_dtb = imx8mm_evk_modify_dtb;

    s = FSL_IMX8MM(object_new_with_props(TYPE_FSL_IMX8MM, OBJECT(machine),
                                         "soc", &error_fatal, NULL));
    object_property_set_uint(OBJECT(s), "fec1-phy-num", 1, &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    memory_region_add_subregion(get_system_memory(), FSL_IMX8MM_RAM_START,
                                machine->ram);

    for (int i = 0; i < FSL_IMX8MM_NUM_USDHCS; i++) {
        BusState *bus;
        DeviceState *carddev;
        BlockBackend *blk;
        DriveInfo *di = drive_get(IF_SD, i, 0);

        if (!di) {
            continue;
        }

        blk = blk_by_legacy_dinfo(di);
        bus = qdev_get_child_bus(DEVICE(&s->usdhc[i]), "sd-bus");
        carddev = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
        qdev_realize_and_unref(carddev, bus, &error_fatal);
    }

    if (!qtest_enabled()) {
        arm_load_kernel(&s->cpu[0], machine, &ims->bootinfo);
    }
}

static const char *imx8mm_evk_get_default_cpu_type(const MachineState *ms)
{
    if (kvm_enabled()) {
        return ARM_CPU_TYPE_NAME("host");
    }

    return ARM_CPU_TYPE_NAME("cortex-a53");
}

static void imx8mm_evk_machine_init(MachineClass *mc)
{
    mc->desc = "NXP i.MX 8MM EVK Board";
    mc->init = imx8mm_evk_init;
    mc->max_cpus = FSL_IMX8MM_NUM_CPUS;
    mc->default_cpus = FSL_IMX8MM_NUM_CPUS;
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "imx8mm-evk.ram";
    mc->get_default_cpu_type = imx8mm_evk_get_default_cpu_type;
}

DEFINE_MACHINE_EXTENDED("imx8mm-evk", MACHINE, Imx8mmEvkMachineState,
                        imx8mm_evk_machine_init, false,
                        NULL)
