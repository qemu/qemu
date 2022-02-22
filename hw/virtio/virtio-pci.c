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

#include "qemu/osdep.h"

#include "exec/memop.h"
#include "standard-headers/linux/virtio_pci.h"
#include "hw/boards.h"
#include "hw/virtio/virtio.h"
#include "migration/qemu-file-types.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/loader.h"
#include "sysemu/kvm.h"
#include "virtio-pci.h"
#include "qemu/range.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/visitor.h"
#include "sysemu/replay.h"

#define VIRTIO_PCI_REGION_SIZE(dev)     VIRTIO_PCI_CONFIG_OFF(msix_present(dev))

#undef VIRTIO_PCI_CONFIG

/* The remaining space is defined by each driver as the per-driver
 * configuration space */
#define VIRTIO_PCI_CONFIG_SIZE(dev)     VIRTIO_PCI_CONFIG_OFF(msix_enabled(dev))

static void virtio_pci_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtIOPCIProxy *dev);
static void virtio_pci_reset(DeviceState *qdev);

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
        pci_set_irq(&proxy->pci_dev, qatomic_read(&vdev->isr) & 1);
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

static const VMStateDescription vmstate_virtio_pci_modern_queue_state = {
    .name = "virtio_pci/modern_queue_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(num, VirtIOPCIQueue),
        VMSTATE_UNUSED(1), /* enabled was stored as be16 */
        VMSTATE_BOOL(enabled, VirtIOPCIQueue),
        VMSTATE_UINT32_ARRAY(desc, VirtIOPCIQueue, 2),
        VMSTATE_UINT32_ARRAY(avail, VirtIOPCIQueue, 2),
        VMSTATE_UINT32_ARRAY(used, VirtIOPCIQueue, 2),
        VMSTATE_END_OF_LIST()
    }
};

static bool virtio_pci_modern_state_needed(void *opaque)
{
    VirtIOPCIProxy *proxy = opaque;

    return virtio_pci_modern(proxy);
}

static const VMStateDescription vmstate_virtio_pci_modern_state_sub = {
    .name = "virtio_pci/modern_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_pci_modern_state_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(dfselect, VirtIOPCIProxy),
        VMSTATE_UINT32(gfselect, VirtIOPCIProxy),
        VMSTATE_UINT32_ARRAY(guest_features, VirtIOPCIProxy, 2),
        VMSTATE_STRUCT_ARRAY(vqs, VirtIOPCIProxy, VIRTIO_QUEUE_MAX, 0,
                             vmstate_virtio_pci_modern_queue_state,
                             VirtIOPCIQueue),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_pci = {
    .name = "virtio_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_virtio_pci_modern_state_sub,
        NULL
    }
};

static bool virtio_pci_has_extra_state(DeviceState *d)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);

    return proxy->flags & VIRTIO_PCI_FLAG_MIGRATE_EXTRA;
}

static void virtio_pci_save_extra_state(DeviceState *d, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);

    vmstate_save_state(f, &vmstate_virtio_pci, proxy, NULL);
}

static int virtio_pci_load_extra_state(DeviceState *d, QEMUFile *f)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);

    return vmstate_load_state(f, &vmstate_virtio_pci, proxy, 1);
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

static bool virtio_pci_ioeventfd_enabled(DeviceState *d)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);

    return (proxy->flags & VIRTIO_PCI_FLAG_USE_IOEVENTFD) != 0;
}

#define QEMU_VIRTIO_PCI_QUEUE_MEM_MULT 0x1000

static inline int virtio_pci_queue_mem_mult(struct VirtIOPCIProxy *proxy)
{
    return (proxy->flags & VIRTIO_PCI_FLAG_PAGE_PER_VQ) ?
        QEMU_VIRTIO_PCI_QUEUE_MEM_MULT : 4;
}

static int virtio_pci_ioeventfd_assign(DeviceState *d, EventNotifier *notifier,
                                       int n, bool assign)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    bool legacy = virtio_pci_legacy(proxy);
    bool modern = virtio_pci_modern(proxy);
    bool fast_mmio = kvm_ioeventfd_any_length_enabled();
    bool modern_pio = proxy->flags & VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY;
    MemoryRegion *modern_mr = &proxy->notify.mr;
    MemoryRegion *modern_notify_mr = &proxy->notify_pio.mr;
    MemoryRegion *legacy_mr = &proxy->bar;
    hwaddr modern_addr = virtio_pci_queue_mem_mult(proxy) *
                         virtio_get_queue_index(vq);
    hwaddr legacy_addr = VIRTIO_PCI_QUEUE_NOTIFY;

    if (assign) {
        if (modern) {
            if (fast_mmio) {
                memory_region_add_eventfd(modern_mr, modern_addr, 0,
                                          false, n, notifier);
            } else {
                memory_region_add_eventfd(modern_mr, modern_addr, 2,
                                          false, n, notifier);
            }
            if (modern_pio) {
                memory_region_add_eventfd(modern_notify_mr, 0, 2,
                                              true, n, notifier);
            }
        }
        if (legacy) {
            memory_region_add_eventfd(legacy_mr, legacy_addr, 2,
                                      true, n, notifier);
        }
    } else {
        if (modern) {
            if (fast_mmio) {
                memory_region_del_eventfd(modern_mr, modern_addr, 0,
                                          false, n, notifier);
            } else {
                memory_region_del_eventfd(modern_mr, modern_addr, 2,
                                          false, n, notifier);
            }
            if (modern_pio) {
                memory_region_del_eventfd(modern_notify_mr, 0, 2,
                                          true, n, notifier);
            }
        }
        if (legacy) {
            memory_region_del_eventfd(legacy_mr, legacy_addr, 2,
                                      true, n, notifier);
        }
    }
    return 0;
}

static void virtio_pci_start_ioeventfd(VirtIOPCIProxy *proxy)
{
    virtio_bus_start_ioeventfd(&proxy->bus);
}

