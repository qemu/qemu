/*
 * Virtio Support
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_H
#define QEMU_VIRTIO_H

#include "exec/memory.h"
#include "hw/qdev-core.h"
#include "net/net.h"
#include "migration/vmstate.h"
#include "qemu/event_notifier.h"
#include "standard-headers/linux/virtio_config.h"
#include "standard-headers/linux/virtio_ring.h"
#include "qom/object.h"

/*
 * A guest should never accept this. It implies negotiation is broken
 * between the driver frontend and the device. This bit is re-used for
 * vhost-user to advertise VHOST_USER_F_PROTOCOL_FEATURES between QEMU
 * and a vhost-user backend.
 */
#define VIRTIO_F_BAD_FEATURE 30

#define VIRTIO_LEGACY_FEATURES ((0x1ULL << VIRTIO_F_BAD_FEATURE) | \
                                (0x1ULL << VIRTIO_F_NOTIFY_ON_EMPTY) | \
                                (0x1ULL << VIRTIO_F_ANY_LAYOUT))

struct VirtQueue;

static inline hwaddr vring_align(hwaddr addr,
                                             unsigned long align)
{
    return QEMU_ALIGN_UP(addr, align);
}

typedef struct VirtIOFeature {
    uint64_t flags;
    size_t end;
} VirtIOFeature;

typedef struct VirtIOConfigSizeParams {
    size_t min_size;
    size_t max_size;
    const VirtIOFeature *feature_sizes;
} VirtIOConfigSizeParams;

size_t virtio_get_config_size(const VirtIOConfigSizeParams *params,
                              uint64_t host_features);

typedef struct VirtQueue VirtQueue;

#define VIRTQUEUE_MAX_SIZE 1024

typedef struct VirtQueueElement
{
    unsigned int index;
    unsigned int len;
    unsigned int ndescs;
    unsigned int out_num;
    unsigned int in_num;
    hwaddr *in_addr;
    hwaddr *out_addr;
    struct iovec *in_sg;
    struct iovec *out_sg;
} VirtQueueElement;

#define VIRTIO_QUEUE_MAX 1024

#define VIRTIO_NO_VECTOR 0xffff

/* special index value used internally for config irqs */
#define VIRTIO_CONFIG_IRQ_IDX -1

#define TYPE_VIRTIO_DEVICE "virtio-device"
OBJECT_DECLARE_TYPE(VirtIODevice, VirtioDeviceClass, VIRTIO_DEVICE)

typedef struct {
    int virtio_bit;
    const char *feature_desc;
} qmp_virtio_feature_map_t;

enum virtio_device_endian {
    VIRTIO_DEVICE_ENDIAN_UNKNOWN,
    VIRTIO_DEVICE_ENDIAN_LITTLE,
    VIRTIO_DEVICE_ENDIAN_BIG,
};

/**
 * struct VirtIODevice - common VirtIO structure
 * @name: name of the device
 * @status: VirtIO Device Status field
 *
 */
struct VirtIODevice
{
    DeviceState parent_obj;
    const char *name;
    uint8_t status;
    uint8_t isr;
    uint16_t queue_sel;
    /**
     * These fields represent a set of VirtIO features at various
     * levels of the stack. @host_features indicates the complete
     * feature set the VirtIO device can offer to the driver.
     * @guest_features indicates which features the VirtIO driver has
     * selected by writing to the feature register. Finally
     * @backend_features represents everything supported by the
     * backend (e.g. vhost) and could potentially be a subset of the
     * total feature set offered by QEMU.
     */
    uint64_t host_features;
    uint64_t guest_features;
    uint64_t backend_features;

