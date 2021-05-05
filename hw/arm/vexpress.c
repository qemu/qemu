/*
 * ARM Versatile Express emulation.
 *
 * Copyright (c) 2010 - 2011 B Labs Ltd.
 * Copyright (c) 2011 Linaro Limited
 * Written by Bahadir Balban, Amit Mahajan, Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributions after 2012-01-13 are licensed under the terms of the
 *  GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/arm/primecell.h"
#include "hw/net/lan9118.h"
#include "hw/i2c/i2c.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/block/flash.h"
#include "sysemu/device_tree.h"
#include "qemu/error-report.h"
#include <libfdt.h>
#include "hw/char/pl011.h"
#include "hw/cpu/a9mpcore.h"
#include "hw/cpu/a15mpcore.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/sd/sd.h"
#include "qom/object.h"

#define VEXPRESS_BOARD_ID 0x8e0
#define VEXPRESS_FLASH_SIZE (64 * 1024 * 1024)
#define VEXPRESS_FLASH_SECT_SIZE (256 * 1024)

/* Number of virtio transports to create (0..8; limited by
 * number of available IRQ lines).
 */
#define NUM_VIRTIO_TRANSPORTS 4

/* Address maps for peripherals:
 * the Versatile Express motherboard has two possible maps,
 * the "legacy" one (used for A9) and the "Cortex-A Series"
 * map (used for newer cores).
 * Individual daughterboards can also have different maps for
 * their peripherals.
 */

enum {
    VE_SYSREGS,
    VE_SP810,
    VE_SERIALPCI,
    VE_PL041,
    VE_MMCI,
    VE_KMI0,
    VE_KMI1,
    VE_UART0,
    VE_UART1,
    VE_UART2,
    VE_UART3,
    VE_WDT,
    VE_TIMER01,
    VE_TIMER23,
    VE_SERIALDVI,
    VE_RTC,
    VE_COMPACTFLASH,
    VE_CLCD,
    VE_NORFLASH0,
    VE_NORFLASH1,
    VE_NORFLASHALIAS,
    VE_SRAM,
    VE_VIDEORAM,
    VE_ETHERNET,
    VE_USB,
    VE_DAPROM,
    VE_VIRTIO,
};

static hwaddr motherboard_legacy_map[] = {
    [VE_NORFLASHALIAS] = 0,
    /* CS7: 0x10000000 .. 0x10020000 */
    [VE_SYSREGS] = 0x10000000,
    [VE_SP810] = 0x10001000,
    [VE_SERIALPCI] = 0x10002000,
    [VE_PL041] = 0x10004000,
    [VE_MMCI] = 0x10005000,
    [VE_KMI0] = 0x10006000,
    [VE_KMI1] = 0x10007000,
    [VE_UART0] = 0x10009000,
    [VE_UART1] = 0x1000a000,
    [VE_UART2] = 0x1000b000,
    [VE_UART3] = 0x1000c000,
    [VE_WDT] = 0x1000f000,
    [VE_TIMER01] = 0x10011000,
    [VE_TIMER23] = 0x10012000,
    [VE_VIRTIO] = 0x10013000,
    [VE_SERIALDVI] = 0x10016000,
    [VE_RTC] = 0x10017000,
    [VE_COMPACTFLASH] = 0x1001a000,
    [VE_CLCD] = 0x1001f000,
    /* CS0: 0x40000000 .. 0x44000000 */
    [VE_NORFLASH0] = 0x40000000,
    /* CS1: 0x44000000 .. 0x48000000 */
    [VE_NORFLASH1] = 0x44000000,
    /* CS2: 0x48000000 .. 0x4a000000 */
    [VE_SRAM] = 0x48000000,
    /* CS3: 0x4c000000 .. 0x50000000 */
    [VE_VIDEORAM] = 0x4c000000,
    [VE_ETHERNET] = 0x4e000000,
    [VE_USB] = 0x4f000000,
};

