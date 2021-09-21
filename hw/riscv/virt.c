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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"

static const MemMapEntry virt_memmap[] = {
    [VIRT_DEBUG] =       {        0x0,         0x100 },
    [VIRT_MROM] =        {     0x1000,        0xf000 },
    [VIRT_TEST] =        {   0x100000,        0x1000 },
    [VIRT_RTC] =         {   0x101000,        0x1000 },
    [VIRT_CLINT] =       {  0x2000000,       0x10000 },
    [VIRT_ACLINT_SSWI] = {  0x2F00000,        0x4000 },
    [VIRT_PCIE_PIO] =    {  0x3000000,       0x10000 },
    [VIRT_PLIC] =        {  0xc000000, VIRT_PLIC_SIZE(VIRT_CPUS_MAX * 2) },
    [VIRT_UART0] =       { 0x10000000,         0x100 },
    [VIRT_VIRTIO] =      { 0x10001000,        0x1000 },
    [VIRT_FW_CFG] =      { 0x10100000,          0x18 },
    [VIRT_FLASH] =       { 0x20000000,     0x4000000 },
    [VIRT_PCIE_ECAM] =   { 0x30000000,    0x10000000 },
    [VIRT_PCIE_MMIO] =   { 0x40000000,    0x40000000 },
    [VIRT_DRAM] =        { 0x80000000,           0x0 },
};

/* PCIe high mmio is fixed for RV32 */
#define VIRT32_HIGH_PCIE_MMIO_BASE  0x300000000ULL
#define VIRT32_HIGH_PCIE_MMIO_SIZE  (4 * GiB)

/* PCIe high mmio for RV64, size is fixed but base depends on top of RAM */
#define VIRT64_HIGH_PCIE_MMIO_SIZE  (16 * GiB)

static MemMapEntry virt_high_pcie_memmap;

#define VIRT_FLASH_SECTOR_SIZE (256 * KiB)

static PFlashCFI01 *virt_flash_create1(RISCVVirtState *s,
                                       const char *name,
                                       const char *alias_prop_name)
{
    /*
     * Create a single flash device.  We use the same parameters as
     * the flash devices on the ARM virt board.
     */
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_property_add_alias(OBJECT(s), alias_prop_name,
                              OBJECT(dev), "drive");

    return PFLASH_CFI01(dev);
}

static void virt_flash_create(RISCVVirtState *s)
{
    s->flash[0] = virt_flash_create1(s, "virt.flash0", "pflash0");
    s->flash[1] = virt_flash_create1(s, "virt.flash1", "pflash1");
}

static void virt_flash_map1(PFlashCFI01 *flash,
                            hwaddr base, hwaddr size,
                            MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);

    assert(QEMU_IS_ALIGNED(size, VIRT_FLASH_SECTOR_SIZE));
    assert(size / VIRT_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", size / VIRT_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       0));
}

static void virt_flash_map(RISCVVirtState *s,
                           MemoryRegion *sysmem)
{
    hwaddr flashsize = virt_memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = virt_memmap[VIRT_FLASH].base;

    virt_flash_map1(s->flash[0], flashbase, flashsize,
                    sysmem);
    virt_flash_map1(s->flash[1], flashbase + flashsize, flashsize,
                    sysmem);
}

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

