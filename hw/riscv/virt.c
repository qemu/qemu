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
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/core/sysbus-fdt.h"
#include "target/riscv/pmu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"
#include "hw/platform-bus.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/tpm.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"

/*
 * The virt machine physical address space used by some of the devices
 * namely ACLINT, PLIC, APLIC, and IMSIC depend on number of Sockets,
 * number of CPUs, and number of IMSIC guest files.
 *
 * Various limits defined by VIRT_SOCKETS_MAX_BITS, VIRT_CPUS_MAX_BITS,
 * and VIRT_IRQCHIP_MAX_GUESTS_BITS are tuned for maximum utilization
 * of virt machine physical address space.
 */

#define VIRT_IMSIC_GROUP_MAX_SIZE      (1U << IMSIC_MMIO_GROUP_MIN_SHIFT)
#if VIRT_IMSIC_GROUP_MAX_SIZE < \
    IMSIC_GROUP_SIZE(VIRT_CPUS_MAX_BITS, VIRT_IRQCHIP_MAX_GUESTS_BITS)
#error "Can't accomodate single IMSIC group in address space"
#endif

#define VIRT_IMSIC_MAX_SIZE            (VIRT_SOCKETS_MAX * \
                                        VIRT_IMSIC_GROUP_MAX_SIZE)
#if 0x4000000 < VIRT_IMSIC_MAX_SIZE
#error "Can't accomodate all IMSIC groups in address space"
#endif

