/*
 * QTest testcase for fuzz case
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

static void test_lp1878642_pci_bus_get_irq_level_assert(void)
{
    QTestState *s;

    s = qtest_init("-M pc-q35-5.0 "
                   "-nographic -monitor none -serial none "
                   "-d guest_errors -trace pci*");

    qtest_outl(s, 0xcf8, 0x8400f841);
    qtest_outl(s, 0xcfc, 0xebed205d);
    qtest_outl(s, 0x5d02, 0xebed205d);
    qtest_quit(s);
}

/*
 * Here a MemoryRegionCache pointed to an MMIO region but had a
 * larger size than the underlying region.
 */
static void test_mmio_oob_from_memory_region_cache(void)
{
    QTestState *s;

    s = qtest_init("-M pc-q35-5.2 -display none -m 512M "
		   "-device virtio-scsi,num_queues=8,addr=03.0 ");

    qtest_outl(s, 0xcf8, 0x80001811);
    qtest_outb(s, 0xcfc, 0x6e);
    qtest_outl(s, 0xcf8, 0x80001824);
    qtest_outl(s, 0xcf8, 0x80001813);
    qtest_outl(s, 0xcfc, 0xa080000);
    qtest_outl(s, 0xcf8, 0x80001802);
    qtest_outl(s, 0xcfc, 0x5a175a63);
    qtest_outb(s, 0x6e08, 0x9e);
    qtest_writeb(s, 0x9f003, 0xff);
    qtest_writeb(s, 0x9f004, 0x01);
    qtest_writeb(s, 0x9e012, 0x0e);
    qtest_writeb(s, 0x9e01b, 0x0e);
    qtest_writeb(s, 0x9f006, 0x01);
    qtest_writeb(s, 0x9f008, 0x01);
    qtest_writeb(s, 0x9f00a, 0x01);
    qtest_writeb(s, 0x9f00c, 0x01);
    qtest_writeb(s, 0x9f00e, 0x01);
    qtest_writeb(s, 0x9f010, 0x01);
    qtest_writeb(s, 0x9f012, 0x01);
    qtest_writeb(s, 0x9f014, 0x01);
    qtest_writeb(s, 0x9f016, 0x01);
    qtest_writeb(s, 0x9f018, 0x01);
    qtest_writeb(s, 0x9f01a, 0x01);
    qtest_writeb(s, 0x9f01c, 0x01);
    qtest_writeb(s, 0x9f01e, 0x01);
    qtest_writeb(s, 0x9f020, 0x01);
    qtest_writeb(s, 0x9f022, 0x01);
    qtest_writeb(s, 0x9f024, 0x01);
    qtest_writeb(s, 0x9f026, 0x01);
    qtest_writeb(s, 0x9f028, 0x01);
    qtest_writeb(s, 0x9f02a, 0x01);
    qtest_writeb(s, 0x9f02c, 0x01);
    qtest_writeb(s, 0x9f02e, 0x01);
    qtest_writeb(s, 0x9f030, 0x01);
    qtest_outb(s, 0x6e10, 0x00);
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        qtest_add_func("fuzz/test_lp1878263_megasas_zero_iov_cnt",
                       test_lp1878263_megasas_zero_iov_cnt);
        qtest_add_func("fuzz/test_lp1878642_pci_bus_get_irq_level_assert",
                       test_lp1878642_pci_bus_get_irq_level_assert);
        qtest_add_func("fuzz/test_mmio_oob_from_memory_region_cache",
                       test_mmio_oob_from_memory_region_cache);
    }

    return g_test_run();
}
