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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include <inttypes.h>

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-serial.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/virtio-balloon.h"
#include "hw/pci/pci.h"
#include "qemu/error-report.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/loader.h"
#include "sysemu/kvm.h"
#include "sysemu/blockdev.h"
#include "virtio-pci.h"
#include "qemu/range.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/visitor.h"

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

/* How many bits to shift physical queue address written to QUEUE_PFN.
 * 12 is historical, and due to x86 page size. */
#define VIRTIO_PCI_QUEUE_ADDR_SHIFT    12

/* Flags track per-device state like workarounds for quirks in older guests. */
#define VIRTIO_PCI_FLAG_BUS_MASTER_BUG  (1 << 0)

static void virtio_pci_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtIOPCIProxy *dev);

/* virtio device */
/* DeviceState to VirtIOPCIProxy. For use off data-path. TODO: use QOM. */
static inline VirtIOPCIProxy *to_virtio_pci_proxy(DeviceState *d)
{
    return container_of(d, VirtIOPCIProxy, pci_dev.qdev);
}

/* DeviceState to VirtIOPCIProxy. Note: used on datapath,
 * be careful and test performance if you change this.
 */
static inline VirtIOPCIProxy *to_virtio_pci_proxy_fast(DeviceState *d)
{
    return container_of(d, VirtIOPCIProxy, pci_dev.qdev);
}

static void virtio_pci_notify(DeviceState *d, uint16_t vector)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy_fast(d);

    if (msix_enabled(&proxy->pci_dev))
        msix_notify(&proxy->pci_dev, vector);
    else {
        VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
        pci_set_irq(&proxy->pci_dev, vdev->isr & 1);
    }
}

static void virtio_pci_save_config(DeviceState *d, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    pci_device_save(&proxy->pci_dev, f);
    msix_save(&proxy->pci_dev, f);
    if (msix_present(&proxy->pci_dev))
        qemu_put_be16(f, vdev->config_vector);
}

static void virtio_pci_save_queue(DeviceState *d, int n, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (msix_present(&proxy->pci_dev))
        qemu_put_be16(f, virtio_queue_vector(vdev, n));
}

static int virtio_pci_load_config(DeviceState *d, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    int ret;
    ret = pci_device_load(&proxy->pci_dev, f);
    if (ret) {
        return ret;
    }
    msix_unuse_all_vectors(&proxy->pci_dev);
    msix_load(&proxy->pci_dev, f);
    if (msix_present(&proxy->pci_dev)) {
        qemu_get_be16s(f, &vdev->config_vector);
    } else {
        vdev->config_vector = VIRTIO_NO_VECTOR;
    }
    if (vdev->config_vector != VIRTIO_NO_VECTOR) {
        return msix_vector_use(&proxy->pci_dev, vdev->config_vector);
    }
    return 0;
}

static int virtio_pci_load_queue(DeviceState *d, int n, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    uint16_t vector;
    if (msix_present(&proxy->pci_dev)) {
        qemu_get_be16s(f, &vector);
    } else {
        vector = VIRTIO_NO_VECTOR;
    }
    virtio_queue_set_vector(vdev, n, vector);
    if (vector != VIRTIO_NO_VECTOR) {
        return msix_vector_use(&proxy->pci_dev, vector);
    }
    return 0;
}

static int virtio_pci_set_host_notifier_internal(VirtIOPCIProxy *proxy,
                                                 int n, bool assign, bool set_handler)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_host_notifier(vq);
    int r = 0;

    if (assign) {
        r = event_notifier_init(notifier, 1);
        if (r < 0) {
            error_report("%s: unable to init event notifier: %d",
                         __func__, r);
            return r;
        }
        virtio_queue_set_host_notifier_fd_handler(vq, true, set_handler);
        memory_region_add_eventfd(&proxy->bar, VIRTIO_PCI_QUEUE_NOTIFY, 2,
                                  true, n, notifier);
    } else {
        memory_region_del_eventfd(&proxy->bar, VIRTIO_PCI_QUEUE_NOTIFY, 2,
                                  true, n, notifier);
        virtio_queue_set_host_notifier_fd_handler(vq, false, false);
        event_notifier_cleanup(notifier);
    }
    return r;
}

static void virtio_pci_start_ioeventfd(VirtIOPCIProxy *proxy)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    int n, r;

    if (!(proxy->flags & VIRTIO_PCI_FLAG_USE_IOEVENTFD) ||
        proxy->ioeventfd_disabled ||
        proxy->ioeventfd_started) {
        return;
    }

    for (n = 0; n < VIRTIO_PCI_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        r = virtio_pci_set_host_notifier_internal(proxy, n, true, true);
        if (r < 0) {
            goto assign_error;
        }
    }
    proxy->ioeventfd_started = true;
    return;

