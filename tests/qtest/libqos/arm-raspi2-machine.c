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
#include "sdhci.h"

#define ARM_PAGE_SIZE             4096
#define RASPI2_RAM_ADDR           0
#define RASPI2_RAM_SIZE           0x20000000

typedef struct QRaspi2Machine QRaspi2Machine;

struct QRaspi2Machine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    QSDHCI_MemoryMapped sdhci;
};

static void *raspi2_get_driver(void *object, const char *interface)
{
    QRaspi2Machine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in arm/raspi2\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *raspi2_get_device(void *obj, const char *device)
{
    QRaspi2Machine *machine = obj;
    if (!g_strcmp0(device, "generic-sdhci")) {
        return &machine->sdhci.obj;
    }

    fprintf(stderr, "%s not present in arm/raspi2\n", device);
    g_assert_not_reached();
}

static void raspi2_destructor(QOSGraphObject *obj)
{
    QRaspi2Machine *machine = (QRaspi2Machine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *qos_create_machine_arm_raspi2(QTestState *qts)
{
    QRaspi2Machine *machine = g_new0(QRaspi2Machine, 1);

    alloc_init(&machine->alloc, 0,
               RASPI2_RAM_ADDR + (1 << 20),
               RASPI2_RAM_ADDR + RASPI2_RAM_SIZE,
               ARM_PAGE_SIZE);
    machine->obj.get_device = raspi2_get_device;
    machine->obj.get_driver = raspi2_get_driver;
    machine->obj.destructor = raspi2_destructor;
    qos_init_sdhci_mm(&machine->sdhci, qts, 0x3f300000, &(QSDHCIProperties) {
        .version = 3,
        .baseclock = 52,
        .capab.sdma = false,
        .capab.reg = 0x052134b4
    });
    return &machine->obj;
}

static void raspi2_register_nodes(void)
{
    qos_node_create_machine("arm/raspi2", qos_create_machine_arm_raspi2);
    qos_node_contains("arm/raspi2", "generic-sdhci", NULL);
}

libqos_init(raspi2_register_nodes);
