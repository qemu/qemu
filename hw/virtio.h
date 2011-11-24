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

#ifndef _QEMU_VIRTIO_H
#define _QEMU_VIRTIO_H

#include "hw.h"
#include "net.h"
#include "qdev.h"
#include "sysemu.h"
#include "block.h"
#include "event_notifier.h"
#ifdef CONFIG_LINUX
#include "9p.h"
#endif

/* from Linux's linux/virtio_config.h */

/* Status byte for guest to report progress, and synchronize features. */
/* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
#define VIRTIO_CONFIG_S_ACKNOWLEDGE     1
/* We have found a driver for the device. */
#define VIRTIO_CONFIG_S_DRIVER          2
/* Driver has used its parts of the config, and is happy */
#define VIRTIO_CONFIG_S_DRIVER_OK       4
/* We've given up on this device. */
#define VIRTIO_CONFIG_S_FAILED          0x80

/* Some virtio feature bits (currently bits 28 through 31) are reserved for the
 * transport being used (eg. virtio_ring), the rest are per-device feature bits. */
#define VIRTIO_TRANSPORT_F_START        28
#define VIRTIO_TRANSPORT_F_END          32

/* We notify when the ring is completely used, even if the guest is suppressing
 * callbacks */
#define VIRTIO_F_NOTIFY_ON_EMPTY        24
/* We support indirect buffer descriptors */
#define VIRTIO_RING_F_INDIRECT_DESC     28
/* The Guest publishes the used index for which it expects an interrupt
 * at the end of the avail ring. Host should ignore the avail->flags field. */
/* The Host publishes the avail index for which it expects a kick
 * at the end of the used ring. Guest should ignore the used->flags field. */
#define VIRTIO_RING_F_EVENT_IDX         29
/* A guest should never accept this.  It implies negotiation is broken. */
#define VIRTIO_F_BAD_FEATURE		30

/* from Linux's linux/virtio_ring.h */

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT       1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE      2
/* This means the buffer contains a list of buffer descriptors. */
#define VRING_DESC_F_INDIRECT  4

/* This means don't notify other side when buffer added. */
#define VRING_USED_F_NO_NOTIFY  1
/* This means don't interrupt guest when buffer consumed. */
#define VRING_AVAIL_F_NO_INTERRUPT      1

struct VirtQueue;

static inline target_phys_addr_t vring_align(target_phys_addr_t addr,
                                             unsigned long align)
{
    return (addr + align - 1) & ~(align - 1);
}

typedef struct VirtQueue VirtQueue;

#define VIRTQUEUE_MAX_SIZE 1024

typedef struct VirtQueueElement
{
    unsigned int index;
    unsigned int out_num;
    unsigned int in_num;
    target_phys_addr_t in_addr[VIRTQUEUE_MAX_SIZE];
    target_phys_addr_t out_addr[VIRTQUEUE_MAX_SIZE];
    struct iovec in_sg[VIRTQUEUE_MAX_SIZE];
    struct iovec out_sg[VIRTQUEUE_MAX_SIZE];
} VirtQueueElement;

typedef struct {
    void (*notify)(void * opaque, uint16_t vector);
    void (*save_config)(void * opaque, QEMUFile *f);
    void (*save_queue)(void * opaque, int n, QEMUFile *f);
    int (*load_config)(void * opaque, QEMUFile *f);
    int (*load_queue)(void * opaque, int n, QEMUFile *f);
    int (*load_done)(void * opaque, QEMUFile *f);
    unsigned (*get_features)(void * opaque);
    bool (*query_guest_notifiers)(void * opaque);
    int (*set_guest_notifiers)(void * opaque, bool assigned);
    int (*set_host_notifier)(void * opaque, int n, bool assigned);
    void (*vmstate_change)(void * opaque, bool running);
} VirtIOBindings;

#define VIRTIO_PCI_QUEUE_MAX 64

#define VIRTIO_NO_VECTOR 0xffff

struct VirtIODevice
{
    const char *name;
    uint8_t status;
    uint8_t isr;
    uint16_t queue_sel;
    uint32_t guest_features;
    size_t config_len;
    void *config;
    uint16_t config_vector;
    int nvectors;
    uint32_t (*get_features)(VirtIODevice *vdev, uint32_t requested_features);
    uint32_t (*bad_features)(VirtIODevice *vdev);
    void (*set_features)(VirtIODevice *vdev, uint32_t val);
    void (*get_config)(VirtIODevice *vdev, uint8_t *config);
    void (*set_config)(VirtIODevice *vdev, const uint8_t *config);
    void (*reset)(VirtIODevice *vdev);
    void (*set_status)(VirtIODevice *vdev, uint8_t val);
    VirtQueue *vq;
    const VirtIOBindings *binding;
    void *binding_opaque;
    uint16_t device_id;
    bool vm_running;
    VMChangeStateEntry *vmstate;
};

VirtQueue *virtio_add_queue(VirtIODevice *vdev, int queue_size,
                            void (*handle_output)(VirtIODevice *,
                                                  VirtQueue *));

