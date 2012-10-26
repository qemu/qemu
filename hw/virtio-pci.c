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

#include "virtio.h"
#include "virtio-blk.h"
#include "virtio-net.h"
#include "virtio-serial.h"
#include "virtio-scsi.h"
#include "pci.h"
#include "qemu-error.h"
#include "msi.h"
#include "msix.h"
#include "net.h"
#include "loader.h"
#include "kvm.h"
#include "blockdev.h"
#include "virtio-pci.h"
#include "range.h"

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

/* QEMU doesn't strictly need write barriers since everything runs in
 * lock-step.  We'll leave the calls to wmb() in though to make it obvious for
 * KVM or if kqemu gets SMP support.
 */
#define wmb() do { } while (0)

/* HACK for virtio to determine if it's running a big endian guest */
bool virtio_is_big_endian(void);

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
    msix_unuse_all_vectors(&proxy->pci_dev);
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

static int virtio_pci_set_host_notifier_internal(VirtIOPCIProxy *proxy,
                                                 int n, bool assign, bool set_handler)
{
    VirtQueue *vq = virtio_get_queue(proxy->vdev, n);
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
    int n, r;

    if (!(proxy->flags & VIRTIO_PCI_FLAG_USE_IOEVENTFD) ||
        proxy->ioeventfd_disabled ||
        proxy->ioeventfd_started) {
        return;
    }

    for (n = 0; n < VIRTIO_PCI_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(proxy->vdev, n)) {
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
        if (!virtio_queue_get_num(proxy->vdev, n)) {
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
    int r;
    int n;

    if (!proxy->ioeventfd_started) {
        return;
    }

    for (n = 0; n < VIRTIO_PCI_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(proxy->vdev, n)) {
            continue;
        }

        r = virtio_pci_set_host_notifier_internal(proxy, n, false, false);
        assert(r >= 0);
    }
    proxy->ioeventfd_started = false;
}

void virtio_pci_reset(DeviceState *d)
{
    VirtIOPCIProxy *proxy = container_of(d, VirtIOPCIProxy, pci_dev.qdev);
    virtio_pci_stop_ioeventfd(proxy);
    virtio_reset(proxy->vdev);
    msix_unuse_all_vectors(&proxy->pci_dev);
    proxy->flags &= ~VIRTIO_PCI_FLAG_BUS_MASTER_BUG;
}

static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = proxy->vdev;
    hwaddr pa;

    switch (addr) {
    case VIRTIO_PCI_GUEST_FEATURES:
	/* Guest does not negotiate properly?  We have to assume nothing. */
	if (val & (1 << VIRTIO_F_BAD_FEATURE)) {
            val = vdev->bad_features ? vdev->bad_features(vdev) : 0;
	}
        virtio_set_features(vdev, val);
        break;
    case VIRTIO_PCI_QUEUE_PFN:
        pa = (hwaddr)val << VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        if (pa == 0) {
            virtio_pci_stop_ioeventfd(proxy);
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
            virtio_reset(proxy->vdev);
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

static uint64_t virtio_pci_config_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    uint64_t val = 0;
    if (addr < config) {
        return virtio_ioport_read(proxy, addr);
    }
    addr -= config;

    switch (size) {
    case 1:
        val = virtio_config_readb(proxy->vdev, addr);
        break;
    case 2:
        val = virtio_config_readw(proxy->vdev, addr);
        if (virtio_is_big_endian()) {
            val = bswap16(val);
        }
        break;
    case 4:
        val = virtio_config_readl(proxy->vdev, addr);
        if (virtio_is_big_endian()) {
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
        virtio_config_writeb(proxy->vdev, addr, val);
        break;
    case 2:
        if (virtio_is_big_endian()) {
            val = bswap16(val);
        }
        virtio_config_writew(proxy->vdev, addr, val);
        break;
    case 4:
        if (virtio_is_big_endian()) {
            val = bswap32(val);
        }
        virtio_config_writel(proxy->vdev, addr, val);
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

    pci_default_write_config(pci_dev, address, val, len);

    if (range_covers_byte(address, len, PCI_COMMAND) &&
        !(pci_dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER) &&
        !(proxy->flags & VIRTIO_PCI_FLAG_BUS_MASTER_BUG)) {
        virtio_pci_stop_ioeventfd(proxy);
        virtio_set_status(proxy->vdev,
                          proxy->vdev->status & ~VIRTIO_CONFIG_S_DRIVER_OK);
    }
}

static unsigned virtio_pci_get_features(void *opaque)
{
    VirtIOPCIProxy *proxy = opaque;
    return proxy->host_features;
}

static int kvm_virtio_pci_vq_vector_use(VirtIOPCIProxy *proxy,
                                        unsigned int queue_no,
                                        unsigned int vector,
                                        MSIMessage msg)
{
    VirtQueue *vq = virtio_get_queue(proxy->vdev, queue_no);
    EventNotifier *n = virtio_queue_get_guest_notifier(vq);
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

    ret = kvm_irqchip_add_irqfd_notifier(kvm_state, n, irqfd->virq);
    if (ret < 0) {
        if (--irqfd->users == 0) {
            kvm_irqchip_release_virq(kvm_state, irqfd->virq);
        }
        return ret;
    }

    virtio_queue_set_guest_notifier_fd_handler(vq, true, true);
    return 0;
}

static void kvm_virtio_pci_vq_vector_release(VirtIOPCIProxy *proxy,
                                             unsigned int queue_no,
                                             unsigned int vector)
{
    VirtQueue *vq = virtio_get_queue(proxy->vdev, queue_no);
    EventNotifier *n = virtio_queue_get_guest_notifier(vq);
    VirtIOIRQFD *irqfd = &proxy->vector_irqfd[vector];
    int ret;

    ret = kvm_irqchip_remove_irqfd_notifier(kvm_state, n, irqfd->virq);
    assert(ret == 0);

    if (--irqfd->users == 0) {
        kvm_irqchip_release_virq(kvm_state, irqfd->virq);
    }

    virtio_queue_set_guest_notifier_fd_handler(vq, true, false);
}

static int kvm_virtio_pci_vector_use(PCIDevice *dev, unsigned vector,
                                     MSIMessage msg)
{
    VirtIOPCIProxy *proxy = container_of(dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = proxy->vdev;
    int ret, queue_no;

    for (queue_no = 0; queue_no < VIRTIO_PCI_QUEUE_MAX; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        if (virtio_queue_vector(vdev, queue_no) != vector) {
            continue;
        }
        ret = kvm_virtio_pci_vq_vector_use(proxy, queue_no, vector, msg);
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
        kvm_virtio_pci_vq_vector_release(proxy, queue_no, vector);
    }
    return ret;
}

static void kvm_virtio_pci_vector_release(PCIDevice *dev, unsigned vector)
{
    VirtIOPCIProxy *proxy = container_of(dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = proxy->vdev;
    int queue_no;

    for (queue_no = 0; queue_no < VIRTIO_PCI_QUEUE_MAX; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        if (virtio_queue_vector(vdev, queue_no) != vector) {
            continue;
        }
        kvm_virtio_pci_vq_vector_release(proxy, queue_no, vector);
    }
}

static int virtio_pci_set_guest_notifier(void *opaque, int n, bool assign)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtQueue *vq = virtio_get_queue(proxy->vdev, n);
    EventNotifier *notifier = virtio_queue_get_guest_notifier(vq);

    if (assign) {
        int r = event_notifier_init(notifier, 0);
        if (r < 0) {
            return r;
        }
        virtio_queue_set_guest_notifier_fd_handler(vq, true, false);
    } else {
        virtio_queue_set_guest_notifier_fd_handler(vq, false, false);
        event_notifier_cleanup(notifier);
    }

    return 0;
}

static bool virtio_pci_query_guest_notifiers(void *opaque)
{
    VirtIOPCIProxy *proxy = opaque;
    return msix_enabled(&proxy->pci_dev);
}

static int virtio_pci_set_guest_notifiers(void *opaque, bool assign)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = proxy->vdev;
    int r, n;

    /* Must unset vector notifier while guest notifier is still assigned */
    if (kvm_msi_via_irqfd_enabled() && !assign) {
        msix_unset_vector_notifiers(&proxy->pci_dev);
        g_free(proxy->vector_irqfd);
        proxy->vector_irqfd = NULL;
    }

    for (n = 0; n < VIRTIO_PCI_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            break;
        }

        r = virtio_pci_set_guest_notifier(opaque, n, assign);
        if (r < 0) {
            goto assign_error;
        }
    }

    /* Must set vector notifier after guest notifier has been assigned */
    if (kvm_msi_via_irqfd_enabled() && assign) {
        proxy->vector_irqfd =
            g_malloc0(sizeof(*proxy->vector_irqfd) *
                      msix_nr_vectors_allocated(&proxy->pci_dev));
        r = msix_set_vector_notifiers(&proxy->pci_dev,
                                      kvm_virtio_pci_vector_use,
                                      kvm_virtio_pci_vector_release);
        if (r < 0) {
            goto assign_error;
        }
    }

    return 0;

assign_error:
    /* We get here on assignment failure. Recover by undoing for VQs 0 .. n. */
    assert(assign);
    while (--n >= 0) {
        virtio_pci_set_guest_notifier(opaque, n, !assign);
    }
    return r;
}

static int virtio_pci_set_host_notifier(void *opaque, int n, bool assign)
{
    VirtIOPCIProxy *proxy = opaque;

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

static void virtio_pci_vmstate_change(void *opaque, bool running)
{
    VirtIOPCIProxy *proxy = opaque;

    if (running) {
        /* Try to find out if the guest has bus master disabled, but is
           in ready state. Then we have a buggy guest OS. */
        if ((proxy->vdev->status & VIRTIO_CONFIG_S_DRIVER_OK) &&
            !(proxy->pci_dev.config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
            proxy->flags |= VIRTIO_PCI_FLAG_BUS_MASTER_BUG;
        }
        virtio_pci_start_ioeventfd(proxy);
    } else {
        virtio_pci_stop_ioeventfd(proxy);
    }
}

static const VirtIOBindings virtio_pci_bindings = {
    .notify = virtio_pci_notify,
    .save_config = virtio_pci_save_config,
    .load_config = virtio_pci_load_config,
    .save_queue = virtio_pci_save_queue,
    .load_queue = virtio_pci_load_queue,
    .get_features = virtio_pci_get_features,
    .query_guest_notifiers = virtio_pci_query_guest_notifiers,
    .set_host_notifier = virtio_pci_set_host_notifier,
    .set_guest_notifiers = virtio_pci_set_guest_notifiers,
    .vmstate_change = virtio_pci_vmstate_change,
};

void virtio_init_pci(VirtIOPCIProxy *proxy, VirtIODevice *vdev)
{
    uint8_t *config;
    uint32_t size;

    proxy->vdev = vdev;

    config = proxy->pci_dev.config;

    if (proxy->class_code) {
        pci_config_set_class(config, proxy->class_code);
    }
    pci_set_word(config + PCI_SUBSYSTEM_VENDOR_ID,
                 pci_get_word(config + PCI_VENDOR_ID));
    pci_set_word(config + PCI_SUBSYSTEM_ID, vdev->device_id);
    config[PCI_INTERRUPT_PIN] = 1;

    if (vdev->nvectors &&
        msix_init_exclusive_bar(&proxy->pci_dev, vdev->nvectors, 1)) {
        vdev->nvectors = 0;
    }

    proxy->pci_dev.config_write = virtio_write_config;

    size = VIRTIO_PCI_REGION_SIZE(&proxy->pci_dev) + vdev->config_len;
    if (size & (size-1))
        size = 1 << qemu_fls(size);

    memory_region_init_io(&proxy->bar, &virtio_pci_config_ops, proxy,
                          "virtio-pci", size);
    pci_register_bar(&proxy->pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO,
                     &proxy->bar);

    if (!kvm_has_many_ioeventfds()) {
        proxy->flags &= ~VIRTIO_PCI_FLAG_USE_IOEVENTFD;
    }

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

    vdev = virtio_blk_init(&pci_dev->qdev, &proxy->blk);
    if (!vdev) {
        return -1;
    }
    vdev->nvectors = proxy->nvectors;
    virtio_init_pci(proxy, vdev);
    /* make the actual value visible */
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static void virtio_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    memory_region_destroy(&proxy->bar);
    msix_uninit_exclusive_bar(pci_dev);
}

static void virtio_blk_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_blk_exit(proxy->vdev);
    virtio_exit_pci(pci_dev);
}

static int virtio_serial_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    if (proxy->class_code != PCI_CLASS_COMMUNICATION_OTHER &&
        proxy->class_code != PCI_CLASS_DISPLAY_OTHER && /* qemu 0.10 */
        proxy->class_code != PCI_CLASS_OTHERS)          /* qemu-kvm  */
        proxy->class_code = PCI_CLASS_COMMUNICATION_OTHER;

    vdev = virtio_serial_init(&pci_dev->qdev, &proxy->serial);
    if (!vdev) {
        return -1;
    }
    vdev->nvectors = proxy->nvectors == DEV_NVECTORS_UNSPECIFIED
                                        ? proxy->serial.max_virtserial_ports + 1
                                        : proxy->nvectors;
    virtio_init_pci(proxy, vdev);
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static void virtio_serial_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_serial_exit(proxy->vdev);
    virtio_exit_pci(pci_dev);
}

static int virtio_net_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_net_init(&pci_dev->qdev, &proxy->nic, &proxy->net);

    vdev->nvectors = proxy->nvectors;
    virtio_init_pci(proxy, vdev);

    /* make the actual value visible */
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static void virtio_net_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_net_exit(proxy->vdev);
    virtio_exit_pci(pci_dev);
}

static int virtio_balloon_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    if (proxy->class_code != PCI_CLASS_OTHERS &&
        proxy->class_code != PCI_CLASS_MEMORY_RAM) { /* qemu < 1.1 */
        proxy->class_code = PCI_CLASS_OTHERS;
    }

    vdev = virtio_balloon_init(&pci_dev->qdev);
    if (!vdev) {
        return -1;
    }
    virtio_init_pci(proxy, vdev);
    return 0;
}

static void virtio_balloon_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_balloon_exit(proxy->vdev);
    virtio_exit_pci(pci_dev);
}

