#ifndef VHOST_H
#define VHOST_H

#include "net/vhost_net.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio.h"
#include "system/memory.h"

#define VHOST_F_DEVICE_IOTLB 63
#define VHOST_USER_F_PROTOCOL_FEATURES 30

#define VU_REALIZE_CONN_RETRIES 3

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
    EventNotifier masked_config_notifier;
    struct vhost_dev *dev;
};

typedef unsigned long vhost_log_chunk_t;
#define VHOST_LOG_PAGE 0x1000
#define VHOST_LOG_BITS (8 * sizeof(vhost_log_chunk_t))
#define VHOST_LOG_CHUNK (VHOST_LOG_PAGE * VHOST_LOG_BITS)
#define VHOST_INVALID_FEATURE_BIT   (0xff)
#define VHOST_QUEUE_NUM_CONFIG_INR 0

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
    /**
     * vhost feature handling requires matching the feature set
     * offered by a backend which may be a subset of the total
     * features eventually offered to the guest.
     *
     * @features: available features provided by the backend
     * @acked_features: final negotiated features with front-end driver
     *
     * @backend_features: this is used in a couple of places to either
     * store VHOST_USER_F_PROTOCOL_FEATURES to apply to
     * VHOST_USER_SET_FEATURES or VHOST_NET_F_VIRTIO_NET_HDR. Its
     * future use should be discouraged and the variable retired as
     * its easy to confuse with the VirtIO backend_features.
     */
    VIRTIO_DECLARE_FEATURES(features);
    VIRTIO_DECLARE_FEATURES(acked_features);
    VIRTIO_DECLARE_FEATURES(backend_features);

    /**
     * @protocol_features: is the vhost-user only feature set by
     * VHOST_USER_SET_PROTOCOL_FEATURES. Protocol features are only
     * negotiated if VHOST_USER_F_PROTOCOL_FEATURES has been offered
     * by the backend (see @features).
     */
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
    QLIST_ENTRY(vhost_dev) logdev_entry;
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
    const int *feature_bits;
    int max_tx_queue_size;
    SaveAcketFeatures *save_acked_features;
    bool is_vhost_user;
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

void vhost_dev_disable_notifiers_nvqs(struct vhost_dev *hdev,
                                      VirtIODevice *vdev,
                                      unsigned int nvqs);

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
bool vhost_config_pending(struct vhost_dev *hdev);
void vhost_config_mask(struct vhost_dev *hdev, VirtIODevice *vdev, bool mask);

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
 * @vrings: true to have vrings enabled in this call
 *
 * Starts the vhost device. From this point VirtIO feature negotiation
 * can start and the device can start processing VirtIO transactions.
 *
 * Return: 0 on success, < 0 on error.
 */
int vhost_dev_start(struct vhost_dev *hdev, VirtIODevice *vdev, bool vrings);

/**
 * vhost_dev_stop() - stop the vhost device
 * @hdev: common vhost_dev structure
 * @vdev: the VirtIODevice structure
 * @vrings: true to have vrings disabled in this call
 *
 * Stop the vhost device. After the device is stopped the notifiers
 * can be disabled (@vhost_dev_disable_notifiers) and the device can
 * be torn down (@vhost_dev_cleanup).
 *
 * Return: 0 on success, != 0 on error when stopping dev.
 */
int vhost_dev_stop(struct vhost_dev *hdev, VirtIODevice *vdev, bool vrings);

/**
 * vhost_dev_force_stop() - force stop the vhost device
 * @hdev: common vhost_dev structure
 * @vdev: the VirtIODevice structure
 * @vrings: true to have vrings disabled in this call
 *
 * Force stop the vhost device. After the device is stopped the notifiers
 * can be disabled (@vhost_dev_disable_notifiers) and the device can
 * be torn down (@vhost_dev_cleanup). Unlike @vhost_dev_stop, this doesn't
 * attempt to flush in-flight backend requests by skipping GET_VRING_BASE
 * entirely.
 */
int vhost_dev_force_stop(struct vhost_dev *hdev, VirtIODevice *vdev,
                         bool vrings);

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
 * vhost_get_features_ex() - sanitize the extended features set
 * @hdev: common vhost_dev structure
 * @feature_bits: pointer to terminated table of feature bits
 * @features: original features set, filtered out on return
 *
 * This is the extended variant of vhost_get_features(), supporting the
 * the extended features set. Filter it with the intersection of what is
 * supported by the vhost backend (hdev->features) and the supported
 * feature_bits.
 */