static void create_fdt_socket_cpus(RISCVVirtState *s, int socket,
                                   char *clust_name, uint32_t *phandle,
                                   bool is_32_bit, uint32_t *intc_phandles)
{
    int cpu;
    uint32_t cpu_phandle;
    MachineState *mc = MACHINE(s);
    char *name, *cpu_name, *core_name, *intc_name;

    for (cpu = s->soc[socket].num_harts - 1; cpu >= 0; cpu--) {
        cpu_phandle = (*phandle)++;

        cpu_name = g_strdup_printf("/cpus/cpu@%d",
            s->soc[socket].hartid_base + cpu);
        qemu_fdt_add_subnode(mc->fdt, cpu_name);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "mmu-type",
            (is_32_bit) ? "riscv,sv32" : "riscv,sv48");
        name = riscv_isa_string(&s->soc[socket].harts[cpu]);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "riscv,isa", name);
        g_free(name);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "compatible", "riscv");
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(mc->fdt, cpu_name, "reg",
            s->soc[socket].hartid_base + cpu);
        qemu_fdt_setprop_string(mc->fdt, cpu_name, "device_type", "cpu");
        riscv_socket_fdt_write_id(mc, mc->fdt, cpu_name, socket);
        qemu_fdt_setprop_cell(mc->fdt, cpu_name, "phandle", cpu_phandle);

        intc_phandles[cpu] = (*phandle)++;

        intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
        qemu_fdt_add_subnode(mc->fdt, intc_name);
        qemu_fdt_setprop_cell(mc->fdt, intc_name, "phandle",
            intc_phandles[cpu]);
        qemu_fdt_setprop_string(mc->fdt, intc_name, "compatible",
            "riscv,cpu-intc");
        qemu_fdt_setprop(mc->fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(mc->fdt, intc_name, "#interrupt-cells", 1);

        core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
        qemu_fdt_add_subnode(mc->fdt, core_name);
        qemu_fdt_setprop_cell(mc->fdt, core_name, "cpu", cpu_phandle);

        g_free(core_name);
        g_free(intc_name);
        g_free(cpu_name);
    }
}

static void create_fdt_socket_memory(RISCVVirtState *s,
                                     const MemMapEntry *memmap, int socket)
{
    char *mem_name;
    uint64_t addr, size;
    MachineState *mc = MACHINE(s);

    addr = memmap[VIRT_DRAM].base + riscv_socket_mem_offset(mc, socket);
    size = riscv_socket_mem_size(mc, socket);
    mem_name = g_strdup_printf("/memory@%lx", (long)addr);
    qemu_fdt_add_subnode(mc->fdt, mem_name);
    qemu_fdt_setprop_cells(mc->fdt, mem_name, "reg",
        addr >> 32, addr, size >> 32, size);
    qemu_fdt_setprop_string(mc->fdt, mem_name, "device_type", "memory");
    riscv_socket_fdt_write_id(mc, mc->fdt, mem_name, socket);
    g_free(mem_name);
}

static void create_fdt_socket_clint(RISCVVirtState *s,
                                    const MemMapEntry *memmap, int socket,
                                    uint32_t *intc_phandles)
{
    int cpu;
    char *clint_name;
    uint32_t *clint_cells;
    unsigned long clint_addr;
    MachineState *mc = MACHINE(s);
    static const char * const clint_compat[2] = {
        "sifive,clint0", "riscv,clint0"
    };

    clint_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);

    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        clint_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        clint_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        clint_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
    }

    clint_addr = memmap[VIRT_CLINT].base + (memmap[VIRT_CLINT].size * socket);
    clint_name = g_strdup_printf("/soc/clint@%lx", clint_addr);
    qemu_fdt_add_subnode(mc->fdt, clint_name);
    qemu_fdt_setprop_string_array(mc->fdt, clint_name, "compatible",
                                  (char **)&clint_compat,
                                  ARRAY_SIZE(clint_compat));
    qemu_fdt_setprop_cells(mc->fdt, clint_name, "reg",
        0x0, clint_addr, 0x0, memmap[VIRT_CLINT].size);
    qemu_fdt_setprop(mc->fdt, clint_name, "interrupts-extended",
        clint_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
    riscv_socket_fdt_write_id(mc, mc->fdt, clint_name, socket);
    g_free(clint_name);

    g_free(clint_cells);
}

