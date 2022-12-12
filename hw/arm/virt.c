/*
 * ARM mach-virt emulation
 *
 * Copyright (c) 2013 Linaro Limited
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
 *
 * Emulate a virtual board which works by passing Linux all the information
 * it needs about what devices are present via the device tree.
 * There are some restrictions about what we can do here:
 *  + we can only present devices whose Linux drivers will work based
 *    purely on the device tree with no platform data at all
 *  + we want to present a very stripped-down minimalist platform,
 *    both because this reduces the security attack surface from the guest
 *    and also because it reduces our exposure to being broken when
 *    the kernel updates its device tree bindings and requires further
 *    information in a device binding that we aren't providing.
 * This is essentially the same approach kvmtool uses.
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qemu/units.h"
#include "qemu/option.h"
#include "monitor/qdev.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/arm/primecell.h"
#include "hw/arm/virt.h"
#include "hw/block/flash.h"
#include "hw/vfio/vfio-calxeda-xgmac.h"
#include "hw/vfio/vfio-amd-xgbe.h"
#include "hw/display/ramfb.h"
#include "net/net.h"
#include "sysemu/device_tree.h"
#include "sysemu/numa.h"
#include "sysemu/runstate.h"
#include "sysemu/tpm.h"
#include "sysemu/kvm.h"
#include "sysemu/hvf.h"
#include "hw/loader.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/pci-host/gpex.h"
#include "hw/virtio/virtio-pci.h"
#include "hw/core/sysbus-fdt.h"
#include "hw/platform-bus.h"
#include "hw/qdev-properties.h"
#include "hw/arm/fdt.h"
#include "hw/intc/arm_gic.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/irq.h"
#include "kvm_arm.h"
#include "hw/firmware/smbios.h"
#include "qapi/visitor.h"
#include "qapi/qapi-visit-common.h"
#include "standard-headers/linux/input.h"
#include "hw/arm/smmuv3.h"
#include "hw/acpi/acpi.h"
#include "target/arm/internals.h"
#include "hw/mem/memory-device.h"
#include "hw/mem/pc-dimm.h"
#include "hw/mem/nvdimm.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/virtio/virtio-mem-pci.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/char/pl011.h"
#include "qemu/guest-random.h"

#define DEFINE_VIRT_MACHINE_LATEST(major, minor, latest) \
    static void virt_##major##_##minor##_class_init(ObjectClass *oc, \
                                                    void *data) \
    { \
        MachineClass *mc = MACHINE_CLASS(oc); \
        virt_machine_##major##_##minor##_options(mc); \
        mc->desc = "QEMU " # major "." # minor " ARM Virtual Machine"; \
        if (latest) { \
            mc->alias = "virt"; \
        } \
    } \
    static const TypeInfo machvirt_##major##_##minor##_info = { \
        .name = MACHINE_TYPE_NAME("virt-" # major "." # minor), \
        .parent = TYPE_VIRT_MACHINE, \
        .class_init = virt_##major##_##minor##_class_init, \
    }; \
    static void machvirt_machine_##major##_##minor##_init(void) \
    { \
        type_register_static(&machvirt_##major##_##minor##_info); \
    } \
    type_init(machvirt_machine_##major##_##minor##_init);

#define DEFINE_VIRT_MACHINE_AS_LATEST(major, minor) \
    DEFINE_VIRT_MACHINE_LATEST(major, minor, true)
#define DEFINE_VIRT_MACHINE(major, minor) \
    DEFINE_VIRT_MACHINE_LATEST(major, minor, false)


/* Number of external interrupt lines to configure the GIC with */
#define NUM_IRQS 256

#define PLATFORM_BUS_NUM_IRQS 64

/* Legacy RAM limit in GB (< version 4.0) */
#define LEGACY_RAMLIMIT_GB 255
#define LEGACY_RAMLIMIT_BYTES (LEGACY_RAMLIMIT_GB * GiB)

/* Addresses and sizes of our components.
 * 0..128MB is space for a flash device so we can run bootrom code such as UEFI.
 * 128MB..256MB is used for miscellaneous device I/O.
 * 256MB..1GB is reserved for possible future PCI support (ie where the
 * PCI memory window will go if we add a PCI host controller).
 * 1GB and up is RAM (which may happily spill over into the
 * high memory region beyond 4GB).
 * This represents a compromise between how much RAM can be given to
 * a 32 bit VM and leaving space for expansion and in particular for PCI.
 * Note that devices should generally be placed at multiples of 0x10000,
 * to accommodate guests using 64K pages.
 */
static const MemMapEntry base_memmap[] = {
    /* Space up to 0x8000000 is reserved for a boot ROM */
    [VIRT_FLASH] =              {          0, 0x08000000 },
    [VIRT_CPUPERIPHS] =         { 0x08000000, 0x00020000 },
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x08000000, 0x00010000 },
    [VIRT_GIC_CPU] =            { 0x08010000, 0x00010000 },
    [VIRT_GIC_V2M] =            { 0x08020000, 0x00001000 },
    [VIRT_GIC_HYP] =            { 0x08030000, 0x00010000 },
    [VIRT_GIC_VCPU] =           { 0x08040000, 0x00010000 },
    /* The space in between here is reserved for GICv3 CPU/vCPU/HYP */
    [VIRT_GIC_ITS] =            { 0x08080000, 0x00020000 },
    /* This redistributor space allows up to 2*64kB*123 CPUs */
    [VIRT_GIC_REDIST] =         { 0x080A0000, 0x00F60000 },
    [VIRT_UART] =               { 0x09000000, 0x00001000 },
    [VIRT_RTC] =                { 0x09010000, 0x00001000 },
    [VIRT_FW_CFG] =             { 0x09020000, 0x00000018 },
    [VIRT_GPIO] =               { 0x09030000, 0x00001000 },
    [VIRT_SECURE_UART] =        { 0x09040000, 0x00001000 },
    [VIRT_SMMU] =               { 0x09050000, 0x00020000 },
    [VIRT_PCDIMM_ACPI] =        { 0x09070000, MEMORY_HOTPLUG_IO_LEN },
    [VIRT_ACPI_GED] =           { 0x09080000, ACPI_GED_EVT_SEL_LEN },
    [VIRT_NVDIMM_ACPI] =        { 0x09090000, NVDIMM_ACPI_IO_LEN},
    [VIRT_PVTIME] =             { 0x090a0000, 0x00010000 },
    [VIRT_SECURE_GPIO] =        { 0x090b0000, 0x00001000 },
    [VIRT_MMIO] =               { 0x0a000000, 0x00000200 },
    /* ...repeating for a total of NUM_VIRTIO_TRANSPORTS, each of that size */
    [VIRT_PLATFORM_BUS] =       { 0x0c000000, 0x02000000 },
    [VIRT_SECURE_MEM] =         { 0x0e000000, 0x01000000 },
    [VIRT_PCIE_MMIO] =          { 0x10000000, 0x2eff0000 },
    [VIRT_PCIE_PIO] =           { 0x3eff0000, 0x00010000 },
    [VIRT_PCIE_ECAM] =          { 0x3f000000, 0x01000000 },
    /* Actual RAM size depends on initial RAM and device memory settings */
    [VIRT_MEM] =                { GiB, LEGACY_RAMLIMIT_BYTES },
};

/*
 * Highmem IO Regions: This memory map is floating, located after the RAM.
 * Each MemMapEntry base (GPA) will be dynamically computed, depending on the
 * top of the RAM, so that its base get the same alignment as the size,
 * ie. a 512GiB entry will be aligned on a 512GiB boundary. If there is
 * less than 256GiB of RAM, the floating area starts at the 256GiB mark.
 * Note the extended_memmap is sized so that it eventually also includes the
 * base_memmap entries (VIRT_HIGH_GIC_REDIST2 index is greater than the last
 * index of base_memmap).
 *
 * The memory map for these Highmem IO Regions can be in legacy or compact
 * layout, depending on 'compact-highmem' property. With legacy layout, the
 * PA space for one specific region is always reserved, even if the region
 * has been disabled or doesn't fit into the PA space. However, the PA space
 * for the region won't be reserved in these circumstances with compact layout.
 */
static MemMapEntry extended_memmap[] = {
    /* Additional 64 MB redist region (can contain up to 512 redistributors) */
    [VIRT_HIGH_GIC_REDIST2] =   { 0x0, 64 * MiB },
    [VIRT_HIGH_PCIE_ECAM] =     { 0x0, 256 * MiB },
    /* Second PCIe window */
    [VIRT_HIGH_PCIE_MMIO] =     { 0x0, 512 * GiB },
};

static const int a15irqmap[] = {
    [VIRT_UART] = 1,
    [VIRT_RTC] = 2,
    [VIRT_PCIE] = 3, /* ... to 6 */
    [VIRT_GPIO] = 7,
    [VIRT_SECURE_UART] = 8,
    [VIRT_ACPI_GED] = 9,
    [VIRT_MMIO] = 16, /* ...to 16 + NUM_VIRTIO_TRANSPORTS - 1 */
    [VIRT_GIC_V2M] = 48, /* ...to 48 + NUM_GICV2M_SPIS - 1 */
    [VIRT_SMMU] = 74,    /* ...to 74 + NUM_SMMU_IRQS - 1 */
    [VIRT_PLATFORM_BUS] = 112, /* ...to 112 + PLATFORM_BUS_NUM_IRQS -1 */
};

static const char *valid_cpus[] = {
    ARM_CPU_TYPE_NAME("cortex-a7"),
    ARM_CPU_TYPE_NAME("cortex-a15"),
    ARM_CPU_TYPE_NAME("cortex-a35"),
    ARM_CPU_TYPE_NAME("cortex-a53"),
    ARM_CPU_TYPE_NAME("cortex-a55"),
    ARM_CPU_TYPE_NAME("cortex-a57"),
    ARM_CPU_TYPE_NAME("cortex-a72"),
    ARM_CPU_TYPE_NAME("cortex-a76"),
    ARM_CPU_TYPE_NAME("a64fx"),
    ARM_CPU_TYPE_NAME("neoverse-n1"),
    ARM_CPU_TYPE_NAME("host"),
    ARM_CPU_TYPE_NAME("max"),
};