static const MemMapEntry virt_memmap[] = {
    [VIRT_DEBUG] =        {        0x0,         0x100 },
    [VIRT_MROM] =         {     0x1000,        0xf000 },
    [VIRT_TEST] =         {   0x100000,        0x1000 },
    [VIRT_RTC] =          {   0x101000,        0x1000 },
    [VIRT_CLINT] =        {  0x2000000,       0x10000 },
    [VIRT_ACLINT_SSWI] =  {  0x2F00000,        0x4000 },
    [VIRT_PCIE_PIO] =     {  0x3000000,       0x10000 },
    [VIRT_PLATFORM_BUS] = {  0x4000000,     0x2000000 },
    [VIRT_PLIC] =         {  0xc000000, VIRT_PLIC_SIZE(VIRT_CPUS_MAX * 2) },
    [VIRT_APLIC_M] =      {  0xc000000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_APLIC_S] =      {  0xd000000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_UART0] =        { 0x10000000,         0x100 },
    [VIRT_VIRTIO] =       { 0x10001000,        0x1000 },
    [VIRT_FW_CFG] =       { 0x10100000,          0x18 },
    [VIRT_FLASH] =        { 0x20000000,     0x4000000 },
    [VIRT_IMSIC_M] =      { 0x24000000, VIRT_IMSIC_MAX_SIZE },
    [VIRT_IMSIC_S] =      { 0x28000000, VIRT_IMSIC_MAX_SIZE },
    [VIRT_PCIE_ECAM] =    { 0x30000000,    0x10000000 },
    [VIRT_PCIE_MMIO] =    { 0x40000000,    0x40000000 },
    [VIRT_DRAM] =         { 0x80000000,           0x0 },
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

static void create_pcie_irq_map(RISCVVirtState *s, void *fdt, char *nodename,
                                uint32_t irqchip_phandle)
{
    int pin, dev;
    uint32_t irq_map_stride = 0;
    uint32_t full_irq_map[GPEX_NUM_IRQS * GPEX_NUM_IRQS *
                          FDT_MAX_INT_MAP_WIDTH] = {};
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

            /* Fill PCI address cells */
            irq_map[i] = cpu_to_be32(devfn << 8);
            i += FDT_PCI_ADDR_CELLS;

            /* Fill PCI Interrupt cells */
            irq_map[i] = cpu_to_be32(pin + 1);
            i += FDT_PCI_INT_CELLS;

            /* Fill interrupt controller phandle and cells */
            irq_map[i++] = cpu_to_be32(irqchip_phandle);
            irq_map[i++] = cpu_to_be32(irq_nr);
            if (s->aia_type != VIRT_AIA_TYPE_NONE) {
                irq_map[i++] = cpu_to_be32(0x4);
            }

            if (!irq_map_stride) {
                irq_map_stride = i;
            }
            irq_map += irq_map_stride;
        }
    }

    qemu_fdt_setprop(fdt, nodename, "interrupt-map", full_irq_map,
                     GPEX_NUM_IRQS * GPEX_NUM_IRQS *
                     irq_map_stride * sizeof(uint32_t));

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
        if (riscv_feature(&s->soc[socket].harts[cpu].env,
                          RISCV_FEATURE_MMU)) {
            qemu_fdt_setprop_string(mc->fdt, cpu_name, "mmu-type",
                                    (is_32_bit) ? "riscv,sv32" : "riscv,sv48");
        } else {
            qemu_fdt_setprop_string(mc->fdt, cpu_name, "mmu-type",
                                    "riscv,none");
        }
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
    unsigned long addr, size;
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

    if (s->aia_type != VIRT_AIA_TYPE_APLIC_IMSIC) {
        addr = memmap[VIRT_CLINT].base + (memmap[VIRT_CLINT].size * socket);
        name = g_strdup_printf("/soc/mswi@%lx", addr);
        qemu_fdt_add_subnode(mc->fdt, name);
        qemu_fdt_setprop_string(mc->fdt, name, "compatible",
            "riscv,aclint-mswi");
        qemu_fdt_setprop_cells(mc->fdt, name, "reg",
            0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
        qemu_fdt_setprop(mc->fdt, name, "interrupts-extended",
            aclint_mswi_cells, aclint_cells_size);
        qemu_fdt_setprop(mc->fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(mc->fdt, name, "#interrupt-cells", 0);
        riscv_socket_fdt_write_id(mc, mc->fdt, name, socket);
        g_free(name);
    }

    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        addr = memmap[VIRT_CLINT].base +
               (RISCV_ACLINT_DEFAULT_MTIMER_SIZE * socket);
        size = RISCV_ACLINT_DEFAULT_MTIMER_SIZE;
    } else {
        addr = memmap[VIRT_CLINT].base + RISCV_ACLINT_SWI_SIZE +
            (memmap[VIRT_CLINT].size * socket);
        size = memmap[VIRT_CLINT].size - RISCV_ACLINT_SWI_SIZE;
    }
    name = g_strdup_printf("/soc/mtimer@%lx", addr);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible",
        "riscv,aclint-mtimer");
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIME,
        0x0, size - RISCV_ACLINT_DEFAULT_MTIME,
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
        0x0, RISCV_ACLINT_DEFAULT_MTIME);
    qemu_fdt_setprop(mc->fdt, name, "interrupts-extended",
        aclint_mtimer_cells, aclint_cells_size);
    riscv_socket_fdt_write_id(mc, mc->fdt, name, socket);
    g_free(name);

    if (s->aia_type != VIRT_AIA_TYPE_APLIC_IMSIC) {
        addr = memmap[VIRT_ACLINT_SSWI].base +
            (memmap[VIRT_ACLINT_SSWI].size * socket);
        name = g_strdup_printf("/soc/sswi@%lx", addr);
        qemu_fdt_add_subnode(mc->fdt, name);
        qemu_fdt_setprop_string(mc->fdt, name, "compatible",
            "riscv,aclint-sswi");
        qemu_fdt_setprop_cells(mc->fdt, name, "reg",
            0x0, addr, 0x0, memmap[VIRT_ACLINT_SSWI].size);
        qemu_fdt_setprop(mc->fdt, name, "interrupts-extended",
            aclint_sswi_cells, aclint_cells_size);
        qemu_fdt_setprop(mc->fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(mc->fdt, name, "#interrupt-cells", 0);
        riscv_socket_fdt_write_id(mc, mc->fdt, name, socket);
        g_free(name);
    }

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

    if (kvm_enabled()) {
        plic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);
    } else {
        plic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);
    }

    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        if (kvm_enabled()) {
            plic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
        } else {
            plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
            plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        }
    }

    plic_phandles[socket] = (*phandle)++;
    plic_addr = memmap[VIRT_PLIC].base + (memmap[VIRT_PLIC].size * socket);
    plic_name = g_strdup_printf("/soc/plic@%lx", plic_addr);
    qemu_fdt_add_subnode(mc->fdt, plic_name);
    qemu_fdt_setprop_cell(mc->fdt, plic_name,
        "#interrupt-cells", FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_cell(mc->fdt, plic_name,
        "#address-cells", FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_string_array(mc->fdt, plic_name, "compatible",
                                  (char **)&plic_compat,
                                  ARRAY_SIZE(plic_compat));
    qemu_fdt_setprop(mc->fdt, plic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(mc->fdt, plic_name, "interrupts-extended",
        plic_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(mc->fdt, plic_name, "reg",
        0x0, plic_addr, 0x0, memmap[VIRT_PLIC].size);
    qemu_fdt_setprop_cell(mc->fdt, plic_name, "riscv,ndev",
                          VIRT_IRQCHIP_NUM_SOURCES - 1);
    riscv_socket_fdt_write_id(mc, mc->fdt, plic_name, socket);
    qemu_fdt_setprop_cell(mc->fdt, plic_name, "phandle",
        plic_phandles[socket]);

    if (!socket) {
        platform_bus_add_all_fdt_nodes(mc->fdt, plic_name,
                                       memmap[VIRT_PLATFORM_BUS].base,
                                       memmap[VIRT_PLATFORM_BUS].size,
                                       VIRT_PLATFORM_BUS_IRQ);
    }

    g_free(plic_name);

    g_free(plic_cells);
}

static uint32_t imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}

static void create_fdt_imsic(RISCVVirtState *s, const MemMapEntry *memmap,
                             uint32_t *phandle, uint32_t *intc_phandles,
                             uint32_t *msi_m_phandle, uint32_t *msi_s_phandle)
{
    int cpu, socket;
    char *imsic_name;
    MachineState *mc = MACHINE(s);
    uint32_t imsic_max_hart_per_socket, imsic_guest_bits;
    uint32_t *imsic_cells, *imsic_regs, imsic_addr, imsic_size;

    *msi_m_phandle = (*phandle)++;
    *msi_s_phandle = (*phandle)++;
    imsic_cells = g_new0(uint32_t, mc->smp.cpus * 2);
    imsic_regs = g_new0(uint32_t, riscv_socket_count(mc) * 4);

    /* M-level IMSIC node */
    for (cpu = 0; cpu < mc->smp.cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
    }
    imsic_max_hart_per_socket = 0;
    for (socket = 0; socket < riscv_socket_count(mc); socket++) {
        imsic_addr = memmap[VIRT_IMSIC_M].base +
                     socket * VIRT_IMSIC_GROUP_MAX_SIZE;
        imsic_size = IMSIC_HART_SIZE(0) * s->soc[socket].num_harts;
        imsic_regs[socket * 4 + 0] = 0;
        imsic_regs[socket * 4 + 1] = cpu_to_be32(imsic_addr);
        imsic_regs[socket * 4 + 2] = 0;
        imsic_regs[socket * 4 + 3] = cpu_to_be32(imsic_size);
        if (imsic_max_hart_per_socket < s->soc[socket].num_harts) {
            imsic_max_hart_per_socket = s->soc[socket].num_harts;
        }
    }
    imsic_name = g_strdup_printf("/soc/imsics@%lx",
        (unsigned long)memmap[VIRT_IMSIC_M].base);
    qemu_fdt_add_subnode(mc->fdt, imsic_name);
    qemu_fdt_setprop_string(mc->fdt, imsic_name, "compatible",
        "riscv,imsics");
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "#interrupt-cells",
        FDT_IMSIC_INT_CELLS);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupt-controller",
        NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "msi-controller",
        NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupts-extended",
        imsic_cells, mc->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(mc->fdt, imsic_name, "reg", imsic_regs,
        riscv_socket_count(mc) * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,num-ids",
        VIRT_IRQCHIP_NUM_MSIS);
    if (riscv_socket_count(mc) > 1) {
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,hart-index-bits",
            imsic_num_bits(imsic_max_hart_per_socket));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-bits",
            imsic_num_bits(riscv_socket_count(mc)));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-shift",
            IMSIC_MMIO_GROUP_MIN_SHIFT);
    }
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "phandle", *msi_m_phandle);

    g_free(imsic_name);

    /* S-level IMSIC node */
    for (cpu = 0; cpu < mc->smp.cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
    }
    imsic_guest_bits = imsic_num_bits(s->aia_guests + 1);
    imsic_max_hart_per_socket = 0;
    for (socket = 0; socket < riscv_socket_count(mc); socket++) {
        imsic_addr = memmap[VIRT_IMSIC_S].base +
                     socket * VIRT_IMSIC_GROUP_MAX_SIZE;
        imsic_size = IMSIC_HART_SIZE(imsic_guest_bits) *
                     s->soc[socket].num_harts;
        imsic_regs[socket * 4 + 0] = 0;
        imsic_regs[socket * 4 + 1] = cpu_to_be32(imsic_addr);
        imsic_regs[socket * 4 + 2] = 0;
        imsic_regs[socket * 4 + 3] = cpu_to_be32(imsic_size);
        if (imsic_max_hart_per_socket < s->soc[socket].num_harts) {
            imsic_max_hart_per_socket = s->soc[socket].num_harts;
        }
    }
    imsic_name = g_strdup_printf("/soc/imsics@%lx",
        (unsigned long)memmap[VIRT_IMSIC_S].base);
    qemu_fdt_add_subnode(mc->fdt, imsic_name);
    qemu_fdt_setprop_string(mc->fdt, imsic_name, "compatible",
        "riscv,imsics");
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "#interrupt-cells",
        FDT_IMSIC_INT_CELLS);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupt-controller",
        NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "msi-controller",
        NULL, 0);
    qemu_fdt_setprop(mc->fdt, imsic_name, "interrupts-extended",
        imsic_cells, mc->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(mc->fdt, imsic_name, "reg", imsic_regs,
        riscv_socket_count(mc) * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,num-ids",
        VIRT_IRQCHIP_NUM_MSIS);
    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,guest-index-bits",
            imsic_guest_bits);
    }
    if (riscv_socket_count(mc) > 1) {
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,hart-index-bits",
            imsic_num_bits(imsic_max_hart_per_socket));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-bits",
            imsic_num_bits(riscv_socket_count(mc)));
        qemu_fdt_setprop_cell(mc->fdt, imsic_name, "riscv,group-index-shift",
            IMSIC_MMIO_GROUP_MIN_SHIFT);
    }
    qemu_fdt_setprop_cell(mc->fdt, imsic_name, "phandle", *msi_s_phandle);
    g_free(imsic_name);

    g_free(imsic_regs);
    g_free(imsic_cells);
}

