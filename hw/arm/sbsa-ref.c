/*
 * ARM SBSA Reference Platform emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Written by Hongbo Zhang <hongbo.zhang@linaro.org>
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
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "sysemu/device_tree.h"
#include "sysemu/kvm.h"
#include "sysemu/numa.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "exec/hwaddr.h"
#include "kvm_arm.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/arm/fdt.h"
#include "hw/arm/smmuv3.h"
#include "hw/block/flash.h"
#include "hw/boards.h"
#include "hw/ide/ide-bus.h"
#include "hw/ide/ahci-sysbus.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/intc/arm_gicv3_its_common.h"
#include "hw/loader.h"
#include "hw/pci-host/gpex.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "hw/usb/xhci.h"
#include "hw/char/pl011.h"
#include "hw/watchdog/sbsa_gwdt.h"
#include "net/net.h"
#include "qapi/qmp/qlist.h"
#include "qom/object.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"

#define RAMLIMIT_GB 8192
#define RAMLIMIT_BYTES (RAMLIMIT_GB * GiB)

#define NUM_IRQS        256
#define NUM_SMMU_IRQS   4
#define NUM_SATA_PORTS  6

/*
 * Generic timer frequency in Hz (which drives both the CPU generic timers
 * and the SBSA watchdog-timer). Older (<2.11) versions of the TF-A firmware
 * assumed 62.5MHz here.
 *
 * Starting with Armv8.6 CPU 1GHz timer frequency is mandated.
 */
#define SBSA_GTIMER_HZ 1000000000

enum {
    SBSA_FLASH,
    SBSA_MEM,
    SBSA_CPUPERIPHS,
    SBSA_GIC_DIST,
    SBSA_GIC_REDIST,
    SBSA_GIC_ITS,
    SBSA_SECURE_EC,
    SBSA_GWDT_WS0,
    SBSA_GWDT_REFRESH,
    SBSA_GWDT_CONTROL,
    SBSA_SMMU,
    SBSA_UART,
    SBSA_RTC,
    SBSA_PCIE,
    SBSA_PCIE_MMIO,
    SBSA_PCIE_MMIO_HIGH,
    SBSA_PCIE_PIO,
    SBSA_PCIE_ECAM,
    SBSA_GPIO,
    SBSA_SECURE_UART,
    SBSA_SECURE_UART_MM,
    SBSA_SECURE_MEM,
    SBSA_AHCI,
    SBSA_XHCI,
};

struct SBSAMachineState {
    MachineState parent;
    struct arm_boot_info bootinfo;
    int smp_cpus;
    void *fdt;
    int fdt_size;
    int psci_conduit;
    DeviceState *gic;
    PFlashCFI01 *flash[2];
};

#define TYPE_SBSA_MACHINE   MACHINE_TYPE_NAME("sbsa-ref")
OBJECT_DECLARE_SIMPLE_TYPE(SBSAMachineState, SBSA_MACHINE)