    size_t config_len;
    void *config;
    uint16_t config_vector;
    uint32_t generation;
    int nvectors;
    VirtQueue *vq;
    MemoryListener listener;
    uint16_t device_id;
    /* @vm_running: current VM running state via virtio_vmstate_change() */
    bool vm_running;
    bool broken; /* device in invalid state, needs reset */
    bool use_disabled_flag; /* allow use of 'disable' flag when needed */
    bool disabled; /* device in temporarily disabled state */
    /**
     * @use_started: true if the @started flag should be used to check the
     * current state of the VirtIO device. Otherwise status bits
     * should be checked for a current status of the device.
     * @use_started is only set via QMP and defaults to true for all
     * modern machines (since 4.1).
     */
    bool use_started;
    bool started;
    bool start_on_kick; /* when virtio 1.0 feature has not been negotiated */
    bool disable_legacy_check;
    bool vhost_started;
    VMChangeStateEntry *vmstate;
    char *bus_name;
    uint8_t device_endian;
    bool use_guest_notifier_mask;
    AddressSpace *dma_as;
    QLIST_HEAD(, VirtQueue) *vector_queues;
    QTAILQ_ENTRY(VirtIODevice) next;
    EventNotifier config_notifier;
    bool device_iotlb_enabled;
};

struct VirtioDeviceClass {
    /*< private >*/
    DeviceClass parent;
    /*< public >*/

    /* This is what a VirtioDevice must implement */
    DeviceRealize realize;
    DeviceUnrealize unrealize;
    uint64_t (*get_features)(VirtIODevice *vdev,
                             uint64_t requested_features,
                             Error **errp);
    uint64_t (*bad_features)(VirtIODevice *vdev);
    void (*set_features)(VirtIODevice *vdev, uint64_t val);
    int (*validate_features)(VirtIODevice *vdev);
    void (*get_config)(VirtIODevice *vdev, uint8_t *config);
    void (*set_config)(VirtIODevice *vdev, const uint8_t *config);
    void (*reset)(VirtIODevice *vdev);
    void (*set_status)(VirtIODevice *vdev, uint8_t val);
    /* Device must validate queue_index.  */
    void (*queue_reset)(VirtIODevice *vdev, uint32_t queue_index);
    /* Device must validate queue_index.  */
    void (*queue_enable)(VirtIODevice *vdev, uint32_t queue_index);
    /* For transitional devices, this is a bitmap of features
     * that are only exposed on the legacy interface but not
     * the modern one.
     */
    uint64_t legacy_features;
    /* Test and clear event pending status.
     * Should be called after unmask to avoid losing events.
     * If backend does not support masking,
     * must check in frontend instead.
     */
    bool (*guest_notifier_pending)(VirtIODevice *vdev, int n);
    /* Mask/unmask events from this vq. Any events reported
     * while masked will become pending.
     * If backend does not support masking,
     * must mask in frontend instead.
     */
    void (*guest_notifier_mask)(VirtIODevice *vdev, int n, bool mask);
    int (*start_ioeventfd)(VirtIODevice *vdev);
    void (*stop_ioeventfd)(VirtIODevice *vdev);
    /* Saving and loading of a device; trying to deprecate save/load
     * use vmsd for new devices.
     */
    void (*save)(VirtIODevice *vdev, QEMUFile *f);
    int (*load)(VirtIODevice *vdev, QEMUFile *f, int version_id);
    /* Post load hook in vmsd is called early while device is processed, and
     * when VirtIODevice isn't fully initialized.  Devices should use this instead,
     * unless they specifically want to verify the migration stream as it's
     * processed, e.g. for bounds checking.
     */
    int (*post_load)(VirtIODevice *vdev);
    const VMStateDescription *vmsd;
    bool (*primary_unplug_pending)(void *opaque);
    struct vhost_dev *(*get_vhost)(VirtIODevice *vdev);
    void (*toggle_device_iotlb)(VirtIODevice *vdev);
};

void virtio_instance_init_common(Object *proxy_obj, void *data,
                                 size_t vdev_size, const char *vdev_name);

void virtio_init(VirtIODevice *vdev, uint16_t device_id, size_t config_size);

void virtio_cleanup(VirtIODevice *vdev);

