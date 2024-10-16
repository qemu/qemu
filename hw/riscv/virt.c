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
#include "hw/char/serial-mm.h"
#include "target/riscv/cpu.h"
#include "hw/core/sysbus-fdt.h"
#include "target/riscv/pmu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/numa.h"
#include "kvm/kvm_riscv.h"
#include "hw/firmware/smbios.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"
#include "hw/platform-bus.h"
#include "chardev/char.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "sysemu/kvm.h"
#include "sysemu/tpm.h"
#include "sysemu/qtest.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"
#include "hw/acpi/aml-build.h"
#include "qapi/qapi-visit-common.h"
#include "hw/virtio/virtio-iommu.h"

/* KVM AIA only supports APLIC MSI. APLIC Wired is always emulated by QEMU. */
static bool virt_use_kvm_aia(RISCVVirtState *s)
{
    return kvm_irqchip_in_kernel() && s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC;
}

static bool virt_aclint_allowed(void)
{
    return tcg_enabled() || qtest_enabled();
}

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
                                   uint32_t *intc_phandles)
{
    int cpu;
    uint32_t cpu_phandle;
    MachineState *ms = MACHINE(s);
    bool is_32_bit = riscv_is_32bit(&s->soc[0]);
    uint8_t satp_mode_max;

    for (cpu = s->soc[socket].num_harts - 1; cpu >= 0; cpu--) {
        RISCVCPU *cpu_ptr = &s->soc[socket].harts[cpu];
        g_autofree char *cpu_name = NULL;
        g_autofree char *core_name = NULL;
        g_autofree char *intc_name = NULL;
        g_autofree char *sv_name = NULL;

        cpu_phandle = (*phandle)++;

        cpu_name = g_strdup_printf("/cpus/cpu@%d",
            s->soc[socket].hartid_base + cpu);
        qemu_fdt_add_subnode(ms->fdt, cpu_name);

        if (cpu_ptr->cfg.satp_mode.supported != 0) {
            satp_mode_max = satp_mode_max_from_map(cpu_ptr->cfg.satp_mode.map);
            sv_name = g_strdup_printf("riscv,%s",
                                      satp_mode_str(satp_mode_max, is_32_bit));
            qemu_fdt_setprop_string(ms->fdt, cpu_name, "mmu-type", sv_name);
        }

        riscv_isa_write_fdt(cpu_ptr, ms->fdt, cpu_name);

        if (cpu_ptr->cfg.ext_zicbom) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cbom-block-size",
                                  cpu_ptr->cfg.cbom_blocksize);
        }

        if (cpu_ptr->cfg.ext_zicboz) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cboz-block-size",
                                  cpu_ptr->cfg.cboz_blocksize);
        }

        if (cpu_ptr->cfg.ext_zicbop) {
            qemu_fdt_setprop_cell(ms->fdt, cpu_name, "riscv,cbop-block-size",
                                  cpu_ptr->cfg.cbop_blocksize);
        }

        qemu_fdt_setprop_string(ms->fdt, cpu_name, "compatible", "riscv");
        qemu_fdt_setprop_string(ms->fdt, cpu_name, "status", "okay");
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "reg",
            s->soc[socket].hartid_base + cpu);
        qemu_fdt_setprop_string(ms->fdt, cpu_name, "device_type", "cpu");
        riscv_socket_fdt_write_id(ms, cpu_name, socket);
        qemu_fdt_setprop_cell(ms->fdt, cpu_name, "phandle", cpu_phandle);

        intc_phandles[cpu] = (*phandle)++;

        intc_name = g_strdup_printf("%s/interrupt-controller", cpu_name);
        qemu_fdt_add_subnode(ms->fdt, intc_name);
        qemu_fdt_setprop_cell(ms->fdt, intc_name, "phandle",
            intc_phandles[cpu]);
        qemu_fdt_setprop_string(ms->fdt, intc_name, "compatible",
            "riscv,cpu-intc");
        qemu_fdt_setprop(ms->fdt, intc_name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, intc_name, "#interrupt-cells", 1);

        core_name = g_strdup_printf("%s/core%d", clust_name, cpu);
        qemu_fdt_add_subnode(ms->fdt, core_name);
        qemu_fdt_setprop_cell(ms->fdt, core_name, "cpu", cpu_phandle);
    }
}

static void create_fdt_socket_memory(RISCVVirtState *s,
                                     const MemMapEntry *memmap, int socket)
{
    g_autofree char *mem_name = NULL;
    uint64_t addr, size;
    MachineState *ms = MACHINE(s);

    addr = memmap[VIRT_DRAM].base + riscv_socket_mem_offset(ms, socket);
    size = riscv_socket_mem_size(ms, socket);
    mem_name = g_strdup_printf("/memory@%lx", (long)addr);
    qemu_fdt_add_subnode(ms->fdt, mem_name);
    qemu_fdt_setprop_cells(ms->fdt, mem_name, "reg",
        addr >> 32, addr, size >> 32, size);
    qemu_fdt_setprop_string(ms->fdt, mem_name, "device_type", "memory");
    riscv_socket_fdt_write_id(ms, mem_name, socket);
}

static void create_fdt_socket_clint(RISCVVirtState *s,
                                    const MemMapEntry *memmap, int socket,
                                    uint32_t *intc_phandles)
{
    int cpu;
    g_autofree char *clint_name = NULL;
    g_autofree uint32_t *clint_cells = NULL;
    unsigned long clint_addr;
    MachineState *ms = MACHINE(s);
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
    qemu_fdt_add_subnode(ms->fdt, clint_name);
    qemu_fdt_setprop_string_array(ms->fdt, clint_name, "compatible",
                                  (char **)&clint_compat,
                                  ARRAY_SIZE(clint_compat));
    qemu_fdt_setprop_cells(ms->fdt, clint_name, "reg",
        0x0, clint_addr, 0x0, memmap[VIRT_CLINT].size);
    qemu_fdt_setprop(ms->fdt, clint_name, "interrupts-extended",
        clint_cells, s->soc[socket].num_harts * sizeof(uint32_t) * 4);
    riscv_socket_fdt_write_id(ms, clint_name, socket);
}

