/*
 * QEMU fw_cfg helpers (X86 specific)
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/numa.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/firmware/smbios.h"
#include "hw/i386/fw_cfg.h"
#include "hw/timer/hpet.h"
#include "hw/nvram/fw_cfg.h"
#include "e820_memory_layout.h"
#include "kvm_i386.h"
#include CONFIG_DEVICES

struct hpet_fw_config hpet_cfg = {.count = UINT8_MAX};

const char *fw_cfg_arch_key_name(uint16_t key)
{
    static const struct {
        uint16_t key;
        const char *name;
    } fw_cfg_arch_wellknown_keys[] = {
        {FW_CFG_ACPI_TABLES, "acpi_tables"},
        {FW_CFG_SMBIOS_ENTRIES, "smbios_entries"},
        {FW_CFG_IRQ0_OVERRIDE, "irq0_override"},
        {FW_CFG_E820_TABLE, "e820_table"},
        {FW_CFG_HPET, "hpet"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(fw_cfg_arch_wellknown_keys); i++) {
        if (fw_cfg_arch_wellknown_keys[i].key == key) {
            return fw_cfg_arch_wellknown_keys[i].name;
        }
    }
    return NULL;
}

void fw_cfg_build_smbios(MachineState *ms, FWCfgState *fw_cfg)
{
#ifdef CONFIG_SMBIOS
    uint8_t *smbios_tables, *smbios_anchor;
    size_t smbios_tables_len, smbios_anchor_len;
    struct smbios_phys_mem_area *mem_array;
    unsigned i, array_count;
    X86CPU *cpu = X86_CPU(ms->possible_cpus->cpus[0].cpu);

    /* tell smbios about cpuid version and features */
    smbios_set_cpuid(cpu->env.cpuid_version, cpu->env.features[FEAT_1_EDX]);

    smbios_tables = smbios_get_table_legacy(ms, &smbios_tables_len);
    if (smbios_tables) {
        fw_cfg_add_bytes(fw_cfg, FW_CFG_SMBIOS_ENTRIES,
                         smbios_tables, smbios_tables_len);
    }

    /* build the array of physical mem area from e820 table */
    mem_array = g_malloc0(sizeof(*mem_array) * e820_get_num_entries());
    for (i = 0, array_count = 0; i < e820_get_num_entries(); i++) {
        uint64_t addr, len;

        if (e820_get_entry(i, E820_RAM, &addr, &len)) {
            mem_array[array_count].address = addr;
            mem_array[array_count].length = len;
            array_count++;
        }
    }
    smbios_get_tables(ms, mem_array, array_count,
                      &smbios_tables, &smbios_tables_len,
                      &smbios_anchor, &smbios_anchor_len);
    g_free(mem_array);

    if (smbios_anchor) {
        fw_cfg_add_file(fw_cfg, "etc/smbios/smbios-tables",
                        smbios_tables, smbios_tables_len);
        fw_cfg_add_file(fw_cfg, "etc/smbios/smbios-anchor",
                        smbios_anchor, smbios_anchor_len);
    }
#endif
}

FWCfgState *fw_cfg_arch_create(MachineState *ms,
                                      uint16_t boot_cpus,
                                      uint16_t apic_id_limit)
{
    FWCfgState *fw_cfg;
    uint64_t *numa_fw_cfg;
    int i;
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *cpus = mc->possible_cpu_arch_ids(ms);
    int nb_numa_nodes = ms->numa_state->num_nodes;

    fw_cfg = fw_cfg_init_io_dma(FW_CFG_IO_BASE, FW_CFG_IO_BASE + 4,
                                &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, boot_cpus);

    /* FW_CFG_MAX_CPUS is a bit confusing/problematic on x86:
     *
     * For machine types prior to 1.8, SeaBIOS needs FW_CFG_MAX_CPUS for
     * building MPTable, ACPI MADT, ACPI CPU hotplug and ACPI SRAT table,
     * that tables are based on xAPIC ID and QEMU<->SeaBIOS interface
     * for CPU hotplug also uses APIC ID and not "CPU index".
     * This means that FW_CFG_MAX_CPUS is not the "maximum number of CPUs",
     * but the "limit to the APIC ID values SeaBIOS may see".
     *
     * So for compatibility reasons with old BIOSes we are stuck with
     * "etc/max-cpus" actually being apic_id_limit
     */
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, apic_id_limit);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, ms->ram_size);
#ifdef CONFIG_ACPI
    fw_cfg_add_bytes(fw_cfg, FW_CFG_ACPI_TABLES,
                     acpi_tables, acpi_tables_len);
