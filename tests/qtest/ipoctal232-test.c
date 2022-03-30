/*
 * QTest testcase for IndustryPack Octal-RS232
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"

typedef struct QIpoctal232 QIpoctal232;

struct QIpoctal232 {
    QOSGraphObject obj;
};

/* Tests only initialization so far. TODO: Replace with functional tests */
static void nop(void *obj, void *data, QGuestAllocator *alloc)
{
}

static void *ipoctal232_create(void *pci_bus, QGuestAllocator *alloc,
                               void *addr)
{
    QIpoctal232 *ipoctal232 = g_new0(QIpoctal232, 1);

    return &ipoctal232->obj;
}

static void ipoctal232_register_nodes(void)
{
    qos_node_create_driver("ipoctal232", ipoctal232_create);
    qos_node_consumes("ipoctal232", "ipack", &(QOSGraphEdgeOptions) {
        .extra_device_opts = "bus=ipack0.0",
    });
}

libqos_init(ipoctal232_register_nodes);

static void register_ipoctal232_test(void)
{
    qos_add_test("nop", "ipoctal232", nop, NULL);
}

libqos_init(register_ipoctal232_test);
