/*
 * Tenstorrent Atlantis RISC-V System on Chip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2025 Tenstorrent, Joel Stanley <joel@jms.id.au>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"

#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/sysbus.h"

#include "target/riscv/cpu.h"

#include "hw/riscv/boot.h"
#include "hw/riscv/fdt-common.h"
#include "hw/riscv/riscv_hart.h"

#include "hw/char/serial-mm.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/misc/unimp.h"

#include "system/system.h"
#include "system/device_tree.h"

#include "hw/riscv/tt_atlantis.h"

#include "aia.h"

#define TT_IRQCHIP_NUM_MSIS       255
#define TT_IRQCHIP_NUM_SOURCES    128
#define TT_IRQCHIP_NUM_PRIO_BITS  3
#define TT_IMSIC_GUESTS           5
#define TT_IMSIC_STRIDE           0x40000 /* Same stride for M and S */
#define TT_IMSIC_GUEST_BITS       6

/* Stride is fixed by hardware, check it's consistent with guest bits. */
QEMU_BUILD_BUG_ON(TT_IMSIC_STRIDE != (0x1000 << TT_IMSIC_GUEST_BITS));

#define TT_ACLINT_MTIME_SIZE    0x8050
#define TT_ACLINT_MTIME         0x0
#define TT_ACLINT_MTIMECMP      0x8000
#define TT_ACLINT_TIMEBASE_FREQ 1000000000

static const MemMapEntry tt_atlantis_memmap[] = {
    /* Keep sorted with :'<,'>!sort -g -k 4 */
    [TT_ATL_DDR_LO] =           { 0x00000000,    0x80000000 },
    [TT_ATL_BOOTROM] =          { 0x80000000,        0x2000 },
    [TT_ATL_MIMSIC] =           { 0xa0000000,      0x200000 },
    [TT_ATL_ACLINT] =           { 0xa2180000,       0x10000 },
    [TT_ATL_SIMSIC] =           { 0xa4000000,      0x200000 },
    [TT_ATL_MAPLIC] =           { 0xcc000000,     0x4000000 },
    [TT_ATL_I2C0] =             { 0xd4040000,       0x10000 },
    [TT_ATL_I2C1] =             { 0xd4050000,       0x10000 },
    [TT_ATL_I2C2] =             { 0xd4060000,       0x10000 },
    [TT_ATL_I2C3] =             { 0xd4070000,       0x10000 },
    [TT_ATL_I2C4] =             { 0xd4080000,       0x10000 },
    [TT_ATL_UART1] =            { 0xd4110000,       0x10000 },
    [TT_ATL_SAPLIC] =           { 0xe8000000,     0x4000000 },
    [TT_ATL_DDR_HI] =          { 0x100000000,  0x1000000000 },
};

static I2CBus *i2c_get_bus(TTAtlantisSoCState *s, unsigned busnr)
{
    assert(busnr < TT_ATL_NUM_I2C);

    return s->i2c[busnr].bus;
}

static uint32_t fdt_phandle = 1;
static uint32_t next_phandle(void)
{
    return fdt_phandle++;
}

static void create_fdt_memory(void *fdt, TTAtlantisSoCState *s)
{
    hwaddr ram_size = memory_region_size(s->dram);
    hwaddr size_lo = ram_size;
    hwaddr size_hi = 0;

    if (size_lo > s->memmap[TT_ATL_DDR_LO].size) {
        size_lo = s->memmap[TT_ATL_DDR_LO].size;
        size_hi = ram_size - size_lo;
    }

    riscv_create_fdt_socket_memory(fdt, s->memmap[TT_ATL_DDR_LO].base,
                                   size_lo, 0, false);
    if (size_hi) {
        /*
         * The first part of the HI address is aliased at the LO address
         * so do not include that as usable memory. Is there any way
         * (or good reason) to describe that aliasing 2GB with DT?
         */
        riscv_create_fdt_socket_memory(fdt,
                                       s->memmap[TT_ATL_DDR_HI].base + size_lo,
                                       size_hi, 0, false);
    }
}

