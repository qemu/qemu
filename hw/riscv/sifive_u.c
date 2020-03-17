/*
 * QEMU RISC-V Board Compatible with SiFive Freedom U SDK
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2019 Bin Meng <bmeng.cn@gmail.com>
 *
 * Provides a board compatible with the SiFive Freedom U SDK:
 *
 * 0) UART
 * 1) CLINT (Core Level Interruptor)
 * 2) PLIC (Platform Level Interrupt Controller)
 * 3) PRCI (Power, Reset, Clock, Interrupt)
 * 4) OTP (One-Time Programmable) memory with stored serial number
 * 5) GEM (Gigabit Ethernet Controller) and management block
 *
 * This board currently generates devicetree dynamically that indicates at least
 * two harts and up to five harts.
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
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "hw/cpu/cluster.h"
#include "hw/misc/unimp.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_plic.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/sifive_uart.h"
#include "hw/riscv/sifive_u.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "net/eth.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#include <libfdt.h>

#if defined(TARGET_RISCV32)
# define BIOS_FILENAME "opensbi-riscv32-sifive_u-fw_jump.bin"
#else
# define BIOS_FILENAME "opensbi-riscv64-sifive_u-fw_jump.bin"
#endif

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} sifive_u_memmap[] = {
    [SIFIVE_U_DEBUG] =    {        0x0,      0x100 },
    [SIFIVE_U_MROM] =     {     0x1000,    0x11000 },
    [SIFIVE_U_CLINT] =    {  0x2000000,    0x10000 },
    [SIFIVE_U_L2LIM] =    {  0x8000000,  0x2000000 },
    [SIFIVE_U_PLIC] =     {  0xc000000,  0x4000000 },
    [SIFIVE_U_PRCI] =     { 0x10000000,     0x1000 },
    [SIFIVE_U_UART0] =    { 0x10010000,     0x1000 },
    [SIFIVE_U_UART1] =    { 0x10011000,     0x1000 },
    [SIFIVE_U_OTP] =      { 0x10070000,     0x1000 },
    [SIFIVE_U_FLASH0] =   { 0x20000000, 0x10000000 },
    [SIFIVE_U_DRAM] =     { 0x80000000,        0x0 },
    [SIFIVE_U_GEM] =      { 0x10090000,     0x2000 },
    [SIFIVE_U_GEM_MGMT] = { 0x100a0000,     0x1000 },
};

#define OTP_SERIAL          1
#define GEM_REVISION        0x10070109

static void create_fdt(SiFiveUState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    char ethclk_names[] = "pclk\0hclk";
    uint32_t plic_phandle, prci_phandle, phandle = 1;
    uint32_t hfclk_phandle, rtcclk_phandle, phy_phandle;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "SiFive HiFive Unleashed A00");
    qemu_fdt_setprop_string(fdt, "/", "compatible",
                            "sifive,hifive-unleashed-a00");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    hfclk_phandle = phandle++;
    nodename = g_strdup_printf("/hfclk");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", hfclk_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "clock-output-names", "hfclk");
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
        SIFIVE_U_HFCLK_FREQ);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, nodename, "#clock-cells", 0x0);
    g_free(nodename);

    rtcclk_phandle = phandle++;
    nodename = g_strdup_printf("/rtcclk");
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", rtcclk_phandle);
    qemu_fdt_setprop_string(fdt, nodename, "clock-output-names", "rtcclk");
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
        SIFIVE_U_RTCCLK_FREQ);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, nodename, "#clock-cells", 0x0);
    g_free(nodename);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[SIFIVE_U_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[SIFIVE_U_DRAM].base >> 32, memmap[SIFIVE_U_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
        SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = ms->smp.cpus - 1; cpu >= 0; cpu--) {
        int cpu_phandle = phandle++;
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa;
        qemu_fdt_add_subnode(fdt, nodename);
        /* cpu 0 is the management hart that does not have mmu */
        if (cpu != 0) {
            qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv48");
            isa = riscv_isa_string(&s->soc.u_cpus.harts[cpu - 1]);
        } else {
            isa = riscv_isa_string(&s->soc.e_cpus.harts[0]);
        }
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", cpu_phandle);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    cells =  g_new0(uint32_t, ms->smp.cpus * 4);
    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
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
        cells, ms->smp.cpus * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    prci_phandle = phandle++;
    nodename = g_strdup_printf("/soc/clock-controller@%lx",
        (long)memmap[SIFIVE_U_PRCI].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", prci_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "#clock-cells", 0x1);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks",
        hfclk_phandle, rtcclk_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SIFIVE_U_PRCI].base,
        0x0, memmap[SIFIVE_U_PRCI].size);
    qemu_fdt_setprop_string(fdt, nodename, "compatible",
        "sifive,fu540-c000-prci");
    g_free(nodename);

    plic_phandle = phandle++;
    cells =  g_new0(uint32_t, ms->smp.cpus * 4 - 2);
    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        /* cpu 0 is the management hart that does not have S-mode */
        if (cpu == 0) {
            cells[0] = cpu_to_be32(intc_phandle);
            cells[1] = cpu_to_be32(IRQ_M_EXT);
        } else {
            cells[cpu * 4 - 2] = cpu_to_be32(intc_phandle);
            cells[cpu * 4 - 1] = cpu_to_be32(IRQ_M_EXT);
            cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
            cells[cpu * 4 + 1] = cpu_to_be32(IRQ_S_EXT);
        }
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
        (long)memmap[SIFIVE_U_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells", 1);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, (ms->smp.cpus * 4 - 2) * sizeof(uint32_t));
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SIFIVE_U_PLIC].base,
        0x0, memmap[SIFIVE_U_PLIC].size);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", 0x35);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", plic_phandle);
    plic_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(cells);
    g_free(nodename);

    phy_phandle = phandle++;
    nodename = g_strdup_printf("/soc/ethernet@%lx",
        (long)memmap[SIFIVE_U_GEM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible",
        "sifive,fu540-c000-gem");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SIFIVE_U_GEM].base,
        0x0, memmap[SIFIVE_U_GEM].size,
        0x0, memmap[SIFIVE_U_GEM_MGMT].base,
        0x0, memmap[SIFIVE_U_GEM_MGMT].size);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_string(fdt, nodename, "phy-mode", "gmii");
    qemu_fdt_setprop_cell(fdt, nodename, "phy-handle", phy_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", SIFIVE_U_GEM_IRQ);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks",
        prci_phandle, PRCI_CLK_GEMGXLPLL, prci_phandle, PRCI_CLK_GEMGXLPLL);
    qemu_fdt_setprop(fdt, nodename, "clock-names", ethclk_names,
        sizeof(ethclk_names));
    qemu_fdt_setprop(fdt, nodename, "local-mac-address",
        s->soc.gem.conf.macaddr.a, ETH_ALEN);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, nodename, "#size-cells", 0);

    qemu_fdt_add_subnode(fdt, "/aliases");
    qemu_fdt_setprop_string(fdt, "/aliases", "ethernet0", nodename);

    g_free(nodename);

    nodename = g_strdup_printf("/soc/ethernet@%lx/ethernet-phy@0",
        (long)memmap[SIFIVE_U_GEM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", phy_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "reg", 0x0);
    g_free(nodename);

    nodename = g_strdup_printf("/soc/serial@%lx",
        (long)memmap[SIFIVE_U_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "sifive,uart0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[SIFIVE_U_UART0].base,
        0x0, memmap[SIFIVE_U_UART0].size);
    qemu_fdt_setprop_cells(fdt, nodename, "clocks",
        prci_phandle, PRCI_CLK_TLCLK);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", SIFIVE_U_UART0_IRQ);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    if (cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }

    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", nodename);

    g_free(nodename);
}

static void riscv_sifive_u_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = sifive_u_memmap;
    SiFiveUState *s = RISCV_U_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *flash0 = g_new(MemoryRegion, 1);
    target_ulong start_addr = memmap[SIFIVE_U_DRAM].base;
    int i;

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            sizeof(s->soc), TYPE_RISCV_U_SOC,
                            &error_abort, NULL);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* register RAM */
    memory_region_init_ram(main_mem, NULL, "riscv.sifive.u.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SIFIVE_U_DRAM].base,
                                main_mem);

    /* register QSPI0 Flash */
    memory_region_init_ram(flash0, NULL, "riscv.sifive.u.flash0",
                           memmap[SIFIVE_U_FLASH0].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SIFIVE_U_FLASH0].base,
                                flash0);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                 memmap[SIFIVE_U_DRAM].base);

    if (machine->kernel_filename) {
        uint64_t kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                                  NULL);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    }

    if (s->start_in_flash) {
        start_addr = memmap[SIFIVE_U_FLASH0].base;
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
        start_addr,                    /* start: .dword */
        0x00000000,
                                       /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[SIFIVE_U_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[SIFIVE_U_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[SIFIVE_U_MROM].base + sizeof(reset_vec),
                          &address_space_memory);
}

static void riscv_sifive_u_soc_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    SiFiveUSoCState *s = RISCV_U_SOC(obj);

    object_initialize_child(obj, "e-cluster", &s->e_cluster,
                            sizeof(s->e_cluster), TYPE_CPU_CLUSTER,
                            &error_abort, NULL);
    qdev_prop_set_uint32(DEVICE(&s->e_cluster), "cluster-id", 0);

    object_initialize_child(OBJECT(&s->e_cluster), "e-cpus",
                            &s->e_cpus, sizeof(s->e_cpus),
                            TYPE_RISCV_HART_ARRAY, &error_abort,
                            NULL);
    qdev_prop_set_uint32(DEVICE(&s->e_cpus), "num-harts", 1);
    qdev_prop_set_uint32(DEVICE(&s->e_cpus), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(&s->e_cpus), "cpu-type", SIFIVE_E_CPU);

    object_initialize_child(obj, "u-cluster", &s->u_cluster,
                            sizeof(s->u_cluster), TYPE_CPU_CLUSTER,
                            &error_abort, NULL);
    qdev_prop_set_uint32(DEVICE(&s->u_cluster), "cluster-id", 1);

    object_initialize_child(OBJECT(&s->u_cluster), "u-cpus",
                            &s->u_cpus, sizeof(s->u_cpus),
                            TYPE_RISCV_HART_ARRAY, &error_abort,
                            NULL);
    qdev_prop_set_uint32(DEVICE(&s->u_cpus), "num-harts", ms->smp.cpus - 1);
    qdev_prop_set_uint32(DEVICE(&s->u_cpus), "hartid-base", 1);
    qdev_prop_set_string(DEVICE(&s->u_cpus), "cpu-type", SIFIVE_U_CPU);

    sysbus_init_child_obj(obj, "prci", &s->prci, sizeof(s->prci),
                          TYPE_SIFIVE_U_PRCI);
    sysbus_init_child_obj(obj, "otp", &s->otp, sizeof(s->otp),
                          TYPE_SIFIVE_U_OTP);
    qdev_prop_set_uint32(DEVICE(&s->otp), "serial", OTP_SERIAL);
    sysbus_init_child_obj(obj, "gem", &s->gem, sizeof(s->gem),
                          TYPE_CADENCE_GEM);
}

