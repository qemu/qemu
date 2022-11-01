/*
 * QTest fuzzer-generated testcase for xlnx-dp display device
 *
 * Copyright (c) 2021 Qiang Liu <cyruscyliu@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/*
 * This used to trigger the out-of-bounds read in xlnx_dp_read
 */
static void test_fuzz_xlnx_dp_0x3ac(void)
{
    QTestState *s = qtest_init("-M xlnx-zcu102 ");
    qtest_readl(s, 0xfd4a03ac);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

   if (strcmp(arch, "aarch64") == 0) {
        qtest_add_func("fuzz/test_fuzz_xlnx_dp/3ac", test_fuzz_xlnx_dp_0x3ac);
   }

   return g_test_run();
}
