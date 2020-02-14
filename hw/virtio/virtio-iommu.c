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
#include "qemu/iov.h"
#include "qemu-common.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio.h"
#include "sysemu/kvm.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "trace.h"

#include "standard-headers/linux/virtio_ids.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "hw/virtio/virtio-iommu.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci.h"

/* Max size */
#define VIOMMU_DEFAULT_QUEUE_SIZE 256

typedef struct VirtIOIOMMUDomain {
    uint32_t id;
    GTree *mappings;
    QLIST_HEAD(, VirtIOIOMMUEndpoint) endpoint_list;
} VirtIOIOMMUDomain;

typedef struct VirtIOIOMMUEndpoint {
    uint32_t id;
    VirtIOIOMMUDomain *domain;
    QLIST_ENTRY(VirtIOIOMMUEndpoint) next;
} VirtIOIOMMUEndpoint;

typedef struct VirtIOIOMMUInterval {
    uint64_t low;
    uint64_t high;
} VirtIOIOMMUInterval;

static inline uint16_t virtio_iommu_get_bdf(IOMMUDevice *dev)
{
    return PCI_BUILD_BDF(pci_bus_num(dev->bus), dev->devfn);
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
        devfn = sid & PCI_DEVFN_MAX;
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

static void virtio_iommu_detach_endpoint_from_domain(VirtIOIOMMUEndpoint *ep)
{
    if (!ep->domain) {
        return;
    }
    QLIST_REMOVE(ep, next);
    ep->domain = NULL;
}

static VirtIOIOMMUEndpoint *virtio_iommu_get_endpoint(VirtIOIOMMU *s,
                                                      uint32_t ep_id)
{
    VirtIOIOMMUEndpoint *ep;

    ep = g_tree_lookup(s->endpoints, GUINT_TO_POINTER(ep_id));
    if (ep) {
        return ep;
    }
    if (!virtio_iommu_mr(s, ep_id)) {
        return NULL;
    }
    ep = g_malloc0(sizeof(*ep));
    ep->id = ep_id;
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
                                                  uint32_t domain_id)
{
    VirtIOIOMMUDomain *domain;

    domain = g_tree_lookup(s->domains, GUINT_TO_POINTER(domain_id));
    if (domain) {
        return domain;
    }
    domain = g_malloc0(sizeof(*domain));
    domain->id = domain_id;
    domain->mappings = g_tree_new_full((GCompareDataFunc)interval_cmp,
                                   NULL, (GDestroyNotify)g_free,
                                   (GDestroyNotify)g_free);
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
        sdev = sbus->pbdev[devfn] = g_malloc0(sizeof(IOMMUDevice));

        sdev->viommu = s;
        sdev->bus = bus;
        sdev->devfn = devfn;

        trace_virtio_iommu_init_iommu_mr(name);

        memory_region_init_iommu(&sdev->iommu_mr, sizeof(sdev->iommu_mr),
                                 TYPE_VIRTIO_IOMMU_MEMORY_REGION,
                                 OBJECT(s), name,
                                 UINT64_MAX);
        address_space_init(&sdev->as,
                           MEMORY_REGION(&sdev->iommu_mr), TYPE_VIRTIO_IOMMU);
        g_free(name);
    }
    return &sdev->as;
}

static int virtio_iommu_attach(VirtIOIOMMU *s,
                               struct virtio_iommu_req_attach *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint32_t ep_id = le32_to_cpu(req->endpoint);
    VirtIOIOMMUDomain *domain;
    VirtIOIOMMUEndpoint *ep;

    trace_virtio_iommu_attach(domain_id, ep_id);

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

    domain = virtio_iommu_get_domain(s, domain_id);
    QLIST_INSERT_HEAD(&domain->endpoint_list, ep, next);

    ep->domain = domain;

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

    trace_virtio_iommu_map(domain_id, virt_start, virt_end, phys_start, flags);

    return VIRTIO_IOMMU_S_UNSUPP;
}

static int virtio_iommu_unmap(VirtIOIOMMU *s,
                              struct virtio_iommu_req_unmap *req)
{
    uint32_t domain_id = le32_to_cpu(req->domain);
    uint64_t virt_start = le64_to_cpu(req->virt_start);
    uint64_t virt_end = le64_to_cpu(req->virt_end);

    trace_virtio_iommu_unmap(domain_id, virt_start, virt_end);

    return VIRTIO_IOMMU_S_UNSUPP;
}

static int virtio_iommu_iov_to_req(struct iovec *iov,
                                   unsigned int iov_cnt,
                                   void *req, size_t req_sz)
{
    size_t sz, payload_sz = req_sz - sizeof(struct virtio_iommu_req_tail);

    sz = iov_to_buf(iov, iov_cnt, 0, req, payload_sz);
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
    int ret = virtio_iommu_iov_to_req(iov, iov_cnt, &req, sizeof(req)); \
                                                                        \
    return ret ? ret : virtio_iommu_ ## __req(s, &req);                 \
}