static void create_fdt_aclint(void *fdt, TTAtlantisSoCState *s,
                              uint32_t *intc_phandles)
{
    g_autofree char *name = NULL;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    uint32_t aclint_cells_size;
    hwaddr addr;

    aclint_mtimer_cells = g_new0(uint32_t, s->cpus.num_harts * 2);

    for (int cpu = 0; cpu < s->cpus.num_harts; cpu++) {
        aclint_mtimer_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mtimer_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
    }
    aclint_cells_size = s->cpus.num_harts * sizeof(uint32_t) * 2;

    addr = s->memmap[TT_ATL_ACLINT].base;

    name = g_strdup_printf("/soc/mtimer@%"HWADDR_PRIX, addr);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aclint-mtimer");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg",
                                 2, addr + TT_ACLINT_MTIME,
                                 2, 0x1000,
                                 2, addr + TT_ACLINT_MTIMECMP,
                                 2, 0x1000);
    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     aclint_mtimer_cells, aclint_cells_size);
}

static void create_fdt_one_imsic(void *fdt, const MemMapEntry *mem, int cpus,
                                 uint32_t *intc_phandles, uint32_t msi_phandle,
                                 int irq_line, uint32_t imsic_guest_bits)
{
    g_autofree char *name = NULL;
    g_autofree uint32_t *imsic_cells = g_new0(uint32_t, cpus * 2);

    for (int cpu = 0; cpu < cpus; cpu++) {
        imsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        imsic_cells[cpu * 2 + 1] = cpu_to_be32(irq_line);
    }

    name = g_strdup_printf("/soc/interrupt-controller@%"HWADDR_PRIX, mem->base);
    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,imsics");

    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 0);
    qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, name, "msi-controller", NULL, 0);
    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     imsic_cells, sizeof(uint32_t) * cpus * 2);
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "riscv,num-ids", TT_IRQCHIP_NUM_MSIS);

    if (imsic_guest_bits) {
        qemu_fdt_setprop_cell(fdt, name, "riscv,guest-index-bits",
                              imsic_guest_bits);
    }
    qemu_fdt_setprop_cell(fdt, name, "phandle", msi_phandle);
}

static void create_fdt_one_aplic(void *fdt,
                                 const MemMapEntry *mem,
                                 uint32_t msi_phandle,
                                 uint32_t *intc_phandles,
                                 uint32_t aplic_phandle,
                                 uint32_t aplic_child_phandle,
                                 int irq_line, int num_harts)
{
    g_autofree char *name =
        g_strdup_printf("/soc/interrupt-controller@%"HWADDR_PRIX, mem->base);
    g_autofree uint32_t *aplic_cells = g_new0(uint32_t, num_harts * 2);

    for (int cpu = 0; cpu < num_harts; cpu++) {
        aplic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aplic_cells[cpu * 2 + 1] = cpu_to_be32(irq_line);
    }

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "riscv,aplic");
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 0);
    qemu_fdt_setprop_cell(fdt, name, "#interrupt-cells", 2);
    qemu_fdt_setprop(fdt, name, "interrupt-controller", NULL, 0);

    qemu_fdt_setprop(fdt, name, "interrupts-extended",
                     aplic_cells, num_harts * sizeof(uint32_t) * 2);
    qemu_fdt_setprop_cell(fdt, name, "msi-parent", msi_phandle);

    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "riscv,num-sources",
                          TT_IRQCHIP_NUM_SOURCES);

    if (aplic_child_phandle) {
        qemu_fdt_setprop_cell(fdt, name, "riscv,children",
                              aplic_child_phandle);
        qemu_fdt_setprop_cells(fdt, name, "riscv,delegation",
                               aplic_child_phandle, 1, TT_IRQCHIP_NUM_SOURCES);
    }

    qemu_fdt_setprop_cell(fdt, name, "phandle", aplic_phandle);
}

