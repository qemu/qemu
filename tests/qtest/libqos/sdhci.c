/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qgraph.h"
#include "pci.h"
#include "qemu/module.h"
#include "sdhci.h"
#include "hw/pci/pci.h"

static void set_qsdhci_fields(QSDHCI *s, uint8_t version, uint8_t baseclock,
                              bool sdma, uint64_t reg)
{
    s->props.version = version;
    s->props.baseclock = baseclock;
    s->props.capab.sdma = sdma;
    s->props.capab.reg = reg;
}

/* Memory mapped implementation of QSDHCI */

static uint16_t sdhci_mm_readw(QSDHCI *s, uint32_t reg)
{
    QSDHCI_MemoryMapped *smm = container_of(s, QSDHCI_MemoryMapped, sdhci);
    return qtest_readw(smm->qts, smm->addr + reg);
}

static uint64_t sdhci_mm_readq(QSDHCI *s, uint32_t reg)
{
    QSDHCI_MemoryMapped *smm = container_of(s, QSDHCI_MemoryMapped, sdhci);
    return qtest_readq(smm->qts, smm->addr + reg);
}

static void sdhci_mm_writeq(QSDHCI *s, uint32_t reg, uint64_t val)
{
    QSDHCI_MemoryMapped *smm = container_of(s, QSDHCI_MemoryMapped, sdhci);
    qtest_writeq(smm->qts, smm->addr + reg, val);
}

static void *sdhci_mm_get_driver(void *obj, const char *interface)
{
    QSDHCI_MemoryMapped *smm = obj;
    if (!g_strcmp0(interface, "sdhci")) {
        return &smm->sdhci;
    }
    fprintf(stderr, "%s not present in generic-sdhci\n", interface);
    g_assert_not_reached();
}

void qos_init_sdhci_mm(QSDHCI_MemoryMapped *sdhci, QTestState *qts,
                       uint32_t addr, QSDHCIProperties *common)
{
    sdhci->obj.get_driver = sdhci_mm_get_driver;
    sdhci->sdhci.readw = sdhci_mm_readw;
    sdhci->sdhci.readq = sdhci_mm_readq;
    sdhci->sdhci.writeq = sdhci_mm_writeq;
    memcpy(&sdhci->sdhci.props, common, sizeof(QSDHCIProperties));
    sdhci->addr = addr;
    sdhci->qts = qts;
}

/* PCI implementation of QSDHCI */

static uint16_t sdhci_pci_readw(QSDHCI *s, uint32_t reg)
{
    QSDHCI_PCI *spci = container_of(s, QSDHCI_PCI, sdhci);
    return qpci_io_readw(&spci->dev, spci->mem_bar, reg);
}

static uint64_t sdhci_pci_readq(QSDHCI *s, uint32_t reg)
{
    QSDHCI_PCI *spci = container_of(s, QSDHCI_PCI, sdhci);
    return qpci_io_readq(&spci->dev, spci->mem_bar, reg);
}

static void sdhci_pci_writeq(QSDHCI *s, uint32_t reg, uint64_t val)
{
    QSDHCI_PCI *spci = container_of(s, QSDHCI_PCI, sdhci);
    return qpci_io_writeq(&spci->dev, spci->mem_bar, reg, val);
}

static void *sdhci_pci_get_driver(void *object, const char *interface)
{
    QSDHCI_PCI *spci = object;
    if (!g_strcmp0(interface, "sdhci")) {
        return &spci->sdhci;
    }

    fprintf(stderr, "%s not present in sdhci-pci\n", interface);
    g_assert_not_reached();
}

static void sdhci_pci_start_hw(QOSGraphObject *obj)
{
    QSDHCI_PCI *spci = (QSDHCI_PCI *)obj;
    qpci_device_enable(&spci->dev);
}

static void sdhci_destructor(QOSGraphObject *obj)
{
    QSDHCI_PCI *spci = (QSDHCI_PCI *)obj;
    qpci_iounmap(&spci->dev, spci->mem_bar);
}

static void *sdhci_pci_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QSDHCI_PCI *spci = g_new0(QSDHCI_PCI, 1);
    QPCIBus *bus = pci_bus;
    uint64_t barsize;

    qpci_device_init(&spci->dev, bus, addr);
    spci->mem_bar = qpci_iomap(&spci->dev, 0, &barsize);
    spci->sdhci.readw = sdhci_pci_readw;
    spci->sdhci.readq = sdhci_pci_readq;
    spci->sdhci.writeq = sdhci_pci_writeq;
    set_qsdhci_fields(&spci->sdhci, 2, 0, 1, 0x057834b4);

    spci->obj.get_driver = sdhci_pci_get_driver;
    spci->obj.start_hw = sdhci_pci_start_hw;
    spci->obj.destructor = sdhci_destructor;
    return &spci->obj;
}

static void qsdhci_register_nodes(void)
{
    QPCIAddress addr = {
        .devfn = QPCI_DEVFN(4, 0),
        .vendor_id = PCI_VENDOR_ID_REDHAT,
        .device_id = PCI_DEVICE_ID_REDHAT_SDHCI,
    };

    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0",
    };

    /* generic-sdhci */
    qos_node_create_driver("generic-sdhci", NULL);
    qos_node_produces("generic-sdhci", "sdhci");

    /* sdhci-pci */
    add_qpci_address(&opts, &addr);
    qos_node_create_driver("sdhci-pci", sdhci_pci_create);
    qos_node_produces("sdhci-pci", "sdhci");
    qos_node_consumes("sdhci-pci", "pci-bus", &opts);

}

libqos_init(qsdhci_register_nodes);