static bool sifive_u_get_start_in_flash(Object *obj, Error **errp)
{
    SiFiveUState *s = RISCV_U_MACHINE(obj);

    return s->start_in_flash;
}

static void sifive_u_set_start_in_flash(Object *obj, bool value, Error **errp)
{
    SiFiveUState *s = RISCV_U_MACHINE(obj);

    s->start_in_flash = value;
}

static void riscv_sifive_u_machine_instance_init(Object *obj)
{
    SiFiveUState *s = RISCV_U_MACHINE(obj);

    s->start_in_flash = false;
    object_property_add_bool(obj, "start-in-flash", sifive_u_get_start_in_flash,
                             sifive_u_set_start_in_flash, NULL);
    object_property_set_description(obj, "start-in-flash",
                                    "Set on to tell QEMU's ROM to jump to " \
                                    "flash. Otherwise QEMU will jump to DRAM",
                                    NULL);
}

static void riscv_sifive_u_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    SiFiveUSoCState *s = RISCV_U_SOC(dev);
    const struct MemmapEntry *memmap = sifive_u_memmap;
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    MemoryRegion *l2lim_mem = g_new(MemoryRegion, 1);
    qemu_irq plic_gpios[SIFIVE_U_PLIC_NUM_SOURCES];
    char *plic_hart_config;
    size_t plic_hart_config_len;
    int i;
    Error *err = NULL;
    NICInfo *nd = &nd_table[0];

    object_property_set_bool(OBJECT(&s->e_cpus), true, "realized",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->u_cpus), true, "realized",
                             &error_abort);
    /*
     * The cluster must be realized after the RISC-V hart array container,
     * as the container's CPU object is only created on realize, and the
     * CPU must exist and have been parented into the cluster before the
     * cluster is realized.
     */
    object_property_set_bool(OBJECT(&s->e_cluster), true, "realized",
                             &error_abort);
    object_property_set_bool(OBJECT(&s->u_cluster), true, "realized",
                             &error_abort);

    /* boot rom */
    memory_region_init_rom(mask_rom, OBJECT(dev), "riscv.sifive.u.mrom",
                           memmap[SIFIVE_U_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SIFIVE_U_MROM].base,
                                mask_rom);

    /*
     * Add L2-LIM at reset size.
     * This should be reduced in size as the L2 Cache Controller WayEnable
     * register is incremented. Unfortunately I don't see a nice (or any) way
     * to handle reducing or blocking out the L2 LIM while still allowing it
     * be re returned to all enabled after a reset. For the time being, just
     * leave it enabled all the time. This won't break anything, but will be
     * too generous to misbehaving guests.
     */
    memory_region_init_ram(l2lim_mem, NULL, "riscv.sifive.u.l2lim",
                           memmap[SIFIVE_U_L2LIM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[SIFIVE_U_L2LIM].base,
                                l2lim_mem);

    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(SIFIVE_U_PLIC_HART_CONFIG) + 1) *
                           ms->smp.cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < ms->smp.cpus; i++) {
        if (i != 0) {
            strncat(plic_hart_config, "," SIFIVE_U_PLIC_HART_CONFIG,
                    plic_hart_config_len);
        } else {
            strncat(plic_hart_config, "M", plic_hart_config_len);
        }
        plic_hart_config_len -= (strlen(SIFIVE_U_PLIC_HART_CONFIG) + 1);
    }

    /* MMIO */
    s->plic = sifive_plic_create(memmap[SIFIVE_U_PLIC].base,
        plic_hart_config,
        SIFIVE_U_PLIC_NUM_SOURCES,
        SIFIVE_U_PLIC_NUM_PRIORITIES,
        SIFIVE_U_PLIC_PRIORITY_BASE,
        SIFIVE_U_PLIC_PENDING_BASE,
        SIFIVE_U_PLIC_ENABLE_BASE,
        SIFIVE_U_PLIC_ENABLE_STRIDE,
        SIFIVE_U_PLIC_CONTEXT_BASE,
        SIFIVE_U_PLIC_CONTEXT_STRIDE,
        memmap[SIFIVE_U_PLIC].size);
    g_free(plic_hart_config);
    sifive_uart_create(system_memory, memmap[SIFIVE_U_UART0].base,
        serial_hd(0), qdev_get_gpio_in(DEVICE(s->plic), SIFIVE_U_UART0_IRQ));
    sifive_uart_create(system_memory, memmap[SIFIVE_U_UART1].base,
        serial_hd(1), qdev_get_gpio_in(DEVICE(s->plic), SIFIVE_U_UART1_IRQ));
    sifive_clint_create(memmap[SIFIVE_U_CLINT].base,
        memmap[SIFIVE_U_CLINT].size, ms->smp.cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE, false);

    object_property_set_bool(OBJECT(&s->prci), true, "realized", &err);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->prci), 0, memmap[SIFIVE_U_PRCI].base);

    object_property_set_bool(OBJECT(&s->otp), true, "realized", &err);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->otp), 0, memmap[SIFIVE_U_OTP].base);

    for (i = 0; i < SIFIVE_U_PLIC_NUM_SOURCES; i++) {
        plic_gpios[i] = qdev_get_gpio_in(DEVICE(s->plic), i);
    }

    if (nd->used) {
        qemu_check_nic_model(nd, TYPE_CADENCE_GEM);
        qdev_set_nic_properties(DEVICE(&s->gem), nd);
    }
    object_property_set_int(OBJECT(&s->gem), GEM_REVISION, "revision",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->gem), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gem), 0, memmap[SIFIVE_U_GEM].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->gem), 0,
                       plic_gpios[SIFIVE_U_GEM_IRQ]);

    create_unimplemented_device("riscv.sifive.u.gem-mgmt",
        memmap[SIFIVE_U_GEM_MGMT].base, memmap[SIFIVE_U_GEM_MGMT].size);
}

