/*
 * QEMU MSHV support
 *
 * Copyright Microsoft, Corp. 2025
 *
 * Authors:
 *  Magnus Kulke      <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "linux/mshv.h"
#include "system/address-spaces.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
#include "hw/hyperv/hvhdk_mini.h"
#include "system/physmem.h"
#include "exec/memattrs.h"
#include <sys/ioctl.h>
#include "trace.h"

static int set_guest_memory(int vm_fd,
                            const struct mshv_user_mem_region *region)
{
    int ret;

    ret = ioctl(vm_fd, MSHV_SET_GUEST_MEMORY, region);
    if (ret < 0) {
        error_report("failed to set guest memory");
        return -errno;
    }

    return 0;
}

static int map_or_unmap(int vm_fd, const MshvMemoryRegion *mr, bool map)
{
    struct mshv_user_mem_region region = {0};

    region.guest_pfn = mr->guest_phys_addr >> MSHV_PAGE_SHIFT;
    region.size = mr->memory_size;
    region.userspace_addr = mr->userspace_addr;

    if (!map) {
        region.flags |= (1 << MSHV_SET_MEM_BIT_UNMAP);
        trace_mshv_unmap_memory(mr->userspace_addr, mr->guest_phys_addr,
                                mr->memory_size);
        return set_guest_memory(vm_fd, &region);
    }

    region.flags = BIT(MSHV_SET_MEM_BIT_EXECUTABLE);
    if (!mr->readonly) {
        region.flags |= BIT(MSHV_SET_MEM_BIT_WRITABLE);
    }

    trace_mshv_map_memory(mr->userspace_addr, mr->guest_phys_addr,
                          mr->memory_size);
    return set_guest_memory(vm_fd, &region);
}

static int handle_unmapped_mmio_region_read(uint64_t gpa, uint64_t size,
                                            uint8_t *data)
{
    warn_report("read from unmapped mmio region gpa=0x%lx size=%lu", gpa, size);

    if (size == 0 || size > 8) {
        error_report("invalid size %lu for reading from unmapped mmio region",
                     size);
        return -1;
    }

    memset(data, 0xFF, size);

    return 0;
}

int mshv_guest_mem_read(uint64_t gpa, uint8_t *data, uintptr_t size,
                        bool is_secure_mode, bool instruction_fetch)
{
    int ret;
    MemTxAttrs memattr = { .secure = is_secure_mode };

    if (instruction_fetch) {
        trace_mshv_insn_fetch(gpa, size);
    } else {
        trace_mshv_mem_read(gpa, size);
    }

    ret = address_space_rw(&address_space_memory, gpa, memattr, (void *)data,
                           size, false);
    if (ret == MEMTX_OK) {
        return 0;
    }

    if (ret == MEMTX_DECODE_ERROR) {
        return handle_unmapped_mmio_region_read(gpa, size, data);
    }

    error_report("failed to read guest memory at 0x%lx", gpa);
    return -1;
}

int mshv_guest_mem_write(uint64_t gpa, const uint8_t *data, uintptr_t size,
                         bool is_secure_mode)
{
    int ret;
    MemTxAttrs memattr = { .secure = is_secure_mode };

    trace_mshv_mem_write(gpa, size);
    ret = address_space_rw(&address_space_memory, gpa, memattr, (void *)data,
                           size, true);
    if (ret == MEMTX_OK) {
        return 0;
    }

    if (ret == MEMTX_DECODE_ERROR) {
        warn_report("write to unmapped mmio region gpa=0x%lx size=%lu", gpa,
                    size);
        return 0;
    }

    error_report("Failed to write guest memory");
    return -1;
}

static int set_memory(const MshvMemoryRegion *mshv_mr, bool add)
{
    int ret = 0;

    if (!mshv_mr) {
        error_report("Invalid mshv_mr");
        return -1;
    }

    trace_mshv_set_memory(add, mshv_mr->guest_phys_addr,
                          mshv_mr->memory_size,
                          mshv_mr->userspace_addr, mshv_mr->readonly,
                          ret);
    return map_or_unmap(mshv_state->vm, mshv_mr, add);
}

/*
 * Calculate and align the start address and the size of the section.
 * Return the size. If the size is 0, the aligned section is empty.
 */
