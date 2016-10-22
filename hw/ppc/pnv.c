/*
 * QEMU PowerPC PowerNV machine model
 *
 * Copyright (c) 2016, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "hw/hw.h"
#include "target-ppc/cpu.h"
#include "qemu/log.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"
#include "qemu/cutils.h"
#include "qapi/visitor.h"

#include <libfdt.h>

#define FDT_MAX_SIZE            0x00100000

#define FW_FILE_NAME            "skiboot.lid"
#define FW_LOAD_ADDR            0x0
#define FW_MAX_SIZE             0x00400000

#define KERNEL_LOAD_ADDR        0x20000000
#define INITRD_LOAD_ADDR        0x40000000

/*
 * On Power Systems E880 (POWER8), the max cpus (threads) should be :
 *     4 * 4 sockets * 12 cores * 8 threads = 1536
 * Let's make it 2^11
 */
#define MAX_CPUS                2048

/*
 * Memory nodes are created by hostboot, one for each range of memory
 * that has a different "affinity". In practice, it means one range
 * per chip.
 */
static void powernv_populate_memory_node(void *fdt, int chip_id, hwaddr start,
                                         hwaddr size)
{
    char *mem_name;
    uint64_t mem_reg_property[2];
    int off;

    mem_reg_property[0] = cpu_to_be64(start);
    mem_reg_property[1] = cpu_to_be64(size);

    mem_name = g_strdup_printf("memory@%"HWADDR_PRIx, start);
    off = fdt_add_subnode(fdt, 0, mem_name);
    g_free(mem_name);

    _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
    _FDT((fdt_setprop(fdt, off, "reg", mem_reg_property,
                       sizeof(mem_reg_property))));
    _FDT((fdt_setprop_cell(fdt, off, "ibm,chip-id", chip_id)));
}

static void powernv_populate_chip(PnvChip *chip, void *fdt)
{
    if (chip->ram_size) {
        powernv_populate_memory_node(fdt, chip->chip_id, chip->ram_start,
                                     chip->ram_size);
    }
}

static void *powernv_create_fdt(MachineState *machine)
{
    const char plat_compat[] = "qemu,powernv\0ibm,powernv";
    PnvMachineState *pnv = POWERNV_MACHINE(machine);
    void *fdt;
    char *buf;
    int off;
    int i;

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create_empty_tree(fdt, FDT_MAX_SIZE)));

    /* Root node */
    _FDT((fdt_setprop_cell(fdt, 0, "#address-cells", 0x2)));
    _FDT((fdt_setprop_cell(fdt, 0, "#size-cells", 0x2)));
    _FDT((fdt_setprop_string(fdt, 0, "model",
                             "IBM PowerNV (emulated by qemu)")));
    _FDT((fdt_setprop(fdt, 0, "compatible", plat_compat,
                      sizeof(plat_compat))));

    buf =  qemu_uuid_unparse_strdup(&qemu_uuid);
    _FDT((fdt_setprop_string(fdt, 0, "vm,uuid", buf)));
    if (qemu_uuid_set) {
        _FDT((fdt_property_string(fdt, "system-id", buf)));
    }
    g_free(buf);

    off = fdt_add_subnode(fdt, 0, "chosen");
    if (machine->kernel_cmdline) {
        _FDT((fdt_setprop_string(fdt, off, "bootargs",
                                 machine->kernel_cmdline)));
    }

    if (pnv->initrd_size) {
        uint32_t start_prop = cpu_to_be32(pnv->initrd_base);
        uint32_t end_prop = cpu_to_be32(pnv->initrd_base + pnv->initrd_size);

        _FDT((fdt_setprop(fdt, off, "linux,initrd-start",
                               &start_prop, sizeof(start_prop))));
        _FDT((fdt_setprop(fdt, off, "linux,initrd-end",
                               &end_prop, sizeof(end_prop))));
    }

    /* Populate device tree for each chip */
    for (i = 0; i < pnv->num_chips; i++) {
        powernv_populate_chip(pnv->chips[i], fdt);
    }
    return fdt;
}