static void create_fdt_socket_aclint(RISCVVirtState *s,
                                     const MemMapEntry *memmap, int socket,
                                     uint32_t *intc_phandles)
{
    int cpu;
    char *name;
    unsigned long addr, size;
    uint32_t aclint_cells_size;
    g_autofree uint32_t *aclint_mswi_cells = NULL;
    g_autofree uint32_t *aclint_sswi_cells = NULL;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    MachineState *ms = MACHINE(s);

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
        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
            "riscv,aclint-mswi");
        qemu_fdt_setprop_cells(ms->fdt, name, "reg",
            0x0, addr, 0x0, RISCV_ACLINT_SWI_SIZE);
        qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
            aclint_mswi_cells, aclint_cells_size);
        qemu_fdt_setprop(ms->fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells", 0);
        riscv_socket_fdt_write_id(ms, name, socket);
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
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "riscv,aclint-mtimer");
    qemu_fdt_setprop_cells(ms->fdt, name, "reg",
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIME,
        0x0, size - RISCV_ACLINT_DEFAULT_MTIME,
        0x0, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
        0x0, RISCV_ACLINT_DEFAULT_MTIME);
    qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
        aclint_mtimer_cells, aclint_cells_size);
    riscv_socket_fdt_write_id(ms, name, socket);
    g_free(name);

    if (s->aia_type != VIRT_AIA_TYPE_APLIC_IMSIC) {
        addr = memmap[VIRT_ACLINT_SSWI].base +
            (memmap[VIRT_ACLINT_SSWI].size * socket);
        name = g_strdup_printf("/soc/sswi@%lx", addr);
        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
            "riscv,aclint-sswi");
        qemu_fdt_setprop_cells(ms->fdt, name, "reg",
            0x0, addr, 0x0, memmap[VIRT_ACLINT_SSWI].size);
        qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
            aclint_sswi_cells, aclint_cells_size);
        qemu_fdt_setprop(ms->fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells", 0);
        riscv_socket_fdt_write_id(ms, name, socket);
        g_free(name);
    }
}

static void create_fdt_socket_plic(RISCVVirtState *s,
                                   const MemMapEntry *memmap, int socket,
                                   uint32_t *phandle, uint32_t *intc_phandles,
                                   uint32_t *plic_phandles)
{
    int cpu;
    g_autofree char *plic_name = NULL;
    g_autofree uint32_t *plic_cells;
    unsigned long plic_addr;
    MachineState *ms = MACHINE(s);
    static const char * const plic_compat[2] = {
        "sifive,plic-1.0.0", "riscv,plic0"
    };

    plic_phandles[socket] = (*phandle)++;
    plic_addr = memmap[VIRT_PLIC].base + (memmap[VIRT_PLIC].size * socket);
    plic_name = g_strdup_printf("/soc/plic@%lx", plic_addr);
    qemu_fdt_add_subnode(ms->fdt, plic_name);
    qemu_fdt_setprop_cell(ms->fdt, plic_name,
        "#interrupt-cells", FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, plic_name,
        "#address-cells", FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_string_array(ms->fdt, plic_name, "compatible",
                                  (char **)&plic_compat,
                                  ARRAY_SIZE(plic_compat));
    qemu_fdt_setprop(ms->fdt, plic_name, "interrupt-controller", NULL, 0);

    if (kvm_enabled()) {
        plic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 2);

        for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
            plic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
        }

        qemu_fdt_setprop(ms->fdt, plic_name, "interrupts-extended",
                         plic_cells,
                         s->soc[socket].num_harts * sizeof(uint32_t) * 2);
   } else {
        plic_cells = g_new0(uint32_t, s->soc[socket].num_harts * 4);

        for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
            plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
            plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        }

        qemu_fdt_setprop(ms->fdt, plic_name, "interrupts-extended",
                         plic_cells,
                         s->soc[socket].num_harts * sizeof(uint32_t) * 4);
    }

    qemu_fdt_setprop_cells(ms->fdt, plic_name, "reg",
        0x0, plic_addr, 0x0, memmap[VIRT_PLIC].size);
    qemu_fdt_setprop_cell(ms->fdt, plic_name, "riscv,ndev",
                          VIRT_IRQCHIP_NUM_SOURCES - 1);
    riscv_socket_fdt_write_id(ms, plic_name, socket);
    qemu_fdt_setprop_cell(ms->fdt, plic_name, "phandle",
        plic_phandles[socket]);

    if (!socket) {
        platform_bus_add_all_fdt_nodes(ms->fdt, plic_name,
                                       memmap[VIRT_PLATFORM_BUS].base,
                                       memmap[VIRT_PLATFORM_BUS].size,
                                       VIRT_PLATFORM_BUS_IRQ);
    }
}

uint32_t imsic_num_bits(uint32_t count)
{
    uint32_t ret = 0;

    while (BIT(ret) < count) {
        ret++;
    }

    return ret;
}

