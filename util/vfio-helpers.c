/*
 * VFIO utility
 *
 * Copyright 2016 - 2018 Red Hat, Inc.
 *
 * Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>
#include "qapi/error.h"
#include "exec/ramlist.h"
#include "exec/cpu-common.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "standard-headers/linux/pci_regs.h"
#include "qemu/event_notifier.h"
#include "qemu/vfio-helpers.h"
#include "trace.h"

#define QEMU_VFIO_DEBUG 0

#define QEMU_VFIO_IOVA_MIN 0x10000ULL
/* XXX: Once VFIO exposes the iova bit width in the IOMMU capability interface,
 * we can use a runtime limit; alternatively it's also possible to do platform
 * specific detection by reading sysfs entries. Until then, 39 is a safe bet.
 **/
#define QEMU_VFIO_IOVA_MAX (1ULL << 39)

typedef struct {
    /* Page aligned addr. */
    void *host;
    size_t size;
    uint64_t iova;
} IOVAMapping;

struct QEMUVFIOState {
    QemuMutex lock;

    /* These fields are protected by BQL */
    int container;
    int group;
    int device;
    RAMBlockNotifier ram_notifier;
    struct vfio_region_info config_region_info, bar_region_info[6];

    /* These fields are protected by @lock */
    /* VFIO's IO virtual address space is managed by splitting into a few
     * sections:
     *
     * ---------------       <= 0
     * |xxxxxxxxxxxxx|
     * |-------------|       <= QEMU_VFIO_IOVA_MIN
     * |             |
     * |    Fixed    |
     * |             |
     * |-------------|       <= low_water_mark
     * |             |
     * |    Free     |
     * |             |
     * |-------------|       <= high_water_mark
     * |             |
     * |    Temp     |
     * |             |
     * |-------------|       <= QEMU_VFIO_IOVA_MAX
     * |xxxxxxxxxxxxx|
     * |xxxxxxxxxxxxx|
     * ---------------
     *
     * - Addresses lower than QEMU_VFIO_IOVA_MIN are reserved as invalid;
     *
     * - Fixed mappings of HVAs are assigned "low" IOVAs in the range of
     *   [QEMU_VFIO_IOVA_MIN, low_water_mark).  Once allocated they will not be
     *   reclaimed - low_water_mark never shrinks;
     *
     * - IOVAs in range [low_water_mark, high_water_mark) are free;
     *
     * - IOVAs in range [high_water_mark, QEMU_VFIO_IOVA_MAX) are volatile
     *   mappings. At each qemu_vfio_dma_reset_temporary() call, the whole area
     *   is recycled. The caller should make sure I/O's depending on these
     *   mappings are completed before calling.
     **/
    uint64_t low_water_mark;
    uint64_t high_water_mark;
    IOVAMapping *mappings;
    int nr_mappings;
};

/**
 * Find group file by PCI device address as specified @device, and return the
 * path. The returned string is owned by caller and should be g_free'ed later.
 */
static char *sysfs_find_group_file(const char *device, Error **errp)
{
    char *sysfs_link;
    char *sysfs_group;
    char *p;
    char *path = NULL;

    sysfs_link = g_strdup_printf("/sys/bus/pci/devices/%s/iommu_group", device);
    sysfs_group = g_malloc0(PATH_MAX);
    if (readlink(sysfs_link, sysfs_group, PATH_MAX - 1) == -1) {
        error_setg_errno(errp, errno, "Failed to find iommu group sysfs path");
        goto out;
    }
    p = strrchr(sysfs_group, '/');
    if (!p) {
        error_setg(errp, "Failed to find iommu group number");
        goto out;
    }

    path = g_strdup_printf("/dev/vfio/%s", p + 1);
out:
    g_free(sysfs_link);
    g_free(sysfs_group);
    return path;
}

static inline void assert_bar_index_valid(QEMUVFIOState *s, int index)
{
    assert(index >= 0 && index < ARRAY_SIZE(s->bar_region_info));
}