static void ppc_powernv_reset(void)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    void *fdt;

    qemu_devices_reset();

    fdt = powernv_create_fdt(machine);

    /* Pack resulting tree */
    _FDT((fdt_pack(fdt)));

    cpu_physical_memory_write(PNV_FDT_ADDR, fdt, fdt_totalsize(fdt));
}

static void ppc_powernv_init(MachineState *machine)
{
    PnvMachineState *pnv = POWERNV_MACHINE(machine);
    MemoryRegion *ram;
    char *fw_filename;
    long fw_size;
    int i;
    char *chip_typename;

    /* allocate RAM */
    if (machine->ram_size < (1 * G_BYTE)) {
        error_report("Warning: skiboot may not work with < 1GB of RAM");
    }

    ram = g_new(MemoryRegion, 1);
    memory_region_allocate_system_memory(ram, NULL, "ppc_powernv.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    /* load skiboot firmware  */
    if (bios_name == NULL) {
        bios_name = FW_FILE_NAME;
    }

    fw_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);

    fw_size = load_image_targphys(fw_filename, FW_LOAD_ADDR, FW_MAX_SIZE);
    if (fw_size < 0) {
        hw_error("qemu: could not load OPAL '%s'\n", fw_filename);
        exit(1);
    }
    g_free(fw_filename);

    /* load kernel */
    if (machine->kernel_filename) {
        long kernel_size;

        kernel_size = load_image_targphys(machine->kernel_filename,
                                          KERNEL_LOAD_ADDR, 0x2000000);
        if (kernel_size < 0) {
            hw_error("qemu: could not load kernel'%s'\n",
                     machine->kernel_filename);
            exit(1);
        }
    }

    /* load initrd */
    if (machine->initrd_filename) {
        pnv->initrd_base = INITRD_LOAD_ADDR;
        pnv->initrd_size = load_image_targphys(machine->initrd_filename,
                                  pnv->initrd_base, 0x10000000); /* 128MB max */
        if (pnv->initrd_size < 0) {
            error_report("qemu: could not load initial ram disk '%s'",
                         machine->initrd_filename);
            exit(1);
        }
    }

    /* We need some cpu model to instantiate the PnvChip class */
    if (machine->cpu_model == NULL) {
        machine->cpu_model = "POWER8";
    }

    /* Create the processor chips */
    chip_typename = g_strdup_printf(TYPE_PNV_CHIP "-%s", machine->cpu_model);
    if (!object_class_by_name(chip_typename)) {
        error_report("qemu: invalid CPU model '%s' for %s machine",
                     machine->cpu_model, MACHINE_GET_CLASS(machine)->name);
        exit(1);
    }

    pnv->chips = g_new0(PnvChip *, pnv->num_chips);
    for (i = 0; i < pnv->num_chips; i++) {
        char chip_name[32];
        Object *chip = object_new(chip_typename);

        pnv->chips[i] = PNV_CHIP(chip);

        /* TODO: put all the memory in one node on chip 0 until we find a
         * way to specify different ranges for each chip
         */
        if (i == 0) {
            object_property_set_int(chip, machine->ram_size, "ram-size",
                                    &error_fatal);
        }

        snprintf(chip_name, sizeof(chip_name), "chip[%d]", PNV_CHIP_HWID(i));
        object_property_add_child(OBJECT(pnv), chip_name, chip, &error_fatal);
        object_property_set_int(chip, PNV_CHIP_HWID(i), "chip-id",
                                &error_fatal);
        object_property_set_bool(chip, true, "realized", &error_fatal);
    }
    g_free(chip_typename);
}

static void pnv_chip_power8e_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER8E";
    k->chip_type = PNV_CHIP_POWER8E;
    k->chip_cfam_id = 0x221ef04980000000ull;  /* P8 Murano DD2.1 */
    dc->desc = "PowerNV Chip POWER8E";
}

static const TypeInfo pnv_chip_power8e_info = {
    .name          = TYPE_PNV_CHIP_POWER8E,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power8e_class_init,
};

static void pnv_chip_power8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER8";
    k->chip_type = PNV_CHIP_POWER8;
    k->chip_cfam_id = 0x220ea04980000000ull; /* P8 Venice DD2.0 */
    dc->desc = "PowerNV Chip POWER8";
}

