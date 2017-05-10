#include "qemu/osdep.h"
#include "hw/boards.h"
#include "hw/acpi/cpu.h"
#include "qapi/error.h"
#include "qapi-event.h"
#include "trace.h"
#include "sysemu/numa.h"

#define ACPI_CPU_HOTPLUG_REG_LEN 12
#define ACPI_CPU_SELECTOR_OFFSET_WR 0
#define ACPI_CPU_FLAGS_OFFSET_RW 4
#define ACPI_CPU_CMD_OFFSET_WR 5
#define ACPI_CPU_CMD_DATA_OFFSET_RW 8

enum {
    CPHP_GET_NEXT_CPU_WITH_EVENT_CMD = 0,
    CPHP_OST_EVENT_CMD = 1,
    CPHP_OST_STATUS_CMD = 2,
    CPHP_CMD_MAX
};

static ACPIOSTInfo *acpi_cpu_device_status(int idx, AcpiCpuStatus *cdev)
{
    ACPIOSTInfo *info = g_new0(ACPIOSTInfo, 1);

    info->slot_type = ACPI_SLOT_TYPE_CPU;
    info->slot = g_strdup_printf("%d", idx);
    info->source = cdev->ost_event;
    info->status = cdev->ost_status;
    if (cdev->cpu) {
        DeviceState *dev = DEVICE(cdev->cpu);
        if (dev->id) {
            info->device = g_strdup(dev->id);
            info->has_device = true;
        }
    }
    return info;
}

void acpi_cpu_ospm_status(CPUHotplugState *cpu_st, ACPIOSTInfoList ***list)
{
    int i;

    for (i = 0; i < cpu_st->dev_count; i++) {
        ACPIOSTInfoList *elem = g_new0(ACPIOSTInfoList, 1);
        elem->value = acpi_cpu_device_status(i, &cpu_st->devs[i]);
        elem->next = NULL;
        **list = elem;
        *list = &elem->next;
    }
}

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
        val |= cdev->is_inserting ? 2 : 0;
        val |= cdev->is_removing  ? 4 : 0;
        trace_cpuhp_acpi_read_flags(cpu_st->selector, val);
        break;
    case ACPI_CPU_CMD_DATA_OFFSET_RW:
        switch (cpu_st->command) {
        case CPHP_GET_NEXT_CPU_WITH_EVENT_CMD:
           val = cpu_st->selector;
           break;
        default:
           break;
        }
        trace_cpuhp_acpi_read_cmd_data(cpu_st->selector, val);
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
    AcpiCpuStatus *cdev;
    ACPIOSTInfo *info;

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
    case ACPI_CPU_FLAGS_OFFSET_RW: /* set is_* fields  */
        cdev = &cpu_st->devs[cpu_st->selector];
        if (data & 2) { /* clear insert event */
            cdev->is_inserting = false;
            trace_cpuhp_acpi_clear_inserting_evt(cpu_st->selector);
        } else if (data & 4) { /* clear remove event */
            cdev->is_removing = false;
            trace_cpuhp_acpi_clear_remove_evt(cpu_st->selector);
        } else if (data & 8) {
            DeviceState *dev = NULL;
            HotplugHandler *hotplug_ctrl = NULL;

            if (!cdev->cpu) {
                trace_cpuhp_acpi_ejecting_invalid_cpu(cpu_st->selector);
                break;
            }

            trace_cpuhp_acpi_ejecting_cpu(cpu_st->selector);
            dev = DEVICE(cdev->cpu);
            hotplug_ctrl = qdev_get_hotplug_handler(dev);
            hotplug_handler_unplug(hotplug_ctrl, dev, NULL);
        }
        break;
    case ACPI_CPU_CMD_OFFSET_WR:
        trace_cpuhp_acpi_write_cmd(cpu_st->selector, data);
        if (data < CPHP_CMD_MAX) {
            cpu_st->command = data;
            if (cpu_st->command == CPHP_GET_NEXT_CPU_WITH_EVENT_CMD) {
                uint32_t iter = cpu_st->selector;

                do {
                    cdev = &cpu_st->devs[iter];
                    if (cdev->is_inserting || cdev->is_removing) {
                        cpu_st->selector = iter;
                        trace_cpuhp_acpi_cpu_has_events(cpu_st->selector,
                            cdev->is_inserting, cdev->is_removing);
                        break;
                    }
                    iter = iter + 1 < cpu_st->dev_count ? iter + 1 : 0;
                } while (iter != cpu_st->selector);
            }
        }
        break;
    case ACPI_CPU_CMD_DATA_OFFSET_RW:
        switch (cpu_st->command) {
        case CPHP_OST_EVENT_CMD: {
           cdev = &cpu_st->devs[cpu_st->selector];
           cdev->ost_event = data;
           trace_cpuhp_acpi_write_ost_ev(cpu_st->selector, cdev->ost_event);
           break;
        }
        case CPHP_OST_STATUS_CMD: {
           cdev = &cpu_st->devs[cpu_st->selector];
           cdev->ost_status = data;
           info = acpi_cpu_device_status(cpu_st->selector, cdev);
           qapi_event_send_acpi_device_ost(info, &error_abort);
           qapi_free_ACPIOSTInfo(info);
           trace_cpuhp_acpi_write_ost_status(cpu_st->selector,
                                             cdev->ost_status);
           break;
        }
        default:
           break;
        }
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
    const CPUArchIdList *id_list;
    int i;

    assert(mc->possible_cpu_arch_ids);
    id_list = mc->possible_cpu_arch_ids(machine);
    state->dev_count = id_list->len;
    state->devs = g_new0(typeof(*state->devs), state->dev_count);
    for (i = 0; i < id_list->len; i++) {
        state->devs[i].cpu =  CPU(id_list->cpus[i].cpu);
        state->devs[i].arch_id = id_list->cpus[i].arch_id;
    }
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
    if (dev->hotplugged) {
        cdev->is_inserting = true;
        acpi_send_event(DEVICE(hotplug_dev), ACPI_CPU_HOTPLUG_STATUS);
    }
}