assign_error:
    while (--n >= 0) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        r = virtio_pci_set_host_notifier_internal(proxy, n, false, false);
        assert(r >= 0);
    }
    proxy->ioeventfd_started = false;
    error_report("%s: failed. Fallback to a userspace (slower).", __func__);
}

static void virtio_pci_stop_ioeventfd(VirtIOPCIProxy *proxy)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    int r;
    int n;

    if (!proxy->ioeventfd_started) {
        return;
    }

    for (n = 0; n < VIRTIO_PCI_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        r = virtio_pci_set_host_notifier_internal(proxy, n, false, false);
        assert(r >= 0);
    }
    proxy->ioeventfd_started = false;
}

static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    hwaddr pa;

    switch (addr) {
    case VIRTIO_PCI_GUEST_FEATURES:
        /* Guest does not negotiate properly?  We have to assume nothing. */
        if (val & (1 << VIRTIO_F_BAD_FEATURE)) {
            val = virtio_bus_get_vdev_bad_features(&proxy->bus);
        }
        virtio_set_features(vdev, val);
        break;
    case VIRTIO_PCI_QUEUE_PFN:
        pa = (hwaddr)val << VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        if (pa == 0) {
            virtio_pci_stop_ioeventfd(proxy);
            virtio_reset(vdev);
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
        if (val < VIRTIO_PCI_QUEUE_MAX) {
            virtio_queue_notify(vdev, val);
        }
        break;
    case VIRTIO_PCI_STATUS:
        if (!(val & VIRTIO_CONFIG_S_DRIVER_OK)) {
            virtio_pci_stop_ioeventfd(proxy);
        }

        virtio_set_status(vdev, val & 0xFF);

        if (val & VIRTIO_CONFIG_S_DRIVER_OK) {
            virtio_pci_start_ioeventfd(proxy);
        }

        if (vdev->status == 0) {
            virtio_reset(vdev);
            msix_unuse_all_vectors(&proxy->pci_dev);
        }

        /* Linux before 2.6.34 sets the device as OK without enabling
           the PCI device bus master bit. In this case we need to disable
           some safety checks. */
        if ((val & VIRTIO_CONFIG_S_DRIVER_OK) &&
            !(proxy->pci_dev.config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
            proxy->flags |= VIRTIO_PCI_FLAG_BUS_MASTER_BUG;
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
        error_report("%s: unexpected address 0x%x value 0x%x",
                     __func__, addr, val);
        break;
    }
}

static uint32_t virtio_ioport_read(VirtIOPCIProxy *proxy, uint32_t addr)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
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
        pci_irq_deassert(&proxy->pci_dev);
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

static uint64_t virtio_pci_config_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    uint64_t val = 0;
    if (addr < config) {
        return virtio_ioport_read(proxy, addr);
    }
    addr -= config;

    switch (size) {
    case 1:
        val = virtio_config_readb(vdev, addr);
        break;
    case 2:
        val = virtio_config_readw(vdev, addr);
        if (virtio_is_big_endian(vdev)) {
            val = bswap16(val);
        }
        break;
    case 4:
        val = virtio_config_readl(vdev, addr);
        if (virtio_is_big_endian(vdev)) {
            val = bswap32(val);
        }
        break;
    }
    return val;
}

static void virtio_pci_config_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    if (addr < config) {
        virtio_ioport_write(proxy, addr, val);
        return;
    }
    addr -= config;
    /*
     * Virtio-PCI is odd. Ioports are LE but config space is target native
     * endian.
     */
    switch (size) {
    case 1:
        virtio_config_writeb(vdev, addr, val);
        break;
    case 2:
        if (virtio_is_big_endian(vdev)) {
            val = bswap16(val);
        }
        virtio_config_writew(vdev, addr, val);
        break;
    case 4:
        if (virtio_is_big_endian(vdev)) {
            val = bswap32(val);
        }
        virtio_config_writel(vdev, addr, val);
        break;
    }
}

static const MemoryRegionOps virtio_pci_config_ops = {
    .read = virtio_pci_config_read,
    .write = virtio_pci_config_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void virtio_write_config(PCIDevice *pci_dev, uint32_t address,
                                uint32_t val, int len)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    pci_default_write_config(pci_dev, address, val, len);

    if (range_covers_byte(address, len, PCI_COMMAND) &&
        !(pci_dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER) &&
        !(proxy->flags & VIRTIO_PCI_FLAG_BUS_MASTER_BUG)) {
        virtio_pci_stop_ioeventfd(proxy);
        virtio_set_status(vdev, vdev->status & ~VIRTIO_CONFIG_S_DRIVER_OK);
    }
}

static unsigned virtio_pci_get_features(DeviceState *d)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    return proxy->host_features;
}

