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

#include "standard-headers/linux/virtio_pci.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-blk.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/virtio-serial.h"
#include "hw/virtio/virtio-scsi.h"
#include "hw/virtio/virtio-balloon.h"
#include "hw/virtio/virtio-input.h"
#include "hw/pci/pci.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/loader.h"
#include "sysemu/kvm.h"
#include "sysemu/block-backend.h"
#include "virtio-pci.h"
#include "qemu/range.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/visitor.h"

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
        pci_set_irq(&proxy->pci_dev, atomic_read(&vdev->isr) & 1);
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

static void virtio_pci_load_modern_queue_state(VirtIOPCIQueue *vq,
                                               QEMUFile *f)
{
    vq->num = qemu_get_be16(f);
    vq->enabled = qemu_get_be16(f);
    vq->desc[0] = qemu_get_be32(f);
    vq->desc[1] = qemu_get_be32(f);
    vq->avail[0] = qemu_get_be32(f);
    vq->avail[1] = qemu_get_be32(f);
    vq->used[0] = qemu_get_be32(f);
    vq->used[1] = qemu_get_be32(f);
}

static bool virtio_pci_has_extra_state(DeviceState *d)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy(d);

    return proxy->flags & VIRTIO_PCI_FLAG_MIGRATE_EXTRA;
}

static int get_virtio_pci_modern_state(QEMUFile *f, void *pv, size_t size,
                                       VMStateField *field)
{
    VirtIOPCIProxy *proxy = pv;
    int i;

    proxy->dfselect = qemu_get_be32(f);
    proxy->gfselect = qemu_get_be32(f);
    proxy->guest_features[0] = qemu_get_be32(f);
    proxy->guest_features[1] = qemu_get_be32(f);
    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        virtio_pci_load_modern_queue_state(&proxy->vqs[i], f);
    }

    return 0;
}

static void virtio_pci_save_modern_queue_state(VirtIOPCIQueue *vq,
                                               QEMUFile *f)
{
    qemu_put_be16(f, vq->num);
    qemu_put_be16(f, vq->enabled);
    qemu_put_be32(f, vq->desc[0]);
    qemu_put_be32(f, vq->desc[1]);
    qemu_put_be32(f, vq->avail[0]);
    qemu_put_be32(f, vq->avail[1]);
    qemu_put_be32(f, vq->used[0]);
    qemu_put_be32(f, vq->used[1]);
}

static int put_virtio_pci_modern_state(QEMUFile *f, void *pv, size_t size,
                                       VMStateField *field, QJSON *vmdesc)
{
    VirtIOPCIProxy *proxy = pv;
    int i;

    qemu_put_be32(f, proxy->dfselect);
    qemu_put_be32(f, proxy->gfselect);
    qemu_put_be32(f, proxy->guest_features[0]);
    qemu_put_be32(f, proxy->guest_features[1]);
    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        virtio_pci_save_modern_queue_state(&proxy->vqs[i], f);
    }

    return 0;
}

static const VMStateInfo vmstate_info_virtio_pci_modern_state = {
    .name = "virtqueue_state",
    .get = get_virtio_pci_modern_state,
    .put = put_virtio_pci_modern_state,
};

static bool virtio_pci_modern_state_needed(void *opaque)
{
    VirtIOPCIProxy *proxy = opaque;

    return virtio_pci_modern(proxy);
}

