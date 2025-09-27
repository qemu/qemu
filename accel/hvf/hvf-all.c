/*
 * QEMU Hypervisor.framework support
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "accel/accel-ops.h"
#include "exec/cpu-common.h"
#include "system/address-spaces.h"
#include "system/memory.h"
#include "system/hvf.h"
#include "system/hvf_int.h"
#include "hw/core/cpu.h"
#include "hw/core/boards.h"
#include "trace.h"

bool hvf_allowed;

const char *hvf_return_string(hv_return_t ret)
{
    switch (ret) {
    case HV_SUCCESS:      return "HV_SUCCESS";
    case HV_ERROR:        return "HV_ERROR";
    case HV_BUSY:         return "HV_BUSY";
    case HV_BAD_ARGUMENT: return "HV_BAD_ARGUMENT";
    case HV_NO_RESOURCES: return "HV_NO_RESOURCES";
    case HV_NO_DEVICE:    return "HV_NO_DEVICE";
    case HV_UNSUPPORTED:  return "HV_UNSUPPORTED";
    case HV_DENIED:       return "HV_DENIED";
    default:              return "[unknown hv_return value]";
    }
}

void assert_hvf_ok_impl(hv_return_t ret, const char *file, unsigned int line,
                        const char *exp)
{
    if (ret == HV_SUCCESS) {
        return;
    }

    error_report("Error: %s = %s (0x%x, at %s:%u)",
        exp, hvf_return_string(ret), ret, file, line);

    abort();
}

static void do_hv_vm_protect(hwaddr start, size_t size,
                             hv_memory_flags_t flags)
{
    intptr_t page_mask = qemu_real_host_page_mask();
    hv_return_t ret;

    trace_hvf_vm_protect(start, size, flags,
                         flags & HV_MEMORY_READ  ? 'R' : '-',
                         flags & HV_MEMORY_WRITE ? 'W' : '-',
                         flags & HV_MEMORY_EXEC  ? 'X' : '-');
    g_assert(!((uintptr_t)start & ~page_mask));
    g_assert(!(size & ~page_mask));

    ret = hv_vm_protect(start, size, flags);
    assert_hvf_ok(ret);
}

void hvf_protect_clean_range(hwaddr addr, size_t size)
{
    do_hv_vm_protect(addr, size, HV_MEMORY_READ | HV_MEMORY_EXEC);
}

void hvf_unprotect_dirty_range(hwaddr addr, size_t size)
{
    do_hv_vm_protect(addr, size,
                     HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
}

static void hvf_set_phys_mem(MemoryRegionSection *section, bool add)
{
    MemoryRegion *area = section->mr;
    bool writable = !area->readonly && !area->rom_device;
    hv_memory_flags_t flags;
    uint64_t page_size = qemu_real_host_page_size();
    uint64_t gpa = section->offset_within_address_space;
    uint64_t size = int128_get64(section->size);
    hv_return_t ret;
    void *mem;

    if (!memory_region_is_ram(area)) {
        if (writable) {
            return;
        } else if (!memory_region_is_romd(area)) {
            /*
             * If the memory device is not in romd_mode, then we actually want
             * to remove the hvf memory slot so all accesses will trap.
             */
             add = false;
        }
    }

    if (!QEMU_IS_ALIGNED(size, page_size) ||
        !QEMU_IS_ALIGNED(gpa, page_size)) {
        /* Not page aligned, so we can not map as RAM */
        add = false;
    }

    if (!add) {
        trace_hvf_vm_unmap(gpa, size);
        ret = hv_vm_unmap(gpa, size);
        assert_hvf_ok(ret);
        return;
    }

    flags = HV_MEMORY_READ | HV_MEMORY_EXEC | (writable ? HV_MEMORY_WRITE : 0);
    mem = memory_region_get_ram_ptr(area) + section->offset_within_region;

    trace_hvf_vm_map(gpa, size, mem, flags,
                     flags & HV_MEMORY_READ ?  'R' : '-',
                     flags & HV_MEMORY_WRITE ? 'W' : '-',
                     flags & HV_MEMORY_EXEC ?  'X' : '-');
    ret = hv_vm_map(mem, gpa, size, flags);
    assert_hvf_ok(ret);
}

static void hvf_log_start(MemoryListener *listener,
                          MemoryRegionSection *section, int old, int new)
{
    assert(new != 0);
    if (old == 0) {
        hvf_protect_clean_range(section->offset_within_address_space,
                                int128_get64(section->size));
    }
}

static void hvf_log_stop(MemoryListener *listener,
                         MemoryRegionSection *section, int old, int new)
{
    assert(old != 0);
    if (new == 0) {
        hvf_unprotect_dirty_range(section->offset_within_address_space,
                                  int128_get64(section->size));
    }
}

static void hvf_log_clear(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    /*
     * The dirty page bits within section are being cleared.
     * Some number of those pages may have been dirtied and
     * the write permission enabled.  Reset the range read-only.
     */
    hvf_protect_clean_range(section->offset_within_address_space,
                            int128_get64(section->size));
}

static void hvf_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    hvf_set_phys_mem(section, true);
}

static void hvf_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    hvf_set_phys_mem(section, false);
}

static MemoryListener hvf_memory_listener = {
    .name = "hvf",
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
    .region_add = hvf_region_add,
    .region_del = hvf_region_del,
    .log_start = hvf_log_start,
    .log_stop = hvf_log_stop,
    .log_clear = hvf_log_clear,
};

static int hvf_accel_init(AccelState *as, MachineState *ms)
{
    hv_return_t ret;
    HVFState *s = HVF_STATE(as);
    int pa_range = 36;
    MachineClass *mc = MACHINE_GET_CLASS(ms);

    if (mc->hvf_get_physical_address_range) {
        pa_range = mc->hvf_get_physical_address_range(ms);
        if (pa_range < 0) {
            return -EINVAL;
        }
    }

    ret = hvf_arch_vm_create(ms, (uint32_t)pa_range);
    if (ret == HV_DENIED) {
        error_report("Could not access HVF. Is the executable signed"
                     " with com.apple.security.hypervisor entitlement?");
        exit(1);
    }
    assert_hvf_ok(ret);

    QTAILQ_INIT(&s->hvf_sw_breakpoints);

    hvf_state = s;
    memory_listener_register(&hvf_memory_listener, &address_space_memory);

    return hvf_arch_init();
}

static int hvf_gdbstub_sstep_flags(AccelState *as)
{
    return SSTEP_ENABLE | SSTEP_NOIRQ;
}

static void hvf_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "HVF";
    ac->init_machine = hvf_accel_init;
    ac->allowed = &hvf_allowed;
    ac->gdbstub_supported_sstep_flags = hvf_gdbstub_sstep_flags;
}

static const TypeInfo hvf_accel_type = {
    .name = TYPE_HVF_ACCEL,
    .parent = TYPE_ACCEL,
    .instance_size = sizeof(HVFState),
    .class_init = hvf_accel_class_init,
};

static void hvf_type_init(void)
{
    type_register_static(&hvf_accel_type);
}

type_init(hvf_type_init);