static int kvm_virtio_pci_vq_vector_use(VirtIOPCIProxy *proxy,
                                        unsigned int queue_no,
                                        unsigned int vector,
                                        MSIMessage msg)
{
    VirtIOIRQFD *irqfd = &proxy->vector_irqfd[vector];
    int ret;

    if (irqfd->users == 0) {
        ret = kvm_irqchip_add_msi_route(kvm_state, msg);
        if (ret < 0) {
            return ret;
        }
        irqfd->virq = ret;
    }
    irqfd->users++;
    return 0;
}

static void kvm_virtio_pci_vq_vector_release(VirtIOPCIProxy *proxy,
                                             unsigned int vector)
{
    VirtIOIRQFD *irqfd = &proxy->vector_irqfd[vector];
    if (--irqfd->users == 0) {
        kvm_irqchip_release_virq(kvm_state, irqfd->virq);
    }
}

static int kvm_virtio_pci_irqfd_use(VirtIOPCIProxy *proxy,
                                 unsigned int queue_no,
                                 unsigned int vector)
{
    VirtIOIRQFD *irqfd = &proxy->vector_irqfd[vector];
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtQueue *vq = virtio_get_queue(vdev, queue_no);
    EventNotifier *n = virtio_queue_get_guest_notifier(vq);
    int ret;
    ret = kvm_irqchip_add_irqfd_notifier(kvm_state, n, NULL, irqfd->virq);
    return ret;
}

static void kvm_virtio_pci_irqfd_release(VirtIOPCIProxy *proxy,
                                      unsigned int queue_no,
                                      unsigned int vector)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtQueue *vq = virtio_get_queue(vdev, queue_no);
    EventNotifier *n = virtio_queue_get_guest_notifier(vq);
    VirtIOIRQFD *irqfd = &proxy->vector_irqfd[vector];
    int ret;

    ret = kvm_irqchip_remove_irqfd_notifier(kvm_state, n, irqfd->virq);
    assert(ret == 0);
}

static int kvm_virtio_pci_vector_use(VirtIOPCIProxy *proxy, int nvqs)
{
    PCIDevice *dev = &proxy->pci_dev;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    unsigned int vector;
    int ret, queue_no;
    MSIMessage msg;

    for (queue_no = 0; queue_no < nvqs; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        vector = virtio_queue_vector(vdev, queue_no);
        if (vector >= msix_nr_vectors_allocated(dev)) {
            continue;
        }
        msg = msix_get_message(dev, vector);
        ret = kvm_virtio_pci_vq_vector_use(proxy, queue_no, vector, msg);
        if (ret < 0) {
            goto undo;
        }
        /* If guest supports masking, set up irqfd now.
         * Otherwise, delay until unmasked in the frontend.
         */
        if (k->guest_notifier_mask) {
            ret = kvm_virtio_pci_irqfd_use(proxy, queue_no, vector);
            if (ret < 0) {
                kvm_virtio_pci_vq_vector_release(proxy, vector);
                goto undo;
            }
        }
    }
    return 0;

undo:
    while (--queue_no >= 0) {
        vector = virtio_queue_vector(vdev, queue_no);
        if (vector >= msix_nr_vectors_allocated(dev)) {
            continue;
        }
        if (k->guest_notifier_mask) {
            kvm_virtio_pci_irqfd_release(proxy, queue_no, vector);
        }
        kvm_virtio_pci_vq_vector_release(proxy, vector);
    }
    return ret;
}

static void kvm_virtio_pci_vector_release(VirtIOPCIProxy *proxy, int nvqs)
{
    PCIDevice *dev = &proxy->pci_dev;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    unsigned int vector;
    int queue_no;
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);

    for (queue_no = 0; queue_no < nvqs; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        vector = virtio_queue_vector(vdev, queue_no);
        if (vector >= msix_nr_vectors_allocated(dev)) {
            continue;
        }
        /* If guest supports masking, clean up irqfd now.
         * Otherwise, it was cleaned when masked in the frontend.
         */
        if (k->guest_notifier_mask) {
            kvm_virtio_pci_irqfd_release(proxy, queue_no, vector);
        }
        kvm_virtio_pci_vq_vector_release(proxy, vector);
    }
}

static int virtio_pci_vq_vector_unmask(VirtIOPCIProxy *proxy,
                                       unsigned int queue_no,
                                       unsigned int vector,
                                       MSIMessage msg)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    VirtQueue *vq = virtio_get_queue(vdev, queue_no);
    EventNotifier *n = virtio_queue_get_guest_notifier(vq);
    VirtIOIRQFD *irqfd;
    int ret = 0;

    if (proxy->vector_irqfd) {
        irqfd = &proxy->vector_irqfd[vector];
        if (irqfd->msg.data != msg.data || irqfd->msg.address != msg.address) {
            ret = kvm_irqchip_update_msi_route(kvm_state, irqfd->virq, msg);
            if (ret < 0) {
                return ret;
            }
        }
    }

    /* If guest supports masking, irqfd is already setup, unmask it.
     * Otherwise, set it up now.
     */
    if (k->guest_notifier_mask) {
        k->guest_notifier_mask(vdev, queue_no, false);
        /* Test after unmasking to avoid losing events. */
        if (k->guest_notifier_pending &&
            k->guest_notifier_pending(vdev, queue_no)) {
            event_notifier_set(n);
        }
    } else {
        ret = kvm_virtio_pci_irqfd_use(proxy, queue_no, vector);
    }
    return ret;
}