static const TypeInfo pnv_chip_power8_info = {
    .name          = TYPE_PNV_CHIP_POWER8,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power8_class_init,
};

static void pnv_chip_power8nvl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER8NVL";
    k->chip_type = PNV_CHIP_POWER8NVL;
    k->chip_cfam_id = 0x120d304980000000ull;  /* P8 Naples DD1.0 */
    dc->desc = "PowerNV Chip POWER8NVL";
}

static const TypeInfo pnv_chip_power8nvl_info = {
    .name          = TYPE_PNV_CHIP_POWER8NVL,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power8nvl_class_init,
};

static void pnv_chip_power9_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->cpu_model = "POWER9";
    k->chip_type = PNV_CHIP_POWER9;
    k->chip_cfam_id = 0x100d104980000000ull; /* P9 Nimbus DD1.0 */
    dc->desc = "PowerNV Chip POWER9";
}

static const TypeInfo pnv_chip_power9_info = {
    .name          = TYPE_PNV_CHIP_POWER9,
    .parent        = TYPE_PNV_CHIP,
    .instance_size = sizeof(PnvChip),
    .class_init    = pnv_chip_power9_class_init,
};

static void pnv_chip_realize(DeviceState *dev, Error **errp)
{
    /* left purposely empty */
}

static Property pnv_chip_properties[] = {
    DEFINE_PROP_UINT32("chip-id", PnvChip, chip_id, 0),
    DEFINE_PROP_UINT64("ram-start", PnvChip, ram_start, 0),
    DEFINE_PROP_UINT64("ram-size", PnvChip, ram_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_chip_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_chip_realize;
    dc->props = pnv_chip_properties;
    dc->desc = "PowerNV Chip";
}

static const TypeInfo pnv_chip_info = {
    .name          = TYPE_PNV_CHIP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .class_init    = pnv_chip_class_init,
    .class_size    = sizeof(PnvChipClass),
    .abstract      = true,
};

static void pnv_get_num_chips(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    visit_type_uint32(v, name, &POWERNV_MACHINE(obj)->num_chips, errp);
}

static void pnv_set_num_chips(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    PnvMachineState *pnv = POWERNV_MACHINE(obj);
    uint32_t num_chips;
    Error *local_err = NULL;

    visit_type_uint32(v, name, &num_chips, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * TODO: should we decide on how many chips we can create based
     * on #cores and Venice vs. Murano vs. Naples chip type etc...,
     */
    if (!is_power_of_2(num_chips) || num_chips > 4) {
        error_setg(errp, "invalid number of chips: '%d'", num_chips);
        return;
    }

    pnv->num_chips = num_chips;
}

static void powernv_machine_initfn(Object *obj)
{
    PnvMachineState *pnv = POWERNV_MACHINE(obj);
    pnv->num_chips = 1;
}

static void powernv_machine_class_props_init(ObjectClass *oc)
{
    object_class_property_add(oc, "num-chips", "uint32_t",
                              pnv_get_num_chips, pnv_set_num_chips,
                              NULL, NULL, NULL);
    object_class_property_set_description(oc, "num-chips",
                              "Specifies the number of processor chips",
                              NULL);
}

static void powernv_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "IBM PowerNV (Non-Virtualized)";
    mc->init = ppc_powernv_init;
    mc->reset = ppc_powernv_reset;
    mc->max_cpus = MAX_CPUS;
    mc->block_default_type = IF_IDE; /* Pnv provides a AHCI device for
                                      * storage */
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    mc->default_ram_size = 1 * G_BYTE;

    powernv_machine_class_props_init(oc);
}

static const TypeInfo powernv_machine_info = {
    .name          = TYPE_POWERNV_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(PnvMachineState),
    .instance_init = powernv_machine_initfn,
    .class_init    = powernv_machine_class_init,
};

static void powernv_machine_register_types(void)
{
    type_register_static(&powernv_machine_info);
    type_register_static(&pnv_chip_info);
    type_register_static(&pnv_chip_power8e_info);
    type_register_static(&pnv_chip_power8_info);
    type_register_static(&pnv_chip_power8nvl_info);
    type_register_static(&pnv_chip_power9_info);
}

type_init(powernv_machine_register_types)