static void riscv_sifive_u_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = riscv_sifive_u_soc_realize;
    /* Reason: Uses serial_hds in realize function, thus can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo riscv_sifive_u_soc_type_info = {
    .name = TYPE_RISCV_U_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SiFiveUSoCState),
    .instance_init = riscv_sifive_u_soc_init,
    .class_init = riscv_sifive_u_soc_class_init,
};

static void riscv_sifive_u_soc_register_types(void)
{
    type_register_static(&riscv_sifive_u_soc_type_info);
}

type_init(riscv_sifive_u_soc_register_types)

static void riscv_sifive_u_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V Board compatible with SiFive U SDK";
    mc->init = riscv_sifive_u_init;
    mc->max_cpus = SIFIVE_U_MANAGEMENT_CPU_COUNT + SIFIVE_U_COMPUTE_CPU_COUNT;
    mc->min_cpus = SIFIVE_U_MANAGEMENT_CPU_COUNT + 1;
    mc->default_cpus = mc->min_cpus;
}

static const TypeInfo riscv_sifive_u_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("sifive_u"),
    .parent     = TYPE_MACHINE,
    .class_init = riscv_sifive_u_machine_class_init,
    .instance_init = riscv_sifive_u_machine_instance_init,
    .instance_size = sizeof(SiFiveUState),
};

static void riscv_sifive_u_machine_init_register_types(void)
{
    type_register_static(&riscv_sifive_u_machine_typeinfo);
}

type_init(riscv_sifive_u_machine_init_register_types)