void acpi_cpu_unplug_request_cb(HotplugHandler *hotplug_dev,
                                CPUHotplugState *cpu_st,
                                DeviceState *dev, Error **errp)
{
    AcpiCpuStatus *cdev;

    cdev = get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }

    cdev->is_removing = true;
    acpi_send_event(DEVICE(hotplug_dev), ACPI_CPU_HOTPLUG_STATUS);
}

void acpi_cpu_unplug_cb(CPUHotplugState *cpu_st,
                        DeviceState *dev, Error **errp)
{
    AcpiCpuStatus *cdev;

    cdev = get_cpu_status(cpu_st, dev);
    if (!cdev) {
        return;
    }

    cdev->cpu = NULL;
}

static const VMStateDescription vmstate_cpuhp_sts = {
    .name = "CPU hotplug device state",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_BOOL(is_inserting, AcpiCpuStatus),
        VMSTATE_BOOL(is_removing, AcpiCpuStatus),
        VMSTATE_UINT32(ost_event, AcpiCpuStatus),
        VMSTATE_UINT32(ost_status, AcpiCpuStatus),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_cpu_hotplug = {
    .name = "CPU hotplug state",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(selector, CPUHotplugState),
        VMSTATE_UINT8(command, CPUHotplugState),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(devs, CPUHotplugState, dev_count,
                                             vmstate_cpuhp_sts, AcpiCpuStatus),
        VMSTATE_END_OF_LIST()
    }
};

#define CPU_NAME_FMT      "C%.03X"
#define CPUHP_RES_DEVICE  "PRES"
#define CPU_LOCK          "CPLK"
#define CPU_STS_METHOD    "CSTA"
#define CPU_SCAN_METHOD   "CSCN"
#define CPU_NOTIFY_METHOD "CTFY"
#define CPU_EJECT_METHOD  "CEJ0"
#define CPU_OST_METHOD    "COST"

#define CPU_ENABLED       "CPEN"
#define CPU_SELECTOR      "CSEL"
#define CPU_COMMAND       "CCMD"
#define CPU_DATA          "CDAT"
#define CPU_INSERT_EVENT  "CINS"
#define CPU_REMOVE_EVENT  "CRMV"
#define CPU_EJECT_EVENT   "CEJ0"

