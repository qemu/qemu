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

typedef struct {
    DeviceInfo qdev;
    int (*init)(VirtIOS390Device *dev);
} VirtIOS390DeviceInfo;


static const VirtIOBindings virtio_s390_bindings;

static ram_addr_t s390_virtio_device_num_vq(VirtIOS390Device *dev);

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

    /* Allocate RAM for VirtIO device pages (descriptors, queues, rings) */
    *ram_size += S390_DEVICE_PAGES * TARGET_PAGE_SIZE;

    return bus;
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

    vdev = virtio_blk_init((DeviceState *)dev, &dev->block);
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

static uint64_t s390_virtio_device_vq_token(VirtIOS390Device *dev, int vq)
{
    ram_addr_t token_off;

    token_off = (dev->dev_offs + VIRTIO_DEV_OFFS_CONFIG) +
                (vq * VIRTIO_VQCONFIG_LEN) +
                VIRTIO_VQCONFIG_OFFS_TOKEN;

    return ldq_phys(token_off);
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
        stq_phys(vq + VIRTIO_VQCONFIG_OFFS_ADDRESS, vring);
        stw_phys(vq + VIRTIO_VQCONFIG_OFFS_NUM, virtio_queue_get_num(dev->vdev, i));
    }

    cur_offs = dev->dev_offs;
    cur_offs += VIRTIO_DEV_OFFS_CONFIG;
    cur_offs += num_vq * VIRTIO_VQCONFIG_LEN;

    /* Sync feature bitmap */
    stl_phys(cur_offs, bswap32(dev->host_features));

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

    features = bswap32(ldl_phys(dev->feat_offs));
    if (vdev->set_features) {
        vdev->set_features(vdev, features);
    }
    vdev->guest_features = features;
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

    QLIST_FOREACH(dev, &bus->bus.children, sibling) {
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

    QLIST_FOREACH(dev, &bus->bus.children, sibling) {
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
    CPUState *env = s390_cpu_addr2state(0);

    if (kvm_enabled()) {
        kvm_s390_virtio_irq(env, 0, token);
    } else {
        cpu_inject_ext(env, VIRTIO_EXT_CODE, 0, token);
    }
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

static VirtIOS390DeviceInfo s390_virtio_net = {
    .init = s390_virtio_net_init,
    .qdev.name = "virtio-net-s390",
    .qdev.alias = "virtio-net",
    .qdev.size = sizeof(VirtIOS390Device),
    .qdev.props = (Property[]) {
        DEFINE_NIC_PROPERTIES(VirtIOS390Device, nic),
        DEFINE_PROP_UINT32("x-txtimer", VirtIOS390Device,
                           net.txtimer, TX_TIMER_INTERVAL),
        DEFINE_PROP_INT32("x-txburst", VirtIOS390Device,
                          net.txburst, TX_BURST),
        DEFINE_PROP_STRING("tx", VirtIOS390Device, net.tx),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static VirtIOS390DeviceInfo s390_virtio_blk = {
    .init = s390_virtio_blk_init,
    .qdev.name = "virtio-blk-s390",
    .qdev.alias = "virtio-blk",
    .qdev.size = sizeof(VirtIOS390Device),
    .qdev.props = (Property[]) {
        DEFINE_BLOCK_PROPERTIES(VirtIOS390Device, block),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static VirtIOS390DeviceInfo s390_virtio_serial = {
    .init = s390_virtio_serial_init,
    .qdev.name = "virtio-serial-s390",
    .qdev.alias = "virtio-serial",
    .qdev.size = sizeof(VirtIOS390Device),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("max_ports", VirtIOS390Device,
                           serial.max_virtserial_ports, 31),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static int s390_virtio_busdev_init(DeviceState *dev, DeviceInfo *info)
{
    VirtIOS390DeviceInfo *_info = (VirtIOS390DeviceInfo *)info;
    VirtIOS390Device *_dev = (VirtIOS390Device *)dev;

    return _info->init(_dev);
}

static void s390_virtio_bus_register_withprop(VirtIOS390DeviceInfo *info)
{
    info->qdev.init = s390_virtio_busdev_init;
    info->qdev.bus_info = &s390_virtio_bus_info;

    assert(info->qdev.size >= sizeof(VirtIOS390Device));
    qdev_register(&info->qdev);
}

static void s390_virtio_register(void)
{
    s390_virtio_bus_register_withprop(&s390_virtio_serial);
    s390_virtio_bus_register_withprop(&s390_virtio_blk);
    s390_virtio_bus_register_withprop(&s390_virtio_net);
}
device_init(s390_virtio_register);


/***************** S390 Virtio Bus Bridge Device *******************/
/* Only required to have the virtio bus as child in the system bus */

static int s390_virtio_bridge_init(SysBusDevice *dev)
{
    /* nothing */
    return 0;
}

static SysBusDeviceInfo s390_virtio_bridge_info = {
    .init = s390_virtio_bridge_init,
    .qdev.name  = "s390-virtio-bridge",
    .qdev.size  = sizeof(SysBusDevice),
    .qdev.no_user = 1,
};

static void s390_virtio_register_devices(void)
{
    sysbus_register_withprop(&s390_virtio_bridge_info);
}

device_init(s390_virtio_register_devices)
