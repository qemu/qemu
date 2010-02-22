/*
 * Virtio PCI Bindings
 *
 * Copyright IBM, Corp. 2007
 * Copyright (c) 2009 CodeSourcery
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paul Brook        <paul@codesourcery.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <inttypes.h>

#include "virtio.h"
#include "virtio-blk.h"
#include "virtio-net.h"
#include "pci.h"
#include "sysemu.h"
#include "msix.h"
#include "net.h"
#include "block_int.h"
#include "loader.h"

/* from Linux's linux/virtio_pci.h */

/* A 32-bit r/o bitmask of the features supported by the host */
#define VIRTIO_PCI_HOST_FEATURES        0

/* A 32-bit r/w bitmask of features activated by the guest */
#define VIRTIO_PCI_GUEST_FEATURES       4

/* A 32-bit r/w PFN for the currently selected queue */
#define VIRTIO_PCI_QUEUE_PFN            8

/* A 16-bit r/o queue size for the currently selected queue */
#define VIRTIO_PCI_QUEUE_NUM            12

/* A 16-bit r/w queue selector */
#define VIRTIO_PCI_QUEUE_SEL            14

/* A 16-bit r/w queue notifier */
#define VIRTIO_PCI_QUEUE_NOTIFY         16

/* An 8-bit device status register.  */
#define VIRTIO_PCI_STATUS               18

/* An 8-bit r/o interrupt status register.  Reading the value will return the
 * current contents of the ISR and will also clear it.  This is effectively
 * a read-and-acknowledge. */
#define VIRTIO_PCI_ISR                  19

/* MSI-X registers: only enabled if MSI-X is enabled. */
/* A 16-bit vector for configuration changes. */
#define VIRTIO_MSI_CONFIG_VECTOR        20
/* A 16-bit vector for selected queue notifications. */
#define VIRTIO_MSI_QUEUE_VECTOR         22

/* Config space size */
#define VIRTIO_PCI_CONFIG_NOMSI         20
#define VIRTIO_PCI_CONFIG_MSI           24
#define VIRTIO_PCI_REGION_SIZE(dev)     (msix_present(dev) ? \
                                         VIRTIO_PCI_CONFIG_MSI : \
                                         VIRTIO_PCI_CONFIG_NOMSI)

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG(dev)          (msix_enabled(dev) ? \
                                         VIRTIO_PCI_CONFIG_MSI : \
                                         VIRTIO_PCI_CONFIG_NOMSI)

/* Virtio ABI version, if we increment this, we break the guest driver. */
#define VIRTIO_PCI_ABI_VERSION          0

/* How many bits to shift physical queue address written to QUEUE_PFN.
 * 12 is historical, and due to x86 page size. */
#define VIRTIO_PCI_QUEUE_ADDR_SHIFT    12

/* QEMU doesn't strictly need write barriers since everything runs in
 * lock-step.  We'll leave the calls to wmb() in though to make it obvious for
 * KVM or if kqemu gets SMP support.
 */
#define wmb() do { } while (0)

/* PCI bindings.  */

typedef struct {
    PCIDevice pci_dev;
    VirtIODevice *vdev;
    uint32_t addr;
    uint32_t class_code;
    uint32_t nvectors;
    BlockConf block;
    NICConf nic;
    uint32_t host_features;
    /* Max. number of ports we can have for a the virtio-serial device */
    uint32_t max_virtserial_ports;
} VirtIOPCIProxy;

/* virtio device */

static void virtio_pci_notify(void *opaque, uint16_t vector)
{
    VirtIOPCIProxy *proxy = opaque;
    if (msix_enabled(&proxy->pci_dev))
        msix_notify(&proxy->pci_dev, vector);
    else
        qemu_set_irq(proxy->pci_dev.irq[0], proxy->vdev->isr & 1);
}

static void virtio_pci_save_config(void * opaque, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = opaque;
    pci_device_save(&proxy->pci_dev, f);
    msix_save(&proxy->pci_dev, f);
    if (msix_present(&proxy->pci_dev))
        qemu_put_be16(f, proxy->vdev->config_vector);
}