static const MemMapEntry sbsa_ref_memmap[] = {
    /* 512M boot ROM */
    [SBSA_FLASH] =              {          0, 0x20000000 },
    /* 512M secure memory */
    [SBSA_SECURE_MEM] =         { 0x20000000, 0x20000000 },
    /* Space reserved for CPU peripheral devices */
    [SBSA_CPUPERIPHS] =         { 0x40000000, 0x00040000 },
    [SBSA_GIC_DIST] =           { 0x40060000, 0x00010000 },
    [SBSA_GIC_REDIST] =         { 0x40080000, 0x04000000 },
    [SBSA_GIC_ITS] =            { 0x44081000, 0x00020000 },
    [SBSA_SECURE_EC] =          { 0x50000000, 0x00001000 },
    [SBSA_GWDT_REFRESH] =       { 0x50010000, 0x00001000 },
    [SBSA_GWDT_CONTROL] =       { 0x50011000, 0x00001000 },
    [SBSA_UART] =               { 0x60000000, 0x00001000 },
    [SBSA_RTC] =                { 0x60010000, 0x00001000 },
    [SBSA_GPIO] =               { 0x60020000, 0x00001000 },
    [SBSA_SECURE_UART] =        { 0x60030000, 0x00001000 },
    [SBSA_SECURE_UART_MM] =     { 0x60040000, 0x00001000 },
    [SBSA_SMMU] =               { 0x60050000, 0x00020000 },
    /* Space here reserved for more SMMUs */
    [SBSA_AHCI] =               { 0x60100000, 0x00010000 },
    [SBSA_XHCI] =               { 0x60110000, 0x00010000 },
    /* Space here reserved for other devices */
    [SBSA_PCIE_PIO] =           { 0x7fff0000, 0x00010000 },
    /* 32-bit address PCIE MMIO space */
    [SBSA_PCIE_MMIO] =          { 0x80000000, 0x70000000 },
    /* 256M PCIE ECAM space */
    [SBSA_PCIE_ECAM] =          { 0xf0000000, 0x10000000 },
    /* ~1TB PCIE MMIO space (4GB to 1024GB boundary) */
    [SBSA_PCIE_MMIO_HIGH] =     { 0x100000000ULL, 0xFF00000000ULL },
    [SBSA_MEM] =                { 0x10000000000ULL, RAMLIMIT_BYTES },
};

static const int sbsa_ref_irqmap[] = {
    [SBSA_UART] = 1,
    [SBSA_RTC] = 2,
    [SBSA_PCIE] = 3, /* ... to 6 */
    [SBSA_GPIO] = 7,
    [SBSA_SECURE_UART] = 8,
    [SBSA_SECURE_UART_MM] = 9,
    [SBSA_AHCI] = 10,
    [SBSA_XHCI] = 11,
    [SBSA_SMMU] = 12, /* ... to 15 */
    [SBSA_GWDT_WS0] = 16,
};

static uint64_t sbsa_ref_cpu_mp_affinity(SBSAMachineState *sms, int idx)
{
    uint8_t clustersz = ARM_DEFAULT_CPUS_PER_CLUSTER;
    return arm_build_mp_affinity(idx, clustersz);
}

static void sbsa_fdt_add_gic_node(SBSAMachineState *sms)
{
    const char *intc_nodename = "/intc";
    const char *its_nodename = "/intc/its";

    qemu_fdt_add_subnode(sms->fdt, intc_nodename);
    qemu_fdt_setprop_sized_cells(sms->fdt, intc_nodename, "reg",
                                 2, sbsa_ref_memmap[SBSA_GIC_DIST].base,
                                 2, sbsa_ref_memmap[SBSA_GIC_DIST].size,
                                 2, sbsa_ref_memmap[SBSA_GIC_REDIST].base,
                                 2, sbsa_ref_memmap[SBSA_GIC_REDIST].size);

    qemu_fdt_add_subnode(sms->fdt, its_nodename);
    qemu_fdt_setprop_sized_cells(sms->fdt, its_nodename, "reg",
                                 2, sbsa_ref_memmap[SBSA_GIC_ITS].base,
                                 2, sbsa_ref_memmap[SBSA_GIC_ITS].size);
}

/*
 * Firmware on this machine only uses ACPI table to load OS, these limited
 * device tree nodes are just to let firmware know the info which varies from
 * command line parameters, so it is not necessary to be fully compatible
 * with the kernel CPU and NUMA binding rules.
 */
