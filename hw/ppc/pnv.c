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

static void *powernv_create_fdt(MachineState *machine)
{
    const char plat_compat[] = "qemu,powernv\0ibm,powernv";
    PnvMachineState *pnv = POWERNV_MACHINE(machine);
    void *fdt;
    char *buf;
    int off;

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

    /* TODO: put all the memory in one node on chip 0 until we find a
     * way to specify different ranges for each chip
     */
    powernv_populate_memory_node(fdt, 0, 0, machine->ram_size);

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
}

static const TypeInfo powernv_machine_info = {
    .name          = TYPE_POWERNV_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(PnvMachineState),
    .class_init    = powernv_machine_class_init,
};

static void powernv_machine_register_types(void)
{
    type_register_static(&powernv_machine_info);
}

type_init(powernv_machine_register_types)