static const VMStateDescription vmstate_virtio_pci_modern_state = {
    .name = "virtio_pci/modern_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = &virtio_pci_modern_state_needed,
    .fields = (VMStateField[]) {
        {
            .name         = "modern_state",
            .version_id   = 0,
            .field_exists = NULL,
            .size         = 0,
            .info         = &vmstate_info_virtio_pci_modern_state,
            .flags        = VMS_SINGLE,
            .offset       = 0,
        },
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_pci = {
    .name = "virtio_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_virtio_pci_modern_state,
        NULL
    }
};

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
        ret = atomic_xchg(&vdev->isr, 0);
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
void virtio_address_space_write(AddressSpace *as, hwaddr addr,
                                const uint8_t *buf, int len)
{
    uint32_t val;

    /* address_space_* APIs assume an aligned address.
     * As address is under guest control, handle illegal values.
     */
    addr &= ~(len - 1);

    /* Make sure caller aligned buf properly */
    assert(!(((uintptr_t)buf) & (len - 1)));

    switch (len) {
    case 1:
        val = pci_get_byte(buf);
        address_space_stb(as, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 2:
        val = pci_get_word(buf);
        address_space_stw_le(as, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    case 4:
        val = pci_get_long(buf);
        address_space_stl_le(as, addr, val, MEMTXATTRS_UNSPECIFIED, NULL);
        break;
    default:
        /* As length is under guest control, handle illegal values. */
        break;
    }
}

static void
virtio_address_space_read(AddressSpace *as, hwaddr addr, uint8_t *buf, int len)
{
    uint32_t val;

    /* address_space_* APIs assume an aligned address.
     * As address is under guest control, handle illegal values.
     */
    addr &= ~(len - 1);

    /* Make sure caller aligned buf properly */
    assert(!(((uintptr_t)buf) & (len - 1)));

    switch (len) {
    case 1:
        val = address_space_ldub(as, addr, MEMTXATTRS_UNSPECIFIED, NULL);
        pci_set_byte(buf, val);
        break;
    case 2:
        val = address_space_lduw_le(as, addr, MEMTXATTRS_UNSPECIFIED, NULL);
        pci_set_word(buf, val);
        break;
    case 4:
        val = address_space_ldl_le(as, addr, MEMTXATTRS_UNSPECIFIED, NULL);
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
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    struct virtio_pci_cfg_cap *cfg;

    pci_default_write_config(pci_dev, address, val, len);

    if (range_covers_byte(address, len, PCI_COMMAND) &&
        !(pci_dev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        virtio_pci_stop_ioeventfd(proxy);
        virtio_set_status(vdev, vdev->status & ~VIRTIO_CONFIG_S_DRIVER_OK);
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
            virtio_address_space_write(&proxy->modern_as, off,
                                       cfg->pci_cfg_data, len);
        }
    }
}

static uint32_t virtio_read_config(PCIDevice *pci_dev,
                                   uint32_t address, int len)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
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
            virtio_address_space_read(&proxy->modern_as, off,
                                      cfg->pci_cfg_data, len);
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
        ret = kvm_irqchip_add_msi_route(kvm_state, vector, &proxy->pci_dev);
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

#ifdef CONFIG_VIRTFS
static void virtio_9p_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    V9fsPCIState *dev = VIRTIO_9P_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static Property virtio_9p_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9p_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);

    k->realize = virtio_9p_pci_realize;
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

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_9P);
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
    return 0;
}

static void virtio_pci_notify_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    VirtIODevice *vdev = opaque;
    VirtIOPCIProxy *proxy = VIRTIO_PCI(DEVICE(vdev)->parent_bus->parent);
    unsigned queue = addr / virtio_pci_queue_mem_mult(proxy);

    if (queue < VIRTIO_QUEUE_MAX) {
        virtio_queue_notify(vdev, queue);
    }
}

static void virtio_pci_notify_write_pio(void *opaque, hwaddr addr,
                                        uint64_t val, unsigned size)
{
    VirtIODevice *vdev = opaque;
    unsigned queue = val;

    if (queue < VIRTIO_QUEUE_MAX) {
        virtio_queue_notify(vdev, queue);
    }
}

static uint64_t virtio_pci_isr_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    uint64_t val = atomic_xchg(&vdev->isr, 0);
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
    VirtIODevice *vdev = opaque;
    uint64_t val = 0;

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
    }
    return val;
}