static void create_fdt_pmu(void *fdt, TTAtlantisSoCState *s)
{
    char pmu_name[] = "/pmu";
    RISCVCPU *hart = &s->cpus.harts[0];

    qemu_fdt_add_subnode(fdt, pmu_name);
    qemu_fdt_setprop_string(fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(fdt, hart->pmu_avail_ctrs, pmu_name);
}

static void create_fdt_cpu(void *fdt, TTAtlantisSoCState *s,
                           uint32_t aplic_s_phandle,
                           uint32_t imsic_s_phandle)
{
    g_autofree uint32_t *intc_phandles = g_new0(uint32_t, s->cpus.num_harts);
    int num_harts = s->cpus.num_harts;

    riscv_fdt_create_cpu_socket_subnode(fdt, TT_ACLINT_TIMEBASE_FREQ);

    riscv_create_fdt_socket_cpus(fdt, s->cpus.harts, 0, num_harts,
                                 s->cpus.hartid_base, &fdt_phandle,
                                 intc_phandles, false, false);

    create_fdt_aclint(fdt, s, intc_phandles);

    /* M-level IMSIC node */
    uint32_t msi_m_phandle = next_phandle();
    create_fdt_one_imsic(fdt, &s->memmap[TT_ATL_MIMSIC], num_harts,
                         intc_phandles, msi_m_phandle,
                         IRQ_M_EXT, TT_IMSIC_GUEST_BITS);

    /* S-level IMSIC node */
    create_fdt_one_imsic(fdt, &s->memmap[TT_ATL_SIMSIC], num_harts,
                         intc_phandles, imsic_s_phandle,
                         IRQ_S_EXT, TT_IMSIC_GUEST_BITS);

    uint32_t aplic_m_phandle = next_phandle();

    /* M-level APLIC node */
    create_fdt_one_aplic(fdt, &s->memmap[TT_ATL_MAPLIC],
                         msi_m_phandle, intc_phandles,
                         aplic_m_phandle, aplic_s_phandle,
                         IRQ_M_EXT, num_harts);

    /* S-level APLIC node */
    create_fdt_one_aplic(fdt, &s->memmap[TT_ATL_SAPLIC],
                         imsic_s_phandle, intc_phandles,
                         aplic_s_phandle, 0,
                         IRQ_S_EXT, num_harts);
}

static void create_fdt_uart(void *fdt, const MemMapEntry *mem, int irq,
                            int irqchip_phandle)
{
    g_autofree char *name = g_strdup_printf("/soc/serial@%"HWADDR_PRIX,
                                            mem->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "reg-shift", 2);
    qemu_fdt_setprop_cell(fdt, name, "reg-io-width", 4);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", irqchip_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts", irq, 0x4);

    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", name);
    qemu_fdt_setprop_string(fdt, "/aliases", "serial0", name);
}

static void create_fdt_rng(void *fdt)
{
    uint8_t rng_seed[32];

    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(fdt, "/chosen", "rng-seed", rng_seed, sizeof(rng_seed));
}

static void create_fdt_clk(void *fdt, const char *clock_name,
                           uint32_t freq, uint32_t phandle)
{
    g_autofree char *name = g_strdup_printf("/clocks/%s", clock_name);

    qemu_fdt_add_path(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "fixed-clock");
    qemu_fdt_setprop_string(fdt, name, "clock-output-names", clock_name);
    qemu_fdt_setprop_cell(fdt, name, "#clock-cells", 0);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", freq);
    qemu_fdt_setprop_cell(fdt, name, "phandle", phandle);
}

