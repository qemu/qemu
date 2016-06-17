/*
 * Copyright (c) 2007, Neocleus Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 *
 *  Assign a PCI device from the host to a guest VM.
 *
 *  This implementation uses the classic device assignment interface of KVM
 *  and is only available on x86 hosts. It is expected to be obsoleted by VFIO
 *  based device assignment.
 *
 *  Adapted for KVM (qemu-kvm) by Qumranet. QEMU version was based on qemu-kvm
 *  revision 4144fe9d48. See its repository for the history.
 *
 *  Copyright (c) 2007, Neocleus, Alex Novik (alex@neocleus.com)
 *  Copyright (c) 2007, Neocleus, Guy Zana (guy@neocleus.com)
 *  Copyright (C) 2008, Qumranet, Amit Shah (amit.shah@qumranet.com)
 *  Copyright (C) 2008, Red Hat, Amit Shah (amit.shah@redhat.com)
 *  Copyright (C) 2008, IBM, Muli Ben-Yehuda (muli@il.ibm.com)
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "qemu/error-report.h"
#include "ui/console.h"
#include "hw/loader.h"
#include "monitor/monitor.h"
#include "qemu/range.h"
#include "sysemu/sysemu.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "linux/kvm.h"
#include "kvm_i386.h"
#include "hw/pci/pci-assign.h"

#define MSIX_PAGE_SIZE 0x1000

/* From linux/ioport.h */
#define IORESOURCE_IO       0x00000100  /* Resource type */
#define IORESOURCE_MEM      0x00000200
#define IORESOURCE_IRQ      0x00000400
#define IORESOURCE_DMA      0x00000800
#define IORESOURCE_PREFETCH 0x00002000  /* No side effects */
#define IORESOURCE_MEM_64   0x00100000

typedef struct PCIRegion {
    int type;           /* Memory or port I/O */
    int valid;
    uint64_t base_addr;
    uint64_t size;    /* size of the region */
    int resource_fd;
} PCIRegion;

typedef struct PCIDevRegions {
    uint8_t bus, dev, func; /* Bus inside domain, device and function */
    int irq;                /* IRQ number */
    uint16_t region_number; /* number of active regions */

    /* Port I/O or MMIO Regions */
    PCIRegion regions[PCI_NUM_REGIONS - 1];
    int config_fd;
} PCIDevRegions;

typedef struct AssignedDevRegion {
    MemoryRegion container;
    MemoryRegion real_iomem;
    union {
        uint8_t *r_virtbase; /* mmapped access address for memory regions */
        uint32_t r_baseport; /* the base guest port for I/O regions */
    } u;
    pcibus_t e_size;    /* emulated size of region in bytes */
    pcibus_t r_size;    /* real size of region in bytes */
    PCIRegion *region;
} AssignedDevRegion;

#define ASSIGNED_DEVICE_PREFER_MSI_BIT  0
#define ASSIGNED_DEVICE_SHARE_INTX_BIT  1

#define ASSIGNED_DEVICE_PREFER_MSI_MASK (1 << ASSIGNED_DEVICE_PREFER_MSI_BIT)
#define ASSIGNED_DEVICE_SHARE_INTX_MASK (1 << ASSIGNED_DEVICE_SHARE_INTX_BIT)

typedef struct MSIXTableEntry {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t data;
    uint32_t ctrl;
} MSIXTableEntry;

typedef enum AssignedIRQType {
    ASSIGNED_IRQ_NONE = 0,
    ASSIGNED_IRQ_INTX_HOST_INTX,
    ASSIGNED_IRQ_INTX_HOST_MSI,
    ASSIGNED_IRQ_MSI,
    ASSIGNED_IRQ_MSIX
} AssignedIRQType;

typedef struct AssignedDevice {
    PCIDevice dev;
    PCIHostDeviceAddress host;
    uint32_t dev_id;
    uint32_t features;
    int intpin;
    AssignedDevRegion v_addrs[PCI_NUM_REGIONS - 1];
    PCIDevRegions real_device;
    PCIINTxRoute intx_route;
    AssignedIRQType assigned_irq_type;
    struct {
#define ASSIGNED_DEVICE_CAP_MSI (1 << 0)
#define ASSIGNED_DEVICE_CAP_MSIX (1 << 1)
        uint32_t available;
#define ASSIGNED_DEVICE_MSI_ENABLED (1 << 0)
#define ASSIGNED_DEVICE_MSIX_ENABLED (1 << 1)
#define ASSIGNED_DEVICE_MSIX_MASKED (1 << 2)
        uint32_t state;
    } cap;
    uint8_t emulate_config_read[PCI_CONFIG_SPACE_SIZE];
    uint8_t emulate_config_write[PCI_CONFIG_SPACE_SIZE];
    int msi_virq_nr;
    int *msi_virq;
    MSIXTableEntry *msix_table;
    hwaddr msix_table_addr;
    uint16_t msix_max;
    MemoryRegion mmio;
    char *configfd_name;
    int32_t bootindex;
} AssignedDevice;

#define TYPE_PCI_ASSIGN "kvm-pci-assign"
#define PCI_ASSIGN(obj) OBJECT_CHECK(AssignedDevice, (obj), TYPE_PCI_ASSIGN)

static void assigned_dev_update_irq_routing(PCIDevice *dev);

static void assigned_dev_load_option_rom(AssignedDevice *dev);

static void assigned_dev_unregister_msix_mmio(AssignedDevice *dev);

static uint64_t assigned_dev_ioport_rw(AssignedDevRegion *dev_region,
                                       hwaddr addr, int size,
                                       uint64_t *data)
{
    uint64_t val = 0;
    int fd = dev_region->region->resource_fd;

    if (data) {
        DEBUG("pwrite data=%" PRIx64 ", size=%d, e_phys=" TARGET_FMT_plx
              ", addr="TARGET_FMT_plx"\n", *data, size, addr, addr);
        if (pwrite(fd, data, size, addr) != size) {
            error_report("%s - pwrite failed %s", __func__, strerror(errno));
        }
    } else {
        if (pread(fd, &val, size, addr) != size) {
            error_report("%s - pread failed %s", __func__, strerror(errno));
            val = (1UL << (size * 8)) - 1;
        }
        DEBUG("pread val=%" PRIx64 ", size=%d, e_phys=" TARGET_FMT_plx
              ", addr=" TARGET_FMT_plx "\n", val, size, addr, addr);
    }
    return val;
}

static void assigned_dev_ioport_write(void *opaque, hwaddr addr,
                                      uint64_t data, unsigned size)
{
    assigned_dev_ioport_rw(opaque, addr, size, &data);
}

static uint64_t assigned_dev_ioport_read(void *opaque,
                                         hwaddr addr, unsigned size)
{
    return assigned_dev_ioport_rw(opaque, addr, size, NULL);
}

static uint32_t slow_bar_readb(void *opaque, hwaddr addr)
{
    AssignedDevRegion *d = opaque;
    uint8_t *in = d->u.r_virtbase + addr;
    uint32_t r;

    r = *in;
    DEBUG("addr=0x" TARGET_FMT_plx " val=0x%08x\n", addr, r);

    return r;
}

static uint32_t slow_bar_readw(void *opaque, hwaddr addr)
{
    AssignedDevRegion *d = opaque;
    uint16_t *in = (uint16_t *)(d->u.r_virtbase + addr);
    uint32_t r;

    r = *in;
    DEBUG("addr=0x" TARGET_FMT_plx " val=0x%08x\n", addr, r);

    return r;
}

static uint32_t slow_bar_readl(void *opaque, hwaddr addr)
{
    AssignedDevRegion *d = opaque;
    uint32_t *in = (uint32_t *)(d->u.r_virtbase + addr);
    uint32_t r;

    r = *in;
    DEBUG("addr=0x" TARGET_FMT_plx " val=0x%08x\n", addr, r);

    return r;
}

static void slow_bar_writeb(void *opaque, hwaddr addr, uint32_t val)
{
    AssignedDevRegion *d = opaque;
    uint8_t *out = d->u.r_virtbase + addr;

    DEBUG("addr=0x" TARGET_FMT_plx " val=0x%02x\n", addr, val);
    *out = val;
}

static void slow_bar_writew(void *opaque, hwaddr addr, uint32_t val)
{
    AssignedDevRegion *d = opaque;
    uint16_t *out = (uint16_t *)(d->u.r_virtbase + addr);

    DEBUG("addr=0x" TARGET_FMT_plx " val=0x%04x\n", addr, val);
    *out = val;
}

static void slow_bar_writel(void *opaque, hwaddr addr, uint32_t val)
{
    AssignedDevRegion *d = opaque;
    uint32_t *out = (uint32_t *)(d->u.r_virtbase + addr);

    DEBUG("addr=0x" TARGET_FMT_plx " val=0x%08x\n", addr, val);
    *out = val;
}