static void virtio_pci_stop_ioeventfd(VirtIOPCIProxy *proxy)
{
    virtio_bus_stop_ioeventfd(&proxy->bus);
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
            virtio_pci_reset(DEVICE(proxy));
        }
        else
            virtio_queue_set_addr(vdev, vdev->queue_sel, pa);
        break;
    case VIRTIO_PCI_QUEUE_SEL:
        if (val < VIRTIO_QUEUE_MAX)
            vdev->queue_sel = val;
        break;
    case VIRTIO_PCI_QUEUE_NOTIFY:
        if (val < VIRTIO_QUEUE_MAX) {
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
            virtio_pci_reset(DEVICE(proxy));
        }

        /* Linux before 2.6.34 drives the device without enabling
           the PCI device bus master bit. Enable it automatically
           for the guest. This is a PCI spec violation but so is
           initiating DMA with bus master bit clear. */
        if (val == (VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER)) {
            pci_default_write_config(&proxy->pci_dev, PCI_COMMAND,
                                     proxy->pci_dev.config[PCI_COMMAND] |
                                     PCI_COMMAND_MASTER, 1);
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
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unexpected address 0x%x value 0x%x\n",
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
        ret = vdev->host_features;
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
        ret = qatomic_xchg(&vdev->isr, 0);
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
    uint32_t config = VIRTIO_PCI_CONFIG_SIZE(&proxy->pci_dev);
    uint64_t val = 0;

    if (vdev == NULL) {
        return UINT64_MAX;
    }

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
    uint32_t config = VIRTIO_PCI_CONFIG_SIZE(&proxy->pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (vdev == NULL) {
        return;
    }

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

static MemoryRegion *virtio_address_space_lookup(VirtIOPCIProxy *proxy,
                                                 hwaddr *off, int len)
{
    int i;
    VirtIOPCIRegion *reg;

    for (i = 0; i < ARRAY_SIZE(proxy->regs); ++i) {
        reg = &proxy->regs[i];
        if (*off >= reg->offset &&
            *off + len <= reg->offset + reg->size) {
            *off -= reg->offset;
            return &reg->mr;
        }
    }

    return NULL;
}

/* Below are generic functions to do memcpy from/to an address space,
 * without byteswaps, with input validation.
 *
 * As regular address_space_* APIs all do some kind of byteswap at least for
 * some host/target combinations, we are forced to explicitly convert to a
 * known-endianness integer value.
 * It doesn't really matter which endian format to go through, so the code
 * below selects the endian that causes the least amount of work on the given
 * host.
 *
 * Note: host pointer must be aligned.
 */
static
void virtio_address_space_write(VirtIOPCIProxy *proxy, hwaddr addr,
                                const uint8_t *buf, int len)
{
    uint64_t val;
    MemoryRegion *mr;

    /* address_space_* APIs assume an aligned address.
     * As address is under guest control, handle illegal values.
     */
    addr &= ~(len - 1);

    mr = virtio_address_space_lookup(proxy, &addr, len);
    if (!mr) {
        return;
    }

    /* Make sure caller aligned buf properly */
    assert(!(((uintptr_t)buf) & (len - 1)));

    switch (len) {
    case 1:
        val = pci_get_byte(buf);
        break;
    case 2:
        val = pci_get_word(buf);
        break;
    case 4:
        val = pci_get_long(buf);
        break;
    default:
        /* As length is under guest control, handle illegal values. */
        return;
    }
    memory_region_dispatch_write(mr, addr, val, size_memop(len) | MO_LE,
                                 MEMTXATTRS_UNSPECIFIED);
}

static void
virtio_address_space_read(VirtIOPCIProxy *proxy, hwaddr addr,
                          uint8_t *buf, int len)
{
    uint64_t val;
    MemoryRegion *mr;

    /* address_space_* APIs assume an aligned address.
     * As address is under guest control, handle illegal values.
     */
    addr &= ~(len - 1);

    mr = virtio_address_space_lookup(proxy, &addr, len);
    if (!mr) {
        return;
    }

    /* Make sure caller aligned buf properly */
    assert(!(((uintptr_t)buf) & (len - 1)));

    memory_region_dispatch_read(mr, addr, &val, size_memop(len) | MO_LE,
                                MEMTXATTRS_UNSPECIFIED);
    switch (len) {
    case 1:
        pci_set_byte(buf, val);
        break;
    case 2:
        pci_set_word(buf, val);
        break;
    case 4:
        pci_set_long(buf, val);
        break;
    default:
        /* As length is under guest control, handle illegal values. */
        break;
    }
}

static void virtio_write_config(PCIDevice *pci_dev, uint32_t address,
                                uint32_t val, int len)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    struct virtio_pci_cfg_cap *cfg;

    pci_default_write_config(pci_dev, address, val, len);

    if (proxy->flags & VIRTIO_PCI_FLAG_INIT_FLR) {
        pcie_cap_flr_write_config(pci_dev, address, val, len);
    }

    if (range_covers_byte(address, len, PCI_COMMAND)) {
        if (!(pci_dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
            virtio_set_disabled(vdev, true);
            virtio_pci_stop_ioeventfd(proxy);
            virtio_set_status(vdev, vdev->status & ~VIRTIO_CONFIG_S_DRIVER_OK);
        } else {
            virtio_set_disabled(vdev, false);
        }
    }

    if (proxy->config_cap &&
        ranges_overlap(address, len, proxy->config_cap + offsetof(struct virtio_pci_cfg_cap,
                                                                  pci_cfg_data),
                       sizeof cfg->pci_cfg_data)) {
        uint32_t off;
        uint32_t len;

        cfg = (void *)(proxy->pci_dev.config + proxy->config_cap);
        off = le32_to_cpu(cfg->cap.offset);
        len = le32_to_cpu(cfg->cap.length);

        if (len == 1 || len == 2 || len == 4) {
            assert(len <= sizeof cfg->pci_cfg_data);
            virtio_address_space_write(proxy, off, cfg->pci_cfg_data, len);
        }
    }
}

static uint32_t virtio_read_config(PCIDevice *pci_dev,
                                   uint32_t address, int len)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(pci_dev);
    struct virtio_pci_cfg_cap *cfg;

    if (proxy->config_cap &&
        ranges_overlap(address, len, proxy->config_cap + offsetof(struct virtio_pci_cfg_cap,
                                                                  pci_cfg_data),
                       sizeof cfg->pci_cfg_data)) {
        uint32_t off;
        uint32_t len;

        cfg = (void *)(proxy->pci_dev.config + proxy->config_cap);
        off = le32_to_cpu(cfg->cap.offset);
        len = le32_to_cpu(cfg->cap.length);

        if (len == 1 || len == 2 || len == 4) {
            assert(len <= sizeof cfg->pci_cfg_data);
            virtio_address_space_read(proxy, off, cfg->pci_cfg_data, len);
        }
    }

    return pci_default_read_config(pci_dev, address, len);
}

static int kvm_virtio_pci_vq_vector_use(VirtIOPCIProxy *proxy,
                                        unsigned int queue_no,
                                        unsigned int vector)
{
    VirtIOIRQFD *irqfd = &proxy->vector_irqfd[vector];
    int ret;

    if (irqfd->users == 0) {
        KVMRouteChange c = kvm_irqchip_begin_route_changes(kvm_state);
        ret = kvm_irqchip_add_msi_route(&c, vector, &proxy->pci_dev);
        if (ret < 0) {
            return ret;
        }
        kvm_irqchip_commit_route_changes(&c);
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
    return kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, n, NULL, irqfd->virq);
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

    ret = kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, n, irqfd->virq);
    assert(ret == 0);
}