static void virtio_pci_device_write(void *opaque, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    VirtIODevice *vdev = opaque;
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

static void virtio_pci_modern_regions_init(VirtIOPCIProxy *proxy)
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


    memory_region_init_io(&proxy->common.mr, OBJECT(proxy),
                          &common_ops,
                          proxy,
                          "virtio-pci-common",
                          proxy->common.size);

    memory_region_init_io(&proxy->isr.mr, OBJECT(proxy),
                          &isr_ops,
                          proxy,
                          "virtio-pci-isr",
                          proxy->isr.size);

    memory_region_init_io(&proxy->device.mr, OBJECT(proxy),
                          &device_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-device",
                          proxy->device.size);

    memory_region_init_io(&proxy->notify.mr, OBJECT(proxy),
                          &notify_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-notify",
                          proxy->notify.size);

    memory_region_init_io(&proxy->notify_pio.mr, OBJECT(proxy),
                          &notify_pio_ops,
                          virtio_bus_get_device(&proxy->bus),
                          "virtio-pci-notify-pio",
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
        if (virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM)) {
            error_setg(errp, "VIRTIO_F_IOMMU_PLATFORM was supported by"
                       "neither legacy nor transitional device.");
            return ;
        }
        /* legacy and transitional */
        pci_set_word(config + PCI_SUBSYSTEM_VENDOR_ID,
                     pci_get_word(config + PCI_VENDOR_ID));
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

        virtio_pci_modern_regions_init(proxy);

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
                error_report("unable to init msix vectors to %" PRIu32,
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
    bool pcie_port = pci_bus_is_express(pci_dev->bus) &&
                     !pci_bus_is_root(pci_dev->bus);

    if (kvm_enabled() && !kvm_has_many_ioeventfds()) {
        proxy->flags &= ~VIRTIO_PCI_FLAG_USE_IOEVENTFD;
    }

    /*
     * virtio pci bar layout used by default.
     * subclasses can re-arrange things if needed.
     *
     *   region 0   --  virtio legacy io bar
     *   region 1   --  msi-x bar
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

    memory_region_init_alias(&proxy->modern_cfg,
                             OBJECT(proxy),
                             "virtio-pci-cfg",
                             &proxy->modern_bar,
                             0,
                             memory_region_size(&proxy->modern_bar));

    address_space_init(&proxy->modern_as, &proxy->modern_cfg, "virtio-pci-cfg-as");

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
            pcie_ats_init(pci_dev, 256);
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

    msix_uninit_exclusive_bar(pci_dev);
    address_space_destroy(&proxy->modern_as);
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
    DEFINE_PROP_ON_OFF_AUTO("disable-legacy", VirtIOPCIProxy, disable_legacy,
                            ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BOOL("disable-modern", VirtIOPCIProxy, disable_modern, false),
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
    DEFINE_PROP_BIT("x-pcie-deverr-init", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_INIT_DEVERR_BIT, true),
    DEFINE_PROP_BIT("x-pcie-lnkctl-init", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_INIT_LNKCTL_BIT, true),
    DEFINE_PROP_BIT("x-pcie-pm-init", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_INIT_PM_BIT, true),
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

    dc->props = virtio_pci_properties;
    k->realize = virtio_pci_realize;
    k->exit = virtio_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_OTHERS;
    vpciklass->parent_dc_realize = dc->realize;
    dc->realize = virtio_pci_dc_realize;
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
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_blk_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOBlkPCI *dev = VIRTIO_BLK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_blk_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = virtio_blk_pci_properties;
    k->realize = virtio_blk_pci_realize;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_BLOCK;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void virtio_blk_pci_instance_init(Object *obj)
{
    VirtIOBlkPCI *dev = VIRTIO_BLK_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_BLK);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
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
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_scsi_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
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
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = virtio_scsi_pci_realize;
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

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SCSI);
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
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_scsi_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostSCSIPCI *dev = VHOST_SCSI_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = vs->conf.num_queues + 3;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void vhost_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_scsi_pci_realize;
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

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_SCSI);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
}

static const TypeInfo vhost_scsi_pci_info = {
    .name          = TYPE_VHOST_SCSI_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VHostSCSIPCI),
    .instance_init = vhost_scsi_pci_instance_init,
    .class_init    = vhost_scsi_pci_class_init,
};
#endif

#if defined(CONFIG_VHOST_USER) && defined(CONFIG_LINUX)
/* vhost-user-scsi-pci */
static Property vhost_user_scsi_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors,
                       DEV_NVECTORS_UNSPECIFIED),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_user_scsi_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostUserSCSIPCI *dev = VHOST_USER_SCSI_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIOSCSICommon *vs = VIRTIO_SCSI_COMMON(vdev);

    if (vpci_dev->nvectors == DEV_NVECTORS_UNSPECIFIED) {
        vpci_dev->nvectors = vs->conf.num_queues + 3;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void vhost_user_scsi_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_user_scsi_pci_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->props = vhost_user_scsi_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_SCSI;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_STORAGE_SCSI;
}

static void vhost_user_scsi_pci_instance_init(Object *obj)
{
    VHostUserSCSIPCI *dev = VHOST_USER_SCSI_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_USER_SCSI);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
}

