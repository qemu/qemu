/*
 * ARM SBSA Reference Platform emulation
 *
 * Copyright (c) 2018 Linaro Limited
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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "sysemu/numa.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "exec/hwaddr.h"
#include "kvm_arm.h"
#include "hw/arm/boot.h"
#include "hw/boards.h"
#include "hw/intc/arm_gicv3_common.h"

#define RAMLIMIT_GB 8192
#define RAMLIMIT_BYTES (RAMLIMIT_GB * GiB)

enum {
    SBSA_FLASH,
    SBSA_MEM,
    SBSA_CPUPERIPHS,
    SBSA_GIC_DIST,
    SBSA_GIC_REDIST,
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
    SBSA_EHCI,
};

typedef struct MemMapEntry {
    hwaddr base;
    hwaddr size;
} MemMapEntry;

typedef struct {
    MachineState parent;
    struct arm_boot_info bootinfo;
    int smp_cpus;
    void *fdt;
    int fdt_size;
    int psci_conduit;
} SBSAMachineState;

#define TYPE_SBSA_MACHINE   MACHINE_TYPE_NAME("sbsa-ref")
#define SBSA_MACHINE(obj) \
    OBJECT_CHECK(SBSAMachineState, (obj), TYPE_SBSA_MACHINE)

static const MemMapEntry sbsa_ref_memmap[] = {
    /* 512M boot ROM */
    [SBSA_FLASH] =              {          0, 0x20000000 },
    /* 512M secure memory */
    [SBSA_SECURE_MEM] =         { 0x20000000, 0x20000000 },
    /* Space reserved for CPU peripheral devices */
    [SBSA_CPUPERIPHS] =         { 0x40000000, 0x00040000 },
    [SBSA_GIC_DIST] =           { 0x40060000, 0x00010000 },
    [SBSA_GIC_REDIST] =         { 0x40080000, 0x04000000 },
    [SBSA_UART] =               { 0x60000000, 0x00001000 },
    [SBSA_RTC] =                { 0x60010000, 0x00001000 },
    [SBSA_GPIO] =               { 0x60020000, 0x00001000 },
    [SBSA_SECURE_UART] =        { 0x60030000, 0x00001000 },
    [SBSA_SECURE_UART_MM] =     { 0x60040000, 0x00001000 },
    [SBSA_SMMU] =               { 0x60050000, 0x00020000 },
    /* Space here reserved for more SMMUs */
    [SBSA_AHCI] =               { 0x60100000, 0x00010000 },
    [SBSA_EHCI] =               { 0x60110000, 0x00010000 },
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

static void sbsa_ref_init(MachineState *machine)
{
    SBSAMachineState *sms = SBSA_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *secure_sysmem = NULL;
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    const CPUArchIdList *possible_cpus;
    int n, sbsa_max_cpus;

    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("cortex-a57"))) {
        error_report("sbsa-ref: CPU type other than the built-in "
                     "cortex-a57 not supported");
        exit(1);
    }

    if (kvm_enabled()) {
        error_report("sbsa-ref: KVM is not supported for this machine");
        exit(1);
    }

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
        object_property_set_int(cpuobj, possible_cpus->cpus[n].arch_id,
                                "mp-affinity", NULL);

        cs = CPU(cpuobj);
        cs->cpu_index = n;

        numa_cpu_pre_plug(&possible_cpus->cpus[cs->cpu_index], DEVICE(cpuobj),
                          &error_fatal);

        if (object_property_find(cpuobj, "reset-cbar", NULL)) {
            object_property_set_int(cpuobj,
                                    sbsa_ref_memmap[SBSA_CPUPERIPHS].base,
                                    "reset-cbar", &error_abort);
        }

        object_property_set_link(cpuobj, OBJECT(sysmem), "memory",
                                 &error_abort);

        object_property_set_link(cpuobj, OBJECT(secure_sysmem),
                                 "secure-memory", &error_abort);

        object_property_set_bool(cpuobj, true, "realized", &error_fatal);
        object_unref(cpuobj);
    }

    memory_region_allocate_system_memory(ram, NULL, "sbsa-ref.ram",
                                         machine->ram_size);
    memory_region_add_subregion(sysmem, sbsa_ref_memmap[SBSA_MEM].base, ram);

    sms->bootinfo.ram_size = machine->ram_size;
    sms->bootinfo.kernel_filename = machine->kernel_filename;
    sms->bootinfo.nb_cpus = smp_cpus;
    sms->bootinfo.board_id = -1;
    sms->bootinfo.loader_start = sbsa_ref_memmap[SBSA_MEM].base;
    arm_load_kernel(ARM_CPU(first_cpu), &sms->bootinfo);
}

static uint64_t sbsa_ref_cpu_mp_affinity(SBSAMachineState *sms, int idx)
{
    uint8_t clustersz = ARM_DEFAULT_CPUS_PER_CLUSTER;
    return arm_cpu_mp_affinity(idx, clustersz);
}

static const CPUArchIdList *sbsa_ref_possible_cpu_arch_ids(MachineState *ms)
{
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
    return idx % nb_numa_nodes;
}

static void sbsa_ref_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = sbsa_ref_init;
    mc->desc = "QEMU 'SBSA Reference' ARM Virtual Machine";
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a57");
    mc->max_cpus = 512;
    mc->pci_allow_0_address = true;
    mc->minimum_page_bits = 12;
    mc->block_default_type = IF_IDE;
    mc->no_cdrom = 1;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = 4;
    mc->possible_cpu_arch_ids = sbsa_ref_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = sbsa_ref_cpu_index_to_props;
    mc->get_default_cpu_node_id = sbsa_ref_get_default_cpu_node_id;
}

static const TypeInfo sbsa_ref_info = {
    .name          = TYPE_SBSA_MACHINE,
    .parent        = TYPE_MACHINE,
    .class_init    = sbsa_ref_class_init,
    .instance_size = sizeof(SBSAMachineState),
};

static void sbsa_ref_machine_init(void)
{
    type_register_static(&sbsa_ref_info);
}

type_init(sbsa_ref_machine_init);
