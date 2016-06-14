#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/acpi/cpu.h"
#include "qapi/error.h"
#include "trace.h"

#define ACPI_CPU_HOTPLUG_REG_LEN 12
#define ACPI_CPU_SELECTOR_OFFSET_WR 0
#define ACPI_CPU_FLAGS_OFFSET_RW 4

static uint64_t cpu_hotplug_rd(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    CPUHotplugState *cpu_st = opaque;
    AcpiCpuStatus *cdev;

    if (cpu_st->selector >= cpu_st->dev_count) {
        return val;
    }

    cdev = &cpu_st->devs[cpu_st->selector];
    switch (addr) {
    case ACPI_CPU_FLAGS_OFFSET_RW: /* pack and return is_* fields */
        val |= cdev->cpu ? 1 : 0;
        trace_cpuhp_acpi_read_flags(cpu_st->selector, val);
        break;
    default:
        break;
    }
    return val;
}

static void cpu_hotplug_wr(void *opaque, hwaddr addr, uint64_t data,
                           unsigned int size)
{
    CPUHotplugState *cpu_st = opaque;

    assert(cpu_st->dev_count);

    if (addr) {
        if (cpu_st->selector >= cpu_st->dev_count) {
            trace_cpuhp_acpi_invalid_idx_selected(cpu_st->selector);
            return;
        }
    }

    switch (addr) {
    case ACPI_CPU_SELECTOR_OFFSET_WR: /* current CPU selector */
        cpu_st->selector = data;
        trace_cpuhp_acpi_write_idx(cpu_st->selector);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps cpu_hotplug_ops = {
    .read = cpu_hotplug_rd,
    .write = cpu_hotplug_wr,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

void cpu_hotplug_hw_init(MemoryRegion *as, Object *owner,
                         CPUHotplugState *state, hwaddr base_addr)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    CPUArchIdList *id_list;
    int i;

    assert(mc->possible_cpu_arch_ids);
    id_list = mc->possible_cpu_arch_ids(machine);
    state->dev_count = id_list->len;
    state->devs = g_new0(typeof(*state->devs), state->dev_count);
    for (i = 0; i < id_list->len; i++) {
        state->devs[i].cpu =  id_list->cpus[i].cpu;
        state->devs[i].arch_id = id_list->cpus[i].arch_id;
    }
    g_free(id_list);
    memory_region_init_io(&state->ctrl_reg, owner, &cpu_hotplug_ops, state,
                          "acpi-mem-hotplug", ACPI_CPU_HOTPLUG_REG_LEN);
    memory_region_add_subregion(as, base_addr, &state->ctrl_reg);
}

static AcpiCpuStatus *get_cpu_status(CPUHotplugState *cpu_st, DeviceState *dev)
{
    CPUClass *k = CPU_GET_CLASS(dev);
    uint64_t cpu_arch_id = k->get_arch_id(CPU(dev));
    int i;

    for (i = 0; i < cpu_st->dev_count; i++) {
        if (cpu_arch_id == cpu_st->devs[i].arch_id) {
            return &cpu_st->devs[i];
        }
    }
    return NULL;
}

void acpi_cpu_plug_cb(HotplugHandler *hotplug_dev,
                      CPUHotplugState *cpu_st, DeviceState *dev, Error **errp)
{
    AcpiCpuStatus *cdev;

    cdev = get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }

    cdev->cpu = CPU(dev);
}

const VMStateDescription vmstate_cpu_hotplug = {
    .name = "CPU hotplug state",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(selector, CPUHotplugState),
        VMSTATE_END_OF_LIST()
    }
};

#define CPU_NAME_FMT      "C%.03X"
#define CPUHP_RES_DEVICE  "PRES"
#define CPU_LOCK          "CPLK"
#define CPU_STS_METHOD    "CSTA"

#define CPU_ENABLED       "CPEN"
#define CPU_SELECTOR      "CSEL"