static void virtio_pci_vq_vector_mask(VirtIOPCIProxy *proxy,
                                             unsigned int queue_no,
                                             unsigned int vector)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);

    /* If guest supports masking, keep irqfd but mask it.
     * Otherwise, clean it up now.
     */ 
    if (k->guest_notifier_mask) {
        k->guest_notifier_mask(vdev, queue_no, true);
    } else {
        kvm_virtio_pci_irqfd_release(proxy, queue_no, vector);
    }
}

static int virtio_pci_vector_unmask(PCIDevice *dev, unsigned vector,
                                    MSIMessage msg)
{
    VirtIOPCIProxy *proxy = container_of(dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    int ret, queue_no;

    for (queue_no = 0; queue_no < proxy->nvqs_with_notifiers; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        if (virtio_queue_vector(vdev, queue_no) != vector) {
            continue;
        }
        ret = virtio_pci_vq_vector_unmask(proxy, queue_no, vector, msg);
        if (ret < 0) {
            goto undo;
        }
    }
    return 0;

undo:
    while (--queue_no >= 0) {
        if (virtio_queue_vector(vdev, queue_no) != vector) {
            continue;
        }
        virtio_pci_vq_vector_mask(proxy, queue_no, vector);
    }
    return ret;
}

static void virtio_pci_vector_mask(PCIDevice *dev, unsigned vector)
{
    VirtIOPCIProxy *proxy = container_of(dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    int queue_no;

    for (queue_no = 0; queue_no < proxy->nvqs_with_notifiers; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        if (virtio_queue_vector(vdev, queue_no) != vector) {
            continue;
        }
        virtio_pci_vq_vector_mask(proxy, queue_no, vector);
    }
}

static void virtio_pci_vector_poll(PCIDevice *dev,
                                   unsigned int vector_start,
                                   unsigned int vector_end)
{
    VirtIOPCIProxy *proxy = container_of(dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    int queue_no;
    unsigned int vector;
    EventNotifier *notifier;
    VirtQueue *vq;

    for (queue_no = 0; queue_no < proxy->nvqs_with_notifiers; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        vector = virtio_queue_vector(vdev, queue_no);
        if (vector < vector_start || vector >= vector_end ||
            !msix_is_masked(dev, vector)) {
            continue;
        }
        vq = virtio_get_queue(vdev, queue_no);
        notifier = virtio_queue_get_guest_notifier(vq);
        if (k->guest_notifier_pending) {
            if (k->guest_notifier_pending(vdev, queue_no)) {
                msix_set_pending(dev, vector);
            }
        } else if (event_notifier_test_and_clear(notifier)) {
            msix_set_pending(dev, vector);
        }
    }
}

static int virtio_pci_set_guest_notifier(DeviceState *d, int n, bool assign,
                                         bool with_irqfd)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_guest_notifier(vq);

    if (assign) {
        int r = event_notifier_init(notifier, 0);
        if (r < 0) {
            return r;
        }
        virtio_queue_set_guest_notifier_fd_handler(vq, true, with_irqfd);
    } else {
        virtio_queue_set_guest_notifier_fd_handler(vq, false, with_irqfd);
        event_notifier_cleanup(notifier);
    }

    if (!msix_enabled(&proxy->pci_dev) && vdc->guest_notifier_mask) {
        vdc->guest_notifier_mask(vdev, n, !assign);
    }

    return 0;
}

static bool virtio_pci_query_guest_notifiers(DeviceState *d)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    return msix_enabled(&proxy->pci_dev);
}

static int virtio_pci_set_guest_notifiers(DeviceState *d, int nvqs, bool assign)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    int r, n;
    bool with_irqfd = msix_enabled(&proxy->pci_dev) &&
        kvm_msi_via_irqfd_enabled();

    nvqs = MIN(nvqs, VIRTIO_PCI_QUEUE_MAX);

    /* When deassigning, pass a consistent nvqs value
     * to avoid leaking notifiers.
     */
    assert(assign || nvqs == proxy->nvqs_with_notifiers);

    proxy->nvqs_with_notifiers = nvqs;

    /* Must unset vector notifier while guest notifier is still assigned */
    if ((proxy->vector_irqfd || k->guest_notifier_mask) && !assign) {
        msix_unset_vector_notifiers(&proxy->pci_dev);
        if (proxy->vector_irqfd) {
            kvm_virtio_pci_vector_release(proxy, nvqs);
            g_free(proxy->vector_irqfd);
            proxy->vector_irqfd = NULL;
        }
    }

    for (n = 0; n < nvqs; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            break;
        }

        r = virtio_pci_set_guest_notifier(d, n, assign, with_irqfd);
        if (r < 0) {
            goto assign_error;
        }
    }

    /* Must set vector notifier after guest notifier has been assigned */
    if ((with_irqfd || k->guest_notifier_mask) && assign) {
        if (with_irqfd) {
            proxy->vector_irqfd =
                g_malloc0(sizeof(*proxy->vector_irqfd) *
                          msix_nr_vectors_allocated(&proxy->pci_dev));
            r = kvm_virtio_pci_vector_use(proxy, nvqs);
            if (r < 0) {
                goto assign_error;
            }
        }
        r = msix_set_vector_notifiers(&proxy->pci_dev,
                                      virtio_pci_vector_unmask,
                                      virtio_pci_vector_mask,
                                      virtio_pci_vector_poll);
        if (r < 0) {
            goto notifiers_error;
        }
    }

    return 0;

