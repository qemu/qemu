/*
 * QTest testcase for SPAPR PHB
 *
 * Authors:
 *  Alexey Kardashevskiy <aik@ozlabs.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"

/* Tests only initialization so far. TODO: Replace with functional tests,
 * for example by producing pci-bus.
 */
static void test_phb_device(void *obj, void *data, QGuestAllocator *alloc)
{
}

static void register_phb_test(void)
{
    qos_add_test("spapr-phb-test", "ppc64/pseries",
                 test_phb_device, &(QOSGraphTestOptions) {
                     .edge.before_cmd_line = "-device spapr-pci-host-bridge"
                                             ",index=30",
                 });
}

libqos_init(register_phb_test);
