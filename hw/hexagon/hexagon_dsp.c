/*
 * Hexagon DSP Subsystem emulation.  This represents a generic DSP
 * subsystem with few peripherals, like the Compute DSP.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


#include "qemu/osdep.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/hexagon/hexagon.h"
#include "hw/hexagon/hex-subsys.h"
#include "hw/core/loader.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "elf.h"
#include "cpu.h"
#include "migration/cpu.h"
#include "system/system.h"
#include "target/hexagon/internal.h"
#include "system/physmem.h"
#include "system/reset.h"
#include "semihosting/semihost.h"

#include "machine_cfg_v66g_1024.h.inc"
#include "machine_cfg_v68n_1024.h.inc"
#include "machine_cfg_v73nc_1.h.inc"
#include "machine_cfg_v79na_1.h.inc"
#include "machine_cfg_v81dgb_1.h.inc"
#include "machine_cfg_v81qa_1.h.inc"
#include "machine_cfg_v85na_1.h.inc"
#include "machine_cfg_v85qa_1.h.inc"

#define TYPE_HEXAGON_DSP_MACHINE "hexagon-dsp-machine"
#define HEXAGON_DSP_SUBSYSTEM_BASE 0xfc900000
#define HEXAGON_DSP_SUBSYSTEM_SIZE (HEXAGON_QTIMER_OFFSET + 0x1000)
OBJECT_DECLARE_SIMPLE_TYPE(HexagonDspMachineState, HEXAGON_DSP_MACHINE)

struct HexagonDspMachineState {
    HexagonCommonMachineState parent_obj;

    hwaddr isdb_secure_flag;
    hwaddr isdb_trusted_flag;
    uint32_t subsystem_base;
    struct hexagon_machine_config config;
};

static HexagonDspMachineState *current_dms;

static void hex_symbol_callback(const char *st_name, int st_info,
                                uint64_t st_value, uint64_t st_size)
{
    if (!g_strcmp0("isdb_secure_flag", st_name)) {
        current_dms->isdb_secure_flag = st_value;
    }
    if (!g_strcmp0("isdb_trusted_flag", st_name)) {
        current_dms->isdb_trusted_flag = st_value;
    }
}

/* Board init.  */
static struct hexagon_board_boot_info hexagon_binfo;

static void hexagon_load_kernel(HexagonDspMachineState *dms, HexagonCPU *cpu)
{
    uint64_t pentry;
    long kernel_size;

    current_dms = dms;
    kernel_size = load_elf_ram_sym(hexagon_binfo.kernel_filename, NULL, NULL,
                      NULL, &pentry, NULL, NULL,
                      &hexagon_binfo.kernel_elf_flags, 0, EM_HEXAGON, 0, 0,
                      &address_space_memory, false, hex_symbol_callback);
    current_dms = NULL;

    if (kernel_size <= 0) {
        error_report("no kernel file '%s'",
            hexagon_binfo.kernel_filename);
        exit(1);
    }

    qdev_prop_set_uint32(DEVICE(cpu), "exec-start-addr", pentry);
}

static void hexagon_init_bootstrap(HexagonDspMachineState *dms, HexagonCPU *cpu)
{
    MachineState *machine = MACHINE(dms);

    if (machine->kernel_filename) {
        uint32_t mem = 1;

        hexagon_load_kernel(dms, cpu);
        if (dms->isdb_secure_flag) {
            physical_memory_write(dms->isdb_secure_flag,
                                     &mem, sizeof(mem));
        }
        if (dms->isdb_trusted_flag) {
            physical_memory_write(dms->isdb_trusted_flag,
                                     &mem, sizeof(mem));
        }
    }
}

static void do_cpu_reset(void *opaque)
{
    HexagonCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    cpu_reset(cs);
}

static void hexagon_sim_get_subsystem_base(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    HexagonDspMachineState *dms = HEXAGON_DSP_MACHINE(obj);
    uint32_t value = dms->subsystem_base;

    visit_type_uint32(v, name, &value, errp);
}

