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
#include "qemu/atomic.h"
#include "qemu/range.h"
#include "qemu/error-report.h"
#include "qemu/memfd.h"
#include "qemu/log.h"
#include "standard-headers/linux/vhost_types.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "migration/blocker.h"
#include "migration/qemu-file-types.h"
#include "sysemu/dma.h"
#include "trace.h"

/* enabled until disconnected backend stabilizes */
#define _VHOST_DEBUG 1

#ifdef _VHOST_DEBUG
#define VHOST_OPS_DEBUG(retval, fmt, ...) \
    do { \
        error_report(fmt ": %s (%d)", ## __VA_ARGS__, \
                     strerror(-retval), -retval); \
    } while (0)
#else
#define VHOST_OPS_DEBUG(retval, fmt, ...) \
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
    uint64_t addr = QEMU_ALIGN_DOWN(start, VHOST_LOG_CHUNK);

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
        log = qatomic_xchg(from, 0);
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

bool vhost_dev_has_iommu(struct vhost_dev *dev)
{
    VirtIODevice *vdev = dev->vdev;

    /*
     * For vhost, VIRTIO_F_IOMMU_PLATFORM means the backend support
     * incremental memory mapping API via IOTLB API. For platform that
     * does not have IOMMU, there's no need to enable this feature
     * which may cause unnecessary IOTLB miss/update transactions.
     */
    if (vdev) {
        return virtio_bus_device_iommu_enabled(vdev) &&
            virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM);
    } else {
        return false;
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

        if (!vq->used_phys && !vq->used_size) {
            continue;
        }

        if (vhost_dev_has_iommu(dev)) {
            IOMMUTLBEntry iotlb;
            hwaddr used_phys = vq->used_phys, used_size = vq->used_size;
            hwaddr phys, s, offset;

            while (used_size) {
                rcu_read_lock();
                iotlb = address_space_get_iotlb_entry(dev->vdev->dma_as,
                                                      used_phys,
                                                      true,
                                                      MEMTXATTRS_UNSPECIFIED);
                rcu_read_unlock();

                if (!iotlb.target_as) {
                    qemu_log_mask(LOG_GUEST_ERROR, "translation "
                                  "failure for used_iova %"PRIx64"\n",
                                  used_phys);
                    return -EINVAL;
                }

                offset = used_phys & iotlb.addr_mask;
                phys = iotlb.translated_addr + offset;

                /*
                 * Distance from start of used ring until last byte of
                 * IOMMU page.
                 */
                s = iotlb.addr_mask - offset;
                /*
                 * Size of used ring, or of the part of it until end
                 * of IOMMU page. To avoid zero result, do the adding
                 * outside of MIN().
                 */
                s = MIN(s, used_size - 1) + 1;

                vhost_dev_sync_region(dev, section, start_addr, end_addr, phys,
                                      range_get_last(phys, s));
                used_size -= s;
                used_phys += s;
            }
        } else {
            vhost_dev_sync_region(dev, section, start_addr,
                                  end_addr, vq->used_phys,
                                  range_get_last(vq->used_phys, vq->used_size));
        }
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
    return log_size;
}

static int vhost_set_backend_type(struct vhost_dev *dev,
                                  VhostBackendType backend_type)
{
    int r = 0;

    switch (backend_type) {
#ifdef CONFIG_VHOST_KERNEL
    case VHOST_BACKEND_TYPE_KERNEL:
        dev->vhost_ops = &kernel_ops;
        break;
#endif
#ifdef CONFIG_VHOST_USER
    case VHOST_BACKEND_TYPE_USER:
        dev->vhost_ops = &user_ops;
        break;
#endif
#ifdef CONFIG_VHOST_VDPA
    case VHOST_BACKEND_TYPE_VDPA:
        dev->vhost_ops = &vdpa_ops;
        break;
#endif
    default:
        error_report("Unknown vhost backend type");
        r = -1;
    }

    return r;
}

static struct vhost_log *vhost_log_alloc(uint64_t size, bool share)
{
    Error *err = NULL;
    struct vhost_log *log;
    uint64_t logsize = size * sizeof(*(log->log));
    int fd = -1;

    log = g_new0(struct vhost_log, 1);
    if (share) {
        log->log = qemu_memfd_alloc("vhost-log", logsize,
                                    F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL,
                                    &fd, &err);
        if (err) {
            error_report_err(err);
            g_free(log);
            return NULL;
        }
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

    dev->log = NULL;
    dev->log_size = 0;
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
        VHOST_OPS_DEBUG(r, "vhost_set_log_base failed");
    }

    vhost_log_put(dev, true);
    dev->log = log;
    dev->log_size = size;
}

static void *vhost_memory_map(struct vhost_dev *dev, hwaddr addr,
                              hwaddr *plen, bool is_write)
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

static int vhost_verify_ring_part_mapping(void *ring_hva,
                                          uint64_t ring_gpa,
                                          uint64_t ring_size,
                                          void *reg_hva,
                                          uint64_t reg_gpa,
                                          uint64_t reg_size)
{
    uint64_t hva_ring_offset;
    uint64_t ring_last = range_get_last(ring_gpa, ring_size);
    uint64_t reg_last = range_get_last(reg_gpa, reg_size);

    if (ring_last < reg_gpa || ring_gpa > reg_last) {
        return 0;
    }
    /* check that whole ring's is mapped */
    if (ring_last > reg_last) {
        return -ENOMEM;
    }
    /* check that ring's MemoryRegion wasn't replaced */
    hva_ring_offset = ring_gpa - reg_gpa;
    if (ring_hva != reg_hva + hva_ring_offset) {
        return -EBUSY;
    }

    return 0;
}

