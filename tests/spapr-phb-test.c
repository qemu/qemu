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

#define TYPE_SPAPR_PCI_HOST_BRIDGE "spapr-pci-host-bridge"

/* Tests only initialization so far. TODO: Replace with functional tests */
static void test_phb_device(void)
{
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/spapr-phb/device", test_phb_device);

    qtest_start("-device " TYPE_SPAPR_PCI_HOST_BRIDGE ",index=30");

    ret = g_test_run();

    qtest_end();

    return ret;
}
