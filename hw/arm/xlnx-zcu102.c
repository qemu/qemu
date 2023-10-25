/*
 * Xilinx ZynqMP ZCU102 board
 *
 * Copyright (C) 2015 Xilinx Inc
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/xlnx-zynqmp.h"
#include "hw/arm/boot.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "sysemu/device_tree.h"
#include "qom/object.h"
#include "net/can_emu.h"
#include "audio/audio.h"

struct XlnxZCU102 {
    MachineState parent_obj;

    XlnxZynqMPState soc;

    bool secure;
    bool virt;

    CanBusState *canbus[XLNX_ZYNQMP_NUM_CAN];

    struct arm_boot_info binfo;
};

#define TYPE_ZCU102_MACHINE   MACHINE_TYPE_NAME("xlnx-zcu102")
OBJECT_DECLARE_SIMPLE_TYPE(XlnxZCU102, ZCU102_MACHINE)


static bool zcu102_get_secure(Object *obj, Error **errp)
{
    XlnxZCU102 *s = ZCU102_MACHINE(obj);

    return s->secure;
}

static void zcu102_set_secure(Object *obj, bool value, Error **errp)
{
    XlnxZCU102 *s = ZCU102_MACHINE(obj);

    s->secure = value;
}

static bool zcu102_get_virt(Object *obj, Error **errp)
{
    XlnxZCU102 *s = ZCU102_MACHINE(obj);

    return s->virt;
}

static void zcu102_set_virt(Object *obj, bool value, Error **errp)
{
    XlnxZCU102 *s = ZCU102_MACHINE(obj);

    s->virt = value;
}

static void zcu102_modify_dtb(const struct arm_boot_info *binfo, void *fdt)
{
    XlnxZCU102 *s = container_of(binfo, XlnxZCU102, binfo);
    bool method_is_hvc;
    char **node_path;
    const char *r;
    int prop_len;
    int i;

    /* If EL3 is enabled, we keep all firmware nodes active.  */
    if (!s->secure) {
        node_path = qemu_fdt_node_path(fdt, NULL, "xlnx,zynqmp-firmware",
                                       &error_fatal);

        for (i = 0; node_path && node_path[i]; i++) {
            r = qemu_fdt_getprop(fdt, node_path[i], "method", &prop_len, NULL);
            method_is_hvc = r && !strcmp("hvc", r);

            /* Allow HVC based firmware if EL2 is enabled.  */
            if (method_is_hvc && s->virt) {
                continue;
            }
            qemu_fdt_setprop_string(fdt, node_path[i], "status", "disabled");
        }
        g_strfreev(node_path);
    }
}

static void bbram_attach_drive(XlnxBBRam *dev)
{
    DriveInfo *dinfo;
    BlockBackend *blk;

    dinfo = drive_get_by_index(IF_PFLASH, 2);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    if (blk) {
        qdev_prop_set_drive(DEVICE(dev), "drive", blk);
    }
}

static void efuse_attach_drive(XlnxEFuse *dev)
{
    DriveInfo *dinfo;
    BlockBackend *blk;

    dinfo = drive_get_by_index(IF_PFLASH, 3);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    if (blk) {
        qdev_prop_set_drive(DEVICE(dev), "drive", blk);
    }
}

