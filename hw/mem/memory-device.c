/*
 * Memory Device Interface
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2014 Red Hat Inc
 * Copyright (c) 2018 Red Hat Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/mem/memory-device.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "qemu/range.h"
#include "hw/virtio/vhost.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"
#include "trace.h"

static bool memory_device_is_empty(const MemoryDeviceState *md)
{
    const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(md);
    Error *local_err = NULL;
    MemoryRegion *mr;

    /* dropping const here is fine as we don't touch the memory region */
    mr = mdc->get_memory_region((MemoryDeviceState *)md, &local_err);
    if (local_err) {
        /* Not empty, we'll report errors later when containing the MR again. */
        error_free(local_err);
        return false;
    }
    return !mr;
}

static gint memory_device_addr_sort(gconstpointer a, gconstpointer b)
{
    const MemoryDeviceState *md_a = MEMORY_DEVICE(a);
    const MemoryDeviceState *md_b = MEMORY_DEVICE(b);
    const MemoryDeviceClass *mdc_a = MEMORY_DEVICE_GET_CLASS(a);
    const MemoryDeviceClass *mdc_b = MEMORY_DEVICE_GET_CLASS(b);
    const uint64_t addr_a = mdc_a->get_addr(md_a);
    const uint64_t addr_b = mdc_b->get_addr(md_b);

    if (addr_a > addr_b) {
        return 1;
    } else if (addr_a < addr_b) {
        return -1;
    }
    return 0;
}

static int memory_device_build_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_MEMORY_DEVICE)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized memory devices matter */
            *list = g_slist_insert_sorted(*list, dev, memory_device_addr_sort);
        }
    }

    object_child_foreach(obj, memory_device_build_list, opaque);
    return 0;
}

static unsigned int memory_device_get_memslots(MemoryDeviceState *md)
{
    const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(md);

    if (mdc->get_memslots) {
        return mdc->get_memslots(md);
    }
    return 1;
}

/*
 * Memslots that are reserved by memory devices (required but still reported
 * as free from KVM / vhost).
 */
static unsigned int get_reserved_memslots(MachineState *ms)
{
    if (ms->device_memory->used_memslots >
        ms->device_memory->required_memslots) {
        /* This is unexpected, and we warned already in the memory notifier. */
        return 0;
    }
    return ms->device_memory->required_memslots -
           ms->device_memory->used_memslots;
}

unsigned int memory_devices_get_reserved_memslots(void)
{
    if (!current_machine->device_memory) {
        return 0;
    }
    return get_reserved_memslots(current_machine);
}

bool memory_devices_memslot_auto_decision_active(void)
{
    if (!current_machine->device_memory) {
        return false;
    }

    return current_machine->device_memory->memslot_auto_decision_active;
}

static unsigned int memory_device_memslot_decision_limit(MachineState *ms,
                                                         MemoryRegion *mr)
{
    const unsigned int reserved = get_reserved_memslots(ms);
    const uint64_t size = memory_region_size(mr);
    unsigned int max = vhost_get_max_memslots();
    unsigned int free = vhost_get_free_memslots();
    uint64_t available_space;
    unsigned int memslots;

    if (kvm_enabled()) {
        max = MIN(max, kvm_get_max_memslots());
        free = MIN(free, kvm_get_free_memslots());
    }

    /*
     * If we only have less overall memslots than what we consider reasonable,
     * just keep it to a minimum.
     */
    if (max < MEMORY_DEVICES_SAFE_MAX_MEMSLOTS) {
        return 1;
    }

    /*
     * Consider our soft-limit across all memory devices. We don't really
     * expect to exceed this limit in reasonable configurations.
     */
    if (MEMORY_DEVICES_SOFT_MEMSLOT_LIMIT <=
        ms->device_memory->required_memslots) {
        return 1;
    }
    memslots = MEMORY_DEVICES_SOFT_MEMSLOT_LIMIT -
               ms->device_memory->required_memslots;

    /*
     * Consider the actually still free memslots. This is only relevant if
     * other memslot consumers would consume *significantly* more memslots than
     * what we prepared for (> 253). Unlikely, but let's just handle it
     * cleanly.
     */
    memslots = MIN(memslots, free - reserved);
    if (memslots < 1 || unlikely(free < reserved)) {
        return 1;
    }

    /* We cannot have any other memory devices? So give all to this device. */
    if (size == ms->maxram_size - ms->ram_size) {
        return memslots;
    }

    /*
     * Simple heuristic: equally distribute the memslots over the space
     * still available for memory devices.
     */
    available_space = ms->maxram_size - ms->ram_size -
                      ms->device_memory->used_region_size;
    memslots = (double)memslots * size / available_space;
    return memslots < 1 ? 1 : memslots;
}

