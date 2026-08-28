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
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/char/serial-mm.h"
#include "target/riscv/cpu.h"
#include "hw/core/sysbus-fdt.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/iommu.h"
#include "hw/riscv/riscv-iommu-bits.h"
#include "hw/riscv/virt.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/fdt-common.h"
#include "hw/riscv/numa.h"
#include "kvm/kvm_riscv.h"
#include "hw/firmware/smbios.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/riscv_aplic.h"
#include "hw/intc/sifive_plic.h"
#include "hw/misc/sifive_test.h"
#include "hw/core/platform-bus.h"
#include "chardev/char.h"
#include "system/device_tree.h"
#include "system/system.h"
#include "system/tcg.h"
#include "system/kvm.h"
#include "system/tpm.h"
#include "system/qtest.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"
#include "hw/acpi/aml-build.h"
#include "qapi/qapi-visit-common.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/uefi/var-service-api.h"

#include "aia.h"

/* KVM AIA only supports APLIC MSI. APLIC Wired is always emulated by QEMU. */
static bool virt_use_kvm_aia_aplic_imsic(RISCVVirtAIAType aia_type)
{
    bool msimode = aia_type == VIRT_AIA_TYPE_APLIC_IMSIC;

    return riscv_is_kvm_aia_aplic_imsic(msimode);
}

static bool virt_use_emulated_aplic(RISCVVirtAIAType aia_type)
{
    bool msimode = aia_type == VIRT_AIA_TYPE_APLIC_IMSIC;

    return riscv_use_emulated_aplic(msimode);
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
    [VIRT_IOMMU_SYS] =    {  0x3010000,        0x1000 },
    [VIRT_PLATFORM_BUS] = {  0x4000000,     0x2000000 },
    [VIRT_PLIC] =         {  0xc000000, VIRT_PLIC_SIZE(VIRT_CPUS_MAX * 2) },
    [VIRT_APLIC_M] =      {  0xc000000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_APLIC_S] =      {  0xd000000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_UART0] =        { 0x10000000,         0x100 },
    [VIRT_VIRTIO] =       { 0x10001000,        0x1000 },
    /* UART1 supports page isolation from UART0 */
    [VIRT_UART1] =        { 0x1000a000,         0x100 },
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
    hwaddr flashsize = s->memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = s->memmap[VIRT_FLASH].base;

    virt_flash_map1(s->flash[0], flashbase, flashsize,
                    sysmem);
    virt_flash_map1(s->flash[1], flashbase + flashsize, flashsize,
                    sysmem);
}

static void create_fdt_socket_aclint(RISCVVirtState *s,
                                     int socket,
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
        addr = s->memmap[VIRT_CLINT].base +
               (s->memmap[VIRT_CLINT].size * socket);
        name = g_strdup_printf("/soc/mswi@%lx", addr);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
            "riscv,aclint-mswi");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
            2, addr, 2, RISCV_ACLINT_SWI_SIZE);
        qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
            aclint_mswi_cells, aclint_cells_size);
        qemu_fdt_setprop(ms->fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells", 0);
        riscv_socket_fdt_write_id(ms, name, socket);
        g_free(name);
    }

    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        addr = s->memmap[VIRT_CLINT].base +
               (RISCV_ACLINT_DEFAULT_MTIMER_SIZE * socket);
        size = RISCV_ACLINT_DEFAULT_MTIMER_SIZE;
    } else {
        addr = s->memmap[VIRT_CLINT].base + RISCV_ACLINT_SWI_SIZE +
               (s->memmap[VIRT_CLINT].size * socket);
        size = s->memmap[VIRT_CLINT].size - RISCV_ACLINT_SWI_SIZE;
    }
    name = g_strdup_printf("/soc/mtimer@%lx", addr);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "riscv,aclint-mtimer");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
        2, addr + RISCV_ACLINT_DEFAULT_MTIME,
        2, size - RISCV_ACLINT_DEFAULT_MTIME,
        2, addr + RISCV_ACLINT_DEFAULT_MTIMECMP,
        2, RISCV_ACLINT_DEFAULT_MTIME);
    qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
        aclint_mtimer_cells, aclint_cells_size);
    riscv_socket_fdt_write_id(ms, name, socket);
    g_free(name);

    if (s->aia_type != VIRT_AIA_TYPE_APLIC_IMSIC) {
        addr = s->memmap[VIRT_ACLINT_SSWI].base +
               (s->memmap[VIRT_ACLINT_SSWI].size * socket);

        name = g_strdup_printf("/soc/sswi@%lx", addr);
        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible",
            "riscv,aclint-sswi");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
            2, addr, 2, s->memmap[VIRT_ACLINT_SSWI].size);
        qemu_fdt_setprop(ms->fdt, name, "interrupts-extended",
            aclint_sswi_cells, aclint_cells_size);
        qemu_fdt_setprop(ms->fdt, name, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells", 0);
        riscv_socket_fdt_write_id(ms, name, socket);
        g_free(name);
    }
}