static void create_fdt(SBSAMachineState *sms)
{
    void *fdt = create_device_tree(&sms->fdt_size);
    const MachineState *ms = MACHINE(sms);
    int nb_numa_nodes = ms->numa_state->num_nodes;
    int cpu;

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    sms->fdt = fdt;

    qemu_fdt_setprop_string(fdt, "/", "compatible", "linux,sbsa-ref");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);

    /*
     * This versioning scheme is for informing platform fw only. It is neither:
     * - A QEMU versioned machine type; a given version of QEMU will emulate
     *   a given version of the platform.
     * - A reflection of level of SBSA (now SystemReady SR) support provided.
     *
     * machine-version-major: updated when changes breaking fw compatibility
     *                        are introduced.
     * machine-version-minor: updated when features are added that don't break
     *                        fw compatibility.
     */
    qemu_fdt_setprop_cell(fdt, "/", "machine-version-major", 0);
    qemu_fdt_setprop_cell(fdt, "/", "machine-version-minor", 4);

    if (ms->numa_state->have_numa_distance) {
        int size = nb_numa_nodes * nb_numa_nodes * 3 * sizeof(uint32_t);
        uint32_t *matrix = g_malloc0(size);
        int idx, i, j;

        for (i = 0; i < nb_numa_nodes; i++) {
            for (j = 0; j < nb_numa_nodes; j++) {
                idx = (i * nb_numa_nodes + j) * 3;
                matrix[idx + 0] = cpu_to_be32(i);
                matrix[idx + 1] = cpu_to_be32(j);
                matrix[idx + 2] =
                    cpu_to_be32(ms->numa_state->nodes[i].distance[j]);
            }
        }

        qemu_fdt_add_subnode(fdt, "/distance-map");
        qemu_fdt_setprop(fdt, "/distance-map", "distance-matrix",
                         matrix, size);
        g_free(matrix);
    }

    /*
     * From Documentation/devicetree/bindings/arm/cpus.yaml
     *  On ARM v8 64-bit systems this property is required
     *    and matches the MPIDR_EL1 register affinity bits.
     *
     *    * If cpus node's #address-cells property is set to 2
     *
     *      The first reg cell bits [7:0] must be set to
     *      bits [39:32] of MPIDR_EL1.
     *
     *      The second reg cell bits [23:0] must be set to
     *      bits [23:0] of MPIDR_EL1.
     */
    qemu_fdt_add_subnode(sms->fdt, "/cpus");
    qemu_fdt_setprop_cell(sms->fdt, "/cpus", "#address-cells", 2);
    qemu_fdt_setprop_cell(sms->fdt, "/cpus", "#size-cells", 0x0);

    for (cpu = sms->smp_cpus - 1; cpu >= 0; cpu--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));
        CPUState *cs = CPU(armcpu);
        uint64_t mpidr = sbsa_ref_cpu_mp_affinity(sms, cpu);

        qemu_fdt_add_subnode(sms->fdt, nodename);
        qemu_fdt_setprop_u64(sms->fdt, nodename, "reg", mpidr);

        if (ms->possible_cpus->cpus[cs->cpu_index].props.has_node_id) {
            qemu_fdt_setprop_cell(sms->fdt, nodename, "numa-node-id",
                ms->possible_cpus->cpus[cs->cpu_index].props.node_id);
        }

        g_free(nodename);
    }

    /* Add CPU topology description through fdt node topology. */
    qemu_fdt_add_subnode(sms->fdt, "/cpus/topology");

    qemu_fdt_setprop_cell(sms->fdt, "/cpus/topology", "sockets", ms->smp.sockets);
    qemu_fdt_setprop_cell(sms->fdt, "/cpus/topology", "clusters", ms->smp.clusters);
    qemu_fdt_setprop_cell(sms->fdt, "/cpus/topology", "cores", ms->smp.cores);
    qemu_fdt_setprop_cell(sms->fdt, "/cpus/topology", "threads", ms->smp.threads);

    sbsa_fdt_add_gic_node(sms);
}

#define SBSA_FLASH_SECTOR_SIZE (256 * KiB)

static PFlashCFI01 *sbsa_flash_create1(SBSAMachineState *sms,
                                        const char *name,
                                        const char *alias_prop_name)
{
    /*
     * Create a single flash device.  We use the same parameters as
     * the flash devices on the Versatile Express board.
     */
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", SBSA_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);
    object_property_add_child(OBJECT(sms), name, OBJECT(dev));
    object_property_add_alias(OBJECT(sms), alias_prop_name,
                              OBJECT(dev), "drive");
    return PFLASH_CFI01(dev);
}

