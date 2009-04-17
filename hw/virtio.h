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
#include "pci.h"

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

/* We notify when the ring is completely used, even if the guest is supressing
 * callbacks */
#define VIRTIO_F_NOTIFY_ON_EMPTY        24
/* A guest should never accept this.  It implies negotiation is broken. */
#define VIRTIO_F_BAD_FEATURE		30

/* from Linux's linux/virtio_ring.h */

/* This marks a buffer as continuing via the next field. */
#define VRING_DESC_F_NEXT       1
/* This marks a buffer as write-only (otherwise read-only). */
#define VRING_DESC_F_WRITE      2

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
typedef struct VirtIODevice VirtIODevice;

#define VIRTQUEUE_MAX_SIZE 1024

typedef struct VirtQueueElement
{
    unsigned int index;
    unsigned int out_num;
    unsigned int in_num;
    target_phys_addr_t in_addr[VIRTQUEUE_MAX_SIZE];
    struct iovec in_sg[VIRTQUEUE_MAX_SIZE];
    struct iovec out_sg[VIRTQUEUE_MAX_SIZE];
} VirtQueueElement;

#define VIRTIO_PCI_QUEUE_MAX 16

struct VirtIODevice
{
    PCIDevice pci_dev;
    const char *name;
    uint32_t addr;
    uint8_t status;
    uint8_t isr;
    uint16_t queue_sel;
    uint32_t features;
    size_t config_len;
    void *config;
    uint32_t (*get_features)(VirtIODevice *vdev);
    uint32_t (*bad_features)(VirtIODevice *vdev);
    void (*set_features)(VirtIODevice *vdev, uint32_t val);
    void (*get_config)(VirtIODevice *vdev, uint8_t *config);
    void (*set_config)(VirtIODevice *vdev, const uint8_t *config);
    void (*reset)(VirtIODevice *vdev);
    VirtQueue *vq;
};

VirtIODevice *virtio_init_pci(PCIBus *bus, const char *name,
                              uint16_t vendor, uint16_t device,
                              uint16_t subvendor, uint16_t subdevice,
                              uint16_t class_code, uint8_t pif,
                              size_t config_size, size_t struct_size);

VirtQueue *virtio_add_queue(VirtIODevice *vdev, int queue_size,
                            void (*handle_output)(VirtIODevice *,
                                                  VirtQueue *));

void virtqueue_push(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len);
void virtqueue_flush(VirtQueue *vq, unsigned int count);
void virtqueue_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx);

int virtqueue_pop(VirtQueue *vq, VirtQueueElement *elem);
int virtqueue_avail_bytes(VirtQueue *vq, int in_bytes, int out_bytes);

void virtio_notify(VirtIODevice *vdev, VirtQueue *vq);

void virtio_save(VirtIODevice *vdev, QEMUFile *f);

void virtio_load(VirtIODevice *vdev, QEMUFile *f);

void virtio_cleanup(VirtIODevice *vdev);

void virtio_notify_config(VirtIODevice *vdev);

void virtio_queue_set_notification(VirtQueue *vq, int enable);

int virtio_queue_ready(VirtQueue *vq);

int virtio_queue_empty(VirtQueue *vq);

#endif