static bool cpu_type_valid(const char *cpu)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(valid_cpus); i++) {
        if (strcmp(cpu, valid_cpus[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void create_randomness(MachineState *ms, const char *node)
{
    struct {
        uint64_t kaslr;
        uint8_t rng[32];
    } seed;

    if (qemu_guest_getrandom(&seed, sizeof(seed), NULL)) {
        return;
    }
    qemu_fdt_setprop_u64(ms->fdt, node, "kaslr-seed", seed.kaslr);
    qemu_fdt_setprop(ms->fdt, node, "rng-seed", seed.rng, sizeof(seed.rng));
}

static void create_fdt(VirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    int nb_numa_nodes = ms->numa_state->num_nodes;
    void *fdt = create_device_tree(&vms->fdt_size);

    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    ms->fdt = fdt;

    /* Header */
    qemu_fdt_setprop_string(fdt, "/", "compatible", "linux,dummy-virt");
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_string(fdt, "/", "model", "linux,dummy-virt");

    /* /chosen must exist for load_dtb to fill in necessary properties later */
    qemu_fdt_add_subnode(fdt, "/chosen");
    if (vms->dtb_randomness) {
        create_randomness(ms, "/chosen");
    }

    if (vms->secure) {
        qemu_fdt_add_subnode(fdt, "/secure-chosen");
        if (vms->dtb_randomness) {
            create_randomness(ms, "/secure-chosen");
        }
    }

    /* Clock node, for the benefit of the UART. The kernel device tree
     * binding documentation claims the PL011 node clock properties are
     * optional but in practice if you omit them the kernel refuses to
     * probe for the device.
     */
    vms->clock_phandle = qemu_fdt_alloc_phandle(fdt);
    qemu_fdt_add_subnode(fdt, "/apb-pclk");
    qemu_fdt_setprop_string(fdt, "/apb-pclk", "compatible", "fixed-clock");
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "#clock-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "clock-frequency", 24000000);
    qemu_fdt_setprop_string(fdt, "/apb-pclk", "clock-output-names",
                                "clk24mhz");
    qemu_fdt_setprop_cell(fdt, "/apb-pclk", "phandle", vms->clock_phandle);

    if (nb_numa_nodes > 0 && ms->numa_state->have_numa_distance) {
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
        qemu_fdt_setprop_string(fdt, "/distance-map", "compatible",
                                "numa-distance-map-v1");
        qemu_fdt_setprop(fdt, "/distance-map", "distance-matrix",
                         matrix, size);
        g_free(matrix);
    }
}

static void fdt_add_timer_nodes(const VirtMachineState *vms)
{
    /* On real hardware these interrupts are level-triggered.
     * On KVM they were edge-triggered before host kernel version 4.4,
     * and level-triggered afterwards.
     * On emulated QEMU they are level-triggered.
     *
     * Getting the DTB info about them wrong is awkward for some
     * guest kernels:
     *  pre-4.8 ignore the DT and leave the interrupt configured
     *   with whatever the GIC reset value (or the bootloader) left it at
     *  4.8 before rc6 honour the incorrect data by programming it back
     *   into the GIC, causing problems
     *  4.8rc6 and later ignore the DT and always write "level triggered"
     *   into the GIC
     *
     * For backwards-compatibility, virt-2.8 and earlier will continue
     * to say these are edge-triggered, but later machines will report
     * the correct information.
     */
    ARMCPU *armcpu;
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    uint32_t irqflags = GIC_FDT_IRQ_FLAGS_LEVEL_HI;
    MachineState *ms = MACHINE(vms);

    if (vmc->claim_edge_triggered_timers) {
        irqflags = GIC_FDT_IRQ_FLAGS_EDGE_LO_HI;
    }

    if (vms->gic_version == VIRT_GIC_VERSION_2) {
        irqflags = deposit32(irqflags, GIC_FDT_IRQ_PPI_CPU_START,
                             GIC_FDT_IRQ_PPI_CPU_WIDTH,
                             (1 << MACHINE(vms)->smp.cpus) - 1);
    }

    qemu_fdt_add_subnode(ms->fdt, "/timer");

    armcpu = ARM_CPU(qemu_get_cpu(0));
    if (arm_feature(&armcpu->env, ARM_FEATURE_V8)) {
        const char compat[] = "arm,armv8-timer\0arm,armv7-timer";
        qemu_fdt_setprop(ms->fdt, "/timer", "compatible",
                         compat, sizeof(compat));
    } else {
        qemu_fdt_setprop_string(ms->fdt, "/timer", "compatible",
                                "arm,armv7-timer");
    }
    qemu_fdt_setprop(ms->fdt, "/timer", "always-on", NULL, 0);
    qemu_fdt_setprop_cells(ms->fdt, "/timer", "interrupts",
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_S_EL1_IRQ, irqflags,
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL1_IRQ, irqflags,
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_VIRT_IRQ, irqflags,
                       GIC_FDT_IRQ_TYPE_PPI, ARCH_TIMER_NS_EL2_IRQ, irqflags);
}

static void fdt_add_cpu_nodes(const VirtMachineState *vms)
{
    int cpu;
    int addr_cells = 1;
    const MachineState *ms = MACHINE(vms);
    const VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    int smp_cpus = ms->smp.cpus;

    /*
     * See Linux Documentation/devicetree/bindings/arm/cpus.yaml
     * On ARM v8 64-bit systems value should be set to 2,
     * that corresponds to the MPIDR_EL1 register size.
     * If MPIDR_EL1[63:32] value is equal to 0 on all CPUs
     * in the system, #address-cells can be set to 1, since
     * MPIDR_EL1[63:32] bits are not used for CPUs
     * identification.
     *
     * Here we actually don't know whether our system is 32- or 64-bit one.
     * The simplest way to go is to examine affinity IDs of all our CPUs. If
     * at least one of them has Aff3 populated, we set #address-cells to 2.
     */
    for (cpu = 0; cpu < smp_cpus; cpu++) {
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));

        if (armcpu->mp_affinity & ARM_AFF3_MASK) {
            addr_cells = 2;
            break;
        }
    }

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", addr_cells);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);

    for (cpu = smp_cpus - 1; cpu >= 0; cpu--) {
        char *nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(cpu));
        CPUState *cs = CPU(armcpu);

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                    armcpu->dtb_compatible);

        if (vms->psci_conduit != QEMU_PSCI_CONDUIT_DISABLED && smp_cpus > 1) {
            qemu_fdt_setprop_string(ms->fdt, nodename,
                                        "enable-method", "psci");
        }

        if (addr_cells == 2) {
            qemu_fdt_setprop_u64(ms->fdt, nodename, "reg",
                                 armcpu->mp_affinity);
        } else {
            qemu_fdt_setprop_cell(ms->fdt, nodename, "reg",
                                  armcpu->mp_affinity);
        }

        if (ms->possible_cpus->cpus[cs->cpu_index].props.has_node_id) {
            qemu_fdt_setprop_cell(ms->fdt, nodename, "numa-node-id",
                ms->possible_cpus->cpus[cs->cpu_index].props.node_id);
        }

        if (!vmc->no_cpu_topology) {
            qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle",
                                  qemu_fdt_alloc_phandle(ms->fdt));
        }

        g_free(nodename);
    }

    if (!vmc->no_cpu_topology) {
        /*
         * Add vCPU topology description through fdt node cpu-map.
         *
         * See Linux Documentation/devicetree/bindings/cpu/cpu-topology.txt
         * In a SMP system, the hierarchy of CPUs can be defined through
         * four entities that are used to describe the layout of CPUs in
         * the system: socket/cluster/core/thread.
         *
         * A socket node represents the boundary of system physical package
         * and its child nodes must be one or more cluster nodes. A system
         * can contain several layers of clustering within a single physical
         * package and cluster nodes can be contained in parent cluster nodes.
         *
         * Note: currently we only support one layer of clustering within
         * each physical package.
         */
        qemu_fdt_add_subnode(ms->fdt, "/cpus/cpu-map");

        for (cpu = smp_cpus - 1; cpu >= 0; cpu--) {
            char *cpu_path = g_strdup_printf("/cpus/cpu@%d", cpu);
            char *map_path;

            if (ms->smp.threads > 1) {
                map_path = g_strdup_printf(
                    "/cpus/cpu-map/socket%d/cluster%d/core%d/thread%d",
                    cpu / (ms->smp.clusters * ms->smp.cores * ms->smp.threads),
                    (cpu / (ms->smp.cores * ms->smp.threads)) % ms->smp.clusters,
                    (cpu / ms->smp.threads) % ms->smp.cores,
                    cpu % ms->smp.threads);
            } else {
                map_path = g_strdup_printf(
                    "/cpus/cpu-map/socket%d/cluster%d/core%d",
                    cpu / (ms->smp.clusters * ms->smp.cores),
                    (cpu / ms->smp.cores) % ms->smp.clusters,
                    cpu % ms->smp.cores);
            }
            qemu_fdt_add_path(ms->fdt, map_path);
            qemu_fdt_setprop_phandle(ms->fdt, map_path, "cpu", cpu_path);

            g_free(map_path);
            g_free(cpu_path);
        }
    }
}

static void fdt_add_its_gic_node(VirtMachineState *vms)
{
    char *nodename;
    MachineState *ms = MACHINE(vms);

    vms->msi_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    nodename = g_strdup_printf("/intc/its@%" PRIx64,
                               vms->memmap[VIRT_GIC_ITS].base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "arm,gic-v3-its");
    qemu_fdt_setprop(ms->fdt, nodename, "msi-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#msi-cells", 1);
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, vms->memmap[VIRT_GIC_ITS].base,
                                 2, vms->memmap[VIRT_GIC_ITS].size);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", vms->msi_phandle);
    g_free(nodename);
}

static void fdt_add_v2m_gic_node(VirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    char *nodename;

    nodename = g_strdup_printf("/intc/v2m@%" PRIx64,
                               vms->memmap[VIRT_GIC_V2M].base);
    vms->msi_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                            "arm,gic-v2m-frame");
    qemu_fdt_setprop(ms->fdt, nodename, "msi-controller", NULL, 0);
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, vms->memmap[VIRT_GIC_V2M].base,
                                 2, vms->memmap[VIRT_GIC_V2M].size);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", vms->msi_phandle);
    g_free(nodename);
}

static void fdt_add_gic_node(VirtMachineState *vms)
{
    MachineState *ms = MACHINE(vms);
    char *nodename;

    vms->gic_phandle = qemu_fdt_alloc_phandle(ms->fdt);
    qemu_fdt_setprop_cell(ms->fdt, "/", "interrupt-parent", vms->gic_phandle);

    nodename = g_strdup_printf("/intc@%" PRIx64,
                               vms->memmap[VIRT_GIC_DIST].base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 3);
    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#address-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#size-cells", 0x2);
    qemu_fdt_setprop(ms->fdt, nodename, "ranges", NULL, 0);
    if (vms->gic_version != VIRT_GIC_VERSION_2) {
        int nb_redist_regions = virt_gicv3_redist_region_count(vms);

        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                "arm,gic-v3");

        qemu_fdt_setprop_cell(ms->fdt, nodename,
                              "#redistributor-regions", nb_redist_regions);

        if (nb_redist_regions == 1) {
            qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                         2, vms->memmap[VIRT_GIC_DIST].base,
                                         2, vms->memmap[VIRT_GIC_DIST].size,
                                         2, vms->memmap[VIRT_GIC_REDIST].base,
                                         2, vms->memmap[VIRT_GIC_REDIST].size);
        } else {
            qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, vms->memmap[VIRT_GIC_DIST].base,
                                 2, vms->memmap[VIRT_GIC_DIST].size,
                                 2, vms->memmap[VIRT_GIC_REDIST].base,
                                 2, vms->memmap[VIRT_GIC_REDIST].size,
                                 2, vms->memmap[VIRT_HIGH_GIC_REDIST2].base,
                                 2, vms->memmap[VIRT_HIGH_GIC_REDIST2].size);
        }

        if (vms->virt) {
            qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                                   GIC_FDT_IRQ_TYPE_PPI, ARCH_GIC_MAINT_IRQ,
                                   GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        }
    } else {
        /* 'cortex-a15-gic' means 'GIC v2' */
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible",
                                "arm,cortex-a15-gic");
        if (!vms->virt) {
            qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                         2, vms->memmap[VIRT_GIC_DIST].base,
                                         2, vms->memmap[VIRT_GIC_DIST].size,
                                         2, vms->memmap[VIRT_GIC_CPU].base,
                                         2, vms->memmap[VIRT_GIC_CPU].size);
        } else {
            qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                         2, vms->memmap[VIRT_GIC_DIST].base,
                                         2, vms->memmap[VIRT_GIC_DIST].size,
                                         2, vms->memmap[VIRT_GIC_CPU].base,
                                         2, vms->memmap[VIRT_GIC_CPU].size,
                                         2, vms->memmap[VIRT_GIC_HYP].base,
                                         2, vms->memmap[VIRT_GIC_HYP].size,
                                         2, vms->memmap[VIRT_GIC_VCPU].base,
                                         2, vms->memmap[VIRT_GIC_VCPU].size);
            qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                                   GIC_FDT_IRQ_TYPE_PPI, ARCH_GIC_MAINT_IRQ,
                                   GIC_FDT_IRQ_FLAGS_LEVEL_HI);
        }
    }

    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", vms->gic_phandle);
    g_free(nodename);
}

static void fdt_add_pmu_nodes(const VirtMachineState *vms)
{
    ARMCPU *armcpu = ARM_CPU(first_cpu);
    uint32_t irqflags = GIC_FDT_IRQ_FLAGS_LEVEL_HI;
    MachineState *ms = MACHINE(vms);

    if (!arm_feature(&armcpu->env, ARM_FEATURE_PMU)) {
        assert(!object_property_get_bool(OBJECT(armcpu), "pmu", NULL));
        return;
    }

    if (vms->gic_version == VIRT_GIC_VERSION_2) {
        irqflags = deposit32(irqflags, GIC_FDT_IRQ_PPI_CPU_START,
                             GIC_FDT_IRQ_PPI_CPU_WIDTH,
                             (1 << MACHINE(vms)->smp.cpus) - 1);
    }

    qemu_fdt_add_subnode(ms->fdt, "/pmu");
    if (arm_feature(&armcpu->env, ARM_FEATURE_V8)) {
        const char compat[] = "arm,armv8-pmuv3";
        qemu_fdt_setprop(ms->fdt, "/pmu", "compatible",
                         compat, sizeof(compat));
        qemu_fdt_setprop_cells(ms->fdt, "/pmu", "interrupts",
                               GIC_FDT_IRQ_TYPE_PPI, VIRTUAL_PMU_IRQ, irqflags);
    }
}

static inline DeviceState *create_acpi_ged(VirtMachineState *vms)
{
    DeviceState *dev;
    MachineState *ms = MACHINE(vms);
    int irq = vms->irqmap[VIRT_ACPI_GED];
    uint32_t event = ACPI_GED_PWR_DOWN_EVT;

    if (ms->ram_slots) {
        event |= ACPI_GED_MEM_HOTPLUG_EVT;
    }

    if (ms->nvdimms_state->is_enabled) {
        event |= ACPI_GED_NVDIMM_HOTPLUG_EVT;
    }

    dev = qdev_new(TYPE_ACPI_GED);
    qdev_prop_set_uint32(dev, "ged-event", event);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, vms->memmap[VIRT_ACPI_GED].base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, vms->memmap[VIRT_PCDIMM_ACPI].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(vms->gic, irq));

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return dev;
}

static void create_its(VirtMachineState *vms)
{
    const char *itsclass = its_class_name();
    DeviceState *dev;

    if (!strcmp(itsclass, "arm-gicv3-its")) {
        if (!vms->tcg_its) {
            itsclass = NULL;
        }
    }

    if (!itsclass) {
        /* Do nothing if not supported */
        return;
    }

    dev = qdev_new(itsclass);

    object_property_set_link(OBJECT(dev), "parent-gicv3", OBJECT(vms->gic),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, vms->memmap[VIRT_GIC_ITS].base);

    fdt_add_its_gic_node(vms);
    vms->msi_controller = VIRT_MSI_CTRL_ITS;
}

static void create_v2m(VirtMachineState *vms)
{
    int i;
    int irq = vms->irqmap[VIRT_GIC_V2M];
    DeviceState *dev;

    dev = qdev_new("arm-gicv2m");
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, vms->memmap[VIRT_GIC_V2M].base);
    qdev_prop_set_uint32(dev, "base-spi", irq);
    qdev_prop_set_uint32(dev, "num-spi", NUM_GICV2M_SPIS);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    for (i = 0; i < NUM_GICV2M_SPIS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(vms->gic, irq + i));
    }

    fdt_add_v2m_gic_node(vms);
    vms->msi_controller = VIRT_MSI_CTRL_GICV2M;
}

