/*
 * Xilinx Versal Virtual board.
 *
 * Copyright (c) 2018 Xilinx Inc.
 * Copyright (c) 2025 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/device_tree.h"
#include "system/address-spaces.h"
#include "hw/block/flash.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/fdt.h"
#include "hw/qdev-properties.h"
#include "hw/arm/xlnx-versal.h"
#include "hw/arm/boot.h"
#include "target/arm/multiprocessing.h"
#include "qom/object.h"
#include "target/arm/cpu.h"

#define TYPE_XLNX_VERSAL_VIRT_MACHINE MACHINE_TYPE_NAME("xlnx-versal-virt")
OBJECT_DECLARE_SIMPLE_TYPE(VersalVirt, XLNX_VERSAL_VIRT_MACHINE)

#define XLNX_VERSAL_NUM_OSPI_FLASH 4

struct VersalVirt {
    MachineState parent_obj;

    Versal soc;

    void *fdt;
    int fdt_size;
    struct {
        uint32_t clk_125Mhz;
        uint32_t clk_25Mhz;
    } phandle;
    struct arm_boot_info binfo;

    CanBusState **canbus;

    struct {
        bool secure;
        char *ospi_model;
    } cfg;
};

static void fdt_create(VersalVirt *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);

    s->fdt = create_device_tree(&s->fdt_size);
    if (!s->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    /* Allocate all phandles.  */
    s->phandle.clk_25Mhz = qemu_fdt_alloc_phandle(s->fdt);
    s->phandle.clk_125Mhz = qemu_fdt_alloc_phandle(s->fdt);

    /* Create /chosen node for load_dtb.  */
    qemu_fdt_add_subnode(s->fdt, "/chosen");
    qemu_fdt_add_subnode(s->fdt, "/aliases");

    /* Header */
    qemu_fdt_setprop_string(s->fdt, "/", "model", mc->desc);
    qemu_fdt_setprop_string(s->fdt, "/", "compatible", "xlnx-versal-virt");
}

static void fdt_add_clk_node(VersalVirt *s, const char *name,
                             unsigned int freq_hz, uint32_t phandle)
{
    qemu_fdt_add_subnode(s->fdt, name);
    qemu_fdt_setprop_cell(s->fdt, name, "phandle", phandle);
    qemu_fdt_setprop_cell(s->fdt, name, "clock-frequency", freq_hz);
    qemu_fdt_setprop_cell(s->fdt, name, "#clock-cells", 0x0);
    qemu_fdt_setprop_string(s->fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop(s->fdt, name, "u-boot,dm-pre-reloc", NULL, 0);
}

static void fdt_nop_memory_nodes(void *fdt, Error **errp)
{
    Error *err = NULL;
    char **node_path;
    int n = 0;

    node_path = qemu_fdt_node_unit_path(fdt, "memory", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    while (node_path[n]) {
        if (g_str_has_prefix(node_path[n], "/memory")) {
            qemu_fdt_nop_node(fdt, node_path[n]);
        }
        n++;
    }
    g_strfreev(node_path);
}

static void fdt_add_memory_nodes(VersalVirt *s, void *fdt, uint64_t ram_size)
{
    /* Describes the various split DDR access regions.  */
    static const struct {
        uint64_t base;
        uint64_t size;
    } addr_ranges[] = {
        { MM_TOP_DDR, MM_TOP_DDR_SIZE },
        { MM_TOP_DDR_2, MM_TOP_DDR_2_SIZE },
        { MM_TOP_DDR_3, MM_TOP_DDR_3_SIZE },
        { MM_TOP_DDR_4, MM_TOP_DDR_4_SIZE }
    };
    uint64_t mem_reg_prop[8] = {0};
    uint64_t size = ram_size;
    Error *err = NULL;
    char *name;
    int i;

    fdt_nop_memory_nodes(fdt, &err);
    if (err) {
        error_report_err(err);
        return;
    }

    name = g_strdup_printf("/memory@%x", MM_TOP_DDR);
    for (i = 0; i < ARRAY_SIZE(addr_ranges) && size; i++) {
        uint64_t mapsize;

        mapsize = size < addr_ranges[i].size ? size : addr_ranges[i].size;

        mem_reg_prop[i * 2] = addr_ranges[i].base;
        mem_reg_prop[i * 2 + 1] = mapsize;
        size -= mapsize;
    }
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "device_type", "memory");

    switch (i) {
    case 1:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1]);
        break;
    case 2:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3]);
        break;
    case 3:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3],
                                     2, mem_reg_prop[4],
                                     2, mem_reg_prop[5]);
        break;
    case 4:
        qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                     2, mem_reg_prop[0],
                                     2, mem_reg_prop[1],
                                     2, mem_reg_prop[2],
                                     2, mem_reg_prop[3],
                                     2, mem_reg_prop[4],
                                     2, mem_reg_prop[5],
                                     2, mem_reg_prop[6],
                                     2, mem_reg_prop[7]);
        break;
    default:
        g_assert_not_reached();
    }
    g_free(name);
}

