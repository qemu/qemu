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
#include "../libqtest.h"
#include "qemu/module.h"
#include "libqos-malloc.h"
#include "qgraph.h"
#include "virtio-mmio.h"
#include "generic-pcihost.h"
#include "hw/pci/pci_regs.h"

#define LOONGARCH_PAGE_SIZE               0x1000
#define LOONGARCH_VIRT_RAM_ADDR           0x100000
#define LOONGARCH_VIRT_RAM_SIZE           0xFF00000

#define LOONGARCH_VIRT_PIO_BASE           0x18000000
#define LOONGARCH_VIRT_PCIE_PIO_OFFSET    0x4000
#define LOONGARCH_VIRT_PCIE_PIO_LIMIT     0x10000
#define LOONGARCH_VIRT_PCIE_ECAM_BASE     0x20000000
#define LOONGARCH_VIRT_PCIE_MMIO32_BASE   0x40000000
#define LOONGARCH_VIRT_PCIE_MMIO32_LIMIT  0x80000000

typedef struct QVirtMachine QVirtMachine;

struct QVirtMachine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    QVirtioMMIODevice virtio_mmio;
    QGenericPCIHost bridge;
};

static void virt_destructor(QOSGraphObject *obj)
{
    QVirtMachine *machine = (QVirtMachine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *virt_get_driver(void *object, const char *interface)
{
    QVirtMachine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in loongarch/virtio\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *virt_get_device(void *obj, const char *device)
{
    QVirtMachine *machine = obj;
    if (!g_strcmp0(device, "generic-pcihost")) {
        return &machine->bridge.obj;
    } else if (!g_strcmp0(device, "virtio-mmio")) {
        return &machine->virtio_mmio.obj;
    }

    fprintf(stderr, "%s not present in loongarch/virt\n", device);
    g_assert_not_reached();
}

static void loongarch_config_qpci_bus(QGenericPCIBus *qpci)
{
    qpci->gpex_pio_base = LOONGARCH_VIRT_PIO_BASE;
    qpci->bus.pio_alloc_ptr = LOONGARCH_VIRT_PCIE_PIO_OFFSET;
    qpci->bus.pio_limit = LOONGARCH_VIRT_PCIE_PIO_LIMIT;
    qpci->bus.mmio_alloc_ptr = LOONGARCH_VIRT_PCIE_MMIO32_BASE;
    qpci->bus.mmio_limit = LOONGARCH_VIRT_PCIE_MMIO32_LIMIT;
    qpci->ecam_alloc_ptr = LOONGARCH_VIRT_PCIE_ECAM_BASE;
}

static void *qos_create_machine_loongarch_virt(QTestState *qts)
{
    QVirtMachine *machine = g_new0(QVirtMachine, 1);

    alloc_init(&machine->alloc, 0,
               LOONGARCH_VIRT_RAM_ADDR,
               LOONGARCH_VIRT_RAM_ADDR + LOONGARCH_VIRT_RAM_SIZE,
               LOONGARCH_PAGE_SIZE);

    qos_create_generic_pcihost(&machine->bridge, qts, &machine->alloc);
    loongarch_config_qpci_bus(&machine->bridge.pci);

    machine->obj.get_device = virt_get_device;
    machine->obj.get_driver = virt_get_driver;
    machine->obj.destructor = virt_destructor;
    return machine;
}

static void virt_machine_register_nodes(void)
{
    qos_node_create_machine_args("loongarch64/virt",
                                 qos_create_machine_loongarch_virt,
                                 " -cpu la464");
    qos_node_contains("loongarch64/virt", "generic-pcihost", NULL);
}

libqos_init(virt_machine_register_nodes);