notifiers_error:
    if (with_irqfd) {
        assert(assign);
        kvm_virtio_pci_vector_release(proxy, nvqs);
    }

assign_error:
    /* We get here on assignment failure. Recover by undoing for VQs 0 .. n. */
    assert(assign);
    while (--n >= 0) {
        virtio_pci_set_guest_notifier(d, n, !assign, with_irqfd);
    }
    return r;
}

static int virtio_pci_set_host_notifier(DeviceState *d, int n, bool assign)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);

    /* Stop using ioeventfd for virtqueue kick if the device starts using host
     * notifiers.  This makes it easy to avoid stepping on each others' toes.
     */
    proxy->ioeventfd_disabled = assign;
    if (assign) {
        virtio_pci_stop_ioeventfd(proxy);
    }
    /* We don't need to start here: it's not needed because backend
     * currently only stops on status change away from ok,
     * reset, vmstop and such. If we do add code to start here,
     * need to check vmstate, device state etc. */
    return virtio_pci_set_host_notifier_internal(proxy, n, assign, false);
}

static void virtio_pci_vmstate_change(DeviceState *d, bool running)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (running) {
        /* Try to find out if the guest has bus master disabled, but is
           in ready state. Then we have a buggy guest OS. */
        if ((vdev->status & VIRTIO_CONFIG_S_DRIVER_OK) &&
            !(proxy->pci_dev.config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
            proxy->flags |= VIRTIO_PCI_FLAG_BUS_MASTER_BUG;
        }
        virtio_pci_start_ioeventfd(proxy);
    } else {
        virtio_pci_stop_ioeventfd(proxy);
    }
}

#ifdef CONFIG_VIRTFS
static int virtio_9p_init_pci(VirtIOPCIProxy *vpci_dev)
{
    V9fsPCIState *dev = VIRTIO_9P_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return 0;
}

static Property virtio_9p_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_VIRTIO_9P_PROPERTIES(V9fsPCIState, vdev.fsconf),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9p_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);

    k->init = virtio_9p_init_pci;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_9P;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = 0x2;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = virtio_9p_pci_properties;
}

static void virtio_9p_pci_instance_init(Object *obj)
{
    V9fsPCIState *dev = VIRTIO_9P_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_9P);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static const TypeInfo virtio_9p_pci_info = {
    .name          = TYPE_VIRTIO_9P_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(V9fsPCIState),
    .instance_init = virtio_9p_pci_instance_init,
    .class_init    = virtio_9p_pci_class_init,
};
#endif /* CONFIG_VIRTFS */

/*
 * virtio-pci: This is the PCIDevice which has a virtio-pci-bus.
 */

/* This is called by virtio-bus just after the device is plugged. */
static void virtio_pci_device_plugged(DeviceState *d)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    VirtioBusState *bus = &proxy->bus;
    uint8_t *config;
    uint32_t size;

    config = proxy->pci_dev.config;
    if (proxy->class_code) {
        pci_config_set_class(config, proxy->class_code);
    }
    pci_set_word(config + PCI_SUBSYSTEM_VENDOR_ID,
                 pci_get_word(config + PCI_VENDOR_ID));
    pci_set_word(config + PCI_SUBSYSTEM_ID, virtio_bus_get_vdev_id(bus));
    config[PCI_INTERRUPT_PIN] = 1;

    if (proxy->nvectors &&
        msix_init_exclusive_bar(&proxy->pci_dev, proxy->nvectors, 1)) {
        error_report("unable to init msix vectors to %" PRIu32,
                     proxy->nvectors);
        proxy->nvectors = 0;
    }

    proxy->pci_dev.config_write = virtio_write_config;

    size = VIRTIO_PCI_REGION_SIZE(&proxy->pci_dev)
         + virtio_bus_get_vdev_config_len(bus);
    if (size & (size - 1)) {
        size = 1 << qemu_fls(size);
    }

    memory_region_init_io(&proxy->bar, OBJECT(proxy), &virtio_pci_config_ops,
                          proxy, "virtio-pci", size);
    pci_register_bar(&proxy->pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO,
                     &proxy->bar);

    if (!kvm_has_many_ioeventfds()) {
        proxy->flags &= ~VIRTIO_PCI_FLAG_USE_IOEVENTFD;
    }

    proxy->host_features |= 0x1 << VIRTIO_F_NOTIFY_ON_EMPTY;
    proxy->host_features |= 0x1 << VIRTIO_F_BAD_FEATURE;
    proxy->host_features = virtio_bus_get_vdev_features(bus,
                                                      proxy->host_features);
}