static int kvm_virtio_pci_vector_use(VirtIOPCIProxy *proxy, int nvqs)
{
    PCIDevice *dev = &proxy->pci_dev;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);
    unsigned int vector;
    int ret, queue_no;

    for (queue_no = 0; queue_no < nvqs; queue_no++) {
        if (!virtio_queue_get_num(vdev, queue_no)) {
            break;
        }
        vector = virtio_queue_vector(vdev, queue_no);
        if (vector >= msix_nr_vectors_allocated(dev)) {
            continue;
        }
        ret = kvm_virtio_pci_vq_vector_use(proxy, queue_no, vector);
        if (ret < 0) {
            goto undo;
        }
        /* If guest supports masking, set up irqfd now.
         * Otherwise, delay until unmasked in the frontend.
         */
        if (vdev->use_guest_notifier_mask && k->guest_notifier_mask) {
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
        if (vdev->use_guest_notifier_mask && k->guest_notifier_mask) {
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
        if (vdev->use_guest_notifier_mask && k->guest_notifier_mask) {
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
            ret = kvm_irqchip_update_msi_route(kvm_state, irqfd->virq, msg,
                                               &proxy->pci_dev);
            if (ret < 0) {
                return ret;
            }
            kvm_irqchip_commit_routes(kvm_state);
        }
    }

    /* If guest supports masking, irqfd is already setup, unmask it.
     * Otherwise, set it up now.
     */
    if (vdev->use_guest_notifier_mask && k->guest_notifier_mask) {
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
    if (vdev->use_guest_notifier_mask && k->guest_notifier_mask) {
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
    VirtQueue *vq = virtio_vector_first_queue(vdev, vector);
    int ret, index, unmasked = 0;

    while (vq) {
        index = virtio_get_queue_index(vq);
        if (!virtio_queue_get_num(vdev, index)) {
            break;
        }
        if (index < proxy->nvqs_with_notifiers) {
            ret = virtio_pci_vq_vector_unmask(proxy, index, vector, msg);
            if (ret < 0) {
                goto undo;
            }
            ++unmasked;
        }
        vq = virtio_vector_next_queue(vq);
    }

    return 0;

undo:
    vq = virtio_vector_first_queue(vdev, vector);
    while (vq && unmasked >= 0) {
        index = virtio_get_queue_index(vq);
        if (index < proxy->nvqs_with_notifiers) {
            virtio_pci_vq_vector_mask(proxy, index, vector);
            --unmasked;
        }
        vq = virtio_vector_next_queue(vq);
    }
    return ret;
}

static void virtio_pci_vector_mask(PCIDevice *dev, unsigned vector)
{
    VirtIOPCIProxy *proxy = container_of(dev, VirtIOPCIProxy, pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtQueue *vq = virtio_vector_first_queue(vdev, vector);
    int index;

    while (vq) {
        index = virtio_get_queue_index(vq);
        if (!virtio_queue_get_num(vdev, index)) {
            break;
        }
        if (index < proxy->nvqs_with_notifiers) {
            virtio_pci_vq_vector_mask(proxy, index, vector);
        }
        vq = virtio_vector_next_queue(vq);
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

    if (!msix_enabled(&proxy->pci_dev) &&
        vdev->use_guest_notifier_mask &&
        vdc->guest_notifier_mask) {
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

    nvqs = MIN(nvqs, VIRTIO_QUEUE_MAX);

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

static int virtio_pci_set_host_notifier_mr(DeviceState *d, int n,
                                           MemoryRegion *mr, bool assign)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    int offset;

    if (n >= VIRTIO_QUEUE_MAX || !virtio_pci_modern(proxy) ||
        virtio_pci_queue_mem_mult(proxy) != memory_region_size(mr)) {
        return -1;
    }

    if (assign) {
        offset = virtio_pci_queue_mem_mult(proxy) * n;
        memory_region_add_subregion_overlap(&proxy->notify.mr, offset, mr, 1);
    } else {
        memory_region_del_subregion(&proxy->notify.mr, mr);
    }

    return 0;
}

static void virtio_pci_vmstate_change(DeviceState *d, bool running)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (running) {
        /* Old QEMU versions did not set bus master enable on status write.
         * Detect DRIVER set and enable it.
         */
        if ((proxy->flags & VIRTIO_PCI_FLAG_BUS_MASTER_BUG_MIGRATION) &&
            (vdev->status & VIRTIO_CONFIG_S_DRIVER) &&
            !(proxy->pci_dev.config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
            pci_default_write_config(&proxy->pci_dev, PCI_COMMAND,
                                     proxy->pci_dev.config[PCI_COMMAND] |
                                     PCI_COMMAND_MASTER, 1);
        }
        virtio_pci_start_ioeventfd(proxy);
    } else {
        virtio_pci_stop_ioeventfd(proxy);
    }
}

/*
 * virtio-pci: This is the PCIDevice which has a virtio-pci-bus.
 */

static int virtio_pci_query_nvectors(DeviceState *d)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);

    return proxy->nvectors;
}

static AddressSpace *virtio_pci_get_dma_as(DeviceState *d)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    PCIDevice *dev = &proxy->pci_dev;

    return pci_get_address_space(dev);
}

static bool virtio_pci_iommu_enabled(DeviceState *d)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    PCIDevice *dev = &proxy->pci_dev;
    AddressSpace *dma_as = pci_device_iommu_address_space(dev);

    if (dma_as == &address_space_memory) {
        return false;
    }

    return true;
}

static bool virtio_pci_queue_enabled(DeviceState *d, int n)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1)) {
        return proxy->vqs[n].enabled;
    }

    return virtio_queue_enabled_legacy(vdev, n);
}

static int virtio_pci_add_mem_cap(VirtIOPCIProxy *proxy,
                                   struct virtio_pci_cap *cap)
{
    PCIDevice *dev = &proxy->pci_dev;
    int offset;

    offset = pci_add_capability(dev, PCI_CAP_ID_VNDR, 0,
                                cap->cap_len, &error_abort);

    assert(cap->cap_len >= sizeof *cap);
    memcpy(dev->config + offset + PCI_CAP_FLAGS, &cap->cap_len,
           cap->cap_len - PCI_CAP_FLAGS);

    return offset;
}

