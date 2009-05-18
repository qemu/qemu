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
#include "pci.h"
#include "sysemu.h"

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

#define VIRTIO_PCI_CONFIG               20

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

    uint16_t vendor;
    uint16_t device;
    uint16_t subvendor;
    uint16_t class_code;
    uint8_t pif;
} VirtIOPCIProxy;

/* virtio device */

static void virtio_pci_update_irq(void *opaque)
{
    VirtIOPCIProxy *proxy = opaque;

    qemu_set_irq(proxy->pci_dev.irq[0], proxy->vdev->isr & 1);
}

static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = proxy->vdev;
    target_phys_addr_t pa;

    addr -= proxy->addr;

    switch (addr) {
    case VIRTIO_PCI_GUEST_FEATURES:
	/* Guest does not negotiate properly?  We have to assume nothing. */
	if (val & (1 << VIRTIO_F_BAD_FEATURE)) {
	    if (vdev->bad_features)
		val = vdev->bad_features(vdev);
	    else
		val = 0;
	}
        if (vdev->set_features)
            vdev->set_features(vdev, val);
        vdev->features = val;
        break;
    case VIRTIO_PCI_QUEUE_PFN:
        pa = (target_phys_addr_t)val << VIRTIO_PCI_QUEUE_ADDR_SHIFT;
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
        if (vdev->status == 0)
            virtio_reset(vdev);
        break;
    }
}

static uint32_t virtio_ioport_read(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = proxy->vdev;
    uint32_t ret = 0xFFFFFFFF;

    addr -= proxy->addr;

    switch (addr) {
    case VIRTIO_PCI_HOST_FEATURES:
        ret = vdev->get_features(vdev);
        ret |= (1 << VIRTIO_F_NOTIFY_ON_EMPTY) | (1 << VIRTIO_F_BAD_FEATURE);
        break;
    case VIRTIO_PCI_GUEST_FEATURES:
        ret = vdev->features;
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
        virtio_update_irq(vdev);
        break;
    default:
        break;
    }

    return ret;
}

static uint32_t virtio_pci_config_readb(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    addr -= proxy->addr + VIRTIO_PCI_CONFIG;
    return virtio_config_readb(proxy->vdev, addr);
}

static uint32_t virtio_pci_config_readw(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    addr -= proxy->addr + VIRTIO_PCI_CONFIG;
    return virtio_config_readw(proxy->vdev, addr);
}

static uint32_t virtio_pci_config_readl(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    addr -= proxy->addr + VIRTIO_PCI_CONFIG;
    return virtio_config_readl(proxy->vdev, addr);
}

static void virtio_pci_config_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    addr -= proxy->addr + VIRTIO_PCI_CONFIG;
    virtio_config_writeb(proxy->vdev, addr, val);
}

static void virtio_pci_config_writew(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    addr -= proxy->addr + VIRTIO_PCI_CONFIG;
    virtio_config_writew(proxy->vdev, addr, val);
}

static void virtio_pci_config_writel(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    addr -= proxy->addr + VIRTIO_PCI_CONFIG;
    virtio_config_writel(proxy->vdev, addr, val);
}

static void virtio_map(PCIDevice *pci_dev, int region_num,
                       uint32_t addr, uint32_t size, int type)
{
    VirtIOPCIProxy *proxy = container_of(pci_dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = proxy->vdev;
    int i;

    proxy->addr = addr;
    for (i = 0; i < 3; i++) {
        register_ioport_write(addr, VIRTIO_PCI_CONFIG, 1 << i,
                              virtio_ioport_write, proxy);
        register_ioport_read(addr, VIRTIO_PCI_CONFIG, 1 << i,
                             virtio_ioport_read, proxy);
    }

    if (vdev->config_len) {
        register_ioport_write(addr + VIRTIO_PCI_CONFIG, vdev->config_len, 1,
                              virtio_pci_config_writeb, proxy);
        register_ioport_write(addr + VIRTIO_PCI_CONFIG, vdev->config_len, 2,
                              virtio_pci_config_writew, proxy);
        register_ioport_write(addr + VIRTIO_PCI_CONFIG, vdev->config_len, 4,
                              virtio_pci_config_writel, proxy);
        register_ioport_read(addr + VIRTIO_PCI_CONFIG, vdev->config_len, 1,
                             virtio_pci_config_readb, proxy);
        register_ioport_read(addr + VIRTIO_PCI_CONFIG, vdev->config_len, 2,
                             virtio_pci_config_readw, proxy);
        register_ioport_read(addr + VIRTIO_PCI_CONFIG, vdev->config_len, 4,
                             virtio_pci_config_readl, proxy);

        vdev->get_config(vdev, vdev->config);
    }
}

static const VirtIOBindings virtio_pci_bindings = {
    .update_irq = virtio_pci_update_irq
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

    size = 20 + vdev->config_len;
    if (size & (size-1))
        size = 1 << qemu_fls(size);

    pci_register_io_region(&proxy->pci_dev, 0, size, PCI_ADDRESS_SPACE_IO,
                           virtio_map);

    virtio_bind_device(vdev, &virtio_pci_bindings, proxy);
}

static void virtio_blk_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_blk_init(&pci_dev->qdev);
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_BLOCK,
                    PCI_CLASS_STORAGE_OTHER,
                    0x00);
}

static void virtio_console_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_console_init(&pci_dev->qdev);
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_CONSOLE,
                    PCI_CLASS_DISPLAY_OTHER,
                    0x00);
}

static void virtio_net_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_net_init(&pci_dev->qdev);
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_NET,
                    PCI_CLASS_NETWORK_ETHERNET,
                    0x00);
}

static void virtio_balloon_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_balloon_init(&pci_dev->qdev);
    virtio_init_pci(proxy, vdev,
                    PCI_VENDOR_ID_REDHAT_QUMRANET,
                    PCI_DEVICE_ID_VIRTIO_BALLOON,
                    PCI_CLASS_MEMORY_RAM,
                    0x00);
}

static void virtio_pci_register_devices(void)
{
    pci_qdev_register("virtio-blk-pci", sizeof(VirtIOPCIProxy),
                      virtio_blk_init_pci);
    pci_qdev_register("virtio-net-pci", sizeof(VirtIOPCIProxy),
                      virtio_net_init_pci);
    pci_qdev_register("virtio-console-pci", sizeof(VirtIOPCIProxy),
                      virtio_console_init_pci);
    pci_qdev_register("virtio-balloon-pci", sizeof(VirtIOPCIProxy),
                      virtio_balloon_init_pci);
}

device_init(virtio_pci_register_devices)