static int vhost_verify_ring_mappings(struct vhost_dev *dev,
                                      void *reg_hva,
                                      uint64_t reg_gpa,
                                      uint64_t reg_size)
{
    int i, j;
    int r = 0;
    const char *part_name[] = {
        "descriptor table",
        "available ring",
        "used ring"
    };

    if (vhost_dev_has_iommu(dev)) {
        return 0;
    }

    for (i = 0; i < dev->nvqs; ++i) {
        struct vhost_virtqueue *vq = dev->vqs + i;

        if (vq->desc_phys == 0) {
            continue;
        }

        j = 0;
        r = vhost_verify_ring_part_mapping(
                vq->desc, vq->desc_phys, vq->desc_size,
                reg_hva, reg_gpa, reg_size);
        if (r) {
            break;
        }

        j++;
        r = vhost_verify_ring_part_mapping(
                vq->avail, vq->avail_phys, vq->avail_size,
                reg_hva, reg_gpa, reg_size);
        if (r) {
            break;
        }

        j++;
        r = vhost_verify_ring_part_mapping(
                vq->used, vq->used_phys, vq->used_size,
                reg_hva, reg_gpa, reg_size);
        if (r) {
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

/*
 * vhost_section: identify sections needed for vhost access
 *
 * We only care about RAM sections here (where virtqueue and guest
 * internals accessed by virtio might live). If we find one we still
 * allow the backend to potentially filter it out of our list.
 */
static bool vhost_section(struct vhost_dev *dev, MemoryRegionSection *section)
{
    MemoryRegion *mr = section->mr;

    if (memory_region_is_ram(mr) && !memory_region_is_rom(mr)) {
        uint8_t dirty_mask = memory_region_get_dirty_log_mask(mr);
        uint8_t handled_dirty;

        /*
         * Kernel based vhost doesn't handle any block which is doing
         * dirty-tracking other than migration for which it has
         * specific logging support. However for TCG the kernel never
         * gets involved anyway so we can also ignore it's
         * self-modiying code detection flags. However a vhost-user
         * client could still confuse a TCG guest if it re-writes
         * executable memory that has already been translated.
         */
        handled_dirty = (1 << DIRTY_MEMORY_MIGRATION) |
            (1 << DIRTY_MEMORY_CODE);

        if (dirty_mask & ~handled_dirty) {
            trace_vhost_reject_section(mr->name, 1);
            return false;
        }

        if (dev->vhost_ops->vhost_backend_mem_section_filter &&
            !dev->vhost_ops->vhost_backend_mem_section_filter(dev, section)) {
            trace_vhost_reject_section(mr->name, 2);
            return false;
        }

        trace_vhost_section(mr->name);
        return true;
    } else {
        trace_vhost_reject_section(mr->name, 3);
        return false;
    }
}

static void vhost_begin(MemoryListener *listener)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    dev->tmp_sections = NULL;
    dev->n_tmp_sections = 0;
}

static void vhost_commit(MemoryListener *listener)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    MemoryRegionSection *old_sections;
    int n_old_sections;
    uint64_t log_size;
    size_t regions_size;
    int r;
    int i;
    bool changed = false;

    /* Note we can be called before the device is started, but then
     * starting the device calls set_mem_table, so we need to have
     * built the data structures.
     */
    old_sections = dev->mem_sections;
    n_old_sections = dev->n_mem_sections;
    dev->mem_sections = dev->tmp_sections;
    dev->n_mem_sections = dev->n_tmp_sections;

    if (dev->n_mem_sections != n_old_sections) {
        changed = true;
    } else {
        /* Same size, lets check the contents */
        for (int i = 0; i < n_old_sections; i++) {
            if (!MemoryRegionSection_eq(&old_sections[i],
                                        &dev->mem_sections[i])) {
                changed = true;
                break;
            }
        }
    }

    trace_vhost_commit(dev->started, changed);
    if (!changed) {
        goto out;
    }

    /* Rebuild the regions list from the new sections list */
    regions_size = offsetof(struct vhost_memory, regions) +
                       dev->n_mem_sections * sizeof dev->mem->regions[0];
    dev->mem = g_realloc(dev->mem, regions_size);
    dev->mem->nregions = dev->n_mem_sections;
    used_memslots = dev->mem->nregions;
    for (i = 0; i < dev->n_mem_sections; i++) {
        struct vhost_memory_region *cur_vmr = dev->mem->regions + i;
        struct MemoryRegionSection *mrs = dev->mem_sections + i;

        cur_vmr->guest_phys_addr = mrs->offset_within_address_space;
        cur_vmr->memory_size     = int128_get64(mrs->size);
        cur_vmr->userspace_addr  =
            (uintptr_t)memory_region_get_ram_ptr(mrs->mr) +
            mrs->offset_within_region;
        cur_vmr->flags_padding   = 0;
    }

    if (!dev->started) {
        goto out;
    }

    for (i = 0; i < dev->mem->nregions; i++) {
        if (vhost_verify_ring_mappings(dev,
                       (void *)(uintptr_t)dev->mem->regions[i].userspace_addr,
                       dev->mem->regions[i].guest_phys_addr,
                       dev->mem->regions[i].memory_size)) {
            error_report("Verify ring failure on region %d", i);
            abort();
        }
    }

    if (!dev->log_enabled) {
        r = dev->vhost_ops->vhost_set_mem_table(dev, dev->mem);
        if (r < 0) {
            VHOST_OPS_DEBUG(r, "vhost_set_mem_table failed");
        }
        goto out;
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
        VHOST_OPS_DEBUG(r, "vhost_set_mem_table failed");
    }
    /* To log less, can only decrease log size after table update. */
    if (dev->log_size > log_size + VHOST_LOG_BUFFER) {
        vhost_dev_log_resize(dev, log_size);
    }

out:
    /* Deref the old list of sections, this must happen _after_ the
     * vhost_set_mem_table to ensure the client isn't still using the
     * section we're about to unref.
     */
    while (n_old_sections--) {
        memory_region_unref(old_sections[n_old_sections].mr);
    }
    g_free(old_sections);
    return;
}

/* Adds the section data to the tmp_section structure.
 * It relies on the listener calling us in memory address order
 * and for each region (via the _add and _nop methods) to
 * join neighbours.
 */
static void vhost_region_add_section(struct vhost_dev *dev,
                                     MemoryRegionSection *section)
{
    bool need_add = true;
    uint64_t mrs_size = int128_get64(section->size);
    uint64_t mrs_gpa = section->offset_within_address_space;
    uintptr_t mrs_host = (uintptr_t)memory_region_get_ram_ptr(section->mr) +
                         section->offset_within_region;
    RAMBlock *mrs_rb = section->mr->ram_block;

    trace_vhost_region_add_section(section->mr->name, mrs_gpa, mrs_size,
                                   mrs_host);

    if (dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER) {
        /* Round the section to it's page size */
        /* First align the start down to a page boundary */
        size_t mrs_page = qemu_ram_pagesize(mrs_rb);
        uint64_t alignage = mrs_host & (mrs_page - 1);
        if (alignage) {
            mrs_host -= alignage;
            mrs_size += alignage;
            mrs_gpa  -= alignage;
        }
        /* Now align the size up to a page boundary */
        alignage = mrs_size & (mrs_page - 1);
        if (alignage) {
            mrs_size += mrs_page - alignage;
        }
        trace_vhost_region_add_section_aligned(section->mr->name, mrs_gpa,
                                               mrs_size, mrs_host);
    }

    if (dev->n_tmp_sections) {
        /* Since we already have at least one section, lets see if
         * this extends it; since we're scanning in order, we only
         * have to look at the last one, and the FlatView that calls
         * us shouldn't have overlaps.
         */
        MemoryRegionSection *prev_sec = dev->tmp_sections +
                                               (dev->n_tmp_sections - 1);
        uint64_t prev_gpa_start = prev_sec->offset_within_address_space;
        uint64_t prev_size = int128_get64(prev_sec->size);
        uint64_t prev_gpa_end   = range_get_last(prev_gpa_start, prev_size);
        uint64_t prev_host_start =
                        (uintptr_t)memory_region_get_ram_ptr(prev_sec->mr) +
                        prev_sec->offset_within_region;
        uint64_t prev_host_end   = range_get_last(prev_host_start, prev_size);

        if (mrs_gpa <= (prev_gpa_end + 1)) {
            /* OK, looks like overlapping/intersecting - it's possible that
             * the rounding to page sizes has made them overlap, but they should
             * match up in the same RAMBlock if they do.
             */
            if (mrs_gpa < prev_gpa_start) {
                error_report("%s:Section '%s' rounded to %"PRIx64
                             " prior to previous '%s' %"PRIx64,
                             __func__, section->mr->name, mrs_gpa,
                             prev_sec->mr->name, prev_gpa_start);
                /* A way to cleanly fail here would be better */
                return;
            }
            /* Offset from the start of the previous GPA to this GPA */
            size_t offset = mrs_gpa - prev_gpa_start;

            if (prev_host_start + offset == mrs_host &&
                section->mr == prev_sec->mr &&
                (!dev->vhost_ops->vhost_backend_can_merge ||
                 dev->vhost_ops->vhost_backend_can_merge(dev,
                    mrs_host, mrs_size,
                    prev_host_start, prev_size))) {
                uint64_t max_end = MAX(prev_host_end, mrs_host + mrs_size);
                need_add = false;
                prev_sec->offset_within_address_space =
                    MIN(prev_gpa_start, mrs_gpa);
                prev_sec->offset_within_region =
                    MIN(prev_host_start, mrs_host) -
                    (uintptr_t)memory_region_get_ram_ptr(prev_sec->mr);
                prev_sec->size = int128_make64(max_end - MIN(prev_host_start,
                                               mrs_host));
                trace_vhost_region_add_section_merge(section->mr->name,
                                        int128_get64(prev_sec->size),
                                        prev_sec->offset_within_address_space,
                                        prev_sec->offset_within_region);
            } else {
                /* adjoining regions are fine, but overlapping ones with
                 * different blocks/offsets shouldn't happen
                 */
                if (mrs_gpa != prev_gpa_end + 1) {
                    error_report("%s: Overlapping but not coherent sections "
                                 "at %"PRIx64,
                                 __func__, mrs_gpa);
                    return;
                }
            }
        }
    }

    if (need_add) {
        ++dev->n_tmp_sections;
        dev->tmp_sections = g_renew(MemoryRegionSection, dev->tmp_sections,
                                    dev->n_tmp_sections);
        dev->tmp_sections[dev->n_tmp_sections - 1] = *section;
        /* The flatview isn't stable and we don't use it, making it NULL
         * means we can memcmp the list.
         */
        dev->tmp_sections[dev->n_tmp_sections - 1].fv = NULL;
        memory_region_ref(section->mr);
    }
}

/* Used for both add and nop callbacks */
static void vhost_region_addnop(MemoryListener *listener,
                                MemoryRegionSection *section)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);

    if (!vhost_section(dev, section)) {
        return;
    }
    vhost_region_add_section(dev, section);
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
    int iommu_idx;
    IOMMUMemoryRegion *iommu_mr;
    int ret;

    if (!memory_region_is_iommu(section->mr)) {
        return;
    }

    iommu_mr = IOMMU_MEMORY_REGION(section->mr);

    iommu = g_malloc0(sizeof(*iommu));
    end = int128_add(int128_make64(section->offset_within_region),
                     section->size);
    end = int128_sub(end, int128_one());
    iommu_idx = memory_region_iommu_attrs_to_index(iommu_mr,
                                                   MEMTXATTRS_UNSPECIFIED);
    iommu_notifier_init(&iommu->n, vhost_iommu_unmap_notify,
                        IOMMU_NOTIFIER_DEVIOTLB_UNMAP,
                        section->offset_within_region,
                        int128_get64(end),
                        iommu_idx);
    iommu->mr = section->mr;
    iommu->iommu_offset = section->offset_within_address_space -
                          section->offset_within_region;
    iommu->hdev = dev;
    ret = memory_region_register_iommu_notifier(section->mr, &iommu->n, NULL);
    if (ret) {
        /*
         * Some vIOMMUs do not support dev-iotlb yet.  If so, try to use the
         * UNMAP legacy message
         */
        iommu->n.notifier_flags = IOMMU_NOTIFIER_UNMAP;
        memory_region_register_iommu_notifier(section->mr, &iommu->n,
                                              &error_fatal);
    }
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

