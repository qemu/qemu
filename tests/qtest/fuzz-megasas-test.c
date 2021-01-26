/*
 * QTest fuzzer-generated testcase for megasas device
 *
 * Copyright (c) 2020 Li Qiang <liq3ea@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqos/libqtest.h"

/*
 * This used to trigger the assert in scsi_dma_complete
 * https://bugs.launchpad.net/qemu/+bug/1878263
 */
static void test_lp1878263_megasas_zero_iov_cnt(void)
{
    QTestState *s;

    s = qtest_init("-nographic -monitor none -serial none "
                   "-M q35 -device megasas -device scsi-cd,drive=null0 "
                   "-blockdev driver=null-co,read-zeroes=on,node-name=null0");
    qtest_outl(s, 0xcf8, 0x80001818);
    qtest_outl(s, 0xcfc, 0xc101);
    qtest_outl(s, 0xcf8, 0x8000181c);
    qtest_outl(s, 0xcf8, 0x80001804);
    qtest_outw(s, 0xcfc, 0x7);
    qtest_outl(s, 0xcf8, 0x8000186a);
    qtest_writeb(s, 0x14, 0xfe);
    qtest_writeb(s, 0x0, 0x02);
    qtest_outb(s, 0xc1c0, 0x17);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("fuzz/test_lp1878263_megasas_zero_iov_cnt",
                       test_lp1878263_megasas_zero_iov_cnt);
    }

    return g_test_run();
}