static void xlnx_zcu102_init(MachineState *machine)
{
    XlnxZCU102 *s = ZCU102_MACHINE(machine);
    int i;
    uint64_t ram_size = machine->ram_size;

    /* Create the memory region to pass to the SoC */
    if (ram_size > XLNX_ZYNQMP_MAX_RAM_SIZE) {
        error_report("ERROR: RAM size 0x%" PRIx64 " above max supported of "
                     "0x%llx", ram_size,
                     XLNX_ZYNQMP_MAX_RAM_SIZE);
        exit(1);
    }

    if (ram_size < 0x08000000) {
        qemu_log("WARNING: RAM size 0x%" PRIx64 " is small for ZCU102",
                 ram_size);
    }

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_XLNX_ZYNQMP);

    if (machine->audiodev) {
        qdev_prop_set_string(DEVICE(&s->soc.dp), "audiodev", machine->audiodev);
    }

    object_property_set_link(OBJECT(&s->soc), "ddr-ram", OBJECT(machine->ram),
                             &error_abort);
    object_property_set_bool(OBJECT(&s->soc), "secure", s->secure,
                             &error_fatal);
    object_property_set_bool(OBJECT(&s->soc), "virtualization", s->virt,
                             &error_fatal);

    for (i = 0; i < XLNX_ZYNQMP_NUM_CAN; i++) {
        gchar *bus_name = g_strdup_printf("canbus%d", i);

        object_property_set_link(OBJECT(&s->soc), bus_name,
                                 OBJECT(s->canbus[i]), &error_fatal);
        g_free(bus_name);
    }

    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* Attach bbram backend, if given */
    bbram_attach_drive(&s->soc.bbram);

    /* Attach efuse backend, if given */
    efuse_attach_drive(&s->soc.efuse);

    /* Create and plug in the SD cards */
    for (i = 0; i < XLNX_ZYNQMP_NUM_SDHCI; i++) {
        BusState *bus;
        DriveInfo *di = drive_get(IF_SD, 0, i);
        BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;
        DeviceState *carddev;
        char *bus_name;

        bus_name = g_strdup_printf("sd-bus%d", i);
        bus = qdev_get_child_bus(DEVICE(&s->soc), bus_name);
        g_free(bus_name);
        if (!bus) {
            error_report("No SD bus found for SD card %d", i);
            exit(1);
        }
        carddev = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
        qdev_realize_and_unref(carddev, bus, &error_fatal);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_SPIS; i++) {
        BusState *spi_bus;
        DeviceState *flash_dev;
        qemu_irq cs_line;
        DriveInfo *dinfo = drive_get(IF_MTD, 0, i);
        gchar *bus_name = g_strdup_printf("spi%d", i);

        spi_bus = qdev_get_child_bus(DEVICE(&s->soc), bus_name);
        g_free(bus_name);

        flash_dev = qdev_new("sst25wf080");
        if (dinfo) {
            qdev_prop_set_drive_err(flash_dev, "drive",
                                    blk_by_legacy_dinfo(dinfo), &error_fatal);
        }
        qdev_prop_set_uint8(flash_dev, "cs", i);
        qdev_realize_and_unref(flash_dev, spi_bus, &error_fatal);

        cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->soc.spi[i]), 1, cs_line);
    }

    for (i = 0; i < XLNX_ZYNQMP_NUM_QSPI_FLASH; i++) {
        BusState *spi_bus;
        DeviceState *flash_dev;
        qemu_irq cs_line;
        DriveInfo *dinfo = drive_get(IF_MTD, 0, XLNX_ZYNQMP_NUM_SPIS + i);
        int bus = i / XLNX_ZYNQMP_NUM_QSPI_BUS_CS;
        gchar *bus_name = g_strdup_printf("qspi%d", bus);

        spi_bus = qdev_get_child_bus(DEVICE(&s->soc), bus_name);
        g_free(bus_name);

        flash_dev = qdev_new("n25q512a11");
        if (dinfo) {
            qdev_prop_set_drive_err(flash_dev, "drive",
                                    blk_by_legacy_dinfo(dinfo), &error_fatal);
        }
        qdev_prop_set_uint8(flash_dev, "cs", i);
        qdev_realize_and_unref(flash_dev, spi_bus, &error_fatal);

        cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);

        sysbus_connect_irq(SYS_BUS_DEVICE(&s->soc.qspi), i + 1, cs_line);
    }

    /* TODO create and connect IDE devices for ide_drive_get() */

    s->binfo.ram_size = ram_size;
    s->binfo.loader_start = 0;
    s->binfo.modify_dtb = zcu102_modify_dtb;
    s->binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    arm_load_kernel(s->soc.boot_cpu_ptr, machine, &s->binfo);
}

static void xlnx_zcu102_machine_instance_init(Object *obj)
{
    XlnxZCU102 *s = ZCU102_MACHINE(obj);

    /* Default to secure mode being disabled */
    s->secure = false;
    /* Default to virt (EL2) being disabled */
    s->virt = false;
    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&s->canbus[0],
                             object_property_allow_set_link,
                             0);

    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&s->canbus[1],
                             object_property_allow_set_link,
                             0);
}

static void xlnx_zcu102_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Xilinx ZynqMP ZCU102 board with 4xA53s and 2xR5Fs based on " \
               "the value of smp";
    mc->init = xlnx_zcu102_init;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->ignore_memory_transaction_failures = true;
    mc->max_cpus = XLNX_ZYNQMP_NUM_APU_CPUS + XLNX_ZYNQMP_NUM_RPU_CPUS;
    mc->default_cpus = XLNX_ZYNQMP_NUM_APU_CPUS;
    mc->default_ram_id = "ddr-ram";

    machine_add_audiodev_property(mc);
    object_class_property_add_bool(oc, "secure", zcu102_get_secure,
                                   zcu102_set_secure);
    object_class_property_set_description(oc, "secure",
                                          "Set on/off to enable/disable the ARM "
                                          "Security Extensions (TrustZone)");

    object_class_property_add_bool(oc, "virtualization", zcu102_get_virt,
                                   zcu102_set_virt);
    object_class_property_set_description(oc, "virtualization",
                                          "Set on/off to enable/disable emulating a "
                                          "guest CPU which implements the ARM "
                                          "Virtualization Extensions");
}

static const TypeInfo xlnx_zcu102_machine_init_typeinfo = {
    .name       = TYPE_ZCU102_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = xlnx_zcu102_machine_class_init,
    .instance_init = xlnx_zcu102_machine_instance_init,
    .instance_size = sizeof(XlnxZCU102),
};

static void xlnx_zcu102_machine_init_register_types(void)
{
    type_register_static(&xlnx_zcu102_machine_init_typeinfo);
}

type_init(xlnx_zcu102_machine_init_register_types)