static hwaddr motherboard_aseries_map[] = {
    [VE_NORFLASHALIAS] = 0,
    /* CS0: 0x08000000 .. 0x0c000000 */
    [VE_NORFLASH0] = 0x08000000,
    /* CS4: 0x0c000000 .. 0x10000000 */
    [VE_NORFLASH1] = 0x0c000000,
    /* CS5: 0x10000000 .. 0x14000000 */
    /* CS1: 0x14000000 .. 0x18000000 */
    [VE_SRAM] = 0x14000000,
    /* CS2: 0x18000000 .. 0x1c000000 */
    [VE_VIDEORAM] = 0x18000000,
    [VE_ETHERNET] = 0x1a000000,
    [VE_USB] = 0x1b000000,
    /* CS3: 0x1c000000 .. 0x20000000 */
    [VE_DAPROM] = 0x1c000000,
    [VE_SYSREGS] = 0x1c010000,
    [VE_SP810] = 0x1c020000,
    [VE_SERIALPCI] = 0x1c030000,
    [VE_PL041] = 0x1c040000,
    [VE_MMCI] = 0x1c050000,
    [VE_KMI0] = 0x1c060000,
    [VE_KMI1] = 0x1c070000,
    [VE_UART0] = 0x1c090000,
    [VE_UART1] = 0x1c0a0000,
    [VE_UART2] = 0x1c0b0000,
    [VE_UART3] = 0x1c0c0000,
    [VE_WDT] = 0x1c0f0000,
    [VE_TIMER01] = 0x1c110000,
    [VE_TIMER23] = 0x1c120000,
    [VE_VIRTIO] = 0x1c130000,
    [VE_SERIALDVI] = 0x1c160000,
    [VE_RTC] = 0x1c170000,
    [VE_COMPACTFLASH] = 0x1c1a0000,
    [VE_CLCD] = 0x1c1f0000,
};

/* Structure defining the peculiarities of a specific daughterboard */

typedef struct VEDBoardInfo VEDBoardInfo;

struct VexpressMachineClass {
    MachineClass parent;
    VEDBoardInfo *daughterboard;
};

struct VexpressMachineState {
    MachineState parent;
    bool secure;
    bool virt;
};

#define TYPE_VEXPRESS_MACHINE   "vexpress"
#define TYPE_VEXPRESS_A9_MACHINE   MACHINE_TYPE_NAME("vexpress-a9")
#define TYPE_VEXPRESS_A15_MACHINE   MACHINE_TYPE_NAME("vexpress-a15")
OBJECT_DECLARE_TYPE(VexpressMachineState, VexpressMachineClass, VEXPRESS_MACHINE)

typedef void DBoardInitFn(const VexpressMachineState *machine,
                          ram_addr_t ram_size,
                          const char *cpu_type,
                          qemu_irq *pic);

struct VEDBoardInfo {
    struct arm_boot_info bootinfo;
    const hwaddr *motherboard_map;
    hwaddr loader_start;
    const hwaddr gic_cpu_if_addr;
    uint32_t proc_id;
    uint32_t num_voltage_sensors;
    const uint32_t *voltages;
    uint32_t num_clocks;
    const uint32_t *clocks;
    DBoardInitFn *init;
};