static void create_gic(VirtMachineState *vms, MemoryRegion *mem)
{
    MachineState *ms = MACHINE(vms);
    /* We create a standalone GIC */
    SysBusDevice *gicbusdev;
    const char *gictype;
    int i;
    unsigned int smp_cpus = ms->smp.cpus;
    uint32_t nb_redist_regions = 0;
    int revision;

    if (vms->gic_version == VIRT_GIC_VERSION_2) {
        gictype = gic_class_name();
    } else {
        gictype = gicv3_class_name();
    }

    switch (vms->gic_version) {
    case VIRT_GIC_VERSION_2:
        revision = 2;
        break;
    case VIRT_GIC_VERSION_3:
        revision = 3;
        break;
    case VIRT_GIC_VERSION_4:
        revision = 4;
        break;
    default:
        g_assert_not_reached();
    }
    vms->gic = qdev_new(gictype);
    qdev_prop_set_uint32(vms->gic, "revision", revision);
    qdev_prop_set_uint32(vms->gic, "num-cpu", smp_cpus);
    /* Note that the num-irq property counts both internal and external
     * interrupts; there are always 32 of the former (mandated by GIC spec).
     */
    qdev_prop_set_uint32(vms->gic, "num-irq", NUM_IRQS + 32);
    if (!kvm_irqchip_in_kernel()) {
        qdev_prop_set_bit(vms->gic, "has-security-extensions", vms->secure);
    }

    if (vms->gic_version != VIRT_GIC_VERSION_2) {
        uint32_t redist0_capacity = virt_redist_capacity(vms, VIRT_GIC_REDIST);
        uint32_t redist0_count = MIN(smp_cpus, redist0_capacity);

        nb_redist_regions = virt_gicv3_redist_region_count(vms);

        qdev_prop_set_uint32(vms->gic, "len-redist-region-count",
                             nb_redist_regions);
        qdev_prop_set_uint32(vms->gic, "redist-region-count[0]", redist0_count);

        if (!kvm_irqchip_in_kernel()) {
            if (vms->tcg_its) {
                object_property_set_link(OBJECT(vms->gic), "sysmem",
                                         OBJECT(mem), &error_fatal);
                qdev_prop_set_bit(vms->gic, "has-lpi", true);
            }
        }

        if (nb_redist_regions == 2) {
            uint32_t redist1_capacity =
                virt_redist_capacity(vms, VIRT_HIGH_GIC_REDIST2);

            qdev_prop_set_uint32(vms->gic, "redist-region-count[1]",
                MIN(smp_cpus - redist0_count, redist1_capacity));
        }
    } else {
        if (!kvm_irqchip_in_kernel()) {
            qdev_prop_set_bit(vms->gic, "has-virtualization-extensions",
                              vms->virt);
        }
    }
    gicbusdev = SYS_BUS_DEVICE(vms->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0, vms->memmap[VIRT_GIC_DIST].base);
    if (vms->gic_version != VIRT_GIC_VERSION_2) {
        sysbus_mmio_map(gicbusdev, 1, vms->memmap[VIRT_GIC_REDIST].base);
        if (nb_redist_regions == 2) {
            sysbus_mmio_map(gicbusdev, 2,
                            vms->memmap[VIRT_HIGH_GIC_REDIST2].base);
        }
    } else {
        sysbus_mmio_map(gicbusdev, 1, vms->memmap[VIRT_GIC_CPU].base);
        if (vms->virt) {
            sysbus_mmio_map(gicbusdev, 2, vms->memmap[VIRT_GIC_HYP].base);
            sysbus_mmio_map(gicbusdev, 3, vms->memmap[VIRT_GIC_VCPU].base);
        }
    }

    /* Wire the outputs from each CPU's generic timer and the GICv3
     * maintenance interrupt signal to the appropriate GIC PPI inputs,
     * and the GIC's IRQ/FIQ/VIRQ/VFIQ interrupt outputs to the CPU's inputs.
     */
    for (i = 0; i < smp_cpus; i++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(i));
        int ppibase = NUM_IRQS + i * GIC_INTERNAL + GIC_NR_SGIS;
        int irq;
        /* Mapping from the output timer irq lines from the CPU to the
         * GIC PPI inputs we use for the virt board.
         */
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
        };

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(vms->gic,
                                                   ppibase + timer_irq[irq]));
        }

        if (vms->gic_version != VIRT_GIC_VERSION_2) {
            qemu_irq irq = qdev_get_gpio_in(vms->gic,
                                            ppibase + ARCH_GIC_MAINT_IRQ);
            qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt",
                                        0, irq);
        } else if (vms->virt) {
            qemu_irq irq = qdev_get_gpio_in(vms->gic,
                                            ppibase + ARCH_GIC_MAINT_IRQ);
            sysbus_connect_irq(gicbusdev, i + 4 * smp_cpus, irq);
        }

        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(vms->gic, ppibase
                                                     + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, i, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, i + smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, i + 2 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, i + 3 * smp_cpus,
                           qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    fdt_add_gic_node(vms);

    if (vms->gic_version != VIRT_GIC_VERSION_2 && vms->its) {
        create_its(vms);
    } else if (vms->gic_version == VIRT_GIC_VERSION_2) {
        create_v2m(vms);
    }
}

static void create_uart(const VirtMachineState *vms, int uart,
                        MemoryRegion *mem, Chardev *chr)
{
    char *nodename;
    hwaddr base = vms->memmap[uart].base;
    hwaddr size = vms->memmap[uart].size;
    int irq = vms->irqmap[uart];
    const char compat[] = "arm,pl011\0arm,primecell";
    const char clocknames[] = "uartclk\0apb_pclk";
    DeviceState *dev = qdev_new(TYPE_PL011);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    MachineState *ms = MACHINE(vms);

    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(mem, base,
                                sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(vms->gic, irq));

    nodename = g_strdup_printf("/pl011@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    /* Note that we can't use setprop_string because of the embedded NUL */
    qemu_fdt_setprop(ms->fdt, nodename, "compatible",
                         compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                     2, base, 2, size);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irq,
                               GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "clocks",
                               vms->clock_phandle, vms->clock_phandle);
    qemu_fdt_setprop(ms->fdt, nodename, "clock-names",
                         clocknames, sizeof(clocknames));

    if (uart == VIRT_UART) {
        qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", nodename);
    } else {
        /* Mark as not usable by the normal world */
        qemu_fdt_setprop_string(ms->fdt, nodename, "status", "disabled");
        qemu_fdt_setprop_string(ms->fdt, nodename, "secure-status", "okay");

        qemu_fdt_setprop_string(ms->fdt, "/secure-chosen", "stdout-path",
                                nodename);
    }

    g_free(nodename);
}

static void create_rtc(const VirtMachineState *vms)
{
    char *nodename;
    hwaddr base = vms->memmap[VIRT_RTC].base;
    hwaddr size = vms->memmap[VIRT_RTC].size;
    int irq = vms->irqmap[VIRT_RTC];
    const char compat[] = "arm,pl031\0arm,primecell";
    MachineState *ms = MACHINE(vms);

    sysbus_create_simple("pl031", base, qdev_get_gpio_in(vms->gic, irq));

    nodename = g_strdup_printf("/pl031@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop(ms->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "clocks", vms->clock_phandle);
    qemu_fdt_setprop_string(ms->fdt, nodename, "clock-names", "apb_pclk");
    g_free(nodename);
}

static DeviceState *gpio_key_dev;
static void virt_powerdown_req(Notifier *n, void *opaque)
{
    VirtMachineState *s = container_of(n, VirtMachineState, powerdown_notifier);

    if (s->acpi_dev) {
        acpi_send_event(s->acpi_dev, ACPI_POWER_DOWN_STATUS);
    } else {
        /* use gpio Pin 3 for power button event */
        qemu_set_irq(qdev_get_gpio_in(gpio_key_dev, 0), 1);
    }
}

static void create_gpio_keys(char *fdt, DeviceState *pl061_dev,
                             uint32_t phandle)
{
    gpio_key_dev = sysbus_create_simple("gpio-key", -1,
                                        qdev_get_gpio_in(pl061_dev, 3));

    qemu_fdt_add_subnode(fdt, "/gpio-keys");
    qemu_fdt_setprop_string(fdt, "/gpio-keys", "compatible", "gpio-keys");

    qemu_fdt_add_subnode(fdt, "/gpio-keys/poweroff");
    qemu_fdt_setprop_string(fdt, "/gpio-keys/poweroff",
                            "label", "GPIO Key Poweroff");
    qemu_fdt_setprop_cell(fdt, "/gpio-keys/poweroff", "linux,code",
                          KEY_POWER);
    qemu_fdt_setprop_cells(fdt, "/gpio-keys/poweroff",
                           "gpios", phandle, 3, 0);
}

#define SECURE_GPIO_POWEROFF 0
#define SECURE_GPIO_RESET    1

static void create_secure_gpio_pwr(char *fdt, DeviceState *pl061_dev,
                                   uint32_t phandle)
{
    DeviceState *gpio_pwr_dev;

    /* gpio-pwr */
    gpio_pwr_dev = sysbus_create_simple("gpio-pwr", -1, NULL);

    /* connect secure pl061 to gpio-pwr */
    qdev_connect_gpio_out(pl061_dev, SECURE_GPIO_RESET,
                          qdev_get_gpio_in_named(gpio_pwr_dev, "reset", 0));
    qdev_connect_gpio_out(pl061_dev, SECURE_GPIO_POWEROFF,
                          qdev_get_gpio_in_named(gpio_pwr_dev, "shutdown", 0));

    qemu_fdt_add_subnode(fdt, "/gpio-poweroff");
    qemu_fdt_setprop_string(fdt, "/gpio-poweroff", "compatible",
                            "gpio-poweroff");
    qemu_fdt_setprop_cells(fdt, "/gpio-poweroff",
                           "gpios", phandle, SECURE_GPIO_POWEROFF, 0);
    qemu_fdt_setprop_string(fdt, "/gpio-poweroff", "status", "disabled");
    qemu_fdt_setprop_string(fdt, "/gpio-poweroff", "secure-status",
                            "okay");

    qemu_fdt_add_subnode(fdt, "/gpio-restart");
    qemu_fdt_setprop_string(fdt, "/gpio-restart", "compatible",
                            "gpio-restart");
    qemu_fdt_setprop_cells(fdt, "/gpio-restart",
                           "gpios", phandle, SECURE_GPIO_RESET, 0);
    qemu_fdt_setprop_string(fdt, "/gpio-restart", "status", "disabled");
    qemu_fdt_setprop_string(fdt, "/gpio-restart", "secure-status",
                            "okay");
}

static void create_gpio_devices(const VirtMachineState *vms, int gpio,
                                MemoryRegion *mem)
{
    char *nodename;
    DeviceState *pl061_dev;
    hwaddr base = vms->memmap[gpio].base;
    hwaddr size = vms->memmap[gpio].size;
    int irq = vms->irqmap[gpio];
    const char compat[] = "arm,pl061\0arm,primecell";
    SysBusDevice *s;
    MachineState *ms = MACHINE(vms);

    pl061_dev = qdev_new("pl061");
    /* Pull lines down to 0 if not driven by the PL061 */
    qdev_prop_set_uint32(pl061_dev, "pullups", 0);
    qdev_prop_set_uint32(pl061_dev, "pulldowns", 0xff);
    s = SYS_BUS_DEVICE(pl061_dev);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(mem, base, sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(vms->gic, irq));

    uint32_t phandle = qemu_fdt_alloc_phandle(ms->fdt);
    nodename = g_strdup_printf("/pl061@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop(ms->fdt, nodename, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#gpio-cells", 2);
    qemu_fdt_setprop(ms->fdt, nodename, "gpio-controller", NULL, 0);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                           GIC_FDT_IRQ_TYPE_SPI, irq,
                           GIC_FDT_IRQ_FLAGS_LEVEL_HI);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "clocks", vms->clock_phandle);
    qemu_fdt_setprop_string(ms->fdt, nodename, "clock-names", "apb_pclk");
    qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", phandle);

    if (gpio != VIRT_GPIO) {
        /* Mark as not usable by the normal world */
        qemu_fdt_setprop_string(ms->fdt, nodename, "status", "disabled");
        qemu_fdt_setprop_string(ms->fdt, nodename, "secure-status", "okay");
    }
    g_free(nodename);

    /* Child gpio devices */
    if (gpio == VIRT_GPIO) {
        create_gpio_keys(ms->fdt, pl061_dev, phandle);
    } else {
        create_secure_gpio_pwr(ms->fdt, pl061_dev, phandle);
    }
}

static void create_virtio_devices(const VirtMachineState *vms)
{
    int i;
    hwaddr size = vms->memmap[VIRT_MMIO].size;
    MachineState *ms = MACHINE(vms);

    /* We create the transports in forwards order. Since qbus_realize()
     * prepends (not appends) new child buses, the incrementing loop below will
     * create a list of virtio-mmio buses with decreasing base addresses.
     *
     * When a -device option is processed from the command line,
     * qbus_find_recursive() picks the next free virtio-mmio bus in forwards
     * order. The upshot is that -device options in increasing command line
     * order are mapped to virtio-mmio buses with decreasing base addresses.
     *
     * When this code was originally written, that arrangement ensured that the
     * guest Linux kernel would give the lowest "name" (/dev/vda, eth0, etc) to
     * the first -device on the command line. (The end-to-end order is a
     * function of this loop, qbus_realize(), qbus_find_recursive(), and the
     * guest kernel's name-to-address assignment strategy.)
     *
     * Meanwhile, the kernel's traversal seems to have been reversed; see eg.
     * the message, if not necessarily the code, of commit 70161ff336.
     * Therefore the loop now establishes the inverse of the original intent.
     *
     * Unfortunately, we can't counteract the kernel change by reversing the
     * loop; it would break existing command lines.
     *
     * In any case, the kernel makes no guarantee about the stability of
     * enumeration order of virtio devices (as demonstrated by it changing
     * between kernel versions). For reliable and stable identification
     * of disks users must use UUIDs or similar mechanisms.
     */
    for (i = 0; i < NUM_VIRTIO_TRANSPORTS; i++) {
        int irq = vms->irqmap[VIRT_MMIO] + i;
        hwaddr base = vms->memmap[VIRT_MMIO].base + i * size;

        sysbus_create_simple("virtio-mmio", base,
                             qdev_get_gpio_in(vms->gic, irq));
    }

    /* We add dtb nodes in reverse order so that they appear in the finished
     * device tree lowest address first.
     *
     * Note that this mapping is independent of the loop above. The previous
     * loop influences virtio device to virtio transport assignment, whereas
     * this loop controls how virtio transports are laid out in the dtb.
     */
    for (i = NUM_VIRTIO_TRANSPORTS - 1; i >= 0; i--) {
        char *nodename;
        int irq = vms->irqmap[VIRT_MMIO] + i;
        hwaddr base = vms->memmap[VIRT_MMIO].base + i * size;

        nodename = g_strdup_printf("/virtio_mmio@%" PRIx64, base);
        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename,
                                "compatible", "virtio,mmio");
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                     2, base, 2, size);
        qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irq,
                               GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);
        qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
        g_free(nodename);
    }
}