static int virtio_rng_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    if (proxy->rng.rng == NULL) {
        proxy->rng.default_backend = RNG_RANDOM(object_new(TYPE_RNG_RANDOM));

        object_property_add_child(OBJECT(pci_dev),
                                  "default-backend",
                                  OBJECT(proxy->rng.default_backend),
                                  NULL);

        object_property_set_link(OBJECT(pci_dev),
                                 OBJECT(proxy->rng.default_backend),
                                 "rng", NULL);
    }

    vdev = virtio_rng_init(&pci_dev->qdev, &proxy->rng);
    if (!vdev) {
        return -1;
    }
    virtio_init_pci(proxy, vdev);
    return 0;
}

static void virtio_rng_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_rng_exit(proxy->vdev);
    virtio_exit_pci(pci_dev);
}

static Property virtio_blk_properties[] = {
    DEFINE_PROP_HEX32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_BLOCK_PROPERTIES(VirtIOPCIProxy, blk.conf),
    DEFINE_BLOCK_CHS_PROPERTIES(VirtIOPCIProxy, blk.conf),
    DEFINE_PROP_STRING("serial", VirtIOPCIProxy, blk.serial),
#ifdef __linux__
    DEFINE_PROP_BIT("scsi", VirtIOPCIProxy, blk.scsi, 0, true),
#endif
    DEFINE_PROP_BIT("config-wce", VirtIOPCIProxy, blk.config_wce, 0, true),
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags, VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_VIRTIO_BLK_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_blk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_blk_init_pci;
    k->exit = virtio_blk_exit_pci;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    dc->reset = virtio_pci_reset;
    dc->props = virtio_blk_properties;
}