static void sbsa_flash_create(SBSAMachineState *sms)
{
    sms->flash[0] = sbsa_flash_create1(sms, "sbsa.flash0", "pflash0");
    sms->flash[1] = sbsa_flash_create1(sms, "sbsa.flash1", "pflash1");
}

static void sbsa_flash_map1(PFlashCFI01 *flash,
                            hwaddr base, hwaddr size,
                            MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);

    assert(QEMU_IS_ALIGNED(size, SBSA_FLASH_SECTOR_SIZE));
    assert(size / SBSA_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", size / SBSA_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       0));
}

static void sbsa_flash_map(SBSAMachineState *sms,
                           MemoryRegion *sysmem,
                           MemoryRegion *secure_sysmem)
{
    /*
     * Map two flash devices to fill the SBSA_FLASH space in the memmap.
     * sysmem is the system memory space. secure_sysmem is the secure view
     * of the system, and the first flash device should be made visible only
     * there. The second flash device is visible to both secure and nonsecure.
     */
    hwaddr flashsize = sbsa_ref_memmap[SBSA_FLASH].size / 2;
    hwaddr flashbase = sbsa_ref_memmap[SBSA_FLASH].base;

    sbsa_flash_map1(sms->flash[0], flashbase, flashsize,
                    secure_sysmem);
    sbsa_flash_map1(sms->flash[1], flashbase + flashsize, flashsize,
                    sysmem);
}

static bool sbsa_firmware_init(SBSAMachineState *sms,
                               MemoryRegion *sysmem,
                               MemoryRegion *secure_sysmem)
{
    const char *bios_name;
    int i;
    BlockBackend *pflash_blk0;

    /* Map legacy -drive if=pflash to machine properties */
    for (i = 0; i < ARRAY_SIZE(sms->flash); i++) {
        pflash_cfi01_legacy_drive(sms->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }

    sbsa_flash_map(sms, sysmem, secure_sysmem);

    pflash_blk0 = pflash_cfi01_get_blk(sms->flash[0]);

    bios_name = MACHINE(sms)->firmware;
    if (bios_name) {
        char *fname;
        MemoryRegion *mr;
        int image_size;

        if (pflash_blk0) {
            error_report("The contents of the first flash device may be "
                         "specified with -bios or with -drive if=pflash... "
                         "but you cannot use both options at once");
            exit(1);
        }

        /* Fall back to -bios */

        fname = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (!fname) {
            error_report("Could not find ROM image '%s'", bios_name);
            exit(1);
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(sms->flash[0]), 0);
        image_size = load_image_mr(fname, mr);
        g_free(fname);
        if (image_size < 0) {
            error_report("Could not load ROM image '%s'", bios_name);
            exit(1);
        }
    }

    return pflash_blk0 || bios_name;
}

static void create_secure_ram(SBSAMachineState *sms,
                              MemoryRegion *secure_sysmem)
{
    MemoryRegion *secram = g_new(MemoryRegion, 1);
    hwaddr base = sbsa_ref_memmap[SBSA_SECURE_MEM].base;
    hwaddr size = sbsa_ref_memmap[SBSA_SECURE_MEM].size;

    memory_region_init_ram(secram, NULL, "sbsa-ref.secure-ram", size,
                           &error_fatal);
    memory_region_add_subregion(secure_sysmem, base, secram);
}

static void create_its(SBSAMachineState *sms)
{
    const char *itsclass = its_class_name();
    DeviceState *dev;

    dev = qdev_new(itsclass);

    object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(sms->gic),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, sbsa_ref_memmap[SBSA_GIC_ITS].base);
}

