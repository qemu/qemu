/*
 * libqos driver framework for risc-v
 *
 * Initial version based on arm-virt-machine.c
 *
 * Copyright (c) 2024 Ventana Micro
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

#define RISCV_PAGE_SIZE            4096

/* VIRT_DRAM */
#define RISCV_VIRT_RAM_ADDR        0x80000000
#define RISCV_VIRT_RAM_SIZE        0x20000000

/*
 * VIRT_VIRTIO. BASE_ADDR  points to the last
 * virtio_mmio device.
 */
#define VIRTIO_MMIO_BASE_ADDR      0x10008000
#define VIRTIO_MMIO_SIZE           0x00001000

/* VIRT_PCIE_PIO  */
#define RISCV_GPEX_PIO_BASE        0x3000000
#define RISCV_BUS_PIO_LIMIT        0x10000

/* VIRT_PCIE_MMIO */
#define RISCV_BUS_MMIO_ALLOC_PTR   0x40000000
#define RISCV_BUS_MMIO_LIMIT       0x80000000

/* VIRT_PCIE_ECAM */
#define RISCV_ECAM_ALLOC_PTR   0x30000000

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

    fprintf(stderr, "%s not present in riscv/virtio\n", interface);
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

    fprintf(stderr, "%s not present in riscv/virt\n", device);
    g_assert_not_reached();
}

static void riscv_config_qpci_bus(QGenericPCIBus *qpci)
{
    qpci->gpex_pio_base = RISCV_GPEX_PIO_BASE;
    qpci->bus.pio_limit = RISCV_BUS_PIO_LIMIT;

    qpci->bus.mmio_alloc_ptr = RISCV_BUS_MMIO_ALLOC_PTR;
    qpci->bus.mmio_limit = RISCV_BUS_MMIO_LIMIT;

    qpci->ecam_alloc_ptr = RISCV_ECAM_ALLOC_PTR;
}

static void *qos_create_machine_riscv_virt(QTestState *qts)
{
    QVirtMachine *machine = g_new0(QVirtMachine, 1);

    alloc_init(&machine->alloc, 0,
               RISCV_VIRT_RAM_ADDR,
               RISCV_VIRT_RAM_ADDR + RISCV_VIRT_RAM_SIZE,
               RISCV_PAGE_SIZE);
    qvirtio_mmio_init_device(&machine->virtio_mmio, qts, VIRTIO_MMIO_BASE_ADDR,
                              VIRTIO_MMIO_SIZE);

    qos_create_generic_pcihost(&machine->bridge, qts, &machine->alloc);
    riscv_config_qpci_bus(&machine->bridge.pci);

    machine->obj.get_device = virt_get_device;
    machine->obj.get_driver = virt_get_driver;
    machine->obj.destructor = virt_destructor;
    return machine;
}

static void virt_machine_register_nodes(void)
{
    qos_node_create_machine_args("riscv32/virt", qos_create_machine_riscv_virt,
                                 "aclint=on,aia=aplic-imsic");
    qos_node_contains("riscv32/virt", "virtio-mmio", NULL);
    qos_node_contains("riscv32/virt", "generic-pcihost", NULL);

    qos_node_create_machine_args("riscv64/virt", qos_create_machine_riscv_virt,
                                 "aclint=on,aia=aplic-imsic");
    qos_node_contains("riscv64/virt", "virtio-mmio", NULL);
    qos_node_contains("riscv64/virt", "generic-pcihost", NULL);
}

libqos_init(virt_machine_register_nodes);
