/*
 * NXP i.MX 8M Plus Evaluation Kit System Emulation
 *
 * Copyright (c) 2024, Bernhard Beschow <shentey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/address-spaces.h"
#include "hw/arm/boot.h"
#include "hw/arm/fsl-imx8mp.h"
#include "hw/arm/machines-qom.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "system/kvm.h"
#include "system/qtest.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include <libfdt.h>

static void imx8mp_evk_modify_dtb(const struct arm_boot_info *info, void *fdt)
{
    int i, offset;

    /* Temporarily disable following nodes until they are implemented */
    const char *nodes_to_remove[] = {
        "nxp,imx8mp-fspi",
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

static void imx8mp_evk_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    FslImx8mpState *s;

    if (machine->ram_size > FSL_IMX8MP_RAM_SIZE_MAX) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08" PRIx64 ")",
                     machine->ram_size, FSL_IMX8MP_RAM_SIZE_MAX);
        exit(1);
    }

    boot_info = (struct arm_boot_info) {
        .loader_start = FSL_IMX8MP_RAM_START,
        .board_id = -1,
        .ram_size = machine->ram_size,
        .psci_conduit = QEMU_PSCI_CONDUIT_SMC,
        .modify_dtb = imx8mp_evk_modify_dtb,
    };

    s = FSL_IMX8MP(object_new(TYPE_FSL_IMX8MP));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(s));
    object_property_set_uint(OBJECT(s), "fec1-phy-num", 1, &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    memory_region_add_subregion(get_system_memory(), FSL_IMX8MP_RAM_START,
                                machine->ram);

    for (int i = 0; i < FSL_IMX8MP_NUM_USDHCS; i++) {
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
        arm_load_kernel(&s->cpu[0], machine, &boot_info);
    }
}

static const char *imx8mp_evk_get_default_cpu_type(const MachineState *ms)
{
    if (kvm_enabled()) {
        return ARM_CPU_TYPE_NAME("host");
    }

    return ARM_CPU_TYPE_NAME("cortex-a53");
}

static void imx8mp_evk_machine_init(MachineClass *mc)
{
    mc->desc = "NXP i.MX 8M Plus EVK Board";
    mc->init = imx8mp_evk_init;
    mc->max_cpus = FSL_IMX8MP_NUM_CPUS;
    mc->default_ram_id = "imx8mp-evk.ram";
    mc->get_default_cpu_type = imx8mp_evk_get_default_cpu_type;
}

DEFINE_MACHINE_AARCH64("imx8mp-evk", imx8mp_evk_machine_init)