static TypeInfo virtio_blk_info = {
    .name          = "virtio-blk-pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .class_init    = virtio_blk_class_init,
};

static Property virtio_net_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags, VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, false),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_VIRTIO_NET_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_NIC_PROPERTIES(VirtIOPCIProxy, nic),
    DEFINE_PROP_UINT32("x-txtimer", VirtIOPCIProxy, net.txtimer, TX_TIMER_INTERVAL),
    DEFINE_PROP_INT32("x-txburst", VirtIOPCIProxy, net.txburst, TX_BURST),
    DEFINE_PROP_STRING("tx", VirtIOPCIProxy, net.tx),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_net_init_pci;
    k->exit = virtio_net_exit_pci;
    k->romfile = "pxe-virtio.rom";
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_NET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->reset = virtio_pci_reset;
    dc->props = virtio_net_properties;
}

static TypeInfo virtio_net_info = {
    .name          = "virtio-net-pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .class_init    = virtio_net_class_init,
};

static Property virtio_serial_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags, VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_HEX32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_PROP_UINT32("max_ports", VirtIOPCIProxy, serial.max_virtserial_ports, 31),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_serial_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_serial_init_pci;
    k->exit = virtio_serial_exit_pci;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_CONSOLE;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
    dc->reset = virtio_pci_reset;
    dc->props = virtio_serial_properties;
}

