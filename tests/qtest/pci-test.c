/*
 * QTest testcase for PCI
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void nop(void *obj, void *data, QGuestAllocator *alloc)
{
}

static void register_pci_test(void)
{
    qos_add_test("nop", "pci-device", nop, NULL);
}

libqos_init(register_pci_test);
