/*
 * vhost support
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/virtio/vhost.h"
#include "hw/hw.h"
#include "qemu/atomic.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "qemu/memfd.h"
#include <linux/vhost.h>
#include "exec/address-spaces.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "migration/blocker.h"
#include "sysemu/dma.h"

/* enabled until disconnected backend stabilizes */
#define _VHOST_DEBUG 1

#ifdef _VHOST_DEBUG
#define VHOST_OPS_DEBUG(fmt, ...) \
    do { error_report(fmt ": %s (%d)", ## __VA_ARGS__, \
                      strerror(errno), errno); } while (0)
#else
#define VHOST_OPS_DEBUG(fmt, ...) \
    do { } while (0)
#endif

static struct vhost_log *vhost_log;
static struct vhost_log *vhost_log_shm;

static unsigned int used_memslots;
static QLIST_HEAD(, vhost_dev) vhost_devices =
    QLIST_HEAD_INITIALIZER(vhost_devices);

bool vhost_has_free_slot(void)
{
    unsigned int slots_limit = ~0U;
    struct vhost_dev *hdev;

    QLIST_FOREACH(hdev, &vhost_devices, entry) {
        unsigned int r = hdev->vhost_ops->vhost_backend_memslots_limit(hdev);
        slots_limit = MIN(slots_limit, r);
    }
    return slots_limit > used_memslots;
}

static void vhost_dev_sync_region(struct vhost_dev *dev,
                                  MemoryRegionSection *section,
                                  uint64_t mfirst, uint64_t mlast,
                                  uint64_t rfirst, uint64_t rlast)
{
    vhost_log_chunk_t *log = dev->log->log;

    uint64_t start = MAX(mfirst, rfirst);
    uint64_t end = MIN(mlast, rlast);
    vhost_log_chunk_t *from = log + start / VHOST_LOG_CHUNK;
    vhost_log_chunk_t *to = log + end / VHOST_LOG_CHUNK + 1;
    uint64_t addr = (start / VHOST_LOG_CHUNK) * VHOST_LOG_CHUNK;

    if (end < start) {
        return;
    }
    assert(end / VHOST_LOG_CHUNK < dev->log_size);
    assert(start / VHOST_LOG_CHUNK < dev->log_size);

    for (;from < to; ++from) {
        vhost_log_chunk_t log;
        /* We first check with non-atomic: much cheaper,
         * and we expect non-dirty to be the common case. */
        if (!*from) {
            addr += VHOST_LOG_CHUNK;
            continue;
        }
        /* Data must be read atomically. We don't really need barrier semantics
         * but it's easier to use atomic_* than roll our own. */
        log = atomic_xchg(from, 0);
        while (log) {
            int bit = ctzl(log);
            hwaddr page_addr;
            hwaddr section_offset;
            hwaddr mr_offset;
            page_addr = addr + bit * VHOST_LOG_PAGE;
            section_offset = page_addr - section->offset_within_address_space;
            mr_offset = section_offset + section->offset_within_region;
            memory_region_set_dirty(section->mr, mr_offset, VHOST_LOG_PAGE);
            log &= ~(0x1ull << bit);
        }
        addr += VHOST_LOG_CHUNK;
    }
}

static int vhost_sync_dirty_bitmap(struct vhost_dev *dev,
                                   MemoryRegionSection *section,
                                   hwaddr first,
                                   hwaddr last)
{
    int i;
    hwaddr start_addr;
    hwaddr end_addr;

    if (!dev->log_enabled || !dev->started) {
        return 0;
    }
    start_addr = section->offset_within_address_space;
    end_addr = range_get_last(start_addr, int128_get64(section->size));
    start_addr = MAX(first, start_addr);
    end_addr = MIN(last, end_addr);

    for (i = 0; i < dev->mem->nregions; ++i) {
        struct vhost_memory_region *reg = dev->mem->regions + i;
        vhost_dev_sync_region(dev, section, start_addr, end_addr,
                              reg->guest_phys_addr,
                              range_get_last(reg->guest_phys_addr,
                                             reg->memory_size));
    }
    for (i = 0; i < dev->nvqs; ++i) {
        struct vhost_virtqueue *vq = dev->vqs + i;
        vhost_dev_sync_region(dev, section, start_addr, end_addr, vq->used_phys,
                              range_get_last(vq->used_phys, vq->used_size));
    }
    return 0;
}

static void vhost_log_sync(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    vhost_sync_dirty_bitmap(dev, section, 0x0, ~0x0ULL);
}

static void vhost_log_sync_range(struct vhost_dev *dev,
                                 hwaddr first, hwaddr last)
{
    int i;
    /* FIXME: this is N^2 in number of sections */
    for (i = 0; i < dev->n_mem_sections; ++i) {
        MemoryRegionSection *section = &dev->mem_sections[i];
        vhost_sync_dirty_bitmap(dev, section, first, last);
    }
}

/* Assign/unassign. Keep an unsorted array of non-overlapping
 * memory regions in dev->mem. */
