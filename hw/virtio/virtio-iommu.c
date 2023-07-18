/*
 * virtio-iommu device
 *
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/iov.h"
#include "exec/target_page.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "sysemu/kvm.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "trace.h"

#include "standard-headers/linux/virtio_ids.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci.h"

/* Max size */
#define VIOMMU_DEFAULT_QUEUE_SIZE 256
#define VIOMMU_PROBE_SIZE 512

typedef struct VirtIOIOMMUDomain {
    uint32_t id;
    bool bypass;
    GTree *mappings;
    QLIST_HEAD(, VirtIOIOMMUEndpoint) endpoint_list;
} VirtIOIOMMUDomain;

typedef struct VirtIOIOMMUEndpoint {
    uint32_t id;
    VirtIOIOMMUDomain *domain;
    IOMMUMemoryRegion *iommu_mr;
    QLIST_ENTRY(VirtIOIOMMUEndpoint) next;
} VirtIOIOMMUEndpoint;

typedef struct VirtIOIOMMUInterval {
    uint64_t low;
    uint64_t high;
} VirtIOIOMMUInterval;

typedef struct VirtIOIOMMUMapping {
    uint64_t phys_addr;
    uint32_t flags;
} VirtIOIOMMUMapping;

static inline uint16_t virtio_iommu_get_bdf(IOMMUDevice *dev)
{
    return PCI_BUILD_BDF(pci_bus_num(dev->bus), dev->devfn);
}

static bool virtio_iommu_device_bypassed(IOMMUDevice *sdev)
{
    uint32_t sid;
    bool bypassed;
    VirtIOIOMMU *s = sdev->viommu;
    VirtIOIOMMUEndpoint *ep;

    sid = virtio_iommu_get_bdf(sdev);

    qemu_rec_mutex_lock(&s->mutex);
    /* need to check bypass before system reset */
    if (!s->endpoints) {
        bypassed = s->config.bypass;
        goto unlock;
    }

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(sid));
    if (!ep || !ep->domain) {
        bypassed = s->config.bypass;
    } else {
        bypassed = ep->domain->bypass;
    }

unlock:
    qemu_rec_mutex_unlock(&s->mutex);
    return bypassed;
}

/* Return whether the device is using IOMMU translation. */
static bool virtio_iommu_switch_address_space(IOMMUDevice *sdev)
{
    bool use_remapping;

    assert(sdev);

    use_remapping = !virtio_iommu_device_bypassed(sdev);

    trace_virtio_iommu_switch_address_space(pci_bus_num(sdev->bus),
                                            PCI_SLOT(sdev->devfn),
                                            PCI_FUNC(sdev->devfn),
                                            use_remapping);

    /* Turn off first then on the other */
    if (use_remapping) {
        memory_region_set_enabled(&sdev->bypass_mr, false);
        memory_region_set_enabled(MEMORY_REGION(&sdev->iommu_mr), true);
    } else {
        memory_region_set_enabled(MEMORY_REGION(&sdev->iommu_mr), false);
        memory_region_set_enabled(&sdev->bypass_mr, true);
    }

    return use_remapping;
}

static void virtio_iommu_switch_address_space_all(VirtIOIOMMU *s)
{
    GHashTableIter iter;
    IOMMUPciBus *iommu_pci_bus;
    int i;

    g_hash_table_iter_init(&iter, s->as_by_busptr);
    while (g_hash_table_iter_next(&iter, NULL, (void **)&iommu_pci_bus)) {
        for (i = 0; i < PCI_DEVFN_MAX; i++) {
            if (!iommu_pci_bus->pbdev[i]) {
                continue;
            }
            virtio_iommu_switch_address_space(iommu_pci_bus->pbdev[i]);
        }
    }
}

/**
 * The bus number is used for lookup when SID based operations occur.
 * In that case we lazily populate the IOMMUPciBus array from the bus hash
 * table. At the time the IOMMUPciBus is created (iommu_find_add_as), the bus
 * numbers may not be always initialized yet.
 */
static IOMMUPciBus *iommu_find_iommu_pcibus(VirtIOIOMMU *s, uint8_t bus_num)
{
    IOMMUPciBus *iommu_pci_bus = s->iommu_pcibus_by_bus_num[bus_num];

    if (!iommu_pci_bus) {
        GHashTableIter iter;

        g_hash_table_iter_init(&iter, s->as_by_busptr);
        while (g_hash_table_iter_next(&iter, NULL, (void **)&iommu_pci_bus)) {
            if (pci_bus_num(iommu_pci_bus->bus) == bus_num) {
                s->iommu_pcibus_by_bus_num[bus_num] = iommu_pci_bus;
                return iommu_pci_bus;
            }
        }
        return NULL;
    }
    return iommu_pci_bus;
}

static IOMMUMemoryRegion *virtio_iommu_mr(VirtIOIOMMU *s, uint32_t sid)
{
    uint8_t bus_n, devfn;
    IOMMUPciBus *iommu_pci_bus;
    IOMMUDevice *dev;

    bus_n = PCI_BUS_NUM(sid);
    iommu_pci_bus = iommu_find_iommu_pcibus(s, bus_n);
    if (iommu_pci_bus) {
        devfn = sid & (PCI_DEVFN_MAX - 1);
        dev = iommu_pci_bus->pbdev[devfn];
        if (dev) {
            return &dev->iommu_mr;
        }
    }
    return NULL;
}

static gint interval_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
    VirtIOIOMMUInterval *inta = (VirtIOIOMMUInterval *)a;
    VirtIOIOMMUInterval *intb = (VirtIOIOMMUInterval *)b;

    if (inta->high < intb->low) {
        return -1;
    } else if (intb->high < inta->low) {
        return 1;
    } else {
        return 0;
    }
}