static void virtio_pci_save_queue(void * opaque, int n, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = opaque;
    if (msix_present(&proxy->pci_dev))
        qemu_put_be16(f, virtio_queue_vector(proxy->vdev, n));
}

static int virtio_pci_load_config(void * opaque, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = opaque;
    int ret;
    ret = pci_device_load(&proxy->pci_dev, f);
    if (ret) {
        return ret;
    }
    msix_load(&proxy->pci_dev, f);
    if (msix_present(&proxy->pci_dev)) {
        qemu_get_be16s(f, &proxy->vdev->config_vector);
    } else {
        proxy->vdev->config_vector = VIRTIO_NO_VECTOR;
    }
    if (proxy->vdev->config_vector != VIRTIO_NO_VECTOR) {
        return msix_vector_use(&proxy->pci_dev, proxy->vdev->config_vector);
    }
    return 0;
}

static int virtio_pci_load_queue(void * opaque, int n, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = opaque;
    uint16_t vector;
    if (msix_present(&proxy->pci_dev)) {
        qemu_get_be16s(f, &vector);
    } else {
        vector = VIRTIO_NO_VECTOR;
    }
    virtio_queue_set_vector(proxy->vdev, n, vector);
    if (vector != VIRTIO_NO_VECTOR) {
        return msix_vector_use(&proxy->pci_dev, vector);
    }
    return 0;
}

static void virtio_pci_reset(DeviceState *d)
{
    VirtIOPCIProxy *proxy = container_of(d, VirtIOPCIProxy, pci_dev.qdev);
    virtio_reset(proxy->vdev);
    msix_reset(&proxy->pci_dev);
}

static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = proxy->vdev;
    target_phys_addr_t pa;

    switch (addr) {
    case VIRTIO_PCI_GUEST_FEATURES:
	/* Guest does not negotiate properly?  We have to assume nothing. */
	if (val & (1 << VIRTIO_F_BAD_FEATURE)) {
	    if (vdev->bad_features)
		val = proxy->host_features & vdev->bad_features(vdev);
	    else
		val = 0;
	}
        if (vdev->set_features)
            vdev->set_features(vdev, val);
        vdev->guest_features = val;
        break;
    case VIRTIO_PCI_QUEUE_PFN:
        pa = (target_phys_addr_t)val << VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        if (pa == 0) {
            virtio_reset(proxy->vdev);
            msix_unuse_all_vectors(&proxy->pci_dev);
        }
        else
            virtio_queue_set_addr(vdev, vdev->queue_sel, pa);
        break;
    case VIRTIO_PCI_QUEUE_SEL:
        if (val < VIRTIO_PCI_QUEUE_MAX)
            vdev->queue_sel = val;
        break;
    case VIRTIO_PCI_QUEUE_NOTIFY:
        virtio_queue_notify(vdev, val);
        break;
    case VIRTIO_PCI_STATUS:
        vdev->status = val & 0xFF;
        if (vdev->status == 0) {
            virtio_reset(proxy->vdev);
            msix_unuse_all_vectors(&proxy->pci_dev);
        }
        break;
    case VIRTIO_MSI_CONFIG_VECTOR:
        msix_vector_unuse(&proxy->pci_dev, vdev->config_vector);
        /* Make it possible for guest to discover an error took place. */
        if (msix_vector_use(&proxy->pci_dev, val) < 0)
            val = VIRTIO_NO_VECTOR;
        vdev->config_vector = val;
        break;
    case VIRTIO_MSI_QUEUE_VECTOR:
        msix_vector_unuse(&proxy->pci_dev,
                          virtio_queue_vector(vdev, vdev->queue_sel));
        /* Make it possible for guest to discover an error took place. */
        if (msix_vector_use(&proxy->pci_dev, val) < 0)
            val = VIRTIO_NO_VECTOR;
        virtio_queue_set_vector(vdev, vdev->queue_sel, val);
        break;
    default:
        fprintf(stderr, "%s: unexpected address 0x%x value 0x%x\n",
                __func__, addr, val);
        break;
    }
}