static int vhost_virtqueue_set_addr(struct vhost_dev *dev,
                                    struct vhost_virtqueue *vq,
                                    unsigned idx, bool enable_log)
{
    struct vhost_vring_addr addr;
    int r;
    memset(&addr, 0, sizeof(struct vhost_vring_addr));

    if (dev->vhost_ops->vhost_vq_get_addr) {
        r = dev->vhost_ops->vhost_vq_get_addr(dev, &addr, vq);
        if (r < 0) {
            VHOST_OPS_DEBUG(r, "vhost_vq_get_addr failed");
            return r;
        }
    } else {
        addr.desc_user_addr = (uint64_t)(unsigned long)vq->desc;
        addr.avail_user_addr = (uint64_t)(unsigned long)vq->avail;
        addr.used_user_addr = (uint64_t)(unsigned long)vq->used;
    }
    addr.index = idx;
    addr.log_guest_addr = vq->used_phys;
    addr.flags = enable_log ? (1 << VHOST_VRING_F_LOG) : 0;
    r = dev->vhost_ops->vhost_set_vring_addr(dev, &addr);
    if (r < 0) {
        VHOST_OPS_DEBUG(r, "vhost_set_vring_addr failed");
    }
    return r;
}

static int vhost_dev_set_features(struct vhost_dev *dev,
                                  bool enable_log)
{
    uint64_t features = dev->acked_features;
    int r;
    if (enable_log) {
        features |= 0x1ULL << VHOST_F_LOG_ALL;
    }
    if (!vhost_dev_has_iommu(dev)) {
        features &= ~(0x1ULL << VIRTIO_F_IOMMU_PLATFORM);
    }
    if (dev->vhost_ops->vhost_force_iommu) {
        if (dev->vhost_ops->vhost_force_iommu(dev) == true) {
            features |= 0x1ULL << VIRTIO_F_IOMMU_PLATFORM;
       }
    }
    r = dev->vhost_ops->vhost_set_features(dev, features);
    if (r < 0) {
        VHOST_OPS_DEBUG(r, "vhost_set_features failed");
        goto out;
    }
    if (dev->vhost_ops->vhost_set_backend_cap) {
        r = dev->vhost_ops->vhost_set_backend_cap(dev);
        if (r < 0) {
            VHOST_OPS_DEBUG(r, "vhost_set_backend_cap failed");
            goto out;
        }
    }

out:
    return r;
}

