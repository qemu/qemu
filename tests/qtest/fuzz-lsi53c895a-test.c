/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QTest fuzzer-generated testcase for LSI53C895A device
 *
 * Copyright (c) Red Hat
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/*
 * This used to trigger a DMA reentrancy issue
 * leading to memory corruption bugs like stack
 * overflow or use-after-free
 * https://gitlab.com/qemu-project/qemu/-/issues/1563
 */
static void test_lsi_dma_reentrancy(void)
{
    QTestState *s;

    s = qtest_init("-M q35 -m 512M -nodefaults "
                   "-blockdev driver=null-co,node-name=null0 "
                   "-device lsi53c810 -device scsi-cd,drive=null0");

    qtest_outl(s, 0xcf8, 0x80000804); /* PCI Command Register */
    qtest_outw(s, 0xcfc, 0x7);        /* Enables accesses */
    qtest_outl(s, 0xcf8, 0x80000814); /* Memory Bar 1 */
    qtest_outl(s, 0xcfc, 0xff100000); /* Set MMIO Address*/
    qtest_outl(s, 0xcf8, 0x80000818); /* Memory Bar 2 */
    qtest_outl(s, 0xcfc, 0xff000000); /* Set RAM Address*/
    qtest_writel(s, 0xff000000, 0xc0000024);
    qtest_writel(s, 0xff000114, 0x00000080);
    qtest_writel(s, 0xff00012c, 0xff000000);
    qtest_writel(s, 0xff000004, 0xff000114);
    qtest_writel(s, 0xff000008, 0xff100014);
    qtest_writel(s, 0xff10002f, 0x000000ff);

    qtest_quit(s);
}

/*
 * This used to trigger a UAF in lsi_do_msgout()
 * https://gitlab.com/qemu-project/qemu/-/issues/972
 */
static void test_lsi_do_msgout_cancel_req(void)
{
    QTestState *s;

    if (sizeof(void *) == 4) {
        g_test_skip("memory size too big for 32-bit build");
        return;
    }

    s = qtest_init("-M q35 -m 2G -nodefaults "
                   "-device lsi53c895a,id=scsi "
                   "-device scsi-hd,drive=disk0 "
                   "-drive file=null-co://,id=disk0,if=none,format=raw");

    qtest_outl(s, 0xcf8, 0x80000810);
    qtest_outl(s, 0xcf8, 0xc000);
    qtest_outl(s, 0xcf8, 0x80000810);
    qtest_outw(s, 0xcfc, 0x7);
    qtest_outl(s, 0xcf8, 0x80000810);
    qtest_outl(s, 0xcfc, 0xc000);
    qtest_outl(s, 0xcf8, 0x80000804);
    qtest_outw(s, 0xcfc, 0x05);
    qtest_writeb(s, 0x69736c10, 0x08);
    qtest_writeb(s, 0x69736c13, 0x58);
    qtest_writeb(s, 0x69736c1a, 0x01);
    qtest_writeb(s, 0x69736c1b, 0x06);
    qtest_writeb(s, 0x69736c22, 0x01);
    qtest_writeb(s, 0x69736c23, 0x07);
    qtest_writeb(s, 0x69736c2b, 0x02);
    qtest_writeb(s, 0x69736c48, 0x08);
    qtest_writeb(s, 0x69736c4b, 0x58);
    qtest_writeb(s, 0x69736c52, 0x04);
    qtest_writeb(s, 0x69736c53, 0x06);
    qtest_writeb(s, 0x69736c5b, 0x02);
    qtest_outl(s, 0xc02d, 0x697300);
    qtest_writeb(s, 0x5a554662, 0x01);
    qtest_writeb(s, 0x5a554663, 0x07);
    qtest_writeb(s, 0x5a55466a, 0x10);
    qtest_writeb(s, 0x5a55466b, 0x22);
    qtest_writeb(s, 0x5a55466c, 0x5a);
    qtest_writeb(s, 0x5a55466d, 0x5a);
    qtest_writeb(s, 0x5a55466e, 0x34);
    qtest_writeb(s, 0x5a55466f, 0x5a);
    qtest_writeb(s, 0x5a345a5a, 0x77);
    qtest_writeb(s, 0x5a345a5b, 0x55);
    qtest_writeb(s, 0x5a345a5c, 0x51);
    qtest_writeb(s, 0x5a345a5d, 0x27);
    qtest_writeb(s, 0x27515577, 0x41);
    qtest_outl(s, 0xc02d, 0x5a5500);
    qtest_writeb(s, 0x364001d0, 0x08);
    qtest_writeb(s, 0x364001d3, 0x58);
    qtest_writeb(s, 0x364001da, 0x01);
    qtest_writeb(s, 0x364001db, 0x26);
    qtest_writeb(s, 0x364001dc, 0x0d);
    qtest_writeb(s, 0x364001dd, 0xae);
    qtest_writeb(s, 0x364001de, 0x41);
    qtest_writeb(s, 0x364001df, 0x5a);
    qtest_writeb(s, 0x5a41ae0d, 0xf8);
    qtest_writeb(s, 0x5a41ae0e, 0x36);
    qtest_writeb(s, 0x5a41ae0f, 0xd7);
    qtest_writeb(s, 0x5a41ae10, 0x36);
    qtest_writeb(s, 0x36d736f8, 0x0c);
    qtest_writeb(s, 0x36d736f9, 0x80);
    qtest_writeb(s, 0x36d736fa, 0x0d);
    qtest_outl(s, 0xc02d, 0x364000);

    qtest_quit(s);
}

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
    g_test_init(&argc, &argv, NULL);

    if (!qtest_has_device("lsi53c895a")) {
        return 0;
    }

    qtest_add_func("fuzz/lsi53c895a/lsi_do_dma_empty_queue",
                   test_lsi_do_dma_empty_queue);

    qtest_add_func("fuzz/lsi53c895a/lsi_do_msgout_cancel_req",
                   test_lsi_do_msgout_cancel_req);

    qtest_add_func("fuzz/lsi53c895a/lsi_dma_reentrancy",
                   test_lsi_dma_reentrancy);

    return g_test_run();
}