static void create_fdt_socket_aclint(RISCVVirtState *s,
                                     const MemMapEntry *memmap, int socket,
                                     uint32_t *intc_phandles)
{
    int cpu;
    char *name;
    unsigned long addr;
    uint32_t aclint_cells_size;
    uint32_t *aclint_mswi_cells;
    uint32_t *aclint_sswi_cells;
    uint32_t *aclint_mtimer_cells;
    MachineState *mc = MACHINE(s);

    aclint_mswi_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);
    aclint_mtimer_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);
    aclint_sswi_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);

    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        aclint_mswi_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mswi_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_SOFT);
        aclint_mtimer_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mtimer_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
        aclint_sswi_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_sswi_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_SOFT);
    }
    aclint_cells_size = s->soc[socket].num_harts * sizeof(uint32_t) * 2;

    addr = memmap[VIRT_CLINT].base + (memmap[VIRT_CLINT].size * socket);
    name = g_strdup_printf("/soc/mswi@%lx", addr);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "riscv,aclint-mswi");
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
    qemu_fdt_setprop(mc->fdt, name, "interrupts-extended",
        aclint_mswi_cells, aclint_cells_size);
    qemu_fdt_setprop(mc->fdt, name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(mc->fdt, name, "#interrupt-cells", 0);
    riscv_socket_fdt_write_id(mc, mc->fdt, name, socket);
    g_free(name);

    addr = memmap[VIRT_CLINT].base + RISCV_ACLINT_SWI_SIZE +
        (memmap[VIRT_CLINT].size * socket);
    name = g_strdup_printf("/soc/mtimer@%lx", addr);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible",
        "riscv,aclint-mtimer");
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIME,
        0x0, memmap[VIRT_CLINT].size - RISCV_ACLINT_SWI_SIZE -
             RISCV_ACLINT_DEFAULT_MTIME,
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
        0x0, RISCV_ACLINT_DEFAULT_MTIME);
    qemu_fdt_setprop(mc->fdt, name, "interrupts-extended",
        aclint_mtimer_cells, aclint_cells_size);
    riscv_socket_fdt_write_id(mc, mc->fdt, name, socket);
    g_free(name);

    addr = memmap[VIRT_ACLINT_SSWI].base +
        (memmap[VIRT_ACLINT_SSWI].size * socket);
    name = g_strdup_printf("/soc/sswi@%lx", addr);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "riscv,aclint-sswi");
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, addr, 0x0, memmap[VIRT_ACLINT_SSWI].size);
    qemu_fdt_setprop(mc->fdt, name, "interrupts-extended",
        aclint_sswi_cells, aclint_cells_size);
    qemu_fdt_setprop(mc->fdt, name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(mc->fdt, name, "#interrupt-cells", 0);
    riscv_socket_fdt_write_id(mc, mc->fdt, name, socket);
    g_free(name);

    g_free(aclint_mswi_cells);
    g_free(aclint_mtimer_cells);
    g_free(aclint_sswi_cells);
}

