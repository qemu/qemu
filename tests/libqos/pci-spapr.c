/*
 * libqos PCI bindings for SPAPR
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "libqos/pci-spapr.h"
#include "libqos/rtas.h"

#include "hw/pci/pci_regs.h"

#include "qemu-common.h"
#include "qemu/host-utils.h"


/* From include/hw/pci-host/spapr.h */

typedef struct QPCIWindow {
    uint64_t pci_base;    /* window address in PCI space */
    uint64_t size;        /* window size */
} QPCIWindow;

typedef struct QPCIBusSPAPR {
    QPCIBus bus;
    QGuestAllocator *alloc;

    uint64_t buid;

    uint64_t pio_cpu_base;
    QPCIWindow pio;

    uint64_t mmio32_cpu_base;
    QPCIWindow mmio32;

    uint64_t pci_hole_start;
    uint64_t pci_hole_size;
    uint64_t pci_hole_alloc;

    uint32_t pci_iohole_start;
    uint32_t pci_iohole_size;
    uint32_t pci_iohole_alloc;
} QPCIBusSPAPR;

/*
 * PCI devices are always little-endian
 * SPAPR by default is big-endian
 * so PCI accessors need to swap data endianness
 */

static uint8_t qpci_spapr_pio_readb(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return readb(s->pio_cpu_base + addr);
}

static uint8_t qpci_spapr_mmio32_readb(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return readb(s->mmio32_cpu_base + addr);
}

static void qpci_spapr_pio_writeb(QPCIBus *bus, uint32_t addr, uint8_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writeb(s->pio_cpu_base + addr, val);
}

static void qpci_spapr_mmio32_writeb(QPCIBus *bus, uint32_t addr, uint8_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writeb(s->mmio32_cpu_base + addr, val);
}

static uint16_t qpci_spapr_pio_readw(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap16(readw(s->pio_cpu_base + addr));
}

static uint16_t qpci_spapr_mmio32_readw(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap16(readw(s->mmio32_cpu_base + addr));
}

static void qpci_spapr_pio_writew(QPCIBus *bus, uint32_t addr, uint16_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writew(s->pio_cpu_base + addr, bswap16(val));
}

static void qpci_spapr_mmio32_writew(QPCIBus *bus, uint32_t addr, uint16_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writew(s->mmio32_cpu_base + addr, bswap16(val));
}

static uint32_t qpci_spapr_pio_readl(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap32(readl(s->pio_cpu_base + addr));
}

static uint32_t qpci_spapr_mmio32_readl(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap32(readl(s->mmio32_cpu_base + addr));
}

static void qpci_spapr_pio_writel(QPCIBus *bus, uint32_t addr, uint32_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writel(s->pio_cpu_base + addr, bswap32(val));
}

static void qpci_spapr_mmio32_writel(QPCIBus *bus, uint32_t addr, uint32_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writel(s->mmio32_cpu_base + addr, bswap32(val));
}

static uint8_t qpci_spapr_config_readb(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    return qrtas_ibm_read_pci_config(s->alloc, s->buid, config_addr, 1);
}

static uint16_t qpci_spapr_config_readw(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    return qrtas_ibm_read_pci_config(s->alloc, s->buid, config_addr, 2);
}

static uint32_t qpci_spapr_config_readl(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    return qrtas_ibm_read_pci_config(s->alloc, s->buid, config_addr, 4);
}

static void qpci_spapr_config_writeb(QPCIBus *bus, int devfn, uint8_t offset,
                                     uint8_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    qrtas_ibm_write_pci_config(s->alloc, s->buid, config_addr, 1, value);
}

static void qpci_spapr_config_writew(QPCIBus *bus, int devfn, uint8_t offset,
                                     uint16_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    qrtas_ibm_write_pci_config(s->alloc, s->buid, config_addr, 2, value);
}

static void qpci_spapr_config_writel(QPCIBus *bus, int devfn, uint8_t offset,
                                     uint32_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    qrtas_ibm_write_pci_config(s->alloc, s->buid, config_addr, 4, value);
}