void virtio_error(VirtIODevice *vdev, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

/* Set the child bus name. */
void virtio_device_set_child_bus_name(VirtIODevice *vdev, char *bus_name);

typedef void (*VirtIOHandleOutput)(VirtIODevice *, VirtQueue *);

VirtQueue *virtio_add_queue(VirtIODevice *vdev, int queue_size,
                            VirtIOHandleOutput handle_output);

void virtio_del_queue(VirtIODevice *vdev, int n);

void virtio_delete_queue(VirtQueue *vq);

void virtqueue_push(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len);
void virtqueue_flush(VirtQueue *vq, unsigned int count);
void virtqueue_detach_element(VirtQueue *vq, const VirtQueueElement *elem,
                              unsigned int len);
void virtqueue_unpop(VirtQueue *vq, const VirtQueueElement *elem,
                     unsigned int len);
bool virtqueue_rewind(VirtQueue *vq, unsigned int num);
void virtqueue_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx);

void virtqueue_map(VirtIODevice *vdev, VirtQueueElement *elem);
void *virtqueue_pop(VirtQueue *vq, size_t sz);
unsigned int virtqueue_drop_all(VirtQueue *vq);
void *qemu_get_virtqueue_element(VirtIODevice *vdev, QEMUFile *f, size_t sz);
void qemu_put_virtqueue_element(VirtIODevice *vdev, QEMUFile *f,
                                VirtQueueElement *elem);
int virtqueue_avail_bytes(VirtQueue *vq, unsigned int in_bytes,
                          unsigned int out_bytes);
void virtqueue_get_avail_bytes(VirtQueue *vq, unsigned int *in_bytes,
                               unsigned int *out_bytes,
                               unsigned max_in_bytes, unsigned max_out_bytes);

void virtio_notify_irqfd(VirtIODevice *vdev, VirtQueue *vq);
void virtio_notify(VirtIODevice *vdev, VirtQueue *vq);

int virtio_save(VirtIODevice *vdev, QEMUFile *f);

extern const VMStateInfo virtio_vmstate_info;

#define VMSTATE_VIRTIO_DEVICE \
    {                                         \
        .name = "virtio",                     \
        .info = &virtio_vmstate_info,         \
        .flags = VMS_SINGLE,                  \
    }

int virtio_load(VirtIODevice *vdev, QEMUFile *f, int version_id);

void virtio_notify_config(VirtIODevice *vdev);

bool virtio_queue_get_notification(VirtQueue *vq);
void virtio_queue_set_notification(VirtQueue *vq, int enable);

int virtio_queue_ready(VirtQueue *vq);

int virtio_queue_empty(VirtQueue *vq);

/* Host binding interface.  */

uint32_t virtio_config_readb(VirtIODevice *vdev, uint32_t addr);
uint32_t virtio_config_readw(VirtIODevice *vdev, uint32_t addr);
uint32_t virtio_config_readl(VirtIODevice *vdev, uint32_t addr);
void virtio_config_writeb(VirtIODevice *vdev, uint32_t addr, uint32_t data);
void virtio_config_writew(VirtIODevice *vdev, uint32_t addr, uint32_t data);
void virtio_config_writel(VirtIODevice *vdev, uint32_t addr, uint32_t data);
uint32_t virtio_config_modern_readb(VirtIODevice *vdev, uint32_t addr);
uint32_t virtio_config_modern_readw(VirtIODevice *vdev, uint32_t addr);
uint32_t virtio_config_modern_readl(VirtIODevice *vdev, uint32_t addr);
void virtio_config_modern_writeb(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data);
void virtio_config_modern_writew(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data);
void virtio_config_modern_writel(VirtIODevice *vdev,
                                 uint32_t addr, uint32_t data);
void virtio_queue_set_addr(VirtIODevice *vdev, int n, hwaddr addr);
hwaddr virtio_queue_get_addr(VirtIODevice *vdev, int n);
void virtio_queue_set_num(VirtIODevice *vdev, int n, int num);
int virtio_queue_get_num(VirtIODevice *vdev, int n);
int virtio_queue_get_max_num(VirtIODevice *vdev, int n);
int virtio_get_num_queues(VirtIODevice *vdev);
void virtio_queue_set_rings(VirtIODevice *vdev, int n, hwaddr desc,
                            hwaddr avail, hwaddr used);