static void virtio_iommu_notify_map_unmap(IOMMUMemoryRegion *mr,
                                          IOMMUTLBEvent *event,
                                          hwaddr virt_start, hwaddr virt_end)
{
    uint64_t delta = virt_end - virt_start;

    event->entry.iova = virt_start;
    event->entry.addr_mask = delta;

    if (delta == UINT64_MAX) {
        memory_region_notify_iommu(mr, 0, *event);
    }

    while (virt_start != virt_end + 1) {
        uint64_t mask = dma_aligned_pow2_mask(virt_start, virt_end, 64);

        event->entry.addr_mask = mask;
        event->entry.iova = virt_start;
        memory_region_notify_iommu(mr, 0, *event);
        virt_start += mask + 1;
        if (event->entry.perm != IOMMU_NONE) {
            event->entry.translated_addr += mask + 1;
        }
    }
}

static void virtio_iommu_notify_map(IOMMUMemoryRegion *mr, hwaddr virt_start,
                                    hwaddr virt_end, hwaddr paddr,
                                    uint32_t flags)
{
    IOMMUTLBEvent event;
    IOMMUAccessFlags perm = IOMMU_ACCESS_FLAG(flags & VIRTIO_IOMMU_MAP_F_READ,
                                              flags & VIRTIO_IOMMU_MAP_F_WRITE);

    if (!(mr->iommu_notify_flags & IOMMU_NOTIFIER_MAP) ||
        (flags & VIRTIO_IOMMU_MAP_F_MMIO) || !perm) {
        return;
    }

    trace_virtio_iommu_notify_map(mr->parent_obj.name, virt_start, virt_end,
                                  paddr, perm);

    event.type = IOMMU_NOTIFIER_MAP;
    event.entry.target_as = &address_space_memory;
    event.entry.perm = perm;
    event.entry.translated_addr = paddr;

    virtio_iommu_notify_map_unmap(mr, &event, virt_start, virt_end);
}

static void virtio_iommu_notify_unmap(IOMMUMemoryRegion *mr, hwaddr virt_start,
                                      hwaddr virt_end)
{
    IOMMUTLBEvent event;

    if (!(mr->iommu_notify_flags & IOMMU_NOTIFIER_UNMAP)) {
        return;
    }

    trace_virtio_iommu_notify_unmap(mr->parent_obj.name, virt_start, virt_end);

    event.type = IOMMU_NOTIFIER_UNMAP;
    event.entry.target_as = &address_space_memory;
    event.entry.perm = IOMMU_NONE;
    event.entry.translated_addr = 0;

    virtio_iommu_notify_map_unmap(mr, &event, virt_start, virt_end);
}

static gboolean virtio_iommu_notify_unmap_cb(gpointer key, gpointer value,
                                             gpointer data)
{
    VirtIOIOMMUInterval *interval = (VirtIOIOMMUInterval *) key;
    IOMMUMemoryRegion *mr = (IOMMUMemoryRegion *) data;

    virtio_iommu_notify_unmap(mr, interval->low, interval->high);

    return false;
}

static gboolean virtio_iommu_notify_map_cb(gpointer key, gpointer value,
                                           gpointer data)
{
    VirtIOIOMMUMapping *mapping = (VirtIOIOMMUMapping *) value;
    VirtIOIOMMUInterval *interval = (VirtIOIOMMUInterval *) key;
    IOMMUMemoryRegion *mr = (IOMMUMemoryRegion *) data;

    virtio_iommu_notify_map(mr, interval->low, interval->high,
                            mapping->phys_addr, mapping->flags);

    return false;
}

static void virtio_iommu_detach_endpoint_from_domain(VirtIOIOMMUEndpoint *ep)
{
    VirtIOIOMMUDomain *domain = ep->domain;
    IOMMUDevice *sdev = container_of(ep->iommu_mr, IOMMUDevice, iommu_mr);

    if (!ep->domain) {
        return;
    }
    g_tree_foreach(domain->mappings, virtio_iommu_notify_unmap_cb,
                   ep->iommu_mr);
    QLIST_REMOVE(ep, next);
    ep->domain = NULL;
    virtio_iommu_switch_address_space(sdev);
}

static VirtIOIOMMUEndpoint *virtio_iommu_get_endpoint(VirtIOIOMMU *s,
                                                      uint32_t ep_id)
{
    VirtIOIOMMUEndpoint *ep;
    IOMMUMemoryRegion *mr;

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(ep_id));
    if (ep) {
        return ep;
    }
    mr = virtio_iommu_mr(s, ep_id);
    if (!mr) {
        return NULL;
    }
    ep = g_malloc0(sizeof(*ep));
    ep->id = ep_id;
    ep->iommu_mr = mr;
    trace_virtio_iommu_get_endpoint(ep_id);
    g_tree_insert(s->endpoints, GUINT_TO_POINTER(ep_id), ep);
    return ep;
}

static void virtio_iommu_put_endpoint(gpointer data)
{
    VirtIOIOMMUEndpoint *ep = (VirtIOIOMMUEndpoint *)data;

    if (ep->domain) {
        virtio_iommu_detach_endpoint_from_domain(ep);
    }

    trace_virtio_iommu_put_endpoint(ep->id);
    g_free(ep);
}

static VirtIOIOMMUDomain *virtio_iommu_get_domain(VirtIOIOMMU *s,
                                                  uint32_t domain_id,
                                                  bool bypass)
{
    VirtIOIOMMUDomain *domain;

    domain = g_tree_lookup(s->domains, GUINT_TO_POINTER(domain_id));
    if (domain) {
        if (domain->bypass != bypass) {
            return NULL;
        }
        return domain;
    }
    domain = g_malloc0(sizeof(*domain));
    domain->id = domain_id;
    domain->mappings = g_tree_new_full((GCompareDataFunc)interval_cmp,
                                   NULL, (GDestroyNotify)g_free,
                                   (GDestroyNotify)g_free);
    domain->bypass = bypass;
    g_tree_insert(s->domains, GUINT_TO_POINTER(domain_id), domain);
    QLIST_INIT(&domain->endpoint_list);
    trace_virtio_iommu_get_domain(domain_id);
    return domain;
}