static void create_fdt_socket_aplic(RISCVVirtState *s,
                                    const MemMapEntry *memmap, int socket,
                                    uint32_t msi_m_phandle,
                                    uint32_t msi_s_phandle,
                                    uint32_t *phandle,
                                    uint32_t *intc_phandles,
                                    uint32_t *aplic_phandles)
{
    int cpu;
    char *aplic_name;
    uint32_t *aplic_cells;
    unsigned long aplic_addr;
    MachineState *mc = MACHINE(s);
    uint32_t aplic_m_phandle, aplic_s_phandle;

    aplic_m_phandle = (*phandle)++;
    aplic_s_phandle = (*phandle)++;
    aplic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);

    /* M-level APLIC node */
    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aplic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_EXT);
    }
    aplic_addr = memmap[VIRT_APLIC_M].base +
                 (memmap[VIRT_APLIC_M].size * socket);
    aplic_name = g_strdup_printf("/soc/aplic@%lx", aplic_addr);
    qemu_fdt_add_subnode(mc->fdt, aplic_name);
    qemu_fdt_setprop_string(mc->fdt, aplic_name, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(mc->fdt, aplic_name,
        "#interrupt-cells", FDT_APLIC_INT_CELLS);
    qemu_fdt_setprop(mc->fdt, aplic_name, "interrupt-controller", NULL, 0);
    if (s->aia_type == VIRT_AIA_TYPE_APLIC) {
        qemu_fdt_setprop(mc->fdt, aplic_name, "interrupts-extended",
            aplic_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 2);
    } else {
        qemu_fdt_setprop_cell(mc->fdt, aplic_name, "msi-parent",
            msi_m_phandle);
    }
    qemu_fdt_setprop_cells(mc->fdt, aplic_name, "reg",
        0x0, aplic_addr, 0x0, memmap[VIRT_APLIC_M].size);
    qemu_fdt_setprop_cell(mc->fdt, aplic_name, "riscv,num-sources",
        VIRT_IRQCHIP_NUM_SOURCES);
    qemu_fdt_setprop_cell(mc->fdt, aplic_name, "riscv,children",
        aplic_s_phandle);
    qemu_fdt_setprop_cells(mc->fdt, aplic_name, "riscv,delegate",
        aplic_s_phandle, 0x1, VIRT_IRQCHIP_NUM_SOURCES);
    riscv_socket_fdt_write_id(mc, mc->fdt, aplic_name, socket);
    qemu_fdt_setprop_cell(mc->fdt, aplic_name, "phandle", aplic_m_phandle);
    g_free(aplic_name);

    /* S-level APLIC node */
    for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
        aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aplic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
    }
    aplic_addr = memmap[VIRT_APLIC_S].base +
                 (memmap[VIRT_APLIC_S].size * socket);
    aplic_name = g_strdup_printf("/soc/aplic@%lx", aplic_addr);
    qemu_fdt_add_subnode(mc->fdt, aplic_name);
    qemu_fdt_setprop_string(mc->fdt, aplic_name, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(mc->fdt, aplic_name,
        "#interrupt-cells", FDT_APLIC_INT_CELLS);
    qemu_fdt_setprop(mc->fdt, aplic_name, "interrupt-controller", NULL, 0);
    if (s->aia_type == VIRT_AIA_TYPE_APLIC) {
        qemu_fdt_setprop(mc->fdt, aplic_name, "interrupts-extended",
            aplic_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 2);
    } else {
        qemu_fdt_setprop_cell(mc->fdt, aplic_name, "msi-parent",
            msi_s_phandle);
    }
    qemu_fdt_setprop_cells(mc->fdt, aplic_name, "reg",
        0x0, aplic_addr, 0x0, memmap[VIRT_APLIC_S].size);
    qemu_fdt_setprop_cell(mc->fdt, aplic_name, "riscv,num-sources",
        VIRT_IRQCHIP_NUM_SOURCES);
    riscv_socket_fdt_write_id(mc, mc->fdt, aplic_name, socket);
    qemu_fdt_setprop_cell(mc->fdt, aplic_name, "phandle", aplic_s_phandle);

    if (!socket) {
        platform_bus_add_all_fdt_nodes(mc->fdt, aplic_name,
                                       memmap[VIRT_PLATFORM_BUS].base,
                                       memmap[VIRT_PLATFORM_BUS].size,
                                       VIRT_PLATFORM_BUS_IRQ);
    }

    g_free(aplic_name);

    g_free(aplic_cells);
    aplic_phandles[socket] = aplic_s_phandle;
}

