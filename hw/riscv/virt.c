/*
 * QEMU RISC-V VirtIO Board
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * RISC-V machine with 16550a UART and VirtIO MMIO
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
#include "qemu/units.h"
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
#include "hw/riscv/sifive_test.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "exec/address-spaces.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"

#include <libfdt.h>

#if defined(TARGET_RISCV32)
# define BIOS_FILENAME "opensbi-riscv32-virt-fw_jump.bin"
#else
# define BIOS_FILENAME "opensbi-riscv64-virt-fw_jump.bin"
#endif

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} virt_memmap[] = {
    [VIRT_DEBUG] =       {        0x0,         0x100 },
    [VIRT_MROM] =        {     0x1000,       0x11000 },
    [VIRT_TEST] =        {   0x100000,        0x1000 },
    [VIRT_CLINT] =       {  0x2000000,       0x10000 },
    [VIRT_PLIC] =        {  0xc000000,     0x4000000 },
    [VIRT_UART0] =       { 0x10000000,         0x100 },
    [VIRT_VIRTIO] =      { 0x10001000,        0x1000 },
    [VIRT_DRAM] =        { 0x80000000,           0x0 },
    [VIRT_PCIE_MMIO] =   { 0x40000000,    0x40000000 },
    [VIRT_PCIE_PIO] =    { 0x03000000,    0x00010000 },
    [VIRT_PCIE_ECAM] =   { 0x30000000,    0x10000000 },
};

static void create_pcie_irq_map(void *fdt, char *nodename,
                                uint32_t plic_phandle)
{
    int pin, dev;
    uint32_t
        full_irq_map[GPEX_NUM_IRQS * GPEX_NUM_IRQS * FDT_INT_MAP_WIDTH] = {};
    uint32_t *irq_map = full_irq_map;

    /* This code creates a standard swizzle of interrupts such that
     * each device's first interrupt is based on it's PCI_SLOT number.
     * (See pci_swizzle_map_irq_fn())
     *
     * We only need one entry per interrupt in the table (not one per
     * possible slot) seeing the interrupt-map-mask will allow the table
     * to wrap to any number of devices.
     */
    for (dev = 0; dev < GPEX_NUM_IRQS; dev++) {
        int devfn = dev * 0x8;

        for (pin = 0; pin < GPEX_NUM_IRQS; pin++) {
            int irq_nr = PCIE_IRQ + ((pin + PCI_SLOT(devfn)) % GPEX_NUM_IRQS);
            int i = 0;

            irq_map[i] = cpu_to_be32(devfn << 8);

            i += FDT_PCI_ADDR_CELLS;
            irq_map[i] = cpu_to_be32(pin + 1);

            i += FDT_PCI_INT_CELLS;
            irq_map[i++] = cpu_to_be32(plic_phandle);

            i += FDT_PLIC_ADDR_CELLS;
            irq_map[i] = cpu_to_be32(irq_nr);

            irq_map += FDT_INT_MAP_WIDTH;
        }
    }

    qemu_fdt_setprop(fdt, nodename, "interrupt-map",
                     full_irq_map, sizeof(full_irq_map));

    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-map-mask",
                           0x1800, 0, 0, 0x7);
}