static void init_cpus(MachineState *ms, const char *cpu_type,
                      const char *privdev, hwaddr periphbase,
                      qemu_irq *pic, bool secure, bool virt)
{
    DeviceState *dev;
    SysBusDevice *busdev;
    int n;
    unsigned int smp_cpus = ms->smp.cpus;

    /* Create the actual CPUs */
    for (n = 0; n < smp_cpus; n++) {
        Object *cpuobj = object_new(cpu_type);

        if (!secure) {
            object_property_set_bool(cpuobj, "has_el3", false, NULL);
        }
        if (!virt) {
            if (object_property_find(cpuobj, "has_el2")) {
                object_property_set_bool(cpuobj, "has_el2", false, NULL);
            }
        }

        if (object_property_find(cpuobj, "reset-cbar")) {
            object_property_set_int(cpuobj, "reset-cbar", periphbase,
                                    &error_abort);
        }
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
    }

    /* Create the private peripheral devices (including the GIC);
     * this must happen after the CPUs are created because a15mpcore_priv
     * wires itself up to the CPU's generic_timer gpio out lines.
     */
    dev = qdev_new(privdev);
    qdev_prop_set_uint32(dev, "num-cpu", smp_cpus);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, periphbase);

    /* Interrupts [42:0] are from the motherboard;
     * [47:43] are reserved; [63:48] are daughterboard
     * peripherals. Note that some documentation numbers
     * external interrupts starting from 32 (because there
     * are internal interrupts 0..31).
     */
    for (n = 0; n < 64; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    /* Connect the CPUs to the GIC */
    for (n = 0; n < smp_cpus; n++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(n));

        sysbus_connect_irq(busdev, n, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(busdev, n + smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(busdev, n + 2 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(busdev, n + 3 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }
}

static void a9_daughterboard_init(const VexpressMachineState *vms,
                                  ram_addr_t ram_size,
                                  const char *cpu_type,
                                  qemu_irq *pic)
{
    MachineState *machine = MACHINE(vms);
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *lowram = g_new(MemoryRegion, 1);
    ram_addr_t low_ram_size;

    if (ram_size > 0x40000000) {
        /* 1GB is the maximum the address space permits */
        error_report("vexpress-a9: cannot model more than 1GB RAM");
        exit(1);
    }

    low_ram_size = ram_size;
    if (low_ram_size > 0x4000000) {
        low_ram_size = 0x4000000;
    }
    /* RAM is from 0x60000000 upwards. The bottom 64MB of the
     * address space should in theory be remappable to various
     * things including ROM or RAM; we always map the RAM there.
     */
    memory_region_init_alias(lowram, NULL, "vexpress.lowmem", machine->ram,
                             0, low_ram_size);
    memory_region_add_subregion(sysmem, 0x0, lowram);
    memory_region_add_subregion(sysmem, 0x60000000, machine->ram);

    /* 0x1e000000 A9MPCore (SCU) private memory region */
    init_cpus(machine, cpu_type, TYPE_A9MPCORE_PRIV, 0x1e000000, pic,
              vms->secure, vms->virt);

    /* Daughterboard peripherals : 0x10020000 .. 0x20000000 */

    /* 0x10020000 PL111 CLCD (daughterboard) */
    sysbus_create_simple("pl111", 0x10020000, pic[44]);

    /* 0x10060000 AXI RAM */
    /* 0x100e0000 PL341 Dynamic Memory Controller */
    /* 0x100e1000 PL354 Static Memory Controller */
    /* 0x100e2000 System Configuration Controller */

    sysbus_create_simple("sp804", 0x100e4000, pic[48]);
    /* 0x100e5000 SP805 Watchdog module */
    /* 0x100e6000 BP147 TrustZone Protection Controller */
    /* 0x100e9000 PL301 'Fast' AXI matrix */
    /* 0x100ea000 PL301 'Slow' AXI matrix */
    /* 0x100ec000 TrustZone Address Space Controller */
    /* 0x10200000 CoreSight debug APB */
    /* 0x1e00a000 PL310 L2 Cache Controller */
    sysbus_create_varargs("l2x0", 0x1e00a000, NULL);
}

/* Voltage values for SYS_CFG_VOLT daughterboard registers;
 * values are in microvolts.
 */
static const uint32_t a9_voltages[] = {
    1000000, /* VD10 : 1.0V : SoC internal logic voltage */
    1000000, /* VD10_S2 : 1.0V : PL310, L2 cache, RAM, non-PL310 logic */
    1000000, /* VD10_S3 : 1.0V : Cortex-A9, cores, MPEs, SCU, PL310 logic */
    1800000, /* VCC1V8 : 1.8V : DDR2 SDRAM, test chip DDR2 I/O supply */
    900000, /* DDR2VTT : 0.9V : DDR2 SDRAM VTT termination voltage */
    3300000, /* VCC3V3 : 3.3V : local board supply for misc external logic */
};

/* Reset values for daughterboard oscillators (in Hz) */
static const uint32_t a9_clocks[] = {
    45000000, /* AMBA AXI ACLK: 45MHz */
    23750000, /* daughterboard CLCD clock: 23.75MHz */
    66670000, /* Test chip reference clock: 66.67MHz */
};

static VEDBoardInfo a9_daughterboard = {
    .motherboard_map = motherboard_legacy_map,
    .loader_start = 0x60000000,
    .gic_cpu_if_addr = 0x1e000100,
    .proc_id = 0x0c000191,
    .num_voltage_sensors = ARRAY_SIZE(a9_voltages),
    .voltages = a9_voltages,
    .num_clocks = ARRAY_SIZE(a9_clocks),
    .clocks = a9_clocks,
    .init = a9_daughterboard_init,
};

static void a15_daughterboard_init(const VexpressMachineState *vms,
                                   ram_addr_t ram_size,
                                   const char *cpu_type,
                                   qemu_irq *pic)
{
    MachineState *machine = MACHINE(vms);
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);

    {
        /* We have to use a separate 64 bit variable here to avoid the gcc
         * "comparison is always false due to limited range of data type"
         * warning if we are on a host where ram_addr_t is 32 bits.
         */
        uint64_t rsz = ram_size;
        if (rsz > (30ULL * 1024 * 1024 * 1024)) {
            error_report("vexpress-a15: cannot model more than 30GB RAM");
            exit(1);
        }
    }

    /* RAM is from 0x80000000 upwards; there is no low-memory alias for it. */
    memory_region_add_subregion(sysmem, 0x80000000, machine->ram);

    /* 0x2c000000 A15MPCore private memory region (GIC) */
    init_cpus(machine, cpu_type, TYPE_A15MPCORE_PRIV,
              0x2c000000, pic, vms->secure, vms->virt);

    /* A15 daughterboard peripherals: */

    /* 0x20000000: CoreSight interfaces: not modelled */
    /* 0x2a000000: PL301 AXI interconnect: not modelled */
    /* 0x2a420000: SCC: not modelled */
    /* 0x2a430000: system counter: not modelled */
    /* 0x2b000000: HDLCD controller: not modelled */
    /* 0x2b060000: SP805 watchdog: not modelled */
    /* 0x2b0a0000: PL341 dynamic memory controller: not modelled */
    /* 0x2e000000: system SRAM */
    memory_region_init_ram(sram, NULL, "vexpress.a15sram", 0x10000,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0x2e000000, sram);

    /* 0x7ffb0000: DMA330 DMA controller: not modelled */
    /* 0x7ffd0000: PL354 static memory controller: not modelled */
}

static const uint32_t a15_voltages[] = {
    900000, /* Vcore: 0.9V : CPU core voltage */
};

static const uint32_t a15_clocks[] = {
    60000000, /* OSCCLK0: 60MHz : CPU_CLK reference */
    0, /* OSCCLK1: reserved */
    0, /* OSCCLK2: reserved */
    0, /* OSCCLK3: reserved */
    40000000, /* OSCCLK4: 40MHz : external AXI master clock */
    23750000, /* OSCCLK5: 23.75MHz : HDLCD PLL reference */
    50000000, /* OSCCLK6: 50MHz : static memory controller clock */
    60000000, /* OSCCLK7: 60MHz : SYSCLK reference */
    40000000, /* OSCCLK8: 40MHz : DDR2 PLL reference */
};

static VEDBoardInfo a15_daughterboard = {
    .motherboard_map = motherboard_aseries_map,
    .loader_start = 0x80000000,
    .gic_cpu_if_addr = 0x2c002000,
    .proc_id = 0x14000237,
    .num_voltage_sensors = ARRAY_SIZE(a15_voltages),
    .voltages = a15_voltages,
    .num_clocks = ARRAY_SIZE(a15_clocks),
    .clocks = a15_clocks,
    .init = a15_daughterboard_init,
};

static int add_virtio_mmio_node(void *fdt, uint32_t acells, uint32_t scells,
                                hwaddr addr, hwaddr size, uint32_t intc,
                                int irq)
{
    /* Add a virtio_mmio node to the device tree blob:
     *   virtio_mmio@ADDRESS {
     *       compatible = "virtio,mmio";
     *       reg = <ADDRESS, SIZE>;
     *       interrupt-parent = <&intc>;
     *       interrupts = <0, irq, 1>;
     *   }
     * (Note that the format of the interrupts property is dependent on the
     * interrupt controller that interrupt-parent points to; these are for
     * the ARM GIC and indicate an SPI interrupt, rising-edge-triggered.)
     */
    int rc;
    char *nodename = g_strdup_printf("/virtio_mmio@%" PRIx64, addr);

    rc = qemu_fdt_add_subnode(fdt, nodename);
    rc |= qemu_fdt_setprop_string(fdt, nodename,
                                  "compatible", "virtio,mmio");
    rc |= qemu_fdt_setprop_sized_cells(fdt, nodename, "reg",
                                       acells, addr, scells, size);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-parent", intc);
    qemu_fdt_setprop_cells(fdt, nodename, "interrupts", 0, irq, 1);
    qemu_fdt_setprop(fdt, nodename, "dma-coherent", NULL, 0);
    g_free(nodename);
    if (rc) {
        return -1;
    }
    return 0;
}

static uint32_t find_int_controller(void *fdt)
{
    /* Find the FDT node corresponding to the interrupt controller
     * for virtio-mmio devices. We do this by scanning the fdt for
     * a node with the right compatibility, since we know there is
     * only one GIC on a vexpress board.
     * We return the phandle of the node, or 0 if none was found.
     */
    const char *compat = "arm,cortex-a9-gic";
    int offset;

    offset = fdt_node_offset_by_compatible(fdt, -1, compat);
    if (offset >= 0) {
        return fdt_get_phandle(fdt, offset);
    }
    return 0;
}

static void vexpress_modify_dtb(const struct arm_boot_info *info, void *fdt)
{
    uint32_t acells, scells, intc;
    const VEDBoardInfo *daughterboard = (const VEDBoardInfo *)info;

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells",
                                   NULL, &error_fatal);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells",
                                   NULL, &error_fatal);
    intc = find_int_controller(fdt);
    if (!intc) {
        /* Not fatal, we just won't provide virtio. This will
         * happen with older device tree blobs.
         */
        warn_report("couldn't find interrupt controller in "
                    "dtb; will not include virtio-mmio devices in the dtb");
    } else {
        int i;
        const hwaddr *map = daughterboard->motherboard_map;

        /* We iterate backwards here because adding nodes
         * to the dtb puts them in last-first.
         */
        for (i = NUM_VIRTIO_TRANSPORTS - 1; i >= 0; i--) {
            add_virtio_mmio_node(fdt, acells, scells,
                                 map[VE_VIRTIO] + 0x200 * i,
                                 0x200, intc, 40 + i);
        }
    }
}


