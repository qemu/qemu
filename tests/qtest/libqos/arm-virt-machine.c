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
#include "qemu/module.h"
#include "libqos/malloc.h"
#include "libqos/qgraph.h"
#include "libqos/virtio-mmio.h"

#define ARM_PAGE_SIZE               4096
#define VIRTIO_MMIO_BASE_ADDR       0x0A003E00
#define ARM_VIRT_RAM_ADDR           0x40000000
#define ARM_VIRT_RAM_SIZE           0x20000000
#define VIRTIO_MMIO_SIZE            0x00000200

typedef struct QVirtMachine QVirtMachine;

struct QVirtMachine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    QVirtioMMIODevice virtio_mmio;
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

    fprintf(stderr, "%s not present in arm/virtio\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *virt_get_device(void *obj, const char *device)
{
    QVirtMachine *machine = obj;
    if (!g_strcmp0(device, "virtio-mmio")) {
        return &machine->virtio_mmio.obj;
    }

    fprintf(stderr, "%s not present in arm/virtio\n", device);
    g_assert_not_reached();
}

static void *qos_create_machine_arm_virt(QTestState *qts)
{
    QVirtMachine *machine = g_new0(QVirtMachine, 1);

    alloc_init(&machine->alloc, 0,
               ARM_VIRT_RAM_ADDR,
               ARM_VIRT_RAM_ADDR + ARM_VIRT_RAM_SIZE,
               ARM_PAGE_SIZE);
    qvirtio_mmio_init_device(&machine->virtio_mmio, qts, VIRTIO_MMIO_BASE_ADDR,
                             VIRTIO_MMIO_SIZE);

    machine->obj.get_device = virt_get_device;
    machine->obj.get_driver = virt_get_driver;
    machine->obj.destructor = virt_destructor;
    return machine;
}

static void virtio_mmio_register_nodes(void)
{
    qos_node_create_machine("arm/virt", qos_create_machine_arm_virt);
    qos_node_contains("arm/virt", "virtio-mmio", NULL);
}

libqos_init(virtio_mmio_register_nodes);
