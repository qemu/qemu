#ifndef VHOST_H
#define VHOST_H

#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio.h"
#include "exec/memory.h"

#define VHOST_F_DEVICE_IOTLB 63
#define VHOST_USER_F_PROTOCOL_FEATURES 30

/* Generic structures common for any vhost based device. */

struct vhost_inflight {
    int fd;
    void *addr;
    uint64_t size;
    uint64_t offset;
    uint16_t queue_size;
};

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
    EventNotifier error_notifier;
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

typedef struct VhostDevConfigOps {
    /* Vhost device config space changed callback
     */
    int (*vhost_dev_config_notifier)(struct vhost_dev *dev);
} VhostDevConfigOps;

struct vhost_memory;

/**
 * struct vhost_dev - common vhost_dev structure
 * @vhost_ops: backend specific ops
 * @config_ops: ops for config changes (see @vhost_dev_set_config_notifier)
 */
struct vhost_dev {
    VirtIODevice *vdev;
    MemoryListener memory_listener;
    MemoryListener iommu_listener;
    struct vhost_memory *mem;
    int n_mem_sections;
    MemoryRegionSection *mem_sections;
    int n_tmp_sections;
    MemoryRegionSection *tmp_sections;
    struct vhost_virtqueue *vqs;
    unsigned int nvqs;
    /* the first virtqueue which would be used by this vhost dev */
    int vq_index;
    /* one past the last vq index for the virtio device (not vhost) */
    int vq_index_end;
    /* if non-zero, minimum required value for max_queues */
    int num_queues;
    uint64_t features;
    /** @acked_features: final set of negotiated features */
    uint64_t acked_features;
    /** @backend_features: backend specific feature bits */
    uint64_t backend_features;
    /** @protocol_features: final negotiated protocol features */
    uint64_t protocol_features;
    uint64_t max_queues;
    uint64_t backend_cap;
    /* @started: is the vhost device started? */
    bool started;
    bool log_enabled;
    uint64_t log_size;
    Error *migration_blocker;
    const VhostOps *vhost_ops;
    void *opaque;
    struct vhost_log *log;
    QLIST_ENTRY(vhost_dev) entry;
    QLIST_HEAD(, vhost_iommu) iommu_list;
    IOMMUNotifier n;
    const VhostDevConfigOps *config_ops;
};

extern const VhostOps kernel_ops;
extern const VhostOps user_ops;
extern const VhostOps vdpa_ops;

struct vhost_net {
    struct vhost_dev dev;
    struct vhost_virtqueue vqs[2];
    int backend;
    NetClientState *nc;
};

/**
 * vhost_dev_init() - initialise the vhost interface
 * @hdev: the common vhost_dev structure
 * @opaque: opaque ptr passed to backend (vhost/vhost-user/vdpa)
 * @backend_type: type of backend
 * @busyloop_timeout: timeout for polling virtqueue
 * @errp: error handle
 *
 * The initialisation of the vhost device will trigger the
 * initialisation of the backend and potentially capability
 * negotiation of backend interface. Configuration of the VirtIO
 * itself won't happen until the interface is started.
 *
 * Return: 0 on success, non-zero on error while setting errp.
 */
int vhost_dev_init(struct vhost_dev *hdev, void *opaque,
                   VhostBackendType backend_type,
                   uint32_t busyloop_timeout, Error **errp);

/**
 * vhost_dev_cleanup() - tear down and cleanup vhost interface
 * @hdev: the common vhost_dev structure
 */
void vhost_dev_cleanup(struct vhost_dev *hdev);

/**
 * vhost_dev_enable_notifiers() - enable event notifiers
 * @hdev: common vhost_dev structure
 * @vdev: the VirtIODevice structure
 *
 * Enable notifications directly to the vhost device rather than being
 * triggered by QEMU itself. Notifications should be enabled before
 * the vhost device is started via @vhost_dev_start.
 *
 * Return: 0 on success, < 0 on error.
 */
int vhost_dev_enable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev);

/**
 * vhost_dev_disable_notifiers - disable event notifications
 * @hdev: common vhost_dev structure
 * @vdev: the VirtIODevice structure
 *
 * Disable direct notifications to vhost device.
 */
void vhost_dev_disable_notifiers(struct vhost_dev *hdev, VirtIODevice *vdev);

/**
 * vhost_dev_is_started() - report status of vhost device
 * @hdev: common vhost_dev structure
 *
 * Return the started status of the vhost device
 */
static inline bool vhost_dev_is_started(struct vhost_dev *hdev)
{
    return hdev->started;
}

/**
 * vhost_dev_start() - start the vhost device
 * @hdev: common vhost_dev structure
 * @vdev: the VirtIODevice structure
 *
 * Starts the vhost device. From this point VirtIO feature negotiation
 * can start and the device can start processing VirtIO transactions.
 *
 * Return: 0 on success, < 0 on error.
 */
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev);