static hwaddr align_section(MemoryRegionSection *section, hwaddr *start)
{
    hwaddr size = int128_get64(section->size);
    hwaddr delta, aligned;

    /*
     * works in page size chunks, but the function may be called
     * with sub-page size and unaligned start address. Pad the start
     * address to next and truncate size to previous page boundary.
     */
    aligned = ROUND_UP(section->offset_within_address_space,
                       qemu_real_host_page_size());
    delta = aligned - section->offset_within_address_space;
    *start = aligned;
    if (delta > size) {
        return 0;
    }

    return (size - delta) & qemu_real_host_page_mask();
}

void mshv_set_phys_mem(MshvMemoryListener *mml, MemoryRegionSection *section,
                       bool add)
{
    int ret = 0;
    MemoryRegion *area = section->mr;
    bool writable = !area->readonly && !area->rom_device;
    hwaddr start_addr, mr_offset, size;
    void *ram;
    MshvMemoryRegion mshv_mr = {0};

    size = align_section(section, &start_addr);
    trace_mshv_set_phys_mem(add, section->mr->name, start_addr);

    /*
     * If the memory device is a writable non-ram area, we do not
     * want to map it into the guest memory. If it is not a ROM device,
     * we want to remove mshv memory mapping, so accesses will trap.
     */
    if (!memory_region_is_ram(area)) {
        if (writable) {
            return;
        } else if (!area->romd_mode) {
            add = false;
        }
    }

    if (!size) {
        return;
    }

    mr_offset = section->offset_within_region + start_addr -
                section->offset_within_address_space;

    ram = memory_region_get_ram_ptr(area) + mr_offset;

    mshv_mr.guest_phys_addr = start_addr;
    mshv_mr.memory_size = size;
    mshv_mr.readonly = !writable;
    mshv_mr.userspace_addr = (uint64_t)ram;

    ret = set_memory(&mshv_mr, add);
    if (ret < 0) {
        error_report("Failed to set memory region");
        abort();
    }
}

static int enable_dirty_page_tracking(int vm_fd)
{
    int ret;
    struct hv_input_set_partition_property in = {0};
    struct mshv_root_hvcall args = {0};

    in.property_code = HV_PARTITION_PROPERTY_GPA_PAGE_ACCESS_TRACKING;
    in.property_value = 1;

    args.code = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz = sizeof(in);
    args.in_ptr = (uint64_t)&in;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to enable dirty page tracking: %s",
                     strerror(errno));
        return -1;
    }

    return 0;
}

/*
 * Retrieve dirty page bitmap for a GPA range, clearing the dirty bits
 * atomically. Large ranges are handled in batches.
 */
static int get_dirty_log(int vm_fd, uint64_t base_pfn, uint64_t page_count,
                         unsigned long *bitmap, size_t bitmap_size)
{
    uint64_t batch, bitmap_offset, completed = 0;
    struct mshv_gpap_access_bitmap args = {0};
    int ret;

    QEMU_BUILD_BUG_ON(MSHV_DIRTY_PAGES_BATCH_SIZE % BITS_PER_LONG != 0);
    assert(bitmap_size >= ROUND_UP(page_count, BITS_PER_LONG) / 8);

    while (completed < page_count) {
        batch = MIN(MSHV_DIRTY_PAGES_BATCH_SIZE, page_count - completed);
        bitmap_offset = completed / BITS_PER_LONG;

        args.access_type = MSHV_GPAP_ACCESS_TYPE_DIRTY;
        args.access_op   = MSHV_GPAP_ACCESS_OP_CLEAR;
        args.page_count  = batch;
        args.gpap_base   = base_pfn + completed;
        args.bitmap_ptr  = (uint64_t)(bitmap + bitmap_offset);

        ret = ioctl(vm_fd, MSHV_GET_GPAP_ACCESS_BITMAP, &args);
        if (ret < 0) {
            error_report("Failed to get dirty log (base_pfn=0x%" PRIx64
                         " batch=%" PRIu64 "): %s",
                         base_pfn + completed, batch, strerror(errno));
            return -1;
        }
        completed += batch;
    }

    return 0;
}

bool mshv_log_global_start(MemoryListener *listener, Error **errp)
{
    int ret;

    ret = enable_dirty_page_tracking(mshv_state->vm);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to enable dirty page tracking");
        return false;
    }
    return true;
}

