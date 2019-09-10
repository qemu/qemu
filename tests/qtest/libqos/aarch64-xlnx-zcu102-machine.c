/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
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
#include "sdhci.h"

typedef struct QXlnxZCU102Machine QXlnxZCU102Machine;

struct QXlnxZCU102Machine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    QSDHCI_MemoryMapped sdhci;
};

#define ARM_PAGE_SIZE          4096
#define XLNX_ZCU102_RAM_ADDR   0
#define XLNX_ZCU102_RAM_SIZE   0x20000000

static void *xlnx_zcu102_get_driver(void *object, const char *interface)
{
    QXlnxZCU102Machine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in aarch64/xlnx-zcu102\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *xlnx_zcu102_get_device(void *obj, const char *device)
{
    QXlnxZCU102Machine *machine = obj;
    if (!g_strcmp0(device, "generic-sdhci")) {
        return &machine->sdhci.obj;
    }

    fprintf(stderr, "%s not present in aarch64/xlnx-zcu102\n", device);
    g_assert_not_reached();
}

static void xlnx_zcu102_destructor(QOSGraphObject *obj)
{
    QXlnxZCU102Machine *machine = (QXlnxZCU102Machine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *qos_create_machine_aarch64_xlnx_zcu102(QTestState *qts)
{
    QXlnxZCU102Machine *machine = g_new0(QXlnxZCU102Machine, 1);

    alloc_init(&machine->alloc, 0,
               XLNX_ZCU102_RAM_ADDR + (1 << 20),
               XLNX_ZCU102_RAM_ADDR + XLNX_ZCU102_RAM_SIZE,
               ARM_PAGE_SIZE);

    machine->obj.get_device = xlnx_zcu102_get_device;
    machine->obj.get_driver = xlnx_zcu102_get_driver;
    machine->obj.destructor = xlnx_zcu102_destructor;
    /* Datasheet: UG1085 (v1.7) */
    qos_init_sdhci_mm(&machine->sdhci, qts, 0xff160000, &(QSDHCIProperties) {
        .version = 3,
        .baseclock = 0,
        .capab.sdma = true,
        .capab.reg = 0x280737ec6481
    });
    return &machine->obj;
}

static void xlnx_zcu102_register_nodes(void)
{
    qos_node_create_machine("aarch64/xlnx-zcu102",
                            qos_create_machine_aarch64_xlnx_zcu102);
    qos_node_contains("aarch64/xlnx-zcu102", "generic-sdhci", NULL);
}

libqos_init(xlnx_zcu102_register_nodes);
