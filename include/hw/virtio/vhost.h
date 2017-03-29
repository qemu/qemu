#ifndef VHOST_H
#define VHOST_H

#include "hw/hw.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio.h"
#include "exec/memory.h"

/* Generic structures common for any vhost based device. */
struct vhost_virtqueue {
    int kick;
    int call;
    void *desc;
    void *avail;
    void *used;
    int num;
    unsigned long long desc_phys;
    unsigned desc_size;
    unsigned long long avail_phys;
    unsigned avail_size;
    unsigned long long used_phys;
    unsigned used_size;
    EventNotifier masked_notifier;
    struct vhost_dev *dev;
};

typedef unsigned long vhost_log_chunk_t;
#define VHOST_LOG_PAGE 0x1000
#define VHOST_LOG_BITS (8 * sizeof(vhost_log_chunk_t))
#define VHOST_LOG_CHUNK (VHOST_LOG_PAGE * VHOST_LOG_BITS)
#define VHOST_INVALID_FEATURE_BIT   (0xff)

struct vhost_log {
    unsigned long long size;
    int refcnt;
    int fd;
    vhost_log_chunk_t *log;
};

struct vhost_dev;
struct vhost_iommu {
    struct vhost_dev *hdev;
    MemoryRegion *mr;
    hwaddr iommu_offset;
    IOMMUNotifier n;
    QLIST_ENTRY(vhost_iommu) iommu_next;
};

struct vhost_memory;
struct vhost_dev {
    VirtIODevice *vdev;
    MemoryListener memory_listener;
    MemoryListener iommu_listener;
    struct vhost_memory *mem;
    int n_mem_sections;
    MemoryRegionSection *mem_sections;
    struct vhost_virtqueue *vqs;
    int nvqs;
    /* the first virtqueue which would be used by this vhost dev */
    int vq_index;
    uint64_t features;
    uint64_t acked_features;
    uint64_t backend_features;
    uint64_t protocol_features;
    uint64_t max_queues;
    bool started;
    bool log_enabled;
    uint64_t log_size;
    Error *migration_blocker;
    bool memory_changed;
    hwaddr mem_changed_start_addr;
    hwaddr mem_changed_end_addr;
    const VhostOps *vhost_ops;
    void *opaque;
    struct vhost_log *log;
    QLIST_ENTRY(vhost_dev) entry;
    QLIST_HEAD(, vhost_iommu) iommu_list;
    IOMMUNotifier n;
};

int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type,
                   uint32_t busyloop_timeout);
void vhost_dev_cleanup(struct vhost_dev *hdev);
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev);
void vhost_dev_stop(struct vhost_dev *hdev, VirtIODevice *vdev);
int vhost_dev_enable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev);
void vhost_dev_disable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev);

/* Test and clear masked event pending status.
 * Should be called after unmask to avoid losing events.
 */
bool vhost_virtqueue_pending(struct vhost_dev *hdev, int n);

/* Mask/unmask events from this vq.
 */
void vhost_virtqueue_mask(struct vhost_dev *hdev, VirtIODevice *vdev, int n,
                          bool mask);
uint64_t vhost_get_features(struct vhost_dev *hdev, const int *feature_bits,
                            uint64_t features);
void vhost_ack_features(struct vhost_dev *hdev, const int *feature_bits,
                        uint64_t features);
bool vhost_has_free_slot(void);

int vhost_net_set_backend(struct vhost_dev *hdev,
                          struct vhost_vring_file *file);

void vhost_device_iotlb_miss(struct vhost_dev *dev, uint64_t iova, int write);
#endif