static void memory_device_check_addable(MachineState *ms, MemoryDeviceState *md,
                                        MemoryRegion *mr, Error **errp)
{
    const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(md);
    const uint64_t used_region_size = ms->device_memory->used_region_size;
    const uint64_t size = memory_region_size(mr);
    const unsigned int reserved_memslots = get_reserved_memslots(ms);
    unsigned int required_memslots, memslot_limit;

    /*
     * Instruct the device to decide how many memslots to use, if applicable,
     * before we query the number of required memslots the first time.
     */
    if (mdc->decide_memslots) {
        memslot_limit = memory_device_memslot_decision_limit(ms, mr);
        mdc->decide_memslots(md, memslot_limit);
    }
    required_memslots = memory_device_get_memslots(md);

    /* we will need memory slots for kvm and vhost */
    if (kvm_enabled() &&
        kvm_get_free_memslots() < required_memslots + reserved_memslots) {
        error_setg(errp, "hypervisor has not enough free memory slots left");
        return;
    }
    if (vhost_get_free_memslots() < required_memslots + reserved_memslots) {
        error_setg(errp, "a used vhost backend has not enough free memory slots left");
        return;
    }

    /* will we exceed the total amount of memory specified */
    if (used_region_size + size < used_region_size ||
        used_region_size + size > ms->maxram_size - ms->ram_size) {
        error_setg(errp, "not enough space, currently 0x%" PRIx64
                   " in use of total space for memory devices 0x" RAM_ADDR_FMT,
                   used_region_size, ms->maxram_size - ms->ram_size);
        return;
    }

}

static uint64_t memory_device_get_free_addr(MachineState *ms,
                                            const uint64_t *hint,
                                            uint64_t align, uint64_t size,
                                            Error **errp)
{
    GSList *list = NULL, *item;
    Range as, new = range_empty;

    range_init_nofail(&as, ms->device_memory->base,
                      memory_region_size(&ms->device_memory->mr));

    /* start of address space indicates the maximum alignment we expect */
    if (!QEMU_IS_ALIGNED(range_lob(&as), align)) {
        warn_report("the alignment (0x%" PRIx64 ") exceeds the expected"
                    " maximum alignment, memory will get fragmented and not"
                    " all 'maxmem' might be usable for memory devices.",
                    align);
    }

    if (hint && !QEMU_IS_ALIGNED(*hint, align)) {
        error_setg(errp, "address must be aligned to 0x%" PRIx64 " bytes",
                   align);
        return 0;
    }

    if (hint) {
        if (range_init(&new, *hint, size) || !range_contains_range(&as, &new)) {
            error_setg(errp, "can't add memory device [0x%" PRIx64 ":0x%" PRIx64
                       "], usable range for memory devices [0x%" PRIx64 ":0x%"
                       PRIx64 "]", *hint, size, range_lob(&as),
                       range_size(&as));
            return 0;
        }
    } else {
        if (range_init(&new, QEMU_ALIGN_UP(range_lob(&as), align), size)) {
            error_setg(errp, "can't add memory device, device too big");
            return 0;
        }
    }

    /* find address range that will fit new memory device */
    object_child_foreach(OBJECT(ms), memory_device_build_list, &list);
    for (item = list; item; item = g_slist_next(item)) {
        const MemoryDeviceState *md = item->data;
        const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(OBJECT(md));
        uint64_t next_addr;
        Range tmp;

        if (memory_device_is_empty(md)) {
            continue;
        }

        range_init_nofail(&tmp, mdc->get_addr(md),
                          memory_device_get_region_size(md, &error_abort));

        if (range_overlaps_range(&tmp, &new)) {
            if (hint) {
                const DeviceState *d = DEVICE(md);
                error_setg(errp, "address range conflicts with memory device"
                           " id='%s'", d->id ? d->id : "(unnamed)");
                goto out;
            }

            next_addr = QEMU_ALIGN_UP(range_upb(&tmp) + 1, align);
            if (!next_addr || range_init(&new, next_addr, range_size(&new))) {
                range_make_empty(&new);
                break;
            }
        } else if (range_lob(&tmp) > range_upb(&new)) {
            break;
        }
    }

    if (!range_contains_range(&as, &new)) {
        error_setg(errp, "could not find position in guest address space for "
                   "memory device - memory fragmented due to alignments");
    }
out:
    g_slist_free(list);
    return range_lob(&new);
}

