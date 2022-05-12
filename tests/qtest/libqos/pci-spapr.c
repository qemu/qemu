/*
 * libqos PCI bindings for SPAPR
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "../libqtest.h"
#include "pci-spapr.h"
#include "rtas.h"
#include "qgraph.h"

#include "hw/pci/pci_regs.h"

#include "qemu/host-utils.h"
#include "qemu/module.h"

/*
 * PCI devices are always little-endian
 * SPAPR by default is big-endian
 * so PCI accessors need to swap data endianness
 */

static uint8_t qpci_spapr_pio_readb(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return qtest_readb(bus->qts, s->pio_cpu_base + addr);
}

static void qpci_spapr_pio_writeb(QPCIBus *bus, uint32_t addr, uint8_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    qtest_writeb(bus->qts, s->pio_cpu_base + addr, val);
}

static uint16_t qpci_spapr_pio_readw(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap16(qtest_readw(bus->qts, s->pio_cpu_base + addr));
}

static void qpci_spapr_pio_writew(QPCIBus *bus, uint32_t addr, uint16_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    qtest_writew(bus->qts, s->pio_cpu_base + addr, bswap16(val));
}

static uint32_t qpci_spapr_pio_readl(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap32(qtest_readl(bus->qts, s->pio_cpu_base + addr));
}

static void qpci_spapr_pio_writel(QPCIBus *bus, uint32_t addr, uint32_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    qtest_writel(bus->qts, s->pio_cpu_base + addr, bswap32(val));
}

static uint64_t qpci_spapr_pio_readq(QPCIBus *bus, uint32_t addr)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    return bswap64(qtest_readq(bus->qts, s->pio_cpu_base + addr));
}

static void qpci_spapr_pio_writeq(QPCIBus *bus, uint32_t addr, uint64_t val)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    qtest_writeq(bus->qts, s->pio_cpu_base + addr, bswap64(val));
}

static void qpci_spapr_memread(QPCIBus *bus, uint32_t addr,
                               void *buf, size_t len)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    qtest_memread(bus->qts, s->mmio32_cpu_base + addr, buf, len);
}

static void qpci_spapr_memwrite(QPCIBus *bus, uint32_t addr,
                                const void *buf, size_t len)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    qtest_memwrite(bus->qts, s->mmio32_cpu_base + addr, buf, len);
}

static uint8_t qpci_spapr_config_readb(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    return qrtas_ibm_read_pci_config(bus->qts, s->alloc, s->buid,
                                     config_addr, 1);
}

static uint16_t qpci_spapr_config_readw(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    return qrtas_ibm_read_pci_config(bus->qts, s->alloc, s->buid,
                                     config_addr, 2);
}

static uint32_t qpci_spapr_config_readl(QPCIBus *bus, int devfn, uint8_t offset)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    return qrtas_ibm_read_pci_config(bus->qts, s->alloc, s->buid,
                                     config_addr, 4);
}

static void qpci_spapr_config_writeb(QPCIBus *bus, int devfn, uint8_t offset,
                                     uint8_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    qrtas_ibm_write_pci_config(bus->qts, s->alloc, s->buid,
                               config_addr, 1, value);
}

static void qpci_spapr_config_writew(QPCIBus *bus, int devfn, uint8_t offset,
                                     uint16_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    qrtas_ibm_write_pci_config(bus->qts, s->alloc, s->buid,
                               config_addr, 2, value);
}

static void qpci_spapr_config_writel(QPCIBus *bus, int devfn, uint8_t offset,
                                     uint32_t value)
{
    QPCIBusSPAPR *s = container_of(bus, QPCIBusSPAPR, bus);
    uint32_t config_addr = (devfn << 8) | offset;
    qrtas_ibm_write_pci_config(bus->qts, s->alloc, s->buid,
                               config_addr, 4, value);
}

#define SPAPR_PCI_BASE               (1ULL << 45)

