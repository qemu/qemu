/*
 * QEMU RISC-V Spike Board
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This provides a RISC-V Board with the following devices:
 *
 * 0) HTIF Console and Poweroff
 * 1) CLINT (Timer and IPI)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/spike.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/char/riscv_htif.h"
#include "hw/intc/riscv_aclint.h"
#include "chardev/char.h"
#include "system/device_tree.h"
#include "system/system.h"

#include <libfdt.h>

static const MemMapEntry spike_memmap[] = {
    [SPIKE_MROM] =     {     0x1000,     0xf000 },
    [SPIKE_HTIF] =     {  0x1000000,     0x1000 },
    [SPIKE_CLINT] =    {  0x2000000,    0x10000 },
    [SPIKE_DRAM] =     { 0x80000000,        0x0 },
};

static void create_fdt(SpikeState *s, const MemMapEntry *memmap,
                       bool is_32_bit, bool htif_custom_base)
{
    void *fdt;
    int fdt_size;
    uint64_t addr, size;
    unsigned long clint_addr;
    int cpu, socket;
    MachineState *ms = MACHINE(s);
    uint32_t *clint_cells;
    uint32_t cpu_phandle, intc_phandle, phandle = 1;
    char *mem_name, *clint_name, *clust_name;
    char *core_name, *cpu_name, *intc_name;
    static const char * const clint_compat[2] = {
        "sifive,clint0", "riscv,clint0"
    };

    fdt = ms->fdt = create_device_tree(&fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "ucbbar,spike-bare,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "ucbbar,spike-bare-dev");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/htif");
    qemu_fdt_setprop_string(fdt, "/htif", "compatible", "ucb,htif0");
    if (htif_custom_base) {
        qemu_fdt_setprop_cells(fdt, "/htif", "reg",
            0x0, memmap[SPIKE_HTIF].base, 0x0, memmap[SPIKE_HTIF].size);
    }

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map");

    for (socket = (riscv_socket_count(ms) - 1); socket >= 0; socket--) {
        clust_name = g_strdup_printf("/cpus/cpu-map/cluster%d", socket);
        qemu_fdt_add_subnode(fdt, clust_name);

        clint_cells =  g_new0(uint32_t, s->soc[socket].num_harts * 4);

        for (cpu = s->soc[socket].num_harts - 1; cpu >= 0; cpu--) {
            cpu_phandle = phandle++;

            cpu_name = g_strdup_printf("/cpus/cpu@%d",
                s->soc[socket].hartid_base + cpu);
            qemu_fdt_add_subnode(fdt, cpu_name);
            if (is_32_bit) {
                qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv32");
            } else {
                qemu_fdt_setprop_string(fdt, cpu_name, "mmu-type", "riscv,sv48");
            }
            riscv_isa_write_fdt(&s->soc[socket].harts[cpu], fdt, cpu_name);
            qemu_fdt_setprop_string(fdt, cpu_name, "compatible", "riscv");
            qemu_fdt_setprop_string(fdt, cpu_name, "status", "okay");
            qemu_fdt_setprop_cell(fdt, cpu_name, "reg",
                s->soc[socket].hartid_base + cpu);
            qemu_fdt_setprop_string(fdt, cpu_name, "device_type", "cpu");
            riscv_socket_fdt_write_id(ms, cpu_name, socket);
            qemu_fdt_setprop_cell(fdt, cpu_name, "phandle", cpu_phandle);

            intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
            qemu_fdt_add_subnode(fdt, intc_name);
            intc_phandle = phandle++;
            qemu_fdt_setprop_cell(fdt, intc_name, "phandle", intc_phandle);
            qemu_fdt_setprop_string(fdt, intc_name, "compatible",
                "riscv,cpu-intc");
            qemu_fdt_setprop(fdt, intc_name, "interrupt-controller", NULL, 0);
            qemu_fdt_setprop_cell(fdt, intc_name, "#interrupt-cells", 1);

            clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
            clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
            clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
            clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);

            core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
            qemu_fdt_add_subnode(fdt, core_name);
            qemu_fdt_setprop_cell(fdt, core_name, "cpu", cpu_phandle);

            g_free(core_name);
            g_free(intc_name);
            g_free(cpu_name);
        }

        addr = memmap[SPIKE_DRAM].base + riscv_socket_mem_offset(ms, socket);
        size = riscv_socket_mem_size(ms, socket);
        mem_name = g_strdup_printf("/memory@%lx", (long)addr);
        qemu_fdt_add_subnode(fdt, mem_name);
        qemu_fdt_setprop_cells(fdt, mem_name, "reg",
            addr >> 32, addr, size >> 32, size);
        qemu_fdt_setprop_string(fdt, mem_name, "device_type", "memory");
        riscv_socket_fdt_write_id(ms, mem_name, socket);
        g_free(mem_name);

        clint_addr = memmap[SPIKE_CLINT].base +
            (memmap[SPIKE_CLINT].size * socket);
        clint_name = g_strdup_printf("/soc/clint@%lx", clint_addr);
        qemu_fdt_add_subnode(fdt, clint_name);
        qemu_fdt_setprop_string_array(fdt, clint_name, "compatible",
            (char **)&clint_compat, ARRAY_SIZE(clint_compat));
        qemu_fdt_setprop_cells(fdt, clint_name, "reg",
            0x0, clint_addr, 0x0, memmap[SPIKE_CLINT].size);
        qemu_fdt_setprop(fdt, clint_name, "interrupts-extended",
            clint_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
        riscv_socket_fdt_write_id(ms, clint_name, socket);

        g_free(clint_name);
        g_free(clint_cells);
        g_free(clust_name);
    }

    riscv_socket_fdt_write_distance_matrix(ms);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", "/htif");
}

static bool spike_test_elf_image(char *filename)
{
    Error *err = NULL;

    load_elf_hdr(filename, NULL, NULL, &err);
    if (err) {
        error_free(err);
        return false;
    } else {
        return true;
    }
}

static void spike_board_init(MachineState *machine)
{
    const MemMapEntry *memmap = spike_memmap;
    SpikeState *s = SPIKE_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    target_ulong firmware_end_addr = memmap[SPIKE_DRAM].base;
    hwaddr firmware_load_addr = memmap[SPIKE_DRAM].base;
    target_ulong kernel_start_addr;
    char *firmware_name;
    uint64_t fdt_load_addr;
    uint64_t kernel_entry;
    char *soc_name;
    int i, base_hartid, hart_count;
    bool htif_custom_base = false;
    RISCVBootInfo boot_info;

    /* Check socket count limit */
    if (SPIKE_SOCKETS_MAX < riscv_socket_count(machine)) {
        error_report("number of sockets/nodes should be less than %d",
            SPIKE_SOCKETS_MAX);
        exit(1);
    }

    /* Initialize sockets */
    for (i = 0; i < riscv_socket_count(machine); i++) {
        if (!riscv_socket_check_hartids(machine, i)) {
            error_report("discontinuous hartids in socket%d", i);
            exit(1);
        }

        base_hartid = riscv_socket_first_hartid(machine, i);
        if (base_hartid < 0) {
            error_report("can't find hartid base for socket%d", i);
            exit(1);
        }

        hart_count = riscv_socket_hart_count(machine, i);
        if (hart_count < 0) {
            error_report("can't find hart count for socket%d", i);
            exit(1);
        }

        soc_name = g_strdup_printf("soc%d", i);
        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
                                TYPE_RISCV_HART_ARRAY);
        g_free(soc_name);
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",
                                machine->cpu_type, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base",
                                base_hartid, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts",
                                hart_count, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_fatal);

        /* Core Local Interruptor (timer and IPI) for each socket */
        riscv_aclint_swi_create(
            memmap[SPIKE_CLINT].base + i * memmap[SPIKE_CLINT].size,
            base_hartid, hart_count, false);
        riscv_aclint_mtimer_create(
            memmap[SPIKE_CLINT].base + i * memmap[SPIKE_CLINT].size +
                RISCV_ACLINT_SWI_SIZE,
            RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
            RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
            RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, false);
    }

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, memmap[SPIKE_DRAM].base,
        machine->ram);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.spike.mrom",
                           memmap[SPIKE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_MROM].base,
                                mask_rom);

    /* Find firmware */
    firmware_name = riscv_find_firmware(machine->firmware,
                        riscv_default_firmware_name(&s->soc[0]));

    /*
     * Test the given firmware or kernel file to see if it is an ELF image.
     * If it is an ELF, we assume it contains the symbols required for
     * the HTIF console, otherwise we fall back to use the custom base
     * passed from device tree for the HTIF console.
     */
    if (!firmware_name && !machine->kernel_filename) {
        htif_custom_base = true;
    } else {
        if (firmware_name) {
            htif_custom_base = !spike_test_elf_image(firmware_name);
        }
        if (!htif_custom_base && machine->kernel_filename) {
            htif_custom_base = !spike_test_elf_image(machine->kernel_filename);
        }
    }

    /* Load firmware */
    if (firmware_name) {
        firmware_end_addr = riscv_load_firmware(firmware_name,
                                                &firmware_load_addr,
                                                htif_symbol_callback);
        g_free(firmware_name);
    }

    /* Create device tree */
    create_fdt(s, memmap, riscv_is_32bit(&s->soc[0]), htif_custom_base);

    /* Load kernel */
    riscv_boot_info_init(&boot_info, &s->soc[0]);
    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                         firmware_end_addr);

        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          true, htif_symbol_callback);
        kernel_entry = boot_info.image_low_addr;
    } else {
       /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;
    }

    fdt_load_addr = riscv_compute_fdt_addr(memmap[SPIKE_DRAM].base,
                                           memmap[SPIKE_DRAM].size,
                                           machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc[0], firmware_load_addr,
                              memmap[SPIKE_MROM].base,
                              memmap[SPIKE_MROM].size, kernel_entry,
                              fdt_load_addr);

    /* initialize HTIF using symbols found in load_kernel */
    htif_mm_init(system_memory, serial_hd(0), memmap[SPIKE_HTIF].base,
                 htif_custom_base);
}

static void spike_set_signature(Object *obj, const char *val, Error **errp)
{
    sig_file = g_strdup(val);
}

static void spike_machine_instance_init(Object *obj)
{
}

static void spike_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V Spike board";
    mc->init = spike_board_init;
    mc->max_cpus = SPIKE_CPUS_MAX;
    mc->is_default = true;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
    mc->default_ram_id = "riscv.spike.ram";
    object_class_property_add_str(oc, "signature", NULL, spike_set_signature);
    object_class_property_set_description(oc, "signature",
                                          "File to write ACT test signature");
    object_class_property_add_uint8_ptr(oc, "signature-granularity",
                                        &line_size, OBJ_PROP_FLAG_WRITE);
    object_class_property_set_description(oc, "signature-granularity",
                                          "Size of each line in ACT signature "
                                          "file");
}

static const TypeInfo spike_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("spike"),
    .parent     = TYPE_MACHINE,
    .class_init = spike_machine_class_init,
    .instance_init = spike_machine_instance_init,
    .instance_size = sizeof(SpikeState),
};

static void spike_machine_init_register_types(void)
{
    type_register_static(&spike_machine_typeinfo);
}

type_init(spike_machine_init_register_types)