static void create_gic(SBSAMachineState *sms, MemoryRegion *mem)
{
    unsigned int smp_cpus = MACHINE(sms)->smp.cpus;
    SysBusDevice *gicbusdev;
    const char *gictype;
    uint32_t redist0_capacity, redist0_count;
    QList *redist_region_count;
    int i;

    gictype = gicv3_class_name();

    sms->gic = qdev_new(gictype);
    qdev_prop_set_uint32(sms->gic, "revision", 3);
    qdev_prop_set_uint32(sms->gic, "num-cpu", smp_cpus);
    /*
     * Note that the num-irq property counts both internal and external
     * interrupts; there are always 32 of the former (mandated by GIC spec).
     */
    qdev_prop_set_uint32(sms->gic, "num-irq", NUM_IRQS + 32);
    qdev_prop_set_bit(sms->gic, "has-security-extensions", true);

    redist0_capacity =
                sbsa_ref_memmap[SBSA_GIC_REDIST].size / GICV3_REDIST_SIZE;
    redist0_count = MIN(smp_cpus, redist0_capacity);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, redist0_count);
    qdev_prop_set_array(sms->gic, "redist-region-count", redist_region_count);

    object_property_set_link(OBJECT(sms->gic), "sysmem",
                             OBJECT(mem), &error_fatal);
    qdev_prop_set_bit(sms->gic, "has-lpi", true);

    gicbusdev = SYS_BUS_DEVICE(sms->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0, sbsa_ref_memmap[SBSA_GIC_DIST].base);
    sysbus_mmio_map(gicbusdev, 1, sbsa_ref_memmap[SBSA_GIC_REDIST].base);

    /*
     * Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < smp_cpus; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int intidbase = NUM_IRQS + i * GIC_INTERNAL;
        int irq;
        /*
         * Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs used for this board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
            [GTIMER_HYPVIRT] = ARCH_TIMER_NS_EL2_VIRT_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(sms->gic,
                                                   intidbase + timer_irq[irq]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(sms->gic,
                                                     intidbase
                                                     + ARCH_GIC_MAINT_IRQ));

        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(sms->gic,
                                                     intidbase
                                                     + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }
    create_its(sms);
}

static void create_uart(const SBSAMachineState *sms, int uart,
                        MemoryRegion *mem, Chardev *chr)
{
    hwaddr base = sbsa_ref_memmap[uart].base;
    int irq = sbsa_ref_irqmap[uart];
    DeviceState *dev = qdev_new(TYPE_PL011);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);

    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(mem, base,
                                sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(sms->gic, irq));
}

static void create_rtc(const SBSAMachineState *sms)
{
    hwaddr base = sbsa_ref_memmap[SBSA_RTC].base;
    int irq = sbsa_ref_irqmap[SBSA_RTC];

    sysbus_create_simple("pl031", base, qdev_get_gpio_in(sms->gic, irq));
}

static void create_wdt(const SBSAMachineState *sms)
{
    hwaddr rbase = sbsa_ref_memmap[SBSA_GWDT_REFRESH].base;
    hwaddr cbase = sbsa_ref_memmap[SBSA_GWDT_CONTROL].base;
    DeviceState *dev = qdev_new(TYPE_WDT_SBSA);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    int irq = sbsa_ref_irqmap[SBSA_GWDT_WS0];

    qdev_prop_set_uint64(dev, "clock-frequency", SBSA_GTIMER_HZ);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, rbase);
    sysbus_mmio_map(s, 1, cbase);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(sms->gic, irq));
}

static DeviceState *gpio_key_dev;
static void sbsa_ref_powerdown_req(Notifier *n, void *opaque)
{
    /* use gpio Pin 3 for power button event */
    qemu_set_irq(qdev_get_gpio_in(gpio_key_dev, 0), 1);
}

static Notifier sbsa_ref_powerdown_notifier = {
    .notify = sbsa_ref_powerdown_req
};