static void create_fdt_socket_plic(RISCVVirtState *s,
                                   int socket,
                                   uint32_t *phandle, uint32_t *intc_phandles,
                                   uint32_t *plic_phandles)
{
    int cpu;
    g_autofree char *plic_name = NULL;
    g_autofree uint32_t *plic_cells;
    MachineState *ms = MACHINE(s);
    unsigned long plic_addr = s->memmap[VIRT_PLIC].base +
                              (s->memmap[VIRT_PLIC].size * socket);
    bool numa_enabled = riscv_numa_enabled(MACHINE(s));
    uint32_t cells_length;

    plic_name = g_strdup_printf("/soc/interrupt-controller@%lx", plic_addr);

    if (kvm_enabled()) {
        cells_length = s->soc[socket].num_harts * 2;
        plic_cells = g_new0(uint32_t, cells_length);

        for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
            plic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_S_EXT);
        }
   } else {
        cells_length = s->soc[socket].num_harts * 4;
        plic_cells = g_new0(uint32_t, cells_length);

        for (cpu = 0; cpu < s->soc[socket].num_harts; cpu++) {
            plic_cells[cpu * 4 + 0] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
            plic_cells[cpu * 4 + 2] = cpu_to_be32(intc_phandles[cpu]);
            plic_cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        }
    }

    plic_phandles[socket] = (*phandle)++;

    riscv_create_fdt_plic(ms->fdt, plic_addr, s->memmap[VIRT_PLIC].size,
                          plic_phandles[socket], FDT_PLIC_INT_CELLS,
                          FDT_PLIC_ADDR_CELLS, plic_cells,
                          cells_length * sizeof(uint32_t),
                          VIRT_IRQCHIP_NUM_SOURCES - 1,
                          numa_enabled, socket);

    if (!socket) {
        platform_bus_add_all_fdt_nodes(ms->fdt, plic_name,
                                       s->memmap[VIRT_PLATFORM_BUS].base,
                                       s->memmap[VIRT_PLATFORM_BUS].size,
                                       VIRT_PLATFORM_BUS_IRQ);
    }
}

