#ifndef VHOST_H
#define VHOST_H

#include "hw/hw.h"
#include "hw/virtio.h"

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
};

typedef unsigned long vhost_log_chunk_t;
#define VHOST_LOG_PAGE 0x1000
#define VHOST_LOG_BITS (8 * sizeof(vhost_log_chunk_t))
#define VHOST_LOG_CHUNK (VHOST_LOG_PAGE * VHOST_LOG_BITS)

struct vhost_memory;
struct vhost_dev {
    CPUPhysMemoryClient client;
    int control;
    struct vhost_memory *mem;
    struct vhost_virtqueue *vqs;
    int nvqs;
    unsigned long long features;
    unsigned long long acked_features;
    unsigned long long backend_features;
    bool started;
    bool log_enabled;
    vhost_log_chunk_t *log;
    unsigned long long log_size;
    bool force;
};

int vhost_dev_init(struct vhost_dev *hdev, int devfd, bool force);
void vhost_dev_cleanup(struct vhost_dev *hdev);
bool vhost_dev_query(struct vhost_dev *hdev, VirtIODevice *vdev);
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev);
void vhost_dev_stop(struct vhost_dev *hdev, VirtIODevice *vdev);

#endif