static TypeInfo virtio_serial_info = {
    .name          = "virtio-serial-pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .class_init    = virtio_serial_class_init,
};

static Property virtio_balloon_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    DEFINE_PROP_HEX32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_balloon_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_balloon_init_pci;
    k->exit = virtio_balloon_exit_pci;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_BALLOON;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_OTHERS;
    dc->reset = virtio_pci_reset;
    dc->props = virtio_balloon_properties;
}

static TypeInfo virtio_balloon_info = {
    .name          = "virtio-balloon-pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .class_init    = virtio_balloon_class_init,
};

static void virtio_rng_initfn(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    object_property_add_link(obj, "rng", TYPE_RNG_BACKEND,
                             (Object **)&proxy->rng.rng, NULL);
}

static Property virtio_rng_properties[] = {
    DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
    /* Set a default rate limit of 2^47 bytes per minute or roughly 2TB/s.  If
       you have an entropy source capable of generating more entropy than this
       and you can pass it through via virtio-rng, then hats off to you.  Until
       then, this is unlimited for all practical purposes.
    */
    DEFINE_PROP_UINT64("max-bytes", VirtIOPCIProxy, rng.max_bytes, INT64_MAX),
    DEFINE_PROP_UINT32("period", VirtIOPCIProxy, rng.period_ms, 1 << 16),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_rng_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_rng_init_pci;
    k->exit = virtio_rng_exit_pci;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_RNG;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_OTHERS;
    dc->reset = virtio_pci_reset;
    dc->props = virtio_rng_properties;
}