static void vhost_dev_unassign_memory(struct vhost_dev *dev,
                                      uint64_t start_addr,
                                      uint64_t size)
{
    int from, to, n = dev->mem->nregions;
    /* Track overlapping/split regions for sanity checking. */
    int overlap_start = 0, overlap_end = 0, overlap_middle = 0, split = 0;

    for (from = 0, to = 0; from < n; ++from, ++to) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        uint64_t reglast;
        uint64_t memlast;
        uint64_t change;

        /* clone old region */
        if (to != from) {
            memcpy(reg, dev->mem->regions + from, sizeof *reg);
        }

        /* No overlap is simple */
        if (!ranges_overlap(reg->guest_phys_addr, reg->memory_size,
                            start_addr, size)) {
            continue;
        }

        /* Split only happens if supplied region
         * is in the middle of an existing one. Thus it can not
         * overlap with any other existing region. */
        assert(!split);

        reglast = range_get_last(reg->guest_phys_addr, reg->memory_size);
        memlast = range_get_last(start_addr, size);

        /* Remove whole region */
        if (start_addr <= reg->guest_phys_addr && memlast >= reglast) {
            --dev->mem->nregions;
            --to;
            ++overlap_middle;
            continue;
        }

        /* Shrink region */
        if (memlast >= reglast) {
            reg->memory_size = start_addr - reg->guest_phys_addr;
            assert(reg->memory_size);
            assert(!overlap_end);
            ++overlap_end;
            continue;
        }

        /* Shift region */
        if (start_addr <= reg->guest_phys_addr) {
            change = memlast + 1 - reg->guest_phys_addr;
            reg->memory_size -= change;
            reg->guest_phys_addr += change;
            reg->userspace_addr += change;
            assert(reg->memory_size);
            assert(!overlap_start);
            ++overlap_start;
            continue;
        }

        /* This only happens if supplied region
         * is in the middle of an existing one. Thus it can not
         * overlap with any other existing region. */
        assert(!overlap_start);
        assert(!overlap_end);
        assert(!overlap_middle);
        /* Split region: shrink first part, shift second part. */
        memcpy(dev->mem->regions + n, reg, sizeof *reg);
        reg->memory_size = start_addr - reg->guest_phys_addr;
        assert(reg->memory_size);
        change = memlast + 1 - reg->guest_phys_addr;
        reg = dev->mem->regions + n;
        reg->memory_size -= change;
        assert(reg->memory_size);
        reg->guest_phys_addr += change;
        reg->userspace_addr += change;
        /* Never add more than 1 region */
        assert(dev->mem->nregions == n);
        ++dev->mem->nregions;
        ++split;
    }
}

/* Called after unassign, so no regions overlap the given range. */
static void vhost_dev_assign_memory(struct vhost_dev *dev,
                                    uint64_t start_addr,
                                    uint64_t size,
                                    uint64_t uaddr)
{
    int from, to;
    struct vhost_memory_region *merged = NULL;
    for (from = 0, to = 0; from < dev->mem->nregions; ++from, ++to) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        uint64_t prlast, urlast;
        uint64_t pmlast, umlast;
        uint64_t s, e, u;

        /* clone old region */
        if (to != from) {
            memcpy(reg, dev->mem->regions + from, sizeof *reg);
        }
        prlast = range_get_last(reg->guest_phys_addr, reg->memory_size);
        pmlast = range_get_last(start_addr, size);
        urlast = range_get_last(reg->userspace_addr, reg->memory_size);
        umlast = range_get_last(uaddr, size);

        /* check for overlapping regions: should never happen. */
        assert(prlast < start_addr || pmlast < reg->guest_phys_addr);
        /* Not an adjacent or overlapping region - do not merge. */
        if ((prlast + 1 != start_addr || urlast + 1 != uaddr) &&
            (pmlast + 1 != reg->guest_phys_addr ||
             umlast + 1 != reg->userspace_addr)) {
            continue;
        }

        if (dev->vhost_ops->vhost_backend_can_merge &&
            !dev->vhost_ops->vhost_backend_can_merge(dev, uaddr, size,
                                                     reg->userspace_addr,
                                                     reg->memory_size)) {
            continue;
        }

        if (merged) {
            --to;
            assert(to >= 0);
        } else {
            merged = reg;
        }
        u = MIN(uaddr, reg->userspace_addr);
        s = MIN(start_addr, reg->guest_phys_addr);
        e = MAX(pmlast, prlast);
        uaddr = merged->userspace_addr = u;
        start_addr = merged->guest_phys_addr = s;
        size = merged->memory_size = e - s + 1;
        assert(merged->memory_size);
    }

    if (!merged) {
        struct vhost_memory_region *reg = dev->mem->regions + to;
        memset(reg, 0, sizeof *reg);
        reg->memory_size = size;
        assert(reg->memory_size);
        reg->guest_phys_addr = start_addr;
        reg->userspace_addr = uaddr;
        ++to;
    }
    assert(to <= dev->mem->nregions + 1);
    dev->mem->nregions = to;
}

static uint64_t vhost_get_log_size(struct vhost_dev *dev)
{
    uint64_t log_size = 0;
    int i;
    for (i = 0; i < dev->mem->nregions; ++i) {
        struct vhost_memory_region *reg = dev->mem->regions + i;
        uint64_t last = range_get_last(reg->guest_phys_addr,
                                       reg->memory_size);
        log_size = MAX(log_size, last / VHOST_LOG_CHUNK + 1);
    }
    for (i = 0; i < dev->nvqs; ++i) {
        struct vhost_virtqueue *vq = dev->vqs + i;
        uint64_t last = vq->used_phys + vq->used_size - 1;
        log_size = MAX(log_size, last / VHOST_LOG_CHUNK + 1);
    }
    return log_size;
}

static struct vhost_log *vhost_log_alloc(uint64_t size, bool share)
{
    struct vhost_log *log;
    uint64_t logsize = size * sizeof(*(log->log));
    int fd = -1;

    log = g_new0(struct vhost_log, 1);
    if (share) {
        log->log = qemu_memfd_alloc("vhost-log", logsize,
                                    F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL,
                                    &fd);
        memset(log->log, 0, logsize);
    } else {
        log->log = g_malloc0(logsize);
    }

    log->size = size;
    log->refcnt = 1;
    log->fd = fd;

    return log;
}

static struct vhost_log *vhost_log_get(uint64_t size, bool share)
{
    struct vhost_log *log = share ? vhost_log_shm : vhost_log;

    if (!log || log->size != size) {
        log = vhost_log_alloc(size, share);
        if (share) {
            vhost_log_shm = log;
        } else {
            vhost_log = log;
        }
    } else {
        ++log->refcnt;
    }

    return log;
}

static void vhost_log_put(struct vhost_dev *dev, bool sync)
{
    struct vhost_log *log = dev->log;

    if (!log) {
        return;
    }
    dev->log = NULL;
    dev->log_size = 0;

    --log->refcnt;
    if (log->refcnt == 0) {
        /* Sync only the range covered by the old log */
        if (dev->log_size && sync) {
            vhost_log_sync_range(dev, 0, dev->log_size * VHOST_LOG_CHUNK - 1);
        }

        if (vhost_log == log) {
            g_free(log->log);
            vhost_log = NULL;
        } else if (vhost_log_shm == log) {
            qemu_memfd_free(log->log, log->size * sizeof(*(log->log)),
                            log->fd);
            vhost_log_shm = NULL;
        }

        g_free(log);
    }
}

