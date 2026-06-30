/*
 * Tenstorrent Atlantis RISC-V System on Chip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2025 Tenstorrent, Joel Stanley <joel@jms.id.au>
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qemu/units.h"

#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"

#include "target/riscv/cpu.h"
#include "target/riscv/pmu.h"

#include "hw/riscv/boot.h"
#include "hw/riscv/fdt-common.h"
#include "hw/riscv/machines-qom.h"
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
#define TT_IRQCHIP_GUESTS         63 /* aia_guests, gives guest_index_bits=6 */
#define TT_IRQCHIP_MIMSIC_STRIDE  0x40000

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
    [TT_ATL_UART1] =            { 0xd4110000,       0x10000 },
    [TT_ATL_SAPLIC] =           { 0xe8000000,     0x4000000 },
    [TT_ATL_DDR_HI] =          { 0x100000000,  0x1000000000 },
};

static uint32_t fdt_phandle = 1;
static uint32_t next_phandle(void)
{
    return fdt_phandle++;
}

static void create_fdt_memory(TTAtlantisState *s)
{
    void *fdt = MACHINE(s)->fdt;
    hwaddr size_lo = MACHINE(s)->ram_size;
    hwaddr size_hi = 0;

    if (size_lo > s->memmap[TT_ATL_DDR_LO].size) {
        size_lo = s->memmap[TT_ATL_DDR_LO].size;
        size_hi = MACHINE(s)->ram_size - size_lo;
    }

    create_fdt_socket_memory(fdt, s->memmap[TT_ATL_DDR_LO].base, size_lo,
                             0, false);
    if (size_hi) {
        /*
         * The first part of the HI address is aliased at the LO address
         * so do not include that as usable memory. Is there any way
         * (or good reason) to describe that aliasing 2GB with DT?
         */
        create_fdt_socket_memory(fdt, s->memmap[TT_ATL_DDR_HI].base + size_lo,
                                 size_hi, 0, false);
    }
}