void vhost_get_features_ex(struct vhost_dev *hdev,
                           const int *feature_bits,
                           uint64_t *features);
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
static inline uint64_t vhost_get_features(struct vhost_dev *hdev,
                                          const int *feature_bits,
                                          uint64_t features)
{
    uint64_t features_ex[VIRTIO_FEATURES_NU64S];

    virtio_features_from_u64(features_ex, features);
    vhost_get_features_ex(hdev, feature_bits, features_ex);
    return features_ex[0];
}

/**
 * vhost_ack_features_ex() - set vhost full set of acked_features
 * @hdev: common vhost_dev structure
 * @feature_bits: pointer to terminated table of feature bits
 * @features: requested feature set
 *
 * This sets the internal hdev->acked_features to the intersection of
 * the backends advertised features and the supported feature_bits.
 */
void vhost_ack_features_ex(struct vhost_dev *hdev, const int *feature_bits,
                           const uint64_t *features);

/**
 * vhost_ack_features() - set vhost acked_features
 * @hdev: common vhost_dev structure
 * @feature_bits: pointer to terminated table of feature bits
 * @features: requested feature set
 *
 * This sets the internal hdev->acked_features to the intersection of
 * the backends advertised features and the supported feature_bits.
 */
static inline void vhost_ack_features(struct vhost_dev *hdev,
                                      const int *feature_bits,
                                      uint64_t features)
{
    uint64_t features_ex[VIRTIO_FEATURES_NU64S];

    virtio_features_from_u64(features_ex, features);
    vhost_ack_features_ex(hdev, feature_bits, features_ex);
}

unsigned int vhost_get_max_memslots(void);
unsigned int vhost_get_free_memslots(void);

int vhost_net_set_backend(struct vhost_dev *hdev,
                          struct vhost_vring_file *file);

void vhost_toggle_device_iotlb(VirtIODevice *vdev);
int vhost_device_iotlb_miss(struct vhost_dev *dev, uint64_t iova, int write);

int vhost_virtqueue_start(struct vhost_dev *dev, struct VirtIODevice *vdev,
                          struct vhost_virtqueue *vq, unsigned idx);
int vhost_virtqueue_stop(struct vhost_dev *dev, struct VirtIODevice *vdev,
                         struct vhost_virtqueue *vq, unsigned idx);

void vhost_dev_reset_inflight(struct vhost_inflight *inflight);
void vhost_dev_free_inflight(struct vhost_inflight *inflight);
int vhost_dev_prepare_inflight(struct vhost_dev *hdev, VirtIODevice *vdev);
int vhost_dev_set_inflight(struct vhost_dev *dev,
                           struct vhost_inflight *inflight);
int vhost_dev_get_inflight(struct vhost_dev *dev, uint16_t queue_size,
                           struct vhost_inflight *inflight);
bool vhost_dev_has_iommu(struct vhost_dev *dev);

#ifdef CONFIG_VHOST
int vhost_reset_device(struct vhost_dev *hdev);
#else
static inline int vhost_reset_device(struct vhost_dev *hdev)
{
    return -ENOSYS;
}
#endif /* CONFIG_VHOST */

/**
 * vhost_supports_device_state(): Checks whether the back-end supports
 * transferring internal device state for the purpose of migration.
 * Support for this feature is required for vhost_set_device_state_fd()
 * and vhost_check_device_state().
 *
 * @dev: The vhost device
 *
 * Returns true if the device supports these commands, and false if it
 * does not.
 */
#ifdef CONFIG_VHOST
bool vhost_supports_device_state(struct vhost_dev *dev);
#else
static inline bool vhost_supports_device_state(struct vhost_dev *dev)
{
    return false;
}
#endif

