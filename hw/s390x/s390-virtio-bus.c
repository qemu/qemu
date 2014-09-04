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

#include "hw/hw.h"
#include "block/block.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "monitor/monitor.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-rng.h"
#include "hw/virtio/virtio-serial.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/vhost-scsi.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"

#include "hw/s390x/s390-virtio-bus.h"
#include "hw/virtio/virtio-bus.h"

/* #define DEBUG_S390 */

#ifdef DEBUG_S390
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static void virtio_s390_bus_new(VirtioBusState *bus, size_t bus_size,
                                VirtIOS390Device *dev);

static const TypeInfo s390_virtio_bus_info = {
    .name = TYPE_S390_VIRTIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtIOS390Bus),
};

static ram_addr_t s390_virtio_device_num_vq(VirtIOS390Device *dev);

/* length of VirtIO device pages */
const hwaddr virtio_size = S390_DEVICE_PAGES * TARGET_PAGE_SIZE;

static void s390_virtio_bus_reset(void *opaque)
{
    VirtIOS390Bus *bus = opaque;
    bus->next_ring = bus->dev_page + TARGET_PAGE_SIZE;
}

void s390_virtio_reset_idx(VirtIOS390Device *dev)
{
    int i;
    hwaddr idx_addr;
    uint8_t num_vq;

    num_vq = s390_virtio_device_num_vq(dev);
    for (i = 0; i < num_vq; i++) {
        idx_addr = virtio_queue_get_avail_addr(dev->vdev, i) +
            VIRTIO_VRING_AVAIL_IDX_OFFS;
        stw_phys(&address_space_memory, idx_addr, 0);
        idx_addr = virtio_queue_get_used_addr(dev->vdev, i) +
            VIRTIO_VRING_USED_IDX_OFFS;
        stw_phys(&address_space_memory, idx_addr, 0);
    }
}

VirtIOS390Bus *s390_virtio_bus_init(ram_addr_t *ram_size)
{
    VirtIOS390Bus *bus;
    BusState *_bus;
    DeviceState *dev;

    /* Create bridge device */
    dev = qdev_create(NULL, "s390-virtio-bridge");
    qdev_init_nofail(dev);

    /* Create bus on bridge device */

    _bus = qbus_create(TYPE_S390_VIRTIO_BUS, dev, "s390-virtio");
    bus = DO_UPCAST(VirtIOS390Bus, bus, _bus);

    bus->dev_page = *ram_size;
    bus->dev_offs = bus->dev_page;
    bus->next_ring = bus->dev_page + TARGET_PAGE_SIZE;

    /* Enable hotplugging */
    _bus->allow_hotplug = 1;

    /* Allocate RAM for VirtIO device pages (descriptors, queues, rings) */
    *ram_size += S390_DEVICE_PAGES * TARGET_PAGE_SIZE;

    qemu_register_reset(s390_virtio_bus_reset, bus);
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
    dev_len += virtio_bus_get_vdev_config_len(&dev->bus);

    bus->dev_offs += dev_len;

    dev->host_features = virtio_bus_get_vdev_features(&dev->bus,
                                                      dev->host_features);
    s390_virtio_device_sync(dev);
    s390_virtio_reset_idx(dev);
    if (dev->qdev.hotplugged) {
        s390_virtio_irq(VIRTIO_PARAM_DEV_ADD, dev->dev_offs);
    }

    return 0;
}

