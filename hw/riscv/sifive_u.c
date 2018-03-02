/*
 * QEMU RISC-V Board Compatible with SiFive Freedom U SDK
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Provides a board compatible with the SiFive Freedom U SDK:
 *
 * 0) UART
 * 1) CLINT (Core Level Interruptor)
 * 2) PLIC (Platform Level Interrupt Controller)
 *
 * This board currently uses a hardcoded devicetree that indicates one hart.
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
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_plic.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/sifive_uart.h"
#include "hw/riscv/sifive_prci.h"
#include "hw/riscv/sifive_u.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "exec/address-spaces.h"
#include "elf.h"

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} sifive_u_memmap[] = {
    [SIFIVE_U_DEBUG] =    {        0x0,      0x100 },
    [SIFIVE_U_MROM] =     {     0x1000,     0x2000 },
    [SIFIVE_U_CLINT] =    {  0x2000000,    0x10000 },
    [SIFIVE_U_PLIC] =     {  0xc000000,  0x4000000 },
    [SIFIVE_U_UART0] =    { 0x10013000,     0x1000 },
    [SIFIVE_U_UART1] =    { 0x10023000,     0x1000 },
    [SIFIVE_U_DRAM] =     { 0x80000000,        0x0 },
};

static void copy_le32_to_phys(hwaddr pa, uint32_t *rom, size_t len)
{
    int i;
    for (i = 0; i < (len >> 2); i++) {
        stl_phys(&address_space_memory, pa + (i << 2), rom[i]);
    }
}

static uint64_t identity_translate(void *opaque, uint64_t addr)
{
    return addr;
}

static uint64_t load_kernel(const char *kernel_filename)
{
    uint64_t kernel_entry, kernel_high;

    if (load_elf(kernel_filename, identity_translate, NULL,
                 &kernel_entry, NULL, &kernel_high,
                 0, ELF_MACHINE, 1, 0) < 0) {
        error_report("qemu: could not load kernel '%s'", kernel_filename);
        exit(1);
    }
    return kernel_entry;
}

static void create_fdt(SiFiveUState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    uint32_t plic_phandle;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "ucbbar,spike-bare,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "ucbbar,spike-bare-dev");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "ucbbar,spike-bare-soc");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[SIFIVE_U_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[SIFIVE_U_DRAM].base >> 32, memmap[SIFIVE_U_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency", 10000000);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency", 1000000000);
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
        (long)memmap[SIFIVE_U_CLINT].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SIFIVE_U_CLINT].base,
        0x0, memmap[SIFIVE_U_CLINT].size);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    cells =  g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
        (long)memmap[SIFIVE_U_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SIFIVE_U_PLIC].base,
        0x0, memmap[SIFIVE_U_PLIC].size);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,max-priority", 7);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", 4);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", 2);
    qemu_fdt_setprop_cells(fdt, nodename, "linux,phandle", 2);
    plic_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(cells);
    g_free(nodename);

    nodename = g_strdup_printf("/uart@%lx",
        (long)memmap[SIFIVE_U_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "sifive,uart0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SIFIVE_U_UART0].base,
        0x0, memmap[SIFIVE_U_UART0].size);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 1);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    g_free(nodename);
}

static void riscv_sifive_u_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = sifive_u_memmap;

    SiFiveUState *s = g_new0(SiFiveUState, 1);
    MemoryRegion *sys_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *boot_rom = g_new(MemoryRegion, 1);

    /* Initialize SOC */
    object_initialize(&s->soc, sizeof(s->soc), TYPE_RISCV_HART_ARRAY);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(&s->soc),
                              &error_abort);
    object_property_set_str(OBJECT(&s->soc), SIFIVE_U_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* register RAM */
    memory_region_init_ram(main_mem, NULL, "riscv.sifive.u.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(sys_memory, memmap[SIFIVE_U_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_ram(boot_rom, NULL, "riscv.sifive.u.mrom",
                           memmap[SIFIVE_U_MROM].base, &error_fatal);
    memory_region_set_readonly(boot_rom, true);
    memory_region_add_subregion(sys_memory, 0x0, boot_rom);

    if (machine->kernel_filename) {
        load_kernel(machine->kernel_filename);
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297,                    /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                    /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                    /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                    /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                    /*     ld     t0, 24(t0) */
#endif
        0x00028067,                    /*     jr     t0 */
        0x00000000,
        memmap[SIFIVE_U_DRAM].base, /* start: .dword DRAM_BASE */
        0x00000000,
                                       /* dtb: */
    };

    /* copy in the reset vector */
    copy_le32_to_phys(memmap[SIFIVE_U_MROM].base, reset_vec, sizeof(reset_vec));

    /* copy in the device tree */
    qemu_fdt_dumpdtb(s->fdt, s->fdt_size);
    cpu_physical_memory_write(memmap[SIFIVE_U_MROM].base +
        sizeof(reset_vec), s->fdt, s->fdt_size);

    /* MMIO */
    s->plic = sifive_plic_create(memmap[SIFIVE_U_PLIC].base,
        (char *)SIFIVE_U_PLIC_HART_CONFIG,
        SIFIVE_U_PLIC_NUM_SOURCES,
        SIFIVE_U_PLIC_NUM_PRIORITIES,
        SIFIVE_U_PLIC_PRIORITY_BASE,
        SIFIVE_U_PLIC_PENDING_BASE,
        SIFIVE_U_PLIC_ENABLE_BASE,
        SIFIVE_U_PLIC_ENABLE_STRIDE,
        SIFIVE_U_PLIC_CONTEXT_BASE,
        SIFIVE_U_PLIC_CONTEXT_STRIDE,
        memmap[SIFIVE_U_PLIC].size);
    sifive_uart_create(sys_memory, memmap[SIFIVE_U_UART0].base,
        serial_hds[0], SIFIVE_PLIC(s->plic)->irqs[SIFIVE_U_UART0_IRQ]);
    /* sifive_uart_create(sys_memory, memmap[SIFIVE_U_UART1].base,
        serial_hds[1], SIFIVE_PLIC(s->plic)->irqs[SIFIVE_U_UART1_IRQ]); */
    sifive_clint_create(memmap[SIFIVE_U_CLINT].base,
        memmap[SIFIVE_U_CLINT].size, smp_cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE);
}

static int riscv_sifive_u_sysbus_device_init(SysBusDevice *sysbusdev)
{
    return 0;
}

static void riscv_sifive_u_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = riscv_sifive_u_sysbus_device_init;
}

static const TypeInfo riscv_sifive_u_device = {
    .name          = TYPE_SIFIVE_U,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SiFiveUState),
    .class_init    = riscv_sifive_u_class_init,
};

static void riscv_sifive_u_register_types(void)
{
    type_register_static(&riscv_sifive_u_device);
}

type_init(riscv_sifive_u_register_types);

static void riscv_sifive_u_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Board compatible with SiFive U SDK";
    mc->init = riscv_sifive_u_init;
    mc->max_cpus = 1;
}

DEFINE_MACHINE("sifive_u", riscv_sifive_u_machine_init)