static bool vhost_dev_log_is_shared(struct vhost_dev *dev)
{
    return dev->vhost_ops->vhost_requires_shm_log &&
           dev->vhost_ops->vhost_requires_shm_log(dev);
}

static inline void vhost_dev_log_resize(struct vhost_dev *dev, uint64_t size)
{
    struct vhost_log *log = vhost_log_get(size, vhost_dev_log_is_shared(dev));
    uint64_t log_base = (uintptr_t)log->log;
    int r;

    /* inform backend of log switching, this must be done before
       releasing the current log, to ensure no logging is lost */
    r = dev->vhost_ops->vhost_set_log_base(dev, log_base, log);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_log_base failed");
    }

    vhost_log_put(dev, true);
    dev->log = log;
    dev->log_size = size;
}

static int vhost_dev_has_iommu(struct vhost_dev *dev)
{
    VirtIODevice *vdev = dev->vdev;

    return virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM);
}

static void *vhost_memory_map(struct vhost_dev *dev, hwaddr addr,
                              hwaddr *plen, int is_write)
{
    if (!vhost_dev_has_iommu(dev)) {
        return cpu_physical_memory_map(addr, plen, is_write);
    } else {
        return (void *)(uintptr_t)addr;
    }
}

static void vhost_memory_unmap(struct vhost_dev *dev, void *buffer,
                               hwaddr len, int is_write,
                               hwaddr access_len)
{
    if (!vhost_dev_has_iommu(dev)) {
        cpu_physical_memory_unmap(buffer, len, is_write, access_len);
    }
}

static int vhost_verify_ring_part_mapping(struct vhost_dev *dev,
                                          void *part,
                                          uint64_t part_addr,
                                          uint64_t part_size,
                                          uint64_t start_addr,
                                          uint64_t size)
{
    hwaddr l;
    void *p;
    int r = 0;

    if (!ranges_overlap(start_addr, size, part_addr, part_size)) {
        return 0;
    }
    l = part_size;
    p = vhost_memory_map(dev, part_addr, &l, 1);
    if (!p || l != part_size) {
        r = -ENOMEM;
    }
    if (p != part) {
        r = -EBUSY;
    }
    vhost_memory_unmap(dev, p, l, 0, 0);
    return r;
}

static int vhost_verify_ring_mappings(struct vhost_dev *dev,
                                      uint64_t start_addr,
                                      uint64_t size)
{
    int i, j;
    int r = 0;
    const char *part_name[] = {
        "descriptor table",
        "available ring",
        "used ring"
    };

    for (i = 0; i < dev->nvqs; ++i) {
        struct vhost_virtqueue *vq = dev->vqs + i;

        j = 0;
        r = vhost_verify_ring_part_mapping(dev, vq->desc, vq->desc_phys,
                                           vq->desc_size, start_addr, size);
        if (!r) {
            break;
        }

        j++;
        r = vhost_verify_ring_part_mapping(dev, vq->avail, vq->avail_phys,
                                           vq->avail_size, start_addr, size);
        if (!r) {
            break;
        }

        j++;
        r = vhost_verify_ring_part_mapping(dev, vq->used, vq->used_phys,
                                           vq->used_size, start_addr, size);
        if (!r) {
            break;
        }
    }

    if (r == -ENOMEM) {
        error_report("Unable to map %s for ring %d", part_name[j], i);
    } else if (r == -EBUSY) {
        error_report("%s relocated for ring %d", part_name[j], i);
    }
    return r;
}

static struct vhost_memory_region *vhost_dev_find_reg(struct vhost_dev *dev,
						      uint64_t start_addr,
						      uint64_t size)
{
    int i, n = dev->mem->nregions;
    for (i = 0; i < n; ++i) {
        struct vhost_memory_region *reg = dev->mem->regions + i;
        if (ranges_overlap(reg->guest_phys_addr, reg->memory_size,
                           start_addr, size)) {
            return reg;
        }
    }
    return NULL;
}

static bool vhost_dev_cmp_memory(struct vhost_dev *dev,
                                 uint64_t start_addr,
                                 uint64_t size,
                                 uint64_t uaddr)
{
    struct vhost_memory_region *reg = vhost_dev_find_reg(dev, start_addr, size);
    uint64_t reglast;
    uint64_t memlast;

    if (!reg) {
        return true;
    }

    reglast = range_get_last(reg->guest_phys_addr, reg->memory_size);
    memlast = range_get_last(start_addr, size);

    /* Need to extend region? */
    if (start_addr < reg->guest_phys_addr || memlast > reglast) {
        return true;
    }
    /* userspace_addr changed? */
    return uaddr != reg->userspace_addr + start_addr - reg->guest_phys_addr;
}

static void vhost_set_memory(MemoryListener *listener,
                             MemoryRegionSection *section,
                             bool add)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    bool log_dirty =
        memory_region_get_dirty_log_mask(section->mr) & ~(1 << DIRTY_MEMORY_MIGRATION);
    int s = offsetof(struct vhost_memory, regions) +
        (dev->mem->nregions + 1) * sizeof dev->mem->regions[0];
    void *ram;

    dev->mem = g_realloc(dev->mem, s);

    if (log_dirty) {
        add = false;
    }

    assert(size);

    /* Optimize no-change case. At least cirrus_vga does this a lot at this time. */
    ram = memory_region_get_ram_ptr(section->mr) + section->offset_within_region;
    if (add) {
        if (!vhost_dev_cmp_memory(dev, start_addr, size, (uintptr_t)ram)) {
            /* Region exists with same address. Nothing to do. */
            return;
        }
    } else {
        if (!vhost_dev_find_reg(dev, start_addr, size)) {
            /* Removing region that we don't access. Nothing to do. */
            return;
        }
    }

    vhost_dev_unassign_memory(dev, start_addr, size);
    if (add) {
        /* Add given mapping, merging adjacent regions if any */
        vhost_dev_assign_memory(dev, start_addr, size, (uintptr_t)ram);
    } else {
        /* Remove old mapping for this memory, if any. */
        vhost_dev_unassign_memory(dev, start_addr, size);
    }
    dev->mem_changed_start_addr = MIN(dev->mem_changed_start_addr, start_addr);
    dev->mem_changed_end_addr = MAX(dev->mem_changed_end_addr, start_addr + size - 1);
    dev->memory_changed = true;
    used_memslots = dev->mem->nregions;
}