static void create_fdt_pmu(RISCVVirtState *s)
{
    char *pmu_name;
    MachineState *mc = MACHINE(s);
    RISCVCPU hart = s->soc[0].harts[0];

    pmu_name = g_strdup_printf("/soc/pmu");
    qemu_fdt_add_subnode(mc->fdt, pmu_name);
    qemu_fdt_setprop_string(mc->fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(mc->fdt, hart.cfg.pmu_num, pmu_name);

    g_free(pmu_name);
}

static void create_fdt_sockets(RISCVVirtState *s, const MemMapEntry *memmap,
                               bool is_32_bit, uint32_t *phandle,
                               uint32_t *irq_mmio_phandle,
                               uint32_t *irq_pcie_phandle,
                               uint32_t *irq_virtio_phandle,
                               uint32_t *msi_pcie_phandle)
{
    char *clust_name;
    int socket, phandle_pos;
    MachineState *mc = MACHINE(s);
    uint32_t msi_m_phandle = 0, msi_s_phandle = 0;
    uint32_t *intc_phandles, xplic_phandles[MAX_NODES];

    qemu_fdt_add_subnode(mc->fdt, "/cpus");
    qemu_fdt_setprop_cell(mc->fdt, "/cpus", "timebase-frequency",
                          RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(mc->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(mc->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(mc->fdt, "/cpus/cpu-map");

    intc_phandles = g_new0(uint32_t, mc->smp.cpus);

    phandle_pos = mc->smp.cpus;
    for (socket = (riscv_socket_count(mc) - 1); socket >= 0; socket--) {
        phandle_pos -= s->soc[socket].num_harts;

        clust_name = g_strdup_printf("/cpus/cpu-map/cluster%d", socket);
        qemu_fdt_add_subnode(mc->fdt, clust_name);

        create_fdt_socket_cpus(s, socket, clust_name, phandle,
            is_32_bit, &intc_phandles[phandle_pos]);

        create_fdt_socket_memory(s, memmap, socket);

        g_free(clust_name);

        if (!kvm_enabled()) {
            if (s->have_aclint) {
                create_fdt_socket_aclint(s, memmap, socket,
                    &intc_phandles[phandle_pos]);
            } else {
                create_fdt_socket_clint(s, memmap, socket,
                    &intc_phandles[phandle_pos]);
            }
        }
    }

    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        create_fdt_imsic(s, memmap, phandle, intc_phandles,
            &msi_m_phandle, &msi_s_phandle);
        *msi_pcie_phandle = msi_s_phandle;
    }

    phandle_pos = mc->smp.cpus;
    for (socket = (riscv_socket_count(mc) - 1); socket >= 0; socket--) {
        phandle_pos -= s->soc[socket].num_harts;

        if (s->aia_type == VIRT_AIA_TYPE_NONE) {
            create_fdt_socket_plic(s, memmap, socket, phandle,
                &intc_phandles[phandle_pos], xplic_phandles);
        } else {
            create_fdt_socket_aplic(s, memmap, socket,
                msi_m_phandle, msi_s_phandle, phandle,
                &intc_phandles[phandle_pos], xplic_phandles);
        }
    }

    g_free(intc_phandles);

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
        if (s->aia_type == VIRT_AIA_TYPE_NONE) {
            qemu_fdt_setprop_cell(mc->fdt, name, "interrupts",
                                  VIRTIO_IRQ + i);
        } else {
            qemu_fdt_setprop_cells(mc->fdt, name, "interrupts",
                                   VIRTIO_IRQ + i, 0x4);
        }
        g_free(name);
    }
}

static void create_fdt_pcie(RISCVVirtState *s, const MemMapEntry *memmap,
                            uint32_t irq_pcie_phandle,
                            uint32_t msi_pcie_phandle)
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
    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        qemu_fdt_setprop_cell(mc->fdt, name, "msi-parent", msi_pcie_phandle);
    }
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

    create_pcie_irq_map(s, mc->fdt, name, irq_pcie_phandle);
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

    name = g_strdup_printf("/reboot");
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(mc->fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(mc->fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(mc->fdt, name, "value", FINISHER_RESET);
    g_free(name);

    name = g_strdup_printf("/poweroff");
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

    name = g_strdup_printf("/soc/serial@%lx", (long)memmap[VIRT_UART0].base);
    qemu_fdt_add_subnode(mc->fdt, name);
    qemu_fdt_setprop_string(mc->fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(mc->fdt, name, "reg",
        0x0, memmap[VIRT_UART0].base,
        0x0, memmap[VIRT_UART0].size);
    qemu_fdt_setprop_cell(mc->fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(mc->fdt, name, "interrupt-parent", irq_mmio_phandle);
    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cell(mc->fdt, name, "interrupts", UART0_IRQ);
    } else {
        qemu_fdt_setprop_cells(mc->fdt, name, "interrupts", UART0_IRQ, 0x4);
    }

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
    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cell(mc->fdt, name, "interrupts", RTC_IRQ);
    } else {
        qemu_fdt_setprop_cells(mc->fdt, name, "interrupts", RTC_IRQ, 0x4);
    }
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

static void create_fdt_fw_cfg(RISCVVirtState *s, const MemMapEntry *memmap)
{
    char *nodename;
    MachineState *mc = MACHINE(s);
    hwaddr base = memmap[VIRT_FW_CFG].base;
    hwaddr size = memmap[VIRT_FW_CFG].size;

    nodename = g_strdup_printf("/fw-cfg@%" PRIx64, base);
    qemu_fdt_add_subnode(mc->fdt, nodename);
    qemu_fdt_setprop_string(mc->fdt, nodename,
                            "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(mc->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop(mc->fdt, nodename, "dma-coherent", NULL, 0);
    g_free(nodename);
}

static void create_fdt(RISCVVirtState *s, const MemMapEntry *memmap,
                       uint64_t mem_size, const char *cmdline, bool is_32_bit)
{
    MachineState *mc = MACHINE(s);
    uint32_t phandle = 1, irq_mmio_phandle = 1, msi_pcie_phandle = 1;
    uint32_t irq_pcie_phandle = 1, irq_virtio_phandle = 1;
    uint8_t rng_seed[32];

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
        &irq_mmio_phandle, &irq_pcie_phandle, &irq_virtio_phandle,
        &msi_pcie_phandle);

    create_fdt_virtio(s, memmap, irq_virtio_phandle);

    create_fdt_pcie(s, memmap, irq_pcie_phandle, msi_pcie_phandle);

    create_fdt_reset(s, memmap, &phandle);

    create_fdt_uart(s, memmap, irq_mmio_phandle);

    create_fdt_rtc(s, memmap, irq_mmio_phandle);

    create_fdt_flash(s, memmap);
    create_fdt_fw_cfg(s, memmap);
    create_fdt_pmu(s);

update_bootargs:
    if (cmdline && *cmdline) {
        qemu_fdt_setprop_string(mc->fdt, "/chosen", "bootargs", cmdline);
    }

    /* Pass seed to RNG */
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(mc->fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
}

static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          hwaddr ecam_base, hwaddr ecam_size,
                                          hwaddr mmio_base, hwaddr mmio_size,
                                          hwaddr high_mmio_base,
                                          hwaddr high_mmio_size,
                                          hwaddr pio_base,
                                          DeviceState *irqchip)
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
        irq = qdev_get_gpio_in(irqchip, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    return dev;
}

static FWCfgState *create_fw_cfg(const MachineState *mc)
{
    hwaddr base = virt_memmap[VIRT_FW_CFG].base;
    FWCfgState *fw_cfg;

    fw_cfg = fw_cfg_init_mem_wide(base + 8, base, 8, base + 16,
                                  &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)mc->smp.cpus);

    return fw_cfg;
}

static DeviceState *virt_create_plic(const MemMapEntry *memmap, int socket,
                                     int base_hartid, int hart_count)
{
    DeviceState *ret;
    char *plic_hart_config;

    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    /* Per-socket PLIC */
    ret = sifive_plic_create(
            memmap[VIRT_PLIC].base + socket * memmap[VIRT_PLIC].size,
            plic_hart_config, hart_count, base_hartid,
            VIRT_IRQCHIP_NUM_SOURCES,
            ((1U << VIRT_IRQCHIP_NUM_PRIO_BITS) - 1),
            VIRT_PLIC_PRIORITY_BASE,
            VIRT_PLIC_PENDING_BASE,
            VIRT_PLIC_ENABLE_BASE,
            VIRT_PLIC_ENABLE_STRIDE,
            VIRT_PLIC_CONTEXT_BASE,
            VIRT_PLIC_CONTEXT_STRIDE,
            memmap[VIRT_PLIC].size);

    g_free(plic_hart_config);

    return ret;
}

static DeviceState *virt_create_aia(RISCVVirtAIAType aia_type, int aia_guests,
                                    const MemMapEntry *memmap, int socket,
                                    int base_hartid, int hart_count)
{
    int i;
    hwaddr addr;
    uint32_t guest_bits;
    DeviceState *aplic_m;
    bool msimode = (aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) ? true : false;

    if (msimode) {
        /* Per-socket M-level IMSICs */
        addr = memmap[VIRT_IMSIC_M].base + socket * VIRT_IMSIC_GROUP_MAX_SIZE;
        for (i = 0; i < hart_count; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0),
                               base_hartid + i, true, 1,
                               VIRT_IRQCHIP_NUM_MSIS);
        }

        /* Per-socket S-level IMSICs */
        guest_bits = imsic_num_bits(aia_guests + 1);
        addr = memmap[VIRT_IMSIC_S].base + socket * VIRT_IMSIC_GROUP_MAX_SIZE;
        for (i = 0; i < hart_count; i++) {
            riscv_imsic_create(addr + i * IMSIC_HART_SIZE(guest_bits),
                               base_hartid + i, false, 1 + aia_guests,
                               VIRT_IRQCHIP_NUM_MSIS);
        }
    }

    /* Per-socket M-level APLIC */
    aplic_m = riscv_aplic_create(
        memmap[VIRT_APLIC_M].base + socket * memmap[VIRT_APLIC_M].size,
        memmap[VIRT_APLIC_M].size,
        (msimode) ? 0 : base_hartid,
        (msimode) ? 0 : hart_count,
        VIRT_IRQCHIP_NUM_SOURCES,
        VIRT_IRQCHIP_NUM_PRIO_BITS,
        msimode, true, NULL);

    if (aplic_m) {
        /* Per-socket S-level APLIC */
        riscv_aplic_create(
            memmap[VIRT_APLIC_S].base + socket * memmap[VIRT_APLIC_S].size,
            memmap[VIRT_APLIC_S].size,
            (msimode) ? 0 : base_hartid,
            (msimode) ? 0 : hart_count,
            VIRT_IRQCHIP_NUM_SOURCES,
            VIRT_IRQCHIP_NUM_PRIO_BITS,
            msimode, false, aplic_m);
    }

    return aplic_m;
}