void virtio_queue_update_rings(VirtIODevice *vdev, int n);
void virtio_init_region_cache(VirtIODevice *vdev, int n);
void virtio_queue_set_align(VirtIODevice *vdev, int n, int align);
void virtio_queue_notify(VirtIODevice *vdev, int n);
uint16_t virtio_queue_vector(VirtIODevice *vdev, int n);
void virtio_queue_set_vector(VirtIODevice *vdev, int n, uint16_t vector);
int virtio_queue_set_host_notifier_mr(VirtIODevice *vdev, int n,
                                      MemoryRegion *mr, bool assign);
int virtio_set_status(VirtIODevice *vdev, uint8_t val);
void virtio_reset(void *opaque);
void virtio_queue_reset(VirtIODevice *vdev, uint32_t queue_index);
void virtio_queue_enable(VirtIODevice *vdev, uint32_t queue_index);
void virtio_update_irq(VirtIODevice *vdev);
int virtio_set_features(VirtIODevice *vdev, uint64_t val);

/* Base devices.  */
typedef struct VirtIOBlkConf VirtIOBlkConf;
struct virtio_net_conf;
typedef struct virtio_serial_conf virtio_serial_conf;
typedef struct virtio_input_conf virtio_input_conf;
typedef struct VirtIOSCSIConf VirtIOSCSIConf;
typedef struct VirtIORNGConf VirtIORNGConf;

#define DEFINE_VIRTIO_COMMON_FEATURES(_state, _field) \
    DEFINE_PROP_BIT64("indirect_desc", _state, _field,    \
                      VIRTIO_RING_F_INDIRECT_DESC, true), \
    DEFINE_PROP_BIT64("event_idx", _state, _field,        \
                      VIRTIO_RING_F_EVENT_IDX, true),     \
    DEFINE_PROP_BIT64("notify_on_empty", _state, _field,  \
                      VIRTIO_F_NOTIFY_ON_EMPTY, true), \
    DEFINE_PROP_BIT64("any_layout", _state, _field, \
                      VIRTIO_F_ANY_LAYOUT, true), \
    DEFINE_PROP_BIT64("iommu_platform", _state, _field, \
                      VIRTIO_F_IOMMU_PLATFORM, false), \
    DEFINE_PROP_BIT64("packed", _state, _field, \
                      VIRTIO_F_RING_PACKED, false), \
    DEFINE_PROP_BIT64("queue_reset", _state, _field, \
                      VIRTIO_F_RING_RESET, true)

hwaddr virtio_queue_get_desc_addr(VirtIODevice *vdev, int n);
bool virtio_queue_enabled_legacy(VirtIODevice *vdev, int n);
bool virtio_queue_enabled(VirtIODevice *vdev, int n);
hwaddr virtio_queue_get_avail_addr(VirtIODevice *vdev, int n);
hwaddr virtio_queue_get_used_addr(VirtIODevice *vdev, int n);
hwaddr virtio_queue_get_desc_size(VirtIODevice *vdev, int n);
hwaddr virtio_queue_get_avail_size(VirtIODevice *vdev, int n);
hwaddr virtio_queue_get_used_size(VirtIODevice *vdev, int n);
unsigned int virtio_queue_get_last_avail_idx(VirtIODevice *vdev, int n);
void virtio_queue_set_last_avail_idx(VirtIODevice *vdev, int n,
                                     unsigned int idx);
void virtio_queue_restore_last_avail_idx(VirtIODevice *vdev, int n);
void virtio_queue_invalidate_signalled_used(VirtIODevice *vdev, int n);
void virtio_queue_update_used_idx(VirtIODevice *vdev, int n);
VirtQueue *virtio_get_queue(VirtIODevice *vdev, int n);
uint16_t virtio_get_queue_index(VirtQueue *vq);
EventNotifier *virtio_queue_get_guest_notifier(VirtQueue *vq);
void virtio_queue_set_guest_notifier_fd_handler(VirtQueue *vq, bool assign,
                                                bool with_irqfd);