#define VIRT_FLASH_SECTOR_SIZE (256 * KiB)

static PFlashCFI01 *virt_flash_create1(VirtMachineState *vms,
                                        const char *name,
                                        const char *alias_prop_name)
{
    /*
     * Create a single flash device.  We use the same parameters as
     * the flash devices on the Versatile Express board.
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
    object_property_add_child(OBJECT(vms), name, OBJECT(dev));
    object_property_add_alias(OBJECT(vms), alias_prop_name,
                              OBJECT(dev), "drive");
    return PFLASH_CFI01(dev);
}

static void virt_flash_create(VirtMachineState *vms)
{
    vms->flash[0] = virt_flash_create1(vms, "virt.flash0", "pflash0");
    vms->flash[1] = virt_flash_create1(vms, "virt.flash1", "pflash1");
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

static void virt_flash_map(VirtMachineState *vms,
                           MemoryRegion *sysmem,
                           MemoryRegion *secure_sysmem)
{
    /*
     * Map two flash devices to fill the VIRT_FLASH space in the memmap.
     * sysmem is the system memory space. secure_sysmem is the secure view
     * of the system, and the first flash device should be made visible only
     * there. The second flash device is visible to both secure and nonsecure.
     * If sysmem == secure_sysmem this means there is no separate Secure
     * address space and both flash devices are generally visible.
     */
    hwaddr flashsize = vms->memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = vms->memmap[VIRT_FLASH].base;

    virt_flash_map1(vms->flash[0], flashbase, flashsize,
                    secure_sysmem);
    virt_flash_map1(vms->flash[1], flashbase + flashsize, flashsize,
                    sysmem);
}

static void virt_flash_fdt(VirtMachineState *vms,
                           MemoryRegion *sysmem,
                           MemoryRegion *secure_sysmem)
{
    hwaddr flashsize = vms->memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = vms->memmap[VIRT_FLASH].base;
    MachineState *ms = MACHINE(vms);
    char *nodename;

    if (sysmem == secure_sysmem) {
        /* Report both flash devices as a single node in the DT */
        nodename = g_strdup_printf("/flash@%" PRIx64, flashbase);
        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "cfi-flash");
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                     2, flashbase, 2, flashsize,
                                     2, flashbase + flashsize, 2, flashsize);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "bank-width", 4);
        g_free(nodename);
    } else {
        /*
         * Report the devices as separate nodes so we can mark one as
         * only visible to the secure world.
         */
        nodename = g_strdup_printf("/secflash@%" PRIx64, flashbase);
        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "cfi-flash");
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                     2, flashbase, 2, flashsize);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "bank-width", 4);
        qemu_fdt_setprop_string(ms->fdt, nodename, "status", "disabled");
        qemu_fdt_setprop_string(ms->fdt, nodename, "secure-status", "okay");
        g_free(nodename);

        nodename = g_strdup_printf("/flash@%" PRIx64, flashbase + flashsize);
        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", "cfi-flash");
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                     2, flashbase + flashsize, 2, flashsize);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "bank-width", 4);
        g_free(nodename);
    }
}

static bool virt_firmware_init(VirtMachineState *vms,
                               MemoryRegion *sysmem,
                               MemoryRegion *secure_sysmem)
{
    int i;
    const char *bios_name;
    BlockBackend *pflash_blk0;

    /* Map legacy -drive if=pflash to machine properties */
    for (i = 0; i < ARRAY_SIZE(vms->flash); i++) {
        pflash_cfi01_legacy_drive(vms->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }

    virt_flash_map(vms, sysmem, secure_sysmem);

    pflash_blk0 = pflash_cfi01_get_blk(vms->flash[0]);

    bios_name = MACHINE(vms)->firmware;
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
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(vms->flash[0]), 0);
        image_size = load_image_mr(fname, mr);
        g_free(fname);
        if (image_size < 0) {
            error_report("Could not load ROM image '%s'", bios_name);
            exit(1);
        }
    }

    return pflash_blk0 || bios_name;
}

static FWCfgState *create_fw_cfg(const VirtMachineState *vms, AddressSpace *as)
{
    MachineState *ms = MACHINE(vms);
    hwaddr base = vms->memmap[VIRT_FW_CFG].base;
    hwaddr size = vms->memmap[VIRT_FW_CFG].size;
    FWCfgState *fw_cfg;
    char *nodename;

    fw_cfg = fw_cfg_init_mem_wide(base + 8, base, 8, base + 16, as);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)ms->smp.cpus);

    nodename = g_strdup_printf("/fw-cfg@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename,
                            "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
    g_free(nodename);
    return fw_cfg;
}

static void create_pcie_irq_map(const MachineState *ms,
                                uint32_t gic_phandle,
                                int first_irq, const char *nodename)
{
    int devfn, pin;
    uint32_t full_irq_map[4 * 4 * 10] = { 0 };
    uint32_t *irq_map = full_irq_map;

    for (devfn = 0; devfn <= 0x18; devfn += 0x8) {
        for (pin = 0; pin < 4; pin++) {
            int irq_type = GIC_FDT_IRQ_TYPE_SPI;
            int irq_nr = first_irq + ((pin + PCI_SLOT(devfn)) % PCI_NUM_PINS);
            int irq_level = GIC_FDT_IRQ_FLAGS_LEVEL_HI;
            int i;

            uint32_t map[] = {
                devfn << 8, 0, 0,                           /* devfn */
                pin + 1,                                    /* PCI pin */
                gic_phandle, 0, 0, irq_type, irq_nr, irq_level }; /* GIC irq */

            /* Convert map to big endian */
            for (i = 0; i < 10; i++) {
                irq_map[i] = cpu_to_be32(map[i]);
            }
            irq_map += 10;
        }
    }

    qemu_fdt_setprop(ms->fdt, nodename, "interrupt-map",
                     full_irq_map, sizeof(full_irq_map));

    qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupt-map-mask",
                           cpu_to_be16(PCI_DEVFN(3, 0)), /* Slot 3 */
                           0, 0,
                           0x7           /* PCI irq */);
}

static void create_smmu(const VirtMachineState *vms,
                        PCIBus *bus)
{
    char *node;
    const char compat[] = "arm,smmu-v3";
    int irq =  vms->irqmap[VIRT_SMMU];
    int i;
    hwaddr base = vms->memmap[VIRT_SMMU].base;
    hwaddr size = vms->memmap[VIRT_SMMU].size;
    const char irq_names[] = "eventq\0priq\0cmdq-sync\0gerror";
    DeviceState *dev;
    MachineState *ms = MACHINE(vms);

    if (vms->iommu != VIRT_IOMMU_SMMUV3 || !vms->iommu_phandle) {
        return;
    }

    dev = qdev_new("arm-smmuv3");

    object_property_set_link(OBJECT(dev), "primary-bus", OBJECT(bus),
                             &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    for (i = 0; i < NUM_SMMU_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(vms->gic, irq + i));
    }

    node = g_strdup_printf("/smmuv3@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, node);
    qemu_fdt_setprop(ms->fdt, node, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(ms->fdt, node, "reg", 2, base, 2, size);

    qemu_fdt_setprop_cells(ms->fdt, node, "interrupts",
            GIC_FDT_IRQ_TYPE_SPI, irq    , GIC_FDT_IRQ_FLAGS_EDGE_LO_HI,
            GIC_FDT_IRQ_TYPE_SPI, irq + 1, GIC_FDT_IRQ_FLAGS_EDGE_LO_HI,
            GIC_FDT_IRQ_TYPE_SPI, irq + 2, GIC_FDT_IRQ_FLAGS_EDGE_LO_HI,
            GIC_FDT_IRQ_TYPE_SPI, irq + 3, GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);

    qemu_fdt_setprop(ms->fdt, node, "interrupt-names", irq_names,
                     sizeof(irq_names));

    qemu_fdt_setprop(ms->fdt, node, "dma-coherent", NULL, 0);

    qemu_fdt_setprop_cell(ms->fdt, node, "#iommu-cells", 1);

    qemu_fdt_setprop_cell(ms->fdt, node, "phandle", vms->iommu_phandle);
    g_free(node);
}

static void create_virtio_iommu_dt_bindings(VirtMachineState *vms)
{
    const char compat[] = "virtio,pci-iommu\0pci1af4,1057";
    uint16_t bdf = vms->virtio_iommu_bdf;
    MachineState *ms = MACHINE(vms);
    char *node;

    vms->iommu_phandle = qemu_fdt_alloc_phandle(ms->fdt);

    node = g_strdup_printf("%s/virtio_iommu@%x,%x", vms->pciehb_nodename,
                           PCI_SLOT(bdf), PCI_FUNC(bdf));
    qemu_fdt_add_subnode(ms->fdt, node);
    qemu_fdt_setprop(ms->fdt, node, "compatible", compat, sizeof(compat));
    qemu_fdt_setprop_sized_cells(ms->fdt, node, "reg",
                                 1, bdf << 8, 1, 0, 1, 0,
                                 1, 0, 1, 0);

    qemu_fdt_setprop_cell(ms->fdt, node, "#iommu-cells", 1);
    qemu_fdt_setprop_cell(ms->fdt, node, "phandle", vms->iommu_phandle);
    g_free(node);

    qemu_fdt_setprop_cells(ms->fdt, vms->pciehb_nodename, "iommu-map",
                           0x0, vms->iommu_phandle, 0x0, bdf,
                           bdf + 1, vms->iommu_phandle, bdf + 1, 0xffff - bdf);
}

static void create_pcie(VirtMachineState *vms)
{
    hwaddr base_mmio = vms->memmap[VIRT_PCIE_MMIO].base;
    hwaddr size_mmio = vms->memmap[VIRT_PCIE_MMIO].size;
    hwaddr base_mmio_high = vms->memmap[VIRT_HIGH_PCIE_MMIO].base;
    hwaddr size_mmio_high = vms->memmap[VIRT_HIGH_PCIE_MMIO].size;
    hwaddr base_pio = vms->memmap[VIRT_PCIE_PIO].base;
    hwaddr size_pio = vms->memmap[VIRT_PCIE_PIO].size;
    hwaddr base_ecam, size_ecam;
    hwaddr base = base_mmio;
    int nr_pcie_buses;
    int irq = vms->irqmap[VIRT_PCIE];
    MemoryRegion *mmio_alias;
    MemoryRegion *mmio_reg;
    MemoryRegion *ecam_alias;
    MemoryRegion *ecam_reg;
    DeviceState *dev;
    char *nodename;
    int i, ecam_id;
    PCIHostState *pci;
    MachineState *ms = MACHINE(vms);

    dev = qdev_new(TYPE_GPEX_HOST);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    ecam_id = VIRT_ECAM_ID(vms->highmem_ecam);
    base_ecam = vms->memmap[ecam_id].base;
    size_ecam = vms->memmap[ecam_id].size;
    nr_pcie_buses = size_ecam / PCIE_MMCFG_SIZE_MIN;
    /* Map only the first size_ecam bytes of ECAM space */
    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, size_ecam);
    memory_region_add_subregion(get_system_memory(), base_ecam, ecam_alias);

    /* Map the MMIO window into system address space so as to expose
     * the section of PCI MMIO space which starts at the same base address
     * (ie 1:1 mapping for that part of PCI MMIO space visible through
     * the window).
     */
    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, base_mmio, size_mmio);
    memory_region_add_subregion(get_system_memory(), base_mmio, mmio_alias);

    if (vms->highmem_mmio) {
        /* Map high MMIO space */
        MemoryRegion *high_mmio_alias = g_new0(MemoryRegion, 1);

        memory_region_init_alias(high_mmio_alias, OBJECT(dev), "pcie-mmio-high",
                                 mmio_reg, base_mmio_high, size_mmio_high);
        memory_region_add_subregion(get_system_memory(), base_mmio_high,
                                    high_mmio_alias);
    }

    /* Map IO port space */
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, base_pio);

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i,
                           qdev_get_gpio_in(vms->gic, irq + i));
        gpex_set_irq_num(GPEX_HOST(dev), i, irq + i);
    }

    pci = PCI_HOST_BRIDGE(dev);
    pci->bypass_iommu = vms->default_bus_bypass_iommu;
    vms->bus = pci->bus;
    if (vms->bus) {
        for (i = 0; i < nb_nics; i++) {
            NICInfo *nd = &nd_table[i];

            if (!nd->model) {
                nd->model = g_strdup("virtio");
            }

            pci_nic_init_nofail(nd, pci->bus, nd->model, NULL);
        }
    }

    nodename = vms->pciehb_nodename = g_strdup_printf("/pcie@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename,
                            "compatible", "pci-host-ecam-generic");
    qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "pci");
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#address-cells", 3);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "#size-cells", 2);
    qemu_fdt_setprop_cell(ms->fdt, nodename, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(ms->fdt, nodename, "bus-range", 0,
                           nr_pcie_buses - 1);
    qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);

    if (vms->msi_phandle) {
        qemu_fdt_setprop_cells(ms->fdt, nodename, "msi-map",
                               0, vms->msi_phandle, 0, 0x10000);
    }

    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base_ecam, 2, size_ecam);

    if (vms->highmem_mmio) {
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "ranges",
                                     1, FDT_PCI_RANGE_IOPORT, 2, 0,
                                     2, base_pio, 2, size_pio,
                                     1, FDT_PCI_RANGE_MMIO, 2, base_mmio,
                                     2, base_mmio, 2, size_mmio,
                                     1, FDT_PCI_RANGE_MMIO_64BIT,
                                     2, base_mmio_high,
                                     2, base_mmio_high, 2, size_mmio_high);
    } else {
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "ranges",
                                     1, FDT_PCI_RANGE_IOPORT, 2, 0,
                                     2, base_pio, 2, size_pio,
                                     1, FDT_PCI_RANGE_MMIO, 2, base_mmio,
                                     2, base_mmio, 2, size_mmio);
    }

    qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 1);
    create_pcie_irq_map(ms, vms->gic_phandle, irq, nodename);

    if (vms->iommu) {
        vms->iommu_phandle = qemu_fdt_alloc_phandle(ms->fdt);

        switch (vms->iommu) {
        case VIRT_IOMMU_SMMUV3:
            create_smmu(vms, vms->bus);
            qemu_fdt_setprop_cells(ms->fdt, nodename, "iommu-map",
                                   0x0, vms->iommu_phandle, 0x0, 0x10000);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static void create_platform_bus(VirtMachineState *vms)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;
    MemoryRegion *sysmem = get_system_memory();

    dev = qdev_new(TYPE_PLATFORM_BUS_DEVICE);
    dev->id = g_strdup(TYPE_PLATFORM_BUS_DEVICE);
    qdev_prop_set_uint32(dev, "num_irqs", PLATFORM_BUS_NUM_IRQS);
    qdev_prop_set_uint32(dev, "mmio_size", vms->memmap[VIRT_PLATFORM_BUS].size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    vms->platform_bus_dev = dev;

    s = SYS_BUS_DEVICE(dev);
    for (i = 0; i < PLATFORM_BUS_NUM_IRQS; i++) {
        int irq = vms->irqmap[VIRT_PLATFORM_BUS] + i;
        sysbus_connect_irq(s, i, qdev_get_gpio_in(vms->gic, irq));
    }

    memory_region_add_subregion(sysmem,
                                vms->memmap[VIRT_PLATFORM_BUS].base,
                                sysbus_mmio_get_region(s, 0));
}

static void create_tag_ram(MemoryRegion *tag_sysmem,
                           hwaddr base, hwaddr size,
                           const char *name)
{
    MemoryRegion *tagram = g_new(MemoryRegion, 1);

    memory_region_init_ram(tagram, NULL, name, size / 32, &error_fatal);
    memory_region_add_subregion(tag_sysmem, base / 32, tagram);
}

static void create_secure_ram(VirtMachineState *vms,
                              MemoryRegion *secure_sysmem,
                              MemoryRegion *secure_tag_sysmem)
{
    MemoryRegion *secram = g_new(MemoryRegion, 1);
    char *nodename;
    hwaddr base = vms->memmap[VIRT_SECURE_MEM].base;
    hwaddr size = vms->memmap[VIRT_SECURE_MEM].size;
    MachineState *ms = MACHINE(vms);

    memory_region_init_ram(secram, NULL, "virt.secure-ram", size,
                           &error_fatal);
    memory_region_add_subregion(secure_sysmem, base, secram);

    nodename = g_strdup_printf("/secram@%" PRIx64, base);
    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "memory");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg", 2, base, 2, size);
    qemu_fdt_setprop_string(ms->fdt, nodename, "status", "disabled");
    qemu_fdt_setprop_string(ms->fdt, nodename, "secure-status", "okay");

    if (secure_tag_sysmem) {
        create_tag_ram(secure_tag_sysmem, base, size, "mach-virt.secure-tag");
    }

    g_free(nodename);
}

static void *machvirt_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
    const VirtMachineState *board = container_of(binfo, VirtMachineState,
                                                 bootinfo);
    MachineState *ms = MACHINE(board);


    *fdt_size = board->fdt_size;
    return ms->fdt;
}