static void versal_virt_modify_dtb(const struct arm_boot_info *binfo,
                                    void *fdt)
{
    VersalVirt *s = container_of(binfo, VersalVirt, binfo);

    fdt_add_memory_nodes(s, fdt, binfo->ram_size);
}

static void *versal_virt_get_dtb(const struct arm_boot_info *binfo,
                                  int *fdt_size)
{
    const VersalVirt *board = container_of(binfo, VersalVirt, binfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

#define NUM_VIRTIO_TRANSPORT 8
static void create_virtio_regions(VersalVirt *s)
{
    int virtio_mmio_size = 0x200;
    int i;

    for (i = 0; i < NUM_VIRTIO_TRANSPORT; i++) {
        hwaddr base = versal_get_reserved_mmio_addr(&s->soc)
            + i * virtio_mmio_size;
        g_autofree char *node = g_strdup_printf("/virtio_mmio@%" PRIx64, base);
        int dtb_irq;
        MemoryRegion *mr;
        DeviceState *dev;
        qemu_irq pic_irq;

        pic_irq = versal_get_reserved_irq(&s->soc, i, &dtb_irq);
        dev = qdev_new("virtio-mmio");
        object_property_add_child(OBJECT(s), "virtio-mmio[*]", OBJECT(dev));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic_irq);
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
        memory_region_add_subregion(&s->soc.mr_ps, base, mr);

        qemu_fdt_add_subnode(s->fdt, node);
        qemu_fdt_setprop(s->fdt, node, "dma-coherent", NULL, 0);
        qemu_fdt_setprop_cells(s->fdt, node, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, dtb_irq,
                               GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);
        qemu_fdt_setprop_sized_cells(s->fdt, node, "reg",
                                     2, base, 2, virtio_mmio_size);
        qemu_fdt_setprop_string(s->fdt, node, "compatible", "virtio,mmio");
    }
}

static void bbram_attach_drive(VersalVirt *s)
{
    DriveInfo *dinfo;
    BlockBackend *blk;

    dinfo = drive_get_by_index(IF_PFLASH, 0);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    if (blk) {
        versal_bbram_attach_drive(&s->soc, blk);
    }
}

static void efuse_attach_drive(VersalVirt *s)
{
    DriveInfo *dinfo;
    BlockBackend *blk;

    dinfo = drive_get_by_index(IF_PFLASH, 1);
    blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
    if (blk) {
        versal_efuse_attach_drive(&s->soc, blk);
    }
}

static void sd_plug_card(VersalVirt *s, int idx, DriveInfo *di)
{
    BlockBackend *blk = di ? blk_by_legacy_dinfo(di) : NULL;

    versal_sdhci_plug_card(&s->soc, idx, blk);
}

static char *versal_get_ospi_model(Object *obj, Error **errp)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);

    return g_strdup(s->cfg.ospi_model);
}

static void versal_set_ospi_model(Object *obj, const char *value, Error **errp)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);

    g_free(s->cfg.ospi_model);
    s->cfg.ospi_model = g_strdup(value);
}