static void virtio_iommu_put_domain(gpointer data)
{
    VirtIOIOMMUDomain *domain = (VirtIOIOMMUDomain *)data;
    VirtIOIOMMUEndpoint *iter, *tmp;

    QLIST_FOREACH_SAFE(iter, &domain->endpoint_list, next, tmp) {
        virtio_iommu_detach_endpoint_from_domain(iter);
    }
    g_tree_destroy(domain->mappings);
    trace_virtio_iommu_put_domain(domain->id);
    g_free(domain);
}

static AddressSpace *virtio_iommu_find_add_as(PCIBus *bus, void *opaque,
                                              int devfn)
{
    VirtIOIOMMU *s = opaque;
    IOMMUPciBus *sbus = g_hash_table_lookup(s->as_by_busptr, bus);
    static uint32_t mr_index;
    IOMMUDevice *sdev;

    if (!sbus) {
        sbus = g_malloc0(sizeof(IOMMUPciBus) +
                         sizeof(IOMMUDevice *) * PCI_DEVFN_MAX);
        sbus->bus = bus;
        g_hash_table_insert(s->as_by_busptr, bus, sbus);
    }

    sdev = sbus->pbdev[devfn];
    if (!sdev) {
        char *name = g_strdup_printf("%s-%d-%d",
                                     TYPE_VIRTIO_IOMMU_MEMORY_REGION,
                                     mr_index++, devfn);
        sdev = sbus->pbdev[devfn] = g_new0(IOMMUDevice, 1);

        sdev->viommu = s;
        sdev->bus = bus;
        sdev->devfn = devfn;

        trace_virtio_iommu_init_iommu_mr(name);

        memory_region_init(&sdev->root, OBJECT(s), name, UINT64_MAX);
        address_space_init(&sdev->as, &sdev->root, TYPE_VIRTIO_IOMMU);

        /*
         * Build the IOMMU disabled container with aliases to the
         * shared MRs.  Note that aliasing to a shared memory region
         * could help the memory API to detect same FlatViews so we
         * can have devices to share the same FlatView when in bypass
         * mode. (either by not configuring virtio-iommu driver or with
         * "iommu=pt").  It will greatly reduce the total number of
         * FlatViews of the system hence VM runs faster.
         */
        memory_region_init_alias(&sdev->bypass_mr, OBJECT(s),
                                 "system", get_system_memory(), 0,
                                 memory_region_size(get_system_memory()));

        memory_region_init_iommu(&sdev->iommu_mr, sizeof(sdev->iommu_mr),
                                 TYPE_VIRTIO_IOMMU_MEMORY_REGION,
                                 OBJECT(s), name,
                                 UINT64_MAX);

        /*
         * Hook both the containers under the root container, we
         * switch between iommu & bypass MRs by enable/disable
         * corresponding sub-containers
         */
        memory_region_add_subregion_overlap(&sdev->root, 0,
                                            MEMORY_REGION(&sdev->iommu_mr),
                                            0);
        memory_region_add_subregion_overlap(&sdev->root, 0,
                                            &sdev->bypass_mr, 0);

        virtio_iommu_switch_address_space(sdev);
        g_free(name);
    }
    return &sdev->as;
}