static int qemu_vfio_pci_init_bar(QEMUVFIOState *s, int index, Error **errp)
{
    assert_bar_index_valid(s, index);
    s->bar_region_info[index] = (struct vfio_region_info) {
        .index = VFIO_PCI_BAR0_REGION_INDEX + index,
        .argsz = sizeof(struct vfio_region_info),
    };
    if (ioctl(s->device, VFIO_DEVICE_GET_REGION_INFO, &s->bar_region_info[index])) {
        error_setg_errno(errp, errno, "Failed to get BAR region info");
        return -errno;
    }

    return 0;
}

/**
 * Map a PCI bar area.
 */
void *qemu_vfio_pci_map_bar(QEMUVFIOState *s, int index,
                            uint64_t offset, uint64_t size,
                            Error **errp)
{
    void *p;
    assert_bar_index_valid(s, index);
    p = mmap(NULL, MIN(size, s->bar_region_info[index].size - offset),
             PROT_READ | PROT_WRITE, MAP_SHARED,
             s->device, s->bar_region_info[index].offset + offset);
    if (p == MAP_FAILED) {
        error_setg_errno(errp, errno, "Failed to map BAR region");
        p = NULL;
    }
    return p;
}

/**
 * Unmap a PCI bar area.
 */
void qemu_vfio_pci_unmap_bar(QEMUVFIOState *s, int index, void *bar,
                             uint64_t offset, uint64_t size)
{
    if (bar) {
        munmap(bar, MIN(size, s->bar_region_info[index].size - offset));
    }
}

/**
 * Initialize device IRQ with @irq_type and and register an event notifier.
 */
int qemu_vfio_pci_init_irq(QEMUVFIOState *s, EventNotifier *e,
                           int irq_type, Error **errp)
{
    int r;
    struct vfio_irq_set *irq_set;
    size_t irq_set_size;
    struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info) };

    irq_info.index = irq_type;
    if (ioctl(s->device, VFIO_DEVICE_GET_IRQ_INFO, &irq_info)) {
        error_setg_errno(errp, errno, "Failed to get device interrupt info");
        return -errno;
    }
    if (!(irq_info.flags & VFIO_IRQ_INFO_EVENTFD)) {
        error_setg(errp, "Device interrupt doesn't support eventfd");
        return -EINVAL;
    }

    irq_set_size = sizeof(*irq_set) + sizeof(int);
    irq_set = g_malloc0(irq_set_size);

    /* Get to a known IRQ state */
    *irq_set = (struct vfio_irq_set) {
        .argsz = irq_set_size,
        .flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = irq_info.index,
        .start = 0,
        .count = 1,
    };

    *(int *)&irq_set->data = event_notifier_get_fd(e);
    r = ioctl(s->device, VFIO_DEVICE_SET_IRQS, irq_set);
    g_free(irq_set);
    if (r) {
        error_setg_errno(errp, errno, "Failed to setup device interrupt");
        return -errno;
    }
    return 0;
}

static int qemu_vfio_pci_read_config(QEMUVFIOState *s, void *buf,
                                     int size, int ofs)
{
    int ret;

    do {
        ret = pread(s->device, buf, size, s->config_region_info.offset + ofs);
    } while (ret == -1 && errno == EINTR);
    return ret == size ? 0 : -errno;
}

static int qemu_vfio_pci_write_config(QEMUVFIOState *s, void *buf, int size, int ofs)
{
    int ret;

    do {
        ret = pwrite(s->device, buf, size, s->config_region_info.offset + ofs);
    } while (ret == -1 && errno == EINTR);
    return ret == size ? 0 : -errno;
}

