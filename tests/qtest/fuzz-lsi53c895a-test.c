/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QTest fuzzer-generated testcase for LSI53C895A device
 *
 * Copyright (c) Red Hat
 */

#include "qemu/osdep.h"
#include "libqos/libqtest.h"

/*
 * This used to trigger the assert in lsi_do_dma()
 * https://bugs.launchpad.net/qemu/+bug/697510
 * https://bugs.launchpad.net/qemu/+bug/1905521
 * https://bugs.launchpad.net/qemu/+bug/1908515
 */
static void test_lsi_do_dma_empty_queue(void)
{
    QTestState *s;

    s = qtest_init("-M q35 -nographic -monitor none -serial none "
                   "-drive if=none,id=drive0,"
                            "file=null-co://,file.read-zeroes=on,format=raw "
                   "-device lsi53c895a,id=scsi0 "
                   "-device scsi-hd,drive=drive0,"
                            "bus=scsi0.0,channel=0,scsi-id=0,lun=0");
    qtest_outl(s, 0xcf8, 0x80001814);
    qtest_outl(s, 0xcfc, 0xe1068000);
    qtest_outl(s, 0xcf8, 0x80001818);
    qtest_outl(s, 0xcf8, 0x80001804);
    qtest_outw(s, 0xcfc, 0x7);
    qtest_outl(s, 0xcf8, 0x80002010);

    qtest_writeb(s, 0xe106802e, 0xff); /* Fill DSP bits 16-23 */
    qtest_writeb(s, 0xe106802f, 0xff); /* Fill DSP bits 24-31: trigger SCRIPT */

    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("fuzz/lsi53c895a/lsi_do_dma_empty_queue",
                       test_lsi_do_dma_empty_queue);
    }

    return g_test_run();
}
