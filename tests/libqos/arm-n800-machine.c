/*
 * libqos driver framework
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
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
#include "libqos/malloc.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"

#define ARM_PAGE_SIZE            4096
#define N800_RAM_START      0x80000000
#define N800_RAM_END        0x88000000

typedef struct QN800Machine QN800Machine;

struct QN800Machine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    OMAPI2C i2c_1;
};

static void *n800_get_driver(void *object, const char *interface)
{
    QN800Machine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in arm/n800\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *n800_get_device(void *obj, const char *device)
{
    QN800Machine *machine = obj;
    if (!g_strcmp0(device, "omap_i2c")) {
        return &machine->i2c_1.obj;
    }

    fprintf(stderr, "%s not present in arm/n800\n", device);
    g_assert_not_reached();
}

static void n800_destructor(QOSGraphObject *obj)
{
    QN800Machine *machine = (QN800Machine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *qos_create_machine_arm_n800(QTestState *qts)
{
    QN800Machine *machine = g_new0(QN800Machine, 1);

    alloc_init(&machine->alloc, 0,
               N800_RAM_START,
               N800_RAM_END,
               ARM_PAGE_SIZE);
    machine->obj.get_device = n800_get_device;
    machine->obj.get_driver = n800_get_driver;
    machine->obj.destructor = n800_destructor;

    omap_i2c_init(&machine->i2c_1, qts, 0x48070000);
    return &machine->obj;
}

static void n800_register_nodes(void)
{
    QOSGraphEdgeOptions edge = {
        .extra_device_opts = "bus=i2c-bus.0"
    };
    qos_node_create_machine("arm/n800", qos_create_machine_arm_n800);
    qos_node_contains("arm/n800", "omap_i2c", &edge, NULL);
}

libqos_init(n800_register_nodes);