#define SPAPR_PCI_MMIO32_WIN_SIZE    0x80000000 /* 2 GiB */
#define SPAPR_PCI_IO_WIN_SIZE        0x10000

static void *qpci_spapr_get_driver(void *obj, const char *interface)
{
    QPCIBusSPAPR *qpci = obj;
    if (!g_strcmp0(interface, "pci-bus")) {
        return &qpci->bus;
    }
    fprintf(stderr, "%s not present in pci-bus-spapr", interface);
    g_assert_not_reached();
}

void qpci_init_spapr(QPCIBusSPAPR *qpci, QTestState *qts,
                     QGuestAllocator *alloc)
{
    assert(qts);

    /* tests cannot use spapr, needs to be fixed first */
    qpci->bus.has_buggy_msi = true;

    qpci->alloc = alloc;

    qpci->bus.pio_readb = qpci_spapr_pio_readb;
    qpci->bus.pio_readw = qpci_spapr_pio_readw;
    qpci->bus.pio_readl = qpci_spapr_pio_readl;
    qpci->bus.pio_readq = qpci_spapr_pio_readq;

    qpci->bus.pio_writeb = qpci_spapr_pio_writeb;
    qpci->bus.pio_writew = qpci_spapr_pio_writew;
    qpci->bus.pio_writel = qpci_spapr_pio_writel;
    qpci->bus.pio_writeq = qpci_spapr_pio_writeq;

    qpci->bus.memread = qpci_spapr_memread;
    qpci->bus.memwrite = qpci_spapr_memwrite;

    qpci->bus.config_readb = qpci_spapr_config_readb;
    qpci->bus.config_readw = qpci_spapr_config_readw;
    qpci->bus.config_readl = qpci_spapr_config_readl;

    qpci->bus.config_writeb = qpci_spapr_config_writeb;
    qpci->bus.config_writew = qpci_spapr_config_writew;
    qpci->bus.config_writel = qpci_spapr_config_writel;

    /* FIXME: We assume the default location of the PHB for now.
     * Ideally we'd parse the device tree deposited in the guest to
     * get the window locations */
    qpci->buid = 0x800000020000000ULL;

    qpci->pio_cpu_base = SPAPR_PCI_BASE;
    qpci->pio.pci_base = 0;
    qpci->pio.size = SPAPR_PCI_IO_WIN_SIZE;

    /* 32-bit portion of the MMIO window is at PCI address 2..4 GiB */
    qpci->mmio32_cpu_base = SPAPR_PCI_BASE;
    qpci->mmio32.pci_base = SPAPR_PCI_MMIO32_WIN_SIZE;
    qpci->mmio32.size = SPAPR_PCI_MMIO32_WIN_SIZE;

    qpci->bus.qts = qts;
    qpci->bus.pio_alloc_ptr = 0xc000;
    qpci->bus.pio_limit = 0x10000;
    qpci->bus.mmio_alloc_ptr = qpci->mmio32.pci_base;
    qpci->bus.mmio_limit = qpci->mmio32.pci_base + qpci->mmio32.size;

    qpci->obj.get_driver = qpci_spapr_get_driver;
}

QPCIBus *qpci_new_spapr(QTestState *qts, QGuestAllocator *alloc)
{
    QPCIBusSPAPR *qpci = g_new0(QPCIBusSPAPR, 1);
    qpci_init_spapr(qpci, qts, alloc);

    return &qpci->bus;
}

void qpci_free_spapr(QPCIBus *bus)
{
    QPCIBusSPAPR *s;

    if (!bus) {
        return;
    }
    s = container_of(bus, QPCIBusSPAPR, bus);

    g_free(s);
}

static void qpci_spapr_register_nodes(void)
{
    qos_node_create_driver("pci-bus-spapr", NULL);
    qos_node_produces("pci-bus-spapr", "pci-bus");
}

libqos_init(qpci_spapr_register_nodes);
