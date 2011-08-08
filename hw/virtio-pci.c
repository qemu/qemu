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
#include "virtio-serial.h"
#include "pci.h"
#include "qemu-error.h"
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

/* Performance improves when virtqueue kick processing is decoupled from the
 * vcpu thread using ioeventfd for some devices. */
#define VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT 1
#define VIRTIO_PCI_FLAG_USE_IOEVENTFD   (1 << VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT)

/* QEMU doesn't strictly need write barriers since everything runs in
 * lock-step.  We'll leave the calls to wmb() in though to make it obvious for
 * KVM or if kqemu gets SMP support.
 */
#define wmb() do { } while (0)

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

static int virtio_pci_set_host_notifier_internal(VirtIOPCIProxy *proxy,
                                                 int n, bool assign)
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
        memory_region_add_eventfd(&proxy->bar, VIRTIO_PCI_QUEUE_NOTIFY, 2,
                                  true, n, event_notifier_get_fd(notifier));
    } else {
        memory_region_del_eventfd(&proxy->bar, VIRTIO_PCI_QUEUE_NOTIFY, 2,
                                  true, n, event_notifier_get_fd(notifier));
        /* Handle the race condition where the guest kicked and we deassigned
         * before we got around to handling the kick.
         */
        if (event_notifier_test_and_clear(notifier)) {
            virtio_queue_notify_vq(vq);
        }

        event_notifier_cleanup(notifier);
    }
    return r;
}

static void virtio_pci_host_notifier_read(void *opaque)
{
    VirtQueue *vq = opaque;
    EventNotifier *n = virtio_queue_get_host_notifier(vq);
    if (event_notifier_test_and_clear(n)) {
        virtio_queue_notify_vq(vq);
    }
}

static void virtio_pci_set_host_notifier_fd_handler(VirtIOPCIProxy *proxy,
                                                    int n, bool assign)
{
    VirtQueue *vq = virtio_get_queue(proxy->vdev, n);
    EventNotifier *notifier = virtio_queue_get_host_notifier(vq);
    if (assign) {
        qemu_set_fd_handler(event_notifier_get_fd(notifier),
                            virtio_pci_host_notifier_read, NULL, vq);
    } else {
        qemu_set_fd_handler(event_notifier_get_fd(notifier),
                            NULL, NULL, NULL);
    }
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

        r = virtio_pci_set_host_notifier_internal(proxy, n, true);
        if (r < 0) {
            goto assign_error;
        }

        virtio_pci_set_host_notifier_fd_handler(proxy, n, true);
    }
    proxy->ioeventfd_started = true;
    return;

assign_error:
    while (--n >= 0) {
        if (!virtio_queue_get_num(proxy->vdev, n)) {
            continue;
        }

        virtio_pci_set_host_notifier_fd_handler(proxy, n, false);
        r = virtio_pci_set_host_notifier_internal(proxy, n, false);
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

        virtio_pci_set_host_notifier_fd_handler(proxy, n, false);
        r = virtio_pci_set_host_notifier_internal(proxy, n, false);
        assert(r >= 0);
    }
    proxy->ioeventfd_started = false;
}

static void virtio_pci_reset(DeviceState *d)
{
    VirtIOPCIProxy *proxy = container_of(d, VirtIOPCIProxy, pci_dev.qdev);
    virtio_pci_stop_ioeventfd(proxy);
    virtio_reset(proxy->vdev);
    msix_reset(&proxy->pci_dev);
    proxy->flags &= ~VIRTIO_PCI_FLAG_BUS_MASTER_BUG;
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

static uint32_t virtio_pci_config_readb(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    if (addr < config)
        return virtio_ioport_read(proxy, addr);
    addr -= config;
    return virtio_config_readb(proxy->vdev, addr);
}

static uint32_t virtio_pci_config_readw(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    if (addr < config)
        return virtio_ioport_read(proxy, addr);
    addr -= config;
    return virtio_config_readw(proxy->vdev, addr);
}

static uint32_t virtio_pci_config_readl(void *opaque, uint32_t addr)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
    if (addr < config)
        return virtio_ioport_read(proxy, addr);
    addr -= config;
    return virtio_config_readl(proxy->vdev, addr);
}