static bool vhost_section(MemoryRegionSection *section)
{
    return memory_region_is_ram(section->mr) &&
        !memory_region_is_rom(section->mr);
}

static void vhost_begin(MemoryListener *listener)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    dev->mem_changed_end_addr = 0;
    dev->mem_changed_start_addr = -1;
}

static void vhost_commit(MemoryListener *listener)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    hwaddr start_addr = 0;
    ram_addr_t size = 0;
    uint64_t log_size;
    int r;

    if (!dev->memory_changed) {
        return;
    }
    if (!dev->started) {
        return;
    }
    if (dev->mem_changed_start_addr > dev->mem_changed_end_addr) {
        return;
    }

    if (dev->started) {
        start_addr = dev->mem_changed_start_addr;
        size = dev->mem_changed_end_addr - dev->mem_changed_start_addr + 1;

        r = vhost_verify_ring_mappings(dev, start_addr, size);
        assert(r >= 0);
    }

    if (!dev->log_enabled) {
        r = dev->vhost_ops->vhost_set_mem_table(dev, dev->mem);
        if (r < 0) {
            VHOST_OPS_DEBUG("vhost_set_mem_table failed");
        }
        dev->memory_changed = false;
        return;
    }
    log_size = vhost_get_log_size(dev);
    /* We allocate an extra 4K bytes to log,
     * to reduce the * number of reallocations. */
#define VHOST_LOG_BUFFER (0x1000 / sizeof *dev->log)
    /* To log more, must increase log size before table update. */
    if (dev->log_size < log_size) {
        vhost_dev_log_resize(dev, log_size + VHOST_LOG_BUFFER);
    }
    r = dev->vhost_ops->vhost_set_mem_table(dev, dev->mem);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_mem_table failed");
    }
    /* To log less, can only decrease log size after table update. */
    if (dev->log_size > log_size + VHOST_LOG_BUFFER) {
        vhost_dev_log_resize(dev, log_size);
    }
    dev->memory_changed = false;
}

static void vhost_region_add(MemoryListener *listener,
                             MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);

    if (!vhost_section(section)) {
        return;
    }

    ++dev->n_mem_sections;
    dev->mem_sections = g_renew(MemoryRegionSection, dev->mem_sections,
                                dev->n_mem_sections);
    dev->mem_sections[dev->n_mem_sections - 1] = *section;
    memory_region_ref(section->mr);
    vhost_set_memory(listener, section, true);
}

static void vhost_region_del(MemoryListener *listener,
                             MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    int i;

    if (!vhost_section(section)) {
        return;
    }

    vhost_set_memory(listener, section, false);
    memory_region_unref(section->mr);
    for (i = 0; i < dev->n_mem_sections; ++i) {
        if (dev->mem_sections[i].offset_within_address_space
            == section->offset_within_address_space) {
            --dev->n_mem_sections;
            memmove(&dev->mem_sections[i], &dev->mem_sections[i+1],
                    (dev->n_mem_sections - i) * sizeof(*dev->mem_sections));
            break;
        }
    }
}

static void vhost_iommu_unmap_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    struct vhost_iommu *iommu = container_of(n, struct vhost_iommu, n);
    struct vhost_dev *hdev = iommu->hdev;
    hwaddr iova = iotlb->iova + iommu->iommu_offset;

    if (vhost_backend_invalidate_device_iotlb(hdev, iova,
                                              iotlb->addr_mask + 1)) {
        error_report("Fail to invalidate device iotlb");
    }
}

static void vhost_iommu_region_add(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         iommu_listener);
    struct vhost_iommu *iommu;
    Int128 end;

    if (!memory_region_is_iommu(section->mr)) {
        return;
    }

    iommu = g_malloc0(sizeof(*iommu));
    end = int128_add(int128_make64(section->offset_within_region),
                     section->size);
    end = int128_sub(end, int128_one());
    iommu_notifier_init(&iommu->n, vhost_iommu_unmap_notify,
                        IOMMU_NOTIFIER_UNMAP,
                        section->offset_within_region,
                        int128_get64(end));
    iommu->mr = section->mr;
    iommu->iommu_offset = section->offset_within_address_space -
                          section->offset_within_region;
    iommu->hdev = dev;
    memory_region_register_iommu_notifier(section->mr, &iommu->n);
    QLIST_INSERT_HEAD(&dev->iommu_list, iommu, iommu_next);
    /* TODO: can replay help performance here? */
}

static void vhost_iommu_region_del(MemoryListener *listener,
                                   MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         iommu_listener);
    struct vhost_iommu *iommu;

    if (!memory_region_is_iommu(section->mr)) {
        return;
    }

    QLIST_FOREACH(iommu, &dev->iommu_list, iommu_next) {
        if (iommu->mr == section->mr &&
            iommu->n.start == section->offset_within_region) {
            memory_region_unregister_iommu_notifier(iommu->mr,
                                                    &iommu->n);
            QLIST_REMOVE(iommu, iommu_next);
            g_free(iommu);
            break;
        }
    }
}

static void vhost_region_nop(MemoryListener *listener,
                             MemoryRegionSection *section)
{
}

static int vhost_virtqueue_set_addr(struct vhost_dev *dev,
                                    struct vhost_virtqueue *vq,
                                    unsigned idx, bool enable_log)
{
    struct vhost_vring_addr addr = {
        .index = idx,
        .desc_user_addr = (uint64_t)(unsigned long)vq->desc,
        .avail_user_addr = (uint64_t)(unsigned long)vq->avail,
        .used_user_addr = (uint64_t)(unsigned long)vq->used,
        .log_guest_addr = vq->used_phys,
        .flags = enable_log ? (1 << VHOST_VRING_F_LOG) : 0,
    };
    int r = dev->vhost_ops->vhost_set_vring_addr(dev, &addr);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_vring_addr failed");
        return -errno;
    }
    return 0;
}