void virtqueue_push(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len);
void virtqueue_flush(VirtQueue *vq, unsigned int count);
void virtqueue_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx);

void virtqueue_map_sg(struct iovec *sg, target_phys_addr_t *addr,
    size_t num_sg, int is_write);
int virtqueue_pop(VirtQueue *vq, VirtQueueElement *elem);
int virtqueue_avail_bytes(VirtQueue *vq, int in_bytes, int out_bytes);

void virtio_notify(VirtIODevice *vdev, VirtQueue *vq);

void virtio_save(VirtIODevice *vdev, QEMUFile *f);

int virtio_load(VirtIODevice *vdev, QEMUFile *f);

void virtio_cleanup(VirtIODevice *vdev);

void virtio_notify_config(VirtIODevice *vdev);

void virtio_queue_set_notification(VirtQueue *vq, int enable);

int virtio_queue_ready(VirtQueue *vq);

int virtio_queue_empty(VirtQueue *vq);

/* Host binding interface.  */

VirtIODevice *virtio_common_init(const char *name, uint16_t device_id,
                                 size_t config_size, size_t struct_size);
uint32_t virtio_config_readb(VirtIODevice *vdev, uint32_t addr);
uint32_t virtio_config_readw(VirtIODevice *vdev, uint32_t addr);
uint32_t virtio_config_readl(VirtIODevice *vdev, uint32_t addr);
void virtio_config_writeb(VirtIODevice *vdev, uint32_t addr, uint32_t data);
void virtio_config_writew(VirtIODevice *vdev, uint32_t addr, uint32_t data);
void virtio_config_writel(VirtIODevice *vdev, uint32_t addr, uint32_t data);
void virtio_queue_set_addr(VirtIODevice *vdev, int n, target_phys_addr_t addr);
target_phys_addr_t virtio_queue_get_addr(VirtIODevice *vdev, int n);
int virtio_queue_get_num(VirtIODevice *vdev, int n);
void virtio_queue_notify(VirtIODevice *vdev, int n);
uint16_t virtio_queue_vector(VirtIODevice *vdev, int n);
void virtio_queue_set_vector(VirtIODevice *vdev, int n, uint16_t vector);
void virtio_set_status(VirtIODevice *vdev, uint8_t val);
void virtio_reset(void *opaque);
void virtio_update_irq(VirtIODevice *vdev);
int virtio_set_features(VirtIODevice *vdev, uint32_t val);

void virtio_bind_device(VirtIODevice *vdev, const VirtIOBindings *binding,
                        void *opaque);

/* Base devices.  */
VirtIODevice *virtio_blk_init(DeviceState *dev, BlockConf *conf,
                              char **serial);
struct virtio_net_conf;
VirtIODevice *virtio_net_init(DeviceState *dev, NICConf *conf,
                              struct virtio_net_conf *net);
typedef struct virtio_serial_conf virtio_serial_conf;
VirtIODevice *virtio_serial_init(DeviceState *dev, virtio_serial_conf *serial);
VirtIODevice *virtio_balloon_init(DeviceState *dev);
#ifdef CONFIG_LINUX
VirtIODevice *virtio_9p_init(DeviceState *dev, V9fsConf *conf);
#endif


void virtio_net_exit(VirtIODevice *vdev);
void virtio_blk_exit(VirtIODevice *vdev);
void virtio_serial_exit(VirtIODevice *vdev);
void virtio_balloon_exit(VirtIODevice *vdev);

#define DEFINE_VIRTIO_COMMON_FEATURES(_state, _field) \
	DEFINE_PROP_BIT("indirect_desc", _state, _field, \
			VIRTIO_RING_F_INDIRECT_DESC, true), \
	DEFINE_PROP_BIT("event_idx", _state, _field, \
			VIRTIO_RING_F_EVENT_IDX, true)

target_phys_addr_t virtio_queue_get_desc_addr(VirtIODevice *vdev, int n);
target_phys_addr_t virtio_queue_get_avail_addr(VirtIODevice *vdev, int n);
target_phys_addr_t virtio_queue_get_used_addr(VirtIODevice *vdev, int n);
target_phys_addr_t virtio_queue_get_ring_addr(VirtIODevice *vdev, int n);
target_phys_addr_t virtio_queue_get_desc_size(VirtIODevice *vdev, int n);
target_phys_addr_t virtio_queue_get_avail_size(VirtIODevice *vdev, int n);
target_phys_addr_t virtio_queue_get_used_size(VirtIODevice *vdev, int n);
target_phys_addr_t virtio_queue_get_ring_size(VirtIODevice *vdev, int n);
uint16_t virtio_queue_get_last_avail_idx(VirtIODevice *vdev, int n);
void virtio_queue_set_last_avail_idx(VirtIODevice *vdev, int n, uint16_t idx);
VirtQueue *virtio_get_queue(VirtIODevice *vdev, int n);
EventNotifier *virtio_queue_get_guest_notifier(VirtQueue *vq);
EventNotifier *virtio_queue_get_host_notifier(VirtQueue *vq);
void virtio_queue_notify_vq(VirtQueue *vq);
void virtio_irq(VirtQueue *vq);
#endif
