#include "qemu/osdep.h"
#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/pc-hotplug.h"
#include "hw/mem/pc-dimm.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"
#include "trace.h"
#include "qapi-event.h"

#define MEMORY_SLOTS_NUMBER          "MDNR"
#define MEMORY_HOTPLUG_IO_REGION     "HPMR"
#define MEMORY_SLOT_ADDR_LOW         "MRBL"
#define MEMORY_SLOT_ADDR_HIGH        "MRBH"
#define MEMORY_SLOT_SIZE_LOW         "MRLL"
#define MEMORY_SLOT_SIZE_HIGH        "MRLH"
#define MEMORY_SLOT_PROXIMITY        "MPX"
#define MEMORY_SLOT_ENABLED          "MES"
#define MEMORY_SLOT_INSERT_EVENT     "MINS"
#define MEMORY_SLOT_REMOVE_EVENT     "MRMV"
#define MEMORY_SLOT_EJECT            "MEJ"
#define MEMORY_SLOT_SLECTOR          "MSEL"
#define MEMORY_SLOT_OST_EVENT        "MOEV"
#define MEMORY_SLOT_OST_STATUS       "MOSC"
#define MEMORY_SLOT_LOCK             "MLCK"
#define MEMORY_SLOT_STATUS_METHOD    "MRST"
#define MEMORY_SLOT_CRS_METHOD       "MCRS"
#define MEMORY_SLOT_OST_METHOD       "MOST"
#define MEMORY_SLOT_PROXIMITY_METHOD "MPXM"
#define MEMORY_SLOT_EJECT_METHOD     "MEJ0"
#define MEMORY_SLOT_NOTIFY_METHOD    "MTFY"
#define MEMORY_SLOT_SCAN_METHOD      "MSCN"
#define MEMORY_HOTPLUG_DEVICE        "MHPD"
#define MEMORY_HOTPLUG_IO_LEN         24
#define MEMORY_DEVICES_CONTAINER     "\\_SB.MHPC"

static uint16_t memhp_io_base;

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
                              MemHotplugState *state, uint16_t io_base)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    state->dev_count = machine->ram_slots;
    if (!state->dev_count) {
        return;
    }

    assert(!memhp_io_base);
    memhp_io_base = io_base;
    state->devs = g_malloc0(sizeof(*state->devs) * state->dev_count);
    memory_region_init_io(&state->io, owner, &acpi_memory_hotplug_ops, state,
                          "acpi-mem-hotplug", MEMORY_HOTPLUG_IO_LEN);
    memory_region_add_subregion(as, memhp_io_base, &state->io);
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

