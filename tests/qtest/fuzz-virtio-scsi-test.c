/*
 * QTest fuzzer-generated testcase for virtio-scsi device
 *
 * Copyright (c) 2020 Li Qiang <liq3ea@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libqtest.h"

/*
 * Here a MemoryRegionCache pointed to an MMIO region but had a
 * larger size than the underlying region.
 */
static void test_mmio_oob_from_memory_region_cache(void)
{
    QTestState *s;

    s = qtest_init("-M pc-q35-5.2 -m 512M "
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
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("fuzz/test_mmio_oob_from_memory_region_cache",
                   test_mmio_oob_from_memory_region_cache);

    return g_test_run();
}