static void *qpci_spapr_iomap(QPCIBus *bus, QPCIDevice *dev, int barno,
                              uint64_t *sizeptr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    static const int bar_reg_map[] = {
        PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1, PCI_BASE_ADDRESS_2,
        PCI_BASE_ADDRESS_3, PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5,
    };
    int bar_reg;
    uint32_t addr;
    uint64_t size;
    uint32_t io_type;

    g_assert(barno >= 0 && barno <= 5);
    bar_reg = bar_reg_map[barno];

    qpci_config_writel(dev, bar_reg, 0xFFFFFFFF);
    addr = qpci_config_readl(dev, bar_reg);

    io_type = addr & PCI_BASE_ADDRESS_SPACE;
    if (io_type == PCI_BASE_ADDRESS_SPACE_IO) {
        addr &= PCI_BASE_ADDRESS_IO_MASK;
    } else {
        addr &= PCI_BASE_ADDRESS_MEM_MASK;
    }

    size = (1ULL << ctzl(addr));
    if (size == 0) {
        return NULL;
    }
    if (sizeptr) {
        *sizeptr = size;
    }

    if (io_type == PCI_BASE_ADDRESS_SPACE_IO) {
        uint16_t loc;

        g_assert(QEMU_ALIGN_UP(s->pci_iohole_alloc, size) + size
                 <= s->pci_iohole_size);
        s->pci_iohole_alloc = QEMU_ALIGN_UP(s->pci_iohole_alloc, size);
        loc = s->pci_iohole_start + s->pci_iohole_alloc;
        s->pci_iohole_alloc += size;

        qpci_config_writel(dev, bar_reg, loc | PCI_BASE_ADDRESS_SPACE_IO);

        return (void *)(unsigned long)loc;
    } else {
        uint64_t loc;

        g_assert(QEMU_ALIGN_UP(s->pci_hole_alloc, size) + size
                 <= s->pci_hole_size);
        s->pci_hole_alloc = QEMU_ALIGN_UP(s->pci_hole_alloc, size);
        loc = s->pci_hole_start + s->pci_hole_alloc;
        s->pci_hole_alloc += size;

        qpci_config_writel(dev, bar_reg, loc);

        return (void *)(unsigned long)loc;
    }
}

static void qpci_spapr_iounmap(QPCIBus *bus, void *data)
{
    /* FIXME */
}

#define SPAPR_PCI_BASE               (1ULL << 45)

#define SPAPR_PCI_MMIO32_WIN_SIZE    0x80000000 /* 2 GiB */
#define SPAPR_PCI_IO_WIN_SIZE        0x10000

QPCIBus *qpci_init_spapr(QGuestAllocator *alloc)
{
    QPCIBusSPAPR *ret;

    ret = g_malloc(sizeof(*ret));

    ret->alloc = alloc;

    ret->bus.pio_readb = qpci_spapr_pio_readb;
    ret->bus.pio_readw = qpci_spapr_pio_readw;
    ret->bus.pio_readl = qpci_spapr_pio_readl;

    ret->bus.pio_writeb = qpci_spapr_pio_writeb;
    ret->bus.pio_writew = qpci_spapr_pio_writew;
    ret->bus.pio_writel = qpci_spapr_pio_writel;

    ret->bus.mmio_readb = qpci_spapr_mmio32_readb;
    ret->bus.mmio_readw = qpci_spapr_mmio32_readw;
    ret->bus.mmio_readl = qpci_spapr_mmio32_readl;

    ret->bus.mmio_writeb = qpci_spapr_mmio32_writeb;
    ret->bus.mmio_writew = qpci_spapr_mmio32_writew;
    ret->bus.mmio_writel = qpci_spapr_mmio32_writel;

    ret->bus.config_readb = qpci_spapr_config_readb;
    ret->bus.config_readw = qpci_spapr_config_readw;
    ret->bus.config_readl = qpci_spapr_config_readl;

    ret->bus.config_writeb = qpci_spapr_config_writeb;
    ret->bus.config_writew = qpci_spapr_config_writew;
    ret->bus.config_writel = qpci_spapr_config_writel;

    ret->bus.iomap = qpci_spapr_iomap;
    ret->bus.iounmap = qpci_spapr_iounmap;

    /* FIXME: We assume the default location of the PHB for now.
     * Ideally we'd parse the device tree deposited in the guest to
     * get the window locations */
    ret->buid = 0x800000020000000ULL;

    ret->pio_cpu_base = SPAPR_PCI_BASE;
    ret->pio.pci_base = 0;
    ret->pio.size = SPAPR_PCI_IO_WIN_SIZE;

    /* 32-bit portion of the MMIO window is at PCI address 2..4 GiB */
    ret->mmio32_cpu_base = SPAPR_PCI_BASE + SPAPR_PCI_MMIO32_WIN_SIZE;
    ret->mmio32.pci_base = 0x80000000; /* 2 GiB */
    ret->mmio32.size = SPAPR_PCI_MMIO32_WIN_SIZE;

    ret->pci_hole_start = 0xC0000000;
    ret->pci_hole_size =
        ret->mmio32.pci_base + ret->mmio32.size - ret->pci_hole_start;
    ret->pci_hole_alloc = 0;

    ret->pci_iohole_start = 0xc000;
    ret->pci_iohole_size =
        ret->pio.pci_base + ret->pio.size - ret->pci_iohole_start;
    ret->pci_iohole_alloc = 0;

    return &ret->bus;
}

void qpci_free_spapr(QPCIBus *bus)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);

    g_free(s);
}