static int vhost_dev_set_log(struct vhost_dev *dev, bool enable_log)
{
    int r, i, idx;
    hwaddr addr;

    r = vhost_dev_set_features(dev, enable_log);
    if (r < 0) {
        goto err_features;
    }
    for (i = 0; i < dev->nvqs; ++i) {
        idx = dev->vhost_ops->vhost_get_vq_index(dev, dev->vq_index + i);
        addr = virtio_queue_get_desc_addr(dev->vdev, idx);
        if (!addr) {
            /*
             * The queue might not be ready for start. If this
             * is the case there is no reason to continue the process.
             * The similar logic is used by the vhost_virtqueue_start()
             * routine.
             */
            continue;
        }
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
        addr = virtio_queue_get_desc_addr(dev->vdev, idx);
        if (!addr) {
            continue;
        }
        vhost_virtqueue_set_addr(dev, dev->vqs + i, idx,
                                 dev->log_enabled);
    }
    vhost_dev_set_features(dev, dev->log_enabled);
err_features:
    return r;
}

static int vhost_migration_log(MemoryListener *listener, bool enable)
{
    struct vhost_dev *dev = container_of(listener, struct vhost_dev,
                                         memory_listener);
    int r;
    if (enable == dev->log_enabled) {
        return 0;
    }
    if (!dev->started) {
        dev->log_enabled = enable;
        return 0;
    }

    r = 0;
    if (!enable) {
        r = vhost_dev_set_log(dev, false);
        if (r < 0) {
            goto check_dev_state;
        }
        vhost_log_put(dev, false);
    } else {
        vhost_dev_log_resize(dev, vhost_get_log_size(dev));
        r = vhost_dev_set_log(dev, true);
        if (r < 0) {
            goto check_dev_state;
        }
    }

check_dev_state:
    dev->log_enabled = enable;
    /*
     * vhost-user-* devices could change their state during log
     * initialization due to disconnect. So check dev state after
     * vhost communication.
     */
    if (!dev->started) {
        /*
         * Since device is in the stopped state, it is okay for
         * migration. Return success.
         */
        r = 0;
    }
    if (r) {
        /* An error occurred. */
        dev->log_enabled = false;
    }

    return r;
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
#if HOST_BIG_ENDIAN
    return vdev->device_endian == VIRTIO_DEVICE_ENDIAN_LITTLE;
#else
    return vdev->device_endian == VIRTIO_DEVICE_ENDIAN_BIG;
#endif
}

