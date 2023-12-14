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
#include "sdhci.h"

#define ARM_PAGE_SIZE             4096
#define SMDKC210_RAM_ADDR         0x40000000ull
#define SMDKC210_RAM_SIZE         0x40000000ull

typedef struct QSmdkc210Machine QSmdkc210Machine;

struct QSmdkc210Machine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    QSDHCI_MemoryMapped sdhci;
};

static void *smdkc210_get_driver(void *object, const char *interface)
{
    QSmdkc210Machine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in arm/smdkc210\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *smdkc210_get_device(void *obj, const char *device)
{
    QSmdkc210Machine *machine = obj;
    if (!g_strcmp0(device, "generic-sdhci")) {
        return &machine->sdhci.obj;
    }

    fprintf(stderr, "%s not present in arm/smdkc210\n", device);
    g_assert_not_reached();
}

static void smdkc210_destructor(QOSGraphObject *obj)
{
    QSmdkc210Machine *machine = (QSmdkc210Machine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *qos_create_machine_arm_smdkc210(QTestState *qts)
{
    QSmdkc210Machine *machine = g_new0(QSmdkc210Machine, 1);

    alloc_init(&machine->alloc, 0,
               SMDKC210_RAM_ADDR,
               SMDKC210_RAM_ADDR + SMDKC210_RAM_SIZE,
               ARM_PAGE_SIZE);
    machine->obj.get_device = smdkc210_get_device;
    machine->obj.get_driver = smdkc210_get_driver;
    machine->obj.destructor = smdkc210_destructor;
    qos_init_sdhci_mm(&machine->sdhci, qts, 0x12510000, &(QSDHCIProperties) {
        .version = 2,
        .baseclock = 0,
        .capab.sdma = true,
        .capab.reg = 0x5e80080,
    });
    return &machine->obj;
}

static void smdkc210_register_nodes(void)
{
    qos_node_create_machine("arm/smdkc210", qos_create_machine_arm_smdkc210);
    qos_node_contains("arm/smdkc210", "generic-sdhci", NULL);
}

libqos_init(smdkc210_register_nodes);