static void virtio_pci_device_unplugged(DeviceState *d)
{
    PCIDevice *pci_dev = PCI_DEVICE(d);
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);

    virtio_pci_stop_ioeventfd(proxy);
    msix_uninit_exclusive_bar(pci_dev);
}

static int virtio_pci_init(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *dev = VIRTIO_PCI(pci_dev);
    VirtioPCIClass *k = VIRTIO_PCI_GET_CLASS(pci_dev);
    virtio_pci_bus_new(&dev->bus, sizeof(dev->bus), dev);
    if (k->init != NULL) {
        return k->init(dev);
    }
    return 0;
}

static void virtio_pci_exit(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(pci_dev);
    memory_region_destroy(&proxy->bar);
}

static void virtio_pci_reset(DeviceState *qdev)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(qdev);
    VirtioBusState *bus = VIRTIO_BUS(&proxy->bus);
    virtio_pci_stop_ioeventfd(proxy);
    virtio_bus_reset(bus);
    msix_unuse_all_vectors(&proxy->pci_dev);
    proxy->flags &= ~VIRTIO_PCI_FLAG_BUS_MASTER_BUG;
}

static void virtio_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_pci_init;
    k->exit = virtio_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_OTHERS;
    dc->reset = virtio_pci_reset;
}

static const TypeInfo virtio_pci_info = {
    .name          = TYPE_VIRTIO_PCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .class_init    = virtio_pci_class_init,
    .class_size    = sizeof(VirtioPCIClass),
    .abstract      = true,
};

/* virtio-blk-pci */

static Property virtio_blk_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_VIRTIO_BLK_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_PROP_END_OF_LIST(),
};

static int virtio_blk_pci_init(VirtIOPCIProxy *vpci_dev)
{
    VirtIOBlkPCI *dev = VIRTIO_BLK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return 0;
}

static void virtio_blk_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = virtio_blk_pci_properties;
    k->init = virtio_blk_pci_init;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void virtio_blk_pci_instance_init(Object *obj)
{
    VirtIOBlkPCI *dev = VIRTIO_BLK_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_BLK);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
    object_unref(OBJECT(&dev->vdev));
    qdev_alias_all_properties(DEVICE(&dev->vdev), obj);
}

static const TypeInfo virtio_blk_pci_info = {
    .name          = TYPE_VIRTIO_BLK_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOBlkPCI),
    .instance_init = virtio_blk_pci_instance_init,
    .class_init    = virtio_blk_pci_class_init,
};

/* virtio-scsi-pci */

static Property virtio_scsi_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_VIRTIO_SCSI_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_VIRTIO_SCSI_PROPERTIES(VirtIOSCSIPCI, vdev.parent_obj.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static int virtio_scsi_pci_init_pci(VirtIOPCIProxy *vpci_dev)
{
    VirtIOSCSIPCI *dev = VIRTIO_SCSI_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);
    DeviceState *proxy = DEVICE(vpci_dev);
    char *bus_name;

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = vs->conf.num_queues + 3;
    }

    /*
     * For command line compatibility, this sets the virtio-scsi-device bus
     * name as before.
     */
    if (proxy->id) {
        bus_name = g_strdup_printf("%s.0", proxy->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return 0;
}

static void virtio_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->init = virtio_scsi_pci_init_pci;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = virtio_scsi_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_SCSI;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void virtio_scsi_pci_instance_init(Object *obj)
{
    VirtIOSCSIPCI *dev = VIRTIO_SCSI_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_SCSI);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static const TypeInfo virtio_scsi_pci_info = {
    .name          = TYPE_VIRTIO_SCSI_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOSCSIPCI),
    .instance_init = virtio_scsi_pci_instance_init,
    .class_init    = virtio_scsi_pci_class_init,
};

/* vhost-scsi-pci */

