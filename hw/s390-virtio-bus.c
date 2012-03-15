/*
 * QEMU S390 virtio target
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

#include "hw.h"
#include "block.h"
#include "sysemu.h"
#include "net.h"
#include "boards.h"
#include "monitor.h"
#include "loader.h"
#include "elf.h"
#include "hw/virtio.h"
#include "hw/virtio-serial.h"
#include "hw/virtio-net.h"
#include "hw/sysbus.h"
#include "kvm.h"

#include "hw/s390-virtio-bus.h"

/* #define DEBUG_S390 */

#ifdef DEBUG_S390
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

#define VIRTIO_EXT_CODE   0x2603

struct BusInfo s390_virtio_bus_info = {
    .name       = "s390-virtio",
    .size       = sizeof(VirtIOS390Bus),
};

static const VirtIOBindings virtio_s390_bindings;

static ram_addr_t s390_virtio_device_num_vq(VirtIOS390Device *dev);

/* length of VirtIO device pages */
const target_phys_addr_t virtio_size = S390_DEVICE_PAGES * TARGET_PAGE_SIZE;

VirtIOS390Bus *s390_virtio_bus_init(ram_addr_t *ram_size)
{
    VirtIOS390Bus *bus;
    BusState *_bus;
    DeviceState *dev;

    /* Create bridge device */
    dev = qdev_create(NULL, "s390-virtio-bridge");
    qdev_init_nofail(dev);

    /* Create bus on bridge device */

    _bus = qbus_create(&s390_virtio_bus_info, dev, "s390-virtio");
    bus = DO_UPCAST(VirtIOS390Bus, bus, _bus);

    bus->dev_page = *ram_size;
    bus->dev_offs = bus->dev_page;
    bus->next_ring = bus->dev_page + TARGET_PAGE_SIZE;

    /* Enable hotplugging */
    _bus->allow_hotplug = 1;

    /* Allocate RAM for VirtIO device pages (descriptors, queues, rings) */
    *ram_size += S390_DEVICE_PAGES * TARGET_PAGE_SIZE;

    return bus;
}

static void s390_virtio_irq(CPUS390XState *env, int config_change, uint64_t token)
{
    if (kvm_enabled()) {
        kvm_s390_virtio_irq(env, config_change, token);
    } else {
        cpu_inject_ext(env, VIRTIO_EXT_CODE, config_change, token);
    }
}

static int s390_virtio_device_init(VirtIOS390Device *dev, VirtIODevice *vdev)
{
    VirtIOS390Bus *bus;
    int dev_len;

    bus = DO_UPCAST(VirtIOS390Bus, bus, dev->qdev.parent_bus);
    dev->vdev = vdev;
    dev->dev_offs = bus->dev_offs;
    dev->feat_len = sizeof(uint32_t); /* always keep 32 bits features */

    dev_len = VIRTIO_DEV_OFFS_CONFIG;
    dev_len += s390_virtio_device_num_vq(dev) * VIRTIO_VQCONFIG_LEN;
    dev_len += dev->feat_len * 2;
    dev_len += vdev->config_len;

    bus->dev_offs += dev_len;

    virtio_bind_device(vdev, &virtio_s390_bindings, dev);
    dev->host_features = vdev->get_features(vdev, dev->host_features);
    s390_virtio_device_sync(dev);

    if (dev->qdev.hotplugged) {
        CPUS390XState *env = s390_cpu_addr2state(0);
        s390_virtio_irq(env, VIRTIO_PARAM_DEV_ADD, dev->dev_offs);
    }

    return 0;
}

static int s390_virtio_net_init(VirtIOS390Device *dev)
{
    VirtIODevice *vdev;

    vdev = virtio_net_init((DeviceState *)dev, &dev->nic, &dev->net);
    if (!vdev) {
        return -1;
    }

    return s390_virtio_device_init(dev, vdev);
}

static int s390_virtio_blk_init(VirtIOS390Device *dev)
{
    VirtIODevice *vdev;

    vdev = virtio_blk_init((DeviceState *)dev, &dev->block,
                           &dev->block_serial);
    if (!vdev) {
        return -1;
    }

    return s390_virtio_device_init(dev, vdev);
}

static int s390_virtio_serial_init(VirtIOS390Device *dev)
{
    VirtIOS390Bus *bus;
    VirtIODevice *vdev;
    int r;

    bus = DO_UPCAST(VirtIOS390Bus, bus, dev->qdev.parent_bus);

    vdev = virtio_serial_init((DeviceState *)dev, &dev->serial);
    if (!vdev) {
        return -1;
    }

    r = s390_virtio_device_init(dev, vdev);
    if (!r) {
        bus->console = dev;
    }

    return r;
}

static int s390_virtio_scsi_init(VirtIOS390Device *dev)
{
    VirtIODevice *vdev;

    vdev = virtio_scsi_init((DeviceState *)dev, &dev->scsi);
    if (!vdev) {
        return -1;
    }

    return s390_virtio_device_init(dev, vdev);
}