static void virt_build_smbios(VirtMachineState *vms)
{
    MachineClass *mc = MACHINE_GET_CLASS(vms);
    MachineState *ms = MACHINE(vms);
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    struct smbios_phys_mem_area mem_array;
    const char *product = "QEMU Virtual Machine";

    if (kvm_enabled()) {
        product = "KVM Virtual Machine";
    }

    smbios_set_defaults("QEMU", product,
                        vmc->smbios_old_sys_ver ? "1.0" : mc->name, false,
                        true, SMBIOS_ENTRY_POINT_TYPE_64);

    /* build the array of physical mem area from base_memmap */
    mem_array.address = vms->memmap[VIRT_MEM].base;
    mem_array.length = ms->ram_size;

    smbios_get_tables(ms, &mem_array, 1,
                      &smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len,
                      &error_fatal);

    if (smbios_anchor) {
        fw_cfg_add_file(vms->fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(vms->fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }
}

static
void virt_machine_done(Notifier *notifier, void *data)
{
    VirtMachineState *vms = container_of(notifier, VirtMachineState,
                                         machine_done);
    MachineState *ms = MACHINE(vms);
    ARMCPU *cpu = ARM_CPU(first_cpu);
    struct arm_boot_info *info = &vms->bootinfo;
    AddressSpace *as = arm_boot_address_space(cpu, info);

    /*
     * If the user provided a dtb, we assume the dynamic sysbus nodes
     * already are integrated there. This corresponds to a use case where
     * the dynamic sysbus nodes are complex and their generation is not yet
     * supported. In that case the user can take charge of the guest dt
     * while qemu takes charge of the qom stuff.
     */
    if (info->dtb_filename == NULL) {
        platform_bus_add_all_fdt_nodes(ms->fdt, "/intc",
                                       vms->memmap[VIRT_PLATFORM_BUS].base,
                                       vms->memmap[VIRT_PLATFORM_BUS].size,
                                       vms->irqmap[VIRT_PLATFORM_BUS]);
    }
    if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms) < 0) {
        exit(1);
    }

    fw_cfg_add_extra_pci_roots(vms->bus, vms->fw_cfg);

    virt_acpi_setup(vms);
    virt_build_smbios(vms);
}

static uint64_t virt_cpu_mp_affinity(VirtMachineState *vms, int idx)
{
    uint8_t clustersz = ARM_DEFAULT_CPUS_PER_CLUSTER;
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);

    if (!vmc->disallow_affinity_adjustment) {
        /* Adjust MPIDR like 64-bit KVM hosts, which incorporate the
         * GIC's target-list limitations. 32-bit KVM hosts currently
         * always create clusters of 4 CPUs, but that is expected to
         * change when they gain support for gicv3. When KVM is enabled
         * it will override the changes we make here, therefore our
         * purposes are to make TCG consistent (with 64-bit KVM hosts)
         * and to improve SGI efficiency.
         */
        if (vms->gic_version == VIRT_GIC_VERSION_2) {
            clustersz = GIC_TARGETLIST_BITS;
        } else {
            clustersz = GICV3_TARGETLIST_BITS;
        }
    }
    return arm_cpu_mp_affinity(idx, clustersz);
}

static inline bool *virt_get_high_memmap_enabled(VirtMachineState *vms,
                                                 int index)
{
    bool *enabled_array[] = {
        &vms->highmem_redists,
        &vms->highmem_ecam,
        &vms->highmem_mmio,
    };

    assert(ARRAY_SIZE(extended_memmap) - VIRT_LOWMEMMAP_LAST ==
           ARRAY_SIZE(enabled_array));
    assert(index - VIRT_LOWMEMMAP_LAST < ARRAY_SIZE(enabled_array));

    return enabled_array[index - VIRT_LOWMEMMAP_LAST];
}

static void virt_set_high_memmap(VirtMachineState *vms,
                                 hwaddr base, int pa_bits)
{
    hwaddr region_base, region_size;
    bool *region_enabled, fits;
    int i;

    for (i = VIRT_LOWMEMMAP_LAST; i < ARRAY_SIZE(extended_memmap); i++) {
        region_enabled = virt_get_high_memmap_enabled(vms, i);
        region_base = ROUND_UP(base, extended_memmap[i].size);
        region_size = extended_memmap[i].size;

        vms->memmap[i].base = region_base;
        vms->memmap[i].size = region_size;

        /*
         * Check each device to see if it fits in the PA space,
         * moving highest_gpa as we go. For compatibility, move
         * highest_gpa for disabled fitting devices as well, if
         * the compact layout has been disabled.
         *
         * For each device that doesn't fit, disable it.
         */
        fits = (region_base + region_size) <= BIT_ULL(pa_bits);
        *region_enabled &= fits;
        if (vms->highmem_compact && !*region_enabled) {
            continue;
        }

        base = region_base + region_size;
        if (fits) {
            vms->highest_gpa = base - 1;
        }
    }
}

static void virt_set_memmap(VirtMachineState *vms, int pa_bits)
{
    MachineState *ms = MACHINE(vms);
    hwaddr base, device_memory_base, device_memory_size, memtop;
    int i;

    vms->memmap = extended_memmap;

    for (i = 0; i < ARRAY_SIZE(base_memmap); i++) {
        vms->memmap[i] = base_memmap[i];
    }

    if (ms->ram_slots > ACPI_MAX_RAM_SLOTS) {
        error_report("unsupported number of memory slots: %"PRIu64,
                     ms->ram_slots);
        exit(EXIT_FAILURE);
    }

    /*
     * !highmem is exactly the same as limiting the PA space to 32bit,
     * irrespective of the underlying capabilities of the HW.
     */
    if (!vms->highmem) {
        pa_bits = 32;
    }

    /*
     * We compute the base of the high IO region depending on the
     * amount of initial and device memory. The device memory start/size
     * is aligned on 1GiB. We never put the high IO region below 256GiB
     * so that if maxram_size is < 255GiB we keep the legacy memory map.
     * The device region size assumes 1GiB page max alignment per slot.
     */
    device_memory_base =
        ROUND_UP(vms->memmap[VIRT_MEM].base + ms->ram_size, GiB);
    device_memory_size = ms->maxram_size - ms->ram_size + ms->ram_slots * GiB;

    /* Base address of the high IO region */
    memtop = base = device_memory_base + ROUND_UP(device_memory_size, GiB);
    if (memtop > BIT_ULL(pa_bits)) {
	    error_report("Addressing limited to %d bits, but memory exceeds it by %llu bytes\n",
			 pa_bits, memtop - BIT_ULL(pa_bits));
        exit(EXIT_FAILURE);
    }
    if (base < device_memory_base) {
        error_report("maxmem/slots too huge");
        exit(EXIT_FAILURE);
    }
    if (base < vms->memmap[VIRT_MEM].base + LEGACY_RAMLIMIT_BYTES) {
        base = vms->memmap[VIRT_MEM].base + LEGACY_RAMLIMIT_BYTES;
    }

    /* We know for sure that at least the memory fits in the PA space */
    vms->highest_gpa = memtop - 1;

    virt_set_high_memmap(vms, base, pa_bits);

    if (device_memory_size > 0) {
        ms->device_memory = g_malloc0(sizeof(*ms->device_memory));
        ms->device_memory->base = device_memory_base;
        memory_region_init(&ms->device_memory->mr, OBJECT(vms),
                           "device-memory", device_memory_size);
    }
}

/*
 * finalize_gic_version - Determines the final gic_version
 * according to the gic-version property
 *
 * Default GIC type is v2
 */
static void finalize_gic_version(VirtMachineState *vms)
{
    unsigned int max_cpus = MACHINE(vms)->smp.max_cpus;

    if (kvm_enabled()) {
        int probe_bitmap;

        if (!kvm_irqchip_in_kernel()) {
            switch (vms->gic_version) {
            case VIRT_GIC_VERSION_HOST:
                warn_report(
                    "gic-version=host not relevant with kernel-irqchip=off "
                     "as only userspace GICv2 is supported. Using v2 ...");
                return;
            case VIRT_GIC_VERSION_MAX:
            case VIRT_GIC_VERSION_NOSEL:
                vms->gic_version = VIRT_GIC_VERSION_2;
                return;
            case VIRT_GIC_VERSION_2:
                return;
            case VIRT_GIC_VERSION_3:
                error_report(
                    "gic-version=3 is not supported with kernel-irqchip=off");
                exit(1);
            case VIRT_GIC_VERSION_4:
                error_report(
                    "gic-version=4 is not supported with kernel-irqchip=off");
                exit(1);
            }
        }

        probe_bitmap = kvm_arm_vgic_probe();
        if (!probe_bitmap) {
            error_report("Unable to determine GIC version supported by host");
            exit(1);
        }

        switch (vms->gic_version) {
        case VIRT_GIC_VERSION_HOST:
        case VIRT_GIC_VERSION_MAX:
            if (probe_bitmap & KVM_ARM_VGIC_V3) {
                vms->gic_version = VIRT_GIC_VERSION_3;
            } else {
                vms->gic_version = VIRT_GIC_VERSION_2;
            }
            return;
        case VIRT_GIC_VERSION_NOSEL:
            if ((probe_bitmap & KVM_ARM_VGIC_V2) && max_cpus <= GIC_NCPU) {
                vms->gic_version = VIRT_GIC_VERSION_2;
            } else if (probe_bitmap & KVM_ARM_VGIC_V3) {
                /*
                 * in case the host does not support v2 in-kernel emulation or
                 * the end-user requested more than 8 VCPUs we now default
                 * to v3. In any case defaulting to v2 would be broken.
                 */
                vms->gic_version = VIRT_GIC_VERSION_3;
            } else if (max_cpus > GIC_NCPU) {
                error_report("host only supports in-kernel GICv2 emulation "
                             "but more than 8 vcpus are requested");
                exit(1);
            }
            break;
        case VIRT_GIC_VERSION_2:
        case VIRT_GIC_VERSION_3:
            break;
        case VIRT_GIC_VERSION_4:
            error_report("gic-version=4 is not supported with KVM");
            exit(1);
        }

        /* Check chosen version is effectively supported by the host */
        if (vms->gic_version == VIRT_GIC_VERSION_2 &&
            !(probe_bitmap & KVM_ARM_VGIC_V2)) {
            error_report("host does not support in-kernel GICv2 emulation");
            exit(1);
        } else if (vms->gic_version == VIRT_GIC_VERSION_3 &&
                   !(probe_bitmap & KVM_ARM_VGIC_V3)) {
            error_report("host does not support in-kernel GICv3 emulation");
            exit(1);
        }
        return;
    }

    /* TCG mode */
    switch (vms->gic_version) {
    case VIRT_GIC_VERSION_NOSEL:
        vms->gic_version = VIRT_GIC_VERSION_2;
        break;
    case VIRT_GIC_VERSION_MAX:
        if (module_object_class_by_name("arm-gicv3")) {
            /* CONFIG_ARM_GICV3_TCG was set */
            if (vms->virt) {
                /* GICv4 only makes sense if CPU has EL2 */
                vms->gic_version = VIRT_GIC_VERSION_4;
            } else {
                vms->gic_version = VIRT_GIC_VERSION_3;
            }
        } else {
            vms->gic_version = VIRT_GIC_VERSION_2;
        }
        break;
    case VIRT_GIC_VERSION_HOST:
        error_report("gic-version=host requires KVM");
        exit(1);
    case VIRT_GIC_VERSION_4:
        if (!vms->virt) {
            error_report("gic-version=4 requires virtualization enabled");
            exit(1);
        }
        break;
    case VIRT_GIC_VERSION_2:
    case VIRT_GIC_VERSION_3:
        break;
    }
}

