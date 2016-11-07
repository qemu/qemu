#include "qemu/osdep.h"
#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/pc-hotplug.h"
#include "hw/mem/pc-dimm.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"
#include "trace.h"
#include "qapi-event.h"

static ACPIOSTInfo *acpi_memory_device_status(int slot, MemStatus *mdev)
{
    ACPIOSTInfo *info = g_new0(ACPIOSTInfo, 1);

    info->slot_type = ACPI_SLOT_TYPE_DIMM;
    info->slot = g_strdup_printf("%d", slot);
    info->source = mdev->ost_event;
    info->status = mdev->ost_status;
    if (mdev->dimm) {
        DeviceState *dev = DEVICE(mdev->dimm);
        if (dev->id) {
            info->device = g_strdup(dev->id);
            info->has_device = true;
        }
    }
    return info;
}

void acpi_memory_ospm_status(MemHotplugState *mem_st, ACPIOSTInfoList ***list)
{
    int i;

    for (i = 0; i < mem_st->dev_count; i++) {
        ACPIOSTInfoList *elem = g_new0(ACPIOSTInfoList, 1);
        elem->value = acpi_memory_device_status(i, &mem_st->devs[i]);
        elem->next = NULL;
        **list = elem;
        *list = &elem->next;
    }
}

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
        val |= mdev->is_removing  ? 4 : 0;
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
    ACPIOSTInfo *info;
    DeviceState *dev = NULL;
    HotplugHandler *hotplug_ctrl = NULL;
    Error *local_err = NULL;

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
        /* TODO: implement memory removal on guest signal */

        info = acpi_memory_device_status(mem_st->selector, mdev);
        qapi_event_send_acpi_device_ost(info, &error_abort);
        qapi_free_ACPIOSTInfo(info);
        break;
    case 0x14: /* set is_* fields  */
        mdev = &mem_st->devs[mem_st->selector];
        if (data & 2) { /* clear insert event */
            mdev->is_inserting  = false;
            trace_mhp_acpi_clear_insert_evt(mem_st->selector);
        } else if (data & 4) {
            mdev->is_removing = false;
            trace_mhp_acpi_clear_remove_evt(mem_st->selector);
        } else if (data & 8) {
            if (!mdev->is_enabled) {
                trace_mhp_acpi_ejecting_invalid_slot(mem_st->selector);
                break;
            }

            dev = DEVICE(mdev->dimm);
            hotplug_ctrl = qdev_get_hotplug_handler(dev);
            /* call pc-dimm unplug cb */
            hotplug_handler_unplug(hotplug_ctrl, dev, &local_err);
            if (local_err) {
                trace_mhp_acpi_pc_dimm_delete_failed(mem_st->selector);
                qapi_event_send_mem_unplug_error(dev->id,
                                                 error_get_pretty(local_err),
                                                 &error_abort);
                error_free(local_err);
                break;
            }
            trace_mhp_acpi_pc_dimm_deleted(mem_st->selector);
        }
        break;
    default:
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
                          "acpi-mem-hotplug", ACPI_MEMORY_HOTPLUG_IO_LEN);
    memory_region_add_subregion(as, ACPI_MEMORY_HOTPLUG_BASE, &state->io);
}

/**
 * acpi_memory_slot_status:
 * @mem_st: memory hotplug state
 * @dev: device
 * @errp: set in case of an error
 *
 * Obtain a single memory slot status.
 *
 * This function will be called by memory unplug request cb and unplug cb.
 */
static MemStatus *
acpi_memory_slot_status(MemHotplugState *mem_st,
                        DeviceState *dev, Error **errp)
{
    Error *local_err = NULL;
    int slot = object_property_get_int(OBJECT(dev), PC_DIMM_SLOT_PROP,
                                       &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    if (slot >= mem_st->dev_count) {
        char *dev_path = object_get_canonical_path(OBJECT(dev));
        error_setg(errp, "acpi_memory_slot_status: "
                   "device [%s] returned invalid memory slot[%d]",
                    dev_path, slot);
        g_free(dev_path);
        return NULL;
    }

    return &mem_st->devs[slot];
}

void acpi_memory_plug_cb(HotplugHandler *hotplug_dev, MemHotplugState *mem_st,
                         DeviceState *dev, Error **errp)
{
    MemStatus *mdev;
    DeviceClass *dc = DEVICE_GET_CLASS(dev);

    if (!dc->hotpluggable) {
        return;
    }

    mdev = acpi_memory_slot_status(mem_st, dev, errp);
    if (!mdev) {
        return;
    }

    mdev->dimm = dev;
    mdev->is_enabled = true;
    if (dev->hotplugged) {
        mdev->is_inserting = true;
        acpi_send_event(DEVICE(hotplug_dev), ACPI_MEMORY_HOTPLUG_STATUS);
    }
}

void acpi_memory_unplug_request_cb(HotplugHandler *hotplug_dev,
                                   MemHotplugState *mem_st,
                                   DeviceState *dev, Error **errp)
{
    MemStatus *mdev;

    mdev = acpi_memory_slot_status(mem_st, dev, errp);
    if (!mdev) {
        return;
    }

    mdev->is_removing = true;
    acpi_send_event(DEVICE(hotplug_dev), ACPI_MEMORY_HOTPLUG_STATUS);
}

void acpi_memory_unplug_cb(MemHotplugState *mem_st,
                           DeviceState *dev, Error **errp)
{
    MemStatus *mdev;

    mdev = acpi_memory_slot_status(mem_st, dev, errp);
    if (!mdev) {
        return;
    }

    mdev->is_enabled = false;
    mdev->dimm = NULL;
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