static void create_gpio(const SBSAMachineState *sms)
{
    DeviceState *pl061_dev;
    hwaddr base = sbsa_ref_memmap[SBSA_GPIO].base;
    int irq = sbsa_ref_irqmap[SBSA_GPIO];

    pl061_dev = sysbus_create_simple("pl061", base,
                                     qdev_get_gpio_in(sms->gic, irq));

    gpio_key_dev = sysbus_create_simple("gpio-key", -1,
                                        qdev_get_gpio_in(pl061_dev, 3));

    /* connect powerdown request */
    qemu_register_powerdown_notifier(&sbsa_ref_powerdown_notifier);
}

static void create_ahci(const SBSAMachineState *sms)
{
    hwaddr base = sbsa_ref_memmap[SBSA_AHCI].base;
    int irq = sbsa_ref_irqmap[SBSA_AHCI];
    DeviceState *dev;
    DriveInfo *hd[NUM_SATA_PORTS];
    SysbusAHCIState *sysahci;

    dev = qdev_new("sysbus-ahci");
    qdev_prop_set_uint32(dev, "num-ports", NUM_SATA_PORTS);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(sms->gic, irq));

    sysahci = SYSBUS_AHCI(dev);
    ide_drive_get(hd, ARRAY_SIZE(hd));
    ahci_ide_create_devs(&sysahci->ahci, hd);
}

static void create_xhci(const SBSAMachineState *sms)
{
    hwaddr base = sbsa_ref_memmap[SBSA_XHCI].base;
    int irq = sbsa_ref_irqmap[SBSA_XHCI];
    DeviceState *dev = qdev_new(TYPE_XHCI_SYSBUS);
    qdev_prop_set_uint32(dev, "slots", XHCI_MAXSLOTS);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(sms->gic, irq));
}

static void create_smmu(const SBSAMachineState *sms, PCIBus *bus)
{
    hwaddr base = sbsa_ref_memmap[SBSA_SMMU].base;
    int irq =  sbsa_ref_irqmap[SBSA_SMMU];
    DeviceState *dev;
    int i;

    dev = qdev_new(TYPE_ARM_SMMUV3);

    object_property_set_str(OBJECT(dev), "stage", "nested", &error_abort);
    object_property_set_link(OBJECT(dev), "primary-bus", OBJECT(bus),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    for (i = 0; i < NUM_SMMU_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(sms->gic, irq + i));
    }
}

static void create_pcie(SBSAMachineState *sms)
{
    hwaddr base_ecam = sbsa_ref_memmap[SBSA_PCIE_ECAM].base;
    hwaddr size_ecam = sbsa_ref_memmap[SBSA_PCIE_ECAM].size;
    hwaddr base_mmio = sbsa_ref_memmap[SBSA_PCIE_MMIO].base;
    hwaddr size_mmio = sbsa_ref_memmap[SBSA_PCIE_MMIO].size;
    hwaddr base_mmio_high = sbsa_ref_memmap[SBSA_PCIE_MMIO_HIGH].base;
    hwaddr size_mmio_high = sbsa_ref_memmap[SBSA_PCIE_MMIO_HIGH].size;
    hwaddr base_pio = sbsa_ref_memmap[SBSA_PCIE_PIO].base;
    int irq = sbsa_ref_irqmap[SBSA_PCIE];
    MachineClass *mc = MACHINE_GET_CLASS(sms);
    MemoryRegion *mmio_alias, *mmio_alias_high, *mmio_reg;
    MemoryRegion *ecam_alias, *ecam_reg;
    DeviceState *dev;
    PCIHostState *pci;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* Map ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, size_ecam);
    memory_region_add_subregion(get_system_memory(), base_ecam, ecam_alias);

    /* Map the MMIO space */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, base_mmio, size_mmio);
    memory_region_add_subregion(get_system_memory(), base_mmio, mmio_alias);

    /* Map the MMIO_HIGH space */
    mmio_alias_high = g_new0(MemoryRegion, 1);
    memory_region_init_alias(mmio_alias_high, OBJECT(dev), "pcie-mmio-high",
                             mmio_reg, base_mmio_high, size_mmio_high);
    memory_region_add_subregion(get_system_memory(), base_mmio_high,
                                mmio_alias_high);

    /* Map IO port space */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, base_pio);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(sms->gic, irq + i));
        gpex_set_irq_num(GPEX_HOST(dev), i, irq + i);
    }

    pci = PCI_HOST_BRIDGE(dev);

    pci_init_nic_devices(pci->bus, mc->default_nic);

    pci_create_simple(pci->bus, -1, "bochs-display");

    create_smmu(sms, pci->bus);
}