static int vhost_dev_set_features(struct vhost_dev *dev,
                                  bool enable_log)
{
    uint64_t features = dev->acked_features;
    int r;
    if (enable_log) {
        features |= 0x1ULL << VHOST_F_LOG_ALL;
    }
    r = dev->vhost_ops->vhost_set_features(dev, features);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_features failed");
    }
    return r < 0 ? -errno : 0;
}

static int vhost_dev_set_log(struct vhost_dev *dev, bool enable_log)
{
    int r, i, idx;
    r = vhost_dev_set_features(dev, enable_log);
    if (r < 0) {
        goto err_features;
    }
    for (i = 0; i < dev->nvqs; ++i) {
        idx = dev->vhost_ops->vhost_get_vq_index(dev, dev->vq_index + i);
        r = vhost_virtqueue_set_addr(dev, dev->vqs + i, idx,
                                     enable_log);
        if (r < 0) {
            goto err_vq;
        }
    }
    return 0;
err_vq:
    for (; i >= 0; --i) {
        idx = dev->vhost_ops->vhost_get_vq_index(dev, dev->vq_index + i);
        vhost_virtqueue_set_addr(dev, dev->vqs + i, idx,
                                 dev->log_enabled);
    }
    vhost_dev_set_features(dev, dev->log_enabled);
err_features:
    return r;
}

static int vhost_migration_log(MemoryListener *listener, int enable)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    int r;
    if (!!enable == dev->log_enabled) {
        return 0;
    }
    if (!dev->started) {
        dev->log_enabled = enable;
        return 0;
    }
    if (!enable) {
        r = vhost_dev_set_log(dev, false);
        if (r < 0) {
            return r;
        }
        vhost_log_put(dev, false);
    } else {
        vhost_dev_log_resize(dev, vhost_get_log_size(dev));
        r = vhost_dev_set_log(dev, true);
        if (r < 0) {
            return r;
        }
    }
    dev->log_enabled = enable;
    return 0;
}

static void vhost_log_global_start(MemoryListener *listener)
{
    int r;

    r = vhost_migration_log(listener, true);
    if (r < 0) {
        abort();
    }
}

static void vhost_log_global_stop(MemoryListener *listener)
{
    int r;

    r = vhost_migration_log(listener, false);
    if (r < 0) {
        abort();
    }
}

static void vhost_log_start(MemoryListener *listener,
                            MemoryRegionSection *section,
                            int old, int new)
{
    /* FIXME: implement */
}

static void vhost_log_stop(MemoryListener *listener,
                           MemoryRegionSection *section,
                           int old, int new)
{
    /* FIXME: implement */
}

/* The vhost driver natively knows how to handle the vrings of non
 * cross-endian legacy devices and modern devices. Only legacy devices
 * exposed to a bi-endian guest may require the vhost driver to use a
 * specific endianness.
 */
static inline bool vhost_needs_vring_endian(VirtIODevice *vdev)
{
    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        return false;
    }
#ifdef HOST_WORDS_BIGENDIAN
    return vdev->device_endian == VIRTIO_DEVICE_ENDIAN_LITTLE;
#else
    return vdev->device_endian == VIRTIO_DEVICE_ENDIAN_BIG;
#endif
}

static int vhost_virtqueue_set_vring_endian_legacy(struct vhost_dev *dev,
                                                   bool is_big_endian,
                                                   int vhost_vq_index)
{
    struct vhost_vring_state s = {
        .index = vhost_vq_index,
        .num = is_big_endian
    };

    if (!dev->vhost_ops->vhost_set_vring_endian(dev, &s)) {
        return 0;
    }

    VHOST_OPS_DEBUG("vhost_set_vring_endian failed");
    if (errno == ENOTTY) {
        error_report("vhost does not support cross-endian");
        return -ENOSYS;
    }

    return -errno;
}

static int vhost_memory_region_lookup(struct vhost_dev *hdev,
                                      uint64_t gpa, uint64_t *uaddr,
                                      uint64_t *len)
{
    int i;

    for (i = 0; i < hdev->mem->nregions; i++) {
        struct vhost_memory_region *reg = hdev->mem->regions + i;

        if (gpa >= reg->guest_phys_addr &&
            reg->guest_phys_addr + reg->memory_size > gpa) {
            *uaddr = reg->userspace_addr + gpa - reg->guest_phys_addr;
            *len = reg->guest_phys_addr + reg->memory_size - gpa;
            return 0;
        }
    }

    return -EFAULT;
}

int vhost_device_iotlb_miss(struct vhost_dev *dev, uint64_t iova, int write)
{
    IOMMUTLBEntry iotlb;
    uint64_t uaddr, len;
    int ret = -EFAULT;

    rcu_read_lock();

    iotlb = address_space_get_iotlb_entry(dev->vdev->dma_as,
                                          iova, write);
    if (iotlb.target_as != NULL) {
        ret = vhost_memory_region_lookup(dev, iotlb.translated_addr,
                                         &uaddr, &len);
        if (ret) {
            error_report("Fail to lookup the translated address "
                         "%"PRIx64, iotlb.translated_addr);
            goto out;
        }

        len = MIN(iotlb.addr_mask + 1, len);
        iova = iova & ~iotlb.addr_mask;

        ret = vhost_backend_update_device_iotlb(dev, iova, uaddr,
                                                len, iotlb.perm);
        if (ret) {
            error_report("Fail to update device iotlb");
            goto out;
        }
    }
out:
    rcu_read_unlock();

    return ret;
}