static void hexagon_sim_set_subsystem_base(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    HexagonDspMachineState *dms = HEXAGON_DSP_MACHINE(obj);
    uint64_t subsystem_end;
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (!value || !QEMU_IS_ALIGNED(value, 64 * KiB)) {
        error_setg(errp, "subsystem-base must be nonzero and 64 KiB aligned");
        return;
    }
    subsystem_end = (uint64_t)value + HEXAGON_DSP_SUBSYSTEM_SIZE;
    if (subsystem_end > (uint64_t)UINT32_MAX + 1) {
        error_setg(errp, "subsystem-base 0x%x requires 0x%x bytes for the "
                   "subsystem, beyond the end of the address space",
                   value, HEXAGON_DSP_SUBSYSTEM_SIZE);
        return;
    }

    dms->subsystem_base = value;
}

static void add_dsp_properties(ObjectClass *oc)
{
    object_class_property_add(oc, "subsystem-base", "uint32",
                              hexagon_sim_get_subsystem_base,
                              hexagon_sim_set_subsystem_base, NULL, NULL);
    object_class_property_set_description(oc, "subsystem-base",
                                          "Simulator subsystem base address");
}

static void hexagon_dsp_init(Object *obj)
{
    HexagonDspMachineState *dms = HEXAGON_DSP_MACHINE(obj);

    dms->subsystem_base = HEXAGON_DSP_SUBSYSTEM_BASE;
}

static void hexagon_common_init(MachineState *machine, Rev_t rev,
                                const struct hexagon_machine_config *m_cfg)
{
    HexagonCommonMachineState *hms = HEXAGON_COMMON_MACHINE(machine);
    HexagonDspMachineState *dms = HEXAGON_DSP_MACHINE(machine);

    memset(&hexagon_binfo, 0, sizeof(hexagon_binfo));
    if (machine->kernel_filename) {
        hexagon_binfo.ram_size = machine->ram_size;
        hexagon_binfo.kernel_filename = machine->kernel_filename;
    }

    machine->enable_graphics = 0;

    dms->config = *m_cfg;
    dms->config.csr_base = dms->subsystem_base;
    dms->config.l2vic_base = dms->subsystem_base + HEXAGON_L2VIC_OFFSET;
    dms->config.qtmr_region = dms->subsystem_base + HEXAGON_QTIMER_OFFSET;
    dms->config.cfgtable.subsystem_base = dms->subsystem_base >> 16;

    hex_subsys_create(hms, &dms->config, rev);

    g_autofree HexagonCPU **cpus = g_new(HexagonCPU *, machine->smp.cpus);

    for (int i = 0; i < machine->smp.cpus; i++) {
        HexagonCPU *cpu = HEXAGON_CPU(object_new(machine->cpu_type));
        qemu_register_reset(do_cpu_reset, cpu);

        qdev_prop_set_uint32(DEVICE(cpu), "htid", i);

        /*
         * CPU #0 is the only CPU running at boot, others must be
         * explicitly enabled via start instruction.
         */
        qdev_prop_set_bit(DEVICE(cpu), "start-powered-off", (i != 0));
        if (i == 0) {
            hexagon_init_bootstrap(dms, cpu);
        }
        hex_subsys_add_cpu(hms, DEVICE(cpu));
        cpus[i] = cpu;
    }

    hex_subsys_realize_cluster(hms);

    for (int i = 0; i < machine->smp.cpus; i++) {
        hex_subsys_realize_cpu(hms, DEVICE(cpus[i]), (i == 0));
    }
}

static void init_mc(MachineClass *mc)
{
    mc->block_default_type = IF_SD;
    mc->default_ram_size = 4 * GiB;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_serial = 1;
    mc->is_default = false;
    mc->max_cpus = 8;
    qemu_semihosting_enable();
}

/* ----------------------------------------------------------------- */
/* Core-specific configuration settings are defined below this line. */
/* Config table values defined in machine_configs.h.inc              */
/* ----------------------------------------------------------------- */

