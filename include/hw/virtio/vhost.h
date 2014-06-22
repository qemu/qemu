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
    unsigned long long used_phys;
    unsigned used_size;
    void *ring;
    unsigned long long ring_phys;
    unsigned ring_size;
    EventNotifier masked_notifier;
};

typedef unsigned long vhost_log_chunk_t;
#define VHOST_LOG_PAGE 0x1000
#define VHOST_LOG_BITS (8 * sizeof(vhost_log_chunk_t))
#define VHOST_LOG_CHUNK (VHOST_LOG_PAGE * VHOST_LOG_BITS)
#define VHOST_INVALID_FEATURE_BIT   (0xff)

struct vhost_memory;
struct vhost_dev {
    MemoryListener memory_listener;
    struct vhost_memory *mem;
    int n_mem_sections;
    MemoryRegionSection *mem_sections;
    struct vhost_virtqueue *vqs;
    int nvqs;
    /* the first virtuque which would be used by this vhost dev */
    int vq_index;
    unsigned long long features;
    unsigned long long acked_features;
    unsigned long long backend_features;
    bool started;
    bool log_enabled;
    vhost_log_chunk_t *log;
    unsigned long long log_size;
    bool force;
    bool memory_changed;
    hwaddr mem_changed_start_addr;
    hwaddr mem_changed_end_addr;
    const VhostOps *vhost_ops;
    void *opaque;
};

int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type, bool force);
void vhost_dev_cleanup(struct vhost_dev *hdev);
bool vhost_dev_query(struct vhost_dev *hdev, VirtIODevice *vdev);
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
unsigned vhost_get_features(struct vhost_dev *hdev, const int *feature_bits,
        unsigned features);
void vhost_ack_features(struct vhost_dev *hdev, const int *feature_bits,
        unsigned features);
#endif