static int vhost_virtqueue_start(struct vhost_dev *dev,
                                struct VirtIODevice *vdev,
                                struct vhost_virtqueue *vq,
                                unsigned idx)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    hwaddr s, l, a;
    int r;
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, idx);
    struct vhost_vring_file file = {
        .index = vhost_vq_index
    };
    struct vhost_vring_state state = {
        .index = vhost_vq_index
    };
    struct VirtQueue *vvq = virtio_get_queue(vdev, idx);


    vq->num = state.num = virtio_queue_get_num(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_num(dev, &state);
    if (r) {
        VHOST_OPS_DEBUG("vhost_set_vring_num failed");
        return -errno;
    }

    state.num = virtio_queue_get_last_avail_idx(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_base(dev, &state);
    if (r) {
        VHOST_OPS_DEBUG("vhost_set_vring_base failed");
        return -errno;
    }

    if (vhost_needs_vring_endian(vdev)) {
        r = vhost_virtqueue_set_vring_endian_legacy(dev,
                                                    virtio_is_big_endian(vdev),
                                                    vhost_vq_index);
        if (r) {
            return -errno;
        }
    }

    vq->desc_size = s = l = virtio_queue_get_desc_size(vdev, idx);
    vq->desc_phys = a = virtio_queue_get_desc_addr(vdev, idx);
    vq->desc = vhost_memory_map(dev, a, &l, 0);
    if (!vq->desc || l != s) {
        r = -ENOMEM;
        goto fail_alloc_desc;
    }
    vq->avail_size = s = l = virtio_queue_get_avail_size(vdev, idx);
    vq->avail_phys = a = virtio_queue_get_avail_addr(vdev, idx);
    vq->avail = vhost_memory_map(dev, a, &l, 0);
    if (!vq->avail || l != s) {
        r = -ENOMEM;
        goto fail_alloc_avail;
    }
    vq->used_size = s = l = virtio_queue_get_used_size(vdev, idx);
    vq->used_phys = a = virtio_queue_get_used_addr(vdev, idx);
    vq->used = vhost_memory_map(dev, a, &l, 1);
    if (!vq->used || l != s) {
        r = -ENOMEM;
        goto fail_alloc_used;
    }

    r = vhost_virtqueue_set_addr(dev, vq, vhost_vq_index, dev->log_enabled);
    if (r < 0) {
        r = -errno;
        goto fail_alloc;
    }

    file.fd = event_notifier_get_fd(virtio_queue_get_host_notifier(vvq));
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
    if (r) {
        VHOST_OPS_DEBUG("vhost_set_vring_kick failed");
        r = -errno;
        goto fail_kick;
    }

    /* Clear and discard previous events if any. */
    event_notifier_test_and_clear(&vq->masked_notifier);

    /* Init vring in unmasked state, unless guest_notifier_mask
     * will do it later.
     */
    if (!vdev->use_guest_notifier_mask) {
        /* TODO: check and handle errors. */
        vhost_virtqueue_mask(dev, vdev, idx, false);
    }

    if (k->query_guest_notifiers &&
        k->query_guest_notifiers(qbus->parent) &&
        virtio_queue_vector(vdev, idx) == VIRTIO_NO_VECTOR) {
        file.fd = -1;
        r = dev->vhost_ops->vhost_set_vring_call(dev, &file);
        if (r) {
            goto fail_vector;
        }
    }

    return 0;

fail_vector:
fail_kick:
fail_alloc:
    vhost_memory_unmap(dev, vq->used, virtio_queue_get_used_size(vdev, idx),
                       0, 0);
fail_alloc_used:
    vhost_memory_unmap(dev, vq->avail, virtio_queue_get_avail_size(vdev, idx),
                       0, 0);
fail_alloc_avail:
    vhost_memory_unmap(dev, vq->desc, virtio_queue_get_desc_size(vdev, idx),
                       0, 0);
fail_alloc_desc:
    return r;
}

static void vhost_virtqueue_stop(struct vhost_dev *dev,
                                    struct VirtIODevice *vdev,
                                    struct vhost_virtqueue *vq,
                                    unsigned idx)
{
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, idx);
    struct vhost_vring_state state = {
        .index = vhost_vq_index,
    };
    int r;

    r = dev->vhost_ops->vhost_get_vring_base(dev, &state);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost VQ %d ring restore failed: %d", idx, r);
    } else {
        virtio_queue_set_last_avail_idx(vdev, idx, state.num);
    }
    virtio_queue_invalidate_signalled_used(vdev, idx);
    virtio_queue_update_used_idx(vdev, idx);

    /* In the cross-endian case, we need to reset the vring endianness to
     * native as legacy devices expect so by default.
     */
    if (vhost_needs_vring_endian(vdev)) {
        vhost_virtqueue_set_vring_endian_legacy(dev,
                                                !virtio_is_big_endian(vdev),
                                                vhost_vq_index);
    }

    vhost_memory_unmap(dev, vq->used, virtio_queue_get_used_size(vdev, idx),
                       1, virtio_queue_get_used_size(vdev, idx));
    vhost_memory_unmap(dev, vq->avail, virtio_queue_get_avail_size(vdev, idx),
                       0, virtio_queue_get_avail_size(vdev, idx));
    vhost_memory_unmap(dev, vq->desc, virtio_queue_get_desc_size(vdev, idx),
                       0, virtio_queue_get_desc_size(vdev, idx));
}

static void vhost_eventfd_add(MemoryListener *listener,
                              MemoryRegionSection *section,
                              bool match_data, uint64_t data, EventNotifier *e)
{
}

static void vhost_eventfd_del(MemoryListener *listener,
                              MemoryRegionSection *section,
                              bool match_data, uint64_t data, EventNotifier *e)
{
}

static int vhost_virtqueue_set_busyloop_timeout(struct vhost_dev *dev,
                                                int n, uint32_t timeout)
{
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, n);
    struct vhost_vring_state state = {
        .index = vhost_vq_index,
        .num = timeout,
    };
    int r;

    if (!dev->vhost_ops->vhost_set_vring_busyloop_timeout) {
        return -EINVAL;
    }

    r = dev->vhost_ops->vhost_set_vring_busyloop_timeout(dev, &state);
    if (r) {
        VHOST_OPS_DEBUG("vhost_set_vring_busyloop_timeout failed");
        return r;
    }

    return 0;
}