static void *sbsa_ref_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
    const SBSAMachineState *board = container_of(binfo, SBSAMachineState,
                                                 bootinfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

static void create_secure_ec(MemoryRegion *mem)
{
    hwaddr base = sbsa_ref_memmap[SBSA_SECURE_EC].base;
    DeviceState *dev = qdev_new("sbsa-ec");
    SysBusDevice *s = SYS_BUS_DEVICE(dev);

    memory_region_add_subregion(mem, base,
                                sysbus_mmio_get_region(s, 0));
}

static void sbsa_ref_init(MachineState *machine)
{
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int max_cpus = machine->smp.max_cpus;
    SBSAMachineState *sms = SBSA_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *secure_sysmem = g_new(MemoryRegion, 1);
    bool firmware_loaded;
    const CPUArchIdList *possible_cpus;
    int n, sbsa_max_cpus;

    if (kvm_enabled()) {
        error_report("sbsa-ref: KVM is not supported for this machine");
        exit(1);
    }

    /*
     * The Secure view of the world is the same as the NonSecure,
     * but with a few extra devices. Create it as a container region
     * containing the system memory at low priority; any secure-only
     * devices go in at higher priority and take precedence.
     */
    memory_region_init(secure_sysmem, OBJECT(machine), "secure-memory",
                       UINT64_MAX);
    memory_region_add_subregion_overlap(secure_sysmem, 0, sysmem, -1);

    firmware_loaded = sbsa_firmware_init(sms, sysmem, secure_sysmem);

    /*
     * This machine has EL3 enabled, external firmware should supply PSCI
     * implementation, so the QEMU's internal PSCI is disabled.
     */
    sms->psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;

    sbsa_max_cpus = sbsa_ref_memmap[SBSA_GIC_REDIST].size / GICV3_REDIST_SIZE;

    if (max_cpus > sbsa_max_cpus) {
        error_report("Number of SMP CPUs requested (%d) exceeds max CPUs "
                     "supported by machine 'sbsa-ref' (%d)",
                     max_cpus, sbsa_max_cpus);
        exit(1);
    }

    sms->smp_cpus = smp_cpus;

    if (machine->ram_size > sbsa_ref_memmap[SBSA_MEM].size) {
        error_report("sbsa-ref: cannot model more than %dGB RAM", RAMLIMIT_GB);
        exit(1);
    }

    possible_cpus = mc->possible_cpu_arch_ids(machine);
    for (n = 0; n < possible_cpus->len; n++) {
        Object *cpuobj;
        CPUState *cs;

        if (n >= smp_cpus) {
            break;
        }

        cpuobj = object_new(possible_cpus->cpus[n].type);
        object_property_set_int(cpuobj, "mp-affinity",
                                possible_cpus->cpus[n].arch_id, NULL);

        cs = CPU(cpuobj);
        cs->cpu_index = n;

        numa_cpu_pre_plug(&possible_cpus->cpus[cs->cpu_index], DEVICE(cpuobj),
                          &error_fatal);

        if (object_property_find(cpuobj, "reset-cbar")) {
            object_property_set_int(cpuobj, "reset-cbar",
                                    sbsa_ref_memmap[SBSA_CPUPERIPHS].base,
                                    &error_abort);
        }

        object_property_set_int(cpuobj, "cntfrq", SBSA_GTIMER_HZ, &error_abort);

        object_property_set_link(cpuobj, "memory", OBJECT(sysmem),
                                 &error_abort);

        object_property_set_link(cpuobj, "secure-memory",
                                 OBJECT(secure_sysmem), &error_abort);

        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        object_unref(cpuobj);
    }

    memory_region_add_subregion(sysmem, sbsa_ref_memmap[SBSA_MEM].base,
                                machine->ram);

    create_fdt(sms);

    create_secure_ram(sms, secure_sysmem);

    create_gic(sms, sysmem);

    create_uart(sms, SBSA_UART, sysmem, serial_hd(0));
    create_uart(sms, SBSA_SECURE_UART, secure_sysmem, serial_hd(1));
    /* Second secure UART for RAS and MM from EL0 */
    create_uart(sms, SBSA_SECURE_UART_MM, secure_sysmem, serial_hd(2));

    create_rtc(sms);

    create_wdt(sms);

    create_gpio(sms);

    create_ahci(sms);

    create_xhci(sms);

    create_pcie(sms);

    create_secure_ec(secure_sysmem);

    sms->bootinfo.ram_size = machine->ram_size;
    sms->bootinfo.board_id = -1;
    sms->bootinfo.loader_start = sbsa_ref_memmap[SBSA_MEM].base;
    sms->bootinfo.get_dtb = sbsa_ref_dtb;
    sms->bootinfo.firmware_loaded = firmware_loaded;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &sms->bootinfo);
}