void build_cpus_aml(Aml *table, MachineState *machine, CPUHotplugFeatures opts,
                    hwaddr io_base,
                    const char *res_root)
{
    Aml *ifctx;
    Aml *field;
    Aml *method;
    Aml *cpu_ctrl_dev;
    Aml *cpus_dev;
    Aml *zero = aml_int(0);
    Aml *one = aml_int(1);
    Aml *sb_scope = aml_scope("_SB");
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(machine);
    char *cphp_res_path = g_strdup_printf("%s." CPUHP_RES_DEVICE, res_root);

    cpu_ctrl_dev = aml_device("%s", cphp_res_path);
    {
        Aml *crs;

        aml_append(cpu_ctrl_dev,
            aml_name_decl("_HID", aml_eisaid("PNP0A06")));
        aml_append(cpu_ctrl_dev,
            aml_name_decl("_UID", aml_string("CPU Hotplug resources")));
        aml_append(cpu_ctrl_dev, aml_mutex(CPU_LOCK, 0));

        crs = aml_resource_template();
        aml_append(crs, aml_io(AML_DECODE16, io_base, io_base, 1,
                               ACPI_CPU_HOTPLUG_REG_LEN));
        aml_append(cpu_ctrl_dev, aml_name_decl("_CRS", crs));

        /* declare CPU hotplug MMIO region with related access fields */
        aml_append(cpu_ctrl_dev,
            aml_operation_region("PRST", AML_SYSTEM_IO, aml_int(io_base),
                                 ACPI_CPU_HOTPLUG_REG_LEN));

        field = aml_field("PRST", AML_BYTE_ACC, AML_NOLOCK,
                          AML_WRITE_AS_ZEROS);
        aml_append(field, aml_reserved_field(ACPI_CPU_FLAGS_OFFSET_RW * 8));
        /* 1 if enabled, read only */
        aml_append(field, aml_named_field(CPU_ENABLED, 1));
        aml_append(cpu_ctrl_dev, field);

        field = aml_field("PRST", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
        /* CPU selector, write only */
        aml_append(field, aml_named_field(CPU_SELECTOR, 32));
        aml_append(cpu_ctrl_dev, field);

    }
    aml_append(sb_scope, cpu_ctrl_dev);

    cpus_dev = aml_device("\\_SB.CPUS");
    {
        int i;
        Aml *ctrl_lock = aml_name("%s.%s", cphp_res_path, CPU_LOCK);
        Aml *cpu_selector = aml_name("%s.%s", cphp_res_path, CPU_SELECTOR);
        Aml *is_enabled = aml_name("%s.%s", cphp_res_path, CPU_ENABLED);

        aml_append(cpus_dev, aml_name_decl("_HID", aml_string("ACPI0010")));
        aml_append(cpus_dev, aml_name_decl("_CID", aml_eisaid("PNP0A05")));

        method = aml_method(CPU_STS_METHOD, 1, AML_SERIALIZED);
        {
            Aml *idx = aml_arg(0);
            Aml *sta = aml_local(0);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(idx, cpu_selector));
            aml_append(method, aml_store(zero, sta));
            ifctx = aml_if(aml_equal(is_enabled, one));
            {
                aml_append(ifctx, aml_store(aml_int(0xF), sta));
            }
            aml_append(method, ifctx);
            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(sta));
        }
        aml_append(cpus_dev, method);

        /* build Processor object for each processor */
        for (i = 0; i < arch_ids->len; i++) {
            Aml *dev;
            Aml *uid = aml_int(i);
            int arch_id = arch_ids->cpus[i].arch_id;

            if (opts.apci_1_compatible && arch_id < 255) {
                dev = aml_processor(i, 0, 0, CPU_NAME_FMT, i);
            } else {
                dev = aml_device(CPU_NAME_FMT, i);
                aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0007")));
                aml_append(dev, aml_name_decl("_UID", uid));
            }

            method = aml_method("_STA", 0, AML_SERIALIZED);
            aml_append(method, aml_return(aml_call1(CPU_STS_METHOD, uid)));
            aml_append(dev, method);

            aml_append(cpus_dev, dev);
        }
    }
    aml_append(sb_scope, cpus_dev);
    aml_append(table, sb_scope);

    g_free(cphp_res_path);
    g_free(arch_ids);
}