static void create_fdt_aclint(TTAtlantisState *s, uint32_t *intc_phandles)
{
    void *fdt = MACHINE(s)->fdt;
    g_autofree char *name = NULL;
    g_autofree uint32_t *aclint_mtimer_cells = NULL;
    uint32_t aclint_cells_size;
    hwaddr addr;

    aclint_mtimer_cells = g_new0(uint32_t, s->soc.num_harts * 2);

    for (int cpu = 0; cpu < s->soc.num_harts; cpu++) {
        aclint_mtimer_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        aclint_mtimer_cells[cpu * 2 + 1] = cpu_to_be32(IRQ_M_TIMER);
    }
    aclint_cells_size = s->soc.num_harts * sizeof(uint32_t) * 2;

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

static void create_fdt_pmu(TTAtlantisState *s)
{
    char pmu_name[] = "/pmu";
    void *fdt = MACHINE(s)->fdt;
    RISCVCPU *hart = &s->soc.harts[0];

    qemu_fdt_add_subnode(fdt, pmu_name);
    qemu_fdt_setprop_string(fdt, pmu_name, "compatible", "riscv,pmu");
    riscv_pmu_generate_fdt_node(fdt, hart->pmu_avail_ctrs, pmu_name);
}

static void create_fdt_cpu(TTAtlantisState *s, const MemMapEntry *memmap,
                           uint32_t aplic_s_phandle,
                           uint32_t imsic_s_phandle)
{
    MachineState *ms = MACHINE(s);
    void *fdt = MACHINE(s)->fdt;
    g_autofree uint32_t *intc_phandles = g_new0(uint32_t, ms->smp.cpus);

    fdt_create_cpu_socket_subnode(fdt, TT_ACLINT_TIMEBASE_FREQ);

    create_fdt_socket_cpus(fdt, s->soc.harts, 0, s->soc.num_harts,
                           s->soc.hartid_base, &fdt_phandle, intc_phandles,
                           false, false);

    create_fdt_memory(s);

    create_fdt_aclint(s, intc_phandles);

    uint32_t imsic_guest_bits = imsic_num_bits(TT_IRQCHIP_GUESTS + 1);

    /* M-level IMSIC node */
    uint32_t msi_m_phandle = next_phandle();
    create_fdt_one_imsic(fdt, &s->memmap[TT_ATL_MIMSIC], ms->smp.cpus,
                         intc_phandles, msi_m_phandle,
                         IRQ_M_EXT, imsic_guest_bits);

    /* S-level IMSIC node */
    create_fdt_one_imsic(fdt, &s->memmap[TT_ATL_SIMSIC], ms->smp.cpus,
                         intc_phandles, imsic_s_phandle,
                         IRQ_S_EXT, imsic_guest_bits);

    uint32_t aplic_m_phandle = next_phandle();

    /* M-level APLIC node */
    create_fdt_one_aplic(fdt, &s->memmap[TT_ATL_MAPLIC],
                         msi_m_phandle, intc_phandles,
                         aplic_m_phandle, aplic_s_phandle,
                         IRQ_M_EXT, s->soc.num_harts);

    /* S-level APLIC node */
    create_fdt_one_aplic(fdt, &s->memmap[TT_ATL_SAPLIC],
                         imsic_s_phandle, intc_phandles,
                         aplic_s_phandle, 0,
                         IRQ_S_EXT, s->soc.num_harts);
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

static void finalize_fdt(TTAtlantisState *s)
{
    uint32_t aplic_s_phandle = next_phandle();
    uint32_t imsic_s_phandle = next_phandle();
    void *fdt = MACHINE(s)->fdt;

    create_fdt_cpu(s, s->memmap, aplic_s_phandle, imsic_s_phandle);

    /*
     * We want to do this, but the Linux aplic driver was broken before v6.16
     *
     * qemu_fdt_setprop_cell(MACHINE(s)->fdt, "/soc", "interrupt-parent",
     *                       aplic_s_phandle);
     */

    create_fdt_uart(fdt, &s->memmap[TT_ATL_UART1], TT_ATL_UART1_IRQ,
                    aplic_s_phandle);
}

static void create_fdt(TTAtlantisState *s)
{
    MachineState *ms = MACHINE(s);

    ms->fdt = create_board_device_tree("Tenstorrent Atlantis RISC-V Machine",
                                       "tenstorrent,atlantis", &s->fdt_size);

    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    create_fdt_rng(ms->fdt);

    qemu_fdt_add_subnode(ms->fdt, "/aliases");

    create_fdt_pmu(s);
}

static void load_fdt(TTAtlantisState *s)
{
    MachineState *ms = MACHINE(s);
    char **node_path;
    Error *err = NULL;

    ms->fdt = load_device_tree(ms->dtb, &s->fdt_size);
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

    create_fdt_memory(s);
}

static void tt_atlantis_machine_done(Notifier *notifier, void *data)
{
    TTAtlantisState *s = container_of(notifier, TTAtlantisState, machine_done);
    MachineState *machine = MACHINE(s);
    hwaddr start_addr = s->memmap[TT_ATL_DDR_LO].base;
    hwaddr mem_size;
    target_ulong firmware_end_addr, kernel_start_addr;
    const char *firmware_name = riscv_default_firmware_name(&s->soc);
    uint64_t fdt_load_addr;
    uint64_t kernel_entry;
    RISCVBootInfo boot_info;

    /*
     * A user provided dtb must include everything, including
     * dynamic sysbus devices. Our FDT needs to be finalized.
     */
    if (machine->dtb == NULL) {
        finalize_fdt(s);
    }

    mem_size = machine->ram_size;
    if (mem_size > s->memmap[TT_ATL_DDR_LO].size) {
        mem_size = s->memmap[TT_ATL_DDR_LO].size;
    }
    riscv_boot_info_init_discontig_mem(&boot_info, &s->soc,
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
    }
    kernel_entry = boot_info.image_low_addr;

    fdt_load_addr = riscv_compute_fdt_addr(s->memmap[TT_ATL_DDR_LO].base,
                                           s->memmap[TT_ATL_DDR_LO].size,
                                           machine, &boot_info);
    riscv_load_fdt(fdt_load_addr, machine->fdt);

    /* load the reset vector */
    riscv_setup_rom_reset_vec(machine, &s->soc, start_addr,
                              s->memmap[TT_ATL_BOOTROM].base,
                              s->memmap[TT_ATL_BOOTROM].size,
                              kernel_entry,
                              fdt_load_addr);
}

static void tt_atlantis_machine_init(MachineState *machine)
{
    TTAtlantisState *s = TT_ATLANTIS_MACHINE(machine);

    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *ram_hi = g_new(MemoryRegion, 1);
    MemoryRegion *ram_lo = g_new(MemoryRegion, 1);
    MemoryRegion *bootrom = g_new(MemoryRegion, 1);
    ram_addr_t lo_ram_size;
    int hart_count = machine->smp.cpus;

    s->memmap = tt_atlantis_memmap;

    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->soc), "cpu-type", machine->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "hartid-base", 0,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "num-harts", hart_count,
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), "resetvec",
                            s->memmap[TT_ATL_BOOTROM].base,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    s->irqchip = riscv_create_aia(true, TT_IRQCHIP_GUESTS,
                                  TT_IRQCHIP_MIMSIC_STRIDE,
                                  TT_IRQCHIP_NUM_SOURCES,
                                  &s->memmap[TT_ATL_MAPLIC],
                                  &s->memmap[TT_ATL_SAPLIC],
                                  &s->memmap[TT_ATL_MIMSIC],
                                  &s->memmap[TT_ATL_SIMSIC],
                                  0, 0, hart_count,
                                  TT_IRQCHIP_NUM_MSIS,
                                  TT_IRQCHIP_NUM_PRIO_BITS);

    riscv_aclint_mtimer_create(s->memmap[TT_ATL_ACLINT].base,
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
    if (machine->ram_size > s->memmap[TT_ATL_DDR_HI].size) {
        char *sz = size_to_str(s->memmap[TT_ATL_DDR_HI].size);
        error_report("RAM size is too large, maximum is %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    memory_region_init_alias(ram_hi, OBJECT(machine), "ram.high", machine->ram,
                             0, machine->ram_size);
    memory_region_add_subregion(system_memory,
                                s->memmap[TT_ATL_DDR_HI].base, ram_hi);

    lo_ram_size = MIN(machine->ram_size, s->memmap[TT_ATL_DDR_LO].size);
    memory_region_init_alias(ram_lo, OBJECT(machine), "ram.low", machine->ram,
                             0, lo_ram_size);
    memory_region_add_subregion(system_memory,
                                s->memmap[TT_ATL_DDR_LO].base, ram_lo);

    /* Boot ROM */
    memory_region_init_rom(bootrom, NULL, "tt-atlantis.bootrom",
                           s->memmap[TT_ATL_BOOTROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, s->memmap[TT_ATL_BOOTROM].base,
                                bootrom);

    /* UART1, the soc console (UART0 is for the boot microcontroller) */
    serial_mm_init(system_memory, s->memmap[TT_ATL_UART1].base, 2,
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
    create_unimplemented_device("tt-atlantis.uart0",
                                s->memmap[TT_ATL_UART1].base,
                                s->memmap[TT_ATL_UART1].size);

    /* Load or create device tree */
    if (machine->dtb) {
        load_fdt(s);
    } else {
        create_fdt(s);
    }

    s->machine_done.notify = tt_atlantis_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
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
        .name       = MACHINE_TYPE_NAME("tt-atlantis"),
        .parent     = TYPE_MACHINE,
        .class_init = tt_atlantis_machine_class_init,
        .instance_size = sizeof(TTAtlantisState),
        .interfaces = riscv64_machine_interfaces,
    },
};

DEFINE_TYPES(tt_atlantis_types)