static int vhost_virtqueue_set_vring_endian_legacy(struct vhost_dev *dev,
                                                   bool is_big_endian,
                                                   int vhost_vq_index)
{
    int r;
    struct vhost_vring_state s = {
        .index = vhost_vq_index,
        .num = is_big_endian
    };

    r = dev->vhost_ops->vhost_set_vring_endian(dev, &s);
    if (r < 0) {
        VHOST_OPS_DEBUG(r, "vhost_set_vring_endian failed");
    }
    return r;
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

    RCU_READ_LOCK_GUARD();

    trace_vhost_iotlb_miss(dev, 1);

    iotlb = address_space_get_iotlb_entry(dev->vdev->dma_as,
                                          iova, write,
                                          MEMTXATTRS_UNSPECIFIED);
    if (iotlb.target_as != NULL) {
        ret = vhost_memory_region_lookup(dev, iotlb.translated_addr,
                                         &uaddr, &len);
        if (ret) {
            trace_vhost_iotlb_miss(dev, 3);
            error_report("Fail to lookup the translated address "
                         "%"PRIx64, iotlb.translated_addr);
            goto out;
        }

        len = MIN(iotlb.addr_mask + 1, len);
        iova = iova & ~iotlb.addr_mask;

        ret = vhost_backend_update_device_iotlb(dev, iova, uaddr,
                                                len, iotlb.perm);
        if (ret) {
            trace_vhost_iotlb_miss(dev, 4);
            error_report("Fail to update device iotlb");
            goto out;
        }
    }

    trace_vhost_iotlb_miss(dev, 2);

out:
    return ret;
}