/*
 * virt_cpu_post_init() must be called after the CPUs have
 * been realized and the GIC has been created.
 */
static void virt_cpu_post_init(VirtMachineState *vms, MemoryRegion *sysmem)
{
    int max_cpus = MACHINE(vms)->smp.max_cpus;
    bool aarch64, pmu, steal_time;
    CPUState *cpu;

    aarch64 = object_property_get_bool(OBJECT(first_cpu), "aarch64", NULL);
    pmu = object_property_get_bool(OBJECT(first_cpu), "pmu", NULL);
    steal_time = object_property_get_bool(OBJECT(first_cpu),
                                          "kvm-steal-time", NULL);

    if (kvm_enabled()) {
        hwaddr pvtime_reg_base = vms->memmap[VIRT_PVTIME].base;
        hwaddr pvtime_reg_size = vms->memmap[VIRT_PVTIME].size;

        if (steal_time) {
            MemoryRegion *pvtime = g_new(MemoryRegion, 1);
            hwaddr pvtime_size = max_cpus * PVTIME_SIZE_PER_CPU;

            /* The memory region size must be a multiple of host page size. */
            pvtime_size = REAL_HOST_PAGE_ALIGN(pvtime_size);

            if (pvtime_size > pvtime_reg_size) {
                error_report("pvtime requires a %" HWADDR_PRId
                             " byte memory region for %d CPUs,"
                             " but only %" HWADDR_PRId " has been reserved",
                             pvtime_size, max_cpus, pvtime_reg_size);
                exit(1);
            }

            memory_region_init_ram(pvtime, NULL, "pvtime", pvtime_size, NULL);
            memory_region_add_subregion(sysmem, pvtime_reg_base, pvtime);
        }

        CPU_FOREACH(cpu) {
            if (pmu) {
                assert(arm_feature(&ARM_CPU(cpu)->env, ARM_FEATURE_PMU));
                if (kvm_irqchip_in_kernel()) {
                    kvm_arm_pmu_set_irq(cpu, PPI(VIRTUAL_PMU_IRQ));
                }
                kvm_arm_pmu_init(cpu);
            }
            if (steal_time) {
                kvm_arm_pvtime_init(cpu, pvtime_reg_base +
                                         cpu->cpu_index * PVTIME_SIZE_PER_CPU);
            }
        }
    } else {
        if (aarch64 && vms->highmem) {
            int requested_pa_size = 64 - clz64(vms->highest_gpa);
            int pamax = arm_pamax(ARM_CPU(first_cpu));

            if (pamax < requested_pa_size) {
                error_report("VCPU supports less PA bits (%d) than "
                             "requested by the memory map (%d)",
                             pamax, requested_pa_size);
                exit(1);
            }
        }
    }
}

static void machvirt_init(MachineState *machine)
{
    VirtMachineState *vms = VIRT_MACHINE(machine);
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *possible_cpus;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *secure_sysmem = NULL;
    MemoryRegion *tag_sysmem = NULL;
    MemoryRegion *secure_tag_sysmem = NULL;
    int n, virt_max_cpus;
    bool firmware_loaded;
    bool aarch64 = true;
    bool has_ged = !vmc->no_ged;
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int max_cpus = machine->smp.max_cpus;

    if (!cpu_type_valid(machine->cpu_type)) {
        error_report("mach-virt: CPU type %s not supported", machine->cpu_type);
        exit(1);
    }

    possible_cpus = mc->possible_cpu_arch_ids(machine);

    /*
     * In accelerated mode, the memory map is computed earlier in kvm_type()
     * to create a VM with the right number of IPA bits.
     */
    if (!vms->memmap) {
        Object *cpuobj;
        ARMCPU *armcpu;
        int pa_bits;

        /*
         * Instanciate a temporary CPU object to find out about what
         * we are about to deal with. Once this is done, get rid of
         * the object.
         */
        cpuobj = object_new(possible_cpus->cpus[0].type);
        armcpu = ARM_CPU(cpuobj);

        pa_bits = arm_pamax(armcpu);

        object_unref(cpuobj);

        virt_set_memmap(vms, pa_bits);
    }

    /* We can probe only here because during property set
     * KVM is not available yet
     */
    finalize_gic_version(vms);

    if (vms->secure) {
        /*
         * The Secure view of the world is the same as the NonSecure,
         * but with a few extra devices. Create it as a container region
         * containing the system memory at low priority; any secure-only
         * devices go in at higher priority and take precedence.
         */
        secure_sysmem = g_new(MemoryRegion, 1);
        memory_region_init(secure_sysmem, OBJECT(machine), "secure-memory",
                           UINT64_MAX);
        memory_region_add_subregion_overlap(secure_sysmem, 0, sysmem, -1);
    }

    firmware_loaded = virt_firmware_init(vms, sysmem,
                                         secure_sysmem ?: sysmem);

    /* If we have an EL3 boot ROM then the assumption is that it will
     * implement PSCI itself, so disable QEMU's internal implementation
     * so it doesn't get in the way. Instead of starting secondary
     * CPUs in PSCI powerdown state we will start them all running and
     * let the boot ROM sort them out.
     * The usual case is that we do use QEMU's PSCI implementation;
     * if the guest has EL2 then we will use SMC as the conduit,
     * and otherwise we will use HVC (for backwards compatibility and
     * because if we're using KVM then we must use HVC).
     */
    if (vms->secure && firmware_loaded) {
        vms->psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;
    } else if (vms->virt) {
        vms->psci_conduit = QEMU_PSCI_CONDUIT_SMC;
    } else {
        vms->psci_conduit = QEMU_PSCI_CONDUIT_HVC;
    }

    /*
     * The maximum number of CPUs depends on the GIC version, or on how
     * many redistributors we can fit into the memory map (which in turn
     * depends on whether this is a GICv3 or v4).
     */
    if (vms->gic_version == VIRT_GIC_VERSION_2) {
        virt_max_cpus = GIC_NCPU;
    } else {
        virt_max_cpus = virt_redist_capacity(vms, VIRT_GIC_REDIST);
        if (vms->highmem_redists) {
            virt_max_cpus += virt_redist_capacity(vms, VIRT_HIGH_GIC_REDIST2);
        }
    }

    if (max_cpus > virt_max_cpus) {
        error_report("Number of SMP CPUs requested (%d) exceeds max CPUs "
                     "supported by machine 'mach-virt' (%d)",
                     max_cpus, virt_max_cpus);
        if (vms->gic_version != VIRT_GIC_VERSION_2 && !vms->highmem_redists) {
            error_printf("Try 'highmem-redists=on' for more CPUs\n");
        }

        exit(1);
    }

    if (vms->secure && (kvm_enabled() || hvf_enabled())) {
        error_report("mach-virt: %s does not support providing "
                     "Security extensions (TrustZone) to the guest CPU",
                     kvm_enabled() ? "KVM" : "HVF");
        exit(1);
    }

    if (vms->virt && (kvm_enabled() || hvf_enabled())) {
        error_report("mach-virt: %s does not support providing "
                     "Virtualization extensions to the guest CPU",
                     kvm_enabled() ? "KVM" : "HVF");
        exit(1);
    }

    if (vms->mte && (kvm_enabled() || hvf_enabled())) {
        error_report("mach-virt: %s does not support providing "
                     "MTE to the guest CPU",
                     kvm_enabled() ? "KVM" : "HVF");
        exit(1);
    }

    create_fdt(vms);

    assert(possible_cpus->len == max_cpus);
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

        aarch64 &= object_property_get_bool(cpuobj, "aarch64", NULL);

        if (!vms->secure) {
            object_property_set_bool(cpuobj, "has_el3", false, NULL);
        }

        if (!vms->virt && object_property_find(cpuobj, "has_el2")) {
            object_property_set_bool(cpuobj, "has_el2", false, NULL);
        }

        if (vmc->kvm_no_adjvtime &&
            object_property_find(cpuobj, "kvm-no-adjvtime")) {
            object_property_set_bool(cpuobj, "kvm-no-adjvtime", true, NULL);
        }

        if (vmc->no_kvm_steal_time &&
            object_property_find(cpuobj, "kvm-steal-time")) {
            object_property_set_bool(cpuobj, "kvm-steal-time", false, NULL);
        }

        if (vmc->no_pmu && object_property_find(cpuobj, "pmu")) {
            object_property_set_bool(cpuobj, "pmu", false, NULL);
        }

        if (vmc->no_tcg_lpa2 && object_property_find(cpuobj, "lpa2")) {
            object_property_set_bool(cpuobj, "lpa2", false, NULL);
        }

        if (object_property_find(cpuobj, "reset-cbar")) {
            object_property_set_int(cpuobj, "reset-cbar",
                                    vms->memmap[VIRT_CPUPERIPHS].base,
                                    &error_abort);
        }

        object_property_set_link(cpuobj, "memory", OBJECT(sysmem),
                                 &error_abort);
        if (vms->secure) {
            object_property_set_link(cpuobj, "secure-memory",
                                     OBJECT(secure_sysmem), &error_abort);
        }

        if (vms->mte) {
            /* Create the memory region only once, but link to all cpus. */
            if (!tag_sysmem) {
                /*
                 * The property exists only if MemTag is supported.
                 * If it is, we must allocate the ram to back that up.
                 */
                if (!object_property_find(cpuobj, "tag-memory")) {
                    error_report("MTE requested, but not supported "
                                 "by the guest CPU");
                    exit(1);
                }

                tag_sysmem = g_new(MemoryRegion, 1);
                memory_region_init(tag_sysmem, OBJECT(machine),
                                   "tag-memory", UINT64_MAX / 32);

                if (vms->secure) {
                    secure_tag_sysmem = g_new(MemoryRegion, 1);
                    memory_region_init(secure_tag_sysmem, OBJECT(machine),
                                       "secure-tag-memory", UINT64_MAX / 32);

                    /* As with ram, secure-tag takes precedence over tag.  */
                    memory_region_add_subregion_overlap(secure_tag_sysmem, 0,
                                                        tag_sysmem, -1);
                }
            }

            object_property_set_link(cpuobj, "tag-memory", OBJECT(tag_sysmem),
                                     &error_abort);
            if (vms->secure) {
                object_property_set_link(cpuobj, "secure-tag-memory",
                                         OBJECT(secure_tag_sysmem),
                                         &error_abort);
            }
        }

        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        object_unref(cpuobj);
    }
    fdt_add_timer_nodes(vms);
    fdt_add_cpu_nodes(vms);

    memory_region_add_subregion(sysmem, vms->memmap[VIRT_MEM].base,
                                machine->ram);
    if (machine->device_memory) {
        memory_region_add_subregion(sysmem, machine->device_memory->base,
                                    &machine->device_memory->mr);
    }

    virt_flash_fdt(vms, sysmem, secure_sysmem ?: sysmem);

    create_gic(vms, sysmem);

    virt_cpu_post_init(vms, sysmem);

    fdt_add_pmu_nodes(vms);

    create_uart(vms, VIRT_UART, sysmem, serial_hd(0));

    if (vms->secure) {
        create_secure_ram(vms, secure_sysmem, secure_tag_sysmem);
        create_uart(vms, VIRT_SECURE_UART, secure_sysmem, serial_hd(1));
    }

    if (tag_sysmem) {
        create_tag_ram(tag_sysmem, vms->memmap[VIRT_MEM].base,
                       machine->ram_size, "mach-virt.tag");
    }

    vms->highmem_ecam &= (!firmware_loaded || aarch64);

    create_rtc(vms);

    create_pcie(vms);

    if (has_ged && aarch64 && firmware_loaded && virt_is_acpi_enabled(vms)) {
        vms->acpi_dev = create_acpi_ged(vms);
    } else {
        create_gpio_devices(vms, VIRT_GPIO, sysmem);
    }

    if (vms->secure && !vmc->no_secure_gpio) {
        create_gpio_devices(vms, VIRT_SECURE_GPIO, secure_sysmem);
    }

     /* connect powerdown request */
     vms->powerdown_notifier.notify = virt_powerdown_req;
     qemu_register_powerdown_notifier(&vms->powerdown_notifier);

    /* Create mmio transports, so the user can create virtio backends
     * (which will be automatically plugged in to the transports). If
     * no backend is created the transport will just sit harmlessly idle.
     */
    create_virtio_devices(vms);

    vms->fw_cfg = create_fw_cfg(vms, &address_space_memory);
    rom_set_fw(vms->fw_cfg);

    create_platform_bus(vms);

    if (machine->nvdimms_state->is_enabled) {
        const struct AcpiGenericAddress arm_virt_nvdimm_acpi_dsmio = {
            .space_id = AML_AS_SYSTEM_MEMORY,
            .address = vms->memmap[VIRT_NVDIMM_ACPI].base,
            .bit_width = NVDIMM_ACPI_IO_LEN << 3
        };

        nvdimm_init_acpi_state(machine->nvdimms_state, sysmem,
                               arm_virt_nvdimm_acpi_dsmio,
                               vms->fw_cfg, OBJECT(vms));
    }

    vms->bootinfo.ram_size = machine->ram_size;
    vms->bootinfo.board_id = -1;
    vms->bootinfo.loader_start = vms->memmap[VIRT_MEM].base;
    vms->bootinfo.get_dtb = machvirt_dtb;
    vms->bootinfo.skip_dtb_autoload = true;
    vms->bootinfo.firmware_loaded = firmware_loaded;
    vms->bootinfo.psci_conduit = vms->psci_conduit;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &vms->bootinfo);

    vms->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&vms->machine_done);
}