static int qemu_vfio_init_pci(QEMUVFIOState *s, const char *device,
                              Error **errp)
{
    int ret;
    int i;
    uint16_t pci_cmd;
    struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
    struct vfio_iommu_type1_info iommu_info = { .argsz = sizeof(iommu_info) };
    struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
    char *group_file = NULL;

    /* Create a new container */
    s->container = open("/dev/vfio/vfio", O_RDWR);

    if (s->container == -1) {
        error_setg_errno(errp, errno, "Failed to open /dev/vfio/vfio");
        return -errno;
    }
    if (ioctl(s->container, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
        error_setg(errp, "Invalid VFIO version");
        ret = -EINVAL;
        goto fail_container;
    }

    if (!ioctl(s->container, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU)) {
        error_setg_errno(errp, errno, "VFIO IOMMU check failed");
        ret = -EINVAL;
        goto fail_container;
    }

    /* Open the group */
    group_file = sysfs_find_group_file(device, errp);
    if (!group_file) {
        ret = -EINVAL;
        goto fail_container;
    }

    s->group = open(group_file, O_RDWR);
    if (s->group == -1) {
        error_setg_errno(errp, errno, "Failed to open VFIO group file: %s",
                         group_file);
        g_free(group_file);
        ret = -errno;
        goto fail_container;
    }
    g_free(group_file);

    /* Test the group is viable and available */
    if (ioctl(s->group, VFIO_GROUP_GET_STATUS, &group_status)) {
        error_setg_errno(errp, errno, "Failed to get VFIO group status");
        ret = -errno;
        goto fail;
    }

    if (!(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        error_setg(errp, "VFIO group is not viable");
        ret = -EINVAL;
        goto fail;
    }

    /* Add the group to the container */
    if (ioctl(s->group, VFIO_GROUP_SET_CONTAINER, &s->container)) {
        error_setg_errno(errp, errno, "Failed to add group to VFIO container");
        ret = -errno;
        goto fail;
    }

    /* Enable the IOMMU model we want */
    if (ioctl(s->container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU)) {
        error_setg_errno(errp, errno, "Failed to set VFIO IOMMU type");
        ret = -errno;
        goto fail;
    }

    /* Get additional IOMMU info */
    if (ioctl(s->container, VFIO_IOMMU_GET_INFO, &iommu_info)) {
        error_setg_errno(errp, errno, "Failed to get IOMMU info");
        ret = -errno;
        goto fail;
    }

    s->device = ioctl(s->group, VFIO_GROUP_GET_DEVICE_FD, device);

    if (s->device < 0) {
        error_setg_errno(errp, errno, "Failed to get device fd");
        ret = -errno;
        goto fail;
    }

    /* Test and setup the device */
    if (ioctl(s->device, VFIO_DEVICE_GET_INFO, &device_info)) {
        error_setg_errno(errp, errno, "Failed to get device info");
        ret = -errno;
        goto fail;
    }

    if (device_info.num_regions < VFIO_PCI_CONFIG_REGION_INDEX) {
        error_setg(errp, "Invalid device regions");
        ret = -EINVAL;
        goto fail;
    }

    s->config_region_info = (struct vfio_region_info) {
        .index = VFIO_PCI_CONFIG_REGION_INDEX,
        .argsz = sizeof(struct vfio_region_info),
    };
    if (ioctl(s->device, VFIO_DEVICE_GET_REGION_INFO, &s->config_region_info)) {
        error_setg_errno(errp, errno, "Failed to get config region info");
        ret = -errno;
        goto fail;
    }

    for (i = 0; i < ARRAY_SIZE(s->bar_region_info); i++) {
        ret = qemu_vfio_pci_init_bar(s, i, errp);
        if (ret) {
            goto fail;
        }
    }

    /* Enable bus master */
    ret = qemu_vfio_pci_read_config(s, &pci_cmd, sizeof(pci_cmd), PCI_COMMAND);
    if (ret) {
        goto fail;
    }
    pci_cmd |= PCI_COMMAND_MASTER;
    ret = qemu_vfio_pci_write_config(s, &pci_cmd, sizeof(pci_cmd), PCI_COMMAND);
    if (ret) {
        goto fail;
    }
    return 0;
fail:
    close(s->group);
fail_container:
    close(s->container);
    return ret;
}

static void qemu_vfio_ram_block_added(RAMBlockNotifier *n,
                                      void *host, size_t size)
{
    QEMUVFIOState *s = container_of(n, QEMUVFIOState, ram_notifier);
    trace_qemu_vfio_ram_block_added(s, host, size);
    qemu_vfio_dma_map(s, host, size, false, NULL);
}

static void qemu_vfio_ram_block_removed(RAMBlockNotifier *n,
                                        void *host, size_t size)
{
    QEMUVFIOState *s = container_of(n, QEMUVFIOState, ram_notifier);
    if (host) {
        trace_qemu_vfio_ram_block_removed(s, host, size);
        qemu_vfio_dma_unmap(s, host);
    }
}

static int qemu_vfio_init_ramblock(RAMBlock *rb, void *opaque)
{
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t length = qemu_ram_get_used_length(rb);
    int ret;
    QEMUVFIOState *s = opaque;

    if (!host_addr) {
        return 0;
    }
    ret = qemu_vfio_dma_map(s, host_addr, length, false, NULL);
    if (ret) {
        fprintf(stderr, "qemu_vfio_init_ramblock: failed %p %" PRId64 "\n",
                host_addr, (uint64_t)length);
    }
    return 0;
}

static void qemu_vfio_open_common(QEMUVFIOState *s)
{
    qemu_mutex_init(&s->lock);
    s->ram_notifier.ram_block_added = qemu_vfio_ram_block_added;
    s->ram_notifier.ram_block_removed = qemu_vfio_ram_block_removed;
    ram_block_notifier_add(&s->ram_notifier);
    s->low_water_mark = QEMU_VFIO_IOVA_MIN;
    s->high_water_mark = QEMU_VFIO_IOVA_MAX;
    qemu_ram_foreach_block(qemu_vfio_init_ramblock, s);
}

/**
 * Open a PCI device, e.g. "0000:00:01.0".
 */
QEMUVFIOState *qemu_vfio_open_pci(const char *device, Error **errp)
{
    int r;
    QEMUVFIOState *s = g_new0(QEMUVFIOState, 1);

    r = qemu_vfio_init_pci(s, device, errp);
    if (r) {
        g_free(s);
        return NULL;
    }
    qemu_vfio_open_common(s);
    return s;
}

static void qemu_vfio_dump_mapping(IOVAMapping *m)
{
    if (QEMU_VFIO_DEBUG) {
        printf("  vfio mapping %p %" PRIx64 " to %" PRIx64 "\n", m->host,
               (uint64_t)m->size, (uint64_t)m->iova);
    }
}

static void qemu_vfio_dump_mappings(QEMUVFIOState *s)
{
    int i;

    if (QEMU_VFIO_DEBUG) {
        printf("vfio mappings\n");
        for (i = 0; i < s->nr_mappings; ++i) {
            qemu_vfio_dump_mapping(&s->mappings[i]);
        }
    }
}

/**
 * Find the mapping entry that contains [host, host + size) and set @index to
 * the position. If no entry contains it, @index is the position _after_ which
 * to insert the new mapping. IOW, it is the index of the largest element that
 * is smaller than @host, or -1 if no entry is.
 */
static IOVAMapping *qemu_vfio_find_mapping(QEMUVFIOState *s, void *host,
                                           int *index)
{
    IOVAMapping *p = s->mappings;
    IOVAMapping *q = p ? p + s->nr_mappings - 1 : NULL;
    IOVAMapping *mid;
    trace_qemu_vfio_find_mapping(s, host);
    if (!p) {
        *index = -1;
        return NULL;
    }
    while (true) {
        mid = p + (q - p) / 2;
        if (mid == p) {
            break;
        }
        if (mid->host > host) {
            q = mid;
        } else if (mid->host < host) {
            p = mid;
        } else {
            break;
        }
    }
    if (mid->host > host) {
        mid--;
    } else if (mid < &s->mappings[s->nr_mappings - 1]
               && (mid + 1)->host <= host) {
        mid++;
    }
    *index = mid - &s->mappings[0];
    if (mid >= &s->mappings[0] &&
        mid->host <= host && mid->host + mid->size > host) {
        assert(mid < &s->mappings[s->nr_mappings]);
        return mid;
    }
    /* At this point *index + 1 is the right position to insert the new
     * mapping.*/
    return NULL;
}

/**
 * Allocate IOVA and and create a new mapping record and insert it in @s.
 */
static IOVAMapping *qemu_vfio_add_mapping(QEMUVFIOState *s,
                                          void *host, size_t size,
                                          int index, uint64_t iova)
{
    int shift;
    IOVAMapping m = {.host = host, .size = size, .iova = iova};
    IOVAMapping *insert;

    assert(QEMU_IS_ALIGNED(size, qemu_real_host_page_size));
    assert(QEMU_IS_ALIGNED(s->low_water_mark, qemu_real_host_page_size));
    assert(QEMU_IS_ALIGNED(s->high_water_mark, qemu_real_host_page_size));
    trace_qemu_vfio_new_mapping(s, host, size, index, iova);

    assert(index >= 0);
    s->nr_mappings++;
    s->mappings = g_renew(IOVAMapping, s->mappings, s->nr_mappings);
    insert = &s->mappings[index];
    shift = s->nr_mappings - index - 1;
    if (shift) {
        memmove(insert + 1, insert, shift * sizeof(s->mappings[0]));
    }
    *insert = m;
    return insert;
}

/* Do the DMA mapping with VFIO. */
static int qemu_vfio_do_mapping(QEMUVFIOState *s, void *host, size_t size,
                                uint64_t iova)
{
    struct vfio_iommu_type1_dma_map dma_map = {
        .argsz = sizeof(dma_map),
        .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
        .iova = iova,
        .vaddr = (uintptr_t)host,
        .size = size,
    };
    trace_qemu_vfio_do_mapping(s, host, size, iova);

    if (ioctl(s->container, VFIO_IOMMU_MAP_DMA, &dma_map)) {
        error_report("VFIO_MAP_DMA failed: %s", strerror(errno));
        return -errno;
    }
    return 0;
}

/**
 * Undo the DMA mapping from @s with VFIO, and remove from mapping list.
 */
static void qemu_vfio_undo_mapping(QEMUVFIOState *s, IOVAMapping *mapping,
                                   Error **errp)
{
    int index;
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = mapping->iova,
        .size = mapping->size,
    };

    index = mapping - s->mappings;
    assert(mapping->size > 0);
    assert(QEMU_IS_ALIGNED(mapping->size, qemu_real_host_page_size));
    assert(index >= 0 && index < s->nr_mappings);
    if (ioctl(s->container, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        error_setg_errno(errp, errno, "VFIO_UNMAP_DMA failed");
    }
    memmove(mapping, &s->mappings[index + 1],
            sizeof(s->mappings[0]) * (s->nr_mappings - index - 1));
    s->nr_mappings--;
    s->mappings = g_renew(IOVAMapping, s->mappings, s->nr_mappings);
}

/* Check if the mapping list is (ascending) ordered. */
static bool qemu_vfio_verify_mappings(QEMUVFIOState *s)
{
    int i;
    if (QEMU_VFIO_DEBUG) {
        for (i = 0; i < s->nr_mappings - 1; ++i) {
            if (!(s->mappings[i].host < s->mappings[i + 1].host)) {
                fprintf(stderr, "item %d not sorted!\n", i);
                qemu_vfio_dump_mappings(s);
                return false;
            }
            if (!(s->mappings[i].host + s->mappings[i].size <=
                  s->mappings[i + 1].host)) {
                fprintf(stderr, "item %d overlap with next!\n", i);
                qemu_vfio_dump_mappings(s);
                return false;
            }
        }
    }
    return true;
}

/* Map [host, host + size) area into a contiguous IOVA address space, and store
 * the result in @iova if not NULL. The caller need to make sure the area is
 * aligned to page size, and mustn't overlap with existing mapping areas (split
 * mapping status within this area is not allowed).
 */
int qemu_vfio_dma_map(QEMUVFIOState *s, void *host, size_t size,
                      bool temporary, uint64_t *iova)
{
    int ret = 0;
    int index;
    IOVAMapping *mapping;
    uint64_t iova0;

    assert(QEMU_PTR_IS_ALIGNED(host, qemu_real_host_page_size));
    assert(QEMU_IS_ALIGNED(size, qemu_real_host_page_size));
    trace_qemu_vfio_dma_map(s, host, size, temporary, iova);
    qemu_mutex_lock(&s->lock);
    mapping = qemu_vfio_find_mapping(s, host, &index);
    if (mapping) {
        iova0 = mapping->iova + ((uint8_t *)host - (uint8_t *)mapping->host);
    } else {
        if (s->high_water_mark - s->low_water_mark + 1 < size) {
            ret = -ENOMEM;
            goto out;
        }
        if (!temporary) {
            iova0 = s->low_water_mark;
            mapping = qemu_vfio_add_mapping(s, host, size, index + 1, iova0);
            if (!mapping) {
                ret = -ENOMEM;
                goto out;
            }
            assert(qemu_vfio_verify_mappings(s));
            ret = qemu_vfio_do_mapping(s, host, size, iova0);
            if (ret) {
                qemu_vfio_undo_mapping(s, mapping, NULL);
                goto out;
            }
            s->low_water_mark += size;
            qemu_vfio_dump_mappings(s);
        } else {
            iova0 = s->high_water_mark - size;
            ret = qemu_vfio_do_mapping(s, host, size, iova0);
            if (ret) {
                goto out;
            }
            s->high_water_mark -= size;
        }
    }
    if (iova) {
        *iova = iova0;
    }
out:
    qemu_mutex_unlock(&s->lock);
    return ret;
}

/* Reset the high watermark and free all "temporary" mappings. */
int qemu_vfio_dma_reset_temporary(QEMUVFIOState *s)
{
    struct vfio_iommu_type1_dma_unmap unmap = {
        .argsz = sizeof(unmap),
        .flags = 0,
        .iova = s->high_water_mark,
        .size = QEMU_VFIO_IOVA_MAX - s->high_water_mark,
    };
    trace_qemu_vfio_dma_reset_temporary(s);
    qemu_mutex_lock(&s->lock);
    if (ioctl(s->container, VFIO_IOMMU_UNMAP_DMA, &unmap)) {
        error_report("VFIO_UNMAP_DMA failed: %s", strerror(errno));
        qemu_mutex_unlock(&s->lock);
        return -errno;
    }
    s->high_water_mark = QEMU_VFIO_IOVA_MAX;
    qemu_mutex_unlock(&s->lock);
    return 0;
}

/* Unmapping the whole area that was previously mapped with
 * qemu_vfio_dma_map(). */
void qemu_vfio_dma_unmap(QEMUVFIOState *s, void *host)
{
    int index = 0;
    IOVAMapping *m;

    if (!host) {
        return;
    }

    trace_qemu_vfio_dma_unmap(s, host);
    qemu_mutex_lock(&s->lock);
    m = qemu_vfio_find_mapping(s, host, &index);
    if (!m) {
        goto out;
    }
    qemu_vfio_undo_mapping(s, m, NULL);
out:
    qemu_mutex_unlock(&s->lock);
}

static void qemu_vfio_reset(QEMUVFIOState *s)
{
    ioctl(s->device, VFIO_DEVICE_RESET);
}

/* Close and free the VFIO resources. */
void qemu_vfio_close(QEMUVFIOState *s)
{
    int i;

    if (!s) {
        return;
    }
    for (i = 0; i < s->nr_mappings; ++i) {
        qemu_vfio_undo_mapping(s, &s->mappings[i], NULL);
    }
    ram_block_notifier_remove(&s->ram_notifier);
    qemu_vfio_reset(s);
    close(s->device);
    close(s->group);
    close(s->container);
}