static void create_fdt_pmu(RISCVVirtState *s)
{
    g_autofree char *pmu_name = g_strdup_printf("/pmu");
    MachineState *ms = MACHINE(s);
    RISCVCPU *hart = &s->soc[0].harts[0];

    qemu_fdt_add_subnode(ms->fdt, pmu_name);
    qemu_fdt_setprop_string(ms->fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(ms->fdt, hart->pmu_avail_ctrs, pmu_name);
}

static void create_fdt_sockets(RISCVVirtState *s,
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
    bool numa_enabled = riscv_numa_enabled(ms);
    bool is_32_bit = riscv_is_32bit(&s->soc[0]);
    APLICFdtProps aplic_props;

    riscv_fdt_create_cpu_socket_subnode(ms->fdt,
        kvm_enabled() ? kvm_riscv_get_timebase_frequency(&s->soc->harts[0]) :
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ);

    intc_phandles = g_new0(uint32_t, ms->smp.cpus);

    phandle_pos = ms->smp.cpus;
    for (socket = (socket_count - 1); socket >= 0; socket--) {
        hwaddr memaddr = s->memmap[VIRT_DRAM].base +
                         riscv_socket_mem_offset(ms, socket);
        uint64_t memsize = riscv_socket_mem_size(ms, socket);

        phandle_pos -= s->soc[socket].num_harts;

        riscv_create_fdt_socket_cpus(ms->fdt, (&s->soc[socket])->harts, socket,
                                     s->soc[socket].num_harts,
                                     s->soc[socket].hartid_base,
                                     phandle, &intc_phandles[phandle_pos],
                                     numa_enabled, is_32_bit);

        riscv_create_fdt_socket_memory(ms->fdt, memaddr, memsize,
                                       socket, riscv_numa_enabled(ms));

        if (virt_aclint_allowed() && s->have_aclint) {
            create_fdt_socket_aclint(s, socket,
                                     &intc_phandles[phandle_pos]);
        } else if (tcg_enabled()) {
            hwaddr clintaddr = s->memmap[VIRT_CLINT].base +
                               s->memmap[VIRT_CLINT].size * socket;

            riscv_create_fdt_socket_clint(ms->fdt, clintaddr,
                                          s->memmap[VIRT_CLINT].size,
                                          socket, &intc_phandles[phandle_pos],
                                          s->soc[socket].num_harts,
                                          numa_enabled);
        }
    }

    if (s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC) {
        IMSICFdtProps props = {
            .soc = &s->soc,
            .socket_count = riscv_socket_count(ms),
            .smp_cpus = ms->smp.cpus,
            .imsic_m_base = !kvm_enabled() ? s->memmap[VIRT_IMSIC_M].base : 0,
            .imsic_s_base = s->memmap[VIRT_IMSIC_S].base,
            .imsic_group_max_size = VIRT_IMSIC_GROUP_MAX_SIZE,
            .irqchip_num_msis = VIRT_IRQCHIP_NUM_MSIS,
            .aia_guests = s->aia_guests
        };

        riscv_create_fdt_imsic(ms->fdt, &props, phandle, intc_phandles,
                               &msi_m_phandle, &msi_s_phandle);

        *msi_pcie_phandle = msi_s_phandle;
    }

    if (s->aia_type != VIRT_AIA_TYPE_NONE) {
        aplic_props.aplic_m = !kvm_enabled() ? &s->memmap[VIRT_APLIC_M] : NULL;
        aplic_props.aplic_s = &s->memmap[VIRT_APLIC_S];
        aplic_props.platform_bus = &s->memmap[VIRT_PLATFORM_BUS];
        aplic_props.platform_bus_irq = VIRT_PLATFORM_BUS_IRQ;
        aplic_props.socket = 0;
        aplic_props.num_harts = ms->smp.cpus;
        aplic_props.numa_enabled = numa_enabled;
        aplic_props.irqchip_num_sources = VIRT_IRQCHIP_NUM_SOURCES;
        aplic_props.aia_type = s->aia_type;
    }

    /*
     * With KVM AIA aplic-imsic, using an irqchip without split
     * mode, we'll use only one APLIC instance.
     */
    if (!virt_use_emulated_aplic(s->aia_type)) {
        riscv_create_fdt_socket_aplic(ms->fdt, &aplic_props,
                                      msi_m_phandle, msi_s_phandle, phandle,
                                      &intc_phandles[0], xplic_phandles);

        *irq_mmio_phandle = xplic_phandles[0];
        *irq_virtio_phandle = xplic_phandles[0];
        *irq_pcie_phandle = xplic_phandles[0];
    } else {
        phandle_pos = ms->smp.cpus;
        for (socket = (socket_count - 1); socket >= 0; socket--) {
            phandle_pos -= s->soc[socket].num_harts;

            if (s->aia_type == VIRT_AIA_TYPE_NONE) {
                create_fdt_socket_plic(s, socket, phandle,
                                       &intc_phandles[phandle_pos],
                                       xplic_phandles);
            } else {
                aplic_props.socket = socket;
                aplic_props.num_harts = s->soc[socket].num_harts;
                riscv_create_fdt_socket_aplic(ms->fdt, &aplic_props,
                                              msi_m_phandle, msi_s_phandle,
                                              phandle,
                                              &intc_phandles[phandle_pos],
                                              xplic_phandles);
            }
        }

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

static void create_fdt_virtio(RISCVVirtState *s, uint32_t irq_virtio_phandle)
{
    int i;
    MachineState *ms = MACHINE(s);
    hwaddr virtio_base = s->memmap[VIRT_VIRTIO].base;

    for (i = 0; i < VIRTIO_COUNT; i++) {
        g_autofree char *name = NULL;
        uint64_t size = s->memmap[VIRT_VIRTIO].size;
        hwaddr addr = virtio_base + i * size;

        name = g_strdup_printf("/soc/virtio_mmio@%"HWADDR_PRIx, addr);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible", "virtio,mmio");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, addr, 2, size);
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

static void create_fdt_uart(RISCVVirtState *s,
                            uint32_t irq_mmio_phandle, int memId, int irqNo)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/serial@%"HWADDR_PRIx,
                           s->memmap[memId].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 2, s->memmap[memId].base,
                                 2, s->memmap[memId].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent", irq_mmio_phandle);
    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", irqNo);
    } else {
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", irqNo, 0x4);
    }

    if (VIRT_UART0 == memId) {
        qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", name);
        qemu_fdt_setprop_string(ms->fdt, "/aliases", "serial0", name);
    }
}

static void create_fdt_uarts(RISCVVirtState *s, uint32_t irq_mmio_phandle)
{
    if (s->uart1_present) {
        create_fdt_uart(s, irq_mmio_phandle, VIRT_UART1, UART1_IRQ);
    }
    create_fdt_uart(s, irq_mmio_phandle, VIRT_UART0, UART0_IRQ);
}

static void create_fdt_rtc(RISCVVirtState *s,
                           uint32_t irq_mmio_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/rtc@%"HWADDR_PRIx,
                           s->memmap[VIRT_RTC].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "google,goldfish-rtc");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 2, s->memmap[VIRT_RTC].base,
                                 2, s->memmap[VIRT_RTC].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent",
        irq_mmio_phandle);
    if (s->aia_type == VIRT_AIA_TYPE_NONE) {
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", RTC_IRQ);
    } else {
        qemu_fdt_setprop_cells(ms->fdt, name, "interrupts", RTC_IRQ, 0x4);
    }
}

static void create_fdt_fw_cfg(RISCVVirtState *s)
{
    MachineState *ms = MACHINE(s);
    hwaddr base = s->memmap[VIRT_FW_CFG].base;
    hwaddr size = s->memmap[VIRT_FW_CFG].size;
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

    pci_node = g_strdup_printf("/soc/pci@%"HWADDR_PRIx,
                               s->memmap[VIRT_PCIE_ECAM].base);
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

static void create_fdt_iommu(RISCVVirtState *s, uint16_t bdf)
{
    const char comp[] = "riscv,pci-iommu";
    void *fdt = MACHINE(s)->fdt;
    uint32_t iommu_phandle;
    g_autofree char *iommu_node = NULL;
    g_autofree char *pci_node = NULL;

    pci_node = g_strdup_printf("/soc/pci@%"HWADDR_PRIx,
                               s->memmap[VIRT_PCIE_ECAM].base);
    iommu_node = g_strdup_printf("%s/iommu@%x", pci_node, bdf);
    iommu_phandle = qemu_fdt_alloc_phandle(fdt);
    qemu_fdt_add_subnode(fdt, iommu_node);

    qemu_fdt_setprop(fdt, iommu_node, "compatible", comp, sizeof(comp));
    qemu_fdt_setprop_cell(fdt, iommu_node, "#iommu-cells", 1);
    qemu_fdt_setprop_cell(fdt, iommu_node, "phandle", iommu_phandle);
    qemu_fdt_setprop_cells(fdt, iommu_node, "reg",
                           bdf << 8, 0, 0, 0, 0);
    qemu_fdt_setprop_cells(fdt, pci_node, "iommu-map",
                           0, iommu_phandle, 0, bdf,
                           bdf + 1, iommu_phandle, bdf + 1, 0xffff - bdf);
    s->pci_iommu_bdf = bdf;
}

static void finalize_fdt(RISCVVirtState *s)
{
    uint32_t phandle = 1, irq_mmio_phandle = 1, msi_pcie_phandle = 1;
    uint32_t irq_pcie_phandle = 1, irq_virtio_phandle = 1;
    uint32_t iommu_sys_phandle = 1;

    create_fdt_sockets(s, &phandle, &irq_mmio_phandle,
                       &irq_pcie_phandle, &irq_virtio_phandle,
                       &msi_pcie_phandle);

    create_fdt_virtio(s, irq_virtio_phandle);

    if (virt_is_iommu_sys_enabled(s)) {
        iommu_sys_phandle =
            riscv_create_fdt_riscv_iommu_sys(MACHINE(s)->fdt,
                                             s->memmap[VIRT_IOMMU_SYS].base,
                                             s->memmap[VIRT_IOMMU_SYS].size,
                                             &phandle,
                                             irq_mmio_phandle,
                                             msi_pcie_phandle,
                                             IOMMU_SYS_IRQ);
    }

    riscv_create_fdt_pcie(MACHINE(s)->fdt, s->aia_type,
                          virt_is_iommu_sys_enabled(s),
                          &s->memmap[VIRT_PCIE_ECAM],
                          &s->memmap[VIRT_PCIE_PIO],
                          &s->memmap[VIRT_PCIE_MMIO],
                          &virt_high_pcie_memmap,
                          irq_pcie_phandle, msi_pcie_phandle,
                          iommu_sys_phandle, PCIE_IRQ);

    riscv_create_fdt_syscon(MACHINE(s)->fdt, &phandle,
                            s->memmap[VIRT_TEST].base,
                            s->memmap[VIRT_TEST].size,
                            FINISHER_RESET, FINISHER_PASS, true);

    create_fdt_uarts(s, irq_mmio_phandle);

    create_fdt_rtc(s, irq_mmio_phandle);
}

static void create_fdt(RISCVVirtState *s)
{
    MachineState *ms = MACHINE(s);
    uint8_t rng_seed[32];
    g_autofree char *name = NULL;

    ms->fdt = riscv_create_board_device_tree("riscv-virtio,qemu",
                                             "riscv-virtio",
                                             &s->fdt_size);

    /*
     * The "/soc/pci@..." node is needed for PCIE hotplugs
     * that might happen before finalize_fdt().
     */
    name = g_strdup_printf("/soc/pci@%"HWADDR_PRIx,
                           s->memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_add_subnode(ms->fdt, name);

    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    /* Pass seed to RNG */
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(ms->fdt, "/chosen", "rng-seed",
                     rng_seed, sizeof(rng_seed));

    qemu_fdt_add_subnode(ms->fdt, "/aliases");

    riscv_create_fdt_flash(ms->fdt, s->memmap[VIRT_FLASH].base,
                           s->memmap[VIRT_FLASH].size / 2);
    create_fdt_fw_cfg(s);
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
    object_property_set_uint(OBJECT(dev), PCI_HOST_ECAM_BASE,
                            ecam_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_ECAM_SIZE,
                            ecam_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_BASE,
                             mmio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_SIZE,
                            mmio_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_ABOVE_4G_MMIO_BASE,
                             high_mmio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_ABOVE_4G_MMIO_SIZE,
                            high_mmio_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_PIO_BASE,
                            pio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_PIO_SIZE,
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

    for (i = 0; i < PCI_NUM_PINS; i++) {
        irq = qdev_get_gpio_in(irqchip, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    GPEX_HOST(dev)->gpex_cfg.bus = PCI_HOST_BRIDGE(dev)->bus;
    return dev;
}

static FWCfgState *create_fw_cfg(const MachineState *ms, hwaddr base)
{
    FWCfgState *fw_cfg;

    fw_cfg = fw_cfg_init_mem_dma(base, &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)ms->smp.cpus);

    return fw_cfg;
}

static DeviceState *virt_create_plic(const MemMapEntry *memmap, int socket,
                                     int base_hartid, int hart_count)
{
    g_autofree char *plic_hart_config = NULL;

    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hart_count);

    /* Per-socket PLIC */
    return sifive_plic_create(
             memmap[VIRT_PLIC].base + socket * memmap[VIRT_PLIC].size,
             plic_hart_config, hart_count, base_hartid,
             VIRT_IRQCHIP_NUM_SOURCES,
             ((1U << VIRT_IRQCHIP_NUM_PRIO_BITS) - 1),
             VIRT_PLIC_PRIORITY_BASE, VIRT_PLIC_PENDING_BASE,
             VIRT_PLIC_ENABLE_BASE, VIRT_PLIC_ENABLE_STRIDE,
             VIRT_PLIC_CONTEXT_BASE,
             VIRT_PLIC_CONTEXT_STRIDE,
             memmap[VIRT_PLIC].size);
}

static void create_platform_bus(RISCVVirtState *s, DeviceState *irqchip)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    int i;
    MemoryRegion *sysmem = get_system_memory();

    dev = qdev_new(TYPE_PLATFORM_BUS_DEVICE);
    dev->id = g_strdup(TYPE_PLATFORM_BUS_DEVICE);
    qdev_prop_set_uint32(dev, "num_irqs", VIRT_PLATFORM_BUS_NUM_IRQS);
    qdev_prop_set_uint32(dev, "mmio_size", s->memmap[VIRT_PLATFORM_BUS].size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    s->platform_bus_dev = dev;

    sysbus = SYS_BUS_DEVICE(dev);
    for (i = 0; i < VIRT_PLATFORM_BUS_NUM_IRQS; i++) {
        int irq = VIRT_PLATFORM_BUS_IRQ + i;
        sysbus_connect_irq(sysbus, i, qdev_get_gpio_in(irqchip, irq));
    }

    memory_region_add_subregion(sysmem,
                                s->memmap[VIRT_PLATFORM_BUS].base,
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
    MachineState *machine = MACHINE(s);
    hwaddr start_addr = s->memmap[VIRT_DRAM].base;
    hwaddr firmware_end_addr;
    vaddr kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->soc[0]);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry = 0;
    BlockBackend *pflash_blk0;
    RISCVBootInfo boot_info;

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

    riscv_boot_info_init(&boot_info, &s->soc[0]);

    firmware_end_addr = riscv_find_and_load_firmware(machine, &boot_info,
                                                     firmware_name,
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
            start_addr = s->memmap[VIRT_FLASH].base;
        } else {
            /*
             * Pflash was supplied but either KVM guest or bios is not none.
             * In this case, base of the flash would contain S-mode payload.
             */
            riscv_setup_firmware_boot(machine);
            kernel_entry = s->memmap[VIRT_FLASH].base;
        }
    }

    if (machine->kernel_filename && !kernel_entry) {
        kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                         firmware_end_addr);
        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          true, NULL);
        kernel_entry = boot_info.image_low_addr;
    }

    fdt_load_addr = riscv_compute_fdt_addr(s->memmap[VIRT_DRAM].base,
                                           s->memmap[VIRT_DRAM].size,
                                           machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc[0], start_addr,
                              s->memmap[VIRT_MROM].base,
                              s->memmap[VIRT_MROM].size, kernel_entry,
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
    RISCVVirtState *s = RISCV_VIRT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    DeviceState *mmio_irqchip, *virtio_irqchip, *pcie_irqchip;
    int i, base_hartid, hart_count;
    int socket_count = riscv_socket_count(machine);

    s->memmap = virt_memmap;

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
                riscv_aclint_mtimer_create(s->memmap[VIRT_CLINT].base +
                            i * RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        base_hartid, hart_count,
                        RISCV_ACLINT_DEFAULT_MTIMECMP,
                        RISCV_ACLINT_DEFAULT_MTIME,
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
            } else {
                /* Per-socket ACLINT MSWI, MTIMER, and SSWI */
                riscv_aclint_swi_create(s->memmap[VIRT_CLINT].base +
                            i * s->memmap[VIRT_CLINT].size,
                        base_hartid, hart_count, false);
                riscv_aclint_mtimer_create(s->memmap[VIRT_CLINT].base +
                            i * s->memmap[VIRT_CLINT].size +
                            RISCV_ACLINT_SWI_SIZE,
                        RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                        base_hartid, hart_count,
                        RISCV_ACLINT_DEFAULT_MTIMECMP,
                        RISCV_ACLINT_DEFAULT_MTIME,
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
                riscv_aclint_swi_create(s->memmap[VIRT_ACLINT_SSWI].base +
                            i * s->memmap[VIRT_ACLINT_SSWI].size,
                        base_hartid, hart_count, true);
            }
        } else if (tcg_enabled()) {
            /* Per-socket SiFive CLINT */
            riscv_aclint_swi_create(
                    s->memmap[VIRT_CLINT].base + i * s->memmap[VIRT_CLINT].size,
                    base_hartid, hart_count, false);
            riscv_aclint_mtimer_create(s->memmap[VIRT_CLINT].base +
                    i * s->memmap[VIRT_CLINT].size + RISCV_ACLINT_SWI_SIZE,
                    RISCV_ACLINT_DEFAULT_MTIMER_SIZE, base_hartid, hart_count,
                    RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
                    RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);
        }

        /* Per-socket interrupt controller */
        if (s->aia_type == VIRT_AIA_TYPE_NONE) {
            s->irqchip[i] = virt_create_plic(s->memmap, i,
                                             base_hartid, hart_count);
        } else {
            int imsic_bits = imsic_num_bits(s->aia_guests + 1);
            s->irqchip[i] = riscv_create_aia(s->aia_type == VIRT_AIA_TYPE_APLIC_IMSIC,
                                             s->aia_guests,
                                             IMSIC_HART_SIZE(0),
                                             IMSIC_HART_SIZE(imsic_bits),
                                             s->num_sources,
                                             &s->memmap[VIRT_APLIC_M],
                                             &s->memmap[VIRT_APLIC_S],
                                             &s->memmap[VIRT_IMSIC_M],
                                             &s->memmap[VIRT_IMSIC_S],
                                             i, base_hartid, hart_count,
                                             VIRT_IRQCHIP_NUM_MSIS,
                                             VIRT_IRQCHIP_NUM_PRIO_BITS);
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

    if (kvm_enabled() && virt_use_kvm_aia_aplic_imsic(s->aia_type)) {
        kvm_riscv_aia_create(machine, IMSIC_MMIO_GROUP_MIN_SHIFT,
                             VIRT_IRQCHIP_NUM_SOURCES, VIRT_IRQCHIP_NUM_MSIS,
                             s->memmap[VIRT_APLIC_S].base,
                             s->memmap[VIRT_IMSIC_S].base,
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
        virt_high_pcie_memmap.base = s->memmap[VIRT_DRAM].base +
                                     machine->ram_size;
        virt_high_pcie_memmap.base =
            ROUND_UP(virt_high_pcie_memmap.base, virt_high_pcie_memmap.size);
    }

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, s->memmap[VIRT_DRAM].base,
                                machine->ram);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_virt_board.mrom",
                           s->memmap[VIRT_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, s->memmap[VIRT_MROM].base,
                                mask_rom);

    /*
     * Init fw_cfg. Must be done before riscv_load_fdt, otherwise the
     * device tree cannot be altered and we get FDT_ERR_NOSPACE.
     */
    s->fw_cfg = create_fw_cfg(machine, s->memmap[VIRT_FW_CFG].base);
    rom_set_fw(s->fw_cfg);

    /* SiFive Test MMIO device */
    sifive_test_create(s->memmap[VIRT_TEST].base);

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            s->memmap[VIRT_VIRTIO].base + i * s->memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(virtio_irqchip, VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory, pcie_irqchip, s);

    create_platform_bus(s, mmio_irqchip);

    serial_mm_init(system_memory, s->memmap[VIRT_UART0].base,
        0, qdev_get_gpio_in(mmio_irqchip, UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    if (serial_hd(1)) {
        serial_mm_init(system_memory, s->memmap[VIRT_UART1].base,
            0, qdev_get_gpio_in(mmio_irqchip, UART1_IRQ), 399193,
            serial_hd(1), DEVICE_LITTLE_ENDIAN);
        s->uart1_present = true;
    }

    sysbus_create_simple("goldfish_rtc", s->memmap[VIRT_RTC].base,
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
        create_fdt(s);
    }

    if (virt_is_iommu_sys_enabled(s)) {
        DeviceState *iommu_sys = qdev_new(TYPE_RISCV_IOMMU_SYS);

        object_property_set_uint(OBJECT(iommu_sys), "addr",
                                 s->memmap[VIRT_IOMMU_SYS].base,
                                 &error_fatal);
        object_property_set_uint(OBJECT(iommu_sys), "base-irq",
                                 IOMMU_SYS_IRQ,
                                 &error_fatal);
        object_property_set_link(OBJECT(iommu_sys), "irqchip",
                                 OBJECT(mmio_irqchip),
                                 &error_fatal);
        /*
         * For riscv64 use a physical address size of 56 bits (44 bit PPN),
         * and for riscv32 use 34 bits (22 bit PPN).
         */
        object_property_set_uint(OBJECT(iommu_sys), "pas-bits",
                                 riscv_is_32bit(&s->soc[0]) ? 34 : 56,
                                 &error_fatal);

        sysbus_realize_and_unref(SYS_BUS_DEVICE(iommu_sys), &error_fatal);
    }

    s->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void virt_machine_instance_finalize(Object *obj)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    for (int i = 0; i < ARRAY_SIZE(s->flash); i++) {
        if (s->flash[i] && !qdev_is_realized(DEVICE(s->flash[i]))) {
            object_unref(OBJECT(s->flash[i]));
        }
    }
    g_free(s->oem_id);
    g_free(s->oem_table_id);
}

static void virt_machine_instance_init(Object *obj)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    virt_flash_create(s);

    s->oem_id = g_strndup(ACPI_BUILD_APPNAME6, 6);
    s->oem_table_id = g_strndup(ACPI_BUILD_APPNAME8, 8);
    s->acpi = ON_OFF_AUTO_AUTO;
    s->iommu_sys = ON_OFF_AUTO_AUTO;
    s->num_sources = VIRT_IRQCHIP_NUM_SOURCES;
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

bool virt_is_iommu_sys_enabled(RISCVVirtState *s)
{
    return s->iommu_sys == ON_OFF_AUTO_ON;
}

static void virt_get_iommu_sys(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);
    OnOffAuto iommu_sys = s->iommu_sys;

    visit_type_OnOffAuto(v, name, &iommu_sys, errp);
}

static void virt_set_iommu_sys(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    RISCVVirtState *s = RISCV_VIRT_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &s->iommu_sys, errp);
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
    RISCVVirtState *s = RISCV_VIRT_MACHINE(machine);

    if (device_is_dynamic_sysbus(mc, dev) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI) ||
        object_dynamic_cast(OBJECT(dev), TYPE_RISCV_IOMMU_PCI)) {
        s->iommu_sys = ON_OFF_AUTO_OFF;
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

    if (object_dynamic_cast(OBJECT(dev), TYPE_RISCV_IOMMU_PCI)) {
        create_fdt_iommu(s, pci_get_bdf(PCI_DEVICE(dev)));
        s->iommu_sys = ON_OFF_AUTO_OFF;
    }
}

static void virt_machine_class_init(ObjectClass *oc, const void *data)
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
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_UEFI_VARS_SYSBUS);
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

    object_class_property_add(oc, "iommu-sys", "OnOffAuto",
                              virt_get_iommu_sys, virt_set_iommu_sys,
                              NULL, NULL);
    object_class_property_set_description(oc, "iommu-sys",
                                          "Enable IOMMU platform device");
}

static const TypeInfo virt_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("virt"),
    .parent     = TYPE_MACHINE,
    .class_init = virt_machine_class_init,
    .instance_init = virt_machine_instance_init,
    .instance_finalize = virt_machine_instance_finalize,
    .instance_size = sizeof(RISCVVirtState),
    .interfaces = (const InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void virt_machine_init_register_types(void)
{
    type_register_static(&virt_machine_typeinfo);
}

type_init(virt_machine_init_register_types)