static void create_fdt_i2c(void *fdt, const MemMapEntry *mem, uint32_t irq,
                           uint32_t irqchip_phandle, uint32_t clk_phandle)
{
    g_autofree char *name = g_strdup_printf("/soc/i2c@%"HWADDR_PRIX, mem->base);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", "snps,designware-i2c");
    qemu_fdt_setprop_sized_cells(fdt, name, "reg", 2, mem->base, 2, mem->size);
    qemu_fdt_setprop_cell(fdt, name, "interrupt-parent", irqchip_phandle);
    qemu_fdt_setprop_cells(fdt, name, "interrupts", irq, 0x4);
    qemu_fdt_setprop_cell(fdt, name, "clocks", clk_phandle);
    qemu_fdt_setprop_cell(fdt, name, "clock-frequency", 100000);
    qemu_fdt_setprop_cell(fdt, name, "#address-cells", 1);
    qemu_fdt_setprop_cell(fdt, name, "#size-cells", 0);
}

static void create_fdt_i2c_device(void *fdt, TTAtlantisSoCState *s, int bus,
                                  const char *compat, int addr)
{
    hwaddr base = s->memmap[TT_ATL_I2C0 + bus].base;
    g_autofree char *name = g_strdup_printf("/soc/i2c@%"HWADDR_PRIX"/sensor@%x",
                                            base, addr);

    qemu_fdt_add_subnode(fdt, name);
    qemu_fdt_setprop_string(fdt, name, "compatible", compat);
    qemu_fdt_setprop_cell(fdt, name, "reg", addr);
}

static void finalize_fdt(void *fdt, TTAtlantisSoCState *s)
{
    uint32_t aplic_s_phandle = next_phandle();
    uint32_t imsic_s_phandle = next_phandle();
    uint32_t periph_clk_phandle = next_phandle();

    create_fdt_cpu(fdt, s, aplic_s_phandle, imsic_s_phandle);

    create_fdt_memory(fdt, s);

    /*
     * We want to do this, but the Linux aplic driver was broken before v6.16
     *
     * qemu_fdt_setprop_cell(MACHINE(s)->fdt, "/soc", "interrupt-parent",
     *                       aplic_s_phandle);
     */

    create_fdt_uart(fdt, &s->memmap[TT_ATL_UART1], TT_ATL_UART1_IRQ,
                    aplic_s_phandle);

    create_fdt_clk(fdt, "periph-clk", 100000000, periph_clk_phandle);

    for (int i = 0; i < TT_ATL_NUM_I2C; i++) {
        create_fdt_i2c(fdt,
                       &s->memmap[TT_ATL_I2C0 + i],
                       TT_ATL_I2C0_IRQ + i,
                       aplic_s_phandle, periph_clk_phandle);
    }

    /* I2C peripherals: qemu specific */
    create_fdt_i2c_device(fdt, s, 0, "dallas,ds1338", 0x6f);
    create_fdt_i2c_device(fdt, s, 4, "ti,tmp105", 0x48);
}

static void create_fdt(TTAtlantisState *ams)
{
    MachineState *ms = MACHINE(ams);
    int fdt_size = 0;

    ms->fdt = riscv_create_board_device_tree("Tenstorrent Atlantis RISC-V Machine",
                                             "tenstorrent,atlantis", &fdt_size);

    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    create_fdt_rng(ms->fdt);

    qemu_fdt_add_subnode(ms->fdt, "/aliases");

    create_fdt_pmu(ms->fdt, &ams->soc);
}

static void load_fdt(TTAtlantisState *ams)
{
    MachineState *ms = MACHINE(ams);
    char **node_path;
    Error *err = NULL;
    int fdt_size = 0;

    ms->fdt = load_device_tree(ms->dtb, &fdt_size);
    if (!ms->fdt) {
        error_report("load_device_tree() failed");
        exit(1);
    }

    qemu_fdt_add_path(ms->fdt, "/chosen");

    /* Clear memory nodes and update with the specified RAM size */
    node_path = qemu_fdt_node_unit_path(ms->fdt, "memory", &err);
    if (err) {
        warn_report_err(err);
    } else {
        for (int i = 0; node_path[i]; i++) {
            warn_report("Replacing device tree %s with the requested RAM size",
                        node_path[i]);
            qemu_fdt_nop_node(ms->fdt, node_path[i]);
        }
        g_strfreev(node_path);
    }

    create_fdt_memory(ms->fdt, &ams->soc);
}