static void v66g_1024_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v66_rev, &v66g_1024);
}

static void v66g_1024_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V66G_1024";
    mc->init = v66g_1024_config_init;
    init_mc(mc);
    mc->is_default = true;
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V66;
    mc->default_cpus = 4;
    add_dsp_properties(oc);
}

static void v68n_1024_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v68_rev, &v68n_1024);
}

static void v68n_1024_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V68N_1024";
    mc->init = v68n_1024_config_init;
    init_mc(mc);
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V68;
    mc->default_cpus = 6;
    add_dsp_properties(oc);
}

static void v73nc_1_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v73nc_1_rev, &v73nc_1);
}

static void v73nc_1_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V73NC_1";
    mc->init = v73nc_1_config_init;
    init_mc(mc);
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V73;
    mc->default_cpus = 6;
}

static void v81dgb_1_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v81dgb_1_rev, &v81dgb_1);
}

static void v79na_1_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v79_rev, &v79na_1);
}

static void v79na_1_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V79NA_1";
    mc->init = v79na_1_config_init;
    init_mc(mc);
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V79;
    mc->default_cpus = 8;
    add_dsp_properties(oc);
}

static void v81dgb_1_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V81DGB_1";
    mc->init = v81dgb_1_config_init;
    init_mc(mc);
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V81;
    mc->max_cpus = 12;
    mc->default_cpus = 12;
    add_dsp_properties(oc);
}

static void v81qa_1_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v81_rev, &v81qa_1);
}

static void v81qa_1_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V81QA_1";
    mc->init = v81qa_1_config_init;
    init_mc(mc);
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V81;
    mc->max_cpus = 12;
    mc->default_cpus = 12;
    add_dsp_properties(oc);
}

static void v85qa_1_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v85qa_1_rev, &v85qa_1);
}

static void v85qa_1_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V85QA_1";
    mc->init = v85qa_1_config_init;
    init_mc(mc);
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V85;
    mc->max_cpus = 12;
    mc->default_cpus = 12;
}

static void v85na_1_config_init(MachineState *machine)
{
    hexagon_common_init(machine, v85na_1_rev, &v85na_1);
}

static void v85na_1_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Hexagon V85NA_1";
    mc->alias = "sim";
    mc->init = v85na_1_config_init;
    init_mc(mc);
    mc->default_cpu_type = TYPE_HEXAGON_CPU_V85;
    mc->max_cpus = 12;
    mc->default_cpus = 12;
}

static const TypeInfo hexagon_machine_types[] = {
    {
        .name = TYPE_HEXAGON_COMMON_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(HexagonCommonMachineState),
        .abstract = true,
    },
    {
        .name = TYPE_HEXAGON_DSP_MACHINE,
        .parent = TYPE_HEXAGON_COMMON_MACHINE,
        .instance_size = sizeof(HexagonDspMachineState),
        .instance_init = hexagon_dsp_init,
        .abstract = true,
    },
    {
        .name = MACHINE_TYPE_NAME("V66G_1024"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v66g_1024_init,
    },
    {
        .name = MACHINE_TYPE_NAME("V68N_1024"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v68n_1024_init,
    },
    {
        .name = MACHINE_TYPE_NAME("V73NC_1"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v73nc_1_init,
    },
    {
        .name = MACHINE_TYPE_NAME("V79NA_1"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v79na_1_init,
    },
    {
        .name = MACHINE_TYPE_NAME("V81DGB_1"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v81dgb_1_init,
    },
    {
        .name = MACHINE_TYPE_NAME("V81QA_1"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v81qa_1_init,
    },
    {
        .name = MACHINE_TYPE_NAME("V85QA_1"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v85qa_1_init,
    },
    {
        .name = MACHINE_TYPE_NAME("V85NA_1"),
        .parent = TYPE_HEXAGON_DSP_MACHINE,
        .class_init = v85na_1_init,
    },
};

DEFINE_TYPES(hexagon_machine_types)