static int virtio_iommu_attach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_attach *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint32_t ep_id = le32_to_cpu(req->endpoint);
    uint32_t flags = le32_to_cpu(req->flags);
    VirtIOIOMMUDomain *domain;
    VirtIOIOMMUEndpoint *ep;
    IOMMUDevice *sdev;

    trace_virtio_iommu_attach(domain_id, ep_id);

    if (flags & ~VIRTIO_IOMMU_ATTACH_F_BYPASS) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    ep = virtio_iommu_get_endpoint(s, ep_id);
    if (!ep) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    if (ep->domain) {
        VirtIOIOMMUDomain *previous_domain = ep->domain;
        /*
         * the device is already attached to a domain,
         * detach it first
         */
        virtio_iommu_detach_endpoint_from_domain(ep);
        if (QLIST_EMPTY(&previous_domain->endpoint_list)) {
            g_tree_remove(s->domains, GUINT_TO_POINTER(previous_domain->id));
        }
    }

    domain = virtio_iommu_get_domain(s, domain_id,
                                     flags & VIRTIO_IOMMU_ATTACH_F_BYPASS);
    if (!domain) {
        /* Incompatible bypass flag */
        return VIRTIO_IOMMU_S_INVAL;
    }
    QLIST_INSERT_HEAD(&domain->endpoint_list, ep, next);

    ep->domain = domain;
    sdev = container_of(ep->iommu_mr, IOMMUDevice, iommu_mr);
    virtio_iommu_switch_address_space(sdev);

    /* Replay domain mappings on the associated memory region */
    g_tree_foreach(domain->mappings, virtio_iommu_notify_map_cb,
                   ep->iommu_mr);

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_detach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_detach *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint32_t ep_id = le32_to_cpu(req->endpoint);
    VirtIOIOMMUDomain *domain;
    VirtIOIOMMUEndpoint *ep;

    trace_virtio_iommu_detach(domain_id, ep_id);

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(ep_id));
    if (!ep) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    domain = ep->domain;

    if (!domain || domain->id != domain_id) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    virtio_iommu_detach_endpoint_from_domain(ep);

    if (QLIST_EMPTY(&domain->endpoint_list)) {
        g_tree_remove(s->domains, GUINT_TO_POINTER(domain->id));
    }
    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_map(VirtIOIOMMU *s,
                            struct virtio_iommu_req_map *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint64_t phys_start = le64_to_cpu(req->phys_start);
    uint64_t virt_start = le64_to_cpu(req->virt_start);
    uint64_t virt_end = le64_to_cpu(req->virt_end);
    uint32_t flags = le32_to_cpu(req->flags);
    VirtIOIOMMUDomain *domain;
    VirtIOIOMMUInterval *interval;
    VirtIOIOMMUMapping *mapping;
    VirtIOIOMMUEndpoint *ep;

    if (flags & ~VIRTIO_IOMMU_MAP_F_MASK) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    domain = g_tree_lookup(s->domains, GUINT_TO_POINTER(domain_id));
    if (!domain) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    if (domain->bypass) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    interval = g_malloc0(sizeof(*interval));

    interval->low = virt_start;
    interval->high = virt_end;

    mapping = g_tree_lookup(domain->mappings, (gpointer)interval);
    if (mapping) {
        g_free(interval);
        return VIRTIO_IOMMU_S_INVAL;
    }

    trace_virtio_iommu_map(domain_id, virt_start, virt_end, phys_start, flags);

    mapping = g_malloc0(sizeof(*mapping));
    mapping->phys_addr = phys_start;
    mapping->flags = flags;

    g_tree_insert(domain->mappings, interval, mapping);

    QLIST_FOREACH(ep, &domain->endpoint_list, next) {
        virtio_iommu_notify_map(ep->iommu_mr, virt_start, virt_end, phys_start,
                                flags);
    }

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_unmap(VirtIOIOMMU *s,
                              struct virtio_iommu_req_unmap *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint64_t virt_start = le64_to_cpu(req->virt_start);
    uint64_t virt_end = le64_to_cpu(req->virt_end);
    VirtIOIOMMUMapping *iter_val;
    VirtIOIOMMUInterval interval, *iter_key;
    VirtIOIOMMUDomain *domain;
    VirtIOIOMMUEndpoint *ep;
    int ret = VIRTIO_IOMMU_S_OK;

    trace_virtio_iommu_unmap(domain_id, virt_start, virt_end);

    domain = g_tree_lookup(s->domains, GUINT_TO_POINTER(domain_id));
    if (!domain) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    if (domain->bypass) {
        return VIRTIO_IOMMU_S_INVAL;
    }

    interval.low = virt_start;
    interval.high = virt_end;

    while (g_tree_lookup_extended(domain->mappings, &interval,
                                  (void **)&iter_key, (void**)&iter_val)) {
        uint64_t current_low = iter_key->low;
        uint64_t current_high = iter_key->high;

        if (interval.low <= current_low && interval.high >= current_high) {
            QLIST_FOREACH(ep, &domain->endpoint_list, next) {
                virtio_iommu_notify_unmap(ep->iommu_mr, current_low,
                                          current_high);
            }
            g_tree_remove(domain->mappings, iter_key);
            trace_virtio_iommu_unmap_done(domain_id, current_low, current_high);
        } else {
            ret = VIRTIO_IOMMU_S_RANGE;
            break;
        }
    }
    return ret;
}

static ssize_t virtio_iommu_fill_resv_mem_prop(VirtIOIOMMU *s, uint32_t ep,
                                               uint8_t *buf, size_t free)
{
    struct virtio_iommu_probe_resv_mem prop = {};
    size_t size = sizeof(prop), length = size - sizeof(prop.head), total;
    int i;

    total = size * s->nb_reserved_regions;

    if (total > free) {
        return -ENOSPC;
    }

    for (i = 0; i < s->nb_reserved_regions; i++) {
        unsigned subtype = s->reserved_regions[i].type;

        assert(subtype == VIRTIO_IOMMU_RESV_MEM_T_RESERVED ||
               subtype == VIRTIO_IOMMU_RESV_MEM_T_MSI);
        prop.head.type = cpu_to_le16(VIRTIO_IOMMU_PROBE_T_RESV_MEM);
        prop.head.length = cpu_to_le16(length);
        prop.subtype = subtype;
        prop.start = cpu_to_le64(s->reserved_regions[i].low);
        prop.end = cpu_to_le64(s->reserved_regions[i].high);

        memcpy(buf, &prop, size);

        trace_virtio_iommu_fill_resv_property(ep, prop.subtype,
                                              prop.start, prop.end);
        buf += size;
    }
    return total;
}

/**
 * virtio_iommu_probe - Fill the probe request buffer with
 * the properties the device is able to return
 */
static int virtio_iommu_probe(VirtIOIOMMU *s,
                              struct virtio_iommu_req_probe *req,
                              uint8_t *buf)
{
    uint32_t ep_id = le32_to_cpu(req->endpoint);
    size_t free = VIOMMU_PROBE_SIZE;
    ssize_t count;

    if (!virtio_iommu_mr(s, ep_id)) {
        return VIRTIO_IOMMU_S_NOENT;
    }

    count = virtio_iommu_fill_resv_mem_prop(s, ep_id, buf, free);
    if (count < 0) {
        return VIRTIO_IOMMU_S_INVAL;
    }
    buf += count;
    free -= count;

    return VIRTIO_IOMMU_S_OK;
}

static int virtio_iommu_iov_to_req(struct iovec *iov,
                                   unsigned int iov_cnt,
                                   void *req, size_t payload_sz)
{
    size_t sz = iov_to_buf(iov, iov_cnt, 0, req, payload_sz);

    if (unlikely(sz != payload_sz)) {
        return VIRTIO_IOMMU_S_INVAL;
    }
    return 0;
}

#define virtio_iommu_handle_req(__req)                                  \
static int virtio_iommu_handle_ ## __req(VirtIOIOMMU *s,                \
                                         struct iovec *iov,             \
                                         unsigned int iov_cnt)          \
{                                                                       \
    struct virtio_iommu_req_ ## __req req;                              \
    int ret = virtio_iommu_iov_to_req(iov, iov_cnt, &req,               \
                    sizeof(req) - sizeof(struct virtio_iommu_req_tail));\
                                                                        \
    return ret ? ret : virtio_iommu_ ## __req(s, &req);                 \
}

virtio_iommu_handle_req(attach)
virtio_iommu_handle_req(detach)
virtio_iommu_handle_req(map)
virtio_iommu_handle_req(unmap)

static int virtio_iommu_handle_probe(VirtIOIOMMU *s,
                                     struct iovec *iov,
                                     unsigned int iov_cnt,
                                     uint8_t *buf)
{
    struct virtio_iommu_req_probe req;
    int ret = virtio_iommu_iov_to_req(iov, iov_cnt, &req, sizeof(req));

    return ret ? ret : virtio_iommu_probe(s, &req, buf);
}