/* Open code a private version of pflash registration since we
 * need to set non-default device width for VExpress platform.
 */
static PFlashCFI01 *ve_pflash_cfi01_register(hwaddr base, const char *name,
                                             DriveInfo *di)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    if (di) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(di));
    }

    qdev_prop_set_uint32(dev, "num-blocks",
                         VEXPRESS_FLASH_SIZE / VEXPRESS_FLASH_SECT_SIZE);
    qdev_prop_set_uint64(dev, "sector-length", VEXPRESS_FLASH_SECT_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    return PFLASH_CFI01(dev);
}

static void vexpress_common_init(MachineState *machine)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(machine);
    VexpressMachineClass *vmc = VEXPRESS_MACHINE_GET_CLASS(machine);
    VEDBoardInfo *daughterboard = vmc->daughterboard;
    DeviceState *dev, *sysctl, *pl041;
    qemu_irq pic[64];
    uint32_t sys_id;
    DriveInfo *dinfo;
    PFlashCFI01 *pflash0;
    I2CBus *i2c;
    ram_addr_t vram_size, sram_size;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *vram = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flashalias = g_new(MemoryRegion, 1);
    MemoryRegion *flash0mem;
    const hwaddr *map = daughterboard->motherboard_map;
    int i;

    daughterboard->init(vms, machine->ram_size, machine->cpu_type, pic);

    /*
     * If a bios file was provided, attempt to map it into memory
     */
    if (machine->firmware) {
        char *fn;
        int image_size;

        if (drive_get(IF_PFLASH, 0, 0)) {
            error_report("The contents of the first flash device may be "
                         "specified with -bios or with -drive if=pflash... "
                         "but you cannot use both options at once");
            exit(1);
        }
        fn = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
        if (!fn) {
            error_report("Could not find ROM image '%s'", machine->firmware);
            exit(1);
        }
        image_size = load_image_targphys(fn, map[VE_NORFLASH0],
                                         VEXPRESS_FLASH_SIZE);
        g_free(fn);
        if (image_size < 0) {
            error_report("Could not load ROM image '%s'", machine->firmware);
            exit(1);
        }
    }

    /* Motherboard peripherals: the wiring is the same but the
     * addresses vary between the legacy and A-Series memory maps.
     */

    sys_id = 0x1190f500;

    sysctl = qdev_new("realview_sysctl");
    qdev_prop_set_uint32(sysctl, "sys_id", sys_id);
    qdev_prop_set_uint32(sysctl, "proc_id", daughterboard->proc_id);
    qdev_prop_set_uint32(sysctl, "len-db-voltage",
                         daughterboard->num_voltage_sensors);
    for (i = 0; i < daughterboard->num_voltage_sensors; i++) {
        char *propname = g_strdup_printf("db-voltage[%d]", i);
        qdev_prop_set_uint32(sysctl, propname, daughterboard->voltages[i]);
        g_free(propname);
    }
    qdev_prop_set_uint32(sysctl, "len-db-clock",
                         daughterboard->num_clocks);
    for (i = 0; i < daughterboard->num_clocks; i++) {
        char *propname = g_strdup_printf("db-clock[%d]", i);
        qdev_prop_set_uint32(sysctl, propname, daughterboard->clocks[i]);
        g_free(propname);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(sysctl), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(sysctl), 0, map[VE_SYSREGS]);

    /* VE_SP810: not modelled */
    /* VE_SERIALPCI: not modelled */

    pl041 = qdev_new("pl041");
    qdev_prop_set_uint32(pl041, "nc_fifo_depth", 512);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pl041), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(pl041), 0, map[VE_PL041]);
    sysbus_connect_irq(SYS_BUS_DEVICE(pl041), 0, pic[11]);

    dev = sysbus_create_varargs("pl181", map[VE_MMCI], pic[9], pic[10], NULL);
    /* Wire up MMC card detect and read-only signals */
    qdev_connect_gpio_out_named(dev, "card-read-only", 0,
                          qdev_get_gpio_in(sysctl, ARM_SYSCTL_GPIO_MMC_WPROT));
    qdev_connect_gpio_out_named(dev, "card-inserted", 0,
                          qdev_get_gpio_in(sysctl, ARM_SYSCTL_GPIO_MMC_CARDIN));
    dinfo = drive_get_next(IF_SD);
    if (dinfo) {
        DeviceState *card;

        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card, qdev_get_child_bus(dev, "sd-bus"),
                               &error_fatal);
    }

    sysbus_create_simple("pl050_keyboard", map[VE_KMI0], pic[12]);
    sysbus_create_simple("pl050_mouse", map[VE_KMI1], pic[13]);

    pl011_create(map[VE_UART0], pic[5], serial_hd(0));
    pl011_create(map[VE_UART1], pic[6], serial_hd(1));
    pl011_create(map[VE_UART2], pic[7], serial_hd(2));
    pl011_create(map[VE_UART3], pic[8], serial_hd(3));

    sysbus_create_simple("sp804", map[VE_TIMER01], pic[2]);
    sysbus_create_simple("sp804", map[VE_TIMER23], pic[3]);

    dev = sysbus_create_simple(TYPE_VERSATILE_I2C, map[VE_SERIALDVI], NULL);
    i2c = (I2CBus *)qdev_get_child_bus(dev, "i2c");
    i2c_slave_create_simple(i2c, "sii9022", 0x39);

    sysbus_create_simple("pl031", map[VE_RTC], pic[4]); /* RTC */

    /* VE_COMPACTFLASH: not modelled */

    sysbus_create_simple("pl111", map[VE_CLCD], pic[14]);

    dinfo = drive_get_next(IF_PFLASH);
    pflash0 = ve_pflash_cfi01_register(map[VE_NORFLASH0], "vexpress.flash0",
                                       dinfo);
    if (!pflash0) {
        error_report("vexpress: error registering flash 0");
        exit(1);
    }

    if (map[VE_NORFLASHALIAS] != -1) {
        /* Map flash 0 as an alias into low memory */
        flash0mem = sysbus_mmio_get_region(SYS_BUS_DEVICE(pflash0), 0);
        memory_region_init_alias(flashalias, NULL, "vexpress.flashalias",
                                 flash0mem, 0, VEXPRESS_FLASH_SIZE);
        memory_region_add_subregion(sysmem, map[VE_NORFLASHALIAS], flashalias);
    }

    dinfo = drive_get_next(IF_PFLASH);
    if (!ve_pflash_cfi01_register(map[VE_NORFLASH1], "vexpress.flash1",
                                  dinfo)) {
        error_report("vexpress: error registering flash 1");
        exit(1);
    }

    sram_size = 0x2000000;
    memory_region_init_ram(sram, NULL, "vexpress.sram", sram_size,
                           &error_fatal);
    memory_region_add_subregion(sysmem, map[VE_SRAM], sram);

    vram_size = 0x800000;
    memory_region_init_ram(vram, NULL, "vexpress.vram", vram_size,
                           &error_fatal);
    memory_region_add_subregion(sysmem, map[VE_VIDEORAM], vram);

    /* 0x4e000000 LAN9118 Ethernet */
    if (nd_table[0].used) {
        lan9118_init(&nd_table[0], map[VE_ETHERNET], pic[15]);
    }

    /* VE_USB: not modelled */

    /* VE_DAPROM: not modelled */

    /* Create mmio transports, so the user can create virtio backends
     * (which will be automatically plugged in to the transports). If
     * no backend is created the transport will just sit harmlessly idle.
     */
    for (i = 0; i < NUM_VIRTIO_TRANSPORTS; i++) {
        sysbus_create_simple("virtio-mmio", map[VE_VIRTIO] + 0x200 * i,
                             pic[40 + i]);
    }

    daughterboard->bootinfo.ram_size = machine->ram_size;
    daughterboard->bootinfo.nb_cpus = machine->smp.cpus;
    daughterboard->bootinfo.board_id = VEXPRESS_BOARD_ID;
    daughterboard->bootinfo.loader_start = daughterboard->loader_start;
    daughterboard->bootinfo.smp_loader_start = map[VE_SRAM];
    daughterboard->bootinfo.smp_bootreg_addr = map[VE_SYSREGS] + 0x30;
    daughterboard->bootinfo.gic_cpu_if_addr = daughterboard->gic_cpu_if_addr;
    daughterboard->bootinfo.modify_dtb = vexpress_modify_dtb;
    /* When booting Linux we should be in secure state if the CPU has one. */
    daughterboard->bootinfo.secure_boot = vms->secure;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &daughterboard->bootinfo);
}