static uint32_t virtio_ioport_read(VirtIOPCIProxy *proxy, uint32_t addr)
{
    VirtIODevice *vdev = proxy->vdev;
    uint32_t ret = 0xFFFFFFFF;

    switch (addr) {
    case VIRTIO_PCI_HOST_FEATURES:
        ret = proxy->host_features;
        break;
    case VIRTIO_PCI_GUEST_FEATURES:
        ret = vdev->guest_features;
        break;
    case VIRTIO_PCI_QUEUE_PFN:
        ret = virtio_queue_get_addr(vdev, vdev->queue_sel)
              >> VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        break;
    case VIRTIO_PCI_QUEUE_NUM:
        ret = virtio_queue_get_num(vdev, vdev->queue_sel);
        break;
    case VIRTIO_PCI_QUEUE_SEL:
        ret = vdev->queue_sel;
        break;
    case VIRTIO_PCI_STATUS:
        ret = vdev->status;
        break;
    case VIRTIO_PCI_ISR:
        /* reading from the ISR also clears it. */
        ret = vdev->isr;
        vdev->isr = 0;
        qemu_set_irq(proxy->pci_dev.irq[0], 0);
        break;
    case VIRTIO_MSI_CONFIG_VECTOR:
        ret = vdev->config_vector;
        break;
    case VIRTIO_MSI_QUEUE_VECTOR:
        ret = virtio_queue_vector(vdev, vdev->queue_sel);
        break;
    default:
        break;
    }

    return ret;
}

static uint32_t virtio_pci_config_readb(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    addr -= proxy->addr;
    if (addr < config)
        return virtio_ioport_read(proxy, addr);
    addr -= config;
    return virtio_config_readb(proxy->vdev, addr);
}

static uint32_t virtio_pci_config_readw(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    addr -= proxy->addr;
    if (addr < config)
        return virtio_ioport_read(proxy, addr);
    addr -= config;
    return virtio_config_readw(proxy->vdev, addr);
}

static uint32_t virtio_pci_config_readl(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    addr -= proxy->addr;
    if (addr < config)
        return virtio_ioport_read(proxy, addr);
    addr -= config;
    return virtio_config_readl(proxy->vdev, addr);
}

static void virtio_pci_config_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    addr -= proxy->addr;
    if (addr < config) {
        virtio_ioport_write(proxy, addr, val);
        return;
    }
    addr -= config;
    virtio_config_writeb(proxy->vdev, addr, val);
}

static void virtio_pci_config_writew(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    addr -= proxy->addr;
    if (addr < config) {
        virtio_ioport_write(proxy, addr, val);
        return;
    }
    addr -= config;
    virtio_config_writew(proxy->vdev, addr, val);
}

static void virtio_pci_config_writel(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    addr -= proxy->addr;
    if (addr < config) {
        virtio_ioport_write(proxy, addr, val);
        return;
    }
    addr -= config;
    virtio_config_writel(proxy->vdev, addr, val);
}

static void virtio_map(PCIDevice *pci_dev, int region_num,
                       pcibus_t addr, pcibus_t size, int type)
{
    VirtIOPCIProxy *proxy = container_of(pci_dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = proxy->vdev;
    unsigned config_len = VIRTIO_PCI_REGION_SIZE(pci_dev) + vdev->config_len;

    proxy->addr = addr;

    register_ioport_write(addr, config_len, 1, virtio_pci_config_writeb, proxy);
    register_ioport_write(addr, config_len, 2, virtio_pci_config_writew, proxy);
    register_ioport_write(addr, config_len, 4, virtio_pci_config_writel, proxy);
    register_ioport_read(addr, config_len, 1, virtio_pci_config_readb, proxy);
    register_ioport_read(addr, config_len, 2, virtio_pci_config_readw, proxy);
    register_ioport_read(addr, config_len, 4, virtio_pci_config_readl, proxy);

    if (vdev->config_len)
        vdev->get_config(vdev, vdev->config);
}

static void virtio_write_config(PCIDevice *pci_dev, uint32_t address,
                                uint32_t val, int len)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    if (PCI_COMMAND == address) {
        if (!(val & PCI_COMMAND_MASTER)) {
            proxy->vdev->status &= ~VIRTIO_CONFIG_S_DRIVER_OK;
        }
    }

    pci_default_write_config(pci_dev, address, val, len);
    msix_write_config(pci_dev, address, val, len);
}