void build_cpus_aml(Aml *table, MachineState *machine, CPUHotplugFeatures opts,
                    hwaddr io_base,
                    const char *res_root,
                    const char *event_handler_method)
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
    const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(machine);
    char *cphp_res_path = g_strdup_printf("%s." CPUHP_RES_DEVICE, res_root);
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
    AcpiDeviceIf *adev = ACPI_DEVICE_IF(obj);

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
        /* (read) 1 if has a insert event. (write) 1 to clear event */
        aml_append(field, aml_named_field(CPU_INSERT_EVENT, 1));
        /* (read) 1 if has a remove event. (write) 1 to clear event */
        aml_append(field, aml_named_field(CPU_REMOVE_EVENT, 1));
        /* initiates device eject, write only */
        aml_append(field, aml_named_field(CPU_EJECT_EVENT, 1));
        aml_append(field, aml_reserved_field(4));
        aml_append(field, aml_named_field(CPU_COMMAND, 8));
        aml_append(cpu_ctrl_dev, field);

        field = aml_field("PRST", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
        /* CPU selector, write only */
        aml_append(field, aml_named_field(CPU_SELECTOR, 32));
        /* flags + cmd + 2byte align */
        aml_append(field, aml_reserved_field(4 * 8));
        aml_append(field, aml_named_field(CPU_DATA, 32));
        aml_append(cpu_ctrl_dev, field);

        if (opts.has_legacy_cphp) {
            method = aml_method("_INI", 0, AML_SERIALIZED);
            /* switch off legacy CPU hotplug HW and use new one,
             * on reboot system is in new mode and writing 0
             * in CPU_SELECTOR selects BSP, which is NOP at
             * the time _INI is called */
            aml_append(method, aml_store(zero, aml_name(CPU_SELECTOR)));
            aml_append(cpu_ctrl_dev, method);
        }
    }
    aml_append(sb_scope, cpu_ctrl_dev);

    cpus_dev = aml_device("\\_SB.CPUS");
    {
        int i;
        Aml *ctrl_lock = aml_name("%s.%s", cphp_res_path, CPU_LOCK);
        Aml *cpu_selector = aml_name("%s.%s", cphp_res_path, CPU_SELECTOR);
        Aml *is_enabled = aml_name("%s.%s", cphp_res_path, CPU_ENABLED);
        Aml *cpu_cmd = aml_name("%s.%s", cphp_res_path, CPU_COMMAND);
        Aml *cpu_data = aml_name("%s.%s", cphp_res_path, CPU_DATA);
        Aml *ins_evt = aml_name("%s.%s", cphp_res_path, CPU_INSERT_EVENT);
        Aml *rm_evt = aml_name("%s.%s", cphp_res_path, CPU_REMOVE_EVENT);
        Aml *ej_evt = aml_name("%s.%s", cphp_res_path, CPU_EJECT_EVENT);

        aml_append(cpus_dev, aml_name_decl("_HID", aml_string("ACPI0010")));
        aml_append(cpus_dev, aml_name_decl("_CID", aml_eisaid("PNP0A05")));

        method = aml_method(CPU_NOTIFY_METHOD, 2, AML_NOTSERIALIZED);
        for (i = 0; i < arch_ids->len; i++) {
            Aml *cpu = aml_name(CPU_NAME_FMT, i);
            Aml *uid = aml_arg(0);
            Aml *event = aml_arg(1);

            ifctx = aml_if(aml_equal(uid, aml_int(i)));
            {
                aml_append(ifctx, aml_notify(cpu, event));
            }
            aml_append(method, ifctx);
        }
        aml_append(cpus_dev, method);

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

        method = aml_method(CPU_EJECT_METHOD, 1, AML_SERIALIZED);
        {
            Aml *idx = aml_arg(0);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(idx, cpu_selector));
            aml_append(method, aml_store(one, ej_evt));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(cpus_dev, method);

        method = aml_method(CPU_SCAN_METHOD, 0, AML_SERIALIZED);
        {
            Aml *else_ctx;
            Aml *while_ctx;
            Aml *has_event = aml_local(0);
            Aml *dev_chk = aml_int(1);
            Aml *eject_req = aml_int(3);
            Aml *next_cpu_cmd = aml_int(CPHP_GET_NEXT_CPU_WITH_EVENT_CMD);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(one, has_event));
            while_ctx = aml_while(aml_equal(has_event, one));
            {
                 /* clear loop exit condition, ins_evt/rm_evt checks
                  * will set it to 1 while next_cpu_cmd returns a CPU
                  * with events */
                 aml_append(while_ctx, aml_store(zero, has_event));
                 aml_append(while_ctx, aml_store(next_cpu_cmd, cpu_cmd));
                 ifctx = aml_if(aml_equal(ins_evt, one));
                 {
                     aml_append(ifctx,
                         aml_call2(CPU_NOTIFY_METHOD, cpu_data, dev_chk));
                     aml_append(ifctx, aml_store(one, ins_evt));
                     aml_append(ifctx, aml_store(one, has_event));
                 }
                 aml_append(while_ctx, ifctx);
                 else_ctx = aml_else();
                 ifctx = aml_if(aml_equal(rm_evt, one));
                 {
                     aml_append(ifctx,
                         aml_call2(CPU_NOTIFY_METHOD, cpu_data, eject_req));
                     aml_append(ifctx, aml_store(one, rm_evt));
                     aml_append(ifctx, aml_store(one, has_event));
                 }
                 aml_append(else_ctx, ifctx);
                 aml_append(while_ctx, else_ctx);
            }
            aml_append(method, while_ctx);
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(cpus_dev, method);

        method = aml_method(CPU_OST_METHOD, 4, AML_SERIALIZED);
        {
            Aml *uid = aml_arg(0);
            Aml *ev_cmd = aml_int(CPHP_OST_EVENT_CMD);
            Aml *st_cmd = aml_int(CPHP_OST_STATUS_CMD);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(uid, cpu_selector));
            aml_append(method, aml_store(ev_cmd, cpu_cmd));
            aml_append(method, aml_store(aml_arg(1), cpu_data));
            aml_append(method, aml_store(st_cmd, cpu_cmd));
            aml_append(method, aml_store(aml_arg(2), cpu_data));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(cpus_dev, method);

        /* build Processor object for each processor */
        for (i = 0; i < arch_ids->len; i++) {
            Aml *dev;
            Aml *uid = aml_int(i);
            GArray *madt_buf = g_array_new(0, 1, 1);
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

            /* build _MAT object */
            assert(adevc && adevc->madt_cpu);
            adevc->madt_cpu(adev, i, arch_ids, madt_buf);
            switch (madt_buf->data[0]) {
            case ACPI_APIC_PROCESSOR: {
                AcpiMadtProcessorApic *apic = (void *)madt_buf->data;
                apic->flags = cpu_to_le32(1);
                break;
            }
            case ACPI_APIC_LOCAL_X2APIC: {
                AcpiMadtProcessorX2Apic *apic = (void *)madt_buf->data;
                apic->flags = cpu_to_le32(1);
                break;
            }
            default:
                assert(0);
            }
            aml_append(dev, aml_name_decl("_MAT",
                aml_buffer(madt_buf->len, (uint8_t *)madt_buf->data)));
            g_array_free(madt_buf, true);

            method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
            aml_append(method, aml_call1(CPU_EJECT_METHOD, uid));
            aml_append(dev, method);

            method = aml_method("_OST", 3, AML_SERIALIZED);
            aml_append(method,
                aml_call4(CPU_OST_METHOD, uid, aml_arg(0),
                          aml_arg(1), aml_arg(2))
            );
            aml_append(dev, method);

            /* Linux guests discard SRAT info for non-present CPUs
             * as a result _PXM is required for all CPUs which might
             * be hot-plugged. For simplicity, add it for all CPUs.
             */
            if (arch_ids->cpus[i].props.has_node_id) {
                aml_append(dev, aml_name_decl("_PXM",
                           aml_int(arch_ids->cpus[i].props.node_id)));
            }

            aml_append(cpus_dev, dev);
        }
    }
    aml_append(sb_scope, cpus_dev);
    aml_append(table, sb_scope);

    method = aml_method(event_handler_method, 0, AML_NOTSERIALIZED);
    aml_append(method, aml_call0("\\_SB.CPUS." CPU_SCAN_METHOD));
    aml_append(table, method);

    g_free(cphp_res_path);
}