static int vhost_virtqueue_init(struct vhost_dev *dev,
                                struct vhost_virtqueue *vq, int n)
{
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, n);
    struct vhost_vring_file file = {
        .index = vhost_vq_index,
    };
    int r = event_notifier_init(&vq->masked_notifier, 0);
    if (r < 0) {
        return r;
    }

    file.fd = event_notifier_get_fd(&vq->masked_notifier);
    r = dev->vhost_ops->vhost_set_vring_call(dev, &file);
    if (r) {
        VHOST_OPS_DEBUG("vhost_set_vring_call failed");
        r = -errno;
        goto fail_call;
    }

    vq->dev = dev;

    return 0;
fail_call:
    event_notifier_cleanup(&vq->masked_notifier);
    return r;
}

static void vhost_virtqueue_cleanup(struct vhost_virtqueue *vq)
{
    event_notifier_cleanup(&vq->masked_notifier);
}

int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout)
{
    uint64_t features;
    int i, r, n_initialized_vqs = 0;
    Error *local_err = NULL;

    hdev->vdev = NULL;
    hdev->migration_blocker = NULL;

    r = vhost_set_backend_type(hdev, backend_type);
    assert(r >= 0);

    r = hdev->vhost_ops->vhost_backend_init(hdev, opaque);
    if (r < 0) {
        goto fail;
    }

    if (used_memslots > hdev->vhost_ops->vhost_backend_memslots_limit(hdev)) {
        error_report("vhost backend memory slots limit is less"
                " than current number of present memory slots");
        r = -1;
        goto fail;
    }

    r = hdev->vhost_ops->vhost_set_owner(hdev);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_owner failed");
        goto fail;
    }

    r = hdev->vhost_ops->vhost_get_features(hdev, &features);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_get_features failed");
        goto fail;
    }

    for (i = 0; i < hdev->nvqs; ++i, ++n_initialized_vqs) {
        r = vhost_virtqueue_init(hdev, hdev->vqs + i, hdev->vq_index + i);
        if (r < 0) {
            goto fail;
        }
    }

    if (busyloop_timeout) {
        for (i = 0; i < hdev->nvqs; ++i) {
            r = vhost_virtqueue_set_busyloop_timeout(hdev, hdev->vq_index + i,
                                                     busyloop_timeout);
            if (r < 0) {
                goto fail_busyloop;
            }
        }
    }

    hdev->features = features;

    hdev->memory_listener = (MemoryListener) {
        .begin = vhost_begin,
        .commit = vhost_commit,
        .region_add = vhost_region_add,
        .region_del = vhost_region_del,
        .region_nop = vhost_region_nop,
        .log_start = vhost_log_start,
        .log_stop = vhost_log_stop,
        .log_sync = vhost_log_sync,
        .log_global_start = vhost_log_global_start,
        .log_global_stop = vhost_log_global_stop,
        .eventfd_add = vhost_eventfd_add,
        .eventfd_del = vhost_eventfd_del,
        .priority = 10
    };

    hdev->iommu_listener = (MemoryListener) {
        .region_add = vhost_iommu_region_add,
        .region_del = vhost_iommu_region_del,
    };

    if (hdev->migration_blocker == NULL) {
        if (!(hdev->features & (0x1ULL << VHOST_F_LOG_ALL))) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: vhost lacks VHOST_F_LOG_ALL feature.");
        } else if (vhost_dev_log_is_shared(hdev) && !qemu_memfd_check()) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: failed to allocate shared memory");
        }
    }

    if (hdev->migration_blocker != NULL) {
        r = migrate_add_blocker(hdev->migration_blocker, &local_err);
        if (local_err) {
            error_report_err(local_err);
            error_free(hdev->migration_blocker);
            goto fail_busyloop;
        }
    }

    hdev->mem = g_malloc0(offsetof(struct vhost_memory, regions));
    hdev->n_mem_sections = 0;
    hdev->mem_sections = NULL;
    hdev->log = NULL;
    hdev->log_size = 0;
    hdev->log_enabled = false;
    hdev->started = false;
    hdev->memory_changed = false;
    memory_listener_register(&hdev->memory_listener, &address_space_memory);
    QLIST_INSERT_HEAD(&vhost_devices, hdev, entry);
    return 0;

fail_busyloop:
    while (--i >= 0) {
        vhost_virtqueue_set_busyloop_timeout(hdev, hdev->vq_index + i, 0);
    }
fail:
    hdev->nvqs = n_initialized_vqs;
    vhost_dev_cleanup(hdev);
    return r;
}

void vhost_dev_cleanup(struct vhost_dev *hdev)
{
    int i;

    for (i = 0; i < hdev->nvqs; ++i) {
        vhost_virtqueue_cleanup(hdev->vqs + i);
    }
    if (hdev->mem) {
        /* those are only safe after successful init */
        memory_listener_unregister(&hdev->memory_listener);
        for (i = 0; i < hdev->n_mem_sections; ++i) {
            MemoryRegionSection *section = &hdev->mem_sections[i];
            memory_region_unref(section->mr);
        }
        QLIST_REMOVE(hdev, entry);
    }
    if (hdev->migration_blocker) {
        migrate_del_blocker(hdev->migration_blocker);
        error_free(hdev->migration_blocker);
    }
    g_free(hdev->mem);
    g_free(hdev->mem_sections);
    if (hdev->vhost_ops) {
        hdev->vhost_ops->vhost_backend_cleanup(hdev);
    }
    assert(!hdev->log);

    memset(hdev, 0, sizeof(struct vhost_dev));
}

/* Stop processing guest IO notifications in qemu.
 * Start processing them in vhost in kernel.
 */
int vhost_dev_enable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    int i, r, e;

    /* We will pass the notifiers to the kernel, make sure that QEMU
     * doesn't interfere.
     */
    r = virtio_device_grab_ioeventfd(vdev);
    if (r < 0) {
        error_report("binding does not support host notifiers");
        goto fail;
    }

    for (i = 0; i < hdev->nvqs; ++i) {
        r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), hdev->vq_index + i,
                                         true);
        if (r < 0) {
            error_report("vhost VQ %d notifier binding failed: %d", i, -r);
            goto fail_vq;
        }
    }

    return 0;
fail_vq:
    while (--i >= 0) {
        e = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), hdev->vq_index + i,
                                         false);
        if (e < 0) {
            error_report("vhost VQ %d notifier cleanup error: %d", i, -r);
        }
        assert (e >= 0);
    }
    virtio_device_release_ioeventfd(vdev);