static uint64_t virtio_pci_common_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    uint32_t val = 0;
    int i;

    if (vdev == NULL) {
        return UINT64_MAX;
    }

    switch (addr) {
    case VIRTIO_PCI_COMMON_DFSELECT:
        val = proxy->dfselect;
        break;
    case VIRTIO_PCI_COMMON_DF:
        if (proxy->dfselect <= 1) {
            VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);

            val = (vdev->host_features & ~vdc->legacy_features) >>
                (32 * proxy->dfselect);
        }
        break;
    case VIRTIO_PCI_COMMON_GFSELECT:
        val = proxy->gfselect;
        break;
    case VIRTIO_PCI_COMMON_GF:
        if (proxy->gfselect < ARRAY_SIZE(proxy->guest_features)) {
            val = proxy->guest_features[proxy->gfselect];
        }
        break;
    case VIRTIO_PCI_COMMON_MSIX:
        val = vdev->config_vector;
        break;
    case VIRTIO_PCI_COMMON_NUMQ:
        for (i = 0; i < VIRTIO_QUEUE_MAX; ++i) {
            if (virtio_queue_get_num(vdev, i)) {
                val = i + 1;
            }
        }
        break;
    case VIRTIO_PCI_COMMON_STATUS:
        val = vdev->status;
        break;
    case VIRTIO_PCI_COMMON_CFGGENERATION:
        val = vdev->generation;
        break;
    case VIRTIO_PCI_COMMON_Q_SELECT:
        val = vdev->queue_sel;
        break;
    case VIRTIO_PCI_COMMON_Q_SIZE:
        val = virtio_queue_get_num(vdev, vdev->queue_sel);
        break;
    case VIRTIO_PCI_COMMON_Q_MSIX:
        val = virtio_queue_vector(vdev, vdev->queue_sel);
        break;
    case VIRTIO_PCI_COMMON_Q_ENABLE:
        val = proxy->vqs[vdev->queue_sel].enabled;
        break;
    case VIRTIO_PCI_COMMON_Q_NOFF:
        /* Simply map queues in order */
        val = vdev->queue_sel;
        break;
    case VIRTIO_PCI_COMMON_Q_DESCLO:
        val = proxy->vqs[vdev->queue_sel].desc[0];
        break;
    case VIRTIO_PCI_COMMON_Q_DESCHI:
        val = proxy->vqs[vdev->queue_sel].desc[1];
        break;
    case VIRTIO_PCI_COMMON_Q_AVAILLO:
        val = proxy->vqs[vdev->queue_sel].avail[0];
        break;
    case VIRTIO_PCI_COMMON_Q_AVAILHI:
        val = proxy->vqs[vdev->queue_sel].avail[1];
        break;
    case VIRTIO_PCI_COMMON_Q_USEDLO:
        val = proxy->vqs[vdev->queue_sel].used[0];
        break;
    case VIRTIO_PCI_COMMON_Q_USEDHI:
        val = proxy->vqs[vdev->queue_sel].used[1];
        break;
    default:
        val = 0;
    }

    return val;
}

static void virtio_pci_common_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (vdev == NULL) {
        return;
    }

    switch (addr) {
    case VIRTIO_PCI_COMMON_DFSELECT:
        proxy->dfselect = val;
        break;
    case VIRTIO_PCI_COMMON_GFSELECT:
        proxy->gfselect = val;
        break;
    case VIRTIO_PCI_COMMON_GF:
        if (proxy->gfselect < ARRAY_SIZE(proxy->guest_features)) {
            proxy->guest_features[proxy->gfselect] = val;
            virtio_set_features(vdev,
                                (((uint64_t)proxy->guest_features[1]) << 32) |
                                proxy->guest_features[0]);
        }
        break;
    case VIRTIO_PCI_COMMON_MSIX:
        msix_vector_unuse(&proxy->pci_dev, vdev->config_vector);
        /* Make it possible for guest to discover an error took place. */
        if (msix_vector_use(&proxy->pci_dev, val) < 0) {
            val = VIRTIO_NO_VECTOR;
        }
        vdev->config_vector = val;
        break;
    case VIRTIO_PCI_COMMON_STATUS:
        if (!(val & VIRTIO_CONFIG_S_DRIVER_OK)) {
            virtio_pci_stop_ioeventfd(proxy);
        }

        virtio_set_status(vdev, val & 0xFF);

        if (val & VIRTIO_CONFIG_S_DRIVER_OK) {
            virtio_pci_start_ioeventfd(proxy);
        }

        if (vdev->status == 0) {
            virtio_pci_reset(DEVICE(proxy));
        }

        break;
    case VIRTIO_PCI_COMMON_Q_SELECT:
        if (val < VIRTIO_QUEUE_MAX) {
            vdev->queue_sel = val;
        }
        break;
    case VIRTIO_PCI_COMMON_Q_SIZE:
        proxy->vqs[vdev->queue_sel].num = val;
        virtio_queue_set_num(vdev, vdev->queue_sel,
                             proxy->vqs[vdev->queue_sel].num);
        break;
    case VIRTIO_PCI_COMMON_Q_MSIX:
        msix_vector_unuse(&proxy->pci_dev,
                          virtio_queue_vector(vdev, vdev->queue_sel));
        /* Make it possible for guest to discover an error took place. */
        if (msix_vector_use(&proxy->pci_dev, val) < 0) {
            val = VIRTIO_NO_VECTOR;
        }
        virtio_queue_set_vector(vdev, vdev->queue_sel, val);
        break;
    case VIRTIO_PCI_COMMON_Q_ENABLE:
        if (val == 1) {
            virtio_queue_set_num(vdev, vdev->queue_sel,
                                 proxy->vqs[vdev->queue_sel].num);
            virtio_queue_set_rings(vdev, vdev->queue_sel,
                       ((uint64_t)proxy->vqs[vdev->queue_sel].desc[1]) << 32 |
                       proxy->vqs[vdev->queue_sel].desc[0],
                       ((uint64_t)proxy->vqs[vdev->queue_sel].avail[1]) << 32 |
                       proxy->vqs[vdev->queue_sel].avail[0],
                       ((uint64_t)proxy->vqs[vdev->queue_sel].used[1]) << 32 |
                       proxy->vqs[vdev->queue_sel].used[0]);
            proxy->vqs[vdev->queue_sel].enabled = 1;
        } else {
            virtio_error(vdev, "wrong value for queue_enable %"PRIx64, val);
        }
        break;
    case VIRTIO_PCI_COMMON_Q_DESCLO:
        proxy->vqs[vdev->queue_sel].desc[0] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_DESCHI:
        proxy->vqs[vdev->queue_sel].desc[1] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_AVAILLO:
        proxy->vqs[vdev->queue_sel].avail[0] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_AVAILHI:
        proxy->vqs[vdev->queue_sel].avail[1] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_USEDLO:
        proxy->vqs[vdev->queue_sel].used[0] = val;
        break;
    case VIRTIO_PCI_COMMON_Q_USEDHI:
        proxy->vqs[vdev->queue_sel].used[1] = val;
        break;
    default:
        break;
    }
}


static uint64_t virtio_pci_notify_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    if (virtio_bus_get_device(&proxy->bus) == NULL) {
        return UINT64_MAX;
    }

    return 0;
}

static void virtio_pci_notify_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    unsigned queue = addr / virtio_pci_queue_mem_mult(proxy);

    if (vdev != NULL && queue < VIRTIO_QUEUE_MAX) {
        virtio_queue_notify(vdev, queue);
    }
}

static void virtio_pci_notify_write_pio(void *opaque, hwaddr addr,
                                        uint64_t val, unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    unsigned queue = val;

    if (vdev != NULL && queue < VIRTIO_QUEUE_MAX) {
        virtio_queue_notify(vdev, queue);
    }
}

static uint64_t virtio_pci_isr_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    uint64_t val;

    if (vdev == NULL) {
        return UINT64_MAX;
    }

    val = qatomic_xchg(&vdev->isr, 0);
    pci_irq_deassert(&proxy->pci_dev);
    return val;
}