/**
 * vhost_set_device_state_fd(): Begin transfer of internal state from/to
 * the back-end for the purpose of migration.  Data is to be transferred
 * over a pipe according to @direction and @phase.  The sending end must
 * only write to the pipe, and the receiving end must only read from it.
 * Once the sending end is done, it closes its FD.  The receiving end
 * must take this as the end-of-transfer signal and close its FD, too.
 *
 * @fd is the back-end's end of the pipe: The write FD for SAVE, and the
 * read FD for LOAD.  This function transfers ownership of @fd to the
 * back-end, i.e. closes it in the front-end.
 *
 * The back-end may optionally reply with an FD of its own, if this
 * improves efficiency on its end.  In this case, the returned FD is
 * stored in *reply_fd.  The back-end will discard the FD sent to it,
 * and the front-end must use *reply_fd for transferring state to/from
 * the back-end.
 *
 * @dev: The vhost device
 * @direction: The direction in which the state is to be transferred.
 *             For outgoing migrations, this is SAVE, and data is read
 *             from the back-end and stored by the front-end in the
 *             migration stream.
 *             For incoming migrations, this is LOAD, and data is read
 *             by the front-end from the migration stream and sent to
 *             the back-end to restore the saved state.
 * @phase: Which migration phase we are in.  Currently, there is only
 *         STOPPED (device and all vrings are stopped), in the future,
 *         more phases such as PRE_COPY or POST_COPY may be added.
 * @fd: Back-end's end of the pipe through which to transfer state; note
 *      that ownership is transferred to the back-end, so this function
 *      closes @fd in the front-end.
 * @reply_fd: If the back-end wishes to use a different pipe for state
 *            transfer, this will contain an FD for the front-end to
 *            use.  Otherwise, -1 is stored here.
 * @errp: Potential error description
 *
 * Returns 0 on success, and -errno on failure.
 */
int vhost_set_device_state_fd(struct vhost_dev *dev,
                              VhostDeviceStateDirection direction,
                              VhostDeviceStatePhase phase,
                              int fd,
                              int *reply_fd,
                              Error **errp);

/**
 * vhost_set_device_state_fd(): After transferring state from/to the
 * back-end via vhost_set_device_state_fd(), i.e. once the sending end
 * has closed the pipe, inquire the back-end to report any potential
 * errors that have occurred on its side.  This allows to sense errors
 * like:
 * - During outgoing migration, when the source side had already started
 *   to produce its state, something went wrong and it failed to finish
 * - During incoming migration, when the received state is somehow
 *   invalid and cannot be processed by the back-end
 *
 * @dev: The vhost device
 * @errp: Potential error description
 *
 * Returns 0 when the back-end reports successful state transfer and
 * processing, and -errno when an error occurred somewhere.
 */
int vhost_check_device_state(struct vhost_dev *dev, Error **errp);

/**
 * vhost_save_backend_state(): High-level function to receive a vhost
 * back-end's state, and save it in @f.  Uses
 * `vhost_set_device_state_fd()` to get the data from the back-end, and
 * stores it in consecutive chunks that are each prefixed by their
 * respective length (be32).  The end is marked by a 0-length chunk.
 *
 * Must only be called while the device and all its vrings are stopped
 * (`VHOST_TRANSFER_STATE_PHASE_STOPPED`).
 *
 * @dev: The vhost device from which to save the state
 * @f: Migration stream in which to save the state
 * @errp: Potential error message
 *
 * Returns 0 on success, and -errno otherwise.
 */
#ifdef CONFIG_VHOST
int vhost_save_backend_state(struct vhost_dev *dev, QEMUFile *f, Error **errp);
#else
static inline int vhost_save_backend_state(struct vhost_dev *dev, QEMUFile *f,
                                           Error **errp)
{
    return -ENOSYS;
}
#endif

/**
 * vhost_load_backend_state(): High-level function to load a vhost
 * back-end's state from @f, and send it over to the back-end.  Reads
 * the data from @f in the format used by `vhost_save_state()`, and uses
 * `vhost_set_device_state_fd()` to transfer it to the back-end.
 *
 * Must only be called while the device and all its vrings are stopped
 * (`VHOST_TRANSFER_STATE_PHASE_STOPPED`).
 *
 * @dev: The vhost device to which to send the state
 * @f: Migration stream from which to load the state
 * @errp: Potential error message
 *
 * Returns 0 on success, and -errno otherwise.
 */
#ifdef CONFIG_VHOST
int vhost_load_backend_state(struct vhost_dev *dev, QEMUFile *f, Error **errp);
#else
static inline int vhost_load_backend_state(struct vhost_dev *dev, QEMUFile *f,
                                           Error **errp)
{
    return -ENOSYS;
}
#endif

#endif