static void mmio_map_unimplemented(MemoryRegion *memory, SysBusDevice *dev,
                                   const char *name, hwaddr addr, uint64_t size)
{
    qdev_prop_set_string(DEVICE(dev), "name", name);
    qdev_prop_set_uint64(DEVICE(dev), "size", size);
    sysbus_realize(dev, &error_abort);

    memory_region_add_subregion_overlap(memory, addr,
                                        sysbus_mmio_get_region(dev, 0), -1000);
}

static void tt_atlantis_machine_done(Notifier *n, void *data)
{
    TTAtlantisState *ams = container_of(n, TTAtlantisState, machine_done);
    TTAtlantisSoCState *s = &ams->soc;
    MachineState *machine = MACHINE(ams);
    hwaddr start_addr = s->memmap[TT_ATL_DDR_LO].base;
    hwaddr mem_size;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->cpus);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry;
    RISCVBootInfo boot_info;

    /*
     * A user provided dtb must include everything, including
     * dynamic sysbus devices. Our FDT needs to be finalized.
     */
    if (machine->dtb == NULL) {
        finalize_fdt(machine->fdt, s);
    }

    mem_size = machine->ram_size;
    if (mem_size > s->memmap[TT_ATL_DDR_LO].size) {
        mem_size = s->memmap[TT_ATL_DDR_LO].size;
    }
    riscv_boot_info_init_discontig_mem(&boot_info, &s->cpus,
                                       s->memmap[TT_ATL_DDR_LO].base,
                                       mem_size);

    firmware_end_addr = riscv_find_and_load_firmware(machine, &boot_info,
                                                     firmware_name,
                                                     &start_addr, NULL);

    kernel_start_addr = riscv_calc_kernel_start_addr(&boot_info,
                                                     firmware_end_addr);
    if (machine->kernel_filename) {
        riscv_load_kernel(machine, &boot_info, kernel_start_addr,
                          true, NULL);
        kernel_entry = boot_info.image_low_addr;
    } else {
        /* If we aren't loading a payload, OpenSBI thinks we are trying to boot
         * address 0, which fails `sbi_domain_check_addr()` as that is where
         * OpenSBI is running. Instead point OpenSBI to the end of the region
         * where it was loaded, which avoids the early hang, allowing the
         * system to proceed with the OpenSBI boot output.
         */
        kernel_entry = kernel_start_addr;
    }

    fdt_load_addr = riscv_compute_fdt_addr(s->memmap[TT_ATL_DDR_LO].base,
                                           s->memmap[TT_ATL_DDR_LO].size,
                                           machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->cpus, start_addr,
                              s->memmap[TT_ATL_BOOTROM].base,
                              s->memmap[TT_ATL_BOOTROM].size,
                              kernel_entry,
                              fdt_load_addr);
}

static void tt_atlantis_soc_init(Object *obj)
{
    TTAtlantisSoCState *s = TT_ATLANTIS_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);

    object_initialize_child(obj, "uart1", &s->uart1,
                            TYPE_UNIMPLEMENTED_DEVICE);

    for (int i = 0; i < TT_ATL_NUM_I2C; i++) {
        object_initialize_child(obj, "i2c[*]", &s->i2c[i],
                                TYPE_DESIGNWARE_I2C);
    }
}