#endif
    fw_cfg_add_i32(fw_cfg, FW_CFG_IRQ0_OVERRIDE, 1);

    fw_cfg_add_bytes(fw_cfg, FW_CFG_E820_TABLE,
                     &e820_reserve, sizeof(e820_reserve));
    fw_cfg_add_file(fw_cfg, "etc/e820", e820_table,
                    sizeof(struct e820_entry) * e820_get_num_entries());

    fw_cfg_add_bytes(fw_cfg, FW_CFG_HPET, &hpet_cfg, sizeof(hpet_cfg));
    /* allocate memory for the NUMA channel: one (64bit) word for the number
     * of nodes, one word for each VCPU->node and one word for each node to
     * hold the amount of memory.
     */
    numa_fw_cfg = g_new0(uint64_t, 1 + apic_id_limit + nb_numa_nodes);
    numa_fw_cfg[0] = cpu_to_le64(nb_numa_nodes);
    for (i = 0; i < cpus->len; i++) {
        unsigned int apic_id = cpus->cpus[i].arch_id;
        assert(apic_id < apic_id_limit);
        numa_fw_cfg[apic_id + 1] = cpu_to_le64(cpus->cpus[i].props.node_id);
    }
    for (i = 0; i < nb_numa_nodes; i++) {
        numa_fw_cfg[apic_id_limit + 1 + i] =
            cpu_to_le64(ms->numa_state->nodes[i].node_mem);
    }
    fw_cfg_add_bytes(fw_cfg, FW_CFG_NUMA, numa_fw_cfg,
                     (1 + apic_id_limit + nb_numa_nodes) *
                     sizeof(*numa_fw_cfg));

    return fw_cfg;
}

void fw_cfg_build_feature_control(MachineState *ms, FWCfgState *fw_cfg)
{
    X86CPU *cpu = X86_CPU(ms->possible_cpus->cpus[0].cpu);
    CPUX86State *env = &cpu->env;
    uint32_t unused, ecx, edx;
    uint64_t feature_control_bits = 0;
    uint64_t *val;

    cpu_x86_cpuid(env, 1, 0, &unused, &unused, &ecx, &edx);
    if (ecx & CPUID_EXT_VMX) {
        feature_control_bits |= FEATURE_CONTROL_VMXON_ENABLED_OUTSIDE_SMX;
    }

    if ((edx & (CPUID_EXT2_MCE | CPUID_EXT2_MCA)) ==
        (CPUID_EXT2_MCE | CPUID_EXT2_MCA) &&
        (env->mcg_cap & MCG_LMCE_P)) {
        feature_control_bits |= FEATURE_CONTROL_LMCE;
    }

    if (!feature_control_bits) {
        return;
    }

    val = g_malloc(sizeof(*val));
    *val = cpu_to_le64(feature_control_bits | FEATURE_CONTROL_LOCKED);
    fw_cfg_add_file(fw_cfg, "etc/msr_feature_control", val, sizeof(*val));
}

void fw_cfg_add_acpi_dsdt(Aml *scope, FWCfgState *fw_cfg)
{
    /*
     * when using port i/o, the 8-bit data register *always* overlaps
     * with half of the 16-bit control register. Hence, the total size
     * of the i/o region used is FW_CFG_CTL_SIZE; when using DMA, the
     * DMA control register is located at FW_CFG_DMA_IO_BASE + 4
     */
    Object *obj = OBJECT(fw_cfg);
    uint8_t io_size = object_property_get_bool(obj, "dma_enabled", NULL) ?
        ROUND_UP(FW_CFG_CTL_SIZE, 4) + sizeof(dma_addr_t) :
        FW_CFG_CTL_SIZE;
    Aml *dev = aml_device("FWCF");
    Aml *crs = aml_resource_template();

    aml_append(dev, aml_name_decl("_HID", aml_string("QEMU0002")));

    /* device present, functioning, decoding, not shown in UI */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));

    aml_append(crs,
        aml_io(AML_DECODE16, FW_CFG_IO_BASE, FW_CFG_IO_BASE, 0x01, io_size));

    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}