static bool virt_get_secure(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->secure;
}

static void virt_set_secure(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->secure = value;
}

static bool virt_get_virt(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->virt;
}

static void virt_set_virt(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->virt = value;
}

static bool virt_get_highmem(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->highmem;
}

static void virt_set_highmem(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->highmem = value;
}

static bool virt_get_compact_highmem(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->highmem_compact;
}

static void virt_set_compact_highmem(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->highmem_compact = value;
}

static bool virt_get_highmem_redists(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->highmem_redists;
}

static void virt_set_highmem_redists(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->highmem_redists = value;
}

static bool virt_get_highmem_ecam(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->highmem_ecam;
}

static void virt_set_highmem_ecam(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->highmem_ecam = value;
}

static bool virt_get_highmem_mmio(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->highmem_mmio;
}

static void virt_set_highmem_mmio(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->highmem_mmio = value;
}


static bool virt_get_its(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->its;
}

static void virt_set_its(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->its = value;
}

static bool virt_get_dtb_randomness(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->dtb_randomness;
}

static void virt_set_dtb_randomness(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->dtb_randomness = value;
}

static char *virt_get_oem_id(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return g_strdup(vms->oem_id);
}

static void virt_set_oem_id(Object *obj, const char *value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);
    size_t len = strlen(value);

    if (len > 6) {
        error_setg(errp,
                   "User specified oem-id value is bigger than 6 bytes in size");
        return;
    }

    strncpy(vms->oem_id, value, 6);
}

static char *virt_get_oem_table_id(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return g_strdup(vms->oem_table_id);
}

static void virt_set_oem_table_id(Object *obj, const char *value,
                                  Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);
    size_t len = strlen(value);

    if (len > 8) {
        error_setg(errp,
                   "User specified oem-table-id value is bigger than 8 bytes in size");
        return;
    }
    strncpy(vms->oem_table_id, value, 8);
}


bool virt_is_acpi_enabled(VirtMachineState *vms)
{
    if (vms->acpi == ON_OFF_AUTO_OFF) {
        return false;
    }
    return true;
}

static void virt_get_acpi(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);
    OnOffAuto acpi = vms->acpi;

    visit_type_OnOffAuto(v, name, &acpi, errp);
}

static void virt_set_acpi(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &vms->acpi, errp);
}

static bool virt_get_ras(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->ras;
}

static void virt_set_ras(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->ras = value;
}

static bool virt_get_mte(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->mte;
}

static void virt_set_mte(Object *obj, bool value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->mte = value;
}

static char *virt_get_gic_version(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);
    const char *val;

    switch (vms->gic_version) {
    case VIRT_GIC_VERSION_4:
        val = "4";
        break;
    case VIRT_GIC_VERSION_3:
        val = "3";
        break;
    default:
        val = "2";
        break;
    }
    return g_strdup(val);
}

static void virt_set_gic_version(Object *obj, const char *value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    if (!strcmp(value, "4")) {
        vms->gic_version = VIRT_GIC_VERSION_4;
    } else if (!strcmp(value, "3")) {
        vms->gic_version = VIRT_GIC_VERSION_3;
    } else if (!strcmp(value, "2")) {
        vms->gic_version = VIRT_GIC_VERSION_2;
    } else if (!strcmp(value, "host")) {
        vms->gic_version = VIRT_GIC_VERSION_HOST; /* Will probe later */
    } else if (!strcmp(value, "max")) {
        vms->gic_version = VIRT_GIC_VERSION_MAX; /* Will probe later */
    } else {
        error_setg(errp, "Invalid gic-version value");
        error_append_hint(errp, "Valid values are 3, 2, host, max.\n");
    }
}

static char *virt_get_iommu(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    switch (vms->iommu) {
    case VIRT_IOMMU_NONE:
        return g_strdup("none");
    case VIRT_IOMMU_SMMUV3:
        return g_strdup("smmuv3");
    default:
        g_assert_not_reached();
    }
}

static void virt_set_iommu(Object *obj, const char *value, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    if (!strcmp(value, "smmuv3")) {
        vms->iommu = VIRT_IOMMU_SMMUV3;
    } else if (!strcmp(value, "none")) {
        vms->iommu = VIRT_IOMMU_NONE;
    } else {
        error_setg(errp, "Invalid iommu value");
        error_append_hint(errp, "Valid values are none, smmuv3.\n");
    }
}

static bool virt_get_default_bus_bypass_iommu(Object *obj, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    return vms->default_bus_bypass_iommu;
}

static void virt_set_default_bus_bypass_iommu(Object *obj, bool value,
                                              Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);

    vms->default_bus_bypass_iommu = value;
}

static CpuInstanceProperties
virt_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static int64_t virt_get_default_cpu_node_id(const MachineState *ms, int idx)
{
    int64_t socket_id = ms->possible_cpus->cpus[idx].props.socket_id;

    return socket_id % ms->numa_state->num_nodes;
}

static const CPUArchIdList *virt_possible_cpu_arch_ids(MachineState *ms)
{
    int n;
    unsigned int max_cpus = ms->smp.max_cpus;
    VirtMachineState *vms = VIRT_MACHINE(ms);
    MachineClass *mc = MACHINE_GET_CLASS(vms);

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
            virt_cpu_mp_affinity(vms, n);

        assert(!mc->smp_props.dies_supported);
        ms->possible_cpus->cpus[n].props.has_socket_id = true;
        ms->possible_cpus->cpus[n].props.socket_id =
            n / (ms->smp.clusters * ms->smp.cores * ms->smp.threads);
        ms->possible_cpus->cpus[n].props.has_cluster_id = true;
        ms->possible_cpus->cpus[n].props.cluster_id =
            (n / (ms->smp.cores * ms->smp.threads)) % ms->smp.clusters;
        ms->possible_cpus->cpus[n].props.has_core_id = true;
        ms->possible_cpus->cpus[n].props.core_id =
            (n / ms->smp.threads) % ms->smp.cores;
        ms->possible_cpus->cpus[n].props.has_thread_id = true;
        ms->possible_cpus->cpus[n].props.thread_id =
            n % ms->smp.threads;
    }
    return ms->possible_cpus;
}

static void virt_memory_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                                 Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(hotplug_dev);
    const MachineState *ms = MACHINE(hotplug_dev);
    const bool is_nvdimm = object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM);

    if (!vms->acpi_dev) {
        error_setg(errp,
                   "memory hotplug is not enabled: missing acpi-ged device");
        return;
    }

    if (vms->mte) {
        error_setg(errp, "memory hotplug is not enabled: MTE is enabled");
        return;
    }

    if (is_nvdimm && !ms->nvdimms_state->is_enabled) {
        error_setg(errp, "nvdimm is not enabled: add 'nvdimm=on' to '-M'");
        return;
    }

    pc_dimm_pre_plug(PC_DIMM(dev), MACHINE(hotplug_dev), NULL, errp);
}

static void virt_memory_plug(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(hotplug_dev);
    MachineState *ms = MACHINE(hotplug_dev);
    bool is_nvdimm = object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM);

    pc_dimm_plug(PC_DIMM(dev), MACHINE(vms));

    if (is_nvdimm) {
        nvdimm_plug(ms->nvdimms_state);
    }

    hotplug_handler_plug(HOTPLUG_HANDLER(vms->acpi_dev),
                         dev, &error_abort);
}

static void virt_virtio_md_pci_pre_plug(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    HotplugHandler *hotplug_dev2 = qdev_get_bus_hotplug_handler(dev);
    Error *local_err = NULL;

    if (!hotplug_dev2 && dev->hotplugged) {
        /*
         * Without a bus hotplug handler, we cannot control the plug/unplug
         * order. We should never reach this point when hotplugging on ARM.
         * However, it's nice to add a safety net, similar to what we have
         * on x86.
         */
        error_setg(errp, "hotplug of virtio based memory devices not supported"
                   " on this bus.");
        return;
    }
    /*
     * First, see if we can plug this memory device at all. If that
     * succeeds, branch of to the actual hotplug handler.
     */
    memory_device_pre_plug(MEMORY_DEVICE(dev), MACHINE(hotplug_dev), NULL,
                           &local_err);
    if (!local_err && hotplug_dev2) {
        hotplug_handler_pre_plug(hotplug_dev2, dev, &local_err);
    }
    error_propagate(errp, local_err);
}

static void virt_virtio_md_pci_plug(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    HotplugHandler *hotplug_dev2 = qdev_get_bus_hotplug_handler(dev);
    Error *local_err = NULL;

    /*
     * Plug the memory device first and then branch off to the actual
     * hotplug handler. If that one fails, we can easily undo the memory
     * device bits.
     */
    memory_device_plug(MEMORY_DEVICE(dev), MACHINE(hotplug_dev));
    if (hotplug_dev2) {
        hotplug_handler_plug(hotplug_dev2, dev, &local_err);
        if (local_err) {
            memory_device_unplug(MEMORY_DEVICE(dev), MACHINE(hotplug_dev));
        }
    }
    error_propagate(errp, local_err);
}

static void virt_virtio_md_pci_unplug_request(HotplugHandler *hotplug_dev,
                                              DeviceState *dev, Error **errp)
{
    /* We don't support hot unplug of virtio based memory devices */
    error_setg(errp, "virtio based memory devices cannot be unplugged.");
}


static void virt_machine_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                            DeviceState *dev, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(hotplug_dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        virt_memory_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_MEM_PCI)) {
        virt_virtio_md_pci_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI)) {
        hwaddr db_start = 0, db_end = 0;
        char *resv_prop_str;

        if (vms->iommu != VIRT_IOMMU_NONE) {
            error_setg(errp, "virt machine does not support multiple IOMMUs");
            return;
        }

        switch (vms->msi_controller) {
        case VIRT_MSI_CTRL_NONE:
            return;
        case VIRT_MSI_CTRL_ITS:
            /* GITS_TRANSLATER page */
            db_start = base_memmap[VIRT_GIC_ITS].base + 0x10000;
            db_end = base_memmap[VIRT_GIC_ITS].base +
                     base_memmap[VIRT_GIC_ITS].size - 1;
            break;
        case VIRT_MSI_CTRL_GICV2M:
            /* MSI_SETSPI_NS page */
            db_start = base_memmap[VIRT_GIC_V2M].base;
            db_end = db_start + base_memmap[VIRT_GIC_V2M].size - 1;
            break;
        }
        resv_prop_str = g_strdup_printf("0x%"PRIx64":0x%"PRIx64":%u",
                                        db_start, db_end,
                                        VIRTIO_IOMMU_RESV_MEM_T_MSI);

        object_property_set_uint(OBJECT(dev), "len-reserved-regions", 1, errp);
        object_property_set_str(OBJECT(dev), "reserved-regions[0]",
                                resv_prop_str, errp);
        g_free(resv_prop_str);
    }
}

static void virt_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(hotplug_dev);

    if (vms->platform_bus_dev) {
        MachineClass *mc = MACHINE_GET_CLASS(vms);

        if (device_is_dynamic_sysbus(mc, dev)) {
            platform_bus_link_device(PLATFORM_BUS_DEVICE(vms->platform_bus_dev),
                                     SYS_BUS_DEVICE(dev));
        }
    }
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        virt_memory_plug(hotplug_dev, dev, errp);
    }

    if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_MEM_PCI)) {
        virt_virtio_md_pci_plug(hotplug_dev, dev, errp);
    }

    if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI)) {
        PCIDevice *pdev = PCI_DEVICE(dev);

        vms->iommu = VIRT_IOMMU_VIRTIO;
        vms->virtio_iommu_bdf = pci_get_bdf(pdev);
        create_virtio_iommu_dt_bindings(vms);
    }
}

static void virt_dimm_unplug_request(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(hotplug_dev);

    if (!vms->acpi_dev) {
        error_setg(errp,
                   "memory hotplug is not enabled: missing acpi-ged device");
        return;
    }

    if (object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM)) {
        error_setg(errp, "nvdimm device hot unplug is not supported yet.");
        return;
    }

    hotplug_handler_unplug_request(HOTPLUG_HANDLER(vms->acpi_dev), dev,
                                   errp);
}

static void virt_dimm_unplug(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    VirtMachineState *vms = VIRT_MACHINE(hotplug_dev);
    Error *local_err = NULL;

    hotplug_handler_unplug(HOTPLUG_HANDLER(vms->acpi_dev), dev, &local_err);
    if (local_err) {
        goto out;
    }

    pc_dimm_unplug(PC_DIMM(dev), MACHINE(vms));
    qdev_unrealize(dev);

out:
    error_propagate(errp, local_err);
}

static void virt_machine_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        virt_dimm_unplug_request(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_MEM_PCI)) {
        virt_virtio_md_pci_unplug_request(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "device unplug request for unsupported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void virt_machine_device_unplug_cb(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        virt_dimm_unplug(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "virt: device unplug for unsupported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static HotplugHandler *virt_machine_get_hotplug_handler(MachineState *machine,
                                                        DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev) ||
        object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_MEM_PCI) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_IOMMU_PCI)) {
        return HOTPLUG_HANDLER(machine);
    }
    return NULL;
}

/*
 * for arm64 kvm_type [7-0] encodes the requested number of bits
 * in the IPA address space
 */