static void virtio_pci_config_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    uint32_t config = VIRTIO_PCI_CONFIG(&proxy->pci_dev);
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
    if (addr < config) {
        virtio_ioport_write(proxy, addr, val);
        return;
    }
    addr -= config;
    virtio_config_writel(proxy->vdev, addr, val);
}

const MemoryRegionPortio virtio_portio[] = {
    { 0, 0x10000, 1, .write = virtio_pci_config_writeb, },
    { 0, 0x10000, 2, .write = virtio_pci_config_writew, },
    { 0, 0x10000, 4, .write = virtio_pci_config_writel, },
    { 0, 0x10000, 1, .read = virtio_pci_config_readb, },
    { 0, 0x10000, 2, .read = virtio_pci_config_readw, },
    { 0, 0x10000, 4, .read = virtio_pci_config_readl, },
    PORTIO_END_OF_LIST()
};

static const MemoryRegionOps virtio_pci_config_ops = {
    .old_portio = virtio_portio,
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

    msix_write_config(pci_dev, address, val, len);
}

static unsigned virtio_pci_get_features(void *opaque)
{
    VirtIOPCIProxy *proxy = opaque;
    return proxy->host_features;
}

static void virtio_pci_guest_notifier_read(void *opaque)
{
    VirtQueue *vq = opaque;
    EventNotifier *n = virtio_queue_get_guest_notifier(vq);
    if (event_notifier_test_and_clear(n)) {
        virtio_irq(vq);
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
        qemu_set_fd_handler(event_notifier_get_fd(notifier),
                            virtio_pci_guest_notifier_read, NULL, vq);
    } else {
        qemu_set_fd_handler(event_notifier_get_fd(notifier),
                            NULL, NULL, NULL);
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

    for (n = 0; n < VIRTIO_PCI_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            break;
        }

        r = virtio_pci_set_guest_notifier(opaque, n, assign);
        if (r < 0) {
            goto assign_error;
        }
    }

    return 0;

assign_error:
    /* We get here on assignment failure. Recover by undoing for VQs 0 .. n. */
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
    return virtio_pci_set_host_notifier_internal(proxy, n, assign);
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
    pci_set_word(config + 0x2c, pci_get_word(config + PCI_VENDOR_ID));
    pci_set_word(config + 0x2e, vdev->device_id);
    config[0x3d] = 1;

    memory_region_init(&proxy->msix_bar, "virtio-msix", 4096);
    if (vdev->nvectors && !msix_init(&proxy->pci_dev, vdev->nvectors,
                                     &proxy->msix_bar, 1, 0)) {
        pci_register_bar_region(&proxy->pci_dev, 1,
                                PCI_BASE_ADDRESS_SPACE_MEMORY,
                                &proxy->msix_bar);
    } else
        vdev->nvectors = 0;

    proxy->pci_dev.config_write = virtio_write_config;

    size = VIRTIO_PCI_REGION_SIZE(&proxy->pci_dev) + vdev->config_len;
    if (size & (size-1))
        size = 1 << qemu_fls(size);

    memory_region_init_io(&proxy->bar, &virtio_pci_config_ops, proxy,
                          "virtio-pci", size);
    pci_register_bar_region(&proxy->pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO,
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

    vdev = virtio_blk_init(&pci_dev->qdev, &proxy->block,
                           &proxy->block_serial);
    if (!vdev) {
        return -1;
    }
    vdev->nvectors = proxy->nvectors;
    virtio_init_pci(proxy, vdev);
    /* make the actual value visible */
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static int virtio_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    int r;

    memory_region_destroy(&proxy->bar);
    r = msix_uninit(pci_dev, &proxy->msix_bar);
    memory_region_destroy(&proxy->msix_bar);
    return r;
}

static int virtio_blk_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_blk_exit(proxy->vdev);
    blockdev_mark_auto_del(proxy->block.bs);
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

static int virtio_serial_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_serial_exit(proxy->vdev);
    return virtio_exit_pci(pci_dev);
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

static int virtio_net_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_net_exit(proxy->vdev);
    return virtio_exit_pci(pci_dev);
}

static int virtio_balloon_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_balloon_init(&pci_dev->qdev);
    if (!vdev) {
        return -1;
    }
    virtio_init_pci(proxy, vdev);
    return 0;
}