static uint64_t s390_virtio_device_vq_token(VirtIOS390Device *dev, int vq)
{
    ram_addr_t token_off;

    token_off = (dev->dev_offs + VIRTIO_DEV_OFFS_CONFIG) +
                (vq * VIRTIO_VQCONFIG_LEN) +
                VIRTIO_VQCONFIG_OFFS_TOKEN;

    return ldq_be_phys(token_off);
}

static ram_addr_t s390_virtio_device_num_vq(VirtIOS390Device *dev)
{
    VirtIODevice *vdev = dev->vdev;
    int num_vq;

    for (num_vq = 0; num_vq < VIRTIO_PCI_QUEUE_MAX; num_vq++) {
        if (!virtio_queue_get_num(vdev, num_vq)) {
            break;
        }
    }

    return num_vq;
}

static ram_addr_t s390_virtio_next_ring(VirtIOS390Bus *bus)
{
    ram_addr_t r = bus->next_ring;

    bus->next_ring += VIRTIO_RING_LEN;
    return r;
}

void s390_virtio_device_sync(VirtIOS390Device *dev)
{
    VirtIOS390Bus *bus = DO_UPCAST(VirtIOS390Bus, bus, dev->qdev.parent_bus);
    ram_addr_t cur_offs;
    uint8_t num_vq;
    int i;

    virtio_reset(dev->vdev);

    /* Sync dev space */
    stb_phys(dev->dev_offs + VIRTIO_DEV_OFFS_TYPE, dev->vdev->device_id);

    stb_phys(dev->dev_offs + VIRTIO_DEV_OFFS_NUM_VQ, s390_virtio_device_num_vq(dev));
    stb_phys(dev->dev_offs + VIRTIO_DEV_OFFS_FEATURE_LEN, dev->feat_len);

    stb_phys(dev->dev_offs + VIRTIO_DEV_OFFS_CONFIG_LEN, dev->vdev->config_len);

    num_vq = s390_virtio_device_num_vq(dev);
    stb_phys(dev->dev_offs + VIRTIO_DEV_OFFS_NUM_VQ, num_vq);

    /* Sync virtqueues */
    for (i = 0; i < num_vq; i++) {
        ram_addr_t vq = (dev->dev_offs + VIRTIO_DEV_OFFS_CONFIG) +
                        (i * VIRTIO_VQCONFIG_LEN);
        ram_addr_t vring;

        vring = s390_virtio_next_ring(bus);
        virtio_queue_set_addr(dev->vdev, i, vring);
        virtio_queue_set_vector(dev->vdev, i, i);
        stq_be_phys(vq + VIRTIO_VQCONFIG_OFFS_ADDRESS, vring);
        stw_be_phys(vq + VIRTIO_VQCONFIG_OFFS_NUM, virtio_queue_get_num(dev->vdev, i));
    }

    cur_offs = dev->dev_offs;
    cur_offs += VIRTIO_DEV_OFFS_CONFIG;
    cur_offs += num_vq * VIRTIO_VQCONFIG_LEN;

    /* Sync feature bitmap */
    stl_le_phys(cur_offs, dev->host_features);

    dev->feat_offs = cur_offs + dev->feat_len;
    cur_offs += dev->feat_len * 2;

    /* Sync config space */
    if (dev->vdev->get_config) {
        dev->vdev->get_config(dev->vdev, dev->vdev->config);
    }

    cpu_physical_memory_write(cur_offs,
                              dev->vdev->config, dev->vdev->config_len);
    cur_offs += dev->vdev->config_len;
}

void s390_virtio_device_update_status(VirtIOS390Device *dev)
{
    VirtIODevice *vdev = dev->vdev;
    uint32_t features;

    virtio_set_status(vdev, ldub_phys(dev->dev_offs + VIRTIO_DEV_OFFS_STATUS));

    /* Update guest supported feature bitmap */

    features = bswap32(ldl_be_phys(dev->feat_offs));
    virtio_set_features(vdev, features);
}

VirtIOS390Device *s390_virtio_bus_console(VirtIOS390Bus *bus)
{
    return bus->console;
}

/* Find a device by vring address */
VirtIOS390Device *s390_virtio_bus_find_vring(VirtIOS390Bus *bus,
                                             ram_addr_t mem,
                                             int *vq_num)
{
    VirtIOS390Device *_dev;
    DeviceState *dev;
    int i;

    QTAILQ_FOREACH(dev, &bus->bus.children, sibling) {
        _dev = (VirtIOS390Device *)dev;
        for(i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
            if (!virtio_queue_get_addr(_dev->vdev, i))
                break;
            if (virtio_queue_get_addr(_dev->vdev, i) == mem) {
                if (vq_num) {
                    *vq_num = i;
                }
                return _dev;
            }
        }
    }

    return NULL;
}