static TypeInfo virtio_rng_info = {
    .name          = "virtio-rng-pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .instance_init = virtio_rng_initfn,
    .class_init    = virtio_rng_class_init,
};

static int virtio_scsi_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_scsi_init(&pci_dev->qdev, &proxy->scsi);
    if (!vdev) {
        return -EINVAL;
    }

    vdev->nvectors = proxy->nvectors == DEV_NVECTORS_UNSPECIFIED
                                        ? proxy->scsi.num_queues + 3
                                        : proxy->nvectors;
    virtio_init_pci(proxy, vdev);

    /* make the actual value visible */
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static void virtio_scsi_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_scsi_exit(proxy->vdev);
    virtio_exit_pci(pci_dev);
}

static Property virtio_scsi_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags, VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, DEV_NVECTORS_UNSPECIFIED),
    DEFINE_VIRTIO_SCSI_PROPERTIES(VirtIOPCIProxy, host_features, scsi),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_scsi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = virtio_scsi_init_pci;
    k->exit = virtio_scsi_exit_pci;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_SCSI;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_STORAGE_SCSI;
    dc->reset = virtio_pci_reset;
    dc->props = virtio_scsi_properties;
}

static TypeInfo virtio_scsi_info = {
    .name          = "virtio-scsi-pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VirtIOPCIProxy),
    .class_init    = virtio_scsi_class_init,
};

static void virtio_pci_register_types(void)
{
    type_register_static(&virtio_blk_info);
    type_register_static(&virtio_net_info);
    type_register_static(&virtio_serial_info);
    type_register_static(&virtio_balloon_info);
    type_register_static(&virtio_scsi_info);
    type_register_static(&virtio_rng_info);
}

type_init(virtio_pci_register_types)