int virtio_device_start_ioeventfd(VirtIODevice *vdev);
int virtio_device_grab_ioeventfd(VirtIODevice *vdev);
void virtio_device_release_ioeventfd(VirtIODevice *vdev);
bool virtio_device_ioeventfd_enabled(VirtIODevice *vdev);
EventNotifier *virtio_queue_get_host_notifier(VirtQueue *vq);
void virtio_queue_set_host_notifier_enabled(VirtQueue *vq, bool enabled);
void virtio_queue_host_notifier_read(EventNotifier *n);
void virtio_queue_aio_attach_host_notifier(VirtQueue *vq, AioContext *ctx);
void virtio_queue_aio_attach_host_notifier_no_poll(VirtQueue *vq, AioContext *ctx);
void virtio_queue_aio_detach_host_notifier(VirtQueue *vq, AioContext *ctx);
VirtQueue *virtio_vector_first_queue(VirtIODevice *vdev, uint16_t vector);
VirtQueue *virtio_vector_next_queue(VirtQueue *vq);
EventNotifier *virtio_config_get_guest_notifier(VirtIODevice *vdev);
void virtio_config_set_guest_notifier_fd_handler(VirtIODevice *vdev,
                                                 bool assign, bool with_irqfd);

static inline void virtio_add_feature(uint64_t *features, unsigned int fbit)
{
    assert(fbit < 64);
    *features |= (1ULL << fbit);
}

static inline void virtio_clear_feature(uint64_t *features, unsigned int fbit)
{
    assert(fbit < 64);
    *features &= ~(1ULL << fbit);
}

static inline bool virtio_has_feature(uint64_t features, unsigned int fbit)
{
    assert(fbit < 64);
    return !!(features & (1ULL << fbit));
}

static inline bool virtio_vdev_has_feature(VirtIODevice *vdev,
                                           unsigned int fbit)
{
    return virtio_has_feature(vdev->guest_features, fbit);
}

static inline bool virtio_host_has_feature(VirtIODevice *vdev,
                                           unsigned int fbit)
{
    return virtio_has_feature(vdev->host_features, fbit);
}

static inline bool virtio_is_big_endian(VirtIODevice *vdev)
{
    if (!virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        assert(vdev->device_endian != VIRTIO_DEVICE_ENDIAN_UNKNOWN);
        return vdev->device_endian == VIRTIO_DEVICE_ENDIAN_BIG;
    }
    /* Devices conforming to VIRTIO 1.0 or later are always LE. */
    return false;
}

/**
 * virtio_device_started() - check if device started
 * @vdev - the VirtIO device
 * @status - the devices status bits
 *
 * Check if the device is started. For most modern machines this is
 * tracked via the @vdev->started field (to support migration),
 * otherwise we check for the final negotiated status bit that
 * indicates everything is ready.
 */
static inline bool virtio_device_started(VirtIODevice *vdev, uint8_t status)
{
    if (vdev->use_started) {
        return vdev->started;
    }

    return status & VIRTIO_CONFIG_S_DRIVER_OK;
}

/**
 * virtio_device_should_start() - check if device startable
 * @vdev - the VirtIO device
 * @status - the devices status bits
 *
 * This is similar to virtio_device_started() but also encapsulates a
 * check on the VM status which would prevent a device starting
 * anyway.
 */
static inline bool virtio_device_should_start(VirtIODevice *vdev, uint8_t status)
{
    if (!vdev->vm_running) {
        return false;
    }

    return virtio_device_started(vdev, status);
}

static inline void virtio_set_started(VirtIODevice *vdev, bool started)
{
    if (started) {
        vdev->start_on_kick = false;
    }

    if (vdev->use_started) {
        vdev->started = started;
    }
}

static inline void virtio_set_disabled(VirtIODevice *vdev, bool disable)
{
    if (vdev->use_disabled_flag) {
        vdev->disabled = disable;
    }
}

static inline bool virtio_device_disabled(VirtIODevice *vdev)
{
    return unlikely(vdev->disabled || vdev->broken);
}

bool virtio_legacy_allowed(VirtIODevice *vdev);
bool virtio_legacy_check_disabled(VirtIODevice *vdev);

#endif
