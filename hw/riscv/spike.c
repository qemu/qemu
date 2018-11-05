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
 * 2) PLIC (Platform Level Interrupt Controller)
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
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_htif.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/spike.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "exec/address-spaces.h"
#include "elf.h"

#include <libfdt.h>

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} spike_memmap[] = {
    [SPIKE_MROM] =     {     0x1000,    0x11000 },
    [SPIKE_CLINT] =    {  0x2000000,    0x10000 },
    [SPIKE_DRAM] =     { 0x80000000,        0x0 },
};

static uint64_t load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf_ram_sym(kernel_filename, NULL, NULL,
            &kernel_entry, NULL, &kernel_high, 0, EM_RISCV, 1, 0,
            NULL, true, htif_symbol_callback) < 0) {
        error_report("could not load kernel '%s'", kernel_filename);
        exit(1);
    }
    return kernel_entry;
}

static void create_fdt(SpikeState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
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

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[SPIKE_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[SPIKE_DRAM].base >> 32, memmap[SPIKE_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
        SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
                              SPIKE_CLOCK_FREQ);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv48");
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", 1);
        qemu_fdt_setprop_cell(fdt, intc, "linux,phandle", 1);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    cells =  g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/clint@%lx",
        (long)memmap[SPIKE_CLINT].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SPIKE_CLINT].base,
        0x0, memmap[SPIKE_CLINT].size);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    if (cmdline) {
        qemu_fdt_add_subnode(fdt, "/chosen");
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
 }

static void spike_v1_10_0_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = spike_memmap;

    SpikeState *s = g_new0(SpikeState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    int i;

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, sizeof(s->soc),
                            TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
    object_property_set_str(OBJECT(&s->soc), SPIKE_V1_10_0_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.spike.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.spike.mrom",
                           memmap[SPIKE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_MROM].base,
                                mask_rom);

    if (machine->kernel_filename) {
        load_kernel(machine->kernel_filename);
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                  /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                  /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                  /*     ld     t0, 24(t0) */
#endif
        0x00028067,                  /*     jr     t0 */
        0x00000000,
        memmap[SPIKE_DRAM].base,     /* start: .dword DRAM_BASE */
        0x00000000,
                                     /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[SPIKE_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[SPIKE_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[SPIKE_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* initialize HTIF using symbols found in load_kernel */
    htif_mm_init(system_memory, mask_rom, &s->soc.harts[0].env, serial_hd(0));

    /* Core Local Interruptor (timer and IPI) */
    sifive_clint_create(memmap[SPIKE_CLINT].base, memmap[SPIKE_CLINT].size,
        smp_cpus, SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE);
}

static void spike_v1_09_1_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = spike_memmap;

    SpikeState *s = g_new0(SpikeState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    int i;

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, sizeof(s->soc),
                            TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
    object_property_set_str(OBJECT(&s->soc), SPIKE_V1_09_1_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv.spike.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_DRAM].base,
        main_mem);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv.spike.mrom",
                           memmap[SPIKE_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SPIKE_MROM].base,
                                mask_rom);

    if (machine->kernel_filename) {
        load_kernel(machine->kernel_filename);
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x297 + memmap[SPIKE_DRAM].base - memmap[SPIKE_MROM].base, /* lui */
        0x00028067,                   /* jump to DRAM_BASE */
        0x00000000,                   /* reserved */
        memmap[SPIKE_MROM].base + sizeof(reset_vec), /* config string pointer */
        0, 0, 0, 0                    /* trap vector */
    };

    /* part one of config string - before memory size specified */
    const char *config_string_tmpl =
        "platform {\n"
        "  vendor ucb;\n"
        "  arch spike;\n"
        "};\n"
        "rtc {\n"
        "  addr 0x%" PRIx64 "x;\n"
        "};\n"
        "ram {\n"
        "  0 {\n"
        "    addr 0x%" PRIx64 "x;\n"
        "    size 0x%" PRIx64 "x;\n"
        "  };\n"
        "};\n"
        "core {\n"
        "  0" " {\n"
        "    " "0 {\n"
        "      isa %s;\n"
        "      timecmp 0x%" PRIx64 "x;\n"
        "      ipi 0x%" PRIx64 "x;\n"
        "    };\n"
        "  };\n"
        "};\n";

    /* build config string with supplied memory size */
    char *isa = riscv_isa_string(&s->soc.harts[0]);
    char *config_string = g_strdup_printf(config_string_tmpl,
        (uint64_t)memmap[SPIKE_CLINT].base + SIFIVE_TIME_BASE,
        (uint64_t)memmap[SPIKE_DRAM].base,
        (uint64_t)ram_size, isa,
        (uint64_t)memmap[SPIKE_CLINT].base + SIFIVE_TIMECMP_BASE,
        (uint64_t)memmap[SPIKE_CLINT].base + SIFIVE_SIP_BASE);
    g_free(isa);
    size_t config_string_len = strlen(config_string);

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[SPIKE_MROM].base, &address_space_memory);

    /* copy in the config string */
    rom_add_blob_fixed_as("mrom.reset", config_string, config_string_len,
                          memmap[SPIKE_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* initialize HTIF using symbols found in load_kernel */
    htif_mm_init(system_memory, mask_rom, &s->soc.harts[0].env, serial_hd(0));

    /* Core Local Interruptor (timer and IPI) */
    sifive_clint_create(memmap[SPIKE_CLINT].base, memmap[SPIKE_CLINT].size,
        smp_cpus, SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE);

    g_free(config_string);
}

static void spike_v1_09_1_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Spike Board (Privileged ISA v1.9.1)";
    mc->init = spike_v1_09_1_board_init;
    mc->max_cpus = 1;
}

static void spike_v1_10_0_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Spike Board (Privileged ISA v1.10)";
    mc->init = spike_v1_10_0_board_init;
    mc->max_cpus = 1;
    mc->is_default = 1;
}

DEFINE_MACHINE("spike_v1.9.1", spike_v1_09_1_machine_init)
DEFINE_MACHINE("spike_v1.10", spike_v1_10_0_machine_init)