static void virtio_iommu_handle_command(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOIOMMU *s = VIRTIO_IOMMU(vdev);
    struct virtio_iommu_req_head head;
    struct virtio_iommu_req_tail tail = {};
    VirtQueueElement *elem;
    unsigned int iov_cnt;
    struct iovec *iov;
    void *buf = NULL;
    size_t sz;

    for (;;) {
        size_t output_size = sizeof(tail);

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            return;
        }

        if (iov_size(elem->in_sg, elem->in_num) < sizeof(tail) ||
            iov_size(elem->out_sg, elem->out_num) < sizeof(head)) {
            virtio_error(vdev, "virtio-iommu bad head/tail size");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }

        iov_cnt = elem->out_num;
        iov = elem->out_sg;
        sz = iov_to_buf(iov, iov_cnt, 0, &head, sizeof(head));
        if (unlikely(sz != sizeof(head))) {
            tail.status = VIRTIO_IOMMU_S_DEVERR;
            goto out;
        }
        qemu_rec_mutex_lock(&s->mutex);
        switch (head.type) {
        case VIRTIO_IOMMU_T_ATTACH:
            tail.status = virtio_iommu_handle_attach(s, iov, iov_cnt);
            break;
        case VIRTIO_IOMMU_T_DETACH:
            tail.status = virtio_iommu_handle_detach(s, iov, iov_cnt);
            break;
        case VIRTIO_IOMMU_T_MAP:
            tail.status = virtio_iommu_handle_map(s, iov, iov_cnt);
            break;
        case VIRTIO_IOMMU_T_UNMAP:
            tail.status = virtio_iommu_handle_unmap(s, iov, iov_cnt);
            break;
        case VIRTIO_IOMMU_T_PROBE:
        {
            struct virtio_iommu_req_tail *ptail;

            output_size = s->config.probe_size + sizeof(tail);
            buf = g_malloc0(output_size);

            ptail = buf + s->config.probe_size;
            ptail->status = virtio_iommu_handle_probe(s, iov, iov_cnt, buf);
            break;
        }
        default:
            tail.status = VIRTIO_IOMMU_S_UNSUPP;
        }
        qemu_rec_mutex_unlock(&s->mutex);

out:
        sz = iov_from_buf(elem->in_sg, elem->in_num, 0,
                          buf ? buf : &tail, output_size);
        assert(sz == output_size);

        virtqueue_push(vq, elem, sz);
        virtio_notify(vdev, vq);
        g_free(elem);
        g_free(buf);
        buf = NULL;
    }
}

static void virtio_iommu_report_fault(VirtIOIOMMU *viommu, uint8_t reason,
                                      int flags, uint32_t endpoint,
                                      uint64_t address)
{
    VirtIODevice *vdev = &viommu->parent_obj;
    VirtQueue *vq = viommu->event_vq;
    struct virtio_iommu_fault fault;
    VirtQueueElement *elem;
    size_t sz;

    memset(&fault, 0, sizeof(fault));
    fault.reason = reason;
    fault.flags = cpu_to_le32(flags);
    fault.endpoint = cpu_to_le32(endpoint);
    fault.address = cpu_to_le64(address);

    elem = virtqueue_pop(vq, sizeof(VirtQueueElement));

    if (!elem) {
        error_report_once(
            "no buffer available in event queue to report event");
        return;
    }

    if (iov_size(elem->in_sg, elem->in_num) < sizeof(fault)) {
        virtio_error(vdev, "error buffer of wrong size");
        virtqueue_detach_element(vq, elem, 0);
        g_free(elem);
        return;
    }

    sz = iov_from_buf(elem->in_sg, elem->in_num, 0,
                      &fault, sizeof(fault));
    assert(sz == sizeof(fault));

    trace_virtio_iommu_report_fault(reason, flags, endpoint, address);
    virtqueue_push(vq, elem, sz);
    virtio_notify(vdev, vq);
    g_free(elem);

}

static IOMMUTLBEntry virtio_iommu_translate(IOMMUMemoryRegion *mr, hwaddr addr,
                                            IOMMUAccessFlags flag,
                                            int iommu_idx)
{
    IOMMUDevice *sdev = container_of(mr, IOMMUDevice, iommu_mr);
    VirtIOIOMMUInterval interval, *mapping_key;
    VirtIOIOMMUMapping *mapping_value;
    VirtIOIOMMU *s = sdev->viommu;
    bool read_fault, write_fault;
    VirtIOIOMMUEndpoint *ep;
    uint32_t sid, flags;
    bool bypass_allowed;
    int granule;
    bool found;
    int i;

    interval.low = addr;
    interval.high = addr + 1;
    granule = ctz64(s->config.page_size_mask);

    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = BIT_ULL(granule) - 1,
        .perm = IOMMU_NONE,
    };

    bypass_allowed = s->config.bypass;

    sid = virtio_iommu_get_bdf(sdev);

    trace_virtio_iommu_translate(mr->parent_obj.name, sid, addr, flag);
    qemu_rec_mutex_lock(&s->mutex);

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(sid));

    if (bypass_allowed)
        assert(ep && ep->domain && !ep->domain->bypass);

    if (!ep) {
        if (!bypass_allowed) {
            error_report_once("%s sid=%d is not known!!", __func__, sid);
            virtio_iommu_report_fault(s, VIRTIO_IOMMU_FAULT_R_UNKNOWN,
                                      VIRTIO_IOMMU_FAULT_F_ADDRESS,
                                      sid, addr);
        } else {
            entry.perm = flag;
        }
        goto unlock;
    }

    for (i = 0; i < s->nb_reserved_regions; i++) {
        ReservedRegion *reg = &s->reserved_regions[i];

        if (addr >= reg->low && addr <= reg->high) {
            switch (reg->type) {
            case VIRTIO_IOMMU_RESV_MEM_T_MSI:
                entry.perm = flag;
                break;
            case VIRTIO_IOMMU_RESV_MEM_T_RESERVED:
            default:
                virtio_iommu_report_fault(s, VIRTIO_IOMMU_FAULT_R_MAPPING,
                                          VIRTIO_IOMMU_FAULT_F_ADDRESS,
                                          sid, addr);
                break;
            }
            goto unlock;
        }
    }

    if (!ep->domain) {
        if (!bypass_allowed) {
            error_report_once("%s %02x:%02x.%01x not attached to any domain",
                              __func__, PCI_BUS_NUM(sid),
                              PCI_SLOT(sid), PCI_FUNC(sid));
            virtio_iommu_report_fault(s, VIRTIO_IOMMU_FAULT_R_DOMAIN,
                                      VIRTIO_IOMMU_FAULT_F_ADDRESS,
                                      sid, addr);
        } else {
            entry.perm = flag;
        }
        goto unlock;
    } else if (ep->domain->bypass) {
        entry.perm = flag;
        goto unlock;
    }

    found = g_tree_lookup_extended(ep->domain->mappings, (gpointer)(&interval),
                                   (void **)&mapping_key,
                                   (void **)&mapping_value);
    if (!found) {
        error_report_once("%s no mapping for 0x%"PRIx64" for sid=%d",
                          __func__, addr, sid);
        virtio_iommu_report_fault(s, VIRTIO_IOMMU_FAULT_R_MAPPING,
                                  VIRTIO_IOMMU_FAULT_F_ADDRESS,
                                  sid, addr);
        goto unlock;
    }

    read_fault = (flag & IOMMU_RO) &&
                    !(mapping_value->flags & VIRTIO_IOMMU_MAP_F_READ);
    write_fault = (flag & IOMMU_WO) &&
                    !(mapping_value->flags & VIRTIO_IOMMU_MAP_F_WRITE);

    flags = read_fault ? VIRTIO_IOMMU_FAULT_F_READ : 0;
    flags |= write_fault ? VIRTIO_IOMMU_FAULT_F_WRITE : 0;
    if (flags) {
        error_report_once("%s permission error on 0x%"PRIx64"(%d): allowed=%d",
                          __func__, addr, flag, mapping_value->flags);
        flags |= VIRTIO_IOMMU_FAULT_F_ADDRESS;
        virtio_iommu_report_fault(s, VIRTIO_IOMMU_FAULT_R_MAPPING,
                                  flags | VIRTIO_IOMMU_FAULT_F_ADDRESS,
                                  sid, addr);
        goto unlock;
    }
    entry.translated_addr = addr - mapping_key->low + mapping_value->phys_addr;
    entry.perm = flag;
    trace_virtio_iommu_translate_out(addr, entry.translated_addr, sid);