/**
 * vhost_dev_stop() - stop the vhost device
 * @hdev: common vhost_dev structure
 * @vdev: the VirtIODevice structure
 *
 * Stop the vhost device. After the device is stopped the notifiers
 * can be disabled (@vhost_dev_disable_notifiers) and the device can
 * be torn down (@vhost_dev_cleanup).
 */
void vhost_dev_stop(struct vhost_dev *hdev, VirtIODevice *vdev);

/**
 * DOC: vhost device configuration handling
 *
 * The VirtIO device configuration space is used for rarely changing
 * or initialisation time parameters. The configuration can be updated
 * by either the guest driver or the device itself. If the device can
 * change the configuration over time the vhost handler should
 * register a @VhostDevConfigOps structure with
 * @vhost_dev_set_config_notifier so the guest can be notified. Some
 * devices register a handler anyway and will signal an error if an
 * unexpected config change happens.
 */

/**
 * vhost_dev_get_config() - fetch device configuration
 * @hdev: common vhost_dev_structure
 * @config: pointer to device appropriate config structure
 * @config_len: size of device appropriate config structure
 *
 * Return: 0 on success, < 0 on error while setting errp
 */
int vhost_dev_get_config(struct vhost_dev *hdev, uint8_t *config,
                         uint32_t config_len, Error **errp);

/**
 * vhost_dev_set_config() - set device configuration
 * @hdev: common vhost_dev_structure
 * @data: pointer to data to set
 * @offset: offset into configuration space
 * @size: length of set
 * @flags: @VhostSetConfigType flags
 *
 * By use of @offset/@size a subset of the configuration space can be
 * written to. The @flags are used to indicate if it is a normal
 * transaction or related to migration.
 *
 * Return: 0 on success, non-zero on error
 */
int vhost_dev_set_config(struct vhost_dev *dev, const uint8_t *data,
                         uint32_t offset, uint32_t size, uint32_t flags);

/**
 * vhost_dev_set_config_notifier() - register VhostDevConfigOps
 * @hdev: common vhost_dev_structure
 * @ops: notifier ops
 *
 * If the device is expected to change configuration a notifier can be
 * setup to handle the case.
 */
void vhost_dev_set_config_notifier(struct vhost_dev *dev,
                                   const VhostDevConfigOps *ops);


/* Test and clear masked event pending status.
 * Should be called after unmask to avoid losing events.
 */
bool vhost_virtqueue_pending(struct vhost_dev *hdev, int n);

/* Mask/unmask events from this vq.
 */
void vhost_virtqueue_mask(struct vhost_dev *hdev, VirtIODevice *vdev, int n,
                          bool mask);

/**
 * vhost_get_features() - return a sanitised set of feature bits
 * @hdev: common vhost_dev structure
 * @feature_bits: pointer to terminated table of feature bits
 * @features: original feature set
 *
 * This returns a set of features bits that is an intersection of what
 * is supported by the vhost backend (hdev->features), the supported
 * feature_bits and the requested feature set.
 */
uint64_t vhost_get_features(struct vhost_dev *hdev, const int *feature_bits,
                            uint64_t features);

/**
 * vhost_ack_features() - set vhost acked_features
 * @hdev: common vhost_dev structure
 * @feature_bits: pointer to terminated table of feature bits
 * @features: requested feature set
 *
 * This sets the internal hdev->acked_features to the intersection of
 * the backends advertised features and the supported feature_bits.
 */
void vhost_ack_features(struct vhost_dev *hdev, const int *feature_bits,
                        uint64_t features);
bool vhost_has_free_slot(void);

int vhost_net_set_backend(struct vhost_dev *hdev,
                          struct vhost_vring_file *file);

int vhost_device_iotlb_miss(struct vhost_dev *dev, uint64_t iova, int write);

int vhost_virtqueue_start(struct vhost_dev *dev, struct VirtIODevice *vdev,
                          struct vhost_virtqueue *vq, unsigned idx);

void vhost_dev_reset_inflight(struct vhost_inflight *inflight);
void vhost_dev_free_inflight(struct vhost_inflight *inflight);
void vhost_dev_save_inflight(struct vhost_inflight *inflight, QEMUFile *f);
int vhost_dev_load_inflight(struct vhost_inflight *inflight, QEMUFile *f);
int vhost_dev_prepare_inflight(struct vhost_dev *hdev, VirtIODevice *vdev);
int vhost_dev_set_inflight(struct vhost_dev *dev,
                           struct vhost_inflight *inflight);
int vhost_dev_get_inflight(struct vhost_dev *dev, uint16_t queue_size,
                           struct vhost_inflight *inflight);
#endif