static void create_fdt_one_imsic(RISCVVirtState *s, hwaddr base_addr,
                                 uint32_t *intc_phandles, uint32_t msi_phandle,
                                 bool m_mode, uint32_t imsic_guest_bits)
{
    int cpu, socket;
    g_autofree char *imsic_name = NULL;
    MachineState *ms = MACHINE(s);
    int socket_count = riscv_socket_count(ms);
    uint32_t imsic_max_hart_per_socket, imsic_addr, imsic_size;
    g_autofree uint32_t *imsic_cells = NULL;
    g_autofree uint32_t *imsic_regs = NULL;
    static const char * const imsic_compat[2] = {
        "qemu,imsics", "riscv,imsics"
    };

    imsic_cells = g_new0(uint32_t, ms->smp.cpus * 2);
    imsic_regs = g_new0(uint32_t, socket_count * 4);

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(m_mode ? IRQ_M_EXT : IRQ_S_EXT);
    }

    imsic_max_hart_per_socket = 0;
    for (socket = 0; socket < socket_count; socket++) {
        imsic_addr = base_addr + socket * VIRT_IMSIC_GROUP_MAX_SIZE;
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

    imsic_name = g_strdup_printf("/soc/interrupt-controller@%lx",
                                 (unsigned long)base_addr);
    qemu_fdt_add_subnode(ms->fdt, imsic_name);
    qemu_fdt_setprop_string_array(ms->fdt, imsic_name, "compatible",
                                  (char **)&imsic_compat,
                                  ARRAY_SIZE(imsic_compat));

    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "#interrupt-cells",
                          FDT_IMSIC_INT_CELLS);
    qemu_fdt_setprop(ms->fdt, imsic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(ms->fdt, imsic_name, "msi-controller", NULL, 0);
    qemu_fdt_setprop(ms->fdt, imsic_name, "interrupts-extended",
                     imsic_cells, ms->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(ms->fdt, imsic_name, "reg", imsic_regs,
                     socket_count * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,num-ids",
                     VIRT_IRQCHIP_NUM_MSIS);

    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,guest-index-bits",
                              imsic_guest_bits);
    }

    if (socket_count > 1) {
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,hart-index-bits",
                              imsic_num_bits(imsic_max_hart_per_socket));
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,group-index-bits",
                              imsic_num_bits(socket_count));
        qemu_fdt_setprop_cell(ms->fdt, imsic_name, "riscv,group-index-shift",
                              IMSIC_MMIO_GROUP_MIN_SHIFT);
    }
    qemu_fdt_setprop_cell(ms->fdt, imsic_name, "phandle", msi_phandle);
}

static void create_fdt_imsic(RISCVVirtState *s, const MemMapEntry *memmap,
                             uint32_t *phandle, uint32_t *intc_phandles,
                             uint32_t *msi_m_phandle, uint32_t *msi_s_phandle)
{
    *msi_m_phandle = (*phandle)++;
    *msi_s_phandle = (*phandle)++;

    if (!kvm_enabled()) {
        /* M-level IMSIC node */
        create_fdt_one_imsic(s, memmap[VIRT_IMSIC_M].base, intc_phandles,
                             *msi_m_phandle, true, 0);
    }

    /* S-level IMSIC node */
    create_fdt_one_imsic(s, memmap[VIRT_IMSIC_S].base, intc_phandles,
                         *msi_s_phandle, false,
                         imsic_num_bits(s->aia_guests + 1));

}

/* Caller must free string after use */
static char *fdt_get_aplic_nodename(unsigned long aplic_addr)
{
    return g_strdup_printf("/soc/interrupt-controller@%lx", aplic_addr);
}

static void create_fdt_one_aplic(RISCVVirtState *s, int socket,
                                 unsigned long aplic_addr, uint32_t aplic_size,
                                 uint32_t msi_phandle,
                                 uint32_t *intc_phandles,
                                 uint32_t aplic_phandle,
                                 uint32_t aplic_child_phandle,
                                 bool m_mode, int num_harts)
{
    int cpu;
    g_autofree char *aplic_name = fdt_get_aplic_nodename(aplic_addr);
    g_autofree uint32_t *aplic_cells = g_new0(uint32_t, num_harts * 2);
    MachineState *ms = MACHINE(s);
    static const char * const aplic_compat[2] = {
        "qemu,aplic", "riscv,aplic"
    };

    for (cpu = 0; cpu < num_harts; cpu++) {
        aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aplic_cells[cpu * 2 + 1] = cpu_to_be32(m_mode ? IRQ_M_EXT : IRQ_S_EXT);
    }

    qemu_fdt_add_subnode(ms->fdt, aplic_name);
    qemu_fdt_setprop_string_array(ms->fdt, aplic_name, "compatible",
                                  (char **)&aplic_compat,
                                  ARRAY_SIZE(aplic_compat));
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "#address-cells",
                          FDT_APLIC_ADDR_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name,
                          "#interrupt-cells", FDT_APLIC_INT_CELLS);
    qemu_fdt_setprop(ms->fdt, aplic_name, "interrupt-controller", NULL, 0);

    if (s->aia_type == VIRT_AIA_TYPE_APLIC) {
        qemu_fdt_setprop(ms->fdt, aplic_name, "interrupts-extended",
                         aplic_cells, num_harts * sizeof(uint32_t) * 2);
    } else {
        qemu_fdt_setprop_cell(ms->fdt, aplic_name, "msi-parent", msi_phandle);
    }

    qemu_fdt_setprop_cells(ms->fdt, aplic_name, "reg",
                           0x0, aplic_addr, 0x0, aplic_size);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "riscv,num-sources",
                          VIRT_IRQCHIP_NUM_SOURCES);

    if (aplic_child_phandle) {
        qemu_fdt_setprop_cell(ms->fdt, aplic_name, "riscv,children",
                              aplic_child_phandle);
        qemu_fdt_setprop_cells(ms->fdt, aplic_name, "riscv,delegation",
                               aplic_child_phandle, 0x1,
                               VIRT_IRQCHIP_NUM_SOURCES);
        /*
         * DEPRECATED_9.1: Compat property kept temporarily
         * to allow old firmwares to work with AIA. Do *not*
         * use 'riscv,delegate' in new code: use
         * 'riscv,delegation' instead.
         */
        qemu_fdt_setprop_cells(ms->fdt, aplic_name, "riscv,delegate",
                               aplic_child_phandle, 0x1,
                               VIRT_IRQCHIP_NUM_SOURCES);
    }

    riscv_socket_fdt_write_id(ms, aplic_name, socket);
    qemu_fdt_setprop_cell(ms->fdt, aplic_name, "phandle", aplic_phandle);
}