static void create_platform_bus(RISCVVirtState *s, DeviceState *irqchip)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    const MemMapEntry *memmap = virt_memmap;
    int i;
    MemoryRegion *sysmem = get_system_memory();

    dev = qdev_new(TYPE_PLATFORM_BUS_DEVICE);
    dev->id = g_strdup(TYPE_PLATFORM_BUS_DEVICE);
    qdev_prop_set_uint32(dev, "num_irqs", VIRT_PLATFORM_BUS_NUM_IRQS);
    qdev_prop_set_uint32(dev, "mmio_size", memmap[VIRT_PLATFORM_BUS].size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    s->platform_bus_dev = dev;

    sysbus = SYS_BUS_DEVICE(dev);
    for (i = 0; i < VIRT_PLATFORM_BUS_NUM_IRQS; i++) {
        int irq = VIRT_PLATFORM_BUS_IRQ + i;
        sysbus_connect_irq(sysbus, i, qdev_get_gpio_in(irqchip, irq));
    }

    memory_region_add_subregion(sysmem,
                                memmap[VIRT_PLATFORM_BUS].base,
                                sysbus_mmio_get_region(sysbus, 0));
}

static void virt_machine_done(Notifier *notifier, void *data)
{
    RISCVVirtState *s = container_of(notifier, RISCVVirtState,
                                     machine_done);
    const MemMapEntry *memmap = virt_memmap;
    MachineState *machine = MACHINE(s);
    target_ulong start_addr = memmap[VIRT_DRAM].base;
    target_ulong firmware_end_addr, kernel_start_addr;
    uint32_t fdt_load_addr;
    uint64_t kernel_entry;

    /*
     * Only direct boot kernel is currently supported for KVM VM,
     * so the "-bios" parameter is not supported when KVM is enabled.
     */
    if (kvm_enabled()) {
        if (machine->firmware) {
            if (strcmp(machine->firmware, "none")) {
                error_report("Machine mode firmware is not supported in "
                             "combination with KVM.");
                exit(1);
            }
        } else {
            machine->firmware = g_strdup("none");
        }
    }

    if (riscv_is_32bit(&s->soc[0])) {
        firmware_end_addr = riscv_find_and_load_firmware(machine,
                                    RISCV32_BIOS_BIN, start_addr, NULL);
    } else {
        firmware_end_addr = riscv_find_and_load_firmware(machine,
                                    RISCV64_BIOS_BIN, start_addr, NULL);
    }

    /*
     * Init fw_cfg.  Must be done before riscv_load_fdt, otherwise the device
     * tree cannot be altered and we get FDT_ERR_NOSPACE.
     */
    s->fw_cfg = create_fw_cfg(machine);
    rom_set_fw(s->fw_cfg);

    if (drive_get(IF_PFLASH, 0, 1)) {
        /*
         * S-mode FW like EDK2 will be kept in second plash (unit 1).
         * When both kernel, initrd and pflash options are provided in the
         * command line, the kernel and initrd will be copied to the fw_cfg
         * table and opensbi will jump to the flash address which is the
         * entry point of S-mode FW. It is the job of the S-mode FW to load
         * the kernel and initrd using fw_cfg table.
         *
         * If only pflash is given but not -kernel, then it is the job of
         * of the S-mode firmware to locate and load the kernel.
         * In either case, the next_addr for opensbi will be the flash address.
         */
        riscv_setup_firmware_boot(machine);
        kernel_entry = virt_memmap[VIRT_FLASH].base +
                       virt_memmap[VIRT_FLASH].size / 2;
    } else if (machine->kernel_filename) {
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

    /* Compute the fdt load address in dram */
    fdt_load_addr = riscv_load_fdt(memmap[VIRT_DRAM].base,
                                   machine->ram_size, machine->fdt);
    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc[0], start_addr,
                              virt_memmap[VIRT_MROM].base,
                              virt_memmap[VIRT_MROM].size, kernel_entry,
                              fdt_load_addr);

    /*
     * Only direct boot kernel is currently supported for KVM VM,
     * So here setup kernel start address and fdt address.
     * TODO:Support firmware loading and integrate to TCG start
     */
    if (kvm_enabled()) {
        riscv_setup_direct_kernel(kernel_entry, fdt_load_addr);
    }
}