static void virtio_pci_isr_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
}

static uint64_t virtio_pci_device_read(void *opaque, hwaddr addr,
                                       unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    uint64_t val;

    if (vdev == NULL) {
        return UINT64_MAX;
    }

    switch (size) {
    case 1:
        val = virtio_config_modern_readb(vdev, addr);
        break;
    case 2:
        val = virtio_config_modern_readw(vdev, addr);
        break;
    case 4:
        val = virtio_config_modern_readl(vdev, addr);
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void virtio_pci_device_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (vdev == NULL) {
        return;
    }

    switch (size) {
    case 1:
        virtio_config_modern_writeb(vdev, addr, val);
        break;
    case 2:
        virtio_config_modern_writew(vdev, addr, val);
        break;
    case 4:
        virtio_config_modern_writel(vdev, addr, val);
        break;
    }
}

static void virtio_pci_modern_regions_init(VirtIOPCIProxy *proxy,
                                           const char *vdev_name)
{
    static const MemoryRegionOps common_ops = {
        .read = virtio_pci_common_read,
        .write = virtio_pci_common_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps isr_ops = {
        .read = virtio_pci_isr_read,
        .write = virtio_pci_isr_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps device_ops = {
        .read = virtio_pci_device_read,
        .write = virtio_pci_device_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps notify_ops = {
        .read = virtio_pci_notify_read,
        .write = virtio_pci_notify_write,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    static const MemoryRegionOps notify_pio_ops = {
        .read = virtio_pci_notify_read,
        .write = virtio_pci_notify_write_pio,
        .impl = {
            .min_access_size = 1,
            .max_access_size = 4,
        },
        .endianness = DEVICE_LITTLE_ENDIAN,
    };
    g_autoptr(GString) name = g_string_new(NULL);

    g_string_printf(name, "virtio-pci-common-%s", vdev_name);
    memory_region_init_io(&proxy->common.mr, OBJECT(proxy),
                          &common_ops,
                          proxy,
                          name->str,
                          proxy->common.size);

    g_string_printf(name, "virtio-pci-isr-%s", vdev_name);
    memory_region_init_io(&proxy->isr.mr, OBJECT(proxy),
                          &isr_ops,
                          proxy,
                          name->str,
                          proxy->isr.size);

    g_string_printf(name, "virtio-pci-device-%s", vdev_name);
    memory_region_init_io(&proxy->device.mr, OBJECT(proxy),
                          &device_ops,
                          proxy,
                          name->str,
                          proxy->device.size);

    g_string_printf(name, "virtio-pci-notify-%s", vdev_name);
    memory_region_init_io(&proxy->notify.mr, OBJECT(proxy),
                          &notify_ops,
                          proxy,
                          name->str,
                          proxy->notify.size);

    g_string_printf(name, "virtio-pci-notify-pio-%s", vdev_name);
    memory_region_init_io(&proxy->notify_pio.mr, OBJECT(proxy),
                          &notify_pio_ops,
                          proxy,
                          name->str,
                          proxy->notify_pio.size);
}

static void virtio_pci_modern_region_map(VirtIOPCIProxy *proxy,
                                         VirtIOPCIRegion *region,
                                         struct virtio_pci_cap *cap,
                                         MemoryRegion *mr,
                                         uint8_t bar)
{
    memory_region_add_subregion(mr, region->offset, &region->mr);

    cap->cfg_type = region->type;
    cap->bar = bar;
    cap->offset = cpu_to_le32(region->offset);
    cap->length = cpu_to_le32(region->size);
    virtio_pci_add_mem_cap(proxy, cap);

}

static void virtio_pci_modern_mem_region_map(VirtIOPCIProxy *proxy,
                                             VirtIOPCIRegion *region,
                                             struct virtio_pci_cap *cap)
{
    virtio_pci_modern_region_map(proxy, region, cap,
                                 &proxy->modern_bar, proxy->modern_mem_bar_idx);
}

static void virtio_pci_modern_io_region_map(VirtIOPCIProxy *proxy,
                                            VirtIOPCIRegion *region,
                                            struct virtio_pci_cap *cap)
{
    virtio_pci_modern_region_map(proxy, region, cap,
                                 &proxy->io_bar, proxy->modern_io_bar_idx);
}

static void virtio_pci_modern_mem_region_unmap(VirtIOPCIProxy *proxy,
                                               VirtIOPCIRegion *region)
{
    memory_region_del_subregion(&proxy->modern_bar,
                                &region->mr);
}

static void virtio_pci_modern_io_region_unmap(VirtIOPCIProxy *proxy,
                                              VirtIOPCIRegion *region)
{
    memory_region_del_subregion(&proxy->io_bar,
                                &region->mr);
}

static void virtio_pci_pre_plugged(DeviceState *d, Error **errp)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (virtio_pci_modern(proxy)) {
        virtio_add_feature(&vdev->host_features, VIRTIO_F_VERSION_1);
    }

    virtio_add_feature(&vdev->host_features, VIRTIO_F_BAD_FEATURE);
}

/* This is called by virtio-bus just after the device is plugged. */
static void virtio_pci_device_plugged(DeviceState *d, Error **errp)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    VirtioBusState *bus = &proxy->bus;
    bool legacy = virtio_pci_legacy(proxy);
    bool modern;
    bool modern_pio = proxy->flags & VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY;
    uint8_t *config;
    uint32_t size;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    /*
     * Virtio capabilities present without
     * VIRTIO_F_VERSION_1 confuses guests
     */
    if (!proxy->ignore_backend_features &&
            !virtio_has_feature(vdev->host_features, VIRTIO_F_VERSION_1)) {
        virtio_pci_disable_modern(proxy);

        if (!legacy) {
            error_setg(errp, "Device doesn't support modern mode, and legacy"
                             " mode is disabled");
            error_append_hint(errp, "Set disable-legacy to off\n");

            return;
        }
    }

    modern = virtio_pci_modern(proxy);

    config = proxy->pci_dev.config;
    if (proxy->class_code) {
        pci_config_set_class(config, proxy->class_code);
    }

    if (legacy) {
        if (!virtio_legacy_allowed(vdev)) {
            /*
             * To avoid migration issues, we allow legacy mode when legacy
             * check is disabled in the old machine types (< 5.1).
             */
            if (virtio_legacy_check_disabled(vdev)) {
                warn_report("device is modern-only, but for backward "
                            "compatibility legacy is allowed");
            } else {
                error_setg(errp,
                           "device is modern-only, use disable-legacy=on");
                return;
            }
        }
        if (virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM)) {
            error_setg(errp, "VIRTIO_F_IOMMU_PLATFORM was supported by"
                       " neither legacy nor transitional device");
            return ;
        }
        /*
         * Legacy and transitional devices use specific subsystem IDs.
         * Note that the subsystem vendor ID (config + PCI_SUBSYSTEM_VENDOR_ID)
         * is set to PCI_SUBVENDOR_ID_REDHAT_QUMRANET by default.
         */
        pci_set_word(config + PCI_SUBSYSTEM_ID, virtio_bus_get_vdev_id(bus));
    } else {
        /* pure virtio-1.0 */
        pci_set_word(config + PCI_VENDOR_ID,
                     PCI_VENDOR_ID_REDHAT_QUMRANET);
        pci_set_word(config + PCI_DEVICE_ID,
                     0x1040 + virtio_bus_get_vdev_id(bus));
        pci_config_set_revision(config, 1);
    }
    config[PCI_INTERRUPT_PIN] = 1;


    if (modern) {
        struct virtio_pci_cap cap = {
            .cap_len = sizeof cap,
        };
        struct virtio_pci_notify_cap notify = {
            .cap.cap_len = sizeof notify,
            .notify_off_multiplier =
                cpu_to_le32(virtio_pci_queue_mem_mult(proxy)),
        };
        struct virtio_pci_cfg_cap cfg = {
            .cap.cap_len = sizeof cfg,
            .cap.cfg_type = VIRTIO_PCI_CAP_PCI_CFG,
        };
        struct virtio_pci_notify_cap notify_pio = {
            .cap.cap_len = sizeof notify,
            .notify_off_multiplier = cpu_to_le32(0x0),
        };

        struct virtio_pci_cfg_cap *cfg_mask;

        virtio_pci_modern_regions_init(proxy, vdev->name);

        virtio_pci_modern_mem_region_map(proxy, &proxy->common, &cap);
        virtio_pci_modern_mem_region_map(proxy, &proxy->isr, &cap);
        virtio_pci_modern_mem_region_map(proxy, &proxy->device, &cap);
        virtio_pci_modern_mem_region_map(proxy, &proxy->notify, &notify.cap);

        if (modern_pio) {
            memory_region_init(&proxy->io_bar, OBJECT(proxy),
                               "virtio-pci-io", 0x4);

            pci_register_bar(&proxy->pci_dev, proxy->modern_io_bar_idx,
                             PCI_BASE_ADDRESS_SPACE_IO, &proxy->io_bar);

            virtio_pci_modern_io_region_map(proxy, &proxy->notify_pio,
                                            &notify_pio.cap);
        }

        pci_register_bar(&proxy->pci_dev, proxy->modern_mem_bar_idx,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_PREFETCH |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                         &proxy->modern_bar);

        proxy->config_cap = virtio_pci_add_mem_cap(proxy, &cfg.cap);
        cfg_mask = (void *)(proxy->pci_dev.wmask + proxy->config_cap);
        pci_set_byte(&cfg_mask->cap.bar, ~0x0);
        pci_set_long((uint8_t *)&cfg_mask->cap.offset, ~0x0);
        pci_set_long((uint8_t *)&cfg_mask->cap.length, ~0x0);
        pci_set_long(cfg_mask->pci_cfg_data, ~0x0);
    }

    if (proxy->nvectors) {
        int err = msix_init_exclusive_bar(&proxy->pci_dev, proxy->nvectors,
                                          proxy->msix_bar_idx, NULL);
        if (err) {
            /* Notice when a system that supports MSIx can't initialize it */
            if (err != -ENOTSUP) {
                warn_report("unable to init msix vectors to %" PRIu32,
                            proxy->nvectors);
            }
            proxy->nvectors = 0;
        }
    }

    proxy->pci_dev.config_write = virtio_write_config;
    proxy->pci_dev.config_read = virtio_read_config;

    if (legacy) {
        size = VIRTIO_PCI_REGION_SIZE(&proxy->pci_dev)
            + virtio_bus_get_vdev_config_len(bus);
        size = pow2ceil(size);

        memory_region_init_io(&proxy->bar, OBJECT(proxy),
                              &virtio_pci_config_ops,
                              proxy, "virtio-pci", size);

        pci_register_bar(&proxy->pci_dev, proxy->legacy_io_bar_idx,
                         PCI_BASE_ADDRESS_SPACE_IO, &proxy->bar);
    }
}

static void virtio_pci_device_unplugged(DeviceState *d)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(d);
    bool modern = virtio_pci_modern(proxy);
    bool modern_pio = proxy->flags & VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY;

    virtio_pci_stop_ioeventfd(proxy);

    if (modern) {
        virtio_pci_modern_mem_region_unmap(proxy, &proxy->common);
        virtio_pci_modern_mem_region_unmap(proxy, &proxy->isr);
        virtio_pci_modern_mem_region_unmap(proxy, &proxy->device);
        virtio_pci_modern_mem_region_unmap(proxy, &proxy->notify);
        if (modern_pio) {
            virtio_pci_modern_io_region_unmap(proxy, &proxy->notify_pio);
        }
    }
}

static void virtio_pci_realize(PCIDevice *pci_dev, Error **errp)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(pci_dev);
    VirtioPCIClass *k = VIRTIO_PCI_GET_CLASS(pci_dev);
    bool pcie_port = pci_bus_is_express(pci_get_bus(pci_dev)) &&
                     !pci_bus_is_root(pci_get_bus(pci_dev));

    if (kvm_enabled() && !kvm_has_many_ioeventfds()) {
        proxy->flags &= ~VIRTIO_PCI_FLAG_USE_IOEVENTFD;
    }

    /* fd-based ioevents can't be synchronized in record/replay */
    if (replay_mode != REPLAY_MODE_NONE) {
        proxy->flags &= ~VIRTIO_PCI_FLAG_USE_IOEVENTFD;
    }

    /*
     * virtio pci bar layout used by default.
     * subclasses can re-arrange things if needed.
     *
     *   region 0   --  virtio legacy io bar
     *   region 1   --  msi-x bar
     *   region 2   --  virtio modern io bar (off by default)
     *   region 4+5 --  virtio modern memory (64bit) bar
     *
     */
    proxy->legacy_io_bar_idx  = 0;
    proxy->msix_bar_idx       = 1;
    proxy->modern_io_bar_idx  = 2;
    proxy->modern_mem_bar_idx = 4;

    proxy->common.offset = 0x0;
    proxy->common.size = 0x1000;
    proxy->common.type = VIRTIO_PCI_CAP_COMMON_CFG;

    proxy->isr.offset = 0x1000;
    proxy->isr.size = 0x1000;
    proxy->isr.type = VIRTIO_PCI_CAP_ISR_CFG;

    proxy->device.offset = 0x2000;
    proxy->device.size = 0x1000;
    proxy->device.type = VIRTIO_PCI_CAP_DEVICE_CFG;

    proxy->notify.offset = 0x3000;
    proxy->notify.size = virtio_pci_queue_mem_mult(proxy) * VIRTIO_QUEUE_MAX;
    proxy->notify.type = VIRTIO_PCI_CAP_NOTIFY_CFG;

    proxy->notify_pio.offset = 0x0;
    proxy->notify_pio.size = 0x4;
    proxy->notify_pio.type = VIRTIO_PCI_CAP_NOTIFY_CFG;

    /* subclasses can enforce modern, so do this unconditionally */
    memory_region_init(&proxy->modern_bar, OBJECT(proxy), "virtio-pci",
                       /* PCI BAR regions must be powers of 2 */
                       pow2ceil(proxy->notify.offset + proxy->notify.size));

    if (proxy->disable_legacy == ON_OFF_AUTO_AUTO) {
        proxy->disable_legacy = pcie_port ? ON_OFF_AUTO_ON : ON_OFF_AUTO_OFF;
    }

    if (!virtio_pci_modern(proxy) && !virtio_pci_legacy(proxy)) {
        error_setg(errp, "device cannot work as neither modern nor legacy mode"
                   " is enabled");
        error_append_hint(errp, "Set either disable-modern or disable-legacy"
                          " to off\n");
        return;
    }

    if (pcie_port && pci_is_express(pci_dev)) {
        int pos;
        uint16_t last_pcie_cap_offset = PCI_CONFIG_SPACE_SIZE;

        pos = pcie_endpoint_cap_init(pci_dev, 0);
        assert(pos > 0);

        pos = pci_add_capability(pci_dev, PCI_CAP_ID_PM, 0,
                                 PCI_PM_SIZEOF, errp);
        if (pos < 0) {
            return;
        }

        pci_dev->exp.pm_cap = pos;

        /*
         * Indicates that this function complies with revision 1.2 of the
         * PCI Power Management Interface Specification.
         */
        pci_set_word(pci_dev->config + pos + PCI_PM_PMC, 0x3);

        if (proxy->flags & VIRTIO_PCI_FLAG_AER) {
            pcie_aer_init(pci_dev, PCI_ERR_VER, last_pcie_cap_offset,
                          PCI_ERR_SIZEOF, NULL);
            last_pcie_cap_offset += PCI_ERR_SIZEOF;
        }

        if (proxy->flags & VIRTIO_PCI_FLAG_INIT_DEVERR) {
            /* Init error enabling flags */
            pcie_cap_deverr_init(pci_dev);
        }

        if (proxy->flags & VIRTIO_PCI_FLAG_INIT_LNKCTL) {
            /* Init Link Control Register */
            pcie_cap_lnkctl_init(pci_dev);
        }

        if (proxy->flags & VIRTIO_PCI_FLAG_INIT_PM) {
            /* Init Power Management Control Register */
            pci_set_word(pci_dev->wmask + pos + PCI_PM_CTRL,
                         PCI_PM_CTRL_STATE_MASK);
        }

        if (proxy->flags & VIRTIO_PCI_FLAG_ATS) {
            pcie_ats_init(pci_dev, last_pcie_cap_offset,
                          proxy->flags & VIRTIO_PCI_FLAG_ATS_PAGE_ALIGNED);
            last_pcie_cap_offset += PCI_EXT_CAP_ATS_SIZEOF;
        }

        if (proxy->flags & VIRTIO_PCI_FLAG_INIT_FLR) {
            /* Set Function Level Reset capability bit */
            pcie_cap_flr_init(pci_dev);
        }
    } else {
        /*
         * make future invocations of pci_is_express() return false
         * and pci_config_size() return PCI_CONFIG_SPACE_SIZE.
         */
        pci_dev->cap_present &= ~QEMU_PCI_CAP_EXPRESS;
    }

    virtio_pci_bus_new(&proxy->bus, sizeof(proxy->bus), proxy);
    if (k->realize) {
        k->realize(proxy, errp);
    }
}

