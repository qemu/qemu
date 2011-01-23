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

#include "virtio-net.h"

#define VIRTIO_DEV_OFFS_TYPE		0	/* 8 bits */
#define VIRTIO_DEV_OFFS_NUM_VQ		1	/* 8 bits */
#define VIRTIO_DEV_OFFS_FEATURE_LEN	2	/* 8 bits */
#define VIRTIO_DEV_OFFS_CONFIG_LEN	3	/* 8 bits */
#define VIRTIO_DEV_OFFS_STATUS		4	/* 8 bits */
#define VIRTIO_DEV_OFFS_CONFIG		5	/* dynamic */

#define VIRTIO_VQCONFIG_OFFS_TOKEN	0	/* 64 bits */
#define VIRTIO_VQCONFIG_OFFS_ADDRESS	8	/* 64 bits */
#define VIRTIO_VQCONFIG_OFFS_NUM	16	/* 16 bits */
#define VIRTIO_VQCONFIG_LEN		24

#define VIRTIO_RING_LEN			(TARGET_PAGE_SIZE * 3)
#define S390_DEVICE_PAGES		256

typedef struct VirtIOS390Device {
    DeviceState qdev;
    ram_addr_t dev_offs;
    ram_addr_t feat_offs;
    uint8_t feat_len;
    VirtIODevice *vdev;
    BlockConf block;
    NICConf nic;
    uint32_t host_features;
    /* Max. number of ports we can have for a the virtio-serial device */
    uint32_t max_virtserial_ports;
    virtio_net_conf net;
} VirtIOS390Device;

typedef struct VirtIOS390Bus {
    BusState bus;

    VirtIOS390Device *console;
    ram_addr_t dev_page;
    ram_addr_t dev_offs;
    ram_addr_t next_ring;
} VirtIOS390Bus;


void s390_virtio_device_update_status(VirtIOS390Device *dev);

VirtIOS390Device *s390_virtio_bus_console(VirtIOS390Bus *bus);
VirtIOS390Bus *s390_virtio_bus_init(ram_addr_t *ram_size);

VirtIOS390Device *s390_virtio_bus_find_vring(VirtIOS390Bus *bus,
                                             ram_addr_t mem, int *vq_num);
VirtIOS390Device *s390_virtio_bus_find_mem(VirtIOS390Bus *bus, ram_addr_t mem);
void s390_virtio_device_sync(VirtIOS390Device *dev);