static int disable_dirty_page_tracking(int vm_fd)
{
    int ret;
    struct hv_input_set_partition_property in = {0};
    struct mshv_root_hvcall args = {0};

    in.property_code = HV_PARTITION_PROPERTY_GPA_PAGE_ACCESS_TRACKING;
    in.property_value = 0;

    args.code = HVCALL_SET_PARTITION_PROPERTY;
    args.in_sz = sizeof(in);
    args.in_ptr = (uint64_t)&in;

    ret = mshv_hvcall(vm_fd, &args);
    if (ret < 0) {
        error_report("Failed to disable dirty page tracking: %s",
                     strerror(errno));
        return -1;
    }

    return 0;
}

static int set_dirty_pages(int vm_fd, uint64_t base_pfn, uint64_t page_count)
{
    uint64_t batch, completed = 0;
    unsigned long bitmap[MSHV_DIRTY_PAGES_BATCH_SIZE / BITS_PER_LONG];
    struct mshv_gpap_access_bitmap args = {0};
    int ret;

    while (completed < page_count) {
        batch = MIN(MSHV_DIRTY_PAGES_BATCH_SIZE, page_count - completed);

        args.access_type = MSHV_GPAP_ACCESS_TYPE_DIRTY;
        args.access_op   = MSHV_GPAP_ACCESS_OP_SET;
        args.page_count  = batch;
        args.gpap_base   = base_pfn + completed;
        args.bitmap_ptr  = (uint64_t)bitmap;

        ret = ioctl(vm_fd, MSHV_GET_GPAP_ACCESS_BITMAP, &args);
        if (ret < 0) {
            error_report("Failed to set dirty pages (base_pfn=0x%" PRIx64
                         " batch=%" PRIu64 "): %s",
                         base_pfn + completed, batch, strerror(errno));
            return -1;
        }
        completed += batch;
    }

    return 0;
}

static bool set_dirty_bits_cb(Int128 start, Int128 len, const MemoryRegion *mr,
                              hwaddr offset_in_region, void *opaque)
{
    int ret, *errp = opaque;
    hwaddr gpa, size;
    uint64_t page_count, base_pfn;

    gpa = int128_get64(start);
    size = int128_get64(len);
    page_count = size >> MSHV_PAGE_SHIFT;
    base_pfn = gpa >> MSHV_PAGE_SHIFT;

    if (!mr->ram || mr->readonly) {
        return false;
    }

    if (page_count == 0) {
        return false;
    }

    ret = set_dirty_pages(mshv_state->vm, base_pfn, page_count);

    /* true aborts the iteration, which is what we want if there's an error */
    if (ret < 0) {
        *errp = ret;
        return true;
    }

    return false;
}

void mshv_log_global_stop(MemoryListener *listener)
{
    int err = 0;
    /* MSHV requires all dirty bits to be set before disabling tracking. */
    FlatView *fv = address_space_to_flatview(&address_space_memory);
    flatview_for_each_range(fv, set_dirty_bits_cb, &err);

    if (err < 0) {
        error_report("Failed to set dirty bits before disabling tracking");
    }

    disable_dirty_page_tracking(mshv_state->vm);
}

void mshv_log_sync(MemoryListener *listener, MemoryRegionSection *section)
{
    hwaddr size, start_addr, mr_offset;
    uint64_t page_count, base_pfn;
    size_t bitmap_size;
    unsigned long *bitmap;
    ram_addr_t ram_addr;
    int ret;
    MemoryRegion *mr = section->mr;

    if (!memory_region_is_ram(mr) || memory_region_is_rom(mr)) {
        return;
    }

    size = align_section(section, &start_addr);
    if (!size) {
        return;
    }

    page_count = size >> MSHV_PAGE_SHIFT;
    base_pfn = start_addr >> MSHV_PAGE_SHIFT;
    bitmap_size = ROUND_UP(page_count, BITS_PER_LONG) / 8;
    bitmap = g_malloc0(bitmap_size);

    ret = get_dirty_log(mshv_state->vm, base_pfn, page_count, bitmap,
                        bitmap_size);
    if (ret < 0) {
        g_free(bitmap);
        return;
    }

    mr_offset = section->offset_within_region + start_addr -
                section->offset_within_address_space;
    ram_addr = memory_region_get_ram_addr(mr) + mr_offset;

    physical_memory_set_dirty_lebitmap(bitmap, ram_addr, page_count);
    g_free(bitmap);
}