static void create_fdt_socket_aplic(RISCVVirtState *s,
                                    const MemMapEntry *memmap, int socket,
                                    uint32_t msi_m_phandle,
                                    uint32_t msi_s_phandle,
                                    uint32_t *phandle,
                                    uint32_t *intc_phandles,
                                    uint32_t *aplic_phandles,
                                    int num_harts)
{
    unsigned long aplic_addr;
    MachineState *ms = MACHINE(s);
    uint32_t aplic_m_phandle, aplic_s_phandle;

    aplic_m_phandle = (*phandle)++;
    aplic_s_phandle = (*phandle)++;

    if (!kvm_enabled()) {
        /* M-level APLIC node */
        aplic_addr = memmap[VIRT_APLIC_M].base +
                     (memmap[VIRT_APLIC_M].size * socket);
        create_fdt_one_aplic(s, socket, aplic_addr, memmap[VIRT_APLIC_M].size,
                             msi_m_phandle, intc_phandles,
                             aplic_m_phandle, aplic_s_phandle,
                             true, num_harts);
    }

    /* S-level APLIC node */
    aplic_addr = memmap[VIRT_APLIC_S].base +
                 (memmap[VIRT_APLIC_S].size * socket);
    create_fdt_one_aplic(s, socket, aplic_addr, memmap[VIRT_APLIC_S].size,
                         msi_s_phandle, intc_phandles,
                         aplic_s_phandle, 0,
                         false, num_harts);

    if (!socket) {
        g_autofree char *aplic_name = fdt_get_aplic_nodename(aplic_addr);
        platform_bus_add_all_fdt_nodes(ms->fdt, aplic_name,
                                       memmap[VIRT_PLATFORM_BUS].base,
                                       memmap[VIRT_PLATFORM_BUS].size,
                                       VIRT_PLATFORM_BUS_IRQ);
    }

    aplic_phandles[socket] = aplic_s_phandle;
}

static void create_fdt_pmu(RISCVVirtState *s)
{
    g_autofree char *pmu_name = g_strdup_printf("/pmu");
    MachineState *ms = MACHINE(s);
    RISCVCPU hart = s->soc[0].harts[0];

    qemu_fdt_add_subnode(ms->fdt, pmu_name);
    qemu_fdt_setprop_string(ms->fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(ms->fdt, hart.pmu_avail_ctrs, pmu_name);
}

static void create_fdt_sockets(RISCVVirtState *s, const MemMapEntry *memmap,
                               uint32_t *phandle,
                               uint32_t *irq_mmio_phandle,
                               uint32_t *irq_pcie_phandle,
                               uint32_t *irq_virtio_phandle,
                               uint32_t *msi_pcie_phandle)
{
    int socket, phandle_pos;
    MachineState *ms = MACHINE(s);
    uint32_t msi_m_phandle = 0, msi_s_phandle = 0;
    uint32_t xplic_phandles[MAX_NODES];
    g_autofree uint32_t *intc_phandles = NULL;
    int socket_count = riscv_socket_count(ms);

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "timebase-frequency",
                          kvm_enabled() ?
                          kvm_riscv_get_timebase_frequency(first_cpu) :
                          RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);
    qemu_fdt_add_subnode(ms->fdt, "/cpus/cpu-map");

    intc_phandles = g_new0(uint32_t, ms->smp.cpus);

    phandle_pos = ms->smp.cpus;
    for (socket = (socket_count - 1); socket >= 0; socket--) {
        g_autofree char *clust_name = NULL;
        phandle_pos -= s->soc[socket].num_harts;

        clust_name = g_strdup_printf("/cpus/cpu-map/cluster%d", socket);
        qemu_fdt_add_subnode(ms->fdt, clust_name);

        create_fdt_socket_cpus(s, socket, clust_name, phandle,
                               &intc_phandles[phandle_pos]);

        create_fdt_socket_memory(s, memmap, socket);

        if (virt_aclint_allowed() && s->have_aclint) {
            create_fdt_socket_aclint(s, memmap, socket,
                                     &intc_phandles[phandle_pos]);
        } else if (tcg_enabled()) {
            create_fdt_socket_clint(s, memmap, socket,
                                    &intc_phandles[phandle_pos]);
        }
    }

    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        create_fdt_imsic(s, memmap, phandle, intc_phandles,
            &msi_m_phandle, &msi_s_phandle);
        *msi_pcie_phandle = msi_s_phandle;
    }

    /* KVM AIA only has one APLIC instance */
    if (kvm_enabled() && virt_use_kvm_aia(s)) {
        create_fdt_socket_aplic(s, memmap, 0,
                                msi_m_phandle, msi_s_phandle, phandle,
                                &intc_phandles[0], xplic_phandles,
                                ms->smp.cpus);
    } else {
        phandle_pos = ms->smp.cpus;
        for (socket = (socket_count - 1); socket >= 0; socket--) {
            phandle_pos -= s->soc[socket].num_harts;

            if (s->aia_type == VIRT_AIA_TYPE_NONE) {
                create_fdt_socket_plic(s, memmap, socket, phandle,
                                       &intc_phandles[phandle_pos],
                                       xplic_phandles);
            } else {
                create_fdt_socket_aplic(s, memmap, socket,
                                        msi_m_phandle, msi_s_phandle, phandle,
                                        &intc_phandles[phandle_pos],
                                        xplic_phandles,
                                        s->soc[socket].num_harts);
            }
        }
    }

    if (kvm_enabled() && virt_use_kvm_aia(s)) {
        *irq_mmio_phandle = xplic_phandles[0];
        *irq_virtio_phandle = xplic_phandles[0];
        *irq_pcie_phandle = xplic_phandles[0];
    } else {
        for (socket = 0; socket < socket_count; socket++) {
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
    }

    riscv_socket_fdt_write_distance_matrix(ms);
}