unlock:
    qemu_rec_mutex_unlock(&s->mutex);
    return entry;
}

static void virtio_iommu_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);
    struct virtio_iommu_config *dev_config = &dev->config;
    struct virtio_iommu_config *out_config = (void *)config_data;

    out_config->page_size_mask = cpu_to_le64(dev_config->page_size_mask);
    out_config->input_range.start = cpu_to_le64(dev_config->input_range.start);
    out_config->input_range.end = cpu_to_le64(dev_config->input_range.end);
    out_config->domain_range.start = cpu_to_le32(dev_config->domain_range.start);
    out_config->domain_range.end = cpu_to_le32(dev_config->domain_range.end);
    out_config->probe_size = cpu_to_le32(dev_config->probe_size);
    out_config->bypass = dev_config->bypass;

    trace_virtio_iommu_get_config(dev_config->page_size_mask,
                                  dev_config->input_range.start,
                                  dev_config->input_range.end,
                                  dev_config->domain_range.start,
                                  dev_config->domain_range.end,
                                  dev_config->probe_size,
                                  dev_config->bypass);
}

static void virtio_iommu_set_config(VirtIODevice *vdev,
                                    const uint8_t *config_data)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);
    struct virtio_iommu_config *dev_config = &dev->config;
    const struct virtio_iommu_config *in_config = (void *)config_data;

    if (in_config->bypass != dev_config->bypass) {
        if (!virtio_vdev_has_feature(vdev, VIRTIO_IOMMU_F_BYPASS_CONFIG)) {
            virtio_error(vdev, "cannot set config.bypass");
            return;
        } else if (in_config->bypass != 0 && in_config->bypass != 1) {
            virtio_error(vdev, "invalid config.bypass value '%u'",
                         in_config->bypass);
            return;
        }
        dev_config->bypass = in_config->bypass;
        virtio_iommu_switch_address_space_all(dev);
    }

    trace_virtio_iommu_set_config(in_config->bypass);
}

static uint64_t virtio_iommu_get_features(VirtIODevice *vdev, uint64_t f,
                                          Error **errp)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);

    f |= dev->features;
    trace_virtio_iommu_get_features(f);
    return f;
}

static gint int_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
    guint ua = GPOINTER_TO_UINT(a);
    guint ub = GPOINTER_TO_UINT(b);
    return (ua > ub) - (ua < ub);
}

static gboolean virtio_iommu_remap(gpointer key, gpointer value, gpointer data)
{
    VirtIOIOMMUMapping *mapping = (VirtIOIOMMUMapping *) value;
    VirtIOIOMMUInterval *interval = (VirtIOIOMMUInterval *) key;
    IOMMUMemoryRegion *mr = (IOMMUMemoryRegion *) data;

    trace_virtio_iommu_remap(mr->parent_obj.name, interval->low, interval->high,
                             mapping->phys_addr);
    virtio_iommu_notify_map(mr, interval->low, interval->high,
                            mapping->phys_addr, mapping->flags);
    return false;
}

static void virtio_iommu_replay(IOMMUMemoryRegion *mr, IOMMUNotifier *n)
{
    IOMMUDevice *sdev = container_of(mr, IOMMUDevice, iommu_mr);
    VirtIOIOMMU *s = sdev->viommu;
    uint32_t sid;
    VirtIOIOMMUEndpoint *ep;

    sid = virtio_iommu_get_bdf(sdev);

    qemu_rec_mutex_lock(&s->mutex);

    if (!s->endpoints) {
        goto unlock;
    }

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(sid));
    if (!ep || !ep->domain) {
        goto unlock;
    }

    g_tree_foreach(ep->domain->mappings, virtio_iommu_remap, mr);

unlock:
    qemu_rec_mutex_unlock(&s->mutex);
}