static int s390_virtio_net_init(VirtIOS390Device *s390_dev)
{
    DeviceState *qdev = DEVICE(s390_dev);
    VirtIONetS390 *dev = VIRTIO_NET_S390(s390_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    virtio_net_set_config_size(&dev->vdev, s390_dev->host_features);
    virtio_net_set_netclient_name(&dev->vdev, qdev->id,
                                  object_get_typename(OBJECT(qdev)));
    qdev_set_parent_bus(vdev, BUS(&s390_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }

    return s390_virtio_device_init(s390_dev, VIRTIO_DEVICE(vdev));
}

static void s390_virtio_net_instance_init(Object *obj)
{
    VirtIONetS390 *dev = VIRTIO_NET_S390(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_NET);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static int s390_virtio_blk_init(VirtIOS390Device *s390_dev)
{
    VirtIOBlkS390 *dev = VIRTIO_BLK_S390(s390_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    qdev_set_parent_bus(vdev, BUS(&s390_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return s390_virtio_device_init(s390_dev, VIRTIO_DEVICE(vdev));
}

static void s390_virtio_blk_instance_init(Object *obj)
{
    VirtIOBlkS390 *dev = VIRTIO_BLK_S390(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_BLK);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
    object_unref(OBJECT(&dev->vdev));
    qdev_alias_all_properties(DEVICE(&dev->vdev), obj);
    object_property_add_alias(obj, "iothread", OBJECT(&dev->vdev),"iothread",
                              &error_abort);
}

static int s390_virtio_serial_init(VirtIOS390Device *s390_dev)
{
    VirtIOSerialS390 *dev = VIRTIO_SERIAL_S390(s390_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    DeviceState *qdev = DEVICE(s390_dev);
    VirtIOS390Bus *bus;
    int r;
    char *bus_name;

    bus = DO_UPCAST(VirtIOS390Bus, bus, qdev->parent_bus);

    /*
     * For command line compatibility, this sets the virtio-serial-device bus
     * name as before.
     */
    if (qdev->id) {
        bus_name = g_strdup_printf("%s.0", qdev->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_set_parent_bus(vdev, BUS(&s390_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }

    r = s390_virtio_device_init(s390_dev, VIRTIO_DEVICE(vdev));
    if (!r) {
        bus->console = s390_dev;
    }

    return r;
}

static void s390_virtio_serial_instance_init(Object *obj)
{
    VirtIOSerialS390 *dev = VIRTIO_SERIAL_S390(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_SERIAL);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static int s390_virtio_scsi_init(VirtIOS390Device *s390_dev)
{
    VirtIOSCSIS390 *dev = VIRTIO_SCSI_S390(s390_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    DeviceState *qdev = DEVICE(s390_dev);
    char *bus_name;

    /*
     * For command line compatibility, this sets the virtio-scsi-device bus
     * name as before.
     */
    if (qdev->id) {
        bus_name = g_strdup_printf("%s.0", qdev->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_set_parent_bus(vdev, BUS(&s390_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }

    return s390_virtio_device_init(s390_dev, VIRTIO_DEVICE(vdev));
}

static void s390_virtio_scsi_instance_init(Object *obj)
{
    VirtIOSCSIS390 *dev = VIRTIO_SCSI_S390(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_SCSI);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

#ifdef CONFIG_VHOST_SCSI
static int s390_vhost_scsi_init(VirtIOS390Device *s390_dev)
{
    VHostSCSIS390 *dev = VHOST_SCSI_S390(s390_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&s390_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }

    return s390_virtio_device_init(s390_dev, VIRTIO_DEVICE(vdev));
}

static void s390_vhost_scsi_instance_init(Object *obj)
{
    VHostSCSIS390 *dev = VHOST_SCSI_S390(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VHOST_SCSI);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}
#endif


static int s390_virtio_rng_init(VirtIOS390Device *s390_dev)
{
    VirtIORNGS390 *dev = VIRTIO_RNG_S390(s390_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&s390_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }

    object_property_set_link(OBJECT(dev),
                             OBJECT(dev->vdev.conf.rng), "rng",
                             NULL);

    return s390_virtio_device_init(s390_dev, VIRTIO_DEVICE(vdev));
}

static void s390_virtio_rng_instance_init(Object *obj)
{
    VirtIORNGS390 *dev = VIRTIO_RNG_S390(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_RNG);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
    object_property_add_link(obj, "rng", TYPE_RNG_BACKEND,
                             (Object **)&dev->vdev.conf.rng,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);
}

static uint64_t s390_virtio_device_vq_token(VirtIOS390Device *dev, int vq)
{
    ram_addr_t token_off;

    token_off = (dev->dev_offs + VIRTIO_DEV_OFFS_CONFIG) +
                (vq * VIRTIO_VQCONFIG_LEN) +
                VIRTIO_VQCONFIG_OFFS_TOKEN;

    return ldq_be_phys(&address_space_memory, token_off);
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
    stb_phys(&address_space_memory,
             dev->dev_offs + VIRTIO_DEV_OFFS_TYPE, dev->vdev->device_id);

    stb_phys(&address_space_memory,
             dev->dev_offs + VIRTIO_DEV_OFFS_NUM_VQ,
             s390_virtio_device_num_vq(dev));
    stb_phys(&address_space_memory,
             dev->dev_offs + VIRTIO_DEV_OFFS_FEATURE_LEN, dev->feat_len);

    stb_phys(&address_space_memory,
             dev->dev_offs + VIRTIO_DEV_OFFS_CONFIG_LEN, dev->vdev->config_len);

    num_vq = s390_virtio_device_num_vq(dev);
    stb_phys(&address_space_memory,
             dev->dev_offs + VIRTIO_DEV_OFFS_NUM_VQ, num_vq);

    /* Sync virtqueues */
    for (i = 0; i < num_vq; i++) {
        ram_addr_t vq = (dev->dev_offs + VIRTIO_DEV_OFFS_CONFIG) +
                        (i * VIRTIO_VQCONFIG_LEN);
        ram_addr_t vring;

        vring = s390_virtio_next_ring(bus);
        virtio_queue_set_addr(dev->vdev, i, vring);
        virtio_queue_set_vector(dev->vdev, i, i);
        stq_be_phys(&address_space_memory,
                    vq + VIRTIO_VQCONFIG_OFFS_ADDRESS, vring);
        stw_be_phys(&address_space_memory,
                    vq + VIRTIO_VQCONFIG_OFFS_NUM,
                    virtio_queue_get_num(dev->vdev, i));
    }

    cur_offs = dev->dev_offs;
    cur_offs += VIRTIO_DEV_OFFS_CONFIG;
    cur_offs += num_vq * VIRTIO_VQCONFIG_LEN;

    /* Sync feature bitmap */
    stl_le_phys(&address_space_memory, cur_offs, dev->host_features);

    dev->feat_offs = cur_offs + dev->feat_len;
    cur_offs += dev->feat_len * 2;

    /* Sync config space */
    virtio_bus_get_vdev_config(&dev->bus, dev->vdev->config);

    cpu_physical_memory_write(cur_offs,
                              dev->vdev->config, dev->vdev->config_len);
    cur_offs += dev->vdev->config_len;
}

void s390_virtio_device_update_status(VirtIOS390Device *dev)
{
    VirtIODevice *vdev = dev->vdev;
    uint32_t features;

    virtio_set_status(vdev, ldub_phys(&address_space_memory,
                                      dev->dev_offs + VIRTIO_DEV_OFFS_STATUS));

    /* Update guest supported feature bitmap */

    features = bswap32(ldl_be_phys(&address_space_memory, dev->feat_offs));
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
    BusChild *kid;
    int i;

    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        VirtIOS390Device *dev = (VirtIOS390Device *)kid->child;

        for(i = 0; i < VIRTIO_PCI_QUEUE_MAX; i++) {
            if (!virtio_queue_get_addr(dev->vdev, i))
                break;
            if (virtio_queue_get_addr(dev->vdev, i) == mem) {
                if (vq_num) {
                    *vq_num = i;
                }
                return dev;
            }
        }
    }

    return NULL;
}

/* Find a device by device descriptor location */
VirtIOS390Device *s390_virtio_bus_find_mem(VirtIOS390Bus *bus, ram_addr_t mem)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        VirtIOS390Device *dev = (VirtIOS390Device *)kid->child;
        if (dev->dev_offs == mem) {
            return dev;
        }
    }

    return NULL;
}

/* DeviceState to VirtIOS390Device. Note: used on datapath,
 * be careful and test performance if you change this.
 */
static inline VirtIOS390Device *to_virtio_s390_device_fast(DeviceState *d)
{
    return container_of(d, VirtIOS390Device, qdev);
}

/* DeviceState to VirtIOS390Device. TODO: use QOM. */
static inline VirtIOS390Device *to_virtio_s390_device(DeviceState *d)
{
    return container_of(d, VirtIOS390Device, qdev);
}

static void virtio_s390_notify(DeviceState *d, uint16_t vector)
{
    VirtIOS390Device *dev = to_virtio_s390_device_fast(d);
    uint64_t token = s390_virtio_device_vq_token(dev, vector);

    s390_virtio_irq(0, token);
}

static unsigned virtio_s390_get_features(DeviceState *d)
{
    VirtIOS390Device *dev = to_virtio_s390_device(d);
    return dev->host_features;
}

/**************** S390 Virtio Bus Device Descriptions *******************/

static Property s390_virtio_net_properties[] = {
    DEFINE_NIC_PROPERTIES(VirtIONetS390, vdev.nic_conf),
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOS390Device, host_features),
    DEFINE_VIRTIO_NET_FEATURES(VirtIOS390Device, host_features),
    DEFINE_VIRTIO_NET_PROPERTIES(VirtIONetS390, vdev.net_conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_net_init;
    dc->props = s390_virtio_net_properties;
}

static const TypeInfo s390_virtio_net = {
    .name          = TYPE_VIRTIO_NET_S390,
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIONetS390),
    .instance_init = s390_virtio_net_instance_init,
    .class_init    = s390_virtio_net_class_init,
};

static void s390_virtio_blk_class_init(ObjectClass *klass, void *data)
{
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_blk_init;
}

static const TypeInfo s390_virtio_blk = {
    .name          = "virtio-blk-s390",
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIOBlkS390),
    .instance_init = s390_virtio_blk_instance_init,
    .class_init    = s390_virtio_blk_class_init,
};

static Property s390_virtio_serial_properties[] = {
    DEFINE_VIRTIO_SERIAL_PROPERTIES(VirtIOSerialS390, vdev.serial),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_serial_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_serial_init;
    dc->props = s390_virtio_serial_properties;
}

static const TypeInfo s390_virtio_serial = {
    .name          = TYPE_VIRTIO_SERIAL_S390,
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIOSerialS390),
    .instance_init = s390_virtio_serial_instance_init,
    .class_init    = s390_virtio_serial_class_init,
};

static Property s390_virtio_rng_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOS390Device, host_features),
    DEFINE_VIRTIO_RNG_PROPERTIES(VirtIORNGS390, vdev.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_rng_init;
    dc->props = s390_virtio_rng_properties;
}

static const TypeInfo s390_virtio_rng = {
    .name          = TYPE_VIRTIO_RNG_S390,
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIORNGS390),
    .instance_init = s390_virtio_rng_instance_init,
    .class_init    = s390_virtio_rng_class_init,
};

static int s390_virtio_busdev_init(DeviceState *dev)
{
    VirtIOS390Device *_dev = (VirtIOS390Device *)dev;
    VirtIOS390DeviceClass *_info = VIRTIO_S390_DEVICE_GET_CLASS(dev);

    virtio_s390_bus_new(&_dev->bus, sizeof(_dev->bus), _dev);

    return _info->init(_dev);
}

static void s390_virtio_busdev_reset(DeviceState *dev)
{
    VirtIOS390Device *_dev = (VirtIOS390Device *)dev;

    virtio_reset(_dev->vdev);
}

static void virtio_s390_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->init = s390_virtio_busdev_init;
    dc->bus_type = TYPE_S390_VIRTIO_BUS;
    dc->unplug = qdev_simple_unplug_cb;
    dc->reset = s390_virtio_busdev_reset;
}

static const TypeInfo virtio_s390_device_info = {
    .name = TYPE_VIRTIO_S390_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VirtIOS390Device),
    .class_init = virtio_s390_device_class_init,
    .class_size = sizeof(VirtIOS390DeviceClass),
    .abstract = true,
};

static Property s390_virtio_scsi_properties[] = {
    DEFINE_VIRTIO_SCSI_PROPERTIES(VirtIOSCSIS390, vdev.parent_obj.conf),
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOS390Device, host_features),
    DEFINE_VIRTIO_SCSI_FEATURES(VirtIOS390Device, host_features),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_virtio_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_virtio_scsi_init;
    dc->props = s390_virtio_scsi_properties;
}

static const TypeInfo s390_virtio_scsi = {
    .name          = TYPE_VIRTIO_SCSI_S390,
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VirtIOSCSIS390),
    .instance_init = s390_virtio_scsi_instance_init,
    .class_init    = s390_virtio_scsi_class_init,
};

#ifdef CONFIG_VHOST_SCSI
static Property s390_vhost_scsi_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOS390Device, host_features),
    DEFINE_VHOST_SCSI_PROPERTIES(VHostSCSIS390, vdev.parent_obj.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void s390_vhost_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOS390DeviceClass *k = VIRTIO_S390_DEVICE_CLASS(klass);

    k->init = s390_vhost_scsi_init;
    dc->props = s390_vhost_scsi_properties;
}

static const TypeInfo s390_vhost_scsi = {
    .name          = TYPE_VHOST_SCSI_S390,
    .parent        = TYPE_VIRTIO_S390_DEVICE,
    .instance_size = sizeof(VHostSCSIS390),
    .instance_init = s390_vhost_scsi_instance_init,
    .class_init    = s390_vhost_scsi_class_init,
};
#endif

/***************** S390 Virtio Bus Bridge Device *******************/
/* Only required to have the virtio bus as child in the system bus */

static int s390_virtio_bridge_init(SysBusDevice *dev)
{
    /* nothing */
    return 0;
}

static void s390_virtio_bridge_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = s390_virtio_bridge_init;
}

static const TypeInfo s390_virtio_bridge_info = {
    .name          = "s390-virtio-bridge",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = s390_virtio_bridge_class_init,
};

/* virtio-s390-bus */

static void virtio_s390_bus_new(VirtioBusState *bus, size_t bus_size,
                                VirtIOS390Device *dev)
{
    DeviceState *qdev = DEVICE(dev);
    BusState *qbus;
    char virtio_bus_name[] = "virtio-bus";

    qbus_create_inplace(bus, bus_size, TYPE_VIRTIO_S390_BUS,
                        qdev, virtio_bus_name);
    qbus = BUS(bus);
    qbus->allow_hotplug = 1;
}

static void virtio_s390_bus_class_init(ObjectClass *klass, void *data)
{
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    BusClass *bus_class = BUS_CLASS(klass);
    bus_class->max_dev = 1;
    k->notify = virtio_s390_notify;
    k->get_features = virtio_s390_get_features;
}

static const TypeInfo virtio_s390_bus_info = {
    .name          = TYPE_VIRTIO_S390_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioS390BusState),
    .class_init    = virtio_s390_bus_class_init,
};

static void s390_virtio_register_types(void)
{
    type_register_static(&virtio_s390_bus_info);
    type_register_static(&s390_virtio_bus_info);
    type_register_static(&virtio_s390_device_info);
    type_register_static(&s390_virtio_serial);
    type_register_static(&s390_virtio_blk);
    type_register_static(&s390_virtio_net);
    type_register_static(&s390_virtio_scsi);
#ifdef CONFIG_VHOST_SCSI
    type_register_static(&s390_vhost_scsi);
#endif
    type_register_static(&s390_virtio_rng);
    type_register_static(&s390_virtio_bridge_info);
}

type_init(s390_virtio_register_types)