static void create_fdt_virtio(RISCVVirtState *s, const MemMapEntry *memmap,
                              uint32_t irq_virtio_phandle)
{
    int i;
    MachineState *ms = MACHINE(s);

    for (i = 0; i < VIRTIO_COUNT; i++) {
        g_autofree char *name =  g_strdup_printf("/soc/virtio_mmio@%lx",
            (long)(memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size));

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible", "virtio,mmio");
        qemu_fdt_setprop_cells(ms->fdt, name, "reg",
            0x0, memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            0x0, memmap[VIRT_VIRTIO].size);
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent",
            irq_virtio_phandle);
        if (s->aia_type == VIRT_AIA_TYPE_NONE) {
            qemu_fdt_setprop_cell(ms->fdt, name, "interrupts",
                                  VIRTIO_IRQ + i);
        } else {
            qemu_fdt_setprop_cells(ms->fdt, name, "interrupts",
                                   VIRTIO_IRQ + i, 0x4);
        }
    }
}

static void create_fdt_pcie(RISCVVirtState *s, const MemMapEntry *memmap,
                            uint32_t irq_pcie_phandle,
                            uint32_t msi_pcie_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/pci@%lx",
        (long) memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_setprop_cell(ms->fdt, name, "#address-cells",
        FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells",
        FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, name, "#size-cells", 0x2);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "pci-host-ecam-generic");
    qemu_fdt_setprop_string(ms->fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cell(ms->fdt, name, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(ms->fdt, name, "bus-range", 0,
        memmap[VIRT_PCIE_ECAM].size / PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(ms->fdt, name, "dma-coherent", NULL, 0);
    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        qemu_fdt_setprop_cell(ms->fdt, name, "msi-parent", msi_pcie_phandle);
    }
    qemu_fdt_setprop_cells(ms->fdt, name, "reg", 0,
        memmap[VIRT_PCIE_ECAM].base, 0, memmap[VIRT_PCIE_ECAM].size);
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "ranges",
        1, FDT_PCI_RANGE_IOPORT, 2, 0,
        2, memmap[VIRT_PCIE_PIO].base, 2, memmap[VIRT_PCIE_PIO].size,
        1, FDT_PCI_RANGE_MMIO,
        2, memmap[VIRT_PCIE_MMIO].base,
        2, memmap[VIRT_PCIE_MMIO].base, 2, memmap[VIRT_PCIE_MMIO].size,
        1, FDT_PCI_RANGE_MMIO_64BIT,
        2, virt_high_pcie_memmap.base,
        2, virt_high_pcie_memmap.base, 2, virt_high_pcie_memmap.size);

    create_pcie_irq_map(s, ms->fdt, name, irq_pcie_phandle);
}

static void create_fdt_reset(RISCVVirtState *s, const MemMapEntry *memmap,
                             uint32_t *phandle)
{
    char *name;
    uint32_t test_phandle;
    MachineState *ms = MACHINE(s);

    test_phandle = (*phandle)++;
    name = g_strdup_printf("/soc/test@%lx",
        (long)memmap[VIRT_TEST].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    {
        static const char * const compat[3] = {
            "sifive,test1", "sifive,test0", "syscon"
        };
        qemu_fdt_setprop_string_array(ms->fdt, name, "compatible",
                                      (char **)&compat, ARRAY_SIZE(compat));
    }
    qemu_fdt_setprop_cells(ms->fdt, name, "reg",
        0x0, memmap[VIRT_TEST].base, 0x0, memmap[VIRT_TEST].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "phandle", test_phandle);
    test_phandle = qemu_fdt_get_phandle(ms->fdt, name);
    g_free(name);

    name = g_strdup_printf("/reboot");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "syscon-reboot");
    qemu_fdt_setprop_cell(ms->fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(ms->fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, name, "value", FINISHER_RESET);
    g_free(name);

    name = g_strdup_printf("/poweroff");
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "syscon-poweroff");
    qemu_fdt_setprop_cell(ms->fdt, name, "regmap", test_phandle);
    qemu_fdt_setprop_cell(ms->fdt, name, "offset", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, name, "value", FINISHER_PASS);
    g_free(name);
}

static void create_fdt_uart(RISCVVirtState *s, const MemMapEntry *memmap,
                            uint32_t irq_mmio_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/serial@%lx", (long)memmap[VIRT_UART0].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(ms->fdt, name, "reg",
        0x0, memmap[VIRT_UART0].base,
        0x0, memmap[VIRT_UART0].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent", irq_mmio_phandle);
    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", UART0_IRQ);
    } else {
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", UART0_IRQ, 0x4);
    }

    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", name);
}

static void create_fdt_rtc(RISCVVirtState *s, const MemMapEntry *memmap,
                           uint32_t irq_mmio_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/rtc@%lx", (long)memmap[VIRT_RTC].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "google,goldfish-rtc");
    qemu_fdt_setprop_cells(ms->fdt, name, "reg",
        0x0, memmap[VIRT_RTC].base, 0x0, memmap[VIRT_RTC].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent",
        irq_mmio_phandle);
    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", RTC_IRQ);
    } else {
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", RTC_IRQ, 0x4);
    }
}

static void create_fdt_flash(RISCVVirtState *s, const MemMapEntry *memmap)
{
    MachineState *ms = MACHINE(s);
    hwaddr flashsize = virt_memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = virt_memmap[VIRT_FLASH].base;
    g_autofree char *name = g_strdup_printf("/flash@%" PRIx64, flashbase);

    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "cfi-flash");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 2, flashbase, 2, flashsize,
                                 2, flashbase + flashsize, 2, flashsize);
    qemu_fdt_setprop_cell(ms->fdt, name, "bank-width", 4);
}