MemoryDeviceInfoList *qmp_memory_device_list(void)
{
    GSList *devices = NULL, *item;
    MemoryDeviceInfoList *list = NULL, **tail = &list;

    object_child_foreach(qdev_get_machine(), memory_device_build_list,
                         &devices);

    for (item = devices; item; item = g_slist_next(item)) {
        const MemoryDeviceState *md = MEMORY_DEVICE(item->data);
        const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(item->data);
        MemoryDeviceInfo *info = g_new0(MemoryDeviceInfo, 1);

        /* Let's query infotmation even for empty memory devices. */
        mdc->fill_device_info(md, info);

        QAPI_LIST_APPEND(tail, info);
    }

    g_slist_free(devices);

    return list;
}

static int memory_device_plugged_size(Object *obj, void *opaque)
{
    uint64_t *size = opaque;

    if (object_dynamic_cast(obj, TYPE_MEMORY_DEVICE)) {
        const DeviceState *dev = DEVICE(obj);
        const MemoryDeviceState *md = MEMORY_DEVICE(obj);
        const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(obj);

        if (dev->realized && !memory_device_is_empty(md)) {
            *size += mdc->get_plugged_size(md, &error_abort);
        }
    }

    object_child_foreach(obj, memory_device_plugged_size, opaque);
    return 0;
}

uint64_t get_plugged_memory_size(void)
{
    uint64_t size = 0;

    memory_device_plugged_size(qdev_get_machine(), &size);

    return size;
}

void memory_device_pre_plug(MemoryDeviceState *md, MachineState *ms,
                            const uint64_t *legacy_align, Error **errp)
{
    const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(md);
    Error *local_err = NULL;
    uint64_t addr, align = 0;
    MemoryRegion *mr;

    /* We support empty memory devices even without device memory. */
    if (memory_device_is_empty(md)) {
        return;
    }

    if (!ms->device_memory) {
        error_setg(errp, "the configuration is not prepared for memory devices"
                         " (e.g., for memory hotplug), consider specifying the"
                         " maxmem option");
        return;
    }

    mr = mdc->get_memory_region(md, &local_err);
    if (local_err) {
        goto out;
    }

    memory_device_check_addable(ms, md, mr, &local_err);
    if (local_err) {
        goto out;
    }

    if (legacy_align) {
        align = *legacy_align;
    } else {
        if (mdc->get_min_alignment) {
            align = mdc->get_min_alignment(md);
        }
        align = MAX(align, memory_region_get_alignment(mr));
    }
    addr = mdc->get_addr(md);
    addr = memory_device_get_free_addr(ms, !addr ? NULL : &addr, align,
                                       memory_region_size(mr), &local_err);
    if (local_err) {
        goto out;
    }
    mdc->set_addr(md, addr, &local_err);
    if (!local_err) {
        trace_memory_device_pre_plug(DEVICE(md)->id ? DEVICE(md)->id : "",
                                     addr);
    }
out:
    error_propagate(errp, local_err);
}

void memory_device_plug(MemoryDeviceState *md, MachineState *ms)
{
    const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(md);
    unsigned int memslots;
    uint64_t addr;
    MemoryRegion *mr;

    if (memory_device_is_empty(md)) {
        return;
    }

    memslots = memory_device_get_memslots(md);
    addr = mdc->get_addr(md);

    /*
     * We expect that a previous call to memory_device_pre_plug() succeeded, so
     * it can't fail at this point.
     */
    mr = mdc->get_memory_region(md, &error_abort);
    g_assert(ms->device_memory);

    ms->device_memory->used_region_size += memory_region_size(mr);
    ms->device_memory->required_memslots += memslots;
    if (mdc->decide_memslots && memslots > 1) {
        ms->device_memory->memslot_auto_decision_active++;
    }

    memory_region_add_subregion(&ms->device_memory->mr,
                                addr - ms->device_memory->base, mr);
    trace_memory_device_plug(DEVICE(md)->id ? DEVICE(md)->id : "", addr);
}

