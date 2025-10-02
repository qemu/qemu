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
#include "linux/mshv.h"
#include "system/address-spaces.h"
#include "system/mshv.h"
#include "system/mshv_int.h"
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