/* Find a device by device descriptor location */
VirtIOS390Device *s390_virtio_bus_find_mem(VirtIOS390Bus *bus, ram_addr_t mem)
{
    VirtIOS390Device *_dev;
    DeviceState *dev;

    QTAILQ_FOREACH(dev, &bus->bus.children, sibling) {
        _dev = (VirtIOS390Device *)dev;
        if (_dev->dev_offs == mem) {
            return _dev;
        }
    }

    return NULL;
}

static void virtio_s390_notify(void *opaque, uint16_t vector)
{
    VirtIOS390Device *dev = (VirtIOS390Device*)opaque;
    uint64_t token = s390_virtio_device_vq_token(dev, vector);
    CPUS390XState *env = s390_cpu_addr2state(0);

    s390_virtio_irq(env, 0, token);
}

static unsigned virtio_s390_get_features(void *opaque)
{
    VirtIOS390Device *dev = (VirtIOS390Device*)opaque;
    return dev->host_features;
}

/**************** S390 Virtio Bus Device Descriptions *******************/

static const VirtIOBindings virtio_s390_bindings = {
    .notify = virtio_s390_notify,
    .get_features = virtio_s390_get_features,
};

static Property s390_virtio_net_properties[] = {
    DEFINE_NIC_PROPERTIES(VirtIOS390Device, nic),
    DEFINE_PROP_UINT32("x-txtimer", VirtIOS390Device,
                       net.txtimer, TX_TIMER_INTERVAL),
    DEFINE_PROP_INT32("x-txburst", VirtIOS390Device,
                      net.txburst, TX_BURST),
    DEFINE_PROP_STRING("tx", VirtIOS390Device, net.tx),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_net_init;
    dc->props = s390_virtio_net_properties;
}

static TypeInfo s390_virtio_net = {
    .name          = "virtio-net-s390",
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIOS390Device),
    .class_init    = s390_virtio_net_class_init,
};

static Property s390_virtio_blk_properties[] = {
    DEFINE_BLOCK_PROPERTIES(VirtIOS390Device, block),
    DEFINE_PROP_STRING("serial", VirtIOS390Device, block_serial),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_blk_init;
    dc->props = s390_virtio_blk_properties;
}

static TypeInfo s390_virtio_blk = {
    .name          = "virtio-blk-s390",
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIOS390Device),
    .class_init    = s390_virtio_blk_class_init,
};

static Property s390_virtio_serial_properties[] = {
    DEFINE_PROP_UINT32("max_ports", VirtIOS390Device,
                       serial.max_virtserial_ports, 31),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_serial_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_serial_init;
    dc->props = s390_virtio_serial_properties;
}

static TypeInfo s390_virtio_serial = {
    .name          = "virtio-serial-s390",
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIOS390Device),
    .class_init    = s390_virtio_serial_class_init,
};

static int s390_virtio_busdev_init(DeviceState *dev)
{
    VirtIOS390Device *_dev = (VirtIOS390Device *)dev;
    VirtIOS390DeviceClass *_info = VIRTIO_S390_DEVICE_GET_CLASS(dev);

    return _info->init(_dev);
}

static void virtio_s390_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->init = s390_virtio_busdev_init;
    dc->bus_info = &s390_virtio_bus_info;
    dc->unplug = qdev_simple_unplug_cb;
}

static TypeInfo virtio_s390_device_info = {
    .name = TYPE_VIRTIO_S390_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIOS390Device),
    .class_init = virtio_s390_device_class_init,
    .class_size = sizeof(VirtIOS390DeviceClass),
    .abstract = true,
};

static Property s390_virtio_scsi_properties[] = {
    DEFINE_VIRTIO_SCSI_PROPERTIES(VirtIOS390Device, host_features, scsi),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_scsi_init;
    dc->props = s390_virtio_scsi_properties;
}

static TypeInfo s390_virtio_scsi = {
    .name          = "virtio-scsi-s390",
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIOS390Device),
    .class_init    = s390_virtio_scsi_class_init,
};

/***************** S390 Virtio Bus Bridge Device *******************/
/* Only required to have the virtio bus as child in the system bus */

static int s390_virtio_bridge_init(SysBusDevice *dev)
{
    /* nothing */
    return 0;
}

static void s390_virtio_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = s390_virtio_bridge_init;
    dc->no_user = 1;
}

static TypeInfo s390_virtio_bridge_info = {
    .name          = "s390-virtio-bridge",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = s390_virtio_bridge_class_init,
};

static void s390_virtio_register_types(void)
{
    type_register_static(&virtio_s390_device_info);
    type_register_static(&s390_virtio_serial);
    type_register_static(&s390_virtio_blk);
    type_register_static(&s390_virtio_net);
    type_register_static(&s390_virtio_scsi);
    type_register_static(&s390_virtio_bridge_info);
}

type_init(s390_virtio_register_types)