static const MemoryRegionOps slow_bar_ops = {
    .old_mmio = {
        .read = { slow_bar_readb, slow_bar_readw, slow_bar_readl, },
        .write = { slow_bar_writeb, slow_bar_writew, slow_bar_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void assigned_dev_iomem_setup(PCIDevice *pci_dev, int region_num,
                                     pcibus_t e_size)
{
    AssignedDevice *r_dev = PCI_ASSIGN(pci_dev);
    AssignedDevRegion *region = &r_dev->v_addrs[region_num];
    PCIRegion *real_region = &r_dev->real_device.regions[region_num];

    if (e_size > 0) {
        memory_region_init(&region->container, OBJECT(pci_dev),
                           "assigned-dev-container", e_size);
        memory_region_add_subregion(&region->container, 0, &region->real_iomem);

        /* deal with MSI-X MMIO page */
        if (real_region->base_addr <= r_dev->msix_table_addr &&
                real_region->base_addr + real_region->size >
                r_dev->msix_table_addr) {
            uint64_t offset = r_dev->msix_table_addr - real_region->base_addr;

            memory_region_add_subregion_overlap(&region->container,
                                                offset,
                                                &r_dev->mmio,
                                                1);
        }
    }
}

static const MemoryRegionOps assigned_dev_ioport_ops = {
    .read = assigned_dev_ioport_read,
    .write = assigned_dev_ioport_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void assigned_dev_ioport_setup(PCIDevice *pci_dev, int region_num,
                                      pcibus_t size)
{
    AssignedDevice *r_dev = PCI_ASSIGN(pci_dev);
    AssignedDevRegion *region = &r_dev->v_addrs[region_num];

    region->e_size = size;
    memory_region_init(&region->container, OBJECT(pci_dev),
                       "assigned-dev-container", size);
    memory_region_init_io(&region->real_iomem, OBJECT(pci_dev),
                          &assigned_dev_ioport_ops, r_dev->v_addrs + region_num,
                          "assigned-dev-iomem", size);
    memory_region_add_subregion(&region->container, 0, &region->real_iomem);
}

static uint32_t assigned_dev_pci_read(PCIDevice *d, int pos, int len)
{
    AssignedDevice *pci_dev = PCI_ASSIGN(d);
    uint32_t val;
    ssize_t ret;
    int fd = pci_dev->real_device.config_fd;

again:
    ret = pread(fd, &val, len, pos);
    if (ret != len) {
        if ((ret < 0) && (errno == EINTR || errno == EAGAIN)) {
            goto again;
        }

        hw_error("pci read failed, ret = %zd errno = %d\n", ret, errno);
    }

    return val;
}

static uint8_t assigned_dev_pci_read_byte(PCIDevice *d, int pos)
{
    return (uint8_t)assigned_dev_pci_read(d, pos, 1);
}

static void assigned_dev_pci_write(PCIDevice *d, int pos, uint32_t val, int len)
{
    AssignedDevice *pci_dev = PCI_ASSIGN(d);
    ssize_t ret;
    int fd = pci_dev->real_device.config_fd;

again:
    ret = pwrite(fd, &val, len, pos);
    if (ret != len) {
        if ((ret < 0) && (errno == EINTR || errno == EAGAIN)) {
            goto again;
        }

        hw_error("pci write failed, ret = %zd errno = %d\n", ret, errno);
    }
}

static void assigned_dev_emulate_config_read(AssignedDevice *dev,
                                             uint32_t offset, uint32_t len)
{
    memset(dev->emulate_config_read + offset, 0xff, len);
}

static void assigned_dev_direct_config_read(AssignedDevice *dev,
                                            uint32_t offset, uint32_t len)
{
    memset(dev->emulate_config_read + offset, 0, len);
}

static void assigned_dev_direct_config_write(AssignedDevice *dev,
                                             uint32_t offset, uint32_t len)
{
    memset(dev->emulate_config_write + offset, 0, len);
}

static uint8_t pci_find_cap_offset(PCIDevice *d, uint8_t cap, uint8_t start)
{
    int id;
    int max_cap = 48;
    int pos = start ? start : PCI_CAPABILITY_LIST;
    int status;

    status = assigned_dev_pci_read_byte(d, PCI_STATUS);
    if ((status & PCI_STATUS_CAP_LIST) == 0) {
        return 0;
    }

    while (max_cap--) {
        pos = assigned_dev_pci_read_byte(d, pos);
        if (pos < 0x40) {
            break;
        }

        pos &= ~3;
        id = assigned_dev_pci_read_byte(d, pos + PCI_CAP_LIST_ID);

        if (id == 0xff) {
            break;
        }
        if (id == cap) {
            return pos;
        }

        pos += PCI_CAP_LIST_NEXT;
    }
    return 0;
}

static void assigned_dev_register_regions(PCIRegion *io_regions,
                                          unsigned long regions_num,
                                          AssignedDevice *pci_dev,
                                          Error **errp)
{
    uint32_t i;
    PCIRegion *cur_region = io_regions;

    for (i = 0; i < regions_num; i++, cur_region++) {
        if (!cur_region->valid) {
            continue;
        }

        /* handle memory io regions */
        if (cur_region->type & IORESOURCE_MEM) {
            int t = PCI_BASE_ADDRESS_SPACE_MEMORY;
            if (cur_region->type & IORESOURCE_PREFETCH) {
                t |= PCI_BASE_ADDRESS_MEM_PREFETCH;
            }
            if (cur_region->type & IORESOURCE_MEM_64) {
                t |= PCI_BASE_ADDRESS_MEM_TYPE_64;
            }

            /* map physical memory */
            pci_dev->v_addrs[i].u.r_virtbase = mmap(NULL, cur_region->size,
                                                    PROT_WRITE | PROT_READ,
                                                    MAP_SHARED,
                                                    cur_region->resource_fd,
                                                    (off_t)0);

            if (pci_dev->v_addrs[i].u.r_virtbase == MAP_FAILED) {
                pci_dev->v_addrs[i].u.r_virtbase = NULL;
                error_setg_errno(errp, errno, "Couldn't mmap 0x%" PRIx64 "!",
                                 cur_region->base_addr);
                return;
            }

            pci_dev->v_addrs[i].r_size = cur_region->size;
            pci_dev->v_addrs[i].e_size = 0;

            /* add offset */
            pci_dev->v_addrs[i].u.r_virtbase +=
                (cur_region->base_addr & 0xFFF);

            if (cur_region->size & 0xFFF) {
                error_report("PCI region %d at address 0x%" PRIx64 " has "
                             "size 0x%" PRIx64 ", which is not a multiple of "
                             "4K.  You might experience some performance hit "
                             "due to that.",
                             i, cur_region->base_addr, cur_region->size);
                memory_region_init_io(&pci_dev->v_addrs[i].real_iomem,
                                      OBJECT(pci_dev), &slow_bar_ops,
                                      &pci_dev->v_addrs[i],
                                      "assigned-dev-slow-bar",
                                      cur_region->size);
            } else {
                void *virtbase = pci_dev->v_addrs[i].u.r_virtbase;
                char name[32];
                snprintf(name, sizeof(name), "%s.bar%d",
                         object_get_typename(OBJECT(pci_dev)), i);
                memory_region_init_ram_ptr(&pci_dev->v_addrs[i].real_iomem,
                                           OBJECT(pci_dev), name,
                                           cur_region->size, virtbase);
                vmstate_register_ram(&pci_dev->v_addrs[i].real_iomem,
                                     &pci_dev->dev.qdev);
            }

            assigned_dev_iomem_setup(&pci_dev->dev, i, cur_region->size);
            pci_register_bar((PCIDevice *) pci_dev, i, t,
                             &pci_dev->v_addrs[i].container);
            continue;
        } else {
            /* handle port io regions */
            uint32_t val;
            int ret;

            /* Test kernel support for ioport resource read/write.  Old
             * kernels return EIO.  New kernels only allow 1/2/4 byte reads
             * so should return EINVAL for a 3 byte read */
            ret = pread(pci_dev->v_addrs[i].region->resource_fd, &val, 3, 0);
            if (ret >= 0) {
                error_report("Unexpected return from I/O port read: %d", ret);
                abort();
            } else if (errno != EINVAL) {
                error_report("Kernel doesn't support ioport resource "
                             "access, hiding this region.");
                close(pci_dev->v_addrs[i].region->resource_fd);
                cur_region->valid = 0;
                continue;
            }

            pci_dev->v_addrs[i].u.r_baseport = cur_region->base_addr;
            pci_dev->v_addrs[i].r_size = cur_region->size;
            pci_dev->v_addrs[i].e_size = 0;

            assigned_dev_ioport_setup(&pci_dev->dev, i, cur_region->size);
            pci_register_bar((PCIDevice *) pci_dev, i,
                             PCI_BASE_ADDRESS_SPACE_IO,
                             &pci_dev->v_addrs[i].container);
        }
    }

    /* success */
}

static void get_real_id(const char *devpath, const char *idname, uint16_t *val,
                        Error **errp)
{
    FILE *f;
    char name[128];
    long id;

    snprintf(name, sizeof(name), "%s%s", devpath, idname);
    f = fopen(name, "r");
    if (f == NULL) {
        error_setg_file_open(errp, errno, name);
        return;
    }
    if (fscanf(f, "%li\n", &id) == 1) {
        *val = id;
    } else {
        error_setg(errp, "Failed to parse contents of '%s'", name);
    }
    fclose(f);
}

static void get_real_vendor_id(const char *devpath, uint16_t *val,
                               Error **errp)
{
    get_real_id(devpath, "vendor", val, errp);
}

static void get_real_device_id(const char *devpath, uint16_t *val,
                               Error **errp)
{
    get_real_id(devpath, "device", val, errp);
}

static void get_real_device(AssignedDevice *pci_dev, Error **errp)
{
    char dir[128], name[128];
    int fd, r = 0;
    FILE *f;
    uint64_t start, end, size, flags;
    uint16_t id;
    PCIRegion *rp;
    PCIDevRegions *dev = &pci_dev->real_device;
    Error *local_err = NULL;

    dev->region_number = 0;

    snprintf(dir, sizeof(dir), "/sys/bus/pci/devices/%04x:%02x:%02x.%x/",
             pci_dev->host.domain, pci_dev->host.bus,
             pci_dev->host.slot, pci_dev->host.function);

    snprintf(name, sizeof(name), "%sconfig", dir);

    if (pci_dev->configfd_name && *pci_dev->configfd_name) {
        dev->config_fd = monitor_fd_param(cur_mon, pci_dev->configfd_name,
                                          &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    } else {
        dev->config_fd = open(name, O_RDWR);

        if (dev->config_fd == -1) {
            error_setg_file_open(errp, errno, name);
            return;
        }
    }
again:
    r = read(dev->config_fd, pci_dev->dev.config,
             pci_config_size(&pci_dev->dev));
    if (r < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            goto again;
        }
        error_setg_errno(errp, errno, "read(\"%s\")",
                         (pci_dev->configfd_name && *pci_dev->configfd_name) ?
                         pci_dev->configfd_name : name);
        return;
    }

    /* Restore or clear multifunction, this is always controlled by qemu */
    if (pci_dev->dev.cap_present & QEMU_PCI_CAP_MULTIFUNCTION) {
        pci_dev->dev.config[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;
    } else {
        pci_dev->dev.config[PCI_HEADER_TYPE] &= ~PCI_HEADER_TYPE_MULTI_FUNCTION;
    }

    /* Clear host resource mapping info.  If we choose not to register a
     * BAR, such as might be the case with the option ROM, we can get
     * confusing, unwritable, residual addresses from the host here. */
    memset(&pci_dev->dev.config[PCI_BASE_ADDRESS_0], 0, 24);
    memset(&pci_dev->dev.config[PCI_ROM_ADDRESS], 0, 4);

    snprintf(name, sizeof(name), "%sresource", dir);

    f = fopen(name, "r");
    if (f == NULL) {
        error_setg_file_open(errp, errno, name);
        return;
    }

    for (r = 0; r < PCI_ROM_SLOT; r++) {
        if (fscanf(f, "%" SCNi64 " %" SCNi64 " %" SCNi64 "\n",
                   &start, &end, &flags) != 3) {
            break;
        }

        rp = dev->regions + r;
        rp->valid = 0;
        rp->resource_fd = -1;
        size = end - start + 1;
        flags &= IORESOURCE_IO | IORESOURCE_MEM | IORESOURCE_PREFETCH
                 | IORESOURCE_MEM_64;
        if (size == 0 || (flags & ~IORESOURCE_PREFETCH) == 0) {
            continue;
        }
        if (flags & IORESOURCE_MEM) {
            flags &= ~IORESOURCE_IO;
        } else {
            flags &= ~IORESOURCE_PREFETCH;
        }
        snprintf(name, sizeof(name), "%sresource%d", dir, r);
        fd = open(name, O_RDWR);
        if (fd == -1) {
            continue;
        }
        rp->resource_fd = fd;

        rp->type = flags;
        rp->valid = 1;
        rp->base_addr = start;
        rp->size = size;
        pci_dev->v_addrs[r].region = rp;
        DEBUG("region %d size %" PRIu64 " start 0x%" PRIx64
              " type %d resource_fd %d\n",
              r, rp->size, start, rp->type, rp->resource_fd);
    }

    fclose(f);

    /* read and fill vendor ID */
    get_real_vendor_id(dir, &id, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pci_dev->dev.config[0] = id & 0xff;
    pci_dev->dev.config[1] = (id & 0xff00) >> 8;

    /* read and fill device ID */
    get_real_device_id(dir, &id, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pci_dev->dev.config[2] = id & 0xff;
    pci_dev->dev.config[3] = (id & 0xff00) >> 8;

    pci_word_test_and_clear_mask(pci_dev->emulate_config_write + PCI_COMMAND,
                                 PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE);

    dev->region_number = r;
}

static void free_msi_virqs(AssignedDevice *dev)
{
    int i;

    for (i = 0; i < dev->msi_virq_nr; i++) {
        if (dev->msi_virq[i] >= 0) {
            kvm_irqchip_release_virq(kvm_state, dev->msi_virq[i]);
            dev->msi_virq[i] = -1;
        }
    }
    g_free(dev->msi_virq);
    dev->msi_virq = NULL;
    dev->msi_virq_nr = 0;
}

static void free_assigned_device(AssignedDevice *dev)
{
    int i;

    if (dev->cap.available & ASSIGNED_DEVICE_CAP_MSIX) {
        assigned_dev_unregister_msix_mmio(dev);
    }
    for (i = 0; i < dev->real_device.region_number; i++) {
        PCIRegion *pci_region = &dev->real_device.regions[i];
        AssignedDevRegion *region = &dev->v_addrs[i];

        if (!pci_region->valid) {
            continue;
        }
        if (pci_region->type & IORESOURCE_IO) {
            if (region->u.r_baseport) {
                memory_region_del_subregion(&region->container,
                                            &region->real_iomem);
            }
        } else if (pci_region->type & IORESOURCE_MEM) {
            if (region->u.r_virtbase) {
                memory_region_del_subregion(&region->container,
                                            &region->real_iomem);

                /* Remove MSI-X table subregion */
                if (pci_region->base_addr <= dev->msix_table_addr &&
                    pci_region->base_addr + pci_region->size >
                    dev->msix_table_addr) {
                    memory_region_del_subregion(&region->container,
                                                &dev->mmio);
                }
                if (munmap(region->u.r_virtbase,
                           (pci_region->size + 0xFFF) & 0xFFFFF000)) {
                    error_report("Failed to unmap assigned device region: %s",
                                 strerror(errno));
                }
            }
        }
        if (pci_region->resource_fd >= 0) {
            close(pci_region->resource_fd);
        }
    }

    if (dev->real_device.config_fd >= 0) {
        close(dev->real_device.config_fd);
    }

    free_msi_virqs(dev);
}

/* This function tries to determine the cause of the PCI assignment failure. It
 * always returns the cause as a dynamically allocated, human readable string.
 * If the function fails to determine the cause for any internal reason, then
 * the returned string will state that fact.
 */
static char *assign_failed_examine(const AssignedDevice *dev)
{
    char name[PATH_MAX], dir[PATH_MAX], driver[PATH_MAX] = {}, *ns;
    uint16_t vendor_id, device_id;
    int r;
    Error *local_err = NULL;

    snprintf(dir, sizeof(dir), "/sys/bus/pci/devices/%04x:%02x:%02x.%01x/",
            dev->host.domain, dev->host.bus, dev->host.slot,
            dev->host.function);

    snprintf(name, sizeof(name), "%sdriver", dir);

    r = readlink(name, driver, sizeof(driver));
    if ((r <= 0) || r >= sizeof(driver)) {
        goto fail;
    }

    driver[r] = 0;
    ns = strrchr(driver, '/');
    if (!ns) {
        goto fail;
    }

    ns++;

    if ((get_real_vendor_id(dir, &vendor_id, &local_err), local_err) ||
        (get_real_device_id(dir, &device_id, &local_err), local_err)) {
        /* We're already analyzing an assignment error, so we suppress this
         * one just like the others above.
         */
        error_free(local_err);
        goto fail;
    }

    return g_strdup_printf(
        "*** The driver '%s' is occupying your device %04x:%02x:%02x.%x.\n"
        "***\n"
        "*** You can try the following commands to free it:\n"
        "***\n"
        "*** $ echo \"%04x %04x\" > /sys/bus/pci/drivers/pci-stub/new_id\n"
        "*** $ echo \"%04x:%02x:%02x.%x\" > /sys/bus/pci/drivers/%s/unbind\n"
        "*** $ echo \"%04x:%02x:%02x.%x\" > /sys/bus/pci/drivers/"
        "pci-stub/bind\n"
        "*** $ echo \"%04x %04x\" > /sys/bus/pci/drivers/pci-stub/remove_id\n"
        "***\n",
        ns, dev->host.domain, dev->host.bus, dev->host.slot,
        dev->host.function, vendor_id, device_id,
        dev->host.domain, dev->host.bus, dev->host.slot, dev->host.function,
        ns, dev->host.domain, dev->host.bus, dev->host.slot,
        dev->host.function, vendor_id, device_id);

fail:
    return g_strdup("Couldn't find out why.\n");
}

static void assign_device(AssignedDevice *dev, Error **errp)
{
    uint32_t flags = KVM_DEV_ASSIGN_ENABLE_IOMMU;
    int r;

    /* Only pass non-zero PCI segment to capable module */
    if (!kvm_check_extension(kvm_state, KVM_CAP_PCI_SEGMENT) &&
        dev->host.domain) {
        error_setg(errp, "Can't assign device inside non-zero PCI segment "
                   "as this KVM module doesn't support it.");
        return;
    }

    if (!kvm_check_extension(kvm_state, KVM_CAP_IOMMU)) {
        error_setg(errp, "No IOMMU found.  Unable to assign device \"%s\"",
                   dev->dev.qdev.id);
        return;
    }

    if (dev->features & ASSIGNED_DEVICE_SHARE_INTX_MASK &&
        kvm_has_intx_set_mask()) {
        flags |= KVM_DEV_ASSIGN_PCI_2_3;
    }

    r = kvm_device_pci_assign(kvm_state, &dev->host, flags, &dev->dev_id);
    if (r < 0) {
        switch (r) {
        case -EBUSY: {
            char *cause;

            cause = assign_failed_examine(dev);
            error_setg_errno(errp, -r, "Failed to assign device \"%s\"",
                             dev->dev.qdev.id);
            error_append_hint(errp, "%s", cause);
            g_free(cause);
            break;
        }
        default:
            error_setg_errno(errp, -r, "Failed to assign device \"%s\"",
                             dev->dev.qdev.id);
            break;
        }
    }
}

static void verify_irqchip_in_kernel(Error **errp)
{
    if (kvm_irqchip_in_kernel()) {
        return;
    }
    error_setg(errp, "pci-assign requires KVM with in-kernel irqchip enabled");
}

static int assign_intx(AssignedDevice *dev, Error **errp)
{
    AssignedIRQType new_type;
    PCIINTxRoute intx_route;
    bool intx_host_msi;
    int r;
    Error *local_err = NULL;

    /* Interrupt PIN 0 means don't use INTx */
    if (assigned_dev_pci_read_byte(&dev->dev, PCI_INTERRUPT_PIN) == 0) {
        pci_device_set_intx_routing_notifier(&dev->dev, NULL);
        return 0;
    }

    verify_irqchip_in_kernel(&local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -ENOTSUP;
    }

    pci_device_set_intx_routing_notifier(&dev->dev,
                                         assigned_dev_update_irq_routing);

    intx_route = pci_device_route_intx_to_irq(&dev->dev, dev->intpin);
    assert(intx_route.mode != PCI_INTX_INVERTED);

    if (!pci_intx_route_changed(&dev->intx_route, &intx_route)) {
        return 0;
    }

    switch (dev->assigned_irq_type) {
    case ASSIGNED_IRQ_INTX_HOST_INTX:
    case ASSIGNED_IRQ_INTX_HOST_MSI:
        intx_host_msi = dev->assigned_irq_type == ASSIGNED_IRQ_INTX_HOST_MSI;
        r = kvm_device_intx_deassign(kvm_state, dev->dev_id, intx_host_msi);
        break;
    case ASSIGNED_IRQ_MSI:
        r = kvm_device_msi_deassign(kvm_state, dev->dev_id);
        break;
    case ASSIGNED_IRQ_MSIX:
        r = kvm_device_msix_deassign(kvm_state, dev->dev_id);
        break;
    default:
        r = 0;
        break;
    }
    if (r) {
        perror("assign_intx: deassignment of previous interrupt failed");
    }
    dev->assigned_irq_type = ASSIGNED_IRQ_NONE;

    if (intx_route.mode == PCI_INTX_DISABLED) {
        dev->intx_route = intx_route;
        return 0;
    }

retry:
    if (dev->features & ASSIGNED_DEVICE_PREFER_MSI_MASK &&
        dev->cap.available & ASSIGNED_DEVICE_CAP_MSI) {
        intx_host_msi = true;
        new_type = ASSIGNED_IRQ_INTX_HOST_MSI;
    } else {
        intx_host_msi = false;
        new_type = ASSIGNED_IRQ_INTX_HOST_INTX;
    }

    r = kvm_device_intx_assign(kvm_state, dev->dev_id, intx_host_msi,
                               intx_route.irq);
    if (r < 0) {
        if (r == -EIO && !(dev->features & ASSIGNED_DEVICE_PREFER_MSI_MASK) &&
            dev->cap.available & ASSIGNED_DEVICE_CAP_MSI) {
            /* Retry with host-side MSI. There might be an IRQ conflict and
             * either the kernel or the device doesn't support sharing. */
            error_report("Host-side INTx sharing not supported, "
                         "using MSI instead");
            error_printf("Some devices do not work properly in this mode.\n");
            dev->features |= ASSIGNED_DEVICE_PREFER_MSI_MASK;
            goto retry;
        }
        error_setg_errno(errp, -r, "Failed to assign irq for \"%s\"",
                         dev->dev.qdev.id);
        error_append_hint(errp, "Perhaps you are assigning a device "
                          "that shares an IRQ with another device?\n");
        return r;
    }

    dev->intx_route = intx_route;
    dev->assigned_irq_type = new_type;
    return r;
}

static void deassign_device(AssignedDevice *dev)
{
    int r;

    r = kvm_device_pci_deassign(kvm_state, dev->dev_id);
    assert(r == 0);
}

/* The pci config space got updated. Check if irq numbers have changed
 * for our devices
 */
static void assigned_dev_update_irq_routing(PCIDevice *dev)
{
    AssignedDevice *assigned_dev = PCI_ASSIGN(dev);
    Error *err = NULL;
    int r;

    r = assign_intx(assigned_dev, &err);
    if (r < 0) {
        error_report_err(err);
        err = NULL;
        qdev_unplug(&dev->qdev, &err);
        assert(!err);
    }
}

static void assigned_dev_update_msi(PCIDevice *pci_dev)
{
    AssignedDevice *assigned_dev = PCI_ASSIGN(pci_dev);
    uint8_t ctrl_byte = pci_get_byte(pci_dev->config + pci_dev->msi_cap +
                                     PCI_MSI_FLAGS);
    int r;

    /* Some guests gratuitously disable MSI even if they're not using it,
     * try to catch this by only deassigning irqs if the guest is using
     * MSI or intends to start. */
    if (assigned_dev->assigned_irq_type == ASSIGNED_IRQ_MSI ||
        (ctrl_byte & PCI_MSI_FLAGS_ENABLE)) {
        r = kvm_device_msi_deassign(kvm_state, assigned_dev->dev_id);
        /* -ENXIO means no assigned irq */
        if (r && r != -ENXIO) {
            perror("assigned_dev_update_msi: deassign irq");
        }

        free_msi_virqs(assigned_dev);

        assigned_dev->assigned_irq_type = ASSIGNED_IRQ_NONE;
        pci_device_set_intx_routing_notifier(pci_dev, NULL);
    }

    if (ctrl_byte & PCI_MSI_FLAGS_ENABLE) {
        MSIMessage msg = msi_get_message(pci_dev, 0);
        int virq;

        virq = kvm_irqchip_add_msi_route(kvm_state, msg, pci_dev);
        if (virq < 0) {
            perror("assigned_dev_update_msi: kvm_irqchip_add_msi_route");
            return;
        }

        assigned_dev->msi_virq = g_malloc(sizeof(*assigned_dev->msi_virq));
        assigned_dev->msi_virq_nr = 1;
        assigned_dev->msi_virq[0] = virq;
        if (kvm_device_msi_assign(kvm_state, assigned_dev->dev_id, virq) < 0) {
            perror("assigned_dev_update_msi: kvm_device_msi_assign");
        }

        assigned_dev->intx_route.mode = PCI_INTX_DISABLED;
        assigned_dev->intx_route.irq = -1;
        assigned_dev->assigned_irq_type = ASSIGNED_IRQ_MSI;
    } else {
        Error *local_err = NULL;

        assign_intx(assigned_dev, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    }
}

static void assigned_dev_update_msi_msg(PCIDevice *pci_dev)
{
    AssignedDevice *assigned_dev = PCI_ASSIGN(pci_dev);
    uint8_t ctrl_byte = pci_get_byte(pci_dev->config + pci_dev->msi_cap +
                                     PCI_MSI_FLAGS);

    if (assigned_dev->assigned_irq_type != ASSIGNED_IRQ_MSI ||
        !(ctrl_byte & PCI_MSI_FLAGS_ENABLE)) {
        return;
    }

    kvm_irqchip_update_msi_route(kvm_state, assigned_dev->msi_virq[0],
                                 msi_get_message(pci_dev, 0), pci_dev);
}

static bool assigned_dev_msix_masked(MSIXTableEntry *entry)
{
    return (entry->ctrl & cpu_to_le32(0x1)) != 0;
}

/*
 * When MSI-X is first enabled the vector table typically has all the
 * vectors masked, so we can't use that as the obvious test to figure out
 * how many vectors to initially enable.  Instead we look at the data field
 * because this is what worked for pci-assign for a long time.  This makes
 * sure the physical MSI-X state tracks the guest's view, which is important
 * for some VF/PF and PF/fw communication channels.
 */
static bool assigned_dev_msix_skipped(MSIXTableEntry *entry)
{
    return !entry->data;
}

static int assigned_dev_update_msix_mmio(PCIDevice *pci_dev)
{
    AssignedDevice *adev = PCI_ASSIGN(pci_dev);
    uint16_t entries_nr = 0;
    int i, r = 0;
    MSIXTableEntry *entry = adev->msix_table;
    MSIMessage msg;

    /* Get the usable entry number for allocating */
    for (i = 0; i < adev->msix_max; i++, entry++) {
        if (assigned_dev_msix_skipped(entry)) {
            continue;
        }
        entries_nr++;
    }

    DEBUG("MSI-X entries: %d\n", entries_nr);

    /* It's valid to enable MSI-X with all entries masked */
    if (!entries_nr) {
        return 0;
    }

    r = kvm_device_msix_init_vectors(kvm_state, adev->dev_id, entries_nr);
    if (r != 0) {
        error_report("fail to set MSI-X entry number for MSIX! %s",
                     strerror(-r));
        return r;
    }

    free_msi_virqs(adev);

    adev->msi_virq_nr = adev->msix_max;
    adev->msi_virq = g_malloc(adev->msix_max * sizeof(*adev->msi_virq));

    entry = adev->msix_table;
    for (i = 0; i < adev->msix_max; i++, entry++) {
        adev->msi_virq[i] = -1;

        if (assigned_dev_msix_skipped(entry)) {
            continue;
        }

        msg.address = entry->addr_lo | ((uint64_t)entry->addr_hi << 32);
        msg.data = entry->data;
        r = kvm_irqchip_add_msi_route(kvm_state, msg, pci_dev);
        if (r < 0) {
            return r;
        }
        adev->msi_virq[i] = r;

        DEBUG("MSI-X vector %d, gsi %d, addr %08x_%08x, data %08x\n", i,
              r, entry->addr_hi, entry->addr_lo, entry->data);

        r = kvm_device_msix_set_vector(kvm_state, adev->dev_id, i,
                                       adev->msi_virq[i]);
        if (r) {
            error_report("fail to set MSI-X entry! %s", strerror(-r));
            break;
        }
    }

    return r;
}

static void assigned_dev_update_msix(PCIDevice *pci_dev)
{
    AssignedDevice *assigned_dev = PCI_ASSIGN(pci_dev);
    uint16_t ctrl_word = pci_get_word(pci_dev->config + pci_dev->msix_cap +
                                      PCI_MSIX_FLAGS);
    int r;

    /* Some guests gratuitously disable MSIX even if they're not using it,
     * try to catch this by only deassigning irqs if the guest is using
     * MSIX or intends to start. */
    if ((assigned_dev->assigned_irq_type == ASSIGNED_IRQ_MSIX) ||
        (ctrl_word & PCI_MSIX_FLAGS_ENABLE)) {
        r = kvm_device_msix_deassign(kvm_state, assigned_dev->dev_id);
        /* -ENXIO means no assigned irq */
        if (r && r != -ENXIO) {
            perror("assigned_dev_update_msix: deassign irq");
        }

        free_msi_virqs(assigned_dev);

        assigned_dev->assigned_irq_type = ASSIGNED_IRQ_NONE;
        pci_device_set_intx_routing_notifier(pci_dev, NULL);
    }

    if (ctrl_word & PCI_MSIX_FLAGS_ENABLE) {
        if (assigned_dev_update_msix_mmio(pci_dev) < 0) {
            perror("assigned_dev_update_msix_mmio");
            return;
        }

        if (assigned_dev->msi_virq_nr > 0) {
            if (kvm_device_msix_assign(kvm_state, assigned_dev->dev_id) < 0) {
                perror("assigned_dev_enable_msix: assign irq");
                return;
            }
        }
        assigned_dev->intx_route.mode = PCI_INTX_DISABLED;
        assigned_dev->intx_route.irq = -1;
        assigned_dev->assigned_irq_type = ASSIGNED_IRQ_MSIX;
    } else {
        Error *local_err = NULL;

        assign_intx(assigned_dev, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    }
}

static uint32_t assigned_dev_pci_read_config(PCIDevice *pci_dev,
                                             uint32_t address, int len)
{
    AssignedDevice *assigned_dev = PCI_ASSIGN(pci_dev);
    uint32_t virt_val = pci_default_read_config(pci_dev, address, len);
    uint32_t real_val, emulate_mask, full_emulation_mask;

    emulate_mask = 0;
    memcpy(&emulate_mask, assigned_dev->emulate_config_read + address, len);
    emulate_mask = le32_to_cpu(emulate_mask);

    full_emulation_mask = 0xffffffff >> (32 - len * 8);

    if (emulate_mask != full_emulation_mask) {
        real_val = assigned_dev_pci_read(pci_dev, address, len);
        return (virt_val & emulate_mask) | (real_val & ~emulate_mask);
    } else {
        return virt_val;
    }
}

static void assigned_dev_pci_write_config(PCIDevice *pci_dev, uint32_t address,
                                          uint32_t val, int len)
{
    AssignedDevice *assigned_dev = PCI_ASSIGN(pci_dev);
    uint16_t old_cmd = pci_get_word(pci_dev->config + PCI_COMMAND);
    uint32_t emulate_mask, full_emulation_mask;
    int ret;

    pci_default_write_config(pci_dev, address, val, len);

    if (kvm_has_intx_set_mask() &&
        range_covers_byte(address, len, PCI_COMMAND + 1)) {
        bool intx_masked = (pci_get_word(pci_dev->config + PCI_COMMAND) &
                            PCI_COMMAND_INTX_DISABLE);

        if (intx_masked != !!(old_cmd & PCI_COMMAND_INTX_DISABLE)) {
            ret = kvm_device_intx_set_mask(kvm_state, assigned_dev->dev_id,
                                           intx_masked);
            if (ret) {
                perror("assigned_dev_pci_write_config: set intx mask");
            }
        }
    }
    if (assigned_dev->cap.available & ASSIGNED_DEVICE_CAP_MSI) {
        if (range_covers_byte(address, len,
                              pci_dev->msi_cap + PCI_MSI_FLAGS)) {
            assigned_dev_update_msi(pci_dev);
        } else if (ranges_overlap(address, len, /* 32bit MSI only */
                                  pci_dev->msi_cap + PCI_MSI_ADDRESS_LO, 6)) {
            assigned_dev_update_msi_msg(pci_dev);
        }
    }
    if (assigned_dev->cap.available & ASSIGNED_DEVICE_CAP_MSIX) {
        if (range_covers_byte(address, len,
                              pci_dev->msix_cap + PCI_MSIX_FLAGS + 1)) {
            assigned_dev_update_msix(pci_dev);
        }
    }

    emulate_mask = 0;
    memcpy(&emulate_mask, assigned_dev->emulate_config_write + address, len);
    emulate_mask = le32_to_cpu(emulate_mask);

    full_emulation_mask = 0xffffffff >> (32 - len * 8);

    if (emulate_mask != full_emulation_mask) {
        if (emulate_mask) {
            val &= ~emulate_mask;
            val |= assigned_dev_pci_read(pci_dev, address, len) & emulate_mask;
        }
        assigned_dev_pci_write(pci_dev, address, val, len);
    }
}

static void assigned_dev_setup_cap_read(AssignedDevice *dev, uint32_t offset,
                                        uint32_t len)
{
    assigned_dev_direct_config_read(dev, offset, len);
    assigned_dev_emulate_config_read(dev, offset + PCI_CAP_LIST_NEXT, 1);
}

static int assigned_device_pci_cap_init(PCIDevice *pci_dev, Error **errp)
{
    AssignedDevice *dev = PCI_ASSIGN(pci_dev);
    PCIRegion *pci_region = dev->real_device.regions;
    int ret, pos;
    Error *local_err = NULL;

    /* Clear initial capabilities pointer and status copied from hw */
    pci_set_byte(pci_dev->config + PCI_CAPABILITY_LIST, 0);
    pci_set_word(pci_dev->config + PCI_STATUS,
                 pci_get_word(pci_dev->config + PCI_STATUS) &
                 ~PCI_STATUS_CAP_LIST);

    /* Expose MSI capability
     * MSI capability is the 1st capability in capability config */
    pos = pci_find_cap_offset(pci_dev, PCI_CAP_ID_MSI, 0);
    if (pos != 0 && kvm_check_extension(kvm_state, KVM_CAP_ASSIGN_DEV_IRQ)) {
        verify_irqchip_in_kernel(&local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -ENOTSUP;
        }
        dev->cap.available |= ASSIGNED_DEVICE_CAP_MSI;
        /* Only 32-bit/no-mask currently supported */
        ret = pci_add_capability2(pci_dev, PCI_CAP_ID_MSI, pos, 10,
                                  &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            return ret;
        }
        pci_dev->msi_cap = pos;

        pci_set_word(pci_dev->config + pos + PCI_MSI_FLAGS,
                     pci_get_word(pci_dev->config + pos + PCI_MSI_FLAGS) &
                     PCI_MSI_FLAGS_QMASK);
        pci_set_long(pci_dev->config + pos + PCI_MSI_ADDRESS_LO, 0);
        pci_set_word(pci_dev->config + pos + PCI_MSI_DATA_32, 0);

        /* Set writable fields */
        pci_set_word(pci_dev->wmask + pos + PCI_MSI_FLAGS,
                     PCI_MSI_FLAGS_QSIZE | PCI_MSI_FLAGS_ENABLE);
        pci_set_long(pci_dev->wmask + pos + PCI_MSI_ADDRESS_LO, 0xfffffffc);
        pci_set_word(pci_dev->wmask + pos + PCI_MSI_DATA_32, 0xffff);
    }
    /* Expose MSI-X capability */
    pos = pci_find_cap_offset(pci_dev, PCI_CAP_ID_MSIX, 0);
    if (pos != 0 && kvm_device_msix_supported(kvm_state)) {
        int bar_nr;
        uint32_t msix_table_entry;
        uint16_t msix_max;

        verify_irqchip_in_kernel(&local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -ENOTSUP;
        }
        dev->cap.available |= ASSIGNED_DEVICE_CAP_MSIX;
        ret = pci_add_capability2(pci_dev, PCI_CAP_ID_MSIX, pos, 12,
                                  &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            return ret;
        }
        pci_dev->msix_cap = pos;

        msix_max = (pci_get_word(pci_dev->config + pos + PCI_MSIX_FLAGS) &
                    PCI_MSIX_FLAGS_QSIZE) + 1;
        msix_max = MIN(msix_max, KVM_MAX_MSIX_PER_DEV);
        pci_set_word(pci_dev->config + pos + PCI_MSIX_FLAGS, msix_max - 1);

        /* Only enable and function mask bits are writable */
        pci_set_word(pci_dev->wmask + pos + PCI_MSIX_FLAGS,
                     PCI_MSIX_FLAGS_ENABLE | PCI_MSIX_FLAGS_MASKALL);

        msix_table_entry = pci_get_long(pci_dev->config + pos + PCI_MSIX_TABLE);
        bar_nr = msix_table_entry & PCI_MSIX_FLAGS_BIRMASK;
        msix_table_entry &= ~PCI_MSIX_FLAGS_BIRMASK;
        dev->msix_table_addr = pci_region[bar_nr].base_addr + msix_table_entry;
        dev->msix_max = msix_max;
    }

    /* Minimal PM support, nothing writable, device appears to NAK changes */
    pos = pci_find_cap_offset(pci_dev, PCI_CAP_ID_PM, 0);
    if (pos) {
        uint16_t pmc;

        ret = pci_add_capability2(pci_dev, PCI_CAP_ID_PM, pos, PCI_PM_SIZEOF,
                                  &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            return ret;
        }

        assigned_dev_setup_cap_read(dev, pos, PCI_PM_SIZEOF);

        pmc = pci_get_word(pci_dev->config + pos + PCI_CAP_FLAGS);
        pmc &= (PCI_PM_CAP_VER_MASK | PCI_PM_CAP_DSI);
        pci_set_word(pci_dev->config + pos + PCI_CAP_FLAGS, pmc);

        /* assign_device will bring the device up to D0, so we don't need
         * to worry about doing that ourselves here. */
        pci_set_word(pci_dev->config + pos + PCI_PM_CTRL,
                     PCI_PM_CTRL_NO_SOFT_RESET);

        pci_set_byte(pci_dev->config + pos + PCI_PM_PPB_EXTENSIONS, 0);
        pci_set_byte(pci_dev->config + pos + PCI_PM_DATA_REGISTER, 0);
    }

    pos = pci_find_cap_offset(pci_dev, PCI_CAP_ID_EXP, 0);
    if (pos) {
        uint8_t version, size = 0;
        uint16_t type, devctl, lnksta;
        uint32_t devcap, lnkcap;

        version = pci_get_byte(pci_dev->config + pos + PCI_EXP_FLAGS);
        version &= PCI_EXP_FLAGS_VERS;
        if (version == 1) {
            size = 0x14;
        } else if (version == 2) {
            /*
             * Check for non-std size, accept reduced size to 0x34,
             * which is what bcm5761 implemented, violating the
             * PCIe v3.0 spec that regs should exist and be read as 0,
             * not optionally provided and shorten the struct size.
             */
            size = MIN(0x3c, PCI_CONFIG_SPACE_SIZE - pos);
            if (size < 0x34) {
                error_setg(errp, "Invalid size PCIe cap-id 0x%x",
                           PCI_CAP_ID_EXP);
                return -EINVAL;
            } else if (size != 0x3c) {
                error_report("WARNING, %s: PCIe cap-id 0x%x has "
                             "non-standard size 0x%x; std size should be 0x3c",
                             __func__, PCI_CAP_ID_EXP, size);
            }
        } else if (version == 0) {
            uint16_t vid, did;
            vid = pci_get_word(pci_dev->config + PCI_VENDOR_ID);
            did = pci_get_word(pci_dev->config + PCI_DEVICE_ID);
            if (vid == PCI_VENDOR_ID_INTEL && did == 0x10ed) {
                /*
                 * quirk for Intel 82599 VF with invalid PCIe capability
                 * version, should really be version 2 (same as PF)
                 */
                size = 0x3c;
            }
        }

        if (size == 0) {
            error_setg(errp, "Unsupported PCI express capability version %d",
                       version);
            return -EINVAL;
        }

        ret = pci_add_capability2(pci_dev, PCI_CAP_ID_EXP, pos, size,
                                  &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            return ret;
        }

        assigned_dev_setup_cap_read(dev, pos, size);

        type = pci_get_word(pci_dev->config + pos + PCI_EXP_FLAGS);
        type = (type & PCI_EXP_FLAGS_TYPE) >> 4;
        if (type != PCI_EXP_TYPE_ENDPOINT &&
            type != PCI_EXP_TYPE_LEG_END && type != PCI_EXP_TYPE_RC_END) {
            error_setg(errp, "Device assignment only supports endpoint "
                       "assignment, device type %d", type);
            return -EINVAL;
        }

        /* capabilities, pass existing read-only copy
         * PCI_EXP_FLAGS_IRQ: updated by hardware, should be direct read */

        /* device capabilities: hide FLR */
        devcap = pci_get_long(pci_dev->config + pos + PCI_EXP_DEVCAP);
        devcap &= ~PCI_EXP_DEVCAP_FLR;
        pci_set_long(pci_dev->config + pos + PCI_EXP_DEVCAP, devcap);

        /* device control: clear all error reporting enable bits, leaving
         *                 only a few host values.  Note, these are
         *                 all writable, but not passed to hw.
         */
        devctl = pci_get_word(pci_dev->config + pos + PCI_EXP_DEVCTL);
        devctl = (devctl & (PCI_EXP_DEVCTL_READRQ | PCI_EXP_DEVCTL_PAYLOAD)) |
                  PCI_EXP_DEVCTL_RELAX_EN | PCI_EXP_DEVCTL_NOSNOOP_EN;
        pci_set_word(pci_dev->config + pos + PCI_EXP_DEVCTL, devctl);
        devctl = PCI_EXP_DEVCTL_BCR_FLR | PCI_EXP_DEVCTL_AUX_PME;
        pci_set_word(pci_dev->wmask + pos + PCI_EXP_DEVCTL, ~devctl);

        /* Clear device status */
        pci_set_word(pci_dev->config + pos + PCI_EXP_DEVSTA, 0);

        /* Link capabilities, expose links and latencues, clear reporting */
        lnkcap = pci_get_long(pci_dev->config + pos + PCI_EXP_LNKCAP);
        lnkcap &= (PCI_EXP_LNKCAP_SLS | PCI_EXP_LNKCAP_MLW |
                   PCI_EXP_LNKCAP_ASPMS | PCI_EXP_LNKCAP_L0SEL |
                   PCI_EXP_LNKCAP_L1EL);
        pci_set_long(pci_dev->config + pos + PCI_EXP_LNKCAP, lnkcap);

        /* Link control, pass existing read-only copy.  Should be writable? */

        /* Link status, only expose current speed and width */
        lnksta = pci_get_word(pci_dev->config + pos + PCI_EXP_LNKSTA);
        lnksta &= (PCI_EXP_LNKSTA_CLS | PCI_EXP_LNKSTA_NLW);
        pci_set_word(pci_dev->config + pos + PCI_EXP_LNKSTA, lnksta);

        if (version >= 2) {
            /* Slot capabilities, control, status - not needed for endpoints */
            pci_set_long(pci_dev->config + pos + PCI_EXP_SLTCAP, 0);
            pci_set_word(pci_dev->config + pos + PCI_EXP_SLTCTL, 0);
            pci_set_word(pci_dev->config + pos + PCI_EXP_SLTSTA, 0);

            /* Root control, capabilities, status - not needed for endpoints */
            pci_set_word(pci_dev->config + pos + PCI_EXP_RTCTL, 0);
            pci_set_word(pci_dev->config + pos + PCI_EXP_RTCAP, 0);
            pci_set_long(pci_dev->config + pos + PCI_EXP_RTSTA, 0);

            /* Device capabilities/control 2, pass existing read-only copy */
            /* Link control 2, pass existing read-only copy */
        }
    }

    pos = pci_find_cap_offset(pci_dev, PCI_CAP_ID_PCIX, 0);
    if (pos) {
        uint16_t cmd;
        uint32_t status;

        /* Only expose the minimum, 8 byte capability */
        ret = pci_add_capability2(pci_dev, PCI_CAP_ID_PCIX, pos, 8,
                                  &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            return ret;
        }

        assigned_dev_setup_cap_read(dev, pos, 8);

        /* Command register, clear upper bits, including extended modes */
        cmd = pci_get_word(pci_dev->config + pos + PCI_X_CMD);
        cmd &= (PCI_X_CMD_DPERR_E | PCI_X_CMD_ERO | PCI_X_CMD_MAX_READ |
                PCI_X_CMD_MAX_SPLIT);
        pci_set_word(pci_dev->config + pos + PCI_X_CMD, cmd);

        /* Status register, update with emulated PCI bus location, clear
         * error bits, leave the rest. */
        status = pci_get_long(pci_dev->config + pos + PCI_X_STATUS);
        status &= ~(PCI_X_STATUS_BUS | PCI_X_STATUS_DEVFN);
        status |= pci_get_bdf(pci_dev);
        status &= ~(PCI_X_STATUS_SPL_DISC | PCI_X_STATUS_UNX_SPL |
                    PCI_X_STATUS_SPL_ERR);
        pci_set_long(pci_dev->config + pos + PCI_X_STATUS, status);
    }

    pos = pci_find_cap_offset(pci_dev, PCI_CAP_ID_VPD, 0);
    if (pos) {
        /* Direct R/W passthrough */
        ret = pci_add_capability2(pci_dev, PCI_CAP_ID_VPD, pos, 8,
                                  &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            return ret;
        }

        assigned_dev_setup_cap_read(dev, pos, 8);

        /* direct write for cap content */
        assigned_dev_direct_config_write(dev, pos + 2, 6);
    }

    /* Devices can have multiple vendor capabilities, get them all */
    for (pos = 0; (pos = pci_find_cap_offset(pci_dev, PCI_CAP_ID_VNDR, pos));
        pos += PCI_CAP_LIST_NEXT) {
        uint8_t len = pci_get_byte(pci_dev->config + pos + PCI_CAP_FLAGS);
        /* Direct R/W passthrough */
        ret = pci_add_capability2(pci_dev, PCI_CAP_ID_VNDR, pos, len,
                                  &local_err);
        if (ret < 0) {
            error_propagate(errp, local_err);
            return ret;
        }

        assigned_dev_setup_cap_read(dev, pos, len);

        /* direct write for cap content */
        assigned_dev_direct_config_write(dev, pos + 2, len - 2);
    }

    /* If real and virtual capability list status bits differ, virtualize the
     * access. */
    if ((pci_get_word(pci_dev->config + PCI_STATUS) & PCI_STATUS_CAP_LIST) !=
        (assigned_dev_pci_read_byte(pci_dev, PCI_STATUS) &
         PCI_STATUS_CAP_LIST)) {
        dev->emulate_config_read[PCI_STATUS] |= PCI_STATUS_CAP_LIST;
    }

    return 0;
}

static uint64_t
assigned_dev_msix_mmio_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    AssignedDevice *adev = opaque;
    uint64_t val;

    memcpy(&val, (void *)((uint8_t *)adev->msix_table + addr), size);

    return val;
}

static void assigned_dev_msix_mmio_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned size)
{
    AssignedDevice *adev = opaque;
    PCIDevice *pdev = &adev->dev;
    uint16_t ctrl;
    MSIXTableEntry orig;
    int i = addr >> 4;

    if (i >= adev->msix_max) {
        return; /* Drop write */
    }

    ctrl = pci_get_word(pdev->config + pdev->msix_cap + PCI_MSIX_FLAGS);

    DEBUG("write to MSI-X table offset 0x%lx, val 0x%lx\n", addr, val);

    if (ctrl & PCI_MSIX_FLAGS_ENABLE) {
        orig = adev->msix_table[i];
    }

    memcpy((uint8_t *)adev->msix_table + addr, &val, size);

    if (ctrl & PCI_MSIX_FLAGS_ENABLE) {
        MSIXTableEntry *entry = &adev->msix_table[i];

        if (!assigned_dev_msix_masked(&orig) &&
            assigned_dev_msix_masked(entry)) {
            /*
             * Vector masked, disable it
             *
             * XXX It's not clear if we can or should actually attempt
             * to mask or disable the interrupt.  KVM doesn't have
             * support for pending bits and kvm_assign_set_msix_entry
             * doesn't modify the device hardware mask.  Interrupts
             * while masked are simply not injected to the guest, so
             * are lost.  Can we get away with always injecting an
             * interrupt on unmask?
             */
        } else if (assigned_dev_msix_masked(&orig) &&
                   !assigned_dev_msix_masked(entry)) {
            /* Vector unmasked */
            if (i >= adev->msi_virq_nr || adev->msi_virq[i] < 0) {
                /* Previously unassigned vector, start from scratch */
                assigned_dev_update_msix(pdev);
                return;
            } else {
                /* Update an existing, previously masked vector */
                MSIMessage msg;
                int ret;

                msg.address = entry->addr_lo |
                    ((uint64_t)entry->addr_hi << 32);
                msg.data = entry->data;

                ret = kvm_irqchip_update_msi_route(kvm_state,
                                                   adev->msi_virq[i], msg,
                                                   pdev);
                if (ret) {
                    error_report("Error updating irq routing entry (%d)", ret);
                }
            }
        }
    }
}

static const MemoryRegionOps assigned_dev_msix_mmio_ops = {
    .read = assigned_dev_msix_mmio_read,
    .write = assigned_dev_msix_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void assigned_dev_msix_reset(AssignedDevice *dev)
{
    MSIXTableEntry *entry;
    int i;

    if (!dev->msix_table) {
        return;
    }

    memset(dev->msix_table, 0, MSIX_PAGE_SIZE);

    for (i = 0, entry = dev->msix_table; i < dev->msix_max; i++, entry++) {
        entry->ctrl = cpu_to_le32(0x1); /* Masked */
    }
}

static void assigned_dev_register_msix_mmio(AssignedDevice *dev, Error **errp)
{
    dev->msix_table = mmap(NULL, MSIX_PAGE_SIZE, PROT_READ|PROT_WRITE,
                           MAP_ANONYMOUS|MAP_PRIVATE, 0, 0);
    if (dev->msix_table == MAP_FAILED) {
        error_setg_errno(errp, errno, "failed to allocate msix_table");
        dev->msix_table = NULL;
        return;
    }

    assigned_dev_msix_reset(dev);

    memory_region_init_io(&dev->mmio, OBJECT(dev), &assigned_dev_msix_mmio_ops,
                          dev, "assigned-dev-msix", MSIX_PAGE_SIZE);
}

static void assigned_dev_unregister_msix_mmio(AssignedDevice *dev)
{
    if (!dev->msix_table) {
        return;
    }

    if (munmap(dev->msix_table, MSIX_PAGE_SIZE) == -1) {
        error_report("error unmapping msix_table! %s", strerror(errno));
    }
    dev->msix_table = NULL;
}

static const VMStateDescription vmstate_assigned_device = {
    .name = "pci-assign",
    .unmigratable = 1,
};

static void reset_assigned_device(DeviceState *dev)
{
    PCIDevice *pci_dev = PCI_DEVICE(dev);
    AssignedDevice *adev = PCI_ASSIGN(pci_dev);
    char reset_file[64];
    const char reset[] = "1";
    int fd, ret;

    /*
     * If a guest is reset without being shutdown, MSI/MSI-X can still
     * be running.  We want to return the device to a known state on
     * reset, so disable those here.  We especially do not want MSI-X
     * enabled since it lives in MMIO space, which is about to get
     * disabled.
     */
    if (adev->assigned_irq_type == ASSIGNED_IRQ_MSIX) {
        uint16_t ctrl = pci_get_word(pci_dev->config +
                                     pci_dev->msix_cap + PCI_MSIX_FLAGS);

        pci_set_word(pci_dev->config + pci_dev->msix_cap + PCI_MSIX_FLAGS,
                     ctrl & ~PCI_MSIX_FLAGS_ENABLE);
        assigned_dev_update_msix(pci_dev);
    } else if (adev->assigned_irq_type == ASSIGNED_IRQ_MSI) {
        uint8_t ctrl = pci_get_byte(pci_dev->config +
                                    pci_dev->msi_cap + PCI_MSI_FLAGS);

        pci_set_byte(pci_dev->config + pci_dev->msi_cap + PCI_MSI_FLAGS,
                     ctrl & ~PCI_MSI_FLAGS_ENABLE);
        assigned_dev_update_msi(pci_dev);
    }

    snprintf(reset_file, sizeof(reset_file),
             "/sys/bus/pci/devices/%04x:%02x:%02x.%01x/reset",
             adev->host.domain, adev->host.bus, adev->host.slot,
             adev->host.function);

    /*
     * Issue a device reset via pci-sysfs.  Note that we use write(2) here
     * and ignore the return value because some kernels have a bug that
     * returns 0 rather than bytes written on success, sending us into an
     * infinite retry loop using other write mechanisms.
     */
    fd = open(reset_file, O_WRONLY);
    if (fd != -1) {
        ret = write(fd, reset, strlen(reset));
        (void)ret;
        close(fd);
    }

    /*
     * When a 0 is written to the bus master register, the device is logically
     * disconnected from the PCI bus. This avoids further DMA transfers.
     */
    assigned_dev_pci_write_config(pci_dev, PCI_COMMAND, 0, 1);
}

static void assigned_realize(struct PCIDevice *pci_dev, Error **errp)
{
    AssignedDevice *dev = PCI_ASSIGN(pci_dev);
    uint8_t e_intx;
    int r;
    Error *local_err = NULL;

    if (!kvm_enabled()) {
        error_setg(&local_err, "pci-assign requires KVM support");
        goto exit_with_error;
    }

    if (!dev->host.domain && !dev->host.bus && !dev->host.slot &&
        !dev->host.function) {
        error_setg(&local_err, "no host device specified");
        goto exit_with_error;
    }

    /*
     * Set up basic config space access control. Will be further refined during
     * device initialization.
     */
    assigned_dev_emulate_config_read(dev, 0, PCI_CONFIG_SPACE_SIZE);
    assigned_dev_direct_config_read(dev, PCI_STATUS, 2);
    assigned_dev_direct_config_read(dev, PCI_REVISION_ID, 1);
    assigned_dev_direct_config_read(dev, PCI_CLASS_PROG, 3);
    assigned_dev_direct_config_read(dev, PCI_CACHE_LINE_SIZE, 1);
    assigned_dev_direct_config_read(dev, PCI_LATENCY_TIMER, 1);
    assigned_dev_direct_config_read(dev, PCI_BIST, 1);
    assigned_dev_direct_config_read(dev, PCI_CARDBUS_CIS, 4);
    assigned_dev_direct_config_read(dev, PCI_SUBSYSTEM_VENDOR_ID, 2);
    assigned_dev_direct_config_read(dev, PCI_SUBSYSTEM_ID, 2);
    assigned_dev_direct_config_read(dev, PCI_CAPABILITY_LIST + 1, 7);
    assigned_dev_direct_config_read(dev, PCI_MIN_GNT, 1);
    assigned_dev_direct_config_read(dev, PCI_MAX_LAT, 1);
    memcpy(dev->emulate_config_write, dev->emulate_config_read,
           sizeof(dev->emulate_config_read));

    get_real_device(dev, &local_err);
    if (local_err) {
        goto out;
    }

    if (assigned_device_pci_cap_init(pci_dev, &local_err) < 0) {
        goto out;
    }

    /* intercept MSI-X entry page in the MMIO */
    if (dev->cap.available & ASSIGNED_DEVICE_CAP_MSIX) {
        assigned_dev_register_msix_mmio(dev, &local_err);
        if (local_err) {
            goto out;
        }
    }

    /* handle real device's MMIO/PIO BARs */
    assigned_dev_register_regions(dev->real_device.regions,
                                  dev->real_device.region_number, dev,
                                  &local_err);
    if (local_err) {
        goto out;
    }

    /* handle interrupt routing */
    e_intx = dev->dev.config[PCI_INTERRUPT_PIN] - 1;
    dev->intpin = e_intx;
    dev->intx_route.mode = PCI_INTX_DISABLED;
    dev->intx_route.irq = -1;

    /* assign device to guest */
    assign_device(dev, &local_err);
    if (local_err) {
        goto out;
    }

    /* assign legacy INTx to the device */
    r = assign_intx(dev, &local_err);
    if (r < 0) {
        goto assigned_out;
    }

    assigned_dev_load_option_rom(dev);

    return;

assigned_out:
    deassign_device(dev);

out:
    free_assigned_device(dev);

exit_with_error:
    assert(local_err);
    error_propagate(errp, local_err);
}

static void assigned_exitfn(struct PCIDevice *pci_dev)
{
    AssignedDevice *dev = PCI_ASSIGN(pci_dev);

    deassign_device(dev);
    free_assigned_device(dev);
}

static void assigned_dev_instance_init(Object *obj)
{
    PCIDevice *pci_dev = PCI_DEVICE(obj);
    AssignedDevice *d = PCI_ASSIGN(pci_dev);

    device_add_bootindex_property(obj, &d->bootindex,
                                  "bootindex", NULL,
                                  &pci_dev->qdev, NULL);
}

static Property assigned_dev_properties[] = {
    DEFINE_PROP_PCI_HOST_DEVADDR("host", AssignedDevice, host),
    DEFINE_PROP_BIT("prefer_msi", AssignedDevice, features,
                    ASSIGNED_DEVICE_PREFER_MSI_BIT, false),
    DEFINE_PROP_BIT("share_intx", AssignedDevice, features,
                    ASSIGNED_DEVICE_SHARE_INTX_BIT, true),
    DEFINE_PROP_STRING("configfd", AssignedDevice, configfd_name),
    DEFINE_PROP_END_OF_LIST(),
};

static void assign_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize      = assigned_realize;
    k->exit         = assigned_exitfn;
    k->config_read  = assigned_dev_pci_read_config;
    k->config_write = assigned_dev_pci_write_config;
    dc->props       = assigned_dev_properties;
    dc->vmsd        = &vmstate_assigned_device;
    dc->reset       = reset_assigned_device;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc        = "KVM-based PCI passthrough";
}

static const TypeInfo assign_info = {
    .name               = TYPE_PCI_ASSIGN,
    .parent             = TYPE_PCI_DEVICE,
    .instance_size      = sizeof(AssignedDevice),
    .class_init         = assign_class_init,
    .instance_init      = assigned_dev_instance_init,
};

static void assign_register_types(void)
{
    type_register_static(&assign_info);
}

type_init(assign_register_types)

static void assigned_dev_load_option_rom(AssignedDevice *dev)
{
    int size = 0;

    pci_assign_dev_load_option_rom(&dev->dev, OBJECT(dev), &size,
                                   dev->host.domain, dev->host.bus,
                                   dev->host.slot, dev->host.function);

    if (!size) {
        error_report("pci-assign: Invalid ROM.");
    }
}
