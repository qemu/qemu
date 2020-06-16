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

typedef struct QXilinxZynqA9Machine QXilinxZynqA9Machine;

struct QXilinxZynqA9Machine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    QSDHCI_MemoryMapped sdhci;
};

#define ARM_PAGE_SIZE             4096
#define XILINX_ZYNQ_A9_RAM_ADDR   0
#define XILINX_ZYNQ_A9_RAM_SIZE   0x20000000

static void *xilinx_zynq_a9_get_driver(void *object, const char *interface)
{
    QXilinxZynqA9Machine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in arm/xilinx-zynq-a9\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *xilinx_zynq_a9_get_device(void *obj, const char *device)
{
    QXilinxZynqA9Machine *machine = obj;
    if (!g_strcmp0(device, "generic-sdhci")) {
        return &machine->sdhci.obj;
    }

    fprintf(stderr, "%s not present in arm/xilinx-zynq-a9\n", device);
    g_assert_not_reached();
}

static void xilinx_zynq_a9_destructor(QOSGraphObject *obj)
{
    QXilinxZynqA9Machine *machine = (QXilinxZynqA9Machine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *qos_create_machine_arm_xilinx_zynq_a9(QTestState *qts)
{
    QXilinxZynqA9Machine *machine = g_new0(QXilinxZynqA9Machine, 1);

    alloc_init(&machine->alloc, 0,
               XILINX_ZYNQ_A9_RAM_ADDR + (1 << 20),
               XILINX_ZYNQ_A9_RAM_ADDR + XILINX_ZYNQ_A9_RAM_SIZE,
               ARM_PAGE_SIZE);

    machine->obj.get_device = xilinx_zynq_a9_get_device;
    machine->obj.get_driver = xilinx_zynq_a9_get_driver;
    machine->obj.destructor = xilinx_zynq_a9_destructor;
    /* Datasheet: UG585 (v1.12.1) */
    qos_init_sdhci_mm(&machine->sdhci, qts, 0xe0100000, &(QSDHCIProperties) {
        .version = 2,
        .baseclock = 0,
        .capab.sdma = true,
        .capab.reg = 0x69ec0080,
    });
    return &machine->obj;
}

static void xilinx_zynq_a9_register_nodes(void)
{
    qos_node_create_machine("arm/xilinx-zynq-a9",
                            qos_create_machine_arm_xilinx_zynq_a9);
    qos_node_contains("arm/xilinx-zynq-a9", "generic-sdhci", NULL);
}

libqos_init(xilinx_zynq_a9_register_nodes);