int vhost_virtqueue_start(struct vhost_dev *dev,
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

    a = virtio_queue_get_desc_addr(vdev, idx);
    if (a == 0) {
        /* Queue might not be ready for start */
        return 0;
    }

    vq->num = state.num = virtio_queue_get_num(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_num(dev, &state);
    if (r) {
        VHOST_OPS_DEBUG(r, "vhost_set_vring_num failed");
        return r;
    }

    state.num = virtio_queue_get_last_avail_idx(vdev, idx);
    r = dev->vhost_ops->vhost_set_vring_base(dev, &state);
    if (r) {
        VHOST_OPS_DEBUG(r, "vhost_set_vring_base failed");
        return r;
    }

    if (vhost_needs_vring_endian(vdev)) {
        r = vhost_virtqueue_set_vring_endian_legacy(dev,
                                                    virtio_is_big_endian(vdev),
                                                    vhost_vq_index);
        if (r) {
            return r;
        }
    }

    vq->desc_size = s = l = virtio_queue_get_desc_size(vdev, idx);
    vq->desc_phys = a;
    vq->desc = vhost_memory_map(dev, a, &l, false);
    if (!vq->desc || l != s) {
        r = -ENOMEM;
        goto fail_alloc_desc;
    }
    vq->avail_size = s = l = virtio_queue_get_avail_size(vdev, idx);
    vq->avail_phys = a = virtio_queue_get_avail_addr(vdev, idx);
    vq->avail = vhost_memory_map(dev, a, &l, false);
    if (!vq->avail || l != s) {
        r = -ENOMEM;
        goto fail_alloc_avail;
    }
    vq->used_size = s = l = virtio_queue_get_used_size(vdev, idx);
    vq->used_phys = a = virtio_queue_get_used_addr(vdev, idx);
    vq->used = vhost_memory_map(dev, a, &l, true);
    if (!vq->used || l != s) {
        r = -ENOMEM;
        goto fail_alloc_used;
    }

    r = vhost_virtqueue_set_addr(dev, vq, vhost_vq_index, dev->log_enabled);
    if (r < 0) {
        goto fail_alloc;
    }

    file.fd = event_notifier_get_fd(virtio_queue_get_host_notifier(vvq));
    r = dev->vhost_ops->vhost_set_vring_kick(dev, &file);
    if (r) {
        VHOST_OPS_DEBUG(r, "vhost_set_vring_kick failed");
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

void vhost_virtqueue_stop(struct vhost_dev *dev,
                          struct VirtIODevice *vdev,
                          struct vhost_virtqueue *vq,
                          unsigned idx)
{
    int vhost_vq_index = dev->vhost_ops->vhost_get_vq_index(dev, idx);
    struct vhost_vring_state state = {
        .index = vhost_vq_index,
    };
    int r;

    if (virtio_queue_get_desc_addr(vdev, idx) == 0) {
        /* Don't stop the virtqueue which might have not been started */
        return;
    }

    r = dev->vhost_ops->vhost_get_vring_base(dev, &state);
    if (r < 0) {
        VHOST_OPS_DEBUG(r, "vhost VQ %u ring restore failed: %d", idx, r);
        /* Connection to the backend is broken, so let's sync internal
         * last avail idx to the device used idx.
         */
        virtio_queue_restore_last_avail_idx(vdev, idx);
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
        VHOST_OPS_DEBUG(r, "vhost_set_vring_busyloop_timeout failed");
        return r;
    }

    return 0;
}

static void vhost_virtqueue_error_notifier(EventNotifier *n)
{
    struct vhost_virtqueue *vq = container_of(n, struct vhost_virtqueue,
                                              error_notifier);
    struct vhost_dev *dev = vq->dev;
    int index = vq - dev->vqs;

    if (event_notifier_test_and_clear(n) && dev->vdev) {
        VHOST_OPS_DEBUG(-EINVAL,  "vhost vring error in virtqueue %d",
                        dev->vq_index + index);
    }
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

    file.fd = event_notifier_get_wfd(&vq->masked_notifier);
    r = dev->vhost_ops->vhost_set_vring_call(dev, &file);
    if (r) {
        VHOST_OPS_DEBUG(r, "vhost_set_vring_call failed");
        goto fail_call;
    }

    vq->dev = dev;

    if (dev->vhost_ops->vhost_set_vring_err) {
        r = event_notifier_init(&vq->error_notifier, 0);
        if (r < 0) {
            goto fail_call;
        }

        file.fd = event_notifier_get_fd(&vq->error_notifier);
        r = dev->vhost_ops->vhost_set_vring_err(dev, &file);
        if (r) {
            VHOST_OPS_DEBUG(r, "vhost_set_vring_err failed");
            goto fail_err;
        }

        event_notifier_set_handler(&vq->error_notifier,
                                   vhost_virtqueue_error_notifier);
    }

    return 0;

fail_err:
    event_notifier_cleanup(&vq->error_notifier);
fail_call:
    event_notifier_cleanup(&vq->masked_notifier);
    return r;
}

static void vhost_virtqueue_cleanup(struct vhost_virtqueue *vq)
{
    event_notifier_cleanup(&vq->masked_notifier);
    if (vq->dev->vhost_ops->vhost_set_vring_err) {
        event_notifier_set_handler(&vq->error_notifier, NULL);
        event_notifier_cleanup(&vq->error_notifier);
    }
}

int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, uint32_t busyloop_timeout,
                   Error **errp)
{
    uint64_t features;
    int i, r, n_initialized_vqs = 0;

    hdev->vdev = NULL;
    hdev->migration_blocker = NULL;

    r = vhost_set_backend_type(hdev, backend_type);
    assert(r >= 0);

    r = hdev->vhost_ops->vhost_backend_init(hdev, opaque, errp);
    if (r < 0) {
        goto fail;
    }

    r = hdev->vhost_ops->vhost_set_owner(hdev);
    if (r < 0) {
        error_setg_errno(errp, -r, "vhost_set_owner failed");
        goto fail;
    }

    r = hdev->vhost_ops->vhost_get_features(hdev, &features);
    if (r < 0) {
        error_setg_errno(errp, -r, "vhost_get_features failed");
        goto fail;
    }

    for (i = 0; i < hdev->nvqs; ++i, ++n_initialized_vqs) {
        r = vhost_virtqueue_init(hdev, hdev->vqs + i, hdev->vq_index + i);
        if (r < 0) {
            error_setg_errno(errp, -r, "Failed to initialize virtqueue %d", i);
            goto fail;
        }
    }

    if (busyloop_timeout) {
        for (i = 0; i < hdev->nvqs; ++i) {
            r = vhost_virtqueue_set_busyloop_timeout(hdev, hdev->vq_index + i,
                                                     busyloop_timeout);
            if (r < 0) {
                error_setg_errno(errp, -r, "Failed to set busyloop timeout");
                goto fail_busyloop;
            }
        }
    }

    hdev->features = features;

    hdev->memory_listener = (MemoryListener) {
        .name = "vhost",
        .begin = vhost_begin,
        .commit = vhost_commit,
        .region_add = vhost_region_addnop,
        .region_nop = vhost_region_addnop,
        .log_start = vhost_log_start,
        .log_stop = vhost_log_stop,
        .log_sync = vhost_log_sync,
        .log_global_start = vhost_log_global_start,
        .log_global_stop = vhost_log_global_stop,
        .priority = 10
    };

    hdev->iommu_listener = (MemoryListener) {
        .name = "vhost-iommu",
        .region_add = vhost_iommu_region_add,
        .region_del = vhost_iommu_region_del,
    };

    if (hdev->migration_blocker == NULL) {
        if (!(hdev->features & (0x1ULL << VHOST_F_LOG_ALL))) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: vhost lacks VHOST_F_LOG_ALL feature.");
        } else if (vhost_dev_log_is_shared(hdev) && !qemu_memfd_alloc_check()) {
            error_setg(&hdev->migration_blocker,
                       "Migration disabled: failed to allocate shared memory");
        }
    }

    if (hdev->migration_blocker != NULL) {
        r = migrate_add_blocker(hdev->migration_blocker, errp);
        if (r < 0) {
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
    memory_listener_register(&hdev->memory_listener, &address_space_memory);
    QLIST_INSERT_HEAD(&vhost_devices, hdev, entry);

    if (used_memslots > hdev->vhost_ops->vhost_backend_memslots_limit(hdev)) {
        error_setg(errp, "vhost backend memory slots limit is less"
                   " than current number of present memory slots");
        r = -EINVAL;
        goto fail_busyloop;
    }

    return 0;

fail_busyloop:
    if (busyloop_timeout) {
        while (--i >= 0) {
            vhost_virtqueue_set_busyloop_timeout(hdev, hdev->vq_index + i, 0);
        }
    }
fail:
    hdev->nvqs = n_initialized_vqs;
    vhost_dev_cleanup(hdev);
    return r;
}

void vhost_dev_cleanup(struct vhost_dev *hdev)
{
    int i;

    trace_vhost_dev_cleanup(hdev);

    for (i = 0; i < hdev->nvqs; ++i) {
        vhost_virtqueue_cleanup(hdev->vqs + i);
    }
    if (hdev->mem) {
        /* those are only safe after successful init */
        memory_listener_unregister(&hdev->memory_listener);
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
    int i, r;

    /* We will pass the notifiers to the kernel, make sure that QEMU
     * doesn't interfere.
     */
    r = virtio_device_grab_ioeventfd(vdev);
    if (r < 0) {
        error_report("binding does not support host notifiers");
        return r;
    }

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    for (i = 0; i < hdev->nvqs; ++i) {
        r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), hdev->vq_index + i,
                                         true);
        if (r < 0) {
            error_report("vhost VQ %d notifier binding failed: %d", i, -r);
            memory_region_transaction_commit();
            vhost_dev_disable_notifiers(hdev, vdev);
            return r;
        }
    }

    memory_region_transaction_commit();

    return 0;
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

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    for (i = 0; i < hdev->nvqs; ++i) {
        r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus), hdev->vq_index + i,
                                         false);
        if (r < 0) {
            error_report("vhost VQ %d notifier cleanup failed: %d", i, -r);
        }
        assert (r >= 0);
    }

    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    for (i = 0; i < hdev->nvqs; ++i) {
        virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus), hdev->vq_index + i);
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
        file.fd = event_notifier_get_wfd(&hdev->vqs[index].masked_notifier);
    } else {
        file.fd = event_notifier_get_wfd(virtio_queue_get_guest_notifier(vvq));
    }

    file.index = hdev->vhost_ops->vhost_get_vq_index(hdev, n);
    r = hdev->vhost_ops->vhost_set_vring_call(hdev, &file);
    if (r < 0) {
        error_report("vhost_set_vring_call failed %d", -r);
    }
}