static bool vexpress_get_secure(Object *obj, Error **errp)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(obj);

    return vms->secure;
}

static void vexpress_set_secure(Object *obj, bool value, Error **errp)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(obj);

    vms->secure = value;
}

static bool vexpress_get_virt(Object *obj, Error **errp)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(obj);

    return vms->virt;
}

static void vexpress_set_virt(Object *obj, bool value, Error **errp)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(obj);

    vms->virt = value;
}

static void vexpress_instance_init(Object *obj)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(obj);

    /* EL3 is enabled by default on vexpress */
    vms->secure = true;
}

static void vexpress_a15_instance_init(Object *obj)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(obj);

    /*
     * For the vexpress-a15, EL2 is by default enabled if EL3 is,
     * but can also be specifically set to on or off.
     */
    vms->virt = true;
}

static void vexpress_a9_instance_init(Object *obj)
{
    VexpressMachineState *vms = VEXPRESS_MACHINE(obj);

    /* The A9 doesn't have the virt extensions */
    vms->virt = false;
}

static void vexpress_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ARM Versatile Express";
    mc->init = vexpress_common_init;
    mc->max_cpus = 4;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "vexpress.highmem";

    object_class_property_add_bool(oc, "secure", vexpress_get_secure,
                                   vexpress_set_secure);
    object_class_property_set_description(oc, "secure",
                                          "Set on/off to enable/disable the ARM "
                                          "Security Extensions (TrustZone)");
}