#ifdef CONFIG_VHOST_SCSI
static Property vhost_scsi_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_VHOST_SCSI_PROPERTIES(VHostSCSIPCI, vdev.parent_obj.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static int vhost_scsi_pci_init_pci(VirtIOPCIProxy *vpci_dev)
{
    VHostSCSIPCI *dev = VHOST_SCSI_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = vs->conf.num_queues + 3;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return 0;
}

static void vhost_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->init = vhost_scsi_pci_init_pci;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = vhost_scsi_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_SCSI;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void vhost_scsi_pci_instance_init(Object *obj)
{
    VHostSCSIPCI *dev = VHOST_SCSI_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VHOST_SCSI);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static const TypeInfo vhost_scsi_pci_info = {
    .name          = TYPE_VHOST_SCSI_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VHostSCSIPCI),
    .instance_init = vhost_scsi_pci_instance_init,
    .class_init    = vhost_scsi_pci_class_init,
};
#endif

/* virtio-balloon-pci */

static void balloon_pci_stats_get_all(Object *obj, struct Visitor *v,
                                      void *opaque, const char *name,
                                      Error **errp)
{
    VirtIOBalloonPCI *dev = opaque;
    object_property_get(OBJECT(&dev->vdev), v, "guest-stats", errp);
}

static void balloon_pci_stats_get_poll_interval(Object *obj, struct Visitor *v,
                                                void *opaque, const char *name,
                                                Error **errp)
{
    VirtIOBalloonPCI *dev = opaque;
    object_property_get(OBJECT(&dev->vdev), v, "guest-stats-polling-interval",
                        errp);
}

static void balloon_pci_stats_set_poll_interval(Object *obj, struct Visitor *v,
                                                void *opaque, const char *name,
                                                Error **errp)
{
    VirtIOBalloonPCI *dev = opaque;
    object_property_set(OBJECT(&dev->vdev), v, "guest-stats-polling-interval",
                        errp);
}

static Property virtio_balloon_pci_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static int virtio_balloon_pci_init(VirtIOPCIProxy *vpci_dev)
{
    VirtIOBalloonPCI *dev = VIRTIO_BALLOON_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->class_code != PCI_CLASS_OTHERS &&
        vpci_dev->class_code != PCI_CLASS_MEMORY_RAM) { /* qemu < 1.1 */
        vpci_dev->class_code = PCI_CLASS_OTHERS;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return 0;
}

static void virtio_balloon_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->init = virtio_balloon_pci_init;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = virtio_balloon_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BALLOON;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_balloon_pci_instance_init(Object *obj)
{
    VirtIOBalloonPCI *dev = VIRTIO_BALLOON_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_BALLOON);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);

    object_property_add(obj, "guest-stats", "guest statistics",
                        balloon_pci_stats_get_all, NULL, NULL, dev,
                        NULL);

    object_property_add(obj, "guest-stats-polling-interval", "int",
                        balloon_pci_stats_get_poll_interval,
                        balloon_pci_stats_set_poll_interval,
                        NULL, dev, NULL);
}

static const TypeInfo virtio_balloon_pci_info = {
    .name          = TYPE_VIRTIO_BALLOON_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOBalloonPCI),
    .instance_init = virtio_balloon_pci_instance_init,
    .class_init    = virtio_balloon_pci_class_init,
};

/* virtio-serial-pci */

static int virtio_serial_pci_init(VirtIOPCIProxy *vpci_dev)
{
    VirtIOSerialPCI *dev = VIRTIO_SERIAL_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    DeviceState *proxy = DEVICE(vpci_dev);
    char *bus_name;

    if (vpci_dev->class_code != PCI_CLASS_COMMUNICATION_OTHER &&
        vpci_dev->class_code != PCI_CLASS_DISPLAY_OTHER && /* qemu 0.10 */
        vpci_dev->class_code != PCI_CLASS_OTHERS) {        /* qemu-kvm  */
            vpci_dev->class_code = PCI_CLASS_COMMUNICATION_OTHER;
    }

    /* backwards-compatibility with machines that were created with
       DEV_NVECTORS_UNSPECIFIED */
    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = dev->vdev.serial.max_virtserial_ports + 1;
    }

    /*
     * For command line compatibility, this sets the virtio-serial-device bus
     * name as before.
     */
    if (proxy->id) {
        bus_name = g_strdup_printf("%s.0", proxy->id);
        virtio_device_set_child_bus_name(VIRTIO_DEVICE(vdev), bus_name);
        g_free(bus_name);
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return 0;
}

static Property virtio_serial_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_VIRTIO_SERIAL_PROPERTIES(VirtIOSerialPCI, vdev.serial),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_serial_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->init = virtio_serial_pci_init;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->props = virtio_serial_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_CONSOLE;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void virtio_serial_pci_instance_init(Object *obj)
{
    VirtIOSerialPCI *dev = VIRTIO_SERIAL_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_SERIAL);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static const TypeInfo virtio_serial_pci_info = {
    .name          = TYPE_VIRTIO_SERIAL_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOSerialPCI),
    .instance_init = virtio_serial_pci_instance_init,
    .class_init    = virtio_serial_pci_class_init,
};

/* virtio-net-pci */