static void tt_atlantis_soc_realize(DeviceState *dev, Error **errp)
{
    TTAtlantisSoCState *s = TT_ATLANTIS_SOC(dev);
    ram_addr_t lo_ram_size, ram_size;
    int hart_count = s->num_harts;

    if (!s->memory) {
        error_setg(errp, "'memory' link is not set");
        return;
    }
    if (!s->dram) {
        error_setg(errp, "'dram' link is not set");
        return;
    }
    ram_size = memory_region_size(s->dram);

    s->memmap = tt_atlantis_memmap;

    /* CPUs */
    object_property_set_str(OBJECT(&s->cpus), "cpu-type", s->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "hartid-base", 0,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", hart_count,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "resetvec",
                            s->memmap[TT_ATL_BOOTROM].base,
                            &error_abort);
    object_property_set_link(OBJECT(&s->cpus), "memory", OBJECT(s->memory),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpus), errp)) {
        return;
    }

    s->irqchip = riscv_create_aia(s->memory,
                                  true, TT_IMSIC_GUESTS,
                                  TT_IMSIC_STRIDE,
                                  TT_IMSIC_STRIDE,
                                  TT_IRQCHIP_NUM_SOURCES,
                                  &s->memmap[TT_ATL_MAPLIC],
                                  &s->memmap[TT_ATL_SAPLIC],
                                  &s->memmap[TT_ATL_MIMSIC],
                                  &s->memmap[TT_ATL_SIMSIC],
                                  0, 0, hart_count,
                                  TT_IRQCHIP_NUM_MSIS,
                                  TT_IRQCHIP_NUM_PRIO_BITS);

    riscv_aclint_mtimer_create(s->memory,
            s->memmap[TT_ATL_ACLINT].base,
            TT_ACLINT_MTIME_SIZE,
            0, hart_count,
            TT_ACLINT_MTIMECMP,
            TT_ACLINT_MTIME,
            TT_ACLINT_TIMEBASE_FREQ, true);

    /*
     * DDR
     *
     * The high address is where RAM lives. It is always present and may be
     * up to 64GB. The low address is an alias of the first 2GB of that RAM.
     */
    if (ram_size > s->memmap[TT_ATL_DDR_HI].size) {
        g_autofree char *sz = size_to_str(s->memmap[TT_ATL_DDR_HI].size);
        error_setg(errp, "RAM size is too large, maximum is %s", sz);
        return;
    }

    memory_region_init_alias(&s->ram_hi, OBJECT(s), "ram.high", s->dram,
                             0, ram_size);
    memory_region_add_subregion(s->memory,
                                s->memmap[TT_ATL_DDR_HI].base, &s->ram_hi);

    lo_ram_size = MIN(ram_size, s->memmap[TT_ATL_DDR_LO].size);
    memory_region_init_alias(&s->ram_lo, OBJECT(s), "ram.low", s->dram,
                             0, lo_ram_size);
    memory_region_add_subregion(s->memory,
                                s->memmap[TT_ATL_DDR_LO].base, &s->ram_lo);

    /* Boot ROM */
    if (!memory_region_init_rom(&s->bootrom, OBJECT(s), "tt-atlantis.bootrom",
                                s->memmap[TT_ATL_BOOTROM].size, errp)) {
        return;
    }
    memory_region_add_subregion(s->memory, s->memmap[TT_ATL_BOOTROM].base,
                                &s->bootrom);

    /* UART1, the soc console (UART0 is for the boot microcontroller) */
    serial_mm_init(s->memory, s->memmap[TT_ATL_UART1].base, 2,
                   qdev_get_gpio_in(s->irqchip, TT_ATL_UART1_IRQ),
                   115200, serial_hd(0), DEVICE_LITTLE_ENDIAN);
    /*
     * Atlantis contains a DesignWare uart while the QEMU machine
     * uses the serial_mm model with the base ns16550 register set.
     * Linux's dw driver writes outside of serial_mm's 0x20 sized
     * mapping and faults.
     *
     * Create an unimplemented device region so writes don't fault
     * and reads return zero, which keeps Linux happy.
     */
    mmio_map_unimplemented(s->memory, SYS_BUS_DEVICE(&s->uart1),
                           "tt-atlantis.uart1", s->memmap[TT_ATL_UART1].base,
                           s->memmap[TT_ATL_UART1].size);

    /* I2C */
    for (int i = 0; i < TT_ATL_NUM_I2C; i++) {
        SysBusDevice *sbd = SYS_BUS_DEVICE(&s->i2c[i]);

        if (!sysbus_realize(sbd, errp)) {
            return;
        }
        memory_region_add_subregion(s->memory,
                                    s->memmap[TT_ATL_I2C0 + i].base,
                                    sysbus_mmio_get_region(sbd, 0));
        sysbus_connect_irq(sbd, 0,
                           qdev_get_gpio_in(s->irqchip, TT_ATL_I2C0_IRQ + i));
    }
}