bool vhost_config_pending(struct vhost_dev *hdev)
{
    assert(hdev->vhost_ops);
    if ((hdev->started == false) ||
        (hdev->vhost_ops->vhost_set_config_call == NULL)) {
        return false;
    }

    EventNotifier *notifier =
        &hdev->vqs[VHOST_QUEUE_NUM_CONFIG_INR].masked_config_notifier;
    return event_notifier_test_and_clear(notifier);
}

void vhost_config_mask(struct vhost_dev *hdev, VirtIODevice *vdev, bool mask)
{
    int fd;
    int r;
    EventNotifier *notifier =
        &hdev->vqs[VHOST_QUEUE_NUM_CONFIG_INR].masked_config_notifier;
    EventNotifier *config_notifier = &vdev->config_notifier;
    assert(hdev->vhost_ops);

    if ((hdev->started == false) ||
        (hdev->vhost_ops->vhost_set_config_call == NULL)) {
        return;
    }
    if (mask) {
        assert(vdev->use_guest_notifier_mask);
        fd = event_notifier_get_fd(notifier);
    } else {
        fd = event_notifier_get_fd(config_notifier);
    }
    r = hdev->vhost_ops->vhost_set_config_call(hdev, fd);
    if (r < 0) {
        error_report("vhost_set_config_call failed %d", -r);
    }
}

static void vhost_stop_config_intr(struct vhost_dev *dev)
{
    int fd = -1;
    assert(dev->vhost_ops);
    if (dev->vhost_ops->vhost_set_config_call) {
        dev->vhost_ops->vhost_set_config_call(dev, fd);
    }
}