virtio_iommu_handle_req(attach)
virtio_iommu_handle_req(detach)
virtio_iommu_handle_req(map)
virtio_iommu_handle_req(unmap)

static void virtio_iommu_handle_command(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOIOMMU *s = VIRTIO_IOMMU(vdev);
    struct virtio_iommu_req_head head;
    struct virtio_iommu_req_tail tail = {};
    VirtQueueElement *elem;
    unsigned int iov_cnt;
    struct iovec *iov;
    size_t sz;

    for (;;) {
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
        qemu_mutex_lock(&s->mutex);
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
        default:
            tail.status = VIRTIO_IOMMU_S_UNSUPP;
        }
        qemu_mutex_unlock(&s->mutex);

out:
        sz = iov_from_buf(elem->in_sg, elem->in_num, 0,
                          &tail, sizeof(tail));
        assert(sz == sizeof(tail));

        virtqueue_push(vq, elem, sizeof(tail));
        virtio_notify(vdev, vq);
        g_free(elem);
    }
}

static IOMMUTLBEntry virtio_iommu_translate(IOMMUMemoryRegion *mr, hwaddr addr,
                                            IOMMUAccessFlags flag,
                                            int iommu_idx)
{
    IOMMUDevice *sdev = container_of(mr, IOMMUDevice, iommu_mr);
    uint32_t sid;

    IOMMUTLBEntry entry = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = addr,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    sid = virtio_iommu_get_bdf(sdev);

    trace_virtio_iommu_translate(mr->parent_obj.name, sid, addr, flag);
    return entry;
}

static void virtio_iommu_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);
    struct virtio_iommu_config *config = &dev->config;

    trace_virtio_iommu_get_config(config->page_size_mask,
                                  config->input_range.start,
                                  config->input_range.end,
                                  config->domain_range.end,
                                  config->probe_size);
    memcpy(config_data, &dev->config, sizeof(struct virtio_iommu_config));
}

static void virtio_iommu_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    struct virtio_iommu_config config;

    memcpy(&config, config_data, sizeof(struct virtio_iommu_config));
    trace_virtio_iommu_set_config(config.page_size_mask,
                                  config.input_range.start,
                                  config.input_range.end,
                                  config.domain_range.end,
                                  config.probe_size);
}

static uint64_t virtio_iommu_get_features(VirtIODevice *vdev, uint64_t f,
                                          Error **errp)
{
    VirtIOIOMMU *dev = VIRTIO_IOMMU(vdev);

    f |= dev->features;
    trace_virtio_iommu_get_features(f);
    return f;
}

/*
 * Migration is not yet supported: most of the state consists
 * of balanced binary trees which are not yet ready for getting
 * migrated
 */
static const VMStateDescription vmstate_virtio_iommu_device = {
    .name = "virtio-iommu-device",
    .unmigratable = 1,
};

static gint int_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
    guint ua = GPOINTER_TO_UINT(a);
    guint ub = GPOINTER_TO_UINT(b);
    return (ua > ub) - (ua < ub);
}

static void virtio_iommu_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    virtio_init(vdev, "virtio-iommu", VIRTIO_ID_IOMMU,
                sizeof(struct virtio_iommu_config));

    memset(s->iommu_pcibus_by_bus_num, 0, sizeof(s->iommu_pcibus_by_bus_num));

    s->req_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE,
                             virtio_iommu_handle_command);
    s->event_vq = virtio_add_queue(vdev, VIOMMU_DEFAULT_QUEUE_SIZE, NULL);

    s->config.page_size_mask = TARGET_PAGE_MASK;
    s->config.input_range.end = -1UL;
    s->config.domain_range.end = 32;

    virtio_add_feature(&s->features, VIRTIO_RING_F_EVENT_IDX);
    virtio_add_feature(&s->features, VIRTIO_RING_F_INDIRECT_DESC);
    virtio_add_feature(&s->features, VIRTIO_F_VERSION_1);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_INPUT_RANGE);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_DOMAIN_RANGE);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_MAP_UNMAP);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_BYPASS);
    virtio_add_feature(&s->features, VIRTIO_IOMMU_F_MMIO);

    qemu_mutex_init(&s->mutex);

    s->as_by_busptr = g_hash_table_new_full(NULL, NULL, NULL, g_free);

    if (s->primary_bus) {
        pci_setup_iommu(s->primary_bus, virtio_iommu_find_add_as, s);
    } else {
        error_setg(errp, "VIRTIO-IOMMU is not attached to any PCI bus!");
    }
}

static void virtio_iommu_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOIOMMU *s = VIRTIO_IOMMU(dev);

    g_tree_destroy(s->domains);
    g_tree_destroy(s->endpoints);

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

static const VMStateDescription vmstate_virtio_iommu = {
    .name = "virtio-iommu",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_iommu_properties[] = {
    DEFINE_PROP_LINK("primary-bus", VirtIOIOMMU, primary_bus, "PCI", PCIBus *),
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
