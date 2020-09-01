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
#include "pci-pc.h"
#include "qemu/module.h"
#include "malloc-pc.h"

typedef struct QX86PCMachine QX86PCMachine;
typedef struct i440FX_pcihost i440FX_pcihost;
typedef struct QSDHCI_PCI  QSDHCI_PCI;

struct i440FX_pcihost {
    QOSGraphObject obj;
    QPCIBusPC pci;
};

struct QX86PCMachine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    i440FX_pcihost bridge;
};

/* i440FX_pcihost */

static QOSGraphObject *i440FX_host_get_device(void *obj, const char *device)
{
    i440FX_pcihost *host = obj;
    if (!g_strcmp0(device, "pci-bus-pc")) {
        return &host->pci.obj;
    }
    fprintf(stderr, "%s not present in i440FX-pcihost\n", device);
    g_assert_not_reached();
}

static void qos_create_i440FX_host(i440FX_pcihost *host,
                                   QTestState *qts,
                                   QGuestAllocator *alloc)
{
    host->obj.get_device = i440FX_host_get_device;
    qpci_init_pc(&host->pci, qts, alloc);
}

/* x86_64/pc machine */

static void pc_destructor(QOSGraphObject *obj)
{
    QX86PCMachine *machine = (QX86PCMachine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *pc_get_driver(void *object, const char *interface)
{
    QX86PCMachine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in x86_64/pc\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *pc_get_device(void *obj, const char *device)
{
    QX86PCMachine *machine = obj;
    if (!g_strcmp0(device, "i440FX-pcihost")) {
        return &machine->bridge.obj;
    }

    fprintf(stderr, "%s not present in x86_64/pc\n", device);
    g_assert_not_reached();
}

static void *qos_create_machine_pc(QTestState *qts)
{
    QX86PCMachine *machine = g_new0(QX86PCMachine, 1);
    machine->obj.get_device = pc_get_device;
    machine->obj.get_driver = pc_get_driver;
    machine->obj.destructor = pc_destructor;
    pc_alloc_init(&machine->alloc, qts, ALLOC_NO_FLAGS);
    qos_create_i440FX_host(&machine->bridge, qts, &machine->alloc);

    return &machine->obj;
}

static void pc_machine_register_nodes(void)
{
    qos_node_create_machine("i386/pc", qos_create_machine_pc);
    qos_node_contains("i386/pc", "i440FX-pcihost", NULL);

    qos_node_create_machine("x86_64/pc", qos_create_machine_pc);
    qos_node_contains("x86_64/pc", "i440FX-pcihost", NULL);

    qos_node_create_driver("i440FX-pcihost", NULL);
    qos_node_contains("i440FX-pcihost", "pci-bus-pc", NULL);
}

libqos_init(pc_machine_register_nodes);