static void virtio_pci_exit(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(pci_dev);
    bool pcie_port = pci_bus_is_express(pci_get_bus(pci_dev)) &&
                     !pci_bus_is_root(pci_get_bus(pci_dev));

    msix_uninit_exclusive_bar(pci_dev);
    if (proxy->flags & VIRTIO_PCI_FLAG_AER && pcie_port &&
        pci_is_express(pci_dev)) {
        pcie_aer_exit(pci_dev);
    }
}

static void virtio_pci_reset(DeviceState *qdev)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(qdev);
    VirtioBusState *bus = VIRTIO_BUS(&proxy->bus);
    PCIDevice *dev = PCI_DEVICE(qdev);
    int i;

    virtio_pci_stop_ioeventfd(proxy);
    virtio_bus_reset(bus);
    msix_unuse_all_vectors(&proxy->pci_dev);

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        proxy->vqs[i].enabled = 0;
        proxy->vqs[i].num = 0;
        proxy->vqs[i].desc[0] = proxy->vqs[i].desc[1] = 0;
        proxy->vqs[i].avail[0] = proxy->vqs[i].avail[1] = 0;
        proxy->vqs[i].used[0] = proxy->vqs[i].used[1] = 0;
    }

    if (pci_is_express(dev)) {
        pcie_cap_deverr_reset(dev);
        pcie_cap_lnkctl_reset(dev);

        pci_set_word(dev->config + dev->exp.pm_cap + PCI_PM_CTRL, 0);
    }
}