static void virt_machine_init(MachineState *machine)
{
    const MemMapEntry *memmap = virt_memmap;
    RISCVVirtState *s = RISCV_VIRT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    char *soc_name;
    DeviceState *mmio_irqchip, *virtio_irqchip, *pcie_irqchip;
    int i, base_hartid, hart_count;

    /* Check socket count limit */
    if (VIRT_SOCKETS_MAX < riscv_socket_count(machine)) {
        error_report("number of sockets/nodes should be less than %d",
            VIRT_SOCKETS_MAX);
        exit(1);
    }

    /* Initialize sockets */
    mmio_irqchip = virtio_irqchip = pcie_irqchip = NULL;
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

        if (!kvm_enabled()) {
            if (s->have_aclint) {
                if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
                    /* Per-socket ACLINT MTIMER */
                    riscv_aclint_mtimer_create(memmap[VIRT_CLINT].base +
                            i * RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        base_hartid, hart_count,
                        RISCV_ACLINT_DEFAULT_MTIMECMP,
                        RISCV_ACLINT_DEFAULT_MTIME,
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
                } else {
                    /* Per-socket ACLINT MSWI, MTIMER, and SSWI */
                    riscv_aclint_swi_create(memmap[VIRT_CLINT].base +
                            i * memmap[VIRT_CLINT].size,
                        base_hartid, hart_count, false);
                    riscv_aclint_mtimer_create(memmap[VIRT_CLINT].base +
                            i * memmap[VIRT_CLINT].size +
                            RISCV_ACLINT_SWI_SIZE,
                        RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        base_hartid, hart_count,
                        RISCV_ACLINT_DEFAULT_MTIMECMP,
                        RISCV_ACLINT_DEFAULT_MTIME,
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
                    riscv_aclint_swi_create(memmap[VIRT_ACLINT_SSWI].base +
                            i * memmap[VIRT_ACLINT_SSWI].size,
                        base_hartid, hart_count, true);
                }
            } else {
                /* Per-socket SiFive CLINT */
                riscv_aclint_swi_create(
                    memmap[VIRT_CLINT].base + i * memmap[VIRT_CLINT].size,
                    base_hartid, hart_count, false);
                riscv_aclint_mtimer_create(memmap[VIRT_CLINT].base +
                        i * memmap[VIRT_CLINT].size + RISCV_ACLINT_SWI_SIZE,
                    RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
                    RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
                    RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
            }
        }

        /* Per-socket interrupt controller */
        if (s->aia_type == VIRT_AIA_TYPE_NONE) {
            s->irqchip[i] = virt_create_plic(memmap, i,
                                             base_hartid, hart_count);
        } else {
            s->irqchip[i] = virt_create_aia(s->aia_type, s->aia_guests,
                                            memmap, i, base_hartid,
                                            hart_count);
        }

        /* Try to use different IRQCHIP instance based device type */
        if (i == 0) {
            mmio_irqchip = s->irqchip[i];
            virtio_irqchip = s->irqchip[i];
            pcie_irqchip = s->irqchip[i];
        }
        if (i == 1) {
            virtio_irqchip = s->irqchip[i];
            pcie_irqchip = s->irqchip[i];
        }
        if (i == 2) {
            pcie_irqchip = s->irqchip[i];
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
    memory_region_add_subregion(system_memory, memmap[VIRT_DRAM].base,
        machine->ram);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           memmap[VIRT_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_MROM].base,
                                mask_rom);

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[VIRT_TEST].base);

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(DEVICE(virtio_irqchip), VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory,
                   memmap[VIRT_PCIE_ECAM].base,
                   memmap[VIRT_PCIE_ECAM].size,
                   memmap[VIRT_PCIE_MMIO].base,
                   memmap[VIRT_PCIE_MMIO].size,
                   virt_high_pcie_memmap.base,
                   virt_high_pcie_memmap.size,
                   memmap[VIRT_PCIE_PIO].base,
                   DEVICE(pcie_irqchip));

    create_platform_bus(s, DEVICE(mmio_irqchip));

    serial_mm_init(system_memory, memmap[VIRT_UART0].base,
        0, qdev_get_gpio_in(DEVICE(mmio_irqchip), UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    sysbus_create_simple("goldfish_rtc", memmap[VIRT_RTC].base,
        qdev_get_gpio_in(DEVICE(mmio_irqchip), RTC_IRQ));

    virt_flash_create(s);

    for (i = 0; i < ARRAY_SIZE(s->flash); i++) {
        /* Map legacy -drive if=pflash to machine properties */
        pflash_cfi01_legacy_drive(s->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }
    virt_flash_map(s, system_memory);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline,
               riscv_is_32bit(&s->soc[0]));

    s->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void virt_machine_instance_init(Object *obj)
{
}

static char *virt_get_aia_guests(Object *obj, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    char val[32];

    sprintf(val, "%d", s->aia_guests);
    return g_strdup(val);
}

static void virt_set_aia_guests(Object *obj, const char *val, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    s->aia_guests = atoi(val);
    if (s->aia_guests < 0 || s->aia_guests > VIRT_IRQCHIP_MAX_GUESTS) {
        error_setg(errp, "Invalid number of AIA IMSIC guests");
        error_append_hint(errp, "Valid values be between 0 and %d.\n",
                          VIRT_IRQCHIP_MAX_GUESTS);
    }
}

static char *virt_get_aia(Object *obj, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    const char *val;

    switch (s->aia_type) {
    case VIRT_AIA_TYPE_APLIC:
        val = "aplic";
        break;
    case VIRT_AIA_TYPE_APLIC_IMSIC:
        val = "aplic-imsic";
        break;
    default:
        val = "none";
        break;
    };

    return g_strdup(val);
}

static void virt_set_aia(Object *obj, const char *val, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    if (!strcmp(val, "none")) {
        s->aia_type = VIRT_AIA_TYPE_NONE;
    } else if (!strcmp(val, "aplic")) {
        s->aia_type = VIRT_AIA_TYPE_APLIC;
    } else if (!strcmp(val, "aplic-imsic")) {
        s->aia_type = VIRT_AIA_TYPE_APLIC_IMSIC;
    } else {
        error_setg(errp, "Invalid AIA interrupt controller type");
        error_append_hint(errp, "Valid values are none, aplic, and "
                          "aplic-imsic.\n");
    }
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

static HotplugHandler *virt_machine_get_hotplug_handler(MachineState *machine,
                                                        DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

static void virt_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(hotplug_dev);

    if (s->platform_bus_dev) {
        MachineClass *mc = MACHINE_GET_CLASS(s);

        if (device_is_dynamic_sysbus(mc, dev)) {
            platform_bus_link_device(PLATFORM_BUS_DEVICE(s->platform_bus_dev),
                                     SYS_BUS_DEVICE(dev));
        }
    }
}

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    char str[128];
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->desc = "RISC-V VirtIO board";
    mc->init = virt_machine_init;
    mc->max_cpus = VIRT_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->pci_allow_0_address = true;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    mc->default_ram_id = "riscv_virt_board.ram";
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = virt_machine_get_hotplug_handler;

    hc->plug = virt_machine_device_plug_cb;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);
#ifdef CONFIG_TPM
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_TPM_TIS_SYSBUS);
#endif

    object_class_property_add_bool(oc, "aclint", virt_get_aclint,
                                   virt_set_aclint);
    object_class_property_set_description(oc, "aclint",
                                          "Set on/off to enable/disable "
                                          "emulating ACLINT devices");

    object_class_property_add_str(oc, "aia", virt_get_aia,
                                  virt_set_aia);
    object_class_property_set_description(oc, "aia",
                                          "Set type of AIA interrupt "
                                          "conttoller. Valid values are "
                                          "none, aplic, and aplic-imsic.");

    object_class_property_add_str(oc, "aia-guests",
                                  virt_get_aia_guests,
                                  virt_set_aia_guests);
    sprintf(str, "Set number of guest MMIO pages for AIA IMSIC. Valid value "
                 "should be between 0 and %d.", VIRT_IRQCHIP_MAX_GUESTS);
    object_class_property_set_description(oc, "aia-guests", str);
}

static const TypeInfo virt_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("virt"),
    .parent     = TYPE_MACHINE,
    .class_init = virt_machine_class_init,
    .instance_init = virt_machine_instance_init,
    .instance_size = sizeof(RISCVVirtState),
    .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void virt_machine_init_register_types(void)
{
    type_register_static(&virt_machine_typeinfo);
}

type_init(virt_machine_init_register_types)