static void *create_fdt(RISCVVirtState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    uint32_t plic_phandle, phandle = 1;
    int i;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "riscv-virtio,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "riscv-virtio");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[VIRT_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[VIRT_DRAM].base >> 32, memmap[VIRT_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        int cpu_phandle = phandle++;
        int intc_phandle;
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency",
                              VIRT_CLOCK_FREQ);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv48");
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_cell(fdt, nodename, "phandle", cpu_phandle);
        qemu_fdt_setprop_cell(fdt, nodename, "linux,phandle", cpu_phandle);
        intc_phandle = phandle++;
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", intc_phandle);
        qemu_fdt_setprop_cell(fdt, intc, "linux,phandle", intc_phandle);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    /* Add cpu-topology node */
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map");
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map/cluster0");
    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        char *core_nodename = g_strdup_printf("/cpus/cpu-map/cluster0/core%d",
                                              cpu);
        char *cpu_nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, cpu_nodename);
        qemu_fdt_add_subnode(fdt, core_nodename);
        qemu_fdt_setprop_cell(fdt, core_nodename, "cpu", intc_phandle);
        g_free(core_nodename);
        g_free(cpu_nodename);
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
        (long)memmap[VIRT_CLINT].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[VIRT_CLINT].base,
        0x0, memmap[VIRT_CLINT].size);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    plic_phandle = phandle++;
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
        (long)memmap[VIRT_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "#address-cells",
                           FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells",
                          FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[VIRT_PLIC].base,
        0x0, memmap[VIRT_PLIC].size);
    qemu_fdt_setprop_string(fdt, nodename, "reg-names", "control");
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,max-priority", 7);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", VIRTIO_NDEV);
    qemu_fdt_setprop_cells(fdt, nodename, "phandle", plic_phandle);
    qemu_fdt_setprop_cells(fdt, nodename, "linux,phandle", plic_phandle);
    plic_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(cells);
    g_free(nodename);

    for (i = 0; i < VIRTIO_COUNT; i++) {
        nodename = g_strdup_printf("/virtio_mmio@%lx",
            (long)(memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size));
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "virtio,mmio");
        qemu_fdt_setprop_cells(fdt, nodename, "reg",
            0x0, memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            0x0, memmap[VIRT_VIRTIO].size);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupts", VIRTIO_IRQ + i);
        g_free(nodename);
    }

    nodename = g_strdup_printf("/soc/pci@%lx",
        (long) memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "#address-cells",
                           FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cells(fdt, nodename, "#interrupt-cells",
                           FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cells(fdt, nodename, "#size-cells", 0x2);
    qemu_fdt_setprop_string(fdt, nodename, "compatible",
                            "pci-host-ecam-generic");
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "pci");
    qemu_fdt_setprop_cell(fdt, nodename, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(fdt, nodename, "bus-range", 0,
                           memmap[VIRT_PCIE_ECAM].size /
                               PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(fdt, nodename, "dma-coherent", NULL, 0);
    qemu_fdt_setprop_cells(fdt, nodename, "reg", 0, memmap[VIRT_PCIE_ECAM].base,
                           0, memmap[VIRT_PCIE_ECAM].size);
    qemu_fdt_setprop_sized_cells(fdt, nodename, "ranges",
        1, FDT_PCI_RANGE_IOPORT, 2, 0,
        2, memmap[VIRT_PCIE_PIO].base, 2, memmap[VIRT_PCIE_PIO].size,
        1, FDT_PCI_RANGE_MMIO,
        2, memmap[VIRT_PCIE_MMIO].base,
        2, memmap[VIRT_PCIE_MMIO].base, 2, memmap[VIRT_PCIE_MMIO].size);
    create_pcie_irq_map(fdt, nodename, plic_phandle);
    g_free(nodename);

    nodename = g_strdup_printf("/test@%lx",
        (long)memmap[VIRT_TEST].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "sifive,test0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[VIRT_TEST].base,
        0x0, memmap[VIRT_TEST].size);
    g_free(nodename);

    nodename = g_strdup_printf("/uart@%lx",
        (long)memmap[VIRT_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[VIRT_UART0].base,
        0x0, memmap[VIRT_UART0].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency", 3686400);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", plic_phandle);
        qemu_fdt_setprop_cells(fdt, nodename, "interrupts", UART0_IRQ);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    if (cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
    g_free(nodename);

    return fdt;
}


static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          hwaddr ecam_base, hwaddr ecam_size,
                                          hwaddr mmio_base, hwaddr mmio_size,
                                          hwaddr pio_base,
                                          DeviceState *plic, bool link_up)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *mmio_reg;
    qemu_irq irq;
    int i;

    dev = qdev_create(NULL, TYPE_GPEX_HOST);

    qdev_init_nofail(dev);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, ecam_size);
    memory_region_add_subregion(get_system_memory(), ecam_base, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, mmio_base, mmio_size);
    memory_region_add_subregion(get_system_memory(), mmio_base, mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, pio_base);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        irq = qdev_get_gpio_in(plic, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    return dev;
}

static void riscv_virt_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = virt_memmap;

    RISCVVirtState *s = g_new0(RISCVVirtState, 1);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    char *plic_hart_config;
    size_t plic_hart_config_len;
    int i;
    unsigned int smp_cpus = machine->smp.cpus;
    void *fdt;

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, sizeof(s->soc),
                            TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
    object_property_set_str(OBJECT(&s->soc), machine->cpu_type, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv_virt_board.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_DRAM].base,
        main_mem);

    /* create device tree */
    fdt = create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           memmap[VIRT_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_MROM].base,
                                mask_rom);

    riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                 memmap[VIRT_DRAM].base);

    if (machine->kernel_filename) {
        uint64_t kernel_entry = riscv_load_kernel(machine->kernel_filename);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
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
        memmap[VIRT_DRAM].base,      /* start: .dword memmap[VIRT_DRAM].base */
        0x00000000,
                                     /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[VIRT_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[VIRT_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[VIRT_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(VIRT_PLIC_HART_CONFIG) + 1) * smp_cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < smp_cpus; i++) {
        if (i != 0) {
            strncat(plic_hart_config, ",", plic_hart_config_len);
        }
        strncat(plic_hart_config, VIRT_PLIC_HART_CONFIG, plic_hart_config_len);
        plic_hart_config_len -= (strlen(VIRT_PLIC_HART_CONFIG) + 1);
    }

    /* MMIO */
    s->plic = sifive_plic_create(memmap[VIRT_PLIC].base,
        plic_hart_config,
        VIRT_PLIC_NUM_SOURCES,
        VIRT_PLIC_NUM_PRIORITIES,
        VIRT_PLIC_PRIORITY_BASE,
        VIRT_PLIC_PENDING_BASE,
        VIRT_PLIC_ENABLE_BASE,
        VIRT_PLIC_ENABLE_STRIDE,
        VIRT_PLIC_CONTEXT_BASE,
        VIRT_PLIC_CONTEXT_STRIDE,
        memmap[VIRT_PLIC].size);
    sifive_clint_create(memmap[VIRT_CLINT].base,
        memmap[VIRT_CLINT].size, smp_cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE);
    sifive_test_create(memmap[VIRT_TEST].base);

    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(DEVICE(s->plic), VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory,
                         memmap[VIRT_PCIE_ECAM].base,
                         memmap[VIRT_PCIE_ECAM].size,
                         memmap[VIRT_PCIE_MMIO].base,
                         memmap[VIRT_PCIE_MMIO].size,
                         memmap[VIRT_PCIE_PIO].base,
                         DEVICE(s->plic), true);

    serial_mm_init(system_memory, memmap[VIRT_UART0].base,
        0, qdev_get_gpio_in(DEVICE(s->plic), UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    g_free(plic_hart_config);
}

static void riscv_virt_board_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V VirtIO Board (Privileged ISA v1.10)";
    mc->init = riscv_virt_board_init;
    mc->max_cpus = 8; /* hardcoded limit in BBL */
    mc->default_cpu_type = VIRT_CPU;
}

DEFINE_MACHINE("virt", riscv_virt_board_machine_init)