static Property virtio_pci_properties[] = {
    DEFINE_PROP_BIT("virtio-pci-bus-master-bug-migration", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_BUS_MASTER_BUG_MIGRATION_BIT, false),
    DEFINE_PROP_BIT("migrate-extra", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_MIGRATE_EXTRA_BIT, true),
    DEFINE_PROP_BIT("modern-pio-notify", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_MODERN_PIO_NOTIFY_BIT, false),
    DEFINE_PROP_BIT("x-disable-pcie", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_DISABLE_PCIE_BIT, false),
    DEFINE_PROP_BIT("page-per-vq", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_PAGE_PER_VQ_BIT, false),
    DEFINE_PROP_BOOL("x-ignore-backend-features", VirtIOPCIProxy,
                     ignore_backend_features, false),
    DEFINE_PROP_BIT("ats", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_ATS_BIT, false),
    DEFINE_PROP_BIT("x-ats-page-aligned", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_ATS_PAGE_ALIGNED_BIT, true),
    DEFINE_PROP_BIT("x-pcie-deverr-init", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_INIT_DEVERR_BIT, true),
    DEFINE_PROP_BIT("x-pcie-lnkctl-init", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_INIT_LNKCTL_BIT, true),
    DEFINE_PROP_BIT("x-pcie-pm-init", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_INIT_PM_BIT, true),
    DEFINE_PROP_BIT("x-pcie-flr-init", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_INIT_FLR_BIT, true),
    DEFINE_PROP_BIT("aer", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_AER_BIT, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_pci_dc_realize(DeviceState *qdev, Error **errp)
{
    VirtioPCIClass *vpciklass = VIRTIO_PCI_GET_CLASS(qdev);
    VirtIOPCIProxy *proxy = VIRTIO_PCI(qdev);
    PCIDevice *pci_dev = &proxy->pci_dev;

    if (!(proxy->flags & VIRTIO_PCI_FLAG_DISABLE_PCIE) &&
        virtio_pci_modern(proxy)) {
        pci_dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    }

    vpciklass->parent_dc_realize(qdev, errp);
}

static void virtio_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);

    device_class_set_props(dc, virtio_pci_properties);
    k->realize = virtio_pci_realize;
    k->exit = virtio_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_OTHERS;
    device_class_set_parent_realize(dc, virtio_pci_dc_realize,
                                    &vpciklass->parent_dc_realize);
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

static Property virtio_pci_generic_properties[] = {
    DEFINE_PROP_ON_OFF_AUTO("disable-legacy", VirtIOPCIProxy, disable_legacy,
                            ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BOOL("disable-modern", VirtIOPCIProxy, disable_modern, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_pci_base_class_init(ObjectClass *klass, void *data)
{
    const VirtioPCIDeviceTypeInfo *t = data;
    if (t->class_init) {
        t->class_init(klass, NULL);
    }
}

static void virtio_pci_generic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_pci_generic_properties);
}

static void virtio_pci_transitional_instance_init(Object *obj)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(obj);

    proxy->disable_legacy = ON_OFF_AUTO_OFF;
    proxy->disable_modern = false;
}

static void virtio_pci_non_transitional_instance_init(Object *obj)
{
    VirtIOPCIProxy *proxy = VIRTIO_PCI(obj);

    proxy->disable_legacy = ON_OFF_AUTO_ON;
    proxy->disable_modern = false;
}

void virtio_pci_types_register(const VirtioPCIDeviceTypeInfo *t)
{
    char *base_name = NULL;
    TypeInfo base_type_info = {
        .name          = t->base_name,
        .parent        = t->parent ? t->parent : TYPE_VIRTIO_PCI,
        .instance_size = t->instance_size,
        .instance_init = t->instance_init,
        .class_size    = t->class_size,
        .abstract      = true,
        .interfaces    = t->interfaces,
    };
    TypeInfo generic_type_info = {
        .name = t->generic_name,
        .parent = base_type_info.name,
        .class_init = virtio_pci_generic_class_init,
        .interfaces = (InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { }
        },
    };

    if (!base_type_info.name) {
        /* No base type -> register a single generic device type */
        /* use intermediate %s-base-type to add generic device props */
        base_name = g_strdup_printf("%s-base-type", t->generic_name);
        base_type_info.name = base_name;
        base_type_info.class_init = virtio_pci_generic_class_init;

        generic_type_info.parent = base_name;
        generic_type_info.class_init = virtio_pci_base_class_init;
        generic_type_info.class_data = (void *)t;

        assert(!t->non_transitional_name);
        assert(!t->transitional_name);
    } else {
        base_type_info.class_init = virtio_pci_base_class_init;
        base_type_info.class_data = (void *)t;
    }

    type_register(&base_type_info);
    if (generic_type_info.name) {
        type_register(&generic_type_info);
    }

    if (t->non_transitional_name) {
        const TypeInfo non_transitional_type_info = {
            .name          = t->non_transitional_name,
            .parent        = base_type_info.name,
            .instance_init = virtio_pci_non_transitional_instance_init,
            .interfaces = (InterfaceInfo[]) {
                { INTERFACE_PCIE_DEVICE },
                { INTERFACE_CONVENTIONAL_PCI_DEVICE },
                { }
            },
        };
        type_register(&non_transitional_type_info);
    }

    if (t->transitional_name) {
        const TypeInfo transitional_type_info = {
            .name          = t->transitional_name,
            .parent        = base_type_info.name,
            .instance_init = virtio_pci_transitional_instance_init,
            .interfaces = (InterfaceInfo[]) {
                /*
                 * Transitional virtio devices work only as Conventional PCI
                 * devices because they require PIO ports.
                 */
                { INTERFACE_CONVENTIONAL_PCI_DEVICE },
                { }
            },
        };
        type_register(&transitional_type_info);
    }
    g_free(base_name);
}

unsigned virtio_pci_optimal_num_queues(unsigned fixed_queues)
{
    /*
     * 1:1 vq to vCPU mapping is ideal because the same vCPU that submitted
     * virtqueue buffers can handle their completion. When a different vCPU
     * handles completion it may need to IPI the vCPU that submitted the
     * request and this adds overhead.
     *
     * Virtqueues consume guest RAM and MSI-X vectors. This is wasteful in
     * guests with very many vCPUs and a device that is only used by a few
     * vCPUs. Unfortunately optimizing that case requires manual pinning inside
     * the guest, so those users might as well manually set the number of
     * queues. There is no upper limit that can be applied automatically and
     * doing so arbitrarily would result in a sudden performance drop once the
     * threshold number of vCPUs is exceeded.
     */
    unsigned num_queues = current_machine->smp.cpus;

    /*
     * The maximum number of MSI-X vectors is PCI_MSIX_FLAGS_QSIZE + 1, but the
     * config change interrupt and the fixed virtqueues must be taken into
     * account too.
     */
    num_queues = MIN(num_queues, PCI_MSIX_FLAGS_QSIZE - fixed_queues);

    /*
     * There is a limit to how many virtqueues a device can have.
     */
    return MIN(num_queues, VIRTIO_QUEUE_MAX - fixed_queues);
}

/* virtio-pci-bus */

static void virtio_pci_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtIOPCIProxy *dev)
{
    DeviceState *qdev = DEVICE(dev);
    char virtio_bus_name[] = "virtio-bus";

    qbus_init(bus, bus_size, TYPE_VIRTIO_PCI_BUS, qdev, virtio_bus_name);
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
    k->save_extra_state = virtio_pci_save_extra_state;
    k->load_extra_state = virtio_pci_load_extra_state;
    k->has_extra_state = virtio_pci_has_extra_state;
    k->query_guest_notifiers = virtio_pci_query_guest_notifiers;
    k->set_guest_notifiers = virtio_pci_set_guest_notifiers;
    k->set_host_notifier_mr = virtio_pci_set_host_notifier_mr;
    k->vmstate_change = virtio_pci_vmstate_change;
    k->pre_plugged = virtio_pci_pre_plugged;
    k->device_plugged = virtio_pci_device_plugged;
    k->device_unplugged = virtio_pci_device_unplugged;
    k->query_nvectors = virtio_pci_query_nvectors;
    k->ioeventfd_enabled = virtio_pci_ioeventfd_enabled;
    k->ioeventfd_assign = virtio_pci_ioeventfd_assign;
    k->get_dma_as = virtio_pci_get_dma_as;
    k->iommu_enabled = virtio_pci_iommu_enabled;
    k->queue_enabled = virtio_pci_queue_enabled;
}

static const TypeInfo virtio_pci_bus_info = {
    .name          = TYPE_VIRTIO_PCI_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioPCIBusState),
    .class_size    = sizeof(VirtioPCIBusClass),
    .class_init    = virtio_pci_bus_class_init,
};

static void virtio_pci_register_types(void)
{
    /* Base types: */
    type_register_static(&virtio_pci_bus_info);
    type_register_static(&virtio_pci_info);
}

type_init(virtio_pci_register_types)