static void versal_virt_init(MachineState *machine)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(machine);
    int psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;
    int i;

    /*
     * If the user provides an Operating System to be loaded, we expect them
     * to use the -kernel command line option.
     *
     * Users can load firmware or boot-loaders with the -device loader options.
     *
     * When loading an OS, we generate a dtb and let arm_load_kernel() select
     * where it gets loaded. This dtb will be passed to the kernel in x0.
     *
     * If there's no -kernel option, we generate a DTB and place it at 0x1000
     * for the bootloaders or firmware to pick up.
     *
     * If users want to provide their own DTB, they can use the -dtb option.
     * These dtb's will have their memory nodes modified to match QEMU's
     * selected ram_size option before they get passed to the kernel or fw.
     *
     * When loading an OS, we turn on QEMU's PSCI implementation with SMC
     * as the PSCI conduit. When there's no -kernel, we assume the user
     * provides EL3 firmware to handle PSCI.
     *
     * Even if the user provides a kernel filename, arm_load_kernel()
     * may suppress PSCI if it's going to boot that guest code at EL3.
     */
    if (machine->kernel_filename) {
        psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    }

    object_initialize_child(OBJECT(machine), "xlnx-versal", &s->soc,
                            TYPE_XLNX_VERSAL);
    object_property_set_link(OBJECT(&s->soc), "ddr", OBJECT(machine->ram),
                             &error_abort);

    for (i = 0; i < versal_get_num_can(VERSAL_VER_VERSAL); i++) {
        g_autofree char *prop_name = g_strdup_printf("canbus%d", i);

        object_property_set_link(OBJECT(&s->soc), prop_name,
                                 OBJECT(s->canbus[i]),
                                 &error_abort);
    }

    fdt_create(s);
    versal_set_fdt(&s->soc, s->fdt);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);
    create_virtio_regions(s);

    fdt_add_clk_node(s, "/old-clk125", 125000000, s->phandle.clk_125Mhz);
    fdt_add_clk_node(s, "/old-clk25", 25000000, s->phandle.clk_25Mhz);

    /*
     * Map the SoC address space onto system memory. This will allow virtio and
     * other modules unaware of multiple address-spaces to work.
     */
    memory_region_add_subregion(get_system_memory(), 0, &s->soc.mr_ps);

    /* Attach bbram backend, if given */
    bbram_attach_drive(s);

    /* Attach efuse backend, if given */
    efuse_attach_drive(s);

    /* Plug SD cards */
    for (i = 0; i < versal_get_num_sdhci(VERSAL_VER_VERSAL); i++) {
        sd_plug_card(s, i, drive_get(IF_SD, 0, i));
    }

    s->binfo.ram_size = machine->ram_size;
    s->binfo.loader_start = 0x0;
    s->binfo.get_dtb = versal_virt_get_dtb;
    s->binfo.modify_dtb = versal_virt_modify_dtb;
    s->binfo.psci_conduit = psci_conduit;
    if (!machine->kernel_filename) {
        /* Some boot-loaders (e.g u-boot) don't like blobs at address 0 (NULL).
         * Offset things by 4K.  */
        s->binfo.loader_start = 0x1000;
        s->binfo.dtb_limit = 0x1000000;
    }
    arm_load_kernel(ARM_CPU(versal_get_boot_cpu(&s->soc)), machine, &s->binfo);

    for (i = 0; i < XLNX_VERSAL_NUM_OSPI_FLASH; i++) {
        ObjectClass *flash_klass;
        DriveInfo *dinfo = drive_get(IF_MTD, 0, i);
        BlockBackend *blk;
        const char *mdl;

        if (s->cfg.ospi_model) {
            flash_klass = object_class_by_name(s->cfg.ospi_model);
            if (!flash_klass ||
                object_class_is_abstract(flash_klass) ||
                !object_class_dynamic_cast(flash_klass, TYPE_M25P80)) {
                error_report("'%s' is either abstract or"
                       " not a subtype of m25p80", s->cfg.ospi_model);
                exit(1);
            }
            mdl = s->cfg.ospi_model;
        } else {
            mdl = "mt35xu01g";
        }

        blk = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
        versal_ospi_create_flash(&s->soc, i, mdl, blk);
    }
}

static void versal_virt_machine_instance_init(Object *obj)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);
    size_t i, num_can;

    num_can = versal_get_num_can(VERSAL_VER_VERSAL);
    s->canbus = g_new0(CanBusState *, num_can);

    /*
     * User can set canbusx properties to can-bus object and optionally connect
     * to socketcan interface via command line.
     */
    for (i = 0; i < num_can; i++) {
        g_autofree char *prop_name = g_strdup_printf("canbus%zu", i);

        object_property_add_link(obj, prop_name, TYPE_CAN_BUS,
                                 (Object **) &s->canbus[i],
                                 object_property_allow_set_link, 0);
    }
}

static void versal_virt_machine_finalize(Object *obj)
{
    VersalVirt *s = XLNX_VERSAL_VIRT_MACHINE(obj);

    g_free(s->cfg.ospi_model);
    g_free(s->canbus);
}

static void versal_virt_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Xilinx Versal Virtual development board";
    mc->init = versal_virt_init;
    mc->min_cpus = XLNX_VERSAL_NR_ACPUS + XLNX_VERSAL_NR_RCPUS;
    mc->max_cpus = XLNX_VERSAL_NR_ACPUS + XLNX_VERSAL_NR_RCPUS;
    mc->default_cpus = XLNX_VERSAL_NR_ACPUS + XLNX_VERSAL_NR_RCPUS;
    mc->no_cdrom = true;
    mc->auto_create_sdcard = true;
    mc->default_ram_id = "ddr";
    object_class_property_add_str(oc, "ospi-flash", versal_get_ospi_model,
                                   versal_set_ospi_model);
    object_class_property_set_description(oc, "ospi-flash",
                                          "Change the OSPI Flash model");
}

static const TypeInfo versal_virt_machine_init_typeinfo = {
    .name       = TYPE_XLNX_VERSAL_VIRT_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = versal_virt_machine_class_init,
    .instance_init = versal_virt_machine_instance_init,
    .instance_size = sizeof(VersalVirt),
    .instance_finalize = versal_virt_machine_finalize,
};

static void versal_virt_machine_init_register_types(void)
{
    type_register_static(&versal_virt_machine_init_typeinfo);
}

type_init(versal_virt_machine_init_register_types)