static const CPUArchIdList *sbsa_ref_possible_cpu_arch_ids(MachineState *ms)
{
    unsigned int max_cpus = ms->smp.max_cpus;
    SBSAMachineState *sms = SBSA_MACHINE(ms);
    int n;

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id =
            sbsa_ref_cpu_mp_affinity(sms, n);
        ms->possible_cpus->cpus[n].props.has_thread_id = true;
        ms->possible_cpus->cpus[n].props.thread_id = n;
    }
    return ms->possible_cpus;
}

static CpuInstanceProperties
sbsa_ref_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static int64_t
sbsa_ref_get_default_cpu_node_id(const MachineState *ms, int idx)
{
    return idx % ms->numa_state->num_nodes;
}

static void sbsa_ref_instance_init(Object *obj)
{
    SBSAMachineState *sms = SBSA_MACHINE(obj);

    sbsa_flash_create(sms);
}

static void sbsa_ref_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a57"),
        ARM_CPU_TYPE_NAME("cortex-a72"),
        ARM_CPU_TYPE_NAME("neoverse-n1"),
        ARM_CPU_TYPE_NAME("neoverse-v1"),
        ARM_CPU_TYPE_NAME("neoverse-n2"),
        ARM_CPU_TYPE_NAME("max"),
        NULL,
    };

    mc->init = sbsa_ref_init;
    mc->desc = "QEMU 'SBSA Reference' ARM Virtual Machine";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("neoverse-n2");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = 512;
    mc->pci_allow_0_address = true;
    mc->minimum_page_bits = 12;
    mc->block_default_type = IF_IDE;
    mc->no_cdrom = 1;
    mc->default_nic = "e1000e";
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "sbsa-ref.ram";
    mc->default_cpus = 4;
    mc->smp_props.clusters_supported = true;
    mc->possible_cpu_arch_ids = sbsa_ref_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = sbsa_ref_cpu_index_to_props;
    mc->get_default_cpu_node_id = sbsa_ref_get_default_cpu_node_id;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
}

static const TypeInfo sbsa_ref_info = {
    .name          = TYPE_SBSA_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_init = sbsa_ref_instance_init,
    .class_init    = sbsa_ref_class_init,
    .instance_size = sizeof(SBSAMachineState),
};

static void sbsa_ref_machine_init(void)
{
    type_register_static(&sbsa_ref_info);
}

type_init(sbsa_ref_machine_init);