static Property virtio_net_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, false),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_VIRTIO_NET_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_NIC_PROPERTIES(VirtIONetPCI, vdev.nic_conf),
    DEFINE_VIRTIO_NET_PROPERTIES(VirtIONetPCI, vdev.net_conf),
    DEFINE_PROP_END_OF_LIST(),
};

static int virtio_net_pci_init(VirtIOPCIProxy *vpci_dev)
{
    DeviceState *qdev = DEVICE(vpci_dev);
    VirtIONetPCI *dev = VIRTIO_NET_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    virtio_net_set_config_size(&dev->vdev, vpci_dev->host_features);
    virtio_net_set_netclient_name(&dev->vdev, qdev->id,
                                  object_get_typename(OBJECT(qdev)));
    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }
    return 0;
}

static void virtio_net_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);

    k->romfile = "efi-virtio.rom";
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_NET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->props = virtio_net_properties;
    vpciklass->init = virtio_net_pci_init;
}

static void virtio_net_pci_instance_init(Object *obj)
{
    VirtIONetPCI *dev = VIRTIO_NET_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_NET);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
}

static const TypeInfo virtio_net_pci_info = {
    .name          = TYPE_VIRTIO_NET_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIONetPCI),
    .instance_init = virtio_net_pci_instance_init,
    .class_init    = virtio_net_pci_class_init,
};

/* virtio-rng-pci */

static Property virtio_rng_pci_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_VIRTIO_RNG_PROPERTIES(VirtIORngPCI, vdev.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static int virtio_rng_pci_init(VirtIOPCIProxy *vpci_dev)
{
    VirtIORngPCI *vrng = VIRTIO_RNG_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vrng->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    if (qdev_init(vdev) < 0) {
        return -1;
    }

    object_property_set_link(OBJECT(vrng),
                             OBJECT(vrng->vdev.conf.rng), "rng",
                             NULL);

    return 0;
}

static void virtio_rng_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_rng_pci_init;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = virtio_rng_pci_properties;

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_RNG;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_rng_initfn(Object *obj)
{
    VirtIORngPCI *dev = VIRTIO_RNG_PCI(obj);
    object_initialize(&dev->vdev, sizeof(dev->vdev), TYPE_VIRTIO_RNG);
    object_property_add_child(obj, "virtio-backend", OBJECT(&dev->vdev), NULL);
    object_property_add_link(obj, "rng", TYPE_RNG_BACKEND,
                             (Object **)&dev->vdev.conf.rng,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE, NULL);

}

static const TypeInfo virtio_rng_pci_info = {
    .name          = TYPE_VIRTIO_RNG_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIORngPCI),
    .instance_init = virtio_rng_initfn,
    .class_init    = virtio_rng_pci_class_init,
};

/* virtio-pci-bus */

static void virtio_pci_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtIOPCIProxy *dev)
{
    DeviceState *qdev = DEVICE(dev);
    BusState *qbus;
    char virtio_bus_name[] = "virtio-bus";

    qbus_create_inplace(bus, bus_size, TYPE_VIRTIO_PCI_BUS, qdev,
                        virtio_bus_name);
    qbus = BUS(bus);
    qbus->allow_hotplug = 1;
}

static void virtio_pci_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bus_class = BUS_CLASS(klass);
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    bus_class->max_dev = 1;
    k->notify = virtio_pci_notify;
    k->save_config = virtio_pci_save_config;
    k->load_config = virtio_pci_load_config;
    k->save_queue = virtio_pci_save_queue;
    k->load_queue = virtio_pci_load_queue;
    k->get_features = virtio_pci_get_features;
    k->query_guest_notifiers = virtio_pci_query_guest_notifiers;
    k->set_host_notifier = virtio_pci_set_host_notifier;
    k->set_guest_notifiers = virtio_pci_set_guest_notifiers;
    k->vmstate_change = virtio_pci_vmstate_change;
    k->device_plugged = virtio_pci_device_plugged;
    k->device_unplugged = virtio_pci_device_unplugged;
}

static const TypeInfo virtio_pci_bus_info = {
    .name          = TYPE_VIRTIO_PCI_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioPCIBusState),
    .class_init    = virtio_pci_bus_class_init,
};

static void virtio_pci_register_types(void)
{
    type_register_static(&virtio_rng_pci_info);
    type_register_static(&virtio_pci_bus_info);
    type_register_static(&virtio_pci_info);
#ifdef CONFIG_VIRTFS
    type_register_static(&virtio_9p_pci_info);
#endif
    type_register_static(&virtio_blk_pci_info);
    type_register_static(&virtio_scsi_pci_info);
    type_register_static(&virtio_balloon_pci_info);
    type_register_static(&virtio_serial_pci_info);
    type_register_static(&virtio_net_pci_info);
#ifdef CONFIG_VHOST_SCSI
    type_register_static(&vhost_scsi_pci_info);
#endif
}

type_init(virtio_pci_register_types)