static unsigned virtio_pci_get_features(void *opaque)
{
    VirtIOPCIProxy *proxy = opaque;
    return proxy->host_features;
}

static const VirtIOBindings virtio_pci_bindings = {
    .notify = virtio_pci_notify,
    .save_config = virtio_pci_save_config,
    .load_config = virtio_pci_load_config,
    .save_queue = virtio_pci_save_queue,
    .load_queue = virtio_pci_load_queue,
    .get_features = virtio_pci_get_features,
};

static void virtio_init_pci(VirtIOPCIProxy *proxy, VirtIODevice *vdev,
                            uint16_t vendor, uint16_t device,
                            uint16_t class_code, uint8_t pif)
{
    uint8_t *config;
    uint32_t size;

    proxy->vdev = vdev;

    config = proxy->pci_dev.config;
    pci_config_set_vendor_id(config, vendor);
    pci_config_set_device_id(config, device);

    config[0x08] = VIRTIO_PCI_ABI_VERSION;

    config[0x09] = pif;
    pci_config_set_class(config, class_code);
    config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL;

    config[0x2c] = vendor & 0xFF;
    config[0x2d] = (vendor >> 8) & 0xFF;
    config[0x2e] = vdev->device_id & 0xFF;
    config[0x2f] = (vdev->device_id >> 8) & 0xFF;

    config[0x3d] = 1;

    if (vdev->nvectors && !msix_init(&proxy->pci_dev, vdev->nvectors, 1, 0)) {
        pci_register_bar(&proxy->pci_dev, 1,
                         msix_bar_size(&proxy->pci_dev),
                         PCI_BASE_ADDRESS_SPACE_MEMORY,
                         msix_mmio_map);
    } else
        vdev->nvectors = 0;

    proxy->pci_dev.config_write = virtio_write_config;

    size = VIRTIO_PCI_REGION_SIZE(&proxy->pci_dev) + vdev->config_len;
    if (size & (size-1))
        size = 1 << qemu_fls(size);

    pci_register_bar(&proxy->pci_dev, 0, size, PCI_BASE_ADDRESS_SPACE_IO,
                           virtio_map);

    virtio_bind_device(vdev, &virtio_pci_bindings, proxy);
    proxy->host_features |= 0x1 << VIRTIO_F_NOTIFY_ON_EMPTY;
    proxy->host_features |= 0x1 << VIRTIO_F_BAD_FEATURE;
    proxy->host_features = vdev->get_features(vdev, proxy->host_features);
}

static int virtio_blk_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    if (proxy->class_code != PCI_CLASS_STORAGE_SCSI &&
        proxy->class_code != PCI_CLASS_STORAGE_OTHER)
        proxy->class_code = PCI_CLASS_STORAGE_SCSI;

    if (!proxy->block.dinfo) {
        qemu_error("virtio-blk-pci: drive property not set\n");
        return -1;
    }
    vdev = virtio_blk_init(&pci_dev->qdev, &proxy->block);
    vdev->nvectors = proxy->nvectors;
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_BLOCK,
                    proxy->class_code, 0x00);
    /* make the actual value visible */
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static int virtio_exit_pci(PCIDevice *pci_dev)
{
    return msix_uninit(pci_dev);
}

static int virtio_blk_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    drive_uninit(proxy->block.dinfo);
    return virtio_exit_pci(pci_dev);
}