static int virtio_iommu_notify_flag_changed(IOMMUMemoryRegion *iommu_mr,
                                            IOMMUNotifierFlag old,
                                            IOMMUNotifierFlag new,
                                            Error **errp)
{
    if (new & IOMMU_NOTIFIER_DEVIOTLB_UNMAP) {
        error_setg(errp, "Virtio-iommu does not support dev-iotlb yet");
        return -EINVAL;
    }

    if (old == IOMMU_NOTIFIER_NONE) {
        trace_virtio_iommu_notify_flag_add(iommu_mr->parent_obj.name);
    } else if (new == IOMMU_NOTIFIER_NONE) {
        trace_virtio_iommu_notify_flag_del(iommu_mr->parent_obj.name);
    }
    return 0;
}

/*
 * The default mask (TARGET_PAGE_MASK) is the smallest supported guest granule,
 * for example 0xfffffffffffff000. When an assigned device has page size
 * restrictions due to the hardware IOMMU configuration, apply this restriction
 * to the mask.
 */
static int virtio_iommu_set_page_size_mask(IOMMUMemoryRegion *mr,
                                           uint64_t new_mask,
                                           Error **errp)
{
    IOMMUDevice *sdev = container_of(mr, IOMMUDevice, iommu_mr);
    VirtIOIOMMU *s = sdev->viommu;
    uint64_t cur_mask = s->config.page_size_mask;

    trace_virtio_iommu_set_page_size_mask(mr->parent_obj.name, cur_mask,
                                          new_mask);

    if ((cur_mask & new_mask) == 0) {
        error_setg(errp, "virtio-iommu %s reports a page size mask 0x%"PRIx64
                   " incompatible with currently supported mask 0x%"PRIx64,
                   mr->parent_obj.name, new_mask, cur_mask);
        return -1;
    }

    /*
     * Once the granule is frozen we can't change the mask anymore. If by
     * chance the hotplugged device supports the same granule, we can still
     * accept it.
     */
    if (s->granule_frozen) {
        int cur_granule = ctz64(cur_mask);

        if (!(BIT_ULL(cur_granule) & new_mask)) {
            error_setg(errp, "virtio-iommu %s does not support frozen granule 0x%llx",
                       mr->parent_obj.name, BIT_ULL(cur_granule));
            return -1;
        }
        return 0;
    }

    s->config.page_size_mask &= new_mask;
    return 0;
}

static void virtio_iommu_system_reset(void *opaque)
{
    VirtIOIOMMU *s = opaque;

    trace_virtio_iommu_system_reset();

    /*
     * config.bypass is sticky across device reset, but should be restored on
     * system reset
     */
    s->config.bypass = s->boot_bypass;
    virtio_iommu_switch_address_space_all(s);

}

static void virtio_iommu_freeze_granule(Notifier *notifier, void *data)
{
    VirtIOIOMMU *s = container_of(notifier, VirtIOIOMMU, machine_done);
    int granule;

    if (likely(s->config.bypass)) {
        /*
         * Transient IOMMU MR enable to collect page_size_mask requirements
         * through memory_region_iommu_set_page_size_mask() called by
         * VFIO region_add() callback
         */
         s->config.bypass = false;
         virtio_iommu_switch_address_space_all(s);
         /* restore default */
         s->config.bypass = true;
         virtio_iommu_switch_address_space_all(s);
    }
    s->granule_frozen = true;
    granule = ctz64(s->config.page_size_mask);
    trace_virtio_iommu_freeze_granule(BIT_ULL(granule));
}

static void virtio_iommu_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    virtio_init(vdev, VIRTIO_ID_IOMMU, sizeof(struct virtio_iommu_config));

    memset(s->iommu_pcibus_by_bus_num, 0, sizeof(s->iommu_pcibus_by_bus_num));

    s->req_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE,
                             virtio_iommu_handle_command);
    s->event_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE, NULL);

    /*
     * config.bypass is needed to get initial address space early, such as
     * in vfio realize
     */
    s->config.bypass = s->boot_bypass;
    s->config.page_size_mask = qemu_target_page_mask();
    s->config.input_range.end = UINT64_MAX;
    s->config.domain_range.end = UINT32_MAX;
    s->config.probe_size = VIOMMU_PROBE_SIZE;

    virtio_add_feature(&s->features, VIRTIO_RING_F_EVENT_IDX);
    virtio_add_feature(&s->features, VIRTIO_RING_F_INDIRECT_DESC);
    virtio_add_feature(&s->features, VIRTIO_F_VERSION_1);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_INPUT_RANGE);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_DOMAIN_RANGE);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_MAP_UNMAP);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_MMIO);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_PROBE);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_BYPASS_CONFIG);

    qemu_rec_mutex_init(&s->mutex);

    s->as_by_busptr = g_hash_table_new_full(NULL, NULL, NULL, g_free);

    if (s->primary_bus) {
        pci_setup_iommu(s->primary_bus, virtio_iommu_find_add_as, s);
    } else {
        error_setg(errp, "VIRTIO-IOMMU is not attached to any PCI bus!");
    }

    s->machine_done.notify = virtio_iommu_freeze_granule;
    qemu_add_machine_init_done_notifier(&s->machine_done);

    qemu_register_reset(virtio_iommu_system_reset, s);
}

static void virtio_iommu_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    qemu_unregister_reset(virtio_iommu_system_reset, s);
    qemu_remove_machine_init_done_notifier(&s->machine_done);

    g_hash_table_destroy(s->as_by_busptr);
    if (s->domains) {
        g_tree_destroy(s->domains);
    }
    if (s->endpoints) {
        g_tree_destroy(s->endpoints);
    }

    qemu_rec_mutex_destroy(&s->mutex);

    virtio_delete_queue(s->req_vq);
    virtio_delete_queue(s->event_vq);
    virtio_cleanup(vdev);
}