static void vexpress_a9_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    VexpressMachineClass *vmc = VEXPRESS_MACHINE_CLASS(oc);

    mc->desc = "ARM Versatile Express for Cortex-A9";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");

    vmc->daughterboard = &a9_daughterboard;
}

static void vexpress_a15_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    VexpressMachineClass *vmc = VEXPRESS_MACHINE_CLASS(oc);

    mc->desc = "ARM Versatile Express for Cortex-A15";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a15");

    vmc->daughterboard = &a15_daughterboard;

    object_class_property_add_bool(oc, "virtualization", vexpress_get_virt,
                                   vexpress_set_virt);
    object_class_property_set_description(oc, "virtualization",
                                          "Set on/off to enable/disable the ARM "
                                          "Virtualization Extensions "
                                          "(defaults to same as 'secure')");

}

static const TypeInfo vexpress_info = {
    .name = TYPE_VEXPRESS_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(VexpressMachineState),
    .instance_init = vexpress_instance_init,
    .class_size = sizeof(VexpressMachineClass),
    .class_init = vexpress_class_init,
};

static const TypeInfo vexpress_a9_info = {
    .name = TYPE_VEXPRESS_A9_MACHINE,
    .parent = TYPE_VEXPRESS_MACHINE,
    .class_init = vexpress_a9_class_init,
    .instance_init = vexpress_a9_instance_init,
};

static const TypeInfo vexpress_a15_info = {
    .name = TYPE_VEXPRESS_A15_MACHINE,
    .parent = TYPE_VEXPRESS_MACHINE,
    .class_init = vexpress_a15_class_init,
    .instance_init = vexpress_a15_instance_init,
};

static void vexpress_machine_init(void)
{
    type_register_static(&vexpress_info);
    type_register_static(&vexpress_a9_info);
    type_register_static(&vexpress_a15_info);
}

type_init(vexpress_machine_init);