static int virt_kvm_type(MachineState *ms, const char *type_str)
{
    VirtMachineState *vms = VIRT_MACHINE(ms);
    int max_vm_pa_size, requested_pa_size;
    bool fixed_ipa;

    max_vm_pa_size = kvm_arm_get_max_vm_ipa_size(ms, &fixed_ipa);

    /* we freeze the memory map to compute the highest gpa */
    virt_set_memmap(vms, max_vm_pa_size);

    requested_pa_size = 64 - clz64(vms->highest_gpa);

    /*
     * KVM requires the IPA size to be at least 32 bits.
     */
    if (requested_pa_size < 32) {
        requested_pa_size = 32;
    }

    if (requested_pa_size > max_vm_pa_size) {
        error_report("-m and ,maxmem option values "
                     "require an IPA range (%d bits) larger than "
                     "the one supported by the host (%d bits)",
                     requested_pa_size, max_vm_pa_size);
        exit(1);
    }
    /*
     * We return the requested PA log size, unless KVM only supports
     * the implicit legacy 40b IPA setting, in which case the kvm_type
     * must be 0.
     */
    return fixed_ipa ? 0 : requested_pa_size;
}

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->init = machvirt_init;
    /* Start with max_cpus set to 512, which is the maximum supported by KVM.
     * The value may be reduced later when we have more information about the
     * configuration of the particular instance.
     */
    mc->max_cpus = 512;
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_VFIO_CALXEDA_XGMAC);
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_VFIO_AMD_XGBE);
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_VFIO_PLATFORM);
#ifdef CONFIG_TPM
    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_TPM_TIS_SYSBUS);
#endif
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->pci_allow_0_address = true;
    /* We know we will never create a pre-ARMv7 CPU which needs 1K pages */
    mc->minimum_page_bits = 12;
    mc->possible_cpu_arch_ids = virt_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = virt_cpu_index_to_props;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a15");
    mc->get_default_cpu_node_id = virt_get_default_cpu_node_id;
    mc->kvm_type = virt_kvm_type;
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = virt_machine_get_hotplug_handler;
    hc->pre_plug = virt_machine_device_pre_plug_cb;
    hc->plug = virt_machine_device_plug_cb;
    hc->unplug_request = virt_machine_device_unplug_request_cb;
    hc->unplug = virt_machine_device_unplug_cb;
    mc->nvdimm_supported = true;
    mc->smp_props.clusters_supported = true;
    mc->auto_enable_numa_with_memhp = true;
    mc->auto_enable_numa_with_memdev = true;
    mc->default_ram_id = "mach-virt.ram";

    object_class_property_add(oc, "acpi", "OnOffAuto",
        virt_get_acpi, virt_set_acpi,
        NULL, NULL);
    object_class_property_set_description(oc, "acpi",
        "Enable ACPI");
    object_class_property_add_bool(oc, "secure", virt_get_secure,
                                   virt_set_secure);
    object_class_property_set_description(oc, "secure",
                                                "Set on/off to enable/disable the ARM "
                                                "Security Extensions (TrustZone)");

    object_class_property_add_bool(oc, "virtualization", virt_get_virt,
                                   virt_set_virt);
    object_class_property_set_description(oc, "virtualization",
                                          "Set on/off to enable/disable emulating a "
                                          "guest CPU which implements the ARM "
                                          "Virtualization Extensions");

    object_class_property_add_bool(oc, "highmem", virt_get_highmem,
                                   virt_set_highmem);
    object_class_property_set_description(oc, "highmem",
                                          "Set on/off to enable/disable using "
                                          "physical address space above 32 bits");

    object_class_property_add_bool(oc, "compact-highmem",
                                   virt_get_compact_highmem,
                                   virt_set_compact_highmem);
    object_class_property_set_description(oc, "compact-highmem",
                                          "Set on/off to enable/disable compact "
                                          "layout for high memory regions");

    object_class_property_add_bool(oc, "highmem-redists",
                                   virt_get_highmem_redists,
                                   virt_set_highmem_redists);
    object_class_property_set_description(oc, "highmem-redists",
                                          "Set on/off to enable/disable high "
                                          "memory region for GICv3 or GICv4 "
                                          "redistributor");

    object_class_property_add_bool(oc, "highmem-ecam",
                                   virt_get_highmem_ecam,
                                   virt_set_highmem_ecam);
    object_class_property_set_description(oc, "highmem-ecam",
                                          "Set on/off to enable/disable high "
                                          "memory region for PCI ECAM");

    object_class_property_add_bool(oc, "highmem-mmio",
                                   virt_get_highmem_mmio,
                                   virt_set_highmem_mmio);
    object_class_property_set_description(oc, "highmem-mmio",
                                          "Set on/off to enable/disable high "
                                          "memory region for PCI MMIO");

    object_class_property_add_str(oc, "gic-version", virt_get_gic_version,
                                  virt_set_gic_version);
    object_class_property_set_description(oc, "gic-version",
                                          "Set GIC version. "
                                          "Valid values are 2, 3, 4, host and max");

    object_class_property_add_str(oc, "iommu", virt_get_iommu, virt_set_iommu);
    object_class_property_set_description(oc, "iommu",
                                          "Set the IOMMU type. "
                                          "Valid values are none and smmuv3");

    object_class_property_add_bool(oc, "default-bus-bypass-iommu",
                                   virt_get_default_bus_bypass_iommu,
                                   virt_set_default_bus_bypass_iommu);
    object_class_property_set_description(oc, "default-bus-bypass-iommu",
                                          "Set on/off to enable/disable "
                                          "bypass_iommu for default root bus");

    object_class_property_add_bool(oc, "ras", virt_get_ras,
                                   virt_set_ras);
    object_class_property_set_description(oc, "ras",
                                          "Set on/off to enable/disable reporting host memory errors "
                                          "to a KVM guest using ACPI and guest external abort exceptions");

    object_class_property_add_bool(oc, "mte", virt_get_mte, virt_set_mte);
    object_class_property_set_description(oc, "mte",
                                          "Set on/off to enable/disable emulating a "
                                          "guest CPU which implements the ARM "
                                          "Memory Tagging Extension");

    object_class_property_add_bool(oc, "its", virt_get_its,
                                   virt_set_its);
    object_class_property_set_description(oc, "its",
                                          "Set on/off to enable/disable "
                                          "ITS instantiation");

    object_class_property_add_bool(oc, "dtb-randomness",
                                   virt_get_dtb_randomness,
                                   virt_set_dtb_randomness);
    object_class_property_set_description(oc, "dtb-randomness",
                                          "Set off to disable passing random or "
                                          "non-deterministic dtb nodes to guest");

    object_class_property_add_bool(oc, "dtb-kaslr-seed",
                                   virt_get_dtb_randomness,
                                   virt_set_dtb_randomness);
    object_class_property_set_description(oc, "dtb-kaslr-seed",
                                          "Deprecated synonym of dtb-randomness");

    object_class_property_add_str(oc, "x-oem-id",
                                  virt_get_oem_id,
                                  virt_set_oem_id);
    object_class_property_set_description(oc, "x-oem-id",
                                          "Override the default value of field OEMID "
                                          "in ACPI table header."
                                          "The string may be up to 6 bytes in size");


    object_class_property_add_str(oc, "x-oem-table-id",
                                  virt_get_oem_table_id,
                                  virt_set_oem_table_id);
    object_class_property_set_description(oc, "x-oem-table-id",
                                          "Override the default value of field OEM Table ID "
                                          "in ACPI table header."
                                          "The string may be up to 8 bytes in size");

}

static void virt_instance_init(Object *obj)
{
    VirtMachineState *vms = VIRT_MACHINE(obj);
    VirtMachineClass *vmc = VIRT_MACHINE_GET_CLASS(vms);

    /* EL3 is disabled by default on virt: this makes us consistent
     * between KVM and TCG for this board, and it also allows us to
     * boot UEFI blobs which assume no TrustZone support.
     */
    vms->secure = false;

    /* EL2 is also disabled by default, for similar reasons */
    vms->virt = false;

    /* High memory is enabled by default */
    vms->highmem = true;
    vms->highmem_compact = !vmc->no_highmem_compact;
    vms->gic_version = VIRT_GIC_VERSION_NOSEL;

    vms->highmem_ecam = !vmc->no_highmem_ecam;
    vms->highmem_mmio = true;
    vms->highmem_redists = true;

    if (vmc->no_its) {
        vms->its = false;
    } else {
        /* Default allows ITS instantiation */
        vms->its = true;

        if (vmc->no_tcg_its) {
            vms->tcg_its = false;
        } else {
            vms->tcg_its = true;
        }
    }

    /* Default disallows iommu instantiation */
    vms->iommu = VIRT_IOMMU_NONE;

    /* The default root bus is attached to iommu by default */
    vms->default_bus_bypass_iommu = false;

    /* Default disallows RAS instantiation */
    vms->ras = false;

    /* MTE is disabled by default.  */
    vms->mte = false;

    /* Supply kaslr-seed and rng-seed by default */
    vms->dtb_randomness = true;

    vms->irqmap = a15irqmap;

    virt_flash_create(vms);

    vms->oem_id = g_strndup(ACPI_BUILD_APPNAME6, 6);
    vms->oem_table_id = g_strndup(ACPI_BUILD_APPNAME8, 8);
}

static const TypeInfo virt_machine_info = {
    .name          = TYPE_VIRT_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(VirtMachineState),
    .class_size    = sizeof(VirtMachineClass),
    .class_init    = virt_machine_class_init,
    .instance_init = virt_instance_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void machvirt_machine_init(void)
{
    type_register_static(&virt_machine_info);
}
type_init(machvirt_machine_init);

static void virt_machine_8_0_options(MachineClass *mc)
{
}
DEFINE_VIRT_MACHINE_AS_LATEST(8, 0)

static void virt_machine_7_2_options(MachineClass *mc)
{
    virt_machine_8_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_2, hw_compat_7_2_len);
}
DEFINE_VIRT_MACHINE(7, 2)

static void virt_machine_7_1_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_7_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_1, hw_compat_7_1_len);
    /* Compact layout for high memory regions was introduced with 7.2 */
    vmc->no_highmem_compact = true;
}
DEFINE_VIRT_MACHINE(7, 1)

static void virt_machine_7_0_options(MachineClass *mc)
{
    virt_machine_7_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_0, hw_compat_7_0_len);
}
DEFINE_VIRT_MACHINE(7, 0)

static void virt_machine_6_2_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_7_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_2, hw_compat_6_2_len);
    vmc->no_tcg_lpa2 = true;
}
DEFINE_VIRT_MACHINE(6, 2)

static void virt_machine_6_1_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_6_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_1, hw_compat_6_1_len);
    mc->smp_props.prefer_sockets = true;
    vmc->no_cpu_topology = true;

    /* qemu ITS was introduced with 6.2 */
    vmc->no_tcg_its = true;
}
DEFINE_VIRT_MACHINE(6, 1)

static void virt_machine_6_0_options(MachineClass *mc)
{
    virt_machine_6_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_0, hw_compat_6_0_len);
}
DEFINE_VIRT_MACHINE(6, 0)

static void virt_machine_5_2_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_6_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_2, hw_compat_5_2_len);
    vmc->no_secure_gpio = true;
}
DEFINE_VIRT_MACHINE(5, 2)

static void virt_machine_5_1_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_5_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_1, hw_compat_5_1_len);
    vmc->no_kvm_steal_time = true;
}
DEFINE_VIRT_MACHINE(5, 1)

static void virt_machine_5_0_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_5_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_5_0, hw_compat_5_0_len);
    mc->numa_mem_supported = true;
    vmc->acpi_expose_flash = true;
    mc->auto_enable_numa_with_memdev = false;
}
DEFINE_VIRT_MACHINE(5, 0)

static void virt_machine_4_2_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_5_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_4_2, hw_compat_4_2_len);
    vmc->kvm_no_adjvtime = true;
}
DEFINE_VIRT_MACHINE(4, 2)

static void virt_machine_4_1_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_4_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_4_1, hw_compat_4_1_len);
    vmc->no_ged = true;
    mc->auto_enable_numa_with_memhp = false;
}
DEFINE_VIRT_MACHINE(4, 1)

static void virt_machine_4_0_options(MachineClass *mc)
{
    virt_machine_4_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_4_0, hw_compat_4_0_len);
}
DEFINE_VIRT_MACHINE(4, 0)

static void virt_machine_3_1_options(MachineClass *mc)
{
    virt_machine_4_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_3_1, hw_compat_3_1_len);
}
DEFINE_VIRT_MACHINE(3, 1)

static void virt_machine_3_0_options(MachineClass *mc)
{
    virt_machine_3_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_3_0, hw_compat_3_0_len);
}
DEFINE_VIRT_MACHINE(3, 0)

static void virt_machine_2_12_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_3_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_12, hw_compat_2_12_len);
    vmc->no_highmem_ecam = true;
    mc->max_cpus = 255;
}
DEFINE_VIRT_MACHINE(2, 12)

static void virt_machine_2_11_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_2_12_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_11, hw_compat_2_11_len);
    vmc->smbios_old_sys_ver = true;
}
DEFINE_VIRT_MACHINE(2, 11)

static void virt_machine_2_10_options(MachineClass *mc)
{
    virt_machine_2_11_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_10, hw_compat_2_10_len);
    /* before 2.11 we never faulted accesses to bad addresses */
    mc->ignore_memory_transaction_failures = true;
}
DEFINE_VIRT_MACHINE(2, 10)

static void virt_machine_2_9_options(MachineClass *mc)
{
    virt_machine_2_10_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_9, hw_compat_2_9_len);
}
DEFINE_VIRT_MACHINE(2, 9)

static void virt_machine_2_8_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_2_9_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_8, hw_compat_2_8_len);
    /* For 2.8 and earlier we falsely claimed in the DT that
     * our timers were edge-triggered, not level-triggered.
     */
    vmc->claim_edge_triggered_timers = true;
}
DEFINE_VIRT_MACHINE(2, 8)

static void virt_machine_2_7_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_2_8_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_7, hw_compat_2_7_len);
    /* ITS was introduced with 2.8 */
    vmc->no_its = true;
    /* Stick with 1K pages for migration compatibility */
    mc->minimum_page_bits = 0;
}
DEFINE_VIRT_MACHINE(2, 7)

static void virt_machine_2_6_options(MachineClass *mc)
{
    VirtMachineClass *vmc = VIRT_MACHINE_CLASS(OBJECT_CLASS(mc));

    virt_machine_2_7_options(mc);
    compat_props_add(mc->compat_props, hw_compat_2_6, hw_compat_2_6_len);
    vmc->disallow_affinity_adjustment = true;
    /* Disable PMU for 2.6 as PMU support was first introduced in 2.7 */
    vmc->no_pmu = true;
}
DEFINE_VIRT_MACHINE(2, 6)