void memory_device_unplug(MemoryDeviceState *md, MachineState *ms)
{
    const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(md);
    const unsigned int memslots = memory_device_get_memslots(md);
    MemoryRegion *mr;

    if (memory_device_is_empty(md)) {
        return;
    }

    /*
     * We expect that a previous call to memory_device_pre_plug() succeeded, so
     * it can't fail at this point.
     */
    mr = mdc->get_memory_region(md, &error_abort);
    g_assert(ms->device_memory);

    memory_region_del_subregion(&ms->device_memory->mr, mr);

    if (mdc->decide_memslots && memslots > 1) {
        ms->device_memory->memslot_auto_decision_active--;
    }
    ms->device_memory->used_region_size -= memory_region_size(mr);
    ms->device_memory->required_memslots -= memslots;
    trace_memory_device_unplug(DEVICE(md)->id ? DEVICE(md)->id : "",
                               mdc->get_addr(md));
}

uint64_t memory_device_get_region_size(const MemoryDeviceState *md,
                                       Error **errp)
{
    const MemoryDeviceClass *mdc = MEMORY_DEVICE_GET_CLASS(md);
    MemoryRegion *mr;

    /* dropping const here is fine as we don't touch the memory region */
    mr = mdc->get_memory_region((MemoryDeviceState *)md, errp);
    if (!mr) {
        return 0;
    }

    return memory_region_size(mr);
}

static void memory_devices_region_mod(MemoryListener *listener,
                                      MemoryRegionSection *mrs, bool add)
{
    DeviceMemoryState *dms = container_of(listener, DeviceMemoryState,
                                          listener);

    if (!memory_region_is_ram(mrs->mr)) {
        warn_report("Unexpected memory region mapped into device memory region.");
        return;
    }

    /*
     * The expectation is that each distinct RAM memory region section in
     * our region for memory devices consumes exactly one memslot in KVM
     * and in vhost. For vhost, this is true, except:
     * * ROM memory regions don't consume a memslot. These get used very
     *   rarely for memory devices (R/O NVDIMMs).
     * * Memslots without a fd (memory-backend-ram) don't necessarily
     *   consume a memslot. Such setups are quite rare and possibly bogus:
     *   the memory would be inaccessible by such vhost devices.
     *
     * So for vhost, in corner cases we might over-estimate the number of
     * memslots that are currently used or that might still be reserved
     * (required - used).
     */
    dms->used_memslots += add ? 1 : -1;

    if (dms->used_memslots > dms->required_memslots) {
        warn_report("Memory devices use more memory slots than indicated as required.");
    }
}

static void memory_devices_region_add(MemoryListener *listener,
                                      MemoryRegionSection *mrs)
{
    return memory_devices_region_mod(listener, mrs, true);
}

static void memory_devices_region_del(MemoryListener *listener,
                                      MemoryRegionSection *mrs)
{
    return memory_devices_region_mod(listener, mrs, false);
}

void machine_memory_devices_init(MachineState *ms, hwaddr base, uint64_t size)
{
    g_assert(size);
    g_assert(!ms->device_memory);
    ms->device_memory = g_new0(DeviceMemoryState, 1);
    ms->device_memory->base = base;

    memory_region_init(&ms->device_memory->mr, OBJECT(ms), "device-memory",
                       size);
    address_space_init(&ms->device_memory->as, &ms->device_memory->mr,
                       "device-memory");
    memory_region_add_subregion(get_system_memory(), ms->device_memory->base,
                                &ms->device_memory->mr);

    /* Track the number of memslots used by memory devices. */
    ms->device_memory->listener.region_add = memory_devices_region_add;
    ms->device_memory->listener.region_del = memory_devices_region_del;
    memory_listener_register(&ms->device_memory->listener,
                             &ms->device_memory->as);
}

static const TypeInfo memory_device_info = {
    .name          = TYPE_MEMORY_DEVICE,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(MemoryDeviceClass),
};

static void memory_device_register_types(void)
{
    type_register_static(&memory_device_info);
}

type_init(memory_device_register_types)