static const TypeInfo vhost_user_scsi_pci_info = {
    .name          = TYPE_VHOST_USER_SCSI_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VHostUserSCSIPCI),
    .instance_init = vhost_user_scsi_pci_instance_init,
    .class_init    = vhost_user_scsi_pci_class_init,
};
#endif

/* vhost-vsock-pci */

#ifdef CONFIG_VHOST_VSOCK
static Property vhost_vsock_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_PROP_END_OF_LIST(),
};

static void vhost_vsock_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VHostVSockPCI *dev = VHOST_VSOCK_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void vhost_vsock_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = vhost_vsock_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = vhost_vsock_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_VSOCK;
    pcidev_k->revision = 0x00;
    pcidev_k->class_id = PCI_CLASS_COMMUNICATION_OTHER;
}

static void vhost_vsock_pci_instance_init(Object *obj)
{
    VHostVSockPCI *dev = VHOST_VSOCK_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VHOST_VSOCK);
}

static const TypeInfo vhost_vsock_pci_info = {
    .name          = TYPE_VHOST_VSOCK_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VHostVSockPCI),
    .instance_init = vhost_vsock_pci_instance_init,
    .class_init    = vhost_vsock_pci_class_init,
};
#endif

/* virtio-balloon-pci */

static Property virtio_balloon_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_balloon_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOBalloonPCI *dev = VIRTIO_BALLOON_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->class_code != PCI_CLASS_OTHERS &&
        vpci_dev->class_code != PCI_CLASS_MEMORY_RAM) { /* qemu < 1.1 */
        vpci_dev->class_code = PCI_CLASS_OTHERS;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_balloon_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = virtio_balloon_pci_realize;
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

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_BALLOON);
    object_property_add_alias(obj, "guest-stats", OBJECT(&dev->vdev),
                                  "guest-stats", &error_abort);
    object_property_add_alias(obj, "guest-stats-polling-interval",
                              OBJECT(&dev->vdev),
                              "guest-stats-polling-interval", &error_abort);
}

static const TypeInfo virtio_balloon_pci_info = {
    .name          = TYPE_VIRTIO_BALLOON_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOBalloonPCI),
    .instance_init = virtio_balloon_pci_instance_init,
    .class_init    = virtio_balloon_pci_class_init,
};

/* virtio-serial-pci */

static void virtio_serial_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
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
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static Property virtio_serial_pci_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_serial_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);
    k->realize = virtio_serial_pci_realize;
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

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_SERIAL);
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
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_net_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    DeviceState *qdev = DEVICE(vpci_dev);
    VirtIONetPCI *dev = VIRTIO_NET_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    virtio_net_set_netclient_name(&dev->vdev, qdev->id,
                                  object_get_typename(OBJECT(qdev)));
    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
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
    vpciklass->realize = virtio_net_pci_realize;
}

static void virtio_net_pci_instance_init(Object *obj)
{
    VirtIONetPCI *dev = VIRTIO_NET_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_NET);
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
}

static const TypeInfo virtio_net_pci_info = {
    .name          = TYPE_VIRTIO_NET_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIONetPCI),
    .instance_init = virtio_net_pci_instance_init,
    .class_init    = virtio_net_pci_class_init,
};

/* virtio-rng-pci */

static void virtio_rng_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIORngPCI *vrng = VIRTIO_RNG_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vrng->vdev);
    Error *err = NULL;

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    object_property_set_link(OBJECT(vrng),
                             OBJECT(vrng->vdev.conf.rng), "rng",
                             NULL);
}

static void virtio_rng_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    k->realize = virtio_rng_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_RNG;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_rng_initfn(Object *obj)
{
    VirtIORngPCI *dev = VIRTIO_RNG_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_RNG);
}

static const TypeInfo virtio_rng_pci_info = {
    .name          = TYPE_VIRTIO_RNG_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIORngPCI),
    .instance_init = virtio_rng_initfn,
    .class_init    = virtio_rng_pci_class_init,
};

/* virtio-input-pci */

static Property virtio_input_pci_properties[] = {
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_input_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOInputPCI *vinput = VIRTIO_INPUT_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&vinput->vdev);

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    virtio_pci_force_virtio_1(vpci_dev);
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);
}

static void virtio_input_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    dc->props = virtio_input_pci_properties;
    k->realize = virtio_input_pci_realize;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    pcidev_k->class_id = PCI_CLASS_INPUT_OTHER;
}

static void virtio_input_hid_kbd_pci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    pcidev_k->class_id = PCI_CLASS_INPUT_KEYBOARD;
}