static void create_fdt_fw_cfg(RISCVVirtState *s, const MemMapEntry *memmap)
{
    MachineState *ms = MACHINE(s);
    hwaddr base = memmap[VIRT_FW_CFG].base;
    hwaddr size = memmap[VIRT_FW_CFG].size;
    g_autofree char *nodename = g_strdup_printf("/fw-cfg@%" PRIx64, base);

    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename,
                            "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
}

static void create_fdt_virtio_iommu(RISCVVirtState *s, uint16_t bdf)
{
    const char compat[] = "virtio,pci-iommu\0pci1af4,1057";
    void *fdt = MACHINE(s)->fdt;
    uint32_t iommu_phandle;
    g_autofree char *iommu_node = NULL;
    g_autofree char *pci_node = NULL;

    pci_node = g_strdup_printf("/soc/pci@%lx",
                               (long) virt_memmap[VIRT_PCIE_ECAM].base);
    iommu_node = g_strdup_printf("%s/virtio_iommu@%x,%x", pci_node,
                                 PCI_SLOT(bdf), PCI_FUNC(bdf));
    iommu_phandle = qemu_fdt_alloc_phandle(fdt);

    qemu_fdt_add_subnode(fdt, iommu_node);

    qemu_fdt_setprop(fdt, iommu_node, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(fdt, iommu_node, "reg",
                                 1, bdf << 8, 1, 0, 1, 0,
                                 1, 0, 1, 0);
    qemu_fdt_setprop_cell(fdt, iommu_node, "#iommu-cells", 1);
    qemu_fdt_setprop_cell(fdt, iommu_node, "phandle", iommu_phandle);

    qemu_fdt_setprop_cells(fdt, pci_node, "iommu-map",
                           0, iommu_phandle, 0, bdf,
                           bdf + 1, iommu_phandle, bdf + 1, 0xffff - bdf);
}

static void finalize_fdt(RISCVVirtState *s)
{
    uint32_t phandle = 1, irq_mmio_phandle = 1, msi_pcie_phandle = 1;
    uint32_t irq_pcie_phandle = 1, irq_virtio_phandle = 1;

    create_fdt_sockets(s, virt_memmap, &phandle, &irq_mmio_phandle,
                       &irq_pcie_phandle, &irq_virtio_phandle,
                       &msi_pcie_phandle);

    create_fdt_virtio(s, virt_memmap, irq_virtio_phandle);

    create_fdt_pcie(s, virt_memmap, irq_pcie_phandle, msi_pcie_phandle);

    create_fdt_reset(s, virt_memmap, &phandle);

    create_fdt_uart(s, virt_memmap, irq_mmio_phandle);

    create_fdt_rtc(s, virt_memmap, irq_mmio_phandle);
}

static void create_fdt(RISCVVirtState *s, const MemMapEntry *memmap)
{
    MachineState *ms = MACHINE(s);
    uint8_t rng_seed[32];
    g_autofree char *name = NULL;

    ms->fdt = create_device_tree(&s->fdt_size);
    if (!ms->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(ms->fdt, "/", "model", "riscv-virtio,qemu");
    qemu_fdt_setprop_string(ms->fdt, "/", "compatible", "riscv-virtio");
    qemu_fdt_setprop_cell(ms->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(ms->fdt, "/soc");
    qemu_fdt_setprop(ms->fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#address-cells", 0x2);

    /*
     * The "/soc/pci@..." node is needed for PCIE hotplugs
     * that might happen before finalize_fdt().
     */
    name = g_strdup_printf("/soc/pci@%lx", (long) memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_add_subnode(ms->fdt, name);

    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    /* Pass seed to RNG */
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(ms->fdt, "/chosen", "rng-seed",
                     rng_seed, sizeof(rng_seed));

    create_fdt_flash(s, memmap);
    create_fdt_fw_cfg(s, memmap);
    create_fdt_pmu(s);
}

static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          DeviceState *irqchip,
                                          RISCVVirtState *s)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *high_mmio_alias, *mmio_reg;
    hwaddr ecam_base = s->memmap[VIRT_PCIE_ECAM].base;
    hwaddr ecam_size = s->memmap[VIRT_PCIE_ECAM].size;
    hwaddr mmio_base = s->memmap[VIRT_PCIE_MMIO].base;
    hwaddr mmio_size = s->memmap[VIRT_PCIE_MMIO].size;
    hwaddr high_mmio_base = virt_high_pcie_memmap.base;
    hwaddr high_mmio_size = virt_high_pcie_memmap.size;
    hwaddr pio_base = s->memmap[VIRT_PCIE_PIO].base;
    hwaddr pio_size = s->memmap[VIRT_PCIE_PIO].size;
    qemu_irq irq;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);

    /* Set GPEX object properties for the virt machine */
    object_property_set_uint(OBJECT(GPEX_HOST(dev)), PCI_HOST_ECAM_BASE,
                            ecam_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_ECAM_SIZE,
                            ecam_size, NULL);
    object_property_set_uint(OBJECT(GPEX_HOST(dev)),
                             PCI_HOST_BELOW_4G_MMIO_BASE,
                             mmio_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_BELOW_4G_MMIO_SIZE,
                            mmio_size, NULL);
    object_property_set_uint(OBJECT(GPEX_HOST(dev)),
                             PCI_HOST_ABOVE_4G_MMIO_BASE,
                             high_mmio_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_ABOVE_4G_MMIO_SIZE,
                            high_mmio_size, NULL);
    object_property_set_uint(OBJECT(GPEX_HOST(dev)), PCI_HOST_PIO_BASE,
                            pio_base, NULL);
    object_property_set_int(OBJECT(GPEX_HOST(dev)), PCI_HOST_PIO_SIZE,
                            pio_size, NULL);

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

    GPEX_HOST(dev)->gpex_cfg.bus = PCI_HOST_BRIDGE(GPEX_HOST(dev))->bus;
    return dev;
}

static FWCfgState *create_fw_cfg(const MachineState *ms)
{
    hwaddr base = virt_memmap[VIRT_FW_CFG].base;
    FWCfgState *fw_cfg;

    fw_cfg = fw_cfg_init_mem_wide(base + 8, base, 8, base + 16,
                                  &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)ms->smp.cpus);

    return fw_cfg;
}

static DeviceState *virt_create_plic(const MemMapEntry *memmap, int socket,
                                     int base_hartid, int hart_count)
{
    DeviceState *ret;
    g_autofree char *plic_hart_config = NULL;

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

    return ret;
}

static DeviceState *virt_create_aia(RISCVVirtAIAType aia_type, int aia_guests,
                                    const MemMapEntry *memmap, int socket,
                                    int base_hartid, int hart_count)
{
    int i;
    hwaddr addr;
    uint32_t guest_bits;
    DeviceState *aplic_s = NULL;
    DeviceState *aplic_m = NULL;
    bool msimode = aia_type == VIRT_AIA_TYPE_APLIC_IMSIC;

    if (msimode) {
        if (!kvm_enabled()) {
            /* Per-socket M-level IMSICs */
            addr = memmap[VIRT_IMSIC_M].base +
                   socket * VIRT_IMSIC_GROUP_MAX_SIZE;
            for (i = 0; i < hart_count; i++) {
                riscv_imsic_create(addr + i * IMSIC_HART_SIZE(0),
                                   base_hartid + i, true, 1,
                                   VIRT_IRQCHIP_NUM_MSIS);
            }
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

    if (!kvm_enabled()) {
        /* Per-socket M-level APLIC */
        aplic_m = riscv_aplic_create(memmap[VIRT_APLIC_M].base +
                                     socket * memmap[VIRT_APLIC_M].size,
                                     memmap[VIRT_APLIC_M].size,
                                     (msimode) ? 0 : base_hartid,
                                     (msimode) ? 0 : hart_count,
                                     VIRT_IRQCHIP_NUM_SOURCES,
                                     VIRT_IRQCHIP_NUM_PRIO_BITS,
                                     msimode, true, NULL);
    }

    /* Per-socket S-level APLIC */
    aplic_s = riscv_aplic_create(memmap[VIRT_APLIC_S].base +
                                 socket * memmap[VIRT_APLIC_S].size,
                                 memmap[VIRT_APLIC_S].size,
                                 (msimode) ? 0 : base_hartid,
                                 (msimode) ? 0 : hart_count,
                                 VIRT_IRQCHIP_NUM_SOURCES,
                                 VIRT_IRQCHIP_NUM_PRIO_BITS,
                                 msimode, false, aplic_m);

    return kvm_enabled() ? aplic_s : aplic_m;
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

static void virt_build_smbios(RISCVVirtState *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);
    MachineState *ms = MACHINE(s);
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    struct smbios_phys_mem_area mem_array;
    const char *product = "QEMU Virtual Machine";

    if (kvm_enabled()) {
        product = "KVM Virtual Machine";
    }

    smbios_set_defaults("QEMU", product, mc->name);

    if (riscv_is_32bit(&s->soc[0])) {
        smbios_set_default_processor_family(0x200);
    } else {
        smbios_set_default_processor_family(0x201);
    }

    /* build the array of physical mem area from base_memmap */
    mem_array.address = s->memmap[VIRT_DRAM].base;
    mem_array.length = ms->ram_size;

    smbios_get_tables(ms, SMBIOS_ENTRY_POINT_TYPE_64,
                      &mem_array, 1,
                      &smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len,
                      &error_fatal);

    if (smbios_anchor) {
        fw_cfg_add_file(s->fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(s->fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }
}

static void virt_machine_done(Notifier *notifier, void *data)
{
    RISCVVirtState *s = container_of(notifier, RISCVVirtState,
                                     machine_done);
    const MemMapEntry *memmap = virt_memmap;
    MachineState *machine = MACHINE(s);
    hwaddr start_addr = memmap[VIRT_DRAM].base;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->soc[0]);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry = 0;
    BlockBackend *pflash_blk0;

    /*
     * An user provided dtb must include everything, including
     * dynamic sysbus devices. Our FDT needs to be finalized.
     */
    if (machine->dtb == NULL) {
        finalize_fdt(s);
    }

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

    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &start_addr, NULL);

    pflash_blk0 = pflash_cfi01_get_blk(s->flash[0]);
    if (pflash_blk0) {
        if (machine->firmware && !strcmp(machine->firmware, "none") &&
            !kvm_enabled()) {
            /*
             * Pflash was supplied but bios is none and not KVM guest,
             * let's overwrite the address we jump to after reset to
             * the base of the flash.
             */
            start_addr = virt_memmap[VIRT_FLASH].base;
        } else {
            /*
             * Pflash was supplied but either KVM guest or bios is not none.
             * In this case, base of the flash would contain S-mode payload.
             */
            riscv_setup_firmware_boot(machine);
            kernel_entry = virt_memmap[VIRT_FLASH].base;
        }
    }

    if (machine->kernel_filename && !kernel_entry) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&s->soc[0],
                                                         firmware_end_addr);

        kernel_entry = riscv_load_kernel(machine, &s->soc[0],
                                         kernel_start_addr, true, NULL);
    }

    fdt_load_addr = riscv_compute_fdt_addr(memmap[VIRT_DRAM].base,
                                           memmap[VIRT_DRAM].size,
                                           machine);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

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

    virt_build_smbios(s);

    if (virt_is_acpi_enabled(s)) {
        virt_acpi_setup(s);
    }
}

static void virt_machine_init(MachineState *machine)
{
    const MemMapEntry *memmap = virt_memmap;
    RISCVVirtState *s = RISCV_VIRT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    DeviceState *mmio_irqchip, *virtio_irqchip, *pcie_irqchip;
    int i, base_hartid, hart_count;
    int socket_count = riscv_socket_count(machine);

    /* Check socket count limit */
    if (VIRT_SOCKETS_MAX < socket_count) {
        error_report("number of sockets/nodes should be less than %d",
            VIRT_SOCKETS_MAX);
        exit(1);
    }

    if (!virt_aclint_allowed() && s->have_aclint) {
        error_report("'aclint' is only available with TCG acceleration");
        exit(1);
    }

    /* Initialize sockets */
    mmio_irqchip = virtio_irqchip = pcie_irqchip = NULL;
    for (i = 0; i < socket_count; i++) {
        g_autofree char *soc_name = g_strdup_printf("soc%d", i);

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

        object_initialize_child(OBJECT(machine), soc_name, &s->soc[i],
                                TYPE_RISCV_HART_ARRAY);
        object_property_set_str(OBJECT(&s->soc[i]), "cpu-type",
                                machine->cpu_type, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "hartid-base",
                                base_hartid, &error_abort);
        object_property_set_int(OBJECT(&s->soc[i]), "num-harts",
                                hart_count, &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->soc[i]), &error_fatal);

        if (virt_aclint_allowed() && s->have_aclint) {
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
        } else if (tcg_enabled()) {
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

    if (kvm_enabled() && virt_use_kvm_aia(s)) {
        kvm_riscv_aia_create(machine, IMSIC_MMIO_GROUP_MIN_SHIFT,
                             VIRT_IRQCHIP_NUM_SOURCES, VIRT_IRQCHIP_NUM_MSIS,
                             memmap[VIRT_APLIC_S].base,
                             memmap[VIRT_IMSIC_S].base,
                             s->aia_guests);
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

    s->memmap = virt_memmap;

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, memmap[VIRT_DRAM].base,
        machine->ram);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           memmap[VIRT_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[VIRT_MROM].base,
                                mask_rom);

    /*
     * Init fw_cfg. Must be done before riscv_load_fdt, otherwise the
     * device tree cannot be altered and we get FDT_ERR_NOSPACE.
     */
    s->fw_cfg = create_fw_cfg(machine);
    rom_set_fw(s->fw_cfg);

    /* SiFive Test MMIO device */
    sifive_test_create(memmap[VIRT_TEST].base);

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            memmap[VIRT_VIRTIO].base + i * memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(virtio_irqchip, VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory, pcie_irqchip, s);

    create_platform_bus(s, mmio_irqchip);

    serial_mm_init(system_memory, memmap[VIRT_UART0].base,
        0, qdev_get_gpio_in(mmio_irqchip, UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    sysbus_create_simple("goldfish_rtc", memmap[VIRT_RTC].base,
        qdev_get_gpio_in(mmio_irqchip, RTC_IRQ));

    for (i = 0; i < ARRAY_SIZE(s->flash); i++) {
        /* Map legacy -drive if=pflash to machine properties */
        pflash_cfi01_legacy_drive(s->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }
    virt_flash_map(s, system_memory);

    /* load/create device tree */
    if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    } else {
        create_fdt(s, memmap);
    }

    s->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void virt_machine_instance_init(Object *obj)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    virt_flash_create(s);

    s->oem_id = g_strndup(ACPI_BUILD_APPNAME6, 6);
    s->oem_table_id = g_strndup(ACPI_BUILD_APPNAME8, 8);
    s->acpi = ON_OFF_AUTO_AUTO;
}

static char *virt_get_aia_guests(Object *obj, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    return g_strdup_printf("%d", s->aia_guests);
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
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    return s->have_aclint;
}

static void virt_set_aclint(Object *obj, bool value, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    s->have_aclint = value;
}

bool virt_is_acpi_enabled(RISCVVirtState *s)
{
    return s->acpi != ON_OFF_AUTO_OFF;
}

static void virt_get_acpi(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    OnOffAuto acpi = s->acpi;

    visit_type_OnOffAuto(v, name, &acpi, errp);
}

static void virt_set_acpi(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->acpi, errp);
}

static HotplugHandler *virt_machine_get_hotplug_handler(MachineState *machine,
                                                        DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI)) {
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

    if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI)) {
        create_fdt_virtio_iommu(s, pci_get_bdf(PCI_DEVICE(dev)));
    }
}

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->desc = "RISC-V VirtIO board";
    mc->init = virt_machine_init;
    mc->max_cpus = VIRT_CPUS_MAX;
    mc->default_cpu_type = TYPE_RISCV_CPU_BASE;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->pci_allow_0_address = true;
    mc->possible_cpu_arch_ids = riscv_numa_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = riscv_numa_cpu_index_to_props;
    mc->get_default_cpu_node_id = riscv_numa_get_default_cpu_node_id;
    mc->numa_mem_supported = true;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
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
                                          "(TCG only) Set on/off to "
                                          "enable/disable emulating "
                                          "ACLINT devices");

    object_class_property_add_str(oc, "aia", virt_get_aia,
                                  virt_set_aia);
    object_class_property_set_description(oc, "aia",
                                          "Set type of AIA interrupt "
                                          "controller. Valid values are "
                                          "none, aplic, and aplic-imsic.");

    object_class_property_add_str(oc, "aia-guests",
                                  virt_get_aia_guests,
                                  virt_set_aia_guests);
    {
        g_autofree char *str =
            g_strdup_printf("Set number of guest MMIO pages for AIA IMSIC. "
                            "Valid value should be between 0 and %d.",
                            VIRT_IRQCHIP_MAX_GUESTS);
        object_class_property_set_description(oc, "aia-guests", str);
    }

    object_class_property_add(oc, "acpi", "OnOffAuto",
                              virt_get_acpi, virt_set_acpi,
                              NULL, NULL);
    object_class_property_set_description(oc, "acpi",
                                          "Enable ACPI");
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
