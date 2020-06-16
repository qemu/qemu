/*
 * libqos driver framework
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
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
#include "libqos/malloc.h"
#include "libqos/qgraph.h"
#include "libqos/i2c.h"

#define ARM_PAGE_SIZE            4096
#define IMX25_PDK_RAM_START      0x80000000
#define IMX25_PDK_RAM_END        0x88000000

typedef struct QIMX25PDKMachine QIMX25PDKMachine;

struct QIMX25PDKMachine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    IMXI2C i2c_1;
};

static void *imx25_pdk_get_driver(void *object, const char *interface)
{
    QIMX25PDKMachine *machine = object;
    if (!g_strcmp0(interface, "memory")) {
        return &machine->alloc;
    }

    fprintf(stderr, "%s not present in arm/imx25_pdk\n", interface);
    g_assert_not_reached();
}

static QOSGraphObject *imx25_pdk_get_device(void *obj, const char *device)
{
    QIMX25PDKMachine *machine = obj;
    if (!g_strcmp0(device, "imx.i2c")) {
        return &machine->i2c_1.obj;
    }

    fprintf(stderr, "%s not present in arm/imx25_pdk\n", device);
    g_assert_not_reached();
}

static void imx25_pdk_destructor(QOSGraphObject *obj)
{
    QIMX25PDKMachine *machine = (QIMX25PDKMachine *) obj;
    alloc_destroy(&machine->alloc);
}

static void *qos_create_machine_arm_imx25_pdk(QTestState *qts)
{
    QIMX25PDKMachine *machine = g_new0(QIMX25PDKMachine, 1);

    alloc_init(&machine->alloc, 0,
               IMX25_PDK_RAM_START,
               IMX25_PDK_RAM_END,
               ARM_PAGE_SIZE);
    machine->obj.get_device = imx25_pdk_get_device;
    machine->obj.get_driver = imx25_pdk_get_driver;
    machine->obj.destructor = imx25_pdk_destructor;

    imx_i2c_init(&machine->i2c_1, qts, 0x43f80000);
    return &machine->obj;
}

static void imx25_pdk_register_nodes(void)
{
    QOSGraphEdgeOptions edge = {
        .extra_device_opts = "bus=i2c-bus.0"
    };
    qos_node_create_machine("arm/imx25-pdk", qos_create_machine_arm_imx25_pdk);
    qos_node_contains("arm/imx25-pdk", "imx.i2c", &edge, NULL);
}

libqos_init(imx25_pdk_register_nodes);