static void vhost_start_config_intr(struct vhost_dev *dev)
{
    int r;

    assert(dev->vhost_ops);
    int fd = event_notifier_get_fd(&dev->vdev->config_notifier);
    if (dev->vhost_ops->vhost_set_config_call) {
        r = dev->vhost_ops->vhost_set_config_call(dev, fd);
        if (!r) {
            event_notifier_set(&dev->vdev->config_notifier);
        }
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

int vhost_dev_get_config(struct vhost_dev *hdev, uint8_t *config,
                         uint32_t config_len, Error **errp)
{
    assert(hdev->vhost_ops);

    if (hdev->vhost_ops->vhost_get_config) {
        return hdev->vhost_ops->vhost_get_config(hdev, config, config_len,
                                                 errp);
    }

    error_setg(errp, "vhost_get_config not implemented");
    return -ENOSYS;
}

int vhost_dev_set_config(struct vhost_dev *hdev, const uint8_t *data,
                         uint32_t offset, uint32_t size, uint32_t flags)
{
    assert(hdev->vhost_ops);

    if (hdev->vhost_ops->vhost_set_config) {
        return hdev->vhost_ops->vhost_set_config(hdev, data, offset,
                                                 size, flags);
    }

    return -ENOSYS;
}

void vhost_dev_set_config_notifier(struct vhost_dev *hdev,
                                   const VhostDevConfigOps *ops)
{
    hdev->config_ops = ops;
}

void vhost_dev_free_inflight(struct vhost_inflight *inflight)
{
    if (inflight && inflight->addr) {
        qemu_memfd_free(inflight->addr, inflight->size, inflight->fd);
        inflight->addr = NULL;
        inflight->fd = -1;
    }
}

static int vhost_dev_resize_inflight(struct vhost_inflight *inflight,
                                     uint64_t new_size)
{
    Error *err = NULL;
    int fd = -1;
    void *addr = qemu_memfd_alloc("vhost-inflight", new_size,
                                  F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL,
                                  &fd, &err);

    if (err) {
        error_report_err(err);
        return -ENOMEM;
    }

    vhost_dev_free_inflight(inflight);
    inflight->offset = 0;
    inflight->addr = addr;
    inflight->fd = fd;
    inflight->size = new_size;

    return 0;
}

void vhost_dev_save_inflight(struct vhost_inflight *inflight, QEMUFile *f)
{
    if (inflight->addr) {
        qemu_put_be64(f, inflight->size);
        qemu_put_be16(f, inflight->queue_size);
        qemu_put_buffer(f, inflight->addr, inflight->size);
    } else {
        qemu_put_be64(f, 0);
    }
}

int vhost_dev_load_inflight(struct vhost_inflight *inflight, QEMUFile *f)
{
    uint64_t size;

    size = qemu_get_be64(f);
    if (!size) {
        return 0;
    }

    if (inflight->size != size) {
        int ret = vhost_dev_resize_inflight(inflight, size);
        if (ret < 0) {
            return ret;
        }
    }
    inflight->queue_size = qemu_get_be16(f);

    qemu_get_buffer(f, inflight->addr, size);

    return 0;
}

int vhost_dev_prepare_inflight(struct vhost_dev *hdev, VirtIODevice *vdev)
{
    int r;

    if (hdev->vhost_ops->vhost_get_inflight_fd == NULL ||
        hdev->vhost_ops->vhost_set_inflight_fd == NULL) {
        return 0;
    }

    hdev->vdev = vdev;

    r = vhost_dev_set_features(hdev, hdev->log_enabled);
    if (r < 0) {
        VHOST_OPS_DEBUG(r, "vhost_dev_prepare_inflight failed");
        return r;
    }

    return 0;
}

int vhost_dev_set_inflight(struct vhost_dev *dev,
                           struct vhost_inflight *inflight)
{
    int r;

    if (dev->vhost_ops->vhost_set_inflight_fd && inflight->addr) {
        r = dev->vhost_ops->vhost_set_inflight_fd(dev, inflight);
        if (r) {
            VHOST_OPS_DEBUG(r, "vhost_set_inflight_fd failed");
            return r;
        }
    }

    return 0;
}

int vhost_dev_get_inflight(struct vhost_dev *dev, uint16_t queue_size,
                           struct vhost_inflight *inflight)
{
    int r;

    if (dev->vhost_ops->vhost_get_inflight_fd) {
        r = dev->vhost_ops->vhost_get_inflight_fd(dev, queue_size, inflight);
        if (r) {
            VHOST_OPS_DEBUG(r, "vhost_get_inflight_fd failed");
            return r;
        }
    }

    return 0;
}

static int vhost_dev_set_vring_enable(struct vhost_dev *hdev, int enable)
{
    if (!hdev->vhost_ops->vhost_set_vring_enable) {
        return 0;
    }

    /*
     * For vhost-user devices, if VHOST_USER_F_PROTOCOL_FEATURES has not
     * been negotiated, the rings start directly in the enabled state, and
     * .vhost_set_vring_enable callback will fail since
     * VHOST_USER_SET_VRING_ENABLE is not supported.
     */
    if (hdev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER &&
        !virtio_has_feature(hdev->backend_features,
                            VHOST_USER_F_PROTOCOL_FEATURES)) {
        return 0;
    }

    return hdev->vhost_ops->vhost_set_vring_enable(hdev, enable);
}

/* Host notifiers must be enabled at this point. */
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev, bool vrings)
{
    int i, r;

    /* should only be called after backend is connected */
    assert(hdev->vhost_ops);

    trace_vhost_dev_start(hdev, vdev->name, vrings);

    vdev->vhost_started = true;
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
        VHOST_OPS_DEBUG(r, "vhost_set_mem_table failed");
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

    r = event_notifier_init(
        &hdev->vqs[VHOST_QUEUE_NUM_CONFIG_INR].masked_config_notifier, 0);
    if (r < 0) {
        return r;
    }
    event_notifier_test_and_clear(
        &hdev->vqs[VHOST_QUEUE_NUM_CONFIG_INR].masked_config_notifier);
    if (!vdev->use_guest_notifier_mask) {
        vhost_config_mask(hdev, vdev, true);
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
            VHOST_OPS_DEBUG(r, "vhost_set_log_base failed");
            goto fail_log;
        }
    }
    if (vrings) {
        r = vhost_dev_set_vring_enable(hdev, true);
        if (r) {
            goto fail_log;
        }
    }
    if (hdev->vhost_ops->vhost_dev_start) {
        r = hdev->vhost_ops->vhost_dev_start(hdev, true);
        if (r) {
            goto fail_start;
        }
    }
    if (vhost_dev_has_iommu(hdev) &&
        hdev->vhost_ops->vhost_set_iotlb_callback) {
            hdev->vhost_ops->vhost_set_iotlb_callback(hdev, true);

        /* Update used ring information for IOTLB to work correctly,
         * vhost-kernel code requires for this.*/
        for (i = 0; i < hdev->nvqs; ++i) {
            struct vhost_virtqueue *vq = hdev->vqs + i;
            vhost_device_iotlb_miss(hdev, vq->used_phys, true);
        }
    }
    vhost_start_config_intr(hdev);
    return 0;
fail_start:
    if (vrings) {
        vhost_dev_set_vring_enable(hdev, false);
    }
fail_log:
    vhost_log_put(hdev, false);
fail_vq:
    while (--i >= 0) {
        vhost_virtqueue_stop(hdev,
                             vdev,
                             hdev->vqs + i,
                             hdev->vq_index + i);
    }

fail_mem:
fail_features:
    vdev->vhost_started = false;
    hdev->started = false;
    return r;
}

/* Host notifiers must be enabled at this point. */
void vhost_dev_stop(struct vhost_dev *hdev, VirtIODevice *vdev, bool vrings)
{
    int i;

    /* should only be called after backend is connected */
    assert(hdev->vhost_ops);
    event_notifier_test_and_clear(
        &hdev->vqs[VHOST_QUEUE_NUM_CONFIG_INR].masked_config_notifier);
    event_notifier_test_and_clear(&vdev->config_notifier);

    trace_vhost_dev_stop(hdev, vdev->name, vrings);

    if (hdev->vhost_ops->vhost_dev_start) {
        hdev->vhost_ops->vhost_dev_start(hdev, false);
    }
    if (vrings) {
        vhost_dev_set_vring_enable(hdev, false);
    }
    for (i = 0; i < hdev->nvqs; ++i) {
        vhost_virtqueue_stop(hdev,
                             vdev,
                             hdev->vqs + i,
                             hdev->vq_index + i);
    }
    if (hdev->vhost_ops->vhost_reset_status) {
        hdev->vhost_ops->vhost_reset_status(hdev);
    }

    if (vhost_dev_has_iommu(hdev)) {
        if (hdev->vhost_ops->vhost_set_iotlb_callback) {
            hdev->vhost_ops->vhost_set_iotlb_callback(hdev, false);
        }
        memory_listener_unregister(&hdev->iommu_listener);
    }
    vhost_stop_config_intr(hdev);
    vhost_log_put(hdev, true);
    hdev->started = false;
    vdev->vhost_started = false;
    hdev->vdev = NULL;
}

int vhost_net_set_backend(struct vhost_dev *hdev,
                          struct vhost_vring_file *file)
{
    if (hdev->vhost_ops->vhost_net_set_backend) {
        return hdev->vhost_ops->vhost_net_set_backend(hdev, file);
    }

    return -ENOSYS;
}