fail:
    return r;
}

/* Stop processing guest IO notifications in vhost.
 * Start processing them in qemu.
 * This might actually run the qemu handlers right away,
 * so virtio in qemu must be completely setup when this is called.
 */
void vhost_dev_disable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(vdev)));
    int i, r;

    for (i = 0; i < hdev->nvqs; ++i) {
        r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), hdev->vq_index + i,
                                         false);
        if (r < 0) {
            error_report("vhost VQ %d notifier cleanup failed: %d", i, -r);
        }
        assert (r >= 0);
    }
    virtio_device_release_ioeventfd(vdev);
}

/* Test and clear event pending status.
 * Should be called after unmask to avoid losing events.
 */
bool vhost_virtqueue_pending(struct vhost_dev *hdev, int n)
{
    struct vhost_virtqueue *vq = hdev->vqs + n - hdev->vq_index;
    assert(n >= hdev->vq_index && n < hdev->vq_index + hdev->nvqs);
    return event_notifier_test_and_clear(&vq->masked_notifier);
}

/* Mask/unmask events from this vq. */
void vhost_virtqueue_mask(struct vhost_dev *hdev, VirtIODevice *vdev, int n,
                         bool mask)
{
    struct VirtQueue *vvq = virtio_get_queue(vdev, n);
    int r, index = n - hdev->vq_index;
    struct vhost_vring_file file;

    /* should only be called after backend is connected */
    assert(hdev->vhost_ops);

    if (mask) {
        assert(vdev->use_guest_notifier_mask);
        file.fd = event_notifier_get_fd(&hdev->vqs[index].masked_notifier);
    } else {
        file.fd = event_notifier_get_fd(virtio_queue_get_guest_notifier(vvq));
    }

    file.index = hdev->vhost_ops->vhost_get_vq_index(hdev, n);
    r = hdev->vhost_ops->vhost_set_vring_call(hdev, &file);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_vring_call failed");
    }
}

uint64_t vhost_get_features(struct vhost_dev *hdev, const int *feature_bits,
                            uint64_t features)
{
    const int *bit = feature_bits;
    while (*bit != VHOST_INVALID_FEATURE_BIT) {
        uint64_t bit_mask = (1ULL << *bit);
        if (!(hdev->features & bit_mask)) {
            features &= ~bit_mask;
        }
        bit++;
    }
    return features;
}

void vhost_ack_features(struct vhost_dev *hdev, const int *feature_bits,
                        uint64_t features)
{
    const int *bit = feature_bits;
    while (*bit != VHOST_INVALID_FEATURE_BIT) {
        uint64_t bit_mask = (1ULL << *bit);
        if (features & bit_mask) {
            hdev->acked_features |= bit_mask;
        }
        bit++;
    }
}

/* Host notifiers must be enabled at this point. */
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    int i, r;

    /* should only be called after backend is connected */
    assert(hdev->vhost_ops);

    hdev->started = true;
    hdev->vdev = vdev;

    r = vhost_dev_set_features(hdev, hdev->log_enabled);
    if (r < 0) {
        goto fail_features;
    }

    if (vhost_dev_has_iommu(hdev)) {
        memory_listener_register(&hdev->iommu_listener, vdev->dma_as);
    }

    r = hdev->vhost_ops->vhost_set_mem_table(hdev, hdev->mem);
    if (r < 0) {
        VHOST_OPS_DEBUG("vhost_set_mem_table failed");
        r = -errno;
        goto fail_mem;
    }
    for (i = 0; i < hdev->nvqs; ++i) {
        r = vhost_virtqueue_start(hdev,
                                  vdev,
                                  hdev->vqs + i,
                                  hdev->vq_index + i);
        if (r < 0) {
            goto fail_vq;
        }
    }

    if (hdev->log_enabled) {
        uint64_t log_base;

        hdev->log_size = vhost_get_log_size(hdev);
        hdev->log = vhost_log_get(hdev->log_size,
                                  vhost_dev_log_is_shared(hdev));
        log_base = (uintptr_t)hdev->log->log;
        r = hdev->vhost_ops->vhost_set_log_base(hdev,
                                                hdev->log_size ? log_base : 0,
                                                hdev->log);
        if (r < 0) {
            VHOST_OPS_DEBUG("vhost_set_log_base failed");
            r = -errno;
            goto fail_log;
        }
    }

    if (vhost_dev_has_iommu(hdev)) {
        hdev->vhost_ops->vhost_set_iotlb_callback(hdev, true);

        /* Update used ring information for IOTLB to work correctly,
         * vhost-kernel code requires for this.*/
        for (i = 0; i < hdev->nvqs; ++i) {
            struct vhost_virtqueue *vq = hdev->vqs + i;
            vhost_device_iotlb_miss(hdev, vq->used_phys, true);
        }
    }
    return 0;
fail_log:
    vhost_log_put(hdev, false);
fail_vq:
    while (--i >= 0) {
        vhost_virtqueue_stop(hdev,
                             vdev,
                             hdev->vqs + i,
                             hdev->vq_index + i);
    }
    i = hdev->nvqs;

fail_mem:
fail_features:

    hdev->started = false;
    return r;
}

/* Host notifiers must be enabled at this point. */
void vhost_dev_stop(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    int i;

    /* should only be called after backend is connected */
    assert(hdev->vhost_ops);

    for (i = 0; i < hdev->nvqs; ++i) {
        vhost_virtqueue_stop(hdev,
                             vdev,
                             hdev->vqs + i,
                             hdev->vq_index + i);
    }

    if (vhost_dev_has_iommu(hdev)) {
        hdev->vhost_ops->vhost_set_iotlb_callback(hdev, false);
        memory_listener_unregister(&hdev->iommu_listener);
    }
    vhost_log_put(hdev, true);
    hdev->started = false;
    hdev->vdev = NULL;
}

int vhost_net_set_backend(struct vhost_dev *hdev,
                          struct vhost_vring_file *file)
{
    if (hdev->vhost_ops->vhost_net_set_backend) {
        return hdev->vhost_ops->vhost_net_set_backend(hdev, file);
    }

    return -1;
}
