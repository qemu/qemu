/*
 * QTest testcase for CXL
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

static void cxl_basic_pxb(void)
{
    qtest_start("-machine q35,cxl=on -device pxb-cxl,bus=pcie.0");
    qtest_end();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/pci/cxl/basic_pxb", cxl_basic_pxb);
    return g_test_run();
}
