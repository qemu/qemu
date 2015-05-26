/*
 * QEMU S390x VirtIO BUS definitions
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_S390_VIRTIO_BUS_H
#define HW_S390_VIRTIO_BUS_H 1

#include <stddef.h>

#include "standard-headers/asm-s390/kvm_virtio.h"
#include "standard-headers/linux/virtio_ring.h"
#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-rng.h"
#include "hw/virtio/virtio-serial.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/virtio-bus.h"
#ifdef CONFIG_VHOST_SCSI
#include "hw/virtio/vhost-scsi.h"
#endif

typedef struct kvm_device_desc KvmDeviceDesc;

#define VIRTIO_DEV_OFFS_TYPE        offsetof(KvmDeviceDesc, type)
#define VIRTIO_DEV_OFFS_NUM_VQ      offsetof(KvmDeviceDesc, num_vq)
#define VIRTIO_DEV_OFFS_FEATURE_LEN offsetof(KvmDeviceDesc, feature_len)
#define VIRTIO_DEV_OFFS_CONFIG_LEN  offsetof(KvmDeviceDesc, config_len)
#define VIRTIO_DEV_OFFS_STATUS      offsetof(KvmDeviceDesc, status)
#define VIRTIO_DEV_OFFS_CONFIG      offsetof(KvmDeviceDesc, config)

typedef struct kvm_vqconfig KvmVqConfig;
#define VIRTIO_VQCONFIG_OFFS_TOKEN   offsetof(KvmVqConfig,token)    /* 64 bit */
#define VIRTIO_VQCONFIG_OFFS_ADDRESS offsetof(KvmVqConfig, address) /* 64 bit */
#define VIRTIO_VQCONFIG_OFFS_NUM     offsetof(KvmVqConfig, num)     /* 16 bit */
#define VIRTIO_VQCONFIG_LEN          sizeof(KvmVqConfig)

#define VIRTIO_RING_LEN			(TARGET_PAGE_SIZE * 3)
#define VIRTIO_VRING_AVAIL_IDX_OFFS offsetof(struct vring_avail, idx)
#define VIRTIO_VRING_USED_IDX_OFFS  offsetof(struct vring_used, idx)
#define S390_DEVICE_PAGES		512

#define TYPE_VIRTIO_S390_DEVICE "virtio-s390-device"
#define VIRTIO_S390_DEVICE(obj) \
     OBJECT_CHECK(VirtIOS390Device, (obj), TYPE_VIRTIO_S390_DEVICE)
#define VIRTIO_S390_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(VirtIOS390DeviceClass, (klass), TYPE_VIRTIO_S390_DEVICE)
#define VIRTIO_S390_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VirtIOS390DeviceClass, (obj), TYPE_VIRTIO_S390_DEVICE)

#define TYPE_S390_VIRTIO_BUS "s390-virtio-bus"
#define S390_VIRTIO_BUS(obj) \
     OBJECT_CHECK(VirtIOS390Bus, (obj), TYPE_S390_VIRTIO_BUS)

/* virtio-s390-bus */

typedef struct VirtioBusState VirtioS390BusState;
typedef struct VirtioBusClass VirtioS390BusClass;

#define TYPE_VIRTIO_S390_BUS "virtio-s390-bus"
#define VIRTIO_S390_BUS(obj) \
        OBJECT_CHECK(VirtioS390BusState, (obj), TYPE_VIRTIO_S390_BUS)
#define VIRTIO_S390_BUS_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioS390BusClass, obj, TYPE_VIRTIO_S390_BUS)
#define VIRTIO_S390_BUS_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioS390BusClass, klass, TYPE_VIRTIO_S390_BUS)


typedef struct VirtIOS390Device VirtIOS390Device;

typedef struct VirtIOS390DeviceClass {
    DeviceClass qdev;
    void (*realize)(VirtIOS390Device *dev, Error **errp);
} VirtIOS390DeviceClass;

struct VirtIOS390Device {
    DeviceState qdev;
    ram_addr_t dev_offs;
    ram_addr_t feat_offs;
    uint8_t feat_len;
    VirtIODevice *vdev;
    VirtioBusState bus;
};

typedef struct VirtIOS390Bus {
    BusState bus;

    VirtIOS390Device *console;
    ram_addr_t dev_page;
    ram_addr_t dev_offs;
    ram_addr_t next_ring;
} VirtIOS390Bus;


void s390_virtio_device_update_status(VirtIOS390Device *dev);

VirtIOS390Bus *s390_virtio_bus_init(ram_addr_t *ram_size);

VirtIOS390Device *s390_virtio_bus_find_vring(VirtIOS390Bus *bus,
                                             ram_addr_t mem, int *vq_num);
VirtIOS390Device *s390_virtio_bus_find_mem(VirtIOS390Bus *bus, ram_addr_t mem);
void s390_virtio_device_sync(VirtIOS390Device *dev);
void s390_virtio_reset_idx(VirtIOS390Device *dev);

/* virtio-blk-s390 */

#define TYPE_VIRTIO_BLK_S390 "virtio-blk-s390"
#define VIRTIO_BLK_S390(obj) \
        OBJECT_CHECK(VirtIOBlkS390, (obj), TYPE_VIRTIO_BLK_S390)

typedef struct VirtIOBlkS390 {
    VirtIOS390Device parent_obj;
    VirtIOBlock vdev;
} VirtIOBlkS390;

/* virtio-scsi-s390 */

#define TYPE_VIRTIO_SCSI_S390 "virtio-scsi-s390"
#define VIRTIO_SCSI_S390(obj) \
        OBJECT_CHECK(VirtIOSCSIS390, (obj), TYPE_VIRTIO_SCSI_S390)

typedef struct VirtIOSCSIS390 {
    VirtIOS390Device parent_obj;
    VirtIOSCSI vdev;
} VirtIOSCSIS390;

/* virtio-serial-s390 */

#define TYPE_VIRTIO_SERIAL_S390 "virtio-serial-s390"
#define VIRTIO_SERIAL_S390(obj) \
        OBJECT_CHECK(VirtIOSerialS390, (obj), TYPE_VIRTIO_SERIAL_S390)

typedef struct VirtIOSerialS390 {
    VirtIOS390Device parent_obj;
    VirtIOSerial vdev;
} VirtIOSerialS390;

/* virtio-net-s390 */

#define TYPE_VIRTIO_NET_S390 "virtio-net-s390"
#define VIRTIO_NET_S390(obj) \
        OBJECT_CHECK(VirtIONetS390, (obj), TYPE_VIRTIO_NET_S390)

typedef struct VirtIONetS390 {
    VirtIOS390Device parent_obj;
    VirtIONet vdev;
} VirtIONetS390;

/* vhost-scsi-s390 */

#ifdef CONFIG_VHOST_SCSI
#define TYPE_VHOST_SCSI_S390 "vhost-scsi-s390"
#define VHOST_SCSI_S390(obj) \
        OBJECT_CHECK(VHostSCSIS390, (obj), TYPE_VHOST_SCSI_S390)

typedef struct VHostSCSIS390 {
    VirtIOS390Device parent_obj;
    VHostSCSI vdev;
} VHostSCSIS390;
#endif

/* virtio-rng-s390 */

#define TYPE_VIRTIO_RNG_S390 "virtio-rng-s390"
#define VIRTIO_RNG_S390(obj) \
        OBJECT_CHECK(VirtIORNGS390, (obj), TYPE_VIRTIO_RNG_S390)

typedef struct VirtIORNGS390 {
    VirtIOS390Device parent_obj;
    VirtIORNG vdev;
} VirtIORNGS390;

#endif
