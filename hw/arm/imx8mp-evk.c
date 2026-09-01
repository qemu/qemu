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
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "system/kvm.h"
#include "system/qtest.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include <libfdt.h>

#define TYPE_IMX8MPEVK_MACHINE MACHINE_TYPE_NAME("imx8mp-evk")
OBJECT_DECLARE_SIMPLE_TYPE(FslImx8mpEvkState, IMX8MPEVK_MACHINE)

struct FslImx8mpEvkState {
    MachineState parent_obj;

    FslImx8mpState soc;
    CanBusState *canbus[FSL_IMX8MP_NUM_CANS];

    struct arm_boot_info boot_info;
};

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
    FslImx8mpEvkState *s = IMX8MPEVK_MACHINE(machine);

    if (machine->ram_size > FSL_IMX8MP_RAM_SIZE_MAX) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08" PRIx64 ")",
                     machine->ram_size, FSL_IMX8MP_RAM_SIZE_MAX);
        exit(1);
    }

    s->boot_info = (struct arm_boot_info) {
        .loader_start = FSL_IMX8MP_RAM_START,
        .board_id = -1,
        .ram_size = machine->ram_size,
        .psci_conduit = QEMU_PSCI_CONDUIT_SMC,
        .modify_dtb = imx8mp_evk_modify_dtb,
    };

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_FSL_IMX8MP);
    object_property_set_uint(OBJECT(&s->soc), "fec1-phy-num", 1, &error_fatal);
    for (int i = 0; i < FSL_IMX8MP_NUM_CANS; i++) {
        g_autofree char *bus_name = g_strdup_printf("canbus%d", i);

        object_property_set_link(OBJECT(&s->soc), bus_name,
                                 OBJECT(s->canbus[i]), &error_fatal);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(&s->soc), &error_fatal);

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
        bus = qdev_get_child_bus(DEVICE(&s->soc.usdhc[i]), "sd-bus");
        carddev = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
        qdev_realize_and_unref(carddev, bus, &error_fatal);
    }

    if (!qtest_enabled()) {
        arm_load_kernel(&s->soc.cpu[0], machine, &s->boot_info);
    }
}

static const char *imx8mp_evk_get_default_cpu_type(const MachineState *ms)
{
    if (kvm_enabled()) {
        return ARM_CPU_TYPE_NAME("host");
    }

    return ARM_CPU_TYPE_NAME("cortex-a53");
}

static void imx8mp_evk_machine_init(Object *obj)
{
    FslImx8mpEvkState *s = IMX8MPEVK_MACHINE(obj);

    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&s->canbus[0],
                             object_property_allow_set_link, 0);

    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&s->canbus[1],
                             object_property_allow_set_link, 0);
}

static void imx8mp_evk_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "NXP i.MX 8M Plus EVK Board";
    mc->init = imx8mp_evk_init;
    mc->default_cpus = 4;
    mc->max_cpus = FSL_IMX8MP_NUM_CPUS;
    mc->default_ram_id = "imx8mp-evk.ram";
    mc->default_ram_size = 6 * GiB;
    mc->get_default_cpu_type = imx8mp_evk_get_default_cpu_type;
}

static const TypeInfo imx8mp_evk_machine_types[] = {
    {
        .name = TYPE_IMX8MPEVK_MACHINE,
        .parent = TYPE_MACHINE,
        .class_init = imx8mp_evk_machine_class_init,
        .instance_init = imx8mp_evk_machine_init,
        .instance_size = sizeof(FslImx8mpEvkState),
    },
};

DEFINE_TYPES(imx8mp_evk_machine_types)