static int virtio_balloon_exit_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);

    virtio_pci_stop_ioeventfd(proxy);
    virtio_balloon_exit(proxy->vdev);
    return virtio_exit_pci(pci_dev);
}

static PCIDeviceInfo virtio_info[] = {
    {
        .qdev.name = "virtio-blk-pci",
        .qdev.alias = "virtio-blk",
        .qdev.size = sizeof(VirtIOPCIProxy),
        .init      = virtio_blk_init_pci,
        .exit      = virtio_blk_exit_pci,
        .vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET,
        .device_id = PCI_DEVICE_ID_VIRTIO_BLOCK,
        .revision  = VIRTIO_PCI_ABI_VERSION,
        .class_id  = PCI_CLASS_STORAGE_SCSI,
        .qdev.props = (Property[]) {
            DEFINE_PROP_HEX32("class", VirtIOPCIProxy, class_code, 0),
            DEFINE_BLOCK_PROPERTIES(VirtIOPCIProxy, block),
            DEFINE_PROP_STRING("serial", VirtIOPCIProxy, block_serial),
            DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                            VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
            DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
            DEFINE_VIRTIO_BLK_FEATURES(VirtIOPCIProxy, host_features),
            DEFINE_PROP_END_OF_LIST(),
        },
        .qdev.reset = virtio_pci_reset,
    },{
        .qdev.name  = "virtio-net-pci",
        .qdev.alias = "virtio-net",
        .qdev.size  = sizeof(VirtIOPCIProxy),
        .init       = virtio_net_init_pci,
        .exit       = virtio_net_exit_pci,
        .romfile    = "pxe-virtio.rom",
        .vendor_id  = PCI_VENDOR_ID_REDHAT_QUMRANET,
        .device_id  = PCI_DEVICE_ID_VIRTIO_NET,
        .revision   = VIRTIO_PCI_ABI_VERSION,
        .class_id   = PCI_CLASS_NETWORK_ETHERNET,
        .qdev.props = (Property[]) {
            DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                            VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, false),
            DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
            DEFINE_VIRTIO_NET_FEATURES(VirtIOPCIProxy, host_features),
            DEFINE_NIC_PROPERTIES(VirtIOPCIProxy, nic),
            DEFINE_PROP_UINT32("x-txtimer", VirtIOPCIProxy,
                               net.txtimer, TX_TIMER_INTERVAL),
            DEFINE_PROP_INT32("x-txburst", VirtIOPCIProxy,
                              net.txburst, TX_BURST),
            DEFINE_PROP_STRING("tx", VirtIOPCIProxy, net.tx),
            DEFINE_PROP_END_OF_LIST(),
        },
        .qdev.reset = virtio_pci_reset,
    },{
        .qdev.name = "virtio-serial-pci",
        .qdev.alias = "virtio-serial",
        .qdev.size = sizeof(VirtIOPCIProxy),
        .init      = virtio_serial_init_pci,
        .exit      = virtio_serial_exit_pci,
        .vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET,
        .device_id = PCI_DEVICE_ID_VIRTIO_CONSOLE,
        .revision  = VIRTIO_PCI_ABI_VERSION,
        .class_id  = PCI_CLASS_COMMUNICATION_OTHER,
        .qdev.props = (Property[]) {
            DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                            VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
            DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                               DEV_NVECTORS_UNSPECIFIED),
            DEFINE_PROP_HEX32("class", VirtIOPCIProxy, class_code, 0),
            DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
            DEFINE_PROP_UINT32("max_ports", VirtIOPCIProxy,
                               serial.max_virtserial_ports, 31),
            DEFINE_PROP_END_OF_LIST(),
        },
        .qdev.reset = virtio_pci_reset,
    },{
        .qdev.name = "virtio-balloon-pci",
        .qdev.alias = "virtio-balloon",
        .qdev.size = sizeof(VirtIOPCIProxy),
        .init      = virtio_balloon_init_pci,
        .exit      = virtio_balloon_exit_pci,
        .vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET,
        .device_id = PCI_DEVICE_ID_VIRTIO_BALLOON,
        .revision  = VIRTIO_PCI_ABI_VERSION,
        .class_id  = PCI_CLASS_MEMORY_RAM,
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
