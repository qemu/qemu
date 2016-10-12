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

    uint64_t mmio_cpu_base;
    QPCIWindow mmio;

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

static uint8_t qpci_spapr_io_readb(QPCIBus *bus, void *addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint64_t port = (uintptr_t)addr;
    uint8_t v;
    if (port < s->pio.size) {
        v = readb(s->pio_cpu_base + port);
    } else {
        v = readb(s->mmio_cpu_base + port);
    }
    return v;
}

static uint16_t qpci_spapr_io_readw(QPCIBus *bus, void *addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint64_t port = (uintptr_t)addr;
    uint16_t v;
    if (port < s->pio.size) {
        v = readw(s->pio_cpu_base + port);
    } else {
        v = readw(s->mmio_cpu_base + port);
    }
    return bswap16(v);
}

static uint32_t qpci_spapr_io_readl(QPCIBus *bus, void *addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint64_t port = (uintptr_t)addr;
    uint32_t v;
    if (port < s->pio.size) {
        v = readl(s->pio_cpu_base + port);
    } else {
        v = readl(s->mmio_cpu_base + port);
    }
    return bswap32(v);
}

static void qpci_spapr_io_writeb(QPCIBus *bus, void *addr, uint8_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint64_t port = (uintptr_t)addr;
    if (port < s->pio.size) {
        writeb(s->pio_cpu_base + port, value);
    } else {
        writeb(s->mmio_cpu_base + port, value);
    }
}

static void qpci_spapr_io_writew(QPCIBus *bus, void *addr, uint16_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint64_t port = (uintptr_t)addr;
    value = bswap16(value);
    if (port < s->pio.size) {
        writew(s->pio_cpu_base + port, value);
    } else {
        writew(s->mmio_cpu_base + port, value);
    }
}

static void qpci_spapr_io_writel(QPCIBus *bus, void *addr, uint32_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint64_t port = (uintptr_t)addr;
    value = bswap32(value);
    if (port < s->pio.size) {
        writel(s->pio_cpu_base + port, value);
    } else {
        writel(s->mmio_cpu_base + port, value);
    }
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

#define SPAPR_PCI_MEM_WIN_BUS_OFFSET 0x80000000ULL
#define SPAPR_PCI_WINDOW_BASE        0x10000000000ULL
#define SPAPR_PCI_WINDOW_SPACING     0x1000000000ULL
#define SPAPR_PCI_MMIO_WIN_OFF       0xA0000000
#define SPAPR_PCI_MMIO_WIN_SIZE      (SPAPR_PCI_WINDOW_SPACING - \
                                     SPAPR_PCI_MEM_WIN_BUS_OFFSET)
#define SPAPR_PCI_IO_WIN_OFF         0x80000000
#define SPAPR_PCI_IO_WIN_SIZE        0x10000

QPCIBus *qpci_init_spapr(QGuestAllocator *alloc)
{
    QPCIBusSPAPR *ret;

    ret = g_malloc(sizeof(*ret));

    ret->alloc = alloc;

    ret->bus.io_readb = qpci_spapr_io_readb;
    ret->bus.io_readw = qpci_spapr_io_readw;
    ret->bus.io_readl = qpci_spapr_io_readl;

    ret->bus.io_writeb = qpci_spapr_io_writeb;
    ret->bus.io_writew = qpci_spapr_io_writew;
    ret->bus.io_writel = qpci_spapr_io_writel;

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

    ret->pio_cpu_base = SPAPR_PCI_WINDOW_BASE + SPAPR_PCI_IO_WIN_OFF;
    ret->pio.pci_base = 0;
    ret->pio.size = SPAPR_PCI_IO_WIN_SIZE;

    ret->mmio_cpu_base = SPAPR_PCI_WINDOW_BASE + SPAPR_PCI_MMIO_WIN_OFF;
    ret->mmio.pci_base = SPAPR_PCI_MEM_WIN_BUS_OFFSET;
    ret->mmio.size = SPAPR_PCI_MMIO_WIN_SIZE;

    ret->pci_hole_start = 0xC0000000;
    ret->pci_hole_size =
        ret->mmio.pci_base + ret->mmio.size - ret->pci_hole_start;
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