static void create_fdt_socket_plic(RISCVVirtState *s,
                                   const MemMapEntry *memmap, int socket,
                                   uint32_t *phandle, uint32_t *intc_phandles,
                                   uint32_t *plic_phandles)
{
    int cpu;
    char *plic_name;
    uint32_t *plic_cells;
    unsigned long plic_addr;
    MachineState *mc = MACHINE(s);
    static const char * const plic_compat[2] = {
        "sifive,plic-1.0.0", "riscv,plic0"
    };

    plic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);

    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
        plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
        plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
    }

    plic_phandles[socket] = (*phandle)++;
    plic_addr = memmap[VIRT_PLIC].base + (memmap[VIRT_PLIC].size * socket);
    plic_name = g_strdup_printf("/soc/plic@%lx", plic_addr);
    qemu_fdt_add_subnode(mc->fdt, plic_name);
    qemu_fdt_setprop_cell(mc->fdt, plic_name,
        "#address-cells", FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_cell(mc->fdt, plic_name,
        "#interrupt-cells", FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_string_array(mc->fdt, plic_name, "compatible",
                                  (char **)&plic_compat,
                                  ARRAY_SIZE(plic_compat));
    qemu_fdt_setprop(mc->fdt, plic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(mc->fdt, plic_name, "interrupts-extended",
        plic_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(mc->fdt, plic_name, "reg",
        0x0, plic_addr, 0x0, memmap[VIRT_PLIC].size);
    qemu_fdt_setprop_cell(mc->fdt, plic_name, "riscv,ndev", VIRTIO_NDEV);
    riscv_socket_fdt_write_id(mc, mc->fdt, plic_name, socket);
    qemu_fdt_setprop_cell(mc->fdt, plic_name, "phandle",
        plic_phandles[socket]);
    g_free(plic_name);

    g_free(plic_cells);
}

static void create_fdt_sockets(RISCVVirtState *s, const MemMapEntry *memmap,
                               bool is_32_bit, uint32_t *phandle,
                               uint32_t *irq_mmio_phandle,
                               uint32_t *irq_pcie_phandle,
                               uint32_t *irq_virtio_phandle)
{
    int socket;
    char *clust_name;
    uint32_t *intc_phandles;
    MachineState *mc = MACHINE(s);
    uint32_t xplic_phandles[MAX_NODES];

    qemu_fdt_add_subnode(mc->fdt, "/cpus");
    qemu_fdt_setprop_cell(mc->fdt, "/cpus", "timebase-frequency",
                          RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(mc->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(mc->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(mc->fdt, "/cpus/cpu-map");

    for (socket = (riscv_socket_count(mc) - 1); socket >= 0; socket--) {
        clust_name = g_strdup_printf("/cpus/cpu-map/cluster%d", socket);
        qemu_fdt_add_subnode(mc->fdt, clust_name);

        intc_phandles = g_new0(uint32_t, s->soc[socket].num_harts);

        create_fdt_socket_cpus(s, socket, clust_name, phandle,
            is_32_bit, intc_phandles);

        create_fdt_socket_memory(s, memmap, socket);

        if (s->have_aclint) {
            create_fdt_socket_aclint(s, memmap, socket, intc_phandles);
        } else {
            create_fdt_socket_clint(s, memmap, socket, intc_phandles);
        }

        create_fdt_socket_plic(s, memmap, socket, phandle,
            intc_phandles, xplic_phandles);

        g_free(intc_phandles);
        g_free(clust_name);
    }

    for (socket = 0; socket < riscv_socket_count(mc); socket++) {
        if (socket == 0) {
            *irq_mmio_phandle = xplic_phandles[socket];
            *irq_virtio_phandle = xplic_phandles[socket];
            *irq_pcie_phandle = xplic_phandles[socket];
        }
        if (socket == 1) {
            *irq_virtio_phandle = xplic_phandles[socket];
            *irq_pcie_phandle = xplic_phandles[socket];
        }
        if (socket == 2) {
            *irq_pcie_phandle = xplic_phandles[socket];
        }
    }

    riscv_socket_fdt_write_distance_matrix(mc, mc->fdt);
}

static void create_fdt_virtio(RISCVVirtState *s, const MemMapEntry *memmap,
                              uint32_t irq_virtio_phandle)
{
    int i;
    char *name;
    MachineState *mc = MACHINE(s);

    for (i = 0; i < VIRTIO_COUNT; i++) {
        name = g_strdup_printf("/soc/virtio_mmio@%lx",
            (long)(memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size));
        qemu_fdt_add_subnode(mc->fdt, name);
        qemu_fdt_setprop_string(mc->fdt, name, "compatible", "virtio,mmio");
        qemu_fdt_setprop_cells(mc->fdt, name, "reg",
            0x0, memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            0x0, memmap[VIRT_VIRTIO].size);
        qemu_fdt_setprop_cell(mc->fdt, name, "interrupt-parent",
            irq_virtio_phandle);
        qemu_fdt_setprop_cell(mc->fdt, name, "interrupts", VIRTIO_IRQ + i);
        g_free(name);
    }
}

static void create_fdt_pcie(RISCVVirtState *s, const MemMapEntry *memmap,
                            uint32_t irq_pcie_phandle)
{
    char *name;
    MachineState *mc = MACHINE(s);

    name = g_strdup_printf("/soc/pci@%lx",
        (long) memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_cell(mc->fdt, name, "#address-cells",
        FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cell(mc->fdt, name, "#interrupt-cells",
        FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cell(mc->fdt, name, "#size-cells", 0x2);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible",
        "pci-host-ecam-generic");
    qemu_fdt_setprop_string(mc->fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cell(mc->fdt, name, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(mc->fdt, name, "bus-range", 0,
        memmap[VIRT_PCIE_ECAM].size / PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(mc->fdt, name, "dma-coherent", NULL, 0);
    qemu_fdt_setprop_cells(mc->fdt, name, "reg", 0,
        memmap[VIRT_PCIE_ECAM].base, 0, memmap[VIRT_PCIE_ECAM].size);
    qemu_fdt_setprop_sized_cells(mc->fdt, name, "ranges",
        1, FDT_PCI_RANGE_IOPORT, 2, 0,
        2, memmap[VIRT_PCIE_PIO].base, 2, memmap[VIRT_PCIE_PIO].size,
        1, FDT_PCI_RANGE_MMIO,
        2, memmap[VIRT_PCIE_MMIO].base,
        2, memmap[VIRT_PCIE_MMIO].base, 2, memmap[VIRT_PCIE_MMIO].size,
        1, FDT_PCI_RANGE_MMIO_64BIT,
        2, virt_high_pcie_memmap.base,
        2, virt_high_pcie_memmap.base, 2, virt_high_pcie_memmap.size);

    create_pcie_irq_map(mc->fdt, name, irq_pcie_phandle);
    g_free(name);
}

static void create_fdt_reset(RISCVVirtState *s, const MemMapEntry *memmap,
                             uint32_t *phandle)
{
    char *name;
    uint32_t test_phandle;
    MachineState *mc = MACHINE(s);

    test_phandle = (*phandle)++;
    name = g_strdup_printf("/soc/test@%lx",
        (long)memmap[VIRT_TEST].base);
    qemu_fdt_add_subnode(mc->fdt, name);
    {
        static const char * const compat[3] = {
            "sifive,test1", "sifive,test0", "syscon"
        };
        qemu_fdt_setprop_string_array(mc->fdt, name, "compatible",
                                      (char **)&compat, ARRAY_SIZE(compat));
    }
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, memmap[VIRT_TEST].base, 0x0, memmap[VIRT_TEST].size);
    qemu_fdt_setprop_cell(mc->fdt, name, "phandle", test_phandle);
    test_phandle = qemu_fdt_get_phandle(mc->fdt, name);
    g_free(name);

    name = g_strdup_printf("/soc/reboot");
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(mc->fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(mc->fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(mc->fdt, name, "value", FINISHER_RESET);
    g_free(name);

    name = g_strdup_printf("/soc/poweroff");
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "syscon-poweroff");
    qemu_fdt_setprop_cell(mc->fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(mc->fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(mc->fdt, name, "value", FINISHER_PASS);
    g_free(name);
}

static void create_fdt_uart(RISCVVirtState *s, const MemMapEntry *memmap,
                            uint32_t irq_mmio_phandle)
{
    char *name;
    MachineState *mc = MACHINE(s);

    name = g_strdup_printf("/soc/uart@%lx", (long)memmap[VIRT_UART0].base);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, memmap[VIRT_UART0].base,
        0x0, memmap[VIRT_UART0].size);
    qemu_fdt_setprop_cell(mc->fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(mc->fdt, name, "interrupt-parent", irq_mmio_phandle);
    qemu_fdt_setprop_cell(mc->fdt, name, "interrupts", UART0_IRQ);

    qemu_fdt_add_subnode(mc->fdt, "/chosen");
    qemu_fdt_setprop_string(mc->fdt, "/chosen", "stdout-path", name);
    g_free(name);
}

static void create_fdt_rtc(RISCVVirtState *s, const MemMapEntry *memmap,
                           uint32_t irq_mmio_phandle)
{
    char *name;
    MachineState *mc = MACHINE(s);

    name = g_strdup_printf("/soc/rtc@%lx", (long)memmap[VIRT_RTC].base);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible",
        "google,goldfish-rtc");
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, memmap[VIRT_RTC].base, 0x0, memmap[VIRT_RTC].size);
    qemu_fdt_setprop_cell(mc->fdt, name, "interrupt-parent",
        irq_mmio_phandle);
    qemu_fdt_setprop_cell(mc->fdt, name, "interrupts", RTC_IRQ);
    g_free(name);
}

static void create_fdt_flash(RISCVVirtState *s, const MemMapEntry *memmap)
{
    char *name;
    MachineState *mc = MACHINE(s);
    hwaddr flashsize = virt_memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = virt_memmap[VIRT_FLASH].base;

    name = g_strdup_printf("/flash@%" PRIx64, flashbase);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "cfi-flash");
    qemu_fdt_setprop_sized_cells(mc->fdt, name, "reg",
                                 2, flashbase, 2, flashsize,
                                 2, flashbase + flashsize, 2, flashsize);
    qemu_fdt_setprop_cell(mc->fdt, name, "bank-width", 4);
    g_free(name);
}

static void create_fdt(RISCVVirtState *s, const MemMapEntry *memmap,
                       uint64_t mem_size, const char *cmdline, bool is_32_bit)
{
    MachineState *mc = MACHINE(s);
    uint32_t phandle = 1, irq_mmio_phandle = 1;
    uint32_t irq_pcie_phandle = 1, irq_virtio_phandle = 1;

    if (mc->dtb) {
        mc->fdt = load_device_tree(mc->dtb, &s->fdt_size);
        if (!mc->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
        goto update_bootargs;
    } else {
        mc->fdt = create_device_tree(&s->fdt_size);
        if (!mc->fdt) {
            error_report("create_device_tree() failed");
            exit(1);
        }
    }

    qemu_fdt_setprop_string(mc->fdt, "/", "model", "riscv-virtio,qemu");
    qemu_fdt_setprop_string(mc->fdt, "/", "compatible", "riscv-virtio");
    qemu_fdt_setprop_cell(mc->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(mc->fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(mc->fdt, "/soc");
    qemu_fdt_setprop(mc->fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(mc->fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(mc->fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(mc->fdt, "/soc", "#address-cells", 0x2);

    create_fdt_sockets(s, memmap, is_32_bit, &phandle,
        &irq_mmio_phandle, &irq_pcie_phandle, &irq_virtio_phandle);

    create_fdt_virtio(s, memmap, irq_virtio_phandle);

    create_fdt_pcie(s, memmap, irq_pcie_phandle);

    create_fdt_reset(s, memmap, &phandle);

    create_fdt_uart(s, memmap, irq_mmio_phandle);

    create_fdt_rtc(s, memmap, irq_mmio_phandle);

    create_fdt_flash(s, memmap);

update_bootargs:
    if (cmdline) {
        qemu_fdt_setprop_string(mc->fdt, "/chosen", "bootargs", cmdline);
    }
}

static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          hwaddr ecam_base, hwaddr ecam_size,
                                          hwaddr mmio_base, hwaddr mmio_size,
                                          hwaddr high_mmio_base,
                                          hwaddr high_mmio_size,
                                          hwaddr pio_base,
                                          DeviceState *plic)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *high_mmio_alias, *mmio_reg;
    qemu_irq irq;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

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

    /* Map high MMIO space */
    high_mmio_alias = g_new0(MemoryRegion, 1);
    memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                             mmio_reg, high_mmio_base, high_mmio_size);
    memory_region_add_subregion(get_system_memory(), high_mmio_base,
                                high_mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, pio_base);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        irq = qdev_get_gpio_in(plic, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    return dev;
}

static FWCfgState *create_fw_cfg(const MachineState *mc)
{
    hwaddr base = virt_memmap[VIRT_FW_CFG].base;
    hwaddr size = virt_memmap[VIRT_FW_CFG].size;
    FWCfgState *fw_cfg;
    char *nodename;

    fw_cfg = fw_cfg_init_mem_wide(base + 8, base, 8, base + 16,
                                  &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)mc->smp.cpus);

    nodename = g_strdup_printf("/fw-cfg@%" PRIx64, base);
    qemu_fdt_add_subnode(mc->fdt, nodename);
    qemu_fdt_setprop_string(mc->fdt, nodename,
                            "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(mc->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop(mc->fdt, nodename, "dma-coherent", NULL, 0);
    g_free(nodename);
    return fw_cfg;
}

/*
 * Return the per-socket PLIC hart topology configuration string
 * (caller must free with g_free())
 */
static char *plic_hart_config_string(int hart_count)
{
    g_autofree const char **vals = g_new(const char *, hart_count + 1);
    int i;

    for (i = 0; i < hart_count; i++) {
        vals[i] = VIRT_PLIC_HART_CONFIG;
    }
    vals[i] = NULL;

    /* g_strjoinv() obliges us to cast away const here */
    return g_strjoinv(",", (char **)vals);
}

static void virt_machine_init(MachineState *machine)
{
    const MemMapEntry *memmap = virt_memmap;
    RISCVVirtState *s = RISCV_VIRT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    char *plic_hart_config, *soc_name;
    target_ulong start_addr = memmap[VIRT_DRAM].base;
    target_ulong firmware_end_addr, kernel_start_addr;
    uint32_t fdt_load_addr;
    uint64_t kernel_entry;
    DeviceState *mmio_plic, *virtio_plic, *pcie_plic;
    int i, base_hartid, hart_count;

    /* Check socket count limit */
    if (VIRT_SOCKETS_MAX < riscv_socket_count(machine)) {
        error_report("number of sockets/nodes should be less than %d",
            VIRT_SOCKETS_MAX);
        exit(1);
    }

    /* Initialize sockets */
    mmio_plic = virtio_plic = pcie_plic = NULL;
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
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_abort);

        /* Per-socket CLINT */
        riscv_aclint_swi_create(
            memmap[VIRT_CLINT].base + i * memmap[VIRT_CLINT].size,
            base_hartid, hart_count, false);
        riscv_aclint_mtimer_create(
            memmap[VIRT_CLINT].base + i * memmap[VIRT_CLINT].size +
                RISCV_ACLINT_SWI_SIZE,
            RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
            RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
            RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);

        /* Per-socket ACLINT SSWI */
        if (s->have_aclint) {
            riscv_aclint_swi_create(
                memmap[VIRT_ACLINT_SSWI].base +
                    i * memmap[VIRT_ACLINT_SSWI].size,
                base_hartid, hart_count, true);
        }

        /* Per-socket PLIC hart topology configuration string */
        plic_hart_config = plic_hart_config_string(hart_count);

        /* Per-socket PLIC */
        s->plic[i] = sifive_plic_create(
            memmap[VIRT_PLIC].base + i * memmap[VIRT_PLIC].size,
            plic_hart_config, hart_count, base_hartid,
            VIRT_PLIC_NUM_SOURCES,
            VIRT_PLIC_NUM_PRIORITIES,
            VIRT_PLIC_PRIORITY_BASE,
            VIRT_PLIC_PENDING_BASE,
            VIRT_PLIC_ENABLE_BASE,
            VIRT_PLIC_ENABLE_STRIDE,
            VIRT_PLIC_CONTEXT_BASE,
            VIRT_PLIC_CONTEXT_STRIDE,
            memmap[VIRT_PLIC].size);
        g_free(plic_hart_config);

        /* Try to use different PLIC instance based device type */
        if (i == 0) {
            mmio_plic = s->plic[i];
            virtio_plic = s->plic[i];
            pcie_plic = s->plic[i];
        }
        if (i == 1) {
            virtio_plic = s->plic[i];
            pcie_plic = s->plic[i];
        }
        if (i == 2) {
            pcie_plic = s->plic[i];
        }
    }

    if (riscv_is_32bit(&s->soc[0])) {
#if HOST_LONG_BITS == 64
        /* limit RAM size in a 32-bit system */
        if (machine->ram_size > 10 * GiB) {
            machine->ram_size = 10 * GiB;
            error_report("Limiting RAM size to 10 GiB");
        }
#endif
        virt_high_pcie_memmap.base = VIRT32_HIGH_PCIE_MMIO_BASE;
        virt_high_pcie_memmap.size = VIRT32_HIGH_PCIE_MMIO_SIZE;
    } else {
        virt_high_pcie_memmap.size = VIRT64_HIGH_PCIE_MMIO_SIZE;
        virt_high_pcie_memmap.base = memmap[VIRT_DRAM].base + machine->ram_size;
        virt_high_pcie_memmap.base =
            ROUND_UP(virt_high_pcie_memmap.base, virt_high_pcie_memmap.size);
    }

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv_virt_board.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline,
               riscv_is_32bit(&s->soc[0]));

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           memmap[VIRT_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_MROM].base,
                                mask_rom);

    if (riscv_is_32bit(&s->soc[0])) {
        firmware_end_addr = riscv_find_and_load_firmware(machine,
                                    RISCV32_BIOS_BIN, start_addr, NULL);
    } else {
        firmware_end_addr = riscv_find_and_load_firmware(machine,
                                    RISCV64_BIOS_BIN, start_addr, NULL);
    }

    if (machine->kernel_filename) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc[0],
                                                         firmware_end_addr);

        kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                         kernel_start_addr, NULL);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(machine->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(machine->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    } else {
       /*
        * If dynamic firmware is used, it doesn't know where is the next mode
        * if kernel argument is not set.
        */
        kernel_entry = 0;
    }

    if (drive_get(IF_PFLASH, 0, 0)) {
        /*
         * Pflash was supplied, let's overwrite the address we jump to after
         * reset to the base of the flash.
         */
        start_addr = virt_memmap[VIRT_FLASH].base;
    }

    /*
     * Init fw_cfg.  Must be done before riscv_load_fdt, otherwise the device
     * tree cannot be altered and we get FDT_ERR_NOSPACE.
     */
    s->fw_cfg = create_fw_cfg(machine);
    rom_set_fw(s->fw_cfg);

    /* Compute the fdt load address in dram */
    fdt_load_addr = riscv_load_fdt(memmap[VIRT_DRAM].base,
                                   machine->ram_size, machine->fdt);
    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc[0], start_addr,
                              virt_memmap[VIRT_MROM].base,
                              virt_memmap[VIRT_MROM].size, kernel_entry,
                              fdt_load_addr, machine->fdt);

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[VIRT_TEST].base);

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(DEVICE(virtio_plic), VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory,
                   memmap[VIRT_PCIE_ECAM].base,
                   memmap[VIRT_PCIE_ECAM].size,
                   memmap[VIRT_PCIE_MMIO].base,
                   memmap[VIRT_PCIE_MMIO].size,
                   virt_high_pcie_memmap.base,
                   virt_high_pcie_memmap.size,
                   memmap[VIRT_PCIE_PIO].base,
                   DEVICE(pcie_plic));

    serial_mm_init(system_memory, memmap[VIRT_UART0].base,
        0, qdev_get_gpio_in(DEVICE(mmio_plic), UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    sysbus_create_simple("goldfish_rtc", memmap[VIRT_RTC].base,
        qdev_get_gpio_in(DEVICE(mmio_plic), RTC_IRQ));

    virt_flash_create(s);

    for (i = 0; i < ARRAY_SIZE(s->flash); i++) {
        /* Map legacy -drive if=pflash to machine properties */
        pflash_cfi01_legacy_drive(s->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }
    virt_flash_map(s, system_memory);
}

static void virt_machine_instance_init(Object *obj)
{
}

static bool virt_get_aclint(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);
    RISCVVirtState *s = RISCV_VIRT_MACHINE(ms);

    return s->have_aclint;
}

static void virt_set_aclint(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);
    RISCVVirtState *s = RISCV_VIRT_MACHINE(ms);

    s->have_aclint = value;
}

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V VirtIO board";
    mc->init = virt_machine_init;
    mc->max_cpus = VIRT_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->pci_allow_0_address = true;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);

    object_class_property_add_bool(oc, "aclint", virt_get_aclint,
                                   virt_set_aclint);
    object_class_property_set_description(oc, "aclint",
                                          "Set on/off to enable/disable "
                                          "emulating ACLINT devices");
}

static const TypeInfo virt_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("virt"),
    .parent     = TYPE_MACHINE,
    .class_init = virt_machine_class_init,
    .instance_init = virt_machine_instance_init,
    .instance_size = sizeof(RISCVVirtState),
};

static void virt_machine_init_register_types(void)
{
    type_register_static(&virt_machine_typeinfo);
}

type_init(virt_machine_init_register_types)