static void virtio_input_hid_mouse_pci_class_init(ObjectClass *klass,
                                                  void *data)
{
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    pcidev_k->class_id = PCI_CLASS_INPUT_MOUSE;
}

static void virtio_keyboard_initfn(Object *obj)
{
    VirtIOInputHIDPCI *dev = VIRTIO_INPUT_HID_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_KEYBOARD);
}

static void virtio_mouse_initfn(Object *obj)
{
    VirtIOInputHIDPCI *dev = VIRTIO_INPUT_HID_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_MOUSE);
}

static void virtio_tablet_initfn(Object *obj)
{
    VirtIOInputHIDPCI *dev = VIRTIO_INPUT_HID_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_TABLET);
}

static const TypeInfo virtio_input_pci_info = {
    .name          = TYPE_VIRTIO_INPUT_PCI,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(VirtIOInputPCI),
    .class_init    = virtio_input_pci_class_init,
    .abstract      = true,
};

static const TypeInfo virtio_input_hid_pci_info = {
    .name          = TYPE_VIRTIO_INPUT_HID_PCI,
    .parent        = TYPE_VIRTIO_INPUT_PCI,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .abstract      = true,
};

static const TypeInfo virtio_keyboard_pci_info = {
    .name          = TYPE_VIRTIO_KEYBOARD_PCI,
    .parent        = TYPE_VIRTIO_INPUT_HID_PCI,
    .class_init    = virtio_input_hid_kbd_pci_class_init,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .instance_init = virtio_keyboard_initfn,
};

static const TypeInfo virtio_mouse_pci_info = {
    .name          = TYPE_VIRTIO_MOUSE_PCI,
    .parent        = TYPE_VIRTIO_INPUT_HID_PCI,
    .class_init    = virtio_input_hid_mouse_pci_class_init,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .instance_init = virtio_mouse_initfn,
};

static const TypeInfo virtio_tablet_pci_info = {
    .name          = TYPE_VIRTIO_TABLET_PCI,
    .parent        = TYPE_VIRTIO_INPUT_HID_PCI,
    .instance_size = sizeof(VirtIOInputHIDPCI),
    .instance_init = virtio_tablet_initfn,
};

#ifdef CONFIG_LINUX
static void virtio_host_initfn(Object *obj)
{
    VirtIOInputHostPCI *dev = VIRTIO_INPUT_HOST_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_INPUT_HOST);
}

static const TypeInfo virtio_host_pci_info = {
    .name          = TYPE_VIRTIO_INPUT_HOST_PCI,
    .parent        = TYPE_VIRTIO_INPUT_PCI,
    .instance_size = sizeof(VirtIOInputHostPCI),
    .instance_init = virtio_host_initfn,
};
#endif

/* virtio-pci-bus */

static void virtio_pci_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtIOPCIProxy *dev)
{
    DeviceState *qdev = DEVICE(dev);
    char virtio_bus_name[] = "virtio-bus";

    qbus_create_inplace(bus, bus_size, TYPE_VIRTIO_PCI_BUS, qdev,
                        virtio_bus_name);
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
    k->vmstate_change = virtio_pci_vmstate_change;
    k->pre_plugged = virtio_pci_pre_plugged;
    k->device_plugged = virtio_pci_device_plugged;
    k->device_unplugged = virtio_pci_device_unplugged;
    k->query_nvectors = virtio_pci_query_nvectors;
    k->ioeventfd_enabled = virtio_pci_ioeventfd_enabled;
    k->ioeventfd_assign = virtio_pci_ioeventfd_assign;
    k->get_dma_as = virtio_pci_get_dma_as;
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
    type_register_static(&virtio_input_pci_info);
    type_register_static(&virtio_input_hid_pci_info);
    type_register_static(&virtio_keyboard_pci_info);
    type_register_static(&virtio_mouse_pci_info);
    type_register_static(&virtio_tablet_pci_info);
#ifdef CONFIG_LINUX
    type_register_static(&virtio_host_pci_info);
#endif
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
#if defined(CONFIG_VHOST_USER) && defined(CONFIG_LINUX)
    type_register_static(&vhost_user_scsi_pci_info);
#endif
#ifdef CONFIG_VHOST_VSOCK
    type_register_static(&vhost_vsock_pci_info);
#endif
}

type_init(virtio_pci_register_types)
