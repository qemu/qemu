#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/pc-hotplug.h"
#include "hw/mem/pc-dimm.h"
#include "hw/boards.h"
#include "trace.h"

static uint64_t acpi_memory_hotplug_read(void *opaque, hwaddr addr,
                                         unsigned int size)
{
    uint32_t val = 0;
    MemHotplugState *mem_st = opaque;
    MemStatus *mdev;
    Object *o;

    if (mem_st->selector >= mem_st->dev_count) {
        trace_mhp_acpi_invalid_slot_selected(mem_st->selector);
        return 0;
    }

    mdev = &mem_st->devs[mem_st->selector];
    o = OBJECT(mdev->dimm);
    switch (addr) {
    case 0x0: /* Lo part of phys address where DIMM is mapped */
        val = o ? object_property_get_int(o, PC_DIMM_ADDR_PROP, NULL) : 0;
        trace_mhp_acpi_read_addr_lo(mem_st->selector, val);
        break;
    case 0x4: /* Hi part of phys address where DIMM is mapped */
        val = o ? object_property_get_int(o, PC_DIMM_ADDR_PROP, NULL) >> 32 : 0;
        trace_mhp_acpi_read_addr_hi(mem_st->selector, val);
        break;
    case 0x8: /* Lo part of DIMM size */
        val = o ? object_property_get_int(o, PC_DIMM_SIZE_PROP, NULL) : 0;
        trace_mhp_acpi_read_size_lo(mem_st->selector, val);
        break;
    case 0xc: /* Hi part of DIMM size */
        val = o ? object_property_get_int(o, PC_DIMM_SIZE_PROP, NULL) >> 32 : 0;
        trace_mhp_acpi_read_size_hi(mem_st->selector, val);
        break;
    case 0x10: /* node proximity for _PXM method */
        val = o ? object_property_get_int(o, PC_DIMM_NODE_PROP, NULL) : 0;
        trace_mhp_acpi_read_pxm(mem_st->selector, val);
        break;
    case 0x14: /* pack and return is_* fields */
        val |= mdev->is_enabled   ? 1 : 0;
        val |= mdev->is_inserting ? 2 : 0;
        trace_mhp_acpi_read_flags(mem_st->selector, val);
        break;
    default:
        val = ~0;
        break;
    }
    return val;
}

static void acpi_memory_hotplug_write(void *opaque, hwaddr addr, uint64_t data,
                                      unsigned int size)
{
    MemHotplugState *mem_st = opaque;
    MemStatus *mdev;

    if (!mem_st->dev_count) {
        return;
    }

    if (addr) {
        if (mem_st->selector >= mem_st->dev_count) {
            trace_mhp_acpi_invalid_slot_selected(mem_st->selector);
            return;
        }
    }

    switch (addr) {
    case 0x0: /* DIMM slot selector */
        mem_st->selector = data;
        trace_mhp_acpi_write_slot(mem_st->selector);
        break;
    case 0x4: /* _OST event  */
        mdev = &mem_st->devs[mem_st->selector];
        if (data == 1) {
            /* TODO: handle device insert OST event */
        } else if (data == 3) {
            /* TODO: handle device remove OST event */
        }
        mdev->ost_event = data;
        trace_mhp_acpi_write_ost_ev(mem_st->selector, mdev->ost_event);
        break;
    case 0x8: /* _OST status */
        mdev = &mem_st->devs[mem_st->selector];
        mdev->ost_status = data;
        trace_mhp_acpi_write_ost_status(mem_st->selector, mdev->ost_status);
        /* TODO: report async error */
        /* TODO: implement memory removal on guest signal */
        break;
    case 0x14:
        mdev = &mem_st->devs[mem_st->selector];
        if (data & 2) { /* clear insert event */
            mdev->is_inserting  = false;
            trace_mhp_acpi_clear_insert_evt(mem_st->selector);
        }
        break;
    }

}
static const MemoryRegionOps acpi_memory_hotplug_ops = {
    .read = acpi_memory_hotplug_read,
    .write = acpi_memory_hotplug_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

void acpi_memory_hotplug_init(MemoryRegion *as, Object *owner,
                              MemHotplugState *state)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    state->dev_count = machine->ram_slots;
    if (!state->dev_count) {
        return;
    }

    state->devs = g_malloc0(sizeof(*state->devs) * state->dev_count);
    memory_region_init_io(&state->io, owner, &acpi_memory_hotplug_ops, state,
                          "apci-mem-hotplug", ACPI_MEMORY_HOTPLUG_IO_LEN);
    memory_region_add_subregion(as, ACPI_MEMORY_HOTPLUG_BASE, &state->io);
}

void acpi_memory_plug_cb(ACPIREGS *ar, qemu_irq irq, MemHotplugState *mem_st,
                         DeviceState *dev, Error **errp)
{
    MemStatus *mdev;
    Error *local_err = NULL;
    int slot = object_property_get_int(OBJECT(dev), "slot", &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (slot >= mem_st->dev_count) {
        char *dev_path = object_get_canonical_path(OBJECT(dev));
        error_setg(errp, "acpi_memory_plug_cb: "
                   "device [%s] returned invalid memory slot[%d]",
                    dev_path, slot);
        g_free(dev_path);
        return;
    }

    mdev = &mem_st->devs[slot];
    mdev->dimm = dev;
    mdev->is_enabled = true;
    mdev->is_inserting = true;

    /* do ACPI magic */
    ar->gpe.sts[0] |= ACPI_MEMORY_HOTPLUG_STATUS;
    acpi_update_sci(ar, irq);
    return;
}

static const VMStateDescription vmstate_memhp_sts = {
    .name = "memory hotplug device state",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_BOOL(is_enabled, MemStatus),
        VMSTATE_BOOL(is_inserting, MemStatus),
        VMSTATE_UINT32(ost_event, MemStatus),
        VMSTATE_UINT32(ost_status, MemStatus),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_memory_hotplug = {
    .name = "memory hotplug state",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(selector, MemHotplugState),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(devs, MemHotplugState, dev_count,
                                             vmstate_memhp_sts, MemStatus),
        VMSTATE_END_OF_LIST()
    }
};