static void virtio_iommu_device_reset(VirtIODevice *vdev)
{
    VirtIOIOMMU *s = VIRTIO_IOMMU(vdev);

    trace_virtio_iommu_device_reset();

    if (s->domains) {
        g_tree_destroy(s->domains);
    }
    if (s->endpoints) {
        g_tree_destroy(s->endpoints);
    }
    s->domains = g_tree_new_full((GCompareDataFunc)int_cmp,
                                 NULL, NULL, virtio_iommu_put_domain);
    s->endpoints = g_tree_new_full((GCompareDataFunc)int_cmp,
                                   NULL, NULL, virtio_iommu_put_endpoint);
}

static void virtio_iommu_set_status(VirtIODevice *vdev, uint8_t status)
{
    trace_virtio_iommu_device_status(status);
}

static void virtio_iommu_instance_init(Object *obj)
{
}

#define VMSTATE_INTERVAL                               \
{                                                      \
    .name = "interval",                                \
    .version_id = 1,                                   \
    .minimum_version_id = 1,                           \
    .fields = (VMStateField[]) {                       \
        VMSTATE_UINT64(low, VirtIOIOMMUInterval),      \
        VMSTATE_UINT64(high, VirtIOIOMMUInterval),     \
        VMSTATE_END_OF_LIST()                          \
    }                                                  \
}

#define VMSTATE_MAPPING                               \
{                                                     \
    .name = "mapping",                                \
    .version_id = 1,                                  \
    .minimum_version_id = 1,                          \
    .fields = (VMStateField[]) {                      \
        VMSTATE_UINT64(phys_addr, VirtIOIOMMUMapping),\
        VMSTATE_UINT32(flags, VirtIOIOMMUMapping),    \
        VMSTATE_END_OF_LIST()                         \
    },                                                \
}

static const VMStateDescription vmstate_interval_mapping[2] = {
    VMSTATE_MAPPING,   /* value */
    VMSTATE_INTERVAL   /* key   */
};

static int domain_preload(void *opaque)
{
    VirtIOIOMMUDomain *domain = opaque;

    domain->mappings = g_tree_new_full((GCompareDataFunc)interval_cmp,
                                       NULL, g_free, g_free);
    return 0;
}

static const VMStateDescription vmstate_endpoint = {
    .name = "endpoint",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, VirtIOIOMMUEndpoint),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_domain = {
    .name = "domain",
    .version_id = 2,
    .minimum_version_id = 2,
    .pre_load = domain_preload,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(id, VirtIOIOMMUDomain),
        VMSTATE_GTREE_V(mappings, VirtIOIOMMUDomain, 1,
                        vmstate_interval_mapping,
                        VirtIOIOMMUInterval, VirtIOIOMMUMapping),
        VMSTATE_QLIST_V(endpoint_list, VirtIOIOMMUDomain, 1,
                        vmstate_endpoint, VirtIOIOMMUEndpoint, next),
        VMSTATE_BOOL_V(bypass, VirtIOIOMMUDomain, 2),
        VMSTATE_END_OF_LIST()
    }
};

static gboolean reconstruct_endpoints(gpointer key, gpointer value,
                                      gpointer data)
{
    VirtIOIOMMU *s = (VirtIOIOMMU *)data;
    VirtIOIOMMUDomain *d = (VirtIOIOMMUDomain *)value;
    VirtIOIOMMUEndpoint *iter;
    IOMMUMemoryRegion *mr;

    QLIST_FOREACH(iter, &d->endpoint_list, next) {
        mr = virtio_iommu_mr(s, iter->id);
        assert(mr);

        iter->domain = d;
        iter->iommu_mr = mr;
        g_tree_insert(s->endpoints, GUINT_TO_POINTER(iter->id), iter);
    }
    return false; /* continue the domain traversal */
}

static int iommu_post_load(void *opaque, int version_id)
{
    VirtIOIOMMU *s = opaque;

    g_tree_foreach(s->domains, reconstruct_endpoints, s);

    /*
     * Memory regions are dynamically turned on/off depending on
     * 'config.bypass' and attached domain type if there is. After
     * migration, we need to make sure the memory regions are
     * still correct.
     */
    virtio_iommu_switch_address_space_all(s);
    return 0;
}

static const VMStateDescription vmstate_virtio_iommu_device = {
    .name = "virtio-iommu-device",
    .minimum_version_id = 2,
    .version_id = 2,
    .post_load = iommu_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_GTREE_DIRECT_KEY_V(domains, VirtIOIOMMU, 2,
                                   &vmstate_domain, VirtIOIOMMUDomain),
        VMSTATE_UINT8_V(config.bypass, VirtIOIOMMU, 2),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_virtio_iommu = {
    .name = "virtio-iommu",
    .minimum_version_id = 2,
    .priority = MIG_PRI_IOMMU,
    .version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_iommu_properties[] = {
    DEFINE_PROP_LINK("primary-bus", VirtIOIOMMU, primary_bus,
                     TYPE_PCI_BUS, PCIBus *),
    DEFINE_PROP_BOOL("boot-bypass", VirtIOIOMMU, boot_bypass, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_iommu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_iommu_properties);
    dc->vmsd = &vmstate_virtio_iommu;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_iommu_device_realize;
    vdc->unrealize = virtio_iommu_device_unrealize;
    vdc->reset = virtio_iommu_device_reset;
    vdc->get_config = virtio_iommu_get_config;
    vdc->set_config = virtio_iommu_set_config;
    vdc->get_features = virtio_iommu_get_features;
    vdc->set_status = virtio_iommu_set_status;
    vdc->vmsd = &vmstate_virtio_iommu_device;
}

static void virtio_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = virtio_iommu_translate;
    imrc->replay = virtio_iommu_replay;
    imrc->notify_flag_changed = virtio_iommu_notify_flag_changed;
    imrc->iommu_set_page_size_mask = virtio_iommu_set_page_size_mask;
}

static const TypeInfo virtio_iommu_info = {
    .name = TYPE_VIRTIO_IOMMU,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOIOMMU),
    .instance_init = virtio_iommu_instance_init,
    .class_init = virtio_iommu_class_init,
};

static const TypeInfo virtio_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_VIRTIO_IOMMU_MEMORY_REGION,
    .class_init = virtio_iommu_memory_region_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_iommu_info);
    type_register_static(&virtio_iommu_memory_region_info);
}

type_init(virtio_register_types)
