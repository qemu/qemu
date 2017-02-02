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

static void qpci_spapr_pio_writeb(QPCIBus *bus, uint32_t addr, uint8_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writeb(s->pio_cpu_base + addr, val);
}

static uint16_t qpci_spapr_pio_readw(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap16(readw(s->pio_cpu_base + addr));
}

static void qpci_spapr_pio_writew(QPCIBus *bus, uint32_t addr, uint16_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writew(s->pio_cpu_base + addr, bswap16(val));
}

static uint32_t qpci_spapr_pio_readl(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap32(readl(s->pio_cpu_base + addr));
}

static void qpci_spapr_pio_writel(QPCIBus *bus, uint32_t addr, uint32_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writel(s->pio_cpu_base + addr, bswap32(val));
}

static uint64_t qpci_spapr_pio_readq(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap64(readq(s->pio_cpu_base + addr));
}

static void qpci_spapr_pio_writeq(QPCIBus *bus, uint32_t addr, uint64_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    writeq(s->pio_cpu_base + addr, bswap64(val));
}

static void qpci_spapr_memread(QPCIBus *bus, uint32_t addr,
                               void *buf, size_t len)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    memread(s->mmio32_cpu_base + addr, buf, len);
}

static void qpci_spapr_memwrite(QPCIBus *bus, uint32_t addr,
                                const void *buf, size_t len)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    memwrite(s->mmio32_cpu_base + addr, buf, len);
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
    ret->bus.pio_readq = qpci_spapr_pio_readq;

    ret->bus.pio_writeb = qpci_spapr_pio_writeb;
    ret->bus.pio_writew = qpci_spapr_pio_writew;
    ret->bus.pio_writel = qpci_spapr_pio_writel;
    ret->bus.pio_writeq = qpci_spapr_pio_writeq;

    ret->bus.memread = qpci_spapr_memread;
    ret->bus.memwrite = qpci_spapr_memwrite;

    ret->bus.config_readb = qpci_spapr_config_readb;
    ret->bus.config_readw = qpci_spapr_config_readw;
    ret->bus.config_readl = qpci_spapr_config_readl;

    ret->bus.config_writeb = qpci_spapr_config_writeb;
    ret->bus.config_writew = qpci_spapr_config_writew;
    ret->bus.config_writel = qpci_spapr_config_writel;

    /* FIXME: We assume the default location of the PHB for now.
     * Ideally we'd parse the device tree deposited in the guest to
     * get the window locations */
    ret->buid = 0x800000020000000ULL;

    ret->pio_cpu_base = SPAPR_PCI_BASE;
    ret->pio.pci_base = 0;
    ret->pio.size = SPAPR_PCI_IO_WIN_SIZE;

    /* 32-bit portion of the MMIO window is at PCI address 2..4 GiB */
    ret->mmio32_cpu_base = SPAPR_PCI_BASE;
    ret->mmio32.pci_base = SPAPR_PCI_MMIO32_WIN_SIZE;
    ret->mmio32.size = SPAPR_PCI_MMIO32_WIN_SIZE;

    ret->bus.pio_alloc_ptr = 0xc000;
    ret->bus.mmio_alloc_ptr = ret->mmio32.pci_base;
    ret->bus.mmio_limit = ret->mmio32.pci_base + ret->mmio32.size;

    return &ret->bus;
}

void qpci_free_spapr(QPCIBus *bus)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);

    g_free(s);
}