void build_memory_hotplug_aml(Aml *table, uint32_t nr_mem,
                              const char *res_root,
                              const char *event_handler_method)
{
    int i;
    Aml *ifctx;
    Aml *method;
    Aml *dev_container;
    Aml *mem_ctrl_dev;
    char *mhp_res_path;

    if (!memhp_io_base) {
        return;
    }

    mhp_res_path = g_strdup_printf("%s." MEMORY_HOTPLUG_DEVICE, res_root);
    mem_ctrl_dev = aml_device("%s", mhp_res_path);
    {
        Aml *crs;

        aml_append(mem_ctrl_dev, aml_name_decl("_HID", aml_string("PNP0A06")));
        aml_append(mem_ctrl_dev,
            aml_name_decl("_UID", aml_string("Memory hotplug resources")));

        crs = aml_resource_template();
        aml_append(crs,
            aml_io(AML_DECODE16, memhp_io_base, memhp_io_base, 0,
                   MEMORY_HOTPLUG_IO_LEN)
        );
        aml_append(mem_ctrl_dev, aml_name_decl("_CRS", crs));

        aml_append(mem_ctrl_dev, aml_operation_region(
            MEMORY_HOTPLUG_IO_REGION, AML_SYSTEM_IO,
            aml_int(memhp_io_base), MEMORY_HOTPLUG_IO_LEN)
        );

    }
    aml_append(table, mem_ctrl_dev);

    dev_container = aml_device(MEMORY_DEVICES_CONTAINER);
    {
        Aml *field;
        Aml *one = aml_int(1);
        Aml *zero = aml_int(0);
        Aml *ret_val = aml_local(0);
        Aml *slot_arg0 = aml_arg(0);
        Aml *slots_nr = aml_name(MEMORY_SLOTS_NUMBER);
        Aml *ctrl_lock = aml_name(MEMORY_SLOT_LOCK);
        Aml *slot_selector = aml_name(MEMORY_SLOT_SLECTOR);
        char *mmio_path = g_strdup_printf("%s." MEMORY_HOTPLUG_IO_REGION,
                                          mhp_res_path);

        aml_append(dev_container, aml_name_decl("_HID", aml_string("PNP0A06")));
        aml_append(dev_container,
            aml_name_decl("_UID", aml_string("DIMM devices")));

        assert(nr_mem <= ACPI_MAX_RAM_SLOTS);
        aml_append(dev_container,
            aml_name_decl(MEMORY_SLOTS_NUMBER, aml_int(nr_mem))
        );

        field = aml_field(mmio_path, AML_DWORD_ACC,
                          AML_NOLOCK, AML_PRESERVE);
        aml_append(field, /* read only */
            aml_named_field(MEMORY_SLOT_ADDR_LOW, 32));
        aml_append(field, /* read only */
            aml_named_field(MEMORY_SLOT_ADDR_HIGH, 32));
        aml_append(field, /* read only */
            aml_named_field(MEMORY_SLOT_SIZE_LOW, 32));
        aml_append(field, /* read only */
            aml_named_field(MEMORY_SLOT_SIZE_HIGH, 32));
        aml_append(field, /* read only */
            aml_named_field(MEMORY_SLOT_PROXIMITY, 32));
        aml_append(dev_container, field);

        field = aml_field(mmio_path, AML_BYTE_ACC,
                          AML_NOLOCK, AML_WRITE_AS_ZEROS);
        aml_append(field, aml_reserved_field(160 /* bits, Offset(20) */));
        aml_append(field, /* 1 if enabled, read only */
            aml_named_field(MEMORY_SLOT_ENABLED, 1));
        aml_append(field,
            /*(read) 1 if has a insert event. (write) 1 to clear event */
            aml_named_field(MEMORY_SLOT_INSERT_EVENT, 1));
        aml_append(field,
            /* (read) 1 if has a remove event. (write) 1 to clear event */
            aml_named_field(MEMORY_SLOT_REMOVE_EVENT, 1));
        aml_append(field,
            /* initiates device eject, write only */
            aml_named_field(MEMORY_SLOT_EJECT, 1));
        aml_append(dev_container, field);

        field = aml_field(mmio_path, AML_DWORD_ACC,
                          AML_NOLOCK, AML_PRESERVE);
        aml_append(field, /* DIMM selector, write only */
            aml_named_field(MEMORY_SLOT_SLECTOR, 32));
        aml_append(field, /* _OST event code, write only */
            aml_named_field(MEMORY_SLOT_OST_EVENT, 32));
        aml_append(field, /* _OST status code, write only */
            aml_named_field(MEMORY_SLOT_OST_STATUS, 32));
        aml_append(dev_container, field);
        g_free(mmio_path);

        method = aml_method("_STA", 0, AML_NOTSERIALIZED);
        ifctx = aml_if(aml_equal(slots_nr, zero));
        {
            aml_append(ifctx, aml_return(zero));
        }
        aml_append(method, ifctx);
        /* present, functioning, decoding, not shown in UI */
        aml_append(method, aml_return(aml_int(0xB)));
        aml_append(dev_container, method);

        aml_append(dev_container, aml_mutex(MEMORY_SLOT_LOCK, 0));

        method = aml_method(MEMORY_SLOT_SCAN_METHOD, 0, AML_NOTSERIALIZED);
        {
            Aml *else_ctx;
            Aml *while_ctx;
            Aml *idx = aml_local(0);
            Aml *eject_req = aml_int(3);
            Aml *dev_chk = aml_int(1);

            ifctx = aml_if(aml_equal(slots_nr, zero));
            {
                aml_append(ifctx, aml_return(zero));
            }
            aml_append(method, ifctx);

            aml_append(method, aml_store(zero, idx));
            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            /* build AML that:
             * loops over all slots and Notifies DIMMs with
             * Device Check or Eject Request notifications if
             * slot has corresponding status bit set and clears
             * slot status.
             */
            while_ctx = aml_while(aml_lless(idx, slots_nr));
            {
                Aml *ins_evt = aml_name(MEMORY_SLOT_INSERT_EVENT);
                Aml *rm_evt = aml_name(MEMORY_SLOT_REMOVE_EVENT);

                aml_append(while_ctx, aml_store(idx, slot_selector));
                ifctx = aml_if(aml_equal(ins_evt, one));
                {
                    aml_append(ifctx,
                               aml_call2(MEMORY_SLOT_NOTIFY_METHOD,
                                         idx, dev_chk));
                    aml_append(ifctx, aml_store(one, ins_evt));
                }
                aml_append(while_ctx, ifctx);

                else_ctx = aml_else();
                ifctx = aml_if(aml_equal(rm_evt, one));
                {
                    aml_append(ifctx,
                        aml_call2(MEMORY_SLOT_NOTIFY_METHOD,
                                  idx, eject_req));
                    aml_append(ifctx, aml_store(one, rm_evt));
                }
                aml_append(else_ctx, ifctx);
                aml_append(while_ctx, else_ctx);

                aml_append(while_ctx, aml_add(idx, one, idx));
            }
            aml_append(method, while_ctx);
            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(one));
        }
        aml_append(dev_container, method);

        method = aml_method(MEMORY_SLOT_STATUS_METHOD, 1, AML_NOTSERIALIZED);
        {
            Aml *slot_enabled = aml_name(MEMORY_SLOT_ENABLED);

            aml_append(method, aml_store(zero, ret_val));
            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method,
                aml_store(aml_to_integer(slot_arg0), slot_selector));

            ifctx = aml_if(aml_equal(slot_enabled, one));
            {
                aml_append(ifctx, aml_store(aml_int(0xF), ret_val));
            }
            aml_append(method, ifctx);

            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(ret_val));
        }
        aml_append(dev_container, method);

        method = aml_method(MEMORY_SLOT_CRS_METHOD, 1, AML_SERIALIZED);
        {
            Aml *mr64 = aml_name("MR64");
            Aml *mr32 = aml_name("MR32");
            Aml *crs_tmpl = aml_resource_template();
            Aml *minl = aml_name("MINL");
            Aml *minh = aml_name("MINH");
            Aml *maxl =  aml_name("MAXL");
            Aml *maxh =  aml_name("MAXH");
            Aml *lenl = aml_name("LENL");
            Aml *lenh = aml_name("LENH");

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));

            aml_append(crs_tmpl,
                aml_qword_memory(AML_POS_DECODE, AML_MIN_FIXED, AML_MAX_FIXED,
                                 AML_CACHEABLE, AML_READ_WRITE,
                                 0, 0x0, 0xFFFFFFFFFFFFFFFEULL, 0,
                                 0xFFFFFFFFFFFFFFFFULL));
            aml_append(method, aml_name_decl("MR64", crs_tmpl));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(14), "MINL"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(18), "MINH"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(38), "LENL"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(42), "LENH"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(22), "MAXL"));
            aml_append(method,
                aml_create_dword_field(mr64, aml_int(26), "MAXH"));

            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_ADDR_HIGH), minh));
            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_ADDR_LOW), minl));
            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_SIZE_HIGH), lenh));
            aml_append(method,
                aml_store(aml_name(MEMORY_SLOT_SIZE_LOW), lenl));

            /* 64-bit math: MAX = MIN + LEN - 1 */
            aml_append(method, aml_add(minl, lenl, maxl));
            aml_append(method, aml_add(minh, lenh, maxh));
            ifctx = aml_if(aml_lless(maxl, minl));
            {
                aml_append(ifctx, aml_add(maxh, one, maxh));
            }
            aml_append(method, ifctx);
            ifctx = aml_if(aml_lless(maxl, one));
            {
                aml_append(ifctx, aml_subtract(maxh, one, maxh));
            }
            aml_append(method, ifctx);
            aml_append(method, aml_subtract(maxl, one, maxl));

            /* return 32-bit _CRS if addr/size is in low mem */
            /* TODO: remove it since all hotplugged DIMMs are in high mem */
            ifctx = aml_if(aml_equal(maxh, zero));
            {
                crs_tmpl = aml_resource_template();
                aml_append(crs_tmpl,
                    aml_dword_memory(AML_POS_DECODE, AML_MIN_FIXED,
                                     AML_MAX_FIXED, AML_CACHEABLE,
                                     AML_READ_WRITE,
                                     0, 0x0, 0xFFFFFFFE, 0,
                                     0xFFFFFFFF));
                aml_append(ifctx, aml_name_decl("MR32", crs_tmpl));
                aml_append(ifctx,
                    aml_create_dword_field(mr32, aml_int(10), "MIN"));
                aml_append(ifctx,
                    aml_create_dword_field(mr32, aml_int(14), "MAX"));
                aml_append(ifctx,
                    aml_create_dword_field(mr32, aml_int(22), "LEN"));
                aml_append(ifctx, aml_store(minl, aml_name("MIN")));
                aml_append(ifctx, aml_store(maxl, aml_name("MAX")));
                aml_append(ifctx, aml_store(lenl, aml_name("LEN")));

                aml_append(ifctx, aml_release(ctrl_lock));
                aml_append(ifctx, aml_return(mr32));
            }
            aml_append(method, ifctx);

            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(mr64));
        }
        aml_append(dev_container, method);

        method = aml_method(MEMORY_SLOT_PROXIMITY_METHOD, 1,
                            AML_NOTSERIALIZED);
        {
            Aml *proximity = aml_name(MEMORY_SLOT_PROXIMITY);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));
            aml_append(method, aml_store(proximity, ret_val));
            aml_append(method, aml_release(ctrl_lock));
            aml_append(method, aml_return(ret_val));
        }
        aml_append(dev_container, method);

        method = aml_method(MEMORY_SLOT_OST_METHOD, 4, AML_NOTSERIALIZED);
        {
            Aml *ost_evt = aml_name(MEMORY_SLOT_OST_EVENT);
            Aml *ost_status = aml_name(MEMORY_SLOT_OST_STATUS);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));
            aml_append(method, aml_store(aml_arg(1), ost_evt));
            aml_append(method, aml_store(aml_arg(2), ost_status));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(dev_container, method);

        method = aml_method(MEMORY_SLOT_EJECT_METHOD, 2, AML_NOTSERIALIZED);
        {
            Aml *eject = aml_name(MEMORY_SLOT_EJECT);

            aml_append(method, aml_acquire(ctrl_lock, 0xFFFF));
            aml_append(method, aml_store(aml_to_integer(slot_arg0),
                                         slot_selector));
            aml_append(method, aml_store(one, eject));
            aml_append(method, aml_release(ctrl_lock));
        }
        aml_append(dev_container, method);

        /* build memory devices */
        for (i = 0; i < nr_mem; i++) {
            Aml *dev;
            const char *s;

            dev = aml_device("MP%02X", i);
            aml_append(dev, aml_name_decl("_UID", aml_string("0x%02X", i)));
            aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0C80")));

            method = aml_method("_CRS", 0, AML_NOTSERIALIZED);
            s = MEMORY_SLOT_CRS_METHOD;
            aml_append(method, aml_return(aml_call1(s, aml_name("_UID"))));
            aml_append(dev, method);

            method = aml_method("_STA", 0, AML_NOTSERIALIZED);
            s = MEMORY_SLOT_STATUS_METHOD;
            aml_append(method, aml_return(aml_call1(s, aml_name("_UID"))));
            aml_append(dev, method);

            method = aml_method("_PXM", 0, AML_NOTSERIALIZED);
            s = MEMORY_SLOT_PROXIMITY_METHOD;
            aml_append(method, aml_return(aml_call1(s, aml_name("_UID"))));
            aml_append(dev, method);

            method = aml_method("_OST", 3, AML_NOTSERIALIZED);
            s = MEMORY_SLOT_OST_METHOD;
            aml_append(method, aml_return(aml_call4(
                s, aml_name("_UID"), aml_arg(0), aml_arg(1), aml_arg(2)
            )));
            aml_append(dev, method);

            method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
            s = MEMORY_SLOT_EJECT_METHOD;
            aml_append(method, aml_return(aml_call2(
                       s, aml_name("_UID"), aml_arg(0))));
            aml_append(dev, method);

            aml_append(dev_container, dev);
        }

        /* build Method(MEMORY_SLOT_NOTIFY_METHOD, 2) {
         *     If (LEqual(Arg0, 0x00)) {Notify(MP00, Arg1)} ... }
         */
        method = aml_method(MEMORY_SLOT_NOTIFY_METHOD, 2, AML_NOTSERIALIZED);
        for (i = 0; i < nr_mem; i++) {
            ifctx = aml_if(aml_equal(aml_arg(0), aml_int(i)));
            aml_append(ifctx,
                aml_notify(aml_name("MP%.02X", i), aml_arg(1))
            );
            aml_append(method, ifctx);
        }
        aml_append(dev_container, method);
    }
    aml_append(table, dev_container);

    method = aml_method(event_handler_method, 0, AML_NOTSERIALIZED);
    aml_append(method,
        aml_call0(MEMORY_DEVICES_CONTAINER "." MEMORY_SLOT_SCAN_METHOD));
    aml_append(table, method);

    g_free(mhp_res_path);
}
