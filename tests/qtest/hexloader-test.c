/*
 * QTest testcase for the Intel Hexadecimal Object File Loader
 *
 * Authors:
 *  Su Hang <suhang16@mails.ucas.ac.cn> 2018
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "libqtest.h"

/* Load 'test.hex' and verify that the in-memory contents are as expected.
 * 'test.hex' is a memory test pattern stored in Hexadecimal Object
 * format.  It loads at 0x10000 in RAM and contains values from 0 through
 * 255.
 */
static void hex_loader_test(void)
{
    unsigned int i;
    const unsigned int base_addr = 0x00010000;

    QTestState *s = qtest_initf(
        "-M vexpress-a9 -device loader,file=tests/data/hex-loader/test.hex");

    for (i = 0; i < 256; ++i) {
        uint8_t val = qtest_readb(s, base_addr + i);
        g_assert_cmpuint(i, ==, val);
    }
    qtest_quit(s);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/tmp/hex_loader", hex_loader_test);
    return g_test_run();
}