static const Property tt_atlantis_soc_props[] = {
    DEFINE_PROP_STRING("cpu-type", TTAtlantisSoCState, cpu_type),
    DEFINE_PROP_UINT32("num-harts", TTAtlantisSoCState, num_harts, 8),
    DEFINE_PROP_LINK("memory", TTAtlantisSoCState, memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_LINK("dram", TTAtlantisSoCState, dram,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void tt_atlantis_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = tt_atlantis_soc_realize;
    device_class_set_props(dc, tt_atlantis_soc_props);
    /* The SoC can only be instantiated from the machine */
    dc->user_creatable = false;
}

static void tt_atlantis_machine_init(MachineState *machine)
{
    TTAtlantisState *ams = TT_ATLANTIS_MACHINE(machine);
    TTAtlantisSoCState *s = &ams->soc;

    memory_region_init(&ams->soc_memory, OBJECT(machine),
                       "tt-atlantis.soc-memory", UINT64_MAX);
    memory_region_add_subregion(get_system_memory(), 0, &ams->soc_memory);

    object_initialize_child(OBJECT(machine), "soc", &ams->soc,
                            TYPE_TT_ATLANTIS_SOC);
    object_property_set_str(OBJECT(&ams->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&ams->soc), "num-harts", machine->smp.cpus,
                            &error_abort);

    object_property_set_link(OBJECT(&ams->soc), "memory",
                             OBJECT(&ams->soc_memory), &error_abort);
    object_property_set_link(OBJECT(&ams->soc), "dram", OBJECT(machine->ram),
                             &error_abort);
    qdev_realize(DEVICE(&ams->soc), NULL, &error_fatal);

    /* I2C peripherals: qemu specific */
    i2c_slave_create_simple(i2c_get_bus(s, 0), "ds1338", 0x6f);
    i2c_slave_create_simple(i2c_get_bus(s, 4), "tmp105", 0x48);

    /* Load or create device tree */
    if (machine->dtb) {
        load_fdt(ams);
    } else {
        create_fdt(ams);
    }

    ams->machine_done.notify = tt_atlantis_machine_done;
    qemu_add_machine_init_done_notifier(&ams->machine_done);
}

static void tt_atlantis_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Tenstorrent Atlantis RISC-V SoC (Experimental)";
    mc->init = tt_atlantis_machine_init;
    mc->max_cpus = 8;
    mc->default_cpus = 8;
    mc->default_ram_size = 4 * GiB;
    mc->default_cpu_type = TYPE_RISCV_CPU_TT_ASCALON;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->default_ram_id = "tt_atlantis.ram";
}

static const TypeInfo tt_atlantis_types[] = {
    {
        .name       = TYPE_TT_ATLANTIS_SOC,
        .parent     = TYPE_DEVICE,
        .instance_size = sizeof(TTAtlantisSoCState),
        .instance_init = tt_atlantis_soc_init,
        .class_init = tt_atlantis_soc_class_init,
    }, {
        .name       = MACHINE_TYPE_NAME("tt-atlantis"),
        .parent     = TYPE_MACHINE,
        .class_init = tt_atlantis_machine_class_init,
        .instance_size = sizeof(TTAtlantisState),
    },
};

DEFINE_TYPES(tt_atlantis_types)