static int virtio_serial_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    if (proxy->class_code != PCI_CLASS_COMMUNICATION_OTHER &&
        proxy->class_code != PCI_CLASS_DISPLAY_OTHER && /* qemu 0.10 */
        proxy->class_code != PCI_CLASS_OTHERS)          /* qemu-kvm  */
        proxy->class_code = PCI_CLASS_COMMUNICATION_OTHER;

    vdev = virtio_serial_init(&pci_dev->qdev, proxy->max_virtserial_ports);
    if (!vdev) {
        return -1;
    }
    vdev->nvectors = proxy->nvectors == -1 ? proxy->max_virtserial_ports + 1
                                           : proxy->nvectors;
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_CONSOLE,
                    proxy->class_code, 0x00);
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static int virtio_net_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_net_init(&pci_dev->qdev, &proxy->nic);

    vdev->nvectors = proxy->nvectors;
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_NET,
                    PCI_CLASS_NETWORK_ETHERNET,
                    0x00);

    /* make the actual value visible */
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static int virtio_net_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_net_exit(proxy->vdev);
    return virtio_exit_pci(pci_dev);
}

static int virtio_balloon_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_balloon_init(&pci_dev->qdev);
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_BALLOON,
                    PCI_CLASS_MEMORY_RAM,
                    0x00);
    return 0;
}

static PCIDeviceInfo virtio_info[] = {
    {
        .qdev.name = "virtio-blk-pci",
        .qdev.size = sizeof(VirtIOPCIProxy),
        .init      = virtio_blk_init_pci,
        .exit      = virtio_blk_exit_pci,
        .qdev.props = (Property[]) {
            DEFINE_PROP_HEX32("class", VirtIOPCIProxy, class_code, 0),
            DEFINE_BLOCK_PROPERTIES(VirtIOPCIProxy, block),
            DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
            DEFINE_VIRTIO_BLK_FEATURES(VirtIOPCIProxy, host_features),
            DEFINE_PROP_END_OF_LIST(),
        },
        .qdev.reset = virtio_pci_reset,
    },{
        .qdev.name  = "virtio-net-pci",
        .qdev.size  = sizeof(VirtIOPCIProxy),
        .init       = virtio_net_init_pci,
        .exit       = virtio_net_exit_pci,
        .romfile    = "pxe-virtio.bin",
        .qdev.props = (Property[]) {
            DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
            DEFINE_VIRTIO_NET_FEATURES(VirtIOPCIProxy, host_features),
            DEFINE_NIC_PROPERTIES(VirtIOPCIProxy, nic),
            DEFINE_PROP_END_OF_LIST(),
        },
        .qdev.reset = virtio_pci_reset,
    },{
        .qdev.name = "virtio-serial-pci",
        .qdev.alias = "virtio-serial",
        .qdev.size = sizeof(VirtIOPCIProxy),
        .init      = virtio_serial_init_pci,
        .exit      = virtio_exit_pci,
        .qdev.props = (Property[]) {
            DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, -1),
            DEFINE_PROP_HEX32("class", VirtIOPCIProxy, class_code, 0),
            DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
            DEFINE_PROP_UINT32("max_ports", VirtIOPCIProxy, max_virtserial_ports,
                               31),
            DEFINE_PROP_END_OF_LIST(),
        },
        .qdev.reset = virtio_pci_reset,
    },{
        .qdev.name = "virtio-balloon-pci",
        .qdev.size = sizeof(VirtIOPCIProxy),
        .init      = virtio_balloon_init_pci,
        .exit      = virtio_exit_pci,
        .qdev.props = (Property[]) {
            DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
            DEFINE_PROP_END_OF_LIST(),
        },
        .qdev.reset = virtio_pci_reset,
    },{
        /* end of list */
    }
};

static void virtio_pci_register_devices(void)
{
    pci_qdev_register_many(virtio_info);
}

device_init(virtio_pci_register_devices)
